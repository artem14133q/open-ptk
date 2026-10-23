// scene.h -- part of the launcher (launcher/src).
//
// EN: The screen's objects: their draw order (the exe's z sort) and what lies under a point.
// RU: Объекты экрана: порядок отрисовки (сортировка по z как в exe) и что находится под точкой.
#ifndef PETKA_LAUNCHER_SCENE_H
#define PETKA_LAUNCHER_SCENE_H

#include "game.h"

// ---- scene ----

typedef struct {
    const PetkaObject *obj;
    int z;
} Drawable;

// EN: Sorts the screen's objects by z as the exe does (kept from frame to frame).
// RU: Сортирует объекты экрана по z как exe (порядок хранится между кадрами).
void z_order_sort(Game *g);
// EN: Collects what is on screen, back to front.
// RU: Собирает то, что на экране, от дальнего к ближнему.
int build_scene(Game *g, Drawable *out, int max);
// EN: Where an object's frame goes on screen.
// RU: Куда на экране ложится кадр объекта.
Rectangle visual_dest(const Game *g, const PetkaObject *obj);
// EN: The topmost active object under a point of the room; an exit under a
// hero comes before him while the cursor walks.
// RU: Верхний активный объект под точкой комнаты; выход под героем -- раньше
// героя, пока курсор "идти".
const PetkaObject *object_at(Game *g, const Drawable *scene, int n, float x, float y);
// EN: A way out: its WALK reaction goes to another room (GOTO) or the map (TOMAP).
// RU: Выход: его реакция на WALK ведёт в другую комнату (GOTO) или на карту (TOMAP).
int object_is_exit(const PetkaObject *obj);
// EN: The object under a point on the map or the panel.
// RU: Объект под точкой на карте или панели.
const PetkaObject *listed_object_at(Game *g, float x, float y);

#endif
