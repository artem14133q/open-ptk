// part.h -- part of the launcher (launcher/src).
//
// EN: A part of the game: opening and closing its stores and script, starting and switching parts.
// RU: Часть игры: открытие и закрытие её архивов и сценария, запуск и смена частей.
#ifndef PETKA_LAUNCHER_PART_H
#define PETKA_LAUNCHER_PART_H

#include "game.h"

// EN: Sets the script going afresh, no room yet.
// RU: Запускает сценарий заново, пока без комнаты.
void game_init(Game *g);
// EN: Enters the part's first room.
// RU: Входит в первую комнату части.
void game_enter(Game *g);
// EN: Starts the part: the script and its first room.
// RU: Начинает часть: сценарий и первая комната.
void game_start(Game *g);
// EN: Lets everything of the loaded part go.
// RU: Освобождает всё, что относится к загруженной части.
void part_close(Game *g);
// EN: Opens a part: its stores, script, dialogues and resources.
// RU: Открывает часть: архивы, сценарий, диалоги и ресурсы.
int part_open(Game *g, int number, int chapter);
// EN: Opens a chapter's store of a part.
// RU: Открывает архив главы части.
void chapter_open(Game *g, int chapter);
// EN: Takes down whatever the launcher has up for the game.
// RU: Закрывает всё, что лаунчер показывает поверх игры.
void game_screens_down(Game *g);
// EN: Takes the game down and builds it again for a part.
// RU: Разбирает игру и собирает заново для части.
void game_switch(Game *g, int part, int chapter);
// EN: Gives a fresh Game its starting state and the VM's callbacks.
// RU: Задаёт новой Game начальное состояние и обратные вызовы машины.
void game_setup(Game *g, const char *game_dir);

#endif
