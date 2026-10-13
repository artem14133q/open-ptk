// Decoder for the BMP variant used by Petka's background/UI images.
//
// They carry a standard header (BM magic, 40-byte BITMAPINFOHEADER,
// bfOffBits=54), but the 16bpp ones -- nearly all of them -- are not really
// Windows BMPs:
//   - 8 more bytes follow the header: a u32 tag (0 or 2, no visible effect)
//     and the file's length as a u32. Pixels start at 62, not at bfOffBits.
//   - rows are width*2 bytes, NOT padded to a multiple of four.
//   - each pixel is a big-endian 16-bit word, BBBBB GGGGGG RRRRR.
//   - the files were made from 24-bit BMPs by taking the pixel bytes three
//     at a time into one word each -- row padding included. Where width*3
//     is a multiple of four there is none and the words are the pixels. In
//     the two 1349-pixel backgrounds of part 2 (the restaurant and the
//     square) each row had one byte of it: the words run on a byte further
//     every row, a pixel's blue, green and red falling into two words on
//     two rows out of three. Those are read back as that stream of bytes.
// Standard BMP decoders (including stb_image, which raylib uses internally)
// get all three wrong. The length field is how this variant is recognised.
//
// A few images are ordinary BMPs with a palette (8bpp, and the 1bpp black
// screen MAIN\BG1.BMP that cutscene sequences use), and 24bpp is handled as
// standard BGR888 too.
//
// See docs/str-format.md for how this was found.
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
