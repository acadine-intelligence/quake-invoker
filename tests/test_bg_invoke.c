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
	int castSpell = SPELL_NONE;

	reset( s );
	CHECK( BG_FindInvocation( s ) == NULL, "empty slots invoke nothing" );

	BG_PushOrb( s, ORB_QUAS );
	CHECK( s[0] == ORB_QUAS && s[1] == ORB_NONE, "first orb fills slot 0" );
	CHECK( BG_FindInvocation( s ) == NULL, "one orb is not enough to invoke" );

	BG_PushOrb( s, ORB_WEX );
	CHECK( BG_FindInvocation( s ) == NULL, "two orbs are not enough to invoke" );

	BG_PushOrb( s, ORB_EXORT );
	CHECK( s[0] == ORB_QUAS && s[1] == ORB_WEX && s[2] == ORB_EXORT, "orbs fill in push order" );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "QWE" ) && inv->kind == INVOKE_KIND_SPELL
		&& !strcmp( inv->name, "Deafening Blast" ), "Q,W,E -> Deafening Blast (spell)" );

	// order matters: the same three orbs in another order are another recipe
	reset( s );
	BG_PushOrb( s, ORB_QUAS ); BG_PushOrb( s, ORB_EXORT ); BG_PushOrb( s, ORB_QUAS );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "QEQ" ) && inv->weapon == WP_SHOTGUN,
		"Q,E,Q -> Shotgun (order selects the recipe)" );

	reset( s );
	BG_PushOrb( s, ORB_WEX ); BG_PushOrb( s, ORB_QUAS ); BG_PushOrb( s, ORB_WEX );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "WQW" ) && inv->kind == INVOKE_KIND_WEAPON
		&& inv->weapon == WP_ROCKET_LAUNCHER && inv->ammo == 15,
		"W,Q,W -> Rocket Launcher with starting ammo" );

	BG_PushOrb( s, ORB_QUAS );
	CHECK( s[0] == ORB_QUAS && s[1] == ORB_WEX && s[2] == ORB_QUAS, "a fourth orb drops the oldest" );
	inv = BG_FindInvocation( s );
	CHECK( inv && !strcmp( inv->combo, "QWQ" ) && inv->weapon == WP_MACHINEGUN,
		"rotation re-reads the new recipe" );

	{
		int i, j, k, n = 0, spells = 0, weapons = 0, portals = 0, reserved = 0, dup = 0;
		char expect[INVOKE_SLOTS + 1];
		static const char letters[ORB_NUM_TYPES] = { '?', 'Q', 'W', 'E' };

		// every one of the 27 ordered recipes must be reachable and unique
		for ( i = 0; i < bg_numInvocations; i++ ) {
			for ( j = 0; j < i; j++ ) {
				if ( !strcmp( bg_invocations[i].combo, bg_invocations[j].combo ) ) {
					printf( "FAIL: duplicate combo %s\n", bg_invocations[i].combo );
					dup++;
				}
			}
			switch ( bg_invocations[i].kind ) {
			case INVOKE_KIND_SPELL:		spells++;	break;
			case INVOKE_KIND_WEAPON:	weapons++;	break;
			case INVOKE_KIND_PORTAL:	portals++;	break;
			default:					reserved++;	break;
			}
		}
		CHECK( !dup && bg_numInvocations == 27 && spells == 10 && weapons == 9
			&& portals == 1 && reserved == 7, "table holds all 27 ordered recipes" );

		for ( i = ORB_QUAS; i < ORB_NUM_TYPES; i++ ) {
			for ( j = ORB_QUAS; j < ORB_NUM_TYPES; j++ ) {
				for ( k = ORB_QUAS; k < ORB_NUM_TYPES; k++ ) {
					reset( s );
					BG_PushOrb( s, (orbType_t)i );
					BG_PushOrb( s, (orbType_t)j );
					BG_PushOrb( s, (orbType_t)k );
					expect[0] = letters[i]; expect[1] = letters[j];
					expect[2] = letters[k]; expect[3] = '\0';
					inv = BG_FindInvocation( s );
					if ( !inv || strcmp( inv->combo, expect ) ) {
						printf( "FAIL: %s did not resolve\n", expect );
						fails++;
					} else {
						n++;
					}
				}
			}
		}
		CHECK( n == 27, "all 27 ordered sequences resolve to their recipe" );

		reset( s );
		BG_PushOrb( s, ORB_EXORT ); BG_PushOrb( s, ORB_WEX ); BG_PushOrb( s, ORB_QUAS );
		inv = BG_FindInvocation( s );
		CHECK( inv && inv->kind == INVOKE_KIND_NONE && !strcmp( inv->name, "Reserved" ),
			"reserved recipes are explicit" );
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
		&& !( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) )
		&& !( hands.grantedWeapons & ( 1u << WP_ROCKET_LAUNCHER ) )
		&& ammo[WP_ROCKET_LAUNCHER] == 15,
		"replacing a hand releases the old weapon and keeps its ammo" );
	ammo[WP_ROCKET_LAUNCHER] = 3;
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_LEFT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& ( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) )
		&& ( hands.grantedWeapons & ( 1u << WP_ROCKET_LAUNCHER ) )
		&& ammo[WP_ROCKET_LAUNCHER] == 3,
		"re-invoking a released weapon never refills it in the same life" );
	BG_InvokeHandsReset( &hands );
	weaponBits = 0;
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& ammo[WP_ROCKET_LAUNCHER] == 15,
		"a fresh life tops the weapon up again" );
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

	// hands can hold spells: equip, release, pack, swap, cast
	BG_InvokeHandsReset( &hands );
	weaponBits = 0;
	CHECK( !BG_InvokeEquipSpell( &hands, -1, SPELL_GHOST_WALK, ammo, &weaponBits )
		&& !BG_InvokeEquipSpell( &hands, INVOKE_HAND_RIGHT, SPELL_NUM, ammo, &weaponBits )
		&& !BG_InvokeEquipSpell( &hands, INVOKE_HAND_RIGHT, SPELL_NONE, ammo, &weaponBits ),
		"invalid spell IDs are rejected" );
	CHECK( BG_InvokeEquipHand( &hands, INVOKE_HAND_RIGHT, WP_ROCKET_LAUNCHER, 15, ammo, &weaponBits )
		&& BG_InvokeEquipSpell( &hands, INVOKE_HAND_RIGHT, SPELL_GHOST_WALK, ammo, &weaponBits )
		&& hands.weapon[INVOKE_HAND_RIGHT] == WP_NONE
		&& BG_InvokeHandSpell( &hands, INVOKE_HAND_RIGHT ) == SPELL_GHOST_WALK,
		"a spell cast replaces the weapon in the hand" );
	CHECK( !( weaponBits & ( 1 << WP_ROCKET_LAUNCHER ) )
		&& !( hands.grantedWeapons & ( 1u << WP_ROCKET_LAUNCHER ) )
		&& ammo[WP_ROCKET_LAUNCHER] == 15,
		"the replaced weapon leaves the hand and keeps its ammo" );
	CHECK( BG_InvokePackedHands( &hands ) == 0
		&& BG_InvokePackedSpells( &hands ) == ( SPELL_GHOST_WALK << 4 ),
		"spells pack into their own hand stat" );
	BG_InvokeEquipSpell( &hands, INVOKE_HAND_LEFT, SPELL_SUNSTRIKE, ammo, &weaponBits );
	BG_InvokeSwapHands( &hands );
	CHECK( BG_InvokeHandSpell( &hands, INVOKE_HAND_LEFT ) == SPELL_GHOST_WALK
		&& BG_InvokeHandSpell( &hands, INVOKE_HAND_RIGHT ) == SPELL_SUNSTRIKE,
		"swap exchanges spell hands" );

	hands.mana = 60;
	CHECK( BG_InvokeTryCast( &hands, INVOKE_HAND_RIGHT, 1000,
		BG_SpellDef( SPELL_SUNSTRIKE )->cost, BG_SpellDef( SPELL_SUNSTRIKE )->cooldown,
		&castSpell ) == INVOKE_FIRE_OK
		&& castSpell == SPELL_SUNSTRIKE
		&& hands.mana == 60.0f - BG_SpellDef( SPELL_SUNSTRIKE )->cost,
		"a cast spends mana and reports the spell" );
	CHECK( BG_InvokeTryCast( &hands, INVOKE_HAND_RIGHT, 1001,
		BG_SpellDef( SPELL_SUNSTRIKE )->cost, BG_SpellDef( SPELL_SUNSTRIKE )->cooldown,
		&castSpell ) == INVOKE_FIRE_COOLDOWN,
		"spell cooldown blocks an early recast" );
	CHECK( BG_InvokeTryCast( &hands, INVOKE_HAND_LEFT, 2000, 95, 1000, &castSpell ) == INVOKE_FIRE_NO_MANA,
		"a hand without the mana cannot cast" );
	CHECK( BG_InvokeTryCast( &hands, INVOKE_HAND_RIGHT, 1001, 0, 0, &castSpell ) == INVOKE_FIRE_INVALID,
		"invalid cast input is rejected" );
	BG_InvokeHandsReset( &hands );
	CHECK( BG_InvokeTryCast( &hands, INVOKE_HAND_LEFT, 1000, 25, 1000, &castSpell ) == INVOKE_FIRE_EMPTY,
		"an empty hand cannot cast" );
	hands.mana = 0;
	BG_InvokeManaRegen( &hands, 3000 );
	CHECK( hands.mana > INVOKE_MANA_REGEN_PER_SEC - 0.5f
		&& hands.mana < INVOKE_MANA_REGEN_PER_SEC + 0.5f,
		"regen clamps a long frame to one second" );
	hands.mana = INVOKE_MANA_MAX - 1;
	BG_InvokeManaRegen( &hands, 1000 );
	CHECK( hands.mana == INVOKE_MANA_MAX, "regen stops at the cap" );

	// every classic spell recipe maps to its definition, and no other
	// recipe carries a spell ID
	{
		int i, seen = 0, tableOk = 1;
		const spellDef_t *def;

		for ( i = 0; i < bg_numInvocations; i++ ) {
			inv = &bg_invocations[i];
			if ( inv->kind == INVOKE_KIND_SPELL ) {
				def = BG_SpellDef( inv->spell );
				if ( !def || strcmp( def->name, inv->name ) || def->cost <= 0
					|| def->cooldown <= 0 || def->cost > INVOKE_MANA_MAX ) {
					printf( "FAIL: spell recipe %s has no valid definition\n", inv->combo );
					tableOk = 0;
				} else {
					seen++;
				}
			} else if ( inv->spell != SPELL_NONE ) {
				printf( "FAIL: non-spell recipe %s carries a spell ID\n", inv->combo );
				tableOk = 0;
			}
		}
		CHECK( tableOk && seen == 10, "all ten classic spells have matching definitions" );
	}

	printf( "%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails );
	return fails ? 1 : 0;
}
