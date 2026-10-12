// Decoder for the BMP variant used by Petka's background/UI images.
//
// These are technically well-formed Windows BMP files (BM magic, standard
// 40-byte BITMAPINFOHEADER, bfOffBits=54, no BITFIELDS section) EXCEPT for
// one quirk found by trial and error: 16bpp pixels are NOT the usual
// little-endian RGB565 Windows uses -- they're a big-endian 16-bit word
// with bits laid out BBBBB GGGGGG RRRRR (B in the high bits, R in the low
// bits). Standard BMP decoders (including stb_image, which raylib uses
// internally) render this as per-pixel noise since they assume the normal
// little-endian 555/565 layout. 24bpp images (if any) are standard BGR888.
//
// See docs/str-format.md for how this was found (visual A/B test of
// several byte-order/channel-order hypotheses against a known background).
#ifndef PETKA_BMP_H
#define PETKA_BMP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decodes `data` (bfSize/length may be unreliable -- only `len` is trusted)
// into a freshly malloc'd top-down RGB888 buffer (3 bytes/pixel, no
// padding). Caller frees the returned pointer. Returns NULL on failure
// (not a BMP, unsupported bit depth, truncated data).
uint8_t *petka_bmp_decode(const uint8_t *data, size_t len, int *out_width, int *out_height);

#ifdef __cplusplus
}
#endif

#endif
