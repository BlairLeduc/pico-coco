/* tapeio.h — CAS files in /coco/tapes/ on the card, serving the tape
 * trap (design.md §12.3, §12.6; tape.h).
 *
 * The deck holds one tape, the whole of its file in a 64 KB buffer the
 * core reads its blocks from (tape.h), so the guest parks for the card
 * only when the trap asks or a recording is to be written. Core 1, with
 * core 0 parked and the machine core 1's (park.h).
 *
 * pico-oric's policy (its tapeio.h), for a deck that plays from where it
 * stands:
 *
 *   - With the deck empty, CLOAD"PIPES" plays /coco/tapes/PIPES.CAS from
 *     its start, or failing that the first tape that has a file called
 *     PIPES. CLOAD with no name and the deck empty gives up.
 *   - A read that runs off the tape's end rewinds it, unless the name
 *     CLOAD was given is not on the tape, or the whole tape has been
 *     read since the last rewind; then it gives up.
 *   - Giving up presses the reset button: BASIC's warm start, the program
 *     kept, where the real machine would wait in silence for a signal
 *     (coco_tape_give_up). The status line says why.
 *   - CSAVE appends to the tape in the deck. With the deck empty it makes
 *     a new tape, named for its first file: CSAVE"GAME" goes to
 *     /coco/tapes/GAME.CAS, appended if that file is already there.
 *   - A recording is written out once its last block is an end block,
 *     the whole tape through <file>.new and a rename (EL §8.6), so the
 *     card only ever holds whole files. A load that finds only the .new,
 *     a save cut off between the two, takes it.
 *
 * A file longer than the buffer is refused: no CoCo tape of the archive
 * comes near it (§15.4, M11).
 */
#ifndef PICO_COCO_TAPEIO_H
#define PICO_COCO_TAPEIO_H

#include <stdbool.h>
#include <stdint.h>

#include "cas.h"
#include "coco.h"
#include "config.h"

#define TAPEIO_DIR "/coco/tapes"

/* Core 0, after every power-on: the deck's buffer lent to the machine,
 * with the tape that was in it, rewound. */
void tapeio_attach(coco_t *m);

/* Core 1, at boot with the card mounted and before the machine exists:
 * the settings' boot_tape read into the deck, a bare name in TAPEIO_DIR.
 * NULL, or why not. tapeio_attach puts it in the machine. */
const char *tapeio_boot(const char *path);

/* Core 0, once a field: whether to park for tapeio_serve. A request the
 * CPU is stalled on, or a recording whose last block is an end block. */
bool tapeio_wanted(const coco_t *m);

/* Core 1, parked: serve the request, or write the recording out. Mounts
 * the card and unmounts it. */
void tapeio_serve(coco_t *m);

/* The deck, with the card mounted and the machine parked. Insert reads a
 * tape in at its start; NULL or "" empties the deck. NULL, or why not:
 * a recording not yet written out is not thrown away. */
const char *tapeio_insert(coco_t *m, const char *path);
const char *tapeio_inserted(void);     /* the path; "" when empty or new */
/* A new empty tape, name.CAS in TAPEIO_DIR, in the deck; a name already
 * there is refused. tapeio_next_name offers TAPEnn, the first free. */
const char *tapeio_new(coco_t *m, const char *name);
void        tapeio_next_name(char out[COCO_MEDIA_NAME_MAX + 1]);
void        tapeio_rewind(coco_t *m);
bool        tapeio_chosen(void);       /* put in by the menu or boot_tape */

/* A save state's part (snapshot.h, snapio.c): the deck's tape, its
 * place and the rewind mark into *md; and after a load, the state's tape
 * read back in at its place, clamped to the tape as the card has it now,
 * or the deck emptied for a state with none. NULL, or why the tape could
 * not go back in, the deck then empty. */
struct snap_media_s;
void        tapeio_media(const coco_t *m, struct snap_media_s *md);
const char *tapeio_restore(coco_t *m, const struct snap_media_s *md);

/* Whether a recording is not yet on the card. */
bool        tapeio_unsaved(const coco_t *m);

/* The last thing a load or save did that the user should hear about,
 * for the status line; "" for nothing. Cleared by reading. */
const char *tapeio_said(void);

typedef struct {
    char     path[COCO_PATH_MAX];
    char     name[CAS_NAME_LEN + 1];   /* the first file's, "" if none */
    uint8_t  type;                     /* its type: 0 BASIC, 2 code    */
    uint32_t size;
} tapeio_entry_t;

/* Up to max .CAS files in TAPEIO_DIR, in directory order. The card must
 * be mounted. */
unsigned tapeio_list(tapeio_entry_t *out, unsigned max);

/* Counters for the heartbeat. */
typedef struct {
    uint32_t finds, rewinds, gave_up, saves, errors;
    uint32_t last_us, max_us;
} tapeio_stats_t;

extern volatile tapeio_stats_t g_tape_stats;

#endif /* PICO_COCO_TAPEIO_H */
