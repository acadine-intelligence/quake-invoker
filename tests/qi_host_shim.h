// Host-side stand-ins for the engine types bg_invoke.c needs when built
// outside the engine (QI_HOST_TEST). Keep in sync with bg_public.h weapon_t.
#ifndef QI_HOST_SHIM_H
#define QI_HOST_SHIM_H
#include <string.h>
#include <stddef.h>
#include <math.h>
#define qboolean int
#define qfalse 0
#define qtrue 1
#define Com_Memset memset
#define ARRAY_LEN(x) (sizeof(x) / sizeof(*(x)))
// Minimal q_math stand-ins for the portal rules; extend when new code needs more.
typedef float vec3_t[3];
#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define VectorScale(v,s,o) ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define VectorSet(v,x,y,z) ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define VectorMA(v,s,b,o) ((o)[0]=(v)[0]+(s)*(b)[0],(o)[1]=(v)[1]+(s)*(b)[1],(o)[2]=(v)[2]+(s)*(b)[2])
#define VectorLengthSquared(v) ((v)[0]*(v)[0]+(v)[1]*(v)[1]+(v)[2]*(v)[2])
typedef enum { WP_NONE, WP_GAUNTLET, WP_MACHINEGUN, WP_SHOTGUN, WP_GRENADE_LAUNCHER, WP_ROCKET_LAUNCHER,
	WP_LIGHTNING, WP_RAILGUN, WP_PLASMAGUN, WP_BFG, WP_GRAPPLING_HOOK, WP_NUM_WEAPONS } weapon_t;
#endif
