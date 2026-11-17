/* cassette.h — tape phase 2: the signal (design.md §12.3; EL §8.3).
 *
 * With fast loading off, the deck's CAS image (tape.h) is played onto
 * PIA1 PA0 as the square wave Color BASIC's own writer would have left
 * on a tape, clocked in master-oscillator ticks, and what the ROM drives
 * onto the DAC is read back into whole blocks appended to the image.
 * Whatever reads PA0, BASIC's CLOAD or a loader of a program's own, sees
 * what it would from a recorder.
 *
 * The writer is the same code at the same addresses in Color BASIC 1.1
 * and 1.3 (tape.h), read off both and measured on the host (2026-11-16;
 * docs/milestones.md, M12). The byte writer at $A82A sends a byte from
 * bit 0 as one cycle of a 36-step sine on the DAC a bit, every step for
 * a 0 and every other for a 1, and a bit is seen as the DAC crossing
 * its middle: up at the table's first entry, down at its nineteenth. In
 * CPU cycles, at the SAM's slow rate, the high half is 207 for a 1 and
 * 396 for a 0; the low half, to the next bit's rise, 227 after a 1 and
 * 417 after a 0, a cycle more when the next bit is a 1. After a byte's
 * last bit the caller's instructions are added: 33 between WRTLDR's
 * leader bytes; in BLKOUT 25 after its first $55, 27 after the sync and
 * after the type, 34 after the length (32 with no data), 38 after a data
 * byte (36 after the last), 18 after the checksum. Before a block's
 * first $55 BLKOUT sums the data, 11 cycles a byte, and the rest is its
 * caller's (cassette.c's table): 81 after a leader for a name block and
 * for every block of an ASCII or data file, each of which has a leader
 * of its own; CSAVE's and CSAVEM's own figures for the blocks of a
 * binary file, which follow one another. The motor comes on 524,365
 * cycles before a leader's first rise, CASON's delay.
 *
 * The player gives exactly those counts for the image's bytes as they
 * are, and adds what a CAS leaves out: the silence before each leader.
 * test_cassette holds its edges to a recording of each ROM's own CSAVE,
 * edge for edge.
 *
 * The motor relay on PIA1 CA2 is the deck's cue: the player runs while
 * it is closed, and holds its place while it is open. Nothing is clocked
 * (EL §4.3): PA0 is brought up to date when PIA1 is read, and only the
 * ROM's polling loops read it. The recorder is the other half: while the
 * relay is closed it times the DAC's crossings as the ROM's own reader
 * would, finds the sync byte, and appends each block whose checksum is
 * right, with the leader bytes before it, so that a save's image is the
 * one the trap would have written. A block cut short, by the relay or a
 * reset, is not kept, and is counted. A span of the relay in which the
 * ROM wrote does not move the player.
 *
 * The trap (tape.h) stays as the port's cue with fast loading off: it
 * still stalls CSRDON with the deck empty, so that the port finds the
 * tape CLOAD named, and then stands aside. A reader that goes on reading
 * past the tape's end stalls the same way, TAPE_END, where the real
 * machine would wait for ever.
 */
#ifndef PICO_COCO_CASSETTE_H
#define PICO_COCO_CASSETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct coco_s;

/* The writer's counts, in CPU cycles (above). */
#define CAS_HIGH_1       207u
#define CAS_HIGH_0       396u
#define CAS_LOW_1        227u
#define CAS_LOW_0        417u
#define CAS_SILENCE   524365u
#define CAS_CYCLE_TICKS   16u   /* the SAM's slow rate, which wrote the tape */

/* Reads of PA0 after the tape's end before the reader is taken to be
 * waiting on it: about a second of BLKIN's loop. */
#define CAS_END_READS  32768u

typedef struct {
    bool     fast;         /* blocks served by the trap (tape.h): no signal */
    bool     motor;        /* the relay, PIA1 CA2 driven high               */
    bool     playing;
    bool     ended;        /* played to the tape's end since it last moved  */
    bool     level;        /* PA0 as the player drives it                   */
    uint64_t next;         /* the tick of its next change, while playing    */
    uint32_t left;         /* ticks to that change, while stopped           */
    uint32_t end_reads;    /* reads of PA0 since the tape ended             */
    uint32_t edges;        /* changes played, for the heartbeat             */

    /* The walk (cassette.c): the byte in hand and where it is in the
     * image's structure. */
    bool     have;         /* a byte in hand                                */
    bool     started;      /* past the first silence                        */
    uint8_t  byte, bit;    /* its bits from 0; bit 8: none in hand          */
    bool     low;          /* the bit's low half is next                    */
    uint32_t gap;          /* cycles after this byte's last bit             */
    bool     span_end;     /* silence follows it                            */
    uint8_t  idx;          /* in a block: 1 its first $55 ... ; 0 outside   */
    uint8_t  blk_n;        /* the block's data bytes                        */
    uint16_t blk_at;       /* bytes of the block played                     */
    uint8_t  writer;       /* whose file this is, from its name block       */

    /* The recorder. */
    bool     rec_level;    /* the DAC at its middle or above                */
    uint64_t rec_rise;     /* the tick it last rose                         */
    uint8_t  rec_state;
    uint8_t  rec_shift, rec_nbit;
    uint32_t rec_lead;     /* bits since the last block, or the relay       */
    uint16_t rec_got, rec_want;
    bool     rec_kept;     /* a block kept since the relay closed           */
    bool     rec_wrote;    /* the ROM wrote a tone since the relay closed   */
    uint8_t  rec_blk[2u + 255u + 1u];   /* type, length, data, checksum    */
    uint32_t rec_blocks, rec_errors;
} cassette_t;

/* ---- the port's ----------------------------------------------------------- */

/* Fast loading: on, the trap serves every block at once (tape.h); off,
 * the signal. On at coco_init. */
void coco_cassette_set_fast(struct coco_s *m, bool fast);

/* The signal is playing or the ROM may be writing one: the guest may run
 * unpaced (§12.3, EL §9.3). */
bool coco_cassette_running(const struct coco_s *m);

/* ---- coco.c's and tape.c's -------------------------------------------------- */

/* PA0 at tick `t`, the player brought up to it. */
bool cassette_level(struct coco_s *m, uint64_t t);

/* The relay as PIA1's CRA leaves it, at tick `t`. */
void cassette_motor(struct coco_s *m, bool on, uint64_t t);

/* PIA1's port A written at tick `t`: the DAC's six bits, for the
 * recorder. */
void cassette_dac(struct coco_s *m, uint8_t dac, uint64_t t);

/* The tape moved under the player (inserted, ejected, rewound, or read
 * by the trap): it plays on from a byte's start, the silence first. */
void cassette_moved(struct coco_s *m);

/* ---- the walk, on its own --------------------------------------------------- */

/* The half-cycles of the deck's image from where the head stands, for
 * tests: each call gives the ticks to the next change of level, the
 * first of them the silence before the first rise; false at the tape's
 * end. Moves the head. */
bool cassette_walk(struct coco_s *m, uint32_t *ticks);

#endif /* PICO_COCO_CASSETTE_H */
