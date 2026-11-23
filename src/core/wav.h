/* wav.h — a RIFF WAVE file's PCM, read a piece at a time (design.md §10).
 *
 * For the drives' recorded sounds (drivesnd.h), which the card may hold
 * as the ROMs are held, never the tree. Only what a sound editor writes
 * by default is taken: uncompressed PCM, 8-bit unsigned or 16-bit
 * signed, mono or stereo, 8 to 96 kHz. Stereo is mixed to mono.
 *
 * The port reads the file's first bytes for wav_header, then its data
 * from data_at in pieces of whole frames (any multiple of 4 bytes is),
 * each through wav_mono16. No I/O, no allocation.
 */
#ifndef PICO_COCO_WAV_H
#define PICO_COCO_WAV_H

#include <stddef.h>
#include <stdint.h>

/* How much of the file's start wav_header looks through for the fmt and
 * data chunks: a LIST chunk an editor puts before them must end in it. */
#define WAV_HEAD_MAX 512u

typedef struct {
    uint32_t rate;          /* frames a second                       */
    uint16_t channels;      /* 1 or 2                                */
    uint16_t bits;          /* 8 or 16                               */
    uint32_t data_at;       /* the samples' offset in the file       */
    uint32_t data_len;      /* their bytes, as far as the file goes  */
} wav_info_t;

/* The file's first `len` bytes, of `file_len` in all: NULL and *w filled,
 * or why the file is not one taken. */
const char *wav_header(const uint8_t *buf, size_t len, uint32_t file_len, wav_info_t *w);

/* Bytes a frame. */
static inline unsigned wav_frame_bytes(const wav_info_t *w) {
    return (unsigned)w->channels * (w->bits / 8u);
}

/* The whole frames in src[0..n), as signed 16-bit mono, into dst, at
 * most max. Returns how many were written. */
size_t wav_mono16(const wav_info_t *w, const uint8_t *src, size_t n, int16_t *dst, size_t max);

#endif /* PICO_COCO_WAV_H */
