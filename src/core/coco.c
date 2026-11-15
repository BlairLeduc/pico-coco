/* coco.c — the machine (design.md §6, §8, §13). See coco.h.
 *
 * The wiring here is §3's, and every line of it is a row of §18 until the
 * real ROMs, executed on the host, settle it. Where XRoar is the source
 * (D8), the comment says so.
 */

#include "coco.h"

#include <string.h>

#include "bus.h"
#include "hot.h"

/* ---- configuration ---------------------------------------------------- */

void coco_config_default(coco_config_t *cfg) {
    cfg->machine = COCO_MACHINE_COCO;
    cfg->ram_k = 64;
}

/* The chips for each size, as XRoar fits them: 4K x 1, 16K x 1, 32K x 1
 * (half-good 64K parts) and 64K x 1, one bank of each (§8.1, §18). */
static void set_ram_org(coco_t *m) {
    switch (m->cfg.ram_k) {
    case 4:  m->ram_addr_bits = 12; m->ram_row_bits = 6; break;
    case 16: m->ram_addr_bits = 14; m->ram_row_bits = 7; break;
    case 32: m->ram_addr_bits = 15; m->ram_row_bits = 8; break;
    default: m->cfg.ram_k = 64;
             m->ram_addr_bits = 16; m->ram_row_bits = 8; break;
    }
    m->ram_banks = 1;
}

/* ---- RAM through the SAM ---------------------------------------------- */

/* The SAM puts a row and a column on the multiplexed address lines, and a
 * RAS line picks the bank; the chip uses as many of each as it has. By
 * memory size M (§8.2), as XRoar's model of the MC6883 has them (§18:
 * medium): 4K and 16K modes take a second bank on A12 or A14 and force the
 * high row and column bit for the first; 64K mode takes the page bit P as
 * the column's top bit. */
int32_t coco_ram_index(const coco_t *m, uint16_t a) {
    static const uint16_t row_mask[4]  = { 0x7Fu, 0x7Fu, 0xFFu, 0xFFu };
    static const uint8_t  col_shift[4] = { 6, 7, 8, 8 };
    static const uint16_t col_mask[4]  = { 0x3Fu, 0x7Fu, 0xFFu, 0xFFu };
    static const uint16_t ras1_bit[4]  = { 0x1000u, 0x4000u, 0u, 0u };

    unsigned msize = sam6883_m(&m->sam);
    unsigned ras1 = msize < 2 ? 0x80u : 0u;
    unsigned page = (msize >= 2 && sam6883_p(&m->sam)) ? 0x80u : 0u;
    unsigned extra = (a & ras1_bit[msize]) ? 0u : ras1;
    unsigned row = (a & row_mask[msize]) | extra;
    unsigned col = ((a >> col_shift[msize]) & col_mask[msize]) | extra | page;
    unsigned bank = (ras1 && (a & ras1_bit[msize])) ? 1u : 0u;
    if (bank >= m->ram_banks) return -1;

    unsigned r = m->ram_row_bits, c = (unsigned)m->ram_addr_bits - r;
    unsigned idx = (row & ((1u << r) - 1u)) | ((col & ((1u << c) - 1u)) << r);
    return (int32_t)((bank << m->ram_addr_bits) | idx);
}

/* ---- the page table (§8.1) -------------------------------------------- */

/* Rebuilt when a bit that chooses the map changes: the SAM's M, P and TY,
 * and a ROM loaded. A RAM page takes the fast path when its 256 bytes lie
 * in order in one chip bank; otherwise every byte is translated on the
 * slow path. */
static void map(coco_t *m) {
    bool ty = sam6883_ty(&m->sam);
    for (unsigned p = 0; p < COCO_IO_PAGE; p++) {
        page_t *pg = &m->page[p];
        uint16_t base = (uint16_t)(p << 8);
        if (p < 0x80u || ty) {
            int32_t i0 = coco_ram_index(m, base);
            bool flat = i0 >= 0;
            for (unsigned i = 1; flat && i < COCO_PAGE_SIZE; i++)
                flat = coco_ram_index(m, (uint16_t)(base + i)) == i0 + (int32_t)i;
            pg->read = flat ? &m->ram[i0] : NULL;
            pg->write = flat ? &m->ram[i0] : NULL;
        } else if (p < 0xC0u) {
            unsigned half = (p - 0x80u) >> 5;
            pg->read = m->rom_present[half] ? &m->rom[(p - 0x80u) << 8] : NULL;
            pg->write = NULL;
        } else {
            pg->read = m->cart_present ? &m->cart[(p - 0xC0u) << 8] : NULL;
            pg->write = NULL;
        }
    }
    m->page[COCO_IO_PAGE].read = NULL;
    m->page[COCO_IO_PAGE].write = NULL;
}

/* ---- time ------------------------------------------------------------- */

uint64_t COCO_HOT1(coco_ticks)(const coco_t *m) {
    return m->tick0 + (m->cpu.cycles - m->cyc0) * m->tpc;
}

/* The ticks a cycle takes, by the SAM's rate (§8.2): 16 at rate 0, 8 with
 * R1 set. M11: R0's address-dependent rate takes a second run loop; until
 * then it runs at 16. */
static void retime(coco_t *m) {
    m->tick0 = coco_ticks(m);
    m->cyc0 = m->cpu.cycles;
    m->tpc = (sam6883_r(&m->sam) & 2u) ? 8u : 16u;
}

/* ---- interrupts ------------------------------------------------------- */

/* PIA0's two outputs on IRQ, PIA1's on FIRQ (§3.3). */
static void update_irq(coco_t *m) {
    mc6809_set_irq(&m->cpu, COCO_IRQ_PIA0, mc6821_irq(&m->pia0.a) || mc6821_irq(&m->pia0.b));
    mc6809_set_firq(&m->cpu, COCO_FIRQ_PIA1, mc6821_irq(&m->pia1.a) || mc6821_irq(&m->pia1.b));
}

/* The PIAs' interrupt outputs rise tRS3 after an enabled edge (mc6821.h):
 * 1 us, 14 ticks as XRoar counts it (§18). Each port's pending rise is a
 * tick, or 0, and the first edge's stands, as XRoar's event does. The
 * field interrupt's timing in XRoar's boot trace is the edge plus this
 * plus the CPU's latch (2026-11-14). */
#define TRS3_TICKS 14u

static mc6821_port_t *pia_port(coco_t *m, unsigned i) {
    switch (i) {
    case 0:  return &m->pia0.a;
    case 1:  return &m->pia0.b;
    case 2:  return &m->pia1.a;
    default: return &m->pia1.b;
    }
}

/* C1 of port `i` takes `level` at tick `t`. */
static void c1_edge(coco_t *m, unsigned i, bool level, uint64_t t) {
    if (mc6821_set_c1(pia_port(m, i), level) && !m->irq_due[i])
        m->irq_due[i] = t + TRS3_TICKS;
}

static void raise_due(coco_t *m, uint64_t t) {
    for (unsigned i = 0; i < 4u; i++)
        if (m->irq_due[i] && m->irq_due[i] <= t) {
            mc6821_raise(pia_port(m, i));
            m->irq_due[i] = 0;
        }
}

static uint64_t next_due(const coco_t *m) {
    uint64_t d = UINT64_MAX;
    for (unsigned i = 0; i < 4u; i++)
        if (m->irq_due[i] && m->irq_due[i] < d) d = m->irq_due[i];
    return d;
}

/* HS, on PIA0 CA1, is brought up to date lazily (§8.3, EL §4.3): lines run
 * from tick 0, since fields follow one another whole, and HS is low for
 * the first hs_low_ticks of each. */
static bool hs_level(const coco_t *m, uint64_t t) {
    return (t % m->line_ticks) >= m->hs_low_ticks;
}

/* The first HS edge after `t` of the kind CA1 is set to take. */
static uint64_t hs_next_edge(const coco_t *m, uint64_t t) {
    uint64_t off = (m->pia0.a.cr & MC6821_CR_C1_RISE) ? m->hs_low_ticks : 0u;
    if (t < off) return off;
    return ((t - off) / m->line_ticks + 1u) * m->line_ticks + off;
}

static void hs_catch_up(coco_t *m, uint64_t t) {
    if (t <= m->hs_seen) return;
    uint64_t e = hs_next_edge(m, m->hs_seen);
    if (e <= t) {
        bool active = (m->pia0.a.cr & MC6821_CR_C1_RISE) != 0;
        c1_edge(m, 0, !active, e);
        c1_edge(m, 0, active, e);
    }
    c1_edge(m, 0, hs_level(m, t), t);
    m->hs_seen = t;
}

/* FS, on PIA0 CB1, falls as line fs_fall_line begins (§13.1). The field
 * loop sets it once the run passes it; an access to a PIA within the
 * instruction that passes it brings it up first, so a read that clears the
 * flag sees the edge that came before it, as XRoar's trace has it. */
static void fs_catch_up(coco_t *m, uint64_t t) {
    uint64_t fall = m->field_start + (uint64_t)m->fs_fall_line * m->line_ticks;
    uint64_t end = m->field_start + (uint64_t)m->field_lines * m->line_ticks;
    if (t >= fall && t < end && m->pia0.b.c1) c1_edge(m, 1, false, fall);
}

static void bring_up(coco_t *m, uint64_t t) {
    hs_catch_up(m, t);
    fs_catch_up(m, t);
    raise_due(m, t);
}

/* When the slow-path access now being made falls: the instruction began
 * at the CPU's count, and the access is at the end of cycle io_at of it,
 * by when the VDG has reached that tick (bus.h). */
static uint64_t access_tick(const coco_t *m) {
    return coco_ticks(m) + (uint64_t)(m->cpu.io_at & ~MC6809_IO_LAST) * m->tpc;
}

/* Before a PIA access: everything up to it, and if it is the instruction's
 * last cycle, the CPU decides at the boundary on the lines as they stood
 * at that cycle's start (mc6809.h). */
static void before_pia(coco_t *m) {
    uint64_t t = access_tick(m);
    if (m->cpu.io_at & MC6809_IO_LAST) {
        bring_up(m, t - m->tpc);
        update_irq(m);
        mc6809_lines_late(&m->cpu);
    }
    bring_up(m, t);
}

/* ---- the PIAs' pins (§3.3) -------------------------------------------- */

/* PIA0 port A: the keyboard's rows, low where a pressed key's column is
 * driven low, and PA7 the joystick comparator. With no joystick the axis
 * reads at its middle and is compared with the DAC, as XRoar does (§11.4). */
static uint8_t pia0a_pins(const coco_t *m) {
    uint8_t cols_low = (uint8_t)~mc6821_out(&m->pia0.b);
    uint8_t rows = 0x7Fu;
    for (unsigned c = 0; c < COCO_KEY_COLS; c++)
        if (cols_low & (1u << c)) rows &= (uint8_t)~m->keys[c];
    unsigned dac = (((unsigned)mc6821_out(&m->pia1.a) & 0xFCu) | 2u) << 8;
    return (uint8_t)(rows | (32767u >= dac ? 0x80u : 0u));
}

/* A 32 or 64 KB CoCo wires PIA0 PB6 to PIA1 PB2 (XRoar; §18). */
static bool linked_pb(const coco_t *m) {
    return m->cfg.machine == COCO_MACHINE_COCO && m->cfg.ram_k >= 32;
}

/* Port B has no pull-ups, unlike port A (MC6821 datasheet): an input
 * nothing drives reads low, as XRoar has it, and the boot trace agrees
 * only so (2026-11-14, §18). */
static uint8_t pia0b_pins(const coco_t *m) {
    uint8_t in = 0x00u;
    if (linked_pb(m) && (mc6821_out(&m->pia1.b) & 0x04u)) in |= 0x40u;
    return in;
}

/* PIA1 port B: on the CoCo, PB2 is the RAM-size strap, low with 4K chips
 * and pulled high with 16K, or the link to PIA0 PB6 with 32K and 64K
 * (XRoar; §18). The Dragon's PB0 is the printer's BUSY, and its PB2 is
 * high with 4K or 16K chips and low with larger (XRoar's dragon32.c; the
 * Dragon 32's boot trace agrees in 16, 32 and 64 KB, 2026-11-14). */
static uint8_t pia1b_pins(const coco_t *m) {
    /* PB1, the single-bit sound, set as an input reads the sound output
     * back, inverted (XRoar's single_bit_feedback): high while it is
     * silent, as it is until M8 brings the audio (§10). XRoar feeds it
     * back only once its sound has updated, and reads it low before
     * then: the Dragon 32 reads it 23 instructions after reset
     * (2026-11-14, §18). */
    uint8_t in = m->sound_fed ? 0x02u : 0x00u;
    if (m->cfg.machine == COCO_MACHINE_COCO) {
        if (m->cfg.ram_k == 16) in |= 0x04u;
        else if (linked_pb(m) && (mc6821_out(&m->pia0.b) & 0x40u)) in |= 0x04u;
    } else {
        /* PB0 is BUSY, which XRoar's printer, absent, never asserts,
         * over the pull-up (its pia1b_data_preread; §18). */
        if (m->cfg.ram_k <= 16) in |= 0x04u;
    }
    return in;
}

/* XRoar's sound updates, and feeds PB1 back, at each FS rise and when the
 * single-bit sound or the multiplexer is enabled: PB1 made an output, or
 * CB2 an output driven high (its sound.c, dragon.c; §18). */
static void note_sound(coco_t *m) {
    const mc6821_port_t *b = &m->pia1.b;
    if ((b->ddr & 0x02u) || ((b->cr & MC6821_CR_C2_OUT) && mc6821_c2_out(b)))
        m->sound_fed = true;
}

/* ---- the I/O page ----------------------------------------------------- */

/* What an address nothing drives reads: the data bus as the last cycle
 * left it (§8.1). This core does not model the CPU's dead cycles, but the
 * cycle before the data access of a direct, extended or 5-bit-offset
 * indexed instruction is one, with $FFFF on the address bus, so the bus
 * holds the ROM's byte there. XRoar, which models every cycle, read it
 * there in the boot trace (2026-11-14, §18); the modes whose last cycle
 * before the access is another (,R reads PC, an indirect reads the
 * pointer) are not modelled. */
static uint8_t open_bus(const coco_t *m) {
    return m->rom_present[1] ? m->rom[0x3FFFu] : 0xFFu;
}

/* The CoCo's PIAs, and an unexpanded Dragon 32's, ignore A2 and repeat
 * over their 32 bytes; a Dragon with more than 32 KB answers only with A2
 * low (XRoar's relaxed decode; §8.1, §18). */
static bool pia_selected(const coco_t *m, uint16_t a) {
    return m->cfg.machine == COCO_MACHINE_COCO || m->cfg.ram_k <= 32 || !(a & 4u);
}

static uint8_t io_read(coco_t *m, uint16_t a) {
    unsigned off = a & 0xFFu;
    unsigned reg = a & 3u;
    if (off < 0x20u) {
        if (!pia_selected(m, a)) return open_bus(m);
        before_pia(m);
        if (reg == 0u) m->pia0.a.in = pia0a_pins(m);
        if (reg == 2u) m->pia0.b.in = pia0b_pins(m);
        uint8_t v = mc6821_read(&m->pia0, reg);
        update_irq(m);
        return v;
    }
    if (off < 0x40u) {
        if (!pia_selected(m, a)) return open_bus(m);
        before_pia(m);
        if (reg == 2u) m->pia1.b.in = pia1b_pins(m);
        /* PA0 is the cassette's comparator, low with no tape (XRoar; the
         * trace agrees, §18). M9: the tape. */
        m->pia1.a.in = 0xFEu;
        uint8_t v = mc6821_read(&m->pia1, reg);
        update_irq(m);
        return v;
    }
    if (off >= 0xE0u)   /* the vectors, from the BASIC ROM whatever the map */
        return m->rom_present[1] ? m->rom[0x3FE0u + (off - 0xE0u)] : open_bus(m);
    return open_bus(m);  /* M13: cartridge I/O at $FF40; the rest unused */
}

static void io_write(coco_t *m, uint16_t a, uint8_t v) {
    unsigned off = a & 0xFFu;
    unsigned reg = a & 3u;
    if (off < 0x20u) {
        if (!pia_selected(m, a)) return;
        before_pia(m);
        mc6821_write(&m->pia0, reg, v);
        update_irq(m);
        return;
    }
    if (off < 0x40u) {
        if (!pia_selected(m, a)) return;
        before_pia(m);
        mc6821_write(&m->pia1, reg, v);   /* M4: VDG mode; M8: sound */
        note_sound(m);
        update_irq(m);
        return;
    }
    if (off >= 0xC0u && off < 0xE0u) {
        uint16_t changed = sam6883_write(&m->sam, a);
        if (changed & SAM_MAP_BITS) map(m);
        if (changed & SAM_R_MASK) retime(m);
    }
}

/* ---- the slow path (bus.c) -------------------------------------------- */

static bool ram_space(const coco_t *m, uint16_t a) {
    return a < 0x8000u || sam6883_ty(&m->sam);
}

uint8_t COCO_HOT1(coco_slow_read)(coco_t *m, uint16_t a) {
    if ((a >> 8) == COCO_IO_PAGE) return io_read(m, a);
    if (ram_space(m, a)) {
        /* No chip answers: the data bus reads high, as XRoar has it (§18). */
        int32_t i = coco_ram_index(m, a);
        return i < 0 ? 0xFFu : m->ram[i];
    }
    return open_bus(m);   /* an empty ROM socket or cartridge slot */
}

void COCO_HOT1(coco_slow_write)(coco_t *m, uint16_t a, uint8_t v) {
    if ((a >> 8) == COCO_IO_PAGE) {
        io_write(m, a, v);
        return;
    }
    if (ram_space(m, a)) {
        int32_t i = coco_ram_index(m, a);
        if (i >= 0) m->ram[i] = v;
    }
    /* A write to ROM goes nowhere. */
}

/* ---- the machine ------------------------------------------------------ */

void coco_init(coco_t *m, const coco_config_t *cfg) {
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    set_ram_org(m);

    /* NTSC's field, from the MC6847's figures as XRoar counts them (§13.1,
     * §18: medium). */
    m->line_ticks = 57u * 16u;
    m->field_lines = 262u;
    m->fs_fall_line = 13u + 25u + 192u;
    m->hs_low_ticks = 64u;
    if (cfg->machine == COCO_MACHINE_DRAGON32) {
        /* The Dragon's PAL field, as XRoar's vdg_pal.c makes it (§13.1,
         * §18: low): the VDG is held for 25 lines of HS alone at its line
         * 254 and again at its line 0, where FS has just risen. So 312
         * lines, every one with its HS; FS falls 25 + 230 lines in. The VDG
         * starts at its line 0 with nothing held, so the first field lacks
         * the leading 25. */
        m->field_lines = 262u + 50u;
        m->fs_fall_line = 25u + m->fs_fall_line;
        m->field_lead = 25u;
    }

    m->tpc = 16u;
    mc6809_init(&m->cpu);
    map(m);
}

bool coco_load_rom(coco_t *m, uint16_t base, const uint8_t *data, size_t len) {
    if (base == 0x8000u && len == 0x4000u) {
        memcpy(m->rom, data, len);
        m->rom_present[0] = m->rom_present[1] = true;
    } else if ((base == 0x8000u || base == 0xA000u) && len == 0x2000u) {
        unsigned half = base == 0xA000u;
        memcpy(&m->rom[half * 0x2000u], data, len);
        m->rom_present[half] = true;
    } else if (base == 0xC000u && len > 0 && len <= 0x4000u) {
        memset(m->cart, 0xFF, sizeof(m->cart));   /* M9: mirrors */
        memcpy(m->cart, data, len);
        m->cart_present = true;
    } else {
        return false;
    }
    map(m);
    return true;
}

void coco_reset(coco_t *m) {
    mc6821_reset(&m->pia0);
    mc6821_reset(&m->pia1);
    sam6883_reset(&m->sam);
    map(m);
    retime(m);
    uint64_t t = coco_ticks(m);
    memset(m->irq_due, 0, sizeof(m->irq_due));
    m->hs_seen = t;
    m->pia0.a.c1 = hs_level(m, t);
    m->pia0.b.c1 = t - m->field_start < (uint64_t)m->fs_fall_line * m->line_ticks;
    update_irq(m);
    mc6809_reset(m);
}

void coco_power_on(coco_t *m) {
    memset(m->ram, 0, sizeof(m->ram));   /* §13.3 */
    memset(m->keys, 0, sizeof(m->keys));
    mc6809_init(&m->cpu);
    m->tick0 = m->cyc0 = 0;
    /* The first field began field_lead lines before tick 0: modulo 2^64,
     * as every use of field_start adds to it or subtracts it. */
    m->field_start = 0u - (uint64_t)m->field_lead * m->line_ticks;
    m->fields = 0;
    m->sound_fed = false;
    coco_reset(m);
}

/* A slice ends at the target, or a cycle after a PIA's interrupt output
 * is due to rise, or at HS's edge while its interrupt is enabled, which
 * BASIC leaves disabled (§8.3). After each, a rise is seen if it came
 * before the last instruction's last cycle, as the CPU latches its lines
 * (mc6809.h). */
uint32_t COCO_HOT2(coco_run)(coco_t *m, uint32_t ticks) {
    uint64_t start = coco_ticks(m), target = start + ticks;
    for (;;) {
        uint64_t now = coco_ticks(m);
        /* A rise already due when the run starts, or after an edge set
         * between runs, is seen at this boundary. */
        if (now >= m->tpc) raise_due(m, now - m->tpc);
        update_irq(m);
        if (now >= target) break;
        uint64_t stop = target;
        if (m->pia0.a.cr & MC6821_CR_C1_IRQ) {
            uint64_t e = hs_next_edge(m, now);
            if (e < stop) stop = e;
        }
        uint64_t due = next_due(m);
        if (due != UINT64_MAX && due + m->tpc < stop) stop = due + m->tpc;
        if (stop <= now) stop = now + 1u;
        uint32_t cycles = (uint32_t)((stop - now + m->tpc - 1u) / m->tpc);
        if (__builtin_expect(m->step_hook != NULL, 0)) {
            (void)mc6809_run(m, 1u);
            m->step_hook(m);
        } else {
            (void)mc6809_run(m, cycles);
        }
        hs_catch_up(m, coco_ticks(m));
    }
    return (uint32_t)(coco_ticks(m) - start);
}

/* FS falls at its line, set at the first boundary at or after it (or
 * sooner, by an access to a PIA), and rises as the next field begins. */
uint32_t COCO_HOT2(coco_run_field)(coco_t *m) {
    uint64_t start = coco_ticks(m);
    uint64_t fall = m->field_start + (uint64_t)m->fs_fall_line * m->line_ticks;
    uint64_t end = m->field_start + (uint64_t)m->field_lines * m->line_ticks;

    uint64_t now = coco_ticks(m);
    if (now < fall) (void)coco_run(m, (uint32_t)(fall - now));
    fs_catch_up(m, fall);
    update_irq(m);

    now = coco_ticks(m);
    if (now < end) (void)coco_run(m, (uint32_t)(end - now));
    c1_edge(m, 1, true, end);
    m->sound_fed = true;
    update_irq(m);

    m->field_start = end;
    m->fields++;
    return (uint32_t)(coco_ticks(m) - start);
}

void coco_key_set(coco_t *m, int row, int col, bool down) {
    if (row < 0 || row >= COCO_KEY_ROWS || col < 0 || col >= COCO_KEY_COLS) return;
    if (down) m->keys[col] |= (uint8_t)(1u << row);
    else      m->keys[col] &= (uint8_t)~(1u << row);
}

void coco_copy(coco_t *dst, const coco_t *src) {
    memcpy(dst, src, sizeof(*dst));
    map(dst);
}
