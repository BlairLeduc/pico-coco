/* hardware/timer.h — sdk_sim's stand-in: the microsecond timer, which
 * only the gap's report reads (audio.h), held at 0. */
#ifndef PICO_COCO_SIM_TIMER_H
#define PICO_COCO_SIM_TIMER_H

#include <stdint.h>

static inline uint32_t time_us_32(void) { return 0; }

#endif
