#include "petka/bmp.h"

#include <stdlib.h>

static uint16_t read_u16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t read_i32le(const uint8_t *p) { return (int32_t)read_u32le(p); }

uint8_t *petka_bmp_decode(const uint8_t *data, size_t len, int *out_width, int *out_height) {
    if (len < 54 || data[0] != 'B' || data[1] != 'M') return NULL;

    uint32_t bits_off = read_u32le(data + 10);
    int32_t width = read_i32le(data + 18);
    int32_t height_field = read_i32le(data + 22);
    uint16_t bpp = read_u16le(data + 28);
    uint32_t compression = read_u32le(data + 30);

    if (width <= 0 || height_field == 0 || compression != 0) return NULL;

    int height = height_field > 0 ? height_field : -height_field;
    int bottom_up = height_field > 0; // negative biHeight means top-down

    size_t row_stride = (((size_t)width * bpp / 8) + 3) & ~(size_t)3;
    if (bits_off + row_stride * (size_t)height > len) return NULL;

    uint8_t *out = (uint8_t *)malloc((size_t)width * (size_t)height * 3);
    if (!out) return NULL;

    for (int y = 0; y < height; y++) {
        int src_row = bottom_up ? (height - 1 - y) : y;
        const uint8_t *row = data + bits_off + (size_t)src_row * row_stride;
        uint8_t *dst = out + (size_t)y * (size_t)width * 3;

        if (bpp == 16) {
            for (int x = 0; x < width; x++) {
                // Big-endian u16, bits: BBBBB GGGGGG RRRRR (see bmp.h).
                uint16_t p = (uint16_t)((row[x * 2] << 8) | row[x * 2 + 1]);
                uint8_t b5 = (p >> 11) & 0x1F;
                uint8_t g6 = (p >> 5) & 0x3F;
                uint8_t r5 = p & 0x1F;
                dst[x * 3 + 0] = (uint8_t)((r5 * 255) / 31);
                dst[x * 3 + 1] = (uint8_t)((g6 * 255) / 63);
                dst[x * 3 + 2] = (uint8_t)((b5 * 255) / 31);
            }
        } else if (bpp == 24) {
            for (int x = 0; x < width; x++) {
                dst[x * 3 + 0] = row[x * 3 + 2]; // R
                dst[x * 3 + 1] = row[x * 3 + 1]; // G
                dst[x * 3 + 2] = row[x * 3 + 0]; // B
            }
        } else {
            free(out);
            return NULL;
        }
    }

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return out;
}
