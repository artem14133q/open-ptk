// extras.c
//
// EN: The extras menu (Tab, or - on the Switch): what this launcher adds, switched on and off.
// RU: Меню «Дополнительно» (Tab, на Switch - минус): что добавляет лаунчер, вкл/выкл.
#include "game.h"
const char *const kExtraName[EXTRA_COUNT] = {
    "Вид ЭЛТ-монитора", "Тёмная подложка под текстом", "Подсветка предметов", "Восстановленные диалоги",
    "Притяжение указателя к предметам",
    "Отладочный экран",
};

// The rows shown: the pull is a controller's only.
int extras_rows(int *rows) {
    int n = 0;
    for (int i = 0; i < EXTRA_COUNT; i++)
        if (i != EXTRA_AIM || plat_pad()) rows[n++] = i;
    return n;
}

int extra_value(const Game *g, int i, int debug) {
    switch (i) {
    case EXTRA_CRT: return crt_on();
    case EXTRA_BAND: return s_band;
    case EXTRA_AIM: return g->opt_aim;
    case EXTRA_HIGHLIGHT: return g->opt_highlight;
    case EXTRA_RESTORED: return g->opt_restored;
    default: return debug;
    }
}

Rectangle extras_row_rect(int k, float scale) {
    return (Rectangle){EXTRAS_X * scale, (EXTRAS_Y + 44 + k * EXTRAS_ROW) * scale, EXTRAS_W * scale,
                       EXTRAS_ROW * scale};
}

// The settings, not the debug overlay, which is a developer's.
void extras_save(const Game *g) {
    FILE *f = fopen(EXTRAS_FILE, "w");
    if (!f) return;
    fprintf(f, "crt=%d\nband=%d\naim=%d\nhighlight=%d\nrestored=%d\n", crt_on() ? 1 : 0, s_band, g->opt_aim,
            g->opt_highlight, g->opt_restored);
    fclose(f);
}

void extras_load(Game *g) {
    FILE *f = fopen(EXTRAS_FILE, "r");
    if (!f) return;
    char line[64];
    int value;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "crt=%d", &value) == 1) crt_set(value != 0);
        else if (sscanf(line, "band=%d", &value) == 1) s_band = value != 0;
        else if (sscanf(line, "aim=%d", &value) == 1) g->opt_aim = value != 0;
        else if (sscanf(line, "highlight=%d", &value) == 1) g->opt_highlight = value != 0;
        else if (sscanf(line, "restored=%d", &value) == 1) g->opt_restored = value != 0;
    }
    fclose(f);
}

// The menu's input, before the game's: up and down (keys or the
// controller) or the mouse pick a row, a click, A, Enter or Space switch
// it; Tab (-), Esc, the right button (B) shut the menu.
void extras_update(Game *g, Vector2 mouse, float scale, int *debug) {
    int rows[EXTRA_COUNT], n = extras_rows(rows);
    if (plat_key(KEY_TAB) || plat_key(KEY_ESCAPE) || plat_rclick()) { g->extras_open = 0; return; }
    int step = plat_list_step() + plat_key(KEY_DOWN) - plat_key(KEY_UP);
    g->extras_focus = ((g->extras_focus + step) % n + n) % n;
    int over = -1;
    for (int k = 0; k < n && !plat_pad(); k++)
        if (CheckCollisionPointRec(mouse, extras_row_rect(k, scale))) over = k;
    if (over >= 0) g->extras_focus = over;
    int pick = plat_key(KEY_ENTER) || plat_key(KEY_SPACE) || (plat_click() && (plat_pad() || over >= 0));
    if (!pick) return;
    switch (rows[g->extras_focus]) {
    case EXTRA_CRT: crt_toggle(); break;
    case EXTRA_BAND: s_band = !s_band; break;
    case EXTRA_AIM: g->opt_aim = !g->opt_aim; break;
    case EXTRA_HIGHLIGHT: g->opt_highlight = !g->opt_highlight; break;
    case EXTRA_RESTORED: g->opt_restored = !g->opt_restored; break;
    case EXTRA_DEBUG: *debug = !*debug; return;
    }
    extras_save(g);
}

void extras_draw(Game *g, Font font, float scale, Vector2 mouse, int debug) {
    int rows[EXTRA_COUNT], n = extras_rows(rows);
    DrawRectangle(0, 0, plat_width(), plat_height(), Fade(BLACK, 0.45f));
    float h = 44 + n * EXTRAS_ROW + 40;
    Rectangle panel = {(EXTRAS_X - 14) * scale, (EXTRAS_Y - 12) * scale, (EXTRAS_W + 28) * scale, h * scale};
    DrawRectangleRounded(panel, 0.08f, 8, Fade(BLACK, 0.85f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.5f * scale, Fade(WHITE, 0.35f));
    float size = 20 * scale;
    DrawTextEx(font, "Дополнительно", (Vector2){EXTRAS_X * scale, EXTRAS_Y * scale}, 24 * scale, 1, WHITE);
    for (int k = 0; k < n; k++) {
        Rectangle r = extras_row_rect(k, scale);
        int focus = k == g->extras_focus;
        if (focus) DrawRectangleRounded(r, 0.3f, 6, Fade(BLUE_LINE, 0.22f));
        Color ink = focus ? BLUE_LINE : WHITE;
        float y = r.y + (r.height - size) / 2;
        DrawTextEx(font, kExtraName[rows[k]], (Vector2){r.x + 8 * scale, y}, size, 1, ink);
        const char *value = extra_value(g, rows[k], debug) ? "вкл" : "выкл";
        float w = MeasureTextEx(font, value, size, 1).x;
        DrawTextEx(font, value, (Vector2){r.x + r.width - w - 8 * scale, y}, size, 1, ink);
    }
    const char *hint = plat_pad() ? "A - переключить     B, \"-\" - закрыть" : "Клик, Enter - переключить     Tab, Esc - закрыть";
    float hy = (EXTRAS_Y + 44 + n * EXTRAS_ROW + 10) * scale;
    DrawTextEx(font, hint, (Vector2){EXTRAS_X * scale, hy}, 16 * scale, 1, Fade(WHITE, 0.7f));
    if (!plat_pad()) {
        HideCursor();
        draw_cursor(g, CURSOR_INTERFACE, 0, 0, mouse, scale);
    }
}
