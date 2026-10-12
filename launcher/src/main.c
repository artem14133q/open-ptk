// Cross-platform (desktop raylib+OpenGL today, Switch later) viewer: cycles
// through every background BMP in a store (Left/Right), renders every
// standing object in the current room simultaneously (see
// build_object_list()'s comment), and plays every WAV in a third store
// (PageUp/PageDown to select -- auto-plays on selection; Space to
// stop/replay). 1-4 picks a verb (LOOK/TAKE/USE/TALK). An object can be
// selected either with the mouse (hover highlights it, left-click
// interacts) or the keyboard (Up/Down to select, Enter to interact); both
// drive the same heuristic topic mapping into PART_N/DIALOGUE.FIX+.LOD --
// see build_object_list()'s comment and docs/str-format.md for why this
// numbering is an approximation, not the recovered original scheme.
// Likewise, room-graph navigation (per PART_N/BGS.INI, right-hand panel)
// works by mouse (click an exit's line) or keyboard (TAB to highlight an
// exit, G to walk through it) -- only if its target room has a known .BG
// mapping (see kRoomFolders -- deliberately incomplete, many rooms show
// "(?)").
#include "petka/bmp.h"
#include "petka/dialogue.h"
#include "petka/flic.h"
#include "petka/msk.h"
#include "petka/store.h"
#include "petka/topology.h"

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

// Extracts just the "XXXX" folder name (no "PART_N\" prefix, no ".BG\"
// suffix) from an on-disk resource name like "PART_1\BOLOTO.BG\BOLOTO.BMP".
static void bg_folder_only(const char *entry_name, char *out, size_t out_sz) {
    char prefix[512];
    folder_prefix(entry_name, prefix, sizeof(prefix));
    const char *p = strchr(prefix, '\\');
    out[0] = 0;
    if (!p) return;
    p++;
    const char *dot = strstr(p, ".BG");
    size_t n = dot ? (size_t)(dot - p) : strlen(p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

// BGS.INI room names (Cyrillic, the game's own display names) <-> .BG
// resource folder names. NOT a recovered mapping -- no such table was
// found anywhere in the data (checked RESOURCE.QRC, BGDATA.DAT's "KARTA"
// map-screen text). Built by hand from transliteration + content
// inspection of each folder's FLC object names in FLC1.STR. All 28 of
// PART_1's rooms are covered; most are high-confidence (direct
// transliteration, or an unambiguous object name), a few are best-effort
// guesses noted inline. Written as UTF-8 and compared against topology
// room names (CP1251) via cp1251_to_utf8() below.
typedef struct {
    const char *room_utf8;
    const char *folder;
} RoomFolderMap;
static const RoomFolderMap kRoomFolders[] = {
    // High confidence: direct transliteration.
    {"БАНЯ", "BANIA"},
    {"БОЛОТО", "BOLOTO"},
    {"ВРЕМЯНКА", "VREMYNKA"},
    {"ХУТОР", "HUTOR"},
    {"ОКНО", "OKNO"},
    {"ПАЛЬМА", "PALMA"},
    {"ПАСЕКА", "PASEKA"},
    {"САРАЙ", "SARAY"},
    {"МОСТИК У ДОМА ФУРМАНОВА", "MOSTIK"},       // мостик = little bridge
    {"ПЕНЕК В ЛЕСУ", "PENEK"},                    // пенек = stump
    {"ДОРОГА ВОКРУГ ХЛЕВА", "HLEV"},              // хлев = cowshed
    {"ПЕРЕКРЕСТОК В ЛЕСУ", "FOUR-WAY"},           // crossroads = four-way
    // High confidence: constructed English name or unambiguous object match.
    {"КОМНАТА АНКИ", "ANKAROOM"},                 // "Anka's Room"
    {"ДОМ АНКИ", "ANHUTOR"},                      // "An(ka)" + "Hutor"
    {"ТРАНСФОРМАТОР", "DEREVO"},                  // DEREVO.BG/POD-TOK has
                                                   // ELEKTRO1-6/TOK-L/S/P
                                                   // (ток=electric current)
    {"КЛУБ", "DK"},                               // Дом Культуры = the "club"
    {"АКТОВЫЙ ЗАЛ В КЛУБЕ", "ZAL-DK"},            // "Зал" (hall) + "ДК"
    {"КОМНАТА ФУРМАНОВА", "SHWARC"},              // SHWARC.BG/FURMANOF/*.FLC
    {"ДОМ ПЕТЬКИ", "LENIN"},                      // LENIN.BG has VHOD-B
                                                   // (entrance-to-bathhouse)
                                                   // + OUTBANIA, matching
                                                   // DOM PETKI's БАНЯ exit
    {"КОМНАТА ПЕТЬКИ", "P-ROOM"},                 // "Petka's Room"; has the
                                                   // aquarium/fish/key props
                                                   // from the dialogue text
    {"ПОЛЕ", "POKOSHEN"},                         // покос = haymaking; has
                                                   // STOG (haystack) objects
    {"СВАЛКА", "WZRIV"},                          // has MAGNIT (scrapyard
                                                   // magnet) + a PASEK
                                                   // object, matching
                                                   // SVALKA's ПАСЕКА exit
    {"МОСТ", "SVOLOCHI"},                         // has MOST.FLC/AN-MOST
                                                   // (bridge to Anka)
    {"ПЕРЕД МАГАЗИНОМ", "SHOP"},                  // SHOP.BG/ALKISHI =
                                                   // drunkards outside
    {"МАГАЗИН ВНУТРИ", "SHOPPING"},               // SHOPPING.BG/TETKA =
                                                   // shopkeeper lady inside
    {"ДОМ ФУРМАНОВА", "DROVA"},                   // well+woodpile yard;
                                                   // placed by elimination
                                                   // next to SHWARC/MOSTIK
    // Low confidence -- genuine coin flip, no distinguishing evidence found:
    {"ЛЕСНАЯ ДОРОГА", "ROAD-L"},
    {"ЛЕСНАЯ ДОРОГА - 2", "ROAD-R"},
};
static const int kRoomFolderCount = sizeof(kRoomFolders) / sizeof(kRoomFolders[0]);

static const char *room_to_folder(const char *room_utf8) {
    for (int i = 0; i < kRoomFolderCount; i++)
        if (strcmp(kRoomFolders[i].room_utf8, room_utf8) == 0) return kRoomFolders[i].folder;
    return NULL;
}

static const char *folder_to_room(const char *folder) {
    for (int i = 0; i < kRoomFolderCount; i++)
        if (strcmp(kRoomFolders[i].folder, folder) == 0) return kRoomFolders[i].room_utf8;
    return NULL;
}

// Heuristic object numbering (NOT the original engine's scheme -- that
// wasn't recovered, see docs/str-format.md): within one background folder,
// every FLC that (a) sits directly in the "XXXX.BG\" folder -- not in a
// nested subfolder -- and (b) has a matching MSK, is treated as one of the
// scene's standing/idle objects, numbered 0,1,2... in on-disk order and
// used as both the PetkaDialogueTopic index and the thing we render
// automatically when a background loads.
//
// The "no nested subfolder" rule matters: those subfolders (ACTIV-XX.YYY,
// POD-TOK, KOLODEC, RAZGOVOR, ...) hold cutscene/reaction animation
// fragments (e.g. a conversation broken into PET-ANK0..PET-ANK5 clips)
// meant to play one at a time when triggered, not simultaneously -- lumping
// them in with the always-visible props would turn busy rooms into visual
// noise instead of a recognizable scene.
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
        if (strchr(e->name + prefix_len, '\\') != NULL) continue; // nested subfolder: skip
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
    const char *speech_store_path = argc > 6 ? argv[6] : "SPEECH1.STR";

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

    PetkaTopology *topo = NULL;
    if (main_store) {
        char bgsini_name[256];
        snprintf(bgsini_name, sizeof(bgsini_name), "%s\\BGS.INI", part);
        const PetkaEntry *bgsini_e = petka_store_find(main_store, bgsini_name);
        if (bgsini_e) {
            size_t bgsini_len = 0;
            const uint8_t *bgsini_bytes = petka_store_entry_data(main_store, bgsini_e, &bgsini_len);
            topo = petka_topology_parse(bgsini_bytes, bgsini_len);
        }
        if (!topo) fprintf(stderr, "topology unavailable for %s (navigation disabled)\n", part);
    }
    int exit_choice = 0;

    PetkaStore *speech_store = petka_store_open(speech_store_path);
    if (!speech_store) fprintf(stderr, "failed to open speech store: %s (voice playback disabled)\n", speech_store_path);
    // Phrase wav_name is a bare filename ("11_01701.wav"); the store holds
    // it as "SPEECHn\11_01701.WAV". n matches the part's trailing digit.
    char speech_prefix[32] = "SPEECH1";
    {
        const char *digits = part;
        while (*digits && (*digits < '0' || *digits > '9')) digits++;
        if (*digits) snprintf(speech_prefix, sizeof(speech_prefix), "SPEECH%s", digits);
    }
    Sound voice_sound = {0};

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

    // Title/start menu. Background is the game's own MAIN\INTRFACE.BG\
    // INTRFASE.BMP (found in MAIN.STR). NOT using that folder's button
    // FLC/MSK pairs for the buttons themselves, though: their MSK rects
    // turn out to be calibrated for a different composite screen (this one
    // background is shared across main menu/options/save-load, all
    // overlaid in the raw asset), so drawing them at face value produces
    // an unreadable overlapping mess. Simpler and more honest to lay out
    // our own plain-text menu over the backdrop than fake a pixel-perfect
    // reconstruction we don't actually have the data for. Up/Down selects,
    // Enter confirms. CONTINUE/LOAD are real entries but there's no save
    // system yet, so they just report that rather than doing nothing.
    if (main_store) {
        typedef struct {
            const char *label_utf8;
            int is_stub;
        } MenuItem;
        MenuItem items[] = {
            {"Новая игра", 0},
            {"Продолжить", 1},
            {"Загрузить", 1},
            {"Выход", 0},
        };
        const int item_count = (int)(sizeof(items) / sizeof(items[0]));

        const PetkaEntry *menu_bg_e = petka_store_find(main_store, "MAIN\\INTRFACE.BG\\INTRFASE.BMP");
        Texture2D menu_bg_tex = {0};
        if (menu_bg_e) {
            size_t blen = 0;
            const uint8_t *bbytes = petka_store_entry_data(main_store, menu_bg_e, &blen);
            int mw = 0, mh = 0;
            uint8_t *rgb = petka_bmp_decode(bbytes, blen, &mw, &mh);
            if (rgb) {
                Image img = {.data = rgb, .width = mw, .height = mh, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8};
                menu_bg_tex = LoadTextureFromImage(img);
                UnloadImage(img);
            }
        }

        int selected = 0;
        char menu_status[256] = "";
        int entered_game = 0;
        while (!WindowShouldClose() && !entered_game) {
            if (IsKeyPressed(KEY_DOWN)) { selected = (selected + 1) % item_count; menu_status[0] = 0; }
            if (IsKeyPressed(KEY_UP)) { selected = (selected - 1 + item_count) % item_count; menu_status[0] = 0; }
            if (IsKeyPressed(KEY_ENTER)) {
                if (items[selected].is_stub) {
                    snprintf(menu_status, sizeof(menu_status), "%s: not implemented yet (no save system)", items[selected].label_utf8);
                } else if (selected == item_count - 1) { // "Выход"
                    CloseWindow();
                    return 0;
                } else {
                    entered_game = 1;
                }
            }

            BeginDrawing();
            ClearBackground(BLACK);
            Rectangle dst = {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
            if (menu_bg_tex.id) DrawTexturePro(menu_bg_tex, (Rectangle){0, 0, (float)menu_bg_tex.width, (float)menu_bg_tex.height}, dst, (Vector2){0, 0}, 0, WHITE);

            int panel_w = 320, panel_h = item_count * 44 + 20;
            int px = GetScreenWidth() / 2 - panel_w / 2, py = GetScreenHeight() / 2 - panel_h / 2;
            DrawRectangle(px, py, panel_w, panel_h, Fade(BLACK, 0.65f));
            for (int i = 0; i < item_count; i++) {
                Color c = (i == selected) ? ORANGE : WHITE;
                const char *prefix = (i == selected) ? "> " : "  ";
                DrawTextEx(cyrillicFont, TextFormat("%s%s", prefix, items[i].label_utf8),
                           (Vector2){(float)(px + 16), (float)(py + 10 + i * 44)}, 28, 1, c);
            }
            if (menu_status[0]) DrawTextEx(cyrillicFont, menu_status, (Vector2){10, (float)(GetScreenHeight() - 30)}, 16, 1, RED);
            EndDrawing();
        }

        if (menu_bg_tex.id) UnloadTexture(menu_bg_tex);
        if (!entered_game) { CloseWindow(); return 0; } // window closed mid-menu
    }

    // BGS.INI's StartRoom= is "Seq - INTRO0", an intro cutscene sequence
    // (not a room) that we don't play; the first real interactive room is
    // КОМНАТА ПЕТЬКИ ("Petka's room", the one with the aquarium) -- see its
    // P-ROOM entry in kRoomFolders. Default to it instead of whatever .BMP
    // happens to sort first in the store.
    int bg_index = 0;
    for (int i = 0; i < bgs.count; i++) {
        char folder[64];
        bg_folder_only(bgs.items[i]->name, folder, sizeof(folder));
        if (strcmp(folder, "P-ROOM") == 0) { bg_index = i; break; }
    }
    Texture2D bg_tex = {0};
    int bg_width = 0, bg_height = 0;

    // All of the current room's standing/idle objects (see
    // build_object_list's comment), rendered simultaneously -- not just one
    // picked by hand -- so the assembled scene actually looks like the
    // room, not a background with everything but the background missing.
#define MAX_SCENE_OBJECTS 32
    PetkaFlic *scene_flic[MAX_SCENE_OBJECTS] = {0};
    Texture2D scene_tex[MAX_SCENE_OBJECTS] = {0};
    PetkaMskRect scene_rect[MAX_SCENE_OBJECTS] = {0};
    double scene_next_frame[MAX_SCENE_OBJECTS] = {0};
    char scene_name[MAX_SCENE_OBJECTS][128] = {{0}};
    int scene_count = 0;
    int selected_object = -1; // index into the scene objects above, for ENTER

    int loaded_bg = -1; // force initial load (also rebuilds scene objects)

    int sound_index = -1; // -1 = none selected
    Sound current_sound = {0};
    int loaded_sound = -2;

    char nav_status[256] = "";

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_RIGHT)) bg_index = (bg_index + 1) % bgs.count;
        if (IsKeyPressed(KEY_LEFT)) bg_index = (bg_index - 1 + bgs.count) % bgs.count;

        // Room-graph navigation: TAB cycles the current room's exits, G
        // walks through the highlighted one (if its target has a known
        // .BG mapping -- see kRoomFolders).
        const PetkaRoom *current_room = NULL;
        if (topo) {
            char folder[64];
            bg_folder_only(bgs.items[bg_index]->name, folder, sizeof(folder));
            const char *room_utf8 = folder_to_room(folder);
            if (room_utf8) {
                for (uint32_t i = 0; i < topo->room_count && !current_room; i++) {
                    char conv[256];
                    cp1251_to_utf8(topo->rooms[i].name, conv, sizeof(conv));
                    if (strcmp(conv, room_utf8) == 0) current_room = &topo->rooms[i];
                }
            }
        }
        if (current_room && current_room->exit_count > 0) {
            if (IsKeyPressed(KEY_TAB)) exit_choice = (exit_choice + 1) % (int)current_room->exit_count;

            // Mouse: clicking an exit's line in the room panel (same layout
            // as the panel drawing code below) selects it and goes, in one
            // click -- no need for TAB first.
            int go_requested = IsKeyPressed(KEY_G);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int panel_w = 360;
                int px = GetScreenWidth() - panel_w;
                Vector2 m = GetMousePosition();
                for (uint32_t i = 0; i < current_room->exit_count; i++) {
                    Rectangle row = {(float)px, (float)(34 + 24 * i), (float)panel_w, 22};
                    if (CheckCollisionPointRec(m, row)) {
                        exit_choice = (int)i;
                        go_requested = 1;
                        break;
                    }
                }
            }
            if (go_requested) {
                const PetkaExit *ex = &current_room->exits[exit_choice];
                char target_utf8[256];
                cp1251_to_utf8(ex->target, target_utf8, sizeof(target_utf8));
                const char *target_folder = room_to_folder(target_utf8);
                int found = -1;
                if (target_folder) {
                    for (int i = 0; i < bgs.count && found < 0; i++) {
                        char f[64];
                        bg_folder_only(bgs.items[i]->name, f, sizeof(f));
                        if (strcmp(f, target_folder) == 0) found = i;
                    }
                }
                if (found >= 0) {
                    bg_index = found;
                    nav_status[0] = 0;
                } else {
                    snprintf(nav_status, sizeof(nav_status), "no mapped background for \"%s\" yet", target_utf8);
                }
            }
        } else {
            exit_choice = 0;
        }
        if (scene_count > 0) {
            if (IsKeyPressed(KEY_UP)) selected_object = selected_object + 1 >= scene_count ? -1 : selected_object + 1;
            if (IsKeyPressed(KEY_DOWN)) selected_object = selected_object - 1 < -1 ? scene_count - 1 : selected_object - 1;
        }

        // Mouse: hovering a scene object's MSK rect selects it (same
        // highlight Up/Down gives); left-click interacts with it using the
        // current verb, same as Enter.
        int hovered_object = -1;
        if (bg_width > 0 && scene_count > 0) {
            float sx = (float)GetScreenWidth() / (float)bg_width;
            float sy = (float)GetScreenHeight() / (float)bg_height;
            Vector2 m = GetMousePosition();
            for (int i = 0; i < scene_count; i++) {
                if (!scene_flic[i]) continue;
                Rectangle clip = {scene_rect[i].left * sx, scene_rect[i].top * sy,
                                   (scene_rect[i].right - scene_rect[i].left) * sx,
                                   (scene_rect[i].bottom - scene_rect[i].top) * sy};
                if (CheckCollisionPointRec(m, clip)) { hovered_object = i; break; }
            }
        }
        if (hovered_object >= 0) selected_object = hovered_object;
        int interact_requested = IsKeyPressed(KEY_ENTER) ||
                                  (hovered_object >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        if (sounds.count > 0) {
            if (IsKeyPressed(KEY_PAGE_UP)) sound_index = sound_index + 1 >= sounds.count ? -1 : sound_index + 1;
            if (IsKeyPressed(KEY_PAGE_DOWN)) sound_index = sound_index - 1 < -1 ? sounds.count - 1 : sound_index - 1;
            if (IsKeyPressed(KEY_SPACE) && sound_index >= 0 && current_sound.frameCount > 0) {
                if (IsSoundPlaying(current_sound)) StopSound(current_sound);
                else PlaySound(current_sound);
            }
        }
        for (int k = 0; k < 4; k++) if (IsKeyPressed(KEY_ONE + k)) verb_choice = k;
        if (interact_requested && dtree && phrases && selected_object >= 0) {
            int topic_idx = selected_object; // same ordering build_object_list produced below

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
                    int first_phrase = -1;
                    while (dialogue_line_count < 4 && petka_dialoguevm_next(&vm, &phrase_idx)) {
                        const char *text = phrase_idx < phrases->count ? phrases->phrases[phrase_idx].text : "<?>";
                        snprintf(dialogue_lines[dialogue_line_count], sizeof(dialogue_lines[0]), "%s", text);
                        dialogue_line_count++;
                        if (first_phrase < 0) first_phrase = (int)phrase_idx;
                    }

                    if (voice_sound.frameCount > 0) { StopSound(voice_sound); UnloadSound(voice_sound); voice_sound = (Sound){0}; }
                    if (speech_store && first_phrase >= 0 && (uint32_t)first_phrase < phrases->count) {
                        char wav_name[64];
                        snprintf(wav_name, sizeof(wav_name), "%s\\%s", speech_prefix, phrases->phrases[first_phrase].wav_name);
                        for (char *p = wav_name; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
                        const PetkaEntry *voice_e = petka_store_find(speech_store, wav_name);
                        if (voice_e) {
                            size_t vlen = 0;
                            const uint8_t *vbytes = petka_store_entry_data(speech_store, voice_e, &vlen);
                            Wave wave = LoadWaveFromMemory(".wav", vbytes, (int)vlen);
                            if (wave.frameCount > 0) { voice_sound = LoadSoundFromWave(wave); PlaySound(voice_sound); }
                            UnloadWave(wave);
                        } else {
                            fprintf(stderr, "voice clip not found: %s\n", wav_name);
                        }
                    }
                }
            }
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
            // Rebuild the scene's standing objects for the new background
            // (see build_object_list's comment: top-level FLC+MSK pairs in
            // this room's folder, all rendered at once).
            for (int i = 0; i < scene_count; i++) {
                if (scene_flic[i]) petka_flic_close(scene_flic[i]);
                if (scene_tex[i].id) UnloadTexture(scene_tex[i]);
                scene_flic[i] = NULL;
                scene_tex[i] = (Texture2D){0};
            }
            scene_count = 0;
            selected_object = -1;
            if (flic_store) {
                char prefix[512];
                folder_prefix(e->name, prefix, sizeof(prefix));
                ObjectList objs = build_object_list(flic_store, &flics, prefix);
                int n = objs.count < MAX_SCENE_OBJECTS ? objs.count : MAX_SCENE_OBJECTS;

                // Load every object first, indexed by STR entry order (which
                // is arbitrary -- not a paint order).
                PetkaFlic *load_flic[MAX_SCENE_OBJECTS] = {0};
                Texture2D load_tex[MAX_SCENE_OBJECTS] = {0};
                PetkaMskRect load_rect[MAX_SCENE_OBJECTS] = {0};
                int loaded_n = 0;
                for (int i = 0; i < n; i++) {
                    const PetkaEntry *oe = objs.items[i];
                    size_t olen = 0;
                    const uint8_t *obytes = petka_store_entry_data(flic_store, oe, &olen);
                    PetkaFlic *flic = petka_flic_open(obytes, olen);
                    if (!flic) { fprintf(stderr, "petka_flic_open failed for %s\n", oe->name); continue; }
                    PetkaMskRect rect = {0, 0, flic->width, flic->height};
                    char msk_name[512];
                    make_msk_name(oe->name, msk_name, sizeof(msk_name));
                    const PetkaEntry *msk_entry = petka_store_find(flic_store, msk_name);
                    if (msk_entry) {
                        size_t mlen = 0;
                        const uint8_t *mbytes = petka_store_entry_data(flic_store, msk_entry, &mlen);
                        petka_msk_rect(mbytes, mlen, &rect);
                    }
                    uint8_t *rgba = flic_to_rgba(flic);
                    Image img = {.data = rgba, .width = flic->width, .height = flic->height, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                    load_flic[loaded_n] = flic;
                    load_tex[loaded_n] = LoadTextureFromImage(img);
                    load_rect[loaded_n] = rect;
                    snprintf(scene_name[loaded_n], sizeof(scene_name[0]), "%s", oe->name);
                    UnloadImage(img);
                    loaded_n++;
                }
                free(objs.items);

                // Rooms are hand-painted with a fixed camera, so there's no
                // real depth data to sort by -- approximate it with a
                // painter's algorithm over each object's MSK rect bottom
                // edge (lower on screen == closer to the camera == drawn
                // later, on top). Without this, e.g. a pond that happens to
                // sit earlier in the STR than a birch trunk in front of it
                // would paint over the trunk instead of behind it.
                int order[MAX_SCENE_OBJECTS];
                for (int i = 0; i < loaded_n; i++) order[i] = i;
                for (int i = 1; i < loaded_n; i++) {
                    int key = order[i];
                    int key_bottom = load_rect[key].bottom;
                    int j = i - 1;
                    while (j >= 0 && load_rect[order[j]].bottom > key_bottom) {
                        order[j + 1] = order[j];
                        j--;
                    }
                    order[j + 1] = key;
                }
                char sorted_name[MAX_SCENE_OBJECTS][128];
                for (int i = 0; i < loaded_n; i++) {
                    int src = order[i];
                    scene_flic[i] = load_flic[src];
                    scene_tex[i] = load_tex[src];
                    scene_rect[i] = load_rect[src];
                    scene_next_frame[i] = GetTime();
                    snprintf(sorted_name[i], sizeof(sorted_name[0]), "%s", scene_name[src]);
                }
                for (int i = 0; i < loaded_n; i++) snprintf(scene_name[i], sizeof(scene_name[0]), "%s", sorted_name[i]);
                scene_count = loaded_n;
            }
            if (scene_count > 0) selected_object = 0;

            loaded_bg = bg_index;
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

        for (int i = 0; i < scene_count; i++) {
            if (!scene_flic[i] || GetTime() < scene_next_frame[i]) continue;
            petka_flic_advance(scene_flic[i]);
            uint8_t *rgba = flic_to_rgba(scene_flic[i]);
            UpdateTexture(scene_tex[i], rgba);
            free(rgba);
            scene_next_frame[i] = GetTime() + scene_flic[i]->speed_ms / 1000.0;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle dst = {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
        if (bg_tex.id) DrawTexturePro(bg_tex, (Rectangle){0, 0, (float)bg_tex.width, (float)bg_tex.height}, dst, (Vector2){0, 0}, 0, WHITE);
        if (bg_width > 0) {
            float sx = (float)GetScreenWidth() / (float)bg_width;
            float sy = (float)GetScreenHeight() / (float)bg_height;
            for (int i = 0; i < scene_count; i++) {
                if (!scene_flic[i]) continue;
                Rectangle src = {(float)scene_rect[i].left, (float)scene_rect[i].top,
                                  (float)(scene_rect[i].right - scene_rect[i].left), (float)(scene_rect[i].bottom - scene_rect[i].top)};
                Rectangle clip = {src.x * sx, src.y * sy, src.width * sx, src.height * sy};
                DrawTexturePro(scene_tex[i], src, clip, (Vector2){0, 0}, 0, WHITE);
                if (i == selected_object) DrawRectangleLinesEx(clip, 2, ORANGE);
            }
        }

        if (dtree) {
            DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
            DrawText(TextFormat("verb [%s]  (1=LOOK 2=TAKE 3=USE 4=TALK, click/ENTER=interact)", kVerbs[verb_choice].label),
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

        if (topo) {
            int panel_w = 360;
            char room_utf8[256] = "(unknown room)";
            if (current_room) cp1251_to_utf8(current_room->name, room_utf8, sizeof(room_utf8));
            int lines = current_room ? 1 + (int)current_room->exit_count : 1;
            int panel_h = 24 * lines + (nav_status[0] ? 24 : 0) + 34 + 10;
            DrawRectangle(GetScreenWidth() - panel_w, 0, panel_w, panel_h, Fade(BLACK, 0.6f));
            DrawTextEx(cyrillicFont, TextFormat("Room: %s  (click or TAB+G to use an exit)", room_utf8),
                       (Vector2){(float)(GetScreenWidth() - panel_w + 10), 8}, 18, 1, SKYBLUE);
            if (current_room) {
                for (uint32_t i = 0; i < current_room->exit_count; i++) {
                    char target_utf8[256];
                    cp1251_to_utf8(current_room->exits[i].target, target_utf8, sizeof(target_utf8));
                    Color c = ((int)i == exit_choice) ? ORANGE : WHITE;
                    const char *marker = ((int)i == exit_choice) ? "> " : "  ";
                    DrawTextEx(cyrillicFont, TextFormat("%s%s %s", marker, target_utf8, room_to_folder(target_utf8) ? "" : "(?)"),
                               (Vector2){(float)(GetScreenWidth() - panel_w + 10), (float)(34 + 24 * i)}, 18, 1, c);
                }
            }
            if (nav_status[0]) {
                DrawTextEx(cyrillicFont, nav_status,
                           (Vector2){(float)(GetScreenWidth() - panel_w + 10), (float)(34 + 24 * lines)}, 16, 1, RED);
            }
        }

        DrawRectangle(0, GetScreenHeight() - 84, GetScreenWidth(), 84, Fade(BLACK, 0.6f));
        DrawText(TextFormat("[%d/%d] %s", bg_index + 1, bgs.count, bgs.items[bg_index]->name), 10, GetScreenHeight() - 74, 18, LIME);
        DrawText(selected_object >= 0
                      ? TextFormat("[%d/%d objects] selected: %s", selected_object + 1, scene_count, scene_name[selected_object])
                      : (scene_count > 0 ? TextFormat("[%d objects in room, none selected]", scene_count) : "(no objects in this room)"),
                  10, GetScreenHeight() - 50, 18, YELLOW);
        DrawText(sound_index >= 0
                      ? TextFormat("[%d/%d] %s %s", sound_index + 1, sounds.count, sounds.items[sound_index]->name,
                                   current_sound.frameCount > 0 && IsSoundPlaying(current_sound) ? "(playing)" : "(stopped)")
                      : "(no sound)",
                  10, GetScreenHeight() - 26, 18, SKYBLUE);
        EndDrawing();
    }

    for (int i = 0; i < scene_count; i++) {
        if (scene_flic[i]) petka_flic_close(scene_flic[i]);
        if (scene_tex[i].id) UnloadTexture(scene_tex[i]);
    }
    if (bg_tex.id) UnloadTexture(bg_tex);
    if (current_sound.frameCount > 0) UnloadSound(current_sound);
    if (voice_sound.frameCount > 0) UnloadSound(voice_sound);
    if (cyrillicFont.texture.id != GetFontDefault().texture.id) UnloadFont(cyrillicFont);
    CloseAudioDevice();
    CloseWindow();

    free(bgs.items);
    free(flics.items);
    free(sounds.items);
    if (dtree) petka_dialoguetree_close(dtree);
    if (topo) petka_topology_close(topo);
    if (phrases) petka_phrasetable_close(phrases);
    if (main_store) petka_store_close(main_store);
    if (flic_store) petka_store_close(flic_store);
    if (sound_store) petka_store_close(sound_store);
    if (speech_store) petka_store_close(speech_store);
    petka_store_close(bg_store);
    return 0;
}
