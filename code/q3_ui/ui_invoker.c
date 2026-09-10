/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
/*
=======================================================================

INVOKER MANUAL

Player-facing controls and recipe page for Quake Invoker. It opens from
the main menu, the in-game (ESC) menu, or the "invmenu" console command
that UI_ConsoleCommand routes here.

=======================================================================
*/

#include "ui_local.h"


#define INVOKER_TITLE_Y		36
#define INVOKER_LINE_STEP	17

typedef struct {
	menuframework_s	menu;
} invokermenu_t;

static invokermenu_t	s_invoker;

static const char *invokerControls[] = {
	"D loads Quas (Q). W loads Wex (W)",
	"A loads Exort (E). S moves back only",
	"One orb per press. Holding keeps moving",
	"R invokes into the right hand. T swaps",
	"MOUSE 1 and MOUSE 2 fire both hands",
	"Other combos give stock Quake weapons",
};

static const char *invokerRecipes[] = {
	"Q Q Q   Cold Snap: freeze and burst",
	"Q Q W   Ghost Walk: slip away unseen",
	"Q Q E   Ice Wall: raise a wall of ice",
	"Q W W   Tornado: lift and toss enemies",
	"W W W   EMP: burst mana and shields",
	"W W E   Alacrity: quicken your attacks",
	"W E E   Chaos Meteor: roll and burn",
	"E E E   Sunstrike: smite a distant spot",
	"Q E E   Forge Spirit: summon a spirit",
	"Q W E   Deafening Blast: shove and disarm",
};


/*
===============
UI_InvokerMenu_Draw
===============
*/
static void UI_InvokerMenu_Draw( void ) {
	int		y;
	int		i;

	y = INVOKER_TITLE_Y;
	UI_DrawProportionalString( 320, y, "QUAKE INVOKER MANUAL", UI_CENTER|UI_BIGFONT|UI_DROPSHADOW, color_white );

	y += 30;
	UI_DrawProportionalString( 320, y, "CONTROLS", UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, color_red );

	y += 18;
	for ( i = 0; i < ARRAY_LEN( invokerControls ); i++ ) {
		UI_DrawProportionalString( 320, y, invokerControls[i], UI_CENTER|UI_SMALLFONT, color_white );
		y += INVOKER_LINE_STEP;
	}

	y += 14;
	UI_DrawProportionalString( 320, y, "RECIPES (the oldest orb comes first)", UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, color_red );

	y += 18;
	for ( i = 0; i < ARRAY_LEN( invokerRecipes ); i++ ) {
		UI_DrawProportionalString( 320, y, invokerRecipes[i], UI_CENTER|UI_SMALLFONT, color_white );
		y += INVOKER_LINE_STEP;
	}

	y += 16;
	UI_DrawProportionalString( 320, y, "New spells appear here as they ship", UI_CENTER|UI_SMALLFONT, color_yellow );
	y += INVOKER_LINE_STEP;
	UI_DrawProportionalString( 320, y, "invmenu opens this page. ESC returns", UI_CENTER|UI_SMALLFONT, color_dim );
}


/*
===============
UI_InvokerMenu_Key
===============
*/
static sfxHandle_t UI_InvokerMenu_Key( int key ) {
	if ( key & K_CHAR_FLAG ) {
		return 0;
	}

	switch ( key ) {
	case K_ESCAPE:
	case K_ENTER:
	case K_SPACE:
	case K_MOUSE1:
		UI_PopMenu();
		break;
	}

	return 0;
}


/*
===============
UI_InvokerMenu
===============
*/
void UI_InvokerMenu( void ) {
	memset( &s_invoker, 0, sizeof( s_invoker ) );

	s_invoker.menu.draw = UI_InvokerMenu_Draw;
	s_invoker.menu.key = UI_InvokerMenu_Key;
	s_invoker.menu.fullscreen = qtrue;
	UI_PushMenu( &s_invoker.menu );

	Com_Printf( "Invoker manual opened\n" );
}


/*
===============
UI_InvokerMenu_f

Console command "invmenu": open the manual, or close it when it is
already on top of the stack.
===============
*/
void UI_InvokerMenu_f( void ) {
	if ( uis.activemenu == &s_invoker.menu ) {
		UI_PopMenu();
		return;
	}

	// pause a local game when the page opens straight from play,
	// the same way the ESC menu does; UI_ForceMenuOff clears it
	if ( uis.menusp == 0 ) {
		trap_Cvar_Set( "cl_paused", "1" );
	}

	UI_InvokerMenu();
}
