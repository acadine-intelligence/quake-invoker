# 03 Playable builds (macOS desktop + browser)

Status: phase 0 to phase 1 bridge. What a "playable" build means, where game data comes from, and how to run it.

## Licensing rule for playable builds

The engine is GPL-2.0. Quake III Arena game data is not. Every playable build therefore uses one of:

1. **OpenArena data (current choice).** openarena-0.8.8.zip from openarena.ws (SourceForge mirror `downloads.sourceforge.net/project/oarena/openarena-0.8.8.zip`, sha256 `5a8faf7f5b51f351b0a1618c06b6b98a5f1a6758f1d39818de2c87df2a0bac4a`). GPL content, standalone-safe, no com_standalone code needed. Its engine (`openarena-engine-source-0.8.8`) ships id's QIIIA Game Source License maps, so its maps are excluded from our repo; only the data dirs are used, and only outside the repository.
2. **Our own assets.** Registered in `docs/ASSETS.md`. Long-term plan; needed before any public release that ships content.

Retail `pak0.pk3` through `pak8.pk3` never enter this repository, a release, or a hosted build.

## macOS desktop build (verified commands, 2026-09-05)

```bash
cd ~/projects/quake-invoker
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
# unpack OA data once, outside the repo:
#   ~/projects/quake-invoker-content/openarena-0.8.8/  (baseoa/, missionpack/)
cp -R ~/projects/quake-invoker-content/openarena-0.8.8/baseoa build/Release/baseoa
cp ~/projects/quake-invoker-content/openarena-0.8.8/missionpack/mp-pak0.pk3 build/Release/missionpack/
open build/Release/ioquake3.app --args +set com_basegame baseoa +map oa_dm1
```

The engine looks in `com_basegame` before `baseq3` and enters standalone mode when no retail paks are found. QVM game logic in `build/Release/baseoa/vm/` is ours to replace later (phase 1 gameplay).

## Browser build (Emscripten)

ioquake3 carries a first-party wasm path: `cmake/platforms/emscripten.cmake` builds `ioquake3.html` + `ioquake3.js` + `ioquake3.wasm` with WebGL1/2. Two data modes:

- **Streaming mode (current choice):** `EMSCRIPTEN_PRELOAD_FILE=OFF`. The page fetches `client-config.json` and streams each pk3 from the web server with a loading progress bar. No .data file to rebuild when content changes.
- **Prepacked mode:** `EMSCRIPTEN_PRELOAD_FILE=ON` embeds `baseoa/` into a .data file at link time. Simpler hosting, heavier rebuild loop.

```bash
source ~/projects/quake-invoker/.emsdk/emsdk_env.sh
emcmake cmake -S . -B build-em -DCMAKE_BUILD_TYPE=Release
cmake --build build-em -j8
# serve from repo root (the page refuses file:// URLs):
python3 -m http.server 8080
# open http://localhost:8080/build-em/Release/ioquake3.html
```

The server must expose the data directory tree under the page URL. Verified working layout (2026-09-05):

- `build-em/Release/baseoa/pak*.pk3` (the 8 GPL paks copied from the OA extract; ~391 MB)
- `build-em/Release/ioquake3-config.json` listing each pak as `{"src": "baseoa/<name>.pk3", "dst": "/baseoa"}`
- serve the repo root: `python3 -m http.server 8080`, then open
  `http://127.0.0.1:8080/build-em/Release/ioquake3.html?com_basegame=baseoa`
- direct into a map: append `&args=%2Bmap%20oa_dm3` (the `+` must be `%2b`-encoded)

Verified end to end: page loads, streams all paks, menu renders, `+map oa_dm3` enters the arena with weapon and HUD live (WebGL). Public hosting is a later, separately approved step (GitHub Pages or Cloudflare).

## Emscripten toolchain

`~/projects/quake-invoker/.emsdk/` (shallow clone, emsdk `latest`). Install once: `./emsdk install latest && ./emsdk activate latest`. brew's emscripten formula was rejected: it pulls a JDK and dozens of desktop deps (~2 GB) for a cross-compiler; upstream emsdk is the packaging ioquake3's CI actually uses.

## Acceptance for "playable"

Desktop: the app opens to a menu, a map loads, the player can move and shoot, then quit. Browser: the page loads, content streams, a map loads and plays at interactive frame rate.

## Agent capacity plan

Not needed for these builds. The M4 does everything: 2-minute native build, emsdk wasm build, local servers. Cloud lanes become worth it when (a) gameplay work runs in parallel worktree lanes overnight, (b) cross-platform CI (Linux/Windows/wasm) gates every push, or (c) the browser build needs public hosting. Each is a separate decision.
