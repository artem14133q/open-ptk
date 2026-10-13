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

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// With the offset table ending at `table_end`, the bottom of `frame`'s rects;
// -2 if the file cannot be laid out that way.
static int frame_bottom_at(const uint8_t *data, size_t table_end, int frame_count, int frame) {
    size_t table = (size_t)frame_count * 4;
    if (table > table_end) return -2;
    size_t lists_end = table_end - table;
    // The table is indexed from its end: frame 1's entry is the last one.
    uint32_t off = read_u32le(data + table_end - 4 * ((size_t)frame + 1));
    if ((size_t)off + 4 > lists_end) return -2;
    int32_t count = read_i32le(data + off);
    if (count < 0 || (size_t)count > (lists_end - off - 4) / 8) return -2;
    int bottom = -1;
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *r = data + off + 4 + 8 * (size_t)i;
        int b = r[6] | r[7] << 8;
        if (b > bottom) bottom = b;
    }
    return bottom;
}

int petka_msk_frame_bottom(const uint8_t *data, size_t len, int frame_count, int frame) {
    if (!data || frame_count <= 0 || frame < 0 || frame >= frame_count) return -1;
    int bottom = len >= 16 ? frame_bottom_at(data, len - 16, frame_count, frame) : -2;
    // Two files end with the offset table, with no covering rect after it.
    if (bottom == -2) bottom = frame_bottom_at(data, len, frame_count, frame);
    return bottom == -2 ? -1 : bottom;
}
