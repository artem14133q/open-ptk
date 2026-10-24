// phrasefix.h -- part of the launcher (launcher/src).
//
// EN: The second game's corrected lines: the texts of DIALOGUE.LOD that the
// Reloaded edition (Steam, 2016) put right -- typos, and the words as the
// voice says them. Each is taken by its part and WAV, and only where the
// text is still the original's.
// RU: Поправленные реплики второй игры: тексты DIALOGUE.LOD, которые
// переиздание Reloaded (Steam, 2016) исправило, -- опечатки и слова так, как
// их произносит озвучка. Каждая находится по части и WAV и заменяется, только
// если текст тот же, что в оригинале.
#ifndef PETKA_LAUNCHER_PHRASEFIX_H
#define PETKA_LAUNCHER_PHRASEFIX_H

#include "game.h"

// EN: Puts the corrected texts into the part's phrase table (g->phrases).
// RU: Подставляет поправленные тексты в таблицу фраз части (g->phrases).
void phrasefix_apply(Game *g);

#endif
