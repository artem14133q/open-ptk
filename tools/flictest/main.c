// Host-only dev tool: decode a .FLC entry from a store and dump N frames
// as .ppm files for visual inspection, before wiring the decoder into the
// raylib launcher.
#include "petka/flic.h"
#include "petka/store.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <store.str> <entry_name> <out_dir> [num_frames]\n", argv[0]);
        return 1;
    }
    const char *store_path = argv[1];
    const char *entry_name = argv[2];
    const char *out_dir = argv[3];
    int num_frames = argc > 4 ? atoi(argv[4]) : 5;

    PetkaStore *store = petka_store_open(store_path);
    if (!store) { fprintf(stderr, "open failed: %s\n", store_path); return 1; }

    const PetkaEntry *entry = petka_store_find(store, entry_name);
    if (!entry) { fprintf(stderr, "entry not found: %s\n", entry_name); return 1; }

    size_t len = 0;
    const uint8_t *bytes = petka_store_entry_data(store, entry, &len);

    PetkaFlic *flic = petka_flic_open(bytes, len);
    if (!flic) { fprintf(stderr, "flic open failed\n"); return 1; }

    printf("w=%d h=%d frames=%d speed_ms=%d oframe1=%u oframe2=%u\n",
           flic->width, flic->height, flic->frame_count, flic->speed_ms,
           flic->oframe1, flic->oframe2);

    for (int i = 0; i < num_frames; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame%03d.ppm", out_dir, i);
        FILE *f = fopen(path, "wb");
        if (!f) { perror("fopen"); break; }
        fprintf(f, "P6\n%d %d\n255\n", flic->width, flic->height);
        fwrite(flic->rgb, 1, (size_t)flic->width * flic->height * 3, f);
        fclose(f);
        printf("wrote %s (frame_index=%d)\n", path, flic->frame_index);
        if (petka_flic_advance(flic) != 0) { fprintf(stderr, "advance failed at %d\n", i); break; }
    }

    petka_flic_close(flic);
    petka_store_close(store);
    return 0;
}
