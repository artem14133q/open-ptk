// text.h -- part of the launcher (launcher/src).
//
// EN: Text: CP1251 to UTF-8, the Cyrillic font, wrapped and rimmed lines, the dark band behind texts.
// RU: Текст: перевод CP1251 в UTF-8, кириллический шрифт, перенос строк, обводка, тёмная подложка.
#ifndef PETKA_LAUNCHER_TEXT_H
#define PETKA_LAUNCHER_TEXT_H

#include "game.h"

extern int s_band;

// EN: Converts the game's CP1251 text to UTF-8.
// RU: Переводит текст игры из CP1251 в UTF-8.
void cp1251_to_utf8(const char *in, char *out, size_t out_sz);
// EN: An object's script name in UTF-8 (for logs).
// RU: Имя объекта из сценария в UTF-8 (для журнала).
const char *utf8_name(const char *cp1251);
// EN: Loads a font with Cyrillic: the system's, or one of the usual places.
// RU: Загружает шрифт с кириллицей: системный или из обычных мест.
Font load_cyrillic_font(void);
// EN: Draws text broken into lines no wider than a width.
// RU: Рисует текст с переносом строк по ширине.
float draw_wrapped(Font font, const char *text, float x, float y, float width, float size, float leading, Color ink);
// EN: Breaks text into lines no wider than a width; returns the count.
// RU: Разбивает текст на строки не шире заданной; возвращает их число.
int wrap_text(Font font, const char *text, float size, float limit, char lines[][512], int max);
// EN: Draws a line of text with a rim around its letters.
// RU: Рисует строку текста с обводкой букв.
void draw_rimmed(Font font, const char *text, Vector2 at, float size, Color ink, Color fill, float scale);
// EN: The dark rounded band behind a text (if switched on).
// RU: Тёмная скруглённая подложка под текстом (если включена).
void draw_band(Rectangle r, float scale);
// EN: Draws lines resting on the bottom of the screen, as subtitles.
// RU: Рисует строки у нижнего края экрана, как субтитры.
void draw_bottom_text(Font font, const char *text, Color ink, Color fill, int centre_lines, int backdrop, float scale);

#endif
