/* fieldlog.h — the machine at the end of a field, as one line of text
 * (design.md §15.5, EL §13.3).
 *
 * The board's first fields are held to the host's: the registers, the
 * cycle and instruction counts and the tick count at the end of each of
 * the first COCO_FIELD_LOG_MAX fields, on the board and on the host, the
 * same loop on each, must give the same lines. The board takes a record
 * a field and prints them once taken, so the log's pace cannot change
 * the guest's; tools/coco-fields.c prints the host's.
 */
#ifndef PICO_COCO_FIELDLOG_H
#define PICO_COCO_FIELDLOG_H

#include <stddef.h>
#include <stdint.h>

#include "coco.h"

typedef struct {
    uint32_t field;
    uint16_t pc, x, y, u, s;
    uint8_t  a, b, dp, cc;
    uint64_t cycles, insns, ticks;
} fieldlog_t;

/* The machine as it is now, after coco_run_field. */
void fieldlog_take(fieldlog_t *r, const coco_t *m);

/* `field 1 pc=A1C1 a=00 b=00 x=0000 y=0000 u=0000 s=03D7 dp=00 cc=D4
 * cyc=14938 ins=3402 t=238944`, with a NUL, in at most COCO_FIELD_LOG_LINE
 * bytes. Returns the length. */
int fieldlog_format(const fieldlog_t *r, char *out, size_t n);

#endif /* PICO_COCO_FIELDLOG_H */
