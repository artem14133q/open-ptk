// Films on a desktop: the ffmpeg program decodes them. The sound goes into
// a WAV file first, read back into memory; the frames come down a pipe.
#ifndef __SWITCH__

#include "film.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Film {
    FILE *frames;
    size_t frame_bytes;
    uint8_t *first; // the frame read to see that ffmpeg is there, not yet handed out
    unsigned char *sound;
    int sound_len;
};

// ffmpeg, by its full path where it can be found: a program started from the
// Finder does not have the shell's PATH. One put next to the launcher wins.
static const char *ffmpeg_program(void) {
    static char beside[1100];
    snprintf(beside, sizeof(beside), "%sffmpeg", GetApplicationDirectory());
    if (FileExists(beside) && !strpbrk(beside, "\"$`\\")) return beside;
    static const char *const places[] = {"/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"};
    for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++)
        if (FileExists(places[i])) return places[i];
    return "ffmpeg";
}

Film *film_open(const char *path, int width, int height, int fps, const char **why) {
    // The path goes into a shell command: nothing in it may end the quotes.
    if (strpbrk(path, "\"$`\\")) { *why = "неподходящий путь к игре"; return NULL; }
    Film *film = (Film *)calloc(1, sizeof(Film));
    if (!film) { *why = "нет памяти"; return NULL; }

    char command[2048], sound_file[512];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(sound_file, sizeof(sound_file), "%s/petka_film_%d.wav", tmp, (int)getpid());
    const char *ffmpeg = ffmpeg_program();
    // Quiet: a film without sound (adv.avi, credits.avi) fails here, and
    // is played without it, as the exe does.
    snprintf(command, sizeof(command), "\"%s\" -v quiet -y -i \"%s\" -vn -f wav \"%s\"", ffmpeg, path, sound_file);
    if (system(command) == 0 && FileExists(sound_file)) film->sound = LoadFileData(sound_file, &film->sound_len);
    remove(sound_file);

    // Fatal errors only: a film cut short closes the pipe, and ffmpeg would
    // complain of it ("Broken pipe"). A film it cannot read at all shows
    // as no first frame below.
    snprintf(command, sizeof(command), "\"%s\" -v fatal -i \"%s\" -an -vf fps=%d,scale=%d:%d -f rawvideo -pix_fmt rgba -",
             ffmpeg, path, fps, width, height);
    film->frames = popen(command, "r");
    film->frame_bytes = (size_t)width * height * 4;
    film->first = (uint8_t *)malloc(film->frame_bytes);
    if (!film->frames || !film->first || fread(film->first, 1, film->frame_bytes, film->frames) != film->frame_bytes) {
        film_close(film);
        *why = "нужен ffmpeg";
        return NULL;
    }
    return film;
}

int film_next(Film *film, uint8_t *rgba) {
    if (film->first) {
        memcpy(rgba, film->first, film->frame_bytes);
        free(film->first);
        film->first = NULL;
        return 1;
    }
    return fread(rgba, 1, film->frame_bytes, film->frames) == film->frame_bytes;
}

const uint8_t *film_sound(Film *film, size_t *len) {
    *len = (size_t)film->sound_len;
    return film->sound;
}

void film_close(Film *film) {
    if (!film) return;
    if (film->frames) pclose(film->frames);
    free(film->first);
    if (film->sound) UnloadFileData(film->sound);
    free(film);
}

#endif
