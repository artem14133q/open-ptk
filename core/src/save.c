#include "petka/save.h"

#include <stdlib.h>
#include <string.h>

// ---- writing ----

typedef struct {
    uint8_t *data;
    size_t len, cap;
    int failed;
} Out;

static void put(Out *o, const void *bytes, size_t n) {
    if (o->failed) return;
    if (o->len + n > o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 65536;
        while (cap < o->len + n) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(o->data, cap);
        if (!grown) { o->failed = 1; return; }
        o->data = grown;
        o->cap = cap;
    }
    memcpy(o->data + o->len, bytes, n);
    o->len += n;
}

static void put_u32(Out *o, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(o, b, 4);
}

static void put_u16(Out *o, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(o, b, 2);
}

static void put_string(Out *o, const char *s) {
    size_t n = s ? strlen(s) : 0;
    put_u32(o, (uint32_t)n);
    put(o, s, n);
}

uint8_t *petka_save_encode(const PetkaSave *save, size_t *out_len) {
    Out o = {0};
    put_u32(&o, save->part);
    put_u32(&o, save->chapter);
    put(&o, save->label, PETKA_SAVE_LABEL);
    put(&o, save->thumb, PETKA_SAVE_THUMB_BYTES);
    put_u32(&o, save->object_count + PETKA_SAVE_EXTRA_OBJECTS);
    for (uint32_t i = 0; i < save->object_count; i++) {
        const PetkaSaveObject *obj = &save->objects[i];
        put_string(&o, obj->name);
        put_string(&o, obj->shown);
        put_u32(&o, (uint32_t)obj->busy);
        put(&o, &obj->status, 1);
        put_u32(&o, (uint32_t)obj->resource);
        put_u32(&o, (uint32_t)obj->z);
        put_u32(&o, (uint32_t)obj->x);
        put_u32(&o, (uint32_t)obj->y);
        put_u32(&o, (uint32_t)obj->visible);
        put_u32(&o, (uint32_t)obj->active);
        put_u32(&o, (uint32_t)obj->animate);
    }
    put_u32(&o, save->item_count);
    for (uint32_t i = 0; i < save->item_count; i++) put_u16(&o, save->items[i]);
    put_string(&o, save->room);
    put_u32(&o, (uint32_t)save->petka_x);
    put_u32(&o, (uint32_t)save->petka_y);
    put_u32(&o, (uint32_t)save->chapayev_x);
    put_u32(&o, (uint32_t)save->chapayev_y);
    put_u32(&o, save->code_count);
    for (uint32_t i = 0; i < save->code_count; i++) put_u32(&o, save->code[i]);
    put_u32(&o, (uint32_t)save->cursor_resource);
    put_u32(&o, (uint32_t)save->cursor_action);
    put_u32(&o, (uint32_t)save->cursor_item);
    put_u32(&o, (uint32_t)save->petka_costume);
    put_u32(&o, (uint32_t)save->chapayev_costume);
    for (int i = 0; i < 8; i++) put_u32(&o, 0xFFFFFFFFu);
    if (o.failed) { free(o.data); return NULL; }
    *out_len = o.len;
    return o.data;
}

// ---- reading ----

typedef struct {
    const uint8_t *data;
    size_t len, at;
    int failed;
} In;

static const uint8_t *take(In *in, size_t n) {
    if (in->failed || n > in->len - in->at) { in->failed = 1; return NULL; }
    const uint8_t *p = in->data + in->at;
    in->at += n;
    return p;
}

static uint32_t get_u32(In *in) {
    const uint8_t *p = take(in, 4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}

static char *get_string(In *in) {
    uint32_t n = get_u32(in);
    const uint8_t *p = take(in, n);
    if (!p) return NULL;
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) { in->failed = 1; return NULL; }
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

static void get_head(In *in, PetkaSave *save) {
    save->part = get_u32(in);
    save->chapter = get_u32(in);
    const uint8_t *label = take(in, PETKA_SAVE_LABEL);
    if (label) {
        memcpy(save->label, label, PETKA_SAVE_LABEL);
        save->label[PETKA_SAVE_LABEL - 1] = 0;
    }
    const uint8_t *thumb = take(in, PETKA_SAVE_THUMB_BYTES);
    if (thumb) memcpy(save->thumb, thumb, PETKA_SAVE_THUMB_BYTES);
}

int petka_save_peek(const uint8_t *data, size_t len, PetkaSave *out_head) {
    In in = {data, len, 0, 0};
    memset(out_head, 0, sizeof(*out_head));
    get_head(&in, out_head);
    return in.failed ? -1 : 0;
}

PetkaSave *petka_save_decode(const uint8_t *data, size_t len) {
    In in = {data, len, 0, 0};
    PetkaSave *save = (PetkaSave *)calloc(1, sizeof(*save));
    if (!save) return NULL;
    get_head(&in, save);

    uint32_t total = get_u32(&in);
    // Every record is at least 41 bytes, which bounds a count gone wrong.
    if (in.failed || total < PETKA_SAVE_EXTRA_OBJECTS || total - PETKA_SAVE_EXTRA_OBJECTS > (len - in.at) / 41) {
        petka_save_free(save);
        return NULL;
    }
    save->object_count = total - PETKA_SAVE_EXTRA_OBJECTS;
    save->objects = (PetkaSaveObject *)calloc(save->object_count ? save->object_count : 1, sizeof(PetkaSaveObject));
    if (!save->objects) { petka_save_free(save); return NULL; }
    for (uint32_t i = 0; i < save->object_count && !in.failed; i++) {
        PetkaSaveObject *obj = &save->objects[i];
        obj->name = get_string(&in);
        obj->shown = get_string(&in);
        obj->busy = (int32_t)get_u32(&in);
        const uint8_t *status = take(&in, 1);
        obj->status = status ? *status : 0;
        obj->resource = (int32_t)get_u32(&in);
        obj->z = (int32_t)get_u32(&in);
        obj->x = (int32_t)get_u32(&in);
        obj->y = (int32_t)get_u32(&in);
        obj->visible = (int32_t)get_u32(&in);
        obj->active = (int32_t)get_u32(&in);
        obj->animate = (int32_t)get_u32(&in);
    }

    save->item_count = get_u32(&in);
    if (in.failed || save->item_count > (len - in.at) / 2) { petka_save_free(save); return NULL; }
    save->items = (uint16_t *)calloc(save->item_count ? save->item_count : 1, sizeof(uint16_t));
    if (!save->items) { petka_save_free(save); return NULL; }
    for (uint32_t i = 0; i < save->item_count; i++) {
        const uint8_t *p = take(&in, 2);
        save->items[i] = p ? (uint16_t)(p[0] | p[1] << 8) : 0;
    }

    save->room = get_string(&in);
    save->petka_x = (int32_t)get_u32(&in);
    save->petka_y = (int32_t)get_u32(&in);
    save->chapayev_x = (int32_t)get_u32(&in);
    save->chapayev_y = (int32_t)get_u32(&in);

    save->code_count = get_u32(&in);
    if (in.failed || save->code_count > (len - in.at) / 4) { petka_save_free(save); return NULL; }
    save->code = (uint32_t *)calloc(save->code_count ? save->code_count : 1, sizeof(uint32_t));
    if (!save->code) { petka_save_free(save); return NULL; }
    for (uint32_t i = 0; i < save->code_count; i++) save->code[i] = get_u32(&in);

    save->cursor_resource = (int32_t)get_u32(&in);
    save->cursor_action = (int32_t)get_u32(&in);
    save->cursor_item = (int32_t)get_u32(&in);
    save->petka_costume = (int32_t)get_u32(&in);
    save->chapayev_costume = (int32_t)get_u32(&in);
    // The eight spare words after this are not looked at.
    if (in.failed) { petka_save_free(save); return NULL; }
    return save;
}

void petka_save_free(PetkaSave *save) {
    if (!save) return;
    for (uint32_t i = 0; save->objects && i < save->object_count; i++) {
        free(save->objects[i].name);
        free(save->objects[i].shown);
    }
    free(save->objects);
    free(save->items);
    free(save->room);
    free(save->code);
    free(save);
}
