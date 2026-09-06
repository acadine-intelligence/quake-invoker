// Host-side unit test for bg_invoke.c (no engine needed).
// Build + run:  make -C tests   (or see tests/Makefile)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#include "qi_host_shim.h"
#include "bg_invoke.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } else { printf("ok:   %s\n", msg); } } while (0)

static void reset( int s[INVOKE_SLOTS] ) { s[0] = s[1] = s[2] = ORB_NONE; }

int main( void ) {
	int s[INVOKE_SLOTS];
	const invocation_t *inv;

	reset( s );
	CHECK( BG_FindInvocation( s ) == NULL, "empty slots invoke nothing" );

	BG_PushOrb( s, ORB_QUAS );
	CHECK( s[0] == ORB_QUAS && s[1] == ORB_NONE, "first orb fills slot 0" );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "QQQ" ), "single Q fills to QQQ" );

	BG_PushOrb( s, ORB_QUAS );
	BG_PushOrb( s, ORB_EXORT );
	CHECK( s[0] == ORB_QUAS && s[1] == ORB_QUAS && s[2] == ORB_EXORT, "Q,Q,E fills in order" );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "QQE" ) && inv->weapon == WP_ROCKET_LAUNCHER, "QQE -> Frost Rockets" );

	BG_PushOrb( s, ORB_WEX );
	CHECK( s[0] == ORB_QUAS && s[1] == ORB_EXORT && s[2] == ORB_WEX, "4th orb drops the oldest" );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "QWE" ), "Q,E,W is order independent -> QWE" );

	reset( s );
	BG_PushOrb( s, ORB_WEX ); BG_PushOrb( s, ORB_WEX ); BG_PushOrb( s, ORB_EXORT );
	inv = BG_FindInvocation( s );
	CHECK( inv && inv->weapon == WP_LIGHTNING, "WWE -> Chaos Lightning (lightning gun)" );

	{
		int i, j, matched = 0, seen[16] = {0};
		// every one of the 10 multisets must be reachable and unique
		for ( i = 0; i < bg_numInvocations; i++ ) {
			for ( j = 0; j < i; j++ ) {
				if ( !memcmp( bg_invocations[i].counts, bg_invocations[j].counts, sizeof( bg_invocations[i].counts ) ) ) {
					printf( "FAIL: duplicate combo %s / %s\n", bg_invocations[i].combo, bg_invocations[j].combo ); fails++;
				}
			}
			if ( bg_invocations[i].counts[1] + bg_invocations[i].counts[2] + bg_invocations[i].counts[3] == 3 ) matched++;
			(void)seen;
		}
		CHECK( bg_numInvocations == 10 && matched == 10, "table has 10 distinct three-orb combos" );
	}

	CHECK( BG_OrbFromString( "q" ) == ORB_QUAS && BG_OrbFromString( "E" ) == ORB_EXORT && BG_OrbFromString( "x" ) == ORB_NONE
		&& BG_OrbFromString( "qq" ) == ORB_NONE, "orb parsing" );

	printf( "%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails );
	return fails ? 1 : 0;
}
