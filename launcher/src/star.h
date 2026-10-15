// star.h -- part of the launcher (launcher/src).
//
// EN: The star: the menu of actions, by the mouse or by a controller's stick, and doing an action.
// RU: Звезда: меню действий, мышью или стиком контроллера, и выполнение действия.
#ifndef PETKA_LAUNCHER_STAR_H
#define PETKA_LAUNCHER_STAR_H

#include "game.h"

// ---- the star ----
//
// The exe's object at QSystem+148 (sub_414C70; clip 5000, SUPRSTAR.FLC, z
// 999): a star of five actions round a briefcase. The right button on the
// main screen (sub_414AF0) puts it up with its middle at the pointer, kept
// on screen, or takes it down. Up, it takes every click: on one of its six
// rects (kept in the object, read from its constructor) it opens the case or
// sets the cursor's action and clip, and in any case it goes away. Its clip
// has a frame for each rect lit and one for none.
#define STAR_CLIP 5000

extern const Rectangle kStarPart[6];

// EN: Puts the star up at a point, or takes it down.
// RU: Открывает звезду в точке или закрывает её.
void star_toggle(Game *g, int px, int py);
// EN: Which part of the star holds a point (6 = none).
// RU: Какая часть звезды под точкой (6 - никакая).
int star_part(const Game *g, int px, int py);
// EN: The part of the star a controller's stick points at.
// RU: Часть звезды, на которую указывает стик.
int star_aim(int middle);
// EN: A click on the star: the case or an action for the cursor.
// RU: Щелчок по звезде: чемодан или действие для курсора.
void star_click(Game *g, int part);
// EN: Whether a pixel is the star's own red or yellow.
// RU: Является ли пиксель красным или жёлтым цветом самой звезды.
int star_body(const uint8_t *c);
// EN: Draws the star with its lit part.
// RU: Рисует звезду с подсвеченной частью.
void draw_star(Game *g, float scale);
// EN: A click with an action on an object, as the script hears it.
// RU: Щелчок действием по объекту - так, как его слышит сценарий.
void act_on(Game *g, const PetkaObject *obj, int verb, float mx, float my);
// EN: Does the star's lit part to the object it is up for (a controller).
// RU: Выполняет выбранное в звезде действие над объектом (контроллер).
void star_pick(Game *g);

#endif
