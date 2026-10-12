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

// Decodes the next frame into ->indices/->rgb, wrapping back to the ring
// frame (oframe2) after the last frame. Returns 0 on success, -1 on error.
int petka_flic_advance(PetkaFlic *flic);

#ifdef __cplusplus
}
#endif

#endif
