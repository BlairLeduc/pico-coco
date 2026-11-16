/* mc6809.h — Motorola MC6809E interpreter (design.md §7).
 *
 * Cycle-correct at instruction granularity (§7.1): every documented opcode
 * in every addressing mode takes its datasheet count, the indexed
 * postbyte's extra cycles and long branches taken or not included, and the
 * condition codes are exact. Held to XRoar's mc6809.c instruction by
 * instruction by test_mc6809_oracle (D2, §7.4).
 *
 * The CPU has no notion of a device. It reaches memory through bus.h, and
 * the bus is what brings devices up to date (§8).
 */
#ifndef PICO_COCO_MC6809_H
#define PICO_COCO_MC6809_H

#include <stdbool.h>
#include <stdint.h>

struct coco_s;

/* Condition code bits. */
#define MC6809_CC_C 0x01u
#define MC6809_CC_V 0x02u
#define MC6809_CC_Z 0x04u
#define MC6809_CC_N 0x08u
#define MC6809_CC_I 0x10u
#define MC6809_CC_H 0x20u
#define MC6809_CC_F 0x40u
#define MC6809_CC_E 0x80u

/* Vectors. */
#define MC6809_VEC_SWI3  0xFFF2u
#define MC6809_VEC_SWI2  0xFFF4u
#define MC6809_VEC_FIRQ  0xFFF6u
#define MC6809_VEC_IRQ   0xFFF8u
#define MC6809_VEC_SWI   0xFFFAu
#define MC6809_VEC_NMI   0xFFFCu
#define MC6809_VEC_RESET 0xFFFEu

/* What the CPU is doing between instructions (wait). */
#define MC6809_RUNNING 0u
#define MC6809_SYNC    1u   /* waiting in SYNC for any interrupt line    */
#define MC6809_CWAI    2u   /* stacked by CWAI, waiting for an unmasked one */
#define MC6809_HCF     3u   /* locked by $14, $15 or $CD until RESET (D12) */

/* Interrupt lines as the wait loop samples them (latch, active). */
#define MC6809_LINE_IRQ  0x01u
#define MC6809_LINE_FIRQ 0x02u
#define MC6809_LINE_NMI  0x04u

typedef struct {
    uint16_t pc, x, y, u, s;
    uint8_t  a, b, dp, cc;

    uint64_t cycles;           /* E cycles since power-on                  */
    uint64_t instructions;     /* executed, trapped ones included; not
                                * interrupt entries or wait cycles         */

    /* IRQ and FIRQ are levels, each a bitmask of asserted sources so they
     * compose (EL §3.2); NMI is an edge, latched, and not taken until S
     * has been loaded since reset (§7.1). The machine names the sources
     * in M3. */
    uint8_t  irq, firq;
    bool     nmi_line;         /* last level seen, for the edge            */
    bool     nmi_pending;
    bool     nmi_armed;
    bool     halt;             /* the HALT line (§7.1)                      */

    /* SYNC and CWAI. The wait polls the lines once a cycle, through a
     * one-cycle latch as the chip's inputs are (latch); CWAI dispatches on
     * what the poll before saw (active). Outside a wait neither is used:
     * lines change only between run slices, so at an instruction boundary
     * the current lines are what the last cycle latched. */
    uint8_t  wait;
    uint8_t  latch, active;

    /* After an interrupt entry or the end of SYNC, the next boundary
     * decides on the lines latched before it (active), not on the lines
     * now: the chip updates what it acts on only as an instruction ends,
     * so a line raised during an entry waits for the handler's first
     * instruction. */
    bool     stale;

    /* Nonzero when the step must look before it fetches: an interrupt line
     * asserted, an NMI pending, HALT, a wait, or stale. Kept by
     * mc6809_attn(). */
    uint8_t  attn;

    /* The cycle of the current instruction, from 1, on which a data access
     * that took the slow path falls; 0 for a fetch, the stack or a vector.
     * Written only on the slow path, so that a PIA can be brought up to
     * that cycle first (§7.2, EL §4.3). */
    uint8_t  io_at;

    /* The current run's length in cycles; mc6809_stop zeroes it. */
    uint32_t run_limit;

    /* Undocumented opcodes and indexed postbytes: executed as the NMOS
     * chip executes them, XRoar's model held by the oracle, and counted
     * (D12, §7.1; the owner, 2026-11-16, from M11's archive run). */
    uint32_t undoc_count;
    uint16_t undoc_pc;         /* the most recent one's instruction        */
    uint16_t undoc_op;         /* its opcode, $10xx and $11xx for pages 2, 3 */
} mc6809_t;

/* Power-on: every register zero, as XRoar measured the chip (§13.3). */
void     mc6809_init(mc6809_t *c);

/* The RESET line's effect: DP zero, F and I set, NMI disarmed, any wait
 * ended, and PC from $FFFE. Other registers are left alone. */
void     mc6809_reset(struct coco_s *m);

/* One instruction, one interrupt entry, or one cycle of a wait or HALT.
 * Returns the cycles taken. */
uint32_t mc6809_step(struct coco_s *m);

/* Whole instructions until at least `cycles` have passed, or sooner if
 * the machine calls mc6809_stop during one; returns the true count, and
 * the caller carries the debt (§6, EL §2.2). A wait or HALT that nothing
 * can end before the slice does is skipped in one go: lines change only
 * between slices (§7.1, EL §3.4). */
uint32_t mc6809_run(struct coco_s *m, uint32_t cycles);

/* End the run after the current instruction: something the slice's length
 * was computed from has changed (the SAM's rate, a PIA's interrupt
 * enables, an interrupt now due; coco.c, §6). */
static inline void mc6809_stop(mc6809_t *c) { c->run_limit = 0; }

/* Will the next step fetch an instruction? False when it will take an
 * interrupt, wait or halt instead: the boundary's own decision, on the
 * lines latched before it where the chip acts on those (stale). For a
 * breakpoint, which fires only before a fetch (tools/trace). */
bool     mc6809_fetches_next(const mc6809_t *c);

/* Interrupt lines. IRQ and FIRQ are level-sensitive per source; NMI is
 * edge-triggered: a false-to-true transition latches. */
void     mc6809_set_irq(mc6809_t *c, uint8_t source, bool asserted);
void     mc6809_set_firq(mc6809_t *c, uint8_t source, bool asserted);
void     mc6809_set_nmi(mc6809_t *c, bool level);
void     mc6809_set_halt(mc6809_t *c, bool asserted);

/* Recompute attn after writing a field it depends on directly. */
void     mc6809_attn(mc6809_t *c);

/* The line changes about to be made fall in the current instruction's last
 * cycle, after the CPU has latched what it acts on: the next boundary
 * decides on the lines as they are now (§7.1). For a device on the slow
 * path, when io_at says the access is the last cycle. */
void     mc6809_lines_late(mc6809_t *c);

/* io_at's top bit: the access is its instruction's last cycle. */
#define MC6809_IO_LAST 0x80u

/* Whether an opcode is documented, as $00xx, $10xx or $11xx. The oracle
 * enumerates what it holds the core to from this. */
bool     mc6809_documented(uint16_t op);

#endif /* PICO_COCO_MC6809_H */
