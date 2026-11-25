/* audio.h — PWM slice, chained DMA, and the PCM queue (design.md §10;
 * EL §6.2), pico-oric's, renamed.
 *
 * Core 0 owns all of it: the field loop pushes the guest's samples in
 * thread context and DMA_IRQ_0, also on core 0, drains them into the
 * ring. The queue is single-producer single-consumer between those two.
 *
 * Pushing blocks while the queue is full, and that is the emulator's
 * throttle (EL §6.3): the PWM wrap is a hardware clock derived from
 * clk_sys, and pacing on it holds the queue at depth without a
 * wall-clock timer.
 */
#ifndef PICO_COCO_AUDIO_H
#define PICO_COCO_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Claim the slice for GP26/GP27 and two DMA channels, and start the ring
 * playing silence. Call once, after board_init_clocks and after anything
 * that claims fixed DMA channels (the LCD claims channel 0). The carrier
 * is computed from clock_get_hz(clk_sys), never assumed; clk_sys is set
 * once at boot, so there is no retune path yet — a clock change needs a
 * stop, retune and restart (hardware-notes.md §3, §5.2). */
void audio_init(void);

/* The sample rate as the exact fraction rate_num / rate_den Hz, for
 * coco_audio_set_rate (design.md §10). */
void audio_rate(uint32_t *rate_num, uint32_t *rate_den);

/* Queue signed 16-bit mono, blocking while the queue is full. Playback
 * starts once COCO_PCM_QUEUE_START samples are waiting. */
void audio_push(const int16_t *pcm, size_t n);

/* The same for n stereo pairs, left then right, 2 x n values: each ear
 * its own channel of the slice (hardware-notes.md §5.1). */
void audio_push_stereo(const int16_t *lr, size_t n);

/* Samples the queue can take now without blocking. Turbo (design.md
 * §12.3, M12) will top the queue up with silence to this, so an
 * unpaced guest never waits on it and the ring never runs dry. */
size_t audio_room(void);

/* 0..256. Muted samples are still queued and consumed, so muting does
 * not change the guest's timing (hardware-notes.md §5.8). */
void audio_set_volume(unsigned volume);
void audio_set_muted(bool muted);

typedef struct {
    uint32_t underrun_samples;  /* the queue was empty when DMA wanted one */
    uint32_t late_refills;      /* the IRQ found its half already replaying */
    uint32_t consumed;          /* samples taken off the queue, or padded   */
    uint32_t level;             /* queue depth now                          */
    uint32_t low_water;         /* lowest depth since the last reset        */
    bool     started;
} audio_stats_t;

/* Two counters, not one (hardware-notes.md §5.8): an underrun is the
 * producer's failure, a late refill the consumer's. */
void audio_stats(audio_stats_t *st, bool reset_low_water);

/* Where core 0's thread is, for the gap's report: the field loop and
 * the park set it as they go, the refill IRQ reads it when the queue
 * first runs dry. */
enum {
    AUDIO_AT_LOOP, AUDIO_AT_KEYS, AUDIO_AT_RUN, AUDIO_AT_SNAPSHOT, AUDIO_AT_DRAIN,
    AUDIO_AT_PUSH, AUDIO_AT_PARK, AUDIO_AT_PARKED, AUDIO_AT_APPLY, AUDIO_AT_LOG,
    AUDIO_AT_COUNT
};
void audio_at(uint32_t at);

/* One stretch the queue ran dry, from its first empty slot to the first
 * sample after it. */
typedef struct {
    uint32_t start_us;      /* time_us_32 at the first empty half         */
    uint32_t samples;       /* empty slots in it                          */
    uint32_t at;            /* AUDIO_AT_ where core 0 was as it began     */
    uint32_t at_us;         /* how long core 0 had been there by then     */
    uint32_t push_us;       /* since the last audio_push returned         */
} audio_gap_t;

/* The last stretch that has ended, once: true and *g filled. */
bool audio_gap(audio_gap_t *g);

#endif /* PICO_COCO_AUDIO_H */
