// restored.h -- part of the launcher (launcher/src).
//
// EN: Restored dialogues: lines of DIALOGUE.FIX the exe never says -- the
// object has its own dialogue for the action, but the script's reaction to
// it has no DIALOG -- that the Reloaded edition (Steam, 2016) put back. An
// extras switch; with it on, the line is said first and the action follows.
// RU: Восстановленные диалоги: реплики из DIALOGUE.FIX, которые exe никогда
// не произносит (у объекта есть свой диалог на действие, но в реакции
// сценария нет DIALOG), а переиздание Reloaded (Steam, 2016) вернуло.
// Переключатель в «Дополнительно»; когда включён, реплика звучит первой, а
// действие выполняется после неё.
#ifndef PETKA_LAUNCHER_RESTORED_H
#define PETKA_LAUNCHER_RESTORED_H

#include "game.h"

// EN: The argument of a DIALOG that scriptfix.c adds to a reaction for a restored line said after the action, not before it: said only with the extras switch on.
// RU: Аргумент DIALOG, который scriptfix.c добавляет в реакцию для восстановленной реплики, звучащей после действия, а не перед ним: звучит только при включённом переключателе.
#define RESTORED_LATE 0xFFFE
// A restored line said in two parts round the action (scriptfix.c puts a
// DIALOG for each): the object's dialogue up to restored_split()'s phrase,
// before it; the rest, after it. With the switch off neither is said.
// RU: Восстановленная реплика в двух частях вокруг действия: до него -- первые
// фразы диалога объекта, после -- остальные.
#define RESTORED_FIRST 0xFFFD
#define RESTORED_REST 0xFFFC
// Phrases of other dialogues a restored scene says where the script has
// none (restored_phrases(): restored.c's kPhrases), put in by scriptfix.c.
// RU: Фразы других диалогов, которые восстановленная сцена говорит там, где
// в сценарии ничего нет.
#define RESTORED_PHRASES 0xFFFB

// The phrases RESTORED_PHRASES says for an object (up to max); how many.
// RU: Фразы, которые RESTORED_PHRASES говорит для объекта; сколько их.
int restored_phrases(const Game *g, uint16_t obj_id, uint16_t *out, int max);

// The dialogue split round its action for an object, and how many of its
// phrases come first; NULL if it has none.
// RU: Диалог объекта, разделённый вокруг действия, и сколько фраз в первой части.
const PetkaDialogueVerb *restored_split(const Game *g, uint16_t obj_id, int *first);

// A dialogue the exe says for the player's action on an object with no
// reaction to it: the phrases of others Reloaded says after it (restored.c's
// kAfter2), set to follow it.
// RU: Диалог, который exe говорит на действие игрока с объектом без реакции:
// фразы других диалогов, которые Reloaded говорит после него.
void restored_after(Game *g, uint16_t obj_id);

// EN: Whether to say the object's own dialogue before this reaction (1), instead of it (2), or not (0) -- the VM's restore_dialogue callback.
// RU: Проиграть ли собственный диалог объекта перед этой реакцией (1), вместо неё (2) или нет (0) - обратный вызов машины restore_dialogue.
int host_restore_dialogue(void *user, uint16_t obj_id, uint32_t key, uint8_t status);
// The player did op to obj (a click, the star, a thing used on it): only a
// reaction to that gets its line back -- not one to a message another
// object's reaction passed on (the wardrobe's door and the mattress taken
// out of it both send the wardrobe USE).
// RU: Игрок сделал op с obj: только реакция на это действие получает
// восстановленную реплику, а не на сообщение, пересланное реакцией другого
// объекта.
void restored_player_action(Game *g, uint16_t obj, uint16_t op);

#endif
