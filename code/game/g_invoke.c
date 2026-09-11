/*
===========================================================================
Copyright (C) 2026 Acadine Intelligence.

This file is part of Quake Invoker.

Quake Invoker is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake Invoker is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake Invoker; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
// g_invoke.c -- server side of the invoker loop
//
// Client commands:
//   orb <q|w|e>   push an orb into the player's slots
//   invoke        put the matching weapon or spell in the right hand
//   invswap       exchange the left and right hand, including an empty hand
//
// State lives in a game-owned per-client table (see below). The client
// learns the orb slots through the "orbs" server command so it can draw
// them; invoked weapons arrive through the STAT_WEAPONS / ammo snapshot
// path, invoked spells through STAT_INVOKE_SPELLS, and mana through
// STAT_INVOKE_MANA. Castable spells: Ghost Walk (invisibility for 5 s),
// Sunstrike (aimed strike after 1.75 s), EMP (charged burst after 2.5 s),
// Chaos Meteor, Tornado (travelling vortex), Deafening Blast (a shove
// that disarms weapon fire), Cold Snap (a chilling mark) and Ice Wall
// (a slowing field).

#include "g_local.h"
#include "bg_invoke.h"

// Per-client invoker state, owned by the game module. Keeping this state
// separate avoids changing the shared client layout for invocation features.
// Shared struct edits require every dependent QVM source to be rebuilt.
// EMP tuning: the burst is scheduled when the cast lands, announced to
// every client, and fires EMP_CHARGE_MS later.
#define EMP_CHARGE_MS	2500
#define EMP_RADIUS		600
#define EMP_DAMAGE		70
// A held fire button with an empty mana pool must not spam the notice.
#define INVOKE_NO_MANA_NOTICE_MS	500
// Tornado: a vortex that lives TORNADO_LIFE_MS and lifts everyone inside
// TORNADO_RADIUS along its path.
#define TORNADO_LIFE_MS		1500
#define TORNADO_RADIUS		170
#define TORNADO_LIFT		210
// Deafening Blast: a pressure wave that bursts after DEAFEN_LIFE_MS of
// flight at most and shoves plus disarms everyone in DEAFEN_RADIUS.
#define DEAFEN_LIFE_MS		900
#define DEAFEN_RADIUS		350
#define DEAFEN_DAMAGE		50
#define DEAFEN_PUSH			260
#define DEAFEN_DISARM_MS	3000
// Cold Snap: an aimed debuff. A qualifying hit on the chilled target
// freezes them briefly and adds damage, at most once per interval. The
// debuff rules themselves live in bg_invoke.c so host tests share them.
#define COLD_SNAP_RANGE		1000
#define COLD_SNAP_DAMAGE	15
// Ice Wall: a placed field. Everyone inside, except the caster, is slowed
// while they stand in it and takes periodic chip damage. The slow rules
// themselves live in bg_invoke.c so host tests share them.
#define ICE_WALL_RANGE		600
#define ICE_WALL_RADIUS		170
#define ICE_WALL_LIFE_MS	6000
#define ICE_WALL_TICK_MS	100
#define ICE_WALL_DAMAGE_INTERVAL_MS	500
#define ICE_WALL_DAMAGE		8
#define ICE_WALL_SLOW_NOTICE_MS	600
#define ICE_WALL_MAX_FIELDS	8

// Forge Spirit: a damageable companion that hovers near its caster and
// lobs fire bolts that shred armor. One caster keeps at most
// FORGE_SPIRIT_MAX alive; a summon at the cap dismisses the oldest. The
// spirit dies with its caster, when its health runs out, or after
// FORGE_SPIRIT_LIFE_MS. The bolt's damage lands through
// G_InvokeSpiritBoltImpact; its shred rule lives in bg_invoke.c. The
// SPIRIT_BOLT_* values are shared with g_missile.c via g_local.h.
#define FORGE_SPIRIT_LIFE_MS	30000
#define FORGE_SPIRIT_HEALTH		60
#define FORGE_SPIRIT_MAX		2
#define FORGE_SPIRIT_BBOX		10
#define FORGE_SPIRIT_SPEED		280
#define FORGE_SPIRIT_HOVER		30
#define FORGE_SPIRIT_LEAD		70
#define FORGE_SPIRIT_SIDE		22
#define FORGE_SPIRIT_STANDOFF	14
#define FORGE_SPIRIT_SIGHT		600
#define FORGE_SPIRIT_FIRE_MS	1300
#define FORGE_SPIRIT_THINK_MS	50
#define PORTAL_FACE_CLEARANCE	4		// portal plane sits this far off the surface
#define PORTAL_NOTICE_MS		1500	// throttle for placement refusal notices
#define PORTAL_THINK_MS			1000

typedef struct {
	int		orbSlots[INVOKE_SLOTS];	// orbType_t values, oldest first
	invokeHands_t hands;
	int		lastManaTime;			// level.time of the last regen step
	int		ghostWalkUntil;			// level.time the cast invisibility ends
	int		sunstrikeTime;			// level.time the aimed strike lands (0 = none)
	vec3_t	sunstrikeOrigin;
	int		empTime;				// level.time the EMP burst lands (0 = none)
	vec3_t	empOrigin;
	int		lastNoManaCp;			// level.time of the last mana notice
	int		disarmedUntil;			// weapon fire stays silent until this time
	chillState_t	chill;			// Cold Snap debuff on this client
	slowState_t		slow;			// Ice Wall slow on this client
	shredState_t	shred;			// Forge Spirit armor shred on this client
	int		portalNextSlot;		// 0/1: which end the next portal shot replaces
	int		lastPortalCp;		// level.time of the last placement notice
	int		portalTravelUntil;	// re-entry guard after a traversal
} invokeState_t;

static invokeState_t	g_invoke[MAX_CLIENTS];

static invokeState_t *G_InvokeState( gentity_t *ent ) {
	return &g_invoke[ent - g_entities];
}

/*
==============
G_InvokeSendOrbs

Tell the owning client what it holds. Format: orbs <s0> <s1> <s2>
==============
*/
static void G_InvokeSendOrbs( gentity_t *ent ) {
	invokeState_t	*st = G_InvokeState( ent );

	trap_SendServerCommand( ent - g_entities, va( "orbs %i %i %i",
		st->orbSlots[0], st->orbSlots[1], st->orbSlots[2] ) );
}

static void G_InvokeSendHands( gentity_t *ent ) {
	invokeState_t *st;
	int clientNum;

	clientNum = ent - g_entities;
	if ( clientNum < 0 || clientNum >= level.maxclients ) {
		return;
	}
	st = G_InvokeState( ent );
	ent->client->ps.stats[STAT_INVOKE_HANDS] = 256 | BG_InvokePackedHands( &st->hands );
	ent->client->ps.stats[STAT_INVOKE_SPELLS] = 256 | BG_InvokePackedSpells( &st->hands );
	ent->client->ps.stats[STAT_INVOKE_MANA] = (int)st->hands.mana;
	trap_SendServerCommand( -1, va( "invhands %i %i %i %i %i", clientNum,
		BG_InvokeHandWeapon( &st->hands, INVOKE_HAND_LEFT ),
		BG_InvokeHandWeapon( &st->hands, INVOKE_HAND_RIGHT ),
		BG_InvokeHandSpell( &st->hands, INVOKE_HAND_LEFT ),
		BG_InvokeHandSpell( &st->hands, INVOKE_HAND_RIGHT ) ) );
}

/*
==============
G_InvokeBroadcastHands

Resend every connected player's hands to all clients. Without this, a
client that spawns or joins after the last hand change never learns the
hands other players are holding.
==============
*/
static void G_InvokeBroadcastHands( void ) {
	int i;

	for ( i = 0; i < level.maxclients; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].client
			&& g_entities[i].client->pers.connected == CON_CONNECTED ) {
			G_InvokeSendHands( &g_entities[i] );
		}
	}
}

/*
==============
G_InvokeDropGrantedWeapons

Remove the weapons the invoke hands granted. Called when a life ends: a
fresh life must not keep last life's invocations. Weapons granted by
other systems, including the starting machinegun and gauntlet, stay.
==============
*/
static void G_InvokeDropGrantedWeapons( gentity_t *ent, invokeState_t *st ) {
	playerState_t	*ps = &ent->client->ps;
	unsigned int	bits = st->hands.grantedWeapons, bit;
	int		w;

	for ( w = WP_NONE + 1; w < WP_NUM_WEAPONS; w++ ) {
		bit = 1u << w;
		if ( !( bits & bit ) || w == WP_MACHINEGUN || w == WP_GAUNTLET ) {
			continue;
		}
		ps->stats[STAT_WEAPONS] &= ~(int)bit;
		ps->ammo[w] = 0;
		if ( ps->weapon == w ) {
			ps->weapon = ( ps->stats[STAT_WEAPONS] & ( 1 << WP_MACHINEGUN ) )
				? WP_MACHINEGUN : WP_GAUNTLET;
		}
	}
}

/*
==============
G_InvokeCancelPendingEmp

A charge that will never land (the caster died or left the game) must be
taken back from every client that is drawing its ring. The cancel names
the caster so a client showing someone else's charge keeps that tell.
==============
*/
void G_InvokeCancelPendingEmp( gentity_t *ent ) {
	invokeState_t	*st = G_InvokeState( ent );

	if ( !st->empTime ) {
		return;
	}
	st->empTime = 0;
	trap_SendServerCommand( -1, va( "invempcancel %i\n", (int)( ent - g_entities ) ) );
}

/*
==============
G_InvokeReset

Called from ClientSpawn and PlayerDie so a fresh life starts with empty
slots and without the previous life's invoked weapons.
==============
*/
void G_InvokeReset( gentity_t *ent ) {
	invokeState_t	*st = G_InvokeState( ent );
	int		i;

	G_InvokeDropGrantedWeapons( ent, st );
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		st->orbSlots[i] = ORB_NONE;
	}
	BG_InvokeHandsReset( &st->hands );
	st->hands.mana = INVOKE_MANA_MAX;
	st->lastManaTime = level.time;
	st->ghostWalkUntil = 0;
	st->sunstrikeTime = 0;
	VectorClear( st->sunstrikeOrigin );
	BG_InvokeChillClear( &st->chill );
	BG_InvokeSlowClear( &st->slow );
	BG_InvokeShredClear( &st->shred );
	// a dead caster's pending EMP dies with the life, like Sunstrike:
	// otherwise the burst lands on the respawned player's behalf
	G_InvokeCancelPendingEmp( ent );
	VectorClear( st->empOrigin );
	st->lastNoManaCp = 0;
	st->disarmedUntil = 0;
	// a respawn takes its portal pair with it and clears the travel guard
	G_InvokeDismissPortals( ent );
	st->portalNextSlot = 0;
	st->lastPortalCp = 0;
	st->portalTravelUntil = 0;
	// a fresh life starts without Alacrity's haste window
	ent->client->ps.powerups[PW_HASTE] = 0;
	ent->client->ps.stats[STAT_INVOKE_MANA] = INVOKE_MANA_MAX;
	G_InvokeSendOrbs( ent );
	// broadcast, not a single send: a spawning or joining client also
	// needs every other player's hands
	G_InvokeBroadcastHands();
}

/*
==============
G_InvokeFindPortalEnd

The owner's portal end for a slot (0 = A, 1 = B), or NULL.
==============
*/
static gentity_t *G_InvokeFindPortalEnd( gentity_t *owner, int slot ) {
	int i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];

		if ( !e->inuse || e->parent != owner || !e->classname ) {
			continue;
		}
		if ( !strcmp( e->classname, "invoke_portal" ) && e->s.frame == slot ) {
			return e;
		}
	}
	return NULL;
}

/*
==============
G_InvokeFreePortalEnd

Severs the link, then frees the end. The partner survives as a lone
portal: visible, but moving nobody until a new shot connects it.
==============
*/
static void G_InvokeFreePortalEnd( gentity_t *self ) {
	if ( self->enemy && self->enemy->inuse ) {
		self->enemy->enemy = NULL;
	}
	G_FreeEntity( self );
}

/*
==============
G_InvokePortalNotice

Throttled refusal notice so a held fire button cannot spam centerprint.
==============
*/
static void G_InvokePortalNotice( gentity_t *ent, invokeState_t *st, const char *msg ) {
	if ( level.time - st->lastPortalCp < PORTAL_NOTICE_MS ) {
		return;
	}
	st->lastPortalCp = level.time;
	trap_SendServerCommand( ent - g_entities, va( "cp \"%s\n\"", msg ) );
	trap_SendServerCommand( ent - g_entities, va( "print \"portal refused: %s\n\"", msg ) );
}

/*
==============
G_InvokePortalAim

Anchors the aim ray on a static world surface, away from sky and movers,
with room for the player hull at the face and distance from the end that
stays. place and normal are optional outputs.
==============
*/
static qboolean G_InvokePortalAim( gentity_t *ent, vec3_t place, vec3_t normal ) {
	static vec3_t hullMins = { -15, -15, -24 };
	static vec3_t hullMaxs = { 15, 15, 32 };
	trace_t tr, fit;
	vec3_t start, end, forward, anchor, exit;
	gentity_t *other;
	invokeState_t *st = G_InvokeState( ent );

	VectorCopy( ent->client->ps.origin, start );
	start[2] += ent->client->ps.viewheight;
	AngleVectors( ent->client->ps.viewangles, forward, NULL, NULL );
	VectorMA( start, PORTAL_RANGE, forward, end );
	trap_Trace( &tr, start, NULL, NULL, end, ent->s.number, MASK_SOLID );
	if ( tr.fraction >= 1.0f ) {
		G_InvokePortalNotice( ent, st, "no surface for a portal" );
		return qfalse;
	}
	if ( tr.entityNum != ENTITYNUM_WORLD ) {
		G_InvokePortalNotice( ent, st, "portals need a static surface" );
		return qfalse;
	}
	if ( tr.surfaceFlags & SURF_SKY ) {
		G_InvokePortalNotice( ent, st, "cannot anchor a portal in the sky" );
		return qfalse;
	}
	VectorMA( tr.endpos, PORTAL_FACE_CLEARANCE, tr.plane.normal, anchor );
	// travellers exit a full offset off the surface, so the hull must fit
	// there; testing at the face itself would always clip the surface
	VectorMA( tr.endpos, PORTAL_EXIT_OFFSET, tr.plane.normal, exit );
	trap_Trace( &fit, exit, hullMins, hullMaxs, exit, ent->s.number, MASK_PLAYERSOLID );
	if ( fit.startsolid ) {
		G_InvokePortalNotice( ent, st, "no room to stand at that portal" );
		return qfalse;
	}
	// the end that stays put must not sit on top of the new one
	other = G_InvokeFindPortalEnd( ent, !st->portalNextSlot );
	if ( other && BG_InvokePortalTooClose( anchor, other->r.currentOrigin ) ) {
		vec3_t delta;
		VectorSubtract( anchor, other->r.currentOrigin, delta );
		G_InvokePortalNotice( ent, st, va( "too close to the other portal (%.0f units apart)",
			(float)sqrt( VectorLengthSquared( delta ) ) ) );
		return qfalse;
	}
	if ( place ) {
		VectorCopy( anchor, place );
	}
	if ( normal ) {
		VectorCopy( tr.plane.normal, normal );
	}
	return qtrue;
}

/*
==============
G_InvokePortalThink

Owns one end's lifetime: the pair follows its owner, and a freed partner
leaves this end inert instead of stale.
==============
*/
void G_InvokePortalThink( gentity_t *self ) {
	gentity_t *owner = self->parent;

	if ( !owner || !owner->inuse || !owner->client ) {
		G_InvokeFreePortalEnd( self );
		return;
	}
	if ( self->enemy && !self->enemy->inuse ) {
		self->enemy = NULL;
	}
	self->nextthink = level.time + PORTAL_THINK_MS;
}

/*
==============
G_InvokePortalTouch

Anyone who reaches a connected face travels to its partner: exit clear of
the partner's surface, entry speed preserved along its facing, and a
per-client cooldown so the exit cannot fall straight back in.
==============
*/
void G_InvokePortalTouch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	static vec3_t hullMins = { -15, -15, -24 };
	static vec3_t hullMaxs = { 15, 15, 32 };
	vec3_t noAngles = { 9999999.0f, 0, 0 };
	gentity_t *dest = self->enemy;
	invokeState_t *st;
	trace_t fit;
	vec3_t exit, out;

	(void)trace;
	if ( !other->client || !dest || !dest->inuse ) {
		return;
	}
	if ( other->client->sess.sessionTeam == TEAM_SPECTATOR || other->health <= 0 ) {
		return;
	}
	st = G_InvokeState( other );
	if ( level.time < st->portalTravelUntil ) {
		return;
	}
	VectorMA( dest->r.currentOrigin, PORTAL_EXIT_OFFSET, dest->movedir, exit );
	trap_Trace( &fit, exit, hullMins, hullMaxs, exit, other->s.number, MASK_PLAYERSOLID );
	if ( fit.startsolid ) {
		// an unsafe exit refuses travel instead of embedding the player
		return;
	}
	// preserve the entry speed, aimed out of the far face
	BG_InvokePortalExitVelocity( other->client->ps.velocity, dest->movedir, out );
	st->portalTravelUntil = level.time + PORTAL_TRAVEL_COOLDOWN_MS;
	// no-angles mode: keep the view, then set the exit velocity ourselves
	TeleportPlayer( other, exit, noAngles );
	VectorCopy( out, other->client->ps.velocity );
	trap_SendServerCommand( other - g_entities, "print \"portal travel\n\"" );
}

/*
==============
G_InvokeDismissPortals

Removes every portal end this client owns. Death, respawn, disconnect and
map restarts all end up here.
==============
*/
void G_InvokeDismissPortals( gentity_t *ent ) {
	int i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];

		if ( e->inuse && e->parent == ent && e->classname
			&& !strcmp( e->classname, "invoke_portal" ) ) {
			G_InvokeFreePortalEnd( e );
		}
	}
}

/*
==============
G_InvokeDismissFields

Frees the placed fields this client owns. Disconnect only: a field
outlives its caster's death by design, but a freed client slot must not
leave a field exempting or blaming whichever player reuses the slot.
==============
*/
void G_InvokeDismissFields( gentity_t *ent ) {
	int i;

	for ( i = 0; i < level.num_entities; i++ ) {
		gentity_t *e = &g_entities[i];

		if ( e->inuse && e->parent == ent && e->classname
			&& !strcmp( e->classname, "invoke_icewall" ) ) {
			G_FreeEntity( e );
		}
	}
}

/*
==============
G_InvokePlacePortal

The QEW cast: records the next end at the aim anchor, alternates which
end the following shot replaces, and links the pair when both exist.
==============
*/
static void G_InvokePlacePortal( gentity_t *ent, invokeState_t *st ) {
	vec3_t place, normal, angles;
	gentity_t *old, *other, *end;
	int slot;

	if ( !G_InvokePortalAim( ent, place, normal ) ) {
		// the fire path vetted this shot earlier in the same frame; a
		// failure here means the view moved mid-tick
		return;
	}
	slot = st->portalNextSlot;
	old = G_InvokeFindPortalEnd( ent, slot );
	if ( old ) {
		G_InvokeFreePortalEnd( old );
	}
	end = G_Spawn();
	end->classname = "invoke_portal";
	end->parent = ent;
	end->s.frame = slot;
	end->s.eType = ET_GENERAL;
	end->s.generic1 = INVOKE_FX_PORTAL;
	VectorCopy( normal, end->movedir );
	G_SetOrigin( end, place );
	vectoangles( normal, angles );
	VectorCopy( angles, end->s.apos.trBase );
	end->s.apos.trType = TR_STATIONARY;
	// a thin trigger shell: travellers that reach the face step through
	VectorSet( end->r.mins, -16, -16, -16 );
	VectorSet( end->r.maxs, 16, 16, 16 );
	end->r.contents = CONTENTS_TRIGGER;
	end->touch = G_InvokePortalTouch;
	end->think = G_InvokePortalThink;
	end->nextthink = level.time + PORTAL_THINK_MS;
	trap_LinkEntity( end );

	other = G_InvokeFindPortalEnd( ent, !slot );
	if ( other ) {
		end->enemy = other;
		other->enemy = end;
	}
	st->portalNextSlot = !slot;
	trap_SendServerCommand( ent - g_entities, va( "print \"placed portal %s (%i %i %i)\n\"",
		slot ? "B" : "A", (int)place[0], (int)place[1], (int)place[2] ) );
	if ( other ) {
		trap_SendServerCommand( ent - g_entities, "print \"portal pair connected\n\"" );
	}
}

/*
==============
Cmd_Orb_f
==============
*/
void Cmd_Orb_f( gentity_t *ent ) {
	invokeState_t	*st = G_InvokeState( ent );
	char		arg[MAX_TOKEN_CHARS];
	orbType_t	orb;

	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0 ) {
		return;
	}
	if ( trap_Argc() != 2 ) {
		trap_SendServerCommand( ent - g_entities, "print \"usage: orb <q|w|e>\n\"" );
		return;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	orb = BG_OrbFromString( arg );
	if ( orb == ORB_NONE ) {
		trap_SendServerCommand( ent - g_entities, "print \"usage: orb <q|w|e>\n\"" );
		return;
	}
	BG_PushOrb( st->orbSlots, orb );
	G_InvokeSendOrbs( ent );
	trap_SendServerCommand( ent - g_entities, va( "print \"orbs: %c %c %c\n\"",
		BG_OrbLetter( st->orbSlots[0] ), BG_OrbLetter( st->orbSlots[1] ),
		BG_OrbLetter( st->orbSlots[2] ) ) );
}

/*
==============
G_InvokeSpellCastable

What can be cast today. Everything else reports "not castable yet" instead
of granting something wrong.
==============
*/
static qboolean G_InvokeSpellCastable( int spell ) {
	switch ( spell ) {
	case SPELL_COLD_SNAP:
	case SPELL_ICE_WALL:
	case SPELL_ALACRITY:
	case SPELL_FORGE_SPIRIT:
	case SPELL_GHOST_WALK:
	case SPELL_SUNSTRIKE:
	case SPELL_EMP:
	case SPELL_CHAOS_METEOR:
	case SPELL_TORNADO:
	case SPELL_DEAFENING_BLAST:
	case SPELL_PORTAL:
		return qtrue;
	default:
		return qfalse;
	}
}

/*
==============
G_InvokeEnsureHeldSelection

Replacing a hand can release the weapon the player selected through the
classic weapon menu; fall back to a starting weapon.
==============
*/
static void G_InvokeEnsureHeldSelection( gentity_t *ent ) {
	if ( ent->client->ps.weapon > WP_NONE
		&& !( ent->client->ps.stats[STAT_WEAPONS] & ( 1 << ent->client->ps.weapon ) ) ) {
		ent->client->ps.weapon = ( ent->client->ps.stats[STAT_WEAPONS] & ( 1 << WP_MACHINEGUN ) )
			? WP_MACHINEGUN : WP_GAUNTLET;
	}
}

/*
==============
Cmd_Invoke_f
==============
*/
void Cmd_Invoke_f( gentity_t *ent ) {
	gclient_t			*client = ent->client;
	invokeState_t		*st = G_InvokeState( ent );
	const invocation_t	*inv;

	if ( client->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0 ) {
		return;
	}
	if ( trap_Argc() != 1 ) {
		trap_SendServerCommand( ent - g_entities, "print \"usage: invoke\n\"" );
		return;
	}
	inv = BG_FindInvocation( st->orbSlots );
	if ( !inv ) {
		trap_SendServerCommand( ent - g_entities, "cp \"Pick three orbs with D/W/A first\n\"" );
		return;
	}
	if ( inv->kind == INVOKE_KIND_SPELL || inv->kind == INVOKE_KIND_PORTAL ) {
		if ( !G_InvokeSpellCastable( inv->spell ) ) {
			trap_SendServerCommand( ent - g_entities, va( "cp \"%s: not castable yet\n\"", inv->name ) );
			trap_SendServerCommand( ent - g_entities, va( "print \"%s: not castable yet\n\"", inv->name ) );
			return;
		}
		if ( !BG_InvokeEquipSpell( &st->hands, INVOKE_HAND_RIGHT, inv->spell,
			client->ps.ammo, &client->ps.stats[STAT_WEAPONS] ) ) {
			return;
		}
		G_InvokeEnsureHeldSelection( ent );
		G_InvokeSendHands( ent );
		trap_SendServerCommand( ent - g_entities, va( "cp \"%s\n\"", inv->name ) );
		trap_SendServerCommand( ent - g_entities, va( "invoked %i %i %i",
			INVOKE_HAND_RIGHT, WP_NONE, inv->spell ) );
		trap_SendServerCommand( ent - g_entities, va( "print \"invoked %s (%s)\n\"", inv->name, inv->combo ) );
		return;
	}
	if ( inv->kind != INVOKE_KIND_WEAPON ) {
		// Spells and the portal are not castable yet; report that instead of
		// silently granting nothing.
		trap_SendServerCommand( ent - g_entities, va( "cp \"%s: not castable yet\n\"", inv->name ) );
		trap_SendServerCommand( ent - g_entities, va( "print \"%s: not castable yet\n\"", inv->name ) );
		return;
	}

	if ( !BG_InvokeEquipHand( &st->hands, INVOKE_HAND_RIGHT, inv->weapon,
		inv->ammo, client->ps.ammo, &client->ps.stats[STAT_WEAPONS] ) ) {
		return;
	}
	G_InvokeEnsureHeldSelection( ent );
	G_InvokeSendHands( ent );
	trap_SendServerCommand( ent - g_entities, va( "cp \"%s\n\"", inv->name ) );
	trap_SendServerCommand( ent - g_entities, va( "invoked %i %i %i",
		INVOKE_HAND_RIGHT, inv->weapon, SPELL_NONE ) );
	trap_SendServerCommand( ent - g_entities, va( "print \"invoked %s (%s)\n\"", inv->name, inv->combo ) );
}

void Cmd_InvokeSwap_f( gentity_t *ent ) {
	invokeState_t *st;

	if ( ent->client->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0 ) {
		return;
	}
	if ( trap_Argc() != 1 ) {
		trap_SendServerCommand( ent - g_entities, "print \"usage: invswap\n\"" );
		return;
	}
	st = G_InvokeState( ent );
	BG_InvokeSwapHands( &st->hands );
	G_InvokeSendHands( ent );
}

static void G_InvokePlaceIceWall( gentity_t *ent, vec3_t aimPoint );
static void G_InvokeSummonForgeSpirit( gentity_t *ent );

/*
==============
G_InvokeCastSpell

Runs a successful cast. Ghost Walk turns the caster invisible for 5 s;
Sunstrike aims now and lands 1.75 s later, so the strike is dodgeable;
Ice Wall places a slow field on the ground ahead.
==============
*/
static void G_InvokeCastSpell( gentity_t *ent, invokeState_t *st, int hand,
	int spell ) {
	playerState_t		*ps = &ent->client->ps;
	const spellDef_t	*def = BG_SpellDef( spell );
	vec3_t			start, end, forward;
	trace_t			tr;

	if ( !def ) {
		return;
	}
	switch ( spell ) {
	case SPELL_COLD_SNAP:
	{
		gentity_t *targ;

		VectorCopy( ps->origin, start );
		start[2] += ps->viewheight;
		AngleVectors( ps->viewangles, forward, NULL, NULL );
		VectorMA( start, COLD_SNAP_RANGE, forward, end );
		// players block this ray: the snap needs a target in view
		trap_Trace( &tr, start, NULL, NULL, end, ent->s.number, MASK_SHOT );
		if ( tr.entityNum < level.maxclients ) {
			targ = &g_entities[tr.entityNum];
			if ( targ->client && targ->client->sess.sessionTeam != TEAM_SPECTATOR
				&& targ->health > 0 ) {
				BG_InvokeChillApply( &G_InvokeState( targ )->chill, level.time );
				trap_SendServerCommand( targ - g_entities,
					va( "invchill %i\n", COLD_SNAP_DEBUFF_MS ) );
				trap_SendServerCommand( ent - g_entities,
					va( "print \"cold snap on %s\\n\"", targ->client->pers.netname ) );
			}
		}
		break;
	}
	case SPELL_ICE_WALL:
	{
		vec3_t	place;

		VectorCopy( ps->origin, start );
		start[2] += ps->viewheight;
		AngleVectors( ps->viewangles, forward, NULL, NULL );
		VectorMA( start, ICE_WALL_RANGE, forward, end );
		// world geometry only, like Sunstrike: the field lands on ground
		// ahead even when an enemy stands in the way
		trap_Trace( &tr, start, NULL, NULL, end, ent->s.number, CONTENTS_SOLID );
		VectorCopy( tr.endpos, place );
		if ( tr.fraction < 1.0f ) {
			// keep the ring's center clear of the wall the ray hit, so
			// the field cannot sit half inside geometry
			vec3_t	hitOffset;

			VectorSubtract( tr.endpos, start, hitOffset );
			if ( VectorLengthSquared( hitOffset ) > 64 * 64 ) {
				VectorMA( place, -64, forward, place );
			}
		}
		G_InvokePlaceIceWall( ent, place );
		break;
	}
	case SPELL_GHOST_WALK:
		st->ghostWalkUntil = level.time + 5000;
		// only extend: a longer invisibility from an item must survive the cast
		if ( ps->powerups[PW_INVIS] < st->ghostWalkUntil ) {
			ps->powerups[PW_INVIS] = st->ghostWalkUntil;
		}
		break;
	case SPELL_ALACRITY:
		// Alacrity rides the engine's haste slot: speed, fire interval
		// and hand cooldowns already read PW_HASTE. The extend helper
		// refreshes a live window without stacking or shortening it.
		BG_InvokeHasteExtend( &ps->powerups[PW_HASTE], level.time );
		break;
	case SPELL_FORGE_SPIRIT:
		G_InvokeSummonForgeSpirit( ent );
		break;
	case SPELL_SUNSTRIKE:
		VectorCopy( ps->origin, start );
		start[2] += ps->viewheight;
		AngleVectors( ps->viewangles, forward, NULL, NULL );
		VectorMA( start, 8192, forward, end );
		// world geometry only: the aim ray passes through players, so the
		// strike can be placed on ground behind them
		trap_Trace( &tr, start, NULL, NULL, end, ent->s.number, CONTENTS_SOLID );
		VectorCopy( tr.endpos, st->sunstrikeOrigin );
		st->sunstrikeTime = level.time + 1750;
		break;
	case SPELL_EMP:
		VectorCopy( ps->origin, st->empOrigin );
		st->empTime = level.time + EMP_CHARGE_MS;
		// the charge ring draws on every client from this until the burst:
		// it is the burst's only warning. Coordinates go as integers, float
		// varargs are not safe through this VM's print/format path. The
		// duration is what remains of the charge, so the ring and the burst
		// stay in step even with command latency.
		trap_SendServerCommand( -1, va( "invemp %i %i %i %i %i\n",
			(int)(ent - g_entities),
			(int)st->empOrigin[0], (int)st->empOrigin[1], (int)st->empOrigin[2],
			st->empTime - level.time ) );
		break;
	case SPELL_CHAOS_METEOR:
		VectorCopy( ps->origin, start );
		start[2] += ps->viewheight;
		AngleVectors( ps->viewangles, forward, NULL, NULL );
		fire_invoke_meteor( ent, start, forward );
		break;
	case SPELL_TORNADO:
	{
		vec3_t flatAngles;

		// the vortex travels level along the view yaw: looking down must
		// not drive it into the floor
		flatAngles[0] = 0;
		flatAngles[1] = ps->viewangles[1];
		flatAngles[2] = 0;
		AngleVectors( flatAngles, forward, NULL, NULL );
		VectorCopy( ps->origin, start );
		start[2] += ps->viewheight;
		fire_invoke_tornado( ent, start, forward );
		break;
	}
	case SPELL_DEAFENING_BLAST:
		VectorCopy( ps->origin, start );
		start[2] += ps->viewheight;
		AngleVectors( ps->viewangles, forward, NULL, NULL );
		fire_invoke_blast( ent, start, forward );
		break;
	case SPELL_PORTAL:
		G_InvokePlacePortal( ent, st );
		break;
	default:
		// reachable only if a spell joins G_InvokeSpellCastable without an
		// implementation here: stay loud instead of quietly eating mana
		G_Printf( "unhandled invoke spell %i\n", spell );
		return;
	}
	trap_SendServerCommand( ent - g_entities, va( "cp \"%s\n\"", def->name ) );
	trap_SendServerCommand( ent - g_entities, va( "print \"cast %s (-%i mana)\n\"",
		def->name, def->cost ) );
	// the client draws the recharge bar from this: only a successful cast
	// arrives here, so the readout never starts on a rejected attempt
	trap_SendServerCommand( ent - g_entities, va( "invcast %i %i\n", hand, spell ) );
}

/*
==============
G_InvokeDamageTaken

Called from G_Damage for every surviving client that took real damage.
A chilled target freezes and takes the trigger damage at most once per
COLD_SNAP_TRIGGER_MS; Cold Snap's own damage never re-triggers it.
==============
*/
void G_InvokeDamageTaken( gentity_t *targ, gentity_t *attacker, vec3_t dir, int mod ) {
	invokeState_t	*st = G_InvokeState( targ );

	if ( !BG_InvokeChillCanTrigger( &st->chill, level.time,
		( mod == MOD_COLD_SNAP ) ? qtrue : qfalse ) ) {
		return;
	}
	// arm the interval and the freeze before the bonus damage lands: the
	// bonus can then never re-enter this trigger, however it bounces
	BG_InvokeChillTriggered( &st->chill, level.time );
	// brief freeze: a hard stop now, and locked movement while it lasts
	VectorClear( targ->client->ps.velocity );
	G_Damage( targ, attacker, attacker, dir, targ->client->ps.origin,
		COLD_SNAP_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_COLD_SNAP );
}

/*
==============
G_InvokeClientFrozen

True while the Cold Snap freeze holds this client. g_active.c consults
this where it sets movement speed each frame, so the freeze covers the
same prediction values the client sees.
==============
*/
qboolean G_InvokeClientFrozen( gentity_t *ent ) {
	if ( !ent || !ent->client ) {
		return qfalse;
	}
	return G_InvokeState( ent )->chill.freezeUntil > level.time;
}

/*
==============
G_InvokeClientSlowed

True while an Ice Wall field holds this client. Like the freeze, this is
consulted where g_active.c sets movement speed each frame, so the slow
covers the same prediction values the client sees.
==============
*/
qboolean G_InvokeClientSlowed( gentity_t *ent ) {
	if ( !ent || !ent->client ) {
		return qfalse;
	}
	return BG_InvokeSlowActive( &G_InvokeState( ent )->slow, level.time );
}

/*
==============
G_InvokeStrike

Applies a landed Sunstrike: a sky-to-ground beam event and radius damage.
The caster is immune to the strike.
==============
*/
static void G_InvokeStrike( gentity_t *ent, vec3_t origin ) {
	gentity_t	*te, *targ;
	vec3_t		dir, diff, start, up = { 0.0f, 0.0f, 1.0f };
	trace_t		tr;
	int			i;

	te = G_TempEntity( origin, EV_SUNSTRIKE );
	VectorMA( origin, 768, up, te->s.origin2 );
	te->s.eventParm = DirToByte( up );
	te->s.weapon = WP_ROCKET_LAUNCHER;

	for ( i = 0; i < level.maxclients; i++ ) {
		targ = &g_entities[i];
		if ( !targ->inuse || !targ->client || targ == ent || targ->health <= 0
			|| targ->client->pers.connected != CON_CONNECTED
			|| targ->client->ps.pm_type == PM_SPECTATOR ) {
			continue;
		}
		VectorSubtract( targ->client->ps.origin, origin, diff );
		if ( VectorLengthSquared( diff ) > 200.0f * 200.0f ) {
			continue;
		}
		// a wall between the strike point and the target shields it
		VectorCopy( origin, start );
		start[2] += 2;
		trap_Trace( &tr, start, NULL, NULL, targ->client->ps.origin, ent->s.number, CONTENTS_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		VectorCopy( diff, dir );
		VectorNormalize( dir );
		// the design rule for Sunstrike: its damage bypasses armor
		G_Damage( targ, ent, ent, dir, targ->client->ps.origin, 90, DAMAGE_NO_ARMOR, MOD_SUNSTRIKE );
	}
}

/*
==============
G_InvokeEmp

A 600-unit burst around the charge point: 70 damage and a shove away from
the center. The caster is immune, like Sunstrike.
==============
*/
static void G_InvokeEmp( gentity_t *ent, vec3_t origin ) {
	gentity_t	*te, *targ;
	vec3_t		dir, diff, up = { 0.0f, 0.0f, 1.0f };
	int			i;

	te = G_TempEntity( origin, EV_EMP );
	te->s.weapon = WP_ROCKET_LAUNCHER;
	te->s.eventParm = DirToByte( up );

	for ( i = 0; i < level.maxclients; i++ ) {
		targ = &g_entities[i];
		if ( !targ->inuse || !targ->client || targ == ent || targ->health <= 0
			|| targ->client->pers.connected != CON_CONNECTED
			|| targ->client->ps.pm_type == PM_SPECTATOR ) {
			continue;
		}
		VectorSubtract( targ->client->ps.origin, origin, diff );
		if ( VectorLengthSquared( diff ) > (float)EMP_RADIUS * EMP_RADIUS ) {
			continue;
		}
		VectorCopy( diff, dir );
		VectorNormalize( dir );
		VectorMA( targ->client->ps.velocity, 320, dir, targ->client->ps.velocity );
		targ->client->ps.velocity[2] += 140;
		// the manual shove above is the whole displacement: no second knockback
		G_Damage( targ, ent, ent, dir, targ->client->ps.origin, EMP_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_EMP );
	}
}

/*
==============
G_InvokeDeafenBurst

The pressure wave: a shove away from the burst point, damage, and a short
weapon disarm on everyone caught, except the caster. Spell casting is not
disarmed: only weapon fire is locked out.
==============
*/
static void G_InvokeDeafenBurst( gentity_t *ent, vec3_t origin ) {
	gentity_t	*targ;
	vec3_t		dir, diff;
	int			i;

	if ( ent && ent->client ) {
		trap_SendServerCommand( ent - g_entities, "print \"deafening blast burst\\n\"" );
	}
	for ( i = 0; i < level.maxclients; i++ ) {
		targ = &g_entities[i];
		if ( !targ->inuse || !targ->client || targ == ent || targ->health <= 0
			|| targ->client->pers.connected != CON_CONNECTED
			|| targ->client->ps.pm_type == PM_SPECTATOR ) {
			continue;
		}
		VectorSubtract( targ->client->ps.origin, origin, diff );
		if ( VectorLengthSquared( diff ) > (float)DEAFEN_RADIUS * DEAFEN_RADIUS ) {
			continue;
		}
		VectorCopy( diff, dir );
		VectorNormalize( dir );
		VectorMA( targ->client->ps.velocity, DEAFEN_PUSH, dir, targ->client->ps.velocity );
		targ->client->ps.velocity[2] += 120;
		G_InvokeState( targ )->disarmedUntil = level.time + DEAFEN_DISARM_MS;
		// tell the victim: dry-firing with no explanation reads as a bug
		trap_SendServerCommand( targ - g_entities, va( "invdeafen %i\n", DEAFEN_DISARM_MS ) );
		// the manual shove above is the whole displacement: no second knockback
		G_Damage( targ, ent, ent, dir, targ->client->ps.origin, DEAFEN_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_DEAFENING_BLAST );
	}
}

/*
==============
G_InvokeBlastImpact

A Deafening Blast missile met world or player geometry. The burst
replaces the standard explosion; the missile becomes the event carrier
so the clients draw the wave where it landed.
==============
*/
void G_InvokeBlastImpact( gentity_t *ent, trace_t *trace ) {
	G_InvokeDeafenBurst( ent->parent, trace->endpos );

	ent->s.eType = ET_GENERAL;
	ent->freeAfterEvent = qtrue;
	SnapVectorTowards( trace->endpos, ent->s.pos.trBase );
	G_SetOrigin( ent, trace->endpos );
	G_AddEvent( ent, EV_DEAFENING, DirToByte( trace->plane.normal ) );
	trap_LinkEntity( ent );
}

/*
==============
G_InvokeBlastThink

Counts down the Deafening Blast flight. If nothing was hit, the wave
bursts in the air at the end of its range.
==============
*/
void G_InvokeBlastThink( gentity_t *self ) {
	if ( !self->inuse || self->s.eType != ET_MISSILE ) {
		return;
	}
	self->nextthink = level.time + 1;
	if ( level.time >= self->s.time + DEAFEN_LIFE_MS ) {
		G_InvokeDeafenBurst( self->parent, self->r.currentOrigin );
		G_AddEvent( self, EV_DEAFENING, 0 );
		self->s.eType = ET_GENERAL;
		self->freeAfterEvent = qtrue;
	}
}

/*
==============
G_InvokeTornadoThink

Per-frame tornado effect: everyone inside the vortex, except the caster,
is lifted. The vortex fades when its lifetime ends; solid walls stop it
earlier through the missile impact path.
==============
*/
void G_InvokeTornadoThink( gentity_t *self ) {
	gentity_t	*targ;
	vec3_t		diff;
	int			i;

	if ( !self->inuse || self->s.eType != ET_MISSILE ) {
		return;
	}
	self->nextthink = level.time + 1;
	for ( i = 0; i < level.maxclients; i++ ) {
		targ = &g_entities[i];
		if ( !targ->inuse || !targ->client || targ == self->parent
			|| targ->health <= 0
			|| targ->client->pers.connected != CON_CONNECTED
			|| targ->client->ps.pm_type == PM_SPECTATOR ) {
			continue;
		}
		VectorSubtract( targ->client->ps.origin, self->r.currentOrigin, diff );
		if ( VectorLengthSquared( diff ) > (float)TORNADO_RADIUS * TORNADO_RADIUS ) {
			continue;
		}
		// a floor of upward speed, not an impulse: targets rise steadily
		// while inside and fall normally once the vortex passes
		if ( targ->client->ps.velocity[2] < TORNADO_LIFT ) {
			targ->client->ps.velocity[2] = TORNADO_LIFT;
		}
	}
	if ( level.time >= self->s.time + TORNADO_LIFE_MS ) {
		if ( self->parent && self->parent->client ) {
			trap_SendServerCommand( self->parent - g_entities, "print \"tornado faded\\n\"" );
		}
		G_FreeEntity( self );
	}
}

/*
==============
G_InvokeIceWallThink

Per-tick field logic: everyone inside, except the caster, is slowed; on
the slower damage cadence they also take chip damage and get the slow
notice. A late think skips damage ticks instead of batching them.
==============
*/
static void G_InvokeIceWallThink( gentity_t *self ) {
	gentity_t	*targ;
	vec3_t		dir, diff;
	int			i, tick;

	if ( !self->inuse || self->s.eType != ET_GENERAL ) {
		return;
	}
	self->nextthink = level.time + ICE_WALL_TICK_MS;
	if ( level.time >= self->s.time + ICE_WALL_LIFE_MS ) {
		G_FreeEntity( self );
		return;
	}
	tick = ( level.time - self->s.time ) / ICE_WALL_DAMAGE_INTERVAL_MS;
	for ( i = 0; i < level.maxclients; i++ ) {
		targ = &g_entities[i];
		if ( !targ->inuse || !targ->client || targ == self->parent
			|| targ->health <= 0
			|| targ->client->pers.connected != CON_CONNECTED
			|| targ->client->ps.pm_type == PM_SPECTATOR ) {
			continue;
		}
		VectorSubtract( targ->client->ps.origin, self->r.currentOrigin, diff );
		if ( VectorLengthSquared( diff ) > (float)ICE_WALL_RADIUS * ICE_WALL_RADIUS ) {
			continue;
		}
		// every field refreshes the same window, so any number of
		// overlapping fields is still one slow
		BG_InvokeSlowRefresh( &G_InvokeState( targ )->slow, level.time );
		if ( tick && tick != self->count ) {
			VectorCopy( diff, dir );
			VectorNormalize( dir );
			// tell the victim, so the drag has a visible source
			trap_SendServerCommand( targ - g_entities,
				va( "invslow %i\n", ICE_WALL_SLOW_NOTICE_MS ) );
			G_Damage( targ, self, self->parent, dir, targ->client->ps.origin,
				ICE_WALL_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_ICE_WALL );
		}
	}
	self->count = tick;
}

/*
==============
G_InvokePlaceIceWall

Drops the field onto the floor under the aim point, then spawns the
field entity. Clients draw the ring from the entity's s.generic1 marker.
The population is bounded: the oldest field is removed at the cap, so
rapid casts cannot grow unbounded entity counts.
==============
*/
static void G_InvokePlaceIceWall( gentity_t *ent, vec3_t aimPoint ) {
	gentity_t	*field, *oldest = NULL;
	vec3_t		place, down;
	trace_t		tr;
	int			i, count = 0;

	// settle to the floor so the field lies on the ground even when the
	// aim ray hit a wall above it
	VectorCopy( aimPoint, place );
	place[2] += 40;
	VectorCopy( place, down );
	down[2] -= 200;
	trap_Trace( &tr, place, NULL, NULL, down, ent->s.number, CONTENTS_SOLID );
	if ( tr.fraction < 1.0f ) {
		VectorCopy( tr.endpos, place );
		place[2] += 2;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].classname
			&& !strcmp( g_entities[i].classname, "invoke_icewall" ) ) {
			count++;
			if ( !oldest || g_entities[i].s.time < oldest->s.time ) {
				oldest = &g_entities[i];
			}
		}
	}
	if ( count >= ICE_WALL_MAX_FIELDS && oldest ) {
		G_FreeEntity( oldest );
	}

	field = G_Spawn();
	field->classname = "invoke_icewall";
	field->nextthink = level.time + ICE_WALL_TICK_MS;
	field->think = G_InvokeIceWallThink;
	field->s.eType = ET_GENERAL;
	field->s.generic1 = INVOKE_FX_ICEWALL;	// cgame draw marker
	field->parent = ent;
	field->r.ownerNum = ent->s.number;
	field->s.time = level.time;			// birth: the field has a lifetime
	field->count = 0;					// last applied damage tick
	G_SetOrigin( field, place );
	trap_LinkEntity( field );
}

/*
==============
G_InvokeArmorScale

CheckArmor asks for this while applying damage: a victim under a spirit
bolt's shred window gets less protection. 1.0 when no window holds.
==============
*/
float G_InvokeArmorScale( gentity_t *ent ) {
	if ( !ent || !ent->client ) {
		return 1.0f;
	}
	return BG_InvokeShredScale( &G_InvokeState( ent )->shred, level.time );
}

/*
==============
G_InvokeForgeSpiritFade / G_InvokeForgeSpiritDie

The companion leaves quietly: no corpse, no explosion. The cgame simply
stops drawing it on the next snapshot.
==============
*/
static void G_InvokeForgeSpiritFade( gentity_t *self ) {
	G_FreeEntity( self );
}

static void G_InvokeForgeSpiritDie( gentity_t *self, gentity_t *inflictor,
	gentity_t *attacker, int damage, int mod ) {
	// the engine's die signature: only the spirit itself matters here
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)mod;
	G_InvokeForgeSpiritFade( self );
}

/*
==============
G_InvokeForgeSpiritThink

Follows the caster at a hover offset and fires at the nearest visible
enemy on an interval. The spirit is server-authoritative: this think
moves, traces and re-stamps the network position each tick; the cgame
draws the wisp and its bolts from the entity markers.
==============
*/
static void G_InvokeForgeSpiritThink( gentity_t *self ) {
	gentity_t	*owner = self->parent;
	gentity_t	*best = NULL;
	vec3_t		want, dir, end, muzzle, fwd, right;
	trace_t		tr;
	float		bestDist, dist;
	int			i;

	if ( !self->inuse ) {
		return;
	}

	// the spirit lives only as long as its caster does
	if ( !owner || !owner->inuse || !owner->client
		|| owner->health <= 0
		|| owner->client->pers.connected != CON_CONNECTED ) {
		G_InvokeForgeSpiritFade( self );
		return;
	}

	// and only for its lifetime
	if ( level.time - self->s.time >= FORGE_SPIRIT_LIFE_MS ) {
		G_InvokeForgeSpiritFade( self );
		return;
	}

	self->nextthink = level.time + FORGE_SPIRIT_THINK_MS;

	// glide toward a point ahead, above and slightly right of the caster
	// so the spirit stays in view; walls stop the glide
	AngleVectors( owner->client->ps.viewangles, fwd, right, NULL );
	VectorMA( owner->r.currentOrigin, FORGE_SPIRIT_LEAD, fwd, want );
	VectorMA( want, FORGE_SPIRIT_SIDE, right, want );
	want[2] += FORGE_SPIRIT_HOVER;
	VectorSubtract( want, self->r.currentOrigin, dir );
	if ( VectorLength( dir ) > FORGE_SPIRIT_STANDOFF ) {
		float step = FORGE_SPIRIT_SPEED * FORGE_SPIRIT_THINK_MS * 0.001f;

		VectorNormalize( dir );
		VectorMA( self->r.currentOrigin, step, dir, end );
		trap_Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs,
			end, self->s.number, MASK_SOLID );
		VectorCopy( tr.endpos, self->r.currentOrigin );
	}

	// fire at the nearest enemy client in sight, on the fire interval
	if ( level.time >= self->wait ) {
		bestDist = FORGE_SPIRIT_SIGHT * FORGE_SPIRIT_SIGHT;
		for ( i = 0; i < level.maxclients; i++ ) {
			gentity_t *cand = &g_entities[i];

			if ( !cand->inuse || !cand->client || cand == owner
				|| cand->health <= 0
				|| cand->client->pers.connected != CON_CONNECTED
				|| cand->client->sess.sessionTeam == TEAM_SPECTATOR
				|| OnSameTeam( owner, cand ) ) {
				continue;
			}
			VectorSubtract( cand->r.currentOrigin, self->r.currentOrigin, dir );
			dist = VectorLengthSquared( dir );
			if ( dist > bestDist ) {
				continue;
			}
			trap_Trace( &tr, self->r.currentOrigin, NULL, NULL,
				cand->r.currentOrigin, self->s.number, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				continue;	// a wall eats this shot; hold fire
			}
			bestDist = dist;
			best = cand;
		}
		if ( best ) {
			VectorSubtract( best->r.currentOrigin, self->r.currentOrigin, dir );
			VectorNormalize( dir );
			VectorCopy( self->r.currentOrigin, muzzle );
			VectorMA( muzzle, SPIRIT_BOLT_OFFSET, dir, muzzle );
			fire_invoke_spirit_bolt( self, muzzle, dir );
			self->wait = level.time + FORGE_SPIRIT_FIRE_MS;
		}
	}

	// re-stamp the network position so clients glide between snapshots
	self->s.pos.trType = TR_LINEAR;
	self->s.pos.trTime = level.time;
	VectorCopy( self->r.currentOrigin, self->s.pos.trBase );
	VectorClear( self->s.pos.trDelta );
	VectorCopy( self->r.currentOrigin, self->s.origin );
	trap_LinkEntity( self );
}

/*
==============
G_InvokeSummonForgeSpirit

Creates the companion beside its caster. At the per-owner cap the oldest
spirit is dismissed first, so a cast always lands and the cap holds.
==============
*/
static void G_InvokeSummonForgeSpirit( gentity_t *ent ) {
	gentity_t	*iter, *oldest = NULL;
	vec3_t		spawn, forward, right;
	trace_t		tr;
	int			i, alive = 0;

	// cap: count this caster's living spirits and find the oldest
	for ( i = 0; i < level.num_entities; i++ ) {
		iter = &g_entities[i];
		if ( !iter->inuse || iter->parent != ent
			|| strcmp( iter->classname, "invoke_forge_spirit" ) ) {
			continue;
		}
		alive++;
		if ( !oldest || iter->s.time < oldest->s.time ) {
			oldest = iter;
		}
	}
	if ( alive >= FORGE_SPIRIT_MAX && oldest ) {
		G_FreeEntity( oldest );
	}

	// spawn beside and above the caster; pull back to open air if the
	// offset lands inside a wall
	AngleVectors( ent->client->ps.viewangles, forward, right, NULL );
	VectorCopy( ent->client->ps.origin, spawn );
	VectorMA( spawn, 54, right, spawn );
	spawn[2] += 30;
	trap_Trace( &tr, ent->client->ps.origin, NULL, NULL, spawn,
		ent->s.number, MASK_SOLID );
	if ( tr.fraction < 1.0f ) {
		VectorCopy( ent->client->ps.origin, spawn );
		spawn[2] += 40;
	}

	iter = G_Spawn();
	iter->classname = "invoke_forge_spirit";
	iter->parent = ent;
	iter->r.ownerNum = ent->s.number;
	iter->s.eType = ET_GENERAL;
	iter->s.generic1 = INVOKE_FX_FORGE_SPIRIT;	// cgame draw marker
	iter->s.time = level.time;					// birth: lifetime and age order
	iter->r.contents = CONTENTS_CORPSE;			// shootable, walk-through
	iter->health = FORGE_SPIRIT_HEALTH;
	iter->takedamage = qtrue;
	iter->die = G_InvokeForgeSpiritDie;
	iter->r.mins[0] = iter->r.mins[1] = iter->r.mins[2] = -FORGE_SPIRIT_BBOX;
	iter->r.maxs[0] = iter->r.maxs[1] = iter->r.maxs[2] = FORGE_SPIRIT_BBOX;
	G_SetOrigin( iter, spawn );
	trap_LinkEntity( iter );
	iter->think = G_InvokeForgeSpiritThink;
	iter->nextthink = level.time + FORGE_SPIRIT_THINK_MS;
	iter->wait = level.time + FORGE_SPIRIT_FIRE_MS;
}

/*
==============
G_InvokeSpiritBoltImpact

Replaces the standard missile explosion for spirit bolts. Enemy clients
take the hit and the armor shred; everything else just stops.
==============
*/
void G_InvokeSpiritBoltImpact( gentity_t *ent, trace_t *trace ) {
	gentity_t	*targ = &g_entities[trace->entityNum];
	gentity_t	*owner = ent->parent;
	vec3_t		dir;

	if ( targ && targ->client && targ->takedamage && targ->health > 0
		&& targ != owner && !OnSameTeam( owner, targ ) ) {
		VectorCopy( ent->s.pos.trDelta, dir );
		VectorNormalize( dir );
		G_Damage( targ, ent, owner, dir, ent->r.currentOrigin,
			SPIRIT_BOLT_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_FORGE_BOLT );
		BG_InvokeShredApply( &G_InvokeState( targ )->shred, level.time );
	}

	ent->s.eType = ET_GENERAL;
	ent->freeAfterEvent = qtrue;
}

/*
==============
G_InvokeProcessPending

Lands a scheduled Sunstrike or EMP once its delay elapses.
==============
*/
static void G_InvokeProcessPending( gentity_t *ent, invokeState_t *st, int now ) {
	vec3_t	origin;

	if ( st->sunstrikeTime && now >= st->sunstrikeTime ) {
		st->sunstrikeTime = 0;
		VectorCopy( st->sunstrikeOrigin, origin );
		G_InvokeStrike( ent, origin );
		trap_SendServerCommand( ent - g_entities, "print \"sunstrike impact\n\"" );
	}
	if ( st->empTime && now >= st->empTime ) {
		st->empTime = 0;
		VectorCopy( st->empOrigin, origin );
		G_InvokeEmp( ent, origin );
		trap_SendServerCommand( ent - g_entities, "print \"emp impact\n\"" );
	}
}

/*
==============
G_InvokeSyncMana
==============
*/
static void G_InvokeSyncMana( gentity_t *ent, invokeState_t *st ) {
	int mana = (int)st->hands.mana;

	if ( mana < 0 ) {
		mana = 0;
	}
	if ( mana > INVOKE_MANA_MAX ) {
		mana = INVOKE_MANA_MAX;
	}
	if ( ent->client->ps.stats[STAT_INVOKE_MANA] != mana ) {
		ent->client->ps.stats[STAT_INVOKE_MANA] = mana;
	}
}

static int G_InvokeCooldown( gentity_t *ent, int weapon ) {
	int cooldown;

	cooldown = BG_InvokeWeaponCooldown( weapon );
#ifdef MISSIONPACK
	if ( ent->client->persistantPowerup && ent->client->persistantPowerup->item
		&& ent->client->persistantPowerup->item->giTag == PW_SCOUT ) {
		cooldown = (int)( cooldown / 1.5f );
	} else
#endif
	if ( ent->client->ps.powerups[PW_HASTE] ) {
		cooldown = (int)( cooldown / 1.3f );
	}
	return cooldown;
}

static void G_InvokeFireHand( gentity_t *ent, invokeState_t *st, int hand,
	int fireTime, qboolean gauntletHit ) {
	int weapon, spell, cooldown;
	invokeFireResult_t result;
	const spellDef_t *def;

	// a frozen victim fires nothing at all while the freeze holds: Pmove's
	// PM_FREEZE skip already holds the classic weapon, and this keeps the
	// hand casts honest with it
	if ( G_InvokeClientFrozen( ent ) ) {
		return;
	}
	weapon = BG_InvokeHandWeapon( &st->hands, hand );
	if ( weapon == WP_NONE ) {
		// a hand holding a spell casts instead of firing a gun
		spell = BG_InvokeHandSpell( &st->hands, hand );
		if ( spell == SPELL_NONE || !G_InvokeSpellCastable( spell ) ) {
			return;
		}
		// a portal shot that cannot anchor spends nothing: refuse before
		// the mana and cooldown gate
		if ( spell == SPELL_PORTAL && !G_InvokePortalAim( ent, NULL, NULL ) ) {
			return;
		}
		def = BG_SpellDef( spell );
		if ( !def ) {
			return;
		}
		result = BG_InvokeTryCast( &st->hands, hand, fireTime, def->cost,
			def->cooldown, &spell );
		if ( result != INVOKE_FIRE_OK ) {
			if ( result == INVOKE_FIRE_NO_MANA ) {
				// a held fire button must not spam the notice every frame
				if ( level.time - st->lastNoManaCp >= INVOKE_NO_MANA_NOTICE_MS ) {
					st->lastNoManaCp = level.time;
					trap_SendServerCommand( ent - g_entities,
						va( "cp \"%s: not enough mana\n\"", def->name ) );
				}
			}
			return;
		}
		G_InvokeCastSpell( ent, st, hand, spell );
		return;
	}
	if ( fireTime < st->disarmedUntil ) {
		// deafened: hand weapons stay silent until the disarm expires
		return;
	}
	cooldown = G_InvokeCooldown( ent, weapon );
	if ( !cooldown ) {
		return;
	}
	if ( weapon == WP_GAUNTLET ) {
		// the classic attack path already resolved a gauntlet hit this
		// frame; never let one swing deal damage twice
		if ( gauntletHit || fireTime < st->hands.nextFireTime[weapon]
			|| !ent->client->ps.ammo[weapon] || !CheckGauntletAttack( ent ) ) {
			return;
		}
	}
	result = BG_InvokeTryFire( &st->hands, hand, fireTime, cooldown,
		ent->client->ps.ammo, &weapon );
	if ( result != INVOKE_FIRE_OK ) {
		return;
	}
	FireWeaponFromHand( ent, weapon, hand );
	G_AddPredictableEvent( ent, hand == INVOKE_HAND_LEFT
		? EV_FIRE_INVOKE_LEFT : EV_FIRE_INVOKE_RIGHT, weapon );
}

// fireTime is level.time, never the client command time, so cooldowns
// cannot be moved by a client clock. buttons is the sanitized Pmove
// command; gauntletHit reports that the classic path already swung.
void G_InvokeClientThink( gentity_t *ent, int buttons, int fireTime,
	qboolean gauntletHit ) {
	invokeState_t *st;

	if ( !ent || !ent->client || fireTime < 0 || ent->health <= 0
		|| ent->client->sess.sessionTeam == TEAM_SPECTATOR
		|| ent->client->ps.pm_type != PM_NORMAL
		|| ( ent->client->ps.pm_flags & PMF_RESPAWNED ) ) {
		return;
	}
	st = G_InvokeState( ent );
	// mana flows back while alive; BG_InvokeManaRegen clamps long frames
	BG_InvokeManaRegen( &st->hands, fireTime - st->lastManaTime );
	st->lastManaTime = fireTime;
	if ( st->ghostWalkUntil && fireTime >= st->ghostWalkUntil ) {
		// clear the invisibility only when this cast still owns the slot;
		// a later item pickup must keep its own timer
		if ( ent->client->ps.powerups[PW_INVIS] == st->ghostWalkUntil ) {
			ent->client->ps.powerups[PW_INVIS] = 0;
		}
		st->ghostWalkUntil = 0;
	}
	if ( st->disarmedUntil > fireTime ) {
		int remain = st->disarmedUntil - fireTime;

		// the classic selected weapon must stay silent too: PM_Weapon only
		// fires while weaponTime <= 0, so hold it above the disarm window
		if ( ent->client->ps.weaponTime < remain ) {
			ent->client->ps.weaponTime = remain;
		}
	}
	G_InvokeProcessPending( ent, st, fireTime );
	if ( buttons & BUTTON_INVOKE_LEFT ) {
		G_InvokeFireHand( ent, st, INVOKE_HAND_LEFT, fireTime, gauntletHit );
	}
	if ( buttons & BUTTON_INVOKE_RIGHT ) {
		G_InvokeFireHand( ent, st, INVOKE_HAND_RIGHT, fireTime, gauntletHit );
	}
	G_InvokeSyncMana( ent, st );
}
