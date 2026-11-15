/* keymap_picocalc.c — PicoCalc key codes to CoCo matrix cells (design.md
 * §11.2, §11.3), adapted from pico-oric's.
 *
 * The cells are the ROMs', not a transcription (§11.2). Each of the 56
 * cells was pressed at the prompt in Color BASIC 1.1, 1.3 and Extended
 * Color BASIC 1.1, alone and with SHIFT, and what the ROM's POLCAT
 * returned was read back (2026-11-15); the three agree in every cell.
 * test_keyboard repeats that sweep and types every entry below through
 * each ROM.
 *
 *   row \ col  0      1      2      3      4      5      6      7
 *   0           @ ^S   A      B      C      D      E      F      G
 *   1           H      I      J      K      L      M      N      O
 *   2           P      Q      R      S      T      U      V      W
 *   3           X      Y      Z      UP     DOWN   LEFT   RIGHT  SPACE
 *   4           0 *    1 !    2 "    3 #    4 $    5 %    6 &    7 '
 *   5           8 (    9 )    : *    ; +    , <    - =    . >    / ?
 *   6           ENTER  CLEAR  BREAK  -      -      -      -      SHIFT
 *
 * What SHIFT gives follows the key; a letter is the same either way
 * while BASIC is in upper case, as it is after a reset. SHIFT+0 types
 * nothing and turns lower case on and off (*), and SHIFT+@ is $13, which
 * pauses a listing. The arrows give $5E, $0A, $08 and $09, and with SHIFT
 * $5F, $5B, $15 (erase the line) and $5D; CLEAR is $0C, and $5C with
 * SHIFT; BREAK is $03 either way. The four cells of row 6 marked '-' have
 * no key on a CoCo 1 or 2 (the CoCo 3's ALT, CTRL, F1 and F2 are there),
 * and the ROMs decode them from whatever follows their table.
 *
 * Typing is by character (§11.3): an entry is the guest key and SHIFT
 * that type the character the PicoCalc sent, so an entry adds the guest's
 * SHIFT (KM_SHIFT) where the CoCo shifts and the PicoCalc does not ('='
 * is SHIFT+-), and takes it away (KM_NOSHIFT) where the PicoCalc shifts
 * and the CoCo does not (':' and '@', and the capitals: a PicoCalc letter
 * types the CoCo's letter whatever its case). The VDG has '^' and '_' as
 * up and left arrows, which are the codes BASIC sees for UP and SHIFT+UP,
 * and '[', ']' and '\' are SHIFT with DOWN, RIGHT and CLEAR. '{', '}',
 * '|', '`' and '~' have no key on the CoCo, and no entry here.
 *
 * Code values are the southbridge firmware's (hardware-notes.md §6;
 * keyboard.h in the PicoCalc repository).
 */

#include "keymatrix.h"

/* CoCo keys as "row, col" (§11.2). */
#define CK_AT     0, 0
#define CK_A      0, 1
#define CK_B      0, 2
#define CK_C      0, 3
#define CK_D      0, 4
#define CK_E      0, 5
#define CK_F      0, 6
#define CK_G      0, 7
#define CK_H      1, 0
#define CK_I      1, 1
#define CK_J      1, 2
#define CK_K      1, 3
#define CK_L      1, 4
#define CK_M      1, 5
#define CK_N      1, 6
#define CK_O      1, 7
#define CK_P      2, 0
#define CK_Q      2, 1
#define CK_R      2, 2
#define CK_S      2, 3
#define CK_T      2, 4
#define CK_U      2, 5
#define CK_V      2, 6
#define CK_W      2, 7
#define CK_X      3, 0
#define CK_Y      3, 1
#define CK_Z      3, 2
#define CK_UP     3, 3
#define CK_DOWN   3, 4
#define CK_LEFT   3, 5
#define CK_RIGHT  3, 6
#define CK_SPACE  3, 7
#define CK_0      4, 0
#define CK_1      4, 1
#define CK_2      4, 2
#define CK_3      4, 3
#define CK_4      4, 4
#define CK_5      4, 5
#define CK_6      4, 6
#define CK_7      4, 7
#define CK_8      5, 0
#define CK_9      5, 1
#define CK_COLON  5, 2
#define CK_SEMI   5, 3
#define CK_COMMA  5, 4
#define CK_MINUS  5, 5
#define CK_DOT    5, 6
#define CK_SLASH  5, 7
#define CK_ENTER  6, 0
#define CK_CLEAR  6, 1
#define CK_BREAK  6, 2

#define NOCELL 0, 0

/* A letter: the CoCo's, whatever the PicoCalc's case. */
#define LETTER(lower, cell) \
    { lower, cell, 0 }, { (uint8_t)((lower) - 'a' + 'A'), cell, KM_NOSHIFT }

const keymap_t keymap_picocalc[] = {
    LETTER('a', CK_A), LETTER('b', CK_B), LETTER('c', CK_C), LETTER('d', CK_D),
    LETTER('e', CK_E), LETTER('f', CK_F), LETTER('g', CK_G), LETTER('h', CK_H),
    LETTER('i', CK_I), LETTER('j', CK_J), LETTER('k', CK_K), LETTER('l', CK_L),
    LETTER('m', CK_M), LETTER('n', CK_N), LETTER('o', CK_O), LETTER('p', CK_P),
    LETTER('q', CK_Q), LETTER('r', CK_R), LETTER('s', CK_S), LETTER('t', CK_T),
    LETTER('u', CK_U), LETTER('v', CK_V), LETTER('w', CK_W), LETTER('x', CK_X),
    LETTER('y', CK_Y), LETTER('z', CK_Z),

    { '0', CK_0, 0 }, { '1', CK_1, 0 }, { '2', CK_2, 0 }, { '3', CK_3, 0 },
    { '4', CK_4, 0 }, { '5', CK_5, 0 }, { '6', CK_6, 0 }, { '7', CK_7, 0 },
    { '8', CK_8, 0 }, { '9', CK_9, 0 },

    /* The CoCo's shifted digits, which the PicoCalc has on other keys
     * from '"' on: the flag is the guest's SHIFT, whatever the host's. */
    { '!', CK_1, KM_SHIFT }, { '"', CK_2, KM_SHIFT }, { '#', CK_3, KM_SHIFT },
    { '$', CK_4, KM_SHIFT }, { '%', CK_5, KM_SHIFT }, { '&', CK_6, KM_SHIFT },
    { '\'', CK_7, KM_SHIFT }, { '(', CK_8, KM_SHIFT }, { ')', CK_9, KM_SHIFT },

    { ':', CK_COLON, KM_NOSHIFT }, { '*', CK_COLON, KM_SHIFT },
    { ';', CK_SEMI, 0 },           { '+', CK_SEMI, KM_SHIFT },
    { ',', CK_COMMA, 0 },          { '<', CK_COMMA, KM_SHIFT },
    { '-', CK_MINUS, 0 },          { '=', CK_MINUS, KM_SHIFT },
    { '.', CK_DOT, 0 },            { '>', CK_DOT, KM_SHIFT },
    { '/', CK_SLASH, 0 },          { '?', CK_SLASH, KM_SHIFT },
    { '@', CK_AT, KM_NOSHIFT },
    { ' ', CK_SPACE, 0 },

    /* The characters BASIC reads from the arrows and CLEAR. */
    { '^', CK_UP, KM_NOSHIFT },  { '_', CK_UP, KM_SHIFT },
    { '[', CK_DOWN, KM_SHIFT },  { ']', CK_RIGHT, KM_SHIFT },
    { '\\', CK_CLEAR, KM_SHIFT },

    { PICOCALC_KEY_ENTER,     CK_ENTER, 0 },
    { PICOCALC_KEY_BACKSPACE, CK_LEFT,  0 },
    { PICOCALC_KEY_DEL,       CK_LEFT,  0 },
    /* BREAK and CLEAR are plain keys, which no swallowed chord can take
     * (§11.3, hardware-notes.md §6.3). */
    { PICOCALC_KEY_ESC,       CK_BREAK, 0 },
    { PICOCALC_KEY_TAB,       CK_CLEAR, 0 },

    /* The CoCo's arrows are the PicoCalc's, without SHIFT whatever the
     * host's Shift is: Shift+Left and Shift+Right send nothing
     * (hardware-notes.md §6.3), and Shift+Up and Shift+Down are other
     * keys, below. */
    { PICOCALC_KEY_LEFT,  CK_LEFT,  0 },
    { PICOCALC_KEY_UP,    CK_UP,    0 },
    { PICOCALC_KEY_DOWN,  CK_DOWN,  0 },
    { PICOCALC_KEY_RIGHT, CK_RIGHT, 0 },

    /* With Shift, these keys send codes of their own (hardware-notes.md
     * §6.3). Each is its base key's cell with SHIFT, as the CoCo's typist
     * would press it: End is SHIFT+LEFT, which erases the line. Alt+I is
     * Insert too, and finds nothing: the Alt layer has no entry for it. */
    { PICOCALC_KEY_INSERT,    CK_ENTER, KM_SHIFT },
    { PICOCALC_KEY_BREAK,     CK_BREAK, KM_SHIFT },
    { PICOCALC_KEY_HOME,      CK_CLEAR, KM_SHIFT },
    { PICOCALC_KEY_END,       CK_LEFT,  KM_SHIFT },
    { PICOCALC_KEY_PAGE_UP,   CK_UP,    KM_SHIFT },
    { PICOCALC_KEY_PAGE_DOWN, CK_DOWN,  KM_SHIFT },

    /* The Alt layer (§11.3). Alt is the guest's SHIFT for the keys whose
     * shifted meaning the PicoCalc's Shift cannot give: the arrows, which
     * arrive as themselves with Alt held (hardware-notes.md §6.3), 0,
     * whose SHIFT turns lower case on and off, and '@', whose SHIFT
     * pauses a listing. Letters arrive in capitals under Alt; Alt+, .
     * Space and B never arrive, and Alt+I is the MCU's Insert. */
    { PICOCALC_KEY_LEFT,  CK_LEFT,  KM_ALT | KM_SHIFT },
    { PICOCALC_KEY_UP,    CK_UP,    KM_ALT | KM_SHIFT },
    { PICOCALC_KEY_DOWN,  CK_DOWN,  KM_ALT | KM_SHIFT },
    { PICOCALC_KEY_RIGHT, CK_RIGHT, KM_ALT | KM_SHIFT },
    { '0', CK_0,  KM_ALT | KM_SHIFT },
    { '@', CK_AT, KM_ALT | KM_SHIFT },
    { 'M', KM_PAGE_MAIN, 0, KM_ALT | KM_MENU },
    { 'H', KM_PAGE_HELP, 0, KM_ALT | KM_MENU },
    { 'P', NOCELL,          KM_ALT | KM_PAUSE },
    { 'K', NOCELL,          KM_ALT | KM_RESET },

    /* The function keys open the menu at a page (§14). The CoCo has no
     * function keys, so they are the menu's everywhere. F6, Shift+F1,
     * takes a screenshot. */
    { PICOCALC_KEY_F1 + 0, KM_PAGE_TAPE,     0, KM_MENU },
    { PICOCALC_KEY_F1 + 1, KM_PAGE_DISC,     0, KM_MENU },
    { PICOCALC_KEY_F1 + 2, KM_PAGE_SNAPSHOT, 0, KM_MENU },
    { PICOCALC_KEY_F1 + 3, KM_PAGE_SETUP,    0, KM_MENU },
    { PICOCALC_KEY_F1 + 4, KM_PAGE_MACHINE,  0, KM_MENU },
    { PICOCALC_KEY_F6,     NOCELL,              KM_SHOT },
    { PICOCALC_KEY_F10,    KM_PAGE_ABOUT,    0, KM_MENU },
};

const size_t keymap_picocalc_len = sizeof keymap_picocalc / sizeof keymap_picocalc[0];

uint8_t keymap_picocalc_canonical(uint8_t code) {
    if (code >= 'A' && code <= 'Z') return (uint8_t)(code + ('a' - 'A'));

    /* Each key's shifted alternate back to its base (keyboard.ino). */
    switch (code) {
    case '!': return '1';  case '@': return '2';  case '#': return '3';
    case '$': return '4';  case '%': return '5';  case '^': return '6';
    case '&': return '7';  case '*': return '8';  case '(': return '9';
    case ')': return '0';  case '_': return '-';  case '+': return '=';
    case '|': return '\\'; case '?': return '/';  case ':': return ';';
    case '"': return '\''; case '<': return ',';  case '>': return '.';
    case '{': return '[';  case '}': return ']';  case '~': return '`';
    case PICOCALC_KEY_END:       return PICOCALC_KEY_DEL;
    case PICOCALC_KEY_HOME:      return PICOCALC_KEY_TAB;
    case PICOCALC_KEY_BREAK:     return PICOCALC_KEY_ESC;
    case PICOCALC_KEY_INSERT:    return PICOCALC_KEY_ENTER;
    case PICOCALC_KEY_PAGE_UP:   return PICOCALC_KEY_UP;
    case PICOCALC_KEY_PAGE_DOWN: return PICOCALC_KEY_DOWN;
    case PICOCALC_KEY_F10:       return PICOCALC_KEY_F1 + 4u;
    default:
        /* F6-F9 are Shift+F1-F4. */
        if (code >= PICOCALC_KEY_F6 && code <= PICOCALC_KEY_F1 + 8u)
            return (uint8_t)(code - 5u);
        return code;
    }
}

unsigned keymap_picocalc_text(uint8_t ch, picocalc_event_t out[COCO_KEY_TEXT_EVENTS]) {
    uint8_t mod = 0, code;
    if (ch == '\r' || ch == '\n') {
        code = PICOCALC_KEY_ENTER;
    } else if (ch == 0x08u || ch == 0x7Fu) {
        code = PICOCALC_KEY_BACKSPACE;
    } else if (ch == 0x09u) {
        code = PICOCALC_KEY_TAB;
    } else if (ch == 0x1Bu) {
        code = PICOCALC_KEY_ESC;
    } else if (ch >= 0x01u && ch <= 0x1Au) {
        /* Ctrl passes the key through unchanged (hardware-notes.md §6.3). */
        mod = PICOCALC_KEY_CTRL;
        code = (uint8_t)('a' + ch - 1u);
    } else if (ch >= 0x20u && ch <= 0x7Eu) {
        code = ch;
        /* A code that is not its own key's base is a Shift chord. */
        if (keymap_picocalc_canonical(ch) != ch) mod = PICOCALC_KEY_SHIFT_L;
    } else if ((ch >= PICOCALC_KEY_F1 && ch <= PICOCALC_KEY_F1 + 8u) || ch == PICOCALC_KEY_F10) {
        /* The function keys as their own codes; F6-F10 are Shift+F1-F5. */
        code = ch;
        if (keymap_picocalc_canonical(ch) != ch) mod = PICOCALC_KEY_SHIFT_L;
    } else {
        return 0;
    }

    unsigned n = 0;
    if (mod) out[n++] = (picocalc_event_t){ KEY_EV_PRESSED, mod };
    out[n++] = (picocalc_event_t){ KEY_EV_PRESSED, code };
    out[n++] = (picocalc_event_t){ KEY_EV_RELEASED, code };
    if (mod) out[n++] = (picocalc_event_t){ KEY_EV_RELEASED, mod };
    return n;
}
