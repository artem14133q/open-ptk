// conversation.h -- part of the launcher (launcher/src).
//
// EN: Dialogues: picking one, phrases with voice and subtitles, the menu of lines, descriptions.
// RU: Диалоги: выбор диалога, фразы с голосом и субтитрами, меню реплик, описания предметов.
#ifndef PETKA_LAUNCHER_CONVERSATION_H
#define PETKA_LAUNCHER_CONVERSATION_H

#include "game.h"

// A thing's description, as the exe's description object draws it
// (sub_421370): the description clip (resource 6008) on the frame the
// message names -- a board with the thing's picture -- and the text from
// INVNTR.TXT in black, broken into lines, in the rect 160,275 - 598,376.
// The object takes every point of the screen, and a click on it shuts it.
#define DESCRIPTION_CLIP 6008

// The dialogue's menu, as the exe's menu object lays it out (sub_420020):
// the lines one under another in the 20-pixel font, each broken at 625
// pixels, starting 5 from the left edge, the whole block resting on row 479
// like any other text over the picture -- white, the line under the pointer
// in light blue, and nothing behind them. Fills `rects` (window pixels) with each
// line's place and, if asked, draws them.
#define MENU_MAX 16

// EN: The dialogue an object has for a verb and sender.
// RU: Диалог объекта для действия и отправителя.
const PetkaDialogueVerb *find_dialogue(const Game *g, uint16_t obj_id, uint32_t key);
// EN: Whether an object has a dialogue (the VM's callback).
// RU: Есть ли у объекта диалог (обратный вызов машины).
int host_has_dialogue(void *user, uint16_t obj_id, uint32_t key);
// EN: Picks the dialogue to play (the VM's callback).
// RU: Выбирает диалог для проигрывания (обратный вызов машины).
void host_select_dialogue(void *user, uint16_t obj_id, uint32_t key);
// EN: Makes an object the one speaking: its talk clip.
// RU: Делает объект говорящим: его клип разговора.
void set_talker(Game *g, uint16_t id);
// EN: Where a speaker's voice sits between left and right.
// RU: Положение голоса говорящего между левым и правым каналом.
float speaker_pan(const Game *g, uint16_t id);
// EN: Goes on to the dialogue's next phrase, menu or end.
// RU: Переходит к следующей фразе диалога, меню или концу.
void next_phrase(Game *g);
// EN: Starts the picked dialogue (the VM's callback).
// RU: Запускает выбранный диалог (обратный вызов машины).
void host_start_dialogue(void *user, uint16_t obj_id, uint16_t arg);
// EN: Draws a thing's description over the game.
// RU: Рисует описание предмета поверх игры.
void draw_description(Game *g, Font font, float scale);
// EN: Lays out (and draws) the menu of lines to say.
// RU: Раскладывает (и рисует) меню реплик.
void menu_lines(const Game *g, Font font, float scale, Rectangle *rects, int hover, int draw);

#endif
