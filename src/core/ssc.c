/* ssc.c — the Speech/Sound Cartridge's board (ssc.h, design.md §12.9). */

#include "ssc.h"

#include <stddef.h>
#include <string.h>

#include "hot.h"

static inline ssc_t *board_of(tms7000_t *c) {
    return (ssc_t *)((char *)c - offsetof(ssc_t, cpu));
}

static inline uint16_t ram_addr(const ssc_t *s, uint8_t c) {
    return (uint16_t)(((unsigned)(c & 0x07u) << 8 | s->portb) & (SSC_RAM_SIZE - 1u));
}

/* ---- the 7040's ports --------------------------------------------------- */

uint8_t tms7000_port_in(tms7000_t *c, unsigned port) {
    ssc_t *s = board_of(c);
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
        if (!(pc & SSC_C_ACS) && !(pc & SSC_C_BDIR) && (pc & SSC_C_BC1))
            s->portd = s->ay_reg[s->ay_addr & 0x0Fu];
        return s->portd;
    }
    default:
        return 0xFFu;
    }
}

void tms7000_port_out(tms7000_t *c, unsigned port, uint8_t v) {
    ssc_t *s = board_of(c);
    switch (port) {
    case TMS7000_PORT_B:
        s->portb = v;
        break;
    case TMS7000_PORT_C: {
        uint8_t was = s->portc;
        if (!(v & SSC_C_RCS) && !(v & SSC_C_RW)) s->ram[ram_addr(s, v)] = s->portd;
        if (!(v & SSC_C_ACS)) {
            if ((v & (SSC_C_BDIR | SSC_C_BC1)) == (SSC_C_BDIR | SSC_C_BC1)) s->ay_addr = s->portd;
            else if ((v & SSC_C_BDIR) && !(v & SSC_C_BC1)) s->ay_reg[s->ay_addr & 0x0Fu] = s->portd;
        }
        /* ALD*'s fall loads an allophone, 0 to 63. */
        if ((was & SSC_C_ALD) && !(v & SSC_C_ALD) && s->portd < 64u) {
            s->allophones++;
            s->last_allophone = s->portd;
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

/* ---- the 6809's side ---------------------------------------------------- */

void ssc_init(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t now) {
    memset(s, 0, sizeof *s);
    s->cycle0 = now;
    s->reset_line = 1;
    tms7000_init(&s->cpu);
    tms7000_reset(&s->cpu, rom);
}

void COCO_HOT1(ssc_run)(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint64_t now) {
    if (now <= s->cycle0) return;
    uint64_t until = (now - s->cycle0) / SSC_TICKS_PER_CYCLE;
    if (s->cpu.cycles < until) tms7000_run(&s->cpu, rom, until);
}

uint8_t ssc_read(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint16_t a, uint64_t now) {
    if (a != SSC_DATA) return 0xFFu;
    ssc_run(s, rom, now);
    uint8_t v = 0x1Fu;
    if (!s->busy) v |= SSC_ST_BUSY;
    v |= SSC_ST_SPEAK;   /* M18: the SP0256's SBY */
    v |= SSC_ST_SOUND;   /* M18: the sound activity detector */
    return v;
}

void ssc_write(ssc_t *s, const uint8_t rom[TMS7000_ROM_SIZE], uint16_t a, uint8_t v, uint64_t now) {
    ssc_run(s, rom, now);
    if (a == SSC_RESET) {
        /* M18: bit 0 high holds the SP0256 in reset. Its fall resets the
         * 7040 and the AY. */
        if ((s->reset_line & 1u) && !(v & 1u)) {
            tms7000_reset(&s->cpu, rom);
            memset(s->ay_reg, 0, sizeof s->ay_reg);
            s->busy = false;
        }
        s->reset_line = v;
    } else if (a == SSC_DATA) {
        s->porta = v;
        s->busy = true;
        tms7000_set_line(&s->cpu, TMS7000_INT3, true);
    }
}
