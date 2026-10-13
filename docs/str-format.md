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
walking (+164):  z = y of the point returned by sub_424210 (not decompiled;
                 by its neighbour sub_424190, the walk's current point)
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
  under the cursor changed and the screen's `+68` is 3 (meaning not known),
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
  being a value of the object at `QSystem+544` (it looks like the walk-speed
  slider of the options panel);
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
| 33 | `SHOWDESCRIPTION` | show the object's text from `INVNTR.TXT` |
| 35 | `QM_WALKED` | (hero) resume the tail parked by a walk |
| 36 / 37 | `WALKTO` / `WALKVICH` | Petka / Chapayev walks to (`arg1`,`arg2`) or the object's walk point |
| 38 | `INITBG` | broadcast when a room is entered, sender = that room |
| 40 | `SYSTEM` | 0/1 = panel off/on (not the cursor), 242 = quit |
| 43 / 45 | `MAP` / `NOMAP` | (room) flag `+144` |
| 47 / 48 | `BGSFX`* / `MUSIC`* | (room) ambience / music resource id |
| 49 | `IMAGE`* | (hero) costume base id: +1 talking, +10..17 walking in 8 directions |
| 50 | `STAND`* | (hero) same as `SAID` |
| 51 / 52 | `ON`* / `OFF`* | active and visible / neither |
| 53 | `PLAY`* | `SET`, then wait for the animation to end |
| 54 | `LEAVEBG`* | broadcast when a room is left, sender = that room |
| 55 | `SHAKE`* | screen shake on (1) / off |
| 57 | `RANDOM`* | see above |
| 58 / 59 | `JUMP`* / `JUMPVICH`* | place Petka / Chapayev instantly |
| 60 / 61 | `PART`* / `CHAPTER`* | switch part / chapter |
| 62 | `AVI`* | play a video by resource id |

A **sequence** is a cutscene built as a room: 68 of the 104 uses name a room
called "Seq - ...", whose background is the black `MAIN\BG1.BMP` (resource
10) and whose objects are full-screen clips. `SETSEQ` hands the room to a
separate interface object (`QSystem+540`, created by `sub_4167C0`; its
methods are at `0x416860` and `0x416FD0`, not decompiled) which shows it
instead of the room the game is in. Nothing is broadcast: the sequence runs
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
| `0x08` | `SKIPN`  | count                                      | randomly pick one of the next `count` `DELIM`-separated phrase blocks (variant/idle-line selection) |
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

`SKIPN` plays one of the next `count` variants. When that variant's `DELIM`
is reached the dialogue goes on after the whole group -- almost always a
`STOP`, in eight cases more dialogue. That, and the random pick, are read off
the data; the exe's own routine was not decompiled, and it may well cycle
through the variants in order rather than pick at random.

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
(a click with the cursor hidden; presumably also the end of the voice clip)
or the number of the menu line picked. The dialogue object keeps a state
(`+24`): 1 while a phrase is up, 2 while a menu is. "Go on" is ignored in
state 2 and a pick in state 1, so a phrase cannot be answered and a menu
cannot be clicked away. Nothing happens either while the dialogue is paused
(`+4`).

Every step hides the cursor, advances the bytecode VM (`sub_40B790`; skipped
once after a start, flag `+16`) and acts on what it stopped at
(`sub_40B6E0`):

| kind | what the step does |
|---:|---|
| 1, phrase | stops the previous voice clip; loads the new one (speech folder + the phrase's wav name) and remembers whether there is one (`+12`); pans it by the speaker (`sub_40E0D0`: the middle of his clip's `.MSK` rect plus his x, against the view offset `QSystem+112`) and plays it. If the speaker is not the previous phrase's, the old speaker (`+44`) gets `SAID` and the new one `SAY`. The subtitle is set on the current screen (slot `+44`). |
| 2, menu | stops the voice clip; the speaker gets `SAID` and is forgotten; the lines go to the main screen (slot `+60`) with three colours, `0xFFFFFF`, `0x0A0A0A`, `0xFF0000`; the cursor is shown again; state 2. With no lines to offer nothing more is done. |
| 3, end | `sub_4165A0` (above). |
| 4 | stops the voice clip, forgets the speaker **without** a `SAID`, clears the subtitle; state 1. By elimination this is the `USERMSG` pause; `sub_40B6E0` was not decompiled. |

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
2. the dialogue is marked paused (`+4`), and `sub_416760` is called on it
   (not decompiled);
3. `USERMSG` is **queued** for Chapayev (the id at `QSystem+422`) with
   arguments `n, 0, 0` and no sender.

`CONTINUE` lifts the pause. The launcher does all of this; panning is the
one thing it leaves out.

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
`PETKA_CLICK=<frame>:<x>:<y>[:<verb>],...` (left-click there),
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
