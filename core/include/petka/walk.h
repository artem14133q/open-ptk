// Reader + pathfinder for Petka's .CVX walk meshes (Walk.cpp in the exe).
// One mesh per background, stored next to it under the same name
// ("P-ROOM-1.BMP" -> "P-ROOM-1.CVX"): the walkable floor cut into convex
// polygons. See docs/str-format.md for the byte layout.
//
// Pure C, no platform dependencies: builds for desktop and Switch alike.
#ifndef PETKA_WALK_H
#define PETKA_WALK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t x, y;
} PetkaPoint;

typedef struct {
    uint32_t *verts; // owned, indices into points, in order around the polygon
    uint32_t count;
    float cx, cy;    // centroid
} PetkaWalkPoly;

// A diagonal shared by two polygons: the way from one into the other.
typedef struct {
    uint32_t poly_a, poly_b;
    uint32_t v0, v1; // indices into points
} PetkaWalkPortal;

typedef struct {
    PetkaPoint *points; // owned; the floor's outline, in order
    uint32_t point_count;
    PetkaWalkPoly *polys; // owned
    uint32_t poly_count;
    PetkaWalkPortal *portals; // owned
    uint32_t portal_count;
} PetkaWalkMesh;

// Parses a whole .CVX buffer. Returns NULL on failure, including "doesn't
// consume the file exactly" -- the format has no other integrity check.
PetkaWalkMesh *petka_walkmesh_open(const uint8_t *data, size_t len);
void petka_walkmesh_close(PetkaWalkMesh *mesh);

// Index of the polygon containing the point, or -1 if it is off the floor.
int petka_walkmesh_locate(const PetkaWalkMesh *mesh, float x, float y);

// Moves a point onto the floor if it is not there already (to the nearest
// point of the nearest polygon). Returns that polygon's index.
int petka_walkmesh_snap(const PetkaWalkMesh *mesh, float x, float y, float *out_x, float *out_y);

// Shortest way across the floor from one point to another. Both ends are
// snapped onto the floor first. Writes the corners to turn at, ending with
// the (snapped) destination, and returns how many were written. Returns 0
// if the two points are in parts of the floor that do not connect, or if
// the route needs more than `max` points.
int petka_walkmesh_path(const PetkaWalkMesh *mesh, float from_x, float from_y,
                        float to_x, float to_y, PetkaPoint *out, int max);

#ifdef __cplusplus
}
#endif

#endif
