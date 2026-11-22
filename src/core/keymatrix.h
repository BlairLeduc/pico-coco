/* keymatrix.h — PicoCalc key events to the CoCo's key matrix (design.md
 * §11).
 *
 * The PicoCalc delivers translated characters as press and release
 * events (hardware-notes.md §6.2); the CoCo wants an 8x7 matrix, columns
 * driven on PIA0 port B and rows read on port A, with one SHIFT at row 6,
 * column 7. This builds a held-key set from the events and drives the
 * matrix from that set once a field (§11.1), never from a character
 * stream.
 *
 * Adapted from pico-oric's keymatrix (§5): the held set, canonical codes,
 * the binding fixed at press, paced replay, the Alt layer and the text
 * path are its. What changes is the guest's: typing is by character
 * (§11.3), so an entry may take the guest's SHIFT away as well as add
 * it, and the CoCo 1 and 2 have no CTRL: Ctrl is CLEAR, which OS-9
 * reads as its control key. Game layouts with their .map
 * files are pico-oric's too, and a layout's key may be a joystick's
 * direction or button as well as a cell (§11.4): the keyboard joystick
 * is a built-in layout.
 *
 * Runs on core 0, which owns coco_t. Events reach it through the port's
 * queue from core 1, which owns the I2C bus (§6).
 */
#ifndef PICO_COCO_KEYMATRIX_H
#define PICO_COCO_KEYMATRIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coco.h"
#include "config.h"

/* ---- the keymap: data (§11.3) ---------------------------------------- */

#define KM_SHIFT   0x01u   /* the guest's SHIFT down with this cell        */
#define KM_ALT     0x02u   /* the entry is on the Alt layer                */
#define KM_NOSHIFT 0x04u   /* the guest's SHIFT up with this cell          */
#define KM_RESET   0x08u   /* no cell: the reset button (§8.3); with row 1,
                            * the power switched off and on               */
#define KM_MENU    0x10u   /* no cell: the emulator menu (§14)             */
#define KM_SHOT    0x20u   /* no cell: a screenshot to the card (§14)      */
#define KM_JOY     0x40u   /* no cell: a joystick's line (§11.4): the row
                            * is the stick, the column a KJ_ line          */
#define KM_PAUSE   0x80u   /* no cell: pause the guest (§14)               */
#define KM_NOCELL  (KM_RESET | KM_MENU | KM_SHOT | KM_JOY | KM_PAUSE)

/* A joystick's lines, a layout's targets under KM_JOY. */
#define KJ_LEFT   0u
#define KJ_RIGHT  1u
#define KJ_UP     2u
#define KJ_DOWN   3u
#define KJ_FIRE   4u

/* The menu's pages (§14), as the siblings have them: KM_MENU's row is
 * the one it opens. */
#define KM_PAGE_MAIN     0u
#define KM_PAGE_TAPE     1u
#define KM_PAGE_DISC     2u
#define KM_PAGE_SNAPSHOT 3u
#define KM_PAGE_SETUP    4u
#define KM_PAGE_MACHINE  5u
#define KM_PAGE_HELP     6u
#define KM_PAGE_ABOUT    7u

/* The CoCo's SHIFT (§11.2). */
#define CK_ROW_SHIFT  6u
#define CK_COL_SHIFT  7u
#define CK_ROW_CLEAR  6u   /* held by the PicoCalc's Ctrl (§11.3)          */
#define CK_COL_CLEAR  1u

typedef struct {
    uint8_t code;         /* host key code, as translated by the MCU      */
    uint8_t row, col;     /* CoCo row (PA0-PA6) and column (PB0-PB7);
                           * under KM_MENU the row is the page            */
    uint8_t flags;
} keymap_t;

extern const keymap_t keymap_picocalc[];
extern const size_t   keymap_picocalc_len;

/* ---- game layouts: overlays on the standard map (§11.3) ---------------- */

/* A binding's code is canonical (keymap_picocalc_canonical), so it holds
 * whichever way the MCU translated the key; its target is one CoCo key,
 * flags 0, SHIFT included as the cell it is, or one line of a joystick,
 * KM_JOY. A cell so bound says nothing of the guest's SHIFT, which stays
 * as the host's Shift has it. */
typedef struct keylayout_s {
    char     name[COCO_KEYMAP_NAME_LEN + 1];
    uint8_t  n;
    keymap_t bind[COCO_KEYMAP_BINDINGS];
    /* Files whose loading selects this layout: a tape put in the deck or
     * a cartridge in the slot, named without the extension (§11.3). */
    uint8_t  n_files;
    char     files[COCO_KEYMAP_FILES][COCO_KEYMAP_FILE_LEN + 1];
} keylayout_t;

/* The built-in ones, chosen by M12's survey of the archive
 * (docs/milestones.md): the keyboard joystick as the right stick and as
 * the left, the arrows its directions and ']' its button (§11.4). */
extern const keylayout_t keylayout_builtin[];
extern const size_t      keylayout_builtin_len;

typedef enum {
    KL_OK = 0,
    KL_SYNTAX,          /* not "word = value", or an empty value       */
    KL_BAD_KEY,         /* no PicoCalc key by that name                */
    KL_BAD_TARGET,      /* no CoCo key or joystick line by that name   */
    KL_DUPLICATE,       /* a key bound twice                           */
    KL_TOO_MANY,        /* more bindings or files than config.h allows */
    KL_TOO_LONG,        /* a name longer than there is room for        */
} keylayout_status_t;

/* A .map file's text (§11.3): "name = ...", "tapes = ..." and "carts =
 * ...", whose files are separated by commas, since the archive's names
 * have spaces, and one "<PicoCalc key> = <CoCo key>" per line; '#' starts
 * a comment line. `name` is used when the file gives none. On failure
 * *line is the 1-based line at fault, and `out` must not be used. */
keylayout_status_t keylayout_parse(keylayout_t *out, const char *name,
                                   const char *text, size_t len, unsigned *line);
const char *keylayout_status_str(keylayout_status_t st);

/* Does loading this file select the layout? The path's last part, less
 * its extension, against the tapes and carts lines, ignoring case: the
 * file is typed by hand. */
bool keylayout_for_file(const keylayout_t *l, const char *path);

/* The names the parser takes, which are the keymap's to know. Both
 * compare without regard to case. */
bool keymap_picocalc_key_named(const char *name, uint8_t *code);
bool keymap_coco_target_named(const char *name, keymap_t *out);

/* A binding as a .map line has it, "left=JR-LEFT", for the menu: the
 * names above, so that the text parses back to the binding. */
void keymap_binding_str(const keymap_t *e, char *out, size_t n);

/* The physical key behind a translated code. The MCU retranslates at
 * every transition, so releasing Shift first turns press 'A' into
 * release 'a' and press '!' into release '1' (hardware-notes.md §6.2);
 * held-state identity is this, not the code. */
uint8_t keymap_picocalc_canonical(uint8_t code);

/* Modifier codes and event states (hardware-notes.md §6.2). */
#define PICOCALC_KEY_ALT      0xA1u
#define PICOCALC_KEY_SHIFT_L  0xA2u
#define PICOCALC_KEY_SHIFT_R  0xA3u
#define PICOCALC_KEY_CTRL     0xA5u
#define PICOCALC_KEY_CAPS     0xC1u

/* F1-F5 unshifted; Shift makes them F6-F10, 0x86-0x90 (keyboard.h). */
#define PICOCALC_KEY_F1       0x81u
#define PICOCALC_KEY_F6       0x86u
#define PICOCALC_KEY_F10      0x90u

/* The other keys this table binds (keyboard.h). */
#define PICOCALC_KEY_BACKSPACE 0x08u
#define PICOCALC_KEY_TAB       0x09u
#define PICOCALC_KEY_ENTER     0x0Au
#define PICOCALC_KEY_ESC       0xB1u
#define PICOCALC_KEY_LEFT      0xB4u
#define PICOCALC_KEY_UP        0xB5u
#define PICOCALC_KEY_DOWN      0xB6u
#define PICOCALC_KEY_RIGHT     0xB7u
#define PICOCALC_KEY_BREAK     0xD0u   /* Shift+Esc */
#define PICOCALC_KEY_INSERT    0xD1u   /* Shift+Enter, or Alt+I */
#define PICOCALC_KEY_DEL       0xD4u

/* Keys that exist only as another's shifted alternate
 * (hardware-notes.md §6.3), by the key they are on. */
#define PICOCALC_KEY_HOME      0xD2u   /* Shift+Tab */
#define PICOCALC_KEY_END       0xD5u   /* Shift+Del */
#define PICOCALC_KEY_PAGE_UP   0xD6u   /* Shift+Up */
#define PICOCALC_KEY_PAGE_DOWN 0xD7u   /* Shift+Down */

#define KEY_EV_PRESSED   1u
#define KEY_EV_HELD      2u
#define KEY_EV_RELEASED  3u

/* One southbridge FIFO entry (hardware-notes.md §6.2). */
typedef struct { uint8_t state, code; } picocalc_event_t;

/* The events a PicoCalc sends for one ASCII byte, so that text from the
 * UART or a host test arrives the way typing would (§11.1). Printable
 * characters are themselves, inside Shift where the PicoCalc types them
 * as a Shift chord; CR and LF are Enter, BS and DEL Backspace, ESC is
 * Esc, TAB is Tab, and the other control characters are Ctrl with a
 * letter, which is CLEAR with it, OS-9's control key. A function key's code is that
 * key, F6-F10 inside Shift. Returns the count, 0 for a byte no key
 * sends. */
unsigned keymap_picocalc_text(uint8_t ch, picocalc_event_t out[COCO_KEY_TEXT_EVENTS]);

/* ---- the held-key set ------------------------------------------------ */

typedef struct {
    uint8_t  canon;       /* keymap_picocalc_canonical() of the press */
    uint8_t  fields;      /* fields it has been down for */
    /* The binding chosen at press, copied: its release undoes exactly
     * that, whatever is in force by then (EL §7.1). */
    keymap_t map;
    /* 1 if its cell waits a field behind the change its binding makes to
     * the guest's SHIFT (keymatrix_field); its hold is counted from the
     * cell. */
    uint8_t lead;
} keymatrix_held_t;

typedef struct {
    uint8_t state, code;
    uint8_t canon;        /* the physical key, decided as the event arrived */
} keymatrix_event_t;

typedef struct {
    /* Events wait here and are replayed at field rate, in order. */
    keymatrix_event_t queue[COCO_KEY_EVENT_QUEUE];
    uint8_t q_head, q_len;
    uint8_t gap;          /* fields before the next press may apply */
    uint32_t dropped;     /* presses refused for want of room */

    /* Keys whose press is queued or applied and whose release has not
     * arrived yet. The queue always keeps a slot for each of their
     * releases: a lost press drops a character, a lost release holds a
     * key down for ever. */
    uint8_t open[COCO_KEY_EVENT_QUEUE];
    uint8_t n_open;

    /* In the order they went down: the latest that says anything about
     * the guest's SHIFT decides it (keymatrix_field). */
    keymatrix_held_t held[COCO_KEY_HELD_MAX];
    uint8_t n;
    bool alt, ctrl;
    uint8_t shift;        /* the host's Shifts that are down: bit 0 left, 1 right */
    /* Fields the host's Shift has been down: it reaches the matrix, so a
     * tap is held as long as a key (§11.1). */
    uint8_t shift_fields;
    /* The guest's SHIFT as the matrix showed it at the last field. */
    bool shift_shown;
    /* Fields Ctrl has been down: it is the CoCo's CLEAR, held as long as
     * a key, as Shift is SHIFT (§11.3). */
    uint8_t ctrl_fields;

    /* Alt as the events arrive, ahead of the replay: it decides which
     * key an Insert is (keymatrix_event). */
    bool ev_alt;

    /* The game layout over the standard map, or NULL (§11.3). */
    const keylayout_t *layout;

    /* Set on a press, cleared by whoever acts on it. */
    bool menu_request;
    uint8_t menu_page;    /* with menu_request: the entry's row */
    bool pause_request;
    bool shot_request;
    bool reset_request;   /* the port presses the reset button (§8.3) */
    bool power_request;   /* the port switches off and on (§13.3)     */
} keymatrix_t;

/* Empty, with no layout. */
void keymatrix_init(keymatrix_t *k);

/* Takes effect from the next press; a key already down keeps the binding
 * it went down with. NULL is the standard map. */
void keymatrix_set_layout(keymatrix_t *k, const keylayout_t *l);

/* One [state, code] event off the southbridge FIFO. Queued, not applied:
 * see keymatrix_field. A press that finds no room for itself and for
 * every outstanding release is refused, and its release with it; a
 * press of a key already down (the MCU's auto-repeat) is absorbed.
 *
 * The key an event belongs to is keymap_picocalc_canonical's, except
 * Insert, which the MCU sends for Shift+Enter and for Alt+I
 * (hardware-notes.md §6.3): with Alt down it is the I key, so a release
 * that arrives as 'i' after Alt has gone up still finds its press. */
void keymatrix_event(keymatrix_t *k, uint8_t state, uint8_t code);

/* Once a field, before the guest runs: replay queued events, drive the
 * matrix and the joysticks from the held set, and age it.
 *
 * Replay is paced for the ROM, not for the poll (§11.1, §18). A press
 * and its release can arrive in the same 30 Hz poll (EL §7.1), and
 * Color BASIC sees a key only while it waits for one, so a release waits
 * until its key has been down COCO_KEY_MIN_FIELDS, and a press waits
 * COCO_KEY_GAP_FIELDS after a release, or the ROM takes a key typed twice
 * for one held. The host's Shift is paced like a key: its release waits
 * the same minimum, so a tap within one poll still reaches a game that
 * reads SHIFT alone. A key whose binding changes the guest's SHIFT (the
 * host's Shift going down, or an entry's KM_SHIFT or KM_NOSHIFT) reaches
 * the matrix a field after SHIFT changes: a scan that read SHIFT before
 * the change would take the key with the wrong one (EL §7.1). Keys that
 * overlapped at the keyboard still overlap here; keys that did not, do
 * not. */
void keymatrix_field(keymatrix_t *k, coco_t *m);

/* Nothing queued and nothing held: whatever was typed has reached the
 * guest. */
static inline bool keymatrix_idle(const keymatrix_t *k) {
    return k->q_len == 0 && k->n == 0;
}

#endif /* PICO_COCO_KEYMATRIX_H */
