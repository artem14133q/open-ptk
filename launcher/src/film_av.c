// Films on the Switch: FFmpeg's libraries, built for it by switch/ffmpeg.sh
// with just the game's formats (AVI; Indeo 3 and Microsoft Video 1; MS ADPCM
// and PCM). The sound is decoded whole when the film is opened, into a WAV
// file in memory; the pictures are decoded as they are asked for, and
// handed out at a steady rate the way ffmpeg's fps filter would: each frame
// is the last one of the film due by then.
#ifdef __SWITCH__

#include "film.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Film {
    AVFormatContext *format;
    AVCodecContext *video;
    int stream;
    double time_base;
    AVPacket *packet;
    AVFrame *frame;        // the decoded frame waiting for its time
    int waiting;           // whether `frame` holds one
    int drained;           // the decoder has been told the film is over
    struct SwsContext *scaler;
    int width, height, fps;
    long handed;           // frames handed out so far
    long decoded;          // frames decoded so far, for ones without a time
    double rate;           // the film's own frame rate
    double last_time;      // when the frame on screen was due
    uint8_t *shown;        // the frame on screen, kept for when none is due
    int have_shown;
    uint8_t *sound;
    size_t sound_len;
};

// FFmpeg reads "name:" at the front of a path as a protocol; the Switch's
// paths start with the device, "sdmc:/...".
static void url_of(const char *path, char *out, size_t size) {
    snprintf(out, size, "file:%s", path);
}

static AVCodecContext *open_decoder(AVFormatContext *format, int stream) {
    const AVCodecParameters *par = format->streams[stream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) return NULL;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) return NULL;
    if (avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }
    return ctx;
}

static void put_u16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v) { put_u16(p, v & 0xFFFF); put_u16(p + 2, v >> 16); }

// The whole sound track as 16-bit PCM in a WAV file in memory (16-bit even
// where the film's is 8: raylib's streams play 8-bit sound badly).
static void decode_sound(Film *film, const char *url) {
    AVFormatContext *format = NULL;
    if (avformat_open_input(&format, url, NULL, NULL) < 0) return;
    AVCodecContext *audio = NULL;
    SwrContext *resampler = NULL;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    uint8_t *pcm = NULL;
    size_t len = 0, cap = 0;
    int stream = -1, channels = 0, rate = 0;
    if (avformat_find_stream_info(format, NULL) < 0) goto done;
    stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream < 0 || !packet || !frame) goto done;
    audio = open_decoder(format, stream);
    if (!audio) goto done;
    channels = audio->ch_layout.nb_channels > 1 ? 2 : 1;
    rate = audio->sample_rate;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, channels);
    if (swr_alloc_set_opts2(&resampler, &out_layout, AV_SAMPLE_FMT_S16, rate, &audio->ch_layout, audio->sample_fmt,
                            rate, 0, NULL) < 0 || swr_init(resampler) < 0)
        goto done;

    for (int flushing = 0;;) {
        if (!flushing) {
            int r = av_read_frame(format, packet);
            if (r < 0) { flushing = 1; avcodec_send_packet(audio, NULL); }
            else {
                if (packet->stream_index == stream) avcodec_send_packet(audio, packet);
                av_packet_unref(packet);
            }
        }
        int got = 0;
        while (avcodec_receive_frame(audio, frame) == 0) {
            got = 1;
            int room = swr_get_out_samples(resampler, frame->nb_samples);
            size_t need = len + (size_t)room * channels * 2;
            if (need > cap) {
                size_t grown = cap ? cap * 2 : 1 << 20;
                while (grown < need) grown *= 2;
                uint8_t *bigger = (uint8_t *)realloc(pcm, 44 + grown);
                if (!bigger) goto done;
                pcm = bigger;
                cap = grown;
            }
            uint8_t *out = pcm + 44 + len;
            int n = swr_convert(resampler, &out, room, (const uint8_t **)frame->extended_data, frame->nb_samples);
            if (n > 0) len += (size_t)n * channels * 2;
        }
        if (flushing && !got) break;
    }
    if (!pcm || len == 0) goto done;

    memcpy(pcm, "RIFF", 4);
    put_u32(pcm + 4, (uint32_t)(36 + len));
    memcpy(pcm + 8, "WAVEfmt ", 8);
    put_u32(pcm + 16, 16);
    put_u16(pcm + 20, 1); // PCM
    put_u16(pcm + 22, (unsigned)channels);
    put_u32(pcm + 24, (uint32_t)rate);
    put_u32(pcm + 28, (uint32_t)(rate * channels * 2));
    put_u16(pcm + 32, (unsigned)(channels * 2));
    put_u16(pcm + 34, 16);
    memcpy(pcm + 36, "data", 4);
    put_u32(pcm + 40, (uint32_t)len);
    film->sound = pcm;
    film->sound_len = 44 + len;
    pcm = NULL;

done:
    free(pcm);
    swr_free(&resampler);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&audio);
    avformat_close_input(&format);
}

Film *film_open(const char *path, int width, int height, int fps, const char **why) {
    char url[1300];
    url_of(path, url, sizeof(url));
    Film *film = (Film *)calloc(1, sizeof(Film));
    if (!film) { *why = "нет памяти"; return NULL; }
    film->width = width;
    film->height = height;
    film->fps = fps;
    *why = "не удалось открыть";
    if (avformat_open_input(&film->format, url, NULL, NULL) < 0) { film_close(film); return NULL; }
    if (avformat_find_stream_info(film->format, NULL) < 0) { film_close(film); return NULL; }
    film->stream = av_find_best_stream(film->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (film->stream < 0) { film_close(film); return NULL; }
    *why = "неизвестный формат";
    film->video = open_decoder(film->format, film->stream);
    if (!film->video) { film_close(film); return NULL; }
    AVStream *s = film->format->streams[film->stream];
    film->time_base = av_q2d(s->time_base);
    film->rate = s->avg_frame_rate.num > 0 ? av_q2d(s->avg_frame_rate) : (s->r_frame_rate.num > 0 ? av_q2d(s->r_frame_rate) : fps);
    film->packet = av_packet_alloc();
    film->frame = av_frame_alloc();
    film->shown = (uint8_t *)malloc((size_t)width * height * 4);
    if (!film->packet || !film->frame || !film->shown) { *why = "нет памяти"; film_close(film); return NULL; }
    decode_sound(film, url);
    return film;
}

// Decodes the next picture into film->frame; 0 at the end.
static int decode_next(Film *film) {
    for (;;) {
        int r = avcodec_receive_frame(film->video, film->frame);
        if (r == 0) { film->decoded++; return 1; }
        if (r != AVERROR(EAGAIN) || film->drained) return 0;
        for (;;) {
            if (av_read_frame(film->format, film->packet) < 0) {
                avcodec_send_packet(film->video, NULL);
                film->drained = 1;
                break;
            }
            int ours = film->packet->stream_index == film->stream;
            if (ours) avcodec_send_packet(film->video, film->packet);
            av_packet_unref(film->packet);
            if (ours) break;
        }
    }
}

// When the decoded frame is due, in seconds from the start.
static double frame_time(const Film *film) {
    int64_t pts = film->frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) return (double)(film->decoded - 1) / film->rate;
    return (double)pts * film->time_base;
}

static void show(Film *film) {
    AVFrame *f = film->frame;
    film->scaler = sws_getCachedContext(film->scaler, f->width, f->height, (enum AVPixelFormat)f->format, film->width,
                                        film->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!film->scaler) return;
    uint8_t *dst[4] = {film->shown, NULL, NULL, NULL};
    int stride[4] = {film->width * 4, 0, 0, 0};
    sws_scale(film->scaler, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, stride);
    // Paletted pictures carry the palette's alpha; the films are opaque.
    size_t n = (size_t)film->width * film->height;
    for (size_t i = 0; i < n; i++) film->shown[i * 4 + 3] = 255;
    film->have_shown = 1;
}

int film_next(Film *film, uint8_t *rgba) {
    double due = (double)film->handed / film->fps;
    // Half a frame of slack, as frame times are rounded.
    double slack = 0.5 / film->rate;
    for (;;) {
        if (!film->waiting) {
            if (!decode_next(film)) {
                // Over: the last picture stays up for its own frame's time.
                if (!film->have_shown || due > film->last_time + 1.0 / film->rate) return 0;
                break;
            }
            film->waiting = 1;
        }
        double t = frame_time(film);
        if (film->have_shown && t > due + slack) break;
        film->last_time = t;
        show(film);
        av_frame_unref(film->frame);
        film->waiting = 0;
    }
    memcpy(rgba, film->shown, (size_t)film->width * film->height * 4);
    film->handed++;
    return 1;
}

const uint8_t *film_sound(Film *film, size_t *len) {
    *len = film->sound_len;
    return film->sound;
}

void film_close(Film *film) {
    if (!film) return;
    sws_freeContext(film->scaler);
    av_frame_free(&film->frame);
    av_packet_free(&film->packet);
    avcodec_free_context(&film->video);
    avformat_close_input(&film->format);
    free(film->shown);
    free(film->sound);
    free(film);
}

#endif
