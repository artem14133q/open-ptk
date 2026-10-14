// The finished frame put on the screen through a shader that makes it look
// like the CRT monitor the game was made for: the scanlines of its 480
// lines, a slightly bulging glass, the phosphor's red, green and blue
// stripes, a little spread of the beam and darker corners. Off, the frame
// goes up as it is.
#ifndef PETKA_CRT_H
#define PETKA_CRT_H

#include "raylib.h"

#include <stdbool.h>

// After the window: loads the shader. It starts on, unless PETKA_CRT=0 --
// or, with no PETKA_CRT, in an unattended dev run (PETKA_SHOT, PETKA_PLAY,
// PETKA_TURBO), whose screenshots are compared pixel for pixel.
void crt_init(void);
void crt_close(void);
void crt_toggle(void);
void crt_set(bool on);
bool crt_on(void);

// Draws `src` of `tex` over `dst`, through the shader when it is on.
void crt_draw(Texture2D tex, Rectangle src, Rectangle dst);

#endif
