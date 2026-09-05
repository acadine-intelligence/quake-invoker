// Host-side stand-ins for the engine types bg_invoke.c needs when built
// outside the engine (QI_HOST_TEST). Keep in sync with bg_public.h weapon_t.
#ifndef QI_HOST_SHIM_H
#define QI_HOST_SHIM_H
#include <string.h>
#include <stddef.h>
#define qboolean int
#define Com_Memset memset
#define ARRAY_LEN(x) (sizeof(x) / sizeof(*(x)))
typedef enum { WP_NONE, WP_GAUNTLET, WP_MACHINEGUN, WP_SHOTGUN, WP_GRENADE_LAUNCHER, WP_ROCKET_LAUNCHER,
	WP_LIGHTNING, WP_RAILGUN, WP_PLASMAGUN, WP_BFG, WP_GRAPPLING_HOOK, WP_NUM_WEAPONS } weapon_t;
#endif
