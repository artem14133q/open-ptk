// inventory.c
//
// EN: The case (the inventory): its things, buttons, lid and pages.
// RU: Чемодан (инвентарь): предметы, кнопки, крышка и страницы.
#include "game.h"
const int kCaseCell[CASE_PAGE][2] = {{120, 145}, {240, 145}, {360, 145}, {100, 220}, {240, 220}, {380, 220}};

const PetkaObject *case_item(Game *g, int k, Visual **out) {
    uint32_t i = (uint32_t)(g->case_first + k);
    if (i >= g->vm->inventory_count) return NULL;
    const PetkaObject *obj = petka_script_find(g->script, g->vm->inventory[i]);
    if (!obj) return NULL;
    const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    if (!st->visible || st->resource == PETKA_NONE) return NULL;
    Visual *v = &g->visuals[obj_index(g, obj)];
    if (v->resource != st->resource) visual_load(g, v, st->resource);
    if (!v->flic) return NULL;
    *out = v;
    return obj;
}

// What the pointer (screen pixels of the game) is over while the case is
// open. The things come first -- they are above the case in z; the case
// itself takes every other point of the screen (its hit test is just "am I
// shown") and lights a button (its hover, 0x415550): "quit" anywhere
// outside the case, else the button whose rect holds the pointer.
const PetkaObject *case_hover(Game *g, int px, int py) {
    for (int k = CASE_PAGE - 1; k >= 0; k--) {
        Visual *v = NULL;
        const PetkaObject *obj = case_item(g, k, &v);
        if (obj && g->vm->state[obj_index(g, obj)].active && clip_hit(v, px - kCaseCell[k][0], py - kCaseCell[k][1]))
            return obj;
    }
    g->case_lit = -1;
    clip_show(g, &g->case_exit, CASE_EXIT, 0);
    const PetkaFlic *mask = g->case_exit.flic;
    if (mask && px >= 0 && py >= 0 && px < mask->width && py < mask->height &&
        !petka_flic_keyed(mask, mask->indices[py * mask->width + px])) {
        g->case_lit = 3;
        return NULL;
    }
    for (int i = 0; i < 6; i++) {
        Visual *b = &g->case_button[i];
        if (b->resource == PETKA_NONE) visual_load(g, b, (uint16_t)(CASE_BUTTON0 + i));
        if (!b->flic || px < b->rect.left || py < b->rect.top || px >= b->rect.right || py >= b->rect.bottom) continue;
        // Chapayev's button is dead while he is not about, and the map's in
        // a room that is not on the map.
        const PetkaObjectState *chapayev = petka_scriptvm_state(g->vm, PETKA_ID_CHAPAYEV);
        if (i == 0 && !(chapayev && chapayev->visible)) break;
        if (i == 2 && !map_allowed(g)) break;
        // Picking a thing for an object, the lid's three do nothing; with a
        // controller Chapayev's never does.
        if (i <= 2 && g->case_for != PETKA_NONE) break;
        if (i == 0 && plat_pad()) break;
        g->case_lit = i;
        break;
    }
    return NULL;
}

// Puts down whatever the cursor carries: the thing is back in the case.
void drop_item(Game *g) {
    PetkaObjectState *st = petka_scriptvm_state(g->vm, g->vm->cursor_object);
    if (st) st->visible = st->active = 1;
    g->vm->cursor_object = g->vm->cursor_resource = PETKA_NONE;
}

// A click on the case with nothing of its contents under it (sub_4151B0).
void case_click(Game *g) {
    // Picking a thing for an object, the lid's three buttons do nothing.
    if (g->case_for != PETKA_NONE && g->case_lit >= 0 && g->case_lit <= 2) return;
    // A controller sends Chapayev with R: his button does nothing.
    if (plat_pad() && g->case_lit == 0) return;
    switch (g->case_lit) {
    case 0: // the "send Chapayev" action, as keys 6 and C
        drop_item(g);
        g->verb = 5;
        break;
    case 1: panel_command(g); break;
    case 2: map_command(g); break;
    case 3: g->case_open = 0; break;
    case 4:
        if ((uint32_t)(g->case_first + CASE_PAGE) < g->vm->inventory_count) g->case_first += CASE_PAGE;
        break;
    case 5:
        g->case_first = g->case_first > CASE_PAGE ? g->case_first - CASE_PAGE : 0;
        break;
    default: break;
    }
}

// The lid's pictures (Chapayev, the diskette, the map) in grey, as the
// case's body has them, for buttons that do nothing: all three (`all`) or
// Chapayev's alone, each made once, a texture over the span of the three. Their own clips will
// not do -- the lit diskette and map sit on a square of their own. A
// pixel is the picture's where it is opaque in its button's clip and,
// in each row, from the first to the last that stands out from the lid:
// the lid's colour there taken as going evenly across the span from the
// middle of a few pixels left of it to the middle of a few right of it.
int case_lid_grey(Game *g, int all) {
    Texture2D *out = &g->case_lid_grey[all ? 0 : 1];
    if (out->id) return 1;
    const Visual *body = &g->case_body;
    if (!body->flic || body->flic->width != GAME_W || body->flic->height != GAME_H) return 0;
    PetkaMskRect span = {GAME_W, GAME_H, 0, 0};
    for (int i = 0; i < 3; i++) {
        Visual *b = &g->case_button[i];
        if (b->resource == PETKA_NONE) visual_load(g, b, (uint16_t)(CASE_BUTTON0 + i));
        if (!b->flic || b->flic->width != GAME_W || b->flic->height != GAME_H) return 0;
        if (b->area.left < span.left) span.left = b->area.left;
        if (b->area.top < span.top) span.top = b->area.top;
        if (b->area.right > span.right) span.right = b->area.right;
        if (b->area.bottom > span.bottom) span.bottom = b->area.bottom;
    }
    enum { SIDE = 8, STANDS_OUT = 60 };
    if (span.left < SIDE || span.right > GAME_W - SIDE || span.right <= span.left || span.bottom <= span.top) return 0;
    const uint8_t *frame = flic_to_rgba(body->flic, (PetkaMskRect){0, 0, GAME_W, GAME_H});
    if (!frame) return 0;
    int w = span.right - span.left, h = span.bottom - span.top;
    uint8_t *rgb = (uint8_t *)malloc((size_t)GAME_W * GAME_H * 4);
    uint8_t *px = (uint8_t *)calloc((size_t)w * h, 4);
    if (!rgb || !px) { free(rgb); free(px); return 0; }
    memcpy(rgb, frame, (size_t)GAME_W * GAME_H * 4);
    for (int y = span.top; y < span.bottom; y++) {
        const uint8_t *row = rgb + (size_t)y * GAME_W * 4;
        float lid[2][3];
        for (int e = 0; e < 2; e++) {
            int x0 = e == 0 ? span.left - SIDE : span.right, order[SIDE];
            for (int k = 0; k < SIDE; k++) order[k] = x0 + k;
            for (int m = 1; m < SIDE; m++)
                for (int k = m; k > 0; k--) {
                    const uint8_t *p = row + order[k] * 4, *q = row + order[k - 1] * 4;
                    if (p[0] + p[1] + p[2] >= q[0] + q[1] + q[2]) break;
                    int t = order[k]; order[k] = order[k - 1]; order[k - 1] = t;
                }
            for (int c = 0; c < 3; c++) lid[e][c] = row[order[SIDE / 2] * 4 + c];
        }
        for (int i = 0; i < (all ? 3 : 1); i++) {
            const Visual *b = &g->case_button[i];
            if (y < b->area.top || y >= b->area.bottom) continue;
            const uint8_t *mask = b->flic->indices + (size_t)y * GAME_W;
            int first = -1, last = -1;
            for (int x = b->area.left; x < b->area.right; x++) {
                if (petka_flic_keyed(b->flic, mask[x])) continue;
                float t = (float)(x - span.left + 1) / (w + 1), d = 0;
                for (int c = 0; c < 3; c++) d += fabsf(row[x * 4 + c] - (lid[0][c] * (1 - t) + lid[1][c] * t));
                if (d <= STANDS_OUT) continue;
                if (first < 0) first = x;
                last = x;
            }
            for (int x = first; first >= 0 && x <= last; x++) {
                if (petka_flic_keyed(b->flic, mask[x])) continue;
                const uint8_t *c = row + x * 4;
                uint8_t *o = px + ((size_t)(y - span.top) * w + (x - span.left)) * 4;
                o[0] = o[1] = o[2] = (uint8_t)((c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8);
                o[3] = 255;
            }
        }
    }
    Image img = {.data = px, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    *out = LoadTextureFromImage(img);
    g->case_lid_span = span;
    free(px);
    free(rgb);
    return out->id != 0;
}

void draw_case(Game *g, float scale) {
    Rectangle screen = {0, 0, GAME_W * scale, GAME_H * scale};
    clip_show(g, &g->case_body, CASE_BODY, 0);
    if (g->case_body.flic)
        visual_draw(&g->case_body, screen);
    // Picking a thing for an object, the lid's three pictures are grey;
    // with a controller Chapayev's is, always.
    int all = g->case_for != PETKA_NONE;
    if ((all || plat_pad()) && case_lid_grey(g, all)) {
        Visual lid = g->case_body;
        lid.tex = g->case_lid_grey[all ? 0 : 1];
        lid.area = g->case_lid_span;
        visual_draw(&lid, screen);
    }
    // The lit button's clip goes over the case, inside its own rect; the
    // others are put back on their first frame.
    for (int i = 0; i < 6; i++) {
        Visual *b = &g->case_button[i];
        clip_show(g, b, (uint16_t)(CASE_BUTTON0 + i), i == g->case_lit);
        if (i != g->case_lit || !b->flic) continue;
        visual_draw(b, screen);
    }
    for (int k = 0; k < CASE_PAGE; k++) {
        Visual *v = NULL;
        if (!case_item(g, k, &v)) continue;
        visual_draw(v, (Rectangle){kCaseCell[k][0] * scale, kCaseCell[k][1] * scale, v->flic->width * scale,
                                   v->flic->height * scale});
    }
    // "Полоска в чемодане", the strap, lies across the things.
    clip_show(g, &g->case_strip, CASE_STRAP, 0);
    if (g->case_strip.flic)
        visual_draw(&g->case_strip, screen);
}
