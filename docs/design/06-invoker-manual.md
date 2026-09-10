# Invoker manual page

Status: implemented (branch `feat/invoker-menu`, 2026-09-10).

## What it is

A player-facing page that lists the controls and the ten classic
recipes. It opens from the main menu ("INVOKER MANUAL"), from the
in-game ESC menu, and from the `invmenu` console command. `invmenu`
toggles: run it again while the page is on top and it closes.
`configs/invoker.cfg` binds the page to F1.

## Why the menus live in C code

The client runs the classic UI module (`code/q3_ui/`), not the newer
`code/ui/` module. The classic module builds every menu from C, so the
page is a new source file rather than a data file. No `ui/*.menu`
content exists anywhere in the build, and none is needed.

The page draws with `UI_DrawProportionalString` only, so it ships in
every build with no new assets.

## Where it hooks in

- `code/q3_ui/ui_invoker.c`: the page itself (custom draw and key
  handling).
- `code/q3_ui/ui_menu.c`: the main menu entry.
- `code/q3_ui/ui_ingame.c`: the ESC menu entry.
- `code/q3_ui/ui_atoms.c`: `UI_ConsoleCommand` routes `invmenu`.
- `code/q3_ui/ui_local.h`: prototypes.
- `cmake/basegame.cmake`: the `ui_invoker.c` source entry.
- `tests/visual_smoke.cfg` and `tests/run_visual_smoke.py`: the smoke
  opens the ESC menu, opens the page, closes it, and returns to play,
  with a screenshot at each step.

## Notes

- Text budget: the UI screen space is 640x480 and the small font runs
  about 13 units per character, so keep a line under roughly 44
  characters. Longer lines clip at both edges of the screen. The smoke
  enforces this: `assert_menu_fits` in `tests/run_visual_smoke.py`
  fails when any bright pixel of the captured page reaches the outer
  8 pixels of the frame.
- The page describes the movement-orb and dual-hand control scheme from
  `05-dual-hands.md`. Update the copy when that scheme changes.
- When the page opens straight from play (`uis.menusp == 0`), it pauses
  a local game the same way the ESC menu does; `UI_ForceMenuOff` clears
  the pause when the page closes.
- Screenshots `ingame_menu`, `invoker_manual`, and `back_to_game` in a
  smoke run are the visual acceptance evidence.
