<div align="center">
<img src="./assets/icon.jpg" alt="Petka and Vasily Ivanovich Save the Galaxy"/>
</div>

# Petka and Vasily Ivanovich Save the Galaxy

## open-ptk

[Русский](README.md)

A new engine for «Petka and Vasily Ivanovich Save the Galaxy» (SKIF/Buka,
1998), written in C from the original `Petka.exe`, taken apart in IDA Pro.
It plays the game from its own files -- the `.STR` stores, the films, the
script -- on macOS, Linux, Windows and the Nintendo Switch. The game's files
are not here: they come from your own CDs.

## What is in it

| folder | what |
|---|---|
| `core/` | `petka_core`, the engine without a platform: the `.STR` stores, BMP, FLIC clips, masks, dialogues, the rooms' graph, the script's machine, the walk meshes and the pathfinder, `.ini` and saved games. Plain C11, no dependencies |
| `launcher/` | `petka_launcher`, the game itself on [raylib](https://www.raylib.com): the scenes, the heroes, the star of actions, the case, the map, the panel, conversations, films, sound, saving and loading |
| `switch/` | the Nintendo Switch build: `petka.nro` and an installed title (NSP), see [switch/README.md](switch/README.md) |
| `tools/unpack/` | `ptk-unpack`, which makes the game's folder from the CD images (`.isz`/`.iso`), see [tools/unpack/README.md](tools/unpack/README.md) |
| `tools/strtool/` | lists and extracts what a `.STR` store holds |
| `tools/*test/` | checks of the core against the real files: clips, dialogues, rooms, the script, walking |
| `tools/play/` | scripted plays through parts of the game (`PETKA_PLAY`) |
| `docs/` | the `.STR` format, [str-format.md](docs/str-format.md) |

Besides the original game, the launcher has an extras menu (Tab on a
desktop, - on the Switch), each of them switched on and off there: a CRT
monitor's look, a dark band under texts, the outline of things that can be
used, the lines the original never says and the Reloaded edition put back
(restored dialogues), and on a controller the pointer's pull to small
things. Mistakes of the game's own script are put right as it is loaded
(`launcher/src/scriptfix.c`).

## The game's files

```
cd tools/unpack && go build -o ptk-unpack .
./ptk-unpack game ~/ptk1-files "…CD 1.isz" "…CD 2.isz" "…CD 3.isz"
```

gives a folder with the `.STR` stores, `intro.avi`, `disk1.id`..`disk3.id`
and `data1_out/`. That is all the launcher reads.

## The second game

The same launcher runs «Петька и Василий Иванович 2: Судный день»
(SKIF/Buka, 1999) as well: its engine and formats are the same. The
launcher tells which game a folder holds by its files (`PART_1\objects.ids`
in `main.str`) and leaves the first game's own fixes out for the second:
the script fixes, the restored dialogues, the advertisement film.

```
./ptk-unpack game ~/ptk2-files ptk2.isz
build/launcher/petka_launcher ~/ptk2-files            # from the start
build/launcher/petka_launcher ~/ptk2-files PART_2     # straight into part 2
```

Its films are Indeo 5, which the usual `ffmpeg` plays too. `-DPETKA_GAME=1`
or `-DPETKA_GAME=2` builds the launcher for one of the games alone (the
default, 0, tells the game by its files). For the Switch,
see [switch/README.md](switch/README.md#the-second-game).

## Building on a desktop

Needs CMake 3.16+, a C11 compiler, raylib (found through pkg-config) and,
for the films, the `ffmpeg` program at run time.

```
brew install cmake raylib pkg-config ffmpeg      # macOS; elsewhere the same packages
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`-DPETKA_STATIC_RAYLIB=ON` puts raylib inside the program, so that one file
runs on a machine with no raylib installed -- the way a release is built.
A `Debug` build adds the debug overlay (F1); `Release` and the default
`RelWithDebInfo` have none.

## Running

```
build/launcher/petka_launcher ~/ptk1-files            # from the start
build/launcher/petka_launcher ~/ptk1-files PART_2     # straight into part 2
```

With no arguments the game is where the program is: copy `petka_launcher`
into the game's folder and start it there. Saved games, `Petka.ini` and
`extras.ini` are kept in that folder. `ffmpeg` is looked for next to the
program first, then on `PATH`.

## Controls

| key | what |
|---|---|
| left click | act on the thing under the cursor, or walk there |
| right click | the star of actions |
| 1-6, or L W G U T C | look, walk, take, use, talk, Chapayev |
| a click, Space | the next phrase of a conversation |
| a click on a line, its number | say that line |
| F2 / F3 | save / load |
| Esc | shut the case, leave the screen that is up |
| Alt+R | the cursor and the star back, when the game is left with none |
| Tab | the extras menu |
| F4 | the CRT look on and off |
| F1 | the debug overlay (a `Debug` build only) |

The Switch's controls are in [switch/README.md](switch/README.md#controls).

## For development

The launcher can play itself with no one at it, through environment
variables (all of them are described at the top of `launcher/src/main.c`):
`PETKA_PLAY=<file>` follows the steps of a play from `tools/play/`,
`PETKA_SHOT=<file.png>` saves a screenshot and quits, `PETKA_TRACE=1` logs
the script's messages, `PETKA_FAST=1` and `PETKA_TURBO=1` make it go fast.

```
PETKA_PLAY=tools/play/part1.play PETKA_TURBO=1 build/launcher/petka_launcher ~/ptk1-files
```

The tools in `tools/*test/` take a store and a part:

```
build/tools/scripttest/scripttest ~/ptk1-files/MAIN.STR PART_1
build/tools/walktest/walktest ~/ptk1-files/FLC1.STR
```

## raylib

The project is built on [raylib](https://www.raylib.com) (Ramon Santamaria
and contributors, zlib/libpng license): the launcher's window, graphics,
sound and input are its. The Nintendo Switch build uses
[raylib-nx](https://github.com/luizpestana/raylib-nx), raylib ported to libnx.

## The original

The game and its files belong to their owners (SKIF, Buka). This
repository holds none of them -- only the engine; you need your own copy of
the game to play.
