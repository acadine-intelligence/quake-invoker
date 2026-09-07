# Movement orbs and dual weapons

Status: implemented and exercised on native macOS and browser builds (2026-09-07). This slice uses the existing stock-weapon recipes. Classic spell behavior and ordered recipes remain in `04-ordered-spells.md`.

## controls

Jonathan confirmed that movement and orb selection share keys. D moves right and selects blue Quas. W moves forward and selects purple Wex. A moves left and selects orange Exort. S moves backward without selecting an orb.

A new press selects one orb. Holding a key continues movement. Another selection requires release and a new press. The HUD displays orb colors without recipe letters. A separate WASD display shows held keys, including opposing keys together.

R invokes into the right hand, replacing only that hand. T swaps the hands, including an empty hand. Mouse 1 fires the left hand. Mouse 2 fires the right hand. Each prepared hand must show its actual weapon model and firing feedback.

For a rocket launcher on the left and lightning gun on the right:

1. Tap D, D, A.
2. Press R.
3. Press T.
4. Tap W, W, A.
5. Press R.

These recipes still use the prototype's order-independent matching. Both weapons must fire with normal ammunition consumption. Preparing or swapping a weapon must preserve its ammunition and cooldown. Two copies of the same weapon share these resources.

## acceptance

- Movement and one-orb-per-press behavior work through actual key input.
- The WASD display shows held and released keys correctly.
- R prepares the right hand. T swaps empty and occupied hands correctly.
- Both models appear on their assigned sides. Either mouse button fires its own weapon, including held fire.
- Ammunition and cooldown tests establish server-side behavior. Swaps and repeated invocations cannot refill ammunition.
- Death, spectator mode and map restart remove stale hand state.
- Native and browser builds load the same updated mod and render correctly.

## verification

`make -C tests` passes 25 shared-logic checks and 65 cgame checks. Both CMake builds succeed. `python3 tests/run_visual_smoke.py` runs the native game through dual firing and respawn, with screenshots.

The browser was exercised with real key and mouse events through local Chrome. A held W moved the player and generated one orb despite repeated key-down events. A/D and W/S both remained visible when held together. Left-click spent rocket ammunition and caused splash damage. Right-click fired lightning and spent lightning ammunition. T exchanged the actual models and retained ammunition.

The read-only input review identified a held-key reset issue. A failing regression test reproduced it before the fix. Full independent server/render review remains separate from these local checks.

The optional `configs/invoker-demo.cfg` prepares a rocket launcher on the left and lightning gun on the right. Launch it with `activeAction` after map loading. The regular `invoker.cfg` only installs controls.