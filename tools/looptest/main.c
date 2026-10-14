// Host-only dev tool: for every multi-frame .FLC in the given stores, check
// that the second loop reproduces the first, frame for frame and pixel for
// pixel. A decoder that wraps around wrongly shows the right frames once
// and then drifts, which nothing else notices.
#include "petka/flic.h"
#include "petka/store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash(PetkaFlic *f) {
    uint32_t h = 2166136261u;
    size_t n = (size_t)f->width * (size_t)f->height;
    for (size_t i = 0; i < n; i++) { h ^= f->indices[i]; h *= 16777619u; }
    const uint8_t *rgb = petka_flic_rgb(f);
    for (size_t i = 0; i < n * 3; i += 7) { h ^= rgb[i]; h *= 16777619u; }
    return h;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <store.str>...\n", argv[0]);
        return 1;
    }
    long clips = 0, looping = 0, bad_clips = 0, bad_frames = 0, errors = 0, no_ring = 0;
    for (int a = 1; a < argc; a++) {
        PetkaStore *store = petka_store_open(argv[a]);
        if (!store) { printf("open failed: %s\n", argv[a]); return 1; }
        for (uint32_t e = 0; e < store->entry_count; e++) {
            const char *name = store->entries[e].name;
            size_t n = strlen(name);
            if (n < 4 || strcmp(name + n - 4, ".FLC") != 0) continue;
            size_t len = 0;
            const uint8_t *bytes = petka_store_entry_data(store, &store->entries[e], &len);
            PetkaFlic *f = petka_flic_open(bytes, len);
            if (!f) { errors++; continue; }
            clips++;
            if (f->frame_count > 1) {
                looping++;
                uint32_t *first = (uint32_t *)malloc((size_t)f->frame_count * sizeof(uint32_t));
                int failed = 0;
                for (int i = 0; i < f->frame_count; i++) {
                    first[i] = hash(f);
                    if (i == f->frame_count - 1) {
                        // is a ring frame there to be played?
                        size_t cur = f->cursor;
                        if (cur + 6 > len || bytes[cur + 4] != 0xFA || bytes[cur + 5] != 0xF1) no_ring++;
                    }
                    if (petka_flic_advance(f) != 0) { failed = -1; break; }
                }
                int mismatches = 0;
                for (int i = 0; failed == 0 && i < f->frame_count; i++) {
                    if (f->frame_index != i || hash(f) != first[i]) mismatches++;
                    if (petka_flic_advance(f) != 0) { failed = -1; break; }
                }
                if (failed) errors++;
                if (mismatches) {
                    bad_clips++;
                    bad_frames += mismatches;
                    if (bad_clips <= 5) printf("  second loop differs: %s (%d of %d frames)\n", name, mismatches, f->frame_count);
                }
                free(first);
            }
            petka_flic_close(f);
        }
        petka_store_close(store);
    }
    printf("clips: %ld, with more than one frame: %ld, without a ring frame: %ld\n", clips, looping, no_ring);
    printf("second loop differs from the first in %ld clips (%ld frames); decode errors: %ld\n", bad_clips, bad_frames, errors);
    return bad_clips || errors;
}
