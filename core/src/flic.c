#include "petka/flic.h"

#include <stdlib.h>
#include <string.h>

// Standard FLI/FLC chunk type IDs (public format, see docs/str-format.md).
#define CHUNK_FRAME 0xF1FA
#define CHUNK_COLOR256 4
#define CHUNK_SS2 7
#define CHUNK_COLOR64 11
#define CHUNK_LC 12
#define CHUNK_BLACK 13
#define CHUNK_BYTE_RUN 15
#define CHUNK_COPY 16
#define CHUNK_PSTAMP 18

typedef struct {
    const uint8_t *base;
    size_t len;
    size_t pos;
} Reader;

static uint8_t rd_u8(Reader *r) { return r->pos < r->len ? r->base[r->pos++] : 0; }
static int8_t rd_i8(Reader *r) { return (int8_t)rd_u8(r); }
static uint16_t rd_u16(Reader *r) {
    uint16_t v = (uint16_t)(rd_u8(r) | (uint16_t)rd_u8(r) << 8);
    return v;
}
static int16_t rd_i16(Reader *r) { return (int16_t)rd_u16(r); }
static uint32_t rd_u32(Reader *r) {
    uint32_t v = rd_u8(r);
    v |= (uint32_t)rd_u8(r) << 8;
    v |= (uint32_t)rd_u8(r) << 16;
    v |= (uint32_t)rd_u8(r) << 24;
    return v;
}

static void decode_byte_run(Reader *r, uint8_t *indices, int width, int height) {
    for (int y = 0; y < height; y++) {
        uint8_t *row = indices + (size_t)y * width;
        rd_u8(r); // packet count byte, unused -- packets run until the row is full
        int x = 0;
        while (x < width) {
            int8_t n = rd_i8(r);
            if (n >= 0) {
                uint8_t pixel = rd_u8(r);
                int count = n + 1;
                for (int i = 0; i < count && x < width; i++) row[x++] = pixel;
            } else {
                int count = -n;
                for (int i = 0; i < count && x < width; i++) row[x++] = rd_u8(r);
            }
        }
    }
}

static void decode_copy(Reader *r, uint8_t *indices, int width, int height) {
    for (int i = 0; i < width * height; i++) indices[i] = rd_u8(r);
}

static void decode_ss2(Reader *r, uint8_t *indices, int width, int height) {
    uint16_t line_count = rd_u16(r);
    int y = 0;
    for (uint16_t li = 0; li < line_count && y < height; li++) {
        int16_t count = rd_i16(r);
        while (count < 0) {
            if (count & 0x4000) {
                // High bit set, bit14 set: skip |count| lines.
                y += -count;
            } else {
                // High bit set, bit14 clear: last pixel of the line is
                // packed directly into the low byte of this word.
                if (y < height && width > 0) indices[(size_t)y * width + (width - 1)] = (uint8_t)(count & 0xFF);
            }
            count = rd_i16(r);
        }
        if (y >= height) break;
        uint8_t *row = indices + (size_t)y * width;
        int x = 0;
        for (int p = 0; p < count; p++) {
            x += rd_u8(r); // skip count
            int8_t size = rd_i8(r);
            if (size >= 0) {
                for (int i = 0; i < size && x + 1 < width; i++) {
                    uint16_t w = rd_u16(r);
                    row[x] = (uint8_t)(w & 0xFF);
                    row[x + 1] = (uint8_t)((w >> 8) & 0xFF);
                    x += 2;
                }
            } else {
                int n = -size;
                uint16_t w = rd_u16(r);
                uint8_t lo = (uint8_t)(w & 0xFF), hi = (uint8_t)((w >> 8) & 0xFF);
                for (int i = 0; i < n && x + 1 < width; i++) {
                    row[x] = lo;
                    row[x + 1] = hi;
                    x += 2;
                }
            }
        }
        y++;
    }
}

static void decode_color256(Reader *r, uint8_t palette[256][3]) {
    uint16_t num_packets = rd_u16(r);
    int pos = 0;
    for (uint16_t p = 0; p < num_packets; p++) {
        int skip = rd_u8(r);
        int copy = rd_u8(r);
        pos += skip;
        int n = copy == 0 ? 256 : copy;
        for (int i = 0; i < n && pos < 256; i++) {
            palette[pos][0] = rd_u8(r);
            palette[pos][1] = rd_u8(r);
            palette[pos][2] = rd_u8(r);
            pos++;
        }
    }
}

static void decode_color64(Reader *r, uint8_t palette[256][3]) {
    uint16_t num_packets = rd_u16(r);
    int pos = 0;
    for (uint16_t p = 0; p < num_packets; p++) {
        int skip = rd_u8(r);
        int copy = rd_u8(r);
        pos += skip;
        int n = copy == 0 ? 256 : copy;
        for (int i = 0; i < n && pos < 256; i++) {
            // 6-bit VGA-style channels, scale up to 8-bit.
            palette[pos][0] = (uint8_t)(rd_u8(r) * 255 / 63);
            palette[pos][1] = (uint8_t)(rd_u8(r) * 255 / 63);
            palette[pos][2] = (uint8_t)(rd_u8(r) * 255 / 63);
            pos++;
        }
    }
}

static void rebuild_rgb(PetkaFlic *f) {
    int n = f->width * f->height;
    for (int i = 0; i < n; i++) {
        uint8_t idx = f->indices[i];
        f->rgb[i * 3 + 0] = f->palette[idx][0];
        f->rgb[i * 3 + 1] = f->palette[idx][1];
        f->rgb[i * 3 + 2] = f->palette[idx][2];
    }
}

static int decode_frame_at(PetkaFlic *f, uint32_t off) {
    if (off + 16 > f->file_len) return -1;
    Reader r = {f->file_data, f->file_len, off};

    uint32_t frame_size = rd_u32(&r);
    uint16_t type = rd_u16(&r);
    uint16_t chunk_count = rd_u16(&r);
    r.pos += 8; // reserved (delay/width/height overrides) -- unused for now
    (void)frame_size;

    if (type != CHUNK_FRAME) return -1;

    for (uint16_t c = 0; c < chunk_count; c++) {
        if (r.pos + 6 > r.len) break;
        size_t chunk_start = r.pos;
        uint32_t csize = rd_u32(&r);
        uint16_t ctype = rd_u16(&r);
        switch (ctype) {
            case CHUNK_BYTE_RUN: decode_byte_run(&r, f->indices, f->width, f->height); break;
            case CHUNK_SS2: decode_ss2(&r, f->indices, f->width, f->height); break;
            case CHUNK_COPY: decode_copy(&r, f->indices, f->width, f->height); break;
            case CHUNK_BLACK: memset(f->indices, 0, (size_t)f->width * f->height); break;
            case CHUNK_COLOR256: decode_color256(&r, f->palette); break;
            case CHUNK_COLOR64: decode_color64(&r, f->palette); break;
            case CHUNK_LC: decode_ss2(&r, f->indices, f->width, f->height); break; // close enough for byte-aligned delta; rare in practice
            case CHUNK_PSTAMP:
            default: break; // unknown/ignored chunk, skip via chunk_start+csize below
        }
        r.pos = chunk_start + csize;
    }

    rebuild_rgb(f);
    return 0;
}

PetkaFlic *petka_flic_open(const uint8_t *data, size_t len) {
    if (len < 128) return NULL;
    Reader r = {data, len, 0};
    rd_u32(&r); // total size, unused (we trust `len`)
    uint16_t magic = rd_u16(&r);
    if (magic != 0xAF11 && magic != 0xAF12) return NULL;
    uint16_t frames = rd_u16(&r);
    uint16_t width = rd_u16(&r);
    uint16_t height = rd_u16(&r);
    r.pos = 16;
    uint16_t speed = rd_u16(&r);
    r.pos = 80;
    uint32_t oframe1 = rd_u32(&r);
    uint32_t oframe2 = rd_u32(&r);

    if (width == 0 || height == 0) return NULL;

    PetkaFlic *f = (PetkaFlic *)calloc(1, sizeof(PetkaFlic));
    if (!f) return NULL;
    f->width = width;
    f->height = height;
    f->frame_count = frames ? frames : 1;
    f->speed_ms = (magic == 0xAF11) ? speed * 14 : (speed ? speed : 1);
    f->file_data = data;
    f->file_len = len;
    f->oframe1 = oframe1;
    f->oframe2 = oframe2;
    f->frame_index = 0;

    f->indices = (uint8_t *)calloc((size_t)width * height, 1);
    f->rgb = (uint8_t *)calloc((size_t)width * height * 3, 1);
    if (!f->indices || !f->rgb) {
        petka_flic_close(f);
        return NULL;
    }

    if (decode_frame_at(f, oframe1) != 0) {
        petka_flic_close(f);
        return NULL;
    }
    f->cursor = oframe1; // next advance() starts from frame 1's own chunk size
    // Re-read frame 1's size to position the cursor right after it.
    {
        Reader hr = {data, len, oframe1};
        uint32_t fsize = rd_u32(&hr);
        f->cursor = oframe1 + fsize;
    }
    return f;
}

void petka_flic_close(PetkaFlic *flic) {
    if (!flic) return;
    free(flic->indices);
    free(flic->rgb);
    free(flic);
}

int petka_flic_advance(PetkaFlic *flic) {
    if (!flic) return -1;
    if (flic->frame_count <= 1) return 0; // nothing to animate

    uint32_t off = flic->cursor;
    int next_index = flic->frame_index + 1;
    if (next_index >= flic->frame_count) {
        // Loop back: the ring frame at oframe2 is a delta from the last
        // frame back to frame 1's state.
        off = flic->oframe2;
        next_index = 0;
    }
    if (off + 4 > flic->file_len) return -1;

    if (decode_frame_at(flic, off) != 0) return -1;

    Reader hr = {flic->file_data, flic->file_len, off};
    uint32_t fsize = rd_u32(&hr);
    flic->cursor = off + fsize;
    flic->frame_index = next_index;
    return 0;
}
