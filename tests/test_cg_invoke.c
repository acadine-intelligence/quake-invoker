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
static float intensity;
static char hudText[256];

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
	(void)x; (void)y; (void)w; (void)h; (void)color;
}
void CG_DrawStringExt( int x, int y, const char *s, const float *color,
	qboolean force, qboolean shadow, int w, int h, int maxChars ) {
	(void)x; (void)y; (void)s; (void)color; (void)force; (void)shadow;
	(void)w; (void)h; (void)maxChars;
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
static void frame( void ) {
	entityCount = lightCount = hudCount = 0;
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
	frame();
	CHECK( entityCount == 0 && lightCount == 0 && hudCount == 3 );
	CG_SetOrbSlots( -1, ORB_NUM_TYPES, 100000 );
	CHECK( !BG_FindInvocation( cg.orbSlots ) );
	CG_SetOrbSlots( ORB_QUAS, ORB_WEX, ORB_EXORT );
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
	CG_InvokeWeapon( WP_NUM_WEAPONS );
	CHECK( !cg.invokeEffectEndTime );
	CG_InvokeWeapon( WP_RAILGUN );
	frame();
	CHECK( entityCount == 33 && lightCount == 1 && intensity == 160 );
	peakColor = entities[15].shaderRGBA[0];
	CG_SetOrbSlots( ORB_QUAS, ORB_QUAS, ORB_EXORT );
	frame();
	CHECK( strstr( hudText, "READY Frost Rockets" ) && strstr( hudText, "CAST Deafening Blast" ) );
	CHECK( entities[15].shaderRGBA[0] == peakColor );
	cg.time += 300;
	frame();
	CHECK( lightCount == 1 && intensity == 80 && entities[15].shaderRGBA[0] < peakColor );
	cg.time += 300;
	frame();
	CHECK( entityCount == 15 && !lightCount );

	CG_InvokeWeapon( WP_ROCKET_LAUNCHER );
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
	cg.snap = NULL;
	frame();
	CHECK( !entityCount && !hudCount );
	printf( "cgame visuals: %d checks passed\n", checks );
	return 0;
}
