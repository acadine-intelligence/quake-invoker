// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise real cgame presentation with a recording renderer, without a window.
#include "../code/cgame/cg_local.h"
#include <assert.h>

cg_t cg;
cgs_t cgs;
vmCvar_t cg_invokeEffects;
static snapshot_t snapshot;
static refEntity_t entities[64];
static int entityCount, lightCount, hudCount, checks;
static int clientCommandCount;
static int cooldownBars;
static float cooldownFillW;
static float intensity;
static char hudText[256];
static char clientCommands[8][32];
typedef struct { float x, y, w, h; } fillRec_t;
typedef struct { int x, y; char text[64]; } strRec_t;
static fillRec_t fills[96];
static int fillCount;
static strRec_t strs[96];
static int strCount;

#define CHECK(c) do { checks++; assert(c); } while (0)

void trap_R_AddRefEntityToScene( const refEntity_t *ent ) {
	assert( entityCount < 64 );
	entities[entityCount++] = *ent;
}
void trap_R_AddLightToScene( const vec3_t org, float value, float r, float g, float b ) {
	(void)org; (void)r; (void)g; (void)b;
	lightCount++;
	intensity = value;
}
void trap_R_SetColor( const float *rgba ) { (void)rgba; }
void CG_DrawPic( float x, float y, float w, float h, qhandle_t shader ) {
	(void)x; (void)y; (void)w; (void)h; (void)shader;
	hudCount++;
}
void CG_FillRect( float x, float y, float w, float h, const float *color ) {
	(void)color;
	assert( fillCount < 96 );
	fills[fillCount].x = x;
	fills[fillCount].y = y;
	fills[fillCount].w = w;
	fills[fillCount].h = h;
	fillCount++;
	// the HUD's spell recharge bars are the only thin fills at x = 120
	if ( x == 120.0f && h == 6.0f ) {
		cooldownBars++;
		if ( w < 66.0f ) {
			cooldownFillW = w;
		}
	}
}
void CG_DrawStringExt( int x, int y, const char *s, const float *color,
	qboolean force, qboolean shadow, int w, int h, int maxChars ) {
	(void)color; (void)force; (void)shadow;
	(void)w; (void)h; (void)maxChars;
	assert( strCount < 96 );
	strs[strCount].x = x;
	strs[strCount].y = y;
	snprintf( strs[strCount].text, sizeof( strs[strCount].text ), "%s", s );
	strCount++;
	assert( strlen( hudText ) + strlen( s ) + 2 < sizeof( hudText ) );
	strcat( hudText, s );
	strcat( hudText, "\n" );
}
void CG_DrawSmallStringColor( int x, int y, const char *s, vec4_t color ) {
	(void)x; (void)y; (void)color;
	assert( strlen( hudText ) + strlen( s ) + 2 < sizeof( hudText ) );
	strcat( hudText, s );
	strcat( hudText, "\n" );
}
char * QDECL va( char *format, ... ) {
	static char buffer[256];
	va_list args;
	va_start( args, format );
	vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}
void trap_SendClientCommand( const char *command ) {
	assert( clientCommandCount < 8 );
	snprintf( clientCommands[clientCommandCount++], sizeof( clientCommands[0] ),
		"%s", command );
}
static int fills_at( float x, float y ) {
	int i, n = 0;
	for ( i = 0; i < fillCount; i++ ) {
		if ( fills[i].x == x && fills[i].y == y ) {
			n++;
		}
	}
	return n;
}

static int str_x_at( const char *s ) {
	int i;
	for ( i = 0; i < strCount; i++ ) {
		if ( !strcmp( strs[i].text, s ) ) {
			return strs[i].x;
		}
	}
	return -1;
}

static void frame( void ) {
	entityCount = lightCount = hudCount = 0;
	cooldownBars = 0;
	cooldownFillW = 0;
	fillCount = strCount = 0;
	hudText[0] = '\0';
	CG_AddInvokeEffects();
	CG_DrawOrbs();
}

int main( void ) {
	int i;
	byte peakColor;
	vec3_t firstOrigin;

	cg.time = 1000;
	cg.snap = &snapshot;
	cg.predictedPlayerState.stats[STAT_HEALTH] = 100;
	cg.predictedPlayerState.pm_type = PM_NORMAL;
	cgs.media.railRingsShader = 1;
	cg_invokeEffects.integer = 1;
	for ( i = 0; i < 3; i++ ) cg.refdef.viewaxis[i][i] = 1;

	CG_ResetInvokeEffects();
	CHECK( cg.invokeMoveKeys == 0 );
	CG_InvokeMovementKey( INVOKE_MOVE_W, qtrue );
	CG_InvokeMovementKey( INVOKE_MOVE_W, qtrue );
	CHECK( clientCommandCount == 1 && !strcmp( clientCommands[0], "orb w" ) );
	CG_InvokeMovementKey( INVOKE_MOVE_A, qtrue );
	CG_InvokeMovementKey( INVOKE_MOVE_D, qtrue );
	CHECK( cg.invokeMoveKeys == ( INVOKE_MOVE_W | INVOKE_MOVE_A | INVOKE_MOVE_D )
		&& clientCommandCount == 3
		&& !strcmp( clientCommands[1], "orb e" )
		&& !strcmp( clientCommands[2], "orb q" ) );
	CG_InvokeMovementKey( INVOKE_MOVE_D, qfalse );
	CG_InvokeMovementKey( INVOKE_MOVE_D, qtrue );
	CHECK( clientCommandCount == 4 && !strcmp( clientCommands[3], "orb q" ) );
	CG_InvokeMovementKey( INVOKE_MOVE_S, qtrue );
	CHECK( clientCommandCount == 4 && ( cg.invokeMoveKeys & INVOKE_MOVE_S ) );
	CG_InvokeMovementKey( 0x4000, qtrue );
	CHECK( clientCommandCount == 4 );
	CG_ResetInvokeEffects();
	CG_InvokeMovementKey( INVOKE_MOVE_W, qtrue );
	CHECK( clientCommandCount == 4 && ( cg.invokeMoveKeys & INVOKE_MOVE_W ) );

	CG_SetInvokeHands( -1, WP_SHOTGUN, WP_ROCKET_LAUNCHER );
	CG_SetInvokeHands( 0, WP_SHOTGUN, WP_ROCKET_LAUNCHER );
	CHECK( cg.invokeHandWeapons[0][INVOKE_HAND_LEFT] == WP_SHOTGUN
		&& cg.invokeHandWeapons[0][INVOKE_HAND_RIGHT] == WP_ROCKET_LAUNCHER );
	CG_SetInvokeHands( 0, -7, WP_NUM_WEAPONS );
	CHECK( cg.invokeHandWeapons[0][INVOKE_HAND_LEFT] == WP_NONE
		&& cg.invokeHandWeapons[0][INVOKE_HAND_RIGHT] == WP_NONE );
	CG_SetInvokeHands( 0, WP_SHOTGUN, WP_ROCKET_LAUNCHER );
	CG_SetInvokeSpells( -1, SPELL_GHOST_WALK, SPELL_SUNSTRIKE );
	CG_SetInvokeSpells( 0, SPELL_GHOST_WALK, SPELL_SUNSTRIKE );
	CHECK( cg.invokeHandSpells[0][INVOKE_HAND_LEFT] == SPELL_GHOST_WALK
		&& cg.invokeHandSpells[0][INVOKE_HAND_RIGHT] == SPELL_SUNSTRIKE );
	CG_SetInvokeSpells( 0, -7, SPELL_NUM );
	CHECK( cg.invokeHandSpells[0][INVOKE_HAND_LEFT] == SPELL_NONE
		&& cg.invokeHandSpells[0][INVOKE_HAND_RIGHT] == SPELL_NONE );
	frame();
	CHECK( entityCount == 0 && lightCount == 0 && hudCount == 3 );
	CG_SetOrbSlots( -1, ORB_NUM_TYPES, 100000 );
	CHECK( !BG_FindInvocation( cg.orbSlots ) );
	// W,Q,E is the ordered Grenade Launcher recipe: a castable weapon whose
	// slot colors satisfy the per-slot color checks below.
	CG_SetOrbSlots( ORB_WEX, ORB_QUAS, ORB_EXORT );
	CG_SetInvokeHands( 0, WP_SHOTGUN, WP_ROCKET_LAUNCHER );
	frame();
	CHECK( entityCount == 15 && !lightCount );
	CHECK( entities[0].shaderRGBA[2] > entities[0].shaderRGBA[0] );
	CHECK( entities[5].shaderRGBA[2] > entities[5].shaderRGBA[1] );
	CHECK( entities[10].shaderRGBA[0] > entities[10].shaderRGBA[2] );
	VectorCopy( entities[0].origin, firstOrigin );
	cg.time += 100;
	frame();
	CHECK( memcmp( firstOrigin, entities[0].origin, sizeof( firstOrigin ) ) );
	for ( i = 0; i < entityCount; i++ ) {
		CHECK( entities[i].reType == RT_SPRITE && entities[i].radius > 0 );
		CHECK( isfinite( entities[i].origin[0] ) && isfinite( entities[i].origin[1] ) );
	}
	CG_InvokeWeapon( -1, WP_RAILGUN, SPELL_NONE );
	CG_InvokeWeapon( INVOKE_HAND_RIGHT, WP_NUM_WEAPONS, SPELL_NONE );
	CHECK( !cg.invokeEffectEndTime );
	CG_InvokeWeapon( INVOKE_HAND_RIGHT, WP_NONE, SPELL_SUNSTRIKE );
	CHECK( !cg.invokeEffectEndTime );
	CG_InvokeWeapon( INVOKE_HAND_RIGHT, WP_GRENADE_LAUNCHER, SPELL_NONE );
	frame();
	CHECK( entityCount == 33 && lightCount == 1 && intensity == 160 );
	peakColor = entities[15].shaderRGBA[0];
	CG_SetOrbSlots( ORB_WEX, ORB_QUAS, ORB_WEX );
	frame();
	CHECK( strstr( hudText, "READY Rocket Launcher" ) && strstr( hudText, "CAST Grenade Launcher" ) );
	CHECK( strstr( hudText, "LEFT Shotgun" ) && strstr( hudText, "RIGHT Rocket Launcher" )
		&& strstr( hudText, "R invoke RIGHT" ) );
	CHECK( strstr( hudText, "W\n" ) && strstr( hudText, "A\n" )
		&& strstr( hudText, "S\n" ) && strstr( hudText, "D\n" ) );
	cg.snap->ps.ammo[WP_SHOTGUN] = 7;
	cg.snap->ps.ammo[WP_ROCKET_LAUNCHER] = 9;
	frame();
	CHECK( strstr( hudText, "LEFT Shotgun [7]" )
		&& strstr( hudText, "RIGHT Rocket Launcher [9]" ) );
	cg.snap->ps.stats[STAT_INVOKE_MANA] = 87;
	CG_SetInvokeSpells( 0, SPELL_NONE, SPELL_GHOST_WALK );
	frame();
	CHECK( strstr( hudText, "MANA" ) && strstr( hudText, "87" ) );
	CHECK( strstr( hudText, "RIGHT Ghost Walk" ) && strstr( hudText, "LEFT Shotgun [7]" ) );
	CG_SetInvokeSpells( 0, SPELL_NONE, SPELL_NONE );
	CHECK( entities[15].shaderRGBA[0] == peakColor );
	cg.time += 300;
	frame();
	CHECK( lightCount == 1 && intensity == 80 && entities[15].shaderRGBA[0] < peakColor );
	cg.time += 300;
	frame();
	CHECK( entityCount == 15 && !lightCount );

	CG_InvokeWeapon( INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, SPELL_NONE );
	cg_invokeEffects.integer = 0;
	frame();
	CHECK( !entityCount && !lightCount && hudCount == 3 );
	cg_invokeEffects.integer = 1;
	cg.renderingThirdPerson = qtrue;
	frame();
	CHECK( !entityCount && hudCount == 3 );
	cg.renderingThirdPerson = qfalse;
	cg.predictedPlayerState.stats[STAT_HEALTH] = 0;
	frame();
	CHECK( !entityCount && !hudCount );
	cg.predictedPlayerState.stats[STAT_HEALTH] = 100;
	cg.predictedPlayerState.persistant[PERS_TEAM] = TEAM_SPECTATOR;
	frame();
	CHECK( !entityCount && !hudCount );
	cg.predictedPlayerState.persistant[PERS_TEAM] = TEAM_FREE;
	cg.predictedPlayerState.pm_flags = PMF_FOLLOW;
	frame();
	CHECK( !entityCount && !hudCount );
	cg.predictedPlayerState.pm_flags = 0;
	cg.snap->ps.clientNum = 1;
	frame();
	CHECK( !entityCount && !hudCount );
	cg.snap->ps.clientNum = 0;
	cg.intermissionStarted = qtrue;
	frame();
	CHECK( !entityCount && !hudCount );
	cg.intermissionStarted = qfalse;
	CG_SetOrbSlots( 0, 0, 0 );
	frame();
	CHECK( !cg.invokeEffectEndTime && !BG_FindInvocation( cg.invokedSlots ) );
	CHECK( !entityCount && !lightCount );

	// a spell invoke flashes the same effect, keyed by spell ID
	cg.time += 1000;
	CG_SetOrbSlots( ORB_QUAS, ORB_QUAS, ORB_WEX );	// Q,Q,W = Ghost Walk
	CG_InvokeWeapon( INVOKE_HAND_RIGHT, WP_NONE, SPELL_SUNSTRIKE );
	frame();
	CHECK( entityCount == 15 && !lightCount );		// wrong spell ID draws no flash
	CG_InvokeWeapon( INVOKE_HAND_RIGHT, WP_NONE, SPELL_GHOST_WALK );
	CHECK( cg.invokeEffectEndTime > cg.time );
	frame();
	CHECK( entityCount == 33 && lightCount == 1 );	// three trails plus the flash
	CG_ResetInvokeEffects();

	// the sunstrike beam draws for its short lifetime, from cg state only
	{
		vec3_t sky = { 0, 0, 512 }, ground = { 0, 0, 0 };

		CG_InvokeStrikeBeam( sky, ground );
		frame();
		CHECK( entityCount == 1 && lightCount == 0 );
		CHECK( entities[0].reType == RT_LIGHTNING );
		cg.time += 700;
		frame();
		CHECK( !entityCount );
	}

	// the cast confirmation drives the HUD recharge readout
	CG_ResetInvokeEffects();
	CHECK( cg.invokeCastTime[SPELL_GHOST_WALK] == 0 );
	CHECK( CG_InvokeReadyFraction( cg.time, 0, 18000 ) == 1.0f );
	CG_InvokeSpellCast( -1, SPELL_GHOST_WALK );
	CG_InvokeSpellCast( INVOKE_HAND_LEFT, SPELL_NUM );
	CHECK( cg.invokeCastTime[SPELL_GHOST_WALK] == 0 );
	CG_SetInvokeHands( 0, WP_SHOTGUN, WP_NONE );
	CG_SetInvokeSpells( 0, SPELL_NONE, SPELL_GHOST_WALK );
	cg.snap->ps.ammo[WP_SHOTGUN] = 5;
	CG_InvokeSpellCast( INVOKE_HAND_RIGHT, SPELL_GHOST_WALK );
	CHECK( cg.invokeCastTime[SPELL_GHOST_WALK] == cg.time );
	CHECK( CG_InvokeReadyFraction( cg.time, cg.invokeCastTime[SPELL_GHOST_WALK],
		18000 ) == 0.0f );
	CHECK( CG_InvokeReadyFraction( cg.time + 9000, cg.invokeCastTime[SPELL_GHOST_WALK],
		18000 ) == 0.5f );
	CHECK( CG_InvokeReadyFraction( cg.time + 18000, cg.invokeCastTime[SPELL_GHOST_WALK],
		18000 ) == 1.0f );
	CHECK( CG_InvokeReadyFraction( cg.time + 1000, cg.invokeCastTime[SPELL_GHOST_WALK],
		0 ) == 1.0f );
	cg.time += 9000;
	frame();
	CHECK( cooldownBars == 2 );			// back bar plus a half-drained bar
	CHECK( cooldownFillW > 32.0f && cooldownFillW < 34.0f );
	CHECK( strstr( hudText, "RIGHT Ghost Walk" ) && strstr( hudText, "LEFT Shotgun [5]" ) );
	cg.time += 9000;
	frame();
	CHECK( !cooldownBars );				// ready again: no bar

	// switching a hand to a spell that was never cast shows it ready
	CG_SetInvokeSpells( 0, SPELL_NONE, SPELL_SUNSTRIKE );
	cg.time += 1;
	frame();
	CHECK( !cooldownBars );

	// two hands holding the same spell share one recharge clock
	CG_SetInvokeSpells( 0, SPELL_GHOST_WALK, SPELL_GHOST_WALK );
	CG_InvokeSpellCast( INVOKE_HAND_LEFT, SPELL_GHOST_WALK );
	CHECK( cg.invokeCastTime[SPELL_GHOST_WALK] == cg.time );
	frame();
	CHECK( cooldownBars == 4 );			// back bar plus drain on both hands
	CG_ResetInvokeEffects();
	CHECK( cg.invokeCastTime[SPELL_GHOST_WALK] == 0 );

	// the emp charge ring draws from cg state until it bursts
	{
		vec3_t here = { 64, 0, 0 };
		float ringRadius;

		CG_ResetInvokeEffects();
		CG_InvokeEmpCharge( 0, here, 2500 );
		frame();
		CHECK( entityCount == EMP_RING_STEPS && lightCount == 1 );	// ring plus charge light
		CHECK( entities[0].reType == RT_SPRITE && entities[1].reType == RT_SPRITE );
		CHECK( entities[0].origin[0] != entities[1].origin[0] );
		ringRadius = entities[0].radius;
		cg.time += 2000;
		frame();
		CHECK( entityCount == EMP_RING_STEPS && entities[0].radius > ringRadius );
		cg.time += 600;
		frame();
		CHECK( !entityCount && !lightCount );
	}

	// a retracted charge stops drawing at once
	{
		vec3_t here = { 128, 0, 0 };

		CG_ResetInvokeEffects();
		CHECK( cg.invokeEmpCaster == -1 );
		CG_InvokeEmpCharge( 0, here, 2500 );
		CHECK( cg.invokeEmpCaster == 0 );
		frame();
		CHECK( entityCount > 0 && lightCount == 1 );
		CG_InvokeEmpCancel( 0 );
		CHECK( cg.invokeEmpCaster == -1 );
		frame();
		CHECK( !entityCount && !lightCount );
	}

	// a cancel only takes down the ring of the caster it names
	{
		vec3_t here = { 128, 0, 0 };
		vec3_t there = { -128, 0, 0 };

		CG_ResetInvokeEffects();
		CG_InvokeEmpCharge( 0, here, 2500 );
		CG_InvokeEmpCharge( 1, there, 2500 );	// caster 1's ring is on screen
		CHECK( cg.invokeEmpCaster == 1 );
		frame();
		CHECK( entityCount == EMP_RING_STEPS && lightCount == 1 );
		CHECK( entities[0].origin[0] < 0 );
		CG_InvokeEmpCancel( 0 );	// caster 0 dies; that ring is not on screen
		CHECK( cg.invokeEmpCaster == 1 );
		frame();
		CHECK( entityCount == EMP_RING_STEPS && lightCount == 1 );
		CG_InvokeEmpCancel( 1 );
		CHECK( cg.invokeEmpCaster == -1 );
		frame();
		CHECK( !entityCount && !lightCount );
	}

	// a cancel that arrives after the window expired is ignored entirely
	{
		vec3_t here = { 64, 0, 0 };

		CG_ResetInvokeEffects();
		CG_InvokeEmpCharge( 2, here, 500 );
		cg.time += 600;
		CG_InvokeEmpCancel( 2 );
		frame();
		CHECK( !entityCount && cg.invokeEmpCaster == 2 );	// nothing was on screen to take down
	}

	// the victim-facing disarm window drains, draws, and clears
	{
		CG_ResetInvokeEffects();
		CHECK( CG_InvokeDisarmFraction() == 0.0f );
		CG_InvokeDisarm( 3000 );
		CHECK( CG_InvokeDisarmFraction() > 0.9f );
		frame();
		CHECK( strstr( hudText, "WEAPONS DISABLED" ) != NULL );
		CHECK( fills_at( 268, 281 ) >= 2 );	// bar back + bar fill
		CHECK( str_x_at( "WEAPONS DISABLED" ) == 272 );	// 16 chars x 6 px, centered
		cg.time += 1500;
		CHECK( CG_InvokeDisarmFraction() > 0.4f && CG_InvokeDisarmFraction() < 0.6f );
		cg.time += 1600;
		frame();
		CHECK( CG_InvokeDisarmFraction() == 0.0f );
		CHECK( strstr( hudText, "WEAPONS DISABLED" ) == NULL );
		CHECK( fills_at( 268, 281 ) == 0 && str_x_at( "WEAPONS DISABLED" ) == -1 );
		CHECK( fills_at( 268, 305 ) == 0 && fills_at( 268, 329 ) == 0 );

		// the reset path must clear a live window, not just a fresh one
		CG_InvokeDisarm( 3000 );
		CHECK( cg.invokeDisarmEndTime > cg.time );
		CG_ResetInvokeEffects();
		CHECK( cg.invokeDisarmEndTime == 0 && CG_InvokeDisarmFraction() == 0.0f );

		// the received value drives the window: a shorter lock expires sooner
		CG_InvokeDisarm( 1000 );
		CHECK( CG_InvokeDisarmFraction() > 0.0f );
		cg.time += 1100;
		CHECK( CG_InvokeDisarmFraction() == 0.0f );
	}

	// the chilled window drains, draws, and clears like the disarm bar
	{
		CG_ResetInvokeEffects();
		CHECK( CG_InvokeChillFraction() == 0.0f );
		CG_InvokeChill( 5000 );
		CHECK( CG_InvokeChillFraction() > 0.9f );
		frame();
		CHECK( strstr( hudText, "CHILLED" ) != NULL );
		CHECK( fills_at( 268, 305 ) >= 2 );
		CHECK( str_x_at( "CHILLED" ) == 299 );	// 7 chars x 6 px, centered
		cg.time += 2500;
		CHECK( CG_InvokeChillFraction() > 0.4f && CG_InvokeChillFraction() < 0.6f );
		cg.time += 2600;
		frame();
		CHECK( CG_InvokeChillFraction() == 0.0f );
		CHECK( strstr( hudText, "CHILLED" ) == NULL );
		CHECK( fills_at( 268, 305 ) == 0 && str_x_at( "CHILLED" ) == -1 );
		CHECK( fills_at( 268, 281 ) == 0 && fills_at( 268, 329 ) == 0 );

		// the reset path must clear a live debuff, not just a fresh one
		CG_InvokeChill( 2000 );
		CHECK( cg.invokeChillEndTime > cg.time );
		CG_ResetInvokeEffects();
		CHECK( cg.invokeChillEndTime == 0 && CG_InvokeChillFraction() == 0.0f );

		// a non-default duration expires on its own clock
		CG_InvokeChill( 1000 );
		CHECK( CG_InvokeChillFraction() > 0.0f );
		cg.time += 1100;
		CHECK( CG_InvokeChillFraction() == 0.0f );
	}

	// the deafening blast rings draw from cg state until the wave fades
	{
		vec3_t here = { 96, 0, 0 };

		CG_ResetInvokeEffects();
		CG_InvokeDeafenBurst( here );
		frame();
		CHECK( entityCount == EMP_RING_STEPS && lightCount == 1 );
		CHECK( entities[0].reType == RT_SPRITE && entities[0].origin[2] > here[2] );
		cg.time += DEAFEN_BURST_MSEC / 2;
		frame();
		CHECK( entityCount == 2 * EMP_RING_STEPS && lightCount == 1 );
		cg.time += DEAFEN_BURST_MSEC / 2 + 1;
		frame();
		CHECK( !entityCount && !lightCount );
	}

	// the tornado and blast missiles draw from their entity marker
	{
		centity_t cent;

		memset( &cent, 0, sizeof( cent ) );
		cent.lerpOrigin[0] = 80;
		cent.lerpOrigin[1] = 16;
		cent.lerpOrigin[2] = 40;
		CG_ResetInvokeEffects();
		CG_InvokeTornado( &cent );
		CHECK( entityCount == TORNADO_SPRITES && lightCount == 1 );
		CHECK( entities[0].reType == RT_SPRITE && entities[0].radius > 0 );
		CHECK( entities[0].origin[0] != entities[1].origin[0] );
		entityCount = lightCount = 0;
		cent.currentState.pos.trDelta[0] = 620;	// the marker's flight direction
		CG_InvokeBlastMissile( &cent );
		CHECK( entityCount == 4 && lightCount == 1 );
	}

	// the invoke marker requires our weapon: team values (missionpack
	// prox mines ride generic1 as 1/2) must never claim the effects
	{
		entityState_t st;

		memset( &st, 0, sizeof( st ) );
		st.weapon = WP_ROCKET_LAUNCHER;
		st.generic1 = INVOKE_FX_TORNADO;
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_TORNADO ) == qtrue );
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_DEAFENING ) == qfalse );
		st.generic1 = INVOKE_FX_DEAFENING;
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_DEAFENING ) == qtrue );
		st.weapon = 0;	// a prox mine's team rides generic1
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_DEAFENING ) == qfalse );
		st.weapon = WP_ROCKET_LAUNCHER;
		st.generic1 = 0;	// a stock rocket
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_TORNADO ) == qfalse );
		st.generic1 = INVOKE_FX_SPIRIT_BOLT;
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_SPIRIT_BOLT ) == qtrue );
		st.weapon = WP_NONE;	// a spawn that forgets the weapon renders nothing
		st.generic1 = INVOKE_FX_TORNADO;
		CHECK( CG_InvokeMissileMarker( &st, INVOKE_FX_TORNADO ) == qfalse );
	}

	// the slowed window drains, draws, and clears like its siblings
	{
		CG_ResetInvokeEffects();
		CHECK( CG_InvokeSlowFraction() == 0.0f );
		CG_InvokeSlow( 600 );
		CHECK( CG_InvokeSlowFraction() > 0.9f );
		frame();
		CHECK( strstr( hudText, "SLOWED" ) != NULL );
		CHECK( fills_at( 268, 329 ) >= 2 );
		CHECK( str_x_at( "SLOWED" ) == 302 );	// 6 chars x 6 px, centered
		cg.time += 300;
		CHECK( CG_InvokeSlowFraction() > 0.4f && CG_InvokeSlowFraction() < 0.6f );
		cg.time += 301;
		frame();
		CHECK( CG_InvokeSlowFraction() == 0.0f );
		CHECK( strstr( hudText, "SLOWED" ) == NULL );
		CHECK( fills_at( 268, 329 ) == 0 && str_x_at( "SLOWED" ) == -1 );
		CHECK( fills_at( 268, 281 ) == 0 && fills_at( 268, 305 ) == 0 );

		// the reset path must clear a live slow, not just a fresh one
		CG_InvokeSlow( 1500 );
		CHECK( cg.invokeSlowEndTime > cg.time );
		CG_ResetInvokeEffects();
		CHECK( cg.invokeSlowEndTime == 0 && CG_InvokeSlowFraction() == 0.0f );

		// every notice refreshes the window: a later one outlives the first
		CG_InvokeSlow( 600 );
		cg.time += 400;
		CG_InvokeSlow( 600 );
		cg.time += 400;
		CHECK( CG_InvokeSlowFraction() > 0.0f );
	}

	// the ice wall field draws its whole ring from the entity marker
	{
		centity_t cent;

		memset( &cent, 0, sizeof( cent ) );
		cent.lerpOrigin[0] = 208;
		cent.lerpOrigin[1] = -32;
		cent.lerpOrigin[2] = 24;
		CG_ResetInvokeEffects();
		CG_InvokeIceField( &cent );
		CHECK( entityCount == ICE_FIELD_OUTER_STEPS + ICE_FIELD_INNER_STEPS
			&& lightCount == 1 );
		CHECK( entities[0].reType == RT_SPRITE && entities[0].radius > 0 );
		CHECK( entities[0].origin[0] != entities[1].origin[0] );
	}

	// the alacrity window reads the player state and draws a named bar
	{
		CG_ResetInvokeEffects();
		cg.snap->ps.powerups[PW_HASTE] = 0;
		frame();
		CHECK( CG_InvokeAlacrityFraction() == 0.0f );
		CHECK( fills_at( 268, 353 ) == 0 && str_x_at( "ALACRITY" ) == -1 );
		cg.snap->ps.powerups[PW_HASTE] = cg.time + ALACRITY_MS;
		CHECK( CG_InvokeAlacrityFraction() > 0.9f );
		frame();
		CHECK( strstr( hudText, "ALACRITY" ) != NULL );
		CHECK( fills_at( 268, 353 ) >= 2 );	// bar back + bar fill
		CHECK( str_x_at( "ALACRITY" ) == 296 );	// 8 chars x 6 px, centered
		cg.time += ALACRITY_MS / 2;
		CHECK( CG_InvokeAlacrityFraction() > 0.4f && CG_InvokeAlacrityFraction() < 0.6f );
		cg.time += ALACRITY_MS / 2;
		frame();
		CHECK( CG_InvokeAlacrityFraction() == 0.0f );
		CHECK( strstr( hudText, "ALACRITY" ) == NULL );
		CHECK( fills_at( 268, 353 ) == 0 && str_x_at( "ALACRITY" ) == -1 );

		// a window longer than ALACRITY_MS (a Speed item) clamps to full
		cg.snap->ps.powerups[PW_HASTE] = cg.time + 2 * ALACRITY_MS;
		CHECK( CG_InvokeAlacrityFraction() == 1.0f );
		cg.snap->ps.powerups[PW_HASTE] = 0;
	}

	// the forge spirit and its bolt draw from their markers' budgets
	{
		centity_t cent;

		memset( &cent, 0, sizeof( cent ) );
		cent.lerpOrigin[0] = 128;
		cent.lerpOrigin[1] = 64;
		cent.lerpOrigin[2] = 40;
		CG_InvokeForgeSpirit( &cent );
		CHECK( entityCount == FORGE_SPIRIT_STEPS );
		CHECK( lightCount == 1 );
		entityCount = lightCount = 0;

		cent.currentState.pos.trDelta[0] = 900;	// the bolt's flight direction
		CG_InvokeSpiritBolt( &cent );
		CHECK( entityCount == FORGE_BOLT_STEPS );
		CHECK( lightCount == 1 );
		entityCount = lightCount = 0;
	}
	cg.snap = NULL;
	frame();
	CHECK( !entityCount && !hudCount );
	printf( "cgame visuals: %d checks passed\n", checks );
	return 0;
}
