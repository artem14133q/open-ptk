// highlight.h -- part of the launcher (launcher/src).
//
// EN: The yellow outline round the thing under the pointer, if it can be
// clicked: the edge of the clip's frame -- grown a little, its notches and
// holes filled -- is followed into loops, their steps are taken out, and
// their corners are rounded (Chaikin), and they are drawn as closed bands, a
// soft glow under a thin line. Made again only when the frame changes.
// RU: Жёлтая обводка вокруг предмета под указателем, если с ним можно
// взаимодействовать: край кадра клипа (немного расширенный, с заполненными
// впадинами и дырками) обходится в замкнутые контуры, ступеньки убираются, и
// углы скругляются (Чайкин), и контуры рисуются замкнутыми лентами: мягкое
// свечение под тонкой линией. Пересчитывается только при смене кадра.
#ifndef PETKA_LAUNCHER_HIGHLIGHT_H
#define PETKA_LAUNCHER_HIGHLIGHT_H

#include "game.h"

// EN: After the window (nothing to load now; kept for the start-up order).
// RU: После создания окна (сейчас загружать нечего).
void highlight_init(void);
// EN: Frees the outline last made.
// RU: Освобождает последний построенный контур.
void highlight_close(void);
// EN: Draws the outline of a clip's frame placed as visual_draw() would put it.
// RU: Рисует обводку кадра клипа там, куда его поставил бы visual_draw().
void highlight_draw(const Visual *v, Rectangle frame);
// EN: Whether an object gets the outline: clickable, drawn or painted on the background, not a hero (Petka, Chapayev), not a way out.
// RU: Обводить ли объект: на него можно нажать, он нарисован клипом или на фоне, это не герой (Петька, Чапаев) и не выход.
int highlight_wanted(const Game *g, const PetkaObject *obj);

#endif
