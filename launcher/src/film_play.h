// film_play.h -- part of the launcher (launcher/src).
//
// EN: Films (.AVI): found next to the game and played full screen over a film source (film.h).
// RU: Видеоролики (.AVI): поиск рядом с игрой и показ на весь экран через источник роликов (film.h).
#ifndef PETKA_LAUNCHER_FILM_PLAY_H
#define PETKA_LAUNCHER_FILM_PLAY_H

#include "game.h"

// ---- films ----
//
// The game's videos are loose .AVI files next to it, not in the stores. The
// exe (sub_4124A0) hands one to the system's player -- MCI: "open <file>
// alias film", stretched to the 640x480 window -- with its own sounds paused,
// and sits in a loop until the film ends or a key or a mouse button is
// pressed. The one the script plays, INTRO.AVI, is Indeo 3 video, which
// nothing here decodes; so, in the same spirit, the launcher hands the file
// to FFmpeg (film.h) and shows the frames it sends back.
#define FILM_FPS 24

// EN: Finds a film next to the game, any case.
// RU: Ищет ролик рядом с игрой, в любом регистре.
int find_film(const Game *g, const char *name, char *out, size_t out_sz);
// EN: Plays a film full screen until it ends or a key is pressed.
// RU: Показывает ролик на весь экран до конца или до нажатия.
void play_film(Game *g, const char *name);

#endif
