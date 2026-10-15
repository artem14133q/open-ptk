// highlight.h -- part of the launcher (launcher/src).
//
// EN: The yellow outline round the things on screen that can be clicked:
// a shader draws a clip's frame again, colouring only the clear pixels next
// to its opaque ones, so the outline follows the thing's own shape.
// RU: Жёлтая обводка вокруг предметов на экране, с которыми можно
// взаимодействовать: шейдер ещё раз рисует кадр клипа и закрашивает только
// прозрачные пиксели рядом с непрозрачными, так что обводка повторяет форму.
#ifndef PETKA_LAUNCHER_HIGHLIGHT_H
#define PETKA_LAUNCHER_HIGHLIGHT_H

#include "game.h"

// EN: After the window: loads the outline shader.
// RU: После создания окна: загружает шейдер обводки.
void highlight_init(void);
// EN: Frees the outline shader.
// RU: Освобождает шейдер обводки.
void highlight_close(void);
// EN: Draws the outline of a clip's frame placed as visual_draw() would put it.
// RU: Рисует обводку кадра клипа там, куда его поставил бы visual_draw().
void highlight_draw(const Visual *v, Rectangle frame);
// EN: Whether an object gets the outline: seen, clickable, not a hero.
// RU: Обводить ли объект: виден, на него можно нажать, не герой.
int highlight_wanted(const Game *g, const PetkaObject *obj);

#endif
