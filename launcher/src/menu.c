// menu.c
//
// EN: The start-up menu.
// RU: Стартовое меню.
#include "game.h"

// Returns 0 if the window was closed or "exit" picked.
// The start-up screen: the game's main menu. In the exe it is a screen of
// its own (QSystem+556) showing the room STARTUP of the menus' script, PART_0
// -- a picture, a tune and five buttons whose clips are hidden until the
// pointer is on them, as on the map. A click goes by the button (0x41EAB0).
// Returns 0 to leave the program, 1 for a new game, 2 for "load".
int run_menu(Game *g, Font font) {
    (void)font;
    enum { MENU_NONE, MENU_NEW, MENU_LOAD, MENU_CREDITS, MENU_EXIT };
    // In the order the room lists them; the pointer is tried on the last first.
    static const struct { const char *clip; int action; } kButtons[5] = {
        {"MAIN\\INTRFACE.BG\\SNEWGAME.FLC", MENU_NEW},
        {"MAIN\\INTRFACE.BG\\SLOADGEM.FLC", MENU_LOAD},
        {"MAIN\\INTRFACE.BG\\SUNINSTL.FLC", MENU_NONE},
        {"MAIN\\INTRFACE.BG\\S-TITLE.FLC", MENU_CREDITS},
        {"MAIN\\INTRFACE.BG\\S-EXIT.FLC", MENU_EXIT},
    };
    Texture2D bg = load_bmp_texture(g, "MAIN\\INTRFACE.BG\\INSTHERO.BMP");
    Visual buttons[5];
    memset(buttons, 0, sizeof(buttons));
    for (int i = 0; i < 5; i++) {
        buttons[i].resource = PETKA_NONE;
        visual_load_path(g, &buttons[i], kButtons[i].clip);
    }
    Music tune = {0};
    size_t tune_len = 0;
    const uint8_t *tune_bytes = res_find(g, "MAIN\\INTRFACE.BG\\start.wav", &tune_len);
    if (tune_bytes) {
        tune = LoadMusicStreamFromMemory(".wav", tune_bytes, (int)tune_len);
        tune.looping = true;
        if (tune.frameCount > 0) PlayMusicStream(tune);
    }
    // dev: PETKA_MENU=<file.png>:<x>,<y> shows the menu with the pointer
    // there, saves a picture of it and goes on to a new game.
    const char *dev = getenv("PETKA_MENU");
    char shot[512] = "";
    float pin_x = -1, pin_y = -1;
    if (dev) sscanf(dev, "%511[^:]:%f,%f", shot, &pin_x, &pin_y);

    int result = -1;
    long frames = 0;
    while (!WindowShouldClose() && result < 0) {
        float scale = (float)plat_width() / GAME_W;
        plat_poll();
        Vector2 mouse = plat_pointer();
        if (pin_x >= 0) mouse = (Vector2){pin_x * scale, pin_y * scale};
        int px = (int)(mouse.x / scale), py = (int)(mouse.y / scale), over = -1;
        for (int i = 4; i >= 0 && over < 0; i--)
            if (buttons[i].flic && clip_hit(&buttons[i], px, py)) over = i;
        if (tune.frameCount > 0) {
            SetMusicVolume(tune, dial_gain(g, DIAL_MUSIC));
            UpdateMusicStream(tune);
        }
        if (plat_click() && over >= 0) {
            switch (kButtons[over].action) {
            case MENU_NEW: result = 1; break;
            case MENU_LOAD: result = 2; break;
            case MENU_EXIT: result = 0; break;
            case MENU_CREDITS:
                // The film, and then the tune from its beginning again.
                if (tune.frameCount > 0) StopMusicStream(tune);
                play_film(g, "credits.avi");
                if (g->quit) result = 0;
                if (tune.frameCount > 0) PlayMusicStream(tune);
                break;
            default: break;
            }
        }
        plat_begin();
        ClearBackground(BLACK);
        Rectangle whole = {0, 0, GAME_W * scale, GAME_H * scale};
        if (bg.id) DrawTexturePro(bg, (Rectangle){0, 0, (float)bg.width, (float)bg.height}, whole, (Vector2){0, 0}, 0, WHITE);
        if (over >= 0)
            visual_draw(&buttons[over], whole);
        HideCursor();
        draw_cursor(g, CURSOR_INTERFACE, over >= 0, 0, mouse, scale);
        plat_end();
        if (dev && ++frames == 30) {
            Image img = LoadImageFromScreen();
            if (shot[0]) ExportImage(img, shot);
            UnloadImage(img);
            fprintf(stderr, "menu: pointer over button %d\n", over);
            result = getenv("PETKA_MENU_LOAD") ? 2 : 1; // dev: ... or on to "load"
        }
    }
    for (int i = 0; i < 5; i++) visual_free(&buttons[i]);
    if (tune.frameCount > 0) { StopMusicStream(tune); UnloadMusicStream(tune); }
    if (bg.id) UnloadTexture(bg);
    return result < 0 ? 0 : result;
}
