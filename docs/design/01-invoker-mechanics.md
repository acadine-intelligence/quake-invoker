# 01 Invoker mechanics (draft)

Status: slice 1 implemented and verified in-engine (2026-09-05). Weapons are stock placeholders; the design intent per combo below is the target, not the current behavior.

## What slice 1 proves

- `orb <q|w|e>` and `invoke` client commands (`code/game/g_invoke.c`), pushed through the normal client command path.
- Orb state lives in a game-owned per-client table (`g_invoke[]`). This keeps invocation state separate from the shared client layout. Shared struct edits require all dependent QVM sources to rebuild. Missing header dependencies caused inconsistent struct offsets during the visual-effects pass; a clean rebuild fixed that failure. The QVM build now tracks headers.
- Order-independent combo lookup in `code/game/bg_invoke.c` (shared by game and cgame, unit-tested host-side by `make -C tests`).
- HUD: colored orb slots with letters, the pending invocation, and the last confirmed invocation (`CG_DrawOrbs` in `code/cgame/cg_invoke.c`).
- One live invoked weapon at a time; invoking a different weapon drops the previous one. Machinegun and gauntlet (spawn weapons) are never dropped.

## In-engine test (scripted, repeatable)

```bash
# from the repo root, with OA data installed per docs/design/03-playable.md:
cmake --build build -j8 --target qagameqvm_baseq3 cgameqvm_baseq3 uiqvm_baseq3
mkdir -p build/Release/invoker/vm && cp build/Release/baseq3/vm/*.qvm build/Release/invoker/vm/
# then launch:
#   build/Release/ioquake3.app with: +set com_basegame baseoa +set fs_game invoker +set sv_pure 0
#     +map oa_dm3
# console: orb q / orb q / orb e / invoke  -> expect "invoked Frost Rockets (QQE)", rocket launcher in hand
```

`configs/invoker.cfg` supplies opt-in bindings: Q/E/R select orb types Q/W/E; F invokes. W keeps its movement binding. See `docs/design/03-playable.md` for installation and verification.

## Engine-native invocation visuals

Held orbs orbit below the crosshair with fading trails. Quas is blue, Wex is violet, and Exort is orange. A confirmed invocation triggers an expanding spark ring and a colored dynamic light for 600 ms. The lower-left HUD distinguishes the pending combo (`READY`) from the last confirmed invocation (`CAST`). Changing orbs leaves the last cast label unchanged.

`cg_invokeEffects 0` disables the first-person effects and HUD pulse. The HUD panel stays visible. Death, spectator views, and follow mode hide the panel and effects. Respawn and map restart clear the stored cast. Third-person views keep the HUD while hiding camera-relative effects.

This pass reuses the installed `railDisc` shader and the engine sprite/light APIs. It adds no art files and changes no weapon damage or projectile behavior. These effects show only for the local player; remote-player orb replication is outside this pass.

`make -C tests` runs the shared rule tests and a cgame renderer-recording test. The macOS script `python3 tests/run_visual_smoke.py` runs real QVMs on OpenArena's `oa_dm3` and captures the rendered output. Its receipt reports command-flow checks separately from visual inspection.

## Orbs (design target)

Three orb types, three slots. Pressing an orb key pushes that orb into the slots and drops the oldest. Order does not matter for the invocation, only the multiset (same as DotA Invoker).

Working names, subject to change:

| Orb | Theme | Passive while held (per orb count) |
|-----|-------|-----------------------------------|
| Q | Frost / control | health regeneration |
| W | Storm / mobility | movement speed |
| E | Fire / damage | damage bonus |

## Invocation

Press Invoke. The engine reads the current three-orb multiset and equips the matching weapon into one of two invoked-weapon slots (newest replaces oldest). Ten combinations exist with three orbs of three types:

| Combination | Weapon / ability idea | Quake analogue |
|-------------|-----------------------|----------------|
| QQQ | Frost wall (blocking, slows) | none |
| WWW | Blink dash | none |
| EEE | Sunstrike (delayed AoE) | BFG-lite |
| QQW | Ice shards (projectile that lays a wall) | plasma |
| QQE | Frost rockets (slow on hit) | rocket launcher |
| WWQ | Tornado (knock up, self launch) | none |
| WWE | Chaos lightning (bouncing) | lightning gun |
| EEQ | Meteor roll (rolling area burn) | none |
| EEW | Alacrity (fire rate buff) | haste |
| QWE | Deafening blast (knockback shot) | shotgun / railgun-ish |

The Quake column is a design aid: which existing weapon code paths in `code/game/g_weapon.c` and `code/cgame/cg_weapons.c` each invocation can start from.

## Implementation sketch

- `code/game/bg_public.h`: add orb state and invoked weapon slots to `playerState_t` stats (fits in `persistant`/`stats` arrays or new fields; check network delta cost).
- `code/game/g_active.c`: read orb and invoke button state from `usercmd_t`.
- `code/game/g_weapon.c`: map invocation id to a fire function.
- `code/cgame/cg_draw.c`: draw orb slots and invoked weapons in the HUD.
- `code/ui/`: keybinds for Q, W, E, Invoke.

Keep every change server-authoritative and predicted in `bg_pmove.c` where movement is affected (blink, tornado).
