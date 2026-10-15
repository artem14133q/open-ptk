// heroes.c
//
// EN: Petka and Chapayev: perspective scale, walk clips and steps along the walk mesh, the camera.
// RU: Петька и Чапаев: масштаб по перспективе, клипы ходьбы и шаги по сетке ходьбы, камера.
#include "game.h"

// ---- heroes ----

// Scale of a hero standing at screen row y (the exe's sub_409460). The five
// numbers a room has in BGS.INI's [Settings] are "a b y0 y1 max": the scale
// is a at row y0 and grows by b over the rows down to y1, never below a and
// never above max. A room with no entry draws heroes at full size.
float hero_scale(const Game *g, float y) {
    if (y > GAME_H) y = GAME_H;
    float grow = g->persp_y1 != g->persp_y0
        ? (y - g->persp_y0) * g->persp_delta / (g->persp_y1 - g->persp_y0) : 0;
    if (grow < 0) grow = 0;
    float s = g->persp_base + grow;
    if (s > g->persp_max) s = g->persp_max;
    return s > 0.05f ? s : 0.05f; // ours: never let a hero vanish
}

void load_perspective(Game *g, const char *room_name) {
    g->persp_base = 1; g->persp_delta = 0; g->persp_y0 = 0; g->persp_y1 = GAME_H; g->persp_max = 1;
    char value[128];
    float a, b, c, d, e;
    if (g->bgs_ini && petka_ini_get(g->bgs_ini, g->bgs_ini_len, "Settings", room_name, value, sizeof(value)) &&
        sscanf(value, "%f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        g->persp_base = a; g->persp_delta = b; g->persp_y0 = c; g->persp_y1 = d; g->persp_max = e;
    }
}

// NAMES.INI maps an object's script name to the name shown to the player
// (many objects share one: "Большой ключ" and "IБольшой ключ" are both
// "Ключ"); CAST.INI gives a speaker's text colour as "R G B". Both have a
// single [all] section. Objects with no NAMES.INI entry are props the
// player is not told the name of.
void load_names(Game *g, const char *part) {
    size_t names_len = 0, cast_len = 0;
    const uint8_t *names = part_file(g, part, "NAMES.INI", &names_len);
    const uint8_t *cast = part_file(g, part, "CAST.INI", &cast_len);
    g->names = (char **)calloc(g->script->object_count, sizeof(char *));
    g->colors = (Color *)calloc(g->script->object_count, sizeof(Color));
    for (uint32_t i = 0; i < g->script->object_count; i++) {
        const char *key = g->script->objects[i].name;
        char value[256];
        g->colors[i] = NO_CAST_INK;
        if (names && petka_ini_get(names, names_len, "all", key, value, sizeof(value)) && value[0]) {
            g->names[i] = (char *)malloc(strlen(value) + 1);
            strcpy(g->names[i], value);
        }
        int r, gr, b;
        if (cast && petka_ini_get(cast, cast_len, "all", key, value, sizeof(value)) &&
            sscanf(value, "%d %d %d", &r, &gr, &b) == 3)
            g->colors[i] = (Color){(unsigned char)r, (unsigned char)gr, (unsigned char)b, 255};
    }
}

// Where a hero's clip goes on screen. While he walks, the frame's foothold
// (from the clip's .LEG file) sits on his position, scaled, so the feet do
// not slide -- the exe's sub_424190. Otherwise -- standing, talking, acting
// -- the clip stands on the position by the middle of its bottom edge. The
// two rules do not agree to the pixel, so he shifts slightly as a walk
// starts or ends, in the original as here.
Rectangle hero_dest(const Game *g, uint16_t hero_id, const PetkaObjectState *st, const Visual *v) {
    const Walk *w = &g->walk[hero_id];
    if (w->active) {
        const PetkaGait *gait = w->walker.gait;
        int dir = w->walker.dir, frame = v->flic->frame_index;
        if (frame > gait->frames[dir]) frame = gait->frames[dir];
        float s = hero_scale(g, (float)w->walker.y);
        return (Rectangle){(float)w->walker.x - (float)gait->leg_x[dir][frame] * s,
                           (float)w->walker.y - (float)gait->leg_y[dir][frame] * s,
                           v->flic->width * s, v->flic->height * s};
    }
    // The hero's set-position (sub_4087D0): the frame, scaled for his row,
    // stands on the position by the middle of its bottom edge.
    float s = hero_scale(g, (float)st->y);
    int width = (int)((float)v->flic->width * s), height = (int)((float)v->flic->height * s);
    return (Rectangle){(float)(st->x - width / 2), (float)(st->y - height), (float)width, (float)height};
}

// The step tables for a costume's walk clips. As in the exe's Walk
// constructor, the clips' common path is whatever precedes "W-N" in the
// path of clip costume+10, and each direction adds its name and .OFF/.LEG.
const PetkaGait *gait_for(Game *g, uint16_t costume) {
    static const char *const kDirs[PETKA_WALK_DIRS] = {"W-N", "W-NE", "W-E", "W-SE", "W-S", "W-SW", "W-W", "W-NW"};
    const char *path = costume != PETKA_NONE ? g->qrc[(uint16_t)(costume + 10)] : NULL;
    if (!path) return NULL;
    char upper[96];
    size_t n = 0;
    for (; path[n] && n + 1 < sizeof(upper); n++)
        upper[n] = path[n] >= 'a' && path[n] <= 'z' ? (char)(path[n] - 32) : path[n];
    upper[n] = 0;
    char *cut_at = strstr(upper, "W-N");
    if (!cut_at) return NULL;
    *cut_at = 0;

    for (int i = 0; i < g->gait_count; i++)
        if (strcmp(g->gaits[i].prefix, upper) == 0) return g->gaits[i].gait;
    if (g->gait_count == MAX_GAITS) return NULL;
    GaitEntry *entry = &g->gaits[g->gait_count++];
    snprintf(entry->prefix, sizeof(entry->prefix), "%s", upper);

    const uint8_t *off[PETKA_WALK_DIRS], *leg[PETKA_WALK_DIRS];
    size_t off_len[PETKA_WALK_DIRS], leg_len[PETKA_WALK_DIRS];
    for (int d = 0; d < PETKA_WALK_DIRS; d++) {
        char name[160];
        snprintf(name, sizeof(name), "%s%s.OFF", upper, kDirs[d]);
        off[d] = res_find(g, name, &off_len[d]);
        snprintf(name, sizeof(name), "%s%s.LEG", upper, kDirs[d]);
        leg[d] = res_find(g, name, &leg_len[d]);
        if (!off[d] || !leg[d]) { fprintf(stderr, "walk files not found: %s\n", name); return NULL; }
    }
    entry->gait = petka_gait_open(off, off_len, leg, leg_len);
    return entry->gait;
}

double walk_scale(void *user, double y) { return (double)hero_scale((const Game *)user, (float)y); }

// The walk mesh sits next to the background under the same name
// (P-ROOM-1.BMP -> P-ROOM-1.CVX). Called as soon as the room is known, not
// from the frame loop: a room's INITBG reaction may start a walk at once.
void load_mesh(Game *g) {
    const PetkaObjectState *room = petka_scriptvm_state(g->vm, screen_room(g));
    uint16_t resource = room ? room->resource : PETKA_NONE;
    if (resource == g->mesh_resource) return;
    petka_walkmesh_close(g->mesh);
    g->mesh = NULL;
    g->mesh_resource = resource;
    const char *path = resource != PETKA_NONE ? g->qrc[resource] : NULL;
    if (!path || !has_ext(path, ".BMP")) return;
    char cvx[512];
    swap_ext(path, "CVX", cvx, sizeof(cvx));
    size_t len = 0;
    const uint8_t *bytes = res_find(g, cvx, &len);
    if (bytes) g->mesh = petka_walkmesh_open(bytes, len);
}

int host_walk(void *user, uint16_t hero_id, int32_t x, int32_t y) {
    Game *g = (Game *)user;
    PetkaObjectState *st = petka_scriptvm_state(g->vm, hero_id);
    if (!st || hero_id > 1) return 0;
    if (!st->visible) {
        // Nobody sees him go: he is simply there.
        st->x = x;
        st->y = y;
        return 0;
    }
    Walk *w = &g->walk[hero_id];
    load_mesh(g);
    PetkaPoint route[MAX_WAYPOINTS];
    int count = g->mesh ? petka_walkmesh_path(g->mesh, (float)st->x, (float)st->y, (float)x, (float)y,
                                              route, MAX_WAYPOINTS) : 0;
    if (count == 0) {
        // No floor to follow (a cutscene screen, the map): go straight.
        route[0] = (PetkaPoint){x, y};
        count = 1;
    }
    // Measured to where the walk would really end: a click off the floor is
    // pulled onto it, possibly right back to where the hero stands.
    PetkaPoint end = route[count - 1];
    float dx = (float)(end.x - st->x), dy = (float)(end.y - st->y);
    if (dx * dx + dy * dy < 25) return 0; // the exe's own "already there" threshold

    const PetkaGait *gait = gait_for(g, st->costume);
    PetkaPoint from = {st->x, st->y};
    if (!gait || !petka_walker_start(&w->walker, gait, from, route, count, walk_scale, g)) {
        // Cannot walk it (no step tables for this costume): just be there.
        st->x = end.x;
        st->y = end.y;
        return 0;
    }
    w->active = 1;
    st->rest_after_loop = 0; // the walk clip loops, as when the exe starts a walk
    // A walking hero is animated, whatever held him still before: he takes
    // a step for each frame of the walk clip, so a frozen one never moves.
    st->animate = 1;
    if (g->trace) {
        fprintf(stderr, "walk %u: from %d,%d to %d,%d; route:", hero_id, st->x, st->y, x, y);
        for (int i = 0; i < w->walker.count; i++) fprintf(stderr, " (%d,%d)", w->walker.route[i].x, w->walker.route[i].y);
        fprintf(stderr, "; dir %d steps %d\n", w->walker.dir, w->walker.steps_left);
    }
    // Walk clips are costume+10 .. costume+17: N, NE, E, SE, S, SW, W, NW.
    st->resource = (uint16_t)(st->costume + 10 + w->walker.dir);
    // Loaded here and now: until the next frame the old clip's picture would
    // be drawn with the new clip's foothold.
    visual_load(g, &g->visuals[obj_index(g, petka_script_find(g->script, hero_id))], st->resource);
    return 1;
}

// Objects, clips, walk meshes and hero positions are all in background
// pixels; only the view moves. It follows Petka, and stays within the
// background.
void camera_update(Game *g, float dt) {
    float max_x = g->bg_tex.id ? (float)g->bg_tex.width - GAME_W : 0;
    if (max_x < 0) max_x = 0;
    if (g->seq_room != PETKA_NONE) { g->camera_x = g->camera_target = 0; return; } // sequences are one screen
    const PetkaObjectState *petka = petka_scriptvm_state(g->vm, PETKA_ID_PETKA);
    // The exe's rule (in the hero's set-position, sub_4087D0): once Petka is
    // within SCROLL_MARGIN of either side of the screen, the view is asked
    // to centre on him.
    if (petka && g->camera_snap) {
        g->camera_target = (float)petka->x - GAME_W / 2.0f;
    } else if (petka && petka->visible) {
        float on_screen = (float)petka->x - g->camera_x;
        if (on_screen < SCROLL_MARGIN || on_screen > GAME_W - SCROLL_MARGIN)
            g->camera_target = (float)petka->x - GAME_W / 2.0f;
    }
    if (g->camera_target < 0) g->camera_target = 0;
    if (g->camera_target > max_x) g->camera_target = max_x;

    float step = g->camera_snap ? max_x + GAME_W : SCROLL_SPEED * dt;
    g->camera_snap = 0;
    if (g->camera_target > g->camera_x + step) g->camera_x += step;
    else if (g->camera_target < g->camera_x - step) g->camera_x -= step;
    else g->camera_x = g->camera_target;
    if (g->camera_x > max_x) g->camera_x = max_x;
}

// Both heroes stop where they are. The exe does this on every click on an
// object, before posting the click's message.
void stop_heroes(Game *g) {
    petka_scriptvm_stop_hero(g->vm, PETKA_ID_PETKA);
    petka_scriptvm_stop_hero(g->vm, PETKA_ID_CHAPAYEV);
}

void host_stop(void *user, uint16_t hero_id) {
    if (hero_id < 2) ((Game *)user)->walk[hero_id].active = 0;
}

// One step of a walking hero, taken when his walk clip moves on a frame
// (the exe's hero walk step, sub_408EF0).
void walk_frame(Game *g, uint16_t hero_id) {
    Walk *w = &g->walk[hero_id];
    PetkaObjectState *st = petka_scriptvm_state(g->vm, hero_id);
    if (!w->active || !st) return;
    int result = petka_walker_step(&w->walker);
    st->x = (int32_t)lround(w->walker.x);
    st->y = (int32_t)lround(w->walker.y);
    if (result == 2) {
        // New direction: its clip, from the first frame.
        st->resource = (uint16_t)(st->costume + 10 + w->walker.dir);
        visual_load(g, &g->visuals[obj_index(g, petka_script_find(g->script, hero_id))], st->resource);
    } else if (result == 0) {
        w->active = 0;
        petka_scriptvm_walk_ended(g->vm, hero_id);
    }
}

// The name the player sees, "" for an object that has none. Inventory
// items and rooms always get one: where NAMES.INI is silent, the script
// name stands in (inventory objects are called "I<name>" there).
const char *display_name(const Game *g, const PetkaObject *obj) {
    const char *name = g->names[obj - g->script->objects];
    if (name) return utf8_name(name);
    if (obj->is_room) return utf8_name(obj->name);
    if (obj->name[0] == 'I' && obj->name[1]) return utf8_name(obj->name + 1);
    return "";
}
