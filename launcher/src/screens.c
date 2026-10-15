// screens.c
//
// EN: The map and the panel (the game's menu with the dials), and their settings file.
// RU: Карта и панель (меню игры с регуляторами), и файл их настроек.
#include "game.h"

// The map can be opened from a room that is on it (the room's flag +144,
// taken off by NOMAP).
int map_allowed(const Game *g) {
    const PetkaObjectState *room = petka_scriptvm_state(g->vm, g->vm->room);
    return room && room->on_map;
}

// Back from the map to the room (the map screen's leave, sub_41D730): the
// cursor's action and the view's scroll are as they were.
void map_leave(Game *g) {
    if (!g->map_open) return;
    g->map_open = g->panel_open = 0;
    g->seq_room = PETKA_NONE;
    g->verb = g->map_verb;
    g->camera_x = g->camera_target = g->map_camera;
}

// The map command (sub_417D80; keys M and Tab, and the case's map button):
// to the map, or back from it. The map is a room of the script, "КАРТА",
// that the exe's map screen (its start, sub_41CF30) puts up in place of the
// current one: the room's picture and its objects, no heroes, no scroll; the
// cursor becomes the interface pointer with the action "look", which is what
// the places on the map answer to; and INITBG goes round with the map as
// its sender.
void map_command(Game *g) {
    if (!g->vm->interface_enabled) return;
    if (g->panel_open) return;
    if (g->map_open) { map_leave(g); return; }
    if (!map_allowed(g) || g->seq_room != PETKA_NONE) return;
    const PetkaObject *map = petka_script_find_name(g->script, "\xCA\xC0\xD0\xD2\xC0"); // "КАРТА", cp1251
    if (!map) { set_status(g, "(в этой части нет карты)"); return; }
    g->case_open = 0;
    drop_item(g);
    g->map_verb = g->verb;
    g->map_camera = g->camera_x;
    g->verb = 0;
    g->map_open = 1;
    g->seq_room = map->id;
    petka_scriptvm_broadcast(g->vm, PETKA_OP_INITBG, 0, 0, 0, map->id);
}

// The panel command (sub_417D20; key O and the case's diskette button): to
// the game's panel -- menu, options, saving and loading -- or back from it.
// Like the map it is a room of the script, "ПАНЕЛЬ УПРАВЛЕНИЯ", put up by a
// screen of its own (its start, sub_41C170), with the interface pointer and
// no scroll; its objects are small clips, placed by a table in the exe in
// the order the room lists them. What the buttons do is not built yet.
void panel_command(Game *g) {
    // word_4CA180 (x) and word_4CA1B0 (y); the eight slider marks after
    // these stay at 0,0.
    static const int16_t kPlace[16][2] = {
        {0, 2}, {5, 70}, {5, 136}, {22, 328}, {87, 224}, {118, 395}, {467, 71}, {432, 144},
        {428, 29}, {434, 170}, {297, 214}, {470, 139}, {318, 87}, {468, 172}, {262, 31}, {231, 137},
    };
    if (!g->vm->interface_enabled) return;
    if (g->panel_open) { map_leave(g); return; }
    if (g->map_open || g->seq_room != PETKA_NONE) return;
    // "ПАНЕЛЬ УПРАВЛЕНИЯ", cp1251
    const PetkaObject *panel = petka_script_find_name(
        g->script, "\xCF\xC0\xCD\xC5\xCB\xDC \xD3\xCF\xD0\xC0\xC2\xCB\xC5\xCD\xC8\xDF");
    if (!panel) { set_status(g, "(в этой части нет панели)"); return; }
    g->case_open = 0;
    drop_item(g);
    take_thumb(g);
    for (uint32_t i = 0; i < panel->attached_count && i < 16; i++) {
        PetkaObjectState *st = petka_scriptvm_state(g->vm, panel->attached[i].obj_id);
        if (st) { st->x = kPlace[i][0]; st->y = kPlace[i][1]; }
    }
    // None of them is animated: a clip's frames are the states of a switch.
    for (uint32_t i = 0; i < panel->attached_count; i++) {
        PetkaObjectState *st = petka_scriptvm_state(g->vm, panel->attached[i].obj_id);
        if (st) st->animate = 0;
    }
    g->map_verb = g->verb;
    g->map_camera = g->camera_x;
    g->verb = 0;
    g->map_open = g->panel_open = 1;
    g->seq_room = panel->id;
}

// A click on the panel (sub_41CAE0). The exe goes by the object's number in
// the screen's list, which is its number in the room's list plus one.
// "New game" (sub_418260): the part and chapter to begin with are read from
// [All] of PARTS.INI, 1 and 1 unless it says otherwise, and that part loaded.
// ---- the panel's settings ----
//
// The exe keeps them in the "Options" section of its profile (sub_41C5A0
// writes them every time one changes; the panel's constructor reads them):
// "Speech volume", "Music volume", "Sfx volume", "Speed" and "Subtitles".
// The launcher keeps the same section in a file, Petka.ini, in the directory
// it is run from -- next to the saved games. dev: PETKA_OPTIONS=<file> names
// another; an unattended run without it neither reads nor writes one.
const char *const kDialKey[4] = {"Speech volume", "Music volume", "Sfx volume", "Speed"};

const char *options_file(void) {
    if (getenv("PETKA_OPTIONS")) return getenv("PETKA_OPTIONS");
    return getenv("PETKA_SHOT") || getenv("PETKA_PLAY") ? NULL : "Petka.ini";
}

void options_load(Game *g) {
    const char *file = options_file();
    size_t len = 0;
    uint8_t *ini = file ? read_whole_file(file, &len) : NULL;
    if (!ini) return;
    char value[32];
    for (int i = 0; i < 4; i++) {
        if (!petka_ini_get(ini, len, "Options", kDialKey[i], value, sizeof(value)) || !value[0]) continue;
        g->dial[i] = atoi(value);
        if (g->dial[i] < 1) g->dial[i] = 1;
        if (g->dial[i] > kDialTop[i]) g->dial[i] = kDialTop[i];
    }
    if (petka_ini_get(ini, len, "Options", "Subtitles", value, sizeof(value)) && value[0])
        g->subtitles = atoi(value) != 0;
    free(ini);
}

void options_save(const Game *g) {
    const char *file = options_file();
    FILE *f = file ? fopen(file, "w") : NULL;
    if (!f) return;
    fprintf(f, "[Options]\n");
    for (int i = 0; i < 4; i++) fprintf(f, "%s=%d\n", kDialKey[i], g->dial[i]);
    fprintf(f, "Subtitles=%d\n", g->subtitles ? 1 : 0);
    fclose(f);
}

void new_game(Game *g) {
    size_t len = 0;
    const uint8_t *ini = res_find(g, "PARTS.INI", &len);
    char value[32];
    g->switch_part = ini && petka_ini_get(ini, len, "All", "Part", value, sizeof(value)) ? atoi(value) : 1;
    g->switch_chapter = ini && petka_ini_get(ini, len, "All", "Chapter", value, sizeof(value)) ? atoi(value) : 1;
    if (g->switch_part <= 0) g->switch_part = 1;
}

void panel_click(Game *g, const PetkaObject *hit) {
    const PetkaObject *panel = petka_script_find(g->script, g->seq_room);
    int n = -1;
    for (uint32_t i = 0; panel && i < panel->attached_count; i++)
        if (panel->attached[i].obj_id == hit->id) n = (int)i;
    switch (n) {
    case 0: map_leave(g); new_game(g); break;
    case 1: map_leave(g); saves_open(g, 1); break; // "restore"
    case 2: map_leave(g); break;                 // "continue"
    case 3: map_leave(g); g->quit = 1; break;    // the door: out of the game
    case 4: map_leave(g); saves_open(g, 2); break; // "save"
    case 7: // the subtitle switch
        g->subtitles = !g->subtitles;
        options_save(g);
        break;
    default:
        // The hidden "min" and "MAX" marks of the four dials, in the dials'
        // own order: a notch of five either way, kept in range.
        if (n >= 16 && n < 24) {
            int which = (n - 16) / 2;
            g->dial[which] += (n - 16) % 2 ? 5 : -5;
            if (g->dial[which] < 1) g->dial[which] = 1;
            if (g->dial[which] > kDialTop[which]) g->dial[which] = kDialTop[which];
            if (g->voice.frameCount > 0) SetSoundVolume(g->voice, dial_gain(g, DIAL_SPEECH));
            options_save(g);
        }
        break;
    }
}

// The panel shows the subtitle switch on two of its objects (the end of
// sub_41C170): the button, number 7, on frame 7 for "on" and 1 for "off",
// and the word, number 11, on frame 2 or 1. And each dial's hand is a clip
// put on the frame its value names (sub_41C740): speech on object 14, music
// on 15, effects on 8, speed on 9.
void panel_show_state(Game *g) {
    static const int kHand[4] = {14, 15, 8, 9};
    const PetkaObject *panel = petka_script_find(g->script, g->seq_room);
    if (!panel || panel->attached_count < 16) return;
    for (int i = 0; i < 4; i++) {
        const PetkaObject *hand = petka_script_find(g->script, panel->attached[kHand[i]].obj_id);
        if (hand) show_frame(g, hand, g->dial[i]);
    }
    const PetkaObject *button = petka_script_find(g->script, panel->attached[7].obj_id);
    const PetkaObject *word = petka_script_find(g->script, panel->attached[11].obj_id);
    if (button) show_frame(g, button, g->subtitles ? 7 : 1);
    if (word) show_frame(g, word, g->subtitles ? 2 : 1);

    // What the pointer is on is lit by a second frame (the panel's mouse
    // function, sub_41C8D0): the five buttons themselves, and for a dial's
    // "min" and "MAX" marks the word that names the dial.
    static const int kDialWord[4] = {12, 10, 6, 13}; // speech, music, effects, speed
    int over = g->panel_hover;
    for (int i = 0; i < 5; i++) {
        const PetkaObject *obj = petka_script_find(g->script, panel->attached[i].obj_id);
        if (obj) show_frame(g, obj, over == i ? 2 : 1);
    }
    for (int i = 0; i < 4; i++) {
        const PetkaObject *obj = petka_script_find(g->script, panel->attached[kDialWord[i]].obj_id);
        if (obj) show_frame(g, obj, over >= 16 && over < 24 && (over - 16) / 2 == i ? 2 : 1);
    }
}
