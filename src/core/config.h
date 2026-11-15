/* config.h — every fixed capacity in the emulator, in one place
 * (design.md §6, EL §2.1).
 *
 * Nothing in src/core/ allocates; every buffer is sized from a constant
 * here, so the SRAM budget (design.md §16) is a link-time fact.
 *
 * A guest fact becomes a #define only once it is settled (design.md §18).
 * What is below is this project's own choice or a decision of design.md
 * §2; the address map, the clocks and the field's shape arrive as M3 and
 * M4 settle them, and the buffer sizes as the milestones that need them.
 *
 * Guest addresses are written $XXXX in comments and 0x in code.
 */
#ifndef PICO_COCO_CONFIG_H
#define PICO_COCO_CONFIG_H

/* ---- Guest address space and the page table (design.md §8.1) --------- */

#define COCO_ADDR_SPACE     65536u
#define COCO_PAGE_SIZE        256u  /* page_t {read, write} per page      */
#define COCO_PAGE_COUNT     (COCO_ADDR_SPACE / COCO_PAGE_SIZE)

/* Page $FF holds the PIAs, cartridge I/O, the SAM's bits and the vectors,
 * and always takes the slow path (§3.2, §8.1). */
#define COCO_IO_PAGE         0xFFu

/* ---- ROMs, the cartridge and the PIAs (design.md §3.2, §12.1) ------- */

/* $8000-$BFFF holds BASIC: two 8 KB halves on a CoCo, one 16 KB image on
 * a Dragon 32. M14: the Dragon 64's second BASIC. */
#define COCO_ROM_SIZE      0x4000u
#define COCO_ROM_HALF      0x2000u
/* $C000-$FEFF is the cartridge's window, up to 16 KB (M9: mirrors). */
#define COCO_CART_SIZE     0x4000u
/* The largest image the ROM table knows (romset.h). */
#define COCO_ROM_IMAGE_MAX 0x4000u

/* PIA0 A and B, PIA1 A and B: the ports whose IRQ output can be due. */
#define COCO_PIA_PORTS          4u

/* The keyboard matrix: columns on PIA0 port B, rows on port A (§11.2).
 * Plain ints, as the loops over them are. */
#define COCO_KEY_COLS           8
#define COCO_KEY_ROWS           7

/* The held-key set (§11.1, keymatrix.h). */
#define COCO_KEY_EVENT_QUEUE   64u  /* southbridge FIFO holds 31 (HW §6.2) */
#define COCO_KEY_HELD_MAX       8u  /* keys down at once                   */
#define COCO_KEY_TEXT_EVENTS    4u  /* one ASCII byte: a modifier around a key */

/* The replay's pace (§11.1, §18). Color BASIC scans the keyboard in
 * the foreground, not on an interrupt, so a key is seen only while BASIC
 * waits for one. Executed 2026-11-15 (test_keyboard): 1.3 and Extended
 * 1.1 take a key held 2 fields from every phase against the field, 1.1
 * needs 3, and all three need a field with the key up before it is typed
 * again; none repeats a held key. The replay holds and gaps a field
 * longer than the slowest ROM needs, for the line BASIC is busy storing
 * when the next key comes (test_keyboard's long program). */
#define COCO_KEY_MIN_FIELDS     4u
#define COCO_KEY_GAP_FIELDS     2u

/* ---- The picture (design.md §9.6, D11) ------------------------------- */

#define COCO_PIC_W            256u
#define COCO_PIC_H            192u
#define COCO_PIC_X             32u  /* top-left on the 320x320 panel      */
#define COCO_PIC_Y             64u
#define COCO_PANEL_W          320u
#define COCO_PANEL_H          320u

/* ---- The VDG and the snapshot pool (design.md §9, EL §2.4) ----------- */

/* The VDG fetches 32 bytes on an active line, or 16, and the snapshot
 * keeps 32 a line for each of the 192 (§9.3). */
#define COCO_VDG_LINE_BYTES    32u
#define COCO_SNAP_BUFFERS       3u
#define COCO_SNAP_VRAM_MAX   (COCO_PIC_H * COCO_VDG_LINE_BYTES)   /* 6144 */
/* The graphics table's widest entry: a byte of a 16-byte mode is 16
 * pixels (§9.2). */
#define COCO_LUT_PX_MAX        16u
/* The presenter's dirty bands, in lines (§9.3, EL §5.3). */
#define COCO_BAND_ROWS          8u
#define COCO_BANDS           (COCO_PIC_H / COCO_BAND_ROWS)

#endif /* PICO_COCO_CONFIG_H */
