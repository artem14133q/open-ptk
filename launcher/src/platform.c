#include "platform.h"

#include "crt.h"

#include <math.h>

#ifndef __SWITCH__

// ---- desktop: raylib as it is ----

// The frame is drawn on a surface the size of the window, then put up
// through the CRT shader (crt.h); F4 turns that on and off.
static RenderTexture2D s_surface;
static bool s_muted; // plat_mute(), until the next plat_poll()

void plat_mute(void) { s_muted = true; }
void plat_init(void) { crt_init(); }
void plat_close(void) {
    if (s_surface.id) UnloadRenderTexture(s_surface);
    crt_close();
}
void plat_begin(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    if (!s_surface.id || s_surface.texture.width != w || s_surface.texture.height != h) {
        if (s_surface.id) UnloadRenderTexture(s_surface);
        s_surface = LoadRenderTexture(w, h);
        SetTextureFilter(s_surface.texture, TEXTURE_FILTER_BILINEAR);
    }
    BeginTextureMode(s_surface);
}
void plat_end(void) {
    EndTextureMode();
    BeginDrawing();
    ClearBackground(BLACK);
    // A render texture is upside down: drawn with a negative height.
    float w = (float)s_surface.texture.width, h = (float)s_surface.texture.height;
    crt_draw(s_surface.texture, (Rectangle){0, 0, w, -h}, (Rectangle){0, 0, w, h});
    EndDrawing();
}
int plat_width(void) { return GetScreenWidth(); }
int plat_height(void) { return GetScreenHeight(); }
void plat_poll(void) {
    s_muted = false;
    if (IsKeyPressed(KEY_F4)) crt_toggle();
}
Vector2 plat_pointer(void) { return GetMousePosition(); }
bool plat_click(void) { return !s_muted && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
bool plat_rclick(void) { return !s_muted && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT); }
bool plat_key(int key) { return !s_muted && IsKeyPressed(key); }
bool plat_key_down(int key) { return !s_muted && IsKeyDown(key); }
bool plat_any_key(void) {
    return !s_muted &&
           (GetKeyPressed() || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));
}
bool plat_chapayev_held(void) { return false; }
bool plat_walk_held(void) { return false; }
bool plat_pad(void) { return false; }
void plat_set_pointer(Vector2 at) { (void)at; }
void plat_hold_pointer(bool hold) { (void)hold; }
int plat_list_step(void) { return 0; }
int plat_list_step_x(void) { return 0; }
int plat_page_step(void) { return 0; }
Vector2 plat_aim(void) { return (Vector2){0, 0}; }
void plat_pointer_speed(float k) { (void)k; }
const char *plat_default_game_dir(void) { return GetApplicationDirectory(); }
Font plat_system_font(int size, int *codepoints, int count) {
    (void)size; (void)codepoints; (void)count;
    return (Font){0};
}

#else

// ---- Nintendo Switch ----

#include <switch.h>

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#define SURFACE_W 960
#define SURFACE_H 720
#define SCREEN_W 1280
#define SCREEN_H 720
#define PAD 0 // the first controller (handheld or the first one attached)

static RenderTexture2D s_surface;
static Vector2 s_pointer = {SURFACE_W / 2.0f, SURFACE_H / 2.0f};
static bool s_tapped, s_touching;
static bool s_held;     // plat_hold_pointer: the stick and the D-pad leave it be
static bool s_latched;  // let go, but the stick is still pushed from then
static float s_speed = 1; // plat_pointer_speed
static int s_list_step; // this frame's step through a list
static int s_stick_dir; // where the stick was pushed last frame: -1, 0, +1
static int s_list_step_x, s_stick_dir_x; // the same across
static bool s_muted; // plat_mute(), until the next plat_poll()
static bool s_sockets; // started by nxlink: sockets up, the output going there

void plat_mute(void) { s_muted = true; }

void plat_init(void) {
    // Started by nxlink from a computer, stdout and stderr go back to it --
    // the launcher's trace and messages. Started any other way the
    // network is left alone: the console's socket service (bsdsocket) has
    // been seen to die under a game that brought sockets up for nothing,
    // and its death takes the whole console down (2162-0002).
    if (__nxlink_host.s_addr != 0 && R_SUCCEEDED(socketInitializeDefault())) {
        s_sockets = nxlinkStdio() >= 0;
        if (!s_sockets) socketExit();
    }
    // A trace.txt in the game's folder turns the trace on and puts it, with
    // everything else said, in petka.log there: for what happens on the
    // console alone. Line by line, so a hang leaves the lines before it.
    FILE *flag = fopen("sdmc:/switch/petka/trace.txt", "r");
    if (flag) {
        fclose(flag);
        // Both appending: each line goes on the end, whichever stream says it.
        remove("sdmc:/switch/petka/petka.log");
        if (freopen("sdmc:/switch/petka/petka.log", "a", stderr)) {
            freopen("sdmc:/switch/petka/petka.log", "a", stdout);
            setvbuf(stderr, NULL, _IOLBF, 0);
            setvbuf(stdout, NULL, _IOLBF, 0);
            setenv("PETKA_TRACE", "1", 1);
        }
    }
    s_surface = LoadRenderTexture(SURFACE_W, SURFACE_H);
    SetTextureFilter(s_surface.texture, TEXTURE_FILTER_BILINEAR);
    crt_init();
}

void plat_close(void) {
    UnloadRenderTexture(s_surface);
    crt_close();
    if (s_sockets) socketExit();
}

void plat_begin(void) { BeginTextureMode(s_surface); }

void plat_end(void) {
    EndTextureMode();
    BeginDrawing();
    ClearBackground(BLACK);
    // A render texture is upside down: drawn with a negative height. Through
    // the CRT shader (crt.h), which a press of the left stick turns on and off.
    crt_draw(s_surface.texture, (Rectangle){0, 0, SURFACE_W, -SURFACE_H},
             (Rectangle){(SCREEN_W - SURFACE_W) / 2.0f, 0, SURFACE_W, SURFACE_H});
    EndDrawing();
}

int plat_width(void) { return SURFACE_W; }
int plat_height(void) { return SURFACE_H; }

void plat_poll(void) {
    s_muted = false;
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_THUMB)) crt_toggle(); // left stick press
    // The touch screen: a touch puts the pointer there, and its first frame
    // is a click.
    bool touching = GetTouchPointCount() > 0;
    if (touching) {
        Vector2 t = GetTouchPosition(0);
        s_pointer.x = t.x - (SCREEN_W - SURFACE_W) / 2.0f;
        s_pointer.y = t.y;
    }
    s_tapped = touching && !s_touching;
    s_touching = touching;

    // The left stick moves the pointer, faster the further it is pushed;
    // the D-pad nudges it a pixel at a time for fine aiming.
    float x = GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_X);
    float y = -GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_Y); // libnx: up is positive
    float len = sqrtf(x * x + y * y);

    // Through a list: a press of up or down on the D-pad, or the stick
    // pushed up or down, once per push.
    int stick = y < -0.6f ? -1 : y > 0.6f ? 1 : 0;
    s_list_step = 0;
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_FACE_UP)) s_list_step = -1;
    else if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) s_list_step = 1;
    else if (stick != s_stick_dir) s_list_step = stick;
    s_stick_dir = stick;
    int stick_x = x < -0.6f ? -1 : x > 0.6f ? 1 : 0;
    s_list_step_x = 0;
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) s_list_step_x = -1;
    else if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) s_list_step_x = 1;
    else if (stick_x != s_stick_dir_x) s_list_step_x = stick_x;
    s_stick_dir_x = stick_x;
    if (s_held) return;

    // Let go while the stick was pushed -- it picked a part of the star --
    // the pointer waits for the stick to come back to rest: else it would
    // run off at once wherever the part was.
    if (s_latched && len <= 0.15f) s_latched = false;
    if (len > 0.15f && !s_latched) {
        float speed = (2.0f + 14.0f * (len - 0.15f) / 0.85f * (len - 0.15f) / 0.85f) * s_speed;
        s_pointer.x += x / len * speed;
        s_pointer.y += y / len * speed;
    }
    if (IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) s_pointer.x -= 1.5f;
    if (IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) s_pointer.x += 1.5f;
    if (IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_UP)) s_pointer.y -= 1.5f;
    if (IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) s_pointer.y += 1.5f;
    if (s_pointer.x < 0) s_pointer.x = 0;
    if (s_pointer.y < 0) s_pointer.y = 0;
    if (s_pointer.x > SURFACE_W - 1) s_pointer.x = SURFACE_W - 1;
    if (s_pointer.y > SURFACE_H - 1) s_pointer.y = SURFACE_H - 1;
}

Vector2 plat_pointer(void) { return s_pointer; }

// raylib-nx: RIGHT_FACE_RIGHT is A, _DOWN B, _UP X, _LEFT Y; MIDDLE_RIGHT is
// +, MIDDLE_LEFT -; the triggers are L, ZL, R, ZR.
bool plat_click(void) { return !s_muted && (s_tapped || IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)); }
bool plat_rclick(void) { return !s_muted && IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_DOWN); }

bool plat_key(int key) {
    if (s_muted) return false;
    switch (key) {
    case KEY_I: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_UP);      // X: the case
    case KEY_M: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);    // Y: the map
    case KEY_O: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_RIGHT);       // +: the panel
    case KEY_TAB: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_LEFT);      // -: the extras
    case KEY_F3: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_2);    // ZL: load
    case KEY_F2: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);   // ZR: save
    case KEY_F1: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_THUMB);       // right stick: debug
    default: return false;
    }
}

bool plat_key_down(int key) { (void)key; return false; }

// Not raylib-nx's GetGamepadButtonPressed(): it names a button while it is
// held, not as it goes down, so the A that picked "new game" from the menu,
// still held, ended the film that came next at once.
bool plat_any_key(void) {
    if (s_muted) return false;
    if (s_tapped) return true;
    for (int b = GAMEPAD_BUTTON_LEFT_FACE_UP; b <= GAMEPAD_BUTTON_RIGHT_THUMB; b++)
        if (IsGamepadButtonPressed(PAD, b)) return true;
    return false;
}

bool plat_chapayev_held(void) { return !s_muted && IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_1); } // R
bool plat_walk_held(void) { return !s_muted && IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_1); }    // L

bool plat_pad(void) { return true; }
void plat_set_pointer(Vector2 at) { s_pointer = at; }
void plat_pointer_speed(float k) { s_speed = k; }
void plat_hold_pointer(bool hold) {
    if (s_held && !hold) s_latched = true;
    s_held = hold;
}
int plat_list_step(void) { return s_muted ? 0 : s_list_step; }
int plat_list_step_x(void) { return s_muted ? 0 : s_list_step_x; }
int plat_page_step(void) {
    if (s_muted) return 0;
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) return -1;  // L
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) return 1;  // R
    return 0;
}
Vector2 plat_aim(void) {
    if (s_muted) return (Vector2){0, 0};
    return (Vector2){GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_X), -GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_Y)};
}

const char *plat_default_game_dir(void) { return "sdmc:/switch/petka/"; }

Font plat_system_font(int size, int *codepoints, int count) {
    // The console's own font, which covers Cyrillic; it lives in shared
    // memory and must stay there, so the data is not copied.
    PlFontData data;
    if (R_FAILED(plInitialize(PlServiceType_User))) return (Font){0};
    Font font = {0};
    if (R_SUCCEEDED(plGetSharedFontByType(&data, PlSharedFontType_Standard)))
        font = LoadFontFromMemory(".ttf", (const unsigned char *)data.address, (int)data.size, size, codepoints, count);
    return font;
}

#endif
