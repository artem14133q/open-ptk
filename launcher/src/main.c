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
// Films are played through ffmpeg, if there is one.
#include "petka/bmp.h"
#include "petka/dialogue.h"
#include "petka/flic.h"
#include "petka/ini.h"
#include "petka/msk.h"
#include "petka/save.h"
#include "petka/script.h"
#include "petka/store.h"
#include "petka/topology.h"
#include "petka/walk.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>

#include "film.h"
#include "platform.h"
#endif

#define GAME_W 640
#define GAME_H 480
#define MAX_STORES 8
#define MAX_SOUNDS 8
// Scrolling of backgrounds wider than the screen: when Petka comes within
// SCROLL_MARGIN of either side of the screen the view is asked to centre on
// him (the exe's rule and number). The view then moves there 8 pixels a
// frame (the exe's frame function, sub_422140), i.e. SCROLL_SPEED at 60 fps.
#define SCROLL_MARGIN 160.0f
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
    PetkaMskRect area; // the part of the frame the texture holds
    const uint8_t *msk; // the clip's .MSK, NULL if it has none
    size_t msk_len;
    double next_frame;
    int sound_pending; // play the clip's sound when its first frame is up
    int restart;       // rewind before the next frame
    int live;    // drawn this frame
} Visual;

#define SAVE_SLOTS 6
// The menu line under the pointer. The exe's is pure blue (0xFF0000 in its
// BGR), which is hard to read on the dark band behind the menu; a lighter
// blue keeps the look and reads well.
#define BLUE_LINE ((Color){110, 160, 255, 255})
#define NO_CAST_INK ((Color){10, 10, 10, 255}) // a subtitle whose speaker has no CAST.INI colour
#define MAX_WAYPOINTS 32
#define MAX_GAITS 8

// A hero on the move: a route across the room's walk mesh, walked one step
// per frame of the walk clip. Both follow the exe (petka/walk.h).
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
    Visual cursor;   // the pointer's own clip
    // The case: the game's panel, with the things carried.
    int map_open;    // a room of the script is up as a screen of its own:
                     // the map, or the panel
    int panel_open;  // ... and it is the panel
    int panel_hover; // which of the panel's objects the pointer is on, -1 = none
    int tracks_hushed; // the room's music and ambience are paused for a sequence
    int map_verb;    // the cursor's action to give back when it is left
    float map_camera; // and the view's scroll
    int subtitles;   // the panel's switch: phrases are shown as text
    int boot_load;   // "load" was picked at start-up: no room has been entered yet
    // The star: the menu of actions the right button puts up at the pointer.
    int star_open;
    int star_x, star_y; // its corner on screen
    int star_lit;       // the part under the pointer, 0..5, or 6 for none
    // With a controller (plat_pad) the cursor is the plain pointer: A on an
    // object puts the star up for it, the stick picks a part, A does it.
    // While R is held the cursor sends Chapayev instead, the one action the
    // star has not.
    uint16_t pad_target; // the object the star is up for
    float pad_x, pad_y;  // and where it was clicked, in the room
    Visual star;
    // A thing's description (SHOWDESCRIPTION), up until the next click.
    char desc[2048]; // UTF-8, "" = none
    int desc_frame;  // the message's number: a frame of the description clip
    Visual desc_clip;
    // The part of the game that is loaded, and the way to another.
    char game_dir[1024];
    char part[16];      // "PART_n", the folder its files are under
    int part_number;
    int chapter;
    int chapter_store;  // which of `stores` is the chapter's, -1 = none
    PetkaScriptHost host;
    int switch_part, switch_chapter; // load this part at the top of the next
                                     // frame; 0 = stay
    char pending_load[64];           // ... and then this saved game
    // Saved games.
    int save_mode;   // the save screen is up: 1 loading, 2 saving
    int save_page;   // 0..2, six slots a page
    Texture2D save_bg;
    struct { int present; Texture2D tex; char label[96]; } save_slot[SAVE_SLOTS];
    uint8_t thumb[PETKA_SAVE_THUMB_BYTES]; // the room as it was when the panel came up
    // The panel's four dials, as the exe keeps them ("Options" in its
    // profile): the number of the dial clip's frame, from 1.
    int dial[4];     // speech 1..31, music 1..41, effects 1..31, speed 1..26
    int case_open;
    int case_first;  // number of the first thing shown, a page is six
    int case_lit;    // the button under the pointer, -1 = none
    Visual case_body, case_exit, case_strip, case_button[6];
    // With a controller the star's middle opens the case to pick a thing
    // for the object the star was up for: a thing clicked is used on it at
    // once. There the lid's Chapayev, diskette and map do nothing and are
    // grey.
    uint16_t case_for;  // that object, PETKA_NONE = the case as usual
    // With a controller the pointer in the case goes from thing to thing
    // only; L and R turn the pages, B shuts it.
    int case_focus;     // the place on the page (0..5) the pointer is on
    Texture2D case_grey[3]; // those three buttons' clips in grey
    Texture2D bg_tex;
    float camera_x;         // left edge of the view, in background pixels
    float camera_target;    // where the view has been asked to move to
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
    // With a controller (plat_pad) the pointer is held from a dialogue's
    // first phrase to its end, and then goes back to where it was; while a
    // menu is up it sits on the line picked out by up and down.
    int menu_focus;    // that line
    int pad_hold;      // the pointer is held
    Vector2 pad_return; // where it goes back to
    int auto_pick;     // dev: line to pick without asking; -1 = the last one, -2 = ask
    int dialogue_done; // tell the VM on the next frame
    char line[512];    // current phrase, UTF-8
    double line_until;
    Sound voice;

    Music music, ambience;
    uint16_t music_id, ambience_id;
    // A sequence's own tracks, apart from the room's (the sequence screen
    // keeps what it last played, +36 and +40).
    Music seq_music, seq_ambience;
    uint16_t seq_music_id, seq_ambience_id;
    int seq_tracks_paused;
    Sound sounds[MAX_SOUNDS];
    const uint8_t *sound_src[MAX_SOUNDS]; // which file each slot holds
    unsigned char sound_paused[MAX_SOUNDS]; // stopped for a sequence, to go on after it
    int voice_paused;

    int verb;
    int quit;
    int to_menu;      // the game is over: back to the start-up menu
    int menu_on_back; // the save screen came from that menu: leaving it goes back there
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
// They are in the exe's own order -- the index is the cursor's action number
// (+128), and its clip is resource 5002 + action -- and, like the exe's
// cursor (sub_402280), the game starts on the first one, "look".
static const struct { uint16_t op; const char *label; } kVerbs[] = {
    {PETKA_OP_LOOK, "СМОТРЕТЬ"}, {PETKA_OP_WALK, "ИДТИ"}, {PETKA_OP_USE, "ПРИМЕНИТЬ"},
    {PETKA_OP_TAKE, "ВЗЯТЬ"}, {PETKA_OP_TALK, "ГОВОРИТЬ"}, {PETKA_OP_OBJECTUSE, "ЧАПАЕВ"},
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
    // The exe draws all its text in bold Arial (two fonts made at start-up,
    // sub_410CD0: 20 and 16 pixels high); take that where it is to be had.
    static const char *candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
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

    Font system = plat_system_font(32, codepoints, n);
    if (system.glyphCount > 0) return system;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!FileExists(candidates[i])) continue;
        Font f = LoadFontEx(candidates[i], 32, codepoints, n);
        if (f.glyphCount > 0) return f;
    }
    fprintf(stderr, "no Cyrillic-capable font found -- text will render as '?'\n");
    return GetFontDefault();
}

// The game's clock. Normally the real one; an unattended run can ask for a
// fixed 60 steps a second with no waiting in between (PETKA_TURBO), so that
// minutes of cutscene go by in seconds and runs repeat exactly.
static double s_clock;
static int s_turbo;
static double game_now(void) { return s_turbo ? s_clock : GetTime(); }
static float game_dt(void) { return s_turbo ? 1.0f / 60.0f : GetFrameTime(); }

static void set_status(Game *g, const char *text) {
    snprintf(g->status, sizeof(g->status), "%s", text);
    g->status_until = game_now() + 4.0;
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

// Lets a resource's bytes go once a copy of them is all that is kept (a
// voice line made into a sound, a background made into a texture): the
// stores are read on demand, and what is read stays until released.
static void res_release(const Game *g, const char *path) {
    for (int i = 0; i < g->store_count; i++) {
        const PetkaEntry *e = petka_store_find(g->stores[i], path);
        if (e) { petka_store_entry_release(g->stores[i], e); return; }
    }
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

// The part of `flic`'s frame `area` in RGBA, in a buffer that is reused
// from call to call.
static const uint8_t *flic_to_rgba(const PetkaFlic *flic, PetkaMskRect area) {
    static uint8_t *rgba;
    static size_t cap;
    int w = area.right - area.left, h = area.bottom - area.top;
    size_t need = (size_t)w * h * 4;
    if (need > cap) {
        free(rgba);
        rgba = (uint8_t *)malloc(need);
        cap = rgba ? need : 0;
        if (!rgba) return NULL;
    }
    // The colour key is palette entry 0's colour, not the entry itself.
    uint32_t colour[256];
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = flic->palette[i];
        uint8_t px[4] = {c[0], c[1], c[2], petka_flic_keyed(flic, (uint8_t)i) ? 0 : 255};
        memcpy(&colour[i], px, 4);
    }
    uint32_t *out = (uint32_t *)rgba;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = flic->indices + (size_t)(area.top + y) * flic->width + area.left;
        for (int x = 0; x < w; x++) *out++ = colour[row[x]];
    }
    return rgba;
}

// The part of a clip's frame its texture holds: the MSK's covering rect
// with a pixel to spare all round (the files give the bottom edge
// inclusive), or the whole frame when there is no usable rect -- two
// explosions' files have it upside down. Checked on every clip of the game:
// no opaque pixel of any frame falls outside. Most objects are small things
// in full-screen clips, so this spares most of the converting, uploading
// and drawing.
static PetkaMskRect clip_area(const PetkaFlic *flic, PetkaMskRect r) {
    r.left -= 1; r.top -= 1; r.right += 1; r.bottom += 1;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > flic->width) r.right = flic->width;
    if (r.bottom > flic->height) r.bottom = flic->height;
    if (r.right <= r.left || r.bottom <= r.top) r = (PetkaMskRect){0, 0, flic->width, flic->height};
    return r;
}

static Texture2D load_bmp_texture(const Game *g, const char *path) {
    Texture2D tex = {0};
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    int w = 0, h = 0;
    uint8_t *rgb = bytes ? petka_bmp_decode(bytes, len, &w, &h) : NULL;
    if (bytes) res_release(g, path);
    if (!rgb) { fprintf(stderr, "background not found: %s\n", path); return tex; }
    Image img = {.data = rgb, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

// The dials' ranges and what the exe makes of them (sub_41C5A0): a
// percentage, (value - 1) * 100 / (top - 1). Their first values are the
// defaults the exe reads its profile with.
enum { DIAL_SPEECH, DIAL_MUSIC, DIAL_SFX, DIAL_SPEED };
static const int kDialTop[4] = {31, 41, 31, 26};
static const int kDialDefault[4] = {31, 20, 23, 13};

static int dial_percent(const Game *g, int which) {
    return (g->dial[which] * 100 - 100) / (kDialTop[which] - 1);
}

// A volume dial as a gain. The exe (sub_40EB70) gives DirectSound
// -(10000 - 10000 * ln(2.75 * percent) / ln(275)) hundredths of a decibel:
// nothing off at 100, about 13 dB off at 47, and silence at 0.
static float dial_gain(const Game *g, int which) {
    int percent = dial_percent(g, which);
    if (percent <= 0) return 0.0f;
    if (percent >= 100) return 1.0f;
    double centibels = -(10000.0 - 10000.0 * log(2.75 * percent) / log(275.0));
    return (float)pow(10.0, centibels / 2000.0);
}

static void play_sound(Game *g, const char *path) {
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    if (!bytes) return;
    // The exe's sound manager keeps one buffer per file (a map by name), so
    // a sound started again starts over; it is never heard twice at once.
    // Started anew each time, a looping clip's sound piled up into a loud
    // rasp (the bar in part 3), and a door clicked twice creaked twice.
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount <= 0 || g->sound_src[i] != bytes) continue;
        StopSound(g->sounds[i]);
        g->sound_paused[i] = 0;
        SetSoundVolume(g->sounds[i], dial_gain(g, DIAL_SFX));
        PlaySound(g->sounds[i]);
        return;
    }
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount > 0 && (IsSoundPlaying(g->sounds[i]) || g->sound_paused[i])) continue;
        if (g->sounds[i].frameCount > 0) UnloadSound(g->sounds[i]);
        Wave wave = LoadWaveFromMemory(".wav", bytes, (int)len);
        g->sounds[i] = LoadSoundFromWave(wave);
        g->sound_src[i] = bytes;
        UnloadWave(wave);
        SetSoundVolume(g->sounds[i], dial_gain(g, DIAL_SFX));
        PlaySound(g->sounds[i]);
        return;
    }
}

// raylib streams a WAV as 16- or 32-bit samples only: an 8-bit one comes out
// as noise -- a loud rasp. One track of the game is 8-bit (part 3's bar,
// MUSIC3\SPACEBAR.wav, stereo at 11025 Hz). Such a file is turned into a
// 16-bit one for the stream, once; the copy is kept, as the stream reads
// from it while it plays.
static const uint8_t *wav_for_stream(const uint8_t *bytes, size_t len, size_t *out_len) {
    static struct { const uint8_t *from; uint8_t *to; size_t len; } cache[8];
    *out_len = len;
    for (int i = 0; i < 8; i++)
        if (cache[i].from == bytes) { *out_len = cache[i].len; return cache[i].to; }
    if (len < 12 || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4)) return bytes;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *data = NULL;
    uint32_t data_len = 0;
    for (size_t o = 12; o + 8 <= len;) {
        uint32_t n = (uint32_t)bytes[o + 4] | (uint32_t)bytes[o + 5] << 8 | (uint32_t)bytes[o + 6] << 16 | (uint32_t)bytes[o + 7] << 24;
        if (!memcmp(bytes + o, "fmt ", 4) && n >= 16 && o + 8 + 16 <= len) {
            const uint8_t *f = bytes + o + 8;
            format = (uint16_t)(f[0] | f[1] << 8);
            channels = (uint16_t)(f[2] | f[3] << 8);
            rate = (uint32_t)f[4] | (uint32_t)f[5] << 8 | (uint32_t)f[6] << 16 | (uint32_t)f[7] << 24;
            bits = (uint16_t)(f[14] | f[15] << 8);
        } else if (!memcmp(bytes + o, "data", 4)) {
            data = bytes + o + 8;
            data_len = n <= len - (o + 8) ? n : (uint32_t)(len - (o + 8));
            break;
        }
        o += 8 + (size_t)n + (n & 1);
    }
    if (format != 1 || bits != 8 || !data || !channels) return bytes;
    size_t total = 44 + (size_t)data_len * 2;
    uint8_t *out = (uint8_t *)malloc(total);
    if (!out) return bytes;
    uint32_t block = channels * 2u, byte_rate = rate * block, body = data_len * 2u;
    uint8_t head[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0};
    uint32_t riff = (uint32_t)total - 8;
    memcpy(head + 4, &riff, 4); // little-endian host
    memcpy(head + 22, &channels, 2);
    memcpy(head + 24, &rate, 4);
    memcpy(head + 28, &byte_rate, 4);
    uint16_t block16 = (uint16_t)block, bits16 = 16;
    memcpy(head + 32, &block16, 2);
    memcpy(head + 34, &bits16, 2);
    memcpy(head + 36, "data", 4);
    memcpy(head + 40, &body, 4);
    memcpy(out, head, 44);
    for (uint32_t i = 0; i < data_len; i++) {
        int16_t s = (int16_t)(((int)data[i] - 128) << 8); // 8-bit WAV samples are unsigned
        memcpy(out + 44 + i * 2, &s, 2);
    }
    for (int i = 0; i < 8; i++)
        if (!cache[i].from) {
            cache[i].from = bytes;
            cache[i].to = out;
            cache[i].len = total;
            break;
        }
    *out_len = total;
    return out;
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
    bytes = wav_for_stream(bytes, len, &len);
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

// Loads a clip by its path in the stores (for the few things that are not
// in the running part's resource table, like the start-up screen).
static void visual_load_path(Game *g, Visual *v, const char *path);

static void visual_load(Game *g, Visual *v, uint16_t resource) {
    visual_free(v);
    v->resource = resource;
    v->sound_pending = 1;
    visual_load_path(g, v, g->qrc[resource]);
}

static void visual_load_path(Game *g, Visual *v, const char *path) {
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
    v->msk = mbytes;
    v->msk_len = mlen;

    v->area = clip_area(v->flic, v->rect);
    const uint8_t *rgba = flic_to_rgba(v->flic, v->area);
    if (!rgba) return;
    Image img = {.data = (void *)rgba, .width = v->area.right - v->area.left, .height = v->area.bottom - v->area.top,
                 .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    v->tex = LoadTextureFromImage(img);
    v->next_frame = game_now() + (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
}

// Brings the texture up to the clip's current frame.
static void visual_upload(Visual *v) {
    const uint8_t *rgba = v->tex.id ? flic_to_rgba(v->flic, v->area) : NULL;
    if (rgba) UpdateTexture(v->tex, rgba);
}

// Draws the clip's frame so that the whole frame would cover `frame` (on
// the screen): only the part the texture holds is drawn.
static void visual_draw(const Visual *v, Rectangle frame) {
    if (!v->tex.id) return;
    float sx = frame.width / v->flic->width, sy = frame.height / v->flic->height;
    DrawTexturePro(v->tex, (Rectangle){0, 0, (float)v->tex.width, (float)v->tex.height},
                   (Rectangle){frame.x + v->area.left * sx, frame.y + v->area.top * sy, v->tex.width * sx,
                               v->tex.height * sy},
                   (Vector2){0, 0}, 0, WHITE);
}

// The pointer. The exe's cursor is an object with a clip (sub_402280): one
// per action, resources 5002.., or the clip a CURSOR message gave it when it
// carries an inventory item. Moving it (sub_402570) puts the middle of the
// clip's MSK rect on the mouse; drawing it (sub_4023D0) puts the frame's
// corner at the cursor's position. The clip only runs while the pointer is
// over an object, otherwise it is held on its first frame.
#define CURSOR_POINTER 5001
#define CURSOR_ACTION0 5002
// Brings a clip that belongs to no script object (the cursor, the case and
// its buttons) to `resource`. With `run` it plays; without, it is held on
// its first frame -- a clip that has moved on is loaded afresh.
static void clip_show(Game *g, Visual *v, uint16_t resource, int run) {
    int rewind = !run && v->flic && v->flic->frame_index != 0;
    if (v->resource != resource || rewind) visual_load(g, v, resource);
    if (!v->flic || !run || v->flic->frame_count <= 1 || game_now() < v->next_frame) return;
    v->next_frame = game_now() + (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
    petka_flic_advance(v->flic);
    visual_upload(v);
}

// `centre` is the last argument of the exe's cursor move: the room's mouse
// function passes 1, the map's 0 -- there the frame's corner is the point.
static void draw_cursor(Game *g, uint16_t resource, int over_object, int centre, Vector2 mouse, float scale) {
    Visual *v = &g->cursor;
    clip_show(g, v, resource, over_object);
    if (!v->flic) return;
    int x = (int)(mouse.x / scale), y = (int)(mouse.y / scale);
    if (centre) {
        x -= v->rect.left + (v->rect.right - v->rect.left) / 2;
        y -= v->rect.top + (v->rect.bottom - v->rect.top) / 2;
    }
    visual_draw(v, (Rectangle){x * scale, y * scale, v->flic->width * scale, v->flic->height * scale});
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
    // A hidden object that is still active is an invisible hot spot -- room
    // exits are made this way (SET a clip, then HIDE) -- so its clip is
    // needed for the pointer even though it is never drawn. The exe loads a
    // clip on the same condition (visible or active).
    if ((!st->visible && !st->active) || st->resource == PETKA_NONE) return;
    if (v->resource != st->resource || v->restart) {
        visual_load(g, v, st->resource);
        v->restart = 0;
    }
    if (!v->flic) return;
    v->live = 1;
    if (!st->animate || !st->visible) return;
    if (v->flic->frame_count <= 1) {
        // Ours, not the exe's: a script waiting on a one-frame clip would
        // wait forever, so that wait alone is ended.
        if (st->play_tail.present && st->play_wait == v->resource)
            petka_scriptvm_animation_ended(g->vm, obj->id, v->resource);
        return;
    }
    if (game_now() < v->next_frame) return;
    double period = (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
    // A walking hero's clock runs faster by the speed dial: the exe
    // multiplies the time that has passed by (percent + 50) / 50.
    if (is_hero(obj->id) && g->walk[obj->id].active) period *= 50.0 / (dial_percent(g, DIAL_SPEED) + 50.0);
    v->next_frame = game_now() + period;

    if (v->sound_pending && v->flic->frame_index == 0) {
        char wav[512];
        swap_ext(g->qrc[v->resource], "WAV", wav, sizeof(wav));
        play_sound(g, wav);
        v->sound_pending = 0;
    }
    petka_flic_advance(v->flic);
    visual_upload(v);

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
        g->colors[i] = NO_CAST_INK;
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
// (from the clip's .LEG file) sits on his position, scaled, so the feet do
// not slide -- the exe's sub_424190. Otherwise -- standing, talking, acting
// -- the clip stands on the position by the middle of its bottom edge. The
// two rules do not agree to the pixel, so he shifts slightly as a walk
// starts or ends, in the original as here.
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
    // The hero's set-position (sub_4087D0): the frame, scaled for his row,
    // stands on the position by the middle of its bottom edge.
    float s = hero_scale(g, (float)st->y);
    int width = (int)((float)v->flic->width * s), height = (int)((float)v->flic->height * s);
    return (Rectangle){(float)(st->x - width / 2), (float)(st->y - height), (float)width, (float)height};
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
    if (!st || hero_id > 1) return 0;
    if (!st->visible) {
        // Nobody sees him go: he is simply there.
        st->x = x;
        st->y = y;
        return 0;
    }
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
    // A walking hero is animated, whatever held him still before: he takes
    // a step for each frame of the walk clip, so a frozen one never moves.
    st->animate = 1;
    if (g->trace) {
        fprintf(stderr, "walk %u: from %d,%d to %d,%d; route:", hero_id, st->x, st->y, x, y);
        for (int i = 0; i < w->walker.count; i++) fprintf(stderr, " (%d,%d)", w->walker.route[i].x, w->walker.route[i].y);
        fprintf(stderr, "; dir %d steps %d\n", w->walker.dir, w->walker.steps_left);
    }
    // Walk clips are costume+10 .. costume+17: N, NE, E, SE, S, SW, W, NW.
    st->resource = (uint16_t)(st->costume + 10 + w->walker.dir);
    // Loaded here and now: until the next frame the old clip's picture would
    // be drawn with the new clip's foothold.
    visual_load(g, &g->visuals[obj_index(g, petka_script_find(g->script, hero_id))], st->resource);
    return 1;
}

// Objects, clips, walk meshes and hero positions are all in background
// pixels; only the view moves. It follows Petka, and stays within the
// background.
static void camera_update(Game *g, float dt) {
    float max_x = g->bg_tex.id ? (float)g->bg_tex.width - GAME_W : 0;
    if (max_x < 0) max_x = 0;
    if (g->seq_room != PETKA_NONE) { g->camera_x = g->camera_target = 0; return; } // sequences are one screen
    const PetkaObjectState *petka = petka_scriptvm_state(g->vm, PETKA_ID_PETKA);
    // The exe's rule (in the hero's set-position, sub_4087D0): once Petka is
    // within SCROLL_MARGIN of either side of the screen, the view is asked
    // to centre on him.
    if (petka && g->camera_snap) {
        g->camera_target = (float)petka->x - GAME_W / 2.0f;
    } else if (petka && petka->visible) {
        float on_screen = (float)petka->x - g->camera_x;
        if (on_screen < SCROLL_MARGIN || on_screen > GAME_W - SCROLL_MARGIN)
            g->camera_target = (float)petka->x - GAME_W / 2.0f;
    }
    if (g->camera_target < 0) g->camera_target = 0;
    if (g->camera_target > max_x) g->camera_target = max_x;

    float step = g->camera_snap ? max_x + GAME_W : SCROLL_SPEED * dt;
    g->camera_snap = 0;
    if (g->camera_target > g->camera_x + step) g->camera_x += step;
    else if (g->camera_target < g->camera_x - step) g->camera_x -= step;
    else g->camera_x = g->camera_target;
    if (g->camera_x > max_x) g->camera_x = max_x;
}

// Both heroes stop where they are. The exe does this on every click on an
// object, before posting the click's message.
static void stop_heroes(Game *g) {
    petka_scriptvm_stop_hero(g->vm, PETKA_ID_PETKA);
    petka_scriptvm_stop_hero(g->vm, PETKA_ID_CHAPAYEV);
}

static void host_stop(void *user, uint16_t hero_id) {
    if (hero_id < 2) ((Game *)user)->walk[hero_id].active = 0;
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
        visual_load(g, &g->visuals[obj_index(g, petka_script_find(g->script, hero_id))], st->resource);
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

// Where a speaker's voice sits between the speakers, -1 (left) to 1 (right).
// The exe (sub_40E0D0) gives DirectSound a pan of 2500 * (2x - w) / (2w)
// hundredths of a decibel, x being the middle of the speaker's clip and w
// the screen's width: up to 12.5 dB off the far channel at either edge.
static float speaker_pan(const Game *g, uint16_t id) {
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

static void next_phrase(Game *g) {
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
static void map_leave(Game *g);

static void host_room_changed(void *user, uint16_t prev_id, uint16_t room_id) {
    Game *g = (Game *)user;
    // A place picked on the map: the map is done with.
    map_leave(g);
    const PetkaObject *room = petka_script_find(g->script, room_id);
    const PetkaObject *prev = prev_id != PETKA_NONE ? petka_script_find(g->script, prev_id) : NULL;
    if (!room) return;
    load_perspective(g, room->name);
    load_mesh(g);
    g->camera_snap = 1;
    // (Both heroes were stopped by the VM before the room changed.)
    g->walk[0].active = g->walk[1].active = 0;
    // The clips of the room that is left go, and their sounds with them:
    // nothing of it is heard in the next one. (The voice is the dialogue's
    // and may well go on across a door.)
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount > 0) StopSound(g->sounds[i]);
        g->sound_paused[i] = 0;
    }

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

static void chapter_open(Game *g, int chapter);

// What a sequence does to the sounds that are playing (the exe's sound
// manager, sub_40E2C0 and sub_40E3F0, called from the sequence screen's
// set-up and leave): on the way in everything playing is stopped where it is
// and marked; on the way out what the sequence itself started is stopped
// and what was marked plays on.
static void sequence_sounds(Game *g, int entering) {
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount <= 0) { g->sound_paused[i] = 0; continue; }
        if (entering) {
            if (!IsSoundPlaying(g->sounds[i]) || g->sound_paused[i]) continue;
            // One sequence straight after another: the first one's sounds go.
            if (g->seq_room != PETKA_NONE) StopSound(g->sounds[i]);
            else { PauseSound(g->sounds[i]); g->sound_paused[i] = 1; }
        } else if (g->sound_paused[i]) {
            ResumeSound(g->sounds[i]);
            g->sound_paused[i] = 0;
        } else if (IsSoundPlaying(g->sounds[i])) {
            StopSound(g->sounds[i]);
        }
    }
    if (g->voice.frameCount <= 0) { g->voice_paused = 0; return; }
    if (entering && g->seq_room == PETKA_NONE && IsSoundPlaying(g->voice)) {
        PauseSound(g->voice);
        g->voice_paused = 1;
    } else if (!entering && g->voice_paused) {
        ResumeSound(g->voice);
        g->voice_paused = 0;
    }
}

// ---- films ----
//
// The game's videos are loose .AVI files next to it, not in the stores. The
// exe (sub_4124A0) hands one to the system's player -- MCI: "open <file>
// alias film", stretched to the 640x480 window -- with its own sounds paused,
// and sits in a loop until the film ends or a key or a mouse button is
// pressed. The one the script plays, INTRO.AVI, is Indeo 3 video, which
// nothing here decodes; so, in the same spirit, the launcher hands the file
// to FFmpeg (film.h) and shows the frames it sends back.
#define FILM_FPS 24

static int find_film(const Game *g, const char *name, char *out, size_t out_sz) {
    static const char *dirs[] = {"", "data1_out/Main/", "Main/"};
    char lower[256];
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(lower); n++)
        lower[n] = name[n] >= 'A' && name[n] <= 'Z' ? (char)(name[n] + 32) : name[n];
    lower[n] = 0;
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        const char *tries[2] = {name, lower};
        for (int t = 0; t < 2; t++) {
            snprintf(out, out_sz, "%s/%s%s", g->game_dir, dirs[d], tries[t]);
            if (FileExists(out)) return 1;
        }
    }
    return 0;
}

static void play_film(Game *g, const char *name) {
    // dev: PETKA_FILM=<seconds>:<file.png> plays films in unattended runs
    // too, saves the frame reached after that long and stops there.
    double shot_at = 0;
    char shot_file[512] = "";
    const char *dev = getenv("PETKA_FILM");
    if (dev) sscanf(dev, "%lf:%511s", &shot_at, shot_file);
    char path[1280];
    if (!name || !find_film(g, name, path, sizeof(path))) {
        set_status(g, TextFormat("(видео %s не найдено)", name ? name : "?"));
        return;
    }
    if ((g->fast || getenv("PETKA_SHOT")) && !dev) {
        set_status(g, TextFormat("(видео %s пропущено)", name));
        return;
    }
    const char *why = "";
    Film *film = film_open(path, GAME_W, GAME_H, FILM_FPS, &why);
    size_t frame_bytes = (size_t)GAME_W * GAME_H * 4;
    uint8_t *pixels = film ? (uint8_t *)malloc(frame_bytes) : NULL;
    if (!pixels || !film_next(film, pixels)) {
        film_close(film);
        free(pixels);
        set_status(g, TextFormat("(видео %s: %s, пропущено)", name, film ? "нет кадров" : why));
        return;
    }

    Music *tracks[4] = {&g->music, &g->ambience, &g->seq_music, &g->seq_ambience};
    for (int i = 0; i < 4; i++)
        if (tracks[i]->frameCount > 0) PauseMusicStream(*tracks[i]);
    Music sound = {0};
    size_t sound_len = 0;
    const uint8_t *sound_data = film_sound(film, &sound_len);
    if (sound_data) {
        sound = LoadMusicStreamFromMemory(".wav", sound_data, (int)sound_len);
        sound.looping = false;
        if (sound.frameCount > 0) {
            SetMusicVolume(sound, dial_gain(g, DIAL_SPEECH));
            PlayMusicStream(sound);
        }
    }
    Image first = {.data = pixels, .width = GAME_W, .height = GAME_H, .mipmaps = 1,
                   .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D screen = LoadTextureFromImage(first);
    HideCursor();

    double start = GetTime();
    long shown = 0;
    int ended = 0;
    while (!ended) {
        if (WindowShouldClose()) { g->quit = 1; break; }
        plat_poll();
        // Any key or either mouse button ends it, as in the original.
        if (plat_any_key()) break;
        if (sound.frameCount > 0) UpdateMusicStream(sound);
        long due = (long)((GetTime() - start) * FILM_FPS);
        while (shown < due && !ended) {
            if (!film_next(film, pixels)) ended = 1;
            shown++;
        }
        if (!ended) UpdateTexture(screen, pixels);
        plat_begin();
        ClearBackground(BLACK);
        DrawTexturePro(screen, (Rectangle){0, 0, GAME_W, GAME_H},
                       (Rectangle){0, 0, (float)plat_width(), (float)plat_height()}, (Vector2){0, 0}, 0, WHITE);
        plat_end();
        if (dev && GetTime() - start >= shot_at) {
            if (shot_file[0]) ExportImage(first, shot_file);
            fprintf(stderr, "film: %s, %ld frames in %.1f s\n", name, shown, GetTime() - start);
            break;
        }
    }

    UnloadTexture(screen);
    if (sound.frameCount > 0) { StopMusicStream(sound); UnloadMusicStream(sound); }
    film_close(film);
    free(pixels);
    // Back to what each was doing: a sequence may have had some paused.
    for (int i = 0; i < 4; i++) {
        int held = i < 2 ? g->tracks_hushed : g->seq_tracks_paused;
        if (tracks[i]->frameCount > 0 && !held) ResumeMusicStream(*tracks[i]);
    }
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
        // A sequence takes the screen from whatever has it. The map or the
        // panel may be up when a clip that was still playing sets one off;
        // left open, they would keep the sequence from ever being shown.
        map_leave(g);
        sequence_sounds(g, 1);
        g->seq_room = obj->id;
        break;
    case PETKA_OP_ENDSEQ:
        if (g->seq_room != PETKA_NONE) sequence_sounds(g, 0);
        g->seq_room = PETKA_NONE;
        g->camera_snap = 1;
        break;
    case PETKA_OP_CONTINUE:
        if (g->paused) {
            g->paused = 0;
            g->talking = 1;
            next_phrase(g);
            // A pause can be the dialogue's last instruction, and then it is
            // over here, in the middle of the script's turn. The script must
            // hear of it now: the messages still waiting may start another
            // dialogue (in part 2 the lieutenant's USERMSG reaction goes
            // straight on to TALK), and an end told a frame later would be
            // taken for that one's.
            if (g->dialogue_done) {
                g->dialogue_done = 0;
                uint16_t talker = g->talker;
                g->talker = PETKA_NONE;
                petka_scriptvm_dialogue_ended(vm, talker);
            }
        }
        break;
    case PETKA_OP_SYSTEM:
        // 242 ends the game (the last of part 3's credits). The exe quits;
        // the launcher goes back to its start-up menu instead.
        if (m->arg1 == 242) g->to_menu = 1;
        break;
    case PETKA_OP_AVI:
        play_film(g, g->qrc[m->arg1]);
        break;
    case PETKA_OP_PART:
        // The whole game is taken down and built again for the new part
        // (sub_412040); that waits for the top of the next frame.
        g->switch_part = m->arg1;
        g->switch_chapter = 1;
        break;
    case PETKA_OP_CHAPTER:
        chapter_open(g, m->arg1);
        break;
    case PETKA_OP_SHOWDESCRIPTION: {
        // The text is the object's line in [all] of INVNTR.TXT, by its
        // script name; the message's number goes with it to the screen.
        size_t len = 0;
        const uint8_t *ini = part_file(g, g->part, "INVNTR.TXT", &len);
        char text[1024];
        if (ini && petka_ini_get(ini, len, "all", obj->name, text, sizeof(text)) && text[0])
            cp1251_to_utf8(text, g->desc, sizeof(g->desc));
        else
            snprintf(g->desc, sizeof(g->desc), "%s", display_name(g, obj));
        g->desc_frame = m->arg1;
        break;
    }
    case PETKA_OP_CURSOR:
        // CURSOR with no clip puts the cursor back on its first action.
        if (m->arg1 == PETKA_NONE) g->verb = 0;
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

// Collects what is on screen right now, back to front: by z ascending, as
// the exe sorts each frame.
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
        // The exe's z update: an animated, visible object with auto_z gets a
        // new z every frame; otherwise z stays whatever it was last given.
        PetkaObjectState *state = &g->vm->state[obj_index(g, obj)];
        if (state->auto_z && state->animate && state->visible) {
            if (hero_here) {
                // A hero (sub_409530): the bottom edge of his scaled frame
                // when standing, which is where the launcher keeps him; the
                // walk's own point, cut to whole pixels (sub_424210), when
                // walking.
                const Walk *walk = &g->walk[obj->id];
                state->z = walk->active ? (int32_t)walk->walker.y : st->y;
            } else {
                // A scene object (sub_401810): the lowest edge of its
                // current frame's MSK rects (its sprite sits at y 0), never
                // less than 1.
                int bottom = petka_msk_frame_bottom(v->msk, v->msk_len, v->flic->frame_count, v->flic->frame_index);
                bottom += state->y;
                state->z = bottom > 1 ? bottom : 1;
            }
        }
        out[n].z = state->z;
        n++;
    }
    qsort(out, (size_t)n, sizeof(Drawable), by_z);
    return n;
}

static Rectangle visual_dest(const Game *g, const PetkaObject *obj) {
    const Visual *v = &g->visuals[obj_index(g, obj)];
    const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    // A hero's clip is a sprite put by his feet; any other object's goes
    // with its corner at the object's position -- 0,0 for everything in a
    // room, whose clips are full frames, but not for the panel's buttons.
    if (is_hero(obj->id)) return hero_dest(g, obj->id, st, v);
    return (Rectangle){(float)st->x, (float)st->y, (float)v->flic->width, (float)v->flic->height};
}

// A scene object's hit test (sub_4018C0): the point must lie in the clip's
// MSK rect, and it or any of its eight neighbours inside that rect must be
// opaque -- a pixel of slack all round, so thin shapes can be clicked.
static int clip_hit(const Visual *v, int x, int y) {
    static const int kDx[9] = {0, -1, 0, 1, -1, 1, -1, 0, 1};
    static const int kDy[9] = {0, -1, -1, -1, 0, 0, 1, 1, 1};
    PetkaMskRect r = v->rect;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > v->flic->width) r.right = v->flic->width;
    if (r.bottom > v->flic->height) r.bottom = v->flic->height;
    if (x < r.left || y < r.top || x >= r.right || y >= r.bottom) return 0;
    for (int k = 0; k < 9; k++) {
        int px = x + kDx[k], py = y + kDy[k];
        if (px < r.left || py < r.top || px >= r.right || py >= r.bottom) continue;
        if (!petka_flic_keyed(v->flic, v->flic->indices[py * v->flic->width + px])) return 1;
    }
    return 0;
}

// Topmost active object under (x, y): the exe walks the z order from the top
// and stops at the first object whose own hit test says yes.
static const PetkaObject *object_at(Game *g, const Drawable *scene, int n, float x, float y) {
    for (int i = n - 1; i >= 0; i--) {
        const PetkaObject *obj = scene[i].obj;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        if (!g->vm->state[obj_index(g, obj)].active) continue;
        if (!is_hero(obj->id)) {
            const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
            if (clip_hit(v, (int)x - st->x, (int)y - st->y)) return obj;
            continue;
        }
        // A hero (sub_4086B0): the pixel itself, on his scaled sprite.
        Rectangle d = visual_dest(g, obj);
        if (d.width <= 0 || x < d.x || y < d.y || x >= d.x + d.width || y >= d.y + d.height) continue;
        int px = (int)((x - d.x) * v->flic->width / d.width);
        int py = (int)((y - d.y) * v->flic->height / d.height);
        if (px < 0 || py < 0 || px >= v->flic->width || py >= v->flic->height) continue;
        if (!petka_flic_keyed(v->flic, v->flic->indices[py * v->flic->width + px])) return obj;
    }
    return NULL;
}

// The object under a point on the map or the panel. Those screens do not
// go by z: their mouse functions walk the screen's own list from its end,
// which is the room's list of objects backwards, and take the first whose
// hit test says yes -- so the panel's dial marks, listed last, come before
// the pictures they lie on.
static const PetkaObject *listed_object_at(Game *g, float x, float y) {
    const PetkaObject *room = petka_script_find(g->script, g->seq_room);
    for (uint32_t i = room ? room->attached_count : 0; i-- > 0;) {
        const PetkaObject *obj = petka_script_find(g->script, room->attached[i].obj_id);
        if (!obj) continue;
        const Visual *v = &g->visuals[obj_index(g, obj)];
        const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
        if (!st->active || !v->live || !v->flic) continue;
        if (clip_hit(v, (int)x - st->x, (int)y - st->y)) return obj;
    }
    return NULL;
}

// Draws text broken into lines no wider than `width`, `leading` apart;
// returns the y below it.
static float draw_wrapped(Font font, const char *text, float x, float y, float width, float size, float leading,
                          Color ink) {
    char row[1024] = "";
    const char *p = text;
    while (*p) {
        const char *space = strchr(p, ' ');
        size_t word = space ? (size_t)(space - p) + 1 : strlen(p);
        char trial[1024];
        snprintf(trial, sizeof(trial), "%s%.*s", row, (int)word, p);
        if (row[0] && MeasureTextEx(font, trial, size, 1).x > width) {
            DrawTextEx(font, row, (Vector2){x, y}, size, 1, ink);
            y += leading;
            row[0] = 0;
            continue;
        }
        snprintf(row, sizeof(row), "%s", trial);
        p += word;
    }
    DrawTextEx(font, row, (Vector2){x, y}, size, 1, ink);
    return y + leading;
}

// The exe's two kinds of text over the picture -- the name of the object
// under the cursor (sub_41F8E0) and a phrase being spoken (sub_420CA0) -- are
// made alike: the 20-pixel bold Arial, broken into lines no wider than 625,
// as a block in the middle of the bottom of the screen, its lower edge on
// row 479, with nothing behind it. A phrase's lines are centred in the
// block; a name's are not.
// Breaks text at spaces into lines no wider than `limit`; returns how many.
static int wrap_text(Font font, const char *text, float size, float limit, char lines[][512], int max) {
    int count = 0;
    char row[512] = "";
    const char *p = text;
    while (*p && count < max) {
        const char *space = strchr(p, ' ');
        size_t word = space ? (size_t)(space - p) + 1 : strlen(p);
        char trial[512];
        snprintf(trial, sizeof(trial), "%s%.*s", row, (int)word, p);
        if (row[0] && MeasureTextEx(font, trial, size, 1).x > limit) {
            snprintf(lines[count++], 512, "%s", row);
            row[0] = 0;
            continue;
        }
        snprintf(row, sizeof(row), "%s", trial);
        p += word;
    }
    if (row[0] && count < max) snprintf(lines[count++], 512, "%s", row);
    return count;
}

// The exe draws these texts antialiased onto a surface filled with a
// "second colour" and then keys that colour out. The pixels on the letters'
// edges, part ink and part fill, stay: a thin rim of the fill colour round
// every letter -- dark red under a speaker's coloured subtitle, near-black
// under a caption or a menu line. Drawn here as the text in the fill colour,
// faint, a pixel off in each direction, under the text itself.
static void draw_rimmed(Font font, const char *text, Vector2 at, float size, Color ink, Color fill, float scale) {
    float d = scale > 1 ? scale * 0.75f : 1;
    Color rim = Fade(fill, 0.55f);
    static const float kOff[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int i = 0; i < 8; i++)
        DrawTextEx(font, text, (Vector2){at.x + kOff[i][0] * d, at.y + kOff[i][1] * d}, size, 1, rim);
    DrawTextEx(font, text, at, size, 1, ink);
}

// Ours, asked for: a dark, see-through band behind text over the picture,
// so that it reads on any background. The original has none.
static void draw_band(Rectangle r, float scale) {
    float pad = 6 * scale;
    DrawRectangleRounded((Rectangle){r.x - pad, r.y - pad / 2, r.width + 2 * pad, r.height + pad}, 0.25f, 6,
                         Fade(BLACK, 0.7f));
}

static void draw_bottom_text(Font font, const char *text, Color ink, Color fill, int centre_lines, int backdrop,
                             float scale) {
    enum { MAX_LINES = 12 };
    static char lines[MAX_LINES][512];
    float size = 20 * scale, widest = 0;
    int count = wrap_text(font, text, size, 625 * scale, lines, MAX_LINES);
    for (int i = 0; i < count; i++) {
        float w = MeasureTextEx(font, lines[i], size, 1).x;
        if (w > widest) widest = w;
    }
    float top = 479 * scale - (count * size + 4 * scale);
    float left = (GAME_W * scale - widest) / 2;
    if (backdrop && count > 0) draw_band((Rectangle){left, top, widest, count * size}, scale);
    for (int i = 0; i < count; i++) {
        float x = centre_lines ? (GAME_W * scale - MeasureTextEx(font, lines[i], size, 1).x) / 2 : left;
        draw_rimmed(font, lines[i], (Vector2){x, top + i * size}, size, ink, fill, scale);
    }
}

// A thing's description, as the exe's description object draws it
// (sub_421370): the description clip (resource 6008) on the frame the
// message names -- a board with the thing's picture -- and the text from
// INVNTR.TXT in black, broken into lines, in the rect 160,275 - 598,376.
// The object takes every point of the screen, and a click on it shuts it.
#define DESCRIPTION_CLIP 6008
static void draw_description(Game *g, Font font, float scale) {
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

// ---- the star ----
//
// The exe's object at QSystem+148 (sub_414C70; clip 5000, SUPRSTAR.FLC, z
// 999): a star of five actions round a briefcase. The right button on the
// main screen (sub_414AF0) puts it up with its middle at the pointer, kept
// on screen, or takes it down. Up, it takes every click: on one of its six
// rects (kept in the object, read from its constructor) it opens the case or
// sets the cursor's action and clip, and in any case it goes away. Its clip
// has a frame for each rect lit and one for none.
#define STAR_CLIP 5000
static const Rectangle kStarPart[6] = {
    {70, 74, 42, 38},   // the middle: the case
    {68, 0, 46, 41},    // look
    {151, 51, 29, 46},  // walk
    {138, 125, 41, 41}, // use
    {55, 145, 41, 30},  // take
    {11, 79, 29, 39},   // talk
};

static void star_toggle(Game *g, int px, int py) {
    if (g->star_open) { g->star_open = 0; return; }
    if (g->star.resource != STAR_CLIP) visual_load(g, &g->star, STAR_CLIP);
    if (!g->star.flic) return;
    int w = g->star.flic->width, h = g->star.flic->height;
    g->star_x = px - w / 2;
    g->star_y = py - h / 2;
    if (g->star_x < 0) g->star_x = 0;
    if (g->star_x > GAME_W - 1 - w) g->star_x = GAME_W - 1 - w;
    if (g->star_y < 0) g->star_y = 0;
    if (g->star_y > GAME_H - 1 - h) g->star_y = GAME_H - 1 - h;
    g->star_open = 1;
}

// Which of the star's parts holds the point; 6 = none.
static int star_part(const Game *g, int px, int py) {
    Vector2 p = {(float)(px - g->star_x), (float)(py - g->star_y)};
    for (int i = 0; i < 6; i++)
        if (CheckCollisionPointRec(p, kStarPart[i])) return i;
    return 6;
}

// The part of the star the left stick points at: of the five round the
// middle, the one whose direction from the middle is nearest the stick's;
// the middle, the case, while the stick is at rest.
static int star_aim(void) {
    Vector2 aim = plat_aim();
    float len = sqrtf(aim.x * aim.x + aim.y * aim.y);
    if (len < 0.5f) return 0;
    const Rectangle *m = &kStarPart[0];
    float cx = m->x + m->width / 2, cy = m->y + m->height / 2;
    int best = 6;
    float best_dot = -2;
    for (int i = 1; i < 6; i++) {
        float dx = kStarPart[i].x + kStarPart[i].width / 2 - cx, dy = kStarPart[i].y + kStarPart[i].height / 2 - cy;
        float dot = (dx * aim.x + dy * aim.y) / (sqrtf(dx * dx + dy * dy) * len);
        if (dot > best_dot) { best_dot = dot; best = i; }
    }
    return best;
}

static void drop_item(Game *g);

static void star_click(Game *g, int part) {
    if (part == 0) {
        g->case_open = 1;
    } else if (part < 6) {
        drop_item(g);
        g->verb = part - 1; // the cursor gets the clip 5001 + part
    }
    g->star_open = 0;
}

static void draw_star(Game *g, float scale) {
    Visual *v = &g->star;
    if (!v->flic) return;
    // Frame 1 is the star with nothing lit; part n is lit on frame n + 2.
    int frame = (g->star_lit + 1) % 7 + 1;
    if (frame <= v->flic->frame_count && v->flic->frame_index != frame - 1) {
        for (int guard = 0; guard < v->flic->frame_count && v->flic->frame_index != frame - 1; guard++)
            petka_flic_advance(v->flic);
        visual_upload(v);
    }
    visual_draw(v, (Rectangle){g->star_x * scale, g->star_y * scale, v->flic->width * scale, v->flic->height * scale});
}

// ---- the case ----
//
// The game's panel is a briefcase that covers most of the screen (the exe's
// object at QSystem+144; key I opens and shuts it). Its clips are all full
// frames, so they line up at 0,0; a button's place is its clip's MSK rect and
// its clip is its lit state. The things carried are shown six to a page, at
// fixed places, in front of the case and behind its strap.
#define CASE_BODY 6000
#define CASE_BUTTON0 6001 // + button: Chapayev, diskette, map, quit, page right, page left
#define CASE_STRAP 6007
#define CASE_EXIT 6009    // opaque where the pointer is outside the case
#define CASE_PAGE 6
static const int kCaseCell[CASE_PAGE][2] = {{120, 145}, {240, 145}, {360, 145}, {100, 220}, {240, 220}, {380, 220}};

// The thing in cell `k` of the page, with its clip loaded; NULL if the cell
// is empty or the thing is not to be seen (the cursor is carrying it).
static const PetkaObject *case_item(Game *g, int k, Visual **out);

// With a controller: the case's pointer on a thing of the page, moved from
// one to the next nearest in the direction pushed (dx, dy), or turned to
// another page. Returns where the pointer goes, in game pixels.
static Vector2 case_steer(Game *g, int dx, int dy, int page) {
    if (page > 0 && (uint32_t)(g->case_first + CASE_PAGE) < g->vm->inventory_count) {
        g->case_first += CASE_PAGE;
        g->case_focus = 0;
    } else if (page < 0 && g->case_first > 0) {
        g->case_first = g->case_first > CASE_PAGE ? g->case_first - CASE_PAGE : 0;
        g->case_focus = 0;
    }
    Visual *v[CASE_PAGE];
    int present[CASE_PAGE], any = 0;
    for (int k = 0; k < CASE_PAGE; k++) {
        v[k] = NULL;
        present[k] = case_item(g, k, &v[k]) != NULL;
        any |= present[k];
    }
    if (!any) return (Vector2){GAME_W / 2.0f, GAME_H / 2.0f};
    if (g->case_focus < 0 || g->case_focus >= CASE_PAGE || !present[g->case_focus])
        for (g->case_focus = 0; !present[g->case_focus]; g->case_focus++) {}
    if (dx || dy) {
        // Of the things that way, the nearest, going straight counted
        // over going aside.
        int from = g->case_focus, best = -1;
        float best_score = 0;
        for (int k = 0; k < CASE_PAGE; k++) {
            if (!present[k] || k == from) continue;
            float ox = (float)(kCaseCell[k][0] - kCaseCell[from][0]), oy = (float)(kCaseCell[k][1] - kCaseCell[from][1]);
            float along = ox * dx + oy * dy, aside = fabsf(ox * dy) + fabsf(oy * dx);
            if (along <= 0) continue;
            float score = along + 2 * aside;
            if (best < 0 || score < best_score) { best = k; best_score = score; }
        }
        if (best >= 0) g->case_focus = best;
    }
    const Visual *f = v[g->case_focus];
    return (Vector2){kCaseCell[g->case_focus][0] + (f->rect.left + f->rect.right) / 2.0f,
                     kCaseCell[g->case_focus][1] + (f->rect.top + f->rect.bottom) / 2.0f};
}

static const PetkaObject *case_item(Game *g, int k, Visual **out) {
    uint32_t i = (uint32_t)(g->case_first + k);
    if (i >= g->vm->inventory_count) return NULL;
    const PetkaObject *obj = petka_script_find(g->script, g->vm->inventory[i]);
    if (!obj) return NULL;
    const PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    if (!st->visible || st->resource == PETKA_NONE) return NULL;
    Visual *v = &g->visuals[obj_index(g, obj)];
    if (v->resource != st->resource) visual_load(g, v, st->resource);
    if (!v->flic) return NULL;
    *out = v;
    return obj;
}

// The map can be opened from a room that is on it (the room's flag +144,
// taken off by NOMAP).
static int map_allowed(const Game *g) {
    const PetkaObjectState *room = petka_scriptvm_state(g->vm, g->vm->room);
    return room && room->on_map;
}

#define CURSOR_INTERFACE 4901
static void drop_item(Game *g);
static void take_thumb(Game *g);

// Back from the map to the room (the map screen's leave, sub_41D730): the
// cursor's action and the view's scroll are as they were.
static void map_leave(Game *g) {
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
static void map_command(Game *g) {
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
static void panel_command(Game *g) {
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

// ---- saved games ----
//
// Files and screen as the original has them: save<n>.dat in the working
// directory, in the exe's own format (petka/save.h); eighteen slots on three
// pages of six, each shown by a small picture of the room with the time of
// saving under it. The slots and the two page corners are rectangles the exe
// builds at start-up; the screen's pictures are LOAD1-3 and SAVE1-3.
#define SAVE_LOAD_PICTURE 4990
#define SAVE_SAVE_PICTURE 4995
static const Rectangle kSaveSlot[SAVE_SLOTS] = {
    {43, 84, 108, 82}, {43, 209, 108, 82}, {43, 335, 108, 82},
    {358, 75, 108, 82}, {360, 200, 108, 82}, {359, 325, 108, 82},
};
static const Rectangle kSavePrev = {10, 414, 28, 28}, kSaveNext = {596, 403, 28, 28};

static void save_path(int page, int slot, char *out, size_t out_sz) {
    snprintf(out, out_sz, "save%d.dat", slot + SAVE_SLOTS * page + 1);
}

static uint8_t *read_whole_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0 ? (uint8_t *)malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); data = NULL; }
    fclose(f);
    if (data) *len = (size_t)size;
    return data;
}

// The picture a save is shown by, taken as the panel comes up (the exe's
// sub_41E230): the whole frame stretched, with no smoothing, into the
// 107x81 corner of a fresh 108x82 surface of 5:6:5 pixels -- the last
// column and row are left as they were made.
static void take_thumb(Game *g) {
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

static Texture2D thumb_texture(const uint8_t *thumb) {
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

static void chapter_open(Game *g, int chapter);

// Writes the game as it stands to a file (the exe's sub_406EB0).
static int game_save(Game *g, const char *path) {
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
static int game_load(Game *g, const char *path) {
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

static void saves_unload(Game *g) {
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
static void saves_show(Game *g) {
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

static void saves_open(Game *g, int mode) {
    g->save_mode = mode;
    saves_show(g);
}

static void saves_close(Game *g) {
    saves_unload(g);
    g->save_mode = 0;
}

// A click on the screen (sub_41E370): a slot loads or saves, the corners
// turn the page.
static void saves_click(Game *g, Vector2 p) {
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (!CheckCollisionPointRec(p, kSaveSlot[i])) continue;
        char path[64];
        save_path(g->save_page, i, path, sizeof(path));
        int loading = g->save_mode == 1;
        if (loading && !g->save_slot[i].present) return;
        saves_close(g);
        if (loading) { if (game_load(g, path)) set_status(g, "(игра загружена)"); }
        else set_status(g, game_save(g, path) ? "(игра сохранена)" : "(не удалось записать сохранение)");
        return;
    }
    if (CheckCollisionPointRec(p, kSavePrev) && g->save_page > 0) { g->save_page--; saves_show(g); }
    if (CheckCollisionPointRec(p, kSaveNext) && g->save_page < 2) { g->save_page++; saves_show(g); }
}

static void draw_saves(Game *g, Font font, float scale) {
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
        // The label, in the exe's dark green and its 20-pixel font, in a
        // box 240 wide a pixel below the picture.
        draw_wrapped(font, g->save_slot[i].label, r.x * scale, (r.y + r.height + 1) * scale, 240 * scale, 20 * scale,
                     20 * scale, (Color){0, 127, 0, 255});
    }
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
static const char *const kDialKey[4] = {"Speech volume", "Music volume", "Sfx volume", "Speed"};

static const char *options_file(void) {
    if (getenv("PETKA_OPTIONS")) return getenv("PETKA_OPTIONS");
    return getenv("PETKA_SHOT") || getenv("PETKA_PLAY") ? NULL : "Petka.ini";
}

static void options_load(Game *g) {
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

static void options_save(const Game *g) {
    const char *file = options_file();
    FILE *f = file ? fopen(file, "w") : NULL;
    if (!f) return;
    fprintf(f, "[Options]\n");
    for (int i = 0; i < 4; i++) fprintf(f, "%s=%d\n", kDialKey[i], g->dial[i]);
    fprintf(f, "Subtitles=%d\n", g->subtitles ? 1 : 0);
    fclose(f);
}

static void new_game(Game *g) {
    size_t len = 0;
    const uint8_t *ini = res_find(g, "PARTS.INI", &len);
    char value[32];
    g->switch_part = ini && petka_ini_get(ini, len, "All", "Part", value, sizeof(value)) ? atoi(value) : 1;
    g->switch_chapter = ini && petka_ini_get(ini, len, "All", "Chapter", value, sizeof(value)) ? atoi(value) : 1;
    if (g->switch_part <= 0) g->switch_part = 1;
}

static void panel_click(Game *g, const PetkaObject *hit) {
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

// Puts an object's clip on a given frame (counted from 1), for the clips
// whose frames are states rather than an animation.
static void show_frame(Game *g, const PetkaObject *obj, int frame) {
    Visual *v = &g->visuals[obj - g->script->objects];
    if (!v->flic || frame < 1 || frame > v->flic->frame_count || v->flic->frame_index == frame - 1) return;
    for (int guard = 0; guard < v->flic->frame_count && v->flic->frame_index != frame - 1; guard++)
        petka_flic_advance(v->flic);
    visual_upload(v);
}

// The panel shows the subtitle switch on two of its objects (the end of
// sub_41C170): the button, number 7, on frame 7 for "on" and 1 for "off",
// and the word, number 11, on frame 2 or 1. And each dial's hand is a clip
// put on the frame its value names (sub_41C740): speech on object 14, music
// on 15, effects on 8, speed on 9.
static void panel_show_state(Game *g) {
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

// What the pointer (screen pixels of the game) is over while the case is
// open. The things come first -- they are above the case in z; the case
// itself takes every other point of the screen (its hit test is just "am I
// shown") and lights a button (its hover, 0x415550): "quit" anywhere
// outside the case, else the button whose rect holds the pointer.
static const PetkaObject *case_hover(Game *g, int px, int py) {
    for (int k = CASE_PAGE - 1; k >= 0; k--) {
        Visual *v = NULL;
        const PetkaObject *obj = case_item(g, k, &v);
        if (obj && g->vm->state[obj_index(g, obj)].active && clip_hit(v, px - kCaseCell[k][0], py - kCaseCell[k][1]))
            return obj;
    }
    g->case_lit = -1;
    clip_show(g, &g->case_exit, CASE_EXIT, 0);
    const PetkaFlic *mask = g->case_exit.flic;
    if (mask && px >= 0 && py >= 0 && px < mask->width && py < mask->height &&
        !petka_flic_keyed(mask, mask->indices[py * mask->width + px])) {
        g->case_lit = 3;
        return NULL;
    }
    for (int i = 0; i < 6; i++) {
        Visual *b = &g->case_button[i];
        if (b->resource == PETKA_NONE) visual_load(g, b, (uint16_t)(CASE_BUTTON0 + i));
        if (!b->flic || px < b->rect.left || py < b->rect.top || px >= b->rect.right || py >= b->rect.bottom) continue;
        // Chapayev's button is dead while he is not about, and the map's in
        // a room that is not on the map.
        const PetkaObjectState *chapayev = petka_scriptvm_state(g->vm, PETKA_ID_CHAPAYEV);
        if (i == 0 && !(chapayev && chapayev->visible)) break;
        if (i == 2 && !map_allowed(g)) break;
        g->case_lit = i;
        break;
    }
    return NULL;
}

// Puts down whatever the cursor carries: the thing is back in the case.
static void drop_item(Game *g) {
    PetkaObjectState *st = petka_scriptvm_state(g->vm, g->vm->cursor_object);
    if (st) st->visible = st->active = 1;
    g->vm->cursor_object = g->vm->cursor_resource = PETKA_NONE;
}

// A click on the case with nothing of its contents under it (sub_4151B0).
static void case_click(Game *g) {
    // Picking a thing for an object, the lid's three buttons do nothing.
    if (g->case_for != PETKA_NONE && g->case_lit >= 0 && g->case_lit <= 2) return;
    switch (g->case_lit) {
    case 0: // the "send Chapayev" action, as keys 6 and C
        drop_item(g);
        g->verb = 5;
        break;
    case 1: panel_command(g); break;
    case 2: map_command(g); break;
    case 3: g->case_open = 0; break;
    case 4:
        if ((uint32_t)(g->case_first + CASE_PAGE) < g->vm->inventory_count) g->case_first += CASE_PAGE;
        break;
    case 5:
        g->case_first = g->case_first > CASE_PAGE ? g->case_first - CASE_PAGE : 0;
        break;
    default: break;
    }
}

// A button of the lid while it does nothing (the case picks a thing for an
// object): its clip in grey, made afresh from the frame shown.
static void draw_button_grey(Game *g, int i, Rectangle screen) {
    Visual *b = &g->case_button[i];
    Texture2D *grey = &g->case_grey[i];
    int w = b->area.right - b->area.left, h = b->area.bottom - b->area.top;
    const uint8_t *rgba = flic_to_rgba(b->flic, b->area);
    if (!rgba || w <= 0 || h <= 0) return;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (!px) return;
    for (size_t k = 0; k < (size_t)w * h; k++) {
        const uint8_t *c = rgba + k * 4;
        uint8_t y = (uint8_t)((c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8);
        px[k * 4 + 0] = px[k * 4 + 1] = px[k * 4 + 2] = y;
        px[k * 4 + 3] = c[3];
    }
    if (grey->id && (grey->width != w || grey->height != h)) { UnloadTexture(*grey); *grey = (Texture2D){0}; }
    if (!grey->id) {
        Image img = {.data = px, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        *grey = LoadTextureFromImage(img);
    } else {
        UpdateTexture(*grey, px);
    }
    free(px);
    Visual shown = *b;
    shown.tex = *grey;
    visual_draw(&shown, screen);
}

static void draw_case(Game *g, float scale) {
    Rectangle screen = {0, 0, GAME_W * scale, GAME_H * scale};
    clip_show(g, &g->case_body, CASE_BODY, 0);
    if (g->case_body.flic)
        visual_draw(&g->case_body, screen);
    // The lit button's clip goes over the case, inside its own rect; the
    // others are put back on their first frame. Picking a thing for an
    // object, the lid's three are always over it, in grey.
    for (int i = 0; i < 6; i++) {
        Visual *b = &g->case_button[i];
        clip_show(g, b, (uint16_t)(CASE_BUTTON0 + i), i == g->case_lit);
        if (!b->flic) continue;
        if (i <= 2 && g->case_for != PETKA_NONE) draw_button_grey(g, i, screen);
        else if (i == g->case_lit) visual_draw(b, screen);
    }
    for (int k = 0; k < CASE_PAGE; k++) {
        Visual *v = NULL;
        if (!case_item(g, k, &v)) continue;
        visual_draw(v, (Rectangle){kCaseCell[k][0] * scale, kCaseCell[k][1] * scale, v->flic->width * scale,
                                   v->flic->height * scale});
    }
    // "Полоска в чемодане", the strap, lies across the things.
    clip_show(g, &g->case_strip, CASE_STRAP, 0);
    if (g->case_strip.flic)
        visual_draw(&g->case_strip, screen);
}

// The dialogue's menu, as the exe's menu object lays it out (sub_420020):
// the lines one under another in the 20-pixel font, each broken at 625
// pixels, starting 5 from the left edge, the whole block resting on row 479
// like any other text over the picture -- white, the line under the pointer
// in light blue, and nothing behind them. Fills `rects` (window pixels) with each
// line's place and, if asked, draws them.
#define MENU_MAX 16
static void menu_lines(const Game *g, Font font, float scale, Rectangle *rects, int hover, int draw) {
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
// The start-up screen: the game's main menu. In the exe it is a screen of
// its own (QSystem+556) showing the room STARTUP of the menus' script, PART_0
// -- a picture, a tune and five buttons whose clips are hidden until the
// pointer is on them, as on the map. A click goes by the button (0x41EAB0).
// Returns 0 to leave the program, 1 for a new game, 2 for "load".
static int run_menu(Game *g, Font font) {
    (void)font;
    enum { MENU_NONE, MENU_NEW, MENU_LOAD, MENU_CREDITS, MENU_EXIT };
    // In the order the room lists them; the pointer is tried on the last first.
    static const struct { const char *clip; int action; } kButtons[5] = {
        {"MAIN\\INTRFACE.BG\\SNEWGAME.FLC", MENU_NEW},
        {"MAIN\\INTRFACE.BG\\SLOADGEM.FLC", MENU_LOAD},
        {"MAIN\\INTRFACE.BG\\SUNINSTL.FLC", MENU_NONE},
        {"MAIN\\INTRFACE.BG\\S-TITLE.FLC", MENU_CREDITS},
        {"MAIN\\INTRFACE.BG\\S-EXIT.FLC", MENU_EXIT},
    };
    Texture2D bg = load_bmp_texture(g, "MAIN\\INTRFACE.BG\\INSTHERO.BMP");
    Visual buttons[5];
    memset(buttons, 0, sizeof(buttons));
    for (int i = 0; i < 5; i++) {
        buttons[i].resource = PETKA_NONE;
        visual_load_path(g, &buttons[i], kButtons[i].clip);
    }
    Music tune = {0};
    size_t tune_len = 0;
    const uint8_t *tune_bytes = res_find(g, "MAIN\\INTRFACE.BG\\start.wav", &tune_len);
    if (tune_bytes) {
        tune = LoadMusicStreamFromMemory(".wav", tune_bytes, (int)tune_len);
        tune.looping = true;
        if (tune.frameCount > 0) PlayMusicStream(tune);
    }
    // dev: PETKA_MENU=<file.png>:<x>,<y> shows the menu with the pointer
    // there, saves a picture of it and goes on to a new game.
    const char *dev = getenv("PETKA_MENU");
    char shot[512] = "";
    float pin_x = -1, pin_y = -1;
    if (dev) sscanf(dev, "%511[^:]:%f,%f", shot, &pin_x, &pin_y);

    int result = -1;
    long frames = 0;
    while (!WindowShouldClose() && result < 0) {
        float scale = (float)plat_width() / GAME_W;
        plat_poll();
        Vector2 mouse = plat_pointer();
        if (pin_x >= 0) mouse = (Vector2){pin_x * scale, pin_y * scale};
        int px = (int)(mouse.x / scale), py = (int)(mouse.y / scale), over = -1;
        for (int i = 4; i >= 0 && over < 0; i--)
            if (buttons[i].flic && clip_hit(&buttons[i], px, py)) over = i;
        if (tune.frameCount > 0) {
            SetMusicVolume(tune, dial_gain(g, DIAL_MUSIC));
            UpdateMusicStream(tune);
        }
        if (plat_click() && over >= 0) {
            switch (kButtons[over].action) {
            case MENU_NEW: result = 1; break;
            case MENU_LOAD: result = 2; break;
            case MENU_EXIT: result = 0; break;
            case MENU_CREDITS:
                // The film, and then the tune from its beginning again.
                if (tune.frameCount > 0) StopMusicStream(tune);
                play_film(g, "credits.avi");
                if (g->quit) result = 0;
                if (tune.frameCount > 0) PlayMusicStream(tune);
                break;
            default: break;
            }
        }
        plat_begin();
        ClearBackground(BLACK);
        Rectangle whole = {0, 0, GAME_W * scale, GAME_H * scale};
        if (bg.id) DrawTexturePro(bg, (Rectangle){0, 0, (float)bg.width, (float)bg.height}, whole, (Vector2){0, 0}, 0, WHITE);
        if (over >= 0)
            visual_draw(&buttons[over], whole);
        HideCursor();
        draw_cursor(g, CURSOR_INTERFACE, over >= 0, 0, mouse, scale);
        plat_end();
        if (dev && ++frames == 30) {
            Image img = LoadImageFromScreen();
            if (shot[0]) ExportImage(img, shot);
            UnloadImage(img);
            fprintf(stderr, "menu: pointer over button %d\n", over);
            result = getenv("PETKA_MENU_LOAD") ? 2 : 1; // dev: ... or on to "load"
        }
    }
    for (int i = 0; i < 5; i++) visual_free(&buttons[i]);
    if (tune.frameCount > 0) { StopMusicStream(tune); UnloadMusicStream(tune); }
    if (bg.id) UnloadTexture(bg);
    return result < 0 ? 0 : result;
}

static void game_init(Game *g) {
    g->vm->rng = (uint32_t)(GetTime() * 1e6) | 1u;
    petka_scriptvm_init(g->vm);
}

static void game_enter(Game *g) {
    const PetkaObject *start = g->topo && g->topo->start_room
        ? petka_script_find_name(g->script, g->topo->start_room) : NULL;
    if (!start) {
        for (uint32_t i = 0; i < g->script->object_count && !start; i++)
            if (g->script->objects[i].is_room) start = &g->script->objects[i];
    }
    if (start) petka_scriptvm_enter_room(g->vm, start->id, 0);
}

static void game_start(Game *g) {
    game_init(g);
    game_enter(g);
}

// Everything of the loaded part is let go: what is on screen or sounding,
// the script and its machine, the dialogue, and the stores themselves.
static void part_close(Game *g) {
    if (g->voice.frameCount > 0) { StopSound(g->voice); UnloadSound(g->voice); g->voice = (Sound){0}; }
    set_stream(g, &g->music, &g->music_id, PETKA_NONE);
    set_stream(g, &g->ambience, &g->ambience_id, PETKA_NONE);
    set_stream(g, &g->seq_music, &g->seq_music_id, PETKA_NONE);
    set_stream(g, &g->seq_ambience, &g->seq_ambience_id, PETKA_NONE);
    g->tracks_hushed = g->seq_tracks_paused = 0;
    for (uint32_t i = 0; g->script && g->visuals && i < g->script->object_count; i++) visual_free(&g->visuals[i]);
    free(g->visuals);
    g->visuals = NULL;
    visual_free(&g->cursor);
    visual_free(&g->star);
    g->star_open = 0;
    visual_free(&g->desc_clip);
    g->desc[0] = 0;
    saves_unload(g);
    visual_free(&g->case_body);
    for (int i = 0; i < 3; i++) {
        if (g->case_grey[i].id) UnloadTexture(g->case_grey[i]);
        g->case_grey[i] = (Texture2D){0};
    }
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
static int part_open(Game *g, int number, int chapter) {
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

    g->vm = petka_scriptvm_open(g->script, &g->host);
    load_names(g, g->part);
    g->visuals = (Visual *)calloc(g->script->object_count, sizeof(Visual));
    for (uint32_t i = 0; i < g->script->object_count; i++) g->visuals[i].resource = PETKA_NONE;
    return 1;
}

// CHAPTER n (the exe's sub_411C00): the chapter's store takes the place of
// the one before it, and every object's shown name and colour are read
// again from it. The game itself goes on untouched.
static void chapter_open(Game *g, int chapter) {
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
static void game_switch(Game *g, int part, int chapter) {
    int old_part = g->part_number, old_chapter = g->chapter;
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

    part_close(g);
    if (!part_open(g, part, chapter)) {
        part_close(g);
        part_open(g, old_part, old_chapter);
        set_status(g, TextFormat("(часть %d не найдена, игра начата заново)", part));
    }
    game_start(g);
}

// ---- dev: a scripted play ----
//
// PETKA_PLAY=<file> plays the game from a list of steps, one a line, each
// taken when the game has come to rest after the one before: what a player
// would do with the mouse, said by object id.
//
//   look|walk|use|take|talk|vich <id>   the cursor's action on an object
//                                       (vich: "send Chapayev")
//   item <item id> <id>                 a thing from the case on an object
//   go <x> <y>                          "walk" on the bare floor at a point
//   near <x> <y>                        check that Petka stands about there
//   map <id>                            open the map and pick a place
//   pick <n> [<n>...]                   the lines to answer the coming
//                                       menus with, from 0
//   room <id> / part <n>                check which room, which part this is
//   has <id> / hasnot <id>              check the case
//   save <n>                            write save<n>.dat
//   shot <file.png>                     save a screenshot
//   wait <frames>
//   # comment
//
// A step that cannot be taken, a menu with no line named for it, or a game
// that does not come to rest ends the run with a report and a screenshot
// (play-fail.png) and exit code 3; at the end of the list it leaves with 0.
#define PLAY_REST 30       // frames at rest before the next step
#define PLAY_PATIENCE 60000 // frames a step may take before the run gives up

typedef struct {
    char verb[12];
    int a, b;
    int picks[16], pick_count;
    char text[160];
    int line;
} PlayStep;

static struct {
    PlayStep *steps;
    int count, at;
    int picks[64], pick_count, pick_at;
    long rest, waited, hold;
    int phase;       // a step in two parts: the map is up, the place not picked yet
    char shot[256];  // a screenshot to save once this frame is on the screen
    int shot_wait;
    int over, failed;
} s_play;

static void play_load(const char *file) {
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

static void play_fail(Game *g, const char *why) {
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

static int play_has(const Game *g, int id) {
    for (uint32_t i = 0; i < g->vm->inventory_count; i++)
        if (g->vm->inventory[i] == id) return 1;
    return 0;
}

// Whether the player could point at this object now: it is one of the
// room's (or a hero, or a thing in the case) and takes the mouse.
static const PetkaObject *play_target(Game *g, int id, char *why, size_t why_sz) {
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
static void play_tick(Game *g, int can_act) {
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

// A click with the cursor's action `verb` on `obj`, at mx, my in the room.
static void act_on(Game *g, const PetkaObject *obj, int verb, float mx, float my) {
    PetkaScriptVM *vm = g->vm;
    if (kVerbs[verb].op == PETKA_OP_WALK && !is_hero(obj->id) &&
        !petka_scriptvm_state(vm, PETKA_ID_PETKA)->active) {
        // "Walk" to something while Petka is off -- a clip of him doing
        // something stands in his place -- is not taken. A way out would
        // otherwise be walked by nobody and the room change with Petka
        // still off: Chapayev came into the next room alone. (It can be
        // clicked: in the church the cursor is back when the crypt's door
        // has opened, with Petka still at the brick.)
        return;
    }
    uint16_t op = kVerbs[verb].op;
    int point = op == PETKA_OP_WALK || op == PETKA_OP_OBJECTUSE;
    stop_heroes(g);
    petka_scriptvm_post(vm, obj->id, op, point ? (uint16_t)mx : 0, point ? (uint16_t)my : 0, 0,
                        op == PETKA_OP_OBJECTUSE ? (int)PETKA_ID_CHAPAYEV : (int)obj->id);
}

// The star put up with a controller: its lit part done to the object it is
// up for.
static void star_pick(Game *g) {
    int part = g->star_lit;
    g->star_open = 0;
    if (part == 0) {
        // The case, to pick a thing for the object -- unless the star is up
        // over the case already, for one of its things.
        if (!g->case_open) {
            g->case_open = 1;
            g->case_for = g->pad_target;
        }
        return;
    }
    if (part < 1 || part > 5) return;
    g->verb = part - 1;
    const PetkaObject *obj = petka_script_find(g->script, g->pad_target);
    if (obj) act_on(g, obj, g->verb, g->pad_x, g->pad_y);
}

int main(int argc, char **argv) {
    // With no arguments the game is where the program is: a copy dropped
    // into the game's folder runs it from there, however it was started, and
    // keeps its saved games and settings in that folder too.
    const char *game_dir = argc > 1 ? argv[1] : plat_default_game_dir();
    if (argc <= 1) ChangeDirectory(game_dir);
    const char *part = argc > 2 ? argv[2] : "PART_1";
    const char *digit = part;
    while (*digit && (*digit < '0' || *digit > '9')) digit++;
    if (!*digit) { fprintf(stderr, "usage: %s [game_dir] [PART_N]\n", argv[0]); return 1; }

    static Game game;
    Game *g = &game;
    g->bg_resource = g->shown_room = g->music_id = g->ambience_id = g->mesh_resource = PETKA_NONE;
    g->seq_music_id = g->seq_ambience_id = PETKA_NONE;
    g->talker = PETKA_NONE;
    g->seq_room = PETKA_NONE;
    g->cursor.resource = PETKA_NONE;
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
    g->host.walk = host_walk;
    g->host.stop = host_stop;
    g->host.room_changed = host_room_changed;
    g->host.message = host_message;
    if (!part_open(g, atoi(digit), 1)) return 1;

    InitWindow(GAME_W * 2, GAME_H * 2, "Петька и Василий Иванович");
    // Esc belongs to the game (it backs out of things); the window's own
    // close button ends the program.
    SetExitKey(KEY_NULL);
    plat_init();
    s_turbo = getenv("PETKA_TURBO") != NULL;
    SetTargetFPS(s_turbo ? 0 : 60);
    InitAudioDevice();
    Font font = load_cyrillic_font();
    // Dev hooks for unattended runs:
    //   PETKA_SHOT=<file.png>  skip the menu, save a screenshot and quit
    //   PETKA_SHOT_FRAME=<n>   ...after this many frames (default 120)
    //   PETKA_POST=<frame>:<obj_id>:<opcode>[:<arg1>:<arg2>][,...]  post script messages
    //   PETKA_FAST=1           no voice-over, phrases flash by
    //   PETKA_PICK=<n>|last    dialogue menus answer themselves with that line
    //   PETKA_TRACE=1          log delivered script messages to stderr
    //   PETKA_TURBO=1          run on a fixed clock as fast as the machine goes
    //   PETKA_SHOT_SERIES=<start>:<step>:<count>  also save <file>-<frame>.png
    //                          for that many frames, <step> apart
    g->fast = getenv("PETKA_FAST") != NULL;
    const char *pick = getenv("PETKA_PICK");
    g->auto_pick = !pick ? -2 : strcmp(pick, "last") == 0 ? -1 : atoi(pick);
    g->trace = getenv("PETKA_TRACE") != NULL;
    const char *shot_path = getenv("PETKA_SHOT");
    const char *post_plan = getenv("PETKA_POST");
    //   PETKA_PLAY=<file>      play the steps of the file (see play_tick)
    if (getenv("PETKA_PLAY")) {
        play_load(getenv("PETKA_PLAY"));
        g->auto_pick = -2; // the play answers the menus itself
    }
    int unattended = shot_path != NULL || s_play.steps != NULL;
    long shot_frame = getenv("PETKA_SHOT_FRAME") ? atol(getenv("PETKA_SHOT_FRAME")) : 120;
    long frame = 0;
    // The exe opens with three films, one after another (sub_410CD0): the
    // publisher's and the developer's marks and an advertisement.
    if (!unattended) {
        play_film(g, "buka.avi");
        play_film(g, "skif.avi");
        play_film(g, "adv.avi");
    }
    int menu = unattended && !getenv("PETKA_MENU") ? 1 : run_menu(g, font);
    if (g->quit || !menu) { plat_close(); CloseWindow(); return 0; }

    if (menu == 2) {
        // "Load": the script is set going, and the save screen comes first.
        game_init(g);
        g->boot_load = 1;
        saves_open(g, 1);
    } else {
        game_start(g);
    }

    int debug = getenv("PETKA_DEBUG") != NULL; // dev: start with the F1 overlay on
    Drawable scene[256];
    while (!WindowShouldClose() && !g->quit) {
        float scale = (float)plat_width() / GAME_W;
        plat_poll();
        Vector2 mouse = plat_pointer();
        // dev: PETKA_MOUSE=<x>,<y> pins the pointer (game pixels) for
        // unattended runs, so hovering can be screenshotted.
        float pin_x, pin_y;
        if (getenv("PETKA_MOUSE") && sscanf(getenv("PETKA_MOUSE"), "%f,%f", &pin_x, &pin_y) == 2)
            mouse = (Vector2){pin_x * scale, pin_y * scale};
        // dev: PETKA_CLICK=<frame>:<x>:<y>[:<verb>][,...] left-clicks at a
        // point of the screen (game pixels), optionally picking a verb first.
        int clicked = plat_click();
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
        // Another part was asked for -- by "new game", the script's PART or
        // a saved game. Nothing of this frame holds the old one yet.
        // "Load" at start-up put the save screen up before any room: left
        // without loading, it gives way to the screen it came from -- the
        // start-up one again (the exe's screens go back to the previous).
        if (g->to_menu) {
            // The game is over: everything of it is silenced and the start-up
            // menu comes up, as at the start.
            g->to_menu = 0;
            set_stream(g, &g->music, &g->music_id, PETKA_NONE);
            set_stream(g, &g->ambience, &g->ambience_id, PETKA_NONE);
            set_stream(g, &g->seq_music, &g->seq_music_id, PETKA_NONE);
            set_stream(g, &g->seq_ambience, &g->seq_ambience_id, PETKA_NONE);
            for (int i = 0; i < MAX_SOUNDS; i++) {
                if (g->sounds[i].frameCount > 0) StopSound(g->sounds[i]);
                g->sound_paused[i] = 0;
            }
            if (g->voice.frameCount > 0) StopSound(g->voice);
            int again = run_menu(g, font);
            if (g->quit || !again) break;
            if (again == 2) {
                g->boot_load = g->menu_on_back = 1;
                saves_open(g, 1);
            } else {
                new_game(g);
            }
            continue;
        }
        if (g->boot_load && !g->save_mode && !g->switch_part) {
            g->boot_load = 0;
            if (g->menu_on_back) {
                // The save screen of the menu after the end, left without
                // loading: the menu again.
                g->menu_on_back = 0;
                g->to_menu = 1;
                continue;
            }
            if (g->vm->room == PETKA_NONE) {
                int again = run_menu(g, font);
                if (g->quit || !again) break;
                if (again == 2) {
                    g->boot_load = 1;
                    saves_open(g, 1);
                    continue;
                }
                game_enter(g);
            }
        }
        if (g->switch_part) {
            int to_part = g->switch_part, to_chapter = g->switch_chapter > 0 ? g->switch_chapter : 1;
            g->switch_part = 0;
            game_switch(g, to_part, to_chapter);
            if (g->pending_load[0]) {
                char save_file[64];
                snprintf(save_file, sizeof(save_file), "%s", g->pending_load);
                g->pending_load[0] = 0;
                // Only if the part is the save's now: no going round in circles.
                if (g->part_number == to_part && game_load(g, save_file)) set_status(g, "(игра загружена)");
            }
        }
        PetkaScriptVM *vm = g->vm;

        if (plat_key(KEY_F1)) debug = !debug;
        // The cursor's action keys, from the exe's accelerator table: a digit
        // and a letter each (note 3 is "take" and 4 is "use"). The exe's
        // handlers do nothing unless the panel is switched on, and they put
        // the action's own clip on the cursor, dropping an item it carried.
        static const struct { int digit, letter, action; } kActionKeys[] = {
            {KEY_ONE, KEY_L, 0}, {KEY_TWO, KEY_W, 1}, {KEY_THREE, KEY_G, 3},
            {KEY_FOUR, KEY_U, 2}, {KEY_FIVE, KEY_T, 4}, {KEY_SIX, KEY_C, 5},
        };
        // (A description on screen switches the panel off, too.)
        for (int k = 0; k < 6 && !g->menu_count && vm->interface_enabled && !g->map_open && !g->desc[0]; k++) {
            if (!plat_key(kActionKeys[k].digit) && !plat_key(kActionKeys[k].letter)) continue;
            g->verb = kActionKeys[k].action;
            drop_item(g);
        }

        frame++;
        s_clock += 1.0 / 60.0;
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
            // The map and the panel leave the room's tracks playing. A
            // sequence does not (its set-up, sub_416860): the game's sounds
            // are paused where they are, and the sequence room's own music
            // and ambience play instead -- kept going if they are the ones
            // the last sequence played, started if they are others. When
            // it ends the room's tracks go on from where they stopped.
            int sequence = g->seq_room != PETKA_NONE && !g->map_open;
            if (sequence != g->tracks_hushed) {
                if (g->music.frameCount > 0) { if (sequence) PauseMusicStream(g->music); else ResumeMusicStream(g->music); }
                if (g->ambience.frameCount > 0) {
                    if (sequence) PauseMusicStream(g->ambience);
                    else ResumeMusicStream(g->ambience);
                }
                g->tracks_hushed = sequence;
            }
            if (sequence) {
                set_stream(g, &g->seq_music, &g->seq_music_id, room_st->music);
                set_stream(g, &g->seq_ambience, &g->seq_ambience_id, room_st->bgsfx);
                if (g->seq_tracks_paused) {
                    if (g->seq_music.frameCount > 0) ResumeMusicStream(g->seq_music);
                    if (g->seq_ambience.frameCount > 0) ResumeMusicStream(g->seq_ambience);
                    g->seq_tracks_paused = 0;
                }
            } else {
                if (!g->seq_tracks_paused) {
                    if (g->seq_music.frameCount > 0) PauseMusicStream(g->seq_music);
                    if (g->seq_ambience.frameCount > 0) PauseMusicStream(g->seq_ambience);
                    g->seq_tracks_paused = 1;
                }
                const PetkaObjectState *under = g->map_open ? petka_scriptvm_state(vm, vm->room) : NULL;
                uint16_t music = room_st->music, ambience = room_st->bgsfx;
                if (under && music == PETKA_NONE) music = under->music;
                if (under && ambience == PETKA_NONE) ambience = under->bgsfx;
                set_stream(g, &g->music, &g->music_id, music);
                set_stream(g, &g->ambience, &g->ambience_id, ambience);
            }
        }
        Music *streams[4] = {&g->music, &g->seq_music, &g->ambience, &g->seq_ambience};
        for (int i = 0; i < 4; i++) {
            if (streams[i]->frameCount <= 0) continue;
            SetMusicVolume(*streams[i], dial_gain(g, i < 2 ? DIAL_MUSIC : DIAL_SFX));
            UpdateMusicStream(*streams[i]);
        }

        camera_update(g, game_dt());
        // Everything in the room is drawn through this; the HUD is not.
        Camera2D view = {.offset = {0, 0}, .target = {roundf(g->camera_x) * scale, 0}, .rotation = 0, .zoom = 1};
        // SHAKE: the exe puts the finished frame on screen 3 pixels to the
        // right on alternate 30 ms ticks, with a black strip where it was.
        if (vm->shake && ((long)(game_now() * 1000.0 / 30.0) & 1)) view.target.x -= 3 * scale;

        for (uint32_t i = 0; i < g->script->object_count; i++) g->visuals[i].live = 0;
        for (uint32_t i = 0; room && i < room->attached_count; i++) {
            const PetkaObject *obj = petka_script_find(g->script, room->attached[i].obj_id);
            if (obj) visual_update(g, obj);
        }
        for (uint16_t id = 0; id < 2 && g->seq_room == PETKA_NONE; id++) {
            const PetkaObject *hero = petka_script_find(g->script, id);
            if (hero && !g->visuals[obj_index(g, hero)].live) visual_update(g, hero);
        }
        // The things in the case have clips of their own, and the script
        // waits for some of them: in part 2 the pen on the cheque hides the
        // cursor, sets the cheque writing itself and gives the cursor back
        // on that clip's END. So they are kept going like the room's
        // objects, the case open or shut.
        for (uint32_t i = 0; i < vm->inventory_count; i++) {
            const PetkaObject *thing = petka_script_find(g->script, vm->inventory[i]);
            if (thing && !g->visuals[obj_index(g, thing)].live) visual_update(g, thing);
        }
        // A PLAY aimed at something that is not on screen (hidden, another
        // room, missing clip) would never end; end it at once instead.
        for (uint32_t i = 0; i < g->script->object_count; i++) {
            const PetkaObjectState *st = &vm->state[i];
            if (st->play_tail.present && (!g->visuals[i].live || !st->visible))
                petka_scriptvm_animation_ended(vm, g->script->objects[i].id, st->play_wait);
        }
        if (petka_scriptvm_run(vm) < 0) set_status(g, "(сценарий зациклился)");

        if (g->panel_open) panel_show_state(g);
        int scene_count = build_scene(g, scene, (int)(sizeof(scene) / sizeof(scene[0])));
        // Alt+R (the exe's command 32787, 0x4181F0) gets the player out of
        // a game left with no cursor: the dialogue's pause is lifted, the
        // cursor shown and the star -- the whole interface -- switched on.
        if ((plat_key_down(KEY_LEFT_ALT) || plat_key_down(KEY_RIGHT_ALT)) && plat_key(KEY_R)) {
            g->paused = 0;
            vm->cursor_visible = 1;
            vm->interface_enabled = 1;
        }
        // Esc (the exe's command 32786, 0x418190): the case is shut, and
        // the screen that is up is left -- the map, the panel and the save
        // screen go back to the game; the game's own screen only takes a
        // description down. dev: PETKA_ESC=<frame> presses it.
        int escape = plat_key(KEY_ESCAPE) || (getenv("PETKA_ESC") && atol(getenv("PETKA_ESC")) == frame);
        int desc_shown = g->desc[0] != 0;
        int shut_desc = 0;
        if (escape) {
            g->case_open = 0;
            if (g->save_mode) saves_close(g);
            else if (g->map_open) map_leave(g);
            else if (desc_shown) shut_desc = 1;
        }
        // A description stays up until a click, which it takes. Shutting
        // it (sub_414AC0) switches the panel on, whatever it was before.
        if (desc_shown && (shut_desc || clicked || plat_rclick() || plat_key(KEY_SPACE))) {
            g->desc[0] = 0;
            vm->interface_enabled = 1;
            clicked = 0;
        }
        int can_act = vm->cursor_visible && !g->talking && (g->seq_room == PETKA_NONE || g->map_open) &&
                      !g->save_mode && !desc_shown;
        if (s_play.steps) play_tick(g, can_act);
        // The save screen takes the mouse to itself. Esc leaves it, as in
        // the original; so, here, do the right button and O.
        if (g->save_mode) {
            if (plat_rclick() || plat_key(KEY_O)) saves_close(g);
            else if (clicked) saves_click(g, (Vector2){mouse.x / scale, mouse.y / scale});
        }
        // dev: PETKA_SAVE=<frame> and PETKA_LOAD=<frame> write and read
        // save1.dat on that frame of an unattended run.
        if (getenv("PETKA_SAVE") && atol(getenv("PETKA_SAVE")) == frame) {
            take_thumb(g);
            fprintf(stderr, "save: %s\n", game_save(g, "save1.dat") ? "ok" : "failed");
        }
        if (getenv("PETKA_LOAD") && atol(getenv("PETKA_LOAD")) == frame)
            fprintf(stderr, "load: %s\n", game_load(g, "save1.dat") ? "ok" : "failed");
        // The case is opened and shut with I (the exe's command 32779),
        // while the panel is on. Open, it takes the mouse off the room.
        // dev: PETKA_CASE=<frame> opens it on that frame of an unattended run.
        int case_key = plat_key(KEY_I) || (getenv("PETKA_CASE") && atol(getenv("PETKA_CASE")) == frame);
        if (case_key && can_act && vm->interface_enabled && !g->menu_count && !g->map_open)
            g->case_open = !g->case_open;
        while (g->case_first > 0 && (uint32_t)g->case_first >= vm->inventory_count) g->case_first -= CASE_PAGE;
        if (g->case_first < 0) g->case_first = 0;
        // dev: PETKA_MAP=<frame> presses M on that frame of an unattended run.
        int map_key = plat_key(KEY_M) || plat_key(KEY_TAB) ||
                      (getenv("PETKA_MAP") && atol(getenv("PETKA_MAP")) == frame);
        if (map_key && can_act && !g->menu_count) map_command(g);
        // dev: PETKA_PANEL=<frame> presses O on that frame of an unattended run.
        int panel_key = plat_key(KEY_O) || (getenv("PETKA_PANEL") && atol(getenv("PETKA_PANEL")) == frame);
        if (panel_key && can_act && !g->menu_count) panel_command(g);
        // F2 and F3 (the exe's commands 32771 and 32772, 0x417B80 and
        // 0x417C00) go straight from the game's own screen to the save
        // screen: F2 to save -- only while the star is active; the case is
        // shut and the slot picture taken first -- and F3 to load.
        int on_main = can_act && !g->menu_count && !g->map_open && !g->star_open;
        if (plat_key(KEY_F2) && on_main && vm->interface_enabled) {
            g->case_open = 0;
            take_thumb(g);
            saves_open(g, 2);
        }
        if (plat_key(KEY_F3) && on_main) saves_open(g, 1);
        // The star is up only while the player can act, and while it is up
        // nothing else is under the pointer.
        if (!can_act || g->map_open) g->star_open = 0;
        // With a controller the case's pointer is on one of its things (a
        // touch on one takes it); the star, while up, keeps it where it is.
        if (plat_pad() && g->case_open && can_act && !g->star_open) {
            Vector2 at = {mouse.x / scale, mouse.y / scale};
            for (int k = 0; k < CASE_PAGE; k++) {
                Visual *v = NULL;
                if (case_item(g, k, &v) && clip_hit(v, (int)at.x - kCaseCell[k][0], (int)at.y - kCaseCell[k][1]))
                    g->case_focus = k;
            }
            at = case_steer(g, plat_list_step_x(), plat_list_step(), plat_page_step());
            mouse = (Vector2){at.x * scale, at.y * scale};
            plat_set_pointer(mouse);
            mx = mouse.x / scale + g->camera_x;
            my = mouse.y / scale;
        }
        int screen_x = (int)(mouse.x / scale), screen_y = (int)(mouse.y / scale);
        // R held: the cursor sends Chapayev; otherwise a controller works
        // the star.
        int send_chapayev = plat_pad() && plat_chapayev_held() && vm->cursor_object == PETKA_NONE && !g->case_open;
        int pad_star = plat_pad() && !send_chapayev;
        if (send_chapayev) g->star_open = 0;
        int verb = send_chapayev ? VERB_COUNT - 1 : g->verb;
        g->star_lit = !g->star_open ? 6 : pad_star ? star_aim() : star_part(g, screen_x, screen_y);
        // dev: PETKA_RCLICK=<frame> presses the right button on that frame.
        int right_click = plat_rclick() ||
                          (getenv("PETKA_RCLICK") && atol(getenv("PETKA_RCLICK")) == frame);
        if (!g->case_open) g->case_for = PETKA_NONE;
        int in_case = g->case_open && can_act && !g->star_open;
        if (!in_case) g->case_lit = -1;
        const PetkaObject *hover = !can_act || g->star_open ? NULL
                                 : in_case ? case_hover(g, (int)(mouse.x / scale), (int)(mouse.y / scale))
                                 : g->map_open ? listed_object_at(g, mx, my)
                                 : object_at(g, scene, scene_count, mx, my);

        g->panel_hover = -1;
        if (g->panel_open && hover) {
            const PetkaObject *panel = petka_script_find(g->script, g->seq_room);
            for (uint32_t i = 0; panel && i < panel->attached_count; i++)
                if (panel->attached[i].obj_id == hover->id) g->panel_hover = (int)i;
        }
        int menu_hover = -1;
        Rectangle menu_rects[MENU_MAX];
        if (g->menu_count > 0) menu_lines(g, font, scale, menu_rects, -1, 0);
        for (int i = 0; i < g->menu_count && i < MENU_MAX; i++)
            if (CheckCollisionPointRec(mouse, menu_rects[i])) menu_hover = i;
        if (plat_pad()) {
            int lines = g->menu_count < MENU_MAX ? g->menu_count : MENU_MAX;
            int in_dialogue = lines > 0 || g->talking || g->paused;
            if (in_dialogue && !g->pad_hold) {
                g->pad_hold = 1;
                g->pad_return = mouse;
                g->menu_focus = 0;
                plat_hold_pointer(true);
            }
            if (lines > 0) {
                // A touch on a line takes it; up and down step from it.
                if (menu_hover >= 0) g->menu_focus = menu_hover;
                // Past either end it comes round to the other.
                g->menu_focus = (g->menu_focus + plat_list_step() + lines) % lines;
                // The pointer sits at the right end of the line's first row,
                // where it hides the least of the text.
                Rectangle r = menu_rects[g->menu_focus];
                mouse = (Vector2){r.x + r.width - 1, r.y + 10 * scale};
                plat_set_pointer(mouse);
                menu_hover = g->menu_focus;
            } else if (g->pad_hold && !in_dialogue) {
                g->pad_hold = 0;
                plat_hold_pointer(false);
                mouse = g->pad_return;
                plat_set_pointer(mouse);
            }
        }
        if (g->menu_count > 0) {
            // The dialogue waits for the player's line: click it or press
            // its number.
            int pick = clicked ? menu_hover : -1;
            for (int k = 0; k < g->menu_count && k < 9; k++) if (plat_key(KEY_ONE + k)) pick = k;
            if (pick >= 0 && petka_dialoguevm_choose(&g->dvm, pick) == 0) {
                g->menu_count = 0;
                next_phrase(g);
            }
        } else if (g->talking) {
            int voiced = g->voice.frameCount > 0;
            // With a controller B skips a phrase: A, pressed again to pick
            // a line of the menu, would skip the first one said after it.
            int skip = plat_pad() ? right_click : clicked;
            if (skip || plat_key(KEY_SPACE) ||
                (voiced ? !IsSoundPlaying(g->voice) : game_now() >= g->line_until))
                next_phrase(g);
        } else if (can_act) {
            // The right button (sub_414AF0), while the panel is on: with
            // the case open and a thing on the cursor, the thing is put down
            // and the cursor becomes "take"; otherwise the star comes up at
            // the pointer, or goes away.
            if (right_click && plat_pad()) {
                // B: the star down, the case shut (a thing taken stays on
                // the cursor), or the thing on the cursor put back.
                if (g->star_open) {
                    g->star_open = 0;
                } else if (g->case_open) {
                    g->case_open = 0;
                } else if (vm->cursor_object != PETKA_NONE) {
                    drop_item(g);
                }
            } else if (right_click && !g->map_open && vm->interface_enabled) {
                if (g->case_open && vm->cursor_object != PETKA_NONE) {
                    drop_item(g);
                    g->verb = 3;
                } else {
                    star_toggle(g, screen_x, screen_y);
                }
            }
            if (clicked) {
                if (g->star_open) {
                    if (pad_star) star_pick(g);
                    else star_click(g, g->star_lit);
                } else if (g->panel_open) {
                    if (hover) panel_click(g, hover);
                } else if (hover && in_case && g->case_for != PETKA_NONE) {
                    // A thing picked for the object: used on it, as if
                    // taken and clicked there.
                    uint16_t target = g->case_for;
                    g->case_open = 0;
                    g->case_for = PETKA_NONE;
                    drop_item(g);
                    stop_heroes(g);
                    petka_scriptvm_post(vm, target, PETKA_OP_OBJECTUSE, 0, 0, 0, hover->id);
                } else if (hover && vm->cursor_object != PETKA_NONE) {
                    uint16_t item = vm->cursor_object;
                    stop_heroes(g);
                    drop_item(g);
                    petka_scriptvm_post(vm, hover->id, PETKA_OP_OBJECTUSE, 0, 0, 0, item);
                } else if (hover && pad_star && !g->map_open && vm->interface_enabled) {
                    star_toggle(g, screen_x, screen_y);
                    g->pad_target = hover->id;
                    g->pad_x = mx;
                    g->pad_y = my;
                } else if (hover) {
                    act_on(g, hover, verb, mx, my);
                } else if (in_case) {
                    case_click(g);
                } else if (vm->cursor_object == PETKA_NONE && !g->map_open) {
                    // Nothing under the click (the end of sub_414320): only
                    // two of the cursor's actions do anything. The
                    // controller's plain pointer walks.
                    uint16_t op = pad_star ? PETKA_OP_WALK : kVerbs[verb].op;
                    if (op == PETKA_OP_WALK) {
                        PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
                        // Sent somewhere else: whatever he was walking for is
                        // off, and if that was a way out of the room,
                        // Chapayev stops following him there.
                        int leaving = 0;
                        for (uint32_t i = 0; petka && i < petka->walk_tail.count; i++)
                            if (petka->walk_tail.messages[i].opcode == PETKA_OP_GOTO) leaving = 1;
                        if (leaving) petka_scriptvm_stop_hero(vm, PETKA_ID_CHAPAYEV);
                        petka_scriptvm_forget_walk(vm, PETKA_ID_PETKA);
                        if (petka && host_walk(g, PETKA_ID_PETKA, (int32_t)mx, (int32_t)my)) petka->busy = 1;
                    } else if (op == PETKA_OP_OBJECTUSE) {
                        // "Send Chapayev" at the floor: he walks there.
                        PetkaObjectState *chapayev = petka_scriptvm_state(vm, PETKA_ID_CHAPAYEV);
                        if (chapayev && host_walk(g, PETKA_ID_CHAPAYEV, (int32_t)mx, (int32_t)my)) chapayev->busy = 1;
                    }
                }
            }
        }

        // The stick works the star while it is up, not the pointer.
        if (plat_pad()) plat_hold_pointer(g->pad_hold || g->star_open || (g->case_open && can_act));

        plat_begin();
        ClearBackground(BLACK);
        BeginMode2D(view);
        if (g->bg_tex.id)
            DrawTexturePro(g->bg_tex, (Rectangle){0, 0, (float)g->bg_tex.width, (float)g->bg_tex.height},
                           (Rectangle){0, 0, g->bg_tex.width * scale, g->bg_tex.height * scale}, (Vector2){0, 0}, 0, WHITE);
        for (int i = 0; i < scene_count; i++) {
            const Visual *v = &g->visuals[obj_index(g, scene[i].obj)];
            Rectangle d = visual_dest(g, scene[i].obj);
            int shown = vm->state[obj_index(g, scene[i].obj)].visible;
            // On the map a place's clip is its highlight: the script keeps it
            // hidden, and the map's mouse function (sub_41D4D0) shows the one
            // under the pointer, on its first frame, and hides the rest.
            if (shown || (g->map_open && !g->panel_open && scene[i].obj == hover))
                visual_draw(v, (Rectangle){d.x * scale, d.y * scale, d.width * scale, d.height * scale});
            if (debug && !is_hero(scene[i].obj->id)) {
                Rectangle r = {v->rect.left * scale, v->rect.top * scale,
                               (v->rect.right - v->rect.left) * scale, (v->rect.bottom - v->rect.top) * scale};
                DrawRectangleLinesEx(r, 1, scene[i].obj == hover ? ORANGE : Fade(shown ? LIME : MAGENTA, 0.6f));
                DrawTextEx(font, TextFormat("%u %s", scene[i].obj->id, utf8_name(scene[i].obj->name)),
                           (Vector2){r.x + 2, r.y + 2}, 14, 1, YELLOW);
            }
        }

        EndMode2D();

        if (g->case_open) draw_case(g, scale);
        if (g->star_open) draw_star(g, scale);
        if (g->save_mode) draw_saves(g, font, scale);

        // Top bar: what a click would do.
        // The object under the cursor is captioned as the exe does it (the
        // end of 0x414020): its NAMES.INI name in a pale cyan, or, where it
        // has none, its script name in dark red.
        const char *caption = "";
        Color caption_color = {192, 255, 255, 255};
        if (hover) {
            const char *shown = g->names[hover - g->script->objects];
            if (shown && shown[0]) {
                caption = utf8_name(shown);
            } else {
                caption = utf8_name(hover->name);
                caption_color = (Color){128, 0, 0, 255};
            }
        }
        const char *action;
        if (!vm->cursor_visible || (g->seq_room != PETKA_NONE && !g->map_open)) {
            action = "...";
            caption = "";
        } else if (vm->cursor_object != PETKA_NONE) {
            const PetkaObject *item = petka_script_find(g->script, vm->cursor_object);
            action = TextFormat("%s -> %s", item ? display_name(g, item) : "?", hover ? "" : "...");
        } else {
            action = kVerbs[g->verb].label;
        }
        // The bar along the top (the cursor's action, the room's name) is
        // the launcher's own, not the original's: shown with F1 only.
        if (debug) {
            DrawRectangle(0, 0, plat_width(), 30, Fade(BLACK, 0.6f));
            DrawTextEx(font, action, (Vector2){10, 5}, 20, 1, WHITE);
        }
        // (The panel's mouse function names nothing: no captions there.)
        if (caption[0] && !g->panel_open) draw_bottom_text(font, caption, caption_color, (Color){10, 10, 10, 255}, 0, 1, scale);
        if (room && debug) {
            const char *name = display_name(g, room);
            Vector2 size = MeasureTextEx(font, name, 18, 1);
            DrawTextEx(font, name, (Vector2){plat_width() - size.x - 10, 6}, 18, 1, SKYBLUE);
        }

        if (g->talking && g->line[0] && g->subtitles) {
            // Each speaker's lines are in his own colour.
            const PetkaObject *talker = petka_script_find(g->script, g->talker);
            // A speaker with a colour of his own (CAST.INI) is drawn on
            // 0x00007F, dark red; one without in 0x0A0A0A on white.
            Color ink = talker ? g->colors[obj_index(g, talker)] : NO_CAST_INK;
            int cast = ink.r != NO_CAST_INK.r || ink.g != NO_CAST_INK.g || ink.b != NO_CAST_INK.b;
            draw_bottom_text(font, g->line, ink, cast ? (Color){127, 0, 0, 255} : WHITE, 1, 1, scale);
        }
        if (g->desc[0]) draw_description(g, font, scale);

        if (g->menu_count > 0) menu_lines(g, font, scale, menu_rects, menu_hover, 1);

        if (g->status[0] && game_now() < g->status_until) {
            // The launcher's own messages, top left (higher with the bar
            // of the F1 overlay off): white, on a band like the rest.
            Vector2 at = {10 + 6 * scale, debug ? 36 : 6 * scale};
            Vector2 extent = MeasureTextEx(font, g->status, 18, 1);
            draw_band((Rectangle){at.x, at.y, extent.x, extent.y}, scale);
            DrawTextEx(font, g->status, at, 18, 1, WHITE);
        }
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
            // What decides whether a click does anything, for a screenshot
            // of a game that has stopped answering.
            const PetkaObjectState *vich = petka_scriptvm_state(vm, PETKA_ID_CHAPAYEV);
            DrawTextEx(font, TextFormat("petka vis %d act %d anim %d busy %d walk %d tail %u res %u | vich vis %d act %d busy %d walk %d",
                                        petka ? petka->visible : -1, petka ? petka->active : -1, petka ? petka->animate : -1,
                                        petka ? petka->busy : -1, g->walk[0].active, petka ? petka->walk_tail.count : 0,
                                        petka ? petka->resource : 0, vich ? vich->visible : -1, vich ? vich->active : -1,
                                        vich ? vich->busy : -1, g->walk[1].active),
                       (Vector2){10, 78}, 16, 1, LIME);
            DrawTextEx(font, TextFormat("cursor %d panel %d verb %d item %d | talk %d pause %d menu %d seq %u map %d case %d star %d save %d | hover %d",
                                        vm->cursor_visible, vm->interface_enabled, g->verb,
                                        vm->cursor_object == PETKA_NONE ? -1 : (int)vm->cursor_object, g->talking, g->paused,
                                        g->menu_count, g->seq_room, g->map_open, g->case_open, g->star_open, g->save_mode,
                                        hover ? (int)hover->id : -1),
                       (Vector2){10, 98}, 16, 1, LIME);
        }
        // The pointer goes on last, over everything (its z is 1000). It is
        // there whenever the cursor is shown; a dialogue hides it for its
        // phrases and brings it back for a menu.
        // With a controller the star stands for it while it is up.
        if (vm->cursor_visible && (g->seq_room == PETKA_NONE || g->map_open) && (!g->talking || g->menu_count > 0) &&
            !(plat_pad() && g->star_open)) {
            uint16_t clip = g->map_open || g->save_mode ? CURSOR_INTERFACE
                          : send_chapayev ? (uint16_t)(CURSOR_ACTION0 + VERB_COUNT - 1)
                          : plat_pad() && vm->cursor_object == PETKA_NONE ? CURSOR_INTERFACE
                          : g->menu_count > 0 ? CURSOR_POINTER
                          : vm->cursor_resource != PETKA_NONE ? vm->cursor_resource
                          : (uint16_t)(CURSOR_ACTION0 + g->verb);
            HideCursor();
            // On the save screen the pointer runs while it is on a slot or
            // a page corner (its mouse function, sub_41E2B0); nothing else
            // there marks what it is over.
            int lively = hover != NULL;
            if (g->save_mode) {
                Vector2 p = {mouse.x / scale, mouse.y / scale};
                lively = CheckCollisionPointRec(p, kSavePrev) || CheckCollisionPointRec(p, kSaveNext);
                for (int i = 0; i < SAVE_SLOTS; i++) lively = lively || CheckCollisionPointRec(p, kSaveSlot[i]);
            }
            // The interface's pointer has its tip at the point, the others
            // their middle.
            draw_cursor(g, clip, lively, clip != CURSOR_INTERFACE, mouse, scale);
        } else {
            ShowCursor();
        }
        plat_end();
        // A play's screenshot is taken two frames on: read right after the
        // buffers are swapped, the screen can still be the frame before.
        if (s_play.shot[0] && ++s_play.shot_wait > 2) {
            Image shot = LoadImageFromScreen();
            ExportImage(shot, s_play.shot);
            UnloadImage(shot);
            s_play.shot[0] = 0;
            s_play.shot_wait = 0;
        }
        if (s_play.over && !s_play.shot[0]) break;
        long series_start = 0, series_step = 1, series_count = 0;
        if (shot_path && getenv("PETKA_SHOT_SERIES") &&
            sscanf(getenv("PETKA_SHOT_SERIES"), "%ld:%ld:%ld", &series_start, &series_step, &series_count) == 3 &&
            series_step > 0 && frame >= series_start && (frame - series_start) % series_step == 0 &&
            (frame - series_start) / series_step < series_count) {
            Image shot = LoadImageFromScreen();
            ExportImage(shot, TextFormat("%s-%ld.png", shot_path, frame));
            UnloadImage(shot);
        }
        if (shot_path && frame >= shot_frame) {
            // Not TakeScreenshot(): it only writes below the working directory.
            Image shot = LoadImageFromScreen();
            ExportImage(shot, shot_path);
            UnloadImage(shot);
            break;
        }
    }

    part_close(g);
    if (g->bg_tex.id) UnloadTexture(g->bg_tex);
    for (int i = 0; i < MAX_SOUNDS; i++) if (g->sounds[i].frameCount > 0) UnloadSound(g->sounds[i]);
    if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
    CloseAudioDevice();
    plat_close();
    CloseWindow();
    return s_play.failed ? 3 : 0;
}
