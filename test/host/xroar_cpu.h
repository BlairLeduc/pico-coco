/* xroar_cpu.h — XRoar's MC6809 behind a plain interface, for the oracle
 * (design.md §7.4, §15.2).
 *
 * xroar_cpu.c is the only file that includes XRoar's headers; it is
 * compiled against the pinned checkout tools/trace/build-xroar.sh makes,
 * and linked only into test_mc6809_oracle (D2, D8). Nothing of XRoar is in
 * this tree.
 *
 * XRoar's CPU calls back once per bus cycle, dead cycles included, so the
 * number of callbacks is the cycle count. It runs as a state machine until
 * told to stop; this driver stops it at the end of each instruction (the
 * posthook) or when a cycle budget is reached.
 */
#ifndef PICO_COCO_XROAR_CPU_H
#define PICO_COCO_XROAR_CPU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  cc, a, b, dp;
    uint16_t x, y, u, s, pc;
    bool     nmi_armed;
} xr_regs_t;

typedef struct {
    uint8_t (*read)(uint16_t a);
    void    (*write)(uint16_t a, uint8_t v);
    /* After each bus cycle, numbered from 1 since xr_load; may call
     * xr_lines. A line set here is latched by the next cycle. */
    void    (*cycle)(unsigned long n);
    /* At the end of each instruction, and when SYNC ends. */
    void    (*boundary)(unsigned long n);
} xr_hooks_t;

void          xr_open(const xr_hooks_t *h);

/* Registers at an instruction boundary, with lines that have been
 * asserted since before it (already latched). NMI here is an edge
 * waiting to be taken. */
void          xr_load(const xr_regs_t *r, bool irq, bool firq, bool nmi);
void          xr_save(xr_regs_t *r);
void          xr_lines(bool irq, bool firq, bool nmi);

/* The HALT line, from the next cycle; may be called from the cycle hook. */
void          xr_halt(bool asserted);

/* The RESET sequence, from the next xr_run. */
void          xr_reset(void);

/* Run until the cycle count since xr_load reaches `budget`, or, if
 * `one`, until the end of one instruction. Returns the cycle count since
 * xr_load. */
unsigned long xr_run(unsigned long budget, bool one);

#endif /* PICO_COCO_XROAR_CPU_H */
