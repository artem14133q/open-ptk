// Cross-platform (desktop raylib+OpenGL today, Switch later) resource
// browser: cycles through every background BMP in a store (Left/Right),
// every FLIC clip in another store composited on top with MSK
// positioning/clipping and palette-index-0 color-key transparency
// (Up/Down; None is one of the cycle positions), and every WAV in a third
// store (PageUp/PageDown to select -- auto-plays on selection; Space to
// stop/replay). 1-4 picks a verb (LOOK/TAKE/USE/TALK), Enter "interacts"
// with the currently selected FLIC overlay (if it belongs to the current
// background and has a matching .MSK) via a heuristic topic mapping into
// PART_N/DIALOGUE.FIX+.LOD -- see build_object_list()'s comment and
// docs/str-format.md for why this numbering is an approximation, not the
// recovered original scheme.
#include "petka/bmp.h"
#include "petka/dialogue.h"
#include "petka/flic.h"
#include "petka/msk.h"
#include "petka/store.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const PetkaEntry **items;
    int count;
} EntryList;

static int ends_with_ci(const char *s, const char *suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return 0;
    const char *tail = s + (ls - lf);
    for (size_t i = 0; i < lf; i++) {
        char a = tail[i], b = suf[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

static EntryList filter_entries(const PetkaStore *store, const char *suffix) {
    EntryList list = {0};
    if (!store) return list;
    list.items = (const PetkaEntry **)malloc(sizeof(*list.items) * store->entry_count);
    for (uint32_t i = 0; i < store->entry_count; i++) {
        if (ends_with_ci(store->entries[i].name, suffix)) list.items[list.count++] = &store->entries[i];
    }
    return list;
}

static void make_msk_name(const char *flic_name, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", flic_name);
    size_t n = strlen(out);
    if (n >= 3) { out[n - 3] = 'M'; out[n - 2] = 'S'; out[n - 1] = 'K'; }
}

// Extracts "PART_N\XXXX.BG\" (everything up to and including the 2nd
// backslash) from an on-disk resource name.
static void folder_prefix(const char *name, char *out, size_t out_sz) {
    int backslashes = 0;
    size_t i = 0;
    for (; name[i] && i + 1 < out_sz; i++) {
        out[i] = name[i];
        if (name[i] == '\\') {
            backslashes++;
            if (backslashes == 2) { i++; break; }
        }
    }
    out[i] = 0;
}

// Heuristic object numbering (NOT the original engine's scheme -- that
// wasn't recovered, see docs/str-format.md): within one background folder,
// every FLC that has a matching MSK is a clickable "object"; we number
// them 0,1,2... in on-disk order and treat that as a PetkaDialogueTopic
// index. Good enough to demo the dialogue system against real objects.
typedef struct {
    const PetkaEntry **items;
    int count;
} ObjectList;

static ObjectList build_object_list(const PetkaStore *flic_store, const EntryList *flics, const char *prefix) {
    ObjectList list = {0};
    if (!flic_store) return list;
    list.items = (const PetkaEntry **)malloc(sizeof(*list.items) * (size_t)flics->count);
    size_t prefix_len = strlen(prefix);
    for (int i = 0; i < flics->count; i++) {
        const PetkaEntry *e = flics->items[i];
        if (strncmp(e->name, prefix, prefix_len) != 0) continue;
        char msk_name[512];
        make_msk_name(e->name, msk_name, sizeof(msk_name));
        if (petka_store_find(flic_store, msk_name)) list.items[list.count++] = e;
    }
    return list;
}

// Dialogue text is CP1251; raylib/DrawText expects UTF-8. Covers ASCII and
// the main Cyrillic letter block (U+0410-U+044F) plus Ё/ё -- good enough
// for dialogue content, which is overwhelmingly plain letters/punctuation.
static void cp1251_to_utf8(const char *in, char *out, size_t out_sz) {
    size_t oi = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && oi + 4 < out_sz; p++) {
        unsigned int cp;
        unsigned char c = *p;
        if (c < 0x80) cp = c;
        else if (c == 0xA8) cp = 0x0401; // Ё
        else if (c == 0xB8) cp = 0x0451; // ё
        else if (c >= 0xC0) cp = 0x0410 + (c - 0xC0); // А-я
        else cp = '?';
        if (cp < 0x80) {
            out[oi++] = (char)cp;
        } else if (cp < 0x800) {
            out[oi++] = (char)(0xC0 | (cp >> 6));
            out[oi++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[oi++] = (char)(0xE0 | (cp >> 12));
            out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[oi++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[oi] = 0;
}

// Default raylib font only has ASCII glyphs. Try a few common system fonts
// with Cyrillic coverage for desktop testing; TODO ship a bundled font for
// a platform-independent release (including Switch).
static Font load_cyrillic_font(void) {
    static const char *candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Tahoma.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    int codepoints[(0x7E - 0x20 + 1) + (0x4FF - 0x400 + 1)];
    int n = 0;
    for (int c = 0x20; c <= 0x7E; c++) codepoints[n++] = c;
    for (int c = 0x400; c <= 0x4FF; c++) codepoints[n++] = c;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!FileExists(candidates[i])) continue;
        Font f = LoadFontEx(candidates[i], 32, codepoints, n);
        if (f.glyphCount > 0) return f;
    }
    fprintf(stderr, "no Cyrillic-capable font found -- dialogue text will render as '?'\n");
    return GetFontDefault();
}

static uint8_t *flic_to_rgba(const PetkaFlic *flic) {
    int n = flic->width * flic->height;
    uint8_t *rgba = (uint8_t *)malloc((size_t)n * 4);
    for (int i = 0; i < n; i++) {
        uint8_t idx = flic->indices[i];
        rgba[i * 4 + 0] = flic->rgb[i * 3 + 0];
        rgba[i * 4 + 1] = flic->rgb[i * 3 + 1];
        rgba[i * 4 + 2] = flic->rgb[i * 3 + 2];
        rgba[i * 4 + 3] = (idx == 0) ? 0 : 255;
    }
    return rgba;
}

int main(int argc, char **argv) {
    const char *bg_store_path = argc > 1 ? argv[1] : "BGS1.STR";
    const char *flic_store_path = argc > 2 ? argv[2] : "data1_out/Flics/FLC1.STR";
    const char *sound_store_path = argc > 3 ? argv[3] : "MUSIC1.STR";
    const char *main_store_path = argc > 4 ? argv[4] : "data1_out/Main/MAIN.STR";
    const char *part = argc > 5 ? argv[5] : "PART_1";

    PetkaStore *bg_store = petka_store_open(bg_store_path);
    if (!bg_store) { fprintf(stderr, "failed to open store: %s\n", bg_store_path); return 1; }
    PetkaStore *flic_store = petka_store_open(flic_store_path);
    if (!flic_store) fprintf(stderr, "failed to open flic store: %s (overlays disabled)\n", flic_store_path);
    PetkaStore *sound_store = petka_store_open(sound_store_path);
    if (!sound_store) fprintf(stderr, "failed to open sound store: %s (audio disabled)\n", sound_store_path);

    EntryList bgs = filter_entries(bg_store, ".BMP");
    EntryList flics = filter_entries(flic_store, ".FLC");
    EntryList sounds = filter_entries(sound_store, ".WAV");
    if (bgs.count == 0) { fprintf(stderr, "no .BMP entries in %s\n", bg_store_path); return 1; }

    PetkaStore *main_store = petka_store_open(main_store_path);
    PetkaPhraseTable *phrases = NULL;
    PetkaDialogueTree *dtree = NULL;
    if (main_store) {
        char lod_name[256], fix_name[256];
        snprintf(lod_name, sizeof(lod_name), "%s\\DIALOGUE.LOD", part);
        snprintf(fix_name, sizeof(fix_name), "%s\\DIALOGUE.FIX", part);
        const PetkaEntry *lod_e = petka_store_find(main_store, lod_name);
        const PetkaEntry *fix_e = petka_store_find(main_store, fix_name);
        if (lod_e && fix_e) {
            size_t lod_len = 0, fix_len = 0;
            const uint8_t *lod_bytes = petka_store_entry_data(main_store, lod_e, &lod_len);
            const uint8_t *fix_bytes = petka_store_entry_data(main_store, fix_e, &fix_len);
            phrases = petka_phrasetable_open(lod_bytes, lod_len);
            dtree = petka_dialoguetree_open(fix_bytes, fix_len);
        }
        if (!phrases || !dtree) fprintf(stderr, "dialogue data unavailable for %s (interaction disabled)\n", part);
    }

    // QM_* message IDs (see docs/str-format.md) used as verb IDs.
    static const struct { uint32_t id; const char *label; } kVerbs[] = {
        {4, "LOOK"}, {6, "TAKE"}, {7, "USE"}, {10, "TALK"},
    };
    int verb_choice = 0;
    char dialogue_lines[4][256];
    int dialogue_line_count = 0;

    InitWindow(1280, 960, "petka-ptk1 browser");
    SetTargetFPS(60);
    InitAudioDevice();
    Font cyrillicFont = load_cyrillic_font();

    int bg_index = 0;
    int flic_index = -1; // -1 = no overlay
    Texture2D bg_tex = {0};
    Texture2D flic_tex = {0};
    PetkaFlic *flic = NULL;
    PetkaMskRect mrect = {0};
    int bg_width = 0, bg_height = 0;
    double next_frame_time = 0;

    int loaded_bg = -1, loaded_flic = -2; // force initial load

    int sound_index = -1; // -1 = none selected
    Sound current_sound = {0};
    int loaded_sound = -2;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_RIGHT)) bg_index = (bg_index + 1) % bgs.count;
        if (IsKeyPressed(KEY_LEFT)) bg_index = (bg_index - 1 + bgs.count) % bgs.count;
        if (flics.count > 0) {
            if (IsKeyPressed(KEY_UP)) flic_index = flic_index + 1 >= flics.count ? -1 : flic_index + 1;
            if (IsKeyPressed(KEY_DOWN)) flic_index = flic_index - 1 < -1 ? flics.count - 1 : flic_index - 1;
        }
        if (sounds.count > 0) {
            if (IsKeyPressed(KEY_PAGE_UP)) sound_index = sound_index + 1 >= sounds.count ? -1 : sound_index + 1;
            if (IsKeyPressed(KEY_PAGE_DOWN)) sound_index = sound_index - 1 < -1 ? sounds.count - 1 : sound_index - 1;
            if (IsKeyPressed(KEY_SPACE) && sound_index >= 0 && current_sound.frameCount > 0) {
                if (IsSoundPlaying(current_sound)) StopSound(current_sound);
                else PlaySound(current_sound);
            }
        }
        for (int k = 0; k < 4; k++) if (IsKeyPressed(KEY_ONE + k)) verb_choice = k;
        if (IsKeyPressed(KEY_ENTER) && dtree && phrases && flic_index >= 0) {
            char prefix[512];
            folder_prefix(bgs.items[bg_index]->name, prefix, sizeof(prefix));
            ObjectList objs = build_object_list(flic_store, &flics, prefix);
            int topic_idx = -1;
            for (int i = 0; i < objs.count; i++)
                if (objs.items[i] == flics.items[flic_index]) { topic_idx = i; break; }

            dialogue_line_count = 0;
            if (topic_idx < 0) {
                snprintf(dialogue_lines[0], sizeof(dialogue_lines[0]), "(select an object from this background first)");
                dialogue_line_count = 1;
            } else if ((uint32_t)topic_idx >= dtree->topic_count) {
                snprintf(dialogue_lines[0], sizeof(dialogue_lines[0]), "(topic %d out of range, only %u topics)", topic_idx, dtree->topic_count);
                dialogue_line_count = 1;
            } else {
                PetkaDialogueTopic *topic = &dtree->topics[topic_idx];
                uint32_t verb_id = kVerbs[verb_choice].id;
                PetkaDialogueVerb *verb = NULL;
                for (uint32_t v = 0; v < topic->verb_count; v++)
                    if (topic->verbs[v].verb_id == verb_id) { verb = &topic->verbs[v]; break; }
                if (!verb || verb->entry_count == 0) {
                    snprintf(dialogue_lines[0], sizeof(dialogue_lines[0]), "(no %s reaction for topic %d)", kVerbs[verb_choice].label, topic_idx);
                    dialogue_line_count = 1;
                } else {
                    PetkaDialogueVM vm;
                    petka_dialoguevm_start(&vm, dtree, verb->entries[0].bytecode_offset);
                    uint32_t phrase_idx;
                    while (dialogue_line_count < 4 && petka_dialoguevm_next(&vm, &phrase_idx)) {
                        const char *text = phrase_idx < phrases->count ? phrases->phrases[phrase_idx].text : "<?>";
                        snprintf(dialogue_lines[dialogue_line_count], sizeof(dialogue_lines[0]), "%s", text);
                        dialogue_line_count++;
                    }
                }
            }
            free(objs.items);
        }

        if (bg_index != loaded_bg) {
            const PetkaEntry *e = bgs.items[bg_index];
            size_t len = 0;
            const uint8_t *bytes = petka_store_entry_data(bg_store, e, &len);
            int w = 0, h = 0;
            uint8_t *rgb = petka_bmp_decode(bytes, len, &w, &h);
            if (rgb) {
                if (bg_tex.id) UnloadTexture(bg_tex);
                Image img = {.data = rgb, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8};
                bg_tex = LoadTextureFromImage(img);
                UnloadImage(img);
                bg_width = w;
                bg_height = h;
            } else {
                fprintf(stderr, "petka_bmp_decode failed for %s\n", e->name);
            }
            loaded_bg = bg_index;
        }

        if (flic_index != loaded_flic) {
            if (flic) { petka_flic_close(flic); flic = NULL; }
            if (flic_tex.id) { UnloadTexture(flic_tex); flic_tex = (Texture2D){0}; }
            if (flic_index >= 0) {
                const PetkaEntry *e = flics.items[flic_index];
                size_t len = 0;
                const uint8_t *bytes = petka_store_entry_data(flic_store, e, &len);
                flic = petka_flic_open(bytes, len);
                if (flic) {
                    mrect = (PetkaMskRect){0, 0, flic->width, flic->height};
                    char msk_name[512];
                    make_msk_name(e->name, msk_name, sizeof(msk_name));
                    const PetkaEntry *msk_entry = petka_store_find(flic_store, msk_name);
                    if (msk_entry) {
                        size_t msk_len = 0;
                        const uint8_t *msk_bytes = petka_store_entry_data(flic_store, msk_entry, &msk_len);
                        petka_msk_rect(msk_bytes, msk_len, &mrect);
                    }
                    uint8_t *rgba = flic_to_rgba(flic);
                    Image img = {.data = rgba, .width = flic->width, .height = flic->height, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                    flic_tex = LoadTextureFromImage(img);
                    UnloadImage(img);
                    next_frame_time = GetTime();
                } else {
                    fprintf(stderr, "petka_flic_open failed for %s\n", e->name);
                }
            }
            loaded_flic = flic_index;
        }

        if (sound_index != loaded_sound) {
            if (current_sound.frameCount > 0) { UnloadSound(current_sound); current_sound = (Sound){0}; }
            if (sound_index >= 0) {
                const PetkaEntry *e = sounds.items[sound_index];
                size_t len = 0;
                const uint8_t *bytes = petka_store_entry_data(sound_store, e, &len);
                Wave wave = LoadWaveFromMemory(".wav", bytes, (int)len);
                if (wave.frameCount > 0) {
                    current_sound = LoadSoundFromWave(wave);
                    PlaySound(current_sound);
                } else {
                    fprintf(stderr, "LoadWaveFromMemory failed for %s\n", e->name);
                }
                UnloadWave(wave);
            }
            loaded_sound = sound_index;
        }

        if (flic && GetTime() >= next_frame_time) {
            petka_flic_advance(flic);
            uint8_t *rgba = flic_to_rgba(flic);
            UpdateTexture(flic_tex, rgba);
            free(rgba);
            next_frame_time = GetTime() + flic->speed_ms / 1000.0;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle dst = {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
        if (bg_tex.id) DrawTexturePro(bg_tex, (Rectangle){0, 0, (float)bg_tex.width, (float)bg_tex.height}, dst, (Vector2){0, 0}, 0, WHITE);
        if (flic && bg_width > 0) {
            float sx = (float)GetScreenWidth() / (float)bg_width;
            float sy = (float)GetScreenHeight() / (float)bg_height;
            Rectangle src = {(float)mrect.left, (float)mrect.top,
                              (float)(mrect.right - mrect.left), (float)(mrect.bottom - mrect.top)};
            Rectangle clip = {src.x * sx, src.y * sy, src.width * sx, src.height * sy};
            DrawTexturePro(flic_tex, src, clip, (Vector2){0, 0}, 0, WHITE);
        }

        if (dtree) {
            DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
            DrawText(TextFormat("verb [%s]  (1=LOOK 2=TAKE 3=USE 4=TALK, ENTER=interact)", kVerbs[verb_choice].label),
                      10, 8, 18, ORANGE);
            if (dialogue_line_count > 0) {
                DrawRectangle(0, 34, GetScreenWidth(), 18 * dialogue_line_count + 10, Fade(BLACK, 0.6f));
                char utf8[1024];
                for (int i = 0; i < dialogue_line_count; i++) {
                    cp1251_to_utf8(dialogue_lines[i], utf8, sizeof(utf8));
                    DrawTextEx(cyrillicFont, utf8, (Vector2){10, (float)(38 + 18 * i)}, 20, 1, WHITE);
                }
            }
        }

        DrawRectangle(0, GetScreenHeight() - 84, GetScreenWidth(), 84, Fade(BLACK, 0.6f));
        DrawText(TextFormat("[%d/%d] %s", bg_index + 1, bgs.count, bgs.items[bg_index]->name), 10, GetScreenHeight() - 74, 18, LIME);
        DrawText(flic_index >= 0 ? TextFormat("[%d/%d] %s", flic_index + 1, flics.count, flics.items[flic_index]->name) : "(no overlay)",
                  10, GetScreenHeight() - 50, 18, YELLOW);
        DrawText(sound_index >= 0
                      ? TextFormat("[%d/%d] %s %s", sound_index + 1, sounds.count, sounds.items[sound_index]->name,
                                   current_sound.frameCount > 0 && IsSoundPlaying(current_sound) ? "(playing)" : "(stopped)")
                      : "(no sound)",
                  10, GetScreenHeight() - 26, 18, SKYBLUE);
        EndDrawing();
    }

    if (flic) petka_flic_close(flic);
    if (bg_tex.id) UnloadTexture(bg_tex);
    if (flic_tex.id) UnloadTexture(flic_tex);
    if (current_sound.frameCount > 0) UnloadSound(current_sound);
    if (cyrillicFont.texture.id != GetFontDefault().texture.id) UnloadFont(cyrillicFont);
    CloseAudioDevice();
    CloseWindow();

    free(bgs.items);
    free(flics.items);
    free(sounds.items);
    if (dtree) petka_dialoguetree_close(dtree);
    if (phrases) petka_phrasetable_close(phrases);
    if (main_store) petka_store_close(main_store);
    if (flic_store) petka_store_close(flic_store);
    if (sound_store) petka_store_close(sound_store);
    petka_store_close(bg_store);
    return 0;
}
