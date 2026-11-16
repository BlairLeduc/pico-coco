/* status.h — what the perf line says (design.md §7.5, §9.6).
 *
 * The perf line is one row of text above the picture, for watching; the
 * heartbeat is for measuring (EL §12). Its figures are host counters,
 * not guest state: core 0 writes them once a second as whole 32-bit
 * words, each single-copy atomic, and core 1 reads them. A read across a
 * write can mix two seconds, which shows for a second and is harmless.
 *
 * Copied from pico-oric and adapted: its line as it stood before audio,
 * with the gaps that close to one space from its later version. The
 * underrun counts return with audio (M8), and the status line's own
 * text, the tape and the disc, with M9 and M13. It is drawn in the VDG's
 * character set, which has no lower case (§9.6), so the line reads in
 * capitals on the panel.
 */
#ifndef PICO_COCO_STATUS_H
#define PICO_COCO_STATUS_H

#include <stdint.h>

#include "config.h"

typedef struct {
    uint32_t busy1000;      /* core 0 outside the pacing wait, thousandths */
    uint32_t head100;       /* times real time it would run unpaced,
                               in hundredths                              */
    uint32_t present_us;    /* the longest present in the second (§9.3)   */
    uint32_t dropped;       /* snapshots dropped in the second            */
    uint32_t hz;            /* the field rate, 60 on the CoCo (§13.1)     */
} perf_line_t;

/* The perf line's text, COCO_TEXT_COLS characters space-padded and a
 * NUL: `C0 31% 3.20x  LCD 12.6ms  Drop 0  60Hz`. A figure too wide is
 * shown at its widest rather than pushing the rest off, and the gaps
 * close to one space when the figures have grown, so the last never
 * goes. */
void status_perf_format(const perf_line_t *p, char out[COCO_TEXT_COLS + 1]);

#endif /* PICO_COCO_STATUS_H */
