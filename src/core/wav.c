/* wav.c — a WAVE file's PCM (wav.h). */

#include "wav.h"

#include <stdbool.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

const char *wav_header(const uint8_t *buf, size_t len, uint32_t file_len, wav_info_t *w) {
    memset(w, 0, sizeof *w);
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return "not a WAVE file";
    bool fmt = false;
    size_t at = 12;
    while (at + 8 <= len) {
        uint32_t size = le32(buf + at + 4);
        const uint8_t *body = buf + at + 8;
        if (memcmp(buf + at, "fmt ", 4) == 0) {
            if (size < 16 || at + 8 + 16 > len) return "a short fmt chunk";
            if (le16(body) != 1) return "not PCM";
            w->channels = le16(body + 2);
            w->rate = le32(body + 4);
            w->bits = le16(body + 14);
            if (w->channels < 1 || w->channels > 2) return "not mono or stereo";
            if (w->bits != 8 && w->bits != 16) return "not 8 or 16 bits";
            if (w->rate < 8000u || w->rate > 96000u) return "not 8 to 96 kHz";
            fmt = true;
        } else if (memcmp(buf + at, "data", 4) == 0) {
            if (!fmt) return "data before fmt";
            w->data_at = (uint32_t)(at + 8);
            uint32_t room = file_len > w->data_at ? file_len - w->data_at : 0;
            w->data_len = size < room ? size : room;
            w->data_len -= w->data_len % wav_frame_bytes(w);
            if (w->data_len == 0) return "no samples";
            return NULL;
        }
        if (size > len) break;   /* a chunk that runs past what was read */
        at += 8u + size + (size & 1u);
    }
    return fmt ? "no data chunk near the start" : "no fmt chunk near the start";
}

size_t wav_mono16(const wav_info_t *w, const uint8_t *src, size_t n, int16_t *dst, size_t max) {
    unsigned fb = wav_frame_bytes(w);
    size_t frames = fb ? n / fb : 0, out = 0;
    for (size_t i = 0; i < frames && out < max; i++, src += fb) {
        int32_t s;
        if (w->bits == 8) {
            s = ((int32_t)src[0] - 128) * 256;
            if (w->channels == 2) s = (s + ((int32_t)src[1] - 128) * 256) / 2;
        } else {
            s = (int16_t)le16(src);
            if (w->channels == 2) s = (s + (int16_t)le16(src + 2)) / 2;
        }
        dst[out++] = (int16_t)s;
    }
    return out;
}
