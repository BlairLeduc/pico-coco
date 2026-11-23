/* menu.c — the emulator's menu and pause (menu.h, design.md §14).
 *
 * pico-oric's menu, page for page and row for row, so that the family
 * reads as one: the same items in the same order, the same function
 * keys, and Esc going back a page, or to the guest from the main page or
 * a page a key opened. On the CoCo's 32 cells and in capitals. The Disks
 * page (the CoCo's word, §12.6), on F2, puts the card's images in the
 * four drives, empties one, or makes a new disk for DSKINI (dskio.h).
 * The Snapshots page saves, loads and deletes four slots (§12.5,
 * snapio.h). The Machine page stages BASIC, the RAM and the cartridge,
 * the RS-DOS controller among them (§14).
 * Setup has the Keys row, which goes round the standard map, the
 * keyboard joystick as either stick and the card's layouts (keymapio.h),
 * and Fast tape, off for the tape's signal (cassette.h).
 */

#include "menu.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"

#include "picocalc_shell.h"

#include "board.h"
#include "card.h"
#include "core0.h"
#include "core1.h"
#include "display.h"
#include "rtc.h"
#include "dskio.h"
#include "handoff.h"
#include "kbd.h"
#include "keymapio.h"
#include "keymatrix.h"
#include "log.h"
#include "pico_coco_version.h"
#include "roms.h"
#include "sd.h"
#include "settingsio.h"
#include "snapio.h"
#include "southbridge.h"
#include "status.h"
#include "storage.h"
#include "tapeio.h"
#include "textpage.h"

/* The keyboard at the live loop's 30 Hz, which also feeds the MCU's 2.5 s
 * bus watchdog (hardware-notes.md §6.1). */
#define POLL_US 33333u
#define BAT_POLL_US 5000000u

/* Backlight register values step by 16 and clamp to 16-240
 * (hardware-notes.md §6). */
#define BKL_STEP   16u
#define BKL_LOWEST 16u

/* The page: a title, the rows from row 2, and at the foot the status
 * row and the keys that work here (§14). */
#define ROW_TOP    2
#define ROW_STATUS (TEXT_ROWS - 2)
#define ROW_KEYS   (TEXT_ROWS - 1)

enum { I_TAPES, I_DISKS, I_SNAPS, I_SETUP, I_MACHINE, I_RESET, I_POWER, I_SAVE, I_SHELL,
       I_ABOUT, I_COUNT };

enum { D_STATUS, D_PERF, D_BACKLIGHT, D_VOLUME, D_KEYS, D_FAST, D_VDG, D_ARTEFACTS, D_PHASE,
       D_PALETTE, D_CLOCK, D_DISK_SOUNDS, D_COUNT };
_Static_assert(ROW_TOP + I_COUNT + 2 <= ROW_STATUS, "the main page's rows fit");
/* The layout's bindings go below the rows when there is room; with the
 * palette's row there is none (design.md §9.5). */
_Static_assert(ROW_TOP + D_COUNT <= ROW_STATUS, "the Setup page's rows fit");

/* The Tapes page: three things to do with the deck, then the card's
 * tapes. */
enum { T_EJECT, T_REWIND, T_NEW, T_FIRST };
#define TAPE_ROWS (ROW_STATUS - 1 - (ROW_TOP + 1))

/* The Disks page, as pico-oric's: row 0 empties the drive, row 1 makes
 * a new disk for it, then one folder of the card: ".." inside a folder,
 * its folders, its images (dirlist.h); < > choose the drive on any row,
 * Enter opens a folder or inserts an image, Backspace goes up. */
enum { K_EMPTY, K_NEW, K_FIRST };
#define DISK_ROWS TAPE_ROWS

/* The Snapshots page, as pico-oric's: the slot, chosen with < > on any
 * of its rows, the three things to do with it, then every slot's state. */
enum { N_SLOT, N_SAVE, N_LOAD, N_DELETE, N_COUNT };

/* The Machine page: BASIC, the RAM and the cartridge staged, and the
 * power-on that applies them. */
enum { M_BASIC, M_RAM, M_CART, M_APPLY, M_COUNT };

static coco_frame_t *s_scr;
static settings_t    s_file;       /* what the file says, for the save */
static bool          s_bkl_set;
static card_job_t    s_job;        /* About's look at the card         */

static tapeio_entry_t s_list[COCO_TAPE_LIST_MAX];
static dirlist_t      s_disks;
static unsigned       s_drive;     /* the Disks page's, kept between openings */
static char           s_disk_dir[COCO_PATH_MAX] = DSKIO_DIR;   /* likewise   */
static char           s_carts[COCO_CART_LIST_MAX][COCO_PATH_MAX];

static const unsigned k_rams[4] = { 4u, 16u, 32u, 64u };

/* The Machine page's cartridge row: none, the RS-DOS controller, then
 * the card's images by index. */
#define CART_NONE  (-1)
#define CART_RSDOS (-2)
static unsigned       s_slot;      /* the Snapshots page's, kept between openings */

static struct {
    coco_t  *m;
    bool     card;
    bool     alt;
    bool     done;
    bool     direct;          /* opened at a page: closing it resumes */
    int      item;
    enum { P_MAIN, P_TAPES, P_DISKS, P_SNAPS, P_SETUP, P_MACHINE, P_ABOUT, P_HELP } page;
    char     status[TEXT_COLS + 1];
    int      battery;

    int      setup_sel;

    int      snap_sel;
    bool     used[SNAPIO_SLOTS];

    unsigned n_tapes;
    int      tape_sel, tape_top;

    unsigned n_disks;          /* s_disks.n, 0 with no card          */
    bool     disk_up;          /* inside a folder: a ".." row         */
    int      disk_sel, disk_top;

    int      machine_sel;
    bool     st_ext;           /* staged: applied only by a power-on */
    unsigned st_ram;           /* index into k_rams                  */
    int      st_cart;          /* CART_NONE, CART_RSDOS, else into s_carts */
    int      run_cart;         /* the running cartridge's             */
    unsigned n_carts;

    struct {
        bool     on;
        bool     disk;         /* a new disk's name, not a tape's    */
        char     text[COCO_MEDIA_NAME_MAX + 1];
        unsigned len;
    } name;

    int      sb_ver;
    int      temp_c;
    bool     about_job;
} s;

void menu_init(coco_frame_t *page) {
    s_scr = page;
    s_file = g_boot.settings;
}

static void say(const char *fmt, const char *arg) {
    snprintf(s.status, sizeof s.status, fmt, arg);
}

static const char *on_off(bool v) {
    return v ? "ON" : "OFF";
}

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static unsigned ram_index(unsigned ram_k) {
    for (unsigned i = 0; i < 4; i++)
        if (k_rams[i] == ram_k) return i;
    return 3;
}

/* The machine as a line: "64K EXTENDED COLOR BASIC", "4K COLOR BASIC
 * 1.1", for the BASIC that RAM runs (roms.h). */
static const char *machine_name(bool ext, unsigned ram_k) {
    static char text[TEXT_COLS + 1];
    const char *why;
    rom_id_t b = roms_basic_for(&g_boot.job, ram_k, &ext, &why);
    snprintf(text, sizeof text, "%uK %s%s", ram_k, ext ? "EXTENDED COLOR BASIC" : "COLOR BASIC",
             ext ? "" : b == ROM_COCO_BAS11 ? " 1.1" : b == ROM_COCO_BAS13 ? " 1.3" : "");
    return text;
}

/* ---- drawing ------------------------------------------------------------- */

static void draw_main(void) {
    static const char *const items[I_COUNT] = {
        " TAPES...", " DISKS...", " SNAPSHOTS...", " SETUP...", " MACHINE...", " RESET",
        " POWER OFF/ON", " SAVE SETTINGS", " EXIT TO SHELL", " ABOUT...",
    };
    for (int i = 0; i < I_COUNT; i++)
        textpage_line(s_scr, ROW_TOP + i, items[i], i == s.item);
    /* Under the items, a row apart. The tape is not repeated here: the
     * status line and the Tapes page show it (the owner, 2026-11-22). */
    char line[TEXT_COLS + 1];
    snprintf(line, sizeof line, " %.30s", machine_name(g_machine.extended, g_machine.ram_k));
    textpage_line(s_scr, ROW_TOP + I_COUNT + 1, line, false);
    if (g_machine.rsdos) {
        const char *d = dskio_inserted(0);
        snprintf(line, sizeof line, " RS-DOS, DISK 0: %.15s", d[0] ? base(d) : "NONE");
        textpage_line(s_scr, ROW_TOP + I_COUNT + 2, line, false);
    }
}

static void draw_tapes(void) {
    char line[TEXT_COLS + 1];
    textpage_line(s_scr, ROW_TOP, !s.card ? " NO CARD"
                                : s.n_tapes ? " FILE                  FIRST"
                                : " NO TAPES IN /COCO/TAPES/", false);
    for (int r = 0; r < TAPE_ROWS; r++) {
        int i = s.tape_top + r;
        line[0] = 0;
        if (i == T_EJECT) {
            snprintf(line, sizeof line, " (EJECT)");
        } else if (i == T_REWIND) {
            snprintf(line, sizeof line, " (REWIND)");
        } else if (i == T_NEW) {
            snprintf(line, sizeof line, " (NEW TAPE)");
        } else if (i < T_FIRST + (int)s.n_tapes) {
            const tapeio_entry_t *e = &s_list[i - T_FIRST];
            bool here = strcmp(e->path, tapeio_inserted()) == 0;
            snprintf(line, sizeof line, "%c%-21.21s %-8.8s", here ? '*' : ' ', base(e->path),
                     e->name[0] ? e->name : "(EMPTY)");
        }
        textpage_line(s_scr, ROW_TOP + 1 + r, line, i == s.tape_sel);
    }
}

/* Which drive an image is in: 0 to 3, or a space. */
static char drive_of(const char *path) {
    for (unsigned d = 0; d < COCO_DISK_DRIVES; d++)
        if (strcasecmp(path, dskio_inserted(d)) == 0) return (char)('0' + d);
    return ' ';
}

/* The row of the folder's first entry, after ".." if there is one. */
static int disk_first(void) {
    return K_FIRST + (s.disk_up ? 1 : 0);
}

/* The entry a row shows, or NULL for the rows before them. */
static const dirlist_entry_t *disk_entry(int row) {
    int i = row - disk_first();
    return i >= 0 && i < (int)s.n_disks ? &s_disks.e[i] : NULL;
}

static void draw_disks(void) {
    char line[TEXT_COLS + 1];
    snprintf(line, sizeof line, " DRIVE < %c >  %.18s", '0' + s_drive,
             !s.card ? "NO CARD" : s.n_disks ? "IMAGE     IN R/O"
             : s.disk_up ? "AN EMPTY FOLDER" : "NONE ON THE CARD");
    textpage_line(s_scr, ROW_TOP, line, false);
    for (int r = 0; r < DISK_ROWS; r++) {
        int i = s.disk_top + r;
        const dirlist_entry_t *e = disk_entry(i);
        line[0] = 0;
        if (i == K_EMPTY) {
            const char *in = dskio_inserted(s_drive);
            snprintf(line, sizeof line, " (EMPTY DRIVE %c: %.12s)", '0' + s_drive,
                     in[0] ? base(in) : "EMPTY");
        } else if (i == K_NEW) {
            snprintf(line, sizeof line, " (NEW DISK, FOR DSKINI)");
        } else if (i == K_FIRST && s.disk_up) {
            /* Up, beside the folder this is: its end, if it is long. */
            const char *below = dirlist_below(s_disk_dir, DSKIO_DIR);
            size_t n = strlen(below), room = TEXT_COLS - 5u;
            snprintf(line, sizeof line, " .. /%s", n > room ? below + (n - room) : below);
        } else if (e && (e->flags & DIRLIST_FOLDER)) {
            snprintf(line, sizeof line, " %.29s/", e->name);
        } else if (e) {
            char full[COCO_PATH_MAX];
            dirlist_join(full, sizeof full, s_disk_dir, e->name);
            snprintf(line, sizeof line, " %-24.24s %c  %c", e->name, drive_of(full),
                     (e->flags & DIRLIST_RDONLY) ? '*' : ' ');
        }
        textpage_line(s_scr, ROW_TOP + 1 + r, line, i == s.disk_sel);
    }
    if (!g_machine.rsdos)
        textpage_line(s_scr, ROW_STATUS - 1, " RS-DOS IS NOT IN: MACHINE PAGE", false);
}

static void draw_setup(void) {
    char line[TEXT_COLS + 1];
    for (int i = 0; i < D_COUNT; i++) {
        switch (i) {
        case D_STATUS:
            snprintf(line, sizeof line, " STATUS LINE     < %s >", on_off(g_ui.status));
            break;
        case D_PERF:
            snprintf(line, sizeof line, " PERF LINE       < %s >", on_off(g_ui.perf_line));
            break;
        case D_BACKLIGHT:
            snprintf(line, sizeof line, " BACKLIGHT       < %u >", g_ui.backlight);
            break;
        case D_VOLUME:
            snprintf(line, sizeof line, " VOLUME          < %u >", g_ui.volume);
            break;
        case D_KEYS:
            /* In the column the others use; a card layout's name is cut
             * to the 11 characters that leave room for " >". */
            snprintf(line, sizeof line, " KEYS            < %.11s >",
                     g_ui.layout ? g_ui.layout->name : "STANDARD");
            break;
        case D_FAST:
            snprintf(line, sizeof line, " FAST TAPE       < %s >", on_off(g_ui.fast_tape));
            break;
        case D_VDG:
            /* The CoCo 2B's VDG, with lower case (display.h). */
            snprintf(line, sizeof line, " VDG             < %s >",
                     g_ui.vdg_t1 ? "6847T1" : "6847");
            break;
        case D_ARTEFACTS: {
            /* §9.5: XRoar's 2-bit renderer's, or its 5-bit one's. */
            static const char *const how[3] = { "OFF", "SIMPLE", "FULL" };
            snprintf(line, sizeof line, " ARTIFACTS       < %s >", how[g_ui.artefacts % 3u]);
            break;
        }
        case D_PHASE: {
            /* The phase drawn now; "random" chooses at each reset. */
            const char *now = g_ui.artefact_red ? "RED" : "BLUE";
            if (g_ui.artefact_phase == SETTINGS_PHASE_RANDOM)
                snprintf(line, sizeof line, " PHASE   < RANDOM, NOW %s >", now);
            else
                snprintf(line, sizeof line, " PHASE           < %s >", now);
            break;
        }
        case D_PALETTE:
            /* The idealised primaries, or the chip's own (§9.5). */
            snprintf(line, sizeof line, " PALETTE         < %s >",
                     g_ui.palette == SETTINGS_PALETTE_DATASHEET ? "DATASHEET" : "PRIMARY");
            break;
        case D_CLOCK:
            /* A DS1216 at $FF78-$FF7F, for OS-9's clock_tc3 (rtc.h). */
            snprintf(line, sizeof line, " SMARTWATCH      < %s >", on_off(g_ui.smartwatch));
            break;
        case D_DISK_SOUNDS:
            /* The drives' steps and motor (drivesnd.h). */
            snprintf(line, sizeof line, " DISK SOUNDS     < %s >", on_off(g_ui.disk_sounds));
            break;
        }
        textpage_line(s_scr, ROW_TOP + i, line, i == s.setup_sel);
    }
    /* The layout in force, two bindings a row as a .map file writes them
     * (keymatrix.h). */
    const keylayout_t *l = g_ui.layout;
    int row = ROW_TOP + D_COUNT;
    for (unsigned i = 0; l && i < l->n && row < ROW_STATUS; i += 2, row++) {
        char a[16] = "", b[16] = "";
        keymap_binding_str(&l->bind[i], a, sizeof a);
        if (i + 1u < l->n) keymap_binding_str(&l->bind[i + 1u], b, sizeof b);
        for (char *c = a; *c; c++) *c = (char)toupper((unsigned char)*c);
        for (char *c = b; *c; c++) *c = (char)toupper((unsigned char)*c);
        snprintf(line, sizeof line, " %-15s%s", a, b);
        textpage_line(s_scr, row, line, false);
    }
}

static void draw_snaps(void) {
    char line[TEXT_COLS + 1];
    static const char *const rows[N_COUNT] = { NULL, " SAVE", " LOAD", " DELETE" };
    for (int i = 0; i < N_COUNT; i++) {
        if (i == N_SLOT) snprintf(line, sizeof line, " SLOT            < %u >", s_slot + 1u);
        else snprintf(line, sizeof line, "%s", rows[i]);
        textpage_line(s_scr, ROW_TOP + i, line, i == s.snap_sel);
    }
    /* Every slot's state, the chosen one marked. */
    for (unsigned i = 0; i < SNAPIO_SLOTS; i++) {
        snprintf(line, sizeof line, "%cSLOT %u: %s", i == s_slot ? '*' : ' ', i + 1u,
                 !s.card ? "NO CARD" : s.used[i] ? "SAVED" : "EMPTY");
        textpage_line(s_scr, ROW_TOP + N_COUNT + 1 + (int)i, line, false);
    }
    textpage_line(s_scr, ROW_TOP + N_COUNT + 2 + (int)SNAPIO_SLOTS, " IN /COCO/SNAPS/. A LOAD POWERS", false);
    textpage_line(s_scr, ROW_TOP + N_COUNT + 3 + (int)SNAPIO_SLOTS, " ON AS THE MACHINE IT WAS SAVED", false);
    textpage_line(s_scr, ROW_TOP + N_COUNT + 4 + (int)SNAPIO_SLOTS, " ON, WITH ITS TAPE, CART, DISKS.", false);
}

static bool machine_changed(int r) {
    switch (r) {
    case M_BASIC: return s.st_ext != g_machine.extended;
    case M_RAM:   return k_rams[s.st_ram] != g_machine.ram_k;
    case M_CART:  return s.st_cart != s.run_cart;
    }
    return false;
}

static bool machine_staged(void) {
    for (int r = 0; r < M_APPLY; r++)
        if (machine_changed(r)) return true;
    return false;
}

static void draw_machine(void) {
    char line[TEXT_COLS + 1];
    for (int i = 0; i < M_COUNT; i++) {
        char mark = machine_changed(i) ? '*' : ' ';
        switch (i) {
        case M_BASIC:
            snprintf(line, sizeof line, "%cBASIC   < %s >", mark,
                     s.st_ext ? "EXTENDED" : k_rams[s.st_ram] <= 4u ? "COLOR 1.1" : "COLOR");
            break;
        case M_RAM:
            snprintf(line, sizeof line, "%cRAM     < %uK >", mark, k_rams[s.st_ram]);
            break;
        case M_CART:
            snprintf(line, sizeof line, "%cCART    < %.16s >", mark,
                     s.st_cart == CART_NONE    ? "NONE"
                     : s.st_cart == CART_RSDOS ? "RS-DOS"
                     : s.st_cart == (int)s.n_carts ? base(g_boot.cart_path)
                                                   : base(s_carts[s.st_cart]));
            break;
        case M_APPLY:
            snprintf(line, sizeof line, " (APPLY AND RESTART)");
            break;
        }
        textpage_line(s_scr, ROW_TOP + i, line, i == s.machine_sel);
    }
    snprintf(line, sizeof line, " = %.28s", machine_name(s.st_ext, k_rams[s.st_ram]));
    textpage_line(s_scr, ROW_TOP + M_COUNT + 1, line, false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 3, " - THE PROGRAM IN MEMORY IS", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 4, "   LOST ON RESTART.", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 5, " - A CARTRIDGE STARTS ITSELF,", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 6, "   FROM /COCO/CARTS/.", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 7, " - RS-DOS IS DISK BASIC: 16K OR", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 8, "   MORE, EXTENDED, DISK11.ROM.", false);
    if (!s.card) textpage_line(s_scr, ROW_TOP + M_COUNT + 9, " - NO CARD: NO CARTRIDGES.", false);
}

static void about_rom(int row, rom_id_t id) {
    char line[TEXT_COLS + 1];
    const char *word = !s.about_job ? "" : s_job.found[id] ? "FOUND" : "--";
    bool running = (id == ROM_COCO_EXTBAS11 && g_machine.extended) || id == g_machine.basic;
    snprintf(line, sizeof line, "%c%-13s %s", running ? '*' : ' ', romset_images[id].file, word);
    textpage_line(s_scr, row, line, false);
}

static void draw_about(void) {
    char line[TEXT_COLS + 1];
    const board_info_t *b = &g_board;
    snprintf(line, sizeof line, " PICO-COCO %.21s", PICO_COCO_VERSION);
    textpage_line(s_scr, 2, line, false);
    snprintf(line, sizeof line, " BOARD %.25s", b->sdk_board);
    textpage_line(s_scr, 3, line, false);
    char sb[4] = "??";
    if (s.sb_ver >= 0) snprintf(sb, sizeof sb, "%02X", (unsigned)(s.sb_ver & 0xFF));
    int t = s.temp_c < -99 ? -99 : s.temp_c > 999 ? 999 : s.temp_c;
    snprintf(line, sizeof line, " %s %u MHZ SB %s %dC", b->chip ? b->chip : "?",
             (unsigned)((b->clk_sys_hz / 1000000u) % 1000u), sb, t);
    textpage_line(s_scr, 4, line, false);
    snprintf(line, sizeof line, " %.30s", machine_name(g_machine.extended, g_machine.ram_k));
    textpage_line(s_scr, 5, line, false);
    textpage_line(s_scr, 7, s.about_job ? " " COCO_ROM_DIR "/:" : " NO CARD: ROMS NOT READ", false);
    for (int id = 0; id < ROM_IMAGE_COUNT && 8 + id < ROW_STATUS - 1; id++) about_rom(8 + id, (rom_id_t)id);
    const char *err = settingsio_error();
    snprintf(line, sizeof line, " SETTINGS %.22s",
             err[0] ? err : settingsio_state_str(settingsio_state()));
    textpage_line(s_scr, ROW_STATUS - 1, line, false);
}

static void draw_help(void) {
    /* Twelve rows between the title and the status row. */
    static const char *const keys[][2] = {
        { "F1/2/3", "TAPES DISKS SNAPSHOTS" },
        { "F4/5/10", "SETUP MACHINE ABOUT" },
        { "ALT+M",  "MENU" },
        { "ALT+H",  "THESE KEYS" },
        { "ALT+P",  "PAUSE" },
        { "ALT+K",  "RESET BUTTON" },
        { "ALT+O",  "POWER OFF AND ON" },
        { "F4 KEYS", "JOYSTICK: ARROWS, ]" },
        { "ESC",    "BREAK" },
        { "CTRL",   "CLEAR, OS-9'S CONTROL" },
        { "TAB",    "CLEAR" },
        { "BKSP",   "LEFT ARROW" },
    };
    _Static_assert(sizeof keys / sizeof keys[0] <= ROW_STATUS - ROW_TOP,
                   "the keys fit above the status row");
    char line[TEXT_COLS + 1];
    int row = ROW_TOP;
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        snprintf(line, sizeof line, " %-8s%s", keys[i][0], keys[i][1]);
        textpage_line(s_scr, row++, line, false);
    }
}

static void draw_battery(void) {
    if (s.battery < 0) return;
    unsigned pct = (unsigned)s.battery & 0x7Fu;
    char text[12];
    snprintf(text, sizeof text, "%s %u%% ", s.battery & 0x80 ? "CHG" : "BAT",
             pct > 100u ? 100u : pct);
    textpage_put(s_scr, 0, TEXT_COLS - (int)strlen(text), text, true);
}

static bool read_battery(void) {
    uint8_t r[2];
    int was = s.battery;
    s.battery = sb_read(SB_REG_BAT, r) == SB_OK ? r[1] : -1;
    return s.battery != was;
}

static void draw(void) {
    static const char *const title[] = {
        [P_MAIN] = " PICO-COCO",          [P_TAPES] = " PICO-COCO: TAPES",
        [P_DISKS] = " PICO-COCO: DISKS",
        [P_SNAPS] = " PICO-COCO: SNAPSHOTS",
        [P_SETUP] = " PICO-COCO: SETUP",  [P_MACHINE] = " PICO-COCO: MACHINE",
        [P_ABOUT] = " PICO-COCO: ABOUT",  [P_HELP] = " PICO-COCO: KEYS",
    };
    static const char *const keys[] = {
        [P_MAIN] = " ARROWS ENTER  ESC RESUMES",  [P_TAPES] = " ENTER INSERTS  ESC BACK",
        [P_DISKS] = " < > DRIVE  ENTER  ESC BACK",
        [P_SNAPS] = " < > SLOT  ENTER  ESC BACK",
        [P_SETUP] = " < > CHANGES  ESC BACK",     [P_MACHINE] = " < > STAGES  ENTER  ESC BACK",
        [P_ABOUT] = " ESC BACK",                  [P_HELP] = " ESC RESUMES",
    };
    textpage_clear(s_scr);
    textpage_line(s_scr, 0, title[s.page], true);
    draw_battery();
    switch (s.page) {
    case P_TAPES:   draw_tapes(); break;
    case P_DISKS:   draw_disks(); break;
    case P_SNAPS:   draw_snaps(); break;
    case P_SETUP:   draw_setup(); break;
    case P_MACHINE: draw_machine(); break;
    case P_ABOUT:   draw_about(); break;
    case P_HELP:    draw_help(); break;
    default:        draw_main(); break;
    }
    if (s.name.on) {
        char line[TEXT_COLS + 1];
        int at = snprintf(line, sizeof line, " %s NAME: %s", s.name.disk ? "DISK" : "TAPE",
                          s.name.text);
        textpage_line(s_scr, ROW_STATUS, line, false);
        if (at < TEXT_COLS) textpage_put(s_scr, ROW_STATUS, at, " ", true);
        textpage_line(s_scr, ROW_KEYS, " ENTER MAKES IT  ESC CANCELS", true);
    } else {
        textpage_line(s_scr, ROW_STATUS, s.status, false);
        textpage_line(s_scr, ROW_KEYS, keys[s.page], true);
    }
    display_present(s_scr, NULL);
    display_settle();   /* the border now, not at the next key */
}

/* ---- opening the pages ------------------------------------------------------ */

static void open_tapes(void) {
    s.n_tapes = s.card ? tapeio_list(s_list, COCO_TAPE_LIST_MAX) : 0;
    s.page = P_TAPES;
    s.tape_sel = T_EJECT;
    for (unsigned i = 0; i < s.n_tapes; i++)
        if (strcmp(s_list[i].path, tapeio_inserted()) == 0) s.tape_sel = T_FIRST + (int)i;
    s.tape_top = s.tape_sel >= TAPE_ROWS ? s.tape_sel - TAPE_ROWS + 1 : 0;
    /* The list in the log too, for a check driven over the UART. */
    for (unsigned i = 0; i < s.n_tapes; i++)
        log_core1("  tapes        : %2u %s, %lu bytes, first \"%s\"%s\n", i + 1u, s_list[i].path,
                  (unsigned long)s_list[i].size, s_list[i].name,
                  s_list[i].type == 2 ? ", code" : "");
}

/* The folder s_disk_dir listed, and `select` chosen if it is there (a
 * name in the folder), else the first entry. A folder gone from the card
 * is left for DSKIO_DIR. */
static void disks_load(const char *select) {
    uint32_t t0 = time_us_32();
    dirlist_clear(&s_disks);
    if (s.card && !dskio_list(s_disk_dir, &s_disks) && strcmp(s_disk_dir, DSKIO_DIR) != 0) {
        snprintf(s_disk_dir, sizeof s_disk_dir, "%s", DSKIO_DIR);
        select = NULL;
        (void)dskio_list(s_disk_dir, &s_disks);
    }
    s.n_disks = s_disks.n;
    s.disk_up = dirlist_below(s_disk_dir, DSKIO_DIR)[0] != 0;
    s.disk_sel = s.n_disks ? disk_first() : s.disk_up ? K_FIRST : K_EMPTY;
    for (unsigned i = 0; select && i < s.n_disks; i++)
        if (strcasecmp(s_disks.e[i].name, select) == 0) s.disk_sel = disk_first() + (int)i;
    s.disk_top = s.disk_sel >= DISK_ROWS ? s.disk_sel - DISK_ROWS + 1 : 0;
    if (s_disks.total > s.n_disks)
        snprintf(s.status, sizeof s.status, " FIRST %u OF %u SHOWN", s.n_disks % 1000u,
                 s_disks.total % 100000u);
    log_core1("  disks        : %s, %u entries%s%s, listed in %lu us\n", s_disk_dir,
              s_disks.total, s_disks.total > s.n_disks ? ", not all held" : "",
              s.card ? "" : " (no card)", (unsigned long)(time_us_32() - t0));
    /* The first 16, so a big folder does not flood the log. */
    for (unsigned i = 0; i < 16u && i < s.n_disks; i++) {
        const dirlist_entry_t *e = &s_disks.e[i];
        log_core1("  disks        : %2u %s%s, %lu bytes%s\n", i + 1u, e->name,
                  (e->flags & DIRLIST_FOLDER) ? "/" : "", (unsigned long)e->size,
                  (e->flags & DIRLIST_RDONLY) ? ", read-only" : "");
    }
}

/* Opened in the folder of the disk in the drive, if there is one; else
 * where the page was last. */
static void open_disks(void) {
    s.page = P_DISKS;
    const char *in = dskio_inserted(s_drive);
    if (in[0] && dirlist_within(in, DSKIO_DIR)) {
        dirlist_folder_of(s_disk_dir, sizeof s_disk_dir, in);
        disks_load(base(in));
    } else {
        disks_load(NULL);
    }
    log_core1("  disks        : page open, drive %u\n", s_drive);
}

static void refresh_slots(void) {
    for (unsigned i = 0; i < SNAPIO_SLOTS; i++) s.used[i] = s.card && snapio_exists(i);
}

static void open_snaps(void) {
    refresh_slots();
    s.page = P_SNAPS;
    s.snap_sel = N_SAVE;
    unsigned used = 0;
    for (unsigned i = 0; i < SNAPIO_SLOTS; i++) used += s.used[i];
    log_core1("  snapshot     : page open, slot %u, %u of %u slots in use\n", s_slot + 1u, used,
              SNAPIO_SLOTS);
}

static void open_machine(void) {
    s.page = P_MACHINE;
    s.machine_sel = M_BASIC;
    s.st_ext = g_machine.extended;
    s.st_ram = ram_index(g_machine.ram_k);
    s.n_carts = s.card ? card_cart_list(s_carts, COCO_CART_LIST_MAX) : 0;
    /* The running cartridge among the card's; one the card no longer
     * lists is offered as itself, after them. */
    s.run_cart = CART_NONE;
    if (g_machine.rsdos) {
        s.run_cart = CART_RSDOS;
    } else if (g_machine.cart) {
        s.run_cart = (int)s.n_carts;
        for (unsigned i = 0; i < s.n_carts; i++)
            if (strcmp(s_carts[i], g_boot.cart_path) == 0) s.run_cart = (int)i;
    }
    s.st_cart = s.run_cart;
    for (unsigned i = 0; i < s.n_carts; i++)
        log_core1("  carts        : %2u %s\n", i + 1u, s_carts[i]);
}

static void open_about(void) {
    uint8_t r[2];
    s.page = P_ABOUT;
    s.sb_ver = sb_read(SB_REG_VER, r) == SB_OK ? r[1] : -1;
    s.temp_c = board_temp_c();
    s.about_job = s.card;
    if (s.card) card_roms_mounted(&s_job, NULL, NULL, NULL);
    log_core1("  about        : %s, %s, id %s, %lu MHz, southbridge %d, die %d C, %s, settings %s\n",
              PICO_COCO_VERSION, g_board.chip, g_board.unique_id,
              (unsigned long)(g_board.clk_sys_hz / 1000000u), s.sb_ver, s.temp_c,
              machine_name(g_machine.extended, g_machine.ram_k),
              settingsio_error()[0] ? settingsio_error() : settingsio_state_str(settingsio_state()));
}

/* ---- actions --------------------------------------------------------------- */

static void save_settings(void) {
    if (!s.card) { say(" NO CARD: NOT SAVED", ""); return; }
    /* Static: four disks' paths make it too big for core 1's stack. */
    static settings_t out;
    out = s_file;
    /* The machine as it runs, not as the Machine page has it staged
     * (EL §10). */
    out.extended = g_machine.extended;
    out.ram_k = g_machine.ram_k;
    settings_card_name(SETTINGS_CART_DIR, g_machine.cart ? g_boot.cart_path : "", out.cart);
    out.volume = g_ui.volume;
    out.perf = g_ui.perf_line;
    out.status = g_ui.status;
    if (s_bkl_set) out.backlight = g_ui.backlight;
    out.fast_tape = g_ui.fast_tape;
    out.vdg_t1 = g_ui.vdg_t1;
    out.artefacts = g_ui.artefacts;
    out.artefact_phase = g_ui.artefact_phase;
    out.palette = g_ui.palette;
    out.smartwatch = g_ui.smartwatch;
    out.disk_sounds = g_ui.disk_sounds;
    /* A layout a loaded file chose is not the user's choice, so the file
     * keeps the one it had (§11.3). */
    if (!keymapio_chosen_by()[0])
        snprintf(out.layout, sizeof out.layout, "%s", g_ui.layout ? g_ui.layout->name : "");
    settings_card_name(SETTINGS_TAPE_DIR, tapeio_chosen() ? tapeio_inserted() : "", out.boot_tape);
    for (unsigned d = 0; d < COCO_DISK_DRIVES; d++)
        settings_card_name(SETTINGS_DISK_DIR, dskio_inserted(d), out.boot_disk[d]);
    const char *err = settingsio_save(&out);
    if (!err) s_file = out;
    say(err ? " NOT SAVED: %.20s" : " SETTINGS SAVED", err);
}

/* Apply and restart (§14): a power-on of the staged machine, or the
 * running one left as it is, and the status row says why. */
static void apply_machine(void) {
    /* Refused while a recording is not on the card yet (EL §10): the
     * power-on would drop it. */
    if (tapeio_unsaved(s.m)) { say(" A RECORDING IS NOT SAVED", ""); return; }
    bool ext = s.st_ext;
    const char *why;
    rom_id_t basic = roms_basic_for(&g_boot.job, k_rams[s.st_ram], &ext, &why);
    if (basic == ROM_UNKNOWN || ext != s.st_ext) { say(" %.30s", why ? why : "NO BASIC"); return; }
    machine_t mc = { .ram_k = k_rams[s.st_ram], .basic = basic, .extended = ext,
                     .cart = s.st_cart != CART_NONE, .rsdos = s.st_cart == CART_RSDOS };
    if (mc.rsdos) {
        const char *no = roms_rsdos_why(mc.ram_k, mc.extended);
        if (no) { say(" %.30s", no); return; }
    }
    if (s.st_cart != s.run_cart) {
        if (s.st_cart == CART_RSDOS) {
            say(" READING DISK11.ROM...", "");
            draw();
            uint32_t len = 0;
            const char *err = card_disk_rom_load(&g_boot.job, g_boot.cart, &len);
            if (err) { say(" RS-DOS: %.22s", err); return; }
            g_boot.cart_len = len;
            snprintf(g_boot.cart_path, sizeof g_boot.cart_path, "%s", CARD_RSDOS);
        } else if (s.st_cart >= 0) {
            say(" READING %.20s...", base(s_carts[s.st_cart]));
            draw();
            uint32_t len = 0;
            const char *err = card_cart_load(s_carts[s.st_cart], g_boot.cart, &len);
            if (err) { say(" CARTRIDGE: %.20s", err); return; }
            g_boot.cart_len = len;
            snprintf(g_boot.cart_path, sizeof g_boot.cart_path, "%s", s_carts[s.st_cart]);
            keymapio_file_loaded(g_boot.cart_path);   /* a layout may name it (§11.3) */
        } else {
            g_boot.cart_len = 0;
            g_boot.cart_path[0] = 0;
        }
    }
    log_core1("  machine      : restarting as %s%s%s\n", machine_name(mc.extended, mc.ram_k),
              mc.rsdos ? ", RS-DOS" : mc.cart ? ", cartridge " : "",
              mc.rsdos ? "" : mc.cart ? g_boot.cart_path : "");
    g_ui.restart_cfg = mc;
    g_ui.restart = true;
    s.done = true;
}

static void exit_to_shell(void) {
    if (tapeio_unsaved(s.m)) { say(" A RECORDING IS NOT SAVED", ""); return; }
    if (!picocalc_shell_loader_present()) { say(" NOT STARTED BY UF2 LOADER", ""); return; }
    if (!picocalc_shell_card_present()) { say(" NO CARD TO LOAD THE SHELL", ""); return; }
    log_core1("  menu         : exit to the shell\n");
    say(" EXITING TO THE SHELL...", "");
    draw();
    if (s.card) storage_unmount();
    busy_wait_ms(100);   /* the UART's last line */
    picocalc_shell_exit();
    s.card = storage_mount() == 0;
    say(" CANNOT RETURN TO THE SHELL", "");
}

static void open_item(void) {
    s.status[0] = 0;
    switch (s.item) {
    case I_TAPES:   open_tapes(); break;
    case I_DISKS:   open_disks(); break;
    case I_SNAPS:   open_snaps(); break;
    case I_SETUP:   s.page = P_SETUP; s.setup_sel = D_STATUS; break;
    case I_MACHINE: open_machine(); break;
    case I_RESET:   g_ui.reset = true; s.done = true; break;
    case I_POWER:
        /* Off and on (§13.3): RAM cleared, BASIC started cold, the
         * media left in. Refused as Apply is, for a recording. */
        if (tapeio_unsaved(s.m)) { say(" A RECORDING IS NOT SAVED", ""); break; }
        g_ui.power = true;
        s.done = true;
        break;
    case I_SAVE:    save_settings(); break;
    case I_SHELL:   exit_to_shell(); break;
    case I_ABOUT:   open_about(); break;
    }
}

/* ---- naming a new tape ------------------------------------------------------ */

static void name_start(bool disk) {
    s.name.on = true;
    s.name.disk = disk;
    if (disk) dskio_next_name(s_disk_dir, s.name.text);
    else tapeio_next_name(s.name.text);
    s.name.len = (unsigned)strlen(s.name.text);
    s.status[0] = 0;
}

/* Letters, digits and -: a name any card takes and the VDG shows. */
static bool name_char(uint8_t c) {
    return c < 0x80u && (isalnum(c) || c == '-');
}

static void name_make(void) {
    if (!s.name.len) return;
    s.name.on = false;
    if (s.name.disk) {
        const char *err = dskio_new(s.m, s_drive, s_disk_dir, s.name.text);
        if (err) { say(" NO NEW DISK: %.18s", err); return; }
        disks_load(base(dskio_inserted(s_drive)));
        say(" IN DRIVE %c: DSKINI IT", (char[]){ (char)('0' + s_drive), 0 });
        return;
    }
    const char *err = tapeio_new(s.m, s.name.text);
    if (err) { say(" NO NEW TAPE: %.18s", err); return; }
    say(" IN: %.12s, CSAVE ONTO IT", base(tapeio_inserted()));
    s.n_tapes = tapeio_list(s_list, COCO_TAPE_LIST_MAX);
    for (unsigned i = 0; i < s.n_tapes; i++)
        if (strcmp(s_list[i].path, tapeio_inserted()) == 0) s.tape_sel = T_FIRST + (int)i;
    if (s.tape_sel >= s.tape_top + TAPE_ROWS) s.tape_top = s.tape_sel - TAPE_ROWS + 1;
}

static void key_name(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_ENTER: name_make(); return;
    case PICOCALC_KEY_ESC:   s.name.on = false; return;
    case PICOCALC_KEY_BACKSPACE:
    case PICOCALC_KEY_DEL:
        if (s.name.len) s.name.text[--s.name.len] = 0;
        return;
    }
    if (name_char(c) && s.name.len < COCO_MEDIA_NAME_MAX) {
        s.name.text[s.name.len++] = (char)toupper(c);
        s.name.text[s.name.len] = 0;
    }
}

/* ---- keys ------------------------------------------------------------------- */

static void key_tapes(uint8_t c) {
    int last = T_FIRST + (int)s.n_tapes - 1;
    switch (c) {
    case PICOCALC_KEY_UP:   if (s.tape_sel > 0) s.tape_sel--; break;
    case PICOCALC_KEY_DOWN: if (s.tape_sel < last) s.tape_sel++; break;
    case PICOCALC_KEY_ENTER:
        if (s.tape_sel == T_EJECT) {
            const char *err = tapeio_insert(s.m, NULL);
            say(err ? " NOT EJECTED: %.18s" : " TAPE EJECTED", err);
        } else if (s.tape_sel == T_REWIND) {
            if (!s.m->tape.inserted) { say(" THE DECK IS EMPTY", ""); return; }
            tapeio_rewind(s.m);
            say(" REWOUND", "");
            return;
        } else if (s.tape_sel == T_NEW) {
            if (!s.card) { say(" NO CARD", ""); return; }
            name_start(false);
            return;
        } else {
            const tapeio_entry_t *e = &s_list[s.tape_sel - T_FIRST];
            const char *err = tapeio_insert(s.m, e->path);
            if (err) { say(" NOT IN: %.22s", err); return; }
            if (e->name[0]) say(e->type == 2 ? " IN: CLOADM\"%.8s\"" : " IN: CLOAD\"%.8s\"", e->name);
            else say(" IN THE DECK: %.18s", base(e->path));
        }
        s.page = P_MAIN;
        return;
    case PICOCALC_KEY_ESC:
        s.page = P_MAIN;
        return;
    }
    if (s.tape_sel < s.tape_top) s.tape_top = s.tape_sel;
    if (s.tape_sel >= s.tape_top + TAPE_ROWS) s.tape_top = s.tape_sel - TAPE_ROWS + 1;
}

/* Up a folder, the one left selected. */
static void disks_up(void) {
    char left[COCO_PATH_MAX];
    if (!dirlist_up(s_disk_dir, DSKIO_DIR, left, sizeof left)) return;
    s.status[0] = 0;
    disks_load(left);
}

static void key_disks(uint8_t c) {
    int last = disk_first() + (int)s.n_disks - 1;
    if (last < K_FIRST && s.disk_up) last = K_FIRST;
    switch (c) {
    case PICOCALC_KEY_BACKSPACE:
        disks_up();
        return;
    case PICOCALC_KEY_UP:   if (s.disk_sel > 0) s.disk_sel--; break;
    case PICOCALC_KEY_DOWN: if (s.disk_sel < last) s.disk_sel++; break;
    case PICOCALC_KEY_LEFT:
    case PICOCALC_KEY_RIGHT:
        s_drive = (s_drive + (c == PICOCALC_KEY_RIGHT ? 1u : COCO_DISK_DRIVES - 1u)) % COCO_DISK_DRIVES;
        s.status[0] = 0;
        break;
    case PICOCALC_KEY_ENTER: {
        char d[2] = { (char)('0' + s_drive), 0 };
        if (s.disk_sel == K_EMPTY) {
            const char *err = dskio_insert(s.m, s_drive, NULL);
            if (err) say(" NOT EMPTIED: %.18s", err);
            else say(" DRIVE %s EMPTIED", d);
        } else if (s.disk_sel == K_NEW) {
            if (!s.card) { say(" NO CARD", ""); return; }
            name_start(true);
        } else if (s.disk_sel == K_FIRST && s.disk_up) {
            disks_up();
        } else if (disk_entry(s.disk_sel)) {
            const dirlist_entry_t *e = disk_entry(s.disk_sel);
            char full[COCO_PATH_MAX];
            if (!dirlist_join(full, sizeof full, s_disk_dir, e->name)) {
                say(" THE PATH IS TOO LONG", "");
                return;
            }
            if (e->flags & DIRLIST_FOLDER) {
                s.status[0] = 0;
                snprintf(s_disk_dir, sizeof s_disk_dir, "%s", full);
                disks_load(NULL);
                return;
            }
            const char *err = dskio_insert(s.m, s_drive, full);
            if (err) { say(" NOT IN: %.22s", err); return; }
            char line[TEXT_COLS + 1];
            snprintf(line, sizeof line, " IN DRIVE %c: %.18s", d[0], e->name);
            say("%s", line);
        }
        return;
    }
    case PICOCALC_KEY_ESC:
        s.page = P_MAIN;
        return;
    }
    if (s.disk_sel < s.disk_top) s.disk_top = s.disk_sel;
    if (s.disk_sel >= s.disk_top + DISK_ROWS) s.disk_top = s.disk_sel - DISK_ROWS + 1;
}

/* ---- snapshots ---------------------------------------------------------------- */

/* The machine as a state needs it (§12.5, EL §8.5): powered on here, on
 * core 1 with the guest parked, as the load that follows changes it,
 * with core 0's own power-on. For a state for another machine, and for
 * one for this machine while it waits on the tape, which a load cannot
 * take over. False, with the status row saying why and the machine as
 * it was, when the card cannot give the machine. */
static bool snap_switch(const snap_info_t *in) {
    machine_t mc;
    snap_status_t st;
    const char *why = snapio_machine(s.m, in, &mc, &st);
    if (why) {
        log_core1("  snapshot     : slot %u: %s; not loaded\n", s_slot + 1u, why);
        say(" %.30s", why);
        return false;
    }
    log_core1("  snapshot     : slot %u is the %s%s%s's; powering on as it\n", s_slot + 1u,
              machine_name(mc.extended, mc.ram_k), mc.rsdos ? " with RS-DOS" : mc.cart ? " with " : "",
              mc.rsdos ? "" : mc.cart ? base(g_boot.cart_path) : "");
    core0_power_on(s.m, &mc);
    return true;
}

/* The state in the slot: on success the menu closes, and the guest
 * resumes from it. A state for another machine powers on as that
 * machine first (snap_switch). A load whose second pass failed has left
 * a machine part old and part new: it is not resumed, but powered on
 * again as it is configured (snapio.h). Refused, before anything, while
 * a recording is not on the card, as the Machine page's Apply is. */
static void snap_load(void) {
    if (tapeio_unsaved(s.m)) { say(" A RECORDING IS NOT SAVED", ""); return; }
    snap_info_t in;
    memset(&in, 0, sizeof in);
    bool recovered, changed = false;
    uint32_t t0 = time_us_32();
    snap_status_t st = snapio_check(s.m, s_slot, &in, &recovered);
    bool busy = coco_tape_pending(s.m) != TAPE_NONE || coco_disk_busy(s.m);
    if (snapshot_other_machine(st) || (st == SNAP_OK && busy)) {
        if (!snap_switch(&in)) return;
        st = SNAP_OK;
        /* A load that fails now leaves a machine just powered on, not
         * the one the user had: it is powered on again, as below. */
        changed = true;
    }
    if (st == SNAP_OK) {
        bool torn;
        st = snapio_load(s.m, &torn);
        changed = st != SNAP_OK && (changed || torn);
    }
    log_core1("  snapshot     : load slot %u: %s%s, %lu us\n", s_slot + 1u, snapshot_status_str(st),
              recovered ? " (from the unpublished .NEW)" : "", (unsigned long)(time_us_32() - t0));
    if (st == SNAP_OK) {
        /* A tape the state had in the deck and the card no longer has:
         * said on the status line, since the menu closes (snapio.h). */
        if (snapio_said()[0]) core1_note(snapio_said() + 1);
        s.done = true;
        return;
    }
    if (changed) {
        log_core1("  snapshot     : failed after the machine had changed; powering on again\n");
        core1_note("NOT LOADED: POWERED ON AGAIN");
        g_ui.restart_cfg = g_machine;
        g_ui.restart = true;
        s.done = true;
        return;
    }
    if (st == SNAP_IO && !s.used[s_slot]) {
        char line[TEXT_COLS + 1];
        snprintf(line, sizeof line, " SLOT %u IS EMPTY", s_slot + 1u);
        say("%s", line);
    } else {
        say(" NOT LOADED: %.19s", snapshot_status_str(st));
    }
}

static void key_snaps(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_UP:   if (s.snap_sel > 0) s.snap_sel--; break;
    case PICOCALC_KEY_DOWN: if (s.snap_sel < N_COUNT - 1) s.snap_sel++; break;
    case PICOCALC_KEY_LEFT:
    case PICOCALC_KEY_RIGHT:
        s_slot = (s_slot + (c == PICOCALC_KEY_RIGHT ? 1u : SNAPIO_SLOTS - 1u)) % SNAPIO_SLOTS;
        s.status[0] = 0;
        break;
    case PICOCALC_KEY_ENTER:
        if (!s.card) { say(" NO CARD", ""); break; }
        s.status[0] = 0;
        if (s.snap_sel == N_SAVE) {
            say(" SAVING...", "");
            draw();
            uint32_t us;
            snap_status_t st = snapio_save(s.m, s_slot, &us);
            log_core1("  snapshot     : save slot %u: %s, %lu us\n", s_slot + 1u,
                      snapshot_status_str(st), (unsigned long)us);
            if (st == SNAP_OK) {
                char line[TEXT_COLS + 1];
                snprintf(line, sizeof line, " SAVED IN SLOT %u", s_slot + 1u);
                say("%s", line);
            } else {
                say(" NOT SAVED: %.20s", snapshot_status_str(st));
            }
            refresh_slots();
        } else if (s.snap_sel == N_LOAD) {
            say(" LOADING...", "");
            draw();
            s.status[0] = 0;
            snap_load();
        } else if (s.snap_sel == N_DELETE) {
            /* Destructive, so logged (EL §8.5). */
            bool gone = snapio_delete(s_slot);
            log_core1("  snapshot     : delete slot %u: %s\n", s_slot + 1u,
                      gone ? "deleted" : "nothing there");
            say(gone ? " DELETED" : " NOTHING TO DELETE", "");
            refresh_slots();
        }
        break;
    case PICOCALC_KEY_ESC:
        s.page = P_MAIN;
        break;
    }
}

static void key_main(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_UP:    s.item = (s.item + I_COUNT - 1) % I_COUNT; break;
    case PICOCALC_KEY_DOWN:  s.item = (s.item + 1) % I_COUNT; break;
    case PICOCALC_KEY_ENTER: open_item(); break;
    case PICOCALC_KEY_ESC:   s.done = true; break;
    }
}

static void set_backlight(int dir) {
    int v = (int)g_ui.backlight + dir;
    v = v < 1 ? 1 : v > 15 ? 15 : v;
    g_ui.backlight = (unsigned)v;
    s_bkl_set = true;
    (void)sb_write(SB_REG_BKL, (uint8_t)(v * (int)BKL_STEP), NULL);
}

/* Standard, then keymapio's list, round and round, as the siblings' Keys
 * row. */
/* The layouts in a row: the left stick's, analog then digital, the
 * standard map, the right stick's, digital then analog, then the card's
 * (keymatrix.h), so Left and Right from the standard map choose a stick
 * and a second press its analog way. Left and Right stop at the ends;
 * Enter (0) steps right and comes round. */
static void step_layout(int dir) {
    int n = (int)keymapio_count() + 1, at = KEYLAYOUT_STANDARD_AT;
    for (unsigned i = 0; i < keymapio_count(); i++)
        if (keymapio_get(i) == g_ui.layout) at = (int)i + (i >= KEYLAYOUT_STANDARD_AT);
    if (dir) {
        at += dir;
        if (at < 0 || at >= n) return;
    } else {
        at = (at + 1) % n;
    }
    keymapio_choose(at == KEYLAYOUT_STANDARD_AT
                        ? NULL
                        : keymapio_get((unsigned)(at - (at > KEYLAYOUT_STANDARD_AT))));
}

static void key_setup(uint8_t c) {
    int dir = 0;
    switch (c) {
    case PICOCALC_KEY_UP:    s.setup_sel = (s.setup_sel + D_COUNT - 1) % D_COUNT; return;
    case PICOCALC_KEY_DOWN:  s.setup_sel = (s.setup_sel + 1) % D_COUNT; return;
    case PICOCALC_KEY_LEFT:  dir = -1; break;
    case PICOCALC_KEY_RIGHT: dir = 1; break;
    case PICOCALC_KEY_ENTER: dir = 0; break;
    case PICOCALC_KEY_ESC:   s.page = P_MAIN; return;
    default: return;
    }
    s.status[0] = 0;
    switch (s.setup_sel) {
    case D_STATUS: g_ui.status = !g_ui.status; break;
    case D_PERF:   g_ui.perf_line = !g_ui.perf_line; break;
    case D_BACKLIGHT:
        if (dir) set_backlight(dir);
        break;
    case D_VOLUME:
        if (dir) {
            int v = (int)g_ui.volume + dir;
            g_ui.volume = (unsigned)(v < 0 ? 0 : v > 8 ? 8 : v);
        }
        break;
    case D_KEYS:
        step_layout(dir);
        break;
    case D_FAST:
        g_ui.fast_tape = !g_ui.fast_tape;
        say(g_ui.fast_tape ? "" : " THE TAPE PLAYS AT ITS OWN PACE", "");
        break;
    case D_VDG:
        g_ui.vdg_t1 = !g_ui.vdg_t1;
        display_set_t1(g_ui.vdg_t1);
        break;
    case D_ARTEFACTS:
        g_ui.artefacts = (uint8_t)((g_ui.artefacts + 3u + (dir < 0 ? -1 : 1)) % 3u);
        display_set_artefacts(g_ui.artefacts, g_ui.artefact_red);
        log_core1("  setup        : artifacts %s\n", settings_artefacts_str(g_ui.artefacts));
        break;
    case D_PHASE:
        /* Blue, red, random: blue and red take effect at once, without a
         * reset, as no real CoCo 1 or 2 could (§9.5); random keeps the
         * phase drawn until the next reset. */
        g_ui.artefact_phase = (uint8_t)((g_ui.artefact_phase + 3u + (dir < 0 ? -1 : 1)) % 3u);
        if (g_ui.artefact_phase == SETTINGS_PHASE_RANDOM) {
            say(" A NEW PHASE AT EACH RESET", "");
        } else {
            g_ui.artefact_red = g_ui.artefact_phase == SETTINGS_PHASE_RED;
            display_set_artefacts(g_ui.artefacts, g_ui.artefact_red);
        }
        log_core1("  setup        : artifact phase %s\n",
                  settings_phase_str(g_ui.artefact_phase));
        break;
    case D_PALETTE:
        g_ui.palette = g_ui.palette == SETTINGS_PALETTE_DATASHEET ? SETTINGS_PALETTE_PRIMARY
                                                                  : SETTINGS_PALETTE_DATASHEET;
        display_set_palette(g_ui.palette == SETTINGS_PALETTE_DATASHEET);
        log_core1("  setup        : palette %s\n", settings_palette_str(g_ui.palette));
        break;
    case D_DISK_SOUNDS:
        g_ui.disk_sounds = !g_ui.disk_sounds;
        log_core1("  setup        : disk sounds %s\n", g_ui.disk_sounds ? "on" : "off");
        break;
    case D_CLOCK:
        g_ui.smartwatch = !g_ui.smartwatch;
        if (g_ui.smartwatch) {
            /* Where its time comes from (rtc.h). */
            char t[24];
            int off = g_clock.offset_min, a = off < 0 ? -off : off;
            snprintf(t, sizeof t, "THE SHELL'S, UTC%c%02d:%02d", off < 0 ? '-' : '+', a / 60,
                     a % 60);
            say(" TIME: %.24s", g_clock.source == RTC_SHELL    ? t
                                : g_clock.source == RTC_DS3231 ? "THE DS3231'S, AS UTC"
                                                               : "NONE, FROM 2000");
        }
        break;
    }
}

static void key_machine(uint8_t c) {
    int dir = c == PICOCALC_KEY_RIGHT ? 1 : -1;
    switch (c) {
    case PICOCALC_KEY_UP:   s.machine_sel = (s.machine_sel + M_COUNT - 1) % M_COUNT; break;
    case PICOCALC_KEY_DOWN: s.machine_sel = (s.machine_sel + 1) % M_COUNT; break;
    case PICOCALC_KEY_LEFT:
    case PICOCALC_KEY_RIGHT:
        switch (s.machine_sel) {
        case M_BASIC:
            /* A 4 KB CoCo runs Color BASIC 1.1 alone (roms.h). */
            if (k_rams[s.st_ram] <= 4u) { say(" EXTENDED NEEDS 16K", ""); return; }
            s.st_ext = !s.st_ext;
            break;
        case M_RAM:
            s.st_ram = (s.st_ram + 4u + (unsigned)dir) % 4u;
            if (k_rams[s.st_ram] <= 4u) s.st_ext = false;
            break;
        case M_CART: {
            /* None, RS-DOS, the card's, and the running one if the card
             * has it no longer: as places 0, 1, 2 on. */
            int n = 2 + (int)s.n_carts + (s.run_cart == (int)s.n_carts ? 1 : 0);
            int at = s.st_cart == CART_NONE ? 0 : s.st_cart == CART_RSDOS ? 1 : s.st_cart + 2;
            at = ((at + dir) % n + n) % n;
            s.st_cart = at == 0 ? CART_NONE : at == 1 ? CART_RSDOS : at - 2;
            break;
        }
        case M_APPLY: return;
        }
        say(machine_staged() ? " APPLY RESTARTS: PROGRAM LOST" : "", "");
        break;
    case PICOCALC_KEY_ENTER:
        if (s.machine_sel == M_APPLY) {
            if (machine_staged()) apply_machine();
            else say(" NOTHING TO APPLY", "");
        }
        break;
    case PICOCALC_KEY_ESC:
        say(machine_staged() ? " NOT APPLIED" : "", "");
        s.page = P_MAIN;
        break;
    }
}

/* Presses only: releases and the MCU's held reports move nothing. Alt is
 * tracked so that Alt+M closes the menu as it opened it. */
static void keys(void) {
    uint8_t st, c;
    while (!s.done && (kbd_pop(&st, &c) || kbd_pop_uart(&st, &c))) {
        if (c == PICOCALC_KEY_ALT) { s.alt = st != KEY_EV_RELEASED; continue; }
        if (st != KEY_EV_PRESSED) continue;
        if (s.alt && (c == 'm' || c == 'M')) { s.done = true; break; }
        if (s.name.on) {
            key_name(c);
            draw();
            continue;
        }
        switch (s.page) {
        case P_SETUP:   key_setup(c); break;
        case P_MACHINE: key_machine(c); break;
        case P_TAPES:   key_tapes(c); break;
        case P_DISKS:   key_disks(c); break;
        case P_SNAPS:   key_snaps(c); break;
        case P_ABOUT:
        case P_HELP:
            if (c == PICOCALC_KEY_ESC || c == PICOCALC_KEY_ENTER) s.page = P_MAIN;
            break;
        default:        key_main(c); break;
        }
        /* A page a key opened goes back to the guest, not the main page. */
        if (s.direct && s.page == P_MAIN) s.done = true;
        if (!s.done) draw();
    }
}

void menu_run(coco_t *m, unsigned page, bool alt) {
    memset(&s, 0, sizeof s);
    s.m = m;
    s.alt = alt;

    s.card = sd_present() && storage_mount() == 0;
    if (!s.card) say(" NO CARD", "");
    /* A written track the disk controller has in hand goes back now:
     * whatever the menu does next, a state saved, a power-on, a disk
     * changed or the shell, it is on the card first (dskio.h). */
    if (s.card) dskio_flush(m);
    /* The card's layouts afresh: a file may have been added or edited
     * on a computer since (keymapio.h). A layout a file chose says so
     * (§11.3), then the tape's last word. */
    if (s.card) keymapio_scan();
    if (g_ui.layout && keymapio_chosen_by()[0]) say(" KEYS CHOSEN BY %.15s", keymapio_chosen_by());
    const char *t = tapeio_said();
    if (!s.status[0] && t[0]) say(" %.30s", t);
    if (!s.status[0] && settingsio_error()[0]) say(" %.30s", settingsio_error());
    if (!s.status[0] && keymapio_error()[0]) say(" %.30s", keymapio_error());

    s.direct = page != KM_PAGE_MAIN;
    char said[sizeof s.status];
    memcpy(said, s.status, sizeof said);
    switch (page) {
    case KM_PAGE_TAPE:     s.item = I_TAPES; open_item(); break;
    case KM_PAGE_DISC:     s.item = I_DISKS; open_item(); break;
    case KM_PAGE_SNAPSHOT: s.item = I_SNAPS; open_item(); break;
    case KM_PAGE_SETUP:    s.item = I_SETUP; open_item(); break;
    case KM_PAGE_MACHINE:  s.item = I_MACHINE; open_item(); break;
    case KM_PAGE_ABOUT:    s.item = I_ABOUT; open_item(); break;
    case KM_PAGE_HELP:     s.page = P_HELP; break;
    default: break;
    }
    if (!s.status[0]) memcpy(s.status, said, sizeof said);

    s.battery = -1;
    (void)read_battery();

    /* The lines describe the running machine, and are hidden while the
     * menu is open; the first present after it closes draws them. */
    display_perf("");
    display_status("");

    log_core1("  menu         : open at page %u%s\n", page, s.card ? "" : " (no card)");
    draw();

    uint32_t last_poll = time_us_32(), last_bat = last_poll;
    while (!s.done) {
        if (time_us_32() - last_poll >= POLL_US) {
            last_poll = time_us_32();
            g_c1.key_events += kbd_poll();
            g_c1.polls++;
            keys();
            if (!s.done && last_poll - last_bat >= BAT_POLL_US) {
                last_bat = last_poll;
                if (read_battery()) draw();
            }
        }
        log_pump();
        /* Never sleep_us on core 1 (hardware-notes.md §9.7). */
        busy_wait_us_32(500);
    }

    if (s.card) storage_unmount();
    display_invalidate();
    log_core1("  menu         : closed\n");
}

/* ---- pause ------------------------------------------------------------------ */

/* The menu's page for a key, or -1: Alt+M, Alt+H and the function keys
 * (§14), from the same table the guest's keys come from. */
static int menu_key(bool alt, uint8_t c) {
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (!(e->flags & KM_MENU)) continue;
        bool layer = (e->flags & KM_ALT) != 0;
        if (layer && alt && (c == e->code || c == e->code + ('a' - 'A'))) return e->row;
        if (!layer && c == e->code) return e->row;
    }
    return -1;
}

int pause_run(bool alt_held, bool *alt_out) {
    char line[COCO_TEXT_COLS + 1];
    status_paused_format(line);
    display_status(line);

    uint8_t r[2] = { 0, 0 };
    bool read = sb_read(SB_REG_BKL, r) == SB_OK;
    uint8_t level = read ? r[1] : BKL_LOWEST;
    bool dimmed = read && level != BKL_LOWEST && sb_write(SB_REG_BKL, BKL_LOWEST, NULL) == SB_OK;
    log_core1("  pause        : paused, backlight %s\n",
              !read ? "unread, left" : dimmed ? "dimmed" : "already lowest");

    bool alt = alt_held, done = false;
    int page = -1;
    uint32_t last_poll = time_us_32();
    while (!done) {
        if (time_us_32() - last_poll >= POLL_US) {
            last_poll = time_us_32();
            g_c1.key_events += kbd_poll();
            g_c1.polls++;
            uint8_t st, c;
            while (!done && (kbd_pop(&st, &c) || kbd_pop_uart(&st, &c))) {
                if (c == PICOCALC_KEY_ALT) { alt = st != KEY_EV_RELEASED; continue; }
                if (st != KEY_EV_PRESSED) continue;
                if (c == PICOCALC_KEY_CTRL || c == PICOCALC_KEY_SHIFT_L ||
                    c == PICOCALC_KEY_SHIFT_R) continue;
                if (alt && (c == 'P' || c == 'p')) continue;
                page = menu_key(alt, c);
                done = true;
            }
        }
        log_pump();
        busy_wait_us_32(500);
    }

    if (dimmed) (void)sb_write(SB_REG_BKL, level, NULL);
    display_status("");
    log_core1("  pause        : resumed%s\n", page >= 0 ? " into the menu" : "");
    *alt_out = alt;
    return page;
}
