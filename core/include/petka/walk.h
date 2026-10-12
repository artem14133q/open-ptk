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

// ---- gait: how a hero moves along a route ----
//
// A hero has eight walk clips, one per direction (N, NE, E, SE, S, SW, W,
// NW). Next to each lie an .OFF and a .LEG file: per frame, how far the
// sprite moves and where in the sprite the hero's foothold is. Their sum is
// how far the foothold travels on that frame; the hero's position *is* the
// foothold. A leg of the route rarely points exactly along a clip, so the
// clip nearest in direction is played and its motion is turned by the
// difference. Reimplements Walk.cpp's sub_422D30, sub_423B80 and sub_424030.

#define PETKA_WALK_DIRS 8

typedef struct {
    int frames[PETKA_WALK_DIRS];        // frames in each direction's clip
    // All owned, frames[d] + 1 entries; entry i belongs to clip frame i.
    int32_t *leg_x[PETKA_WALK_DIRS], *leg_y[PETKA_WALK_DIRS];   // foothold inside the sprite
    int32_t *step_x[PETKA_WALK_DIRS], *step_y[PETKA_WALK_DIRS]; // foothold travel on reaching frame i
    double slope[PETKA_WALK_DIRS];      // dy/dx of the clip's own motion
} PetkaGait;

// off[d] / leg[d] are the two files of direction d ("xyof", then frames + 1
// pairs of i32). Returns NULL if any of the sixteen is missing or malformed.
PetkaGait *petka_gait_open(const uint8_t *const off[PETKA_WALK_DIRS], const size_t off_len[PETKA_WALK_DIRS],
                           const uint8_t *const leg[PETKA_WALK_DIRS], const size_t leg_len[PETKA_WALK_DIRS]);
void petka_gait_close(PetkaGait *gait);

// Hero scale at a screen row; steps are multiplied by it.
typedef double (*PetkaScaleFn)(void *user, double y);

#define PETKA_WALK_MAX_ROUTE 34

typedef struct {
    const PetkaGait *gait;
    PetkaScaleFn scale;
    void *user;

    PetkaPoint route[PETKA_WALK_MAX_ROUTE]; // route[0] is where the walk starts
    int count, next;                        // next = point being walked towards

    int dir;        // clip in use, 0..7
    int frame;      // frame of that clip the hero is on
    int steps_left; // steps before the current leg is over
    double cos_a, sin_a;     // turn applied to the clip's motion
    double start_x, start_y; // where the current leg began
    double acc_x, acc_y;     // foothold travel so far on this leg, before the turn
    double x, y;             // the hero's position
} PetkaWalker;

// Sets a walk up from `from` through `points`. Returns 0 if there is nowhere
// to go (no points, or all of them where the hero already stands).
int petka_walker_start(PetkaWalker *w, const PetkaGait *gait, PetkaPoint from,
                       const PetkaPoint *points, int count, PetkaScaleFn scale, void *user);

// One step, to be taken each time the walk clip moves on a frame. Returns
// 0 when the hero has arrived (x, y are then exactly the last point),
// 1 after an ordinary step, 2 when the direction -- and so the clip --
// changed; the new clip starts at its first frame.
int petka_walker_step(PetkaWalker *w);

#ifdef __cplusplus
}
#endif

#endif
