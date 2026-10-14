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
int plat_action_step(void) { return 0; }
const char *plat_default_game_dir(void) { return GetApplicationDirectory(); }
Font plat_system_font(int size, int *codepoints, int count) {
    (void)size; (void)codepoints; (void)count;
    return (Font){0};
}
bool plat_has_films(void) { return true; }

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
    if (len > 0.15f) {
        float speed = 2.0f + 14.0f * (len - 0.15f) / 0.85f * (len - 0.15f) / 0.85f;
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

int plat_action_step(void) {
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) return -1;  // L
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) return 1;  // R
    return 0;
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

bool plat_has_films(void) { return false; }

#endif
