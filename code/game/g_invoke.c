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
// Chaos Meteor, Tornado (travelling vortex) and Deafening Blast (a shove
// that disarms weapon fire).

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
	// a dead caster's pending EMP dies with the life, like Sunstrike:
	// otherwise the burst lands on the respawned player's behalf
	st->empTime = 0;
	VectorClear( st->empOrigin );
	st->lastNoManaCp = 0;
	st->disarmedUntil = 0;
	ent->client->ps.stats[STAT_INVOKE_MANA] = INVOKE_MANA_MAX;
	G_InvokeSendOrbs( ent );
	// broadcast, not a single send: a spawning or joining client also
	// needs every other player's hands
	G_InvokeBroadcastHands();
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
	case SPELL_GHOST_WALK:
	case SPELL_SUNSTRIKE:
	case SPELL_EMP:
	case SPELL_CHAOS_METEOR:
	case SPELL_TORNADO:
	case SPELL_DEAFENING_BLAST:
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
	if ( inv->kind == INVOKE_KIND_SPELL ) {
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

/*
==============
G_InvokeCastSpell

Runs a successful cast. Ghost Walk turns the caster invisible for 5 s;
Sunstrike aims now and lands 1.75 s later, so the strike is dodgeable.
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
	case SPELL_GHOST_WALK:
		st->ghostWalkUntil = level.time + 5000;
		// only extend: a longer invisibility from an item must survive the cast
		if ( ps->powerups[PW_INVIS] < st->ghostWalkUntil ) {
			ps->powerups[PW_INVIS] = st->ghostWalkUntil;
		}
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
		G_Damage( targ, ent, ent, dir, targ->client->ps.origin, EMP_DAMAGE, 0, MOD_EMP );
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
		G_Damage( targ, ent, ent, dir, targ->client->ps.origin, DEAFEN_DAMAGE, 0, MOD_DEAFENING_BLAST );
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

	weapon = BG_InvokeHandWeapon( &st->hands, hand );
	if ( weapon == WP_NONE ) {
		// a hand holding a spell casts instead of firing a gun
		spell = BG_InvokeHandSpell( &st->hands, hand );
		if ( spell == SPELL_NONE || !G_InvokeSpellCastable( spell ) ) {
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
