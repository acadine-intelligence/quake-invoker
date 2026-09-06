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
//   invoke        grant the weapon matching the held orbs and switch to it
//
// State lives in a game-owned per-client table (see below). The client
// learns the orb slots through the "orbs" server command so it can draw
// them; the invoked weapon arrives through the STAT_WEAPONS / ammo snapshot
// path and a weapon-select server command.

#include "g_local.h"
#include "bg_invoke.h"

// Per-client invoker state, owned by the game module. Kept out of gclient_t
// on purpose: the engine writes the leading playerState_t of each client
// every frame, and the QVM and native layouts of gclient_t do not agree on
// where trailing fields land, so anything appended there gets stomped.
typedef struct {
	int		orbSlots[INVOKE_SLOTS];	// orbType_t values, oldest first
	int		invokedWeapon;			// WP_ granted by last invoke, WP_NONE if none
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

/*
==============
G_InvokeReset

Called from ClientSpawn so a fresh life starts with empty slots.
==============
*/
void G_InvokeReset( gentity_t *ent ) {
	invokeState_t	*st = G_InvokeState( ent );
	int		i;

	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		st->orbSlots[i] = ORB_NONE;
	}
	st->invokedWeapon = WP_NONE;
	G_InvokeSendOrbs( ent );
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
	inv = BG_FindInvocation( st->orbSlots );
	if ( !inv ) {
		trap_SendServerCommand( ent - g_entities, "cp \"Hold an orb first (Q, W or E)\n\"" );
		return;
	}

	// drop the previous invocation so only one invoked weapon is live.
	// starting weapons (machinegun, gauntlet) are never removed.
	if ( st->invokedWeapon > WP_NONE && st->invokedWeapon != inv->weapon
		&& st->invokedWeapon != WP_MACHINEGUN && st->invokedWeapon != WP_GAUNTLET ) {
		client->ps.stats[STAT_WEAPONS] &= ~( 1 << st->invokedWeapon );
		client->ps.ammo[st->invokedWeapon] = 0;
	}

	client->ps.stats[STAT_WEAPONS] |= ( 1 << inv->weapon );
	if ( inv->ammo < 0 ) {
		client->ps.ammo[inv->weapon] = -1;
	} else {
		client->ps.ammo[inv->weapon] = inv->ammo;
	}
	st->invokedWeapon = inv->weapon;

	// The client owns weapon selection (usercmd.weapon drives PM_Weapon), so
	// tell it which weapon to select; forcing ps.weapon here would be undone
	// by the next usercmd still carrying the old selection.
	trap_SendServerCommand( ent - g_entities, va( "cp \"%s\n\"", inv->name ) );
	trap_SendServerCommand( ent - g_entities, va( "invoked %i", inv->weapon ) );
	trap_SendServerCommand( ent - g_entities, va( "print \"invoked %s (%s)\n\"", inv->name, inv->combo ) );
}
