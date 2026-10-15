// audio.c
//
// EN: Sound: the panel's dials, clip sounds, music and ambience streams, sounds held during sequences.
// RU: Звук: регуляторы панели, звуки клипов, потоки музыки и фона, звуки на время заставок.
#include "game.h"
const int kDialTop[4] = {31, 41, 31, 26};
const int kDialDefault[4] = {31, 20, 23, 13};

int dial_percent(const Game *g, int which) {
    return (g->dial[which] * 100 - 100) / (kDialTop[which] - 1);
}

// A volume dial as a gain. The exe (sub_40EB70) gives DirectSound
// -(10000 - 10000 * ln(2.75 * percent) / ln(275)) hundredths of a decibel:
// nothing off at 100, about 13 dB off at 47, and silence at 0.
float dial_gain(const Game *g, int which) {
    int percent = dial_percent(g, which);
    if (percent <= 0) return 0.0f;
    if (percent >= 100) return 1.0f;
    double centibels = -(10000.0 - 10000.0 * log(2.75 * percent) / log(275.0));
    return (float)pow(10.0, centibels / 2000.0);
}

void play_sound(Game *g, const char *path) {
    size_t len = 0;
    const uint8_t *bytes = res_find(g, path, &len);
    if (!bytes) return;
    // The exe's sound manager keeps one buffer per file (a map by name), so
    // a sound started again starts over; it is never heard twice at once.
    // Started anew each time, a looping clip's sound piled up into a loud
    // rasp (the bar in part 3), and a door clicked twice creaked twice.
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount <= 0 || g->sound_src[i] != bytes) continue;
        StopSound(g->sounds[i]);
        g->sound_paused[i] = 0;
        SetSoundVolume(g->sounds[i], dial_gain(g, DIAL_SFX));
        PlaySound(g->sounds[i]);
        return;
    }
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount > 0 && (IsSoundPlaying(g->sounds[i]) || g->sound_paused[i])) continue;
        if (g->sounds[i].frameCount > 0) UnloadSound(g->sounds[i]);
        Wave wave = LoadWaveFromMemory(".wav", bytes, (int)len);
        g->sounds[i] = LoadSoundFromWave(wave);
        g->sound_src[i] = bytes;
        UnloadWave(wave);
        SetSoundVolume(g->sounds[i], dial_gain(g, DIAL_SFX));
        PlaySound(g->sounds[i]);
        return;
    }
}

// raylib streams a WAV as 16- or 32-bit samples only: an 8-bit one comes out
// as noise -- a loud rasp. One track of the game is 8-bit (part 3's bar,
// MUSIC3\SPACEBAR.wav, stereo at 11025 Hz). Such a file is turned into a
// 16-bit one for the stream, once; the copy is kept, as the stream reads
// from it while it plays.
const uint8_t *wav_for_stream(const uint8_t *bytes, size_t len, size_t *out_len) {
    static struct { const uint8_t *from; uint8_t *to; size_t len; } cache[8];
    *out_len = len;
    for (int i = 0; i < 8; i++)
        if (cache[i].from == bytes) { *out_len = cache[i].len; return cache[i].to; }
    if (len < 12 || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4)) return bytes;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *data = NULL;
    uint32_t data_len = 0;
    for (size_t o = 12; o + 8 <= len;) {
        uint32_t n = (uint32_t)bytes[o + 4] | (uint32_t)bytes[o + 5] << 8 | (uint32_t)bytes[o + 6] << 16 | (uint32_t)bytes[o + 7] << 24;
        if (!memcmp(bytes + o, "fmt ", 4) && n >= 16 && o + 8 + 16 <= len) {
            const uint8_t *f = bytes + o + 8;
            format = (uint16_t)(f[0] | f[1] << 8);
            channels = (uint16_t)(f[2] | f[3] << 8);
            rate = (uint32_t)f[4] | (uint32_t)f[5] << 8 | (uint32_t)f[6] << 16 | (uint32_t)f[7] << 24;
            bits = (uint16_t)(f[14] | f[15] << 8);
        } else if (!memcmp(bytes + o, "data", 4)) {
            data = bytes + o + 8;
            data_len = n <= len - (o + 8) ? n : (uint32_t)(len - (o + 8));
            break;
        }
        o += 8 + (size_t)n + (n & 1);
    }
    if (format != 1 || bits != 8 || !data || !channels) return bytes;
    size_t total = 44 + (size_t)data_len * 2;
    uint8_t *out = (uint8_t *)malloc(total);
    if (!out) return bytes;
    uint32_t block = channels * 2u, byte_rate = rate * block, body = data_len * 2u;
    uint8_t head[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0};
    uint32_t riff = (uint32_t)total - 8;
    memcpy(head + 4, &riff, 4); // little-endian host
    memcpy(head + 22, &channels, 2);
    memcpy(head + 24, &rate, 4);
    memcpy(head + 28, &byte_rate, 4);
    uint16_t block16 = (uint16_t)block, bits16 = 16;
    memcpy(head + 32, &block16, 2);
    memcpy(head + 34, &bits16, 2);
    memcpy(head + 36, "data", 4);
    memcpy(head + 40, &body, 4);
    memcpy(out, head, 44);
    for (uint32_t i = 0; i < data_len; i++) {
        int16_t s = (int16_t)(((int)data[i] - 128) << 8); // 8-bit WAV samples are unsigned
        memcpy(out + 44 + i * 2, &s, 2);
    }
    for (int i = 0; i < 8; i++)
        if (!cache[i].from) {
            cache[i].from = bytes;
            cache[i].to = out;
            cache[i].len = total;
            break;
        }
    *out_len = total;
    return out;
}

// Room music and ambience keep playing across rooms that share a track.
void set_stream(Game *g, Music *stream, uint16_t *current, uint16_t id) {
    if (*current == id) return;
    if (stream->frameCount > 0) { StopMusicStream(*stream); UnloadMusicStream(*stream); *stream = (Music){0}; }
    *current = id;
    const char *path = id != PETKA_NONE ? g->qrc[id] : NULL;
    size_t len = 0;
    const uint8_t *bytes = path ? res_find(g, path, &len) : NULL;
    if (!bytes) return;
    bytes = wav_for_stream(bytes, len, &len);
    *stream = LoadMusicStreamFromMemory(".wav", bytes, (int)len);
    stream->looping = true;
    PlayMusicStream(*stream);
}

// What a sequence does to the sounds that are playing (the exe's sound
// manager, sub_40E2C0 and sub_40E3F0, called from the sequence screen's
// set-up and leave): on the way in everything playing is stopped where it is
// and marked; on the way out what the sequence itself started is stopped
// and what was marked plays on.
void sequence_sounds(Game *g, int entering) {
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount <= 0) { g->sound_paused[i] = 0; continue; }
        if (g->trace) fprintf(stderr, "seq sounds: %s sound %d (%u frames, paused %d)\n", entering ? "in" : "out", i,
                              g->sounds[i].frameCount, g->sound_paused[i]);
        if (entering) {
            if (!IsSoundPlaying(g->sounds[i]) || g->sound_paused[i]) continue;
            // One sequence straight after another: the first one's sounds go.
            if (g->seq_room != PETKA_NONE) StopSound(g->sounds[i]);
            else { PauseSound(g->sounds[i]); g->sound_paused[i] = 1; }
        } else if (g->sound_paused[i]) {
            ResumeSound(g->sounds[i]);
            g->sound_paused[i] = 0;
        } else if (IsSoundPlaying(g->sounds[i])) {
            StopSound(g->sounds[i]);
        }
    }
    if (g->voice.frameCount <= 0) { g->voice_paused = 0; return; }
    if (g->trace) fprintf(stderr, "seq sounds: %s voice (paused %d)\n", entering ? "in" : "out", g->voice_paused);
    if (entering && g->seq_room == PETKA_NONE && IsSoundPlaying(g->voice)) {
        PauseSound(g->voice);
        g->voice_paused = 1;
    } else if (!entering && g->voice_paused) {
        ResumeSound(g->voice);
        g->voice_paused = 0;
    }
}
