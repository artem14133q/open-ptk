// Decoder for the standard Autodesk FLI/FLC animation format used for
// Petka's .FLC clips (both the menu splash clips in MAIN.STR and the
// per-location clips in FLC1/2/3.STR -- same real format, confirmed byte
// for byte against Petka.exe's header-parsing routine; see
// docs/str-format.md).
//
// RU: Декодер анимаций FLIC (.FLC) игры: кадры как индексы палитры, цвет по
// запросу, прозрачный цвет-ключ как в exe.
#ifndef PETKA_FLIC_H
#define PETKA_FLIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width, height;
    int frame_count;
    int speed_ms;        // delay between frames, in milliseconds
    int frame_index;     // 0-based index of the frame currently in rgb[]

    const uint8_t *file_data; // not owned -- must outlive the PetkaFlic
    size_t file_len;
    uint32_t oframe1, oframe2;
    uint32_t cursor;     // byte offset of the next frame chunk to decode

    uint8_t palette[256][3];
    // The palette as the exe holds it (a 6-bit chunk's values times four,
    // not spread to 255), and whether the last palette chunk was a 6-bit
    // one: what petka_flic_keyed() goes by.
    uint8_t exe_palette[256][3];
    int key_is_fixed;
    uint8_t *indices;    // width*height, current frame as palette indices
    // width*height*3, the frame in colour: made from indices+palette only
    // when petka_flic_rgb() asks for it -- a player that converts just the
    // part it draws should not pay for the whole frame on every decode.
    uint8_t *rgb;
    int rgb_stale;
} PetkaFlic;

// Parses the header and decodes frame 1. `data` is NOT copied -- it must
// stay valid for the lifetime of the returned PetkaFlic (e.g. it can point
// straight into a PetkaStore's buffer). Returns NULL on failure.
// RU: Разбирает заголовок и декодирует 1-й кадр; данные не копируются.
PetkaFlic *petka_flic_open(const uint8_t *data, size_t len);

// EN: Frees the clip.
// RU: Освобождает клип.
void petka_flic_close(PetkaFlic *flic);

// Decodes the next frame into ->indices. After the last frame the
// first one comes round again (frame_index 0), exactly as it first was.
// Returns 0 on success, -1 on error.
// RU: Декодирует следующий кадр (после последнего - снова первый).
int petka_flic_advance(PetkaFlic *flic);

// The current frame in colour (->rgb), brought up to date; NULL if out of
// memory.
// RU: Текущий кадр в цвете (строится по запросу).
const uint8_t *petka_flic_rgb(PetkaFlic *flic);

// Whether a palette entry is the clip's transparent colour. The exe keeps
// a 16-bit colour for every palette entry, draws with those, and keys out
// the one of entry 0. So an entry is transparent when its colour falls on
// the same 16 bits: entry 0 itself, and any other entry within the low
// bits of it. Clips do rely on that: PROVODA.FLC is filled with entry 31
// (252,252,252) beside a key of 255,255,255, and STOL.FLC has no pixel of
// entry 0 at all.
// RU: Прозрачен ли цвет палитры (цвет-ключ как в exe).
int petka_flic_keyed(const PetkaFlic *flic, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif
