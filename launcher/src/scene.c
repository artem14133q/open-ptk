// scene.c
//
// EN: The screen's objects: their draw order (the exe's z sort) and what lies under a point.
// RU: Объекты экрана: порядок отрисовки (сортировка по z как в exe) и что находится под точкой.
#include "game.h"

// The screen's objects in draw order, kept from frame to frame as the exe
// keeps its list (QSystem+0x210): filled in the room's order, then the
// heroes, and sorted again every frame by z ascending with the exe's own
// selection sort (sub_422580) -- for each place, the first object after it
// with a strictly lower z than the lowest so far is swapped in. Objects of
// equal z (hidden hot spots are often all at -1) keep the order that
// gives; the C library's qsort put them in an order of its own, different
// on the Switch than on a Mac -- in part 3's hall the window, big and
// hidden, came over the way out in front of it on one and not the other.
void z_order_sort(Game *g) {
    uint16_t key = screen_room(g);
    int heroes = g->seq_room == PETKA_NONE;
    if (key != g->z_order_room || heroes != g->z_order_heroes) {
        g->z_order_n = 0;
        const PetkaObject *room = petka_script_find(g->script, key);
        for (uint32_t i = 0; room && i < room->attached_count; i++)
            if (petka_script_find(g->script, room->attached[i].obj_id))
                g->z_order[g->z_order_n++] = room->attached[i].obj_id;
        for (uint16_t id = 0; id < 2 && heroes; id++) {
            int listed = 0;
            for (int k = 0; k < g->z_order_n && !listed; k++) listed = g->z_order[k] == id;
            if (!listed && petka_script_find(g->script, id)) g->z_order[g->z_order_n++] = id;
        }
        g->z_order_room = key;
        g->z_order_heroes = heroes;
    }
    int n = g->z_order_n;
    for (int i = 0; i < n; i++) {
        int lowest = i;
        int32_t lowest_z = petka_scriptvm_state(g->vm, g->z_order[i])->z;
        for (int j = i + 1; j < n; j++) {
            int32_t z = petka_scriptvm_state(g->vm, g->z_order[j])->z;
            if (z < lowest_z) { lowest = j; lowest_z = z; }
        }
        uint16_t t = g->z_order[i];
        g->z_order[i] = g->z_order[lowest];
        g->z_order[lowest] = t;
    }
}

// Collects what is on screen right now, back to front: by z ascending, as
// the exe sorts each frame.
int build_scene(Game *g, Drawable *out, int max) {
    // The exe's z update first: an animated, visible object with auto_z
    // gets a new z every frame; otherwise z stays whatever it was last given.
    const PetkaObject *room = petka_script_find(g->script, screen_room(g));
    uint32_t extra = g->seq_room != PETKA_NONE ? 0 : 2;
    for (uint32_t i = 0; room && i < room->attached_count + extra; i++) {
        const PetkaObject *obj = i < room->attached_count
            ? petka_script_find(g->script, room->attached[i].obj_id)
            : petka_script_find(g->script, (uint16_t)(i - room->attached_count));
        if (!obj) continue;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        PetkaObjectState *state = &g->vm->state[obj_index(g, obj)];
        if (!v->live || !state->auto_z || !state->animate || !state->visible) continue;
        if (is_hero(obj->id)) {
            // A hero (sub_409530): the bottom edge of his scaled frame
            // when standing, which is where the launcher keeps him; the
            // walk's own point, cut to whole pixels (sub_424210), when
            // walking.
            const Walk *walk = &g->walk[obj->id];
            state->z = walk->active ? (int32_t)walk->walker.y : state->y;
        } else {
            // A scene object (sub_401810): the lowest edge of its current
            // frame's MSK rects (its sprite sits at y 0), never less than 1.
            int bottom = petka_msk_frame_bottom(v->msk, v->msk_len, v->flic->frame_count, v->flic->frame_index);
            bottom += state->y;
            state->z = bottom > 1 ? bottom : 1;
        }
    }
    z_order_sort(g);
    // The heroes walk the game's room; a sequence shows only its own objects.
    int n = 0;
    for (int k = 0; k < g->z_order_n && n < max; k++) {
        const PetkaObject *obj = petka_script_find(g->script, g->z_order[k]);
        if (!obj || !g->visuals[obj_index(g, obj)].live) continue;
        out[n].obj = obj;
        out[n].z = g->vm->state[obj_index(g, obj)].z;
        n++;
    }
    return n;
}

Rectangle visual_dest(const Game *g, const PetkaObject *obj) {
    const Visual *v = &g->visuals[obj_index(g, obj)];
    const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    // A hero's clip is a sprite put by his feet; any other object's goes
    // with its corner at the object's position -- 0,0 for everything in a
    // room, whose clips are full frames, but not for the panel's buttons.
    if (is_hero(obj->id)) return hero_dest(g, obj->id, st, v);
    return (Rectangle){(float)st->x, (float)st->y, (float)v->flic->width, (float)v->flic->height};
}

// Topmost active object under (x, y): the exe walks the z order from the top
// and stops at the first object whose own hit test says yes.
const PetkaObject *object_at(Game *g, const Drawable *scene, int n, float x, float y) {
    for (int i = n - 1; i >= 0; i--) {
        const PetkaObject *obj = scene[i].obj;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        if (!g->vm->state[obj_index(g, obj)].active) continue;
        if (!is_hero(obj->id)) {
            const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
            if (clip_hit(v, (int)x - st->x, (int)y - st->y)) return obj;
            continue;
        }
        // A hero (sub_4086B0): the pixel itself, on his scaled sprite.
        Rectangle d = visual_dest(g, obj);
        if (d.width <= 0 || x < d.x || y < d.y || x >= d.x + d.width || y >= d.y + d.height) continue;
        int px = (int)((x - d.x) * v->flic->width / d.width);
        int py = (int)((y - d.y) * v->flic->height / d.height);
        if (px < 0 || py < 0 || px >= v->flic->width || py >= v->flic->height) continue;
        if (!petka_flic_keyed(v->flic, v->flic->indices[py * v->flic->width + px])) return obj;
    }
    return NULL;
}

// The object under a point on the map or the panel. Those screens do not
// go by z: their mouse functions walk the screen's own list from its end,
// which is the room's list of objects backwards, and take the first whose
// hit test says yes -- so the panel's dial marks, listed last, come before
// the pictures they lie on.
const PetkaObject *listed_object_at(Game *g, float x, float y) {
    const PetkaObject *room = petka_script_find(g->script, g->seq_room);
    for (uint32_t i = room ? room->attached_count : 0; i-- > 0;) {
        const PetkaObject *obj = petka_script_find(g->script, room->attached[i].obj_id);
        if (!obj) continue;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
        if (!st->active || !v->live || !v->flic) continue;
        if (clip_hit(v, (int)x - st->x, (int)y - st->y)) return obj;
    }
    return NULL;
}
