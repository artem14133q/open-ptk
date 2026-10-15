// star.c
//
// EN: The star: the menu of actions, by the mouse or by a controller's stick, and doing an action.
// RU: Звезда: меню действий, мышью или стиком контроллера, и выполнение действия.
#include "game.h"
const Rectangle kStarPart[6] = {
    {.x = 70 , .y = 74 , .width = 42, .height = 38}, // the middle: the case
    {.x = 68 , .y = 0  , .width = 46, .height = 41}, // look
    {.x = 151, .y = 51 , .width = 29, .height = 46}, // walk
    {.x = 138, .y = 125, .width = 41, .height = 41}, // use
    {.x = 55 , .y = 145, .width = 41, .height = 30}, // take
    {.x = 11 , .y = 79 , .width = 29, .height = 39}, // talk
};

void star_toggle(Game *g, const int px, const int py) {
    if (g->star_open) { g->star_open = 0; return; }
    if (g->star.resource != STAR_CLIP) visual_load(g, &g->star, STAR_CLIP);
    if (!g->star.flic) return;

    const int w = g->star.flic->width;
    const int h = g->star.flic->height;

    g->star_x = px - w / 2;
    g->star_y = py - h / 2;

    if (g->star_x < 0) g->star_x = 0;
    if (g->star_x > GAME_W - 1 - w) g->star_x = GAME_W - 1 - w;
    if (g->star_y < 0) g->star_y = 0;
    if (g->star_y > GAME_H - 1 - h) g->star_y = GAME_H - 1 - h;

    g->star_open = 1;
}

// Which of the star's parts holds the point; 6 = none.
int star_part(const Game *g, const int px, const int py) {
    const Vector2 p = {.x = (float)(px - g->star_x), .y = (float)(py - g->star_y)};

    for (int i = 0; i < 6; i++) if (CheckCollisionPointRec(p, kStarPart[i])) return i;

    return 6;
}

// The part of the star the left stick points at: of the five round the
// middle, the one whose direction from the middle is nearest the stick's;
// while the stick is at rest the middle, the case -- or nothing, when the
// case is open already (`middle` 0).
int star_aim(const int middle) {
    const Vector2 aim = plat_aim();
    const float len = sqrtf(aim.x * aim.x + aim.y * aim.y);

    if (len < 0.5f) return middle ? 0 : 6;

    const Rectangle *m = &kStarPart[0];
    const float cx = m->x + m->width / 2;
    const float cy = m->y + m->height / 2;

    int best = 6;
    float best_dot = -2;
    for (int i = 1; i < 6; i++) {
        const float dx = kStarPart[i].x + kStarPart[i].width / 2 - cx;
        const float dy = kStarPart[i].y + kStarPart[i].height / 2 - cy;
        const float dot = (dx * aim.x + dy * aim.y) / (sqrtf(dx * dx + dy * dy) * len);

        if (dot > best_dot) { best_dot = dot; best = i; }
    }

    return best;
}

void star_click(Game *g, int part) {
    if (part == 0) {
        g->case_open = 1;
    } else if (part < 6) {
        drop_item(g);
        g->verb = part - 1; // the cursor gets the clip 5001 + part
    }

    g->star_open = 0;
}

// The star's own red and yellow, round the case drawn in its middle.
int star_body(const uint8_t *c) { return c[0] > 150 && c[2] < 100; }

void draw_star(Game *g, const float scale) {
    Visual *v = &g->star;

    if (!v->flic) return;

    // Frame 1 is the star with nothing lit; part n is lit on frame n + 2.
    const int frame = (g->star_lit + 1) % 7 + 1;

    if (frame <= v->flic->frame_count && v->flic->frame_index != frame - 1) {
        for (int guard = 0; guard < v->flic->frame_count && v->flic->frame_index != frame - 1; guard++)
            petka_flic_advance(v->flic);
        visual_upload(v);
    }

    const Rectangle at = {
        .x = (float)g->star_x * scale,
        .y = (float)g->star_y * scale,
        .width = (float)v->flic->width * scale,
        .height = (float)v->flic->height * scale,
    };

    visual_draw(v, at);
    // With a controller, over the open case, the middle (the case itself)
    // does nothing: the case drawn there is grey, the star round it not.
    if (plat_pad() && g->case_open) {
        const Rectangle *m = &kStarPart[0];
        draw_grey(
            v,
            (PetkaMskRect){
                .left = (int)m->x,
                .top = (int)m->y,
                .right = (int)(m->x + m->width),
                .bottom = (int)(m->y + m->height)
            },
            &g->star_grey,
            at,
            star_body
        );
    }
}

// A click with the cursor's action `verb` on `obj`, at mx, my in the room.
void act_on(Game *g, const PetkaObject *obj, const int verb, const float mx, const float my) {
    PetkaScriptVM *vm = g->vm;
    if (kVerbs[verb].op == PETKA_OP_WALK && !is_hero(obj->id) &&
        !petka_scriptvm_state(vm, PETKA_ID_PETKA)->active) {
        // "Walk" to something while Petka is off -- a clip of him doing
        // something stands in his place -- is not taken. A way out would
        // otherwise be walked by nobody and the room change with Petka
        // still off: Chapayev came into the next room alone. (It can be
        // clicked: in the church the cursor is back when the crypt's door
        // has opened, with Petka still at the brick.)
        return;
    }

    const uint16_t op = kVerbs[verb].op;
    const int point = op == PETKA_OP_WALK || op == PETKA_OP_OBJECTUSE;

    stop_heroes(g);
    petka_scriptvm_post(
        vm,
        obj->id,
        op,
        point ? (uint16_t)mx : 0,
        point ? (uint16_t)my : 0,
        0,
        op == PETKA_OP_OBJECTUSE ? (int)PETKA_ID_CHAPAYEV : (int)obj->id
    );
    restored_player_action(g, obj->id, op);
}

// The star put up with a controller: its lit part done to the object it is
// up for.
void star_pick(Game *g) {
    const int part = g->star_lit;
    g->star_open = 0;

    if (part == 0) {
        // The case, to pick a thing for the object -- unless the star is up
        // over the case already, for one of its things.
        if (!g->case_open) {
            g->case_open = 1;
            g->case_for = g->pad_target;
        }
        return;
    }

    if (part < 1 || part > 5) return;

    g->verb = part - 1;

    const PetkaObject *obj = petka_script_find(g->script, g->pad_target);

    if (obj) act_on(g, obj, g->verb, g->pad_x, g->pad_y);
}
