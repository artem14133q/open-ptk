// host.h -- part of the launcher (launcher/src).
//
// EN: The script VM's host: what the launcher does for the script's messages and room changes.
// RU: Хост машины скрипта: что лаунчер делает по сообщениям скрипта и при смене комнаты.
#ifndef PETKA_LAUNCHER_HOST_H
#define PETKA_LAUNCHER_HOST_H

#include "game.h"

// EN: The room has changed: perspective, mesh, camera, sounds (the VM's callback).
// RU: Сменилась комната: перспектива, сетка, камера, звуки (обратный вызов машины).
void host_room_changed(void *user, uint16_t prev_id, uint16_t room_id);
// EN: Does what a script message asks of the screen (the VM's callback).
// RU: Выполняет то, что сообщение сценария требует от экрана (обратный вызов машины).
void host_message(void *user, PetkaScriptVM *vm, const PetkaObject *obj, const PetkaQueued *q, int reacted);

#endif
