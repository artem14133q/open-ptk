#include "petka/ini.h"

#include <string.h>

// Lower-case for CP1251: ASCII, the main Cyrillic block and Ё.
static unsigned char lower(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    if (c >= 0xC0 && c <= 0xDF) return (unsigned char)(c + 32);
    if (c == 0xA8) return 0xB8;
    return c;
}

static int is_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Does the trimmed span [s, s + n) equal `name`, ignoring case?
static int same_name(const uint8_t *s, size_t n, const char *name) {
    while (n && is_space(s[0])) { s++; n--; }
    while (n && is_space(s[n - 1])) n--;
    if (strlen(name) != n) return 0;
    for (size_t i = 0; i < n; i++)
        if (lower(s[i]) != lower((unsigned char)name[i])) return 0;
    return 1;
}

int petka_ini_get(const uint8_t *data, size_t len, const char *section, const char *key,
                  char *out, size_t out_sz) {
    if (out_sz) out[0] = 0;
    int in_section = 0;
    size_t pos = 0;
    while (pos < len) {
        const uint8_t *line = data + pos;
        size_t n = 0;
        while (pos + n < len && line[n] != '\n') n++;
        pos += n + 1;
        while (n && is_space(line[0])) { line++; n--; }
        if (!n || line[0] == ';') continue;

        if (line[0] == '[') {
            const uint8_t *close = (const uint8_t *)memchr(line, ']', n);
            in_section = close && same_name(line + 1, (size_t)(close - line - 1), section);
            continue;
        }
        const uint8_t *eq = in_section ? (const uint8_t *)memchr(line, '=', n) : NULL;
        if (!eq || !same_name(line, (size_t)(eq - line), key)) continue;

        const uint8_t *v = eq + 1;
        size_t vn = n - (size_t)(v - line);
        while (vn && is_space(v[0])) { v++; vn--; }
        while (vn && is_space(v[vn - 1])) vn--;
        if (vn >= 2 && (v[0] == '"' || v[0] == '\'') && v[vn - 1] == v[0]) { v++; vn -= 2; }
        if (out_sz) {
            if (vn > out_sz - 1) vn = out_sz - 1;
            memcpy(out, v, vn);
            out[vn] = 0;
        }
        return 1;
    }
    return 0;
}
