// text.c
//
// EN: Text: CP1251 to UTF-8, the Cyrillic font, wrapped and rimmed lines, the dark band behind texts.
// RU: Текст: перевод CP1251 в UTF-8, кириллический шрифт, перенос строк, обводка, тёмная подложка.
#include "game.h"

// Dialogue and object names are CP1251; raylib expects UTF-8. Covers ASCII
// and the main Cyrillic block plus Ё/ё -- enough for the game's text.
void cp1251_to_utf8(const char *in, char *out, const size_t out_sz) {
    size_t oi = 0;

    for (const unsigned char *p = (const unsigned char *)in; *p && oi + 4 < out_sz; p++) {
        unsigned int cp;
        const unsigned char c = *p;

        if (c < 0x80) cp = c;
        else if (c == 0xA8) cp = 0x0401; // Ё
        else if (c == 0xB8) cp = 0x0451; // ё
        else if (c >= 0xC0) cp = 0x0410 + (c - 0xC0); // А-я
        else cp = '?';

        if (cp < 0x80) {
            out[oi++] = (char)cp;
        } else {
            out[oi++] = (char)(0xC0 | (cp >> 6));
            out[oi++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    out[oi] = 0;
}

const char *utf8_name(const char *cp1251) {
    static char buf[4][256];
    static int next;

    char *out = buf[next++ & 3];
    cp1251_to_utf8(cp1251, out, sizeof(buf[0]));

    return out;
}

// Default raylib font only has ASCII glyphs. Try a few common system fonts
// with Cyrillic coverage for desktop testing; TODO ship a bundled font for
// a platform-independent release (including Switch).
Font load_cyrillic_font(void) {
    // The exe draws all its text in bold Arial (two fonts made at start-up,
    // sub_410CD0: 20 and 16 pixels high); take that where it is to be had.
    static const char *candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Tahoma.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };

    int codepoints[0x7E - 0x20 + 1 + 0x4FF - 0x400 + 1];
    int n = 0;

    for (int c = 0x20; c <= 0x7E; c++) codepoints[n++] = c;
    for (int c = 0x400; c <= 0x4FF; c++) codepoints[n++] = c;

    const Font system = plat_system_font(32, codepoints, n);

    if (system.glyphCount > 0) return system;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!FileExists(candidates[i])) continue;
        const Font f = LoadFontEx(candidates[i], 32, codepoints, n);
        if (f.glyphCount > 0) return f;
    }

    fprintf(stderr, "no Cyrillic-capable font found -- text will render as '?'\n");
    return GetFontDefault();
}

// Draws text broken into lines no wider than `width`, `leading` apart;
// returns the y below it.
float draw_wrapped(
    const Font font,
    const char *text,
    const float x,
    float y,
    const float width,
    const float size,
    const float leading,
    const Color ink
) {
    char row[1024] = "";
    const char *p = text;

    while (*p) {
        const char *space = strchr(p, ' ');
        const size_t word = space ? (size_t)(space - p) + 1 : strlen(p);
        char trial[1024];
        snprintf(trial, sizeof(trial), "%s%.*s", row, (int)word, p);
        if (row[0] && MeasureTextEx(font, trial, size, 1).x > width) {
            DrawTextEx(font, row, (Vector2){.x = x, .y = y}, size, 1, ink);
            y += leading;
            row[0] = 0;
            continue;
        }

        snprintf(row, sizeof(row), "%s", trial);
        p += word;
    }

    DrawTextEx(font, row, (Vector2){.x = x, .y = y}, size, 1, ink);
    return y + leading;
}

// The exe's two kinds of text over the picture -- the name of the object
// under the cursor (sub_41F8E0) and a phrase being spoken (sub_420CA0) -- are
// made alike: the 20-pixel bold Arial, broken into lines no wider than 625,
// as a block in the middle of the bottom of the screen, its lower edge on
// row 479, with nothing behind it. A phrase's lines are centred in the
// block; a name's are not.
// Breaks text at spaces into lines no wider than `limit`; returns how many.
int wrap_text(
    const Font font,
    const char *text,
    const float size,
    const float limit,
    char lines[][512],
    const int max
) {
    int count = 0;
    char row[512] = "";
    const char *p = text;
    while (*p && count < max) {
        const char *space = strchr(p, ' ');
        const size_t word = space ? (size_t)(space - p) + 1 : strlen(p);
        char trial[512];
        snprintf(trial, sizeof(trial), "%s%.*s", row, (int)word, p);
        if (row[0] && MeasureTextEx(font, trial, size, 1).x > limit) {
            snprintf(lines[count++], 512, "%s", row);
            row[0] = 0;
            continue;
        }
        snprintf(row, sizeof(row), "%s", trial);
        p += word;
    }

    if (row[0] && count < max) snprintf(lines[count++], 512, "%s", row);
    return count;
}

// The exe draws these texts antialiased onto a surface filled with a
// "second colour" and then keys that colour out. The pixels on the letters'
// edges, part ink and part fill, stay: a thin rim of the fill colour round
// every letter -- dark red under a speaker's coloured subtitle, near-black
// under a caption or a menu line. Drawn here as the text in the fill colour,
// faint, a pixel off in each direction, under the text itself.
void draw_rimmed(
    const Font font,
    const char *text,
    const Vector2 at,
    const float size,
    const Color ink,
    const Color fill,
    const float scale
) {
    const float d = scale > 1 ? scale * 0.75f : 1;
    const Color rim = Fade(fill, 0.55f);

    static const float kOff[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };

    for (int i = 0; i < 8; i++) DrawTextEx(font, text, (Vector2){
        .x = at.x + kOff[i][0] * d,
        .y = at.y + kOff[i][1] * d
    }, size, 1, rim);

    DrawTextEx(font, text, at, size, 1, ink);
}

// Ours, asked for: a dark, see-through band behind text over the picture,
// so that it reads on any background. The original has none.
int s_band = 1; // the band is on (the extras menu)

void draw_band(const Rectangle r, const float scale) {
    if (!s_band) return;
    const float pad = 6 * scale;
    DrawRectangleRounded((Rectangle){
        .x = r.x - pad,
        .y = r.y - pad / 2,
        .width = r.width + 2 * pad,
        .height = r.height + pad
    }, 0.25f, 6, Fade(BLACK, 0.7f));
}

void draw_bottom_text(
    const Font font,
    const char *text,
    const Color ink,
    const Color fill,
    const int centre_lines,
    const int backdrop,
    const float scale
) {
    enum { MAX_LINES = 12 };
    static char lines[MAX_LINES][512];
    const float size = 20 * scale;
    float widest = 0;
    const int count = wrap_text(font, text, size, 625 * scale, lines, MAX_LINES);

    for (int i = 0; i < count; i++) {
        float w = MeasureTextEx(font, lines[i], size, 1).x;
        if (w > widest) widest = w;
    }

    const float top = 479 * scale - ((float)count * size + 4 * scale);
    const float left = (GAME_W * scale - widest) / 2;

    if (backdrop && count > 0) draw_band((Rectangle){
        .x = left,
        .y = top,
        .width = widest,
        .height = (float)count * size
    }, scale);

    for (int i = 0; i < count; i++) {
        const float x = centre_lines ? (GAME_W * scale - MeasureTextEx(font, lines[i], size, 1).x) / 2 : left;

        draw_rimmed(
            font,
            lines[i],
            (Vector2){
                .x = x,
                .y = top + (float)i * size
            },
            size,
            ink,
            fill,
            scale
        );
    }
}
