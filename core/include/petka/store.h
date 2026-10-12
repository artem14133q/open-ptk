// Reader for Petka's "StOR" resource containers (.STR files).
// Format reverse-engineered from Petka.exe -- see docs/str-format.md.
// Pure C, no platform dependencies: builds for desktop and Switch alike.
#ifndef PETKA_STORE_H
#define PETKA_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;       // on-disk name, backslash-separated, e.g.
                      // "PART_1\\BOLOTO.BG\\BOLOTO.BMP"
    uint32_t offset;  // absolute byte offset into the store's data
    uint32_t length;  // byte length of this resource
} PetkaEntry;

typedef struct {
    uint8_t *data;    // whole file contents, owned by the store
    size_t size;
    PetkaEntry *entries;
    uint32_t entry_count;
} PetkaStore;

// Reads a whole .STR file into memory and parses its trailing index.
// Returns NULL on failure (I/O error, bad magic, malformed index).
PetkaStore *petka_store_open(const char *path);

void petka_store_close(PetkaStore *store);

// Case-insensitive lookup by on-disk name. Returns NULL if not found.
const PetkaEntry *petka_store_find(const PetkaStore *store, const char *name);

// Pointer directly into the store's buffer for this entry (valid as long
// as the store stays open). out_len may be NULL.
const uint8_t *petka_store_entry_data(const PetkaStore *store, const PetkaEntry *entry, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
