/* soundio.c — the drives' recorded sounds off the card (soundio.h). */

#include "soundio.h"

#include <stdio.h>

#include "ff.h"

#include "config.h"
#include "log.h"
#include "wav.h"

static int16_t            s_step[COCO_DRIVESND_STEP_MAX];
static int16_t            s_motor[COCO_DRIVESND_MOTOR_MAX];
static drivesnd_samples_t s_rec;
static bool               s_have;
static FIL                s_file;
static uint8_t            s_buf[WAV_HEAD_MAX];

/* One file into dst; its length in samples, 0 for none. */
static uint32_t load(const char *name, int16_t *dst, uint32_t max, uint32_t *inc) {
    char path[COCO_PATH_MAX];
    snprintf(path, sizeof path, COCO_DRIVESND_DIR "/%s", name);
    if (f_open(&s_file, path, FA_READ) != FR_OK) return 0;   /* not there: synthesised */
    UINT got = 0;
    uint32_t n = 0;
    wav_info_t w;
    const char *err = NULL;
    if (f_read(&s_file, s_buf, sizeof s_buf, &got) != FR_OK) err = "a read failed";
    if (!err) err = wav_header(s_buf, got, (uint32_t)f_size(&s_file), &w);
    if (!err && f_lseek(&s_file, w.data_at) != FR_OK) err = "a seek failed";
    for (uint32_t left = err ? 0 : w.data_len; left && n < max;) {
        UINT want = left < sizeof s_buf ? (UINT)left : (UINT)sizeof s_buf;
        if (f_read(&s_file, s_buf, want, &got) != FR_OK || got == 0) {
            err = "a read failed";
            break;
        }
        n += (uint32_t)wav_mono16(&w, s_buf, got, dst + n, max - n);
        left -= got;
    }
    f_close(&s_file);
    if (err) {
        log_core1("  sounds       : %s: %s; synthesised\n", path, err);
        return 0;
    }
    *inc = drivesnd_inc(w.rate);
    log_core1("  sounds       : %s, %u Hz, %u samples%s\n", path, (unsigned)w.rate, (unsigned)n,
              w.data_len / wav_frame_bytes(&w) > n ? ", cut short" : "");
    return n;
}

void soundio_load(void) {
    s_rec.step = s_step;
    s_rec.motor = s_motor;
    s_rec.step_len = load("step.wav", s_step, COCO_DRIVESND_STEP_MAX, &s_rec.step_inc);
    s_rec.motor_len = load("motor.wav", s_motor, COCO_DRIVESND_MOTOR_MAX, &s_rec.motor_inc);
    s_have = s_rec.step_len || s_rec.motor_len;
}

const drivesnd_samples_t *soundio_samples(void) {
    return s_have ? &s_rec : NULL;
}
