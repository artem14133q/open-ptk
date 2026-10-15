// The finished frame put on the screen through a shader that makes it look
// like the CRT monitor the game was made for: the scanlines of its 480
// lines, a slightly bulging glass, the phosphor's red, green and blue
// stripes, a little spread of the beam and darker corners. Off, the frame
// goes up as it is.
//
// RU: Готовый кадр выводится через шейдер, имитирующий ЭЛТ-монитор: строки
// развёртки, выпуклое стекло, полосы люминофора, тёмные углы.
#ifndef PETKA_CRT_H
#define PETKA_CRT_H

#include "raylib.h"

#include <stdbool.h>

// After the window: loads the shader. It starts on, unless PETKA_CRT=0 --
// or, with no PETKA_CRT, in an unattended dev run (PETKA_SHOT, PETKA_PLAY,
// PETKA_TURBO), whose screenshots are compared pixel for pixel.
// RU: Загружает шейдер; включён по умолчанию, кроме автоматических прогонов.
void crt_init(void);
// EN: Frees the shader.
// RU: Освобождает шейдер.
void crt_close(void);
// EN: Turns the CRT look on or off.
// RU: Включает или выключает вид ЭЛТ.
void crt_toggle(void);
// EN: Sets the CRT look on or off.
// RU: Включает или выключает вид ЭЛТ явно.
void crt_set(bool on);
// EN: Whether the CRT look is on.
// RU: Включён ли вид ЭЛТ.
bool crt_on(void);

// Draws `src` of `tex` over `dst`, through the shader when it is on.
// RU: Рисует текстуру через шейдер, если он включён.
void crt_draw(Texture2D tex, Rectangle src, Rectangle dst);

#endif
