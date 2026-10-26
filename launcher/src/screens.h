// screens.h -- part of the launcher (launcher/src).
//
// EN: The map and the panel (the game's menu with the dials), and their settings file.
// RU: Карта и панель (меню игры с регуляторами), и файл их настроек.
#ifndef PETKA_LAUNCHER_SCREENS_H
#define PETKA_LAUNCHER_SCREENS_H

#include "game.h"

extern const char *const kDialKey[4];

// EN: Whether the map can be opened from this room.
// RU: Можно ли открыть карту из этой комнаты.
int map_allowed(const Game *g);
// EN: The places on the map in use now are known: none of them blinks (at the part's start, a game loaded).
// RU: Места на карте, открытые сейчас, считаются известными: ни одно не мигает.
void map_places_known(Game *g);
// EN: Whether a new place's picture is up in this moment of its blinking on the map.
// RU: Видна ли сейчас картинка нового места, мигающая на карте.
int map_blinking(const Game *g, const PetkaObject *obj);
// EN: Takes the map (or the panel) down.
// RU: Закрывает карту (или панель).
void map_leave(Game *g);
// EN: The map command: to the map, or back from it.
// RU: Команда карты: открыть карту или вернуться.
void map_command(Game *g);
// EN: The panel command: to the panel, or back from it.
// RU: Команда панели: открыть панель или вернуться.
void panel_command(Game *g);
// EN: Where the panel's settings are kept.
// RU: Где хранятся настройки панели.
const char *options_file(void);
// EN: Reads the panel's settings.
// RU: Читает настройки панели.
void options_load(Game *g);
// EN: Writes the panel's settings.
// RU: Записывает настройки панели.
void options_save(const Game *g);
// EN: Asks for a new game: the first part from PARTS.INI.
// RU: Запрашивает новую игру: первая часть из PARTS.INI.
void new_game(Game *g);
// EN: A click on the panel: its buttons and dials.
// RU: Щелчок по панели: кнопки и регуляторы.
void panel_click(Game *g, const PetkaObject *hit);
// EN: Shows the dials and switches on the panel's clips.
// RU: Показывает регуляторы и переключатели на клипах панели.
void panel_show_state(Game *g);

#endif
