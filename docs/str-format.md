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

## `.CVX` convex walk-mesh — SOLVED (byte-exact, via decompiled `Walk.cpp`)

Reverse-engineered from `sub_423390` (confirmed as `Walk.cpp` via the
`aDGameWalkCpp` string constant passed to every allocation). This is the
walkable-area navigation mesh, loaded by lowercasing the background's own
filename, truncating at a known substring, and appending `CVX` — i.e. it's
derived from the background name, not looked up via a `.cvx` string literal
(which is why that string search came up empty).

```
u32 countA
countA * (i32 x, i32 y)     -- pool A: outline vertex points
u32 countB
countB * (i32 idxFrom, i32 idxTo)  -- pool B: edges, as index pairs into pool A
u32 poly_count
poly_count * u32            -- vertex count per convex polygon
for each polygon:
  N * i32 index             -- combined index: < countA -> pool A vertex,
                             --                >= countA -> pool B edge (index - countA)
```

Verified byte-exact against `BOLOTO.CVX` (976 bytes): `countA=30` (the
outline), `countB=48` (30 boundary edges + 18 internal diagonals splitting
the outline into convex pieces), `poly_count=19` convex polygons — **parses
to exactly 976/976 bytes, zero leftover** (this resolved the earlier
"mystery 188 trailing bytes," which was just mis-framing from guessing the
layout before having the decompiled loader).

Still open: the exact geometric meaning of a polygon index that resolves
into pool B (an edge, i.e. two vertex indices, not a single point) isn't
nailed down -- plausibly a half-edge/winged-edge style mesh representation
used for fast neighbor lookups during A* pathfinding (shared edges between
adjacent convex cells). Not needed until pathfinding/walking is actually
implemented; the byte layout itself is fully solved either way.

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

## `CAST.INI` / `NAMES.INI` — SOLVED: plain text, found in `CHAP21/22.STR`

Both are plain `cp1251` (not cp866!) INI text, trivial to parse:

- **`CAST.INI`**: `<character/animation-state name> = <R> <G> <B>` — a color
  per character/state (likely name-tag or outline color), e.g.
  `Петька = 255 255 0`, `Василий Иванович = 0 255 0`.
- **`NAMES.INI`**: `I<internal item/object name> = <display name>` — inventory
  item localization, where many internal variants collapse to one display
  name, e.g. `IГолова Чапаева = Голова`, `IГолова с Усами = Голова`.

## `BGDATA.DAT` / `BGEDIT.BG` / `BACKGRND.BG` / (`PART_0` only) `SCRIPT.DAT` — level-editor debug artifacts, deprioritized

These are small (under 2KB) binary tables found only in `MAIN.STR`'s
`PART_0` entries. Content is overwhelmingly background-editor debug-panel
UI strings (`"Панель опций - слайдер 1 min"`, `"STARTUP - New"`,
`"SAVELOAD"`) plus the two hero names (`Петька`, `Василий Иванович`) — not
per-background hotspot/trigger coordinates as originally suspected. Record
shape is roughly `u32 index/flag, u32 strlen, string bytes, then ~20 bytes
of 0x00/0xFF padding` (confirmed over ~19 records in `BGDATA.DAT`, with two
irregular short-padding exceptions) but exact padding semantics weren't
worth fully nailing down — this is **`BGEDIT` = Background Editor**, i.e.
leftover authoring-tool state, not something the shipped game needs at
runtime for rendering or gameplay. **Deprioritized — skip unless something
later turns out to depend on it.**

Actual per-screen interactivity is most likely just the already-solved
`.FLC`+`.MSK` pairs (each interactive object = one animated clip + one
clickable mask rect) combined with the `.CVX` walk mesh — no separate
hotspot format may even exist.

## Genuinely still open (deferred — separate, large task)

**`dialogue.dat` bytecode VM** (`Bigdlg.cpp`, `BigDialogue`) — dialogue
trees are compiled bytecode interpreted by a small VM baked into the engine
itself (confirmed via `"DisAssembled *ip:\n%s\n"`, `"goto %04xh\n"`, `"code
%02xh is not disassembled yet\n"`). This is effectively "reverse-engineer a
small custom instruction set," comparable in scope to the rest of this
investigation combined — not a format worth cracking by byte-staring. Punt
this to its own dedicated session once dialogue/interaction is actually on
the critical path (not needed for rendering backgrounds/animations or
walking around).

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
