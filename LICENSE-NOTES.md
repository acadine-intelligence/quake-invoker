# License notes

Quake Invoker is free software under the GNU General Public License, version 2 or (at your option) any later version. The full license text is in `COPYING.txt`.

## Lineage

1. id Software released the Quake III Arena source under GPL-2.0 on 20 August 2005 (github.com/id-Software/Quake-III-Arena).
2. ioquake3 (github.com/ioquake/ioq3) is a GPL-2.0 continuation of that release with a CMake build, SDL2, OpenAL and modern platform support.
3. Quake Invoker forks ioquake3. The `upstream` Git remote tracks ioquake3 so engine fixes can be merged.

## What the GPL requires of this project

- Every distributed binary ships with, or offers, the complete corresponding source: engine, game code and the QVM game logic sources.
- Modified files carry a notice that they were changed and when.
- Nobody may add terms that restrict the rights the GPL grants.

## What the GPL does not cover

- Quake III Arena game data (maps, textures, models, sounds in the retail `pak*.pk3` files) belongs to id Software and is not licensed under the GPL. It is not in this repository and must not be added.
- Assets created for Quake Invoker are separate works. Each one needs its license recorded in `docs/ASSETS.md`.

## Third-party code with its own notice

Kept from the upstream tree, unchanged:

| Component | Files | License |
|-----------|-------|---------|
| zlib unzip | `code/qcommon/unzip.c` | zlib |
| MD4 | `code/qcommon/md4.c` | RSA Data Security notice, see `docs/md4-readme.txt` |
| BSD libc replacements | `code/game/bg_lib.c` | BSD-4-Clause (UC Regents) |
| libjpeg 9f | `code/thirdparty/jpeg-9f/` | IJG |
| SDL2 2.32.8 | `code/thirdparty/SDL2-2.32.8/` | zlib |
| OpenAL Soft 1.24.3 | `code/thirdparty/openal-soft-1.24.3/` | LGPL-2.0 (dynamically loaded) |
| curl 8.15.0 | `code/thirdparty/curl-8.15.0/` | curl (MIT-style) |
| libogg, libvorbis, opus, opusfile | `code/thirdparty/libogg-1.3.6/` etc. | BSD-3-Clause |
| zlib 1.3.1 | `code/thirdparty/zlib-1.3.1/` | zlib |

See `docs/id-readme.txt` for the original id Software notes.

## Trademarks

"Quake" and "Quake III Arena" are trademarks of id Software / ZeniMax / Microsoft. This project is not affiliated with or endorsed by them. The name "Quake Invoker" describes the lineage; it will be reviewed before any public release.
