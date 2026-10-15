// A film's pictures and sound, for play_film() in film_play.c. There are two
// sources: on a desktop the ffmpeg program, run as a process of its own
// (film_pipe.c); on the Switch, which runs no other programs, FFmpeg's
// libraries inside the launcher (film_av.c).
//
// RU: Источник кадров и звука ролика: на компьютере программа ffmpeg, на
// Switch - библиотеки FFmpeg внутри лаунчера.
#ifndef PETKA_FILM_H
#define PETKA_FILM_H

#include <stddef.h>
#include <stdint.h>

typedef struct Film Film;

// Opens the film at `path`, its frames to come `fps` a second, scaled to
// `width` x `height`. NULL if it cannot be played; `why` then says why, for
// the status line.
// RU: Открывает ролик; NULL и причина, если показать нельзя.
Film *film_open(const char *path, int width, int height, int fps, const char **why);

// The next frame, in RGBA, into `rgba` (width * height * 4 bytes); 0 once
// the film is over.
// RU: Следующий кадр в RGBA; 0 когда ролик кончился.
int film_next(Film *film, uint8_t *rgba);

// The film's sound as a WAV file in memory, NULL if it has none. It stays
// until film_close().
// RU: Звук ролика как WAV в памяти, NULL если нет.
const uint8_t *film_sound(Film *film, size_t *len);

// EN: Closes the film.
// RU: Закрывает ролик.
void film_close(Film *film);

#endif
