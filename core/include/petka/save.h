// Saved games, in the original's format (save<N>.dat).
//
// Written by the exe's sub_406EB0 and read by sub_407410, in one pass, all
// little-endian, with no compression and no check:
//
//   u32, u32     part, chapter
//   char[30]     the label under the slot (the time of saving)
//   17712 bytes  the slot picture: 108x82 pixels of 16 bits, RGB 5:6:5
//   u32 n        the length of the exe's object list: every object and room
//                of SCRIPT.DAT, in order, plus three objects of its own
//   (n - 3) x    u32 len + script name, u32 len + shown name, u32 busy,
//                u8 status, u32 resource, z, x, y, visible, active, animate
//   u32 k, k u16 the things carried
//   u32 len + the current room's script name
//   i32 x, y     Petka, by his feet;  i32 x, y  Chapayev
//   u32 w, w u32 the dialogue code, whole: it holds the menus' on/off bits
//                and the SKIPN turns
//   u32, u32     the cursor's resource and action
//   i32          the thing on the cursor, or -1
//   u32, u32     Petka's and Chapayev's costume
//   8 x i32      -1
//
// On reading, the exe looks objects up by name, throws away the shown name
// and the saved z, and leaves everything not listed here as it finds it.
//
// RU: Формат сохранений игры: запись и чтение состояния, заголовок с
// картинкой.
#ifndef PETKA_SAVE_H
#define PETKA_SAVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PETKA_SAVE_LABEL 30
#define PETKA_SAVE_THUMB_W 108
#define PETKA_SAVE_THUMB_H 82
#define PETKA_SAVE_THUMB_BYTES (PETKA_SAVE_THUMB_W * PETKA_SAVE_THUMB_H * 2)
// The exe's own three objects at the end of its list: cursor, case, panel.
#define PETKA_SAVE_EXTRA_OBJECTS 3

typedef struct {
    char *name;  // script name, cp1251; owned
    char *shown; // shown name, cp1251, may be ""; owned
    int32_t busy;
    uint8_t status;
    int32_t resource, z, x, y;
    int32_t visible, active, animate;
} PetkaSaveObject;

typedef struct {
    uint32_t part, chapter;
    char label[PETKA_SAVE_LABEL];
    uint8_t thumb[PETKA_SAVE_THUMB_BYTES];
    uint32_t object_count;
    PetkaSaveObject *objects; // owned
    uint32_t item_count;
    uint16_t *items;          // owned
    char *room;               // owned
    int32_t petka_x, petka_y, chapayev_x, chapayev_y;
    uint32_t code_count;
    uint32_t *code;           // owned
    int32_t cursor_resource, cursor_action, cursor_item;
    int32_t petka_costume, chapayev_costume;
} PetkaSave;

// Lays a saved game out as the bytes of a save file. Returns a malloc'd
// buffer and its length, or NULL.
// RU: Записывает состояние игры в байты сохранения.
uint8_t *petka_save_encode(const PetkaSave *save, size_t *out_len);

// Reads a save file. Returns NULL if the bytes are not one. Free the result
// with petka_save_free.
// RU: Читает сохранение целиком.
PetkaSave *petka_save_decode(const uint8_t *data, size_t len);

// Only the head of a save file -- part, chapter, label and picture -- for
// showing a slot. Returns 0 on success, -1 if the file is too short.
// RU: Читает только заголовок сохранения (метка, картинка).
int petka_save_peek(const uint8_t *data, size_t len, PetkaSave *out_head);

// EN: Frees a decoded save.
// RU: Освобождает прочитанное сохранение.
void petka_save_free(PetkaSave *save);

#ifdef __cplusplus
}
#endif

#endif // PETKA_SAVE_H
