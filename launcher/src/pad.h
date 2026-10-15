// pad.h -- part of the launcher (launcher/src).
//
// EN: A controller's help: the pointer drawn to things nearby, the case walked as a grid.
// RU: Помощь контроллеру: притяжение указателя к предметам, обход чемодана по сетке.
#ifndef PETKA_LAUNCHER_PAD_H
#define PETKA_LAUNCHER_PAD_H

#include "game.h"

// With a controller small things are hard to stop on, so the pointer is
// drawn to them: when the stick is about at rest, the pointer on nothing
// but within AIM_REACH of something that can be clicked (not a hero), it
// glides a share of the way a frame to that thing's nearest point -- found
// on rings round the pointer, nearest first -- and over something the
// stick moves it slower. Room pixels in, the pointer's new place out.
#define AIM_REACH 18

// With a controller: the case's pointer on one of the things of the page
// (places 0..5) or, in the case opened as usual, one of the lid's buttons
// that work -- the diskette and the map (places 7, 8; Chapayev's, 6, is
// sent with R) -- moved to
// the nearest one in the direction pushed (dx, dy), or a page turned.
// Returns where the pointer goes, in game pixels.
#define CASE_PLACES (CASE_PAGE + 3)

// EN: Draws a controller's pointer to things nearby; its new place.
// RU: Притягивает указатель контроллера к ближайшим предметам; новое место.
Vector2 aim_assist(Game *g, const Drawable *scene, int n, Vector2 at);
// EN: Moves a controller's pointer in the case as a grid; where it goes.
// RU: Двигает указатель контроллера по чемодану как по сетке; куда он встаёт.
Vector2 case_steer(Game *g, int dx, int dy, int page);

#endif
