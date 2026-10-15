// play.h -- part of the launcher (launcher/src).
//
// EN: dev: a scripted play (PETKA_PLAY) that plays the game from a list of steps.
// RU: Для разработки: прохождение по сценарию (PETKA_PLAY) из списка шагов.
#ifndef PETKA_LAUNCHER_PLAY_H
#define PETKA_LAUNCHER_PLAY_H

#include "game.h"

// ---- dev: a scripted play ----
//
// PETKA_PLAY=<file> plays the game from a list of steps, one a line, each
// taken when the game has come to rest after the one before: what a player
// would do with the mouse, said by object id.
//
//   look|walk|use|take|talk|vich <id>   the cursor's action on an object
//                                       (vich: "send Chapayev")
//   item <item id> <id>                 a thing from the case on an object
//   go <x> <y>                          "walk" on the bare floor at a point
//   near <x> <y>                        check that Petka stands about there
//   map <id>                            open the map and pick a place
//   pick <n> [<n>...]                   the lines to answer the coming
//                                       menus with, from 0
//   room <id> / part <n>                check which room, which part this is
//   has <id> / hasnot <id>              check the case
//   save <n>                            write save<n>.dat
//   shot <file.png>                     save a screenshot
//   wait <frames>
//   # comment
//
// A step that cannot be taken, a menu with no line named for it, or a game
// that does not come to rest ends the run with a report and a screenshot
// (play-fail.png) and exit code 3; at the end of the list it leaves with 0.
#define PLAY_REST 30       // frames at rest before the next step

#define PLAY_PATIENCE 60000 // frames a step may take before the run gives up

typedef struct {
    char verb[12];
    int a, b;
    int picks[16], pick_count;
    char text[160];
    int line;
} PlayStep;

// EN: The play's state: its steps and how far it has gone.
// RU: Состояние прохождения: шаги и докуда дошли.
typedef struct {
    PlayStep *steps;
    int count, at;
    int picks[64], pick_count, pick_at;
    long rest, waited, hold;
    int phase;       // a step in two parts: the map is up, the place not picked yet
    char shot[256];  // a screenshot to save once this frame is on the screen
    int shot_wait;
    int over, failed;
} PlayState;
extern PlayState s_play;

// EN: Reads a play's list of steps.
// RU: Читает список шагов прохождения.
void play_load(const char *file);
// EN: Ends a play that cannot go on: a report, a screenshot, exit code 3.
// RU: Завершает прохождение, которое не может продолжиться: отчёт, скриншот, код 3.
void play_fail(Game *g, const char *why);
// EN: Whether a thing is in the case.
// RU: Есть ли предмет в чемодане.
int play_has(const Game *g, int id);
// EN: The object a step names, by id or name.
// RU: Объект шага по номеру или имени.
const PetkaObject *play_target(Game *g, int id, char *why, size_t why_sz);
// EN: Takes the play's next step when the game is at rest.
// RU: Делает следующий шаг, когда игра в покое.
void play_tick(Game *g, int can_act);

#endif
