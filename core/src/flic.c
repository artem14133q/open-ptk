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
        rd_u8(r); // packet count byte -- redundant with "packets until the row is full", not used
        int x = 0;
        while (x < width) {
            int8_t n = rd_i8(r);
            if (n >= 0) {
                // Replicate packet: the next byte is ONE pixel, repeated
                // `n` times -- NOT `n + 1`. Getting this +1 wrong doesn't
                // break byte-alignment much (each row is still roughly
                // `width` pixels, so neighboring rows mostly resync), but
                // it corrupts every replicate run's length by one pixel,
                // which is invisible on flat-colored runs (most of a
                // frame) and only shows up as compounding smear/tearing
                // on busy, detail-heavy regions with many short runs
                // (confirmed on PART_1/P-ROOM.BG/KROVATI.FLC's patchwork
                // quilt -- plain areas like the pillow decoded fine even
                // with the bug, only the quilt pattern was garbled).
                uint8_t pixel = rd_u8(r);
                int count = n;
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

static void decode_color256(Reader *r, PetkaFlic *f) {
    uint8_t (*palette)[3] = f->palette;
    f->key_is_fixed = 0;
    uint16_t num_packets = rd_u16(r);
    int pos = 0;
    for (uint16_t p = 0; p < num_packets; p++) {
        int skip = rd_u8(r);
        int copy = rd_u8(r);
        pos += skip;
        int n = copy == 0 ? 256 : copy;
        for (int i = 0; i < n && pos < 256; i++) {
            for (int c = 0; c < 3; c++) palette[pos][c] = f->exe_palette[pos][c] = rd_u8(r);
            pos++;
        }
    }
}

static void decode_color64(Reader *r, PetkaFlic *f) {
    uint8_t (*palette)[3] = f->palette;
    f->key_is_fixed = 1;
    uint16_t num_packets = rd_u16(r);
    int pos = 0;
    for (uint16_t p = 0; p < num_packets; p++) {
        int skip = rd_u8(r);
        int copy = rd_u8(r);
        pos += skip;
        int n = copy == 0 ? 256 : copy;
        for (int i = 0; i < n && pos < 256; i++) {
            // 6-bit VGA-style channels, scale up to 8-bit. (The exe just
            // multiplies by four, in a byte.)
            for (int c = 0; c < 3; c++) {
                uint8_t v = rd_u8(r);
                palette[pos][c] = (uint8_t)(v * 255 / 63);
                f->exe_palette[pos][c] = (uint8_t)(v * 4);
            }
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
            case CHUNK_COLOR256: decode_color256(&r, f); break;
            case CHUNK_COLOR64: decode_color64(&r, f); break;
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

static uint32_t chunk_size_at(const PetkaFlic *flic, uint32_t off) {
    Reader hr = {flic->file_data, flic->file_len, off};
    return rd_u32(&hr);
}

// A palette entry as the 16 bits the exe keeps for it. Each palette chunk's
// handler turns the whole table into words again, in its own way:
//  - the 8-bit chunk's (sub_410060) by the screen's own pixel format, each
//    channel cut to that format's width -- 5-6-5 here, the usual one;
//  - the 6-bit chunk's (sub_410280) as r5 << 11 | g5 << 6 | b5: green cut
//    to five bits too, its low bit left clear.
static unsigned colour16(const PetkaFlic *flic, uint8_t index) {
    const uint8_t *rgb = flic->exe_palette[index];
    unsigned green = flic->key_is_fixed ? (unsigned)(rgb[1] >> 3) << 6 : (unsigned)(rgb[1] >> 2) << 5;
    return (unsigned)(rgb[0] >> 3) << 11 | green | (unsigned)(rgb[2] >> 3);
}

int petka_flic_keyed(const PetkaFlic *flic, uint8_t index) {
    return index == 0 || colour16(flic, index) == colour16(flic, 0);
}

int petka_flic_advance(PetkaFlic *flic) {
    if (!flic) return -1;
    if (flic->frame_count <= 1) return 0; // nothing to animate

    uint32_t second = flic->oframe1 + chunk_size_at(flic, flic->oframe1);
    if (flic->frame_index + 1 >= flic->frame_count) {
        // Back to the first frame, by decoding it afresh. The file does
        // carry a "ring" chunk after the last frame for this -- the changes
        // from the last frame to the first -- and for 1219 of the game's
        // 1224 multi-frame clips playing it gives the very same picture;
        // for the other five it does not quite, so a loop would drift.
        //
        // (This code once restarted from the *second* frame's chunk, i.e.
        // applied "first to second" changes to the last frame's picture.
        // Every looping clip then ran a frame ahead of its frame number and
        // kept stray pixels from the end of the loop.)
        memset(flic->indices, 0, (size_t)flic->width * (size_t)flic->height);
        if (decode_frame_at(flic, flic->oframe1) != 0) return -1;
        flic->cursor = second;
        flic->frame_index = 0;
        return 0;
    }

    uint32_t off = flic->cursor;
    if (off + 4 > flic->file_len) return -1;
    if (decode_frame_at(flic, off) != 0) return -1;
    flic->cursor = off + chunk_size_at(flic, off);
    flic->frame_index++;
    return 0;
}
