// visual.h -- part of the launcher (launcher/src).
//
// EN: Clips on screen: FLIC frames to textures (only the part an object covers), drawing, the cursor, a clip's frame clock and hit test.
// RU: Клипы на экране: кадры FLIC в текстуры (только та часть, что занимает объект), отрисовка, курсор, часы кадров клипа и проверка попадания.
#ifndef PETKA_LAUNCHER_VISUAL_H
#define PETKA_LAUNCHER_VISUAL_H

#include "game.h"

// The pointer. The exe's cursor is an object with a clip (sub_402280): one
// per action, resources 5002.., or the clip a CURSOR message gave it when it
// carries an inventory item. Moving it (sub_402570) puts the middle of the
// clip's MSK rect on the mouse; drawing it (sub_4023D0) puts the frame's
// corner at the cursor's position. The clip only runs while the pointer is
// over an object, otherwise it is held on its first frame.
#define CURSOR_POINTER 5001

#define CURSOR_ACTION0 5002

#define CURSOR_INTERFACE 4901

// EN: The part of a FLIC frame a texture holds, in RGBA (keyed colour clear).
// RU: Часть кадра FLIC для текстуры в RGBA (цвет-ключ прозрачный).
const uint8_t *flic_to_rgba(const PetkaFlic *flic, PetkaMskRect area);
// EN: The part of a clip's frame its texture holds: its MSK rect and a pixel.
// RU: Часть кадра клипа в текстуре: прямоугольник MSK плюс пиксель.
PetkaMskRect clip_area(const PetkaFlic *flic, PetkaMskRect r);
// EN: Lets a clip go, its texture with it.
// RU: Освобождает клип вместе с текстурой.
void visual_free(Visual *v);
// EN: Loads the clip of a resource into a visual.
// RU: Загружает клип ресурса.
void visual_load(Game *g, Visual *v, uint16_t resource);
// EN: Loads a clip by its path in the stores.
// RU: Загружает клип по пути в архивах.
void visual_load_path(Game *g, Visual *v, const char *path);
// EN: Brings the texture up to the clip's current frame.
// RU: Обновляет текстуру до текущего кадра клипа.
void visual_upload(Visual *v);
// EN: Draws a clip's frame so the whole frame would cover a rect.
// RU: Рисует кадр клипа так, чтобы весь кадр покрыл прямоугольник.
void visual_draw(const Visual *v, Rectangle frame);
// EN: Draws part of a clip's frame in grey (shown, but doing nothing).
// RU: Рисует часть кадра клипа серым (видно, но не работает).
void draw_grey(const Visual *v, PetkaMskRect part, Texture2D *cache, Rectangle frame, int (*keep)(const uint8_t *rgba));
// EN: Brings a clip outside the script (cursor, case) to a resource; plays it or holds frame 1.
// RU: Ставит клип вне сценария (курсор, чемодан) на ресурс; проигрывает или держит 1-й кадр.
void clip_show(Game *g, Visual *v, uint16_t resource, int run);
// EN: Draws the pointer's clip at the pointer.
// RU: Рисует клип курсора у указателя.
void draw_cursor(Game *g, uint16_t resource, int over_object, int centre, Vector2 mouse, float scale);
// EN: Runs an object's clip on its clock: frames, sounds, END and HALF for the script.
// RU: Ведёт клип объекта по его часам: кадры, звуки, END и HALF для сценария.
void visual_update(Game *g, const PetkaObject *obj);
// EN: A scene object's hit test: in its MSK rect and opaque, with a pixel of slack.
// RU: Попадание по объекту: в прямоугольнике MSK и непрозрачно, с запасом в пиксель.
int clip_hit(const Visual *v, int x, int y);
// EN: The point of a clip nearest a point that a click hits.
// RU: Ближайшая к точке точка клипа, куда попадает щелчок.
Vector2 clip_point(const Visual *v, Vector2 p);
// EN: Puts an object's clip on a given frame (clips whose frames are states).
// RU: Ставит клип объекта на заданный кадр (клипы, где кадры - состояния).
void show_frame(const Game *g, const PetkaObject *obj, int frame);

#endif
