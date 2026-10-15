// resources.h -- part of the launcher (launcher/src).
//
// EN: The game's files: the stores (.STR), RESOURCE.QRC, resources by path, backgrounds, file names.
// RU: Файлы игры: архивы (.STR), RESOURCE.QRC, ресурсы по пути, фоны, работа с именами файлов.
#ifndef PETKA_LAUNCHER_RESOURCES_H
#define PETKA_LAUNCHER_RESOURCES_H

#include "game.h"

// EN: Opens a store (.STR) wherever the game keeps it.
// RU: Открывает архив (.STR) там, где его держит игра.
PetkaStore *open_store(const char *game_dir, const char *file);
// EN: A resource's bytes by its path in the stores, NULL if none.
// RU: Байты ресурса по пути в архивах, NULL если нет.
const uint8_t *res_find(const Game *g, const char *path, size_t *len);
// EN: Lets a resource's bytes go once a copy is all that is kept.
// RU: Освобождает байты ресурса, когда осталась только копия.
void res_release(const Game *g, const char *path);
// EN: A file of a part (PART_n\file) from the stores.
// RU: Файл части (PART_n\файл) из архивов.
const uint8_t *part_file(const Game *g, const char *part, const char *file, size_t *len);
// EN: Reads the part's RESOURCE.QRC: resource id -> path.
// RU: Читает RESOURCE.QRC части: номер ресурса -> путь.
void load_qrc(Game *g, const uint8_t *data, size_t len);
// EN: Whether a path ends in an extension (any case).
// RU: Заканчивается ли путь расширением (без учёта регистра).
int has_ext(const char *path, const char *ext);
// EN: A path with its extension replaced.
// RU: Путь с заменённым расширением.
void swap_ext(const char *path, const char *ext, char *out, size_t out_sz);
// EN: Loads a background (.BMP) from the stores as a texture.
// RU: Загружает фон (.BMP) из архивов в текстуру.
Texture2D load_bmp_texture(const Game *g, const char *path);
// EN: Reads a whole file into memory.
// RU: Читает файл целиком в память.
uint8_t *read_whole_file(const char *path, size_t *len);

#endif
