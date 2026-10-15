// film_play.c
//
// EN: Films (.AVI): found next to the game and played full screen over a film source (film.h).
// RU: Видеоролики (.AVI): поиск рядом с игрой и показ на весь экран через источник роликов (film.h).
#include "game.h"

int find_film(const Game *g, const char *name, char *out, size_t out_sz) {
    static const char *dirs[] = {"", "data1_out/Main/", "Main/"};
    char lower[256];
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(lower); n++)
        lower[n] = name[n] >= 'A' && name[n] <= 'Z' ? (char)(name[n] + 32) : name[n];
    lower[n] = 0;
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        const char *tries[2] = {name, lower};
        for (int t = 0; t < 2; t++) {
            snprintf(out, out_sz, "%s/%s%s", g->game_dir, dirs[d], tries[t]);
            if (FileExists(out)) return 1;
        }
    }
    return 0;
}

void play_film(Game *g, const char *name) {
    // dev: PETKA_FILM=<seconds>:<file.png> plays films in unattended runs
    // too, saves the frame reached after that long and stops there.
    double shot_at = 0;
    char shot_file[512] = "";
    const char *dev = getenv("PETKA_FILM");
    if (dev) sscanf(dev, "%lf:%511s", &shot_at, shot_file);
    char path[1280];
    if (!name || !find_film(g, name, path, sizeof(path))) {
        set_status(g, TextFormat("(видео %s не найдено)", name ? name : "?"));
        return;
    }
    if ((g->fast || getenv("PETKA_SHOT")) && !dev) {
        set_status(g, TextFormat("(видео %s пропущено)", name));
        return;
    }
    const char *why = "";
    Film *film = film_open(path, GAME_W, GAME_H, FILM_FPS, &why);
    size_t frame_bytes = (size_t)GAME_W * GAME_H * 4;
    uint8_t *pixels = film ? (uint8_t *)malloc(frame_bytes) : NULL;
    if (!pixels || !film_next(film, pixels)) {
        film_close(film);
        free(pixels);
        set_status(g, TextFormat("(видео %s: %s, пропущено)", name, film ? "нет кадров" : why));
        return;
    }

    Music *tracks[4] = {&g->music, &g->ambience, &g->seq_music, &g->seq_ambience};
    for (int i = 0; i < 4; i++)
        if (tracks[i]->frameCount > 0) PauseMusicStream(*tracks[i]);
    Music sound = {0};
    size_t sound_len = 0;
    const uint8_t *sound_data = film_sound(film, &sound_len);
    if (sound_data) {
        sound = LoadMusicStreamFromMemory(".wav", sound_data, (int)sound_len);
        sound.looping = false;
        if (sound.frameCount > 0) {
            SetMusicVolume(sound, dial_gain(g, DIAL_SPEECH));
            PlayMusicStream(sound);
        }
    }
    Image first = {.data = pixels, .width = GAME_W, .height = GAME_H, .mipmaps = 1,
                   .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D screen = LoadTextureFromImage(first);
    HideCursor();

    double start = GetTime();
    long shown = 0;
    int ended = 0, polled = 0;
    while (!ended) {
        if (WindowShouldClose()) { g->quit = 1; break; }
        plat_poll();
        // Any key or either mouse button ends it, as in the original --
        // once a frame of it has been shown: before that the input is still
        // the frame's that started it, and the click on "credits" in the
        // menu, or on the thing whose film it is, ended it at once.
        if (polled && plat_any_key()) break;
        if (sound.frameCount > 0) UpdateMusicStream(sound);
        long due = (long)((GetTime() - start) * FILM_FPS);
        while (shown < due && !ended) {
            if (!film_next(film, pixels)) ended = 1;
            shown++;
        }
        if (!ended) UpdateTexture(screen, pixels);
        plat_begin();
        ClearBackground(BLACK);
        DrawTexturePro(screen, (Rectangle){0, 0, GAME_W, GAME_H},
                       (Rectangle){0, 0, (float)plat_width(), (float)plat_height()}, (Vector2){0, 0}, 0, WHITE);
        plat_end();
        polled = 1; // the input is read afresh at the end of a frame
        if (dev && GetTime() - start >= shot_at) {
            if (shot_file[0]) ExportImage(first, shot_file);
            fprintf(stderr, "film: %s, %ld frames in %.1f s\n", name, shown, GetTime() - start);
            break;
        }
    }

    UnloadTexture(screen);
    if (sound.frameCount > 0) { StopMusicStream(sound); UnloadMusicStream(sound); }
    film_close(film);
    free(pixels);
    // Back to what each was doing: a sequence may have had some paused.
    for (int i = 0; i < 4; i++) {
        int held = i < 2 ? g->tracks_hushed : g->seq_tracks_paused;
        if (tracks[i]->frameCount > 0 && !held) ResumeMusicStream(*tracks[i]);
    }
}
