// extras.h -- part of the launcher (launcher/src).
//
// EN: The extras menu (Tab, or - on the Switch): what this launcher adds, switched on and off.
// RU: Меню «Дополнительно» (Tab, на Switch - минус): что добавляет лаунчер, вкл/выкл.
#ifndef PETKA_LAUNCHER_EXTRAS_H
#define PETKA_LAUNCHER_EXTRAS_H

#include "game.h"

// ---- the extras ----
//
// What this launcher adds to the game, switched on and off in a menu of its
// own over the game -- Tab on a desktop, - on the Switch -- and kept in
// extras.ini beside the saves. The game goes on under it but hears no input.
enum { EXTRA_CRT, EXTRA_BAND, EXTRA_HIGHLIGHT, EXTRA_AIM, EXTRA_DEBUG, EXTRA_COUNT };

#define EXTRAS_FILE "extras.ini"

#define EXTRAS_X 60

#define EXTRAS_W 520

#define EXTRAS_Y 150

#define EXTRAS_ROW 30

extern const char *const kExtraName[EXTRA_COUNT];

// EN: The rows the extras menu shows.
// RU: Строки, которые показывает меню «Дополнительно».
int extras_rows(int *rows);
// EN: Whether an extra is on.
// RU: Включена ли функция.
int extra_value(const Game *g, int i, int debug);
// EN: Where a row of the extras menu is on screen.
// RU: Где на экране строка меню «Дополнительно».
Rectangle extras_row_rect(int k, float scale);
// EN: Writes the extras to extras.ini.
// RU: Записывает настройки в extras.ini.
void extras_save(const Game *g);
// EN: Reads the extras from extras.ini.
// RU: Читает настройки из extras.ini.
void extras_load(Game *g);
// EN: The extras menu's input, before the game's.
// RU: Ввод меню «Дополнительно», раньше игры.
void extras_update(Game *g, Vector2 mouse, float scale, int *debug);
// EN: Draws the extras menu over the game.
// RU: Рисует меню «Дополнительно» поверх игры.
void extras_draw(Game *g, Font font, float scale, Vector2 mouse, int debug);

#endif
