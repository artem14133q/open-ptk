// common.h -- part of the launcher (launcher/src).
//
// EN: The launcher's shared state and small helpers: the game's clock, the status line, object lookups.
// RU: Общее состояние лаунчера и мелкие помощники: часы игры, строка состояния, поиск объектов.
#ifndef PETKA_LAUNCHER_COMMON_H
#define PETKA_LAUNCHER_COMMON_H

#include "game.h"

extern const Verb kVerbs[6];
extern double s_clock;
extern int s_turbo;

// EN: The game's clock in seconds (the frame counter in turbo runs).
// RU: Часы игры в секундах (счётчик кадров в турбо-режиме).
double game_now(void);
// EN: The length of this frame in seconds.
// RU: Длительность этого кадра в секундах.
float game_dt(void);
// EN: Puts a note on the status line for a few seconds.
// RU: Показывает строку в строке состояния на несколько секунд.
void set_status(Game *g, const char *text);
// EN: An object's index in the script's list, for its state and visual.
// RU: Номер объекта в списке сценария (для его состояния и клипа).
int obj_index(const Game *g, const PetkaObject *obj);
// EN: Whether an object is Petka or Chapayev.
// RU: Является ли объект Петькой или Чапаевым.
int is_hero(uint16_t id);
// EN: The room on screen: a sequence's or the map's if one is up, else the game's.
// RU: Комната на экране: заставки или карты, если они открыты, иначе игровая.
uint16_t screen_room(const Game *g);

#endif
