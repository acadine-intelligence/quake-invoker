# AGENTS.md - Quake Invoker

Entry point for any agent working in this repository.

## What this is

Quake Invoker is a GPL-2.0 open-source game built on the Quake III Arena engine (via ioquake3). It combines Quake's movement and combat with DotA Invoker's spell-combination mechanic: the player holds three orb slots and invokes weapons and abilities from the orb combination. First target is a one-player roguelite mode. Later targets: 1v1, deathmatch and other modes.

Owner: Jonathan (acadine-intelligence). Canonical root: `~/projects/quake-invoker/`. Hermes project slug: `quake-invoker`.

## License boundary (read before adding anything)

- Engine and game code: GPL-2.0-or-later. Every modification stays GPL. See `COPYING.txt` and `LICENSE-NOTES.md`.
- Third-party code inside the tree keeps its own notice (zlib, MD4, BSD libc, IJG). Do not strip headers.
- Do not add any file from a retail Quake III Arena install (`pak0.pk3` to `pak8.pk3`). The game data is not GPL.
- Do not add assets with unclear provenance. New assets need a recorded license in `docs/ASSETS.md` before they land.
- Do not use the "QIIIA Game Source License" mod SDK code. Only the GPL release lineage is allowed.

## Layout

| Path | Purpose |
|------|---------|
| `code/qcommon/`, `code/server/`, `code/client/`, `code/renderer*` | Engine (ioquake3) |
| `code/game/` | Server-side game logic (weapons, items, entities, game modes). Invoker logic lives here. |
| `code/cgame/` | Client-side game presentation (HUD, effects, prediction). Orb HUD lives here. |
| `code/ui/` | Menus |
| `cmake/identity.cmake` | Product name, module names, base game dir |
| `docs/` | ioquake3 docs plus `docs/design/` for Quake Invoker design notes |
| `build/` | CMake output, ignored by Git |

## Build (macOS arm64, verified 2026-09-05)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Dependencies: Xcode Command Line Tools, CMake (`brew install cmake`). SDL2 comes from the internal libs bundle.

Standalone build (no retail data detection), once a base game directory exists:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_STANDALONE=1
```

No formal test suite exists yet. Verification today = clean configure + clean build + the client binary starts.

## Working rules

- One substantial change per branch/worktree. Keep `main` clean and buildable.
- Keep engine changes minimal; put gameplay in `code/game/` and `code/cgame/` so upstream ioquake3 merges stay cheap.
- Pull upstream through the `upstream` remote (`git fetch upstream && git merge upstream/main`), never by re-cloning.
- Remote push, release and publication are Jonathan's gates.
- Document a command only after you have run it.

## Design docs

- `docs/design/00-vision.md` - what we are building and why
- `docs/design/01-invoker-mechanics.md` - orbs, invocations, weapon table (draft)
- `docs/design/02-roguelite-mode.md` - first game mode (draft)
