// Cross-platform (desktop raylib+OpenGL today, Switch later) game front end.
// The scene is whatever the script VM (petka/script.h) says it is: each
// object of the current room is drawn from its VM state (resource, visible,
// z), a click becomes a script message, and animations, walks and dialogues
// report back to the VM when they finish. See docs/str-format.md.
//
// Controls: left click = act on the object under the cursor (or walk when
// there is none), right click / 1-6 = pick a verb, click an inventory item
// to use it on something, F1 = debug overlay.
//
// In a conversation, click a line of the menu (or press its number) to say
// it; a click or Space skips the phrase being spoken.
//
// Known gaps (not in the script, still to be reversed):
// AVI playback and part switching are only reported.
#include "petka/bmp.h"
#include "petka/dialogue.h"
#include "petka/flic.h"
#include "petka/ini.h"
#include "petka/msk.h"
#include "petka/script.h"
#include "petka/store.h"
#include "petka/topology.h"
#include "petka/walk.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_W 640
#define GAME_H 480
#define MAX_STORES 8
#define MAX_SOUNDS 8
// Scrolling of backgrounds wider than the screen: the view moves when Petka
// comes within SCROLL_MARGIN of its edge. Both numbers are ours; the exe is
// only known to centre the view on the entry point when a room is entered.
#define SCROLL_MARGIN 200.0f
#define SCROLL_SPEED 480.0f // px/s
// A hero's idle or PLAY clip stops on its first frame after one loop: the
// exe (sub_407BF0) winds that clip's clock back by this much.
#define HERO_REST_SECONDS 10.0

// What is on screen for one script object.
typedef struct {
    uint16_t resource; // resource currently loaded, PETKA_NONE = none
    PetkaFlic *flic;
    Texture2D tex;
    PetkaMskRect rect; // bounding rect of the clip inside its frame
    double next_frame;
    int sound_pending; // play the clip's sound when its first frame is up
    int restart;       // rewind before the next frame
    int live;    // drawn this frame
} Visual;

#define MAX_WAYPOINTS 32
#define MAX_GAITS 8

// A hero on the move. The route across the room's walk mesh is ours; how he
// moves along it -- one step per frame of the walk clip -- is the exe's
// (petka/walk.h).
typedef struct {
    int active;
    PetkaWalker walker;
} Walk;

// The step tables of one set of walk clips, by the path they share
// ("MAIN\WALK\P-" for Petka's W-N.FLC ... W-NW.FLC).
typedef struct {
    char prefix[96];
    PetkaGait *gait; // NULL if the files are missing
} GaitEntry;

typedef struct {
    PetkaStore *stores[MAX_STORES];
    int store_count;
    char *qrc[65536]; // resource id -> store path, from RESOURCE.QRC

    PetkaScript *script;
    PetkaScriptVM *vm;
    PetkaTopology *topo;
    PetkaPhraseTable *phrases;
    PetkaDialogueTree *dtree;
    const uint8_t *bgs_ini; // raw BGS.INI text (CP1251), for the scale params
    size_t bgs_ini_len;

    // Per object, parallel to script->objects, from NAMES.INI and CAST.INI.
    char **names;  // what the player sees it called (CP1251), NULL = unnamed
    Color *colors; // colour of its lines in a dialogue
    char speech_prefix[16];

    Visual *visuals; // parallel to script->objects
    Texture2D bg_tex;
    float camera_x;         // left edge of the view, in background pixels
    int camera_snap;        // a room was just entered: centre the view, don't glide
    PetkaWalkMesh *mesh;    // the current background's floor, NULL if it has none
    uint16_t mesh_resource; // background the mesh belongs to
    uint16_t bg_resource;
    uint16_t shown_room;
    // A cutscene "sequence" (SETSEQ): this room is on screen instead of the
    // one the game is in, until ENDSEQ. PETKA_NONE = no sequence.
    uint16_t seq_room;

    // Hero scale by screen row, from BGS.INI; see hero_scale().
    float persp_base, persp_delta, persp_y0, persp_y1, persp_max;
    Walk walk[2];
    GaitEntry gaits[MAX_GAITS];
    int gait_count;

    const PetkaDialogueVerb *dialogue; // picked by the script, played by DIALOG
    PetkaDialogueVM dvm;
    int talking;
    int paused;        // dialogue waits for the script's CONTINUE
    uint16_t talker;   // object speaking the current phrase, PETKA_NONE = nobody
    uint32_t menu[16]; // phrase indices of the lines the player may pick
    int menu_count;    // > 0 while the dialogue waits for that pick
    int auto_pick;     // dev: line to pick without asking; -1 = the last one, -2 = ask
    int dialogue_done; // tell the VM on the next frame
    char line[512];    // current phrase, UTF-8
    double line_until;
    Sound voice;

    Music music, ambience;
    uint16_t music_id, ambience_id;
    Sound sounds[MAX_SOUNDS];

    int verb;
    int quit;
    int fast;  // dev: no voice, phrases flash by
    int trace; // dev: log delivered script messages
    char status[256];
    double status_until;
} Game;

// The cursor's actions. A click on an object posts the action's message to
// it (the exe's click handler, sub_401B30): the sender is the object itself
// and the arguments are zero, except that WALK carries the clicked point.
// The last action sends Chapayev over: OBJECTUSE with him as the sender and
// the clicked point. (A seventh, using an inventory item, is OBJECTUSE with
// the item as the sender; it is picked from the inventory, not from here.)
static const struct { uint16_t op; const char *label; } kVerbs[] = {
    {PETKA_OP_WALK, "ИДТИ"}, {PETKA_OP_LOOK, "СМОТРЕТЬ"}, {PETKA_OP_TAKE, "ВЗЯТЬ"},
    {PETKA_OP_USE, "ПРИМЕНИТЬ"}, {PETKA_OP_TALK, "ГОВОРИТЬ"}, {PETKA_OP_OBJECTUSE, "ЧАПАЕВ"},
};
#define VERB_COUNT ((int)(sizeof(kVerbs) / sizeof(kVerbs[0])))

// Dialogue and object names are CP1251; raylib expects UTF-8. Covers ASCII
// and the main Cyrillic block plus Ё/ё -- enough for the game's text.
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
        } else {
            out[oi++] = (char)(0xC0 | (cp >> 6));
            out[oi++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[oi] = 0;
}

static const char *utf8_name(const char *cp1251) {
    static char buf[4][256];
    static int next;
    char *out = buf[next++ & 3];
    cp1251_to_utf8(cp1251, out, sizeof(buf[0]));
    return out;
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
    fprintf(stderr, "no Cyrillic-capable font found -- text will render as '?'\n");
    return GetFontDefault();
}

static void set_status(Game *g, const char *text) {
    snprintf(g->status, sizeof(g->status), "%s", text);
    g->status_until = GetTime() + 4.0;
}

// ---- resources ----

// The stores of a part live in different places depending on whether the
// game was copied from the CDs or installed; look in all of them.
static PetkaStore *open_store(const char *game_dir, const char *file) {
    static const char *dirs[] = {
        "", "data1_out/Main/", "data1_out/Flics/", "data1_out/Sfxs/", "Main/", "Flics/", "Sfxs/",
    };
    char path[1024];
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s%s", game_dir, dirs[i], file);
        if (FileExists(path)) return petka_store_open(path);
    }
    return NULL;
}

// Like the exe's MultiOpen: a path is looked up in every open store.
static const uint8_t *res_find(const Game *g, const char *path, size_t *len) {
    for (int i = 0; i < g->store_count; i++) {
        const PetkaEntry *e = petka_store_find(g->stores[i], path);
        if (e) return petka_store_entry_data(g->stores[i], e, len);
    }
    return NULL;
}

static const uint8_t *part_file(const Game *g, const char *part, const char *file, size_t *len) {
    char name[256];
    snprintf(name, sizeof(name), "%s\\%s", part, file);
    return res_find(g, name, len);
}

// RESOURCE.QRC is text: "<id>  =  <path>" (some lines use "==").
static void load_qrc(Game *g, const uint8_t *data, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        size_t end = pos;
        while (end < len && data[end] != '\n') end++;
        char line[512];
        size_t n = end - pos < sizeof(line) - 1 ? end - pos : sizeof(line) - 1;
        memcpy(line, data + pos, n);
        line[n] = 0;
        pos = end + 1;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        long id = strtol(line, NULL, 10);
        if (id <= 0 || id > 0xFFFF) continue;
        char *path = eq;
        while (*path == '=' || *path == ' ' || *path == '\t') path++;
        size_t plen = strlen(path);
        while (plen && (path[plen - 1] == '\r' || path[plen - 1] == ' ' || path[plen - 1] == '\t')) path[--plen] = 0;
        if (!plen) continue;
        free(g->qrc[id]);
        g->qrc[id] = (char *)malloc(plen + 1);
        memcpy(g->qrc[id], path, plen + 1);
    }
}

static int has_ext(const char *path, const char *ext) {
    size_t lp = strlen(path), le = strlen(ext);
    if (le > lp) return 0;
    for (size_t i = 0; i < le; i++) {
        char a = path[lp - le + i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (a != ext[i]) return 0;
    }
    return 1;
}

// Same path with another 3-letter extension (X.FLC -> X.MSK / X.WAV).
static void swap_ext(const char *path, const char *ext, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", path);
    size_t n = strlen(out);
    if (n >= 3) memcpy(out + n - 3, ext, 3);
}

static uint8_t *flic_to_rgba(const PetkaFlic *flic) {
    int n = flic->width * flic->height;
    uint8_t *rgba = (uint8_t *)malloc((size_t)n * 4);
    for (int i = 0; i < n; i++) {
        uint8_t idx = flic->indices[i];
        rgba[i * 4 + 0] = flic->rgb[i * 3 + 0];
        rgba[i * 4 + 1] = flic->rgb[i * 3 + 1];
        rgba[i * 4 + 2] = flic->rgb[i * 3 + 2];
        rgba[i * 4 + 3] = (idx == 0) ? 0 : 255; // palette index 0 is the color key
    }
    return rgba;
}

static Texture2D load_bmp_texture(const Game *g, const char *path) {
    Texture2D tex = {0};
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    int w = 0, h = 0;
    uint8_t *rgb = bytes ? petka_bmp_decode(bytes, len, &w, &h) : NULL;
    if (!rgb) { fprintf(stderr, "background not found: %s\n", path); return tex; }
    Image img = {.data = rgb, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static void play_sound(Game *g, const char *path) {
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    if (!bytes) return;
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount > 0 && IsSoundPlaying(g->sounds[i])) continue;
        if (g->sounds[i].frameCount > 0) UnloadSound(g->sounds[i]);
        Wave wave = LoadWaveFromMemory(".wav", bytes, (int)len);
        g->sounds[i] = LoadSoundFromWave(wave);
        UnloadWave(wave);
        PlaySound(g->sounds[i]);
        return;
    }
}

// Room music and ambience keep playing across rooms that share a track.
static void set_stream(Game *g, Music *stream, uint16_t *current, uint16_t id) {
    if (*current == id) return;
    if (stream->frameCount > 0) { StopMusicStream(*stream); UnloadMusicStream(*stream); *stream = (Music){0}; }
    *current = id;
    const char *path = id != PETKA_NONE ? g->qrc[id] : NULL;
    size_t len = 0;
    const uint8_t *bytes = path ? res_find(g, path, &len) : NULL;
    if (!bytes) return;
    *stream = LoadMusicStreamFromMemory(".wav", bytes, (int)len);
    stream->looping = true;
    PlayMusicStream(*stream);
}

// ---- visuals ----

static void visual_free(Visual *v) {
    if (v->flic) petka_flic_close(v->flic);
    if (v->tex.id) UnloadTexture(v->tex);
    memset(v, 0, sizeof(*v));
    v->resource = PETKA_NONE;
}

static void visual_load(Game *g, Visual *v, uint16_t resource) {
    visual_free(v);
    v->resource = resource;
    v->sound_pending = 1;
    const char *path = g->qrc[resource];
    if (!path || !has_ext(path, ".FLC")) return;
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    v->flic = bytes ? petka_flic_open(bytes, len) : NULL;
    if (!v->flic) { fprintf(stderr, "clip not found: %s\n", path); return; }

    v->rect = (PetkaMskRect){0, 0, v->flic->width, v->flic->height};
    char msk[512];
    swap_ext(path, "MSK", msk, sizeof(msk));
    size_t mlen = 0;
    const uint8_t *mbytes = res_find(g, msk, &mlen);
    if (mbytes) petka_msk_rect(mbytes, mlen, &v->rect);

    uint8_t *rgba = flic_to_rgba(v->flic);
    Image img = {.data = rgba, .width = v->flic->width, .height = v->flic->height, .mipmaps = 1,
                 .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    v->tex = LoadTextureFromImage(img);
    UnloadImage(img);
    v->next_frame = GetTime() + (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
}

static int obj_index(const Game *g, const PetkaObject *obj) {
    return (int)(obj - g->script->objects);
}

static int is_hero(uint16_t id) { return id == PETKA_ID_PETKA || id == PETKA_ID_CHAPAYEV; }

static void walk_frame(Game *g, uint16_t hero_id);

// Brings one object's visual in line with its VM state and advances its
// animation, following the exe's object update (sub_4015E0):
//   - a clip of a single frame is never updated: no END, no sound;
//   - the clip's sound starts while its first frame is up;
//   - when the last frame comes up END is posted, on every loop, and the
//     sound is armed again if the object repeats it;
//   - when the middle frame comes up HALF is posted;
//   - the clip then simply goes round again.
// The heroes have their own update (sub_407BF0) that differs in two ways
// handled here: their sound is armed again on every loop regardless, and a
// clip marked rest_after_loop pauses once it is back on its first frame.
// (It also scales the walk clip's speed by an options slider and moves the
// hero one step per frame of that clip; neither is reproduced.)
// A clip's sound is the same path with a .WAV extension.
static void visual_update(Game *g, const PetkaObject *obj) {
    Visual *v = &g->visuals[obj_index(g, obj)];
    PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    if (!st->visible || st->resource == PETKA_NONE) return;
    if (v->resource != st->resource || v->restart) {
        visual_load(g, v, st->resource);
        v->restart = 0;
    }
    if (!v->flic) return;
    v->live = 1;
    if (!st->animate) return;
    if (v->flic->frame_count <= 1) {
        // Ours, not the exe's: a script waiting on a one-frame clip would
        // wait forever, so that wait alone is ended.
        if (st->play_tail.present && st->play_wait == v->resource)
            petka_scriptvm_animation_ended(g->vm, obj->id, v->resource);
        return;
    }
    if (GetTime() < v->next_frame) return;
    v->next_frame = GetTime() + (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;

    if (v->sound_pending && v->flic->frame_index == 0) {
        char wav[512];
        swap_ext(g->qrc[v->resource], "WAV", wav, sizeof(wav));
        play_sound(g, wav);
        v->sound_pending = 0;
    }
    petka_flic_advance(v->flic);
    uint8_t *rgba = flic_to_rgba(v->flic);
    UpdateTexture(v->tex, rgba);
    free(rgba);

    int frame = v->flic->frame_index + 1; // the exe counts frames from 1
    if (frame == v->flic->frame_count) {
        if (st->repeat_sound || is_hero(obj->id)) v->sound_pending = 1;
        petka_scriptvm_animation_ended(g->vm, obj->id, v->resource);
    }
    if (frame == v->flic->frame_count / 2)
        petka_scriptvm_post(g->vm, obj->id, PETKA_OP_HALF, v->resource, 0, 0, -1);
    if (is_hero(obj->id) && st->rest_after_loop && frame == 1) v->next_frame += HERO_REST_SECONDS;
    // A walking hero takes one step for each frame of his walk clip.
    if (is_hero(obj->id) && g->walk[obj->id].active) walk_frame(g, obj->id);
}

// ---- heroes ----

// Scale of a hero standing at screen row y (the exe's sub_409460). The five
// numbers a room has in BGS.INI's [Settings] are "a b y0 y1 max": the scale
// is a at row y0 and grows by b over the rows down to y1, never below a and
// never above max. A room with no entry draws heroes at full size.
static float hero_scale(const Game *g, float y) {
    if (y > GAME_H) y = GAME_H;
    float grow = g->persp_y1 != g->persp_y0
        ? (y - g->persp_y0) * g->persp_delta / (g->persp_y1 - g->persp_y0) : 0;
    if (grow < 0) grow = 0;
    float s = g->persp_base + grow;
    if (s > g->persp_max) s = g->persp_max;
    return s > 0.05f ? s : 0.05f; // ours: never let a hero vanish
}

static void load_perspective(Game *g, const char *room_name) {
    g->persp_base = 1; g->persp_delta = 0; g->persp_y0 = 0; g->persp_y1 = GAME_H; g->persp_max = 1;
    char value[128];
    float a, b, c, d, e;
    if (g->bgs_ini && petka_ini_get(g->bgs_ini, g->bgs_ini_len, "Settings", room_name, value, sizeof(value)) &&
        sscanf(value, "%f %f %f %f %f", &a, &b, &c, &d, &e) == 5) {
        g->persp_base = a; g->persp_delta = b; g->persp_y0 = c; g->persp_y1 = d; g->persp_max = e;
    }
}

// NAMES.INI maps an object's script name to the name shown to the player
// (many objects share one: "Большой ключ" and "IБольшой ключ" are both
// "Ключ"); CAST.INI gives a speaker's text colour as "R G B". Both have a
// single [all] section. Objects with no NAMES.INI entry are props the
// player is not told the name of.
static void load_names(Game *g, const char *part) {
    size_t names_len = 0, cast_len = 0;
    const uint8_t *names = part_file(g, part, "NAMES.INI", &names_len);
    const uint8_t *cast = part_file(g, part, "CAST.INI", &cast_len);
    g->names = (char **)calloc(g->script->object_count, sizeof(char *));
    g->colors = (Color *)calloc(g->script->object_count, sizeof(Color));
    for (uint32_t i = 0; i < g->script->object_count; i++) {
        const char *key = g->script->objects[i].name;
        char value[256];
        g->colors[i] = WHITE;
        if (names && petka_ini_get(names, names_len, "all", key, value, sizeof(value)) && value[0]) {
            g->names[i] = (char *)malloc(strlen(value) + 1);
            strcpy(g->names[i], value);
        }
        int r, gr, b;
        if (cast && petka_ini_get(cast, cast_len, "all", key, value, sizeof(value)) &&
            sscanf(value, "%d %d %d", &r, &gr, &b) == 3)
            g->colors[i] = (Color){(unsigned char)r, (unsigned char)gr, (unsigned char)b, 255};
    }
}

static const char *display_name(const Game *g, const PetkaObject *obj);

// Where a hero's clip goes on screen. While he walks, the frame's foothold
// (from the clip's .LEG file) sits on his position, so the feet do not
// slide. A clip with no .LEG -- standing, talking, acting -- is placed by
// the bottom centre of its MSK rect instead, which is our assumption, so he
// may shift by a few pixels when a walk starts or ends.
static Rectangle hero_dest(const Game *g, uint16_t hero_id, const PetkaObjectState *st, const Visual *v) {
    const Walk *w = &g->walk[hero_id];
    if (w->active) {
        const PetkaGait *gait = w->walker.gait;
        int dir = w->walker.dir, frame = v->flic->frame_index;
        if (frame > gait->frames[dir]) frame = gait->frames[dir];
        float s = hero_scale(g, (float)w->walker.y);
        return (Rectangle){(float)w->walker.x - (float)gait->leg_x[dir][frame] * s,
                           (float)w->walker.y - (float)gait->leg_y[dir][frame] * s,
                           v->flic->width * s, v->flic->height * s};
    }
    float s = hero_scale(g, (float)st->y);
    float ax = (v->rect.left + v->rect.right) / 2.0f, ay = (float)v->rect.bottom;
    return (Rectangle){st->x - ax * s, st->y - ay * s, v->flic->width * s, v->flic->height * s};
}

// The step tables for a costume's walk clips. As in the exe's Walk
// constructor, the clips' common path is whatever precedes "W-N" in the
// path of clip costume+10, and each direction adds its name and .OFF/.LEG.
static const PetkaGait *gait_for(Game *g, uint16_t costume) {
    static const char *const kDirs[PETKA_WALK_DIRS] = {"W-N", "W-NE", "W-E", "W-SE", "W-S", "W-SW", "W-W", "W-NW"};
    const char *path = costume != PETKA_NONE ? g->qrc[(uint16_t)(costume + 10)] : NULL;
    if (!path) return NULL;
    char upper[96];
    size_t n = 0;
    for (; path[n] && n + 1 < sizeof(upper); n++)
        upper[n] = path[n] >= 'a' && path[n] <= 'z' ? (char)(path[n] - 32) : path[n];
    upper[n] = 0;
    char *cut_at = strstr(upper, "W-N");
    if (!cut_at) return NULL;
    *cut_at = 0;

    for (int i = 0; i < g->gait_count; i++)
        if (strcmp(g->gaits[i].prefix, upper) == 0) return g->gaits[i].gait;
    if (g->gait_count == MAX_GAITS) return NULL;
    GaitEntry *entry = &g->gaits[g->gait_count++];
    snprintf(entry->prefix, sizeof(entry->prefix), "%s", upper);

    const uint8_t *off[PETKA_WALK_DIRS], *leg[PETKA_WALK_DIRS];
    size_t off_len[PETKA_WALK_DIRS], leg_len[PETKA_WALK_DIRS];
    for (int d = 0; d < PETKA_WALK_DIRS; d++) {
        char name[160];
        snprintf(name, sizeof(name), "%s%s.OFF", upper, kDirs[d]);
        off[d] = res_find(g, name, &off_len[d]);
        snprintf(name, sizeof(name), "%s%s.LEG", upper, kDirs[d]);
        leg[d] = res_find(g, name, &leg_len[d]);
        if (!off[d] || !leg[d]) { fprintf(stderr, "walk files not found: %s\n", name); return NULL; }
    }
    entry->gait = petka_gait_open(off, off_len, leg, leg_len);
    return entry->gait;
}

static double walk_scale(void *user, double y) { return (double)hero_scale((const Game *)user, (float)y); }

// The room on screen: the sequence being played, or else the room the game
// is in. A sequence is a room made of full-screen clips, usually on the
// black MAIN\BG1.BMP; the script moves it along with the clips' END
// reactions and closes it with ENDSEQ. No INITBG/LEAVEBG is involved, and
// the game's own room can change (GOTO) while the sequence is up.
static uint16_t screen_room(const Game *g) {
    return g->seq_room != PETKA_NONE ? g->seq_room : g->vm->room;
}

// The walk mesh sits next to the background under the same name
// (P-ROOM-1.BMP -> P-ROOM-1.CVX). Called as soon as the room is known, not
// from the frame loop: a room's INITBG reaction may start a walk at once.
static void load_mesh(Game *g) {
    const PetkaObjectState *room = petka_scriptvm_state(g->vm, screen_room(g));
    uint16_t resource = room ? room->resource : PETKA_NONE;
    if (resource == g->mesh_resource) return;
    petka_walkmesh_close(g->mesh);
    g->mesh = NULL;
    g->mesh_resource = resource;
    const char *path = resource != PETKA_NONE ? g->qrc[resource] : NULL;
    if (!path || !has_ext(path, ".BMP")) return;
    char cvx[512];
    swap_ext(path, "CVX", cvx, sizeof(cvx));
    size_t len = 0;
    const uint8_t *bytes = res_find(g, cvx, &len);
    if (bytes) g->mesh = petka_walkmesh_open(bytes, len);
}

static int host_walk(void *user, uint16_t hero_id, int32_t x, int32_t y) {
    Game *g = (Game *)user;
    PetkaObjectState *st = petka_scriptvm_state(g->vm, hero_id);
    if (!st || !st->visible || hero_id > 1) return 0;
    Walk *w = &g->walk[hero_id];
    load_mesh(g);
    PetkaPoint route[MAX_WAYPOINTS];
    int count = g->mesh ? petka_walkmesh_path(g->mesh, (float)st->x, (float)st->y, (float)x, (float)y,
                                              route, MAX_WAYPOINTS) : 0;
    if (count == 0) {
        // No floor to follow (a cutscene screen, the map): go straight.
        route[0] = (PetkaPoint){x, y};
        count = 1;
    }
    // Measured to where the walk would really end: a click off the floor is
    // pulled onto it, possibly right back to where the hero stands.
    PetkaPoint end = route[count - 1];
    float dx = (float)(end.x - st->x), dy = (float)(end.y - st->y);
    if (dx * dx + dy * dy < 25) return 0; // the exe's own "already there" threshold

    const PetkaGait *gait = gait_for(g, st->costume);
    PetkaPoint from = {st->x, st->y};
    if (!gait || !petka_walker_start(&w->walker, gait, from, route, count, walk_scale, g)) {
        // Cannot walk it (no step tables for this costume): just be there.
        st->x = end.x;
        st->y = end.y;
        return 0;
    }
    w->active = 1;
    st->rest_after_loop = 0; // the walk clip loops, as when the exe starts a walk
    // Walk clips are costume+10 .. costume+17: N, NE, E, SE, S, SW, W, NW.
    st->resource = (uint16_t)(st->costume + 10 + w->walker.dir);
    g->visuals[obj_index(g, petka_script_find(g->script, hero_id))].restart = 1;
    return 1;
}

// Objects, clips, walk meshes and hero positions are all in background
// pixels; only the view moves. It follows Petka, and stays within the
// background.
static void camera_update(Game *g, float dt) {
    float max_x = g->bg_tex.id ? (float)g->bg_tex.width - GAME_W : 0;
    if (max_x < 0) max_x = 0;
    if (g->seq_room != PETKA_NONE) { g->camera_x = 0; return; } // sequences are one screen
    const PetkaObjectState *petka = petka_scriptvm_state(g->vm, PETKA_ID_PETKA);
    float target = g->camera_x;
    if (petka && g->camera_snap) {
        target = (float)petka->x - GAME_W / 2.0f;
    } else if (petka && petka->visible) {
        float on_screen = (float)petka->x - g->camera_x;
        if (on_screen < SCROLL_MARGIN) target = (float)petka->x - SCROLL_MARGIN;
        if (on_screen > GAME_W - SCROLL_MARGIN) target = (float)petka->x - (GAME_W - SCROLL_MARGIN);
    }
    if (target < 0) target = 0;
    if (target > max_x) target = max_x;

    float step = g->camera_snap ? max_x + GAME_W : SCROLL_SPEED * dt;
    g->camera_snap = 0;
    if (target > g->camera_x + step) g->camera_x += step;
    else if (target < g->camera_x - step) g->camera_x -= step;
    else g->camera_x = target;
    if (g->camera_x > max_x) g->camera_x = max_x;
}

// Both heroes stop where they are. The exe does this on every click on an
// object, before posting the click's message.
static void stop_heroes(Game *g) {
    for (uint16_t id = 0; id < 2; id++) {
        PetkaObjectState *st = petka_scriptvm_state(g->vm, id);
        if (!st || !g->walk[id].active) continue;
        g->walk[id].active = 0;
        petka_scriptvm_walk_ended(g->vm, id);
    }
}

// One step of a walking hero, taken when his walk clip moves on a frame
// (the exe's hero walk step, sub_408EF0).
static void walk_frame(Game *g, uint16_t hero_id) {
    Walk *w = &g->walk[hero_id];
    PetkaObjectState *st = petka_scriptvm_state(g->vm, hero_id);
    if (!w->active || !st) return;
    int result = petka_walker_step(&w->walker);
    st->x = (int32_t)lround(w->walker.x);
    st->y = (int32_t)lround(w->walker.y);
    if (result == 2) {
        // New direction: its clip, from the first frame.
        st->resource = (uint16_t)(st->costume + 10 + w->walker.dir);
        g->visuals[obj_index(g, petka_script_find(g->script, hero_id))].restart = 1;
    } else if (result == 0) {
        w->active = 0;
        petka_scriptvm_walk_ended(g->vm, hero_id);
    }
}

// ---- dialogue ----

static const PetkaDialogueVerb *find_dialogue(const Game *g, uint16_t obj_id, uint32_t key) {
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

static int host_has_dialogue(void *user, uint16_t obj_id, uint32_t key) {
    return find_dialogue((Game *)user, obj_id, key) != NULL;
}

static void host_select_dialogue(void *user, uint16_t obj_id, uint32_t key) {
    Game *g = (Game *)user;
    g->dialogue = find_dialogue(g, obj_id, key);
}

// Whoever speaks gets SAY, and SAID when he stops: heroes switch to their
// talking clip themselves, other objects do it in their SAY/SAID reactions.
// Both are delivered at once, not queued, like the exe's SAID at the end of
// a dialogue.
static void set_talker(Game *g, uint16_t id) {
    if (g->talker == id) return;
    uint16_t prev = g->talker;
    g->talker = id;
    if (prev != PETKA_NONE) petka_scriptvm_send(g->vm, prev, PETKA_OP_SAID, 0, 0, 0, -1);
    if (id != PETKA_NONE) petka_scriptvm_send(g->vm, id, PETKA_OP_SAY, 0, 0, 0, -1);
}

static void next_phrase(Game *g) {
    if (g->voice.frameCount > 0) { StopSound(g->voice); UnloadSound(g->voice); g->voice = (Sound){0}; }
    uint32_t idx;
    int step = petka_dialoguevm_next(&g->dvm, &idx);
    if (step == 2) {
        // The dialogue hands control to the script and waits for CONTINUE.
        set_talker(g, PETKA_NONE);
        g->talking = 0;
        g->paused = 1;
        g->line[0] = 0;
        petka_scriptvm_post(g->vm, PETKA_ID_CHAPAYEV, PETKA_OP_USERMSG, (uint16_t)idx, PETKA_NONE,
                            PETKA_NONE, PETKA_ID_CHAPAYEV);
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
    // Without a voice clip the line stays up for a reading-speed guess.
    g->line_until = GetTime() + 1.2 + 0.05 * (double)strlen(ph->text);
    if (g->fast) {
        g->line_until = GetTime() + 0.1;
        const PetkaObject *who = petka_script_find(g->script, g->talker);
        fprintf(stderr, "say %.1fs [%s]: %s\n", GetTime(), who ? utf8_name(who->name) : "?", g->line);
    } else if (ph->wav_name[0]) {
        char wav[64];
        snprintf(wav, sizeof(wav), "%s\\%s", g->speech_prefix, ph->wav_name);
        size_t len = 0;
        const uint8_t *bytes = res_find(g, wav, &len);
        if (bytes) {
            Wave wave = LoadWaveFromMemory(".wav", bytes, (int)len);
            if (wave.frameCount > 0) { g->voice = LoadSoundFromWave(wave); PlaySound(g->voice); }
            UnloadWave(wave);
        }
    }
}

static void host_start_dialogue(void *user, uint16_t obj_id, uint16_t arg) {
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

// ---- script host ----

// BGS.INI: in the new room's section, the key is the room we came from and
// the value names the object whose walk point is the entry spot.
static void host_room_changed(void *user, uint16_t prev_id, uint16_t room_id) {
    Game *g = (Game *)user;
    const PetkaObject *room = petka_script_find(g->script, room_id);
    const PetkaObject *prev = prev_id != PETKA_NONE ? petka_script_find(g->script, prev_id) : NULL;
    if (!room) return;
    load_perspective(g, room->name);
    load_mesh(g);
    g->camera_snap = 1;
    g->walk[0].active = g->walk[1].active = 0;

    const PetkaRoom *section = g->topo ? petka_topology_find_room(g->topo, room->name) : NULL;
    const PetkaObject *entry = NULL;
    for (uint32_t i = 0; section && i < section->exit_count && !entry; i++)
        if (prev && strcmp(section->exits[i].target, prev->name) == 0)
            entry = petka_script_find_name(g->script, section->exits[i].description);
    // Arriving from somewhere with no exit of its own (the map, a cutscene):
    // the exe falls back to the first entry point it finds.
    for (uint32_t i = 0; section && i < section->exit_count && !entry; i++)
        entry = petka_script_find_name(g->script, section->exits[i].description);
    if (!entry) return;

    const PetkaObjectState *spot = petka_scriptvm_state(g->vm, entry->id);
    PetkaObjectState *petka = petka_scriptvm_state(g->vm, PETKA_ID_PETKA);
    PetkaObjectState *chapayev = petka_scriptvm_state(g->vm, PETKA_ID_CHAPAYEV);
    if (!spot || spot->walk_x == -1) return;
    if (petka) { petka->x = spot->walk_x; petka->y = spot->walk_y; petka->busy = 0; }
    if (chapayev) { chapayev->x = spot->walk_x; chapayev->y = spot->walk_y - 2; chapayev->busy = 0; }
}

static void host_message(void *user, PetkaScriptVM *vm, const PetkaObject *obj,
                         const PetkaQueued *q, int reacted) {
    Game *g = (Game *)user;
    const PetkaMessage *m = &q->msg;
    // A broadcast reaches every object; only the reactions are worth logging.
    int noise = !reacted && (m->opcode == PETKA_OP_INITBG || m->opcode == PETKA_OP_LEAVEBG ||
                             m->opcode == PETKA_OP_TOTALINIT || m->opcode == PETKA_OP_END ||
                             m->opcode == PETKA_OP_HALF);
    if (g->trace && !noise)
        fprintf(stderr, "%-10s -> %u[%s] %d %d %d%s\n", petka_opcode_name(m->opcode), obj->id,
                utf8_name(obj->name), (int16_t)m->arg1, (int16_t)m->arg2, (int16_t)m->arg3,
                reacted ? "  (reaction)" : "");
    switch (m->opcode) {
    case PETKA_OP_SET:
    case PETKA_OP_PLAY:
        // PLAY starts the clip over; so does SET with 1 as its second argument.
        if (m->opcode == PETKA_OP_PLAY || m->arg2 == 1) g->visuals[obj_index(g, obj)].restart = 1;
        break;
    case PETKA_OP_SETSEQ:
        g->seq_room = obj->id;
        break;
    case PETKA_OP_ENDSEQ:
        g->seq_room = PETKA_NONE;
        g->camera_snap = 1;
        break;
    case PETKA_OP_CONTINUE:
        if (g->paused) {
            g->paused = 0;
            g->talking = 1;
            next_phrase(g);
        }
        break;
    case PETKA_OP_SYSTEM:
        if (m->arg1 == 242) g->quit = 1;
        break;
    case PETKA_OP_AVI:
        set_status(g, TextFormat("(видео %s пропущено)", g->qrc[m->arg1] ? g->qrc[m->arg1] : "?"));
        break;
    case PETKA_OP_PART:
        set_status(g, TextFormat("(переход к части %u не реализован)", m->arg1));
        break;
    case PETKA_OP_CHAPTER:
        set_status(g, TextFormat("(глава %u)", m->arg1));
        break;
    case PETKA_OP_SHOWDESCRIPTION:
        set_status(g, display_name(g, obj));
        break;
    default:
        break;
    }
}

// ---- scene ----

typedef struct {
    const PetkaObject *obj;
    int z;
} Drawable;

static int by_z(const void *a, const void *b) {
    return ((const Drawable *)a)->z - ((const Drawable *)b)->z;
}

// Collects what is visible right now, back to front. Draw order is the
// object's z; where the script leaves it unset, the bottom edge of its MSK
// rect stands in (the two agree wherever z is given).
static int build_scene(Game *g, Drawable *out, int max) {
    int n = 0;
    const PetkaObject *room = petka_script_find(g->script, screen_room(g));
    // The heroes walk the game's room; a sequence shows only its own objects.
    uint32_t extra = g->seq_room != PETKA_NONE ? 0 : 2;
    for (uint32_t i = 0; room && i < room->attached_count + extra && n < max; i++) {
        const PetkaObject *obj = i < room->attached_count
            ? petka_script_find(g->script, room->attached[i].obj_id)
            : petka_script_find(g->script, (uint16_t)(i - room->attached_count)); // then the two heroes
        if (!obj) continue;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
        if (!v->live) continue;
        int hero_here = is_hero(obj->id);
        // A hero attached to this room explicitly was already listed.
        if (i >= room->attached_count && st->room == room->id) continue;
        out[n].obj = obj;
        out[n].z = hero_here ? st->y : (st->z != -1 ? st->z : v->rect.bottom);
        n++;
    }
    qsort(out, (size_t)n, sizeof(Drawable), by_z);
    return n;
}

static Rectangle visual_dest(const Game *g, const PetkaObject *obj) {
    const Visual *v = &g->visuals[obj_index(g, obj)];
    const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    // Room clips are full-frame and already in place; hero clips are sprites.
    if (is_hero(obj->id)) return hero_dest(g, obj->id, st, v);
    return (Rectangle){0, 0, (float)v->flic->width, (float)v->flic->height};
}

// Topmost active object whose clip has an opaque pixel under (x, y).
static const PetkaObject *object_at(Game *g, const Drawable *scene, int n, float x, float y) {
    for (int i = n - 1; i >= 0; i--) {
        const PetkaObject *obj = scene[i].obj;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        if (!g->vm->state[obj_index(g, obj)].active) continue;
        Rectangle d = visual_dest(g, obj);
        if (d.width <= 0 || x < d.x || y < d.y || x >= d.x + d.width || y >= d.y + d.height) continue;
        int px = (int)((x - d.x) * v->flic->width / d.width);
        int py = (int)((y - d.y) * v->flic->height / d.height);
        if (px < 0 || py < 0 || px >= v->flic->width || py >= v->flic->height) continue;
        if (v->flic->indices[py * v->flic->width + px] != 0) return obj;
    }
    return NULL;
}

// Row of the line menu, in window pixels; the menu sits at the bottom.
static Rectangle menu_row(const Game *g, int row) {
    float top = (float)GetScreenHeight() - 16 - 34.0f * (float)g->menu_count;
    return (Rectangle){10, top + 34.0f * (float)row, (float)GetScreenWidth() - 20, 32};
}

// The name the player sees, "" for an object that has none. Inventory
// items and rooms always get one: where NAMES.INI is silent, the script
// name stands in (inventory objects are called "I<name>" there).
static const char *display_name(const Game *g, const PetkaObject *obj) {
    const char *name = g->names[obj - g->script->objects];
    if (name) return utf8_name(name);
    if (obj->is_room) return utf8_name(obj->name);
    if (obj->name[0] == 'I' && obj->name[1]) return utf8_name(obj->name + 1);
    return "";
}

// Returns 0 if the window was closed or "exit" picked.
static int run_menu(const Game *g, Font font) {
    static const char *items[] = {"Новая игра", "Выход"};
    Texture2D bg = load_bmp_texture(g, "MAIN\\INTRFACE.BG\\INTRFASE.BMP");
    int selected = 0, start = 0;
    while (!WindowShouldClose() && !start) {
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP)) selected ^= 1;
        if (IsKeyPressed(KEY_ENTER)) {
            if (selected == 1) break;
            start = 1;
        }
        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle dst = {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()};
        if (bg.id) DrawTexturePro(bg, (Rectangle){0, 0, (float)bg.width, (float)bg.height}, dst, (Vector2){0, 0}, 0, WHITE);
        int px = GetScreenWidth() / 2 - 160, py = GetScreenHeight() / 2 - 54;
        DrawRectangle(px, py, 320, 108, Fade(BLACK, 0.65f));
        for (int i = 0; i < 2; i++)
            DrawTextEx(font, TextFormat("%s%s", i == selected ? "> " : "  ", items[i]),
                       (Vector2){(float)(px + 16), (float)(py + 10 + i * 44)}, 28, 1, i == selected ? ORANGE : WHITE);
        EndDrawing();
    }
    if (bg.id) UnloadTexture(bg);
    return start;
}

int main(int argc, char **argv) {
    const char *game_dir = argc > 1 ? argv[1] : ".";
    const char *part = argc > 2 ? argv[2] : "PART_1";
    const char *digit = part;
    while (*digit && (*digit < '0' || *digit > '9')) digit++;
    if (!*digit) { fprintf(stderr, "usage: %s [game_dir] [PART_N]\n", argv[0]); return 1; }

    static Game game;
    Game *g = &game;
    g->bg_resource = g->shown_room = g->music_id = g->ambience_id = g->mesh_resource = PETKA_NONE;
    g->talker = PETKA_NONE;
    g->seq_room = PETKA_NONE;
    snprintf(g->speech_prefix, sizeof(g->speech_prefix), "SPEECH%s", digit);

    // The last one is the store of the part's first chapter; only a part
    // split into chapters has it (it carries that chapter's speech, names
    // and colours). Switching chapters mid-game is not done yet.
    static const char *const kStoreNames[] = {"MAIN.STR", "BGS%s.STR", "FLC%s.STR", "WAV%s.STR",
                                              "SFX%s.STR", "MUSIC%s.STR", "SPEECH%s.STR", "CHAP%s1.STR"};
    const size_t store_names = sizeof(kStoreNames) / sizeof(kStoreNames[0]);
    for (size_t i = 0; i < store_names; i++) {
        char file[64];
        snprintf(file, sizeof(file), kStoreNames[i], digit);
        PetkaStore *store = open_store(game_dir, file);
        if (store) g->stores[g->store_count++] = store;
        else if (i + 1 < store_names) fprintf(stderr, "store not found under %s: %s\n", game_dir, file);
    }

    size_t script_len = 0, bg_len = 0, len = 0;
    const uint8_t *script_data = part_file(g, part, "SCRIPT.DAT", &script_len);
    const uint8_t *bg_data = part_file(g, part, "BACKGRND.BG", &bg_len);
    g->script = script_data && bg_data ? petka_script_open(script_data, script_len, bg_data, bg_len) : NULL;
    if (!g->script) { fprintf(stderr, "no script for %s under %s\n", part, game_dir); return 1; }

    const uint8_t *bytes = part_file(g, part, "RESOURCE.QRC", &len);
    if (bytes) load_qrc(g, bytes, len);
    bytes = part_file(g, part, "BGS.INI", &len);
    if (bytes) {
        g->topo = petka_topology_parse(bytes, len);
        g->bgs_ini = bytes;
        g->bgs_ini_len = len;
    }
    size_t lod_len = 0, fix_len = 0;
    const uint8_t *lod = part_file(g, part, "DIALOGUE.LOD", &lod_len);
    const uint8_t *fix = part_file(g, part, "DIALOGUE.FIX", &fix_len);
    if (lod && fix) {
        g->phrases = petka_phrasetable_open(lod, lod_len);
        g->dtree = petka_dialoguetree_open(fix, fix_len);
    }
    if (!g->phrases || !g->dtree) fprintf(stderr, "dialogue data unavailable for %s\n", part);

    PetkaScriptHost host = {0};
    host.user = g;
    host.has_dialogue = host_has_dialogue;
    host.select_dialogue = host_select_dialogue;
    host.start_dialogue = host_start_dialogue;
    host.walk = host_walk;
    host.room_changed = host_room_changed;
    host.message = host_message;
    g->vm = petka_scriptvm_open(g->script, &host);
    load_names(g, part);
    g->visuals = (Visual *)calloc(g->script->object_count, sizeof(Visual));
    for (uint32_t i = 0; i < g->script->object_count; i++) g->visuals[i].resource = PETKA_NONE;

    InitWindow(GAME_W * 2, GAME_H * 2, "Петька и Василий Иванович");
    SetTargetFPS(60);
    InitAudioDevice();
    Font font = load_cyrillic_font();
    // Dev hooks for unattended runs:
    //   PETKA_SHOT=<file.png>  skip the menu, save a screenshot and quit
    //   PETKA_SHOT_FRAME=<n>   ...after this many frames (default 120)
    //   PETKA_POST=<frame>:<obj_id>:<opcode>[:<arg1>:<arg2>][,...]  post script messages
    //   PETKA_FAST=1           no voice-over, phrases flash by
    //   PETKA_PICK=<n>|last    dialogue menus answer themselves with that line
    //   PETKA_TRACE=1          log delivered script messages to stderr
    g->fast = getenv("PETKA_FAST") != NULL;
    const char *pick = getenv("PETKA_PICK");
    g->auto_pick = !pick ? -2 : strcmp(pick, "last") == 0 ? -1 : atoi(pick);
    g->trace = getenv("PETKA_TRACE") != NULL;
    const char *shot_path = getenv("PETKA_SHOT");
    const char *post_plan = getenv("PETKA_POST");
    long shot_frame = getenv("PETKA_SHOT_FRAME") ? atol(getenv("PETKA_SHOT_FRAME")) : 120;
    long frame = 0;
    if (!shot_path && !run_menu(g, font)) { CloseWindow(); return 0; }

    g->vm->rng = (uint32_t)(GetTime() * 1e6) | 1u;
    petka_scriptvm_init(g->vm);
    const PetkaObject *start = g->topo && g->topo->start_room
        ? petka_script_find_name(g->script, g->topo->start_room) : NULL;
    if (!start) {
        for (uint32_t i = 0; i < g->script->object_count && !start; i++)
            if (g->script->objects[i].is_room) start = &g->script->objects[i];
    }
    if (start) petka_scriptvm_enter_room(g->vm, start->id, 0);

    int debug = getenv("PETKA_DEBUG") != NULL; // dev: start with the F1 overlay on
    Drawable scene[256];
    while (!WindowShouldClose() && !g->quit) {
        float scale = (float)GetScreenWidth() / GAME_W;
        Vector2 mouse = GetMousePosition();
        // dev: PETKA_MOUSE=<x>,<y> pins the pointer (game pixels) for
        // unattended runs, so hovering can be screenshotted.
        float pin_x, pin_y;
        if (getenv("PETKA_MOUSE") && sscanf(getenv("PETKA_MOUSE"), "%f,%f", &pin_x, &pin_y) == 2)
            mouse = (Vector2){pin_x * scale, pin_y * scale};
        // dev: PETKA_CLICK=<frame>:<x>:<y>[:<verb>][,...] left-clicks at a
        // point of the screen (game pixels), optionally picking a verb first.
        int clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        for (const char *p = getenv("PETKA_CLICK"); p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "") {
            long at = 0;
            float cx = 0, cy = 0;
            int verb = -1;
            if (sscanf(p, "%ld:%f:%f:%d", &at, &cx, &cy, &verb) < 3 || at != frame + 1) continue;
            mouse = (Vector2){cx * scale, cy * scale};
            clicked = 1;
            if (verb >= 0 && verb < VERB_COUNT) g->verb = verb;
        }
        // The pointer in background pixels: where it is in the room.
        float mx = mouse.x / scale + g->camera_x, my = mouse.y / scale;
        PetkaScriptVM *vm = g->vm;

        if (IsKeyPressed(KEY_F1)) debug = !debug;
        for (int k = 0; k < VERB_COUNT && !g->menu_count; k++) if (IsKeyPressed(KEY_ONE + k)) g->verb = k;

        frame++;
        for (const char *p = post_plan; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "") {
            long at = 0;
            unsigned obj_id = 0, opcode = 0, arg1 = PETKA_NONE, arg2 = PETKA_NONE;
            if (sscanf(p, "%ld:%u:%u:%u:%u", &at, &obj_id, &opcode, &arg1, &arg2) >= 3 && at == frame)
                petka_scriptvm_post(vm, (uint16_t)obj_id, (uint16_t)opcode, (uint16_t)arg1, (uint16_t)arg2,
                                    PETKA_NONE, -1);
        }

        if (g->dialogue_done) {
            g->dialogue_done = 0;
            // The VM sends the last speaker his SAID before going on.
            uint16_t talker = g->talker;
            g->talker = PETKA_NONE;
            petka_scriptvm_dialogue_ended(vm, talker);
        }
        if (petka_scriptvm_run(vm) < 0) set_status(g, "(сценарий зациклился)");

        // The room the VM is in may have changed: drop the old room's clips.
        if (g->shown_room != screen_room(g)) {
            for (uint32_t i = 0; i < g->script->object_count; i++)
                if (!is_hero(g->script->objects[i].id)) visual_free(&g->visuals[i]);
            g->shown_room = screen_room(g);
        }
        const PetkaObject *room = petka_script_find(g->script, screen_room(g));
        const PetkaObjectState *room_st = room ? &vm->state[obj_index(g, room)] : NULL;
        if (room_st && room_st->resource != g->bg_resource) {
            if (g->bg_tex.id) UnloadTexture(g->bg_tex);
            g->bg_tex = (Texture2D){0};
            g->bg_resource = room_st->resource;
            const char *path = g->bg_resource != PETKA_NONE ? g->qrc[g->bg_resource] : NULL;
            if (path && has_ext(path, ".BMP")) g->bg_tex = load_bmp_texture(g, path);
        }
        load_mesh(g);
        if (room_st) {
            // A sequence with no track of its own keeps the room's playing.
            const PetkaObjectState *under = g->seq_room != PETKA_NONE ? petka_scriptvm_state(vm, vm->room) : NULL;
            uint16_t music = room_st->music, ambience = room_st->bgsfx;
            if (under && music == PETKA_NONE) music = under->music;
            if (under && ambience == PETKA_NONE) ambience = under->bgsfx;
            set_stream(g, &g->music, &g->music_id, music);
            set_stream(g, &g->ambience, &g->ambience_id, ambience);
        }
        if (g->music.frameCount > 0) UpdateMusicStream(g->music);
        if (g->ambience.frameCount > 0) UpdateMusicStream(g->ambience);

        camera_update(g, GetFrameTime());
        // Everything in the room is drawn through this; the HUD is not.
        Camera2D view = {.offset = {0, 0}, .target = {roundf(g->camera_x) * scale, 0}, .rotation = 0, .zoom = 1};

        for (uint32_t i = 0; i < g->script->object_count; i++) g->visuals[i].live = 0;
        for (uint32_t i = 0; room && i < room->attached_count; i++) {
            const PetkaObject *obj = petka_script_find(g->script, room->attached[i].obj_id);
            if (obj) visual_update(g, obj);
        }
        for (uint16_t id = 0; id < 2 && g->seq_room == PETKA_NONE; id++) {
            const PetkaObject *hero = petka_script_find(g->script, id);
            if (hero && !g->visuals[obj_index(g, hero)].live) visual_update(g, hero);
        }
        // A PLAY aimed at something that is not on screen (hidden, another
        // room, missing clip) would never end; end it at once instead.
        for (uint32_t i = 0; i < g->script->object_count; i++) {
            const PetkaObjectState *st = &vm->state[i];
            if (st->play_tail.present && !g->visuals[i].live)
                petka_scriptvm_animation_ended(vm, g->script->objects[i].id, st->play_wait);
        }
        if (petka_scriptvm_run(vm) < 0) set_status(g, "(сценарий зациклился)");

        int scene_count = build_scene(g, scene, (int)(sizeof(scene) / sizeof(scene[0])));
        int can_act = vm->interface_enabled && !g->talking && g->seq_room == PETKA_NONE;
        const PetkaObject *hover = can_act ? object_at(g, scene, scene_count, mx, my) : NULL;

        // Inventory strip along the bottom edge.
        int inv_hover = -1;
        float inv_y = (float)GetScreenHeight() - 30;
        for (uint32_t i = 0; can_act && i < vm->inventory_count; i++) {
            Rectangle cell = {8 + 150.0f * i, inv_y, 146, 24};
            if (CheckCollisionPointRec(mouse, cell)) inv_hover = (int)i;
        }

        int menu_hover = -1;
        for (int i = 0; i < g->menu_count; i++)
            if (CheckCollisionPointRec(mouse, menu_row(g, i))) menu_hover = i;
        if (g->menu_count > 0) {
            // The dialogue waits for the player's line: click it or press
            // its number.
            int pick = clicked ? menu_hover : -1;
            for (int k = 0; k < g->menu_count && k < 9; k++) if (IsKeyPressed(KEY_ONE + k)) pick = k;
            if (pick >= 0 && petka_dialoguevm_choose(&g->dvm, pick) == 0) {
                g->menu_count = 0;
                next_phrase(g);
            }
        } else if (g->talking) {
            int voiced = g->voice.frameCount > 0;
            if (clicked || IsKeyPressed(KEY_SPACE) ||
                (voiced ? !IsSoundPlaying(g->voice) : GetTime() >= g->line_until))
                next_phrase(g);
        } else if (can_act) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (vm->cursor_object != PETKA_NONE) vm->cursor_object = PETKA_NONE;
                else g->verb = (g->verb + 1) % VERB_COUNT;
            }
            if (clicked) {
                if (inv_hover >= 0) {
                    vm->cursor_object = vm->inventory[inv_hover];
                } else if (hover && vm->cursor_object != PETKA_NONE) {
                    stop_heroes(g);
                    petka_scriptvm_post(vm, hover->id, PETKA_OP_OBJECTUSE, 0, 0, 0, vm->cursor_object);
                    vm->cursor_object = PETKA_NONE;
                } else if (hover) {
                    uint16_t op = kVerbs[g->verb].op;
                    int point = op == PETKA_OP_WALK || op == PETKA_OP_OBJECTUSE;
                    stop_heroes(g);
                    petka_scriptvm_post(vm, hover->id, op, point ? (uint16_t)mx : 0, point ? (uint16_t)my : 0, 0,
                                        op == PETKA_OP_OBJECTUSE ? (int)PETKA_ID_CHAPAYEV : (int)hover->id);
                } else if (my < inv_y / scale) {
                    PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
                    if (petka && host_walk(g, PETKA_ID_PETKA, (int32_t)mx, (int32_t)my)) petka->busy = 1;
                }
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode2D(view);
        if (g->bg_tex.id)
            DrawTexturePro(g->bg_tex, (Rectangle){0, 0, (float)g->bg_tex.width, (float)g->bg_tex.height},
                           (Rectangle){0, 0, g->bg_tex.width * scale, g->bg_tex.height * scale}, (Vector2){0, 0}, 0, WHITE);
        for (int i = 0; i < scene_count; i++) {
            const Visual *v = &g->visuals[obj_index(g, scene[i].obj)];
            Rectangle d = visual_dest(g, scene[i].obj);
            DrawTexturePro(v->tex, (Rectangle){0, 0, (float)v->flic->width, (float)v->flic->height},
                           (Rectangle){d.x * scale, d.y * scale, d.width * scale, d.height * scale},
                           (Vector2){0, 0}, 0, WHITE);
            if (debug && !is_hero(scene[i].obj->id)) {
                Rectangle r = {v->rect.left * scale, v->rect.top * scale,
                               (v->rect.right - v->rect.left) * scale, (v->rect.bottom - v->rect.top) * scale};
                DrawRectangleLinesEx(r, 1, scene[i].obj == hover ? ORANGE : Fade(LIME, 0.6f));
                DrawTextEx(font, TextFormat("%u %s", scene[i].obj->id, utf8_name(scene[i].obj->name)),
                           (Vector2){r.x + 2, r.y + 2}, 14, 1, YELLOW);
            }
        }

        EndMode2D();

        // Top bar: what a click would do.
        const char *action;
        if (!vm->interface_enabled || g->seq_room != PETKA_NONE) action = "...";
        else if (vm->cursor_object != PETKA_NONE) {
            const PetkaObject *item = petka_script_find(g->script, vm->cursor_object);
            action = TextFormat("%s -> %s", item ? display_name(g, item) : "?",
                                hover && display_name(g, hover)[0] ? display_name(g, hover) : "...");
        } else {
            const char *name = hover ? display_name(g, hover) : "";
            action = TextFormat("%s%s%s", kVerbs[g->verb].label, name[0] ? ": " : "", name);
        }
        DrawRectangle(0, 0, GetScreenWidth(), 30, Fade(BLACK, 0.6f));
        DrawTextEx(font, action, (Vector2){10, 5}, 20, 1, ORANGE);
        if (room) {
            const char *name = display_name(g, room);
            Vector2 size = MeasureTextEx(font, name, 18, 1);
            DrawTextEx(font, name, (Vector2){GetScreenWidth() - size.x - 10, 6}, 18, 1, SKYBLUE);
        }

        if (g->talking && g->line[0]) {
            // Word-wrap the phrase into the box.
            float box_w = (float)GetScreenWidth() - 40, y = (float)GetScreenHeight() - 150;
            DrawRectangle(10, (int)y - 10, GetScreenWidth() - 20, 110, Fade(BLACK, 0.7f));
            // Each speaker's lines are in his own colour.
            const PetkaObject *talker = petka_script_find(g->script, g->talker);
            Color ink = talker ? g->colors[obj_index(g, talker)] : WHITE;
            char row[512] = "";
            const char *p = g->line;
            while (*p) {
                const char *space = strchr(p, ' ');
                size_t word = space ? (size_t)(space - p) + 1 : strlen(p);
                char trial[512];
                snprintf(trial, sizeof(trial), "%s%.*s", row, (int)word, p);
                if (row[0] && MeasureTextEx(font, trial, 24, 1).x > box_w) {
                    DrawTextEx(font, row, (Vector2){20, y}, 24, 1, ink);
                    y += 28;
                    row[0] = 0;
                    continue;
                }
                snprintf(row, sizeof(row), "%s", trial);
                p += word;
            }
            DrawTextEx(font, row, (Vector2){20, y}, 24, 1, ink);
        }

        for (int i = 0; i < g->menu_count; i++) {
            Rectangle row = menu_row(g, i);
            char text[512];
            cp1251_to_utf8(g->menu[i] < g->phrases->count ? g->phrases->phrases[g->menu[i]].text : "?",
                           text, sizeof(text));
            DrawRectangleRec(row, Fade(BLACK, i == menu_hover ? 0.9f : 0.7f));
            if (i == menu_hover) DrawRectangleLinesEx(row, 1, ORANGE);
            DrawTextEx(font, TextFormat("%d. %s", i + 1, text), (Vector2){row.x + 10, row.y + 5}, 22, 1,
                       i == menu_hover ? ORANGE : WHITE);
        }

        for (uint32_t i = 0; !g->menu_count && i < vm->inventory_count; i++) {
            const PetkaObject *item = petka_script_find(g->script, vm->inventory[i]);
            Rectangle cell = {8 + 150.0f * i, inv_y, 146, 24};
            int picked = vm->cursor_object == vm->inventory[i];
            DrawRectangleRec(cell, Fade(picked ? ORANGE : BLACK, 0.7f));
            DrawRectangleLinesEx(cell, 1, (int)i == inv_hover ? ORANGE : GRAY);
            if (item) DrawTextEx(font, display_name(g, item), (Vector2){cell.x + 4, cell.y + 4}, 16, 1, WHITE);
        }

        if (g->status[0] && GetTime() < g->status_until)
            DrawTextEx(font, g->status, (Vector2){10, 36}, 18, 1, YELLOW);
        if (debug) {
            // The floor's polygons, and the route each hero is following.
            BeginMode2D(view);
            for (uint32_t i = 0; g->mesh && i < g->mesh->poly_count; i++) {
                const PetkaWalkPoly *poly = &g->mesh->polys[i];
                for (uint32_t k = 0; k < poly->count; k++) {
                    const PetkaPoint *a = &g->mesh->points[poly->verts[k]];
                    const PetkaPoint *b = &g->mesh->points[poly->verts[(k + 1) % poly->count]];
                    DrawLineEx((Vector2){a->x * scale, a->y * scale}, (Vector2){b->x * scale, b->y * scale},
                               1, Fade(SKYBLUE, 0.8f));
                }
            }
            for (uint16_t id = 0; id < 2; id++) {
                const Walk *w = &g->walk[id];
                const PetkaObjectState *hero = petka_scriptvm_state(vm, id);
                Vector2 from = {hero ? hero->x * scale : 0, hero ? hero->y * scale : 0};
                for (int i = w->walker.next; w->active && hero && i < w->walker.count; i++) {
                    Vector2 to = {w->walker.route[i].x * scale, w->walker.route[i].y * scale};
                    DrawLineEx(from, to, 3, id ? MAGENTA : RED);
                    DrawCircleV(to, 5, id ? MAGENTA : RED);
                    from = to;
                }
            }
            EndMode2D();
            const PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
            DrawTextEx(font, TextFormat("room %u  objects %d  queue %u  view %d  mouse %d,%d  petka %d,%d %s",
                                        vm->room, scene_count, vm->queue_count, (int)g->camera_x, (int)mx, (int)my,
                                        petka ? petka->x : 0, petka ? petka->y : 0,
                                        petka && petka->visible ? "" : "(hidden)"),
                       (Vector2){10, 58}, 16, 1, LIME);
        }
        EndDrawing();
        if (shot_path && frame >= shot_frame) {
            // Not TakeScreenshot(): it only writes below the working directory.
            Image shot = LoadImageFromScreen();
            ExportImage(shot, shot_path);
            UnloadImage(shot);
            break;
        }
    }

    for (uint32_t i = 0; i < g->script->object_count; i++) visual_free(&g->visuals[i]);
    free(g->visuals);
    for (uint32_t i = 0; i < g->script->object_count; i++) free(g->names[i]);
    free(g->names);
    free(g->colors);
    for (int i = 0; i < g->gait_count; i++) petka_gait_close(g->gaits[i].gait);
    if (g->bg_tex.id) UnloadTexture(g->bg_tex);
    petka_walkmesh_close(g->mesh);
    if (g->voice.frameCount > 0) UnloadSound(g->voice);
    for (int i = 0; i < MAX_SOUNDS; i++) if (g->sounds[i].frameCount > 0) UnloadSound(g->sounds[i]);
    if (g->music.frameCount > 0) UnloadMusicStream(g->music);
    if (g->ambience.frameCount > 0) UnloadMusicStream(g->ambience);
    if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
    CloseAudioDevice();
    CloseWindow();

    petka_scriptvm_close(g->vm);
    petka_script_close(g->script);
    if (g->dtree) petka_dialoguetree_close(g->dtree);
    if (g->phrases) petka_phrasetable_close(g->phrases);
    if (g->topo) petka_topology_close(g->topo);
    for (int i = 0; i < 65536; i++) free(g->qrc[i]);
    for (int i = 0; i < g->store_count; i++) petka_store_close(g->stores[i]);
    return 0;
}
