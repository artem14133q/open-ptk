// Decoder for the standard Autodesk FLI/FLC animation format used for
// Petka's .FLC clips (both the menu splash clips in MAIN.STR and the
// per-location clips in FLC1/2/3.STR -- same real format, confirmed byte
// for byte against Petka.exe's header-parsing routine; see
// docs/str-format.md).
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
    uint8_t *indices;    // width*height, current frame as palette indices
    uint8_t *rgb;         // width*height*3, derived from indices+palette
} PetkaFlic;

// Parses the header and decodes frame 1. `data` is NOT copied -- it must
// stay valid for the lifetime of the returned PetkaFlic (e.g. it can point
// straight into a PetkaStore's buffer). Returns NULL on failure.
PetkaFlic *petka_flic_open(const uint8_t *data, size_t len);

void petka_flic_close(PetkaFlic *flic);

// Decodes the next frame into ->indices/->rgb. After the last frame the
// first one comes round again (frame_index 0), exactly as it first was.
// Returns 0 on success, -1 on error.
int petka_flic_advance(PetkaFlic *flic);

// Whether a palette entry is the clip's transparent colour. The exe turns
// every frame into 16 bits a pixel and keys out the *colour* of palette
// entry 0 there, so an entry is transparent when its colour falls on the
// same 16-bit one -- entry 0 itself, and any other entry within the low
// bits of it. Clips do rely on that: PROVODA.FLC is filled with entry 31
// (252,252,252) beside a key of 255,255,255, and STOL.FLC has no pixel of
// entry 0 at all.
int petka_flic_keyed(const PetkaFlic *flic, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif
