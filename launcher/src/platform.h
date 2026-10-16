// What differs between a desktop window and the Nintendo Switch, behind a few
// calls the launcher makes instead of raylib's own.
//
// The game is drawn on a surface of plat_width() x plat_height() pixels: the
// window itself on a desktop; on the Switch a 960x720 picture (640x480 at one
// and a half) shown in the middle of the 1280x720 screen. Input is the mouse
// and the keyboard on a desktop; on the Switch the touch screen and the
// controller drive a pointer and stand in for the keys the game uses.
//
// RU: Чем отличаются окно компьютера и Nintendo Switch: поверхность для
// рисования, ввод (мышь и клавиатура или сенсор и контроллер), папка игры,
// системный шрифт.
#ifndef PETKA_PLATFORM_H
#define PETKA_PLATFORM_H

#include "raylib.h"

#include <stdbool.h>

// For the rest of this frame the game hears no input: every call below
// that reads a key, a button or a stick says nothing (the pointer still
// moves). For a menu over the game that has had its input first.
// RU: До конца кадра игра не получает ввода (для меню поверх игры).
void plat_mute(void);

// After InitWindow: makes the Switch's drawing surface.
// RU: После создания окна: поверхность для рисования, шейдер, журнал на Switch.
void plat_init(void);
// Before CloseWindow.
// RU: Перед закрытием окна.
void plat_close(void);

// EN: Starts a frame (drawing on the surface).
// RU: Начало кадра (рисование на поверхность).
void plat_begin(void);
// EN: Ends a frame: the surface put on the screen.
// RU: Конец кадра: поверхность на экран.
void plat_end(void);

// EN: The surface's width in pixels.
// RU: Ширина поверхности в пикселях.
int plat_width(void);
// EN: The surface's height in pixels.
// RU: Высота поверхности в пикселях.
int plat_height(void);

// Once a frame, before any of the input calls below: reads the touch screen
// and the controller (nothing to do on a desktop).
// RU: Раз за кадр, до остального ввода: читает сенсор и контроллер.
void plat_poll(void);

// EN: The pointer, in surface pixels.
// RU: Указатель в пикселях поверхности.
Vector2 plat_pointer(void);
// EN: The left button, a tap, A.
// RU: Левая кнопка, касание, A.
bool plat_click(void);
// EN: The right button, B.
// RU: Правая кнопка, B.
bool plat_rclick(void);
// EN: A key pressed, or the controller's stand-in for it.
// RU: Нажатие клавиши или её замены на контроллере.
bool plat_key(int key);
// EN: A key held down.
// RU: Клавиша удерживается.
bool plat_key_down(int key);
// EN: Any key or button at all (to end a film).
// RU: Нажата любая клавиша или кнопка (чтобы прервать ролик).
bool plat_any_key(void);
// The controller's R held down: the cursor sends Chapayev while it is
// (always false on a desktop, which has a key for that action).
// RU: Зажата R: курсор посылает Чапаева.
bool plat_chapayev_held(void);
// The controller's L held down: the cursor walks while it is (always false
// on a desktop).
// RU: Зажата L: курсор идёт.
bool plat_walk_held(void);

// Picking from a list with the controller (the Switch): the pointer is
// put on an entry, held there -- the stick and the D-pad stop moving it,
// a touch still does -- and the controller's up and down step through the
// list. On a desktop the mouse does it all: plat_pad() is false and the
// rest do nothing.
// RU: Управляется ли игра контроллером (Switch).
bool plat_pad(void);
// EN: Puts the pointer at a point.
// RU: Ставит указатель в точку.
void plat_set_pointer(Vector2 at);
// EN: Holds the pointer: the stick and the D-pad leave it be.
// RU: Держит указатель: стик и крестовина его не двигают.
void plat_hold_pointer(bool hold);
// EN: A step through a list: -1 up, +1 down, 0 neither.
// RU: Шаг по списку вверх/вниз: -1, +1 или 0.
int plat_list_step(void);
// EN: The same across: -1 left, +1 right.
// RU: Шаг по списку влево/вправо: -1, +1 или 0.
int plat_list_step_x(void);
// L and R pressed: -1 or +1 to turn a page of the case, 0 otherwise.
// RU: L/R: листать страницы чемодана.
int plat_page_step(void);
// Where the left stick is pushed, x right and y down, each -1..1; zero on a
// desktop.
// RU: Куда отклонён левый стик (x вправо, y вниз).
Vector2 plat_aim(void);
// How fast the stick moves the pointer from the next frame on: 1 as it is,
// less over something to be clicked, to stop on it more easily.
// RU: Скорость указателя от стика (меньше над предметами).
void plat_pointer_speed(float k);

// The folder the game's files are in when none is given: next to the
// program on a desktop; on the Switch the title's own romfs when the game is
// inside it (make nsp-full), else sdmc:/switch/petka/.
// RU: Папка игры по умолчанию: рядом с программой; на Switch -- romfs
// приложения, если игра внутри него (make nsp-full), иначе sdmc:/switch/petka/.
const char *plat_default_game_dir(void);
// Where the saved games, the settings and the like are written when no
// folder is given: the game's folder on a desktop, sdmc:/switch/petka/ on
// the Switch (made if it is not there) -- a romfs is read-only.
// RU: Куда пишутся сохранения и настройки: на Switch всегда
// sdmc:/switch/petka/ (romfs только для чтения).
const char *plat_data_dir(void);

// A font with Cyrillic in it from the system, or a zero Font. On the Switch
// this is the console's shared font.
// RU: Системный шрифт с кириллицей или пустой.
Font plat_system_font(int size, int *codepoints, int count);

#endif
