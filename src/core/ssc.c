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

static void sp_catch_up(ssc_t *s, uint64_t tick);

/* ---- the sound ----------------------------------------------------------- */

uint32_t ssc_mix(uint32_t ay_level, int32_t sp) {
    int64_t d = (int64_t)ay_level * SSC_AY_SHARE / AY_LEVEL_MAX +
                (int64_t)sp * SSC_SP_SHARE / (SP0256_LEVEL_MAX + 1);
    int64_t a = d < 0 ? -d : d;
    if (a > SSC_KNEE) {
        /* Above the knee, k + r.e/(r + e): its slope 1 at the knee,
         * falling, and short of the rail however large e grows. */
        int64_t r = SSC_HALF - SSC_KNEE, e = a - SSC_KNEE;
        a = SSC_KNEE + r * e / (r + e);
    }
    return (uint32_t)(SSC_SP_MID + (d < 0 ? -a : a));
}

static void mix(ssc_t *s, uint64_t at) {
    uint32_t v = ssc_mix(s->ay_level, s->sp.out);
    if (v == s->level) return;
    s->level = v;
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
        if (c->line[TMS7000_INT3]) s->effects++;
        tms7000_set_line(c, TMS7000_INT3, false);   /* reading takes the byte */
        return s->porta;
    case TMS7000_PORT_C:
        return s->portc;
    case TMS7000_PORT_D: {
        uint8_t pc = s->portc;
        /* The RAM selected and reading; the AY selected and read. */
        if (!(pc & SSC_C_RCS) && (pc & SSC_C_RW)) s->portd = s->ram[ram_addr(s, pc)];
        if (!(pc & SSC_C_ACS) && !(pc & SSC_C_BDIR) && (pc & SSC_C_BC1)) {
            sp_catch_up(s, s->cycle0 + s->cpu.cycles * SSC_TICKS_PER_CYCLE);
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
        if (v != s->portb) s->effects++;
        s->portb = v;
        break;
    case TMS7000_PORT_C: {
        uint8_t was = s->portc;
        bool ram_write = !(v & SSC_C_RCS) && !(v & SSC_C_RW);
        if (v == was && (v & SSC_C_ACS)) {
            /* The same lines, the AY not selected: only a held RAM write
             * can do anything, of a byte the bus has changed since. */
            if (ram_write && s->ram[ram_addr(s, v)] != s->portd) {
                s->ram[ram_addr(s, v)] = s->portd;
                s->effects++;
            }
            break;
        }
        s->effects++;
        /* The SP0256 and the AY up to now, before either is touched. */
        sp_catch_up(s, s->cycle0 + s->cpu.cycles * SSC_TICKS_PER_CYCLE);
        if (ram_write) s->ram[ram_addr(s, v)] = s->portd;
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
            c->stop = true;   /* the SP0256 is busy: ssc_run interleaves it */
            s->allophones++;
        }
        /* BUSY's latch is cleared on its line's rise. */
        if (!(was & SSC_C_BUSY) && (v & SSC_C_BUSY)) s->busy = false;
        s->portc = v;
        break;
    }
    case TMS7000_PORT_D:
        if (v != s->portd) s->effects++;
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

/* The SP0256's samples due by `tick`, each LRQ onto INT1 after it. */
static void COCO_HOT1(sp_catch_up)(ssc_t *s, uint64_t tick) {
    const uint8_t *al2 = s->al2_now;
    if (!al2) return;
    if (s->sp_next <= tick && !s->exact && sp0256_settled(&s->sp)) {
        /* Standing by, its filter at rest: the samples are silence and
         * LRQ stays high, and only the pitch counter moves. All but the
         * last one or two at once; those the usual way, exactly. */
        uint64_t k = (tick - s->sp_next) * SSC_SP_TICKS_DEN / SSC_SP_TICKS_NUM;
        if (k > 1u) {
            k--;
            sp0256_skip_settled(&s->sp, (uint32_t)k);
            uint64_t frac = s->sp_frac + k * (SSC_SP_TICKS_NUM % SSC_SP_TICKS_DEN);
            s->sp_next += k * (SSC_SP_TICKS_NUM / SSC_SP_TICKS_DEN) + frac / SSC_SP_TICKS_DEN;
            s->sp_frac = (uint32_t)(frac % SSC_SP_TICKS_DEN);
        }
    }
    while (s->sp_next <= tick) {
        ay8912_advance(&s->ay, (s->sp_next - s->cycle0) / SSC_TICKS_PER_AY);
        int16_t before = s->sp.out;
        sp0256_sample(&s->sp, al2);
        if (s->sp.out != before) mix(s, s->sp_next);
        tms7000_set_line(&s->cpu, TMS7000_INT1, s->sp.lrq);
        sp_schedule(s);
    }
}

/* The 7040's state for the repeat test: everything an instruction reads
 * or writes but the timer's count, which a skip runs on (tms7000_skip). */
static void idle_regs(const ssc_t *s, uint8_t r[12]) {
    const tms7000_t *c = &s->cpu;
    r[0] = c->st; r[1] = c->sp; r[2] = c->iocnt0; r[3] = c->t1_ctl;
    r[4] = c->latch[1]; r[5] = c->latch[2]; r[6] = c->latch[3];
    r[7] = c->ddr[2]; r[8] = c->ddr[3];
    r[9] = (uint8_t)(c->line[0] | c->line[2] << 1 | c->idle << 2);
    r[10] = (uint8_t)c->pc; r[11] = (uint8_t)(c->pc >> 8);
}

/* Run the 7040 to `until` while the SP0256 stands by, skipping passes
 * the firmware repeats exactly. At a point it returns to (idle_pc), its
 * state is kept; coming back with the same state, the outside untouched
 * and no interrupt taken (one taken makes the pass longer than the next,
 * which will not have it), the pass in between is a cycle, and as many
 * more as fit before the timer's next interrupt are added at once
 * (tms7000_skip): the state after them is the same. Ends early when the
 * SP0256 is woken (stop). */
static void COCO_HOT1(run_quiet)(ssc_t *s, const uint8_t *rom, uint64_t until) {
    tms7000_t *c = &s->cpu;
    c->stop = false;
    while (c->cycles < until && !c->stop) {
        if (c->pc == s->idle_pc && !s->exact) {
            uint8_t r[12];
            idle_regs(s, r);
            uint64_t period = c->cycles - s->idle_cycles;
            if (s->idle_effects == s->effects && s->idle_entries == c->entries && period &&
                memcmp(r, s->idle_regs, sizeof r) == 0 &&
                memcmp(c->rf, s->idle_rf, sizeof c->rf) == 0) {
                uint64_t room = until - c->cycles, horizon = tms7000_timer_horizon(c);
                if (horizon != UINT64_MAX) room = room < horizon ? room : horizon;
                /* Short of the limit by a margin, so what ends the cycle
                 * happens by stepping. */
                uint64_t n = room > 64u ? (room - 64u) / period : 0;
                if (n) {
                    uint64_t insns = c->instructions - s->idle_insns;
                    tms7000_skip(c, n * period, n * insns);
                    s->skipped += (uint32_t)n;
                }
                s->idle_cycles = c->cycles;
                s->idle_insns = c->instructions;
                s->idle_period = period;
                s->idle_entries = c->entries;
            } else if (period > 8192u || s->idle_effects != s->effects || s->idle_entries != c->entries ||
                       (s->idle_period && period >= s->idle_period)) {
                /* No repeat within a long pass, or the outside touched:
                 * a new snapshot here. A mismatch sooner is the inside of
                 * a pass, and the snapshot stands. */
                memcpy(s->idle_rf, c->rf, sizeof c->rf);
                memcpy(s->idle_regs, r, sizeof r);
                s->idle_cycles = c->cycles;
                s->idle_insns = c->instructions;
                s->idle_effects = s->effects;
                s->idle_entries = c->entries;
            }
        } else if (c->cycles - s->idle_cycles > 16384u) {
            /* Not back in a while: watch where it is now. */
            s->idle_pc = c->pc;
            s->idle_cycles = c->cycles - 16384u;   /* snapshot at the next visit */
            s->idle_effects = s->effects - 1u;
            continue;
        }
        tms7000_step(c, rom);
    }
}

/* One sample of the SP0256 at sp_next, its level mixed; INT1 left to
 * the caller. */
static inline void sp_one(ssc_t *s, const uint8_t *al2) {
    ay8912_advance(&s->ay, (s->sp_next - s->cycle0) / SSC_TICKS_PER_AY);
    int16_t before = s->sp.out;
    sp0256_sample(&s->sp, al2);
    if (s->sp.out != before) mix(s, s->sp_next);
    sp_schedule(s);
}

#define SSC_AHEAD 64u   /* samples the SP0256 may run ahead, LRQ low */

void COCO_HOT1(ssc_run)(ssc_t *s, const uint8_t *rom, const uint8_t *al2, uint64_t now) {
    if (now < s->cycle0) return;
    s->al2_now = al2;
    uint64_t until = (now - s->cycle0) / SSC_TICKS_PER_CYCLE;
    while (s->cpu.cycles < until) {
        if (s->exact) {
            /* Every sample in turn, the 7040 to each (the tests' control). */
            uint64_t to = s->sp_next < now ? s->sp_next : now;
            uint64_t u = (to - s->cycle0) / SSC_TICKS_PER_CYCLE;
            if (s->cpu.cycles < u) tms7000_run(&s->cpu, rom, u);
            if (s->sp_next <= now) sp_catch_up(s, s->sp_next);
            else break;
            continue;
        }
        if (s->sp.lrq) {
            /* LRQ high moves nothing the 7040 sees until the 7040 loads
             * an allophone: the 7040 alone, the SP0256 caught up before it
             * is touched (stop, at a load). */
            run_quiet(s, rom, until);
            continue;
        }
        /* LRQ low: only the sequencer taking the allophone raises it. The
         * SP0256 ahead to that, or SSC_AHEAD samples, or now; then the
         * 7040 to the same moment, and INT1 then. */
        uint64_t limit = now;
        uint64_t ahead = s->sp_next + (uint64_t)SSC_AHEAD * SSC_SP_TICKS_NUM / SSC_SP_TICKS_DEN;
        if (ahead < limit) limit = ahead;
        uint64_t rise = UINT64_MAX;
        while (s->sp_next <= limit) {
            /* The sample that raises LRQ is taken back, and taken again
             * once the 7040 has come to it: before it, a load the 7040
             * writes finds LRQ low and is dropped, as the chip drops it. */
            static sp0256_t before;
            before = s->sp;
            uint64_t at = s->sp_next;
            uint32_t frac = s->sp_frac;
            sp_one(s, al2);
            if (s->sp.lrq) {
                s->sp = before;
                s->sp_next = at;
                s->sp_frac = frac;
                rise = at;
                break;
            }
        }
        uint64_t to = rise != UINT64_MAX ? rise : (limit < now ? limit : now);
        uint64_t u = (to - s->cycle0) / SSC_TICKS_PER_CYCLE;
        /* A load written now is dropped (LRQ low) but stops the run: on to
         * the mark regardless. */
        while (s->cpu.cycles < u) run_quiet(s, rom, u);
        if (rise != UINT64_MAX) sp_catch_up(s, rise);   /* LRQ onto INT1 with it */
        else tms7000_set_line(&s->cpu, TMS7000_INT1, s->sp.lrq);
        if (rise == UINT64_MAX && s->sp_next > now) {
            while (s->cpu.cycles < until) run_quiet(s, rom, until);
            break;
        }
    }
    sp_catch_up(s, now);
    ay8912_advance(&s->ay, (now - s->cycle0) / SSC_TICKS_PER_AY);
    s->al2_now = NULL;
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
        s->bytes++;
        s->busy = true;
        tms7000_set_line(&s->cpu, TMS7000_INT3, true);
    }
}
