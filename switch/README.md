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
| left stick, D-pad | move the pointer (the D-pad a pixel at a time) |
| A | click |
| B | right button: the star, putting a thing back |
| L / R | previous / next cursor action |
| X | the case |
| Y | the map |
| + | the panel (menu, options, saving) |
| - | back (Esc) |
| ZL / ZR | load / save screen |
| right stick press | the debug overlay |

The picture is the game's 640x480 at one and a half, 960x720 in the middle
of the screen.

## Not done yet

- films: the desktop launcher has ffmpeg decode them, which the Switch has
  not; they are passed over with a note;
- the icon is libnx's default.
