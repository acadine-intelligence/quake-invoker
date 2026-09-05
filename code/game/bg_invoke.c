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
// bg_invoke.c -- invoker orb / invocation rules shared by game and cgame

#ifndef QI_HOST_TEST
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#else
#include "qi_host_shim.h"
#endif
#include "bg_invoke.h"

// Slice 1 table: ten multisets of three orbs over three types.
// counts[] is indexed by orbType_t: {NONE, QUAS, WEX, EXORT}.
// Weapons are stock so the loop plays on unmodified content; the design
// intent per combo is in docs/design/01-invoker-mechanics.md.
const invocation_t bg_invocations[] = {
	{ {0,3,0,0}, WP_GAUNTLET,			-1,	"Frost Wall",		"QQQ" },
	{ {0,0,3,0}, WP_MACHINEGUN,			100,"Blink Dash",		"WWW" },
	{ {0,0,0,3}, WP_BFG,				10,	"Sunstrike",		"EEE" },
	{ {0,2,1,0}, WP_PLASMAGUN,			60,	"Ice Shards",		"QQW" },
	{ {0,2,0,1}, WP_ROCKET_LAUNCHER,	15,	"Frost Rockets",	"QQE" },
	{ {0,1,2,0}, WP_GRENADE_LAUNCHER,	10,	"Tornado",			"WWQ" },
	{ {0,0,2,1}, WP_LIGHTNING,			120,"Chaos Lightning",	"WWE" },
	{ {0,1,0,2}, WP_SHOTGUN,			15,	"Meteor",			"EEQ" },
	{ {0,0,1,2}, WP_RAILGUN,			10,	"Alacrity",			"EEW" },
	{ {0,1,1,1}, WP_RAILGUN,			10,	"Deafening Blast",	"QWE" },
};
const int bg_numInvocations = ARRAY_LEN( bg_invocations );

/*
==============
BG_PushOrb
==============
*/
int BG_PushOrb( int slots[INVOKE_SLOTS], orbType_t orb ) {
	int		i, n;

	if ( orb <= ORB_NONE || orb >= ORB_NUM_TYPES ) {
		return 0;
	}
	// fill an empty slot first (oldest position), else shift out the oldest
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		if ( slots[i] == ORB_NONE ) {
			slots[i] = orb;
			break;
		}
	}
	if ( i == INVOKE_SLOTS ) {
		for ( i = 0; i < INVOKE_SLOTS - 1; i++ ) {
			slots[i] = slots[i + 1];
		}
		slots[INVOKE_SLOTS - 1] = orb;
	}

	n = 0;
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		if ( slots[i] != ORB_NONE ) {
			n++;
		}
	}
	return n;
}

/*
==============
BG_FindInvocation
==============
*/
const invocation_t *BG_FindInvocation( const int slots[INVOKE_SLOTS] ) {
	int		counts[ORB_NUM_TYPES];
	int		i, held, newest;

	Com_Memset( counts, 0, sizeof( counts ) );
	held = 0;
	newest = ORB_NONE;
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		if ( slots[i] > ORB_NONE && slots[i] < ORB_NUM_TYPES ) {
			counts[slots[i]]++;
			held++;
			newest = slots[i];
		}
	}
	if ( held == 0 ) {
		return NULL;
	}
	// fill empty slots with the newest orb so one press can invoke
	counts[newest] += INVOKE_SLOTS - held;

	for ( i = 0; i < bg_numInvocations; i++ ) {
		if ( bg_invocations[i].counts[ORB_QUAS] == counts[ORB_QUAS]
			&& bg_invocations[i].counts[ORB_WEX] == counts[ORB_WEX]
			&& bg_invocations[i].counts[ORB_EXORT] == counts[ORB_EXORT] ) {
			return &bg_invocations[i];
		}
	}
	return NULL;
}

/*
==============
BG_OrbLetter
==============
*/
char BG_OrbLetter( int orb ) {
	switch ( orb ) {
	case ORB_QUAS:	return 'Q';
	case ORB_WEX:	return 'W';
	case ORB_EXORT:	return 'E';
	default:		return '-';
	}
}

/*
==============
BG_OrbFromString
==============
*/
orbType_t BG_OrbFromString( const char *s ) {
	if ( !s || !s[0] || s[1] ) {
		return ORB_NONE;
	}
	switch ( s[0] ) {
	case 'q': case 'Q':	return ORB_QUAS;
	case 'w': case 'W':	return ORB_WEX;
	case 'e': case 'E':	return ORB_EXORT;
	default:			return ORB_NONE;
	}
}
