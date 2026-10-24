// part.c
//
// EN: A part of the game: opening and closing its stores and script, starting and switching parts.
// RU: Часть игры: открытие и закрытие её архивов и сценария, запуск и смена частей.
#include "game.h"

void game_init(Game *g) {
    g->vm->rng = (uint32_t)(GetTime() * 1e6) | 1u;
    petka_scriptvm_init(g->vm);
}

void game_enter(Game *g) {
    const PetkaObject *start = g->topo && g->topo->start_room
        ? petka_script_find_name(g->script, g->topo->start_room) : NULL;
    if (!start) {
        for (uint32_t i = 0; i < g->script->object_count && !start; i++)
            if (g->script->objects[i].is_room) start = &g->script->objects[i];
    }
    if (start) petka_scriptvm_enter_room(g->vm, start->id, 0);
}

void game_start(Game *g) {
    game_init(g);
    game_enter(g);
}

// Everything of the loaded part is let go: what is on screen or sounding,
// the script and its machine, the dialogue, and the stores themselves.
void part_close(Game *g) {
    if (g->voice.frameCount > 0) { StopSound(g->voice); UnloadSound(g->voice); g->voice = (Sound){0}; }
    set_stream(g, &g->music, &g->music_id, PETKA_NONE);
    set_stream(g, &g->ambience, &g->ambience_id, PETKA_NONE);
    set_stream(g, &g->seq_music, &g->seq_music_id, PETKA_NONE);
    set_stream(g, &g->seq_ambience, &g->seq_ambience_id, PETKA_NONE);
    g->tracks_hushed = g->seq_tracks_paused = 0;
    for (uint32_t i = 0; g->script && g->visuals && i < g->script->object_count; i++) visual_free(&g->visuals[i]);
    free(g->visuals);
    g->visuals = NULL;
    free(g->z_order);
    g->z_order = NULL;
    g->z_order_n = 0;
    g->z_order_room = PETKA_NONE;
    visual_free(&g->cursor);
    visual_free(&g->carried);
    visual_free(&g->star);
    g->star_open = 0;
    visual_free(&g->desc_clip);
    g->desc[0] = 0;
    saves_unload(g);
    visual_free(&g->case_body);
    for (int i = 0; i < 2; i++) {
        if (g->case_lid_grey[i].id) UnloadTexture(g->case_lid_grey[i]);
        g->case_lid_grey[i] = (Texture2D){0};
    }
    if (g->star_grey.id) UnloadTexture(g->star_grey);
    g->star_grey = (Texture2D){0};
    visual_free(&g->case_exit);
    visual_free(&g->case_strip);
    for (int i = 0; i < 6; i++) visual_free(&g->case_button[i]);
    for (uint32_t i = 0; g->script && g->names && i < g->script->object_count; i++) free(g->names[i]);
    free(g->names);
    free(g->colors);
    g->names = NULL;
    g->colors = NULL;
    for (int i = 0; i < g->gait_count; i++) petka_gait_close(g->gaits[i].gait);
    g->gait_count = 0;
    petka_walkmesh_close(g->mesh);
    g->mesh = NULL;
    if (g->vm) petka_scriptvm_close(g->vm);
    if (g->script) petka_script_close(g->script);
    if (g->dtree) petka_dialoguetree_close(g->dtree);
    if (g->phrases) petka_phrasetable_close(g->phrases);
    if (g->topo) petka_topology_close(g->topo);
    g->vm = NULL;
    g->script = NULL;
    g->dtree = NULL;
    g->phrases = NULL;
    g->topo = NULL;
    g->bgs_ini = NULL;
    g->bgs_ini_len = 0;
    for (int i = 0; i < 65536; i++) { free(g->qrc[i]); g->qrc[i] = NULL; }
    for (int i = 0; i < g->store_count; i++) petka_store_close(g->stores[i]);
    g->store_count = 0;
    g->chapter_store = -1;
}

// Opens a part: its stores, script, resource table, room graph, dialogue and
// names (the exe's sub_412040 and what it calls). Returns 0 if the part is
// not there; whatever was opened is then for part_close to let go.
int part_open(Game *g, int number, int chapter) {
    snprintf(g->part, sizeof(g->part), "PART_%d", number);
    snprintf(g->speech_prefix, sizeof(g->speech_prefix), "SPEECH%d", number);
    g->part_number = number;
    g->chapter = chapter;
    g->chapter_store = -1;

    static const char *const kStoreNames[] = {"MAIN.STR", "BGS%d.STR", "FLC%d.STR", "WAV%d.STR",
                                              "SFX%d.STR", "MUSIC%d.STR", "SPEECH%d.STR"};
    char file[64];
    for (size_t i = 0; i < sizeof(kStoreNames) / sizeof(kStoreNames[0]); i++) {
        if (i == 0) snprintf(file, sizeof(file), "%s", kStoreNames[i]);
        else snprintf(file, sizeof(file), kStoreNames[i], number);
        PetkaStore *store = open_store(g->game_dir, file);
        if (store) g->stores[g->store_count++] = store;
        else fprintf(stderr, "store not found under %s: %s\n", g->game_dir, file);
    }
    // Which game this is: the build's (PETKA_GAME), or, built for either,
    // the second if PART_1\objects.ids is in its main store, the first having
    // nothing of the kind. What the launcher puts right in the first game's
    // script, by its parts and objects, is not for it.
    g->game = PETKA_GAME;
    if (!g->game) {
        g->game = 1;
        for (int i = 0; i < g->store_count; i++)
            if (petka_store_find(g->stores[i], "PART_1\\objects.ids")) g->game = 2;
    }
    // Only a part split into chapters has a chapter store; it carries that
    // chapter's speech, names and colours.
    snprintf(file, sizeof(file), "CHAP%d%d.STR", number, chapter);
    PetkaStore *chapter_store = open_store(g->game_dir, file);
    if (chapter_store) {
        g->chapter_store = g->store_count;
        g->stores[g->store_count++] = chapter_store;
    }

    size_t script_len = 0, bg_len = 0, len = 0;
    const uint8_t *script_data = part_file(g, g->part, "SCRIPT.DAT", &script_len);
    const uint8_t *bg_data = part_file(g, g->part, "BACKGRND.BG", &bg_len);
    g->script = script_data && bg_data ? petka_script_open(script_data, script_len, bg_data, bg_len) : NULL;
    if (!g->script) { fprintf(stderr, "no script for %s under %s\n", g->part, g->game_dir); return 0; }
    scriptfix_script(g);

    const uint8_t *bytes = part_file(g, g->part, "RESOURCE.QRC", &len);
    if (bytes) load_qrc(g, bytes, len);
    bytes = part_file(g, g->part, "BGS.INI", &len);
    if (bytes) {
        g->topo = petka_topology_parse(bytes, len);
        g->bgs_ini = bytes;
        g->bgs_ini_len = len;
    }
    size_t lod_len = 0, fix_len = 0;
    const uint8_t *lod = part_file(g, g->part, "DIALOGUE.LOD", &lod_len);
    const uint8_t *fix = part_file(g, g->part, "DIALOGUE.FIX", &fix_len);
    if (lod && fix) {
        g->phrases = petka_phrasetable_open(lod, lod_len);
        g->dtree = petka_dialoguetree_open(fix, fix_len);
    }
    if (!g->phrases || !g->dtree) fprintf(stderr, "dialogue data unavailable for %s\n", g->part);
    phrasefix_apply(g);

    g->vm = petka_scriptvm_open(g->script, &g->host);
    load_names(g, g->part);
    g->visuals = (Visual *)calloc(g->script->object_count, sizeof(Visual));
    g->z_order = (uint16_t *)calloc(g->script->object_count + 2, sizeof(uint16_t));
    g->z_order_n = 0;
    g->z_order_room = PETKA_NONE;
    for (uint32_t i = 0; i < g->script->object_count; i++) g->visuals[i].resource = PETKA_NONE;
    return 1;
}

// CHAPTER n (the exe's sub_411C00): the chapter's store takes the place of
// the one before it, and every object's shown name and colour are read
// again from it. The game itself goes on untouched.
void chapter_open(Game *g, int chapter) {
    if (chapter <= 0 || chapter == g->chapter) return;
    char file[64];
    snprintf(file, sizeof(file), "CHAP%d%d.STR", g->part_number, chapter);
    PetkaStore *store = open_store(g->game_dir, file);
    if (!store) return; // a part that is not split into chapters
    if (g->voice.frameCount > 0) { StopSound(g->voice); UnloadSound(g->voice); g->voice = (Sound){0}; }
    if (g->chapter_store >= 0) {
        petka_store_close(g->stores[g->chapter_store]);
        g->stores[g->chapter_store] = store;
    } else if (g->store_count < MAX_STORES) {
        g->chapter_store = g->store_count;
        g->stores[g->store_count++] = store;
    } else {
        petka_store_close(store);
        return;
    }
    g->chapter = chapter;
    for (uint32_t i = 0; g->names && i < g->script->object_count; i++) free(g->names[i]);
    free(g->names);
    free(g->colors);
    load_names(g, g->part);
}

// Takes the game down and builds it again for a part, as the exe does for
// "new game", for the script's PART message and for a saved game of another
// part (sub_412040): nothing of the old game is kept -- and since the game
// writes into its script and dialogue code as it goes, this is also how a
// part is started over.
// Whatever the launcher has up for the game -- the map, the save screen,
// a dialogue, a sequence, the case, a walk -- is taken down.
void game_screens_down(Game *g) {
    map_leave(g);
    saves_close(g);
    g->dialogue = NULL;
    g->talking = g->paused = g->menu_count = g->dialogue_done = 0;
    g->line[0] = 0;
    g->talker = PETKA_NONE;
    g->seq_room = PETKA_NONE;
    g->shown_room = g->bg_resource = g->mesh_resource = PETKA_NONE;
    g->walk[0].active = g->walk[1].active = 0;
    g->case_open = g->case_first = 0;
    g->case_lit = -1;
    g->case_for = PETKA_NONE;
    g->verb = 0;
    g->camera_x = g->camera_target = 0;
    g->camera_snap = 1;
    g->star_open = 0;
    g->desc[0] = 0;
}

void game_switch(Game *g, int part, int chapter) {
    int old_part = g->part_number, old_chapter = g->chapter;
    game_screens_down(g);
    part_close(g);
    if (!part_open(g, part, chapter)) {
        part_close(g);
        part_open(g, old_part, old_chapter);
        set_status(g, TextFormat("(часть %d не найдена, игра начата заново)", part));
    }
    game_start(g);
}

// EN: Gives a fresh Game its starting state: nothing loaded, the panel's
// settings read, and the launcher's callbacks for the script VM.
// RU: Задаёт новой Game начальное состояние: ничего не загружено, настройки
// панели прочитаны, обратные вызовы лаунчера для машины сценария.
void game_setup(Game *g, const char *game_dir) {
    g->bg_resource = g->shown_room = g->music_id = g->ambience_id = g->mesh_resource = PETKA_NONE;
    g->acted_obj = PETKA_NONE;
    g->seq_music_id = g->seq_ambience_id = PETKA_NONE;
    g->talker = PETKA_NONE;
    g->seq_room = PETKA_NONE;
    g->cursor.resource = PETKA_NONE;
    g->carried.resource = PETKA_NONE;
    g->desc_clip.resource = PETKA_NONE;
    g->star.resource = PETKA_NONE;
    g->star_lit = 6;
    g->chapter_store = -1;
    snprintf(g->game_dir, sizeof(g->game_dir), "%s", game_dir);
    g->case_body.resource = g->case_exit.resource = g->case_strip.resource = PETKA_NONE;
    for (int i = 0; i < 6; i++) g->case_button[i].resource = PETKA_NONE;
    g->case_lit = -1;
    g->case_for = PETKA_NONE;
    g->subtitles = 1;
    for (int i = 0; i < 4; i++) g->dial[i] = kDialDefault[i];
    options_load(g);

    g->host.user = g;
    g->host.has_dialogue = host_has_dialogue;
    g->host.select_dialogue = host_select_dialogue;
    g->host.start_dialogue = host_start_dialogue;
    g->host.restore_dialogue = host_restore_dialogue;
    g->host.walk = host_walk;
    g->host.stop = host_stop;
    g->host.room_changed = host_room_changed;
    g->host.message = host_message;
}
