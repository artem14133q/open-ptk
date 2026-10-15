// inventory.h -- part of the launcher (launcher/src).
//
// EN: The case (the inventory): its things, buttons, lid and pages.
// RU: Чемодан (инвентарь): предметы, кнопки, крышка и страницы.
#ifndef PETKA_LAUNCHER_INVENTORY_H
#define PETKA_LAUNCHER_INVENTORY_H

#include "game.h"

// ---- the case ----
//
// The game's panel is a briefcase that covers most of the screen (the exe's
// object at QSystem+144; key I opens and shuts it). Its clips are all full
// frames, so they line up at 0,0; a button's place is its clip's MSK rect and
// its clip is its lit state. The things carried are shown six to a page, at
// fixed places, in front of the case and behind its strap.
#define CASE_BODY 6000

#define CASE_BUTTON0 6001 // + button: Chapayev, diskette, map, quit, page right, page left

#define CASE_STRAP 6007

#define CASE_EXIT 6009    // opaque where the pointer is outside the case

#define CASE_PAGE 6

extern const int kCaseCell[CASE_PAGE][2];

// EN: The thing at a place of the case's page.
// RU: Предмет в ячейке страницы чемодана.
const PetkaObject *case_item(Game *g, int k, Visual **out);
// EN: What the pointer is over in the case: a thing or a button.
// RU: Что под указателем в чемодане: предмет или кнопка.
const PetkaObject *case_hover(Game *g, int px, int py);
// EN: Puts the thing on the cursor back in the case.
// RU: Возвращает предмет с курсора в чемодан.
void drop_item(Game *g);
// EN: A click on the case's buttons.
// RU: Щелчок по кнопкам чемодана.
void case_click(Game *g);
// EN: The lid's pictures in grey, for buttons that do nothing.
// RU: Картинки на крышке серым для неработающих кнопок.
int case_lid_grey(Game *g, int all);
// EN: Draws the case, its buttons and things.
// RU: Рисует чемодан, его кнопки и предметы.
void draw_case(Game *g, float scale);

#endif
