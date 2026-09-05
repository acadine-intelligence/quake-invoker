# 02 Roguelite mode (draft)

Status: draft for discussion. Nothing here is implemented.

## Run structure

- A run is a sequence of rooms (small arenas). Clear the wave, pick one upgrade, move to the next room.
- Death ends the run. Nothing carries over except unlocks (new orb variants, cosmetic).
- Target run length: 10 to 15 minutes.

## What varies between runs

- Room order and enemy mix (seeded, so a seed can be shared).
- Upgrade offers: passive orb bonuses, invocation modifiers (e.g. rockets split, lightning chains one extra), movement tweaks.
- Boss room every N rooms.

## Enemies

Start with the stock Quake bots (`code/botlib/`, `code/game/ai_*.c`) driven by simple behaviour flags. Later: custom monster entities with fixed patterns, which read better in a roguelite than bot aim.

## Minimum playable version

1. One room, one wave of bots, invoke works, clear condition, restart.
2. Three rooms chained with one upgrade choice between them.
3. Seeded runs and a run summary screen.

## Engine hooks

- Game mode: new `GT_ROGUELITE` gametype in `code/game/bg_public.h`, handled in `g_main.c`, `g_client.c`, `g_combat.c`.
- Room loading: single map with sealed arenas and teleport gates is cheaper than map changes between rooms.
- Persistence between rooms: server-side run state, not player state, so a room transition cannot lose progress.
