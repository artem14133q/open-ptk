#include "platform.h"

#include <math.h>

#ifndef __SWITCH__

// ---- desktop: raylib as it is ----

void plat_init(void) {}
void plat_close(void) {}
void plat_begin(void) { BeginDrawing(); }
void plat_end(void) { EndDrawing(); }
int plat_width(void) { return GetScreenWidth(); }
int plat_height(void) { return GetScreenHeight(); }
void plat_poll(void) {}
Vector2 plat_pointer(void) { return GetMousePosition(); }
bool plat_click(void) { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
bool plat_rclick(void) { return IsMouseButtonPressed(MOUSE_BUTTON_RIGHT); }
bool plat_key(int key) { return IsKeyPressed(key); }
bool plat_key_down(int key) { return IsKeyDown(key); }
bool plat_any_key(void) {
    return GetKeyPressed() || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
}
bool plat_chapayev_held(void) { return false; }
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

void plat_init(void) {
    // Started by nxlink from a computer, stdout and stderr go back to it --
    // the launcher's trace and messages. Started any other way this fails
    // quietly and nothing changes.
    if (R_SUCCEEDED(socketInitializeDefault()) && nxlinkStdio() < 0) socketExit();
    s_surface = LoadRenderTexture(SURFACE_W, SURFACE_H);
    SetTextureFilter(s_surface.texture, TEXTURE_FILTER_BILINEAR);
}

void plat_close(void) { UnloadRenderTexture(s_surface); }

void plat_begin(void) { BeginTextureMode(s_surface); }

void plat_end(void) {
    EndTextureMode();
    BeginDrawing();
    ClearBackground(BLACK);
    // A render texture is upside down: drawn with a negative height.
    DrawTexturePro(s_surface.texture, (Rectangle){0, 0, SURFACE_W, -SURFACE_H},
                   (Rectangle){(SCREEN_W - SURFACE_W) / 2.0f, 0, SURFACE_W, SURFACE_H}, (Vector2){0, 0}, 0, WHITE);
    EndDrawing();
}

int plat_width(void) { return SURFACE_W; }
int plat_height(void) { return SURFACE_H; }

void plat_poll(void) {
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
bool plat_click(void) { return s_tapped || IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT); }
bool plat_rclick(void) { return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_DOWN); }

bool plat_key(int key) {
    switch (key) {
    case KEY_I: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_UP);      // X: the case
    case KEY_M: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);    // Y: the map
    case KEY_O: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_RIGHT);       // +: the panel
    case KEY_ESCAPE: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_LEFT);   // -: back out
    case KEY_F3: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_2);    // ZL: load
    case KEY_F2: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);   // ZR: save
    case KEY_F1: return IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_THUMB);       // right stick: debug
    default: return false;
    }
}

bool plat_key_down(int key) { (void)key; return false; }

bool plat_any_key(void) {
    return s_tapped || GetGamepadButtonPressed() != GAMEPAD_BUTTON_UNKNOWN;
}

bool plat_chapayev_held(void) { return IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_1); } // R

bool plat_pad(void) { return true; }
void plat_set_pointer(Vector2 at) { s_pointer = at; }
void plat_pointer_speed(float k) { s_speed = k; }
void plat_hold_pointer(bool hold) {
    if (s_held && !hold) s_latched = true;
    s_held = hold;
}
int plat_list_step(void) { return s_list_step; }
int plat_list_step_x(void) { return s_list_step_x; }
int plat_page_step(void) {
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) return -1;  // L
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) return 1;  // R
    return 0;
}
Vector2 plat_aim(void) {
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
