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
// eight of the ten classic spells cast today; the remaining two and the
// portal report "not castable yet" until their own passes land. Mapping
// rationale: docs/design/04-ordered-spells.md.
const invocation_t bg_invocations[] = {
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_COLD_SNAP,	"Cold Snap",		"QQQ" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_GHOST_WALK,	"Ghost Walk",		"QQW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_ICE_WALL,		"Ice Wall",			"QQE" },
	{ INVOKE_KIND_WEAPON,	WP_MACHINEGUN,		100,SPELL_NONE,			"Machinegun",		"QWQ" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_TORNADO,		"Tornado",			"QWW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_DEAFENING_BLAST,	"Deafening Blast",	"QWE" },
	{ INVOKE_KIND_WEAPON,	WP_SHOTGUN,			15,	SPELL_NONE,			"Shotgun",			"QEQ" },
	{ INVOKE_KIND_PORTAL,	WP_NONE,			0,	SPELL_PORTAL,		"Portal Pair",		"QEW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_FORGE_SPIRIT,	"Forge Spirit",		"QEE" },
	{ INVOKE_KIND_WEAPON,	WP_GAUNTLET,		-1,	SPELL_NONE,			"Gauntlet",			"WQQ" },
	{ INVOKE_KIND_WEAPON,	WP_ROCKET_LAUNCHER,	15,	SPELL_NONE,			"Rocket Launcher",	"WQW" },
	{ INVOKE_KIND_WEAPON,	WP_GRENADE_LAUNCHER,10,	SPELL_NONE,			"Grenade Launcher",	"WQE" },
	{ INVOKE_KIND_WEAPON,	WP_LIGHTNING,		120,SPELL_NONE,			"Lightning Gun",	"WWQ" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_EMP,			"EMP",				"WWW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_ALACRITY,		"Alacrity",			"WWE" },
	{ INVOKE_KIND_WEAPON,	WP_RAILGUN,			10,	SPELL_NONE,			"Railgun",			"WEQ" },
	{ INVOKE_KIND_WEAPON,	WP_PLASMAGUN,		60,	SPELL_NONE,			"Plasma Gun",		"WEW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_CHAOS_METEOR,	"Chaos Meteor",		"WEE" },
	{ INVOKE_KIND_WEAPON,	WP_BFG,				10,	SPELL_NONE,			"BFG10K",			"EQQ" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EQW" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EQE" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EWQ" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EWW" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EWE" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EEQ" },
	{ INVOKE_KIND_NONE,		WP_NONE,			0,	SPELL_NONE,			"Reserved",			"EEW" },
	{ INVOKE_KIND_SPELL,	WP_NONE,			0,	SPELL_SUNSTRIKE,	"Sunstrike",		"EEE" },
};
const int bg_numInvocations = ARRAY_LEN( bg_invocations );

// The classic ten, indexed by spellType_t so a recipe only stores an ID.
// Costs and cooldowns follow the mana model in 04-ordered-spells.md: one
// 100-point pool regenerated at INVOKE_MANA_REGEN_PER_SEC.
const spellDef_t bg_spells[] = {
	{ "Empty",			0,		0 },
	{ "Cold Snap",		35,		20000 },
	{ "Ghost Walk",		25,		18000 },
	{ "Ice Wall",		40,		25000 },
	{ "Tornado",		40,		25000 },
	{ "Deafening Blast",45,		30000 },
	{ "Forge Spirit",	60,		40000 },
	{ "EMP",			45,		30000 },
	{ "Alacrity",		30,		20000 },
	{ "Chaos Meteor",	55,		35000 },
	{ "Sunstrike",		45,		24000 },
	{ "Portal Pair",	15,		1500 }
};
typedef char bg_invoke_spell_table_fits[( ARRAY_LEN( bg_spells ) == SPELL_NUM ) ? 1 : -1];

const spellDef_t *BG_SpellDef( int spell ) {
	if ( spell <= SPELL_NONE || spell >= SPELL_NUM ) {
		return NULL;
	}
	return &bg_spells[spell];
}

/*
==============
BG_InvokeChillClear / Apply / CanTrigger / Triggered

Cold Snap's debuff rules, kept here so the host tests and the server run
the same code. A hit triggers the freeze and the extra damage only when
the debuff holds, the internal interval has elapsed, and the damage did
not come from Cold Snap itself (its own damage must never recurse).
==============
*/
void BG_InvokeChillClear( chillState_t *chill ) {
	chill->until = 0;
	chill->nextTrigger = 0;
	chill->freezeUntil = 0;
}

void BG_InvokeChillApply( chillState_t *chill, int now ) {
	if ( !chill->until || now >= chill->until ) {
		// a fresh snap may trigger on the very next hit
		chill->nextTrigger = 0;
	}
	// every snap refreshes the debuff, but a live interval or freeze
	// keeps its own end: recasts never shorten the victim's floor
	chill->until = now + COLD_SNAP_DEBUFF_MS;
}

qboolean BG_InvokeChillCanTrigger( const chillState_t *chill, int now,
	qboolean ownDamage ) {
	if ( !chill->until || now >= chill->until ) {
		return qfalse;
	}
	if ( ownDamage ) {
		return qfalse;
	}
	if ( now < chill->nextTrigger ) {
		return qfalse;
	}
	return qtrue;
}

void BG_InvokeChillTriggered( chillState_t *chill, int now ) {
	chill->nextTrigger = now + COLD_SNAP_TRIGGER_MS;
	chill->freezeUntil = now + COLD_SNAP_FREEZE_MS;
}

/*
==============
BG_InvokeSlowClear / Refresh / Active

Ice Wall's slow rules, kept here so the host tests and the server run the
same code. Every field covering a player refreshes the same stamp, so the
slow is a single window no matter how many fields overlap.
==============
*/
void BG_InvokeSlowClear( slowState_t *slow ) {
	slow->until = 0;
}

void BG_InvokeSlowRefresh( slowState_t *slow, int now ) {
	slow->until = now + ICE_WALL_SLOW_GRACE_MS;
}

qboolean BG_InvokeSlowActive( const slowState_t *slow, int now ) {
	if ( !slow->until || now >= slow->until ) {
		return qfalse;
	}
	return qtrue;
}

/*
==============
BG_InvokeHasteExtend / BG_InvokeWeaponDamageScale

Alacrity's rules, kept here so the host tests and the server run the same
code. The haste powerup's expiry stamp is the whole state: a cast moves it
forward and never pulls it back, so recasts refresh the window and cannot
stack. The damage scale multiplies the base (quad) factor so the two
buffs compose instead of replacing each other; only classic weapons pass
through it, spells do not.
==============
*/
void BG_InvokeHasteExtend( int *powerupEnd, int now ) {
	if ( *powerupEnd < now + ALACRITY_MS ) {
		*powerupEnd = now + ALACRITY_MS;
	}
}

float BG_InvokeWeaponDamageScale( float base, int hasteActive ) {
	if ( hasteActive ) {
		return base * ALACRITY_DAMAGE_SCALE;
	}
	return base;
}

/*
==============
BG_InvokeShredClear / BG_InvokeShredApply / BG_InvokeShredActive /
BG_InvokeShredScale

Forge Spirit bolts crack the victim's armor: while the shred window holds,
CheckArmor gives the victim less protection. One window per victim and a
second hit refreshes it. Kept here so the host tests run the same rules.
==============
*/
void BG_InvokeShredClear( shredState_t *shred ) {
	shred->until = 0;
}

void BG_InvokeShredApply( shredState_t *shred, int now ) {
	shred->until = now + ARMOR_SHRED_MS;
}

qboolean BG_InvokeShredActive( const shredState_t *shred, int now ) {
	if ( !shred->until || now >= shred->until ) {
		return qfalse;
	}
	return qtrue;
}

float BG_InvokeShredScale( const shredState_t *shred, int now ) {
	if ( BG_InvokeShredActive( shred, now ) ) {
		return ARMOR_SHRED_SCALE;
	}
	return 1.0f;
}

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

int BG_InvokeHandSpell( const invokeHands_t *hands, int hand ) {
	int spell;

	if ( !hands || hand < 0 || hand >= INVOKE_HANDS ) {
		return SPELL_NONE;
	}
	spell = hands->spell[hand];
	if ( spell <= SPELL_NONE || spell >= SPELL_NUM ) {
		return SPELL_NONE;
	}
	return spell;
}

// STAT_INVOKE_HANDS and STAT_INVOKE_SPELLS pack each hand's item into 4
// bits; these fail to compile if a table ever outgrows the nibble.
typedef char bg_invoke_weapon_pack_fits[( WP_NUM_WEAPONS <= 16 ) ? 1 : -1];
typedef char bg_invoke_spell_pack_fits[( SPELL_NUM <= 16 ) ? 1 : -1];

// A weapon lives only while a hand holds it: replacing it drops the old
// weapon unless the other hand still holds a copy. Starting weapons are
// never removed.
static void BG_InvokeReleaseWeapon( invokeHands_t *hands, int hand, int oldWeapon,
	int *weaponBits ) {
	unsigned int oldBit;

	if ( oldWeapon <= WP_NONE || oldWeapon >= WP_NUM_WEAPONS
		|| oldWeapon == WP_MACHINEGUN || oldWeapon == WP_GAUNTLET ) {
		return;
	}
	if ( oldWeapon == BG_InvokeHandWeapon( hands, hand == INVOKE_HAND_LEFT
		? INVOKE_HAND_RIGHT : INVOKE_HAND_LEFT ) ) {
		return;
	}
	oldBit = 1u << oldWeapon;
	*weaponBits &= ~(int)oldBit;
	hands->grantedWeapons &= ~oldBit;
	// remaining ammo stays on the books: releasing and re-invoking a weapon
	// must never generate ammunition
}

int BG_InvokeEquipHand( invokeHands_t *hands, int hand, int weapon,
	int initialAmmo, int ammo[WP_NUM_WEAPONS], int *weaponBits ) {
	unsigned int bit;
	int old;

	if ( !hands || !ammo || !weaponBits || hand < 0 || hand >= INVOKE_HANDS
		|| weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS || initialAmmo < -1 ) {
		return 0;
	}
	old = hands->weapon[hand];
	if ( old != weapon ) {
		BG_InvokeReleaseWeapon( hands, hand, old, weaponBits );
	}
	bit = 1u << weapon;
	*weaponBits |= (int)bit;
	hands->grantedWeapons |= bit;
	if ( !( hands->grantedOnce & bit ) ) {
		// the first invoke of this weapon in a life tops it up; later
		// releases and re-invokes keep whatever ammo is left
		if ( initialAmmo < 0 ) {
			ammo[weapon] = -1;
		} else if ( ammo[weapon] < initialAmmo ) {
			ammo[weapon] = initialAmmo;
		}
		hands->grantedOnce |= bit;
	}
	hands->weapon[hand] = weapon;
	hands->spell[hand] = SPELL_NONE;
	return 1;
}

// Equipping a spell replaces whatever the hand held; a weapon that is now
// held by neither hand is released exactly as in BG_InvokeEquipHand.
int BG_InvokeEquipSpell( invokeHands_t *hands, int hand, int spell,
	int ammo[WP_NUM_WEAPONS], int *weaponBits ) {
	if ( !hands || !ammo || !weaponBits || hand < 0 || hand >= INVOKE_HANDS
		|| spell <= SPELL_NONE || spell >= SPELL_NUM ) {
		return 0;
	}
	BG_InvokeReleaseWeapon( hands, hand, hands->weapon[hand], weaponBits );
	hands->weapon[hand] = WP_NONE;
	hands->spell[hand] = spell;
	return 1;
}

void BG_InvokeSwapHands( invokeHands_t *hands ) {
	int weapon, spell;

	if ( !hands ) {
		return;
	}
	weapon = hands->weapon[INVOKE_HAND_LEFT];
	spell = hands->spell[INVOKE_HAND_LEFT];
	hands->weapon[INVOKE_HAND_LEFT] = hands->weapon[INVOKE_HAND_RIGHT];
	hands->spell[INVOKE_HAND_LEFT] = hands->spell[INVOKE_HAND_RIGHT];
	hands->weapon[INVOKE_HAND_RIGHT] = weapon;
	hands->spell[INVOKE_HAND_RIGHT] = spell;
}

int BG_InvokePackedHands( const invokeHands_t *hands ) {
	return BG_InvokeHandWeapon( hands, INVOKE_HAND_LEFT )
		| ( BG_InvokeHandWeapon( hands, INVOKE_HAND_RIGHT ) << 4 );
}

int BG_InvokePackedSpells( const invokeHands_t *hands ) {
	return BG_InvokeHandSpell( hands, INVOKE_HAND_LEFT )
		| ( BG_InvokeHandSpell( hands, INVOKE_HAND_RIGHT ) << 4 );
}

/*
==============
BG_InvokePortalTooClose

Portal ends closer than PORTAL_MIN_SEPARATION are refused: an exit pressed
against its own entrance would bounce the traveller straight back.
==============
*/
qboolean BG_InvokePortalTooClose( const vec3_t a, const vec3_t b ) {
	vec3_t delta;

	VectorSubtract( a, b, delta );
	return VectorLengthSquared( delta ) < ( PORTAL_MIN_SEPARATION * PORTAL_MIN_SEPARATION );
}

/*
==============
BG_InvokePortalExitVelocity

Exits keep the entry speed along the exit face's normal, capped so a long
chain of portals cannot compound into runaway velocity.
==============
*/
void BG_InvokePortalExitVelocity( const vec3_t inVel, const vec3_t exitNormal, vec3_t out ) {
	float speed = (float)sqrt( inVel[0] * inVel[0] + inVel[1] * inVel[1] + inVel[2] * inVel[2] );

	if ( speed > PORTAL_EXIT_SPEED_CAP ) {
		speed = PORTAL_EXIT_SPEED_CAP;
	}
	VectorScale( exitNormal, speed, out );
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

invokeFireResult_t BG_InvokeTryCast( invokeHands_t *hands, int hand, int now,
	int cost, int cooldown, int *castSpell ) {
	int spell;

	if ( castSpell ) {
		*castSpell = SPELL_NONE;
	}
	if ( !hands || !castSpell || hand < 0 || hand >= INVOKE_HANDS
		|| now < 0 || cost < 0 || cooldown <= 0 ) {
		return INVOKE_FIRE_INVALID;
	}
	spell = BG_InvokeHandSpell( hands, hand );
	if ( spell == SPELL_NONE ) {
		return INVOKE_FIRE_EMPTY;
	}
	if ( now < hands->nextCastTime[spell] ) {
		return INVOKE_FIRE_COOLDOWN;
	}
	if ( hands->mana < cost ) {
		return INVOKE_FIRE_NO_MANA;
	}
	hands->mana -= cost;
	hands->nextCastTime[spell] = now + cooldown;
	*castSpell = spell;
	return INVOKE_FIRE_OK;
}

void BG_InvokeManaRegen( invokeHands_t *hands, int dtMs ) {
	if ( !hands || dtMs < 0 ) {
		return;
	}
	if ( dtMs > 1000 ) {
		// a paused or stalled server frame must not deliver a burst
		dtMs = 1000;
	}
	hands->mana += dtMs * ( INVOKE_MANA_REGEN_PER_SEC / 1000.0f );
	if ( hands->mana > INVOKE_MANA_MAX ) {
		hands->mana = INVOKE_MANA_MAX;
	}
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
