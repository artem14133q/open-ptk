// scriptfix.h -- part of the launcher (launcher/src).
//
// EN: Fixes to the game's own script (SCRIPT.DAT): mistakes the original
// has and the exe plays as they are, put right as the script is loaded --
// a message added to a reaction -- and in a saved game made before, the
// object left in the mistake's state put right as it is loaded.
// RU: Исправления сценария игры (SCRIPT.DAT): ошибки оригинала, которые
// exe воспроизводит как есть, исправляются при загрузке сценария (в реакцию
// добавляется сообщение), а в сделанных раньше сохранениях объект,
// оставшийся в ошибочном состоянии, исправляется при их загрузке.
#ifndef PETKA_LAUNCHER_SCRIPTFIX_H
#define PETKA_LAUNCHER_SCRIPTFIX_H

#include "game.h"

// EN: After the part's script is loaded, before the VM is made: the reactions put right.
// RU: После загрузки сценария части, до создания машины: исправляет реакции.
void scriptfix_script(Game *g);
// EN: After a saved game is loaded: objects left in a mistake's state put right.
// RU: После загрузки сохранения: исправляет объекты, оставшиеся в ошибочном состоянии.
void scriptfix_state(Game *g);

#endif
