// visual.c
//
// EN: Clips on screen: FLIC frames to textures (only the part an object covers), drawing, the cursor, a clip's frame clock and hit test.
// RU: Клипы на экране: кадры FLIC в текстуры (только та часть, что занимает объект), отрисовка, курсор, часы кадров клипа и проверка попадания.
#include "game.h"

// The part of `flic`'s frame `area` in RGBA, in a buffer that is reused
// from call to call.
const uint8_t *flic_to_rgba(const PetkaFlic *flic, PetkaMskRect area) {
    static uint8_t *rgba;
    static size_t cap;
    int w = area.right - area.left, h = area.bottom - area.top;
    size_t need = (size_t)w * h * 4;
    if (need > cap) {
        free(rgba);
        rgba = (uint8_t *)malloc(need);
        cap = rgba ? need : 0;
        if (!rgba) return NULL;
    }
    // The colour key is palette entry 0's colour, not the entry itself.
    uint32_t colour[256];
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = flic->palette[i];
        uint8_t px[4] = {c[0], c[1], c[2], petka_flic_keyed(flic, (uint8_t)i) ? 0 : 255};
        memcpy(&colour[i], px, 4);
    }
    uint32_t *out = (uint32_t *)rgba;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = flic->indices + (size_t)(area.top + y) * flic->width + area.left;
        for (int x = 0; x < w; x++) *out++ = colour[row[x]];
    }
    return rgba;
}

// The part of a clip's frame its texture holds: the MSK's covering rect
// with a pixel to spare all round (the files give the bottom edge
// inclusive), or the whole frame when there is no usable rect -- two
// explosions' files have it upside down. Checked on every clip of the game:
// no opaque pixel of any frame falls outside. Most objects are small things
// in full-screen clips, so this spares most of the converting, uploading
// and drawing.
PetkaMskRect clip_area(const PetkaFlic *flic, PetkaMskRect r) {
    r.left -= 1; r.top -= 1; r.right += 1; r.bottom += 1;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > flic->width) r.right = flic->width;
    if (r.bottom > flic->height) r.bottom = flic->height;
    if (r.right <= r.left || r.bottom <= r.top) r = (PetkaMskRect){0, 0, flic->width, flic->height};
    return r;
}

// ---- visuals ----

void visual_free(Visual *v) {
    if (v->flic) petka_flic_close(v->flic);
    if (v->tex.id) UnloadTexture(v->tex);
    memset(v, 0, sizeof(*v));
    v->resource = PETKA_NONE;
}

void visual_load(Game *g, Visual *v, uint16_t resource) {
    visual_free(v);
    v->resource = resource;
    v->sound_pending = 1;
    visual_load_path(g, v, g->qrc[resource]);
}

void visual_load_path(Game *g, Visual *v, const char *path) {
    if (!path || !has_ext(path, ".FLC")) return;
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    v->flic = bytes ? petka_flic_open(bytes, len) : NULL;
    if (!v->flic) { fprintf(stderr, "clip not found: %s\n", path); return; }

    v->rect = (PetkaMskRect){0, 0, v->flic->width, v->flic->height};
    char msk[512];
    swap_ext(path, "MSK", msk, sizeof(msk));
    size_t mlen = 0;
    const uint8_t *mbytes = res_find(g, msk, &mlen);
    if (mbytes) petka_msk_rect(mbytes, mlen, &v->rect);
    v->msk = mbytes;
    v->msk_len = mlen;

    v->area = clip_area(v->flic, v->rect);
    const uint8_t *rgba = flic_to_rgba(v->flic, v->area);
    if (!rgba) return;
    Image img = {.data = (void *)rgba, .width = v->area.right - v->area.left, .height = v->area.bottom - v->area.top,
                 .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    v->tex = LoadTextureFromImage(img);
    v->next_frame = game_now() + (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
}

// Brings the texture up to the clip's current frame.
void visual_upload(Visual *v) {
    const uint8_t *rgba = v->tex.id ? flic_to_rgba(v->flic, v->area) : NULL;
    if (rgba) UpdateTexture(v->tex, rgba);
}

// Draws the clip's frame so that the whole frame would cover `frame` (on
// the screen): only the part the texture holds is drawn.
void visual_draw(const Visual *v, Rectangle frame) {
    if (!v->tex.id) return;
    float sx = frame.width / v->flic->width, sy = frame.height / v->flic->height;
    DrawTexturePro(v->tex, (Rectangle){0, 0, (float)v->tex.width, (float)v->tex.height},
                   (Rectangle){frame.x + v->area.left * sx, frame.y + v->area.top * sy, v->tex.width * sx,
                               v->tex.height * sy},
                   (Vector2){0, 0}, 0, WHITE);
}

// Draws `part` of a clip's frame in grey, as visual_draw() would put it:
// for what is shown but does nothing. Pixels `keep` says yes to (if given)
// are left out, in their colour underneath. The grey picture is made
// afresh from the frame each time, in `cache`.
void draw_grey(const Visual *v, PetkaMskRect part, Texture2D *cache, Rectangle frame,
                      int (*keep)(const uint8_t *rgba)) {
    if (part.left < v->area.left) part.left = v->area.left;
    if (part.top < v->area.top) part.top = v->area.top;
    if (part.right > v->area.right) part.right = v->area.right;
    if (part.bottom > v->area.bottom) part.bottom = v->area.bottom;
    int w = part.right - part.left, h = part.bottom - part.top;
    if (!v->flic || w <= 0 || h <= 0) return;
    const uint8_t *rgba = flic_to_rgba(v->flic, part);
    if (!rgba) return;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (!px) return;
    for (size_t k = 0; k < (size_t)w * h; k++) {
        const uint8_t *c = rgba + k * 4;
        uint8_t y = (uint8_t)((c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8);
        px[k * 4 + 0] = px[k * 4 + 1] = px[k * 4 + 2] = y;
        px[k * 4 + 3] = keep && keep(c) ? 0 : c[3];
    }
    if (cache->id && (cache->width != w || cache->height != h)) { UnloadTexture(*cache); *cache = (Texture2D){0}; }
    if (!cache->id) {
        Image img = {.data = px, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        *cache = LoadTextureFromImage(img);
    } else {
        UpdateTexture(*cache, px);
    }
    free(px);
    Visual shown = *v;
    shown.tex = *cache;
    shown.area = part;
    visual_draw(&shown, frame);
}
// Brings a clip that belongs to no script object (the cursor, the case and
// its buttons) to `resource`. With `run` it plays; without, it is held on
// its first frame -- a clip that has moved on is loaded afresh.
void clip_show(Game *g, Visual *v, uint16_t resource, int run) {
    int rewind = !run && v->flic && v->flic->frame_index != 0;
    if (v->resource != resource || rewind) visual_load(g, v, resource);
    if (!v->flic || !run || v->flic->frame_count <= 1 || game_now() < v->next_frame) return;
    v->next_frame = game_now() + (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
    petka_flic_advance(v->flic);
    visual_upload(v);
}

// `centre` is the last argument of the exe's cursor move: the room's mouse
// function passes 1, the map's 0 -- there the frame's corner is the point.
void draw_cursor(Game *g, uint16_t resource, int over_object, int centre, Vector2 mouse, float scale) {
    Visual *v = &g->cursor;
    clip_show(g, v, resource, over_object);
    if (!v->flic) return;
    int x = (int)(mouse.x / scale), y = (int)(mouse.y / scale);
    if (centre) {
        x -= v->rect.left + (v->rect.right - v->rect.left) / 2;
        y -= v->rect.top + (v->rect.bottom - v->rect.top) / 2;
    }
    visual_draw(v, (Rectangle){x * scale, y * scale, v->flic->width * scale, v->flic->height * scale});
}

// Brings one object's visual in line with its VM state and advances its
// animation, following the exe's object update (sub_4015E0):
//   - a clip of a single frame is never updated: no END, no sound;
//   - the clip's sound starts while its first frame is up;
//   - when the last frame comes up END is posted, on every loop, and the
//     sound is armed again if the object repeats it;
//   - when the middle frame comes up HALF is posted;
//   - the clip then simply goes round again.
// The heroes have their own update (sub_407BF0) that differs in two ways
// handled here: their sound is armed again on every loop regardless, and a
// clip marked rest_after_loop pauses once it is back on its first frame.
// (It also scales the walk clip's speed by an options slider and moves the
// hero one step per frame of that clip; neither is reproduced.)
// A clip's sound is the same path with a .WAV extension.
void visual_update(Game *g, const PetkaObject *obj) {
    Visual *v = &g->visuals[obj_index(g, obj)];
    PetkaObjectState *st = &g->vm->state[obj_index(g, obj)];
    // A hidden object that is still active is an invisible hot spot -- room
    // exits are made this way (SET a clip, then HIDE) -- so its clip is
    // needed for the pointer even though it is never drawn. The exe loads a
    // clip on the same condition (visible or active).
    if ((!st->visible && !st->active) || st->resource == PETKA_NONE) return;
    if (v->resource != st->resource || v->restart) {
        visual_load(g, v, st->resource);
        v->restart = 0;
        if (g->trace) fprintf(stderr, "clip: %u starts %u at %.3f\n", obj->id, st->resource, game_now());
    }
    if (!v->flic) return;
    v->live = 1;
    if (!st->animate || !st->visible) return;
    if (v->flic->frame_count <= 1) {
        // Ours, not the exe's: a script waiting on a one-frame clip would
        // wait forever, so that wait alone is ended.
        if (st->play_tail.present && st->play_wait == v->resource)
            petka_scriptvm_animation_ended(g->vm, obj->id, v->resource);
        return;
    }
    double now = game_now();
    if (now < v->next_frame) return;
    double period = (v->flic->speed_ms > 0 ? v->flic->speed_ms : 70) / 1000.0;
    // A walking hero's clock runs faster by the speed dial: the exe
    // multiplies the time that has passed by (percent + 50) / 50.
    if (is_hero(obj->id) && g->walk[obj->id].active) period *= 50.0 / (dial_percent(g, DIAL_SPEED) + 50.0);
    // The next frame is due a period after this one was, not after now:
    // counted from now, every frame came late by however long it waited for
    // the game's frame -- more for clips updated later in it, after others
    // were decoded -- so clips ran slow, and two started together drifted
    // apart (part 2's pentagram ended well after Petka's clip in the church,
    // and the sequence waiting on it stood still meanwhile). Behind, it
    // catches up a frame of its own a frame of the game -- so a stall of
    // the game holds back every clip alike -- unless it is far behind, after
    // a film or the save screen, when it goes on from now.
    v->next_frame += period;
    if (v->next_frame < now - 1.0) v->next_frame = now + period;

    if (v->sound_pending && v->flic->frame_index == 0) {
        char wav[512];
        swap_ext(g->qrc[v->resource], "WAV", wav, sizeof(wav));
        if (!g->clips_quiet) play_sound(g, wav);
        v->sound_pending = 0;
    }
    petka_flic_advance(v->flic);
    visual_upload(v);

    int frame = v->flic->frame_index + 1; // the exe counts frames from 1
    if (frame == v->flic->frame_count) {
        if (g->trace) fprintf(stderr, "clip: %u ends %u at %.3f\n", obj->id, v->resource, now);
        if (st->repeat_sound || is_hero(obj->id)) v->sound_pending = 1;
        petka_scriptvm_animation_ended(g->vm, obj->id, v->resource);
    }
    if (frame == v->flic->frame_count / 2)
        petka_scriptvm_post(g->vm, obj->id, PETKA_OP_HALF, v->resource, 0, 0, -1);
    if (is_hero(obj->id) && st->rest_after_loop && frame == 1) v->next_frame += HERO_REST_SECONDS;
    // A walking hero takes one step for each frame of his walk clip.
    if (is_hero(obj->id) && g->walk[obj->id].active) walk_frame(g, obj->id);
}

// A scene object's hit test (sub_4018C0): the point must lie in the clip's
// MSK rect, and it or any of its eight neighbours inside that rect must be
// opaque -- a pixel of slack all round, so thin shapes can be clicked.
int clip_hit(const Visual *v, int x, int y) {
    static const int kDx[9] = {0, -1, 0, 1, -1, 1, -1, 0, 1};
    static const int kDy[9] = {0, -1, -1, -1, 0, 0, 1, 1, 1};
    PetkaMskRect r = v->rect;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > v->flic->width) r.right = v->flic->width;
    if (r.bottom > v->flic->height) r.bottom = v->flic->height;
    if (x < r.left || y < r.top || x >= r.right || y >= r.bottom) return 0;
    for (int k = 0; k < 9; k++) {
        int px = x + kDx[k], py = y + kDy[k];
        if (px < r.left || py < r.top || px >= r.right || py >= r.bottom) continue;
        if (!petka_flic_keyed(v->flic, v->flic->indices[py * v->flic->width + px])) return 1;
    }
    return 0;
}
// The point of a clip's frame nearest `p` where a click hits it (clip_hit),
// found on rings round `p`; `p` itself if there is none.
Vector2 clip_point(const Visual *v, Vector2 p) {
    if (clip_hit(v, (int)p.x, (int)p.y)) return p;
    int reach = v->flic->width > v->flic->height ? v->flic->width : v->flic->height;
    for (int r = 1; r <= reach; r++) {
        int steps = r * 6 + 8;
        for (int k = 0; k < steps; k++) {
            float a = 6.2831853f * k / steps;
            Vector2 q = {p.x + r * cosf(a), p.y + r * sinf(a)};
            if (clip_hit(v, (int)q.x, (int)q.y)) return q;
        }
    }
    return p;
}

// Puts an object's clip on a given frame (counted from 1), for the clips
// whose frames are states rather than an animation.
void show_frame(Game *g, const PetkaObject *obj, int frame) {
    Visual *v = &g->visuals[obj - g->script->objects];
    if (!v->flic || frame < 1 || frame > v->flic->frame_count || v->flic->frame_index == frame - 1) return;
    for (int guard = 0; guard < v->flic->frame_count && v->flic->frame_index != frame - 1; guard++)
        petka_flic_advance(v->flic);
    visual_upload(v);
}
