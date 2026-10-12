// Host-only dev tool: load a hero's walk step tables (.OFF/.LEG, usually
// from MAIN.STR) and walk thousands of random legs, checking that the hero
// ends up next to where each leg was meant to end. A wrong sign or a swapped
// table in the gait code sends him off in another direction instead.
#include "petka/store.h"
#include "petka/walk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *kDirs[8] = {"W-N", "W-NE", "W-E", "W-SE", "W-S", "W-SW", "W-W", "W-NW"};

static double scale_fn(void *user, double y) { (void)y; return *(double *)user; }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <store.str> [clip prefix, default MAIN\\WALK\\P-]\n", argv[0]);
        return 1;
    }
    PetkaStore *store = petka_store_open(argv[1]);
    if (!store) { fprintf(stderr, "open failed\n"); return 1; }
    const char *prefix = argc > 2 ? argv[2] : "MAIN\\WALK\\P-";
    int failed = 0;
    const uint8_t *off[8], *leg[8];
    size_t off_len[8], leg_len[8];
    for (int d = 0; d < 8; d++) {
        char name[128];
        snprintf(name, sizeof(name), "%s%s.OFF", prefix, kDirs[d]);
        const PetkaEntry *e = petka_store_find(store, name);
        off[d] = e ? petka_store_entry_data(store, e, &off_len[d]) : NULL;
        snprintf(name, sizeof(name), "%s%s.LEG", prefix, kDirs[d]);
        e = petka_store_find(store, name);
        leg[d] = e ? petka_store_entry_data(store, e, &leg_len[d]) : NULL;
    }
    PetkaGait *gait = petka_gait_open(off, off_len, leg, leg_len);
    if (!gait) { printf("gait load failed\n"); return 1; }
    for (int d = 0; d < 8; d++) {
        long sx = 0, sy = 0;
        for (int i = 0; i < gait->frames[d]; i++) { sx += gait->step_x[d][i]; sy += gait->step_y[d][i]; }
        printf("%-5s frames %2d  one cycle moves (%4ld, %4ld)  slope %8.3f\n", kDirs[d], gait->frames[d], sx, sy, gait->slope[d]);
    }
    srand(7);
    for (int pass = 0; pass < 2; pass++) {
        double scale = pass ? 0.4 : 1.0;
        double worst = 0, sum = 0;
        int dirs[8] = {0}, n = 4000, steps_total = 0;
        for (int k = 0; k < n; k++) {
            PetkaPoint a = {rand() % 640, rand() % 480}, b = {rand() % 640, rand() % 480};
            double len = hypot(b.x - a.x, b.y - a.y);
            if (len < 60) { k--; continue; }
            PetkaWalker w;
            if (!petka_walker_start(&w, gait, a, &b, 1, scale_fn, &scale)) { printf("start failed\n"); return 1; }
            dirs[w.dir]++;
            // where the leg ends before the snap onto the last point
            double lx = w.x, ly = w.y;
            int r, steps = 0;
            while ((r = petka_walker_step(&w)) != 0 && steps < 5000) { lx = w.x; ly = w.y; steps++; }
            double miss = hypot(lx - b.x, ly - b.y);
            if (miss > worst) worst = miss;
            sum += miss;
            steps_total += steps;
        }
        printf("scale %.1f: %d legs, off the end point by %.1f px on average, %.1f at worst; %.1f steps per leg; clips used:",
               scale, n, sum / n, worst, (double)steps_total / n);
        for (int d = 0; d < 8; d++) printf(" %d", dirs[d]);
        printf("\n");
        // A leg ends within a step of its target; the longest step is
        // about 30 pixels at full size.
        if (worst > 30.0 * scale) failed = 1;
    }
    petka_gait_close(gait);
    petka_store_close(store);
    return failed;
}
