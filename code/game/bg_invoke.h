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
// Three orb types, three slots. Pushing an orb drops the oldest.
// Invocation depends on the multiset of held orbs, not their order.
// Slice 1: each invocation maps to a stock weapon so the loop can be
// played on unmodified content. See docs/design/01-invoker-mechanics.md.

#ifndef BG_INVOKE_H
#define BG_INVOKE_H

#define INVOKE_SLOTS		3

typedef enum {
	ORB_NONE,
	ORB_QUAS,		// Q - frost / control
	ORB_WEX,		// W - storm / mobility
	ORB_EXORT,		// E - fire / damage
	ORB_NUM_TYPES
} orbType_t;

typedef struct {
	int		counts[ORB_NUM_TYPES];	// counts[ORB_QUAS] etc; ORB_NONE unused
	int		weapon;					// WP_ granted by this invocation
	int		ammo;					// ammo granted (-1 = infinite)
	const char	*name;				// human name shown in HUD
	const char	*combo;				// canonical key spelling, e.g. "QQE"
} invocation_t;

extern const invocation_t bg_invocations[];
extern const int bg_numInvocations;

// push one orb into slots[] (oldest dropped). slots holds orbType_t values,
// ORB_NONE for empty. Returns the new orb count.
int			BG_PushOrb( int slots[INVOKE_SLOTS], orbType_t orb );

// find the invocation that matches the held orbs. Empty slots are filled
// with copies of the most recently pushed orb so a single press can invoke.
// Returns NULL when no orb is held.
const invocation_t *BG_FindInvocation( const int slots[INVOKE_SLOTS] );

// single character for HUD display: 'Q', 'W', 'E' or '-'
char		BG_OrbLetter( int orb );

// parse "q", "w", "e" (case insensitive) to an orb type, ORB_NONE otherwise
orbType_t	BG_OrbFromString( const char *s );

#endif
