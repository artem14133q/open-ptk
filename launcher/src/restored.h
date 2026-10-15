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
