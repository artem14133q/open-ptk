# Petka (SKIF/Buka, 1998) — `.STR` resource format (SOLVED)

Fully reverse-engineered by decompiling `Petka.exe` in IDA Pro
(`storeInit`/`MultiOpen`/`sub_40F210` in `storeh.cpp`) and cross-checking the
result byte-for-byte against real files in `ptk1-files`. Every file we tested
decodes with **perfect, exact EOF alignment** — this is ground truth, not a
guess. Source filenames below (`D:\GAME\*.cpp`) come straight from the exe's
debug strings.

## Container format

```
offset 0:            "StOR"      magic (4 bytes)
offset 4:             u32 LE trailer_off
                      Absolute file offset where the index starts.
                      NOTE: this already counts the 8-byte header — the
                      payload spans [8, trailer_off), not [8, 8+trailer_off).
[8, trailer_off):     raw resource bytes, addressed ONLY via the index below.
                      Not self-delimited in general — don't scan this region
                      looking for magic numbers, just use the index.
at trailer_off:       u32 count
                      count * 12-byte records:
                        u32 _unused_on_disk   (engine overwrites this with an
                                              in-memory CString pointer at
                                              load time for name lookups;
                                              on-disk it's leftover build-tool
                                              garbage — ignore when parsing)
                        u32 offset            absolute file offset of resource
                        u32 length            byte length of resource
                      count NUL-terminated name strings (CP866), concatenated
                      in the same order as the records, e.g.
                      "PART_1\BOLOTO.BG\BOLOTO.BMP". The last name's NUL ends
                      exactly at EOF — this is the integrity check that
                      confirms the format (verified on BGS1.STR, FLC1.STR,
                      MUSIC1.STR, MAIN.STR; `count` ranges from 8 to 1311
                      depending on the store).
```

Confirmed by decompiling (pseudocode, cleaned up):

```c
// storeh.cpp, storeInit (sub_41EDC0)
AfxTrace("Opening store \"%s\"...\n", lpFileName);
HANDLE f = RawOpen(lpFileName, 2);
u32 magic = ReadU32(f);              // "StOR" == 1380938835
u32 trailer_off = ReadU32(f);
Seek(f, trailer_off, FILE_BEGIN);    // sub_40F210(f, trailer_off, 0) -> absolute seek
u32 total_filesize = GetFileSize(f);
u32 index_bytes = total_filesize - trailer_off;   // header(8) + index size
void *buf = alloc(index_bytes - 4);
u32 count = ReadU32(f);              // store[slot].count
ReadBytes(f, buf, index_bytes - 4);  // count*12-byte records + name blob

// fix up record[i].namePtr to point into the name blob (CString-style,
// NUL-terminated, walked with strlen)
char *names = buf + 12*count;
for (i = 0; i < count; i++) {
    *(char**)(buf + 12*i) = names;
    names += strlen(names) + 1;
}
```

```c
// storeh.cpp, MultiOpen (sub_41F1E0) — resolves a logical name to a handle
// by trying two base paths, then linear-searching every open store's name
// table with _strcmpi against record[i].namePtr (first 4 bytes of the
// 12-byte record, after the fixup above).
```

`QFlic::Init` (`Flic_cc.cpp`, `sub_40F390`) opens FLIC clips by logical name
through this same `MultiOpen` — e.g. `"PART_1\BOLOTO.BG\PENEK.FLC"` — and for
clips >600000 bytes streams them in 600000-byte chunks ("disk flic") instead
of loading the whole clip into memory ("memory flic").

## What this means for the open questions from before

- **There is no separate `resource.qrc` file in the retail build.** The
  per-store name→offset→length table described above *is* the compiled
  resource manifest — baked directly into the tail of every `.STR` by
  whatever build tool produced these archives.
- **`FLC1/2/3.STR` are addressed purely via this trailer index** — the
  payload bytes are not self-delimited, which is why scanning from the front
  looked like noise. Example, real entries from `FLC1.STR` (count=1311,
  covers the whole 66MB file exactly):
  ```
  PART_1\BOLOTO.BG\BOLOTO.CVX     offset=8      len=976    <- convex-hull walk mesh (Walk.cpp)
  PART_1\BOLOTO.BG\PENEK.FLC      offset=984    len=16618
  PART_1\BOLOTO.BG\BEREZA.FLC     offset=17602  len=15178
  PART_1\BOLOTO.BG\PENEK.MSK      offset=112802 len=32     <- mask for PENEK.FLC
  ```
  `.CVX` files are the walkable-area convex polygons used by `Walk.cpp`'s
  pathfinder (`"Point doesn't belong to any convex!"`) — one per background.
- **`BGS*.STR`'s "mystery second blob" is simply the next named background**,
  not a companion mask/zbuffer for the first one. `BGS1.STR` (count=34,
  covers the whole 20MB file exactly) is just 34 full 640x480 BMP
  backgrounds for Part 1, e.g. `PART_1\BOLOTO.BG\BOLOTO.BMP`,
  `PART_1\LENIN.BG\LENIN000.BMP`, `PART_1\LENIN.BG\LENIN010.BMP`,
  `PART_1\LENIN.BG\LENIN.BMP` (one `.BG`-named group can hold multiple room
  variants).
- **`MAIN.STR`** (count=814, covers the whole file exactly) turns out to mix
  the splash-screen assets we found manually (`MAIN\BG1.BMP`, `MAIN\NEXT.FLC`,
  `MAIN\BACK.FLC`) with per-part bootstrap files: `DISK0.ID`, `PARTS.INI`,
  and (surprisingly) `PART_0\BGEDIT.BG` / `PART_0\BACKGRND.BG` /
  `PART_0\SCRIPT.DAT` / `PART_0\BGDATA.DAT` — editor/metadata files for a
  "Part 0" that isn't exposed in the normal `[Part N]` config, plus a stray
  individual speech line (`SPEECH3\03_01401.WAV`). The "UI label table" we
  decoded earlier by hand is just more named entries in this same index, not
  a separate special structure — no need to special-case it anymore.
- `MUSIC1.STR` (count=8, covers the whole file exactly) has just 8 real
  named tracks (`MUSIC1\BOLOTO.WAV`, `MUSIC1\DEREVNIA.WAV`, ...) — longer,
  whole-track files, not hundreds of short clips.

## BMP pixel format — SOLVED: a private 16bpp layout behind a standard header

The background and UI images open like Windows BMPs -- `BM`, a 40-byte
`BITMAPINFOHEADER`, `bfOffBits=54`, `biBitCount=16`, `biCompression=0`, no
BITFIELDS -- but the 16bpp ones (111 of the 115 images) are not:

```
0   standard 54-byte BMP header (bfSize and biSizeImage are stale, ignore)
54  u32 tag            0 or 2; no difference found between the two
58  u32 file length    the real one
62  pixels             height rows, bottom-up, each width*2 bytes -- NOT
                       padded to a multiple of 4
    (two zero bytes follow in the tag=2 files)
```

Each pixel is a **big-endian** 16-bit word, `BBBBB GGGGGG RRRRR`.

**Row padding went through the conversion.** The files were made from
24-bit BMPs (the stale `bfSize` is still the 24-bit file's) by packing the
pixel bytes three at a time into a word -- all of them, the bytes that pad
a 24-bit row to a multiple of four included. Nearly every width gives rows
with no padding (640, 1296, 2012, 2116), and there the words are simply the
pixels. Two backgrounds are 1349 wide, `RESTORAN.BG\RESTORAN.BMP` and
`FONTAN.BG\FONTAN.BMP` of part 2: 4047 bytes a row and one of padding, so
the file holds 4048 * 480 / 3 = 647680 words (and one more), not
1349 * 480, and the words slip a byte further on every row. Read as rows of
1349 words the picture comes out grey and striped, the three colours
changing places from row to row with a period of three. The decoder reads
such a file back as the stream of bytes it was made from (byte `i` is
component `i % 3` of word `i / 3`) and takes the pixels out of that at the
24-bit row length, which gives the colours back -- at five or six bits a
component, whichever word a byte fell into. What the exe makes of these two
files has not been read.

How each part was found:

- The word layout came first, by rendering every plausible byte and channel
  order and comparing. Standard decoders (stb_image, which raylib uses) assume
  little-endian 555/565 and show per-pixel colour noise.
- The 8 bytes at 54 were missed at that point, so pixels were read from 54:
  every row came out 4 pixels to the right, with the end of the row below
  wrapped into a strip down the left edge of every background. The second
  word being exactly the file length (614464 for a 640x480 image) is what
  gives the block away; with pixels read from 62 the object clips, which are
  cut from the same paintings, match the background best at a shift of
  exactly 0 (measured over all clips of `P-ROOM.BG` and `BOLOTO.BG`; before,
  the background sat 4 pixels off).
- The missing row padding only matters for odd widths. `FONTAN.BMP` and
  `RESTORAN.BMP` are 1349 wide: `62 + 480*2698 + 2` is their exact length,
  and with padded rows they do not fit in the file at all.

Three images are ordinary palette BMPs instead: `BANIA-0.BMP` and
`KARTA000.BMP` (8bpp, palette at 54, pixels at 1078, padded rows) and the
1bpp `MAIN\BG1.BMP`, which is the only one `petka_bmp_decode` does not
handle.

Implemented in `core/src/bmp.c` (`petka_bmp_decode`), which recognises the
private layout by the length field and decodes to top-down RGB888.

## FLIC frame codec — SOLVED (standard format, no reversing needed)

Decompiled `sub_410950` (`Flic_cc.cpp`'s memory-flic header parser, called
from `QFlic::Init`). Its field offsets are an exact match for the **public,
documented** Autodesk Animator FLI/FLC header:

```
offset 0:  u32  total size
offset 4:  u16  magic (0xAF11=FLI, 0xAF12=FLC)
offset 6:  u16  frame count
offset 8:  u16  width
offset 10: u16  height
offset 16: u16  speed (FLI: in 1/70s jiffies, x14 to convert; FLC: ms directly)
offset 80: u32  offset to frame 1 (relative to start of this header)
offset 84: u32  offset to frame 2
```

`a2[399] = sub_410820` stores a function pointer to the actual per-frame
decoder — never pulled, and **didn't need to**: verified directly against
real bytes that `.FLC` entries inside `FLC1.STR` (e.g. `PENEK.FLC` at
offset=984) start with this exact header (`magic=0xAF12`, `size=16618`
matching the index's `length` field exactly). Since this is the same
well-known FLI/FLC format used by countless public decoders (e.g. ffmpeg's
`libavcodec/flicvideo.c`), frame decoding (chunk types `BYTE_RUN`, `SS2`,
`COLOR_256`, etc.) can reuse any existing public-domain implementation
instead of reversing `sub_410820`.

**Fixed decoder bug:** `decode_byte_run` (the `BYTE_RUN`/chunk-15 decoder)
had an off-by-one on its "replicate" packet (`n >= 0`): pixel count was
`n + 1`, should be `n`. First caught as visible corruption (horizontal
smearing/tearing) in `PART_1/P-ROOM.BG/KROVATI.FLC` (a single-frame FLC,
no inter-frame delta involved) — confirmed it was a decode bug and not a
launcher scaling/draw issue by dumping the raw decoded frame via
`flictest` (`tools/flictest`) and seeing the same smearing in the `.ppm`
output. The bug was subtle to isolate: on flat-colored regions (most of
any frame) an off-by-one on run length barely matters visually and total
chunk byte-consumption still comes out *almost* right over hundreds of
packets, so plain/simple test FLCs (campfire, pond ripple, birds,
signposts) all looked and decoded fine; it only became glaring on
`KROVATI.FLC`'s patchwork-quilt region, which is dense with many short
runs, where the per-packet error compounds into a growing horizontal
offset within each row. Root-caused by independently re-decoding the raw
chunk bytes in Python (bypassing the C code entirely) and bisecting across
candidate conventions (packet-count-authoritative vs `x < width`-bound
loop; `n`/`n+1`/`-n`/`-n-1` run lengths) until total bytes consumed
matched the chunk size exactly *and* the rendered image was coherent, not
just byte-accounted-for -- byte accounting alone falsely "validated" a
still-broken variant (loop bound fix without the length fix) earlier in
the investigation.

**Fixed decoder bug: looping.** After a clip's last frame the decoder
restarted from the *second* frame's chunk -- it applied the "first to
second" changes to the last frame's picture. From the second loop on, every
looping clip showed frame `n + 1` while reporting frame `n`, and kept stray
pixels left over from the end of the loop. It went unnoticed for a long time
because most scenery clips change little from frame to frame. It became
glaring once heroes were drawn by the per-frame foothold table of their walk
clips: a walking hero jumped by tens of pixels and left debris behind after
his first few steps -- found by a person actually playing, not by any
automated run.

A FLIC file does carry a "ring" chunk after the last frame, the changes from
the last frame back to the first. For 1219 of the game's 1224 multi-frame
clips playing it reproduces the first frame exactly; for five long one-shot
clips (`ZAKUR000`, `SVCH-1-1`, `AERO4`, `TONNEL-8`, `LABOR03`) it does not
quite. The decoder therefore returns to the first frame by decoding it
afresh, which is exact for all of them. `tools/looptest` checks that every
clip's second loop matches its first pixel for pixel.

## `.MSK` mask files — SOLVED: per-frame rects, used for draw order

```
for each frame of the clip:  i32 count, then count x (u16 left, top, right, bottom)
one u32 per frame:           offset of that frame's list -- the FIRST frame's entry is LAST
16 bytes:                    i32 left, top, right, bottom -- the rect covering the whole clip
```

The per-frame lists are the parts of the frame the object occupies in that
frame. 977 of the 979 masks in `FLC1.STR` and `MAIN.STR` fit this to the
byte; the other two (`BANG--.MSK`, `BANG1.MSK`) are the same without the
final 16 bytes. Across all four stores that is 2331 clips and 55,725 frames,
none with a rect reaching past its frame; about 1,400 frames have no rects
(the object is not in the picture).

An earlier pass read only the final rect (from `QFlic::Init`) and took the
rest for editor padding -- "28 repeats of the same rect followed by a
countdown" is 28 one-rect frames followed by their offset table.

The rects are **not a transparency mask**: that is the clip's palette index
0. They are what objects are ordered by. Each frame, before the screen's
objects are sorted by `z`, every object's z update runs (`sub_401810` for
scene objects): if the object is animated, visible and has its automatic-z
flag set (`+52`, the first argument of `ZBUFFER`), then

```
z = max(1, sprite_y + bottom)   over the current frame's rects
```

so an object whose lowest point is lower on screen is drawn later, on top --
and the order follows the animation frame by frame. With the flag off, `z`
stays what it was last set to (`ZBUFFER`'s second argument, or the value
from `BACKGRND.BG`). The flag starts **on**: no constant for it was found in
the constructor, but the script switches it off 479 times and on 22 times,
and over a thousand scene objects get neither a `ZBUFFER` nor a `z` in
`BACKGRND.BG` yet must stack correctly (the bed in the foreground of Petka's
room, in front of anyone standing behind it).

Implemented in `core/src/msk.c` (`petka_msk_rect`, `petka_msk_frame_bottom`).
The heroes have their own z update (`sub_409530`), under the same three
conditions (animated, visible, automatic z):

```
walking (+164):  z = y of the point returned by sub_424210: the walk's
                 current position (two doubles, +352 and +360 of the walk
                 object), each cut to an integer
otherwise:       z = sprite_y + (i16)(frame_height * scale)
```

A standing hero's sprite is put at `y - frame_height * scale`
(`sub_4087D0`), so both come to the y of his feet -- and there is no floor of
1 here. The script does use the flag on heroes: `ZBUFFER 0 441` pins Petka
once, and nine `ZBUFFER 1` hand the heroes back to the automatic order.

## `.CVX` convex walk-mesh — SOLVED (byte-exact, geometry verified)

The walkable floor of a background, cut into convex polygons. Loaded by
`sub_423390` (`Walk.cpp`), which builds the name from the background's own:
`P-ROOM-1.BMP` -> `P-ROOM-1.CVX`, same folder (there is no `.cvx` string
literal in the exe). A room with several backgrounds has a mesh for each
(`LENIN.CVX`, `LENIN000.CVX`, `LENIN010.CVX`).

```
u32 countA
countA * (i32 x, i32 y)            -- pool A: corners of the floor's outline, in order
u32 countB
countB * (i32 from, i32 to)        -- pool B: edges, as index pairs into pool A
u32 poly_count
poly_count * u32                   -- entry count per polygon
for each polygon:
  N * i32 index                    -- an edge: index into pool B
```

Pool B starts with the outline itself -- its first `countA` edges are
`(i-1, i)` -- followed by the diagonals that cut the outline into convex
pieces. A polygon is listed by its edges, outline edges and diagonals
alike, in no particular order: its corners are the ends of those edges, and
since it is convex, sorting them by angle around the centroid gives the
ring. Two polygons that list the same diagonal are neighbours, and that
diagonal is the way between them.

(An earlier reading took an index below `countA` for "corner `i`" rather
than "outline edge `i`". Edge `i` ends at corner `i`, and every corner of a
polygon is the end of two of its edges, so both readings give the same
corners -- but the exe's `sub_425120` looks an outline edge's number up in
these lists, which settles it.)

Checked on the 33 meshes of `PART_1`: the outline edges are as described,
every diagonal a polygon lists is listed by exactly two, and the polygons'
areas add up to the outline's area to the pixel. All 85 meshes of the three
parts parse to their exact length, and `walktest` routes 425,000 random
point pairs across them without one route missing or leaving the floor. Two
meshes (`OKNO.CVX`, `VREMUNKY.CVX`) contain a polygon
that is concave by a pixel or two -- level-editor slack, harmless.

Implemented in `core/src/walk.c`: `petka_walkmesh_open` and
`petka_walkmesh_path`, which builds a route the way the exe does
(`sub_423650`):

1. Each end gets its polygon; an end that is off the floor is first moved
   onto it (`sub_425120`).
2. If both are in one polygon, the route is the straight line.
3. Otherwise the chain of polygons between them is found. The exe searches
   depth-first, taking each polygon's neighbours in turn and refusing only
   to step straight back. In all 85 meshes the polygons form a tree (one
   diagonal fewer than polygons), so there is exactly one chain; the
   implementation finds it with Dijkstra.
4. The route is the start, the **midpoint of each diagonal crossed**, and
   the goal.
5. It is straightened as it is built: after each point is added, if the
   point two back can be joined to it by a straight line that stays on the
   floor (`sub_424E40`), the point in between is dropped.

So the hero keeps to the middle of doorways and gaps instead of cutting
corners. The stays-on-the-floor test (`sub_424E40`) is: the line must not
intersect any edge of the outline, taken in order and closed back to the
first point (identical points pass at once). Two segments intersect
(`sub_424AE0`) if their lines cross at a point lying on both, ends included
-- touching counts -- and never if they are parallel, even on one line.

Whether a point is off the floor (`sub_424F10`) is decided by a vote: the
point is joined to eight points on the edge of the 640x480 screen -- the
three on the left edge at rows 0, y and 480 (the last one twice), the three
on the right edge, and the one straight above at row 0 -- and each line
whose crossings with the outline (`sub_425000`) come to an odd number votes
"inside"; fewer than four such votes means off the floor. The implementation
instead asks whether the point is in one of the mesh's polygons, which
agrees except on the outline itself, and does not inherit the vote's blind
spot in rooms wider than the screen, where some of those eight points lie
on the floor.

Moving an off-floor point onto the floor (`sub_425120`): the nearest point
of every outline edge is found (`sub_4252F0`), the closest of them replaces
the point (truncated to whole pixels), and the polygon is the one that lists
that edge. Such a point lies on the outline and so touches it: a route to or
from it is never straightened through it. (The implementation remembers
that an end was moved instead of relying on the touch being detected:
truncation can leave the point a fraction of a pixel outside the edge, and a
line between two such points across a bay of the outline crosses nothing.
Whether the exe has that hole was not checked.)

`walktest` routes 425,000 random point pairs with it,
none missing and none leaving the floor.

## Resource-ID indirection — SOLVED: `RESOURCE.QRC` is plain text

`PART_0/RESOURCE.QRC` (found inside `MAIN.STR`'s index, 2950 bytes) is a
**plain human-readable text file**, not binary:

```
10      =  MAIN\BG1.BMP
30000   == MAIN\MAINHERO\v__f.FLC
30010   == MAIN\WALK\W-W-N.FLC
30011   == MAIN\WALK\W-W-NE.FLC
...
6000    == MAIN\CASE\BRIFCASE.FLC
5000    == MAIN\STAR\SUPRSTAR.FLC
4900    =  MAIN\INTRFACE.BG\INTRFASE.BMP
```

This closes the full resource pipeline end to end:

```
numeric resource ID (used by game/script logic)
  -> RESOURCE.QRC text lookup -> virtual path (e.g. "MAIN\WALK\W-W-N.FLC")
  -> MultiOpen searches every open store's name table (see above)
  -> store's trailer index -> (offset, length) within that .STR file
```

Only `MAIN.STR` has a `RESOURCE.QRC` entry (checked `BGS1/FLC1/MUSIC1/SFX1/
SPEECH1.STR` — none). Per-part gameplay resources are evidently referenced
directly by virtual path (no numeric ID layer) rather than through a
per-part `RESOURCE.QRC` — the ID indirection seen above seems specific to
the startup/menu UI (`MAIN\WALK\*`, `MAIN\CASE\*`, `MAIN\STAR\*`,
`MAIN\INTRFACE.BG\*` — all menu-screen assets).

## `CAST.INI` / `NAMES.INI` — SOLVED: plain text, keyed by object name

Both are plain `cp1251` (not cp866!) INI text with a single `[all]` section
whose keys are `SCRIPT.DAT` object names. `PART_1` and `PART_3` keep them in
`MAIN.STR`; `PART_2` keeps them in its chapter stores (`CHAP21.STR`,
`CHAP22.STR`), one pair per chapter.

- **`NAMES.INI`**: `<object name> = <name shown to the player>`. Many objects
  share one shown name: the key on the floor ("Большой ключ") and the same
  key in the inventory ("IБольшой ключ") are both "Ключ". About two thirds of
  the objects have an entry (`PART_1`: 354 of 562); the rest are props and
  helper objects the player is never told the name of.
- **`CAST.INI`**: `<object name> = <R> <G> <B>`, the colour of that object's
  lines in a dialogue, e.g. `Петька = 255 255 0`, `Василий Иванович = 0 255 0`.
  Every object that speaks a phrase in `DIALOGUE.LOD` has one, in all three
  parts.

The exe reads both with `GetPrivateProfileString` when a chapter is loaded
(`sub_411C00`) and stores the results on each object (`+84` name, `+88`
colour as `R | G<<8 | B<<16`), so lookups are case-insensitive: the room
"БАНЯ" finds the key "Баня". `core/src/ini.c` (`petka_ini_get`) follows the
same rules.

## `SCRIPT.DAT` / `BACKGRND.BG` — SOLVED: this is the game script

Found in `MAIN.STR` under every `PART_N`. An earlier pass wrote these off as
level-editor leftovers because `strings` only shows debug-panel labels and
character names; those are simply object names. Both files parse **byte-exact
to EOF** in all four parts, and the semantics below come from decompiling
`Base.cpp` in `Petka.exe`. Implemented in `core/src/script.c`
(`petka_script_open`, `petka_scriptvm_*`); `tools/scripttest` runs it.

(`BGDATA.DAT` and `BGEDIT.BG` in the same folders are still unexamined.)

### `SCRIPT.DAT`: objects, reactions, messages

```
u32 object_count
u32 room_count
(object_count + room_count) × object, plain objects first, then rooms:
    u16 id
    u32 name_len, name bytes (CP1251, no NUL)
    u32 reaction_count
    reaction_count × reaction:
        u16 opcode        -- the message this reaction answers
        u8  status        -- 0xFF = any, else the object's status must match
        u16 sender        -- 0xFFFF = any, else the sender's id must match
        u32 message_count
        message_count × { u16 obj_id, u16 opcode, u16 arg1, u16 arg2, u16 arg3 }
```

`PART_1`: 507 objects + 55 rooms, 1127 reactions, 3039 messages. Ids 0 and 1
are Petka and Chapayev in every part.

### `BACKGRND.BG`: which objects live in which room

```
u32 room_count
room_count × { u16 room_id, u32 n, n × { u16 obj_id, i32 x, i32 y, i32 z,
                                         i32 walk_x, i32 walk_y } }
```

`walk_x`/`walk_y` is where a hero stands to interact with the object (`-1` =
none); `z` is the draw order. In memory this is the list the engine draws
from: `loadRoom` (`sub_413870`) puts the room and its attached objects on
screen and removes the previous room's.

The file is read by the main screen's constructor (`sub_413250`,
`Intrface.cpp`), straight into the objects: `x` -> `+12`, `y` -> `+16`,
`z` -> `+8`, `walk_x` -> `+20`, `walk_y` -> `+24`; the room keeps only the
list of ids (28-byte records at screen `+40`, `room_count` at `+36`). `x`
and `y` are an object's position on the background, the offset its clip is
drawn at and the one the hit test and the z update work from -- and they
are **0 in every one of the 1613 entries** of the three parts: every clip
is a full frame already in place. Only the heroes ever stand anywhere else
-- and the objects of the panel screen, which the exe places from a table
of its own (see "The panel command").

The same constructor starts the screen's object list (`+4`) with three
objects that are on every screen of the game: the cursor (`QSystem+140`)
and the two panel objects (`+144`, `+148`); and it builds the dialogue
object inside the screen, at `+44`.

### How a message is executed

Everything goes through one queue in `QSystem`:

- `sub_405060` appends a message for one object; `sub_405370` appends one for
  **every** object (a broadcast).
- `sub_405680` walks the queue front to back and calls the target's
  `processMessage` (vtable slot 13) -- but only if the object's busy flag
  (`+56`) is clear. A message for a busy object stays queued and later ones
  overtake it. Messages posted during delivery are handled in the same pass.
- `QMessageObject::processMessage` (`sub_403270`) runs every reaction whose
  opcode, status and sender match, posting its messages in order, then
  applies the message's own built-in effect.

Three opcodes are conditions, evaluated **when the reaction is posted** (so a
`STATUS` earlier in the same reaction has not taken effect yet): `CHECK` stops
the reaction unless the target's status equals `arg1`; `IF` stops it unless
its arguments match the incoming message's (`-1` = any); `RANDOM` replaces its
own `arg1` with `rand() % arg2`.

Four kinds of message park the rest of the reaction until something finishes:

| message | rest is parked on | resumed by |
|---|---|---|
| `PLAY` | the target object (`+116`, waits for resource `+120`) | `END` naming that resource |
| `DIALOG` | one global slot (`dword_4D4178`) | end of dialogue (`sub_4165A0`) |
| `WALK`, `WALKTO` | Petka (`+172`) | `QM_WALKED` |
| `WALKVICH` | Chapayev (`+172`) | `QM_WALKED` |

`sub_404790` resumes a parked tail; it honours `CHECK` but not `IF`/`RANDOM`.
For walks, `QM_WALKED` is queued right behind the walk message and waits
there because starting a walk (`sub_408BB0`) sets the hero's busy flag; a walk
shorter than 5 px, or by a hidden hero, never sets it, so the script goes
straight on.

If an object has no matching reaction but `DIALOGUE.FIX` has an entry for
(object id, verb), that dialogue starts instead. For `OBJECTUSE` the verb key
is `(item_id << 16) | 26` -- the "5th verb" described in the dialogue section.

### How a click becomes a message

A click on an object (`sub_401B30`, a virtual method of the object) first
stops both heroes, then posts one message to the object, chosen by the
cursor's current action (`+128` of the cursor object, `QSystem+140`):

| action | message | arguments | sender |
|---:|---|---|---|
| 0 | `LOOK` | 0, 0, 0 | the object itself |
| 1 | `WALK` | x, y of the click | the object itself |
| 2 | `USE` | 0, 0, 0 | the object itself |
| 3 | `TAKE` | 0, 0, 0 | the object itself |
| 4 | `TALK` | 0, 0, 0 | the object itself |
| 5 | `OBJECTUSE` | x, y of the click | Chapayev |
| 6 | `OBJECTUSE` | 0, 0, 0 | the inventory object on the cursor (`+124`) |

The cursor is an object like the others, made by `sub_402280` after
`TOTALINIT` has gone round: id 4097, name `Cursor`, placed at the mouse,
`z` 1000 so that it is drawn over everything, **action 0** and clip 5002.
The clips sit in `RESOURCE.QRC` in the order of the actions:

| resource | clip | |
|---:|---|---|
| 5000 | `MAIN\STAR\SUPRSTAR.FLC` | |
| 5001 | `MAIN\STAR\KURSOR1.FLC` | a plain pointer |
| 5002 | `MAIN\STAR\EYES.FLC` | action 0, look |
| 5003 | `MAIN\STAR\RUN0.FLC` | action 1, walk |
| 5004 | `MAIN\STAR\HAND11.FLC` | action 2, use |
| 5005 | `MAIN\STAR\HAND21.FLC` | action 3, take |
| 5006 | `MAIN\STAR\ROT-2.FLC` | action 4, talk |
| 5007 | `MAIN\STAR\CHIP-KUR.FLC` | action 5, send Chapayev |

The action is changed by six `WM_COMMAND` handlers (`0x417E80` ...
`0x418100`, found by their writes to cursor `+128`; they sit in an MFC
message map with command ids 32780-32785). Each does the same: if the panel
object (`QSystem+148`) is active (`+60`) and the main screen is the current
one, hide the cursor, give it the clip `5002 + action`, set the action, show
it again. The keys come from the exe's accelerator table (resource 130):

| key | command | |
|---|---:|---|
| `1`, `L` | 32780 | action 0, look |
| `2`, `W` | 32781 | action 1, walk |
| `3`, `G` | 32783 | action 3, take |
| `4`, `U` | 32782 | action 2, use |
| `5`, `T` | 32784 | action 4, talk |
| `6`, `C` | 32785 | action 5, send Chapayev |
| `I` | 32779 | opens and shuts the case (below) |
| `M`, `Tab` | 32776 | `sub_417D80`, also the case's map button: to the map and back (below) |
| `O` | 32777 | `sub_417D20`, also the case's diskette button: to the panel screen and back (below) |
| `Esc` | 32786 | `0x418190`: hides the case, then, unless the current screen is the sequence screen (`QSystem+540`) or the start-up one (`+556`), calls its "leave" (slot `+56`). For the main screen that is `0x413860`, which only takes a description down if one is up (`+104`); for the map, the panel and the save/load screen it is the way back to the game -- all three leaves are the same: scroll, width, cursor clip and action restored, the previous screen made current |
| `F2` | 32771 | `0x417B80`: if the star is active and the main screen is current -- hides the case, draws a frame, takes the slot picture (`sub_41E230`), sets the save/load screen up for **saving** and makes it current |
| `F3` | 32772 | `0x417C00`: if the main screen is current, sets the save/load screen up for **loading** and makes it current (no other condition) |
| `Alt+R` | 32787 | `0x4181F0`, a way out of a stuck game: clears the dialogue's pause (`+4` of the dialogue, i.e. main screen `+48`), shows the cursor, makes the star active and redraws |

The launcher takes the twelve action keys, on the same condition, and
`I`, `M`/`Tab`, `O`, `Esc`, `F2`, `F3` and `Alt+R` as described. (The
handlers of `F2` and `F3` are in the same message map, at `0x4B9938` and
`0x4B9950`, just before the others.) (The
cursor's vtable is at `0x4B9268`; its slot `+44`, `sub_4026E0`, is "show or
hide": it marks the cursor's rect for redrawing and sets `+44`.)

Where the clip goes:

- **moving** (`0x402570(x, y, centre)`, vtable slot `+64`, called by the
  mouse functions with `centre` = 1): the view's scroll (`QSystem+104`) is
  taken off `x`, and then the clip is centred on the point -- the cursor's
  position becomes `point - rect.topleft - rect.size / 2`, `rect` being the
  clip's covering rect from its `.MSK`. If the cursor's animate flag (`+48`)
  is clear the clip is put back on its first frame; that flag is what the
  mouse-move function sets to "there is an object under the pointer". So
  the pointer's picture **only moves while it is over something**;
- **drawing** (`sub_4023D0`, slot `+20`): only if the cursor is shown
  (`+44`); the frame's corner goes to the cursor's position, clipped to the
  640x480 screen, with the clip's transparent colour keyed out.

The launcher keeps its actions in this order, starts on "look", and draws
the pointer this way (`draw_cursor`): the action's clip, or the clip a
`CURSOR` message gave it for the inventory item it carries, or the plain
pointer 5001 over a dialogue menu (that last choice is the launcher's; what
the dialogue sets the cursor to was not decompiled).

**The star.** The object at `QSystem+148` -- called "the panel object" in
these notes wherever its active flag (`+60`) is what lets the action keys,
the case and the map work -- is the star: a menu of actions that comes up at
the pointer. Its constructor (`sub_414C70`) gives it id 4098, clip 5000
(`MAIN\STAR\SUPRSTAR.FLC`, 181x176, 7 frames), `z` 999, fixed z, hidden but
active, and six rectangles inside the clip (`+124` on):

| # | left, top - right, bottom | a click on it |
|---:|---|---|
| 0 | 70,74 - 112,112 (the middle) | shows the case |
| 1 | 68,0 - 114,41 | action 0, look |
| 2 | 151,51 - 180,97 | action 1, walk |
| 3 | 138,125 - 179,166 | action 2, use |
| 4 | 55,145 - 96,175 | action 3, take |
| 5 | 11,79 - 40,118 | action 4, talk |

- **The right button** on the main screen (`sub_414AF0`, the screen's slot
  `+52`) does nothing unless the star is active. If the case is shown and
  the cursor carries a thing (action 6), the thing is let go and the cursor
  becomes "take" (clip 5005, action 3). Otherwise, if the star is hidden it
  is placed with its middle at the pointer -- kept inside the screen: x from
  0 to `639 - width`, y from 0 to `479 - height` -- and then its visibility
  is flipped: the right button shows the star and takes it away again.
  There is no stepping through the actions one by one;
- its **hit test** (`0x414EC0`) is "am I shown": up, it takes every click;
- **the cursor is over you** (`0x414ED0`) finds which rectangle holds the
  pointer and puts the clip on frame `(n + 1) % 7 + 1` -- frame 1 for none,
  `n + 2` for rectangle `n`, the star with that point lit;
- its **click** (`0x414FD0`): rectangle 0 shows the case; 1-5 hide the
  cursor, give it the clip `5001 + n` and the action `n - 1`, and show it;
  and whatever was clicked, the star is hidden.

"Send Chapayev" is not on the star: it is the case's first button, and keys
`6` and `C`. The launcher has the star (`star_toggle`, `star_click`,
`draw_star`).

**The case.** The object at `QSystem+144` (made by `sub_4150A0`, right
after the cursor) is the briefcase the game keeps its panel in: id 4099,
clip 6000, `z` 980 (under the cursor's 1000, over everything else), fixed
`z` (`+52` = 0), and **hidden** to begin with (`+44` = 0). It holds a word
array (`+132`) -- by its place the ids of the inventory -- and two fields
set to `-1` (`+124`) and 0 (`+128`). Its pictures:

| resource | clip |
|---:|---|
| 6000 | `MAIN\CASE\BRIFCASE.FLC`, the case itself |
| 6001 | `MAIN\CASE\CHIPPER.FLC` |
| 6002 | `MAIN\CASE\DISKETA.FLC` |
| 6003 | `MAIN\CASE\MAP-CASE.FLC` |
| 6004 | `MAIN\CASE\QUIT.FLC` |
| 6005, 6006 | `MAIN\CASE\SKROLL-R.FLC`, `SKROLL-L.FLC` |
| 6007 | `MAIN\CASE\POLOSKA.FLC` |
| 6009 | `MAIN\CASE\EXITCASE.FLC` |

Every one of these clips is a full 640x480 frame, so they all go at the
case's own position and line up by themselves; where a button is on screen
is its clip's `.MSK` rect:

| | clip | rect (left, top - right, bottom) | frames |
|---:|---|---|---:|
| | `BRIFCASE` | 29,3 - 579,435 | 1 |
| button 0 | `CHIPPER` | 138,46 - 243,146 | 4 |
| button 1 | `DISKETA` | 362,55 - 477,143 | 1 |
| button 2 | `MAP-CASE` | 254,58 - 361,140 | 1 |
| button 3 | `QUIT` | 240,348 - 356,400 | 1 |
| button 4 | `SKROLL-R` | 351,356 - 442,396 | 10 |
| button 5 | `SKROLL-L` | 143,360 - 237,403 | 10 |
| | `POLOSKA` | 44,338 - 563,348 | 1 |

The case covers most of the screen: three buttons along the top, the
strip, the two scroll arrows and "quit" along the bottom, and the space
between -- about y 146 to 338 -- left for the things carried.

**Drawing** (`sub_415280`): only while the case is shown (`+44`). First the
case's clip, as any object (`sub_401230`). Then, if a button is lit -- `+124`
holds its number 0-5, `-1` for none -- the clip `6001 + number` is drawn
over it at the same position, but only inside the rects of that clip's
current frame (the per-frame lists of its `.MSK`), and only where the
screen is being redrawn. So a button's clip is its lit state, and the plain
case picture is all of them unlit.

**A click** on the case (`sub_4151B0`) does what the lit button says:

| button | | |
|---:|---|---|
| 0 | `CHIPPER` | the "send Chapayev" command (`sub_418100`, the same as keys `6` and `C`) |
| 1 | `DISKETA` | command 32777 (`sub_417D20`, the same as key `O`): the panel screen |
| 2 | `MAP-CASE` | command 32776 (`sub_417D80`, the same as keys `M` and `Tab`; not decompiled -- the map) |
| 3 | `QUIT` | hides the case (its own slot `+44` with 0): it quits the case, not the game |
| 4 | `SKROLL-R` | if there is a thing at `first + 6`, `first += 6`; lay the things out again (`sub_415900`) and redraw |
| 5 | `SKROLL-L` | if `first > 0`, `first = max(0, first - 6)`; the same |

`first` is `+128` of the case: the things are shown **six at a time**, and
the arrows turn the page. With no button lit a click on the case does
nothing here -- the things in it are objects in their own right and take
their own clicks.

The case is opened and shut by command 32779, key `I` (`0x417E30`, read
from the disassembly): unless the start-up screen (`QSystem+556`) is the
current one, and only while the panel object is active, it calls the current
screen's slot `+56` and then shows the case if it is hidden and hides it if
it is shown.

**The things** (`sub_415900`, run when the case is shown and when a page
is turned): every object with `z` = the case's + 1 is taken off the main
screen's list, and then the six things of the page -- ids `first` ..
`first + 5` of the case's word array -- are put on it with that `z` (981)
and a place from a table at `0x4C9E70`, the view's scroll added to x:

```
(120,145) (240,145) (360,145)
(100,220) (240,220) (380,220)
```

A thing's clip is a 132x129 frame of two pictures. Being ordinary objects
on the screen's list, the things are hit-tested and clicked like anything
in a room: a click posts the cursor's action to the thing (`TAKE` is what
the script answers with `CURSOR`, putting the thing on the pointer).

The rest of the case's methods (vtable `0x4B9838`), read from the
disassembly:

- **hit test** (`0x415270`): just "am I shown". An open case takes every
  point of the screen that nothing above it takes, so the room behind it
  cannot be clicked;
- **the cursor is over you** (`0x415550`), which lights the buttons. If the
  pixel under the pointer in `EXITCASE.FLC` (6009) is not transparent --
  that clip is a mask, opaque everywhere *outside* the case -- button 3,
  "quit", is lit: a click anywhere off the case shuts it. Otherwise the
  first of the buttons 0-5 whose current frame's first `.MSK` rect holds
  the pointer; none, and nothing is lit. Button 0 is not lit while
  Chapayev is not visible (`QSystem+388`), nor button 2 while the current
  room (`QSystem+152`) is not on the map (its flag `+144`, below). A button
  losing the light is put back on its first frame;
- **show / hide** (`0x415BB0`): the case's x is set to the view's scroll.
  Shown, the things are laid out and the object named
  `Полоска в чемодане` (object 2 of the script, clip 6007, the strap) is
  put on the list with `z` = the case's + 2, over the things. Hidden,
  everything with those two `z` values is taken off the list again.

The launcher has all of this (`draw_case`, `case_hover`, `case_click`), with
key `I`, except the two buttons whose commands are not decompiled, which
only say so. It lights a button by its clip's covering rect rather than the
first rect of the current frame.

**The map command** (`sub_417D80`: 32776, keys `M` and `Tab`, and the
case's map button) does nothing on the start-up screen, with the panel
object inactive, or in a room whose flag `+144` is clear. That flag is "this
room is on the map": a room's constructor (`sub_401D00`) sets it to 1 and
the script clears it with `NOMAP` in the rooms one cannot leave by the map
(3 in `PART_1`, 33 messages in `PART_3`). Otherwise the command hides the
case and then, on the main screen, clears the caption under the cursor and
starts the map screen (`QSystem+548`, its slot `+24`); on the map screen, it
leaves it (that screen's slot `+56`). So `M` goes to the map and back. The
launcher does the same (`map_command`).

**The panel command** (`sub_417D20`: 32777, key `O`, and the case's
diskette button) has the map command's shape: nothing on the start-up
screen or with the panel object inactive; otherwise the case is hidden and,
on the main screen, the screen at `QSystem+544` is started (its slot `+24`),
or, on that screen, it is left (slot `+56`). There is no "on the map"
condition here.

That screen (constructor `sub_41C020`, vtable `0x4B9DF0`) is the object the
walk-speed slider and the subtitle flag are read from, and its pictures are
the `MAIN\INTRFACE.BG` set: the background `INTRFASE.BMP` (4900), the
pointers `KURSOR1`/`KURSOR2` (4901, 4902), `NEW-GAME`, `LOADING`,
`CONTINUE`, `DOOR-E`, `SAVER` (4903-4907), the sliders and switches
`EFFEKTS`, `MUSIKA`, `CUBTITRA`, `SPEAKE`, `SPEED` (4909, 4913-4916) with
their end marks `SL1MIN` ... `SL4MAX` (4921-4928), and the slot pictures
`LOAD1-3`, `SAVE1-3` (4990-4997). So it is the game's menu, options and
save/load in one.

Like the map, it is a room of the script put up by its own screen. The room
is 511 `ПАНЕЛЬ УПРАВЛЕНИЯ` (`SET 4900`) with 24 objects, `Панель опций -
<what>`, that have a clip each and no reactions at all; the eight slider
marks (`... слайдер N min/max`) are hidden. The screen's start
(`sub_41C170`):

1. hides the case, draws a frame of the room as it is, and calls
   `sub_41E230` on the screen at `QSystem+552` (not decompiled -- by its
   place, taking the picture a saved game is shown by);
2. empties its list, puts the room on it and then every object attached to
   the room, each on its first frame, not animated, and **placed by two
   tables** in the exe (`word_4CA180` x, `word_4CA1B0` y), by its number in
   the room's list. These are the only objects of the game not at 0,0:
   their clips are small pictures, not full frames;

   | # | object | at | # | object | at |
   |---:|---|---|---:|---|---|
   | 0 | новая игра | 0,2 | 8 | слайдер 1 | 428,29 |
   | 1 | загрузить | 5,70 | 9 | слайдер 2 | 434,170 |
   | 2 | продолжить | 5,136 | 10 | музыка | 297,214 |
   | 3 | выход | 22,328 | 11 | субтитры | 470,139 |
   | 4 | сохранить | 87,224 | 12 | речь | 318,87 |
   | 5 | сейф | 118,395 | 13 | скорость | 468,172 |
   | 6 | эффекты | 467,71 | 14 | слайдер 3 | 262,31 |
   | 7 | кнопка | 432,144 | 15 | слайдер 4 | 231,137 |

   (the eight marks, 16-23, have 0,0 in the tables);
3. takes the cursor over as the map does -- clip and action saved, the
   interface pointer 4901 with action 0, moved without centring -- but
   animated;
4. saves the view's scroll and width and sets them to 0 and 640, then
   `sub_41C740` (not decompiled; by its place, setting the sliders);
5. shows the state of the subtitle switch (screen `+60`): object 7, the
   button, on frame 7 if it is on and 1 if not; object 11 on frame 2 or 1;
6. makes itself the current screen.

Its **click** (`sub_41CAE0`) finds the object under the point as the map's
does and acts on its number in the screen's list -- the room is entry 0, so
that is the number in the table above plus one:

| list # | object | what the click does |
|---:|---|---|
| 1 | новая игра | `sub_418260`, the handler of command 32789: reads `Part` and `Chapter` from `[All]` of `PARTS.INI` (both 1 there, and 1 if absent), sets the chapter global and loads that part with `sub_412040` (not decompiled), then redraws |
| 2 | загрузить | leaves the panel; the screen at `QSystem+552` is set up with 0 (its slot `+60`) and made current |
| 3 | продолжить | leaves the panel (its own slot `+56`) |
| 4 | выход | leaves the panel, then a call on the application object that the disassembler takes for a library routine -- by the button, closing the game |
| 5 | сохранить | leaves the panel; the screen at `QSystem+552` set up with 1 and made current |
| 8 | кнопка | flips the subtitle switch (screen `+60`), shows it on objects 7 and 11 as the start does, then `sub_41C740` |
| 17, 18 | слайдер 1 min, max | value at screen `+64` down or up by 5, then `sub_41C740` |
| 19, 20 | слайдер 2 min, max | the same for `+72` |
| 21, 22 | слайдер 3 min, max | the same for `+80` |
| 23, 24 | слайдер 4 min, max | the same for `+88` |

So `QSystem+552` is one screen for both loading (0) and saving (1), and the
hidden slider marks are hot spots: a click on a dial's "min" or "MAX" moves
it a notch. Which value is which dial is in `sub_41C740`.

**The dials.** The four values are frame numbers. `sub_41C740` puts each
dial's hand -- a clip of as many frames as the dial has positions -- on the
frame its value names, after `sub_41C5A0` has brought the values into range,
turned each into a percentage `(value - 1) * 100 / (top - 1)`, handed the
three volumes to the sound manager (`sub_40E160(channel, percent)`) and
written all of it to the `Options` section of the program's profile:

| value | key in `Options` | range | default | percent at | hand (object) | marks | sound channel |
|---|---|---|---:|---|---|---|---:|
| `+64` | `Speech volume` | 1-31 | 31 | `+68` | 14, `SERP` | 16, 17 (`SL1MIN/MAX`) | 2 |
| `+72` | `Music volume` | 1-41 | 20 | `+76` | 15, `MOLOTOC` | 18, 19 (`SL2`) | 0 |
| `+80` | `Sfx volume` | 1-31 | 23 | `+84` | 8, `AROWE-1` | 20, 21 (`SL3`) | 1 |
| `+88` | `Speed` | 1-26 | 13 | `+92` | 9, `AROWE` | 22, 23 (`SL4`) | -- |

The marks are full-frame clips whose `.MSK` rects are the "min" and "MAX"
labels round each dial. `Subtitles` is kept in the same section, and its
default is **0**: the game starts with the subtitles off. The speed percent
is the `n` of the walking hero's clock (see "Object update"): at the
default, 48, he walks at `(48 + 50) / 50`, about twice the walk clip's own
speed.

The sound manager (`sub_40E160(channel, percent)`) keeps the percent of
each channel -- 0 music, 1 effects, 2 speech -- and passes it to every
loaded sound of that channel (a sound's channel is its `+40`), whose
`sub_40EB70` sets the DirectSound volume:

```
percent 0:    -10000                        (silence)
percent 100:  0                             (nothing off)
otherwise:    -(10000 - 10000 * ln(2.75 * percent) / ln(275))
```

in hundredths of a decibel. The curve is steep: the default music, 47 %,
is 13.4 dB down (a gain of 0.21), the default effects, 73 %, 5.6 dB down
(0.53), and 10 % is all but silent.

**Loading a part** (`sub_412040(part)`, a method of the application
object) is what "new game", the script's `PART` and a saved game of another
part come to. It keeps nothing of the running game:

1. `sub_412320(part)` first, the check that the part's disc is in the
   drive: it tries to open `disk<part>.id` (a marker file; `disk1.id`,
   `disk2.id` and `disk3.id` lie next to the stores). Found, it resets the
   sound manager and says yes. Not found, it shows the system cursor, sends
   MCI `set cdaudio door open`, and asks `Insert Disk <part> and press OK.`
   in a message box, over and over until the file is there or the player
   cancels -- and on a cancel either quits the game (if `+252` of the
   application is set) or lets the sound go on, and says no, in which case
   nothing more happens;
2. the part number is stored (`+196` of the application, the global
   `dword_4DE87C`);
3. `QSystem`, the object at `dword_4DE8E8`, the resource manager
   (`dword_4DE8C8`), the dialogue manager (`dword_4D416C`) and the object at
   `dword_4D4178` are destroyed;
4. the stores are opened again: `patch.str` **first**, then `main.str`
   (`sub_41EDC0`); `PARTS.INI` is read (`sub_4121E0`) and the part's own
   stores opened (`sub_411450`); the sound manager is reset (`sub_40F060`);
5. a new dialogue manager, resource manager, `dword_4DE8E8` object and
   `QSystem` are made, and `sub_405D00` loads the script -- which sends
   `TOTALINIT` round and builds the screens.

`patch.str` is a store looked in before any other; the copy of the game
examined has none. The launcher does the same in `game_switch`
(`part_close`, `part_open`): everything of the old part is let go and the
new one opened, and since the game writes into its script and dialogue code
as it goes, that is also how a part is started over. `CHAPTER`
(`chapter_open`) only changes the chapter's store and reads the names and
colours again.

**Fonts.** The game has two, both made at start-up by `sub_410CD0` with
`CFont::CreateFont`: **Arial, weight 700 (bold)**, default character set,
TrueType precision, **20** pixels high (`0x4E1ED8`) and **16** pixels high
(`0x4E1EE0`). The 20-pixel one writes the labels of the saved games, the
name of the object under the cursor and the phrases of a dialogue; the
16-pixel one a thing's description.

**Text over the picture.** A screen's slot `+40` (`0x412C20`, the caption)
and slot `+44` (`0x412F50`, the subtitle) each take down the text object
that was up and, unless the new text is empty, make one: `sub_41F8E0` for a
caption, `sub_420CA0` for a phrase (the object whose update times a phrase,
`0x421170`). Both are built the same way, read from the disassembly:

1. the text is measured in the 20-pixel font with word breaks in a rect 625
   wide (`5,2 - 630,...`, `DT_CALCRECT | DT_WORDBREAK`);
2. the object gets a surface of that size plus 10 across and 4 down,
   `z` **3000** -- over the cursor and everything else -- and the place
   `left = (640 - width) / 2`, `top = 479 - height`, `right = 639 - left`,
   `bottom = 479`: **the middle of the bottom edge of the screen**;
3. the surface is filled with the second colour the caller gave and the text
   drawn on it in the first, with `DT_WORDBREAK` for a caption and
   `DT_WORDBREAK | DT_CENTER` for a phrase.

The **menu of a dialogue** is a third object of the kind. The main
screen's slot `+60` (`sub_4144C0(lines, colour, fill, lit)`) clears the
object under the cursor, takes the old text down and makes it
(`sub_420020`): the lines one under another in the same 20-pixel font, each
measured and broken in the rect `5,2 - 630,...`, on a surface as wide as the
screen that rests on row 479 like the others, with `z` 3000. The dialogue
step passes white for the lines (`0xFFFFFF`), `0x0A0A0A` for the fill and
`0xFF0000` -- **blue**, as a `COLORREF` -- for the line under the pointer;
the object keeps the line's number at `+72`, which is what a click sends to
the step. There are no numbers before the lines and nothing behind them.

So the "second colour" passed with every caption and subtitle (see the
dialogue step) is the surface's fill, which by the calls after it is keyed
out -- a background chosen to differ from the text, not an outline: the
words stand straight on the picture with nothing behind them. The launcher
draws all three this way (`draw_bottom_text`, `menu_lines`); a menu line can
still be picked by its number on the keyboard, which the original has not; it keeps a bar of its own along
the top, with the cursor's action and the room's name, which the original
has not. The same start-up reads the registry value `CD path` under
`HKLM\SOFTWARE\SKIF\Petka` -- the path a film is looked for under when it
is not in the current directory -- opens `patch.str` and `main.str`, and
refuses to run twice (`PreventSecondInstance`). The launcher writes its text
in bold Arial too where the system has it, at those sizes for the two uses
that are known.

**A thing's description.** `sub_4147A0(text, n)` marks the main screen as
showing one (`+104` = 1), makes the panel object **inactive** -- so no
action keys, case or map while it is up -- clears the object under the
cursor, and puts a new object on the screen's list (`sub_421370`, vtable
`0x4BA010`). That object has `z` 999, just under the cursor, and a surface
of its own, 640x480: the clip 6008 (`MAIN\INVENTOR.1\T-01.FLC`, 40 frames)
on frame `n` -- a wooden board along the bottom of the screen with the
thing's picture -- and over it the text, drawn with GDI in **black**, with
word breaks, in the rect **160,275 - 598,376**, in the 16-pixel font
(below). Its hit test answers yes for every point and its click calls
the screen's slot `+68`, `sub_414AC0`: the flag `+104` is cleared, so is the
object under the cursor, the panel object is made **active** -- always, not
"as it was before" -- and the screen's slot `+36` is called (not decompiled;
by its place what takes the object off the list). Any click anywhere shuts
a description. The launcher shows it the same way
(`draw_description`), in its own font.

**Films.** The script plays one: `AVI 50000` in the `INITBG` reaction of
the room `Seq - INTRO0`, the first thing a new game does. Resource 50000 is
`INTRO.AVI`, and it is **not in the stores**: `sub_4124A0(path)` opens it as
a plain file, first as named and then with a path of the application's
(`+212`) put before it -- the disc, presumably. Then:

1. the game's sounds are paused (`sub_40DAC0`);
2. the film is given to MCI: `open <path> alias film`, `window film handle
   <the game's window>`, `put film destination at 0 0 640 480`, `play film
   notify`;
3. the function **stays in a loop**, taking messages off the window's queue
   itself, until a key goes down (`WM_KEYDOWN`), either mouse button goes
   down (`WM_LBUTTONDOWN`, `WM_RBUTTONDOWN`) or MCI says the film is over
   (`MM_MCINOTIFY`);
4. `close film`, the queue is emptied of those four kinds of message, and
   the sounds resume (`sub_40DB00`).

So a film is a pause in everything, and any key or click ends it. Three
more are played when the program starts, before anything else is shown
(`sub_410CD0`, the application's start-up): `buka.avi`, `skif.avi` and
`adv.avi`, in that order.
`INTRO.AVI` is 640x480 Indeo 3 video at about 24 frames a second with
MS-ADPCM sound, three and a half minutes long. A fifth film, `credits.avi`,
is played from the start-up screen (below).

**The start-up screen** (`QSystem+556`, constructor `sub_41E620`) is the
game's main menu, and like the map and the panel it is a room of a script:
`STARTUP`, room 36 of **`PART_0`** -- a part of its own that holds only the
menus (it also has a `ПАНЕЛЬ УПРАВЛЕНИЯ` and a `SAVELOAD`). The room has a
picture (`SET 4980`, `MAIN\INTRFACE.BG\INSTHERO.BMP`), music (`MUSIC 4986`,
`start.wav`) and five objects with a clip each and no reactions:

| object | clip | a click (`0x41EAB0`, by the clip's resource less 4981) |
|---|---|---|
| `STARTUP - Exit` | 4981 `S-EXIT.FLC` | the application's close (`0x4181E0`, the call the panel's door makes) |
| `STARTUP - Credits` | 4982 `S-TITLE.FLC` | plays `credits.avi` (`sub_4124A0`), then finds the room `STARTUP` again and starts its music afresh |
| `STARTUP - Load` | 4983 `SLOADGEM.FLC` | sets the save/load screen up for loading and makes it current |
| `STARTUP - New` | 4984 `SNEWGAME.FLC` | new game (`sub_418260`) |
| `STARTUP - Misc` | 4985 `SUNINSTL.FLC` | nothing |

The click goes by the object under the cursor (screen `+24`). The rest of
the screen (vtable `0x4B9EB0`; read from the disassembly) is the map over
again: its start (`0x41E670`) empties the message queue, puts the room and
its objects on its list, loads and plays the room's music, and takes the
cursor over as the interface pointer 4901; its mouse move (`0x41E990`)
skips the cursor and the background (4980), makes the first object that
takes the point the one under the cursor and **visible**, and hides the
rest -- so a button's clip is its lit picture, shown only under the
pointer. The buttons' places are their clips' `.MSK` rects: "new" 316,76 -
589,106; "load" 327,119 - 577,152; "credits" 406,303 - 492,334; "exit"
404,342 - 498,376 (`SUNINSTL.FLC` has no usable rect, so it can never be
pointed at).

The launcher's menu is this screen (`run_menu`), drawn from the same
pictures: new game, load (the save screen comes up before any room is
entered; left without loading, it gives way to this screen again, as the
exe's screens go back to the previous one), credits, and
exit. The launcher does what the exe does -- gives the file to someone
else: `ffmpeg`, if it is installed, decodes it, and the launcher shows the
frames in its window at 24 a second with the sound alongside, until it ends
or a key or button is pressed (`play_film`). Without `ffmpeg`, and in
unattended runs, the film is passed over with a note.

**The save/load screen** (`QSystem+552`; constructor `sub_41D940`, which
only sets the vtable, `0x4B9E70`) is a third screen of the same kind. Its
room is 513 `SAVELOAD`, which has a picture (`SET 4990`, `LOAD1.BMP`; the
set goes on with `LOAD2`, `LOAD3`, `SAVE1`-`SAVE3`) and **no objects**: the
slots must be the screen's own doing. Its **set-up** (`sub_41D9E0`, slot `+60`; the panel calls it with 0 for
loading and 1 for saving):

1. saves the view's scroll and width and sets them to 0 and 640; empties
   its list; remembers "loading" (`+88`);
2. puts the room on the list with its picture changed to
   `4990 + page` for loading and `4995 + page` for saving -- `LOAD1`-`3`,
   `SAVE1`-`3`; the page is the global `dword_4DE980`. So there are **three
   pages of six slots**, 18 saved games;
3. for each of the six slots opens `save<n>.dat`, `n = slot + 6 * page + 1`,
   in the current directory, and notes whether it is there (`+64 + 4 *
   slot`). If it is, it reads a 38-byte header and then the pixels of a
   108x82 picture straight into a 16-bit surface, draws that picture **onto
   the room's background** at the slot's place, and under it the text at
   byte 8 of the header -- the game's name -- in the colour `0x007F00`, in
   a 240x30 box starting one pixel below the slot;
4. takes the cursor over as the map does (pointer 4901, action 0, not
   animated) and makes itself the current screen.

The slots, and the two page buttons, are rectangles built at start-up
(`0x4DE8F0` and on; read from their initialisers):

| | left, top - right, bottom |
|---|---|
| slot 1 | 43,84 - 151,166 |
| slot 2 | 43,209 - 151,291 |
| slot 3 | 43,335 - 151,417 |
| slot 4 | 358,75 - 466,157 |
| slot 5 | 360,200 - 468,282 |
| slot 6 | 359,325 - 467,407 |
| `0x4DE970`, previous page | 10,414 - 38,442 |
| `0x4DE960`, next page | 596,403 - 624,431 |

Its **click** (`sub_41E370`) goes by these rectangles alone, not by
objects:

- in a slot: the name `save<n>.dat` is built as above, and
  - when loading, and only if the slot has a file: the screen is left
    (slot `+56`) and **`sub_407410(name)`** called -- the loading of a game;
  - when saving: the screen is left and **`sub_406EB0(name)`** called -- the
    saving. An occupied slot is overwritten without a question;
- in the previous-page corner, on page 1 or 2: the page goes down by one,
  the screen is left and set up again in the same mode;
- in the next-page corner, on page 0 or 1: the same, a page up.

**A saved game** is written by `sub_406EB0(name)`, a method of `QSystem`,
in one pass and with no compression or check:

```
u32        the part       (dword_4DE87C)
u32        the chapter    (dword_4DE884)
char[30]   the name shown under the slot: the time of saving, formatted
           "%X   %d %B %Y" (the locale's time, day, month name, year)
pixels     the slot picture: 82 rows of 108 16-bit pixels, in the surface's
           own row length -- 17,712 bytes if a row is not padded
u32        n, the length of QSystem's object list: every object and room of
           SCRIPT.DAT, in its order, and then the cursor, the case and the
           panel object
(n - 3) x  one record per script object (the last three are not saved):
             u32 len, bytes   its script name            (+80)
             u32 len, bytes   its shown name             (+84)
             u32              busy                       (+56)
             u8               status                     (+76)
             u32              resource                   (+4)
             u32              z                          (+8)
             u32              x                          (+12)
             u32              y                          (+16)
             u32              visible                    (+44)
             u32              active                     (+60)
             u32              animate                    (+48)
u32        number of things in the case, then that many u16 ids
u32 len, bytes   the current room's script name
i32 x, y   Petka:    x + frame_width * scale / 2,  y + frame_height * scale
i32 x, y   Chapayev: the same -- for both, the point between the feet
u32        number of words of dialogue code, then all of them (sub_40C1B0)
u32        the cursor's resource (+4)
u32        the cursor's action (+128)
i32        id of the thing on the cursor (+124), or -1
u32        Petka's +148
u32        Chapayev's +148
8 x i32    -1, room for more
```

What is **not** in it: the message queue and the tails of reactions waiting
on a clip, a walk or a dialogue; an object's automatic-z flag, its clip's
frame, its sound flags; a room's map flag, music and ambience; the view's
scroll. The whole of the dialogue code is, because that is where the
dialogues keep their state: the menu lines switched off and the `SKIPN`
turns live in the instruction words. A hero is saved twice -- as object 0
or 1 with his sprite's corner, and again by his feet.

The launcher's load therefore drops both (`petka_scriptvm_drop_pending`)
and switches the cursor and the panel on, which a game being saved always
has. Without that a save of another part came up wrong: that part is
started first, its opening scene is queued by the first room's `INITBG`
(in part 2: cursor off, heroes off, the undressing clips), and the queue
then ran over the loaded game.

**Loading** (`sub_407410(name)`) reads the same file back, and what it
does with each part of it matters as much as the format:

1. the part is read into a scratch global (`dword_4DE880`) and compared with
   the part that is running. **If they differ nothing is loaded**: the name
   is remembered, a flag is set (`dword_4DE8B0`), and the file is closed --
   the game has to change part first and comes back to it. Otherwise the
   chapter is taken from the file and `sub_411C00` opens that chapter;
2. the label and the picture are read and dropped;
3. each object record is applied to the object **found by its script
   name** (`sub_406D40`), so a save does not depend on the order of the
   list: busy, status, resource, x, y, visible, active, animate. The shown
   name is read and dropped, and so is **z** -- an object keeps the z it has;
4. the case is emptied and filled from the list;
5. the room is found by name and entered through `loadRoom` with its last
   argument 1 -- the form that sends no `LEAVEBG` and no `INITBG`;
6. Petka is put by his feet (his slot `+28`, `sub_4087D0`), and the view's
   scroll is set to his x less 320, kept between 0 and the room's width
   less 640; then Chapayev is put;
7. the dialogue code is read over the one in memory (`sub_40C1F0`);
8. the cursor's resource, action and thing are set;
9. each hero's saved `+148`, if it is not -1 and is a multiple of 100, is
   sent to him as message 49, `IMAGE`, with 1 as the second argument, at
   once -- so `+148` is the hero's costume, and this is how he gets his
   clips back;
10. the eight spare words are read, the panel object is made active, and
    the screen redrawn.

Nothing is done to the message queue, and `TOTALINIT` is not sent.

**The slot picture** is taken by `sub_41E230`, which the panel's start
calls before it draws anything of its own: a fresh 108x82 surface is made
(kept at `+96` of this screen, the one the saving writes out) and the
frame just drawn -- the back buffer, all 640x480 of it -- is stretched by
DirectDraw into its rect 0,0 - 107,81. So the picture is the room as it
was, cursor and all, 107x81 with one column and one row to spare.

Its **mouse move** (`sub_41E2B0`) marks nothing on the page: it sets the
cursor's animate flag to "the pointer is in a slot or in a page corner" and
moves the cursor without centring. (Its leave, `0x41E590`, and the panel's,
`0x41CE00`, were read from the disassembly: the same as the map's.)

The launcher saves and loads in this format (`core/src/save.c`:
`petka_save_encode`, `petka_save_decode`, `petka_save_peek`; `game_save`,
`game_load` and the screen in the launcher), to `save<n>.dat` in the
directory it is run from, like the exe. The panel's "restore" and "save"
lead to the screen, a click on a slot loads or saves, the corners turn the
page; `Esc` leaves it (and so, in the launcher, do the right button and `O`). The slot picture is taken as the
exe takes it, from the launcher's own window (so with its bar of text). A save from
another part makes the launcher load that part and then the save.

The panel's **mouse move** (`sub_41C8D0`) goes down the list from its end
and stops at the first object that takes the point. That object is "lit"
(state 2) only if it is one of the five buttons (0-4) or a dial mark
(16-23); every other object, and every object not under the pointer, is
state 1. An object whose state changes is put on the frame of that number
-- 1 or 2 -- and redrawn at its place from the table; for a dial mark the
frame is put not on the mark but on the dial's word: marks 16, 17 light
object 12 (`речь`), 18, 19 object 10 (`музыка`), 20, 21 object 6
(`эффекты`), 22, 23 object 13 (`скорость`). The cursor is shown and moved
without centring. So a button's second frame is its lit picture, and
pointing at a dial's "min" or "MAX" lights the dial's name. The launcher
does the same. The launcher puts the panel up the same
way (`panel_command`) and takes "continue", the door and the subtitle
switch and the four dials (`panel_click`); with the subtitles off a phrase
is not written out, and one without a voice is not waited for. The dials
start at the exe's defaults and work: the three volumes, on the exe's
curve (the ambience counts as an effect), and the walking speed. They are
kept between runs in the same `Options` section, with the exe's key names,
in a file `Petka.ini` in the directory the launcher is run from
(`options_load`, `options_save`; the exe keeps the section in its Windows
profile). Without that file the launcher starts with the exe's defaults,
except that the subtitles are on. "Restore" and "save" lead to the save/load screen (below).
"New game" loads the part and chapter `PARTS.INI` names, as the original
does.

**The map** is two things. In the script it is a room, 512 `КАРТА`: a
picture (`SET 4800`, `MAIN\MAP1.BG\MAP-00.BMP`; the room's `INITBG`
reactions swap in `MAP-01` and `MAP-02` by its status) and one object per
place, `Карта 1 - <place>` (19 of them in `PART_1`), each with a clip
(4801.., `M-4WAY.FLC` ...), hidden by `TOTALINIT`, and one reaction:

```
on LOOK:   <the place's room> GOTO
```

In the exe it is a screen of its own (`QSystem+548`; constructor
`sub_41CE90`, which only sets a flag at `+36`; vtable `0x4B9E30`). Its
start (`sub_41CF30`, slot `+24`), which does nothing in a room that is not
on the map:

1. clears the caption and empties its own object list;
2. finds the room by the name `КАРТА` and puts it on the list (its picture
   is kept at screen `+64`), then every object `BACKGRND.BG` attaches to
   it, each with `z` 1, position 0,0 and **animate set to its visible
   flag** -- nothing is shown or hidden here;
3. takes the cursor over: its clip and action are saved (screen `+40`,
   `+42`), and it gets the interface pointer (resource 4901,
   `MAIN\INTRFACE.BG\KURSOR1.FLC`), shown, not animated, with **action 0,
   "look"** -- which is the one message the places react to;
4. saves the view's scroll and width (`QSystem+104`, `+112`) and sets them
   to 0 and 640;
5. broadcasts `INITBG` with the map room as the sender (so the room's
   reactions can pick its picture), and makes itself the current screen.

The places stay hidden but active: invisible hot spots like a room's exits.
Three of `PART_1`'s start out `PASSIVE`.

Its **mouse move** (`sub_41D4D0`) is what shows them. It goes down the
list from the end to the entry after the room, skipping the cursor and
anything that has the room's picture; the first object whose own hit test
(vtable slot `+32`) takes the point becomes "the place under the pointer"
(screen `+24`) and gets state 2, every other one state 1 (the state is kept
at object `+32` and `+36`). An object whose state changes is put on its
first frame, **made visible if the state is 2 and hidden otherwise**, and
redrawn. So a place's clip is its highlight. The cursor is animated while
there is a place under it and moved with 0 as its last argument -- on the
map the pointer's corner, not its middle, is the point. The caption follows
the same rule as in a room: the name from `NAMES.INI` in `0xFFFFC0`, else
the script name in `0x000080`, cleared when the pointer leaves.

Its **leave** (`sub_41D730`, slot `+56`) undoes the start: the place under
the pointer is hidden, the caption cleared, the view's scroll and width and
the cursor's clip and action put back, the previous screen (`QSystem+532`)
made current again and told where the pointer is (its mouse move), and the
base screen's leave (`sub_412A80`) called. Nothing in it places the heroes:
after a `GOTO` from the map they arrive as from anywhere else, both on the
room's entry point.

Its **click** (`sub_41D690`) goes down the list the same way -- from the
end to the entry after the room -- and gives the click (vtable slot `+40`,
`sub_401B30` for a place) to the first object whose hit test takes the
point. With the cursor's action being "look" that posts `LOOK` to the
place, whose reaction is the `GOTO`. A click on nothing does nothing: there
is no walking on the map.

The launcher shows the map the way it shows a sequence -- the room `КАРТА`
in place of the current one, no heroes, no scroll -- with the pointer and
the action as above; a `GOTO` from a place, or `M` again, leaves it and
gives the cursor its action back. It draws a place's clip while the pointer
is on it, and the pointer as the mouse function has it.

Action 5 is "send Chapayev": with no reaction to it, the built-in
`OBJECTUSE` handling walks him to the object. Action 6 is what the `CURSOR`
message switches the cursor to. The arguments matter because a reaction's
`IF` compares them.

Whether a point hits a scene object is `sub_4018C0`. The object must be
active (`+60`; visibility is not asked -- see "Hidden is not inactive"), and
the point, less the object's position, must lie inside the clip's covering
rect from its `.MSK` (`QFlic+1640`). The clip's surface (`QFlic+1632`) holds
just that rect (its corner is `QFlic+1656`, `+1660`), and on it the point
**and its eight neighbours** are tried, in the order of two tables in
`.rdata` (`0x4B9148` dx, `0x4B9170` dy):

```
(0,0) (-1,-1) (0,-1) (1,-1) (-1,0) (1,0) (-1,1) (0,1) (1,1)
```

The first one that falls on the surface and is not the transparent colour
(`QFlic+40`) makes it a hit: one pixel of slack all round.

**The transparent colour is a colour, not a palette entry.** `QFlic+40` is
not a field of its own. The FLC decoder's state sits inside the `QFlic` from
`+8`, and holds two palettes: 256 four-byte entries at `+0x220` and 256
16-bit ones at `+0x20` -- so `QFlic+40` is **the 16-bit colour of palette
entry 0**. The frame's chunks are dispatched by `sub_4104B0` (types 4, 7,
11, 15 and 16), and each of the two palette chunks has a handler that
stores its triples and then turns **all 256** entries into words again:

- type 4, the 8-bit palette (`sub_410060`): the triples as they are, and
  the words by the screen's own pixel format -- each channel shifted down to
  that format's width and up to its place, from the widths at `0x4CA45C`,
  `0x4CA460`, `0x4CA464` and the shifts at `0x4E1EE8`, `0x4CA468`,
  `0x4CA46C`. On the usual 5-6-5 screen that is
  `(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)`;
- type 11, the 6-bit palette (`sub_410280`): every component multiplied by
  four, in a byte (so white is 252), and the words by a formula of its own,
  `(r >> 3) << 11 | (g >> 3) << 6 | (b >> 3)` -- laid out 5-6-5 whatever the
  screen is, with green cut to five bits and its low bit clear.

Both do the words only when the screen is 16 bits a pixel (decoder
`+0x634`); in a palettised mode the 6-bit one makes a DirectDraw palette of
the table instead. The clip's loader hands the word at `+40` to the
surface's vtable `+0x74` (`0x40F84B`; `SetColorKey` by its place), and the
hit test above compares against the same word. So every pixel whose entry
falls on entry 0's word is transparent, and clips count on it. All 2339
clips of `MAIN.STR` and `FLC1-3.STR` have the 8-bit palette (the 6-bit
handler is never reached with this data), and 22 of them have such pixels
in their first frame under another entry. `PROVODA.FLC` (the wires at the
dump, shown once the magnet has pulled the rubbish up) has an 8-bit palette
with a key of 255,255,255 and is filled with entry 31, 252,252,252 --
301375 pixels; `SHWARC.BG\STOL.FLC` has no pixel of entry 0 at all, its
magenta fill being another entry; the four `BUDUAR.BG\ANKA\SHIRMA0x.FLC`
of part 2 are the same; `INVENTOR.1\PRICELL.FLC` is nothing but such
pixels. The launcher does the same (`petka_flic_keyed`, by the palette as
the exe holds it and the kind of the last palette chunk, for a 5-6-5
screen), for the picture and for the hit tests.

A hero overrides this with `sub_4086B0`, which has no slack. It answers
yes when

- the hero is active (`+60`) -- visibility is **not** asked;
- the point, less the sprite's corner (`+12`, `+16`), lies inside the scaled
  frame (`+128` x `+132`);
- the pixel there on the hero's own surface (`+144`, the frame already
  scaled, 16 bits a pixel) is not the clip's transparent colour (`QFlic+40`).

So a hero is hit pixel-exactly, on the scaled picture. The launcher asks
the same three questions (`object_at`).

Which object wins is decided by the screen's two mouse functions, read from
the disassembly (the hit test is vtable slot `+32`, the click slot `+40`,
"the cursor is over you" slot `+36`). Both walk the screen's object array
(`+8`, count `+12`) -- the one the frame function keeps sorted by `z` --
**from its end down and stop at the first hit**: the topmost object takes
the mouse, whatever is behind it gets nothing. A hero standing in a doorway
does cover the exit behind him.

- `0x414020`, mouse move: it clears the screen's "object under the cursor"
  (`+24` of `QSystem+536`), then, only while the cursor is shown (cursor
  `+44`), gives the first hit slot `+36` -- `sub_401A60`, which does nothing
  but write the object back into that field. The cursor is told whether it
  is over something (cursor `+48`) and moved (its slot `+64`). If the object
  under the cursor changed and the screen's `+68` is 3 -- that is `+24` of
  the dialogue object built into the screen at `+44`, its state, and 3 is
  "no phrase and no menu up", so objects are not named during a dialogue --
  the caption is set through the screen's slot `+40`:
  - the object's shown name (`+84`, from `NAMES.INI`) in `0xFFFFC0`
    (COLORREF: R 192, G 255, B 255) if it differs from the string at
    `0x4D417C`;
  - otherwise the string at object `+80` in `0x000080` (R 128, a dark red);
  - with nothing under the cursor, the string at `0x4D417C` itself.

  All three with `0x0A0A0A` as a second colour (an outline or shadow). The
  string at `0x4D417C` is the empty string: zeros in the exe's data, and the
  default the loader hands `GetPrivateProfileString` for `NAMES.INI`,
  `CAST.INI` and `BGS.INI`. Object `+80` is the name from `SCRIPT.DAT`
  (`sub_405D00` copies it there, then looks `+84` up by it). So an object
  absent from `NAMES.INI` is labelled with its script name in dark red --
  this is a build with its assertions left in, and it shows. The launcher
  does the same.
- `0x414320`, click: with the cursor hidden the click goes to the dialogue
  instead (`sub_415F80(-1)` on `QSystem+536`: skip the phrase). Otherwise
  the first hit gets slot `+40` (`sub_401B30`, above). With **nothing** hit,
  only two actions do anything:
  - action 1 (`WALK`): if Petka's parked walk tail (`+172`) holds a `GOTO`,
    Chapayev is stopped (slot `+80`, `sub_408D80`) -- he was following Petka
    out of the room; the tail is deleted; Petka walks to the point (slot
    `+72`, `sub_408BB0`);
  - action 5 ("send Chapayev"): Chapayev walks to the point.

  Petka is `QSystem+160`, Chapayev `QSystem+344`.

Where the other engine-sent messages come from: `END` and `HALF` from the
object update (below), `TOTALINIT` from `sub_405D00` at load; and, read off
the constants at the call sites without decompiling the functions,
`USERMSG` from `sub_409610`. `SAY`/`SAID` come from the dialogue step,
`sub_415F80` (see "The dialogue step" below).

### Object update: `END`, `HALF` and clip sounds

Every frame each object's update method runs (vtable slot 6; `sub_4015E0`
for ordinary scene objects, `0x407BF0` for the heroes, a stub for rooms). It
does nothing unless the object is animated (`+48`) and visible (`+44`), and
nothing for a clip of a single frame. Otherwise it adds the elapsed time and,
once a frame's delay has passed (at most one frame per call):

1. if the object has a sound armed (`+68`) and frame 1 is up, starts it;
2. steps to the next frame;
3. if that is the last frame, posts `END` (arg1 = the clip's resource id, no
   sender) and, if the object repeats its sound (`+72`), arms it again;
4. if that is frame `count / 2`, posts `HALF` (same argument).

The clip then goes round again: nothing stops on the last frame, and `END`
comes on every loop. `PLAY` relies on exactly this -- the script's next
message normally replaces the clip. A clip's sound is the file of the same
path with a `.WAV` extension; `SET` arms it, and `SET` with 5 as its second
argument clears `+72`, i.e. "play the sound once".

The heroes' update (`sub_407BF0`) is the same loop with these differences:

- a clip flagged `+124` -- the hero's message handler sets it for his idle
  pose and for `PLAY` clips, not for the talking clip -- has its clock wound
  back by 10 seconds when it returns to frame 1, so it plays once and then
  rests on its first frame;
- while the hero walks, elapsed time is multiplied by `(n + 50) / 50`, `n`
  being the speed dial of the panel screen as a percentage (`+92` of
  `QSystem+544`), so from 1x to 3x;
- after each frame it calls the hero's walk step (vtable slot `+76`,
  `sub_408EF0`, below): the hero moves once per frame of the walk clip
  rather than continuously;
- the clip's drawn size is its frame size times the double at `+152`, the
  hero's current scale;
- the sound is armed again on every loop regardless of `+72`, and it may
  step several frames in one call.

The walk step asks the hero's walk object (`+160`, `sub_424030`, not
decompiled -- the route and presumably the `.OFF`/`.LEG` files are in there)
and gets one of three answers:

- direction changed: the clip becomes `costume + 10 + direction`, from its
  first frame;
- a step was made: the hero is moved;
- arrived: the hero is put exactly on the destination and, normally, sent
  `SET costume 1` at once (the idle pose). But if the reaction parked on this
  walk contains a `GOTO` or a `SETSEQ` -- he is walking out of the room -- he
  is sent `ANIMATE 0` instead and stays frozen in mid-stride, and
  `SET costume 1` and `ANIMATE 1` are appended to the parked reaction, so he
  stands up only after the room has changed. Either way the busy flag `+56`
  is then cleared, which is what releases the `QM_WALKED` queued behind the
  walk.

**Stopping a hero** (`sub_408D80`, vtable slot `+80`) is not arriving. It
clears the walking and busy flags, readdresses any `QM_WALKED` queued for
the hero to nobody, and **throws away the reaction parked on the walk** (the
debug build traces "after_walk is lost!"); then, unless the hero is in his
resting state (`+124`), puts him in the idle pose with `SET costume 1`. It
is called for both heroes when an object is clicked (`sub_401B30`) and
before a room is loaded (`GOTO`), and for the hero concerned when a reaction
is about to send him walking (`sub_408080`). So clicking an exit and then
something else calls the exit off: the parked `GOTO` is discarded, not run.
(Treating a stop as an arrival does the opposite -- the hero halts and the
room changes on the spot.) Implemented as `petka_scriptvm_stop_hero`.

The launcher reproduces the resting clip, the re-armed sound, the arrival
(`petka_scriptvm_walk_ended`) and the step per clip frame (see "Gait"
below); the walk-speed slider is not.

### The cursor and the panel are separate switches

Whether the player can click is whether the cursor is shown (the cursor
object, `QSystem+140`); only message 24 changes that. The panel object
(`QSystem+148`) has an active flag of its own, switched by 24 as well, by
`SYSTEM` 0/1, and switched back on when a dialogue ends (`sub_4165A0`).

They must not be merged. A cutscene is typically written

```
on TAKE:  STOP 0, SpDialog34 TALK          -- cursor off, then talk
SpDialog34 on TALK:  DIALOG, WALKTO, PLAY 1737, ..., ADDINV, STOP 1
```

so the dialogue ends in the middle, and the cursor comes back only with the
final `STOP 1`. With one flag for both, the end of the dialogue hands the
clicks back early and the player can start the same cutscene again on top of
itself. Of the verb reactions that play a clip or walk a hero, the ones not
opening with `STOP 0` are nearly all room exits (`WALKTO`, `GOTO`), where
clicking again while walking is meant to work.

### Hidden is not inactive

`HIDE` only stops an object being drawn; `OFF` also makes it inactive. An
object that is hidden but active still answers the pointer: its clip is the
shape of an invisible hot spot. Room exits are built this way --

```
OBJ "Выход из Комнаты Петьки во Времянку"
    on TOTALINIT: SET 1718, HIDE
    on WALK:      WALKTO, ВРЕМЯНКА GOTO
```

-- as are windows, cupboards and the like (10 of the 18 objects in Petka's
room). The exe loads an object's clip if it is visible *or* active
(`sub_413870`); a front end that only loads visible ones has no exits.

### Opcodes

Names 0-46 are the exe's own (`sub_402C70` turns an opcode into its name).
47-62 have no name in the exe; those marked * are ours.

| op | name | effect |
|---:|---|---|
| 1, 4, 6, 10 | `USE`, `LOOK`, `TAKE`, `TALK` | player verbs; only trigger reactions |
| 3 | `GOTO` | (room) make this room current, see below |
| 5 / 28 | `SAY` / `SAID` | (hero) show costume+1 (talking) / costume (idle) |
| 9 | `WALK` | click with no reaction: Petka walks to the object |
| 11 | `END` | a clip reached its last frame; resumes the tail parked by `PLAY` |
| 14 | `SET` | change the object's resource id; `arg2`: 1 = rewind, 5 = play the clip's sound once instead of on every loop |
| 15 / 16 | `SHOW` / `HIDE` | visibility |
| 17 | `DIALOG` | start the current dialogue |
| 18 | `ZBUFFER` | `arg1`: follow the mask automatically (1) or keep a fixed `z` (0); `arg2`: that `z`, if given |
| 19 | `TOTALINIT` | every object's setup reaction |
| 20 | `ANIMATE` | `+48` = `arg1` |
| 21 | `STATUS` | object status = `arg1` |
| 22 / 23 / 46 | `ADDINV` / `DELINV` / `SETINV` | inventory add / remove / replace the sender with this object |
| 24 | (exe: `SYSTEM`) | show/hide the cursor, and switch the panel with it |
| 25 | `CURSOR` | cursor carries this item; `-1` = default cursor (5002) |
| 26 | `OBJECTUSE` | an inventory item was used on this object |
| 27 / 44 | `ACTIVE` / `PASSIVE` | `+60` |
| 29 / 30 | `SETSEQ` / `ENDSEQ` | (room) put this room on screen as a cutscene sequence / go back to the game, see below |
| 31, 32 | `CHECK`, `IF` | conditions, see above |
| 34 | `HALF` | a clip reached its middle frame (a reaction trigger; used once) |
| 33 | `SHOWDESCRIPTION` | the object's line in `[all]` of `INVNTR.TXT`, looked up by its script name, is handed to the main screen (slot `+64`, `sub_4147A0`) with `arg1`, a number 1-40 in `PART_1`, one per thing; see "A thing's description" |
| 35 | `QM_WALKED` | (hero) resume the tail parked by a walk |
| 36 / 37 | `WALKTO` / `WALKVICH` | Petka / Chapayev walks to (`arg1`,`arg2`) or the object's walk point |
| 38 | `INITBG` | broadcast when a room is entered, sender = that room |
| 40 | `SYSTEM` | 0/1 = panel off/on (not the cursor), 242 = quit |
| 43 / 45 | `MAP` / `NOMAP` | (room) flag `+144`, "on the map": set to begin with, it lets the map be opened from the room |
| 47 / 48 | `BGSFX`* / `MUSIC`* | (room) ambience / music resource id |
| 49 | `IMAGE`* | (hero) costume base id: +1 talking, +10..17 walking in 8 directions |
| 50 | `STAND`* | (hero) same as `SAID` |
| 51 / 52 | `ON`* / `OFF`* | active and visible / neither |
| 53 | `PLAY`* | `SET`, then wait for the animation to end |
| 54 | `LEAVEBG`* | broadcast when a room is left, sender = that room |
| 55 | `SHAKE`* | screen shake on (1) / off |
| 57 | `RANDOM`* | see above |
| 58 / 59 | `JUMP`* / `JUMPVICH`* | place Petka / Chapayev instantly |
| 60 / 61 | `PART`* / `CHAPTER`* | `PART n` does not load anything itself: it sets the wanted part (`dword_4DE880`), chapter 1, an empty "save to load" name and the flag `dword_4DE8B0` -- the same request a saved game of another part leaves -- plus a second flag, `dword_4DE8B4`; the part is loaded later (`PART 2` ends part 1, `PART 3` part 2). `CHAPTER n` calls `sub_411C00(n)` at once (part 2 only: `CHAPTER 2`, `CHAPTER 1`). See "Loading a part" |
| 62 | `AVI`* | the resource's path (`sub_419B20`) is handed to `sub_4124A0`, the film player; see "Films" |

A **sequence** is a cutscene built as a room: 68 of the 104 uses name a room
called "Seq - ...", whose background is the black `MAIN\BG1.BMP` (resource
10) and whose objects are full-screen clips. `SETSEQ` hands the room to a
separate screen (`QSystem+540`, created by `sub_4167C0`, vtable `0x4B9890`)
which shows it instead of the room the game is in. That screen does very
little, by its methods (read from the disassembly): its mouse move
(`0x416F20`) and its right button (`0x416FC0`) do nothing at all -- no
cursor, no captions, no star; its click (`0x416F30`) looks down its list for
a text object (resource `-2`, a phrase on screen) and hands the click to it,
which skips the phrase, and does nothing if there is none; its leave
(`0x416FD0`) takes the text down and, for every object on its list, frees
the clip, stops the object's sound (`sub_416EF0`) and forgets it. `Esc`
does not leave it.

Its **set-up** (`sub_416860(room)`), which `SETSEQ` comes to:

1. the text is taken down and whatever sequence was up before is let go as
   in the leave; **the game's sounds are paused** (`sub_40E2C0`, below);
2. the room goes on the list. Its music (`+148`) is compared with the music
   this screen last played (`+36`): the same, and it is kept going
   (`sub_416D90`); different, and the old one is stopped, the new one loaded
   and played. The same for its ambience (`+152`, kept at `+40`). So a
   sequence has its own tracks, apart from the room's;
3. every object `BACKGRND.BG` attaches to the room goes on the list with its
   clip loaded and its clip's sound ready (`+92`, with `+64` = 0 and `+68` =
   "it has one");
4. the screen makes itself current. Nothing is sent to the script.

**Pausing the sounds** is the sound manager's (`dword_4DE7A4`), which keeps
every loaded sound in a map by name:

- `sub_40E2C0` goes through the map and calls each sound's `sub_40EAE0`: a
  sound that has a buffer, is playing (`+44`) and is not already paused
  (`+48`) is marked paused and its DirectSound buffer stopped -- which
  leaves it where it was;
- `sub_40E3F0` calls each sound's `sub_40EB20`: a paused one is played
  again from there, looping if it was (`+52` or `+56`), and unmarked.

They are called in three places: by the sequence screen (the set-up pauses,
and the leave, after stopping its own objects' sounds, resumes); by the
disc prompt (pause before, resume on a cancel); and by the handler of
command 32793 (`0x4183C0`), which flips a flag at `QSystem+76` and pauses or
resumes accordingly -- a "pause the sound" command with no key in the
accelerator table. So during a sequence **everything** that was sounding
waits -- music, ambience, clip sounds, a voice -- and only what the sequence
starts is heard. (A film uses a different pair, `sub_40DAC0` and
`sub_40DB00`.)

The launcher's sequences behave the same (`sequence_sounds`, and two pairs
of streams): what was playing is paused and goes on afterwards, what the
sequence started is stopped when it ends, and the sequence's own music and
ambience are kept apart from the room's -- going on if the next sequence
names the same ones, as on the exe's screen. Nothing is broadcast: the sequence runs
on its clips' `END` reactions -- each sets the next clip -- and the last one
posts `ENDSEQ`, usually together with a `GOTO` for where the game continues:

```
OBJ "Темно" (in ROOM "Seq - ВСЕ ВЫХОДЯТ ИЗ АКТОВОГО ЗАЛА")
    on END: IF 1423 -> SET 1424        on END: IF 1424 -> SET 1425
    on END: IF 1425 -> SET 1426
    on END: IF 1426 -> КЛУБ GOTO, Петька ON, ВИЧ ON, ENDSEQ
```

`QSystem` start-up (`sub_405D00`) confirms several things read off the data
elsewhere in this document:

- objects 0 and 1 of `SCRIPT.DAT` are loaded straight into the two hero
  objects embedded in `QSystem` (`+160` Petka, `+344` Chapayev); the other
  plain objects are 124 bytes each, rooms 156;
- every object's shown name and colour are read from `NAMES.INI`/`CAST.INI`
  here, as on a chapter change;
- each room's `BGS.INI` `[Settings]` line is parsed as
  `"%lf %lf %d %d %lf"` into a per-room record, default `1.0 0.0 0 480 1.0`
  -- `a b y0 y1 max`, the hero scale of that room (below);
- `TOTALINIT` is posted to every object, in list order, with no sender, and
  before the cursor and the two panel objects are created -- so those three
  never get it.

**Hero scale** (`sub_409460`, the heroes' vtable slot `+68`): for a hero at
screen row `y` in a room whose `BGS.INI` line is `a b y0 y1 max`,

```
y     = min(y, 480)
grow  = max(0, (y - y0) * b / (y1 - y0))
scale = min(a + grow, max)
```

and 1.0 for a room with no line. So the hero is `a` times full size at row
`y0` and above, grows by `b` down to row `y1`, and `max` is a ceiling, not a
factor: `МОСТ = 0.9 0.0 0 480 0.9` is a flat 0.9. The walk step asks for
this on every frame, and the hero's clip is drawn at its frame size times it.

**Gait** -- how a hero moves along a route (`Walk.cpp`; implemented in
`core/src/walk.c` as `PetkaGait` / `PetkaWalker`, checked by `gaittest`).

Next to each of a hero's eight walk clips (`MAIN\WALK\P-W-N.FLC` ... `W-NW`,
in the order N, NE, E, SE, S, SW, W, NW) lie an `.OFF` and a `.LEG` file of
the same layout:

```
"xyof"
(clip frames + 1) x (i32 x, i32 y)     -- the last pair repeats the first
```

`.OFF` is how far the sprite moves on each frame, `.LEG` where in the sprite
the hero's foothold is on each frame. The constructor (`sub_422D30`) adds
them up into the step tables,

```
step[i] = OFF[i] + LEG[i] - LEG[i-1]        (i = 1..frames; step[0] = step[frames])
```

i.e. how far the foothold travels on reaching frame `i`. The bobbing of the
sprite and of the foothold inside it cancel out, leaving a steady advance
(about 10 pixels a frame for the eastward clip; one cycle moves Petka
(123, -1) east, (2, -20) north, (-119, -3) west, (3, 30) south). It also
keeps each clip's slope: the sum of the y steps over the sum of the x steps.

The hero's position is the foothold. A route is a list of points; for each
leg (`sub_423B80`):

1. the leg's angle is taken counter-clockwise from east with y pointing up,
   and the clip whose angle is nearest is chosen from the table
   `pi/2, pi/4, 0, -pi/4, -pi/2, -3pi/4, pi, 3pi/4` (built at start-up at
   `0x4E1F18`; read out of the four initialisers at `0x422C00`-`0x422C80`
   by hand);
2. the angle between the clip's own line of motion (its slope) and the leg
   is the turn, folded into +-90 degrees;
3. the leg is dry-run: steps are added up, each times the hero scale at the
   row reached, until ten in a row bring the hero no nearer to the leg's
   end; the count at the nearest approach is the leg's number of steps.

Each time the walk clip moves on a frame (`sub_424030`) the frame's step,
times the hero scale, is added to two running sums, and the position is the
leg's start plus those sums turned by the turn. When the steps run out the
next leg is set up from its exact start point; after the last one the hero
is put exactly on the destination. The sprite is drawn with the current
frame's `.LEG` point on the position.

`gaittest` walks 4000 random legs per hero and scale: the hero ends 1-5
pixels from the leg's end on average and always within one step, with all
eight clips in use -- which a wrong sign in the turn or swapped tables
would not survive. Where a walking hero's sprite goes is `sub_424190`: its
top-left corner is the position minus the current frame's `.LEG` point times
the hero scale at that row -- so the foothold stays on the position and the
sprite moves by exactly `.OFF`. Any other clip (standing, talking, acting)
is placed by the heroes' set-position method (`sub_4087D0`): the frame,
scaled by the hero scale at his row, stands on the position by the middle of
its bottom edge -- `x - w/2`, `y - h`. The two rules do not agree to the
pixel, so a hero shifts slightly as a walk starts or ends. The route itself
is described under `.CVX` above.

`CHAPTER n` (`sub_411C00`) opens the store named by `[Part P Chapter n]` in
`PARTS.INI` (`chap21.str`, `chap22.str`) and re-reads `NAMES.INI`/`CAST.INI`
from it for every object -- which is why those two files live in the chapter
stores.

`SHAKE` sets a flag (`+8`) on the screen object (`*(dword_4DE8E4 + 60)`).
While it is set, the frame function (`sub_422140`) puts the finished frame on
screen shifted right by 0 or 3 pixels, switching every 30 ms, and blacks out
the 3-pixel strip it leaves. Used twice, in one `PART_3` cutscene.

That frame function is the game's main loop body, and shows the order of a
frame: every object's update (vtable slot 6) with the milliseconds since the
last frame; then every object's slot `+48` (`0x401810` for scene objects,
`0x409530` for heroes: the recalculation of `z`); then the sort of the screen's objects by `z` ascending (`sub_422580`,
a selection sort); then every object's draw (slot `+20`). It is also where
the view scrolls: 8 pixels a frame towards the requested offset.

With that, every opcode the three parts use is accounted for.

### Scene object state variants — solved by the script

The "which FLC of a group is active" question (door/aquarium variants in
`P-ROOM.BG`) is answered here, not by per-room code: each variant is its own
script object, switched with `ON`/`OFF`/`SHOW`/`HIDE`/`SET`, and hidden ones
turn themselves `OFF` in `TOTALINIT`. The overlap heuristic in the launcher
can go once it draws from VM state.

## `DIALOGUE.FIX` / `DIALOGUE.LOD` bytecode VM — SOLVED (byte-exact, content-verified)

Full per-part dialogue/interaction data lives in `MAIN.STR` under
`PART_N/DIALOGUE.FIX` and `PART_N/DIALOGUE.LOD` (**not** inside the
per-part `BGS`/`FLC` stores — easy to miss). Reverse-engineered from
`Bigdlg.cpp`'s `BigDialogue` loader (`sub_40AF90`), destructor (`sub_40B270`,
confirms every field independently), and the runtime step/disassemble
helpers (`sub_40B500`, `sub_40BAA0`, `sub_40BC80`). Both the tree structure
and the bytecode semantics were validated **byte-exact to EOF** and
cross-checked against the actual extracted Russian dialogue text — not just
a plausible guess.

### `DIALOGUE.LOD`: phrase table + text blob

```
u32 phrase_count N
N × 24-byte phrase records:
    u32  speaker         id of the SCRIPT.DAT object that says the phrase
    char filename[16]    "11_01701.wav" style voice-clip filename (NUL-padded,
                         and legitimately EMPTY for ~58% of entries -- not
                         every phrase has its own voice clip)
    u32  _unused         (on-disk garbage, overwritten at load time)
immediately followed by:
N NUL-terminated strings, CP1251, in the same order as the phrase
records — the actual on-screen dialogue text for each phrase.
```

An earlier reading put a second, always-zero header word before the records
and two unused words after each filename. That is the same bytes shifted by
four: the "zero header word" was phrase 0's speaker (Petka, object 0), and it
cost phrase 0 the first four letters of its text ("Только не при Василь
Иваныче..." came out as "ко не при...").

Verified on all three parts: the table is `4 + 24*N` bytes, the text blob
runs to exactly EOF, and the speaker of every one of the 8420 phrases is an
existing object id that fits the line -- Petka (0) and Chapayev (1) say most
of them, then the part's characters (`PART_1`: object 173 "Анка", 280
"Фурманов", ...). The speaker is often a stand-in object rather than the
hero himself, e.g. "Петька - КОМНАТА ПЕТЬКИ", the Petka lying in bed in the
intro.

The speaker is what drives talking animations. Whoever starts speaking is
sent `SAY`, and `SAID` when the next phrase belongs to someone else or the
dialogue pauses or ends. A hero turns those into his talking clip (costume
+ 1) and back; other speakers have `SAY`/`SAID` reactions in the script that
do the same with their own clips (Anka: `SET 1614` / `SET 1607`). Only the
`SAID` at the end of a dialogue was seen in the exe (`sub_4165A0`); sending
them between phrases is inferred from the data.

### `DIALOGUE.FIX`: a 4-level tree index + one shared bytecode array

```
u32 topic_count N
N × 12-byte TopicRecord:      { u32 id; u32 n_subs; u32 _ptr_ignored }
for each topic, immediately: n_subs × 16-byte SubRecord:
                               { u32 verb_id; u32 n_subsubs; u32 data; u32 _ptr_ignored }
for each sub, immediately:    n_subsubs × 12-byte SubsubRecord:
                               { u32 bytecode_offset; u32 n_extra; u32 _ptr_ignored }
for each subsub, immediately: n_extra × u32   (optional extra data; empty for most entries)

then, once, after the whole tree:
u32 bytecode_word_count M
M × u32 bytecode words   -- ONE shared program for the whole file,
                            indexed into by every SubsubRecord.bytecode_offset
```

This is a **"flat array, then recurse into each child" serialization at
every level** (not fully depth-first-interleaved) — each level's whole
sibling block is read in one shot before recursing into any of them, which
is why naively reading header-then-immediately-recurse per node
desyncs the parser. All three pointer-sized "ignored" fields are leftover
build-tool memory (same `0x83xxxxxx`-pattern garbage seen elsewhere in
these formats), confirmed safe to skip by the destructor freeing exactly
those fields and no others.

Verified byte-exact on `PART_1/DIALOGUE.FIX` (48868 bytes): the tree
(`topic_count=285`) consumes bytes `[4, 30528)`, then
`bytecode_word_count=4584` at offset 30528 accounts for the remaining
`4 + 4*4584 = 18340` bytes — **`30528 + 18340 = 48868`, exactly the file
size.**

`TopicRecord` = one interactive object/NPC. `SubRecord.verb_id` = one
applicable verb/action for it (small IDs like 1/4/6/10 recur across many
different topics — a shared verb-ID space, e.g. look/use/take/talk).
`SubsubRecord.bytecode_offset` is a **direct word-index into the shared
bytecode array** — confirmed by cross-referencing: topic0's four subs have
`bytecode_offset` 0, 2, 20, 22, and those are exactly where four distinct
instruction sequences start in the decoded word dump.

Beyond the 4 standard verbs, `verb_id` also encodes a **5th, per-item
"use/put inventory item X on this object" verb**, decoded by reading real
reply text: `verb_id = 0xFFFE` (65534) is the generic reply when any item
is used on this object with no specific reaction written for it (e.g.
topic 89, the aquarium: *"Хватит бросать что-попало в аквариум."* / "Stop
throwing random stuff in the aquarium"; topic 95, the bed: *"Я не буду
класть что попало к себе в кровать."*). `verb_id = (item_id << 16) | 26`
is a reaction specific to inventory item `item_id` (e.g. topic 89 also has
`verb_id = (1<<16)|26` → *"Я выпил бы, да для желудка говорят вредно."* /
"I'd drink it, but they say it's bad for the stomach" — a joke reaction to
using a specific drink item on the aquarium). No item-id→name table was
found in `CAST.INI`/`NAMES.INI`/`INVNTR.TXT` to confirm which `item_id`
is which inventory item by number; inferred only from each reply's joke
content matching a specific item.

### Bytecode instruction set (word = `u32`, opcode = top byte)

| opcode | mnemonic | operand (low 24 bits)                    | meaning |
|-------:|----------|-------------------------------------------|---------|
| `0x01` | `DELIM`  | unused                                     | ends a menu label or a `SKIPN` variant |
| `0x02` | `CHOICE` | low byte = option count, bits 8-23 = one on/off bit per option | player dialogue menu, see below |
| `0x03` | `GOTO`   | u16 target                                 | jump to absolute word offset |
| `0x06` | `STOP`   | unused                                     | end of this instruction sequence (also used as an error sentinel by the runtime) |
| `0x07` | `PHRASE` | phrase index                               | speak/display phrase N (index into `DIALOGUE.LOD`'s phrase table — text + voice clip) |
| `0x08` | `SKIPN`  | count (low word), turn (third byte)        | say one of the next `count` `DELIM`-separated phrases, the next one each time (variant/idle-line selection) |
| `0x04` | `DISABLE` | `(option_index << 16) \| choice_addr`     | switch option `option_index` of the `CHOICE` at word `choice_addr` off |
| `0x05` | `ENABLE` | `(option_index << 16) \| choice_addr`      | switch that option on |
| `0x09` | `USERMSG` | message number                           | pause the dialogue and post script message `USERMSG` N to Chapayev (object 1); the script resumes it with `CONTINUE` |

A menu is laid out as

```
CHOICE(count | bits << 8)
count label segments, each ending in DELIM
count GOTOs                 -- one per option, into the code that plays it
```

The label is the first `PHRASE` of its segment; three menus carry another
word in a segment besides, so the segments have to be found by counting
`DELIM`s, not by assuming two words each. Bit `i` of the `CHOICE` word says
whether option `i` is offered. The code an option jumps to speaks the label
again (it is the player's line), plays the answer, and usually ends with
`DISABLE` for itself followed by a `GOTO` back to a menu, or `STOP`.

The bits live in the bytecode itself and are rewritten in place, so a line
that was said stays gone for the rest of the game. `ENABLE` is the opposite
and rare (13 in the three parts); every one of them targets an option whose
bit starts cleared -- e.g. topic 173 (Anka): "Ну что, Анка? Как там насчет
баньки?" is not offered until the conversation about the bathhouse has
switched it on. (An earlier reading called this "mark an outer menu's
option as used"; the starting bits show it is the reverse.) A `DISABLE` may
name any menu, not just the one being played: the options of Anka's
sub-menu at word 975 disable themselves there and then jump back to the
top menu at 951.

`SKIPN` is "one of these `count` phrases, **in turn**" (`sub_40B790`). The
word keeps whose turn it is in its third byte; the VM stops on the `SKIPN`
itself while the phrase is up, and when it moves on it writes
`(turn + 1) % count` back into the word and steps over `count` `DELIM`s, i.e.
the whole group -- to what is almost always a `STOP`, in eight cases more
dialogue. Nothing is random: an object's idle remarks come round in the
order they were written, and the place is kept for as long as the dialogue
code stays loaded. In the data every one of the 1089 variants is exactly one
`PHRASE` and a `DELIM`, and all 304 turns start at 0.

The phrase itself is handed out by `sub_40B500(&speaker, &wav, line)`, which
answers by the instruction the VM stands on:

- `PHRASE`: record number `word & 0xFFFF` of the phrase table -- 24 bytes
  each, the speaker at 0, the wav name at 4 and, at 20, a pointer to the
  text (the "unused" last field of a `DIALOGUE.LOD` record is where the
  loader puts it). No phrase word of the game has anything above bit 15.
- `SKIPN`: it steps **into** the group, over `turn` variants, and answers
  with the phrase it lands on -- and leaves the VM standing there. So the
  variant shown is the one whose turn it is, and this is what the advance's
  walk back from a `DELIM` is for: moving on steps over that phrase, meets
  the variant's `DELIM`, walks back to the `SKIPN`, passes the turn and
  steps over the group. The port keeps the VM on the `SKIPN` instead; the
  effect is the same.
- `CHOICE`, with `line` counting only the lines switched on: it finds that
  line's label, and if the label does not begin with a `PHRASE` runs the
  advance from there until it comes to one; then puts the VM back on the
  `CHOICE`. All 319 labels of the game begin with their `PHRASE` (three
  have a `DISABLE` after it, which is therefore never run from here), and
  the port takes the first `PHRASE` of the label.

The same routine is the whole of the VM's advance. Its argument is the menu
line picked, or `-1`; it runs instructions until it comes to a second thing
that needs showing, and stops on it:

- `PHRASE`: stepped over; `USERMSG`: stepped over, or, when come to, handed
  to `sub_409610` (the pause);
- `CHOICE`: come to, it stops (a menu). Left, the pick is turned from "the
  n-th line that is switched on" into a line number, the labels are stepped
  over and the pick's `GOTO` is taken. A pick of `-1` counts as 0, and one
  past the end as the last line. With **no** line switched on the pick runs
  off the end of the `GOTO` table and the dialogue goes on with whatever
  follows it;
- `GOTO`, `DISABLE` (which also tells the screen's menu, `sub_40C050`),
  `ENABLE`: done on the way; `STOP`: stops for good;
- `DELIM`: walks **back** to the nearest `SKIPN` or `CHOICE` before it and
  runs that again. This is how a `SKIPN` phrase is left (see `sub_40B500`
  above); nothing else in the game runs into a `DELIM` -- every one closes
  a menu label or a `SKIPN` variant, and no `GOTO` leads into either. The
  port, which never stands inside a group, ends the dialogue there.

Checked on the three parts: all 79 menus have this layout and all 213
`DISABLE`/`ENABLE` words name an existing option; `dlgtest` plays every
entry point 50 times with random menu picks -- 145,200 playthroughs, every
one comes to an end with all phrases and labels in range.

`0x09` was first read as "give inventory item N" because the two samples
looked at both hand something over (topic 220, the shopkeeper: "На, может
поумнеешь!" then `0x09(242)`). It is the dialogue's hook back into the
script: object 1 (Chapayev) has one `USERMSG` reaction per number, each
starting with `IF N`. Most just `CONTINUE`; the others run a cutscene first
(number 13 on the bridge makes Anka walk off, 242 switches the shopkeeper to
her hand-over animation). All 20 distinct operands in `PART_1` are numbers Chapayev reacts
to. Not traced in the exe; verified by running it -- the intro plays from
Petka's room to the bridge and back only when `0x09` is handled this way.

None of this is the mechanism behind scene object state variants
(door/aquarium visibility) — it's purely about the dialogue menu/inventory
system. Object state is driven by `SCRIPT.DAT`; see that section above.

Content-verified end to end: bytecode word 2 is `SKIPN(8)` followed by
`PHRASE(1), DELIM, PHRASE(2), DELIM, ..., PHRASE(8), DELIM` — decoding
phrases 1-8 against `DIALOGUE.LOD`'s text blob gives exactly Petka's
self-introduction monologue variants ("Это я.", "Любимый!", "Неправда ли,
хорош?", "Ладно скроен, крепко сбит!", "Умен не по годам...", "Да еще и
скромен, к тому же...", "Нравлюсь?..", "То-то же!") — a random-idle-line
system, which is exactly what `SKIPN` + `DELIM`-separated `PHRASE`s should
produce.

The glue to gameplay is the script: `TopicRecord.id` is the `SCRIPT.DAT`
object id (topic 89 is object 89 "Аквариум", topic 95 is "Кровать Петьки")
and `SubRecord.verb_id` is the message opcode (1 `USE`, 4 `LOOK`, 6 `TAKE`,
10 `TALK`, 26 `OBJECTUSE`). `processMessage` looks the pair up with
`sub_40BEB0` and the `DIALOG` message plays the entry it found.

### The dialogue step (`sub_415F80`)

One function moves a running dialogue on. Its argument is `-1` for "go on"
or the number of the menu line picked (who calls it is listed below). The dialogue object keeps a state
(`+24`): 1 while a phrase is up, 2 while a menu is, 3 otherwise -- its
constructor (`sub_415E00`), its start and its end (`sub_4165A0`) all write
3, the first and the last with `-1` for the dialogue's id (`+20`). The
screen's base constructor (`sub_4128C0`) only empties the object list and
the "object under the cursor". "Go on" is ignored in
state 2 and a pick in state 1, so a phrase cannot be answered and a menu
cannot be clicked away. Nothing happens either while the dialogue is paused
(`+4`).

Every step hides the cursor, advances the bytecode VM (`sub_40B790`; skipped
once after a start, flag `+16`) and acts on what it stopped at
(`sub_40B6E0`):

| kind | what the step does |
|---:|---|
| 1, phrase | stops the previous voice clip; loads the new one (speech folder + the phrase's wav name) and remembers whether there is one (`+12`); pans it by the speaker (`sub_40E0D0`, below) and plays it. If the speaker is not the previous phrase's, the old speaker (`+44`) gets `SAID` and the new one `SAY`. The subtitle is set on the current screen (slot `+44`). |
| 2, menu | stops the voice clip; the speaker gets `SAID` and is forgotten; the lines go to the main screen (slot `+60`) with three colours, `0xFFFFFF`, `0x0A0A0A`, `0xFF0000`; the cursor is shown again; state 2. With no lines to offer nothing more is done. |
| 3, end | `sub_4165A0` (above). |
| 4 | stops the voice clip, forgets the speaker **without** a `SAID`, clears the subtitle; state 1. This is the `USERMSG` pause (below). |

The kind comes from `sub_40B6E0`, which looks at the instruction the VM
stands on and moves it along until there is something to show:

| instruction | answer |
|---|---|
| no position, or `STOP` (6) | 3, end |
| `PHRASE` (7), `SKIPN` (8) | 1, phrase |
| `USERMSG` (9) | 4, pause |
| `CHOICE` (2) with **more than one** line switched on | 2, menu |
| `CHOICE` with one line or none switched on | advance with pick 0 and look again |
| anything else (`DELIM`, `GOTO`, `DISABLE`, `ENABLE`) | advance and look again |

So a menu is only put up for a real choice: when all but one of its lines
have been said and switched off, the last one is taken without asking.
`petka_dialoguevm_next` does the same, including the menu with no line
left (see `SKIPN` and the advance, above).

`SAY` and `SAID` are handed to the object's handler directly (vtable slot
`+52`), not queued, with no arguments and no sender.

Subtitle colours (COLORREF, `B<<16 | G<<8 | R`): a speaker with a `CAST.INI`
colour gets it, with `0x00007F` as the second colour; a speaker without one
gets `0x0A0A0A` on `0xFFFFFF`. A phrase whose speaker is not an object only
traces `DLG: No speaker`.

The pause itself is set up by `sub_409610(n)`, which the bytecode VM calls
on its `USERMSG n` instruction, before the step gets to row 4:

1. the speaker (`+44` of the dialogue), if there is one, gets `SAID`,
   directly -- which is why row 4 has none to send;
2. the dialogue is marked paused (`+4`), and `sub_416760` gives the cursor
   back what the dialogue keeps of it: shown or not (dialogue `+52` ->
   cursor `+44`), the over-an-object flag (`+56` -> `+48`), its picture
   (`u16 +50` -> `+4`) and its action (`u16 +48` -> `+128`). The dialogue
   saves these when it starts (the saving side was not decompiled), so the
   script that runs during the pause finds the cursor as it was before the
   first phrase;
3. `USERMSG` is **queued** for Chapayev (the id at `QSystem+422`) with
   arguments `n, 0, 0` and no sender.

`CONTINUE` lifts the pause. The launcher does all of this. It has nothing
to restore in step 2: its dialogue
never touches the cursor's state or the chosen action, it only takes the
input while a phrase or a menu is up.

A pause may be the last thing in a dialogue, and then `CONTINUE` ends it.
The launcher tells the script of that end at once, inside the `CONTINUE`,
not on the next frame: the reaction that the pause set off may still have a
`TALK` to come, and its `DIALOG` parks a new tail in the one slot there is
for "the rest after the dialogue". Told late, the old dialogue's end ran
that new tail after the new dialogue's first phrase. Part 2 has the case:
the lieutenant in the restaurant ends on `USERMSG 5`, Chapayev passes
`USERMSG 3` on to him and `CONTINUE`s, and the lieutenant's reaction is
`SETSEQ` (Anka at the door of the S&M room), `TALK` to `SpDialog23`, whose
tail starts the clip of her walking in -- which the next phrase then
replaced, so its `END` never came and the game stood still in the corridor.

**Who moves a dialogue on.** The step has seven callers (found by their
call instructions; the last four read from the disassembly):

- the click function `0x414320`, with the cursor hidden: `-1`;
- the message handler `sub_403270` and the dialogue's start (`0x415F20`);
- `0x420C70`, a click on the menu: the number of the line under the cursor
  (`+72` of the menu object), if it is one of its lines;
- `0x421310`: stops the voice clip and steps with `-1` -- "skip this
  phrase";
- `0x4164D0`: unless a menu is up, stops the voice clip, gives the speaker
  his `SAID`, and steps for as long as the VM stands on a phrase -- "skip to
  the next menu, pause or end". It is **dead code**: the exe holds no call
  or jump to it and its address is in no table, so the game has no way to
  skip a whole dialogue, only a phrase at a time;
- `0x421170`, the subtitle object's update, called with the milliseconds
  since the last frame. This is what ends a phrase by itself:
  - a phrase **with a voice clip** (dialogue `+12`) ends when the clip does:
    the dialogue's handle (`+28`, given to the clip when it is started, see
    `sub_413FA0`) is polled through an import called as `f(handle, 0)` --
    by its use `WaitForSingleObject` -- and the step is taken on the first
    frame it returns 0;
  - a phrase **without one** is timed: the step is taken once

    ```
    elapsed > 1000 + 30 * strlen(text)      milliseconds
    ```

    or at once, if a flag of the settings object (`+60` of `QSystem+544`)
    is clear -- by its effect "show subtitles": with no voice and no text
    there is nothing to wait for.

`sub_413FA0(name, a, b)` itself only finds the loaded clip by name and, if
it is not already playing (`+44` of the clip), starts it with `(a, b)`.

**Voice panning** (`sub_40E0D0(name, x, w)`): if the clip is loaded, its
DirectSound pan is set to

```
2500 * (2x - w) / (2w)      hundredths of a decibel, integer arithmetic
```

where `x` is the middle of the speaker's clip (the left edge of its `.MSK`
rect, plus the object's x, plus half the rect's width) and `w` is
`QSystem+112` -- by its place in the formula the width of the screen, 640.
So a voice is centred for a speaker in the middle of the screen, and at
either edge the far channel is 12.5 dB down (a gain of 0.24); it is never
taken out altogether. The launcher sets the same balance
(`speaker_pan`), taking `x` on screen, i.e. less the view's scroll -- whether
the original's object x already has the scroll taken off was not checked.

## Room topology — SOLVED: `BGS.INI` is the room graph, plain text

`PART_N/BGS.INI` (found inside `MAIN.STR`, same location/family as
`DIALOGUE.FIX`/`.LOD`/`CAST.INI`/`NAMES.INI` — **not** the per-part
`BGS*.STR`/`FLC*.STR` stores, which is why this was missed on the first
several passes looking for topology there). Plain CP1251 INI text:

```
[Settings]
StartRoom = Seq - INTRO0          ; or a room name directly
;StartRoom = КОМНАТА ПЕТЬКИ         (commented-out alternates, ignored)

<room name> = a b y0 y1 max        ; hero scale in that room, e.g.
                                    ; "0.3 0.7 130 290 1.0" (see below)

[<room name>]
<target room name> = <human-readable exit description>
...                                 ; one [section] per room; every line is
                                     ; one exit to an adjacent room
```

The value is not free text: it is the **name of a `SCRIPT.DAT` object**, the
entry point. On `GOTO` (`sub_401F80`) the engine reads key = previous room in
section = new room, finds that object and puts Petka on its walk point
(Chapayev 2 px above), scrolling the camera to `walk_x - 320` clamped to
`[0, background width - 640]`. All 135 exit lines of the three parts name an
existing object.

Implemented in `core/src/topology.c` (`petka_topology_parse`). Verified on
`PART_1/BGS.INI`: 28 rooms, **zero dangling exit targets** (every exit's
destination resolves to an actual `[room]` section) — e.g. `[БОЛОТО]` (the
swamp) has one exit to `ЛЕСНАЯ ДОРОГА` ("Выход из Болота на Лес с
Транспарантом"), `[ДОМ ПЕТЬКИ]` (Petka's house) has four exits
(`ВРЕМЯНКА`/`БАНЯ`/`ХУТОР`/`ОКНО`).

**Room name → `.BG` folder mapping — all 28/28 of PART_1 covered.** Room
names here (`БОЛОТО`, etc.) are the game's own Cyrillic display names, not
the `.BG` resource folder names used everywhere else (`BOLOTO.BG`) — no
such mapping exists anywhere in the data (checked `RESOURCE.QRC` and
`BGDATA.DAT`'s "KARTA" map-screen text, both dead ends). Built by hand in
`launcher/src/main.c`'s `kRoomFolders` table from transliteration plus
inspecting each folder's `.FLC` object names for unambiguous content clues
— e.g. `SHWARC.BG` contains a subfolder literally named `FURMANOF`, so it's
`КОМНАТА ФУРМАНОВА`; `LENIN.BG` has a `VHOD-B` ("entrance-to-bathhouse")
object matching `ДОМ ПЕТЬКИ`'s only unexplained exit (to `БАНЯ`). Verified
live in the engine: navigating into the mapped `ЛЕСНАЯ ДОРОГА` (→ `ROAD-L`)
renders a background with a literal signpost reading "ЛЕС **С
ТРАНСПАРАНТОМ**" ("forest **with a banner**") — exactly matching that
exit's own description text in `BGS.INI`, confirming the guess rather than
just plausibly fitting. Two entries (`ЛЕСНАЯ ДОРОГА`/`ЛЕСНАЯ ДОРОГА - 2` →
`ROAD-L`/`ROAD-R`) are flagged in the table as lower-confidence (no
distinguishing object-name evidence beyond generic road props), though the
transparent/signpost match above is actually decent indirect evidence this
one's right too.

## Tooling

`tools/strtool/main.c` — host-side (macOS/Linux/clang) lister/extractor.
Fully table-driven now (reads the trailer index above, no guessing). Usage:

```
strtool <file.str> [extract_dir]
```

With `extract_dir`, dumps every named resource to
`<extract_dir>/<original backslash-path-with-/-separators>`, creating
directories as needed. Verified against `BGS1.STR` (34/34 BMPs extracted,
valid per `file(1)`).

`tools/flictest/main.c` — dumps N decoded FLIC frames as `.ppm` for visual
inspection. `tools/dlgtest/main.c` — walks every topic/verb in a part's
dialogue tree and prints the resulting phrase text. `tools/topotest/main.c`
— prints a part's room graph and flags any dangling exit target. All three
take `<store.str> <part_prefix e.g. PART_1>` (dlgtest/topotest) or
`<store.str> <entry_name> <out_dir> [num_frames]` (flictest).

`launcher/` — the game front end: `petka_launcher <game_dir> [PART_N]`. It
draws the current room from the script VM's state, turns clicks into script
messages and tells the VM when animations, walks and dialogues finish; heroes
walk across the room's `.CVX` mesh, dialogue menus are shown for the player
to pick a line, whoever speaks plays his talking clip and his lines are in
his `CAST.INI` colour, objects are called by their `NAMES.INI` names, and
backgrounds wider than the screen scroll to keep Petka in view. They move
along the route by the game's own gait (one step per walk-clip frame, from
`.OFF`/`.LEG`). Not done yet: AVI playback, part switching, save/load. For unattended runs it reads
`PETKA_SHOT=<png>` / `PETKA_SHOT_FRAME=<n>` (skip the menu, screenshot,
quit), `PETKA_POST=<frame>:<obj>:<opcode>[:<arg1>:<arg2>],...`, `PETKA_FAST=1`,
`PETKA_PICK=<n>|last` (answer dialogue menus without asking),
`PETKA_MOUSE=<x>,<y>` (pin the pointer, in game pixels),
`PETKA_CASE=<frame>` (open the case on that frame),
`PETKA_RCLICK=<frame>` (the right button on that frame),
`PETKA_MAP=<frame>` (the map command on that frame),
`PETKA_PANEL=<frame>` (the panel command on that frame),
`PETKA_SAVE=<frame>` and `PETKA_LOAD=<frame>` (write and read `save1.dat`),
`PETKA_OPTIONS=<file>` (keep the panel's settings there; an unattended run
without it neither reads nor writes `Petka.ini`),
`PETKA_PLAY=<file>` (play the game from a list of steps -- the cursor's
actions on objects by id, things from the case, the map, menu answers,
checks of the room and the case, saves and screenshots -- each taken when
the game has come to rest; a step that cannot be taken ends the run with a
report, a screenshot `play-fail.png` and exit code 3. `PETKA_PLAY_LAST=1`
answers a menu no pick was named for with its last line. The steps are
listed at `play_tick` in the launcher; `tools/play/part1.play` goes through
the whole of part 1 by the published walkthrough, 220 steps, and leaves
`save2.dat` at the bridge with everything collected and `save3.dat` at the
start of part 2),
`PETKA_FILM=<seconds>:<file.png>` (play films in an unattended run, save the
frame reached after that long and stop there),
`PETKA_CLICK=<frame>:<x>:<y>[:<action>],...` (left-click there, with the
cursor's action number as in the table under "How a click becomes a
message"),
`PETKA_TURBO=1` (a fixed 60-steps-a-second clock run as fast as the machine
goes: the intro in seconds, and runs repeat exactly),
`PETKA_SHOT_SERIES=<start>:<step>:<count>` (more screenshots), `PETKA_TRACE=1`
and `PETKA_DEBUG=1` (start with the F1 overlay, which draws
the walk mesh and the heroes' routes).

Seven backgrounds are wider than the 640-pixel screen (six in `PART_2`, up to
2116 pixels -- the two corridors -- and one in `PART_3`). In such a room
everything is in background pixels: the object clips are as wide as the
background, and the MSK rects, walk mesh, walk points and hero positions
use the same coordinates, so only the view moves. On `GOTO` the exe centres
it on the entry point (`walk_x - 320`, clamped to the background).
Afterwards, whenever the followed hero is positioned within 160 pixels of
either side of the screen, the view is asked to centre on him (`x - 320`,
clamped; `sub_4087D0` writes it to `QSystem+108`, the view itself is
`QSystem+104`). The frame function (`sub_422140`) then moves the view 8
pixels a frame towards it.

`tools/looptest/main.c` — decodes every multi-frame `.FLC` of the given
stores twice round and fails if the second loop differs from the first:
`looptest <store.str>...`.

`tools/gaittest/main.c` — loads a hero's `.OFF`/`.LEG` step tables and walks
thousands of random legs, failing if the hero does not end up next to the
end of each: `gaittest <MAIN.STR> [clip prefix, e.g. MAIN\WALK\W-]`.

`tools/walktest/main.c` — parses every `.CVX` in a store and routes random
point pairs across each, failing if a route is missing or leaves the floor:
`walktest <FLCn.STR> [pairs_per_mesh]`.

`tools/scripttest/main.c` — parses a part's `SCRIPT.DAT` + `BACKGRND.BG` and
runs the message VM headless (animations and dialogues end instantly):

```
scripttest <store.str> <part_prefix> [room_id [obj_id opcode]]
```

With no room it prints statistics and runs `TOTALINIT`; with a room it traces
that room's `INITBG`; with an object and opcode it traces that message, e.g.
`scripttest MAIN.STR PART_1 524 89 1` plays the aquarium cutscene (`USE` on
object 89 in Petka's room).
