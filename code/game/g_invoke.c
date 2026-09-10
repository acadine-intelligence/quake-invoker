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
//   invoke        put the weapon matching the held orbs in the right hand
//   invswap       exchange the left and right hand, including an empty hand
//
// State lives in a game-owned per-client table (see below). The client
// learns the orb slots through the "orbs" server command so it can draw
// them; the invoked weapon arrives through the STAT_WEAPONS / ammo snapshot
// path and a weapon-select server command.

#include "g_local.h"
#include "bg_invoke.h"

// Per-client invoker state, owned by the game module. Keeping this state
// separate avoids changing the shared client layout for invocation features.
// Shared struct edits require every dependent QVM source to be rebuilt.
typedef struct {
	int		orbSlots[INVOKE_SLOTS];	// orbType_t values, oldest first
	invokeHands_t hands;
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
	trap_SendServerCommand( -1, va( "invhands %i %i %i", clientNum,
		BG_InvokeHandWeapon( &st->hands, INVOKE_HAND_LEFT ),
		BG_InvokeHandWeapon( &st->hands, INVOKE_HAND_RIGHT ) ) );
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
		trap_SendServerCommand( ent - g_entities, "cp \"Move with D/W/A to choose an orb\n\"" );
		return;
	}

	if ( !BG_InvokeEquipHand( &st->hands, INVOKE_HAND_RIGHT, inv->weapon,
		inv->ammo, client->ps.ammo, &client->ps.stats[STAT_WEAPONS] ) ) {
		return;
	}
	// replacing a hand can release the weapon the player had selected via
	// the classic weapon menu; fall back to a starting weapon
	if ( client->ps.weapon > WP_NONE
		&& !( client->ps.stats[STAT_WEAPONS] & ( 1 << client->ps.weapon ) ) ) {
		client->ps.weapon = ( client->ps.stats[STAT_WEAPONS] & ( 1 << WP_MACHINEGUN ) )
			? WP_MACHINEGUN : WP_GAUNTLET;
	}
	G_InvokeSendHands( ent );
	trap_SendServerCommand( ent - g_entities, va( "cp \"%s\n\"", inv->name ) );
	trap_SendServerCommand( ent - g_entities, va( "invoked %i %i",
		INVOKE_HAND_RIGHT, inv->weapon ) );
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
	int weapon, cooldown;
	invokeFireResult_t result;

	weapon = BG_InvokeHandWeapon( &st->hands, hand );
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
	if ( buttons & BUTTON_INVOKE_LEFT ) {
		G_InvokeFireHand( ent, st, INVOKE_HAND_LEFT, fireTime, gauntletHit );
	}
	if ( buttons & BUTTON_INVOKE_RIGHT ) {
		G_InvokeFireHand( ent, st, INVOKE_HAND_RIGHT, fireTime, gauntletHit );
	}
}
