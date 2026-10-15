// audio.h -- part of the launcher (launcher/src).
//
// EN: Sound: the panel's dials, clip sounds, music and ambience streams, sounds held during sequences.
// RU: Звук: регуляторы панели, звуки клипов, потоки музыки и фона, звуки на время заставок.
#ifndef PETKA_LAUNCHER_AUDIO_H
#define PETKA_LAUNCHER_AUDIO_H

#include "game.h"

// The dials' ranges and what the exe makes of them (sub_41C5A0): a
// percentage, (value - 1) * 100 / (top - 1). Their first values are the
// defaults the exe reads its profile with.
enum { DIAL_SPEECH, DIAL_MUSIC, DIAL_SFX, DIAL_SPEED };

extern const int kDialTop[4];
extern const int kDialDefault[4];

// EN: A dial of the panel as a percentage.
// RU: Регулятор панели в процентах.
int dial_percent(const Game *g, int which);
// EN: A volume dial as a gain for raylib.
// RU: Регулятор громкости как множитель для raylib.
float dial_gain(const Game *g, int which);
// EN: Plays a clip's sound (one buffer per file, restarted).
// RU: Проигрывает звук клипа (один буфер на файл, с перезапуском).
void play_sound(Game *g, const char *path);
// EN: A WAV made fit for raylib's streams (8-bit widened to 16).
// RU: WAV, пригодный для потоков raylib (8 бит расширяются до 16).
const uint8_t *wav_for_stream(const uint8_t *bytes, size_t len, size_t *out_len);
// EN: Brings a music or ambience stream to a resource, or stops it.
// RU: Переключает поток музыки или фона на ресурс или останавливает его.
void set_stream(Game *g, Music *stream, uint16_t *current, uint16_t id);
// EN: Pauses the room's sounds for a sequence, or brings them back.
// RU: Приостанавливает звуки комнаты на время заставки или возвращает их.
void sequence_sounds(Game *g, int entering);

#endif
