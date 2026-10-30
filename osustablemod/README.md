# osu! stable module

aim assist and relax for osu! stable

the module also re-enables the gameplay and song select behaviour that the Relax
mod normally disables, ported from the osu!patcher IL patches into native code

## requirements

- windows
- visual studio 2022 with the desktop c++ workload
- cmake 3.20 or newer
- ninja

## build

build the 32-bit release from the repository root

run these commands from the x86 native tools command prompt for visual studio 2022

```powershell
cmake --preset release
cmake --build build/release
```

or run `scripts/build.cmd`, which sets up the toolchain, builds out of tree and
copies the result into `build/release/bin`

the compiled files are placed in `build/release/bin`

- `DiscordHook.dll`
- `DiscordSetup.exe`

keep both files in the same directory

## use

1. run `DiscordSetup.exe`
2. start osu!
3. wait for the injector to finish

the injector loads `DiscordHook.dll` by default

run `DiscordSetup.exe --disable-auth` to also stop `osu!auth.dll` threads

## configuration

aim assist settings are in `module/aim/aim_assist.hpp`

relax settings are in `module/relax/relax.hpp`

patch settings are in `module/il/patch.h`

rebuild after changing any of them

relax reads the configured osu! left and right keys automatically and falls back to `Z` and `X`

## approach rate override

create `ar.txt` next to `osu!.exe` with the approach rate you want on every
beatmap, for example

```
9.5
```

the file is read every second, the value is applied the next time a beatmap is
loaded, and a map restart is enough to pick up an edit (no game restart)

- values are clamped to 0..11
- the file is optional, without it beatmaps keep their own approach rate
- `#` or `;` starts a comment line
- the log records the value and the resulting preempt time of every map

```
[2026-10-29 23:05:53] approach rate applied to beatmap: 950
[2026-10-29 23:05:56] map preempt ms: 525
```

the implementation hooks the beatmap parser (the approach rate store sits at
offset `0x2C` of the managed beatmap object) and writes the value again before
every play initializes its hit object manager, which is what makes a retry pick
up a new value

## gameplay patches

the module hooks the jit compiler and rewrites a method body the first time the
clr compiles it, so nothing is patched on disk and every rewrite is applied
before the method runs

| patch | effect |
| --- | --- |
| `AllowRelaxDrawMisses` | draws the miss markers while Relax is active |
| `AllowRelaxComboBreakSound` | plays the combo break sound while Relax is active |
| `AllowRelaxFailing` | lets a Relax play fail like a normal play |
| `AutoSaveRelaxScores` | stores finished Relax scores to the local leaderboard |
| `CustomSongSelectThumbnailAlpha` | raises the thumbnail overlay alpha from 50 to 185 |
| `DisableThumbnailFadeIn` | stops thumbnails from fading in from zero on every update |

`ShowModsInGameplay` is implemented but turned off by default: the mods overlay
sprite of the current osu! build is an empty placeholder that the game fills in
from its replay path, so forcing it to draw in a normal play only produces a
solid black rectangle in the bottom right corner of the playfield

each patch can be turned off in `module/il/patch.h`, the mods overlay alpha and
the thumbnail alpha are configurable there as well

methods are matched by their il opcode sequence instead of an address or a name,
so a game update that keeps the same code shape keeps the patches working

every applied patch is appended to `relax-patches.log` next to `osu!.exe`, for
example

```
[2026-10-29 22:36:10] initializing
[2026-10-29 22:36:10] jit hook installed
[2026-10-29 22:36:19] patched AllowRelaxComboBreakSound (il 40 bytes)
```

the gameplay patches only appear in that log once the matching mode is loaded
(gameplay for the relax scores, song select for the thumbnails)
