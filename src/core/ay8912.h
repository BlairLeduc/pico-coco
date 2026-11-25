/* ay8912.h — the AY-3-8912, as the AY-3-8913 of the Speech/Sound
 * Cartridge (design.md §12.9).
 *
 * pico-oric's (its design.md §2.3, §8, §16), brought under §5's rules.
 * The 8913 is the 8912 without its I/O port: the registers of the port
 * are kept and make no sound. Here the chip's clock is the cartridge's,
 * 1.79 MHz, so a "cycle" below is one of its cycles, eight master
 * ticks, and its output goes to the board rather than to a pcm: the
 * board defines ay8912_sink_advance and ay8912_sink_level (ssc.c) and
 * finds itself from the ay8912_t, a member of its own state, as the
 * TMS7040's ports work (tms7000.h). Nothing else changed.
 *
 * Sixteen registers behind an address latch, reached through BDIR and
 * BC1 on a shared data bus. And its sound: three tone generators, one
 * noise generator and one envelope, mixed and turned into a level.
 *
 * The chip knows nothing of its board. The board puts the bus mode and
 * the data lines on it with ay8912_bus(), and reads what the chip drives
 * back from `bus_out` while the mode is a read. The datasheet's modes,
 * BDIR and BC1 (BC2 tied high):
 *   BDIR BC1
 *    0    0   inactive
 *    0    1   read from the latched register
 *    1    0   write to the latched register
 *    1    1   latch an address
 *
 * The bus is level-sensitive: the mode acts for as long as it is held,
 * so a held write follows the data lines, as the part does.
 *
 * The generators count the chip's clock divided by 8, a "tick" every
 * eight cycles, at cycles that are multiples of 8. Each counts up
 * and acts when its count reaches its period, then starts again from 0,
 * so a period shortened below the count acts on the next tick (as
 * MAME's ay8910.cpp has it; F the chip's clock):
 *
 *   tone      toggles every max(1, TP) ticks: f = F / (16 TP)
 *   noise     every max(1, NP) ticks flips a prescaler, and the 17-bit
 *             LFSR (bit 0 XOR bit 3 in at the top, bit 0 out) shifts
 *             when the prescaler returns to 0: f = F / (16 NP)
 *   envelope  steps every max(1, 2 EP) ticks, sixteen steps a cycle:
 *             f = F / (256 EP), and EP = 0 is twice EP = 1
 *
 * A channel is (tone | tone off) & (noise | noise off) times its
 * amplitude, register 8-10's low four bits or, with bit 4, the
 * envelope's. Within a cycle, a register write stamped at it acts
 * first, then the tick if there is one, then the level holds for the
 * cycle.
 *
 * The sound is brought up to date lazily (EL §4.3): at a write to
 * a sound register, and at the field's end. Between those it runs from
 * event to event, and only through the events that can change the level:
 * a generator nobody can hear is caught up arithmetically when it next
 * matters. A tone whose half period is shorter than a sample is not
 * stepped at all: its mean, half its amplitude, stands in.
 */
#ifndef PICO_COCO_AY8912_H
#define PICO_COCO_AY8912_H

#include <stdbool.h>
#include <stdint.h>


#define AY_REG_COUNT  16u

/* Registers with a meaning outside the sound generator. */
#define AY_MIXER      7u      /* bit 6: port A is an output        */
#define AY_ENV_SHAPE 13u
#define AY_PORT_A    14u
#define AY_PORT_B    15u      /* the 8912 has no port B pins        */

#define AY_MIXER_IOA_OUT  0x40u

/* A tick is eight cycles of the chip's clock. */
#define AY_TICK_CYCLES 8u

/* The amplitude of level 15 in the volume table (ay8912.c), in mV above
 * level 0, and the mixed level of all three channels at it: the full
 * scale of what the sink is given. Levels carry twice the amplitude,
 * so that a tone's mean, half of it, is exact. */
#define AY_AMP_MAX    1433u
#define AY_LEVEL_MAX  (3u * 2u * AY_AMP_MAX)

/* The generators, as indices into next[] and per[]. */
enum { AY_GEN_TONE_A, AY_GEN_TONE_B, AY_GEN_TONE_C, AY_GEN_NOISE, AY_GEN_ENV, AY_GEN_COUNT };

typedef enum {
    AY_BUS_INACTIVE = 0,
    AY_BUS_READ     = 1,
    AY_BUS_WRITE    = 2,
    AY_BUS_LATCH    = 3,
} ay_bus_t;

typedef struct {
    uint8_t reg[AY_REG_COUNT];
    uint8_t addr;           /* the latched register number            */
    bool    selected;       /* the last latch carried select code 0000 */
    uint8_t mode;           /* ay_bus_t, as last put on the pins      */
    bool    driving;        /* in a read: the chip drives the bus     */
    uint8_t bus_out;        /* what it drives while `driving`         */
    uint8_t port_a_in;      /* levels outside on port A's pins        */
    uint32_t writes;        /* register writes                         */
    uint32_t env_starts;    /* writes to the shape, which restart it   */

    /* ---- the sound ------------------------------------------------- */

    /* Every cycle before `t` is accounted for: its writes applied, its
     * ticks counted and its level given to the sink. Tick k is at cycle
     * 8k; the first not yet counted is (t + 7) / 8. */
    uint64_t t;

    /* The tick at which each generator next acts, and its period in
     * ticks. A held envelope's next[] means nothing. */
    int64_t  next[AY_GEN_COUNT];
    uint32_t per[AY_GEN_COUNT];

    uint8_t  tone_out;      /* bit per channel                        */
    uint8_t  noise_pre;     /* the noise prescaler                    */
    uint32_t rng;           /* the 17-bit LFSR; bit 0 is the noise    */
    int8_t   env_step;      /* 15 down to 0                           */
    uint8_t  env_attack;    /* 0 or 15, XORed with the step           */
    bool     env_hold, env_alt, env_holding;

    uint8_t  stepped;       /* bit per generator run event by event   */
    uint8_t  averaged;      /* bit per tone whose mean stands in      */
    uint16_t avg_tp;        /* tone periods below this are averaged   */
    bool     avg_off;       /* tests: step every tone, however fast   */

    uint32_t events;        /* ticks run event by event               */
} ay8912_t;

/* The board's (ssc.c): the level holds until cycle `at`; the level from
 * cycle `at`, in the units of AY_LEVEL_MAX. Times only go forward. */
void ay8912_sink_advance(ay8912_t *ay, uint64_t at);
void ay8912_sink_level(ay8912_t *ay, uint64_t at, uint32_t level);

/* RESET: every register to 0 and every generator to its start, at cycle
 * `now`; the pins float high. The level goes to the sink at once. The
 * averaging threshold is the chip's wiring to the output, not its state,
 * and is kept. */
void ay8912_reset(ay8912_t *ay, uint64_t now);

/* Put a bus mode and data on the pins at cycle `now`. A read leaves the
 * register's value in bus_out with `driving` set; any other mode clears
 * it. A write to a sound register brings the sound up to `now` first. */
void ay8912_bus(ay8912_t *ay, ay_bus_t mode, uint8_t data, uint64_t now);

/* Bring the sound up to cycle `now`. */
void ay8912_advance(ay8912_t *ay, uint64_t now);

/* Average tones whose half period, 8 TP cycles, is shorter than a sample
 * of num/den cycles of the chip's. */
void ay8912_set_average(ay8912_t *ay, uint32_t num, uint32_t den);

/* The level now, in the units AY_LEVEL_MAX is in. */
uint32_t ay8912_level(const ay8912_t *ay);

/* The envelope's output now, 0-15. */
static inline unsigned ay8912_env_volume(const ay8912_t *ay) {
    return (unsigned)((ay->env_step ^ ay->env_attack) & 0x0F);
}

/* The amplitude of a 4-bit level, in mV above level 0 (ay8912.c). */
uint16_t ay8912_amplitude(unsigned level);

/* Port A's pins as the outside world sees them: the register while the
 * mixer makes the port an output, otherwise whatever pulls them. */
static inline uint8_t ay8912_port_a(const ay8912_t *ay) {
    return (ay->reg[AY_MIXER] & AY_MIXER_IOA_OUT) ? ay->reg[AY_PORT_A] : ay->port_a_in;
}

#endif /* PICO_COCO_AY8912_H */
