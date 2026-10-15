// Reader for Petka's "StOR" resource containers (.STR files).
// Format reverse-engineered from Petka.exe -- see docs/str-format.md.
// Pure C, no platform dependencies: builds for desktop and Switch alike.
//
// RU: Архивы игры (.STR): оглавление при открытии, данные записей читаются по
// запросу.
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
    void *file;       // the open file (FILE *); entries are read from it on demand
    size_t size;
    PetkaEntry *entries;
    uint32_t entry_count;
    uint8_t **cache;  // one buffer per entry once it has been read, else NULL
} PetkaStore;

// Opens a .STR file and parses its trailing index; the resources themselves
// are read only when asked for. Returns NULL on failure (I/O error, bad
// magic, malformed index).
// RU: Открывает архив .STR: читает оглавление.
PetkaStore *petka_store_open(const char *path);

// EN: Closes the store.
// RU: Закрывает архив.
void petka_store_close(PetkaStore *store);

// Case-insensitive lookup by on-disk name. Returns NULL if not found.
// RU: Запись архива по имени, NULL если нет.
const PetkaEntry *petka_store_find(const PetkaStore *store, const char *name);

// The entry's bytes, read from the file on the first call and kept: the
// pointer stays valid until the store is closed or the entry released.
// Returns NULL on a read error. out_len may be NULL.
// RU: Байты записи (читаются при первом обращении).
const uint8_t *petka_store_entry_data(const PetkaStore *store, const PetkaEntry *entry, size_t *out_len);

// Lets the entry's bytes go, for a resource whose copy is all that is
// needed (a voice line once made into a sound). Asked for again, it is read
// again. Any pointer to the old bytes is no longer valid.
// RU: Освобождает прочитанные байты записи.
void petka_store_entry_release(PetkaStore *store, const PetkaEntry *entry);

#ifdef __cplusplus
}
#endif

#endif
