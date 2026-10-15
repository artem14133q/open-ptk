// play.c
//
// EN: dev: a scripted play (PETKA_PLAY) that plays the game from a list of steps.
// RU: Для разработки: прохождение по сценарию (PETKA_PLAY) из списка шагов.
#include "game.h"

PlayState s_play;

void play_load(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) { fprintf(stderr, "play: cannot open %s\n", file); s_play.over = s_play.failed = 1; return; }
    char line[256];
    int number = 0, cap = 0;
    while (fgets(line, sizeof(line), f)) {
        number++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        PlayStep st;
        memset(&st, 0, sizeof(st));
        st.line = number;
        char rest[200] = "";
        if (sscanf(line, "%11s %199[^\n]", st.verb, rest) < 1) continue;
        snprintf(st.text, sizeof(st.text), "%s %s", st.verb, rest);
        if (!strcmp(st.verb, "pick")) {
            for (char *p = strtok(rest, " \t"); p && st.pick_count < 16; p = strtok(NULL, " \t")) st.picks[st.pick_count++] = atoi(p);
        } else if (!strcmp(st.verb, "shot")) {
            sscanf(rest, "%159s", st.text);
        } else {
            sscanf(rest, "%d %d", &st.a, &st.b);
        }
        if (s_play.count == cap) {
            cap = cap ? cap * 2 : 64;
            s_play.steps = (PlayStep *)realloc(s_play.steps, (size_t)cap * sizeof(PlayStep));
        }
        s_play.steps[s_play.count++] = st;
    }
    fclose(f);
    fprintf(stderr, "play: %d steps from %s\n", s_play.count, file);
}

void play_fail(Game *g, const char *why) {
    const PlayStep *st = s_play.at < s_play.count ? &s_play.steps[s_play.at] : NULL;
    const PetkaObject *room = petka_script_find(g->script, g->vm->room);
    fprintf(stderr, "play: FAILED at step %d (line %d: %s): %s\n", s_play.at + 1, st ? st->line : 0, st ? st->text : "end", why);
    fprintf(stderr, "play: room %u %s; queue %u, talking %d, paused %d, menu %d, sequence %u, cursor %d, panel %d, walking %d/%d\n",
            g->vm->room, room ? utf8_name(room->name) : "?", g->vm->queue_count, g->talking, g->paused, g->menu_count,
            g->seq_room, g->vm->cursor_visible, g->vm->interface_enabled, g->walk[0].active, g->walk[1].active);
    fprintf(stderr, "play: case:");
    for (uint32_t i = 0; i < g->vm->inventory_count; i++) {
        const PetkaObject *it = petka_script_find(g->script, g->vm->inventory[i]);
        fprintf(stderr, " %u[%s]", g->vm->inventory[i], it ? utf8_name(it->name) : "?");
    }
    fprintf(stderr, "\n");
    snprintf(s_play.shot, sizeof(s_play.shot), "play-fail.png");
    s_play.over = s_play.failed = 1;
}

int play_has(const Game *g, int id) {
    for (uint32_t i = 0; i < g->vm->inventory_count; i++)
        if (g->vm->inventory[i] == id) return 1;
    return 0;
}

// Whether the player could point at this object now: it is one of the
// room's (or a hero, or a thing in the case) and takes the mouse.
const PetkaObject *play_target(Game *g, int id, char *why, size_t why_sz) {
    const PetkaObject *obj = petka_script_find(g->script, (uint16_t)id);
    if (!obj) { snprintf(why, why_sz, "no object %d", id); return NULL; }
    if (play_has(g, id)) return obj;
    const PetkaObject *room = petka_script_find(g->script, screen_room(g));
    int here = is_hero(obj->id) && !g->map_open;
    for (uint32_t i = 0; room && i < room->attached_count; i++) here = here || room->attached[i].obj_id == obj->id;
    if (!here) { snprintf(why, why_sz, "%d[%s] is not in this room", id, utf8_name(obj->name)); return NULL; }
    if (!g->vm->state[obj_index(g, obj)].active) {
        snprintf(why, why_sz, "%d[%s] does not take the mouse (it is off)", id, utf8_name(obj->name));
        return NULL;
    }
    return obj;
}

// One frame of the play: answers menus, shuts descriptions, and when the
// game has been at rest long enough takes the next step.
void play_tick(Game *g, int can_act) {
    PetkaScriptVM *vm = g->vm;
    if (s_play.over) return;
    // PETKA_PLAY_SUBSHOT=<file.png>: a screenshot of the first phrase said.
    static int subshot_taken;
    if (!subshot_taken && getenv("PETKA_PLAY_SUBSHOT") && g->talking && g->line[0] && !s_play.shot[0]) {
        snprintf(s_play.shot, sizeof(s_play.shot), "%s", getenv("PETKA_PLAY_SUBSHOT"));
        subshot_taken = 1;
    }
    if (g->menu_count > 0) {
        // PETKA_PLAY_LAST=1: a menu with no pick named for it is answered
        // with its last line, which as a rule is the goodbye.
        if (s_play.pick_at >= s_play.pick_count && getenv("PETKA_PLAY_LAST") && s_play.pick_count < 64) {
            s_play.picks[s_play.pick_count++] = g->menu_count - 1;
            fprintf(stderr, "play: no pick named, taking the last of:\n");
            for (int i = 0; i < g->menu_count; i++)
                fprintf(stderr, "play:   %d: %s\n", i,
                        g->menu[i] < g->phrases->count ? utf8_name(g->phrases->phrases[g->menu[i]].text) : "?");
        }
        if (s_play.pick_at >= s_play.pick_count) {
            fprintf(stderr, "play: a menu of %d lines:\n", g->menu_count);
            for (int i = 0; i < g->menu_count; i++)
                fprintf(stderr, "play:   %d: %s\n", i,
                        g->menu[i] < g->phrases->count ? utf8_name(g->phrases->phrases[g->menu[i]].text) : "?");
            play_fail(g, "a menu came up and no pick is left for it");
            return;
        }
        int pick = s_play.picks[s_play.pick_at++];
        fprintf(stderr, "play: menu of %d, picking %d: %s\n", g->menu_count, pick,
                pick < g->menu_count && g->menu[pick] < g->phrases->count ? utf8_name(g->phrases->phrases[g->menu[pick]].text) : "?");
        if (pick < 0 || pick >= g->menu_count || petka_dialoguevm_choose(&g->dvm, pick) != 0) {
            play_fail(g, "the menu has no such line");
            return;
        }
        g->menu_count = 0;
        next_phrase(g);
        s_play.rest = 0;
        return;
    }
    if (g->desc[0]) {
        g->desc[0] = 0;
        vm->interface_enabled = 1;
        s_play.rest = 0;
        return;
    }
    // PETKA_PLAY_HASTY=1: a walk under way does not hold the next step back,
    // for clicking again before the hero is there.
    int walking = (g->walk[0].active || g->walk[1].active) && !getenv("PETKA_PLAY_HASTY");
    int at_rest = can_act && vm->interface_enabled && !g->paused && vm->queue_count == 0 && !walking &&
                  !g->save_mode && (!g->map_open || s_play.phase);
    s_play.rest = at_rest ? s_play.rest + 1 : 0;
    if (s_play.hold > 0) { s_play.hold--; return; }
    if (s_play.rest < PLAY_REST) {
        if (++s_play.waited > PLAY_PATIENCE) play_fail(g, "the game did not come to rest");
        return;
    }
    if (s_play.at >= s_play.count) {
        fprintf(stderr, "play: done, %d steps\n", s_play.count);
        s_play.over = 1;
        return;
    }
    const PlayStep *st = &s_play.steps[s_play.at];
    char why[200] = "";
    static const char *const kNames[] = {"look", "walk", "use", "take", "talk", "vich"};
    int verb = -1;
    for (int i = 0; i < 6; i++) if (!strcmp(st->verb, kNames[i])) verb = i;
    if (!s_play.phase) fprintf(stderr, "play: step %d (line %d): %s\n", s_play.at + 1, st->line, st->text);
    if (verb >= 0) {
        const PetkaObject *obj = play_target(g, st->a, why, sizeof(why));
        if (!obj) { play_fail(g, why); return; }
        uint16_t op = kVerbs[verb].op;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        const PetkaObjectState *os = &vm->state[obj_index(g, obj)];
        int px = os->x + (v->rect.left + v->rect.right) / 2, py = os->y + (v->rect.top + v->rect.bottom) / 2;
        int point = op == PETKA_OP_WALK || op == PETKA_OP_OBJECTUSE;
        stop_heroes(g);
        petka_scriptvm_post(vm, obj->id, op, point ? (uint16_t)px : 0, point ? (uint16_t)py : 0, 0,
                            op == PETKA_OP_OBJECTUSE ? (int)PETKA_ID_CHAPAYEV : (int)obj->id);
    } else if (!strcmp(st->verb, "item")) {
        if (!play_has(g, st->a)) { snprintf(why, sizeof(why), "thing %d is not in the case", st->a); play_fail(g, why); return; }
        const PetkaObject *obj = play_target(g, st->b, why, sizeof(why));
        if (!obj) { play_fail(g, why); return; }
        stop_heroes(g);
        petka_scriptvm_post(vm, obj->id, PETKA_OP_OBJECTUSE, 0, 0, 0, st->a);
    } else if (!strcmp(st->verb, "go")) {
        // A "walk" click on the bare floor, as the mouse does it.
        PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
        int32_t was_x = petka->x, was_y = petka->y;
        petka_scriptvm_forget_walk(vm, PETKA_ID_PETKA);
        if (!host_walk(g, PETKA_ID_PETKA, st->a, st->b)) {
            snprintf(why, sizeof(why), "Petka does not set off from %d,%d (busy %d, visible %d, active %d)", was_x, was_y,
                     petka->busy, petka->visible, petka->active);
            play_fail(g, why);
            return;
        }
        petka->busy = 1;
    } else if (!strcmp(st->verb, "near")) {
        const PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
        int dx = petka->x - st->a, dy = petka->y - st->b;
        if (dx * dx + dy * dy > 40 * 40) {
            snprintf(why, sizeof(why), "Petka is at %d,%d, not near %d,%d (animate %d, walking %d)", petka->x, petka->y,
                     st->a, st->b, petka->animate, g->walk[0].active);
            play_fail(g, why);
            return;
        }
    } else if (!strcmp(st->verb, "map")) {
        if (!s_play.phase) {
            if (!g->map_open) map_command(g);
            if (!g->map_open) { play_fail(g, "the map does not open here"); return; }
            s_play.phase = 1;
            s_play.rest = 0;
            return;
        }
        s_play.phase = 0;
        const PetkaObject *obj = play_target(g, st->a, why, sizeof(why));
        if (!obj) { play_fail(g, why); return; }
        petka_scriptvm_post(vm, obj->id, PETKA_OP_LOOK, 0, 0, 0, obj->id);
    } else if (!strcmp(st->verb, "pick")) {
        for (int i = 0; i < st->pick_count && s_play.pick_count < 64; i++) s_play.picks[s_play.pick_count++] = st->picks[i];
    } else if (!strcmp(st->verb, "part")) {
        if (g->part_number != st->a) { snprintf(why, sizeof(why), "this is part %d, not %d", g->part_number, st->a); play_fail(g, why); return; }
    } else if (!strcmp(st->verb, "room")) {
        if (vm->room != st->a) { snprintf(why, sizeof(why), "this is room %u, not %d", vm->room, st->a); play_fail(g, why); return; }
    } else if (!strcmp(st->verb, "has") || !strcmp(st->verb, "hasnot")) {
        if (play_has(g, st->a) != (st->verb[3] == 0)) { play_fail(g, "the case is not as expected"); return; }
    } else if (!strcmp(st->verb, "save")) {
        char file[32];
        snprintf(file, sizeof(file), "save%d.dat", st->a);
        take_thumb(g);
        if (!game_save(g, file)) { play_fail(g, "the save was not written"); return; }
    } else if (!strcmp(st->verb, "shot")) {
        snprintf(s_play.shot, sizeof(s_play.shot), "%s", st->text);
    } else if (!strcmp(st->verb, "wait")) {
        s_play.hold = st->a;
    } else {
        play_fail(g, "unknown step");
        return;
    }
    s_play.at++;
    s_play.rest = 0;
    s_play.waited = 0;
}
