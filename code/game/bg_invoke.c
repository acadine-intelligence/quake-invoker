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

// Ordered recipe table: the exact sequence of the three held orbs
// (oldest first) selects the invocation. Ten classic spells, nine stock
// weapons, one portal pair, seven reserved slots. Weapons are castable;
// spells and the portal report "not castable yet" until their own passes
// land. Mapping rationale: docs/design/04-ordered-spells.md.
const invocation_t bg_invocations[] = {
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Cold Snap",		"QQQ" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Ghost Walk",		"QQW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Ice Wall",			"QQE" },
	{ INVOKE_KIND_WEAPON,	WP_MACHINEGUN,			100,"Machinegun",		"QWQ" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Tornado",			"QWW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Deafening Blast",	"QWE" },
	{ INVOKE_KIND_WEAPON,	WP_SHOTGUN,				15,	"Shotgun",			"QEQ" },
	{ INVOKE_KIND_PORTAL,	WP_NONE,				0,	"Portal Pair",		"QEW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Forge Spirit",		"QEE" },
	{ INVOKE_KIND_WEAPON,	WP_GAUNTLET,			-1,	"Gauntlet",			"WQQ" },
	{ INVOKE_KIND_WEAPON,	WP_ROCKET_LAUNCHER,		15,	"Rocket Launcher",	"WQW" },
	{ INVOKE_KIND_WEAPON,	WP_GRENADE_LAUNCHER,	10,	"Grenade Launcher",	"WQE" },
	{ INVOKE_KIND_WEAPON,	WP_LIGHTNING,			120,"Lightning Gun",	"WWQ" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"EMP",				"WWW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Alacrity",			"WWE" },
	{ INVOKE_KIND_WEAPON,	WP_RAILGUN,				10,	"Railgun",			"WEQ" },
	{ INVOKE_KIND_WEAPON,	WP_PLASMAGUN,			60,	"Plasma Gun",		"WEW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Chaos Meteor",		"WEE" },
	{ INVOKE_KIND_WEAPON,	WP_BFG,					10,	"BFG10K",			"EQQ" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EQW" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EQE" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EWQ" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EWW" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EWE" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EEQ" },
	{ INVOKE_KIND_NONE,		WP_NONE,				0,	"Reserved",			"EEW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,				0,	"Sunstrike",		"EEE" },
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
	char	key[INVOKE_SLOTS + 1];
	int		i, j;

	// All three orbs must be set; the sequence, oldest first, is the recipe.
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		if ( slots[i] <= ORB_NONE || slots[i] >= ORB_NUM_TYPES ) {
			return NULL;
		}
		key[i] = BG_OrbLetter( slots[i] );
	}
	key[INVOKE_SLOTS] = '\0';

	for ( i = 0; i < bg_numInvocations; i++ ) {
		for ( j = 0; j < INVOKE_SLOTS; j++ ) {
			if ( bg_invocations[i].combo[j] != key[j] ) {
				break;
			}
		}
		if ( j == INVOKE_SLOTS && bg_invocations[i].combo[j] == '\0' ) {
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

void BG_InvokeHandsReset( invokeHands_t *hands ) {
	if ( !hands ) {
		return;
	}
	Com_Memset( hands, 0, sizeof( *hands ) );
}

int BG_InvokeHandWeapon( const invokeHands_t *hands, int hand ) {
	int weapon;

	if ( !hands || hand < 0 || hand >= INVOKE_HANDS ) {
		return WP_NONE;
	}
	weapon = hands->weapon[hand];
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return WP_NONE;
	}
	return weapon;
}

// STAT_INVOKE_HANDS packs each hand's weapon into 4 bits; this fails to
// compile if a new weapon ever pushes the weapon count past 16.
typedef char bg_invoke_weapon_pack_fits[( WP_NUM_WEAPONS <= 16 ) ? 1 : -1];

int BG_InvokeEquipHand( invokeHands_t *hands, int hand, int weapon,
	int initialAmmo, int ammo[WP_NUM_WEAPONS], int *weaponBits ) {
	unsigned int bit, oldBit;
	int old;

	if ( !hands || !ammo || !weaponBits || hand < 0 || hand >= INVOKE_HANDS
		|| weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS || initialAmmo < -1 ) {
		return 0;
	}
	// a weapon lives only while a hand holds it: replacing it drops the old
	// weapon unless the other hand still holds a copy. Starting weapons are
	// never removed.
	old = hands->weapon[hand];
	if ( old > WP_NONE && old < WP_NUM_WEAPONS && old != weapon
		&& old != WP_MACHINEGUN && old != WP_GAUNTLET
		&& old != BG_InvokeHandWeapon( hands, hand == INVOKE_HAND_LEFT
			? INVOKE_HAND_RIGHT : INVOKE_HAND_LEFT ) ) {
		oldBit = 1u << old;
		*weaponBits &= ~(int)oldBit;
		ammo[old] = 0;
		hands->grantedWeapons &= ~oldBit;
	}
	bit = 1u << weapon;
	if ( !( hands->grantedWeapons & bit ) ) {
		*weaponBits |= (int)bit;
		if ( initialAmmo < 0 ) {
			ammo[weapon] = -1;
		} else if ( ammo[weapon] < initialAmmo ) {
			ammo[weapon] = initialAmmo;
		}
		hands->grantedWeapons |= bit;
	}
	hands->weapon[hand] = weapon;
	return 1;
}

void BG_InvokeSwapHands( invokeHands_t *hands ) {
	int weapon;

	if ( !hands ) {
		return;
	}
	weapon = hands->weapon[INVOKE_HAND_LEFT];
	hands->weapon[INVOKE_HAND_LEFT] = hands->weapon[INVOKE_HAND_RIGHT];
	hands->weapon[INVOKE_HAND_RIGHT] = weapon;
}

int BG_InvokePackedHands( const invokeHands_t *hands ) {
	return BG_InvokeHandWeapon( hands, INVOKE_HAND_LEFT )
		| ( BG_InvokeHandWeapon( hands, INVOKE_HAND_RIGHT ) << 4 );
}

int BG_InvokeWeaponCooldown( int weapon ) {
	switch ( weapon ) {
	case WP_LIGHTNING:			return 50;
	case WP_MACHINEGUN:			return 100;
	case WP_PLASMAGUN:			return 100;
	case WP_BFG:				return 200;
	case WP_GAUNTLET:			return 400;
	case WP_GRENADE_LAUNCHER:	return 800;
	case WP_ROCKET_LAUNCHER:	return 800;
	case WP_SHOTGUN:			return 1000;
	case WP_RAILGUN:			return 1500;
	default:				return 0;
	}
}

invokeFireResult_t BG_InvokeTryFire( invokeHands_t *hands, int hand, int now,
	int cooldown, int ammo[WP_NUM_WEAPONS], int *firedWeapon ) {
	int weapon;

	if ( firedWeapon ) {
		*firedWeapon = WP_NONE;
	}
	if ( !hands || !ammo || !firedWeapon || hand < 0 || hand >= INVOKE_HANDS
		|| now < 0 || cooldown <= 0 ) {
		return INVOKE_FIRE_INVALID;
	}
	weapon = BG_InvokeHandWeapon( hands, hand );
	if ( weapon == WP_NONE ) {
		return INVOKE_FIRE_EMPTY;
	}
	if ( now < hands->nextFireTime[weapon] ) {
		return INVOKE_FIRE_COOLDOWN;
	}
	if ( ammo[weapon] == 0 ) {
		return INVOKE_FIRE_NO_AMMO;
	}
	if ( ammo[weapon] > 0 ) {
		ammo[weapon]--;
	}
	hands->nextFireTime[weapon] = now + cooldown;
	*firedWeapon = weapon;
	return INVOKE_FIRE_OK;
}

const char *BG_InvokeWeaponName( int weapon ) {
	switch ( weapon ) {
	case WP_GAUNTLET:			return "Gauntlet";
	case WP_MACHINEGUN:			return "Machinegun";
	case WP_SHOTGUN:			return "Shotgun";
	case WP_GRENADE_LAUNCHER:	return "Grenade Launcher";
	case WP_ROCKET_LAUNCHER:	return "Rocket Launcher";
	case WP_LIGHTNING:			return "Lightning Gun";
	case WP_RAILGUN:			return "Railgun";
	case WP_PLASMAGUN:			return "Plasma Gun";
	case WP_BFG:				return "BFG10K";
	default:				return "Empty";
	}
}
