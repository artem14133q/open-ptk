// Parser for Petka's room-topology config, PART_N/BGS.INI (found inside
// MAIN.STR, not the per-part BGS/FLC stores -- same family as CAST.INI/
// NAMES.INI: plain CP1251 INI text). See docs/str-format.md.
//
// Layout:
//   [Settings]
//   StartRoom = <room name, or "Seq - XXXX" for a cutscene entry point>
//   <room name> = <5 numbers>   -- per-room walk/perspective scale params,
//                                  not parsed here (not needed for the
//                                  room graph itself)
//
//   [<room name>]
//   <target room name> = <human-readable exit description>
//   ...                          -- one [section] per room, listing every
//                                   exit to an adjacent room
//
// Room names here are the game's own display names (often Cyrillic, e.g.
// "БОЛОТО"), NOT the .BG resource folder names (e.g. "BOLOTO.BG") -- no
// direct mapping between the two has been found; matching them (e.g. by
// transliteration) is left to the caller.
#ifndef PETKA_TOPOLOGY_H
#define PETKA_TOPOLOGY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *target; // owned, CP1251
    char *description; // owned, CP1251
} PetkaExit;

typedef struct {
    char *name; // owned, CP1251
    PetkaExit *exits;
    uint32_t exit_count;
} PetkaRoom;

typedef struct {
    char *start_room; // owned, CP1251 (may be NULL if no StartRoom= line)
    PetkaRoom *rooms;
    uint32_t room_count;
} PetkaTopology;

// Parses a whole BGS.INI buffer (CP1251 text). Returns NULL only on
// allocation failure -- unlike the binary formats, there's no checksum/EOF
// alignment to validate against, so malformed input is best-effort parsed
// rather than rejected.
PetkaTopology *petka_topology_parse(const uint8_t *data, size_t len);
void petka_topology_close(PetkaTopology *topo);

// Convenience lookup by room name (case-sensitive, CP1251 byte compare).
// Returns NULL if not found.
const PetkaRoom *petka_topology_find_room(const PetkaTopology *topo, const char *name);

#ifdef __cplusplus
}
#endif

#endif
