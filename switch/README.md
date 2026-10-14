# Nintendo Switch build

[По-русски](README.ru.md)

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

## Controls

| | |
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
| - | the extras menu: the CRT look, the band under texts, the pull to things |
| ZL / ZR | load / save screen |
| left stick press | the CRT look on and off |
| right stick press | the debug overlay |

The picture is the game's 640x480 at one and a half, 960x720 in the middle
of the screen, through a shader that makes it look like a CRT monitor
(`launcher/src/crt.c`: scanlines, a bulging glass, the phosphor's stripes);
on a desktop F4 turns it on and off.

Films are decoded by FFmpeg's libraries built into the program
(`ffmpeg.sh`: the AVI demuxer, Indeo 3, Microsoft Video 1, MS ADPCM and PCM
only); the desktop launcher runs the ffmpeg program instead.

## A log from the console

Put an empty `trace.txt` in `sdmc:/switch/petka/` and the launcher writes
its trace (the script's messages, clips, sounds) and raylib's messages to
`sdmc:/switch/petka/petka.log`, line by line. Remove the file to turn it
off. Started through nxlink (`nxlink -s petka.nro`), the output goes to the
computer instead.

The icon (`icon.jpg`) is the game's box cover, cut square in the middle
and brought down to 256x256.
