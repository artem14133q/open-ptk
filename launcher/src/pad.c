// pad.c
//
// EN: A controller's help: the pointer drawn to things nearby, the case walked as a grid.
// RU: Помощь контроллеру: притяжение указателя к предметам, обход чемодана по сетке.
#include "game.h"
Vector2 aim_assist(Game *g, const Drawable *scene, int n, Vector2 at) {
    const PetkaObject *over = g->map_open ? listed_object_at(g, at.x, at.y) : object_at(g, scene, n, at.x, at.y);
    plat_pointer_speed(over ? 0.5f : 1.0f);
    Vector2 aim = plat_aim();
    if (over || aim.x * aim.x + aim.y * aim.y > 0.25f) return at;
    for (int r = 2; r <= AIM_REACH; r += 2) {
        int steps = r * 2 + 8;
        for (int k = 0; k < steps; k++) {
            float a = 6.2831853f * k / steps;
            Vector2 p = {at.x + r * cosf(a), at.y + r * sinf(a)};
            const PetkaObject *obj = g->map_open ? listed_object_at(g, p.x, p.y) : object_at(g, scene, n, p.x, p.y);
            if (!obj || is_hero(obj->id)) continue;
            // A little past the edge, to be well on it.
            Vector2 goal = {p.x + 2 * cosf(a), p.y + 2 * sinf(a)};
            return (Vector2){at.x + (goal.x - at.x) * 0.3f, at.y + (goal.y - at.y) * 0.3f};
        }
    }
    return at;
}

Vector2 case_steer(Game *g, int dx, int dy, int page) {
    if (page > 0 && (uint32_t)(g->case_first + CASE_PAGE) < g->vm->inventory_count) {
        g->case_first += CASE_PAGE;
        g->case_focus = 0;
    } else if (page < 0 && g->case_first > 0) {
        g->case_first = g->case_first > CASE_PAGE ? g->case_first - CASE_PAGE : 0;
        g->case_focus = 0;
    }
    Vector2 at[CASE_PLACES];
    int present[CASE_PLACES], any = 0;
    for (int k = 0; k < CASE_PAGE; k++) {
        Visual *v = NULL;
        present[k] = case_item(g, k, &v) != NULL;
        if (present[k])
            at[k] = (Vector2){kCaseCell[k][0] + (v->rect.left + v->rect.right) / 2.0f,
                              kCaseCell[k][1] + (v->rect.top + v->rect.bottom) / 2.0f};
    }
    for (int i = 0; i < 3; i++) {
        Visual *b = &g->case_button[i];
        if (b->resource == PETKA_NONE) visual_load(g, b, (uint16_t)(CASE_BUTTON0 + i));
        int k = CASE_PAGE + i;
        present[k] = g->case_for == PETKA_NONE && b->flic && i != 0 && !(i == 2 && !map_allowed(g));
        if (present[k])
            at[k] = (Vector2){(b->rect.left + b->rect.right) / 2.0f, (b->rect.top + b->rect.bottom) / 2.0f};
    }
    for (int k = 0; k < CASE_PLACES; k++) any |= present[k];
    if (!any) return (Vector2){GAME_W / 2.0f, GAME_H / 2.0f};
    if (g->case_focus < 0 || g->case_focus >= CASE_PLACES || !present[g->case_focus])
        for (g->case_focus = 0; !present[g->case_focus]; g->case_focus++) {}
    // The places are a grid: the page's two rows of three, and above them
    // the lid's buttons, in the order they lie (Chapayev, the map, the
    // diskette). Across goes along the row to the next thing there; up and
    // down go to the nearest row with anything in it, to the thing in the
    // nearest column -- not by where the pictures sit, which in their cells
    // are high and low and sent "right" off to the other row.
    static const int kLidColumn[3] = {0, 2, 1};
    int row[CASE_PLACES], column[CASE_PLACES];
    for (int k = 0; k < CASE_PLACES; k++) {
        row[k] = k < CASE_PAGE ? k / 3 : -1;
        column[k] = k < CASE_PAGE ? k % 3 : kLidColumn[k - CASE_PAGE];
    }
    int from = g->case_focus;
    for (int c = column[from] + dx; dx && c >= 0 && c < 3; c += dx) {
        int found = -1;
        for (int k = 0; k < CASE_PLACES && found < 0; k++)
            if (present[k] && row[k] == row[from] && column[k] == c) found = k;
        if (found >= 0) { g->case_focus = found; break; }
    }
    for (int r = row[from] + dy; dy && r >= -1 && r <= 1; r += dy) {
        int found = -1;
        for (int k = 0; k < CASE_PLACES; k++)
            if (present[k] && row[k] == r &&
                (found < 0 || abs(column[k] - column[from]) < abs(column[found] - column[from])))
                found = k;
        if (found >= 0) { g->case_focus = found; break; }
    }
    // The middle of a thing's rect can be a hole in it (the moustache of
    // part 2's "nose with moustache" is a ring): the pointer goes to the
    // point nearest the middle that clicks on the thing.
    if (g->case_focus < CASE_PAGE) {
        Visual *v = NULL;
        int k = g->case_focus;
        if (case_item(g, k, &v)) {
            Vector2 local = clip_point(v, (Vector2){at[k].x - kCaseCell[k][0], at[k].y - kCaseCell[k][1]});
            return (Vector2){local.x + kCaseCell[k][0], local.y + kCaseCell[k][1]};
        }
    }
    return at[g->case_focus];
}
