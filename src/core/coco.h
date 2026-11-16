/* coco.h — the machine (design.md §6, §8).
 *
 * The CPU, the page table it reaches memory through, the SAM, the two
 * PIAs, RAM, the ROMs and the keyboard matrix, and the field loop that
 * runs them in master-oscillator ticks (§8.2, §13), and the snapshot of
 * what the VDG shows (§9.3), and the sound bus as PCM (§10). The card
 * and cartridges arrive in M9.
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
#include "mc6847.h"
#include "pcm.h"
#include "sam6883.h"
#include "tape.h"

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
    bool           tape_trap;  /* serve Color BASIC's tape routines (tape.h) */
} coco_config_t;

/* What the VDG shows in a field (§9.3): its mode and the SAM's, and for
 * each active line the bytes the VDG fetched on it, COCO_VDG_LINE_BYTES
 * apart, the first 16 or 32 of them used by the mode. */
typedef struct {
    uint8_t  mode;        /* the VDG's, packed (mc6847.h)               */
    uint8_t  sam_v;       /* the SAM's video mode                        */
    uint16_t base;        /* the display offset, F x 512                 */
    uint32_t field;       /* coco_t.fields when taken                    */
    uint8_t  lines[COCO_SNAP_VRAM_MAX];
} coco_frame_t;

/* The sound bus's level (§10, §18: low): XRoar's voltages, measured on a
 * real Dragon, in hundredths of a volt over 126, so that every input,
 * the DAC's 6 bits over 63 and the idle tape's half, is exact. 4.70 V,
 * the most any source reaches, is the full scale; at 0.3 of it or more,
 * PIA1 PB1 set as an input reads low (coco.c). */
#define COCO_SOUND_LEVEL_MAX  (470u * 126u)
#define COCO_SOUND_PB1_LOW    (141u * 126u)

/* The CPU's interrupt sources (mc6809.h). */
#define COCO_IRQ_PIA0   0x01u
#define COCO_FIRQ_PIA1  0x01u

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
    uint64_t irq_due[COCO_PIA_PORTS]; /* PIA0 A, B, PIA1 A, B: a rise due */
    uint32_t fields;
    bool     sound_fed;        /* the sound has fed PIA1 PB1 back (coco.c) */
    uint32_t sound_level;      /* the bus now, to COCO_SOUND_LEVEL_MAX  */
    uint32_t sound_edges;      /* changes of it, since power-on         */

    /* The bus as PCM (§10), drained by the port once a field with
     * coco_audio_drain. Not the machine's state: RESET leaves it. */
    pcm_t    pcm;

    /* §9.4: the video bits (the VDG's mode and the SAM's V and F) as last
     * seen, whether they changed during this field's active lines, and how
     * many fields so far have had such a change. */
    uint32_t video_bits;
    bool     video_split;
    uint32_t split_fields;

    /* The field's shape, computed at power-on (§13.1, §18: medium). */
    uint32_t line_ticks;       /* 57 E cycles of 16 ticks               */
    uint32_t field_lines;      /* 262 on NTSC, 312 on PAL               */
    uint32_t fs_fall_line;     /* FS falls as this line begins          */
    uint32_t field_lead;       /* PAL: lines the first field lacks      */
    uint32_t active_line;      /* the first active line                 */
    uint32_t hs_low_ticks;     /* HS is low this long from a line's start */

    /* RAM: the chips' organisation, from the size (§8.1). */
    uint8_t  ram_addr_bits;    /* address bits of one chip              */
    uint8_t  ram_row_bits;
    uint8_t  ram_banks;        /* banks fitted                          */

    bool     rom_present[2];   /* the $8000 and $A000 halves            */
    bool     cart_present;     /* a ROM at $C000 (§12.2)                */
    bool     cart_autostart;   /* its CART line on the Q clock (coco.c) */

    /* The tape deck and the trap's request (tape.h). */
    coco_tape_t tape;

    /* The trace driver's hook (tools/trace/coco-trace.c): when set, each
     * run slice is one step, an instruction, an interrupt's entry or a
     * cycle of a wait, and the hook is called after it. Tested once a
     * slice, not once an instruction. NULL in the emulator. */
    void   (*step_hook)(struct coco_s *m);
    void    *hook_ctx;

    uint8_t  ram[COCO_ADDR_SPACE];
    uint8_t  rom[COCO_ROM_SIZE];      /* $8000-$BFFF                    */
    uint8_t  cart[COCO_CART_SIZE];    /* $C000-$FEFF                    */
} coco_t;

/* A CoCo with 64 KB (D6). */
void     coco_config_default(coco_config_t *cfg);

/* Every field cleared, the configuration taken, the page table built.
 * Load the ROMs next, then power on. */
void     coco_init(coco_t *m, const coco_config_t *cfg);

/* A ROM image at `base`: $8000 or $A000 for 8 KB, $8000 for 16 KB (the
 * Dragon's, or the CoCo's two in one file), $C000 for a cartridge of up to
 * 16 KB, which autostarts (coco_cart_autostart). False for any other
 * placement. */
bool     coco_load_rom(coco_t *m, uint16_t base, const uint8_t *data, size_t len);

/* The cartridge taken out: $C000 reads as an empty slot. Power on after. */
void     coco_cart_remove(coco_t *m);

/* Whether the cartridge's CART line is tied to the Q clock, as a game's
 * is, so that BASIC starts it (§12.2); a DOS's is not (M13). */
void     coco_cart_autostart(coco_t *m, bool on);

/* RAM cleared (§13.3), then RESET. */
void     coco_power_on(coco_t *m);

/* The RESET line: the CPU, both PIAs and the SAM (§8.3). */
void     coco_reset(coco_t *m);

/* Whole instructions until at least `ticks` have passed; returns the true
 * count, and the caller carries the overshoot (§6, EL §2.2). */
uint32_t coco_run(coco_t *m, uint32_t ticks);

/* One field, split where FS falls and where it rises (§13.1), ending
 * where the next field's first active line begins, which is where the
 * caller takes the snapshot (EL §5.6). The first after power-on runs from
 * tick 0. Returns its ticks. */
uint32_t coco_run_field(coco_t *m);

/* What the VDG will show in the field whose active lines begin now
 * (§9.3): its mode as PIA1 port B drives it, and each line's bytes as the
 * SAM's counter fetches them in the SAM's mode, from RAM as it is now.
 * Called after coco_run_field. */
void     coco_video_snapshot(const coco_t *m, coco_frame_t *out);

/* The VDG's mode as PIA1 port B drives it now. */
uint8_t  coco_vdg_mode(const coco_t *m);

/* The time now, in ticks. */
uint64_t coco_ticks(const coco_t *m);

/* The port's sample rate, as the exact fraction rate_num / rate_den Hz
 * (§10). The samples not yet drained are kept. coco_init starts at the
 * nominal COCO_AUDIO_RATE. */
void     coco_audio_set_rate(coco_t *m, uint32_t rate_num, uint32_t rate_den);

/* Move up to `max` samples of the fields run so far out, oldest first.
 * Returns how many. coco_run_field brings the PCM to the field's end. */
size_t   coco_audio_drain(coco_t *m, int16_t *dst, size_t max);

/* After snapshot_load has put a state's fields back: what follows from
 * them worked out again, the keys released and the sound restarted at
 * the restored tick (snapshot.h). */
void     coco_restored(coco_t *m);

/* A key, by the matrix's own row and column (§11.2). */
void     coco_key_set(coco_t *m, int row, int col, bool down);

/* The machine's copy: never `=`, since the page table points into the
 * struct (§6, EL §2.2). */
void     coco_copy(coco_t *dst, const coco_t *src);

/* Where an address in the RAM space is in `ram`, through the SAM's row
 * and column and the chips' organisation; -1 where no chip answers. */
int32_t  coco_ram_index(const coco_t *m, uint16_t a);

/* The same for the VDG's address: no page bit, and the first bank, as
 * XRoar has it (§18). */
int32_t  coco_vram_index(const coco_t *m, uint16_t a);

/* The slow path's decode, for bus.c. */
uint8_t  coco_slow_read(coco_t *m, uint16_t a);
void     coco_slow_write(coco_t *m, uint16_t a, uint8_t v);

#endif /* PICO_COCO_COCO_H */
