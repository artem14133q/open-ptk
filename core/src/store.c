#include "petka/store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define petka_strcasecmp _stricmp
#else
#include <strings.h>
#define petka_strcasecmp strcasecmp
#endif

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void store_free(PetkaStore *store) {
    if (store->entries) {
        for (uint32_t i = 0; i < store->entry_count; i++) free(store->entries[i].name);
        free(store->entries);
    }
    if (store->cache) {
        for (uint32_t i = 0; i < store->entry_count; i++) free(store->cache[i]);
        free(store->cache);
    }
    if (store->file) fclose((FILE *)store->file);
    free(store);
}

// Only the header and the index at the end are read here: the stores are
// large (the three speech stores alone are over 900 MB), and a game is played
// from a small part of them. An entry's bytes are read when first asked for.
PetkaStore *petka_store_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    PetkaStore *store = (PetkaStore *)calloc(1, sizeof(PetkaStore));
    if (!store) { fclose(f); return NULL; }
    store->file = f;

    uint8_t head[8];
    if (fseek(f, 0, SEEK_END) != 0) { store_free(store); return NULL; }
    long filesize = ftell(f);
    if (filesize < 8 || fseek(f, 0, SEEK_SET) != 0 || fread(head, 1, 8, f) != 8 || memcmp(head, "StOR", 4) != 0) {
        store_free(store);
        return NULL;
    }
    store->size = (size_t)filesize;

    uint32_t trailer_off = read_u32le(head + 4);
    if ((long)trailer_off + 4 > filesize) { store_free(store); return NULL; }
    size_t index_len = (size_t)(filesize - (long)trailer_off);
    uint8_t *index = (uint8_t *)malloc(index_len);
    if (!index || fseek(f, (long)trailer_off, SEEK_SET) != 0 || fread(index, 1, index_len, f) != index_len) {
        free(index);
        store_free(store);
        return NULL;
    }

    uint32_t count = read_u32le(index);
    size_t names_off = 4 + (size_t)count * 12;
    if (names_off > index_len) { free(index); store_free(store); return NULL; }
    store->entries = (PetkaEntry *)calloc(count ? count : 1, sizeof(PetkaEntry));
    store->cache = (uint8_t **)calloc(count ? count : 1, sizeof(uint8_t *));
    if (!store->entries || !store->cache) { free(index); store_free(store); return NULL; }
    store->entry_count = count;

    size_t pos = names_off;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *rec = index + 4 + (size_t)i * 12;
        store->entries[i].offset = read_u32le(rec + 4);
        store->entries[i].length = read_u32le(rec + 8);
        size_t start = pos;
        while (pos < index_len && index[pos] != 0) pos++;
        if (pos >= index_len) { free(index); store_free(store); return NULL; }
        size_t n = pos - start;
        store->entries[i].name = (char *)malloc(n + 1);
        if (!store->entries[i].name) { free(index); store_free(store); return NULL; }
        memcpy(store->entries[i].name, index + start, n);
        store->entries[i].name[n] = 0;
        pos++; // skip NUL
    }
    free(index);
    return store;
}

void petka_store_close(PetkaStore *store) {
    if (store) store_free(store);
}

const PetkaEntry *petka_store_find(const PetkaStore *store, const char *name) {
    if (!store) return NULL;
    for (uint32_t i = 0; i < store->entry_count; i++) {
        if (petka_strcasecmp(store->entries[i].name, name) == 0) return &store->entries[i];
    }
    return NULL;
}

const uint8_t *petka_store_entry_data(const PetkaStore *store, const PetkaEntry *entry, size_t *out_len) {
    if (!store || !entry || entry < store->entries || entry >= store->entries + store->entry_count) return NULL;
    if ((size_t)entry->offset + entry->length > store->size) return NULL;
    size_t i = (size_t)(entry - store->entries);
    if (!store->cache[i]) {
        // One byte more than asked for, so a zero-length entry still has a
        // buffer of its own.
        uint8_t *buf = (uint8_t *)malloc((size_t)entry->length + 1);
        FILE *f = (FILE *)store->file;
        if (!buf || fseek(f, (long)entry->offset, SEEK_SET) != 0 ||
            fread(buf, 1, entry->length, f) != entry->length) {
            free(buf);
            return NULL;
        }
        store->cache[i] = buf;
    }
    if (out_len) *out_len = entry->length;
    return store->cache[i];
}

void petka_store_entry_release(PetkaStore *store, const PetkaEntry *entry) {
    if (!store || !entry || entry < store->entries || entry >= store->entries + store->entry_count) return;
    size_t i = (size_t)(entry - store->entries);
    free(store->cache[i]);
    store->cache[i] = NULL;
}
