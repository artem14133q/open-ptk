// Host-only dev tool: parse every .CVX walk mesh in a store (usually
// FLCn.STR) and stress the pathfinder on random point pairs, checking that
// a route is always found and never leaves the floor.
#include "petka/store.h"
#include "petka/walk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng_state = 12345;
static float rnd(float lo, float hi) {
    rng_state = rng_state * 1103515245u + 12345u;
    return lo + (hi - lo) * (float)((rng_state >> 8) & 0xFFFF) / 65535.0f;
}

// The floor's polygons are convex, so a segment leaves the floor only if
// some point along it does; sample it densely.
static int segment_on_floor(const PetkaWalkMesh *mesh, float ax, float ay, float bx, float by) {
    float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    int n = (int)len + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n, x = ax + (bx - ax) * t, y = ay + (by - ay) * t, sx, sy;
        petka_walkmesh_snap(mesh, x, y, &sx, &sy);
        // Three pixels of slack: waypoints are rounded to whole pixels, and
        // a few shipped polygons are concave by a pixel or two (OKNO.CVX,
        // VREMUNKY.CVX), so a straight line across one can clip its notch.
        if ((sx - x) * (sx - x) + (sy - y) * (sy - y) > 9.0f) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <store.str> [pairs_per_mesh]\n", argv[0]);
        return 1;
    }
    int pairs = argc > 2 ? atoi(argv[2]) : 2000;
    PetkaStore *store = petka_store_open(argv[1]);
    if (!store) { fprintf(stderr, "open failed\n"); return 1; }

    int meshes = 0, bad_parse = 0;
    long routes = 0, no_route = 0, off_floor = 0, corners = 0, max_corners = 0;
    for (uint32_t e = 0; e < store->entry_count; e++) {
        const char *name = store->entries[e].name;
        size_t n = strlen(name);
        if (n < 4 || strcmp(name + n - 4, ".CVX") != 0) continue;
        size_t len = 0;
        const uint8_t *bytes = petka_store_entry_data(store, &store->entries[e], &len);
        PetkaWalkMesh *mesh = petka_walkmesh_open(bytes, len);
        if (!mesh) { printf("!! parse failed: %s\n", name); bad_parse++; continue; }
        meshes++;

        int32_t x0 = mesh->points[0].x, x1 = x0, y0 = mesh->points[0].y, y1 = y0;
        for (uint32_t i = 0; i < mesh->point_count; i++) {
            if (mesh->points[i].x < x0) x0 = mesh->points[i].x;
            if (mesh->points[i].x > x1) x1 = mesh->points[i].x;
            if (mesh->points[i].y < y0) y0 = mesh->points[i].y;
            if (mesh->points[i].y > y1) y1 = mesh->points[i].y;
        }
        long mesh_bad = 0;
        for (int k = 0; k < pairs; k++) {
            // Points come from a box somewhat larger than the floor, so
            // off-floor clicks get exercised too.
            float ax = rnd((float)x0 - 30, (float)x1 + 30), ay = rnd((float)y0 - 30, (float)y1 + 30);
            float bx = rnd((float)x0 - 30, (float)x1 + 30), by = rnd((float)y0 - 30, (float)y1 + 30);
            PetkaPoint path[64];
            int count = petka_walkmesh_path(mesh, ax, ay, bx, by, path, 64);
            routes++;
            if (count == 0) { no_route++; mesh_bad++; continue; }
            corners += count;
            if (count > max_corners) max_corners = count;
            float px, py;
            petka_walkmesh_snap(mesh, ax, ay, &px, &py);
            for (int i = 0; i < count; i++) {
                if (!segment_on_floor(mesh, px, py, (float)path[i].x, (float)path[i].y)) {
                    off_floor++;
                    mesh_bad++;
                    break;
                }
                px = (float)path[i].x;
                py = (float)path[i].y;
            }
        }
        printf("%-44s points %3u polys %3u portals %3u  %s\n", name, mesh->point_count,
               mesh->poly_count, mesh->portal_count, mesh_bad ? "!! bad routes" : "ok");
        petka_walkmesh_close(mesh);
    }
    printf("\nmeshes: %d (parse failures: %d)\n", meshes, bad_parse);
    printf("routes: %ld  none found: %ld  left the floor: %ld  avg corners: %.2f  max: %ld\n",
           routes, no_route, off_floor, routes ? (double)corners / (double)routes : 0.0, max_corners);
    petka_store_close(store);
    return bad_parse || no_route || off_floor;
}
