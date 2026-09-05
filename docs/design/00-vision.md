# 00 Vision

## One line

Quake movement and gunplay with Invoker's spell grammar: the weapon you hold is the combination you invoked.

## Why it is interesting

Quake III gives a solved, fast, readable combat loop with strafe-jumping, rocket-jumping and item control. Invoker (DotA 2) gives a decision layer where three orb slots (Quas, Wex, Exort) combine into ten spells, and skill lives in remembering and executing combinations under pressure. Putting the second on top of the first turns weapon switching, the one flat part of Quake, into the most expressive part of the game.

## Scope by phase

| Phase | Deliverable | Players |
|-------|-------------|---------|
| 0 | Engine fork builds clean, standalone identity, no gameplay change | 0 |
| 1 | Orb slots + invoke on top of stock weapons, one test map | 1 |
| 2 | Roguelite run: rooms, waves, upgrades between rooms, permadeath run | 1 |
| 3 | 1v1 duel with invoke | 2 |
| 4 | Deathmatch, further modes | N |

## Non-goals for now

- Retail Quake III content. Everything ships standalone.
- Multiplayer before the single-player loop is fun.
- Engine rewrites. Gameplay lives in `code/game/` and `code/cgame/`.

## Open decisions for Jonathan

1. Orb count and names: three orbs like Invoker, or two for a smaller combination table at phase 1.
2. Invoke cost: cooldown, mana, or free with a slot limit of two invoked weapons at a time (Invoker has two).
3. Whether stock Quake weapons survive as a fallback or every weapon is an invocation.
