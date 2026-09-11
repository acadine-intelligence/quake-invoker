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
	memset( cg.invokeHandWeapons, 0, sizeof( cg.invokeHandWeapons ) );
	memset( cg.invokeHandSpells, 0, sizeof( cg.invokeHandSpells ) );
	memset( cg.invokeHandFireTime, 0, sizeof( cg.invokeHandFireTime ) );
	memset( cg.invokeCastTime, 0, sizeof( cg.invokeCastTime ) );
	cg.invokeEmpStartTime = 0;
	cg.invokeEmpEndTime = 0;
	cg.invokeEmpCaster = -1;	// -1: no caster; 0 is a real client number
	cg.invokeDeafenStartTime = 0;
	cg.invokeDeafenEndTime = 0;
	cg.invokeDisarmStartTime = 0;
	cg.invokeDisarmEndTime = 0;
	cg.invokeChillStartTime = 0;
	cg.invokeChillEndTime = 0;
	cg.invokeSlowStartTime = 0;
	cg.invokeSlowEndTime = 0;
	cg.invokeStrikeEndTime = 0;
	// Physical held keys survive gameplay/visual resets until key-up.
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

void CG_SetInvokeHands( int clientNum, int leftWeapon, int rightWeapon ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	if ( leftWeapon <= WP_NONE || leftWeapon >= WP_NUM_WEAPONS ) {
		leftWeapon = WP_NONE;
	}
	if ( rightWeapon <= WP_NONE || rightWeapon >= WP_NUM_WEAPONS ) {
		rightWeapon = WP_NONE;
	}
	cg.invokeHandWeapons[clientNum][INVOKE_HAND_LEFT] = leftWeapon;
	cg.invokeHandWeapons[clientNum][INVOKE_HAND_RIGHT] = rightWeapon;
}

void CG_SetInvokeSpells( int clientNum, int leftSpell, int rightSpell ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	if ( leftSpell <= SPELL_NONE || leftSpell >= SPELL_NUM ) {
		leftSpell = SPELL_NONE;
	}
	if ( rightSpell <= SPELL_NONE || rightSpell >= SPELL_NUM ) {
		rightSpell = SPELL_NONE;
	}
	cg.invokeHandSpells[clientNum][INVOKE_HAND_LEFT] = leftSpell;
	cg.invokeHandSpells[clientNum][INVOKE_HAND_RIGHT] = rightSpell;
}

// The sunstrike column: remember the beam the server announced. CG_AddInvokeEffects
// draws it for its short lifetime each frame, so nothing outlives the strike.
void CG_InvokeStrikeBeam( vec3_t start, vec3_t end ) {
	VectorCopy( start, cg.invokeStrikeStart );
	VectorCopy( end, cg.invokeStrikeEnd );
	cg.invokeStrikeEndTime = cg.time + 600;
}

// The server announces each EMP charge with "invemp": where the burst sits
// and how long the charge runs. The client draws the ring from cg state.
void CG_InvokeEmpCharge( int caster, vec3_t origin, int duration ) {
	if ( duration <= 0 ) {
		return;
	}
	VectorCopy( origin, cg.invokeEmpOrigin );
	cg.invokeEmpStartTime = cg.time;
	cg.invokeEmpEndTime = cg.time + duration;
	cg.invokeEmpCaster = caster;
}

// A caster's charge is retracted (death, disconnect). Only take the ring
// down when it is the one on screen: another caster's charge may be the
// one drawn here, and its dodge cue must survive. A cancel for an expired
// window is a no-op: nothing of it remains on screen.
void CG_InvokeEmpCancel( int caster ) {
	if ( cg.invokeEmpEndTime <= cg.time || caster != cg.invokeEmpCaster ) {
		return;
	}
	cg.invokeEmpStartTime = 0;
	cg.invokeEmpEndTime = 0;
	cg.invokeEmpCaster = -1;
}

// The burst hit this player: weapons are silent for the duration. The HUD
// shows the window; without it a dry trigger reads as a broken gun.
void CG_InvokeDisarm( int duration ) {
	if ( duration <= 0 ) {
		return;
	}
	cg.invokeDisarmStartTime = cg.time;
	cg.invokeDisarmEndTime = cg.time + duration;
}

float CG_InvokeDisarmFraction( void ) {
	int span;

	if ( cg.invokeDisarmEndTime <= cg.time ) {
		return 0.0f;
	}
	span = cg.invokeDisarmEndTime - cg.invokeDisarmStartTime;
	if ( span <= 0 ) {
		return 0.0f;
	}
	return ( cg.invokeDisarmEndTime - cg.time ) / (float)span;
}

// Cold Snap landed on this player: the debuff runs for its duration. The
// HUD shows it; a chilled player needs to know a hit will freeze them.
void CG_InvokeChill( int duration ) {
	if ( duration <= 0 ) {
		return;
	}
	cg.invokeChillStartTime = cg.time;
	cg.invokeChillEndTime = cg.time + duration;
}

float CG_InvokeChillFraction( void ) {
	int span;

	if ( cg.invokeChillEndTime <= cg.time ) {
		return 0.0f;
	}
	span = cg.invokeChillEndTime - cg.invokeChillStartTime;
	if ( span <= 0 ) {
		return 0.0f;
	}
	return ( cg.invokeChillEndTime - cg.time ) / (float)span;
}

// An Ice Wall field holds this player: the drag is felt directly, and the
// bar names the source. Each notice refreshes the window; standing in the
// field keeps it alive, leaving lets it drain.
void CG_InvokeSlow( int duration ) {
	if ( duration <= 0 ) {
		return;
	}
	cg.invokeSlowStartTime = cg.time;
	cg.invokeSlowEndTime = cg.time + duration;
}

float CG_InvokeSlowFraction( void ) {
	int span;

	if ( cg.invokeSlowEndTime <= cg.time ) {
		return 0.0f;
	}
	span = cg.invokeSlowEndTime - cg.invokeSlowStartTime;
	if ( span <= 0 ) {
		return 0.0f;
	}
	return ( cg.invokeSlowEndTime - cg.time ) / (float)span;
}

// Alacrity: the caster's haste window, read straight from the player
// state, so expiry, death and respawn clear the bar with no extra
// plumbing. A "Speed" pickup rides the same slot and lights the bar.
float CG_InvokeAlacrityFraction( void ) {
	int end;

	if ( !cg.snap ) {
		return 0.0f;
	}
	end = cg.snap->ps.powerups[PW_HASTE];
	if ( end <= cg.time ) {
		return 0.0f;
	}
	if ( end - cg.time > ALACRITY_MS ) {
		return 1.0f;
	}
	return ( end - cg.time ) / (float)ALACRITY_MS;
}

static void CG_InvokeFlashStart( void ) {
	memcpy( cg.invokedSlots, cg.orbSlots, sizeof( cg.invokedSlots ) );
	cg.invokeEffectEndTime = cg.time + INVOKE_FLASH_MSEC;
}

void CG_InvokeWeapon( int hand, int weapon, int spell ) {
	const invocation_t *inv;

	if ( hand < 0 || hand >= INVOKE_HANDS ) {
		return;
	}
	inv = BG_FindInvocation( cg.orbSlots );
	if ( !inv ) {
		return;
	}
	if ( weapon > WP_NONE && weapon < WP_NUM_WEAPONS
		&& inv->kind == INVOKE_KIND_WEAPON && inv->weapon == weapon ) {
		CG_InvokeFlashStart();
	} else if ( spell > SPELL_NONE && spell < SPELL_NUM
		&& inv->kind == INVOKE_KIND_SPELL && inv->spell == spell ) {
		CG_InvokeFlashStart();
	}
}

void CG_InvokeMovementKey( int moveKey, qboolean down ) {
	const char *orbCommand;

	if ( !( moveKey & INVOKE_MOVE_ALL ) || ( moveKey & ~INVOKE_MOVE_ALL )
		|| ( moveKey & ( moveKey - 1 ) ) ) {
		return;
	}
	if ( !down ) {
		cg.invokeMoveKeys &= ~moveKey;
		return;
	}
	if ( cg.invokeMoveKeys & moveKey ) {
		return;
	}
	cg.invokeMoveKeys |= moveKey;
	orbCommand = NULL;
	switch ( moveKey ) {
	case INVOKE_MOVE_W:	orbCommand = "orb w"; break;
	case INVOKE_MOVE_A:	orbCommand = "orb e"; break;
	case INVOKE_MOVE_D:	orbCommand = "orb q"; break;
	default:		break;
	}
	if ( orbCommand ) {
		trap_SendClientCommand( orbCommand );
	}
}


void CG_InvokeHandFired( centity_t *cent, int hand, int weapon ) {
	int clientNum;

	if ( !cent ) {
		return;
	}
	clientNum = cent->currentState.clientNum;
	if ( hand < 0 || hand >= INVOKE_HANDS || weapon <= WP_NONE
		|| weapon >= WP_NUM_WEAPONS || clientNum < 0 || clientNum >= MAX_CLIENTS
		|| cg.invokeHandWeapons[clientNum][hand] != weapon ) {
		return;
	}
	cg.invokeHandFireTime[clientNum][hand] = cg.time;
}

// The server confirms each successful cast with an "invcast" message. The
// client remembers when, so the HUD can show the spell recharging. Server
// timing stays authoritative; this only drives the readout.
void CG_InvokeSpellCast( int hand, int spell ) {
	if ( hand < 0 || hand >= INVOKE_HANDS
		|| spell <= SPELL_NONE || spell >= SPELL_NUM ) {
		return;
	}
	// keyed by spell: the server cools down per spell, so a hand that later
	// swaps to another spell must not inherit this clock
	cg.invokeCastTime[spell] = cg.time;
}

// 0.0 right after a cast, 1.0 when the spell is ready again. A hand that
// never cast, or a spell without a cooldown, is always ready.
float CG_InvokeReadyFraction( int now, int castTime, int cooldown ) {
	int remaining;

	if ( cooldown <= 0 || castTime <= 0 ) {
		return 1.0f;
	}
	remaining = castTime + cooldown - now;
	if ( remaining <= 0 ) {
		return 1.0f;
	}
	if ( remaining >= cooldown ) {
		return 0.0f;
	}
	return 1.0f - remaining / (float)cooldown;
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

// World-anchored sprite without the first-person depth hack: EMP charges
// sit in the level and must depth test like any other scenery.
static void CG_InvokeWorldSprite( const vec3_t origin, float radius,
	const vec4_t color, float brightness, float rotation ) {
	refEntity_t ent;
	int i;

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
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

// The deafening blast: remember where the wave burst. CG_AddInvokeEffects
// draws the expanding rings for the short window; nothing persists after.
void CG_InvokeDeafenBurst( vec3_t origin ) {
	VectorCopy( origin, cg.invokeDeafenOrigin );
	cg.invokeDeafenStartTime = cg.time;
	cg.invokeDeafenEndTime = cg.time + DEAFEN_BURST_MSEC;
}

// Tornado: a twisting column of sprites rigged to the missile entity, so
// the vortex stays glued to the real (server-side) projectile. The missile
// carries the INVOKE_FX_TORNADO marker instead of a weapon model.
void CG_InvokeTornado( centity_t *cent ) {
	const vec4_t col = { 0.45f, 0.75f, 1.0f, 1.0f };
	int i;
	float a, f;
	vec3_t p;

	for ( i = 0; i < TORNADO_SPRITES; i++ ) {
		f = i / (float)( TORNADO_SPRITES - 1 );
		a = cg.time * 0.010f + f * 4.2f;
		p[0] = cent->lerpOrigin[0] + ( 10.0f + 44.0f * f ) * cos( a );
		p[1] = cent->lerpOrigin[1] + ( 10.0f + 44.0f * f ) * sin( a );
		p[2] = cent->lerpOrigin[2] - 26.0f + 128.0f * f;
		CG_InvokeWorldSprite( p, 3.0f + 8.0f * ( 1.0f - f ), col,
			0.30f + 0.55f * ( 1.0f - f ), a * 180.0f / M_PI );
	}
	trap_R_AddLightToScene( cent->lerpOrigin, 120, col[0], col[1], col[2] );
}

// Deafening Blast in flight: a bright pressure core with a short trail,
// drawn from the INVOKE_FX_DEAFENING marker.
void CG_InvokeBlastMissile( centity_t *cent ) {
	const vec4_t col = { 0.85f, 0.80f, 1.0f, 1.0f };
	vec3_t dir, p;
	float len;
	int i;

	// normalize the flight direction without q_math: the host test builds
	// cg_invoke.c on its own and has no engine math library
	VectorCopy( cent->currentState.pos.trDelta, dir );
	len = sqrt( dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2] );
	if ( len > 0.001f ) {
		VectorScale( dir, 1.0f / len, dir );
	} else {
		dir[0] = 0;
		dir[1] = 0;
		dir[2] = 1;
	}
	for ( i = 3; i >= 0; i-- ) {
		VectorMA( cent->lerpOrigin, -16.0f * i, dir, p );
		CG_InvokeWorldSprite( p, i ? 6.0f - 1.2f * i : 10.0f, col,
			i ? 0.28f - 0.06f * i : 0.85f, 0 );
	}
	trap_R_AddLightToScene( cent->lerpOrigin, 150, col[0], col[1], col[2] );
}

// An Ice Wall field lies on the ground; the ring is drawn from the field
// entity itself, so any number of fields draws at once and none outlives
// its server-side lifetime. Bounded: 20 + 10 sprites per field.
void CG_InvokeIceField( centity_t *cent ) {
	const vec4_t col = { 0.55f, 0.85f, 1.0f, 1.0f };
	float bloom, pulse, phase;
	int i;
	vec3_t p;

	// a short bloom at spawn, clamped: any clock skew just skips it
	bloom = 1.0f;
	if ( cent->currentState.time && cg.time > cent->currentState.time ) {
		bloom = ( cg.time - cent->currentState.time ) / 300.0f;
		if ( bloom > 1.0f ) {
			bloom = 1.0f;
		}
	}
	if ( bloom < 0.3f ) {
		bloom = 0.3f;
	}
	pulse = 0.7f + 0.2f * sin( cg.time * 0.004f );
	phase = ( cg.time % 4000 ) * ( 2.0f * M_PI / 4000.0f );
	for ( i = 0; i < ICE_FIELD_OUTER_STEPS; i++ ) {
		float a = phase * 0.25f + i * ( 2.0f * M_PI / ICE_FIELD_OUTER_STEPS );

		p[0] = cent->lerpOrigin[0] + 160 * bloom * cos( a );
		p[1] = cent->lerpOrigin[1] + 160 * bloom * sin( a );
		p[2] = cent->lerpOrigin[2] + 5 + 2 * sin( phase + i );
		CG_InvokeWorldSprite( p, ( i % 2 ) ? 9.0f : 13.0f, col, pulse * bloom, a * 180 / M_PI );
	}
	for ( i = 0; i < ICE_FIELD_INNER_STEPS; i++ ) {
		float a = -phase * 0.5f + i * ( 2.0f * M_PI / ICE_FIELD_INNER_STEPS );

		p[0] = cent->lerpOrigin[0] + 88 * bloom * cos( a );
		p[1] = cent->lerpOrigin[1] + 88 * bloom * sin( a );
		p[2] = cent->lerpOrigin[2] + 8;
		CG_InvokeWorldSprite( p, 7.0f, col, pulse * 0.7f * bloom, a * 180 / M_PI );
	}
	trap_R_AddLightToScene( cent->lerpOrigin, 200, 0.4f, 0.7f, 1.0f );
}

// Camera-relative motes orbit below the crosshair. No game entities or
// particles accumulate: each frame submits a fixed, bounded render list.
void CG_AddInvokeEffects( void ) {
	int i, j, orb, held;
	float angle, phase, fade, flash, progress, span;
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

	// sunstrike: a short-lived beam column, drawn from cg state so no
	// entity or particle outlives its lifetime
	if ( cg.invokeStrikeEndTime > cg.time ) {
		refEntity_t	beam;

		memset( &beam, 0, sizeof( beam ) );
		VectorCopy( cg.invokeStrikeStart, beam.origin );
		VectorCopy( cg.invokeStrikeEnd, beam.oldorigin );
		beam.reType = RT_LIGHTNING;
		beam.customShader = cgs.media.lightningShader;
		trap_R_AddRefEntityToScene( &beam );
	}

	// emp: an expanding charge ring at the burst point until it lands.
	// A circle of sprites, not one billboard: a single sprite centred at
	// the caster's feet sits below the frustum and gets culled.
	if ( cg.invokeEmpEndTime > cg.time ) {
		float charge = 0.0f;

		span = cg.invokeEmpEndTime - cg.invokeEmpStartTime;
		if ( span > 0 ) {
			charge = ( cg.time - cg.invokeEmpStartTime ) / span;
			if ( charge < 0 ) {
				charge = 0;
			}
			if ( charge > 1 ) {
				charge = 1;
			}
		}
		for ( i = 0; i < EMP_RING_STEPS; i++ ) {
			float	a = phase * 0.5f + i * ( 2.0f * M_PI / EMP_RING_STEPS );
			vec3_t	p;

			p[0] = cg.invokeEmpOrigin[0] + ( 24 + 200 * charge ) * cos( a );
			p[1] = cg.invokeEmpOrigin[1] + ( 24 + 200 * charge ) * sin( a );
			p[2] = cg.invokeEmpOrigin[2] + 2;
			CG_InvokeWorldSprite( p, 5 + 15 * charge,
				orbColors[ORB_WEX], 0.45f + 0.55f * charge, a * 180 / M_PI );
		}
		trap_R_AddLightToScene( cg.invokeEmpOrigin, 100 + 140 * charge,
			0.35f, 0.6f, 1.0f );
	}

	// deafening blast: two flat rings leave the burst point as one wave
	if ( cg.invokeDeafenEndTime > cg.time ) {
		const vec4_t deafenCol = { 0.85f, 0.82f, 1.0f, 1.0f };
		float wave = 0.0f;

		span = cg.invokeDeafenEndTime - cg.invokeDeafenStartTime;
		if ( span > 0 ) {
			wave = ( cg.time - cg.invokeDeafenStartTime ) / span;
			if ( wave < 0 ) {
				wave = 0;
			}
			if ( wave > 1 ) {
				wave = 1;
			}
		}
		for ( j = 0; j < 2; j++ ) {
			float r = 60 + 280 * wave - j * 70;

			if ( r < 8 ) {
				continue;
			}
			for ( i = 0; i < EMP_RING_STEPS; i++ ) {
				float	a = i * ( 2.0f * M_PI / EMP_RING_STEPS );
				vec3_t	p;

				p[0] = cg.invokeDeafenOrigin[0] + r * cos( a );
				p[1] = cg.invokeDeafenOrigin[1] + r * sin( a );
				p[2] = cg.invokeDeafenOrigin[2] + 6;
				CG_InvokeWorldSprite( p, j ? 3.0f : 5.0f, deafenCol,
					( j ? 0.4f : 0.85f ) * ( 1.0f - wave ), a * 180 / M_PI );
			}
		}
		trap_R_AddLightToScene( cg.invokeDeafenOrigin, 260 - 160 * wave,
			0.7f, 0.72f, 1.0f );
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

/*
==============
CG_InvokeHandLabel

One HUD line for a hand: no ammo count for an empty hand or for
infinite-ammo weapons, a live count otherwise.
==============
*/
static const char *CG_InvokeHandLabel( const char *label, int weapon, int spell,
	const int *ammo ) {
	const spellDef_t *def;

	if ( spell > SPELL_NONE && spell < SPELL_NUM ) {
		def = BG_SpellDef( spell );
		if ( def ) {
			return va( "%s %s", label, def->name );
		}
	}
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return va( "%s Empty", label );
	}
	if ( ammo[weapon] < 0 ) {
		return va( "%s %s", label, BG_InvokeWeaponName( weapon ) );
	}
	return va( "%s %s [%d]", label, BG_InvokeWeaponName( weapon ), ammo[weapon] );
}

void CG_DrawOrbs( void ) {
	const invocation_t *inv;
	const char *text;
	int i, orb, mana, spellId;
	float x, size, pulse, flash, ready;
	const spellDef_t *spellDef;
	vec4_t color;
	const vec4_t panel = { 0.025f, 0.035f, 0.065f, 0.78f };
	const vec4_t barBack = { 0.09f, 0.13f, 0.21f, 0.90f };
	const vec4_t barFill = { 0.25f, 0.55f, 1.00f, 0.95f };
	const vec4_t cooldownFill = { 0.95f, 0.72f, 0.25f, 0.95f };
	vec4_t muted = { 0.58f, 0.66f, 0.78f, 1.0f };
	vec4_t white = { 0.94f, 0.97f, 1.0f, 1.0f };
	int clientNum;
	vec4_t keyColor;

	if ( !CG_InvokeVisible() ) {
		return;
	}
	flash = cg_invokeEffects.integer ? CG_InvokeFlash() : 0;
	CG_FillRect( 16, 72, 186, 196, panel );
	CG_DrawStringExt( 24, 78, "INVOKER", muted, qtrue, qtrue, 6, 10, 0 );
	for ( i = 0; i < INVOKE_SLOTS; i++ ) {
		orb = CG_ValidOrb( cg.orbSlots[i] );
		memcpy( color, orbColors[orb], sizeof( color ) );
		x = 48 + i * 60;
		pulse = ( cg.time - cg.orbChangeTime ) / 300.0f;
		pulse = pulse >= 0 && pulse < 1 && cg_invokeEffects.integer ? 1 - pulse : 0;
		size = 28 + 6 * pulse + 6 * flash;
		trap_R_SetColor( color );
		CG_DrawPic( x - size / 2, 110 - size / 2, size, size, cgs.media.railRingsShader );
		trap_R_SetColor( NULL );
		if ( orb ) {
			// key legend on the ring: D, W and A push Q, W and E
			CG_DrawStringExt( (int)x - 4, 104, va( "%c", BG_OrbLetter( orb ) ),
				white, qtrue, qtrue, 8, 12, 0 );
		}
	}
	inv = BG_FindInvocation( cg.orbSlots );
	text = inv ? va( "READY %s", inv->name ) : "Choose an orb";
	CG_DrawStringExt( 24, 133, text, white, qtrue, qtrue, 6, 10, 0 );
	inv = BG_FindInvocation( cg.invokedSlots );
	text = inv ? va( "CAST %s", inv->name ) : "Invoke to equip";
	CG_DrawStringExt( 24, 145, text, muted, qtrue, qtrue, 6, 10, 0 );

	mana = cg.snap->ps.stats[STAT_INVOKE_MANA];
	if ( mana < 0 ) {
		mana = 0;
	}
	if ( mana > INVOKE_MANA_MAX ) {
		mana = INVOKE_MANA_MAX;
	}
	CG_DrawStringExt( 24, 156, "MANA", muted, qtrue, qtrue, 6, 10, 0 );
	CG_DrawStringExt( 168, 156, va( "%d", mana ), white, qtrue, qtrue, 6, 10, 0 );
	CG_FillRect( 24, 166, 162, 6, barBack );
	if ( mana > 0 ) {
		CG_FillRect( 24, 166, 162 * mana / INVOKE_MANA_MAX, 6, barFill );
	}

	clientNum = cg.snap->ps.clientNum;
	for ( i = 0; i < INVOKE_HANDS; i++ ) {
		spellId = cg.invokeHandSpells[clientNum][i];
		spellDef = BG_SpellDef( spellId );
		ready = 1.0f;
		if ( spellDef && spellDef->cooldown > 0 ) {
			// the readout is keyed by spell, not by hand: two hands holding
			// the same spell share one recharge clock, like the server
			ready = CG_InvokeReadyFraction( cg.time, cg.invokeCastTime[spellId],
				spellDef->cooldown );
		}
		CG_DrawStringExt( 24, 176 + i * 18, CG_InvokeHandLabel(
			i == INVOKE_HAND_LEFT ? "LEFT" : "RIGHT",
			cg.invokeHandWeapons[clientNum][i], spellId, cg.snap->ps.ammo ),
			ready < 1.0f ? muted : white, qtrue, qtrue, 6, 10, 0 );
		if ( ready < 1.0f ) {
			// recharging: a full amber bar drains as the spell comes back.
			// It keeps its own row under the label so longer names fit.
			CG_FillRect( 120, 187 + i * 18, 66, 6, barBack );
			CG_FillRect( 120, 187 + i * 18, 66 * ( 1.0f - ready ), 6, cooldownFill );
		}
	}

	memcpy( keyColor, orbColors[ORB_WEX], sizeof( keyColor ) );
	keyColor[3] = ( cg.invokeMoveKeys & INVOKE_MOVE_W ) ? 0.9f : 0.20f;
	CG_FillRect( 96, 202, 20, 16, keyColor );
	CG_DrawStringExt( 102, 203, "W", white, qtrue, qtrue, 8, 12, 0 );
	memcpy( keyColor, orbColors[ORB_EXORT], sizeof( keyColor ) );
	keyColor[3] = ( cg.invokeMoveKeys & INVOKE_MOVE_A ) ? 0.9f : 0.20f;
	CG_FillRect( 72, 220, 20, 16, keyColor );
	CG_DrawStringExt( 78, 221, "A", white, qtrue, qtrue, 8, 12, 0 );
	keyColor[0] = 0.65f; keyColor[1] = 0.68f; keyColor[2] = 0.72f;
	keyColor[3] = ( cg.invokeMoveKeys & INVOKE_MOVE_S ) ? 0.9f : 0.20f;
	CG_FillRect( 96, 220, 20, 16, keyColor );
	CG_DrawStringExt( 102, 221, "S", white, qtrue, qtrue, 8, 12, 0 );
	memcpy( keyColor, orbColors[ORB_QUAS], sizeof( keyColor ) );
	keyColor[3] = ( cg.invokeMoveKeys & INVOKE_MOVE_D ) ? 0.9f : 0.20f;
	CG_FillRect( 120, 220, 20, 16, keyColor );
	CG_DrawStringExt( 126, 221, "D", white, qtrue, qtrue, 8, 12, 0 );
	CG_DrawStringExt( 24, 241, "R invoke RIGHT  T swap", muted, qtrue, qtrue, 6, 10, 0 );
	CG_DrawStringExt( 24, 252, "M1 LEFT / M2 RIGHT", muted, qtrue, qtrue, 6, 10, 0 );
	if ( flash ) {
		color[0] = 0.65f; color[1] = 0.80f; color[2] = 1.0f; color[3] = flash;
		CG_FillRect( 16, 261, 186 * flash, 2, color );
	}

	// deafened: the burst hit THIS player. Warn under the crosshair with
	// the time left on a bar; nothing else about the lockout is visible.
	if ( cg.invokeDisarmEndTime > cg.time ) {
		const vec4_t warnCol = { 0.98f, 0.62f, 0.20f, 1.0f };
		const vec4_t warnBack = { 0.09f, 0.13f, 0.21f, 0.90f };
		float left = CG_InvokeDisarmFraction();

		CG_DrawStringExt( 272, 268, "WEAPONS DISABLED", warnCol, qtrue, qtrue, 6, 10, 0 );
		CG_FillRect( 268, 281, 104, 5, warnBack );
		CG_FillRect( 268, 281, 104 * left, 5, warnCol );
	}

	// chilled: a Cold Snap will freeze THIS player on the next hit taken,
	// so the debuff runs on the HUD while it holds
	if ( cg.invokeChillEndTime > cg.time ) {
		const vec4_t chillCol = { 0.55f, 0.85f, 1.0f, 1.0f };
		const vec4_t chillBack = { 0.09f, 0.13f, 0.21f, 0.90f };
		float left = CG_InvokeChillFraction();

		CG_DrawStringExt( 299, 292, "CHILLED", chillCol, qtrue, qtrue, 6, 10, 0 );
		CG_FillRect( 268, 305, 104, 5, chillBack );
		CG_FillRect( 268, 305, 104 * left, 5, chillCol );
	}

	// slowed: an Ice Wall field holds THIS player. The drag is felt
	// directly; the bar names the source while it lasts.
	if ( cg.invokeSlowEndTime > cg.time ) {
		const vec4_t slowCol = { 0.55f, 0.85f, 1.0f, 1.0f };
		const vec4_t slowBack = { 0.09f, 0.13f, 0.21f, 0.90f };
		float left = CG_InvokeSlowFraction();

		CG_DrawStringExt( 302, 316, "SLOWED", slowCol, qtrue, qtrue, 6, 10, 0 );
		CG_FillRect( 268, 329, 104, 5, slowBack );
		CG_FillRect( 268, 329, 104 * left, 5, slowCol );
	}

	// alacrity: this player's own haste window. The speed and fire-rate
	// lift is felt directly; the bar names it and shows the time left.
	if ( cg.snap && cg.snap->ps.powerups[PW_HASTE] > cg.time ) {
		const vec4_t alacCol = { 1.0f, 0.83f, 0.25f, 1.0f };
		const vec4_t alacBack = { 0.09f, 0.13f, 0.21f, 0.90f };
		float left = CG_InvokeAlacrityFraction();

		CG_DrawStringExt( 296, 340, "ALACRITY", alacCol, qtrue, qtrue, 6, 10, 0 );
		CG_FillRect( 268, 353, 104, 5, alacBack );
		CG_FillRect( 268, 353, 104 * left, 5, alacCol );
	}
	trap_R_SetColor( NULL );
}
