// conversation.c
//
// EN: Dialogues: picking one, phrases with voice and subtitles, the menu of lines, descriptions.
// RU: Диалоги: выбор диалога, фразы с голосом и субтитрами, меню реплик, описания предметов.
#include "game.h"

// ---- dialogue ----

const PetkaDialogueVerb *find_dialogue(const Game *g, uint16_t obj_id, uint32_t key) {
    if (!g->dtree) return NULL;
    for (uint32_t t = 0; t < g->dtree->topic_count; t++) {
        const PetkaDialogueTopic *topic = &g->dtree->topics[t];
        if (topic->topic_id != obj_id) continue;
        const PetkaDialogueVerb *generic = NULL;
        for (uint32_t v = 0; v < topic->verb_count; v++) {
            if (!topic->verbs[v].entry_count) continue;
            if (topic->verbs[v].verb_id == key) return &topic->verbs[v];
            if (topic->verbs[v].verb_id == 0xFFFE) generic = &topic->verbs[v];
        }
        // 0xFFFE is the reply to any item with no line of its own.
        return (key & 0xFFFF) == PETKA_OP_OBJECTUSE && key > 0xFFFF ? generic : NULL;
    }
    return NULL;
}

int host_has_dialogue(void *user, uint16_t obj_id, uint32_t key) {
    return find_dialogue((Game *)user, obj_id, key) != NULL;
}

void host_select_dialogue(void *user, uint16_t obj_id, uint32_t key) {
    Game *g = (Game *)user;
    g->dialogue = find_dialogue(g, obj_id, key);
}

// Whoever speaks gets SAY, and SAID when he stops: heroes switch to their
// talking clip themselves, other objects do it in their SAY/SAID reactions.
// Both are delivered at once, not queued, like the exe's SAID at the end of
// a dialogue.
void set_talker(Game *g, uint16_t id) {
    if (g->talker == id) return;
    uint16_t prev = g->talker;
    g->talker = id;
    if (prev != PETKA_NONE) petka_scriptvm_send(g->vm, prev, PETKA_OP_SAID, 0, 0, 0, -1);
    if (id != PETKA_NONE) petka_scriptvm_send(g->vm, id, PETKA_OP_SAY, 0, 0, 0, -1);
}

// Where a speaker's voice sits between the speakers, -1 (left) to 1 (right).
// The exe (sub_40E0D0) gives DirectSound a pan of 2500 * (2x - w) / (2w)
// hundredths of a decibel, x being the middle of the speaker's clip and w
// the screen's width: up to 12.5 dB off the far channel at either edge.
float speaker_pan(const Game *g, uint16_t id) {
    const PetkaObject *obj = petka_script_find(g->script, id);
    if (!obj) return 0;
    const Visual *v = &g->visuals[obj_index(g, obj)];
    if (!v->live) return 0;
    float mid;
    if (is_hero(id)) {
        Rectangle d = hero_dest(g, id, &g->vm->state[obj_index(g, obj)], v);
        mid = d.x + d.width / 2;
    } else {
        mid = (float)(v->rect.left + (v->rect.right - v->rect.left) / 2);
    }
    const int width = 640;
    int x = (int)(mid - g->camera_x);
    int centibels = 2500 * (2 * x - width) / (2 * width);
    // As a balance: the far channel's gain is what DirectSound would leave.
    float far_gain = powf(10.0f, -(float)abs(centibels) / 2000.0f);
    float pan = 1.0f - far_gain;
    if (pan > 1.0f) pan = 1.0f;
    return centibels < 0 ? -pan : pan;
}

void next_phrase(Game *g) {
    if (g->voice.frameCount > 0) { StopSound(g->voice); UnloadSound(g->voice); g->voice = (Sound){0}; }
    g->voice_paused = 0;
    uint32_t idx;
    int step = petka_dialoguevm_next(&g->dvm, &idx);
    if (step == 2) {
        // The dialogue hands control to the script and waits for CONTINUE
        // (sub_409610): the speaker gets his SAID, then USERMSG is queued
        // for Chapayev with the number alone and no sender.
        set_talker(g, PETKA_NONE);
        g->talking = 0;
        g->paused = 1;
        g->line[0] = 0;
        petka_scriptvm_post(g->vm, PETKA_ID_CHAPAYEV, PETKA_OP_USERMSG, (uint16_t)idx, 0, 0, -1);
        return;
    }
    if (step == 3) {
        set_talker(g, PETKA_NONE);
        g->menu_count = petka_dialoguevm_choices(&g->dvm, g->menu, 16);
        if (g->menu_count > 16) g->menu_count = 16;
        g->line[0] = 0;
        if (g->auto_pick != -2 && g->menu_count > 0) {
            // Unattended run: answer at once.
            int pick = g->auto_pick >= 0 && g->auto_pick < g->menu_count ? g->auto_pick : g->menu_count - 1;
            fprintf(stderr, "menu: %d lines, picking %d\n", g->menu_count, pick);
            g->menu_count = 0;
            petka_dialoguevm_choose(&g->dvm, pick);
            next_phrase(g);
        }
        return;
    }
    if (step != 1 || idx >= g->phrases->count) {
        g->talking = 0;
        g->dialogue_done = 1;
        g->line[0] = 0;
        return;
    }
    const PetkaPhrase *ph = &g->phrases->phrases[idx];
    set_talker(g, ph->speaker <= 0xFFFE ? (uint16_t)ph->speaker : PETKA_NONE);
    cp1251_to_utf8(ph->text, g->line, sizeof(g->line));
    // Without a voice clip the line stays up for a second plus 30 ms a
    // character, as the exe's subtitle update has it (0x421170).
    // With the subtitles off there is nothing to wait for.
    g->line_until = game_now() + (g->subtitles ? 1.0 + 0.03 * (double)strlen(ph->text) : 0.0);
    if (g->fast) {
        g->line_until = game_now() + 0.1;
        const PetkaObject *who = petka_script_find(g->script, g->talker);
        fprintf(stderr, "say %.1fs [%s] pan %+.2f: %s\n", game_now(), who ? utf8_name(who->name) : "?",
                speaker_pan(g, g->talker), g->line);
    } else if (ph->wav_name[0]) {
        char wav[64];
        snprintf(wav, sizeof(wav), "%s\\%s", g->speech_prefix, ph->wav_name);
        size_t len = 0;
        const uint8_t *bytes = res_find(g, wav, &len);
        if (bytes) {
            Wave wave = LoadWaveFromMemory(".wav", bytes, (int)len);
            if (wave.frameCount > 0) {
                g->voice = LoadSoundFromWave(wave);
                SetSoundPan(g->voice, speaker_pan(g, g->talker));
                SetSoundVolume(g->voice, dial_gain(g, DIAL_SPEECH));
                PlaySound(g->voice);
            }
            UnloadWave(wave);
            res_release(g, wav);
        }
    }
}

void host_start_dialogue(void *user, uint16_t obj_id, uint16_t arg) {
    Game *g = (Game *)user;
    (void)obj_id; (void)arg;
    if (!g->dialogue || !g->phrases) { g->dialogue_done = 1; return; }
    petka_dialoguevm_start(&g->dvm, g->dtree, g->dialogue->entries[0].bytecode_offset);
    g->dialogue = NULL;
    g->talking = 1;
    g->paused = 0;
    g->menu_count = 0;
    next_phrase(g);
}
void draw_description(Game *g, Font font, float scale) {
    Visual *v = &g->desc_clip;
    if (v->resource != DESCRIPTION_CLIP) visual_load(g, v, DESCRIPTION_CLIP);
    if (v->flic && g->desc_frame >= 1 && g->desc_frame <= v->flic->frame_count) {
        if (v->flic->frame_index != g->desc_frame - 1) {
            for (int guard = 0; guard < v->flic->frame_count && v->flic->frame_index != g->desc_frame - 1; guard++)
                petka_flic_advance(v->flic);
            visual_upload(v);
        }
        visual_draw(v, (Rectangle){0, 0, v->flic->width * scale, v->flic->height * scale});
    }
    // The exe's font for this is the 16-pixel one.
    draw_wrapped(font, g->desc, 160 * scale, 275 * scale, 438 * scale, 16 * scale, 16 * scale, BLACK);
}
void menu_lines(const Game *g, Font font, float scale, Rectangle *rects, int hover, int draw) {
    enum { MAX_ROWS = 6 };
    static char lines[MENU_MAX][MAX_ROWS][512];
    int rows[MENU_MAX], total = 0;
    float size = 20 * scale;
    int count = g->menu_count < MENU_MAX ? g->menu_count : MENU_MAX;
    for (int i = 0; i < count; i++) {
        char text[512];
        cp1251_to_utf8(g->menu[i] < g->phrases->count ? g->phrases->phrases[g->menu[i]].text : "?", text, sizeof(text));
        rows[i] = wrap_text(font, text, size, 625 * scale, lines[i], MAX_ROWS);
        if (rows[i] < 1) rows[i] = 1;
        total += rows[i];
    }
    // The lines start at one x, the block they make sits in the middle of
    // the screen, as in the original.
    float widest = 0;
    for (int i = 0; i < count; i++)
        for (int r = 0; r < rows[i]; r++) {
            float w = MeasureTextEx(font, lines[i][r], size, 1).x;
            if (w > widest) widest = w;
        }
    float left = (GAME_W * scale - widest) / 2;
    if (left < 5 * scale) left = 5 * scale;
    float y = 479 * scale - (total * size + 4 * scale);
    if (draw && count > 0) draw_band((Rectangle){left, y, widest, total * size}, scale);
    for (int i = 0; i < count; i++) {
        rects[i] = (Rectangle){left, y, widest, rows[i] * size};
        for (int r = 0; draw && r < rows[i]; r++)
            draw_rimmed(font, lines[i][r], (Vector2){left, y + r * size}, size, i == hover ? BLUE_LINE : WHITE,
                        (Color){10, 10, 10, 255}, scale);
        y += rows[i] * size;
    }
}
