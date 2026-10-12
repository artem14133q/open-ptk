#include "petka/walk.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---- .CVX ----

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    int error;
} Reader;

static uint32_t rd_u32(Reader *r) {
    if (r->error || r->len - r->pos < 4) { r->error = 1; return 0; }
    const uint8_t *p = r->data + r->pos;
    r->pos += 4;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Guards the allocations against counts read from a corrupt file.
static int count_ok(Reader *r, uint32_t count, size_t record) {
    if (r->error || (r->len - r->pos) / record < count) { r->error = 1; return 0; }
    return 1;
}

static void add_vertex(PetkaWalkPoly *poly, uint32_t v) {
    for (uint32_t i = 0; i < poly->count; i++)
        if (poly->verts[i] == v) return;
    poly->verts[poly->count++] = v;
}

// Sorts a convex polygon's corners by angle around its centroid. The file
// lists a polygon as a mix of corners and shared diagonals, not as a ring.
static void order_polygon(const PetkaWalkMesh *mesh, PetkaWalkPoly *poly) {
    float cx = 0, cy = 0;
    for (uint32_t i = 0; i < poly->count; i++) {
        cx += (float)mesh->points[poly->verts[i]].x;
        cy += (float)mesh->points[poly->verts[i]].y;
    }
    poly->cx = cx / (float)poly->count;
    poly->cy = cy / (float)poly->count;
    for (uint32_t i = 1; i < poly->count; i++) {
        uint32_t v = poly->verts[i];
        float a = atan2f((float)mesh->points[v].y - poly->cy, (float)mesh->points[v].x - poly->cx);
        uint32_t j = i;
        for (; j > 0; j--) {
            uint32_t w = poly->verts[j - 1];
            if (atan2f((float)mesh->points[w].y - poly->cy, (float)mesh->points[w].x - poly->cx) <= a) break;
            poly->verts[j] = w;
        }
        poly->verts[j] = v;
    }
}

PetkaWalkMesh *petka_walkmesh_open(const uint8_t *data, size_t len) {
    Reader r = { data, len, 0, 0 };
    PetkaWalkMesh *mesh = (PetkaWalkMesh *)calloc(1, sizeof(PetkaWalkMesh));
    uint32_t *edges = NULL, *counts = NULL, *owner = NULL;

    // Pool A: the outline's corners.
    uint32_t point_count = rd_u32(&r);
    if (!count_ok(&r, point_count, 8) || point_count < 3) goto fail;
    mesh->points = (PetkaPoint *)calloc(point_count, sizeof(PetkaPoint));
    mesh->point_count = point_count;
    for (uint32_t i = 0; i < point_count; i++) {
        mesh->points[i].x = (int32_t)rd_u32(&r);
        mesh->points[i].y = (int32_t)rd_u32(&r);
    }

    // Pool B: edges as corner index pairs. The first point_count of them
    // are the outline itself; the rest are the diagonals cutting it up.
    uint32_t edge_count = rd_u32(&r);
    if (!count_ok(&r, edge_count, 8)) goto fail;
    edges = (uint32_t *)calloc(edge_count ? edge_count : 1, 2 * sizeof(uint32_t));
    for (uint32_t i = 0; i < edge_count; i++) {
        edges[2 * i] = rd_u32(&r);
        edges[2 * i + 1] = rd_u32(&r);
        if (edges[2 * i] >= point_count || edges[2 * i + 1] >= point_count) goto fail;
    }

    uint32_t poly_count = rd_u32(&r);
    if (!count_ok(&r, poly_count, 4) || poly_count == 0) goto fail;
    counts = (uint32_t *)calloc(poly_count, sizeof(uint32_t));
    for (uint32_t i = 0; i < poly_count; i++) counts[i] = rd_u32(&r);

    mesh->polys = (PetkaWalkPoly *)calloc(poly_count, sizeof(PetkaWalkPoly));
    mesh->poly_count = poly_count;
    mesh->portals = (PetkaWalkPortal *)calloc(edge_count ? edge_count : 1, sizeof(PetkaWalkPortal));
    // owner[e] = first polygon seen using edge e, +1 (0 = none yet).
    owner = (uint32_t *)calloc(edge_count ? edge_count : 1, sizeof(uint32_t));

    for (uint32_t i = 0; i < poly_count; i++) {
        PetkaWalkPoly *poly = &mesh->polys[i];
        if (!count_ok(&r, counts[i], 4)) goto fail;
        // Each entry adds at most two corners.
        poly->verts = (uint32_t *)calloc(counts[i] ? 2 * (size_t)counts[i] : 1, sizeof(uint32_t));
        for (uint32_t k = 0; k < counts[i]; k++) {
            // One index space for both pools: below point_count it is a
            // corner, otherwise it is an edge of pool B -- a diagonal this
            // polygon shares with exactly one neighbour.
            uint32_t idx = rd_u32(&r);
            if (r.error) goto fail;
            if (idx < point_count) { add_vertex(poly, idx); continue; }
            if (idx >= edge_count) goto fail;
            add_vertex(poly, edges[2 * idx]);
            add_vertex(poly, edges[2 * idx + 1]);
            if (!owner[idx]) {
                owner[idx] = i + 1;
            } else if (owner[idx] != i + 1) {
                PetkaWalkPortal *portal = &mesh->portals[mesh->portal_count++];
                portal->poly_a = owner[idx] - 1;
                portal->poly_b = i;
                portal->v0 = edges[2 * idx];
                portal->v1 = edges[2 * idx + 1];
            }
        }
        if (poly->count < 3) goto fail;
        order_polygon(mesh, poly);
    }
    if (r.error || r.pos != r.len) goto fail;

    free(edges);
    free(counts);
    free(owner);
    return mesh;

fail:
    free(edges);
    free(counts);
    free(owner);
    petka_walkmesh_close(mesh);
    return NULL;
}

void petka_walkmesh_close(PetkaWalkMesh *mesh) {
    if (!mesh) return;
    for (uint32_t i = 0; i < mesh->poly_count; i++) free(mesh->polys[i].verts);
    free(mesh->polys);
    free(mesh->portals);
    free(mesh->points);
    free(mesh);
}

// ---- geometry ----

static float cross(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

static int poly_contains(const PetkaWalkMesh *mesh, const PetkaWalkPoly *poly, float x, float y,
                         float slack) {
    int pos = 0, neg = 0;
    for (uint32_t i = 0; i < poly->count; i++) {
        const PetkaPoint *a = &mesh->points[poly->verts[i]];
        const PetkaPoint *b = &mesh->points[poly->verts[(i + 1) % poly->count]];
        float ex = (float)(b->x - a->x), ey = (float)(b->y - a->y);
        float len = sqrtf(ex * ex + ey * ey);
        if (len <= 0) continue;
        // Signed distance from the edge's line.
        float d = cross(ex, ey, x - (float)a->x, y - (float)a->y) / len;
        if (d > slack) pos = 1;
        if (d < -slack) neg = 1;
    }
    return !(pos && neg);
}

int petka_walkmesh_locate(const PetkaWalkMesh *mesh, float x, float y) {
    // The slack only makes a point on an edge count as inside. It must stay
    // tiny: "within s of every edge line" is not "within s of the polygon"
    // -- at a sharp corner that region reaches far outside.
    for (uint32_t i = 0; i < mesh->poly_count; i++)
        if (poly_contains(mesh, &mesh->polys[i], x, y, 0.01f)) return (int)i;
    return -1;
}

int petka_walkmesh_snap(const PetkaWalkMesh *mesh, float x, float y, float *out_x, float *out_y) {
    int inside = petka_walkmesh_locate(mesh, x, y);
    if (inside >= 0) { *out_x = x; *out_y = y; return inside; }

    float best = FLT_MAX;
    int best_poly = 0;
    *out_x = x;
    *out_y = y;
    for (uint32_t i = 0; i < mesh->poly_count; i++) {
        const PetkaWalkPoly *poly = &mesh->polys[i];
        for (uint32_t k = 0; k < poly->count; k++) {
            const PetkaPoint *a = &mesh->points[poly->verts[k]];
            const PetkaPoint *b = &mesh->points[poly->verts[(k + 1) % poly->count]];
            float ex = (float)(b->x - a->x), ey = (float)(b->y - a->y);
            float len2 = ex * ex + ey * ey;
            float t = len2 > 0 ? ((x - (float)a->x) * ex + (y - (float)a->y) * ey) / len2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float px = (float)a->x + ex * t, py = (float)a->y + ey * t;
            float d = (px - x) * (px - x) + (py - y) * (py - y);
            if (d < best) {
                best = d;
                best_poly = (int)i;
                // Nudge towards the centroid so the point is safely inside
                // rather than exactly on the outline.
                float nx = poly->cx - px, ny = poly->cy - py;
                float n = sqrtf(nx * nx + ny * ny);
                *out_x = n > 0 ? px + nx / n * 0.5f : px;
                *out_y = n > 0 ? py + ny / n * 0.5f : py;
            }
        }
    }
    return best_poly;
}

// ---- pathfinding ----

// Chain of polygons from `from` to `to` by Dijkstra over centroid
// distances. (The exe searches depth-first, refusing only to step straight
// back; the polygons of a cut-up outline form a tree, so there is one chain
// and both find it.) Fills via[p] with the portal used to enter p (-1 for the
// start). Returns 0 if `to` cannot be reached.
static int find_corridor(const PetkaWalkMesh *mesh, int from, int to, int *via) {
    uint32_t n = mesh->poly_count;
    float *dist = (float *)malloc(n * sizeof(float));
    uint8_t *done = (uint8_t *)calloc(n, 1);
    for (uint32_t i = 0; i < n; i++) { dist[i] = FLT_MAX; via[i] = -1; }
    dist[from] = 0;
    int found = 0;
    for (;;) {
        int cur = -1;
        for (uint32_t i = 0; i < n; i++)
            if (!done[i] && dist[i] < FLT_MAX && (cur < 0 || dist[i] < dist[cur])) cur = (int)i;
        if (cur < 0) break;
        if (cur == to) { found = 1; break; }
        done[cur] = 1;
        for (uint32_t k = 0; k < mesh->portal_count; k++) {
            const PetkaWalkPortal *portal = &mesh->portals[k];
            int next;
            if ((int)portal->poly_a == cur) next = (int)portal->poly_b;
            else if ((int)portal->poly_b == cur) next = (int)portal->poly_a;
            else continue;
            float dx = mesh->polys[next].cx - mesh->polys[cur].cx;
            float dy = mesh->polys[next].cy - mesh->polys[cur].cy;
            float d = dist[cur] + sqrtf(dx * dx + dy * dy);
            if (d < dist[next]) { dist[next] = d; via[next] = (int)k; }
        }
    }
    free(dist);
    free(done);
    return found;
}

static float orient(float ax, float ay, float bx, float by, float cx, float cy) {
    return cross(bx - ax, by - ay, cx - ax, cy - ay);
}

// Do the closed segments a-b and c-d have a point in common?
static int segments_meet(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
    float o1 = orient(ax, ay, bx, by, cx, cy), o2 = orient(ax, ay, bx, by, dx, dy);
    float o3 = orient(cx, cy, dx, dy, ax, ay), o4 = orient(cx, cy, dx, dy, bx, by);
    if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0))) return 1;
    // Touching or collinear: a point of one lying on the other.
    #define ON(o, px, py, qx, qy, rx, ry) \
        ((o) == 0 && (rx) >= fminf(px, qx) && (rx) <= fmaxf(px, qx) && (ry) >= fminf(py, qy) && (ry) <= fmaxf(py, qy))
    int touch = ON(o1, ax, ay, bx, by, cx, cy) || ON(o2, ax, ay, bx, by, dx, dy) ||
                ON(o3, cx, cy, dx, dy, ax, ay) || ON(o4, cx, cy, dx, dy, bx, by);
    #undef ON
    return touch;
}

// Does the straight line from a to b leave the floor? It does if it meets
// the outline anywhere. (Our stand-in for the exe's sub_424E40, which was
// not decompiled; touching the outline counts as leaving, the safe side.)
static int blocked(const PetkaWalkMesh *mesh, float ax, float ay, float bx, float by) {
    for (uint32_t i = 0; i < mesh->point_count; i++) {
        const PetkaPoint *c = &mesh->points[i], *d = &mesh->points[(i + 1) % mesh->point_count];
        if (segments_meet(ax, ay, bx, by, (float)c->x, (float)c->y, (float)d->x, (float)d->y)) return 1;
    }
    return 0;
}

// The route the exe builds (sub_423650): through the middle of every
// diagonal between the start's polygon and the goal's, then straightened by
// dropping a point whenever its two neighbours can see each other.
int petka_walkmesh_path(const PetkaWalkMesh *mesh, float from_x, float from_y,
                        float to_x, float to_y, PetkaPoint *out, int max) {
    float start[2], goal[2];
    int from = petka_walkmesh_snap(mesh, from_x, from_y, &start[0], &start[1]);
    int to = petka_walkmesh_snap(mesh, to_x, to_y, &goal[0], &goal[1]);
    if (max < 1) return 0;
    PetkaPoint goal_pt = { (int32_t)lroundf(goal[0]), (int32_t)lroundf(goal[1]) };
    if (from == to) { out[0] = goal_pt; return 1; }

    int *via = (int *)malloc(mesh->poly_count * sizeof(int));
    if (!find_corridor(mesh, from, to, via)) { free(via); return 0; }
    uint32_t steps = 0;
    for (int p = to; via[p] >= 0; steps++) {
        const PetkaWalkPortal *portal = &mesh->portals[via[p]];
        p = (int)portal->poly_a == p ? (int)portal->poly_b : (int)portal->poly_a;
    }

    // Start, the diagonals' midpoints in walking order, goal.
    uint32_t total = steps + 2;
    float *pts = (float *)malloc(total * 2 * sizeof(float));
    pts[0] = start[0];
    pts[1] = start[1];
    uint32_t at = steps;
    for (int p = to; via[p] >= 0; at--) {
        const PetkaWalkPortal *portal = &mesh->portals[via[p]];
        const PetkaPoint *u = &mesh->points[portal->v0], *v = &mesh->points[portal->v1];
        pts[2 * at] = (float)((u->x + v->x) >> 1);
        pts[2 * at + 1] = (float)((u->y + v->y) >> 1);
        p = (int)portal->poly_a == p ? (int)portal->poly_b : (int)portal->poly_a;
    }
    pts[2 * (total - 1)] = goal[0];
    pts[2 * (total - 1) + 1] = goal[1];
    free(via);

    // Straighten in place: n points are kept so far.
    uint32_t n = 1;
    for (uint32_t i = 1; i < total; i++) {
        pts[2 * n] = pts[2 * i];
        pts[2 * n + 1] = pts[2 * i + 1];
        n++;
        if (n >= 3 && !blocked(mesh, pts[2 * (n - 3)], pts[2 * (n - 3) + 1], pts[2 * (n - 1)], pts[2 * (n - 1) + 1])) {
            pts[2 * (n - 2)] = pts[2 * (n - 1)];
            pts[2 * (n - 2) + 1] = pts[2 * (n - 1) + 1];
            n--;
        }
    }

    int count = 0;
    if ((int)(n - 1) <= max) {
        for (uint32_t i = 1; i < n; i++) {
            out[count].x = (int32_t)lroundf(pts[2 * i]);
            out[count].y = (int32_t)lroundf(pts[2 * i + 1]);
            count++;
        }
    }
    free(pts);
    return count;
}

// ---- gait ----

static int32_t rd_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

// "xyof" then pairs of i32. Returns the number of pairs, 0 if malformed.
static int load_xyof(const uint8_t *data, size_t len, int32_t **out_x, int32_t **out_y) {
    if (!data || len < 12 || memcmp(data, "xyof", 4) != 0 || (len - 4) % 8 != 0) return 0;
    int n = (int)((len - 4) / 8);
    *out_x = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    *out_y = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) {
        (*out_x)[i] = rd_i32(data + 4 + 8 * i);
        (*out_y)[i] = rd_i32(data + 8 + 8 * i);
    }
    return n;
}

PetkaGait *petka_gait_open(const uint8_t *const off[PETKA_WALK_DIRS], const size_t off_len[PETKA_WALK_DIRS],
                           const uint8_t *const leg[PETKA_WALK_DIRS], const size_t leg_len[PETKA_WALK_DIRS]) {
    PetkaGait *gait = (PetkaGait *)calloc(1, sizeof(PetkaGait));
    for (int d = 0; d < PETKA_WALK_DIRS; d++) {
        int32_t *off_x = NULL, *off_y = NULL;
        int n_off = load_xyof(off[d], off_len[d], &off_x, &off_y);
        int n_leg = load_xyof(leg[d], leg_len[d], &gait->leg_x[d], &gait->leg_y[d]);
        int n = n_off - 1; // the last pair repeats the first
        if (n < 1 || n_leg != n_off) {
            free(off_x);
            free(off_y);
            petka_gait_close(gait);
            return NULL;
        }
        gait->frames[d] = n;
        gait->step_x[d] = (int32_t *)malloc((size_t)(n + 1) * sizeof(int32_t));
        gait->step_y[d] = (int32_t *)malloc((size_t)(n + 1) * sizeof(int32_t));
        // The sprite's own move plus the foothold's move inside the sprite.
        for (int i = 1; i <= n; i++) {
            gait->step_x[d][i] = gait->leg_x[d][i] + off_x[i] - gait->leg_x[d][i - 1];
            gait->step_y[d][i] = gait->leg_y[d][i] + off_y[i] - gait->leg_y[d][i - 1];
        }
        gait->step_x[d][0] = gait->step_x[d][n];
        gait->step_y[d][0] = gait->step_y[d][n];
        free(off_x);
        free(off_y);

        // Where the clip takes the hero by itself, summed over 150 steps as
        // the exe does (a whole number of cycles would do as well).
        long sum_x = 0, sum_y = 0;
        for (int k = 0, i = 1; k < 150; k++, i = (i + 1) % n) {
            sum_x += gait->step_x[d][i];
            sum_y += gait->step_y[d][i];
        }
        gait->slope[d] = (double)sum_y / (double)sum_x; // +-inf for a vertical clip, as in the exe
    }
    return gait;
}

void petka_gait_close(PetkaGait *gait) {
    if (!gait) return;
    for (int d = 0; d < PETKA_WALK_DIRS; d++) {
        free(gait->leg_x[d]);
        free(gait->leg_y[d]);
        free(gait->step_x[d]);
        free(gait->step_y[d]);
    }
    free(gait);
}

#define PI 3.14159265358979323846

// Angle of each clip, counter-clockwise from east with y pointing up -- the
// table the exe builds at start-up (0x4E1F18).
static const double kDirAngle[PETKA_WALK_DIRS] = {
    PI / 2, PI / 4, 0, -PI / 4, -PI / 2, -3 * PI / 4, PI, 3 * PI / 4,
};

// Sets up the leg from a to b: picks the clip, the turn and the number of
// steps (sub_423B80). Returns 0 for a leg of no length.
static int walker_leg(PetkaWalker *w, PetkaPoint a, PetkaPoint b) {
    const PetkaGait *gait = w->gait;
    double dx = (double)(b.x - a.x), dy = (double)(b.y - a.y);
    if (dx == 0.0 && dy == 0.0) return 0;

    // Nearest clip by direction. The second form of the angle catches the
    // wrap-around at due west.
    double angle = atan2(-dy, dx);
    double other = angle >= 0.0 ? 2 * PI - angle : angle + 2 * PI;
    double best = 4.0;
    for (int d = 0; d < PETKA_WALK_DIRS; d++) {
        if (fabs(angle - kDirAngle[d]) < best) { best = fabs(angle - kDirAngle[d]); w->dir = d; }
        if (fabs(other - kDirAngle[d]) < best) { best = fabs(other - kDirAngle[d]); w->dir = d; }
    }

    // Turn the clip's own line of motion onto the leg. The exe measures the
    // angle against the point where that line meets the leg's end column,
    // then folds it into +-90 degrees; the line has no direction, so this
    // comes to the same thing.
    double turn = atan2(dy, dx) - atan(gait->slope[w->dir]);
    while (turn > PI / 2) turn -= PI;
    while (turn < -PI / 2) turn += PI;
    w->cos_a = cos(turn);
    w->sin_a = sin(turn);

    // Dry run: step until ten steps in a row bring the hero no nearer to
    // the end, and keep the step count of the nearest approach.
    int n = gait->frames[w->dir];
    double acc_x = 0, acc_y = 0, y = (double)a.y;
    double nearest = dx * dx + dy * dy + 1.0;
    int steps = 0, worse = 0, frame = 1 % n;
    w->steps_left = 0;
    while (worse < 10) {
        double s = w->scale ? w->scale(w->user, (double)(long long)y) : 1.0;
        acc_x += (double)gait->step_x[w->dir][frame] * s;
        acc_y += (double)gait->step_y[w->dir][frame] * s;
        steps++;
        frame = (frame + 1) % n;
        y = acc_y * w->cos_a + acc_x * w->sin_a + (double)a.y;
        double ex = acc_x * w->cos_a - acc_y * w->sin_a + (double)a.x - (double)b.x;
        double ey = y - (double)b.y;
        double d2 = ex * ex + ey * ey;
        if (d2 >= nearest) {
            worse++;
        } else {
            nearest = d2;
            w->steps_left = steps;
            worse = 0;
        }
    }

    w->acc_x = w->acc_y = 0;
    w->x = w->start_x = (double)a.x;
    w->y = w->start_y = (double)a.y;
    w->frame = 0;
    return 1;
}

int petka_walker_start(PetkaWalker *w, const PetkaGait *gait, PetkaPoint from,
                       const PetkaPoint *points, int count, PetkaScaleFn scale, void *user) {
    memset(w, 0, sizeof(*w));
    w->gait = gait;
    w->scale = scale;
    w->user = user;
    // Drop legs of no length, which the leg set-up cannot take.
    w->route[w->count++] = from;
    for (int i = 0; i < count && w->count < PETKA_WALK_MAX_ROUTE; i++) {
        PetkaPoint prev = w->route[w->count - 1];
        if (points[i].x != prev.x || points[i].y != prev.y) w->route[w->count++] = points[i];
    }
    if (w->count < 2) return 0;
    w->next = 1;
    return walker_leg(w, w->route[0], w->route[1]);
}

// sub_424030.
int petka_walker_step(PetkaWalker *w) {
    const PetkaGait *gait = w->gait;
    int dir = w->dir;
    w->frame = (w->frame + 1) % gait->frames[dir];
    if (--w->steps_left < 0) {
        // This leg is done: on to the next point, or arrived.
        if (++w->next >= w->count) {
            w->x = (double)w->route[w->count - 1].x;
            w->y = (double)w->route[w->count - 1].y;
            return 0;
        }
        int frame = w->frame;
        walker_leg(w, w->route[w->next - 1], w->route[w->next]);
        if (w->dir != dir) return 2;
        w->frame = frame; // same clip: it just carries on
        return 1;
    }
    double s = w->scale ? w->scale(w->user, (double)(long long)w->y) : 1.0;
    w->acc_x += (double)gait->step_x[dir][w->frame] * s;
    w->acc_y += (double)gait->step_y[dir][w->frame] * s;
    w->x = w->acc_x * w->cos_a - w->acc_y * w->sin_a + w->start_x;
    w->y = w->acc_y * w->cos_a + w->acc_x * w->sin_a + w->start_y;
    return 1;
}
