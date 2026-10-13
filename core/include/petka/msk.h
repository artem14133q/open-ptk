// Reader for Petka's .MSK companion files (paired with a same-named .FLC).
//
// Layout, from the exe (QFlic::Init, and the z update sub_401810 that walks
// it), checked on every .MSK of the game:
//
//   for each frame of the clip: i32 count, then count rects of 4 u16
//                               (left, top, right, bottom) -- the parts of
//                               that frame the object occupies
//   one u32 per frame:          offset of that frame's list, the first
//                               frame's entry LAST
//   16 bytes:                   4 i32 left, top, right, bottom -- the rect
//                               covering the whole clip (two files lack it)
//
// The rects are not a pixel mask: transparency comes from the FLIC's palette
// index 0. They are what the engine sorts by -- an object's draw order is
// the lowest edge of its current frame's rects (see petka/script.h, auto_z).
#ifndef PETKA_MSK_H
#define PETKA_MSK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int left, top, right, bottom;
} PetkaMskRect;

// Returns 0 on success, -1 if `len` is too small to hold a trailing rect.
int petka_msk_rect(const uint8_t *data, size_t len, PetkaMskRect *out);

// The lowest bottom edge among the rects of one frame (0-based) of a clip of
// `frame_count` frames, or -1 if the frame has no rects or the file does not
// have the layout above.
int petka_msk_frame_bottom(const uint8_t *data, size_t len, int frame_count, int frame);

#ifdef __cplusplus
}
#endif

#endif
