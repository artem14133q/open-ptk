// What differs between a desktop window and the Nintendo Switch, behind a few
// calls the launcher makes instead of raylib's own.
//
// The game is drawn on a surface of plat_width() x plat_height() pixels: the
// window itself on a desktop; on the Switch a 960x720 picture (640x480 at one
// and a half) shown in the middle of the 1280x720 screen. Input is the mouse
// and the keyboard on a desktop; on the Switch the touch screen and the
// controller drive a pointer and stand in for the keys the game uses.
#ifndef PETKA_PLATFORM_H
#define PETKA_PLATFORM_H

#include "raylib.h"

#include <stdbool.h>

// After InitWindow: makes the Switch's drawing surface.
void plat_init(void);
// Before CloseWindow.
void plat_close(void);

void plat_begin(void); // BeginDrawing, on the surface
void plat_end(void);   // EndDrawing, the surface put on the screen

int plat_width(void);
int plat_height(void);

// Once a frame, before any of the input calls below: reads the touch screen
// and the controller (nothing to do on a desktop).
void plat_poll(void);

Vector2 plat_pointer(void); // the pointer, in surface pixels
bool plat_click(void);      // the left button, a tap, A
bool plat_rclick(void);     // the right button, B
bool plat_key(int key);     // IsKeyPressed, or the controller's stand-in for that key
bool plat_key_down(int key);
bool plat_any_key(void);    // any key or button at all (to end a film)
// The controller's L and R: -1 or +1 to step through the cursor's actions,
// 0 otherwise (always 0 on a desktop, which has a key for each).
int plat_action_step(void);

// Picking from a list with the controller (the Switch): the pointer is
// put on an entry, held there -- the stick and the D-pad stop moving it,
// a touch still does -- and the controller's up and down step through the
// list. On a desktop the mouse does it all: plat_pad() is false and the
// rest do nothing.
bool plat_pad(void);
void plat_set_pointer(Vector2 at); // surface pixels
void plat_hold_pointer(bool hold);
int plat_list_step(void); // -1 up, +1 down, 0 neither
// Where the left stick is pushed, x right and y down, each -1..1; zero on a
// desktop.
Vector2 plat_aim(void);

// The folder the game's files are in when none is given: next to the
// program on a desktop, sdmc:/switch/petka/ on the Switch.
const char *plat_default_game_dir(void);

// A font with Cyrillic in it from the system, or a zero Font. On the Switch
// this is the console's shared font.
Font plat_system_font(int size, int *codepoints, int count);

#endif
