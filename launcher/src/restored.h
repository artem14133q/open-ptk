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

// EN: Whether to say the object's own dialogue before this reaction (the VM's restore_dialogue callback).
// RU: Проиграть ли собственный диалог объекта перед этой реакцией (обратный вызов машины restore_dialogue).
int host_restore_dialogue(void *user, uint16_t obj_id, uint32_t key, uint8_t status);

#endif
