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

**Known decoder bug, not yet fixed:** `core/src/flic.c`'s frame decode
produces visibly corrupted output (horizontal smearing/tearing) for at
least `PART_1/P-ROOM.BG/KROVATI.FLC` (a single-frame, i.e. no inter-frame
delta involved) — confirmed it's a decode bug and not a launcher
scaling/draw issue by dumping the raw decoded frame directly via
`flictest` (`tools/flictest`) and seeing the same smearing in the `.ppm`
output. Likely a bug in one of the chunk-type decoders (`BYTE_RUN`/`SS2`)
for this file's specific chunk layout, not yet isolated. Other FLCs
(campfire, pond ripple, birds, signposts, etc.) decode and animate
correctly, so this isn't a wholesale format misunderstanding — probably an
edge case (chunk size, packet count, or line-count field) the simpler
test files never exercised.

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

## `BGDATA.DAT` / `BGEDIT.BG` / `BACKGRND.BG` / `SCRIPT.DAT` — level-editor debug artifacts, deprioritized

Found under `MAIN.STR`'s `PART_0` *and* `PART_1`/`PART_2`/`PART_3` entries
(bigger for the real parts, e.g. `PART_1/SCRIPT.DAT` is 53KB vs `PART_0`'s
1.9KB — scales with how many interactive objects that part has, but it's
the same record family either way — initially mistaken for real per-part
gameplay script logic given the size and promising name, until decoded).
Content is overwhelmingly background-editor debug-panel UI strings
(`"Панель опций - слайдер 1 min"`, `"STARTUP - New"`, `"SAVELOAD"`) plus
character names — not per-background hotspot/trigger coordinates as
originally suspected, and not related to the real dialogue/interaction
data (that's `DIALOGUE.FIX`/`.LOD`, solved below). Record shape is roughly
`u32 index/flag, u32 strlen, string bytes, then ~20 bytes of 0x00/0xFF
padding` but exact padding semantics weren't worth fully nailing down —
this is **`BGEDIT` = Background Editor**, i.e. leftover authoring-tool
state, not something the shipped game needs at runtime for rendering or
gameplay. **Deprioritized — skip unless something later turns out to
depend on it.**

Actual per-screen interactivity is the combination of the already-solved
`.FLC`+`.MSK` pairs (each interactive object = one animated clip + one
clickable mask rect), the `.CVX` walk mesh, and — now solved — the
`DIALOGUE.FIX`/`.LOD` topic/verb/bytecode system below for what happens on
interaction.

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
u32 _unused        (always 0 in samples seen)
N × 24-byte phrase records:
    char filename[16]   "11_01701.wav" style voice-clip filename (NUL-padded,
                         and legitimately EMPTY for ~58% of entries -- not
                         every phrase has its own voice clip)
    u32  _unused1        (on-disk garbage, overwritten at load time)
    u32  _unused2
immediately followed by:
N NUL-terminated strings, CP1251, in the same order as the phrase
records — the actual on-screen dialogue text for each phrase.
```

Verified on `PART_1/DIALOGUE.LOD`: `N=2916`, phrase table is exactly
`8 + 2916*24 = 69992` bytes, and the text blob runs to **exactly** EOF
(164025 bytes total) with no remainder.

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
| `0x01` | `DELIM`  | unused                                     | separates alternatives within a block |
| `0x02` | `CHOICE` | low byte = option count, bits 8-23 = availability bitmask | player dialogue menu: pick the Nth *available* option, where availability is tested per `DELIM`-separated segment against game-state flags |
| `0x03` | `GOTO`   | u16 target                                 | jump to absolute word offset |
| `0x06` | `STOP`   | unused                                     | end of this instruction sequence (also used as an error sentinel by the runtime) |
| `0x07` | `PHRASE` | phrase index                               | speak/display phrase N (index into `DIALOGUE.LOD`'s phrase table — text + voice clip) |
| `0x08` | `SKIPN`  | count                                      | randomly pick one of the next `count` `DELIM`-separated phrase blocks (variant/idle-line selection) |
| `0x04` | `MARKOPT` | `(option_index << 16) \| choice_addr`      | mark option `option_index` of the `CHOICE` block at word offset `choice_addr` as used, so it won't be offered again |
| `0x05` | `MARKOPT_OUTER` | `(option_index << 16) \| choice_addr`| same as `MARKOPT`, but for a `CHOICE` *other than* the nearest enclosing one (see below) |
| `0x09` | `GIVEITEM` | item id                                  | grant inventory item N to the player |

`MARKOPT`/`MARKOPT_OUTER`/`GIVEITEM` fully decoded by tracing real
conversations byte-by-byte (not guessed from isolated operand patterns —
see `PART_1/DIALOGUE.FIX` topic 173, the Petka/Anka/Vasily banya
conversation, and topic 220, the shopkeeper). Confirms and completes
`CHOICE`'s "availability bitmask" note above: a `CHOICE` block is a player
dialogue menu, each option followed (after its own reply text) by one of
these two mark instructions, naming *this same* `choice_addr` with the
*option index* that was just taken — the runtime must flip that bit in
the bitmask so re-visiting this `CHOICE` (dialogues loop back via `GOTO`
to let the player ask something else) no longer offers an already-heard
line. `MARKOPT_OUTER` is rarer (4 occurrences) and marks an *ancestor*
`CHOICE` instead of the enclosing one — seen when a nested sub-conversation
fully resolves a question that was also a top-level menu option, so both
get retired together (topic 173: finishing the inner "баня" chat, reached
through menu-option 2 of `CHOICE(1058)`, also marks option 4 of the outer
`CHOICE(951)` — the standalone "Как там насчет баньки?" question — as
answered). `GIVEITEM`'s operand is a small item id (4-244 in
`PART_1/DIALOGUE.FIX`); confirmed by context, not a lookup table (no
numeric item-id table exists in `CAST.INI`/`NAMES.INI`/`INVNTR.TXT` — item
names there are plain strings with no ids) — e.g. word 1533-1534 in topic
220 is the shopkeeper saying "На, может поумнеешь!" ("Here, might make you
smarter") handing over a book, immediately followed by `GIVEITEM(242)`;
word 1576-1577 is her giving seeds instead of a palm tree, followed by
`GIVEITEM(243)`.

None of this turned out to be the mechanism behind "Scene object state
variants" below (door/aquarium visibility) — it's purely about the
dialogue menu/inventory system. That problem remains unsolved; see that
section.

Content-verified end to end: bytecode word 2 is `SKIPN(8)` followed by
`PHRASE(1), DELIM, PHRASE(2), DELIM, ..., PHRASE(8), DELIM` — decoding
phrases 1-8 against `DIALOGUE.LOD`'s text blob gives exactly Petka's
self-introduction monologue variants ("Это я.", "Любимый!", "Неправда ли,
хорош?", "Ладно скроен, крепко сбит!", "Умен не по годам...", "Да еще и
скромен, к тому же...", "Нравлюсь?..", "То-то же!") — a random-idle-line
system, which is exactly what `SKIPN` + `DELIM`-separated `PHRASE`s should
produce.

Not yet mapped: exactly how `TopicRecord.id` and `SubRecord.verb_id`
connect to a specific clicked screen object and cursor action at runtime
(i.e. the glue between `QObject`/`QM_*` messages and a topic/verb lookup) —
needed to actually *drive* dialogue from gameplay, not just extract/replay
it. The data format itself, however, is fully solved: all dialogue text,
voice-clip filenames, and branching/random-variant logic can already be
extracted and replayed from any `PART_N/DIALOGUE.FIX`+`.LOD` pair.

### Scene object state variants — NOT SOLVED: which FLC of a group is "active" at a given time

Some rooms' top-level FLC+MSK pairs aren't independent simultaneous props;
they're mutually exclusive **state variants of the same physical object**.
`PART_1/P-ROOM.BG` (Petka's room) is the clearest example: `CLOSEDOR` /
`DOR-OPEN` / `D-OPEN1` / `OPEN-DOR` / `DOR-DOWN` / `D-CLOSE` / `DOORDOWN`
are 7 states of one door, `AQUA-FSH` / `AQUAR-1` / `AQUAR-2a` / `AQUAR-5` /
`AQUAR-6` / `FISH&KEY` / `RIBKA` are variants of the aquarium, `KEY-UP` /
`KEY-DOWN` are a key's two positions. Rendering every one of them at once
(what the engine does today, lacking real state data) produces visual
garbage -- all variants stacked on the same screen position. Current
engine behavior is a heuristic workaround, not a real fix: scene-object
loading keeps only the first-loaded object in any group whose MSK rects
overlap by ≥60% of the smaller rect's area, discarding the rest (see
`build_object_list`'s caller in `launcher/src/main.c`).

Investigated and ruled out as the "real" state-selection mechanism:
- **IDA string search** for the specific FLC names (`AQUAR-1`, `KEY-UP`,
  etc.) inside `Petka.exe` — no hits. The engine doesn't address these
  resources by name at all.
- **`RESOURCE.QRC`** (found under `MAIN.STR`'s `PART_N` entries, not
  previously catalogued) — a plain-text `ID = PART_N\path` table (CRLF,
  `id<tab/spaces>=<tab/spaces>path`), explaining the *lack* of string
  literals: the engine references resources by this numeric ID instead.
  Each room gets a contiguous ID block (P-ROOM is 1700-1762). This solves
  the "where's the ID→path table" question but **is not itself a
  state/visibility table** — every ID (including every door/aquarium
  variant) is listed once, flatly, with no grouping or flag info. Some
  note: a few entries use `==` instead of `=` (e.g. `1318 == MAIN\...`);
  meaning not understood, doesn't correlate with the state-variant groups
  seen in P-ROOM (those all use plain `=`).
- **`DIALOGUE.FIX`'s `SubRecord.data` field** (documented as unused
  padding above) — confirmed always `0` across all 285 topics in
  `PART_1/DIALOGUE.FIX`. Not a hidden resource-ID reference.
- **`BGDATA.DAT`/`SCRIPT.DAT`** — already established above to be editor
  debug-panel strings, re-checked specifically for these object names via
  `strings`, no hits.
- The `0x04`/`0x05` bytecode opcodes (see the instruction set table above)
  are now **fully decoded**: `MARKOPT`/`MARKOPT_OUTER`, which retire a
  used dialogue-menu option so it isn't offered again. Ruled out as the
  object-visibility mechanism — confirmed to be purely about the dialogue
  `CHOICE` menu system, nothing to do with which FLC is drawn.
- `0x09` is also decoded (`GIVEITEM`, grants an inventory item) — same
  verdict, unrelated to scene rendering.

Likely conclusion: this is real per-room game logic compiled into
`Petka.exe` (e.g. a `CRoom`-style per-room message handler), not a generic
data format — consistent with this being a Qt-based engine where business
logic lives in C++ classes, not Qt's own (generic) plumbing. Finding it
would mean locating each room's handler function in IDA without a string
anchor to pivot from (e.g. via xrefs from the FLC-loading function
filtered to the room's QRC ID range) -- not attempted; large effort for
uncertain payoff relative to the overlap heuristic above.

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
