// highlight.c
//
// EN: The yellow outline round the thing under the pointer, if it can be clicked.
// RU: Жёлтая обводка вокруг предмета под указателем, если с ним можно взаимодействовать.
#include "game.h"

#include "rlgl.h"

// ---- the shape's outline as curves ----
//
// The clip's opaque pixels, grown by GROW pixels all round (so the line
// lies just outside the thing) and with their holes filled, make one or
// more solid shapes; the edge of each is followed along the pixels' sides
// into a loop of corners, the steps are taken out of it (Ramer-Douglas-Peucker, SIMPLIFY pixels), and what
// is left is rounded (smooth_loops) and drawn as a closed band. All
// in the clip's frame pixels; made again only when the frame changes.
#define GROW 2
// Before that the shape is closed: grown by CLOSE and shrunk back by
// CLOSE - GROW, which fills its notches and the gaps between twigs and
// rounds it, while a thin thing (a stick, a rope) keeps its outline.
#define CLOSE 5
#define MARGIN (CLOSE + 2)
#define SIMPLIFY 1.6f
#define MIN_AREA 40 // pixels: specks are no shapes

typedef struct {
    const Visual *v;
    uint16_t resource;
    int frame;
    PetkaMskRect rect;
    Vector2 *pts;   // the loops one after another, frame pixels
    int *starts;    // where each loop begins in pts; starts[loops] = the end
    int loops;
} Outline;

static Outline s_outline;

static void outline_free(void) {
    free(s_outline.pts);
    free(s_outline.starts);
    memset(&s_outline, 0, sizeof(s_outline));
}

void highlight_init(void) {}

void highlight_close(void) { outline_free(); }

// Ramer-Douglas-Peucker over pts[a..b]: keep[] marks the points kept.
static void simplify(const Vector2 *pts, int a, int b, char *keep) {
    if (b <= a + 1) return;
    float dx = pts[b].x - pts[a].x, dy = pts[b].y - pts[a].y, len = sqrtf(dx * dx + dy * dy);
    float worst = -1;
    int at = -1;
    for (int i = a + 1; i < b; i++) {
        float ex = pts[i].x - pts[a].x, ey = pts[i].y - pts[a].y;
        float d = len > 0 ? fabsf(ex * dy - ey * dx) / len : sqrtf(ex * ex + ey * ey);
        if (d > worst) { worst = d; at = i; }
    }
    if (worst <= SIMPLIFY) return;
    keep[at] = 1;
    simplify(pts, a, at, keep);
    simplify(pts, at, b, keep);
}

// The corners made round: each loop cut into pieces of at most SEGMENT
// pixels, then its corners cut off CHAIKIN times (Chaikin: each piece gives
// the points a quarter and three quarters along it). The curve stays inside
// the corners it rounds -- no overshoot -- and on a long straight side
// only its ends are rounded, by a few pixels.
#define SEGMENT 6.0f
#define CHAIKIN 3

static void smooth_loops(void) {
    int total = 0;
    for (int l = 0; l < s_outline.loops; l++) {
        int count = s_outline.starts[l + 1] - s_outline.starts[l];
        const Vector2 *p = s_outline.pts + s_outline.starts[l];
        int pieces = 0;
        for (int i = 0; i < count; i++) {
            Vector2 a = p[i], b = p[(i + 1) % count];
            pieces += 1 + (int)(sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)) / SEGMENT);
        }
        total += pieces << CHAIKIN;
    }
    Vector2 *out = (Vector2 *)malloc((size_t)(total + 1) * sizeof(Vector2));
    Vector2 *work = (Vector2 *)malloc((size_t)(total + 1) * sizeof(Vector2));
    int *starts = (int *)malloc((size_t)(s_outline.loops + 1) * sizeof(int));
    int n = 0;
    for (int l = 0; l < s_outline.loops; l++) {
        int count = s_outline.starts[l + 1] - s_outline.starts[l];
        const Vector2 *p = s_outline.pts + s_outline.starts[l];
        Vector2 *cur = out + n, *other = work + n;
        int m = 0;
        for (int i = 0; i < count; i++) {
            Vector2 a = p[i], b = p[(i + 1) % count];
            int k = 1 + (int)(sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)) / SEGMENT);
            for (int j = 0; j < k; j++)
                cur[m++] = (Vector2){a.x + (b.x - a.x) * j / k, a.y + (b.y - a.y) * j / k};
        }
        for (int it = 0; it < CHAIKIN; it++) {
            for (int i = 0; i < m; i++) {
                Vector2 a = cur[i], b = cur[(i + 1) % m];
                other[2 * i] = (Vector2){a.x * 0.75f + b.x * 0.25f, a.y * 0.75f + b.y * 0.25f};
                other[2 * i + 1] = (Vector2){a.x * 0.25f + b.x * 0.75f, a.y * 0.25f + b.y * 0.75f};
            }
            m *= 2;
            Vector2 *t = cur; cur = other; other = t;
        }
        // CHAIKIN is odd: the last pass wrote to work; back to out.
        if (cur != out + n) memcpy(out + n, cur, (size_t)m * sizeof(Vector2));
        starts[l] = n;
        n += m;
    }
    starts[s_outline.loops] = n;
    free(work);
    free(s_outline.pts);
    free(s_outline.starts);
    s_outline.pts = out;
    s_outline.starts = starts;
}

// One pass of `morph`: along rows (step 1, lines w apart) or columns (step
// w, lines 1 apart), each pixel set if any (grow) or all (shrink) of the r
// either side of it are; past the edge counts as clear.
static void morph_pass(const uint8_t *src, uint8_t *dst, int len, int lines, int step, int line_step, int r,
                       int shrink) {
    for (int l = 0; l < lines; l++) {
        const uint8_t *in = src + (size_t)l * line_step;
        uint8_t *out = dst + (size_t)l * line_step;
        int count = 0;
        for (int i = 0; i < r && i < len; i++) count += in[(size_t)i * step];
        for (int i = 0; i < len; i++) {
            if (i + r < len) count += in[(size_t)(i + r) * step];
            if (i - r - 1 >= 0) count -= in[(size_t)(i - r - 1) * step];
            out[(size_t)i * step] = shrink ? count == 2 * r + 1 : count > 0;
        }
    }
}

// Grows (shrink 0) or shrinks (1) the shape src by a square of r into dst.
static void morph(const uint8_t *src, uint8_t *dst, uint8_t *tmp, int w, int h, int r, int shrink) {
    morph_pass(src, tmp, w, h, 1, w, r, shrink);
    morph_pass(tmp, dst, h, w, w, 1, r, shrink);
}

static void outline_make(const Visual *v) {
    outline_free();
    s_outline.v = v;
    s_outline.resource = v->resource;
    s_outline.frame = v->flic->frame_index;
    s_outline.rect = v->rect;
    const PetkaFlic *f = v->flic;
    PetkaMskRect r = v->rect;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > f->width) r.right = f->width;
    if (r.bottom > f->height) r.bottom = f->height;
    if (r.right <= r.left || r.bottom <= r.top) return;
    const int w = r.right - r.left + 2 * MARGIN, h = r.bottom - r.top + 2 * MARGIN, ox = r.left - MARGIN,
              oy = r.top - MARGIN;
    uint8_t *opaque = (uint8_t *)calloc((size_t)w * h, 1);
    uint8_t *solid = (uint8_t *)calloc((size_t)w * h, 1);
    int *queue = (int *)malloc((size_t)w * h * sizeof(int));
    for (int y = r.top; y < r.bottom; y++)
        for (int x = r.left; x < r.right; x++)
            opaque[(y - oy) * w + (x - ox)] = !petka_flic_keyed(f, f->indices[y * f->width + x]);
    // Grown by CLOSE, then shrunk by CLOSE - GROW: a square each time, in
    // two passes (rows, then columns) of a running count, so that a big
    // clip -- a character, whose frame changes as he talks -- costs no more
    // per pixel than a small one.
    uint8_t *tmp = (uint8_t *)malloc((size_t)w * h);
    morph(opaque, solid, tmp, w, h, CLOSE, 0);
    morph(solid, opaque, tmp, w, h, CLOSE - GROW, 1);
    free(tmp);
    memcpy(solid, opaque, (size_t)w * h);
    // The holes filled: whatever the outside cannot reach is inside. The
    // margin's ring of clear pixels joins all of the outside up.
    uint8_t *outside = opaque; // reused
    memset(outside, 0, (size_t)w * h);
    int head = 0, tail = 0;
    queue[tail++] = 0;
    outside[0] = 1;
    while (head < tail) {
        int i = queue[head++], x = i % w, y = i / w;
        static const int kDx4[4] = {1, -1, 0, 0}, kDy4[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + kDx4[k], ny = y + kDy4[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            int n = ny * w + nx;
            if (outside[n] || solid[n]) continue;
            outside[n] = 1;
            queue[tail++] = n;
        }
    }
    for (int i = 0; i < w * h; i++) solid[i] = !outside[i];
    // The edges between solid and clear pixels, each going clockwise round
    // the solid (y down): along a pixel's top to the right, down its right
    // side, along its bottom to the left, up its left side. From each
    // corner of the grid go at most two of them (where two shapes touch
    // corner to corner).
    const int vw = w + 1, vh = h + 1;
    int8_t *out = (int8_t *)malloc((size_t)vw * vh * 2);
    memset(out, -1, (size_t)vw * vh * 2);
    static const int kEx[4] = {1, 0, -1, 0}, kEy[4] = {0, 1, 0, -1}; // right, down, left, up
#define SOLID(x, y) ((x) >= 0 && (y) >= 0 && (x) < w && (y) < h && solid[(y) * w + (x)])
#define ADD(vx, vy, d)                                                              \
    do {                                                                             \
        int8_t *o = out + ((size_t)(vy) * vw + (vx)) * 2;                            \
        o[o[0] < 0 ? 0 : 1] = (int8_t)(d);                                           \
    } while (0)
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!solid[y * w + x]) continue;
            if (!SOLID(x, y - 1)) ADD(x, y, 0);
            if (!SOLID(x + 1, y)) ADD(x + 1, y, 1);
            if (!SOLID(x, y + 1)) ADD(x + 1, y + 1, 2);
            if (!SOLID(x - 1, y)) ADD(x, y + 1, 3);
        }
#undef ADD
#undef SOLID
    // Followed into loops: at a corner with two ways on, the right turn,
    // which keeps to the solid. Each loop's corners are its points.
    int cap = 1024, n = 0, loops_cap = 8, loops = 0, edge_cap = 1024;
    Vector2 *pts = (Vector2 *)malloc((size_t)cap * sizeof(Vector2));
    int *starts = (int *)malloc((size_t)(loops_cap + 1) * sizeof(int));
    Vector2 *edge = (Vector2 *)malloc((size_t)(edge_cap + 1) * sizeof(Vector2));
    char *keep = NULL;
    for (int v0 = 0; v0 < vw * vh; v0++)
        while (out[v0 * 2] >= 0 || out[v0 * 2 + 1] >= 0) {
            int vx = v0 % vw, vy = v0 / vw, dir = out[v0 * 2] >= 0 ? out[v0 * 2] : out[v0 * 2 + 1];
            int count = 0;
            double area2 = 0;
            for (;;) {
                int8_t *o = out + ((size_t)vy * vw + vx) * 2;
                // Take the way: the right turn if there are two.
                int pick = o[0] == dir ? 0 : o[1] == dir ? 1 : -1;
                if (pick < 0) {
                    int right = (dir + 1) % 4;
                    pick = o[0] == right ? 0 : o[1] == right ? 1 : o[0] >= 0 ? 0 : o[1] >= 0 ? 1 : -1;
                }
                if (pick < 0) break; // back at the start
                int d = o[pick];
                o[pick] = -1;
                if (count == edge_cap) {
                    edge_cap *= 2;
                    edge = (Vector2 *)realloc(edge, (size_t)(edge_cap + 1) * sizeof(Vector2));
                }
                // A point where the way turns.
                if (count == 0 || d != dir) edge[count++] = (Vector2){(float)(vx + ox), (float)(vy + oy)};
                int nx = vx + kEx[d], ny = vy + kEy[d];
                area2 += (double)vx * ny - (double)nx * vy;
                vx = nx;
                vy = ny;
                dir = d;
            }
            // Clockwise on screen is a positive area here; specks are dropped.
            if (area2 / 2 < MIN_AREA || count < 4) continue;
            // The steps out: simplified as a loop, split at the start and
            // at its farthest point.
            keep = (char *)realloc(keep, (size_t)count + 1);
            memset(keep, 0, (size_t)count + 1);
            int far = 0;
            float best = -1;
            for (int i = 1; i < count; i++) {
                float dx = edge[i].x - edge[0].x, dy = edge[i].y - edge[0].y;
                if (dx * dx + dy * dy > best) { best = dx * dx + dy * dy; far = i; }
            }
            keep[0] = keep[far] = 1;
            edge[count] = edge[0];
            simplify(edge, 0, far, keep);
            simplify(edge, far, count, keep);
            int kept = 0;
            for (int i = 0; i < count; i++) kept += keep[i];
            if (kept < 3) continue;
            while (n + kept > cap) { cap *= 2; pts = (Vector2 *)realloc(pts, (size_t)cap * sizeof(Vector2)); }
            if (loops == loops_cap) { loops_cap *= 2; starts = (int *)realloc(starts, (size_t)(loops_cap + 1) * sizeof(int)); }
            starts[loops++] = n;
            for (int i = 0; i < count; i++) if (keep[i]) pts[n++] = edge[i];
        }
    free(out);
    starts[loops] = n;
    s_outline.pts = pts;
    s_outline.starts = starts;
    s_outline.loops = loops;
    smooth_loops();
    free(keep);
    free(edge);
    free(queue);
    free(solid);
    free(opaque);
}

// A way out: walking to it takes the heroes to another room -- a WALK
// reaction with a GOTO in it.
static int is_exit(const PetkaObject *obj) {
    for (uint32_t r = 0; r < obj->reaction_count; r++) {
        const PetkaReaction *re = &obj->reactions[r];
        if (re->opcode != PETKA_OP_WALK) continue;
        for (uint32_t m = 0; m < re->message_count; m++)
            if (re->messages[m].opcode == PETKA_OP_GOTO) return 1;
    }
    return 0;
}

int highlight_wanted(const Game *g, const PetkaObject *obj) {
    // Not the heroes, whom the player leads; the other characters are.
    if (is_hero(obj->id) || is_exit(obj)) return 0;
    // Seen or not: a hidden one that can be clicked is a thing painted on
    // the background, and its clip is that thing cut out of it.
    return g->vm->state[obj_index(g, obj)].active;
}

// One loop as a closed band THICK wide on the screen, centred on it: at
// each point the band's edges are pushed out along the mean of the two
// pieces' normals (a mitre), so the pieces meet without gaps or overlaps.
static void draw_loop(const Vector2 *p, int count, Rectangle frame, float sx, float sy, float thick, Color ink) {
    static Vector2 *in, *out;
    static int cap;
    if (count > cap) {
        cap = count > 2 * cap ? count : 2 * cap;
        in = (Vector2 *)realloc(in, (size_t)cap * sizeof(Vector2));
        out = (Vector2 *)realloc(out, (size_t)cap * sizeof(Vector2));
    }
    float half = thick / 2;
    for (int i = 0; i < count; i++) {
        Vector2 a = p[(i + count - 1) % count], b = p[i], c = p[(i + 1) % count];
        a = (Vector2){frame.x + a.x * sx, frame.y + a.y * sy};
        b = (Vector2){frame.x + b.x * sx, frame.y + b.y * sy};
        c = (Vector2){frame.x + c.x * sx, frame.y + c.y * sy};
        float d1x = b.x - a.x, d1y = b.y - a.y, d2x = c.x - b.x, d2y = c.y - b.y;
        float l1 = sqrtf(d1x * d1x + d1y * d1y), l2 = sqrtf(d2x * d2x + d2y * d2y);
        Vector2 n1 = l1 > 0 ? (Vector2){-d1y / l1, d1x / l1} : (Vector2){0, 0};
        Vector2 n2 = l2 > 0 ? (Vector2){-d2y / l2, d2x / l2} : n1;
        if (l1 <= 0) n1 = n2;
        Vector2 m = {n1.x + n2.x, n1.y + n2.y};
        float ml = sqrtf(m.x * m.x + m.y * m.y);
        m = ml > 0 ? (Vector2){m.x / ml, m.y / ml} : n1;
        float cosine = m.x * n1.x + m.y * n1.y, len = cosine > 0.5f ? half / cosine : 2 * half;
        out[i] = (Vector2){b.x + m.x * len, b.y + m.y * len};
        in[i] = (Vector2){b.x - m.x * len, b.y - m.y * len};
    }
    rlDisableBackfaceCulling();
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        rlCheckRenderBatchLimit(6);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(ink.r, ink.g, ink.b, ink.a);
        rlVertex2f(out[i].x, out[i].y);
        rlVertex2f(in[i].x, in[i].y);
        rlVertex2f(in[j].x, in[j].y);
        rlVertex2f(out[i].x, out[i].y);
        rlVertex2f(in[j].x, in[j].y);
        rlVertex2f(out[j].x, out[j].y);
        rlEnd();
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void highlight_draw(const Visual *v, Rectangle frame) {
    if (!v->flic || !v->flic->indices) return;
    if (s_outline.v != v || s_outline.resource != v->resource || s_outline.frame != v->flic->frame_index ||
        memcmp(&s_outline.rect, &v->rect, sizeof(v->rect)) != 0)
        outline_make(v);
    float sx = frame.width / v->flic->width, sy = frame.height / v->flic->height;
    // A soft glow under a thin bright line.
    for (int pass = 0; pass < 2; pass++) {
        float thick = (pass == 0 ? 4.0f : 1.6f) * sx;
        Color ink = pass == 0 ? (Color){255, 220, 26, 70} : (Color){255, 220, 26, 245};
        for (int l = 0; l < s_outline.loops; l++)
            draw_loop(s_outline.pts + s_outline.starts[l], s_outline.starts[l + 1] - s_outline.starts[l], frame, sx, sy,
                      thick, ink);
    }
}
