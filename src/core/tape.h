/* tape.h — tape phase 1: Color BASIC's cassette routines served by a trap
 * (design.md §12.3; EL §8.2).
 *
 * The deck is a CAS image (cas.h) in a buffer the port owns and attaches
 * once; the whole tape is in it, so a block is served at once, from the
 * core, with no wait for the card. The port is asked only when the deck
 * cannot answer: there is no tape, or the tape has run out.
 *
 * The routines, behind Color BASIC's vectors at $A004-$A00C, are at the
 * same addresses in 1.1 and 1.3, read off both ROMs (2026-11-15):
 *
 *   $A77C  CSRDON  motor on, wait, find the leader       trapped at $A780
 *   $A70B  BLKIN   read a block to ($7E)                 trapped at $A70F
 *   $A7D8  WRTLDR  motor on, wait, write $92 $55s        trapped at $A7DC
 *   $A7F4  BLKOUT  write the block at ($7E)              trapped at $A7F6
 *   $A7CA  CASON   motor on, then a delay of ($8A) loops trapped at $A7D1
 *
 * Each trap point is after what the routine does first, which the ROM
 * runs itself: the ORCC that masks the interrupts, the cursor's blink
 * (BLKIN's $A6F3), the motor (CASON). The trap does the routine's work
 * on the tape and leaves the machine where the ROM is as it ends its last
 * byte, and the ROM runs its own end:
 *
 *   BLKIN    the bytes stored through the bus at X and read back, as the
 *            ROM's STA and CMPA; resumed at $A73D, the SUBA of the
 *            checksum, with A the checksum off the tape, $80 the sum and
 *            $81 zero; or at $A744 (LDA #2) on a byte that did not store,
 *            as the ROM's BNE goes. $7C and $7D the block's type and
 *            length, $82 zero as the byte reader leaves it.
 *   CSRDON   resumed at $A794, its STA <$84, with A and $82 $A0: the
 *            leader found, and the bit phase the ROM keeps in $84 taken
 *            as one of its two (the block trap reads whole bytes).
 *   CASON    resumed at its RTS with X zero and Z set: no delay.
 *   WRTLDR   ($92) $55s appended; resumed at $A7E0, its LEAX, with X 1.
 *   BLKOUT   $55, $3C, the type, the length, the data, the checksum and
 *            $55 appended; resumed at the byte writer's PULS A,PC with A
 *            pushed as the writer's PSHS left it.
 *
 * After a byte the writer leaves A the byte, B zero, Y $A880, CC with Z,
 * V and C set, its next DAC value in $85, and PIA1's port A latch where
 * its last write to $FF20 put it, which the trap writes through the bus
 * so the sound bus sees it (§10). Both the read and the find read $FF20
 * once, as the ROM's reads clear PIA1's port A flags.
 *
 * A routine's PC is trapped only while the BASIC ROM at $A000 is one of
 * the two images whose routines these are, by SHA-1, and page $A7 is
 * mapped as ROM (the SAM's map type 0). A JSR, BSR, LBSR or JMP to a
 * routine's entry (bus_jump: one compare, on those instructions alone)
 * puts page $A7 on the slow path until that routine's trap point is
 * served, and the opcode fetch at a trap point, the read of that address
 * with the PC one past it, is the trap. The page is not slow otherwise:
 * BASIC's cursor waits in CASON's delay loop at $A7D3, called there
 * directly, and a slow page cost the idle prompt 58 % more host time an
 * instruction on the workstation (2026-11-15). A program that copies the
 * ROM to RAM and runs it there, in map type 1, is not trapped.
 *
 * Deliberate differences from the ROM's routine, held to it by
 * test_tape: time, which passes only for the instructions the ROM runs
 * itself; the stack below S, where the bit and byte routines the trap
 * does not run leave their return addresses; B and $83 after a read,
 * which the bit reader leaves as the last bit's timing; $84, as above.
 *
 * A block is found at the next $3C from where the tape stands, as BLKIN's
 * search for the sync pattern finds it in a CAS whose bytes are whole;
 * CSRDON does not move the tape. A save is appended at the tape's end,
 * whatever the read position, so that a tape only ever gains whole files.
 *
 * When the deck cannot answer, the CPU stalls on the HALT line (§7.1)
 * with the PC at the trap point and a request in coco_t.tape.op, which
 * the port serves at the next field boundary: TAPE_FIND for CSRDON with
 * no tape in the deck, carrying the name CLOAD was given ($1D1, its
 * length, and the eight characters at $1D2); TAPE_END for a read with no
 * sync byte, or no whole block, before the tape's end. The port puts a
 * tape in, or rewinds, and resumes, and the trap is taken again; or it
 * gives up, and the reset button is pressed (coco_tape_give_up), where
 * the real machine would wait in silence for a signal.
 */
#ifndef PICO_COCO_TAPE_H
#define PICO_COCO_TAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cas.h"

struct coco_s;

typedef enum {
    TAPE_NONE = 0,
    TAPE_FIND,     /* CSRDON with no tape: the port finds one by name  */
    TAPE_END,      /* a read past the tape's end                       */
} tape_op_t;

/* The trap points' page. */
#define TAPE_PAGE          0xA7u

/* coco.c's: page $A7 as tape.hot says, slow while it is set. */
void     coco_tape_page(struct coco_s *m);

typedef struct {
    /* The deck: the port's buffer, attached once. */
    uint8_t  *buf;
    uint32_t  cap;
    bool      inserted;    /* a tape in the deck, if only an empty one   */
    uint32_t  len;         /* the tape's bytes                           */
    uint32_t  pos;         /* where the head stands                      */
    uint32_t  saved;       /* the bytes the card has; len past it is a
                            * recording not yet written out             */
    bool      overflow;    /* a write did not fit, and was dropped       */

    bool      rom_ok;      /* Color BASIC 1.1 or 1.3 at $A000            */
    uint16_t  hot;         /* the trap point page $A7 is slow for, or 0  */

    tape_op_t op;          /* the request the CPU is stalled on          */
    uint16_t  op_pc;       /* its trap point                             */
    uint8_t   want[CAS_NAME_LEN];
    uint8_t   want_len;    /* 0: CLOAD gave no name                      */

    /* Counts, for the heartbeat and the tests. */
    uint32_t  blocks_in, blocks_out, errors, requests;
} coco_tape_t;

/* coco.c's: whether the BASIC ROM just loaded at $A000 has these routines,
 * and the read of page $A7 on the slow path, which is the trap when it is
 * the opcode fetch at a trap point. */
void     tape_rom_loaded(struct coco_s *m, const uint8_t *image, size_t len);
uint8_t  tape_page_read(struct coco_s *m, uint16_t a);

/* bus.h's: a jump or call into page $A7. At a routine's entry, the page
 * takes the slow path until its trap point is served (tape.h). */
void     tape_jump(struct coco_s *m, uint16_t target);

/* The RESET line: any request goes with the program that made it. */
void     tape_reset(struct coco_s *m);

/* ---- the port's ----------------------------------------------------------- */

/* The deck's buffer, `cap` bytes; it must outlive the machine. Empties
 * the deck. */
void     coco_tape_attach(struct coco_s *m, uint8_t *buf, uint32_t cap);

/* The port has put `len` bytes of a tape in the buffer: in the deck, at
 * its start, all of it on the card. len 0 is a new, empty tape. */
void     coco_tape_insert(struct coco_s *m, uint32_t len);

/* The deck emptied; a recording not written out is lost. */
void     coco_tape_eject(struct coco_s *m);

void     coco_tape_rewind(struct coco_s *m);

/* A recording not yet on the card: [*from, *to) of the buffer. */
bool     coco_tape_unsaved(const struct coco_s *m, uint32_t *from, uint32_t *to);

/* The whole tape is on the card. */
void     coco_tape_saved(struct coco_s *m);

/* The request the CPU is stalled on, TAPE_NONE if none. */
tape_op_t coco_tape_pending(const struct coco_s *m);

/* The request is answered (a tape put in, or rewound): the CPU takes the
 * trap again. */
void     coco_tape_resume(struct coco_s *m);

/* No tape will answer: the request goes, and the reset button is pressed,
 * BASIC's warm start, the program kept (design.md §12.3). */
void     coco_tape_give_up(struct coco_s *m);

#endif /* PICO_COCO_TAPE_H */
