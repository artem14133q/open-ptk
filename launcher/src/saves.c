// saves.c
//
// EN: Saved games: the save screen, its slots and pictures, writing and reading a game.
// RU: Сохранения: экран сохранений, ячейки и картинки, запись и чтение игры.
#include "game.h"
const Rectangle kSaveSlot[SAVE_SLOTS] = {
    {43, 84, 108, 82}, {43, 209, 108, 82}, {43, 335, 108, 82},
    {358, 75, 108, 82}, {360, 200, 108, 82}, {359, 325, 108, 82},
};
const Rectangle kSavePrev = {10, 414, 28, 28}, kSaveNext = {596, 403, 28, 28};

void save_path(int page, int slot, char *out, size_t out_sz) {
    snprintf(out, out_sz, "save%d.dat", slot + SAVE_SLOTS * page + 1);
}

// The picture a save is shown by, taken as the panel comes up (the exe's
// sub_41E230): the whole frame stretched, with no smoothing, into the
// 107x81 corner of a fresh 108x82 surface of 5:6:5 pixels -- the last
// column and row are left as they were made.
void take_thumb(Game *g) {
    const int w = PETKA_SAVE_THUMB_W - 1, h = PETKA_SAVE_THUMB_H - 1;
    Image shot = LoadImageFromScreen();
    ImageResizeNN(&shot, w, h);
    ImageFormat(&shot, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const uint8_t *px = (const uint8_t *)shot.data;
    memset(g->thumb, 0, sizeof(g->thumb));
    for (int y = 0; px && y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *c = px + 4 * (y * w + x);
            uint16_t p = (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3);
            g->thumb[2 * (y * PETKA_SAVE_THUMB_W + x)] = (uint8_t)p;
            g->thumb[2 * (y * PETKA_SAVE_THUMB_W + x) + 1] = (uint8_t)(p >> 8);
        }
    }
    UnloadImage(shot);
}

Texture2D thumb_texture(const uint8_t *thumb) {
    uint8_t *rgba = (uint8_t *)malloc(PETKA_SAVE_THUMB_W * PETKA_SAVE_THUMB_H * 4);
    for (int i = 0; i < PETKA_SAVE_THUMB_W * PETKA_SAVE_THUMB_H; i++) {
        uint16_t p = (uint16_t)(thumb[2 * i] | thumb[2 * i + 1] << 8);
        rgba[4 * i] = (uint8_t)((p >> 11) * 255 / 31);
        rgba[4 * i + 1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
        rgba[4 * i + 2] = (uint8_t)((p & 31) * 255 / 31);
        rgba[4 * i + 3] = 255;
    }
    Image img = {.data = rgba, .width = PETKA_SAVE_THUMB_W, .height = PETKA_SAVE_THUMB_H, .mipmaps = 1,
                 .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

// Writes the game as it stands to a file (the exe's sub_406EB0).
int game_save(Game *g, const char *path) {
    PetkaScriptVM *vm = g->vm;
    PetkaSave save = {0};
    save.part = (uint32_t)g->part_number;
    save.chapter = (uint32_t)g->chapter;
    time_t now = time(NULL);
    strftime(save.label, sizeof(save.label), "%X   %d %B %Y", localtime(&now));
    memcpy(save.thumb, g->thumb, sizeof(save.thumb));

    save.object_count = g->script->object_count;
    save.objects = (PetkaSaveObject *)calloc(save.object_count, sizeof(PetkaSaveObject));
    for (uint32_t i = 0; save.objects && i < save.object_count; i++) {
        const PetkaObject *obj = &g->script->objects[i];
        const PetkaObjectState *st = &vm->state[i];
        PetkaSaveObject *out = &save.objects[i];
        out->name = (char *)obj->name;
        out->shown = g->names[i] ? (char *)g->names[i] : (char *)"";
        out->busy = st->busy;
        out->status = (uint8_t)st->status;
        out->resource = st->resource == PETKA_NONE ? -1 : st->resource;
        out->z = st->z;
        out->x = st->x;
        out->y = st->y;
        // The exe keeps a hero's sprite corner here; his feet come later.
        if (is_hero(obj->id) && g->visuals[i].flic) {
            Rectangle d = hero_dest(g, obj->id, st, &g->visuals[i]);
            out->x = (int32_t)d.x;
            out->y = (int32_t)d.y;
        }
        out->visible = st->visible;
        out->active = st->active;
        out->animate = st->animate;
    }
    save.item_count = vm->inventory_count;
    save.items = vm->inventory;
    const PetkaObject *room = petka_script_find(g->script, vm->room);
    save.room = room ? (char *)room->name : (char *)"";
    const PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
    const PetkaObjectState *chapayev = petka_scriptvm_state(vm, PETKA_ID_CHAPAYEV);
    if (petka) { save.petka_x = petka->x; save.petka_y = petka->y; }
    if (chapayev) { save.chapayev_x = chapayev->x; save.chapayev_y = chapayev->y; }
    save.petka_costume = petka && petka->costume != PETKA_NONE ? petka->costume : -1;
    save.chapayev_costume = chapayev && chapayev->costume != PETKA_NONE ? chapayev->costume : -1;
    if (g->dtree) { save.code_count = g->dtree->code_count; save.code = g->dtree->code; }
    if (vm->cursor_object != PETKA_NONE) {
        save.cursor_resource = vm->cursor_resource;
        save.cursor_action = 6;
        save.cursor_item = vm->cursor_object;
    } else {
        save.cursor_resource = CURSOR_ACTION0 + g->verb;
        save.cursor_action = g->verb;
        save.cursor_item = -1;
    }

    size_t len = 0;
    uint8_t *bytes = save.objects ? petka_save_encode(&save, &len) : NULL;
    free(save.objects);
    if (!bytes) return 0;
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(bytes, 1, len, f) == len;
    if (f) fclose(f);
    free(bytes);
    return ok;
}

// Brings a saved game back (the exe's sub_407410). Objects are found by
// name; a saved z is not used; what the file does not hold stays as it is.
int game_load(Game *g, const char *path) {
    PetkaScriptVM *vm = g->vm;
    size_t len = 0;
    uint8_t *bytes = read_whole_file(path, &len);
    PetkaSave *save = bytes ? petka_save_decode(bytes, len) : NULL;
    free(bytes);
    if (!save) return 0;
    if ((int)save->part != g->part_number) {
        // A save from another part is not loaded here: the game goes to that
        // part first and comes back to the file.
        snprintf(g->pending_load, sizeof(g->pending_load), "%s", path);
        g->switch_part = (int)save->part;
        g->switch_chapter = save->chapter ? (int)save->chapter : 1;
        petka_save_free(save);
        return 0;
    }
    chapter_open(g, (int)save->chapter);
    map_leave(g);
    g->case_open = 0;
    g->talking = g->paused = g->menu_count = 0;
    g->line[0] = 0;
    if (g->voice.frameCount > 0) { StopSound(g->voice); UnloadSound(g->voice); g->voice = (Sound){0}; }
    g->walk[0].active = g->walk[1].active = 0;
    // Nothing the script had in hand outlasts a load: a save holds neither
    // the queue nor the parked reactions. It matters most for a save of
    // another part, which is loaded after that part has been started --
    // the opening scene of the part was still in the queue and ran over the
    // loaded game, hiding the cursor and the heroes. And a game can only
    // be saved with the cursor and the panel on, so on they go.
    petka_scriptvm_drop_pending(vm);
    g->dialogue_done = 0;
    g->talker = PETKA_NONE;
    g->seq_room = PETKA_NONE;
    vm->cursor_visible = 1;
    vm->interface_enabled = 1;

    for (uint32_t i = 0; i < save->object_count; i++) {
        const PetkaSaveObject *in = &save->objects[i];
        const PetkaObject *obj = in->name ? petka_script_find_name(g->script, in->name) : NULL;
        if (!obj) continue;
        PetkaObjectState *st = &vm->state[obj_index(g, obj)];
        st->busy = in->busy;
        st->status = in->status;
        st->resource = in->resource < 0 || in->resource >= PETKA_NONE ? PETKA_NONE : (uint16_t)in->resource;
        st->x = in->x;
        st->y = in->y;
        st->visible = in->visible != 0;
        st->active = in->active != 0;
        st->animate = in->animate != 0;
    }
    petka_scriptvm_set_inventory(vm, save->items, save->item_count);
    g->case_first = 0;

    // The room is entered without its INITBG going round.
    const PetkaObject *room = save->room ? petka_script_find_name(g->script, save->room) : NULL;
    if (room) petka_scriptvm_enter_room(vm, room->id, 1);
    PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
    PetkaObjectState *chapayev = petka_scriptvm_state(vm, PETKA_ID_CHAPAYEV);
    if (petka) { petka->x = save->petka_x; petka->y = save->petka_y; }
    if (chapayev) { chapayev->x = save->chapayev_x; chapayev->y = save->chapayev_y; }
    g->camera_snap = 1; // the view goes to Petka

    if (g->dtree && save->code_count == g->dtree->code_count)
        memcpy(g->dtree->code, save->code, sizeof(uint32_t) * save->code_count);

    const PetkaObject *item = save->cursor_item >= 0 ? petka_script_find(g->script, (uint16_t)save->cursor_item) : NULL;
    if (item) {
        vm->cursor_object = item->id;
        vm->cursor_resource = (uint16_t)save->cursor_resource;
    } else {
        vm->cursor_object = vm->cursor_resource = PETKA_NONE;
        if (save->cursor_action >= 0 && save->cursor_action < VERB_COUNT) g->verb = save->cursor_action;
    }
    // A hero's costume comes back as an IMAGE message to him.
    if (save->petka_costume != -1 && save->petka_costume % 100 == 0)
        petka_scriptvm_send(vm, PETKA_ID_PETKA, PETKA_OP_IMAGE, (uint16_t)save->petka_costume, 1, 0, -1);
    if (save->chapayev_costume != -1 && save->chapayev_costume % 100 == 0)
        petka_scriptvm_send(vm, PETKA_ID_CHAPAYEV, PETKA_OP_IMAGE, (uint16_t)save->chapayev_costume, 1, 0, -1);
    vm->interface_enabled = 1;
    vm->cursor_visible = 1;
    petka_save_free(save);
    return 1;
}

void saves_unload(Game *g) {
    if (g->save_bg.id) UnloadTexture(g->save_bg);
    g->save_bg = (Texture2D){0};
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (g->save_slot[i].tex.id) UnloadTexture(g->save_slot[i].tex);
        g->save_slot[i].tex = (Texture2D){0};
        g->save_slot[i].present = 0;
    }
}

// Puts up a page of the screen (its set-up, sub_41D9E0): the picture for the
// mode and page, and for every slot with a file its picture and label.
void saves_show(Game *g) {
    saves_unload(g);
    int picture = (g->save_mode == 1 ? SAVE_LOAD_PICTURE : SAVE_SAVE_PICTURE) + g->save_page;
    if (g->qrc[picture]) g->save_bg = load_bmp_texture(g, g->qrc[picture]);
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char path[64];
        save_path(g->save_page, i, path, sizeof(path));
        size_t len = 0;
        uint8_t *bytes = read_whole_file(path, &len);
        PetkaSave head;
        if (bytes && petka_save_peek(bytes, len, &head) == 0) {
            g->save_slot[i].present = 1;
            g->save_slot[i].tex = thumb_texture(head.thumb);
            cp1251_to_utf8(head.label, g->save_slot[i].label, sizeof(g->save_slot[i].label));
        }
        free(bytes);
    }
}

void saves_open(Game *g, int mode) {
    g->save_mode = mode;
    saves_show(g);
}

void saves_close(Game *g) {
    saves_unload(g);
    g->save_mode = 0;
}

// A click on the screen (sub_41E370): a slot loads or saves, the corners
// turn the page.
void saves_click(Game *g, Vector2 p) {
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (!CheckCollisionPointRec(p, kSaveSlot[i])) continue;
        char path[64];
        save_path(g->save_page, i, path, sizeof(path));
        int loading = g->save_mode == 1;
        if (loading && !g->save_slot[i].present) return;
        saves_close(g);
        // A save picked from the screen "load" put up at start-up, or after
        // the end: that screen was not left empty-handed, so not back to the
        // menu (the end's went back there at once, the game loaded or not).
        if (loading) g->boot_load = g->menu_on_back = 0;
        if (loading) { if (game_load(g, path)) set_status(g, "(игра загружена)"); }
        else set_status(g, game_save(g, path) ? "(игра сохранена)" : "(не удалось записать сохранение)");
        return;
    }
    if (CheckCollisionPointRec(p, kSavePrev) && g->save_page > 0) { g->save_page--; saves_show(g); }
    if (CheckCollisionPointRec(p, kSaveNext) && g->save_page < 2) { g->save_page++; saves_show(g); }
}

void draw_saves(Game *g, Font font, float scale) {
    if (g->save_bg.id)
        DrawTexturePro(g->save_bg, (Rectangle){0, 0, (float)g->save_bg.width, (float)g->save_bg.height},
                       (Rectangle){0, 0, GAME_W * scale, GAME_H * scale}, (Vector2){0, 0}, 0, WHITE);
    else
        DrawRectangle(0, 0, (int)(GAME_W * scale), (int)(GAME_H * scale), BLACK);
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (!g->save_slot[i].present) continue;
        Rectangle r = kSaveSlot[i];
        DrawTexturePro(g->save_slot[i].tex, (Rectangle){0, 0, PETKA_SAVE_THUMB_W, PETKA_SAVE_THUMB_H},
                       (Rectangle){r.x * scale, r.y * scale, r.width * scale, r.height * scale}, (Vector2){0, 0}, 0, WHITE);
        // The label in a box 240 wide a pixel below the picture, in the
        // exe's 20-pixel font -- not its dark green, but as the subtitles
        // are drawn: white, rimmed in black, on the dark band.
        enum { LABEL_LINES = 4 };
        char lines[LABEL_LINES][512];
        const float limit = 240 * scale, x = r.x * scale, y = (r.y + r.height + 1) * scale;
        // On one line if it can be: a font wider than the exe's (the
        // Switch's own) would break the date and time in two, so the size
        // comes down to fit -- to 14 at the least; past that it breaks.
        float size = 20 * scale, whole = MeasureTextEx(font, g->save_slot[i].label, size, 1).x;
        if (whole > limit) size = fmaxf(14 * scale, size * limit / whole);
        int count = wrap_text(font, g->save_slot[i].label, size, limit, lines, LABEL_LINES);
        float widest = 0;
        for (int k = 0; k < count; k++) {
            size_t n = strlen(lines[k]);
            while (n && lines[k][n - 1] == ' ') lines[k][--n] = 0; // the space the line broke at
            float w = MeasureTextEx(font, lines[k], size, 1).x;
            if (w > widest) widest = w;
        }
        if (count > 0) draw_band((Rectangle){x, y, widest, count * size}, scale);
        for (int k = 0; k < count; k++)
            draw_rimmed(font, lines[k], (Vector2){x, y + k * size}, size, WHITE, BLACK, scale);
    }
}
