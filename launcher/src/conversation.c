// conversation.c
//
// EN: Dialogues: picking one, phrases with voice and subtitles, the menu of lines, descriptions.
// RU: Диалоги: выбор диалога, фразы с голосом и субтитрами, меню реплик, описания предметов.
#include "game.h"

// ---- dialogue ----

// The dialogue for a message to an object: the core looks it up as the
// exe does (petka_dialoguetree_find), the stock replies included.
const PetkaDialogueVerb *find_dialogue(const Game *g, uint16_t obj_id, uint32_t key) {
    return petka_dialoguetree_find(g->dtree, obj_id, key, NULL);
}

int host_has_dialogue(void *user, uint16_t obj_id, uint32_t key) {
    int generic = 0;
    const PetkaDialogueVerb *verb = petka_dialoguetree_find(((Game *)user)->dtree, obj_id, key, &generic);
    return !verb ? 0 : generic ? 2 : 1;
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

// The names subtitles go under. The script's objects that speak are the
// characters in a room, a sequence or a pose ("Петька - КОМНАТА ПЕТЬКИ",
// "ВИЧ на стуле", "Grass1- Seq - ПОДВОДНЫЙ МИР" is Petka): the character's
// own name for each, by the part. The heroes are the same in every part;
// any other speaker goes by his object's name.
static const struct {
    uint8_t part; // 0: every part
    uint16_t id;
    const char *name;
} kSpeakers[] = {
    {0, 0, "Петька"}, {0, 1, "Василий Иванович"},
    {1, 85, "Петька"}, {1, 86, "Василий Иванович"}, {1, 197, "Алкаш"}, {1, 198, "Красноармеец"},
    {1, 199, "Красноармеец"}, {1, 200, "Красноармеец"}, {1, 201, "Красноармеец"}, {1, 202, "Красноармеец"},
    {1, 305, "Пасечник"}, {1, 307, "Пасечник"}, {1, 364, "Солдаты"}, {1, 365, "Солдаты"},
    {1, 369, "Петька"}, {1, 422, "Белогвардеец"}, {1, 423, "Белогвардеец"}, {1, 428, "Петька"},
    {1, 434, "Анка"},
    {2, 111, "Петька"}, {2, 113, "Василий Иванович"}, {2, 131, "Петька"}, {2, 148, "Анка"},
    {2, 151, "Петька"}, {2, 152, "Василий Иванович"}, {2, 200, "Фотограф"}, {2, 201, "Фотомодель"},
    {2, 202, "Фотомодель"}, {2, 261, "Качковский"}, {2, 307, "Качковский"}, {2, 308, "Поп"},
    {2, 310, "Бармен"}, {2, 316, "Анка"}, {2, 367, "Белый генерал"}, {2, 426, "Клерк"},
    {2, 599, "Поп"}, {2, 678, "Анка"},
    {3, 58, "Фурманов"}, {3, 59, "Инопланетянин"}, {3, 103, "Петька"}, {3, 104, "Василий Иванович"},
    {3, 140, "Фурманов"}, {3, 212, "Бармен"}, {3, 275, "Лаборанты"}, {3, 309, "Петька"},
    {3, 310, "Василий Иванович"}, {3, 311, "Лже-Фурманов"}, {3, 325, "Лже-Фурманов"}, {3, 356, "Петька"},
    {3, 357, "Василий Иванович"},
};

const char *speaker_name(const Game *g, uint16_t id) {
    for (size_t i = 0; i < sizeof(kSpeakers) / sizeof(kSpeakers[0]); i++)
        if ((kSpeakers[i].part == 0 || kSpeakers[i].part == g->part_number) && kSpeakers[i].id == id)
            return kSpeakers[i].name;
    const PetkaObject *obj = id == PETKA_NONE ? NULL : petka_script_find(g->script, id);
    return obj ? utf8_name(obj->name) : "";
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
    uint32_t idx = 0;
    int step = g->phrases_only ? 0 : petka_dialoguevm_next(&g->dvm, &idx);
    // A split restored dialogue (restored.h): its second part passes over
    // the phrases the first has said; the first ends where the second begins.
    while (step == 1 && g->say_skip > 0) {
        g->say_skip--;
        step = petka_dialoguevm_next(&g->dvm, &idx);
    }
    if (step == 1 && g->say_limit == 0) step = 0;
    if (step == 1 && g->say_limit > 0) g->say_limit--;
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
    // Over -- but a restored line may have phrases to say after it
    // (restored.c's `then`), before the script goes on.
    if ((step != 1 || idx >= g->phrases->count) && g->then_pos < g->then_count) {
        idx = g->then_phrase[g->then_pos++];
        step = 1;
    }
    if (step != 1 || idx >= g->phrases->count) {
        g->then_count = g->then_pos = 0;
        g->phrases_only = 0;
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
    // A restored line put after the action (scriptfix.c): not said with the switch off.
    if (arg == RESTORED_LATE && !g->opt_restored) g->dialogue = NULL;
    // Phrases where the script says nothing (restored.h): no dialogue, the
    // phrases alone, one after another.
    if (arg == RESTORED_PHRASES) {
        g->then_count = g->opt_restored ? restored_phrases(g, obj_id, g->then_phrase, 4) : 0;
        g->then_pos = 0;
        g->say_limit = -1;
        g->say_skip = 0;
        if (!g->then_count || !g->phrases) { g->dialogue_done = 1; return; }
        g->phrases_only = 1;
        g->talking = 1;
        g->paused = 0;
        g->menu_count = 0;
        next_phrase(g);
        return;
    }
    // A restored line in two parts round the action: its own dialogue, the
    // first phrases or the rest.
    const PetkaDialogueVerb *dialogue = g->dialogue;
    g->say_limit = -1;
    g->say_skip = 0;
    if (arg == RESTORED_FIRST || arg == RESTORED_REST) {
        int first = 0;
        dialogue = g->opt_restored ? restored_split(g, obj_id, &first) : NULL;
        if (arg == RESTORED_FIRST) g->say_limit = first;
        else g->say_skip = first;
    } else {
        g->dialogue = NULL;
    }
    if (!dialogue || !g->phrases) { g->dialogue_done = 1; return; }
    petka_dialoguevm_start(&g->dvm, g->dtree, dialogue->entries[0].bytecode_offset);
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
        // Each line after a dash, as a line of talk is written.
        char text[512] = "- ";
        cp1251_to_utf8(g->menu[i] < g->phrases->count ? g->phrases->phrases[g->menu[i]].text : "?", text + 2,
                       sizeof(text) - 2);
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
