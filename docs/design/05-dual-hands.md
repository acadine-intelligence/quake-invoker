# Movement orbs and dual weapons

Status: implemented and exercised on native macOS and browser builds (2026-09-07). The later ordered-recipe pass changed how recipes resolve; see the note below and `04-ordered-spells.md`.

## controls

Jonathan confirmed that movement and orb selection share keys. D moves right and selects blue Quas. W moves forward and selects purple Wex. A moves left and selects orange Exort. S moves backward without selecting an orb.

A new press selects one orb. Holding a key continues movement. Another selection requires release and a new press. The HUD displays orb colors without recipe letters. A separate WASD display shows held keys, including opposing keys together.

R invokes into the right hand, replacing only that hand. T swaps the hands, including an empty hand. Mouse 1 fires the left hand. Mouse 2 fires the right hand. Each prepared hand must show its actual weapon model and firing feedback.

1. Tap W, Q, W (rocket launcher recipe).
2. Press R.
3. Press T.
4. Tap W, W, Q (lightning gun recipe).
5. Press R.

The earlier pass resolved recipes from orb counts only. Recipes are now order-sensitive: the rocket example above is W,Q,W and the lightning example W,W,Q (see `04-ordered-spells.md`). Controls, hands, and resource rules are unchanged.

## acceptance

- Movement and one-orb-per-press behavior work through actual key input.
- The WASD display shows held and released keys correctly.
- R prepares the right hand. T swaps empty and occupied hands correctly.
- Both models appear on their assigned sides. Either mouse button fires its own weapon, including held fire.
- Ammunition and cooldown tests establish server-side behavior. Swaps and repeated invocations cannot refill ammunition.
- Death, spectator mode and map restart remove stale hand state.
- Native and browser builds load the same updated mod and render correctly.

## verification

`make -C tests` passes the shared-logic check suite and 65 cgame checks. Both CMake builds succeed. `python3 tests/run_visual_smoke.py` runs the native game through dual firing and respawn, with screenshots.

The browser was exercised with real key and mouse events through local Chrome. A held W moved the player and generated one orb despite repeated key-down events. A/D and W/S both remained visible when held together. Left-click spent rocket ammunition and caused splash damage. Right-click fired lightning and spent lightning ammunition. T exchanged the actual models and retained ammunition.

The read-only input review identified a held-key reset issue. A failing regression test reproduced it before the fix. Full independent server/render review remains separate from these local checks.

The optional `configs/invoker-demo.cfg` prepares a rocket launcher on the left and lightning gun on the right. Launch it with `activeAction` after map loading. The regular `invoker.cfg` only installs controls.

Review follow-up (2026-09-10): replaced weapons are released when no hand holds them, so they no longer stay selectable through the classic weapon menu; a fresh life drops the previous life's invocations; late joins receive every player's hands; the gauntlet cannot double-hit when the classic and invoke paths run in one frame; invoke cooldowns use the server clock; a compile-time check pins the 4-bit hand packing; and the stock viewmodel shows again while both hands are empty.

Spell follow-up (2026-09-10, branch `feat/spell-casting`): a hand can hold a weapon or a spell. Ghost Walk (Q,Q,W) turns the caster invisible for 5 s; Sunstrike (E,E,E) marks the aimed point and lands after 1.75 s in a 200-unit radius for 90 damage. Casting spends mana from a shared 100-point pool (4 points per second regen) and starts the spell's cooldown on the server clock. The HUD adds a mana bar and shows spell names in the hand rows. Every other spell still reports "not castable yet".

Ammo economy (second review, 2026-09-10): a released weapon keeps its remaining ammo on the books, and each weapon tops up only on its first invoke of a life, so cycling invokes cannot generate ammunition.

HUD feedback (branch feat/invoker-hud): each orb ring carries its key letter, so the D/W/A mapping to Q/W/E is readable on the ring itself. A spell hand shows a full amber bar that drains as the spell comes back, and its label dims while recharging. The server confirms each successful cast with an `invcast` message; the client draws the timer from that, so a rejected cast never starts the readout.
