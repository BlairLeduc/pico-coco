/* menu.c — the emulator's menu and pause (menu.h, design.md §14).
 *
 * pico-oric's menu, page for page and row for row, so that the family
 * reads as one: the same items in the same order, the same function
 * keys, and Esc going back a page, or to the guest from the main page or
 * a page a key opened. On the CoCo's 32 cells and in capitals. The Discs
 * and Snapshots items are in their places and say when they come (M13,
 * M10). The Machine page stages BASIC, the RAM and the cartridge (§14).
 * Setup has no keys row and no fast tape yet: the layouts and the
 * signal are M12's.
 */

#include "menu.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "picocalc_shell.h"

#include "board.h"
#include "card.h"
#include "core1.h"
#include "display.h"
#include "handoff.h"
#include "kbd.h"
#include "keymatrix.h"
#include "log.h"
#include "pico_coco_version.h"
#include "roms.h"
#include "sd.h"
#include "settingsio.h"
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

enum { I_TAPES, I_DISCS, I_SNAPS, I_SETUP, I_MACHINE, I_RESET, I_SAVE, I_SHELL, I_ABOUT,
       I_COUNT };

enum { D_STATUS, D_PERF, D_BACKLIGHT, D_VOLUME, D_COUNT };

/* The Tapes page: three things to do with the deck, then the card's
 * tapes. */
enum { T_EJECT, T_REWIND, T_NEW, T_FIRST };
#define TAPE_ROWS (ROW_STATUS - 1 - (ROW_TOP + 1))

/* The Machine page: BASIC, the RAM and the cartridge staged, and the
 * power-on that applies them. */
enum { M_BASIC, M_RAM, M_CART, M_APPLY, M_COUNT };

static coco_frame_t *s_scr;
static settings_t    s_file;       /* what the file says, for the save */
static bool          s_bkl_set;
static card_job_t    s_job;        /* About's look at the card         */

static tapeio_entry_t s_list[COCO_TAPE_LIST_MAX];
static char           s_carts[COCO_CART_LIST_MAX][COCO_PATH_MAX];

static const unsigned k_rams[4] = { 4u, 16u, 32u, 64u };

static struct {
    coco_t  *m;
    bool     card;
    bool     alt;
    bool     done;
    bool     direct;          /* opened at a page: closing it resumes */
    int      item;
    enum { P_MAIN, P_TAPES, P_SETUP, P_MACHINE, P_ABOUT, P_HELP } page;
    char     status[TEXT_COLS + 1];
    int      battery;

    int      setup_sel;

    unsigned n_tapes;
    int      tape_sel, tape_top;

    int      machine_sel;
    bool     st_ext;           /* staged: applied only by a power-on */
    unsigned st_ram;           /* index into k_rams                  */
    int      st_cart;          /* -1 none, else into s_carts         */
    int      run_cart;         /* the running cartridge's, -2 unknown */
    unsigned n_carts;

    struct {
        bool     on;
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

/* The machine as a line: "64K EXTENDED COLOR BASIC". */
static const char *machine_name(bool ext, unsigned ram_k) {
    static char text[TEXT_COLS + 1];
    snprintf(text, sizeof text, "%uK %s", ram_k, ext ? "EXTENDED COLOR BASIC" : "COLOR BASIC");
    return text;
}

/* ---- drawing ------------------------------------------------------------- */

static void draw_main(void) {
    static const char *const items[I_COUNT] = {
        " TAPES...", " DISCS...", " SNAPSHOTS...", " SETUP...", " MACHINE...", " RESET",
        " SAVE SETTINGS", " EXIT TO SHELL", " ABOUT...",
    };
    for (int i = 0; i < I_COUNT; i++)
        textpage_line(s_scr, ROW_TOP + i, items[i], i == s.item);
    char line[TEXT_COLS + 1];
    const char *in = tapeio_inserted();
    snprintf(line, sizeof line, " TAPE: %.25s", in[0] ? base(in) : "NONE");
    textpage_line(s_scr, ROW_TOP + I_COUNT + 1, line, false);
    snprintf(line, sizeof line, " %.30s", machine_name(g_machine.extended, g_machine.ram_k));
    textpage_line(s_scr, ROW_TOP + I_COUNT + 2, line, false);
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
        }
        textpage_line(s_scr, ROW_TOP + i, line, i == s.setup_sel);
    }
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
            snprintf(line, sizeof line, "%cBASIC   < %s >", mark, s.st_ext ? "EXTENDED" : "COLOR");
            break;
        case M_RAM:
            snprintf(line, sizeof line, "%cRAM     < %uK >", mark, k_rams[s.st_ram]);
            break;
        case M_CART:
            snprintf(line, sizeof line, "%cCART    < %.16s >", mark,
                     s.st_cart < 0 ? "NONE" : s.st_cart == (int)s.n_carts ? base(g_boot.cart_path)
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
    if (!s.card) textpage_line(s_scr, ROW_TOP + M_COUNT + 7, " - NO CARD: NO CARTRIDGES.", false);
}

static void about_rom(int row, rom_id_t id) {
    char line[TEXT_COLS + 1];
    const char *word = !s.about_job ? "" : s_job.found[id] ? "FOUND" : "--";
    bool running = (id == ROM_COCO_EXTBAS11 && g_machine.extended) || id == g_boot.job.basic;
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
    static const char *const keys[][2] = {
        { "F1",     "TAPES" },
        { "F4",     "SETUP" },
        { "F5",     "MACHINE" },
        { "F10",    "ABOUT" },
        { "ALT+M",  "MENU" },
        { "ALT+H",  "THESE KEYS" },
        { "ALT+P",  "PAUSE" },
        { "ALT+K",  "RESET BUTTON" },
        { "ESC",    "BREAK" },
        { "TAB",    "CLEAR" },
        { "BKSP",   "LEFT ARROW" },
    };
    char line[TEXT_COLS + 1];
    int row = ROW_TOP;
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        snprintf(line, sizeof line, " %-8s %s", keys[i][0], keys[i][1]);
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
        [P_SETUP] = " PICO-COCO: SETUP",  [P_MACHINE] = " PICO-COCO: MACHINE",
        [P_ABOUT] = " PICO-COCO: ABOUT",  [P_HELP] = " PICO-COCO: KEYS",
    };
    static const char *const keys[] = {
        [P_MAIN] = " ARROWS ENTER  ESC RESUMES",  [P_TAPES] = " ENTER INSERTS  ESC BACK",
        [P_SETUP] = " < > CHANGES  ESC BACK",     [P_MACHINE] = " < > STAGES  ENTER  ESC BACK",
        [P_ABOUT] = " ESC BACK",                  [P_HELP] = " ESC RESUMES",
    };
    textpage_clear(s_scr);
    textpage_line(s_scr, 0, title[s.page], true);
    draw_battery();
    switch (s.page) {
    case P_TAPES:   draw_tapes(); break;
    case P_SETUP:   draw_setup(); break;
    case P_MACHINE: draw_machine(); break;
    case P_ABOUT:   draw_about(); break;
    case P_HELP:    draw_help(); break;
    default:        draw_main(); break;
    }
    if (s.name.on) {
        char line[TEXT_COLS + 1];
        int at = snprintf(line, sizeof line, " TAPE NAME: %s", s.name.text);
        textpage_line(s_scr, ROW_STATUS, line, false);
        if (at < TEXT_COLS) textpage_put(s_scr, ROW_STATUS, at, " ", true);
        textpage_line(s_scr, ROW_KEYS, " ENTER MAKES IT  ESC CANCELS", true);
    } else {
        textpage_line(s_scr, ROW_STATUS, s.status, false);
        textpage_line(s_scr, ROW_KEYS, keys[s.page], true);
    }
    display_present(s_scr, NULL);
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

static void open_machine(void) {
    s.page = P_MACHINE;
    s.machine_sel = M_BASIC;
    s.st_ext = g_machine.extended;
    s.st_ram = ram_index(g_machine.ram_k);
    s.n_carts = s.card ? card_cart_list(s_carts, COCO_CART_LIST_MAX) : 0;
    /* The running cartridge among the card's; one the card no longer
     * lists is offered as itself, after them. */
    s.run_cart = -1;
    if (g_machine.cart) {
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
    if (s.card) card_roms_mounted(&s_job, NULL, NULL);
    log_core1("  about        : %s, %s, id %s, %lu MHz, southbridge %d, die %d C, %s, settings %s\n",
              PICO_COCO_VERSION, g_board.chip, g_board.unique_id,
              (unsigned long)(g_board.clk_sys_hz / 1000000u), s.sb_ver, s.temp_c,
              machine_name(g_machine.extended, g_machine.ram_k),
              settingsio_error()[0] ? settingsio_error() : settingsio_state_str(settingsio_state()));
}

/* ---- actions --------------------------------------------------------------- */

static void save_settings(void) {
    if (!s.card) { say(" NO CARD: NOT SAVED", ""); return; }
    settings_t out = s_file;
    /* The machine as it runs, not as the Machine page has it staged
     * (EL §10). */
    out.extended = g_machine.extended;
    out.ram_k = g_machine.ram_k;
    settings_card_name(SETTINGS_CART_DIR, g_machine.cart ? g_boot.cart_path : "", out.cart);
    out.volume = g_ui.volume;
    out.perf = g_ui.perf_line;
    out.status = g_ui.status;
    if (s_bkl_set) out.backlight = g_ui.backlight;
    settings_card_name(SETTINGS_TAPE_DIR, tapeio_chosen() ? tapeio_inserted() : "", out.boot_tape);
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
    if (s.st_ext && !g_boot.job.extended) { say(" NO EXTBAS11.ROM ON THE CARD", ""); return; }
    if (s.st_ext && g_boot.job.basic != ROM_COCO_BAS13) { say(" EXTENDED NEEDS BAS13.ROM", ""); return; }
    machine_t mc = { k_rams[s.st_ram], s.st_ext, s.st_cart >= 0 };
    if (s.st_cart != s.run_cart) {
        if (s.st_cart >= 0) {
            say(" READING %.20s...", base(s_carts[s.st_cart]));
            draw();
            uint32_t len = 0;
            const char *err = card_cart_load(s_carts[s.st_cart], g_boot.cart, &len);
            if (err) { say(" CARTRIDGE: %.20s", err); return; }
            g_boot.cart_len = len;
            snprintf(g_boot.cart_path, sizeof g_boot.cart_path, "%s", s_carts[s.st_cart]);
        } else {
            g_boot.cart_len = 0;
            g_boot.cart_path[0] = 0;
        }
    }
    log_core1("  machine      : restarting as %s%s%s\n", machine_name(mc.extended, mc.ram_k),
              mc.cart ? ", cartridge " : "", mc.cart ? g_boot.cart_path : "");
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
    case I_DISCS:   say(" DISCS COME WITH RS-DOS (M13)", ""); break;
    case I_SNAPS:   say(" SNAPSHOTS COME IN M10", ""); break;
    case I_SETUP:   s.page = P_SETUP; s.setup_sel = D_STATUS; break;
    case I_MACHINE: open_machine(); break;
    case I_RESET:   g_ui.reset = true; s.done = true; break;
    case I_SAVE:    save_settings(); break;
    case I_SHELL:   exit_to_shell(); break;
    case I_ABOUT:   open_about(); break;
    }
}

/* ---- naming a new tape ------------------------------------------------------ */

static void name_start(void) {
    s.name.on = true;
    tapeio_next_name(s.name.text);
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
            name_start();
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
        case M_BASIC: s.st_ext = !s.st_ext; break;
        case M_RAM:   s.st_ram = (s.st_ram + 4u + (unsigned)dir) % 4u; break;
        case M_CART: {
            /* None, the card's, and the running one if the card has it
             * no longer. */
            int n = (int)s.n_carts + (s.run_cart == (int)s.n_carts ? 1 : 0);
            int v = s.st_cart + 1 + dir;
            s.st_cart = ((v % (n + 1)) + (n + 1)) % (n + 1) - 1;
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
    const char *t = tapeio_said();
    if (!s.status[0] && t[0]) say(" %.30s", t);
    if (!s.status[0] && settingsio_error()[0]) say(" %.30s", settingsio_error());

    s.direct = page != KM_PAGE_MAIN;
    char said[sizeof s.status];
    memcpy(said, s.status, sizeof said);
    switch (page) {
    case KM_PAGE_TAPE:     s.item = I_TAPES; open_item(); break;
    case KM_PAGE_DISC:     s.item = I_DISCS; open_item(); s.direct = false; break;
    case KM_PAGE_SNAPSHOT: s.item = I_SNAPS; open_item(); s.direct = false; break;
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
