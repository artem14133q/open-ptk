// saves.h -- part of the launcher (launcher/src).
//
// EN: Saved games: the save screen, its slots and pictures, writing and reading a game.
// RU: Сохранения: экран сохранений, ячейки и картинки, запись и чтение игры.
#ifndef PETKA_LAUNCHER_SAVES_H
#define PETKA_LAUNCHER_SAVES_H

#include "game.h"

// ---- saved games ----
//
// Files and screen as the original has them: save<n>.dat in the working
// directory, in the exe's own format (petka/save.h); eighteen slots on three
// pages of six, each shown by a small picture of the room with the time of
// saving under it. The slots and the two page corners are rectangles the exe
// builds at start-up; the screen's pictures are LOAD1-3 and SAVE1-3.
#define SAVE_LOAD_PICTURE 4990

#define SAVE_SAVE_PICTURE 4995

extern const Rectangle kSaveSlot[SAVE_SLOTS];
extern const Rectangle kSavePrev, kSaveNext;

// EN: The file of a save slot.
// RU: Файл ячейки сохранения.
void save_path(int page, int slot, char *out, size_t out_sz);
// EN: Takes the picture of the room for a save.
// RU: Делает картинку комнаты для сохранения.
void take_thumb(Game *g);
// EN: A save's picture as a texture.
// RU: Картинка сохранения как текстура.
Texture2D thumb_texture(const uint8_t *thumb);
// EN: Writes the game to a file.
// RU: Записывает игру в файл.
int game_save(Game *g, const char *path);
// EN: Reads the game from a file (another part: switches to it).
// RU: Читает игру из файла (другая часть - переключается на неё).
int game_load(Game *g, const char *path);
// EN: Lets the save screen's pictures go.
// RU: Освобождает картинки экрана сохранений.
void saves_unload(Game *g);
// EN: Reads the slots of the save screen's page.
// RU: Читает ячейки страницы экрана сохранений.
void saves_show(Game *g);
// EN: Puts the save screen up, to load (1) or save (2).
// RU: Открывает экран сохранений: загрузка (1) или запись (2).
void saves_open(Game *g, int mode);
// EN: Takes the save screen down.
// RU: Закрывает экран сохранений.
void saves_close(Game *g);
// EN: A click on the save screen: a slot or a page corner.
// RU: Щелчок по экрану сохранений: ячейка или уголок страницы.
void saves_click(Game *g, Vector2 p);
// EN: Draws the save screen.
// RU: Рисует экран сохранений.
void draw_saves(Game *g, Font font, float scale);

#endif
