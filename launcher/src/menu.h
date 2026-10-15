// menu.h -- part of the launcher (launcher/src).
//
// EN: The start-up menu.
// RU: Стартовое меню.
#ifndef PETKA_LAUNCHER_MENU_H
#define PETKA_LAUNCHER_MENU_H

#include "game.h"

// EN: The start-up menu; 0 to leave, 1 for a new game, 2 for "load".
// RU: Стартовое меню; 0 - выход, 1 - новая игра, 2 - загрузка.
int run_menu(Game *g, Font font);

#endif
