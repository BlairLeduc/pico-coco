/* ssc.c — the Speech/Sound Cartridge's board (ssc.h, design.md §12.9). */

#include "ssc.h"

#include <stddef.h>
#include <string.h>

#include "hot.h"

static inline ssc_t *board_of_cpu(tms7000_t *c) {
    return (ssc_t *)((char *)c - offsetof(ssc_t, cpu));
}

static inline ssc_t *board_of_ay(ay8912_t *ay) {
    return (ssc_t *)((char *)ay - offsetof(ssc_t, ay));
}

static inline uint16_t ram_addr(const ssc_t *s, uint8_t c) {
    return (uint16_t)(((unsigned)(c & 0x07u) << 8 | s->portb) & (SSC_RAM_SIZE - 1u));
}

/* The 7040's cycle now as the AY's: two of them. */
static inline uint64_t ay_now(const ssc_t *s) {
    return s->cpu.cycles * (SSC_TICKS_PER_CYCLE / SSC_TICKS_PER_AY);
}

/* ---- the sound ----------------------------------------------------------- */

static void mix(ssc_t *s, uint64_t at) {
    int32_t sp = s->sp.out;
    int64_t v = (int64_t)s->ay_level * SSC_AY_SHARE / AY_LEVEL_MAX + SSC_SP_MID +
                (int64_t)sp * SSC_SP_SHARE / (SP0256_LEVEL_MAX + 1);
    if (v < 0) v = 0;
    if (v > SSC_LEVEL_MAX) v = SSC_LEVEL_MAX;
    if ((uint32_t)v == s->level) return;
    s->level = (uint32_t)v;
    if (s->attached) ssc_level_changed(s, at);
}

void ay8912_sink_advance(ay8912_t *ay, uint64_t at) {
    (void)ay;
    (void)at;   /* the level holds; the machine's pcm advances itself */
}

void ay8912_sink_level(ay8912_t *ay, uint64_t at, uint32_t level) {
    ssc_t *s = board_of_ay(ay);
    s->ay_level = level;
    mix(s, s->cycle0 + at * SSC_TICKS_PER_AY);
}

/* ---- the 7040's ports --------------------------------------------------- */

uint8_t tms7000_port_in(tms7000_t *c, unsigned port) {
    ssc_t *s = board_of_cpu(c);
    switch (port) {
    case TMS7000_PORT_A:
        tms7000_set_line(c, TMS7000_INT3, false);   /* reading takes the byte */
        return s->porta;
    case TMS7000_PORT_C:
        return s->portc;
    case TMS7000_PORT_D: {
        uint8_t pc = s->portc;
        /* The RAM selected and reading; the AY selected and read. */
        if (!(pc & SSC_C_RCS) && (pc & SSC_C_RW)) s->portd = s->ram[ram_addr(s, pc)];
        if (!(pc & SSC_C_ACS) && !(pc & SSC_C_BDIR) && (pc & SSC_C_BC1)) {
            ay8912_bus(&s->ay, AY_BUS_READ, 0, ay_now(s));
            if (s->ay.driving) s->portd = s->ay.bus_out;
        }
        return s->portd;
    }
    default:
        return 0xFFu;
    }
}

void tms7000_port_out(tms7000_t *c, unsigned port, uint8_t v) {
    ssc_t *s = board_of_cpu(c);
    switch (port) {
    case TMS7000_PORT_B:
        s->portb = v;
        break;
    case TMS7000_PORT_C: {
        uint8_t was = s->portc;
        if (!(v & SSC_C_RCS) && !(v & SSC_C_RW)) s->ram[ram_addr(s, v)] = s->portd;
        if (!(v & SSC_C_ACS)) {
            /* BDIR and BC1 as the AY's bus modes (ay8912.h); a mode held
             * while the rest of port C changes is the same write. */
            ay_bus_t mode = (v & SSC_C_BDIR) ? ((v & SSC_C_BC1) ? AY_BUS_LATCH : AY_BUS_WRITE)
                                             : ((v & SSC_C_BC1) ? AY_BUS_READ : AY_BUS_INACTIVE);
            if (mode != AY_BUS_READ) ay8912_bus(&s->ay, mode, s->portd, ay_now(s));
        } else if (!(was & SSC_C_ACS)) {
            ay8912_bus(&s->ay, AY_BUS_INACTIVE, 0, ay_now(s));
        }
        /* ALD*'s fall loads an allophone, 0 to 63 (MAME drops others). */
        if ((was & SSC_C_ALD) && !(v & SSC_C_ALD) && s->portd < 64u) {
            sp0256_ald(&s->sp, s->portd);
            tms7000_set_line(c, TMS7000_INT1, s->sp.lrq);   /* LRQ falls at once */
            s->allophones++;
        }
        /* BUSY's latch is cleared on its line's rise. */
        if (!(was & SSC_C_BUSY) && (v & SSC_C_BUSY)) s->busy = false;
        s->portc = v;
        break;
    }
    case TMS7000_PORT_D:
        s->portd = v;
        break;
    default:
        break;
    }
}

/* ---- running ------------------------------------------------------------ */

static inline void sp_schedule(ssc_t *s) {
    s->sp_next += SSC_SP_TICKS_NUM / SSC_SP_TICKS_DEN;
    s->sp_frac += SSC_SP_TICKS_NUM % SSC_SP_TICKS_DEN;
    if (s->sp_frac >= SSC_SP_TICKS_DEN) {
        s->sp_frac -= SSC_SP_TICKS_DEN;
        s->sp_next++;
    }
}

void COCO_HOT1(ssc_run)(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint64_t now) {
    if (now < s->cycle0) return;
    for (;;) {
        /* The 7040 up to the next sample, or to `now`: instructions that
         * start before it. */
        uint64_t to = s->sp_next < now ? s->sp_next : now;
        uint64_t until = (to - s->cycle0) / SSC_TICKS_PER_CYCLE;
        if (s->cpu.cycles < until) tms7000_run(&s->cpu, rom, until);
        if (s->sp_next > now) break;
        /* The AY up to the sample, so the levels go out in time order;
         * the sample; and LRQ onto INT1 after it. */
        ay8912_advance(&s->ay, (s->sp_next - s->cycle0) / SSC_TICKS_PER_AY);
        int16_t before = s->sp.out;
        sp0256_sample(&s->sp, al2);
        if (s->sp.out != before) mix(s, s->sp_next);
        tms7000_set_line(&s->cpu, TMS7000_INT1, s->sp.lrq);
        sp_schedule(s);
    }
    ay8912_advance(&s->ay, (now - s->cycle0) / SSC_TICKS_PER_AY);
}

void ssc_init(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t now) {
    memset(s, 0, sizeof *s);
    s->cycle0 = now;
    s->reset_line = 1;
    s->sp_next = now;
    sp0256_reset(&s->sp);
    s->level = SSC_SP_MID;
    ay8912_reset(&s->ay, 0);
    tms7000_init(&s->cpu);
    tms7000_set_line(&s->cpu, TMS7000_INT1, s->sp.lrq);
    tms7000_reset(&s->cpu, rom);
}

uint8_t ssc_read(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint16_t a, uint64_t now) {
    if (a != SSC_DATA) return 0xFFu;
    ssc_run(s, rom, al2, now);
    uint8_t v = 0x1Fu;
    if (!s->busy) v |= SSC_ST_BUSY;
    if (s->sp.sby) v |= SSC_ST_SPEAK;
    /* The sound activity detector is analogue, on the AY's output (MAME
     * models a filter and two thresholds, §18): here, a generator the
     * level depends on. */
    if (!(s->ay.stepped | s->ay.averaged)) v |= SSC_ST_SOUND;
    return v;
}

void ssc_write(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint16_t a, uint8_t v,
               uint64_t now) {
    ssc_run(s, rom, al2, now);
    if (a == SSC_RESET) {
        /* Bit 0 high holds the SP0256 in reset; its fall resets the 7040
         * and the AY (MAME's coco_ssc.cpp). */
        if (v & 1u) {
            sp0256_reset(&s->sp);
            tms7000_set_line(&s->cpu, TMS7000_INT1, s->sp.lrq);
        }
        if ((s->reset_line & 1u) && !(v & 1u)) {
            tms7000_reset(&s->cpu, rom);
            ay8912_reset(&s->ay, ay_now(s));
            s->busy = false;
        }
        s->reset_line = v;
    } else if (a == SSC_DATA) {
        s->porta = v;
        s->busy = true;
        tms7000_set_line(&s->cpu, TMS7000_INT3, true);
    }
}
