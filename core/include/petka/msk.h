// Reader for Petka's .MSK companion files (paired with a same-named .FLC).
//
// Structure, reverse-engineered from QFlic::Init (sub_40F390): the last 16
// bytes of the file are 4 i32 fields -- left, top, right, bottom -- the
// screen-space rect this clip is composited into. (The body before that is
// a fixed-size editor scratch buffer that does NOT hold a real per-pixel
// mask in the samples inspected -- see docs/str-format.md. Per-pixel
// transparency instead comes from the FLIC's own palette index 0, treated
// as the color key.)
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

#ifdef __cplusplus
}
#endif

#endif
