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

## BMP pixel format — SOLVED: non-standard 16bpp word order

Extracted BMPs (e.g. `BOLOTO.BMP`) are well-formed 640x480 **16bpp**
Windows BMPs (`biBitCount=16`, `biCompression=0/BI_RGB`, standard 54-byte
header, no BITFIELDS section) — `file(1)`'s `cbSize 921656` is just a stale
`bfSize` header field (real file is 614464 bytes = 640*480*2 + header,
exactly right for 16bpp; the field is simply wrong/unused, not a sign of a
different bit depth).

The catch: standard BMP decoders (including stb_image, which raylib uses
for `LoadImageFromMemory`) assume 16bpp `BI_RGB` pixels are little-endian
RGB565/555. Feeding these files through that path produces full-image
per-pixel color noise with perfectly sharp edges (confirmed — see
`/tmp/boloto_565.png` et al. during investigation) — a dead giveaway of a
bit-packing/endianness mismatch, not a structural (width/stride) one.

Found empirically by rendering every plausible hypothesis and comparing
(scripted A/B test, see commit history of this doc): the actual packing is
a **big-endian 16-bit word**, with bits `BBBBB GGGGGG RRRRR` (B in the high
5 bits, G in the middle 6, R in the low 5 — i.e. "BGR565" read as a
big-endian u16). 24bpp images (if any exist) are standard little-endian BGR
triples, no quirk.

Implemented in `core/src/bmp.c` (`petka_bmp_decode`) — decodes directly to
top-down RGB888, used by the launcher instead of raylib's built-in BMP
loader. Verified visually: swamp background renders correctly (blue sky,
green trees, red toadstools, campfire) end-to-end through the real
`petka_launcher` raylib window.

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

## `.MSK` mask files — rect confirmed, no real per-pixel data found

Reverse-engineered from `QFlic::Init`: the **last 16 bytes** of a `.MSK`
file are 4×`i32` — `left, top, right, bottom` — the screen rect this FLIC
clip is composited into. Implemented in `core/src/msk.c`
(`petka_msk_rect`), used by the launcher to crop/position FLIC overlays.

The body before that (the rest of the file) does **not** look like a real
per-pixel alpha mask in the samples inspected: small masks (e.g.
`PENEK.MSK`, 32 bytes) are *entirely* header (a redundant compact copy of
the same rect, `u16`-packed); a larger one (`BOLOTO-4.MSK`, 496 bytes)
turned out to be 28 repeats of the same rect followed by a perfectly linear
countdown sequence (336 → 0, step -12) — clearly fixed-size editor scratch
buffer padding, not meaningful mask data. Per-pixel transparency for FLIC
overlays instead comes from the clip's own palette index 0 treated as a
color key (confirmed visually: tree silhouettes render correctly against
both black and blue key-color backgrounds).

Known minor artifact: a faint horizontal dashed line sometimes appears
within the masked region (seen in both `ELKA.FLC` and `BANGS.FLC`). It's
present even on single-frame clips (whose first frame decodes via
`BYTE_RUN`, not the `SS2` delta path), so it isn't an `SS2` bug — more
likely either a genuine artifact in the original 1998 asset, or a seam
from our simplified "index 0 = transparent" rule where the real engine's
compositing is subtler. Cosmetic only, not investigated further yet.

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
  N * i32 index                    -- < countA: a corner (pool A index)
                                   -- >= countA: an edge (pool B index, used as is)
```

Pool B starts with the outline itself -- its first `countA` edges are
`(i-1, i)` -- followed by the diagonals that cut the outline into convex
pieces. A polygon's entries are its corners plus the diagonals it shares
with its neighbours, not a ring: the corner set is the listed corners plus
both ends of every listed diagonal, and since the polygon is convex, sorting
them by angle around the centroid gives the ring. Two polygons that list the
same diagonal are neighbours, and that diagonal is the way between them.

Checked on the 33 meshes of `PART_1`: the outline edges are as described,
every diagonal a polygon lists is listed by exactly two, and the polygons'
areas add up to the outline's area to the pixel. All 85 meshes of the three
parts parse to their exact length, and `walktest` routes 425,000 random
point pairs across them without one route missing or leaving the floor. Two
meshes (`OKNO.CVX`, `VREMUNKY.CVX`) contain a polygon
that is concave by a pixel or two -- level-editor slack, harmless.

Implemented in `core/src/walk.c`: `petka_walkmesh_open`, and
`petka_walkmesh_path`, which finds the chain of polygons between two points
(Dijkstra over centroids) and pulls the route tight through the shared
diagonals (funnel algorithm). This is our own pathfinder over the game's
mesh; how `Walk.cpp` itself routes was not decompiled, so the hero may take
a slightly different line than in the original.

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

### Opcodes

Names 0-46 are the exe's own (`sub_402C70` turns an opcode into its name).
47-62 have no name in the exe; those marked * are ours.

| op | name | effect |
|---:|---|---|
| 1, 4, 6, 10 | `USE`, `LOOK`, `TAKE`, `TALK` | player verbs; only trigger reactions |
| 3 | `GOTO` | (room) make this room current, see below |
| 5 / 28 | `SAY` / `SAID` | (hero) show costume+1 (talking) / costume (idle) |
| 9 | `WALK` | click with no reaction: Petka walks to the object |
| 11 | `END` | resume the tail parked by `PLAY` |
| 14 | `SET` | change the object's resource id; `arg2`: 1 = rewind, 5 = clears `+72` |
| 15 / 16 | `SHOW` / `HIDE` | visibility |
| 17 | `DIALOG` | start the current dialogue |
| 18 | `ZBUFFER` | `arg1` -> `+52`, `arg2` -> draw order |
| 19 | `TOTALINIT` | every object's setup reaction |
| 20 | `ANIMATE` | `+48` = `arg1` |
| 21 | `STATUS` | object status = `arg1` |
| 22 / 23 / 46 | `ADDINV` / `DELINV` / `SETINV` | inventory add / remove / replace the sender with this object |
| 24 | (exe: `SYSTEM`) | show/hide cursor and enable/disable the interface |
| 25 | `CURSOR` | cursor carries this item; `-1` = default cursor (5002) |
| 26 | `OBJECTUSE` | an inventory item was used on this object |
| 27 / 44 | `ACTIVE` / `PASSIVE` | `+60` |
| 29 / 30 | `SETSEQ` / `ENDSEQ` | (room) start / end a cutscene sequence |
| 31, 32 | `CHECK`, `IF` | conditions, see above |
| 33 | `SHOWDESCRIPTION` | show the object's text from `INVNTR.TXT` |
| 35 | `QM_WALKED` | (hero) resume the tail parked by a walk |
| 36 / 37 | `WALKTO` / `WALKVICH` | Petka / Chapayev walks to (`arg1`,`arg2`) or the object's walk point |
| 38 | `INITBG` | broadcast when a room is entered, sender = that room |
| 40 | `SYSTEM` | 0/1 = interface off/on, 242 = quit |
| 43 / 45 | `MAP` / `NOMAP` | (room) flag `+144` |
| 47 / 48 | `BGSFX`* / `MUSIC`* | (room) ambience / music resource id |
| 49 | `IMAGE`* | (hero) costume base id: +1 talking, +10..17 walking in 8 directions |
| 50 | `STAND`* | (hero) same as `SAID` |
| 51 / 52 | `ON`* / `OFF`* | active and visible / neither |
| 53 | `PLAY`* | `SET`, then wait for the animation to end |
| 54 | `LEAVEBG`* | broadcast when a room is left, sender = that room |
| 57 | `RANDOM`* | see above |
| 58 / 59 | `JUMP`* / `JUMPVICH`* | place Petka / Chapayev instantly |
| 60 / 61 | `PART`* / `CHAPTER`* | switch part / chapter |
| 62 | `AVI`* | play a video by resource id |

`CHAPTER n` (`sub_411C00`) opens the store named by `[Part P Chapter n]` in
`PARTS.INI` (`chap21.str`, `chap22.str`) and re-reads `NAMES.INI`/`CAST.INI`
from it for every object -- which is why those two files live in the chapter
stores.

Not understood: opcode 55 (sets a flag on the screen object; used twice in
`PART_3`), and `HALF`/`USERMSG`, which appear only as reaction triggers. Who
sends `TOTALINIT` and `END` was not traced; the VM broadcasts the former at
start-up and leaves the latter to the host's animation code.

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

## Room topology — SOLVED: `BGS.INI` is the room graph, plain text

`PART_N/BGS.INI` (found inside `MAIN.STR`, same location/family as
`DIALOGUE.FIX`/`.LOD`/`CAST.INI`/`NAMES.INI` — **not** the per-part
`BGS*.STR`/`FLC*.STR` stores, which is why this was missed on the first
several passes looking for topology there). Plain CP1251 INI text:

```
[Settings]
StartRoom = Seq - INTRO0          ; or a room name directly
;StartRoom = КОМНАТА ПЕТЬКИ         (commented-out alternates, ignored)

<room name> = <5 numbers>          ; per-room walk/perspective scale params,
                                    ; e.g. "0.3 0.7 130 290 1.0" -- not
                                    ; parsed (not needed for the graph
                                    ; itself; likely camera/character
                                    ; depth-scaling factors, unverified)

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
his `CAST.INI` colour, and objects are called by their `NAMES.INI` names. Not done yet: per-frame hero offsets (`.OFF`/`.LEG`),
AVI playback, part switching, save/load. For unattended runs it reads
`PETKA_SHOT=<png>` / `PETKA_SHOT_FRAME=<n>` (skip the menu, screenshot,
quit), `PETKA_POST=<frame>:<obj>:<opcode>,...`, `PETKA_FAST=1`,
`PETKA_PICK=<n>|last` (answer dialogue menus without asking),
`PETKA_MOUSE=<x>,<y>` (pin the pointer, in game pixels), `PETKA_TRACE=1`
and `PETKA_DEBUG=1` (start with the F1 overlay, which draws
the walk mesh and the heroes' routes).

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
