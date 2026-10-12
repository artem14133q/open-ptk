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

static void store_free_entries(PetkaStore *store) {
    if (!store->entries) return;
    for (uint32_t i = 0; i < store->entry_count; i++) free(store->entries[i].name);
    free(store->entries);
    store->entries = NULL;
    store->entry_count = 0;
}

PetkaStore *petka_store_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long filesize = ftell(f);
    if (filesize < 8) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc((size_t)filesize);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)filesize, f) != (size_t)filesize) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    if (memcmp(data, "StOR", 4) != 0) {
        free(data);
        return NULL;
    }

    uint32_t trailer_off = read_u32le(data + 4);
    if ((long)trailer_off + 4 > filesize) {
        free(data);
        return NULL;
    }

    uint32_t count = read_u32le(data + trailer_off);
    long records_off = (long)trailer_off + 4;
    long names_off = records_off + (long)count * 12;
    if (names_off > filesize) {
        free(data);
        return NULL;
    }

    PetkaEntry *entries = (PetkaEntry *)calloc(count, sizeof(PetkaEntry));
    if (!entries) {
        free(data);
        return NULL;
    }

    long pos = names_off;
    for (uint32_t i = 0; i < count; i++) {
        long rec = records_off + (long)i * 12;
        entries[i].offset = read_u32le(data + rec + 4);
        entries[i].length = read_u32le(data + rec + 8);

        long start = pos;
        while (pos < filesize && data[pos] != 0) pos++;
        if (pos >= filesize) {
            for (uint32_t j = 0; j <= i; j++) free(entries[j].name);
            free(entries);
            free(data);
            return NULL;
        }
        size_t n = (size_t)(pos - start);
        entries[i].name = (char *)malloc(n + 1);
        if (!entries[i].name) {
            for (uint32_t j = 0; j <= i; j++) free(entries[j].name);
            free(entries);
            free(data);
            return NULL;
        }
        memcpy(entries[i].name, data + start, n);
        entries[i].name[n] = 0;
        pos++; // skip NUL
    }

    PetkaStore *store = (PetkaStore *)malloc(sizeof(PetkaStore));
    if (!store) {
        for (uint32_t j = 0; j < count; j++) free(entries[j].name);
        free(entries);
        free(data);
        return NULL;
    }
    store->data = data;
    store->size = (size_t)filesize;
    store->entries = entries;
    store->entry_count = count;
    return store;
}

void petka_store_close(PetkaStore *store) {
    if (!store) return;
    store_free_entries(store);
    free(store->data);
    free(store);
}

const PetkaEntry *petka_store_find(const PetkaStore *store, const char *name) {
    if (!store) return NULL;
    for (uint32_t i = 0; i < store->entry_count; i++) {
        if (petka_strcasecmp(store->entries[i].name, name) == 0) return &store->entries[i];
    }
    return NULL;
}

const uint8_t *petka_store_entry_data(const PetkaStore *store, const PetkaEntry *entry, size_t *out_len) {
    if (!store || !entry) return NULL;
    if ((size_t)entry->offset + entry->length > store->size) return NULL;
    if (out_len) *out_len = entry->length;
    return store->data + entry->offset;
}
