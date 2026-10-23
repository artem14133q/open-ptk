// resources.c
//
// EN: The game's files: the stores (.STR), RESOURCE.QRC, resources by path, backgrounds, file names.
// RU: Файлы игры: архивы (.STR), RESOURCE.QRC, ресурсы по пути, фоны, работа с именами файлов.
#include "game.h"

#include <ctype.h>

// ---- resources ----

// The stores of a part live in different places depending on whether the
// game was copied from the CDs or installed; look in all of them. Their
// names are in capitals on the first game's discs and in small letters on
// the second's (flc1.str), which matters where names mind the case.
PetkaStore *open_store(const char *game_dir, const char *file) {
    static const char *dirs[] = {
        "", "data1_out/Main/", "data1_out/Flics/", "data1_out/Sfxs/", "Main/", "Flics/", "Sfxs/",
    };
    char lower[64];
    size_t n = 0;
    for (; file[n] && n < sizeof(lower) - 1; n++) lower[n] = (char)tolower((unsigned char)file[n]);
    lower[n] = 0;
    char path[1024];
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        for (int k = 0; k < 2; k++) {
            snprintf(path, sizeof(path), "%s/%s%s", game_dir, dirs[i], k ? lower : file);
            if (FileExists(path)) return petka_store_open(path);
        }
    }
    return NULL;
}

// Like the exe's MultiOpen: a path is looked up in every open store.
const uint8_t *res_find(const Game *g, const char *path, size_t *len) {
    for (int i = 0; i < g->store_count; i++) {
        const PetkaEntry *e = petka_store_find(g->stores[i], path);
        if (e) return petka_store_entry_data(g->stores[i], e, len);
    }
    return NULL;
}

// Lets a resource's bytes go once a copy of them is all that is kept (a
// voice line made into a sound, a background made into a texture): the
// stores are read on demand, and what is read stays until released.
void res_release(const Game *g, const char *path) {
    for (int i = 0; i < g->store_count; i++) {
        const PetkaEntry *e = petka_store_find(g->stores[i], path);
        if (e) { petka_store_entry_release(g->stores[i], e); return; }
    }
}

const uint8_t *part_file(const Game *g, const char *part, const char *file, size_t *len) {
    char name[256];
    snprintf(name, sizeof(name), "%s\\%s", part, file);
    return res_find(g, name, len);
}

// RESOURCE.QRC is text: "<id>  =  <path>" (some lines use "==").
void load_qrc(Game *g, const uint8_t *data, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        size_t end = pos;
        while (end < len && data[end] != '\n') end++;
        char line[512];
        size_t n = end - pos < sizeof(line) - 1 ? end - pos : sizeof(line) - 1;
        memcpy(line, data + pos, n);
        line[n] = 0;
        pos = end + 1;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        long id = strtol(line, NULL, 10);
        if (id <= 0 || id > 0xFFFF) continue;
        char *path = eq;
        while (*path == '=' || *path == ' ' || *path == '\t') path++;
        size_t plen = strlen(path);
        while (plen && (path[plen - 1] == '\r' || path[plen - 1] == ' ' || path[plen - 1] == '\t')) path[--plen] = 0;
        if (!plen) continue;
        free(g->qrc[id]);
        g->qrc[id] = (char *)malloc(plen + 1);
        memcpy(g->qrc[id], path, plen + 1);
    }
}

int has_ext(const char *path, const char *ext) {
    size_t lp = strlen(path), le = strlen(ext);
    if (le > lp) return 0;
    for (size_t i = 0; i < le; i++) {
        char a = path[lp - le + i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (a != ext[i]) return 0;
    }
    return 1;
}

// Same path with another 3-letter extension (X.FLC -> X.MSK / X.WAV).
void swap_ext(const char *path, const char *ext, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", path);
    size_t n = strlen(out);
    if (n >= 3) memcpy(out + n - 3, ext, 3);
}

Texture2D load_bmp_texture(const Game *g, const char *path) {
    Texture2D tex = {0};
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    int w = 0, h = 0;
    uint8_t *rgb = bytes ? petka_bmp_decode(bytes, len, &w, &h) : NULL;
    if (bytes) res_release(g, path);
    if (!rgb) { fprintf(stderr, "background not found: %s\n", path); return tex; }
    Image img = {.data = rgb, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

uint8_t *read_whole_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0 ? (uint8_t *)malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); data = NULL; }
    fclose(f);
    if (data) *len = (size_t)size;
    return data;
}
