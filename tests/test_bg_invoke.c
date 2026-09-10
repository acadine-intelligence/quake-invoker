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
	invokeHands_t hands;
	int ammo[WP_NUM_WEAPONS] = {0};
	int weaponBits = 0;
	int firedWeapon = WP_NONE;

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

	BG_InvokeHandsReset( &hands );
	CHECK( BG_InvokeHandWeapon( &hands, INVOKE_HAND_LEFT ) == WP_NONE
		&& BG_InvokeHandWeapon( &hands, INVOKE_HAND_RIGHT ) == WP_NONE,
		"both hands reset empty" );
	CHECK( !BG_InvokeEquipHand( &hands, -1, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& !BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_NUM_WEAPONS, 15, ammo, &weaponBits ),
		"invalid hand and weapon IDs are rejected" );
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& hands.weapon[INVOKE_HAND_RIGHT] == WP_ROCKET_LAUNCHER
		&& ammo[WP_ROCKET_LAUNCHER] == 15
		&& ( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) ),
		"invoke equips right hand and grants initial ammo" );
	ammo[WP_ROCKET_LAUNCHER] = 9;
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& ammo[WP_ROCKET_LAUNCHER] == 9,
		"reinvoking never refills ammo" );
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_LEFT, WP_SHOTGUN, 15, ammo, &weaponBits ),
		"left hand accepts a different stock weapon" );
	BG_InvokeSwapHands( &hands );
	CHECK( hands.weapon[INVOKE_HAND_LEFT] == WP_ROCKET_LAUNCHER
		&& hands.weapon[INVOKE_HAND_RIGHT] == WP_SHOTGUN,
		"swap exchanges both hand slots" );
	CHECK( BG_InvokePackedHands( &hands ) == ( WP_ROCKET_LAUNCHER | ( WP_SHOTGUN << 4 ) ),
		"hand assignments pack into predicted player state" );
	hands.weapon[INVOKE_HAND_RIGHT] = WP_NONE;
	BG_InvokeSwapHands( &hands );
	CHECK( hands.weapon[INVOKE_HAND_LEFT] == WP_NONE
		&& hands.weapon[INVOKE_HAND_RIGHT] == WP_ROCKET_LAUNCHER,
		"swap preserves an empty slot" );

	CHECK( BG_InvokeTryFire( &hands, INVOKE_HAND_RIGHT, 1000,
		BG_InvokeWeaponCooldown( WP_ROCKET_LAUNCHER ), ammo, &firedWeapon ) == INVOKE_FIRE_OK
		&& firedWeapon == WP_ROCKET_LAUNCHER && ammo[WP_ROCKET_LAUNCHER] == 8,
		"successful fire consumes one ammo" );
	CHECK( BG_InvokeTryFire( &hands, INVOKE_HAND_RIGHT, 1799,
		BG_InvokeWeaponCooldown( WP_ROCKET_LAUNCHER ), ammo, &firedWeapon ) == INVOKE_FIRE_COOLDOWN
		&& ammo[WP_ROCKET_LAUNCHER] == 8,
		"weapon cooldown blocks early refire without consuming ammo" );
	CHECK( BG_InvokeTryFire( &hands, INVOKE_HAND_RIGHT, 1800,
		BG_InvokeWeaponCooldown( WP_ROCKET_LAUNCHER ), ammo, &firedWeapon ) == INVOKE_FIRE_OK
		&& ammo[WP_ROCKET_LAUNCHER] == 7,
		"weapon fires exactly when cooldown expires" );
	hands.weapon[INVOKE_HAND_LEFT] = WP_ROCKET_LAUNCHER;
	CHECK( BG_InvokeTryFire( &hands, INVOKE_HAND_LEFT, 1800,
		BG_InvokeWeaponCooldown( WP_ROCKET_LAUNCHER ), ammo, &firedWeapon ) == INVOKE_FIRE_COOLDOWN,
		"same weapon in both hands shares its cooldown" );
	BG_InvokeSwapHands( &hands );
	CHECK( BG_InvokeTryFire( &hands, INVOKE_HAND_LEFT, 1801,
		BG_InvokeWeaponCooldown( WP_ROCKET_LAUNCHER ), ammo, &firedWeapon ) == INVOKE_FIRE_COOLDOWN,
		"swapping does not reset cooldown" );
	hands.nextFireTime[WP_ROCKET_LAUNCHER] = 0;
	ammo[WP_ROCKET_LAUNCHER] = 0;
	CHECK( BG_InvokeTryFire( &hands, INVOKE_HAND_LEFT, 2000,
		BG_InvokeWeaponCooldown( WP_ROCKET_LAUNCHER ), ammo, &firedWeapon ) == INVOKE_FIRE_NO_AMMO,
		"empty weapon cannot fire" );
	CHECK( BG_InvokeTryFire( &hands, -1, 2000, 100, ammo, &firedWeapon ) == INVOKE_FIRE_INVALID
		&& BG_InvokeTryFire( &hands, INVOKE_HAND_RIGHT, 2000, 0, ammo, &firedWeapon ) == INVOKE_FIRE_INVALID,
		"invalid firing input is rejected" );

	// replacing a hand releases the old weapon once no hand holds it
	BG_InvokeHandsReset( &hands );
	weaponBits = 0;
	ammo[WP_ROCKET_LAUNCHER] = 0;
	ammo[WP_PLASMAGUN] = 0;
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& ( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) ) && ammo[WP_ROCKET_LAUNCHER] == 15,
		"grant sets the weapon bit and starting ammo" );
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_PLASMAGUN, 60, ammo, &weaponBits )
		&& !( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) ) && ammo[WP_ROCKET_LAUNCHER] == 0,
		"replacing a hand releases the old weapon" );
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_LEFT, WP_SHOTGUN, 15, ammo, &weaponBits )
		&& BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& ( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) )
		&& BG_InvokeEquipHand( &hands, INVOKE_HAND_LEFT, WP_BFG, 10, ammo, &weaponBits )
		&& ( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) ),
		"a weapon survives while the other hand still holds it" );
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_LEFT, WP_MACHINEGUN, 100, ammo, &weaponBits )
		&& BG_InvokeEquipHand( &hands, INVOKE_HAND_LEFT, WP_SHOTGUN, 15, ammo, &weaponBits )
		&& ( weaponBits & ( 1 << WP_MACHINEGUN ) ),
		"starting weapons are never removed" );
	{
		int w, packs = 1;

		for ( w = WP_NONE + 1; w < WP_NUM_WEAPONS; w++ ) {
			BG_InvokeHandsReset( &hands );
			weaponBits = 0;
			BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, w, -1, ammo, &weaponBits );
			if ( BG_InvokePackedHands( &hands ) != ( w << 4 )
				|| ( BG_InvokePackedHands( &hands ) >> 4 ) != w ) {
				packs = 0;
			}
		}
		CHECK( packs, "every weapon packs into the hand byte" );
	}

	printf( "%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails );
	return fails ? 1 : 0;
}
