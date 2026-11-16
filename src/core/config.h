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
#define COCO_KEY_RING          COCO_KEY_EVENT_QUEUE  /* core 1 to core 0 (kbd.c) */

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

/* ---- Timing (design.md §13.2) ---------------------------------------- */

/* Master-oscillator ticks a second, the only clock (§13.2): the NTSC
 * machines' 14.31818 MHz, four times the colour subcarrier. The device
 * paces the guest on it (core0.c), and the audio's sample period is a
 * fraction of it (pcm.h). The PAL machines' crystal is §18's to settle,
 * and XRoar counts them in these ticks too; the Dragon reaches the device
 * in M14. */
#define COCO_TICK_HZ 14318180u

/* ---- Audio (design.md §10; EL §6; hardware-notes.md §5) --------------- */

#define COCO_PWM_TOP          2047u  /* 11 bits, 73.2 kHz carrier          */
#define COCO_PWM_OVERSAMPLE      2u  /* each frame written twice           */

/* The nominal sample rate as a fraction, 150,000,000 / 4,096 Hz, which
 * makes a sample 91,636,352/234,375 ticks (§10). The port recomputes it
 * from clock_get_hz(clk_sys) and hands the real one to
 * coco_audio_set_rate; this is what a host build runs at. */
#define COCO_AUDIO_RATE_NUM  150000000u
#define COCO_AUDIO_RATE_DEN  ((COCO_PWM_TOP + 1u) * COCO_PWM_OVERSAMPLE)

/* Samples the core holds between drains: an NTSC field is 611.0, a PAL
 * one 727.6. The port drains after every field (§10). */
#define COCO_AUDIO_BUF_LEN    1024u

#define COCO_PCM_QUEUE_LEN    1024u  /* SPSC, ~28 ms (EL §6.2)             */
#define COCO_PCM_QUEUE_START   768u  /* start streaming at this depth      */

/* Power-of-two AND aligned, with the hardware read wrap (HW §5.3). At
 * oversample 2 a frame is two slots; a half is 3.5 ms of sound. */
#define COCO_DMA_FRAMES_PER_HALF 128u
#define COCO_DMA_SLOTS_PER_HALF  (COCO_DMA_FRAMES_PER_HALF * COCO_PWM_OVERSAMPLE)
#define COCO_DMA_RING_SLOTS      (COCO_DMA_SLOTS_PER_HALF * 2u)

/* ---- The picture (design.md §9.6, D11) ------------------------------- */

#define COCO_PIC_W            256u
#define COCO_PIC_H            192u
#define COCO_PIC_X             32u  /* top-left on the 320x320 panel      */
#define COCO_PIC_Y             64u
#define COCO_PANEL_W          320u
#define COCO_PANEL_H          320u

/* The VDG's border (§9.5): 32 px at the sides and 48 rows above and
 * below, as pico-atom draws its MC6847's, so it stops 16 rows short of
 * the panel's top and bottom, where the two text lines go. */
#define COCO_BORDER_X          32u
#define COCO_BORDER_Y          48u

/* The perf line at the top and the status line at the foot (§9.6), as
 * the siblings place them: 40 cells of the VDG's 8x12 glyphs across the
 * panel, each centred in the 16 rows the border leaves. */
#define COCO_TEXT_COLS         40u
#define COCO_TEXT_GLYPH_W       8u
#define COCO_TEXT_GLYPH_H      12u
#define COCO_PERF_Y             2u
#define COCO_STATUS_Y         306u

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

/* DMA ping-pong line buffers, the panel's width so that the status and
 * perf lines can use them too (HW §4.6). */
#define COCO_LINEBUF_COUNT      2u
#define COCO_LINEBUF_PIXELS  COCO_PANEL_W

/* ---- Port buffers (design.md §6, §16) -------------------------------- */

/* Core 0's log, drained by core 1 (EL §2.3): the ring, a power of two,
 * and the longest line formatted onto core 0's stack. */
#define COCO_LOG_RING        2048u
#define COCO_LOG_LINE         512u

/* The board's first fields, recorded and held to the host's (§15.5,
 * fieldlog.h), and the longest line one makes. */
#define COCO_FIELD_LOG_MAX    150u
#define COCO_FIELD_LOG_LINE   128u

/* ---- The card (design.md §12.1, §12.6) ------------------------------- */

#define COCO_PATH_MAX         128u  /* a path on the card, with its NUL    */
#define COCO_ROM_DIR     "/coco/roms"
#define COCO_CART_DIR    "/coco/carts"
#define COCO_CARD_CHUNK       512u  /* a file read a sector at a time      */

/* The settings file, /coco/coco.ini (§12.6, settings.h). */
#define COCO_SETTINGS_FILE_MAX 2048u
#define COCO_SETTINGS_LINE_MAX (COCO_PATH_MAX + 32u)

/* The deck's buffer, the whole of a CAS image (§12.3, §16, tape.h), and
 * the menu's lists of tapes and cartridges (§14). */
#define COCO_TAPE_IMAGE_MAX  65536u
#define COCO_MEDIA_NAME_MAX     16u  /* a new tape's name, typed in        */
#define COCO_TAPE_LIST_MAX      64u
#define COCO_CART_LIST_MAX      64u

#endif /* PICO_COCO_CONFIG_H */
