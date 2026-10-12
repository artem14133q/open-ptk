#include "petka/msk.h"

static int32_t read_i32le(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

int petka_msk_rect(const uint8_t *data, size_t len, PetkaMskRect *out) {
    if (len < 16) return -1;
    const uint8_t *tail = data + (len - 16);
    out->left = read_i32le(tail + 0);
    out->top = read_i32le(tail + 4);
    out->right = read_i32le(tail + 8);
    out->bottom = read_i32le(tail + 12);
    if (out->left < 0) out->left = 0;
    if (out->top < 0) out->top = 0;
    return 0;
}
