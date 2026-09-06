/*
===========================================================================
Copyright (C) 2026 Acadine Intelligence.

This file is part of Quake Invoker.

Quake Invoker is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

Quake Invoker is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details. You should have received a copy of the GNU General
Public License along with Quake Invoker; see COPYING.txt.
===========================================================================
*/

#include "cg_local.h"

#define INVOKE_FLASH_MSEC 600
#define ORB_TRAIL_STEPS 5
#define INVOKE_SPARKS 18

// The existing railDisc shader accepts vertex RGB in both OA and Q3 data.
// Additive shaders fade through RGB, even when they ignore vertex alpha.
static const vec4_t orbColors[ORB_NUM_TYPES] = {
	{ 0.18f, 0.22f, 0.30f, 1.0f },
	{ 0.20f, 0.65f, 1.00f, 1.0f },
	{ 0.72f, 0.28f, 1.00f, 1.0f },
	{ 1.00f, 0.38f, 0.08f, 1.0f }
};

static int CG_ValidOrb( int orb ) {
	return orb > ORB_NONE && orb < ORB_NUM_TYPES ? orb : ORB_NONE;
}

void CG_ResetInvokeEffects( void ) {
	memset( cg.orbSlots, 0, sizeof( cg.orbSlots ) );
	memset( cg.invokedSlots, 0, sizeof( cg.invokedSlots ) );
	cg.orbChangeTime = 0;
	cg.invokeEffectEndTime = 0;
}

void CG_SetOrbSlots( int a, int b, int c ) {
	cg.orbSlots[0] = CG_ValidOrb( a );
	cg.orbSlots[1] = CG_ValidOrb( b );
	cg.orbSlots[2] = CG_ValidOrb( c );
	cg.orbChangeTime = cg.time;
	// The server sends empty slots on spawn. Cancel the previous life's FX.
	if ( !cg.orbSlots[0] && !cg.orbSlots[1] && !cg.orbSlots[2] ) {
		CG_ResetInvokeEffects();
	}
}

void CG_InvokeWeapon( int weapon ) {
	const invocation_t *inv;

	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return;
	}
	cg.weaponSelect = weapon;
	cg.weaponSelectTime = cg.time;
	inv = BG_FindInvocation( cg.orbSlots );
	if ( inv && inv->weapon == weapon ) {
		memcpy( cg.invokedSlots, cg.orbSlots, sizeof( cg.invokedSlots ) );
		cg.invokeEffectEndTime = cg.time + INVOKE_FLASH_MSEC;
	}
}

static qboolean CG_InvokeVisible( void ) {
	return cg.snap && cg.snap->ps.clientNum == cg.clientNum
		&& cg.predictedPlayerState.pm_type == PM_NORMAL
		&& cg.predictedPlayerState.stats[STAT_HEALTH] > 0
		&& cg.predictedPlayerState.persistant[PERS_TEAM] != TEAM_SPECTATOR
		&& !( cg.predictedPlayerState.pm_flags & PMF_FOLLOW )
		&& !cg.intermissionStarted;
}

static float CG_InvokeFlash( void ) {
	int remaining;

	remaining = cg.invokeEffectEndTime - cg.time;
	if ( remaining <= 0 || remaining > INVOKE_FLASH_MSEC ) {
		return 0.0f;
	}
	return remaining / (float)INVOKE_FLASH_MSEC;
}

static void CG_InvokePoint( float forward, float left, float up, vec3_t out ) {
	VectorMA( cg.refdef.vieworg, forward, cg.refdef.viewaxis[0], out );
	VectorMA( out, left, cg.refdef.viewaxis[1], out );
	VectorMA( out, up, cg.refdef.viewaxis[2], out );
}

static void CG_InvokeSprite( const vec3_t origin, float radius,
	const vec4_t color, float brightness, float rotation ) {
	refEntity_t ent;
	int i;

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	ent.renderfx = RF_FIRST_PERSON | RF_DEPTHHACK;
	ent.customShader = cgs.media.railRingsShader;
	VectorCopy( origin, ent.origin );
	ent.radius = radius;
	ent.rotation = rotation;
	for ( i = 0; i < 3; i++ ) {
		ent.shaderRGBA[i] = (byte)( 255 * color[i] * brightness );
	}
	ent.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &ent );
}

// Camera-relative motes orbit below the crosshair. No game entities or
// particles accumulate: each frame submits a fixed, bounded render list.
void CG_AddInvokeEffects( void ) {
	int i, j, orb, held;
	float angle, phase, fade, flash, progress;
	vec3_t origin;
	vec4_t color;

	if ( !CG_InvokeVisible() || !cg_invokeEffects.integer
		|| cg.renderingThirdPerson || cg.hyperspace
		|| !cgs.media.railRingsShader ) {
		return;
	}
	phase = ( cg.time % 6000 ) * ( 2.0f * M_PI / 6000.0f );
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		orb = CG_ValidOrb( cg.orbSlots[i] );
		if ( !orb ) {
			continue;
		}
		for ( j = 0; j < ORB_TRAIL_STEPS; j++ ) {
			angle = phase + i * ( 2.0f * M_PI / INVOKE_SLOTS ) - j * 0.13f;
			CG_InvokePoint( 28 + 2 * sin( angle ), 11 * cos( angle ),
				-10 + 3 * sin( angle ), origin );
			fade = 1.0f - j / (float)ORB_TRAIL_STEPS;
			CG_InvokeSprite( origin, j ? 0.65f * fade : 1.7f,
				orbColors[orb], fade, angle * 180 / M_PI );
		}
	}

	flash = CG_InvokeFlash();
	if ( !flash ) {
		return;
	}
	VectorClear( color );
	color[3] = 1.0f;
	held = 0;
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		orb = CG_ValidOrb( cg.invokedSlots[i] );
		if ( orb ) {
			VectorAdd( color, orbColors[orb], color );
			held++;
		}
	}
	if ( !held ) {
		return;
	}
	VectorScale( color, 1.0f / held, color );
	progress = 1.0f - flash;
	for ( i = 0; i < INVOKE_SPARKS; i++ ) {
		angle = i * ( 2.0f * M_PI / INVOKE_SPARKS ) + progress;
		CG_InvokePoint( 36, ( 3 + 16 * progress ) * cos( angle ),
			-8 + ( 2 + 9 * progress ) * sin( angle ), origin );
		CG_InvokeSprite( origin, 1.4f * flash + 0.2f, color, flash,
			angle * 180 / M_PI );
	}
	CG_InvokePoint( 18, 0, -8, origin );
	trap_R_AddLightToScene( origin, 160 * flash, color[0], color[1], color[2] );
}

void CG_DrawOrbs( void ) {
	const invocation_t *inv;
	const char *text;
	int i, orb;
	float x, size, pulse, flash;
	vec4_t color;
	const vec4_t panel = { 0.025f, 0.035f, 0.065f, 0.78f };
	vec4_t muted = { 0.58f, 0.66f, 0.78f, 1.0f };
	vec4_t white = { 0.94f, 0.97f, 1.0f, 1.0f };
	char letter[2];

	if ( !CG_InvokeVisible() ) {
		return;
	}
	flash = cg_invokeEffects.integer ? CG_InvokeFlash() : 0;
	CG_FillRect( 16, 276, 200, 108, panel );
	CG_DrawSmallStringColor( 28, 280, "INVOKER", muted );
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		orb = CG_ValidOrb( cg.orbSlots[i] );
		memcpy( color, orbColors[orb], sizeof( color ) );
		x = 52 + i * 60;
		pulse = ( cg.time - cg.orbChangeTime ) / 300.0f;
		pulse = pulse >= 0 && pulse < 1 && cg_invokeEffects.integer ? 1 - pulse : 0;
		size = 38 + 8 * pulse + 8 * flash;
		trap_R_SetColor( color );
		CG_DrawPic( x - size / 2, 317 - size / 2, size, size, cgs.media.railRingsShader );
		trap_R_SetColor( NULL );
		letter[0] = BG_OrbLetter( orb );
		letter[1] = '\0';
		CG_DrawStringExt( x - 6, 309, letter, white, qtrue, qtrue, 12, 16, 0 );
	}
	inv = BG_FindInvocation( cg.orbSlots );
	text = inv ? va( "READY %s", inv->name ) : "Choose an orb";
	CG_DrawSmallStringColor( 28, 344, text, white );
	inv = BG_FindInvocation( cg.invokedSlots );
	text = inv ? va( "CAST %s", inv->name ) : "Invoke to equip";
	CG_DrawSmallStringColor( 28, 362, text, muted );
	if ( flash ) {
		color[0] = 0.65f; color[1] = 0.80f; color[2] = 1.0f; color[3] = flash;
		CG_FillRect( 16, 382, 200 * flash, 2, color );
	}
	trap_R_SetColor( NULL );
}
