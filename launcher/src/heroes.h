// heroes.h -- part of the launcher (launcher/src).
//
// EN: Petka and Chapayev: perspective scale, walk clips and steps along the walk mesh, the camera.
// RU: Петька и Чапаев: масштаб по перспективе, клипы ходьбы и шаги по сетке ходьбы, камера.
#ifndef PETKA_LAUNCHER_HEROES_H
#define PETKA_LAUNCHER_HEROES_H

#include "game.h"

// EN: A hero's scale at a height on the screen (the room's perspective).
// RU: Масштаб героя на данной высоте экрана (перспектива комнаты).
float hero_scale(const Game *g, float y);
// EN: Reads the room's perspective from BGS.INI.
// RU: Читает перспективу комнаты из BGS.INI.
void load_perspective(Game *g, const char *room_name);
// EN: Reads NAMES.INI and CAST.INI: objects' names and speech colours.
// RU: Читает NAMES.INI и CAST.INI: имена объектов и цвета реплик.
void load_names(Game *g, const char *part);
// EN: Where a hero's frame goes on screen, scaled, by his feet.
// RU: Куда на экране ложится кадр героя, в масштабе, по ногам.
Rectangle hero_dest(const Game *g, uint16_t hero_id, const PetkaObjectState *st, const Visual *v);
// EN: The step tables of a set of walk clips.
// RU: Таблицы шагов для набора клипов ходьбы.
const PetkaGait *gait_for(Game *g, uint16_t costume);
// EN: A walking hero's scale at his point.
// RU: Масштаб идущего героя в его точке.
double walk_scale(void *user, double y);
// EN: Loads the room's walk mesh (.CVX).
// RU: Загружает сетку ходьбы комнаты (.CVX).
void load_mesh(Game *g);
// EN: Sets a hero walking to a point (the VM's walk callback).
// RU: Отправляет героя к точке (обратный вызов ходьбы для машины).
int host_walk(void *user, uint16_t hero_id, int32_t x, int32_t y);
// EN: Moves the view of a wide room towards Petka.
// RU: Сдвигает вид широкой комнаты к Петьке.
void camera_update(Game *g, float dt);
// EN: Stops both heroes where they are.
// RU: Останавливает обоих героев на месте.
void stop_heroes(Game *g);
// EN: Stops a hero (the VM's callback).
// RU: Останавливает героя (обратный вызов машины).
void host_stop(void *user, uint16_t hero_id);
// EN: Moves a walking hero one step a frame of his walk clip.
// RU: Двигает идущего героя на шаг за кадр клипа ходьбы.
void walk_frame(Game *g, uint16_t hero_id);
// EN: The name the player sees, "" for none.
// RU: Имя, которое видит игрок, "" если нет.
const char *display_name(const Game *g, const PetkaObject *obj);

#endif
