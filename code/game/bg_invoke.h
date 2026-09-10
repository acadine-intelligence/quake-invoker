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
// bg_invoke.h -- invoker orb / invocation rules shared by game and cgame
//
// Three orb types, three slots. The order of the three held orbs selects
// the invocation: 27 ordered recipes. Pushing a fourth orb drops the
// oldest. See docs/design/04-ordered-spells.md for the recipe table:
// ten classic spells, nine stock weapons, one portal pair, seven future
// slots. Weapons and a growing set of spells are castable; anything not
// castable yet says so instead of granting something wrong.

#ifndef BG_INVOKE_H
#define BG_INVOKE_H

#define INVOKE_SLOTS		3
#define INVOKE_HANDS		2

// One shared mana pool per player. Spells spend it; the server regenerates
// it at a constant rate. Host tests and the game both use these rules.
#define INVOKE_MANA_MAX			100
#define INVOKE_MANA_REGEN_PER_SEC	4

#define INVOKE_MOVE_W		0x01
#define INVOKE_MOVE_A		0x02
#define INVOKE_MOVE_S		0x04
#define INVOKE_MOVE_D		0x08
#define INVOKE_MOVE_ALL		0x0f

typedef enum {
	INVOKE_HAND_LEFT,
	INVOKE_HAND_RIGHT
} invokeHand_t;

typedef enum {
	INVOKE_FIRE_INVALID = -1,
	INVOKE_FIRE_EMPTY,
	INVOKE_FIRE_COOLDOWN,
	INVOKE_FIRE_NO_AMMO,
	INVOKE_FIRE_NO_MANA,
	INVOKE_FIRE_OK
} invokeFireResult_t;

typedef enum {
	SPELL_NONE,
	SPELL_COLD_SNAP,
	SPELL_GHOST_WALK,
	SPELL_ICE_WALL,
	SPELL_TORNADO,
	SPELL_DEAFENING_BLAST,
	SPELL_FORGE_SPIRIT,
	SPELL_EMP,
	SPELL_ALACRITY,
	SPELL_CHAOS_METEOR,
	SPELL_SUNSTRIKE,
	SPELL_NUM
} spellType_t;

typedef struct {
	const char	*name;		// human name shown in HUD
	int			cost;		// mana spent per cast
	int			cooldown;	// milliseconds between casts
} spellDef_t;

extern const spellDef_t bg_spells[];
const spellDef_t *BG_SpellDef( int spell );

// Server-authoritative hand assignment and per-item timing. nextFireTime is
// indexed by weapon so duplicate weapons in both hands necessarily share it;
// nextCastTime is indexed by spell the same way. mana is one pool for both
// hands. A hand holds either a weapon (weapon[hand] != WP_NONE) or a spell
// (spell[hand] != SPELL_NONE), never both. grantedWeapons tracks what the
// invoke system currently owns; grantedOnce remembers which weapons already
// took their once-per-life ammo top-up, so a release plus re-invoke can
// never generate ammunition.
typedef struct {
	int		weapon[INVOKE_HANDS];
	int		spell[INVOKE_HANDS];
	int		nextFireTime[WP_NUM_WEAPONS];
	int		nextCastTime[SPELL_NUM];
	unsigned int	grantedWeapons;
	unsigned int	grantedOnce;
	float	mana;
} invokeHands_t;

typedef enum {
	ORB_NONE,
	ORB_QUAS,		// Q - frost / control
	ORB_WEX,		// W - storm / mobility
	ORB_EXORT,		// E - fire / damage
	ORB_NUM_TYPES
} orbType_t;

typedef enum {
	INVOKE_KIND_NONE,		// reserved recipe, nothing castable yet
	INVOKE_KIND_WEAPON,		// grants a stock weapon to the hand
	INVOKE_KIND_SPELL,		// classic spell (castable when implemented)
	INVOKE_KIND_PORTAL		// portal pair (castable in a later pass)
} invokeKind_t;

typedef struct {
	int			kind;		// invokeKind_t
	int			weapon;		// WP_ for WEAPON recipes, WP_NONE otherwise
	int			ammo;		// starting ammo for WEAPON recipes (-1 = infinite)
	int			spell;		// SPELL_ for SPELL recipes, SPELL_NONE otherwise
	const char	*name;		// human name shown in HUD
	const char	*combo;		// ordered recipe, oldest orb first, e.g. "WQW"
} invocation_t;

extern const invocation_t bg_invocations[];
extern const int bg_numInvocations;

// push one orb into slots[] (oldest dropped). slots holds orbType_t values,
// ORB_NONE for empty. Returns the new orb count.
int			BG_PushOrb( int slots[INVOKE_SLOTS], orbType_t orb );

// find the invocation for the held orbs. All three slots must be set; the
// sequence, oldest orb first, is the recipe. Returns NULL until three
// orbs are held.
const invocation_t *BG_FindInvocation( const int slots[INVOKE_SLOTS] );

// single character for HUD display: 'Q', 'W', 'E' or '-'
char		BG_OrbLetter( int orb );

// parse "q", "w", "e" (case insensitive) to an orb type, ORB_NONE otherwise
orbType_t	BG_OrbFromString( const char *s );

void		BG_InvokeHandsReset( invokeHands_t *hands );
int			BG_InvokeHandWeapon( const invokeHands_t *hands, int hand );
int			BG_InvokeHandSpell( const invokeHands_t *hands, int hand );
int			BG_InvokeEquipHand( invokeHands_t *hands, int hand, int weapon,
				int initialAmmo, int ammo[WP_NUM_WEAPONS], int *weaponBits );
int			BG_InvokeEquipSpell( invokeHands_t *hands, int hand, int spell,
				int ammo[WP_NUM_WEAPONS], int *weaponBits );
void		BG_InvokeSwapHands( invokeHands_t *hands );
// STAT_INVOKE_HANDS layout: [3:0] left weapon, [7:4] right weapon, bit 8
// marker. STAT_INVOKE_SPELLS uses the same layout for spell IDs.
int			BG_InvokePackedHands( const invokeHands_t *hands );
int			BG_InvokePackedSpells( const invokeHands_t *hands );
int			BG_InvokeWeaponCooldown( int weapon );
invokeFireResult_t BG_InvokeTryFire( invokeHands_t *hands, int hand, int now,
				int cooldown, int ammo[WP_NUM_WEAPONS], int *firedWeapon );
invokeFireResult_t BG_InvokeTryCast( invokeHands_t *hands, int hand, int now,
				int cost, int cooldown, int *castSpell );
void		BG_InvokeManaRegen( invokeHands_t *hands, int dtMs );
const char	*BG_InvokeWeaponName( int weapon );

#endif
