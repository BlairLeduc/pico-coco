/* coco.h — the machine (design.md §6, §8).
 *
 * The CPU, the page table it reaches memory through, the SAM, the two
 * PIAs, RAM, the ROMs and the keyboard matrix, and the field loop that
 * runs them in master-oscillator ticks (§8.2, §13). The VDG arrives in M4,
 * audio in M8, the card and cartridges in M9.
 *
 * The bus's slow path is bus.c's, and it decodes this machine. A host test
 * that runs the CPU alone on its own memory (test_mc6809_*, the bench)
 * supplies bus_read_slow and bus_write_slow itself and uses none of the
 * machine's functions, so bus.c is never linked into it.
 *
 * Load the ROMs, then power on (§13.3): coco_init, coco_load_rom,
 * coco_power_on, in that order, which the host harness shares.
 */
#ifndef PICO_COCO_COCO_H
#define PICO_COCO_COCO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "mc6809.h"
#include "mc6821.h"
#include "sam6883.h"

/* Exactly two pointers, as the siblings' (EL §4.1). A NULL read pointer
 * sends the access to the slow path; a NULL write pointer sends a write
 * there, which is how ROM and I/O are told from RAM. */
typedef struct {
    const uint8_t *read;
    uint8_t       *write;
} page_t;

typedef enum {
    COCO_MACHINE_COCO = 0,     /* the CoCo 1 and 2 (D6)                    */
    COCO_MACHINE_DRAGON32 = 1, /* the second wiring: PAL, on the host from M3 */
} coco_machine_t;

typedef struct {
    coco_machine_t machine;
    unsigned       ram_k;      /* 4, 16, 32 or 64                          */
} coco_config_t;

/* The CPU's interrupt sources (mc6809.h). */
#define COCO_IRQ_PIA0   0x01u
#define COCO_FIRQ_PIA1  0x01u

/* Keyboard matrix: eight columns, seven rows (§11.2). */
#define COCO_KEY_COLS   8
#define COCO_KEY_ROWS   7

typedef struct coco_s {
    mc6809_t cpu;
    page_t   page[COCO_PAGE_COUNT];

    coco_config_t cfg;
    sam6883_t sam;
    mc6821_t  pia0, pia1;

    /* Pressed keys: per column, a bit per row. */
    uint8_t  keys[COCO_KEY_COLS];

    /* Time, in master-oscillator ticks (§13.2): the tick count at the CPU
     * cycle count cyc0, and the ticks a cycle takes from there. */
    uint64_t tick0, cyc0;
    uint32_t tpc;
    uint64_t field_start;      /* the tick the current field began, modulo
                                * 2^64: before tick 0 on a PAL machine,
                                * whose first field is short (coco.c)    */
    uint64_t hs_seen;          /* HS edges are accounted up to here     */
    uint64_t irq_due[4];       /* PIA0 A, B, PIA1 A, B: an output's rise */
    uint32_t fields;
    bool     sound_fed;        /* the sound has fed PIA1 PB1 back (coco.c) */

    /* The field's shape, computed at power-on (§13.1, §18: medium). */
    uint32_t line_ticks;       /* 57 E cycles of 16 ticks               */
    uint32_t field_lines;      /* 262 on NTSC, 312 on PAL               */
    uint32_t fs_fall_line;     /* FS falls as this line begins          */
    uint32_t field_lead;       /* PAL: lines the first field lacks      */
    uint32_t hs_low_ticks;     /* HS is low this long from a line's start */

    /* RAM: the chips' organisation, from the size (§8.1). */
    uint8_t  ram_addr_bits;    /* address bits of one chip              */
    uint8_t  ram_row_bits;
    uint8_t  ram_banks;        /* banks fitted                          */

    bool     rom_present[2];   /* the $8000 and $A000 halves            */
    bool     cart_present;     /* M9: cartridges                        */

    /* The trace driver's hook (tools/trace/coco-trace.c): when set, each
     * run slice is one step, an instruction, an interrupt's entry or a
     * cycle of a wait, and the hook is called after it. Tested once a
     * slice, not once an instruction. NULL in the emulator. */
    void   (*step_hook)(struct coco_s *m);
    void    *hook_ctx;

    uint8_t  ram[COCO_ADDR_SPACE];
    uint8_t  rom[0x4000];      /* $8000-$BFFF                           */
    uint8_t  cart[0x4000];     /* $C000-$FEFF                           */
} coco_t;

/* A CoCo with 64 KB (D6). */
void     coco_config_default(coco_config_t *cfg);

/* Every field cleared, the configuration taken, the page table built.
 * Load the ROMs next, then power on. */
void     coco_init(coco_t *m, const coco_config_t *cfg);

/* A ROM image at `base`: $8000 or $A000 for 8 KB, $8000 for 16 KB (the
 * Dragon's, or the CoCo's two in one file), $C000 for a cartridge of up to
 * 16 KB. False for any other placement. */
bool     coco_load_rom(coco_t *m, uint16_t base, const uint8_t *data, size_t len);

/* RAM cleared (§13.3), then RESET. */
void     coco_power_on(coco_t *m);

/* The RESET line: the CPU, both PIAs and the SAM (§8.3). */
void     coco_reset(coco_t *m);

/* Whole instructions until at least `ticks` have passed; returns the true
 * count, and the caller carries the overshoot (§6, EL §2.2). */
uint32_t coco_run(coco_t *m, uint32_t ticks);

/* One field, split where FS falls (§13.1). Returns its ticks. */
uint32_t coco_run_field(coco_t *m);

/* The time now, in ticks. */
uint64_t coco_ticks(const coco_t *m);

/* A key, by the matrix's own row and column (§11.2). */
void     coco_key_set(coco_t *m, int row, int col, bool down);

/* The machine's copy: never `=`, since the page table points into the
 * struct (§6, EL §2.2). */
void     coco_copy(coco_t *dst, const coco_t *src);

/* Where an address in the RAM space is in `ram`, through the SAM's row
 * and column and the chips' organisation; -1 where no chip answers. */
int32_t  coco_ram_index(const coco_t *m, uint16_t a);

/* The slow path's decode, for bus.c. */
uint8_t  coco_slow_read(coco_t *m, uint16_t a);
void     coco_slow_write(coco_t *m, uint16_t a, uint8_t v);

#endif /* PICO_COCO_COCO_H */
