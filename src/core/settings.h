/* settings.h — the emulator's settings at power-on, and the card file
 * that changes them, /coco/coco.ini (design.md §12.6; EL §8.7).
 *
 * settings_default() is where every default lives: the machine's own
 * (coco_config_default's RAM, and Extended Color BASIC, D6) and the
 * host's. The file names only what it changes, one `key = value` a line:
 *
 *   basic     = extended | color   Extended over Color BASIC 1.3, or a
 *                                  Color BASIC alone (1.3, else 1.1)
 *   ram       = 4 | 16 | 32 | 64   KB
 *   cart      = <file> | rsdos | mpi
 *                                  a ROM cartridge in /coco/carts/, the
 *                                  RS-DOS controller with Disk BASIC
 *                                  (rsdos.h), a Multi-Pak, or none
 *   slot1 .. slot4 = <file> | rsdos | smartwatch
 *                                  the Multi-Pak's slots (§12.8): a ROM
 *                                  cartridge, the RS-DOS controller, the
 *                                  SmartWatch (§12.7), or empty
 *   mpi_switch = 1-4               its front switch: the slot it starts
 *   volume    = 0-8
 *   perf      = on | off           the perf line above the picture (§9.6)
 *   status    = on | off           the status line below it
 *   backlight = 1-15
 *   layout    = standard | <name>  a game layout: L ANALOG, L DIGITAL,
 *                                  R DIGITAL, R ANALOG (§11.4), or one
 *                                  of /coco/keymaps/ (§11.3)
 *   fast_tape = on | off           the trap, or the tape's signal (§12.3)
 *   vdg       = 6847 | 6847t1      the CoCo 1 and 2's VDG, or the CoCo
 *                                  2B's, with lower case (§9.2)
 *   smartwatch = on | off          retired, taken and ignored: the
 *                                  SmartWatch is a cartridge (§12.7)
 *   disk_sounds = on | off         the drives' steps and motor heard (§10)
 *   stereo    = on | off           an Orchestra-90's DACs each to its own
 *                                  ear, or both to both (§12.10)
 *   boot_tape = <file>             a tape in /coco/tapes/ put in the deck
 *   artifacts = off | simple | full    the artefact colours (§9.5)
 *   artifact_phase = blue | red | random
 *   background = dark | black      text and the two-colour screens on dark
 *                                  green or orange, or on black (§9.5)
 *   boot_disk = <file>             a disk in /coco/disks/ put in drive 0;
 *   boot_disk1 .. boot_disk3       and in drives 1 to 3 (§12.4)
 *   color_<name> = RRGGBB          a colour drawn as given, in hex, in
 *                                  place of the VDG's own: green, yellow,
 *                                  blue, red, buff, cyan, magenta,
 *                                  orange, black, dark_green, dark_orange,
 *                                  text_orange, artifact_blue and
 *                                  artifact_red; left out, the VDG's
 *                                  own. Only the file sets them (§9.5),
 *                                  and a save keeps them.
 *
 * The parser is here, in the core, so it is tested on the host. What a
 * value names on the card is the port's to find, and it says so when it
 * cannot.
 *
 * Copied from pico-oric and renamed, with the CoCo's keys (§12.6, §14).
 */
#ifndef PICO_COCO_SETTINGS_H
#define PICO_COCO_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "coco.h"
#include "mc6847.h"

/* Where a bare tape, cartridge or disk name in the file is looked for
 * (§12.6). */
#define SETTINGS_TAPE_DIR "/coco/tapes"
#define SETTINGS_CART_DIR "/coco/carts"
#define SETTINGS_DISK_DIR COCO_DISK_DIR

/* The artefact colours (§9.5): off, XRoar's 2-bit renderer's, or its
 * 5-bit one's; the phase whose pair 01 is blue or red, or either. */
enum { SETTINGS_ARTEFACTS_OFF, SETTINGS_ARTEFACTS_SIMPLE, SETTINGS_ARTEFACTS_FULL };
enum { SETTINGS_PHASE_BLUE, SETTINGS_PHASE_RED, SETTINGS_PHASE_RANDOM };

#define SETTINGS_COLOUR_NONE 0xFFFFFFFFu

typedef struct {
    bool       extended;    /* Extended Color BASIC, else Color BASIC alone */
    unsigned   ram_k;       /* 4, 16, 32 or 64 (§8.1)                    */
    unsigned   volume;      /* 0-8, as the menu shows it                 */
    bool       perf;        /* the perf line above the guest (§9.6)      */
    bool       status;      /* the status line below it                  */
    unsigned   backlight;   /* 1-15, as the menu shows it; 0 leaves the
                               panel's own, and a save keeps the file's  */

    bool       fast_tape;   /* blocks served by the trap (§12.3)         */
    bool       vdg_t1;      /* the MC6847T1 drawn, not the MC6847 (§9.2) */
    bool       disk_sounds; /* the drives' steps and motor (§10)         */
    bool       stereo;      /* an Orchestra-90 in stereo (§12.10)        */
    /* The artefact colours (§9.5): how they are drawn, and their phase,
     * which "random" chooses afresh at each reset, as a real CoCo's VDG
     * comes up locked to either clock edge. */
    uint8_t    artefacts;       /* SETTINGS_ARTEFACTS_*                  */
    uint8_t    artefact_phase;  /* SETTINGS_PHASE_*                      */
    bool       dark_bg;         /* text on dark green or orange (§9.5)   */
    /* Each VDG colour as 0xRRGGBB, or SETTINGS_COLOUR_NONE for the VDG's
     * own (mc6847_palette_over, §9.5). */
    uint32_t   colour[VDG_COLOUR_COUNT];
    /* A layout's name, upper case; "" is the standard map (§11.3). */
    char       layout[COCO_KEYMAP_NAME_LEN + 1];

    /* As written in the file, "" for none. The port resolves a bare name
     * against SETTINGS_CART_DIR, SETTINGS_TAPE_DIR or SETTINGS_DISK_DIR. */
    char       cart[COCO_PATH_MAX];
    char       boot_tape[COCO_PATH_MAX];
    char       boot_disk[COCO_DISK_DRIVES][COCO_PATH_MAX];   /* by drive */
    /* The Multi-Pak's slots, as cart is written, and its switch, 1-4,
     * when cart is "mpi" (§12.8). */
    char       slot[COCO_MPI_SLOTS][COCO_PATH_MAX];
    unsigned   mpi_switch;
} settings_t;

void settings_default(settings_t *s);

typedef enum {
    SET_OK = 0,
    SET_SYNTAX,         /* not key = value                  */
    SET_UNKNOWN,        /* no such setting                  */
    SET_BAD_VALUE,      /* not one of the setting's values  */
    SET_DUPLICATE,      /* the setting was given twice      */
    SET_TOO_LONG,       /* a line, a path, or a rewritten file */
    SET_MISMATCH,       /* a rewrite that does not read back */
} settings_status_t;

/* Apply the file's text over *s, which holds the defaults or an earlier
 * file's values. A line that is wrong changes nothing and the lines after
 * it still apply, so one typing mistake does not cost the rest of the
 * file. Returns the first line's status, and its number in *line (0 when
 * every line was good). */
settings_status_t settings_parse(settings_t *s, const char *text, size_t len,
                                 unsigned *line);
const char *settings_status_str(settings_status_t st);

/* *s written into the file's text, for the menu's save (§14). The text
 * is edited, not regenerated (EL §8.7):
 *
 *   - a key the file already gives keeps its line, its place, its
 *     indentation and its comment; only the value changes, and not even
 *     that if the value there already says the same;
 *   - a key the file does not give is appended only if the value differs
 *     from the default, so a default the user never touched keeps
 *     following the firmware's;
 *   - a line naming a key with a value the parser refuses is that key's
 *     line if no other line gives the key a good value, and the value in
 *     force is written over the refused one; if another line does, the
 *     refused line becomes a comment, "# " before its text, as it does
 *     when the value in force is one no line can say (a backlight of 0);
 *   - everything else — comments, blank lines, other keys, lines that do
 *     not parse — is copied as it stands;
 *   - the file's own line ending is kept, and appended lines use it.
 *
 * A key given twice is SET_DUPLICATE, and a result longer than
 * COCO_SETTINGS_FILE_MAX is SET_TOO_LONG. The result is parsed back
 * before it is returned, and must give *s's values again, or it is
 * SET_MISMATCH. On SET_OK, *out is the new text in a static buffer, valid
 * until the next call. No I/O. */
settings_status_t settings_rewrite(const char *text, size_t len, const settings_t *s,
                                   const char **out, size_t *out_len);

/* How the file names what is at `path`: a bare name for a file directly
 * in `dir`, otherwise the path as it is. "" stays "". */
void settings_card_name(const char *dir, const char *path, char out[COCO_PATH_MAX]);

/* The BASIC and RAM as the file writes them: "extended", "color"; "4",
 * "16", "32", "64". */
const char *settings_basic_str(bool extended);
const char *settings_ram_str(unsigned ram_k);

/* The artefacts and their phase as the file writes them: "off",
 * "simple", "full"; "blue", "red", "random". */
const char *settings_artefacts_str(unsigned a);
const char *settings_phase_str(unsigned p);

#endif /* PICO_COCO_SETTINGS_H */
