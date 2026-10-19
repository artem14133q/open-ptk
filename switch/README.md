# Nintendo Switch build

[Русский](README.ru.md)

`petka.nro`, a homebrew application: the launcher and the engine's core on
[raylib-nx](https://github.com/luizpestana/raylib-nx) (raylib 6.0 on libnx,
with sound through audren).

## Building

Needs devkitPro with `switch-dev`, `switch-mesa` and `switch-libdrm_nouveau`.

```
cd switch
./raylib.sh        # once: gets raylib-nx 6.0 and builds it into deps/
./ffmpeg.sh        # once: builds FFmpeg 7.1 with the films' formats into deps/
make               # petka.nro
```

## Installing

The game's files go to `sdmc:/switch/petka/` on the SD card, laid out as
the desktop launcher wants them (`ptk-unpack game` makes that folder from
the CD images): the `.STR` stores, `disk1.id`..`disk3.id`, `intro.avi` and
`data1_out/`. Put `petka.nro` in the same folder. Saved games and
`Petka.ini` are written there too.

Start it from the homebrew menu, preferably over a game (holding R while
starting one), so that it has the console's full memory rather than the
album applet's.

`nxlink -s switch/petka.nro` sends it from a computer and shows its output
there.

## An installed title (NSP)

Installed, the game keeps its saved games as the console's games do: the
console asks who plays as it starts, and the saved games, `Petka.ini` and
`extras.ini` are that user's save data of the title -- in Data Management,
kept when the title is reinstalled. The first time, those an NRO left in
`sdmc:/switch/petka/` are taken in. (The NRO itself keeps them on the SD
card: it may run under another game's title.)

`make nsp` makes `petka.nsp`: the same program as an installed title, with
its icon on the home menu among the games, started without the homebrew
menu -- and so always with the console's full memory. The game's files stay
where they are, in `sdmc:/switch/petka/`.

```
./hacbrewpack.sh   # once: hacBrewPack built into deps/, and the boot logo
make nsp           # petka.nsp
```

It needs the console's own keys, `prod.keys`, dumped from it with
Lockpick_RCM, at `~/.switch/prod.keys` or `.switch/prod.keys` at the top of
the repository (kept out of it by `.gitignore`), or where `PETKA_KEYS` says: the
title's contents are encrypted with them. No key of Nintendo's signs it, so
the console runs it only with signature patches (sigpatches) for its
firmware. Install it as any NSP (DBI, Tinfoil, Goldleaf). Its title id is
`0100F35700A50000` (`nsp.sh`, `petka.json`).

`make nsp-full` makes `petka-full.nsp`, the same with the game inside it: one
file to install, about 2 GB, nothing to copy to the SD card. Its files are
those `assets.txt` lists -- the ones the launcher reads, and no others --
taken from the game's folder that `PETKA_ASSETS` names (as the desktop
launcher wants it, `ptk-unpack game` makes it):

```
PETKA_ASSETS=~/ptk1-files make nsp-full
```

Started, it reads the game from inside itself. It holds the original
game's files: it is for your own console, not to be given to anyone.

## Controls

| event | description |
|---|---|
| touch screen | the pointer goes there; a tap is a click |
| left stick, D-pad | move the pointer (the D-pad a pixel at a time); near something small the pointer is drawn to it |
| A | on a thing: the star of actions for it; on the floor: walk there |
| left stick, with the star up | points at an action; A does it, the middle opens the case to pick a thing for it |
| B | the star down, the case shut, a thing put back; in a dialogue, the next phrase |
| up / down in a dialogue's menu | the line to say (A says it) |
| L held / R held | the cursor walks / sends Chapayev |
| X | the case: up, down, left, right go from thing to thing, L / R turn the pages |
| Y | the map |
| + | the panel (menu, options, saving) |
| - | the extras menu: the CRT look, the band under texts, the outline of things, the pull to things |
| ZL / ZR | load / save screen |

The picture is the game's 640x480 at one and a half, 960x720 in the middle
of the screen, through a shader that makes it look like a CRT monitor
(`launcher/src/crt.c`: scanlines, a bulging glass, the phosphor's stripes),
switched on and off in the extras menu (-); on a desktop F4 does it too.

Films are decoded by FFmpeg's libraries built into the program
(`ffmpeg.sh`: the AVI demuxer, Indeo 3, Microsoft Video 1, MS ADPCM and PCM
only); the desktop launcher runs the ffmpeg program instead.

## A log from the console

Put an empty `trace.txt` in `sdmc:/switch/petka/` and the launcher writes
its trace (the script's messages, clips, sounds) and raylib's messages to
`sdmc:/switch/petka/petka.log`, line by line. Remove the file to turn it
off. Started through nxlink (`nxlink -s petka.nro`), the output goes to the
computer instead.

The icon (`icon.jpg`) is the game's box cover, whole, fitted into 256x256
with black bars above and below.
