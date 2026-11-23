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

/* $8000-$BFFF holds BASIC: Extended Color BASIC's 8 KB half and Color
 * BASIC's. */
#define COCO_ROM_SIZE      0x4000u
#define COCO_ROM_HALF      0x2000u
/* $C000-$FEFF is the cartridge's window, up to 16 KB (M9: mirrors). */
#define COCO_CART_SIZE     0x4000u
/* The largest image the ROM table knows (romset.h). */
#define COCO_ROM_IMAGE_MAX 0x2000u

/* PIA0 A and B, PIA1 A and B: the ports whose IRQ output can be due. */
#define COCO_PIA_PORTS          4u

/* The keyboard matrix: columns on PIA0 port B, rows on port A (§11.2).
 * Plain ints, as the loops over them are. */
#define COCO_KEY_COLS           8
#define COCO_KEY_ROWS           7

/* The archive's survey of what titles read (§11.3, coco.h): counted in
 * the host build alone, which defines it. */
#ifndef COCO_SURVEY
#define COCO_SURVEY 0
#endif

/* The joystick ports, right and left (§11.4). */
#define COCO_JOYSTICKS          2u
#define COCO_JOY_CENTRE     32767u

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

/* Game layouts (§11.3, keymatrix.h): the built-in ones and the card's
 * together, and what one .map file may hold. A file a layout goes with
 * is named as the archive names it, less its extension. */
#define COCO_KEYMAP_NAME_LEN    16u  /* a layout's name                     */
#define COCO_KEYMAP_LAYOUTS     10u  /* built-in and card layouts together  */
#define COCO_KEYMAP_BINDINGS    16u  /* bindings a layout                   */
#define COCO_KEYMAP_FILES        4u  /* tapes and cartridges it names       */
#define COCO_KEYMAP_FILE_LEN    48u  /* one of them, without its extension  */
#define COCO_KEYMAP_FILE_MAX  1024u  /* the largest .map file read          */
#define COCO_KEYMAP_DIR  "/coco/keymaps"

/* ---- Timing (design.md §13.2) ---------------------------------------- */

/* Master-oscillator ticks a second, the only clock (§13.2): the NTSC
 * machines' 14.31818 MHz, four times the colour subcarrier. The device
 * paces the guest on it (core0.c), and the audio's sample period is a
 * fraction of it (pcm.h). */
#define COCO_TICK_HZ 14318180u

/* The NTSC field, the only one (D6), from the MC6847's figures as XRoar
 * counts them (§13.1, §18: medium): 262 lines of 57 E cycles of 16
 * ticks; HS low for the first 64 ticks of each; the first active line
 * after 13 of blanking and 25 of top border; FS falling as the line
 * after the 192 active ones begins. Constants, so that the field loop's
 * divisions by the line are multiplications (§13.1). */
#define COCO_LINE_TICKS     (57u * 16u)
#define COCO_FIELD_LINES    262u
#define COCO_FIELD_TICKS    (COCO_LINE_TICKS * COCO_FIELD_LINES)
#define COCO_HS_LOW_TICKS   64u
#define COCO_ACTIVE_LINE    (13u + 25u)
#define COCO_FS_FALL_LINE   (COCO_ACTIVE_LINE + 192u)

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
#define COCO_SPLIT_MAX       (COCO_LINE_SPLITS * COCO_PIC_H)

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
/* §9.4: the changes of the video bits a field's picture is drawn by, and
 * how many of them one line shows part-way along. Dark Star and Rommel's
 * Revenge change CSS twice on a line, on and off (docs/milestones.md,
 * M12), and Dragonfire up to six times, on its moat's lower edge (M13).
 * A line's pixels last 32 E cycles and the quickest store takes 4, so a
 * program makes about 8 at most; more on a line fold into its last, and
 * more in a field than eight a line are dropped and counted.
 * COCO_SPLIT_MAX is given its value below, with the picture's. */
#define COCO_LINE_SPLITS        8u
/* Where a line's 256 pixels begin, in ticks from HS's fall: the pulse,
 * the back porch and the left border, XRoar's MC6847 figures (§18), and
 * the ticks a pixel takes. A change of the video bits before them shows
 * on the whole line, one among them from its pixel on, and one after
 * them from the next line (§9.4). */
#define COCO_LINE_ACTIVE_TICKS (64u + 70u + 120u)
#define COCO_PIXEL_TICKS        2u
/* A PIA1 write that changes the VDG's mode reaches the picture 1 us
 * after the write's cycle ends, 14 ticks, 7 pixels: the MC6821's output
 * delay, as TRS3_TICKS is its interrupt's. Settled from two pictures of
 * Dragonfire, in which every change of colour set falls on a byte's
 * edge, and two titles' headings, not from a datasheet (§9.4, §18). */
#define COCO_PIA_VIDEO_TICKS   14u
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

/* ---- Disks: the RS-DOS controller (design.md §12.4) ------------------ */

/* Four drives, as the latch selects (rsdos.h). */
#define COCO_DISK_DRIVES         4u
/* A raw track as the WD1793 sees it after decoding (mfmdisk.h): a
 * 5.25-inch drive at 300 rpm, double density, 250,000 bits a second, so
 * 6,250 bytes a revolution, a byte every 32 us (§18). */
#define COCO_DISK_TRACK_LEN   6250u
/* The heads' reach, the most tracks a drive may be given, and the IDs one
 * track's index holds. */
#define COCO_DISK_TRACKS_MAX    80u
#define COCO_DISK_SECTORS_MAX   32u
/* A drive's type until set (wd1793.h): two sides of 80 tracks, which the
 * head never stops short of on any image (§12.4). */
#define COCO_DISK_DRIVE_SIDES    2u
#define COCO_DISK_DRIVE_TRACKS  80u
/* One side of a track of a JVC image, every sector of it: 18 of 256
 * bytes, RS-DOS's and OS-9's, is the most taken (jvc.h). */
#define COCO_DISK_TRACK_DATA_MAX 4608u
#define COCO_DISK_DIR    "/coco/disks"
/* The Disks page's list of one folder (dirlist.h): its folders and
 * images, their names in one pool. The CoCo SDC image's largest folder
 * holds 306 of 8.3 names, 4 KB of them (2026-11-22); the owner's own
 * names run to 60 characters. A folder that fills either shows the
 * entries that fit. */
#define COCO_DISK_LIST_MAX     320u
#define COCO_DIR_POOL         8192u

#endif /* PICO_COCO_CONFIG_H */
