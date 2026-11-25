/* coco.c — the machine (design.md §6, §8, §13). See coco.h.
 *
 * The wiring here is §3's, and every line of it is a row of §18 until the
 * real ROMs, executed on the host, settle it. Where XRoar is the source
 * (D8), the comment says so.
 */

#include "coco.h"

#include <stddef.h>
#include <string.h>

#include "bus.h"
#include "hot.h"

/* ---- configuration ---------------------------------------------------- */

void coco_config_default(coco_config_t *cfg) {
    cfg->ram_k = 64;
    cfg->tape_trap = true;
    cfg->cart = CART_NONE;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) cfg->slot[s] = CART_NONE;
    cfg->mpi_switch = 3;   /* slot 4, where Disk BASIC is usually kept */
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
static int32_t ram_index(const coco_t *m, uint16_t a, bool video) {
    static const uint16_t row_mask[4]  = { 0x7Fu, 0x7Fu, 0xFFu, 0xFFu };
    static const uint8_t  col_shift[4] = { 6, 7, 8, 8 };
    static const uint16_t col_mask[4]  = { 0x3Fu, 0x7Fu, 0xFFu, 0xFFu };
    static const uint16_t ras1_bit[4]  = { 0x1000u, 0x4000u, 0u, 0u };

    unsigned msize = sam6883_m(&m->sam);
    unsigned ras1 = msize < 2 ? 0x80u : 0u;
    unsigned page = (!video && msize >= 2 && sam6883_p(&m->sam)) ? 0x80u : 0u;
    unsigned extra = (a & ras1_bit[msize]) ? 0u : ras1;
    unsigned row = (a & row_mask[msize]) | extra;
    unsigned col = ((a >> col_shift[msize]) & col_mask[msize]) | extra | page;
    unsigned bank = (!video && ras1 && (a & ras1_bit[msize])) ? 1u : 0u;
    if (bank >= m->ram_banks) return -1;

    unsigned r = m->ram_row_bits, c = (unsigned)m->ram_addr_bits - r;
    unsigned idx = (row & ((1u << r) - 1u)) | ((col & ((1u << c) - 1u)) << r);
    return (int32_t)((bank << m->ram_addr_bits) | idx);
}

int32_t COCO_HOT1(coco_ram_index)(const coco_t *m, uint16_t a) {
    return ram_index(m, a, false);
}

/* The VDG's fetch goes through the same multiplexer, but XRoar's takes
 * neither the page bit nor the second bank (its VRAM_TRANSLATE and the
 * fetch from bank 0; §18). */
int32_t COCO_HOT1(coco_vram_index)(const coco_t *m, uint16_t a) {
    return ram_index(m, a, true);
}

/* ---- the page table (§8.1) -------------------------------------------- */

/* Do the page at `base` (a multiple of 256) and its 256 bytes lie in order
 * in one bank? coco_ram_index is a bit map: each index bit is one address
 * bit or a constant, and the address bits that pick the bank and the
 * extra row and column bit are all above bit 7, so constant over a page.
 * So the page lies in order exactly when the base's index has its low
 * eight bits clear and each low address bit moves the index by itself
 * alone: nine translations, not 256. test_machine holds this to the byte
 * by byte check in every mode and size. */
static bool page_in_order(const coco_t *m, uint16_t base, int32_t *first) {
    int32_t i0 = coco_ram_index(m, base);
    if (i0 < 0 || (i0 & 0xFF)) return false;
    for (unsigned k = 0; k < 8u; k++)
        if (coco_ram_index(m, (uint16_t)(base | (1u << k))) != i0 + (int32_t)(1u << k))
            return false;
    *first = i0;
    return true;
}

/* Is Color BASIC's page of tape routines to be trapped (tape.h)? */
static bool tape_armed(const coco_t *m) {
    return m->cfg.tape_trap && m->tape.rom_ok && m->rom_present[1];
}

static const uint8_t *rom_page(const coco_t *m, unsigned p) {
    unsigned half = (p - 0x80u) / (COCO_ROM_HALF / COCO_PAGE_SIZE);
    if (p == TAPE_PAGE && m->tape.hot && tape_armed(m)) return NULL;
    return m->rom_present[half] ? &m->rom[(p - 0x80u) * COCO_PAGE_SIZE] : NULL;
}

void coco_tape_page(coco_t *m) {
    if (!sam6883_ty(&m->sam)) m->page[TAPE_PAGE].read = rom_page(m, TAPE_PAGE);
}

static void lines_due(coco_t *m, uint64_t now);
static void vid_protect(coco_t *m);

/* A RAM index the VDG may fetch from this field (§9.3). */
static inline bool vid_page(const coco_t *m, int32_t i) {
    unsigned p = (unsigned)i >> 8;
    return (m->vid_pages[p >> 5] >> (p & 31u)) & 1u;
}

/* Rebuilt when a bit that chooses the map changes: the SAM's M, P and TY,
 * a ROM loaded, and a field's start. A RAM page takes the fast path when
 * its 256 bytes lie in order in one chip bank, and its writes do unless
 * the VDG may fetch from it this field (§9.3); otherwise every byte is
 * translated on the slow path. The page of tape routines takes the slow path while a
 * routine of it is about to be trapped (tape.h). */
static void map(coco_t *m) {
    bool ty = sam6883_ty(&m->sam);
    for (unsigned p = 0; p < COCO_IO_PAGE; p++) {
        page_t *pg = &m->page[p];
        uint16_t base = (uint16_t)(p << 8);
        if (p < 0x80u || ty) {
            int32_t i0 = 0;
            bool flat = page_in_order(m, base, &i0);
            pg->read = flat ? &m->ram[i0] : NULL;
            pg->write = flat && !vid_page(m, i0) ? &m->ram[i0] : NULL;
        } else if (p < 0xC0u) {
            pg->read = rom_page(m, p);
            pg->write = NULL;
        } else {
            pg->read = cart_page(m, p);
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
    /* The slice was sized in cycles at the old rate: end it after this
     * instruction, so the run recomputes it in ticks (coco_run). */
    mc6809_stop(&m->cpu);
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
    if (mc6821_set_c1(pia_port(m, i), level) && !m->irq_due[i]) {
        m->irq_due[i] = t + TRS3_TICKS;
        if (m->irq_due[i] < m->due_first) m->due_first = m->irq_due[i];
        mc6809_stop(&m->cpu);   /* a new stop point for the slice (coco_run) */
    }
}

/* The first rise due, or UINT64_MAX: kept in due_first, which 0 says to
 * work out again, so every PIA access need not look at all four. */
static uint64_t next_due(coco_t *m) {
    if (m->due_first) return m->due_first;
    uint64_t d = UINT64_MAX;
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++)
        if (m->irq_due[i] && m->irq_due[i] < d) d = m->irq_due[i];
    m->due_first = d;
    return d;
}

static void raise_due(coco_t *m, uint64_t t) {
    if (t < next_due(m)) return;
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++)
        if (m->irq_due[i] && m->irq_due[i] <= t) {
            mc6821_raise_c1(pia_port(m, i));   /* only C1s are wired */
            m->irq_due[i] = 0;
        }
    m->due_first = 0;
}

/* A cartridge's CART line tied to the Q clock reaches PIA1 CB1 as an
 * edge every cycle (§12.2), so its flag is set again a cycle after
 * anything clears it: a read of port B's data, the RESET line, or a
 * change of CB1's polarity. XRoar toggles the line every 100 ms instead
 * (its cart.c), and BASIC, which waits for the flag, cannot tell. */
static void cart_q(coco_t *m, uint64_t t) {
    if (!cart_on_q(&m->port)) return;
    bool active = (m->pia1.b.cr & MC6821_CR_C1_RISE) != 0;
    c1_edge(m, 3, !active, t);
    c1_edge(m, 3, active, t);
}

/* HS, on PIA0 CA1, is brought up to date lazily (§8.3, EL §4.3): lines run
 * from tick 0, since fields follow one another whole, and HS is low for
 * the first COCO_HS_LOW_TICKS of each. */
_Static_assert(COCO_FIELD_TICKS % COCO_LINE_TICKS == 0, "a field is whole lines");

/* Where `t` falls in its line. Every field starts on a line's first tick,
 * so this field's ticks give it in 32 bits, which the M33 divides in
 * hardware; a 64-bit divide is libgcc's, hundreds of cycles, and HS is
 * brought up at every PIA access, a game's poll of the fire button
 * among them (design.md §7.5). A tick outside this field takes the
 * long way. */
static inline uint32_t line_tick(const coco_t *m, uint64_t t) {
    uint64_t d = t - m->field_start;
    if (t >= m->field_start && d <= UINT32_MAX) return (uint32_t)d % COCO_LINE_TICKS;
    return (uint32_t)(t % COCO_LINE_TICKS);
}

static bool hs_level(const coco_t *m, uint64_t t) {
    return line_tick(m, t) >= COCO_HS_LOW_TICKS;
}

/* The first HS edge after `t` of the kind CA1 is set to take. */
static uint64_t hs_next_edge(const coco_t *m, uint64_t t) {
    uint32_t off = (m->pia0.a.cr & MC6821_CR_C1_RISE) ? COCO_HS_LOW_TICKS : 0u;
    if (t < off) return off;
    /* ((t - off) / LINE + 1) * LINE + off, without the divide. */
    uint32_t in = line_tick(m, t);
    uint32_t past = in >= off ? in - off : in + COCO_LINE_TICKS - off;
    return t - past + COCO_LINE_TICKS;
}

static void COCO_HOT1(hs_catch_up)(coco_t *m, uint64_t t) {
    if (t <= m->hs_seen) return;
    /* HS holds its level until hs_change: nothing to bring up but the
     * tick, which a loop polling a PIA asks for at every access. */
    if (t < m->hs_change) {
        m->hs_seen = t;
        return;
    }
    uint64_t e = hs_next_edge(m, m->hs_seen);
    if (e <= t) {
        bool active = (m->pia0.a.cr & MC6821_CR_C1_RISE) != 0;
        c1_edge(m, 0, !active, e);
        c1_edge(m, 0, active, e);
    }
    c1_edge(m, 0, hs_level(m, t), t);
    m->hs_seen = t;
    uint32_t in = line_tick(m, t);
    m->hs_change = t - in + (in < COCO_HS_LOW_TICKS ? COCO_HS_LOW_TICKS : COCO_LINE_TICKS);
}

/* FS, on PIA0 CB1, falls as line COCO_FS_FALL_LINE begins (§13.1). The field
 * loop sets it once the run passes it; an access to a PIA within the
 * instruction that passes it brings it up first, so a read that clears the
 * flag sees the edge that came before it, as XRoar's trace has it. */
static void fs_catch_up(coco_t *m, uint64_t t) {
    uint64_t fall = m->field_start + (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
    uint64_t end = m->field_start + (uint64_t)COCO_FIELD_LINES * COCO_LINE_TICKS;
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
static void COCO_HOT1(before_pia)(coco_t *m) {
    uint64_t t = access_tick(m);
    if (m->cpu.io_at & MC6809_IO_LAST) {
        bring_up(m, t - m->tpc);
        update_irq(m);
        mc6809_lines_late(&m->cpu);
    }
    bring_up(m, t);
}

/* ---- the video bits (§9.4) -------------------------------------------- */

/* PIA1 PB3-PB7 drive CSS, GM0-GM2 and A/G (§3.3). An input nothing drives
 * reads low, as XRoar's PIA has it for the VDG too, so before BASIC sets
 * the port's direction the VDG is in its first alphanumeric mode. */
uint8_t COCO_HOT1(coco_vdg_mode)(const coco_t *m) {
    const mc6821_port_t *b = &m->pia1.b;
    return mc6847_mode_from_pb((uint8_t)(b->out & b->ddr));
}

static uint32_t video_bits(const coco_t *m) {
    return ((uint32_t)coco_vdg_mode(m) << 16) | (m->sam.reg & (SAM_V_MASK | SAM_F_MASK));
}

static void mode_written(coco_t *m, uint64_t t);

/* After a write that may change the video bits: if they changed while the
 * VDG was showing an active line, this field shows more than one mode,
 * and it is counted. A change of the VDG's mode or the SAM's is kept
 * with the line and the pixel it fell on, by the tick of the write
 * (§9.4): before the line's pixels, the line's first; after them, the
 * next line's. The display offset is loaded only as the field begins,
 * so a change of it alone is not kept. */
static void COCO_HOT1(note_video)(coco_t *m, uint64_t t) {
    uint32_t bits = video_bits(m);
    if (bits == m->video_bits) return;
    uint32_t changed = bits ^ m->video_bits;
    m->video_bits = bits;
    uint64_t in_field = t - m->field_start;
    uint64_t line = in_field / COCO_LINE_TICKS;
    if (line < COCO_ACTIVE_LINE || line >= COCO_ACTIVE_LINE + COCO_PIC_H) return;
    m->video_split = true;
    if (!(changed & ~(uint32_t)SAM_F_MASK)) return;

    unsigned y = (unsigned)(line - COCO_ACTIVE_LINE);
    unsigned off = (unsigned)(in_field % COCO_LINE_TICKS), x = 0;
    if (off > COCO_LINE_ACTIVE_TICKS) {
        x = (off - COCO_LINE_ACTIVE_TICKS + COCO_PIXEL_TICKS - 1u) / COCO_PIXEL_TICKS;
        if (x >= COCO_PIC_W) {
            x = 0;
            y++;
        }
    }
    if (y >= COCO_PIC_H) return;
    coco_split_t s = { (uint8_t)y, (uint8_t)x, coco_vdg_mode(m), (uint8_t)sam6883_v(&m->sam) };
    coco_split_t *last = m->n_split_now ? &m->split_now[m->n_split_now - 1u] : NULL;
    if (last && last->line == y && last->x == x) *last = s;
    else if (m->n_split_now < COCO_SPLIT_MAX) m->split_now[m->n_split_now++] = s;
    else m->split_dropped++;
}

/* ---- the PIAs' pins (§3.3) -------------------------------------------- */

/* A 32 or 64 KB CoCo wires PIA0 PB6 to PIA1 PB2 (XRoar; §18). */
static bool linked_pb(const coco_t *m) {
    return m->cfg.ram_k >= 32;
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
 * (XRoar; §18). */
static uint8_t pia1b_pins(const coco_t *m) {
    /* PB1, the single-bit sound, set as an input reads the sound bus
     * back, inverted (XRoar's single_bit_feedback): high while the bus is
     * under 0.3 of its full scale (§10). XRoar feeds it back only once
     * its sound has updated, and reads it low before then (§18). */
    uint8_t in = (m->sound_fed && m->sound_level < COCO_SOUND_PB1_LOW) ? 0x02u : 0x00u;
    if (m->cfg.ram_k == 16) in |= 0x04u;
    else if (linked_pb(m) && (mc6821_out(&m->pia0.b) & 0x40u)) in |= 0x04u;
    return in;
}

/* ---- the sound bus (§10) ---------------------------------------------- */

/* Where each control line stands, as XRoar's PIA has it: an output set by
 * hand drives its level; an input, or the pulse modes before their first
 * strobe, read high on the A side, which has pull-ups, and low on the B
 * side, which has none (its PIA_VALUE_CA2 and _CB2; §18). The pulse
 * modes' strobes are not modelled: no ROM uses them for sound. */
static bool ca2_level(const mc6821_port_t *a) {
    return (a->cr & MC6821_CR_C2_OUT) ? mc6821_c2_out(a) : true;
}

static bool cb2_level(const mc6821_port_t *b) {
    return (b->cr & MC6821_CR_C2_OUT) ? mc6821_c2_out(b) : false;
}

/* PIA0 port A: the keyboard's rows, low where a pressed key's column is
 * driven low, and PA7 the joystick comparator (§11.4): the axis PIA0's
 * CA2 chooses, of the stick its CB2 chooses, the multiplexer's two
 * select lines, against the DAC, high when the axis is at the DAC's
 * level or above. A stick's button pulls its row line low whatever the
 * columns: the right's PA0, the left's PA1. All as XRoar has them
 * (its dragon.c; §18). */
static uint8_t pia0a_pins(coco_t *m) {
    uint8_t cols_low = (uint8_t)~mc6821_out(&m->pia0.b);
    uint8_t rows = 0x7Fu;
    for (unsigned c = 0; c < COCO_KEY_COLS; c++)
        if (cols_low & (1u << c)) rows &= (uint8_t)~m->keys[c];
    rows &= (uint8_t)~(m->joy_buttons & 3u);
    unsigned stick = cb2_level(&m->pia0.b), axis = ca2_level(&m->pia0.a);
    uint8_t out = mc6821_out(&m->pia1.a);
    unsigned dac = (((unsigned)out & 0xFCu) | 2u) << 8;

#if COCO_SURVEY
    /* The survey's counts (coco.h), on the host alone: BASIC's prompt
     * reads this port all the time. */
    if ((out & 0xFCu) != m->joy_dac_read) {
        m->joy_dac_read = out & 0xFCu;
        m->joy_samples[stick]++;
    }
    if (cols_low && !(cols_low & (cols_low - 1u)) && (m->cpu.pc < 0x8000u || m->cpu.pc >= 0xC000u))
        m->key_col_reads[__builtin_ctz(cols_low)]++;
#endif

    return (uint8_t)(rows | (m->joy_axis[stick][axis] >= dac ? 0x80u : 0u));
}

/* The bus's level by source and by the single bit, XRoar's measured
 * voltages on a real Dragon in hundredths of a volt (its sound.c): index
 * 0 the single bit disabled, 1 enabled and low, 2 enabled and high. The
 * sources are PIA0 CB2 and CA2 as a number: the DAC, the tape, the
 * cartridge, and a fourth, which XRoar gives the cartridge's levels and
 * no input on a CoCo. The CoCo's are assumed the Dragon's (§18: low). */
enum { SOURCE_DAC, SOURCE_TAPE, SOURCE_CART, SOURCE_NONE };
static const uint16_t sound_gain[4][3] = {
    { 450u, 284u, 340u }, { 50u, 40u, 50u }, { 470u, 284u, 340u }, { 470u, 284u, 340u },
};
static const uint16_t sound_offset[4][3] = {
    { 20u, 18u, 130u }, { 205u, 160u, 235u }, { 0u, 18u, 130u }, { 0u, 18u, 130u },
};
/* The single bit alone, the multiplexer disabled: low, or high. */
static const uint16_t sbs_offset[2] = { 0u, 390u };

/* The level the bus takes from the pins now, to COCO_SOUND_LEVEL_MAX.
 * With neither the multiplexer nor the single bit driving it, it holds
 * the level it had (XRoar's bus_offset = bus_level). Each source's input
 * is in 126ths: the DAC's six bits doubled, the tape with no tape at its
 * middle, as XRoar's idle tape is 0.5, and nothing from the cartridge
 * until M9 has one. */
static uint32_t sound_level_now(const coco_t *m) {
    const mc6821_port_t *pb = &m->pia1.b;
    bool sbs = (pb->ddr & 0x02u) != 0;
    bool sbs_high = (pb->out & 0x02u) != 0;
    unsigned s = sbs ? (sbs_high ? 2u : 1u) : 0u;
    if (cb2_level(pb)) {
        unsigned src = (cb2_level(&m->pia0.b) ? 2u : 0u) | (ca2_level(&m->pia0.a) ? 1u : 0u);
        unsigned in = 0;
        if (src == SOURCE_DAC) in = ((unsigned)mc6821_out(&m->pia1.a) >> 2) * 2u;
        else if (src == SOURCE_TAPE) in = 63u;
        else if (src == SOURCE_CART && cart_ssc_live(&m->port)) {
            /* The Speech/Sound Cartridge's level, finer than the 126ths
             * (ssc.h). */
            return (uint32_t)((uint64_t)sound_gain[src][s] * 126u * m->port.ssc.level / SSC_LEVEL_MAX) +
                   (uint32_t)sound_offset[src][s] * 126u;
        }
        return (uint32_t)sound_gain[src][s] * in + (uint32_t)sound_offset[src][s] * 126u;
    }
    if (sbs) return (uint32_t)sbs_offset[sbs_high] * 126u;
    return m->sound_level;
}

/* What the PCM hears: the bus and, beside the multiplexer, the
 * cartridges' own sound (cart_beside, §12.10), to the full scale. */
static inline uint32_t sound_out(const coco_t *m, uint32_t bus) {
    uint32_t out = bus + cart_beside(m);
    return out > COCO_SOUND_LEVEL_MAX ? COCO_SOUND_LEVEL_MAX : out;
}

/* The bus moved to `level` at tick `at`, or the cartridges' sound beside
 * it did: the pcm's mono and side levels from there, if either changed. */
static inline void sound_stamp(coco_t *m, uint32_t at, uint32_t level) {
    uint32_t out = sound_out(m, level);
    int32_t side = cart_side(m);
    if (level == m->sound_level && out == m->sound_out && side == m->sound_side) return;
    if (out != m->sound_out) pcm_set_level(&m->pcm, at, out);
    if (side != m->sound_side) pcm_set_side(&m->pcm, at, side);
    m->sound_level = level;
    m->sound_out = out;
    m->sound_side = side;
    m->sound_edges++;
}

/* After a write that can move the bus: the level, stamped with the tick
 * at the start of the writing instruction (EL §6.1). XRoar's sound
 * updates, and feeds PB1 back, at each FS rise and when the single bit
 * or the multiplexer is first enabled (its sound.c, dragon.c; §18). */
static void COCO_HOT1(update_sound)(coco_t *m) {
    const mc6821_port_t *pb = &m->pia1.b;
    if ((pb->ddr & 0x02u) || cb2_level(pb)) m->sound_fed = true;
    sound_stamp(m, (uint32_t)coco_ticks(m), sound_level_now(m));
}

/* Whether the bus hears the cartridge now: the multiplexer enabled and
 * set to it. Only then must the Speech/Sound Cartridge be brought up to
 * a tick before something else moves the bus; otherwise its levels move
 * nothing, and it runs on in long stretches, cheaply (ssc.c). */
static inline bool cart_heard(const coco_t *m) {
    return cart_ssc_live(&m->port) && cb2_level(&m->pia1.b) && cb2_level(&m->pia0.b) &&
           !ca2_level(&m->pia0.a);
}

/* The Speech/Sound Cartridge's level moved at tick `at` (ssc.h): the bus
 * from there, if the multiplexer has it. The cartridge is brought up to
 * a tick before anything else moves the bus (cart_ssc_run), so `at` is
 * never behind the pcm. */
void ssc_level_changed(ssc_t *s, uint64_t at) {
    coco_t *m = (coco_t *)((char *)s - offsetof(cart_port_t, ssc) - offsetof(coco_t, port));
    sound_stamp(m, (uint32_t)at, sound_level_now(m));
}

void coco_audio_set_rate(coco_t *m, uint32_t rate_num, uint32_t rate_den) {
    uint32_t now = (uint32_t)coco_ticks(m);
    pcm_advance(&m->pcm, now);
    /* What is waiting and the DC blocker stay as they are, so the change
     * makes no step. */
    pcm_set_rate(&m->pcm, now, COCO_SOUND_LEVEL_MAX, COCO_TICK_HZ, rate_num, rate_den);
    if (cart_ssc_live(&m->port))
        ay8912_set_average(&m->port.ssc.ay, m->pcm.num, m->pcm.den * SSC_TICKS_PER_AY);
}

size_t coco_audio_drain(coco_t *m, int16_t *dst, size_t max) {
    return pcm_drain(&m->pcm, dst, max);
}

size_t coco_audio_drain_stereo(coco_t *m, int16_t *dst, size_t max) {
    return pcm_drain_stereo(&m->pcm, dst, max);
}

/* The cassette's relay, PIA1 CA2 driven high (§12.3): an input, as
 * after RESET, leaves it open. */
static bool motor_on(const coco_t *m) {
    const mc6821_port_t *a = &m->pia1.a;
    return (a->cr & MC6821_CR_C2_OUT) && mc6821_c2_out(a);
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
    return m->rom_present[1] ? m->rom[COCO_ROM_SIZE - 1u] : 0xFFu;
}

static uint8_t io_read(coco_t *m, uint16_t a) {
    unsigned off = a & 0xFFu;
    unsigned reg = a & 3u;
    if (off < 0x20u) {
        before_pia(m);
        if (reg == 0u) m->pia0.a.in = pia0a_pins(m);
        if (reg == 2u) m->pia0.b.in = pia0b_pins(m);
        uint8_t v = mc6821_read(&m->pia0, reg);
        update_irq(m);
        return v;
    }
    if (off < 0x40u) {
        before_pia(m);
        if (reg == 2u && cart_heard(m)) cart_ssc_run(m, access_tick(m));   /* PB1 */
        if (reg == 2u) m->pia1.b.in = pia1b_pins(m);
        /* PA0 is the cassette's comparator, low with no tape (XRoar; the
         * trace agrees, §18), and the signal's level while one plays
         * (cassette.h). */
        m->pia1.a.in = (m->cas.playing || m->cas.ended) && cassette_level(m, access_tick(m))
                           ? 0xFFu : 0xFEu;
        uint8_t v = mc6821_read(&m->pia1, reg);
        if (reg == 2u) cart_q(m, access_tick(m) + m->tpc);
        update_irq(m);
        return v;
    }
    if (off >= 0xE0u)   /* the vectors, from the BASIC ROM whatever the map */
        return m->rom_present[1] ? m->rom[COCO_ROM_SIZE - 0x20u + (off - 0xE0u)] : open_bus(m);
    /* The cartridge port (§12.8); what it does not answer is unused. */
    uint8_t bus = open_bus(m);
    if (off < 0xC0u) bus = cart_io_read(m, a, bus, access_tick(m));
    return bus;
}

static void io_write(coco_t *m, uint16_t a, uint8_t v) {
    unsigned off = a & 0xFFu;
    unsigned reg = a & 3u;
    if (off < 0x20u) {
        before_pia(m);
        /* CA2 and CB2 choose the multiplexer's source. */
        if (cart_heard(m) || (cart_ssc_live(&m->port) && (reg & 1u))) cart_ssc_run(m, access_tick(m));
        mc6821_write(&m->pia0, reg, v);
        if (reg & 1u) update_sound(m);   /* CA2 and CB2 choose the source */
        update_irq(m);
        if (reg & 1u) mc6809_stop(&m->cpu);   /* CA1's enable moves the slice's end */
        return;
    }
    if (off < 0x40u) {
        before_pia(m);
        /* CB2 enables the multiplexer. */
        if (cart_heard(m) || (cart_ssc_live(&m->port) && reg == 3u)) cart_ssc_run(m, access_tick(m));
        lines_due(m, access_tick(m));   /* in the mode they had (§9.3) */
        mc6821_write(&m->pia1, reg, v);
        if (reg == 3u) cart_q(m, access_tick(m) + m->tpc);
        if (reg == 1u) cassette_motor(m, motor_on(m), access_tick(m));
        else if (reg == 0u && !m->cas.fast)
            cassette_dac(m, mc6821_out(&m->pia1.a) & 0xFCu, access_tick(m));
        update_sound(m);
        note_video(m, access_tick(m) + COCO_PIA_VIDEO_TICKS);
        mode_written(m, access_tick(m));
        update_irq(m);
        if (reg & 1u) mc6809_stop(&m->cpu);
        return;
    }
    if (off < 0xC0u) {   /* the cartridge port (§12.8) */
        if (cart_io_write(m, a, v, access_tick(m))) update_sound(m);
        return;
    }
    if (off >= 0xC0u && off < 0xE0u) {
        lines_due(m, access_tick(m));   /* with the V they had (§9.3) */
        uint16_t changed = sam6883_write(&m->sam, a);
        if (changed & SAM_M_MASK) vid_protect(m);   /* the chips' organisation */
        if (changed & SAM_MAP_BITS) map(m);
        if (changed & SAM_R_MASK) retime(m);
        if (changed & (SAM_V_MASK | SAM_F_MASK)) note_video(m, access_tick(m));
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
    if ((a >> 8) == TAPE_PAGE && tape_armed(m)) return tape_page_read(m, a);
    return open_bus(m);   /* an empty ROM socket or cartridge slot */
}

void COCO_HOT1(coco_slow_write)(coco_t *m, uint16_t a, uint8_t v) {
    /* A page of RAM the VDG may fetch from this field, the only RAM page
     * that reads fast and writes here (map): the lines shown before the
     * write, as they were (§9.3), then the byte. */
    /* As integers: r may be NULL or point into ROM, and a relational
     * compare of unrelated pointers is undefined in C. */
    const uint8_t *r = m->page[a >> 8].read;
    uintptr_t off = (uintptr_t)r - (uintptr_t)m->ram;
    if (off < COCO_ADDR_SPACE) {
        if (m->vid_line < COCO_PIC_H) lines_due(m, access_tick(m));
        m->ram[off + (a & 0xFFu)] = v;
        return;
    }
    if ((a >> 8) == COCO_IO_PAGE) {
        io_write(m, a, v);
        return;
    }
    if (ram_space(m, a)) {
        int32_t i = coco_ram_index(m, a);
        if (i >= 0) {
            /* A page out of order in the chips: the lines shown before
             * the write, as they were (§9.3). */
            if (m->vid_line < COCO_PIC_H && vid_page(m, i)) lines_due(m, access_tick(m));
            m->ram[i] = v;
        }
    }
    /* A write to ROM goes nowhere. */
}

/* ---- the machine ------------------------------------------------------ */

/* Both sticks let go: centred, as XRoar reads a port with nothing in it,
 * and no button. */
static void joysticks_centre(coco_t *m) {
    for (unsigned s = 0; s < COCO_JOYSTICKS; s++)
        m->joy_axis[s][0] = m->joy_axis[s][1] = COCO_JOY_CENTRE;
    m->joy_buttons = 0;
}

/* The pool of cartridge ROMs is the struct's last member, and coco_init
 * leaves it: the port reads the slots' ROMs into it before a power-on
 * and registers them after (coco_slot_rom_placed), with no second copy
 * (design.md §12.8). */
_Static_assert(offsetof(coco_t, cart) + sizeof(((coco_t *)0)->cart) == sizeof(coco_t),
               "the ROM pool is coco_t's last member");

void coco_init(coco_t *m, const coco_config_t *cfg) {
    memset(m, 0, offsetof(coco_t, cart));
    m->cfg = *cfg;
    set_ram_org(m);

    m->tpc = 16u;
    m->cas.fast = true;
    joysticks_centre(m);
    mc6809_init(&m->cpu);
    cart_init(m);
    pcm_init(&m->pcm, 0, COCO_SOUND_LEVEL_MAX, COCO_TICK_HZ, 0, 0);
    m->vid_bytes_mode = 0x01u;   /* no mode yet (lines_due) */
    map(m);
}

bool coco_load_rom(coco_t *m, uint16_t base, const uint8_t *data, size_t len) {
    if ((base == 0x8000u || base == 0xA000u) && len == COCO_ROM_HALF) {
        unsigned half = base == 0xA000u;
        memcpy(&m->rom[half * COCO_ROM_HALF], data, len);
        m->rom_present[half] = true;
    } else if (base == 0xC000u) {
        if (!cart_load_rom(m, 0, data, len)) return false;
    } else {
        return false;
    }
    if (m->rom_present[1]) tape_rom_loaded(m, &m->rom[COCO_ROM_HALF], COCO_ROM_HALF);
    map(m);
    return true;
}

bool coco_load_slot_rom(coco_t *m, unsigned s, const uint8_t *data, size_t len) {
    if (!cart_load_rom(m, s, data, len)) return false;
    map(m);
    return true;
}

bool coco_slot_rom_placed(coco_t *m, unsigned s, uint32_t at, size_t len) {
    if (!cart_rom_placed(m, s, at, len)) return false;
    map(m);
    return true;
}

bool coco_load_ssc_roms(coco_t *m, const uint8_t *rom, const uint8_t *al2) {
    return cart_load_ssc(m, rom, al2);
}

bool coco_ssc_roms_placed(coco_t *m, uint32_t rom_at, uint32_t al2_at) {
    return cart_ssc_placed(m, rom_at, al2_at);
}

bool coco_serial_put(coco_t *m, uint8_t c) {
    uint64_t now = coco_ticks(m);
    bool ok = cart_serial_put(m, c, now);
    coco_cart_line(m, now);
    return ok;
}

int coco_serial_get(coco_t *m) {
    return cart_serial_get(m);
}

const acia_t *coco_serial(const coco_t *m) {
    return cart_acia(&m->port);
}

void coco_cart_remove(coco_t *m) {
    cart_remove_rom(m, 0);
    map(m);
}

void coco_cart_autostart(coco_t *m, bool on) {
    m->port.slot[0].autostart = on;
}

void coco_cart_selected(coco_t *m) {
    map(m);
    cart_q(m, coco_ticks(m) + m->tpc);
    coco_cart_line(m, coco_ticks(m));
}

/* A 6551's /IRQ on the CART line (cart.h): CB1 follows it, so its
 * falling edge sets the flag a polarity of 0 selects, which NitrOS-9's
 * sc6551 enables for FIRQ (§12.11). */
void coco_cart_line(coco_t *m, uint64_t now) {
    bool low = cart_line_low(&m->port);
    if (low == m->port.cart_low) return;
    m->port.cart_low = low;
    c1_edge(m, 3, !low, now);
}

void coco_reset(coco_t *m) {
    lines_due(m, coco_ticks(m));   /* in the state they had (§9.3) */
    cart_reset_select(m);
    mc6821_reset(&m->pia0);
    mc6821_reset(&m->pia1);
    sam6883_reset(&m->sam);
    tape_reset(m);
    /* M is cleared, which reorganises the chips: the pages the VDG may
     * fetch for the rest of the field, from the base FS's rise loaded. */
    vid_protect(m);
    map(m);
    retime(m);
    uint64_t t = coco_ticks(m);
    cassette_motor(m, false, t);
    memset(m->irq_due, 0, sizeof(m->irq_due));
    m->due_first = 0;
    m->hs_seen = t;
    m->hs_change = 0;
    m->pia0.a.c1 = hs_level(m, t);
    m->pia0.b.c1 = t - m->field_start < (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
    cart_q(m, t + m->tpc);
    update_irq(m);
    update_sound(m);   /* the bus holds what it had: nothing drives it */
    mode_written(m, t);   /* the PIA's outputs go to inputs: mode $00 */
    mc6809_reset(m);
    cart_reset(m);
}

static void lines_begin(coco_t *m);

void coco_power_on(coco_t *m) {
    memset(m->ram, 0, sizeof(m->ram));   /* §13.3 */
    memset(m->keys, 0, sizeof(m->keys));
    joysticks_centre(m);
    memset(m->joy_samples, 0, sizeof m->joy_samples);
    memset(m->key_col_reads, 0, sizeof m->key_col_reads);
    mc6809_init(&m->cpu);
    m->tick0 = m->cyc0 = 0;
    m->field_start = 0;
    m->fields = 0;
    m->sound_fed = false;
    m->sound_level = 0;   /* XRoar's bus starts at 0 */
    m->sound_out = sound_out(m, 0);
    m->sound_side = cart_side(m);
    m->sound_edges = 0;
    /* The tick count starts again, and the next sample with it; the
     * drives fall silent, their motors stopped. */
    pcm_restart(&m->pcm, 0);
    pcm_set_level(&m->pcm, 0, m->sound_out);
    pcm_set_side(&m->pcm, 0, m->sound_side);
    drivesnd_motor(&m->pcm.drive, false);
    drivesnd_quiet(&m->pcm.drive);
    m->video_split = false;
    m->split_fields = 0;
    m->n_split_now = m->n_split_last = 0;
    m->split_dropped = 0;
    coco_reset(m);
    m->video_bits = video_bits(m);
    m->vid_shown = coco_vdg_mode(m);
    m->vid_next_at = UINT64_MAX;
    vid_protect(m);
    map(m);
    lines_begin(m);
}

/* ---- the lines as fetched (§9.3) ------------------------------------- */

/* One line's bytes through the SAM's counter: HS clears its low bits,
 * then the VDG fetches through the right border, 42 bytes a line in its
 * 32-byte modes and 22 in its 16-byte ones, of which the picture shows
 * the first 32 or 16 (XRoar's VDG, §18: medium). The dividers are wired
 * for V as the line begins. */
static void COCO_HOT1(fetch_bytes)(const coco_t *m, sam6883_vc_t *vc, unsigned shown, unsigned v,
                                   uint8_t *dst) {
    unsigned fetched = shown == 32u ? 42u : 22u;
    vc->v = (uint8_t)v;
    sam6883_vc_hsync(vc);
    unsigned got = 0;
    while (got < fetched) {
        uint16_t a;
        unsigned n = sam6883_vc_fetch(vc, fetched - got, &a);
        if (got < shown) {
            unsigned k = n < shown - got ? n : shown - got;
            /* Sixteen bytes from a 16-byte boundary lie in order in the
             * chips: the low four address bits are row bits. */
            int32_t i = coco_vram_index(m, a);
            if (i < 0) memset(dst + got, 0xFF, k);
            else       memcpy(dst + got, &m->ram[i], k);
        }
        got += n;
    }
    if (shown < COCO_VDG_LINE_BYTES) memset(dst + shown, 0, COCO_VDG_LINE_BYTES - shown);
}

/* The tick line y of this field has its first byte fetched. */
static inline uint64_t line_fetch_tick(const coco_t *m, unsigned y) {
    return m->field_start + (uint64_t)(COCO_ACTIVE_LINE + y) * COCO_LINE_TICKS +
           COCO_LINE_ACTIVE_TICKS;
}

/* The VDG's mode the picture shows at tick t: a PIA1 write's, from
 * COCO_PIA_VIDEO_TICKS after it (§9.4). */
static uint8_t shown_mode(coco_t *m, uint64_t t) {
    if (m->vid_next_at <= t) {
        m->vid_shown = m->vid_next;
        m->vid_next_at = UINT64_MAX;
    }
    return m->vid_shown;
}

/* A PIA1 write at tick t that may change the VDG's mode. */
static void mode_written(coco_t *m, uint64_t t) {
    uint8_t mode = coco_vdg_mode(m);
    uint64_t at = t + COCO_PIA_VIDEO_TICKS;
    (void)shown_mode(m, at);
    if (mode == (m->vid_next_at == UINT64_MAX ? m->vid_shown : m->vid_next)) return;
    m->vid_next = mode;
    m->vid_next_at = at;
}

/* FS rises: the counter is loaded from F, and the field's lines are
 * fetched from its first. */
/* The RAM pages the VDG may fetch from, from vid_base: the counter only
 * counts up from it, and 192 lines of at most 42 bytes, 8,064, lie
 * within 8 KB of it, wrapping at 64 KB. Sixteen bytes from a 16-byte
 * boundary lie in one place in the chips. The caller remaps. */
static void vid_protect(coco_t *m) {
    m->vid_prot_base = m->vid_base;
    m->vid_prot_m = (uint16_t)(m->sam.reg & SAM_M_MASK);
    memset(m->vid_pages, 0, sizeof m->vid_pages);
    for (unsigned k = 0; k < 8192u; k += 16u) {
        int32_t i = coco_vram_index(m, (uint16_t)(m->vid_base + k));
        if (i < 0) continue;
        unsigned p = (unsigned)i >> 8;
        m->vid_pages[p >> 5] |= 1u << (p & 31u);
    }
}

static void lines_begin(coco_t *m) {
    m->vid_bytes_mode = 0x01u;   /* no mode: its low three bits are 0 */
    sam6883_vc_load(&m->vid_vc, &m->sam);
    m->vid_base = sam6883_display_base(&m->sam);
    m->vid_line = 0;
    m->vid_whole = true;
    /* The page table again only when the pages change: rebuilding it is
     * 2,300 translations, 4 % of core 0 at 60 fields a second. */
    if (m->vid_base != m->vid_prot_base || (m->sam.reg & SAM_M_MASK) != m->vid_prot_m) {
        vid_protect(m);
        map(m);
    }
}

/* The lines whose first byte is due by now, each whole at that moment:
 * taken a byte at a time at each byte's tick instead, Dragonfire's room
 * was drawn the same in 76 fields of 76 (docs/milestones.md). Called at
 * each slice's boundary, and before anything that would change what a
 * line due by then fetched: a write to RAM the VDG may fetch, and a
 * write to PIA1 or the SAM, each at its own cycle. So the run need not
 * stop at each line. */
static void COCO_HOT1(lines_due)(coco_t *m, uint64_t now) {
    while (m->vid_line < COCO_PIC_H && line_fetch_tick(m, m->vid_line) <= now) {
        unsigned y = m->vid_line;
        uint8_t mode = shown_mode(m, line_fetch_tick(m, y));
        unsigned v = sam6883_v(&m->sam);
        m->vid_mode[y] = mode;
        m->vid_v[y] = (uint8_t)v;
        if (mode != m->vid_bytes_mode) {   /* the table is in flash */
            m->vid_bytes_mode = mode;
            m->vid_bytes = (uint8_t)mc6847_mode_info(mode)->bytes_per_line;
        }
        fetch_bytes(m, &m->vid_vc, m->vid_bytes, v, &m->vid_lines[y * COCO_VDG_LINE_BYTES]);
        m->vid_line++;
    }
}

/* A slice ends at the target, or a cycle after a PIA's interrupt output
 * is due to rise, or at HS's edge while its interrupt is enabled, which
 * BASIC leaves disabled (§8.3); and early, after the instruction that
 * changes one of those (mc6809_stop: a PIA control write, a new rise
 * due, the SAM's rate). After each, a rise is seen if it came
 * before the last instruction's last cycle, as the CPU latches its lines
 * (mc6809.h). */
uint32_t COCO_HOT2(coco_run)(coco_t *m, uint32_t ticks) {
    uint64_t start = coco_ticks(m), target = start + ticks;
    for (;;) {
        m->slices++;
        uint64_t now = coco_ticks(m);
        /* A rise already due when the run starts, or after an edge set
         * between runs, is seen at this boundary. */
        if (now >= m->tpc) raise_due(m, now - m->tpc);
        update_irq(m);
        lines_due(m, now);
        if (now >= target) break;
        uint64_t stop = target;
        if (m->pia0.a.cr & MC6821_CR_C1_IRQ) {
            uint64_t e = hs_next_edge(m, now);
            if (e < stop) stop = e;
        }
        uint64_t due = next_due(m);
        if (due != UINT64_MAX && due + m->tpc < stop) stop = due + m->tpc;
        /* The cartridge's next event (cart.h): for the disk controller,
         * a byte ready, which ends a HALT, or INTRQ, which is the NMI
         * (rsdos.h). */
        uint64_t cart_at = cart_due(&m->port);
        if (cart_at <= now) {
            cart_run(m, now);
            continue;
        }
        if (cart_at < stop) stop = cart_at;
        if (stop <= now) stop = now + 1u;
        /* Within the run's ticks, so 32 bits, divided in hardware. */
        uint32_t left = (uint32_t)(stop - now);
        uint32_t cycles = left / m->tpc + (left % m->tpc != 0u);
        if (__builtin_expect(m->step_hook != NULL, 0)) {
            (void)mc6809_run(m, 1u);
            m->step_hook(m);
        } else {
            (void)mc6809_run(m, cycles);
        }
        hs_catch_up(m, coco_ticks(m));
        if (cart_due(&m->port) <= coco_ticks(m)) cart_run(m, coco_ticks(m));
    }
    return (uint32_t)(coco_ticks(m) - start);
}

/* A field's run ends where FS falls, as the line after its 192 active
 * ones begins: every line has been fetched as it was shown (§9.3), and
 * the snapshot is of the field just shown. The next run begins with the
 * blanking, FS low, until it rises as the next field begins and the
 * SAM's counter is loaded; then that field's active lines. FS falls at
 * the first boundary at or after its line, or sooner, by an access to a
 * PIA. A run from a field's start (a power-on, a state restored before
 * its fall) goes to that field's fall. */
void COCO_HOT2(coco_run_blanking)(coco_t *m) {
    uint64_t fall = m->field_start + (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
    uint64_t end = m->field_start + (uint64_t)COCO_FIELD_LINES * COCO_LINE_TICKS;
    uint64_t now = coco_ticks(m);
    if (now < fall) return;
    fs_catch_up(m, now);
    update_irq(m);
    if (now < end) (void)coco_run(m, (uint32_t)(end - now));
    c1_edge(m, 1, true, end);
    m->sound_fed = true;
    update_irq(m);

    m->field_start = end;
    m->fields++;
    if (m->video_split) m->split_fields++;
    m->video_split = false;
    lines_begin(m);
}

uint32_t COCO_HOT2(coco_run_field)(coco_t *m) {
    uint64_t start = coco_ticks(m);
    coco_run_blanking(m);
    uint64_t fall = m->field_start + (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
    uint64_t now = coco_ticks(m);
    if (now < fall) (void)coco_run(m, (uint32_t)(fall - now));
    fs_catch_up(m, fall);
    update_irq(m);

    /* The changes this field's active lines had, which its snapshot is
     * drawn by (§9.4). */
    memcpy(m->split_last, m->split_now, m->n_split_now * sizeof m->split_now[0]);
    m->n_split_last = m->n_split_now;
    m->n_split_now = 0;
    /* The Speech/Sound Cartridge up to here, once a field unless the bus
     * hears it sooner (ssc.h); then the samples, for the port to drain
     * with the snapshot. */
    if (cart_ssc_live(&m->port)) cart_ssc_run(m, coco_ticks(m));
    pcm_advance(&m->pcm, (uint32_t)coco_ticks(m));
    return (uint32_t)(coco_ticks(m) - start);
}

/* ---- the snapshot (§9.3) ---------------------------------------------- */

/* The field's lines as they were fetched (§9.3), or RAM as it is. In
 * matched modes the bytes fetched through the right border change
 * nothing; in some mismatched ones they move the X divider (fetch_bytes).
 * In SRAM from tier 1 with the counter it walks: from flash it cost core
 * 0 4.8 % at the idle prompt once it drew each line's mode (§9.4;
 * docs/milestones.md, M12). */
void COCO_HOT1(coco_video_snapshot)(const coco_t *m, coco_frame_t *out) {
    uint8_t mode = coco_vdg_mode(m);
    unsigned v = sam6883_v(&m->sam);

    out->mode = mode;
    out->sam_v = (uint8_t)v;
    out->base = sam6883_display_base(&m->sam);
    out->field = m->fields;
    out->splits = m->n_split_last;
    const coco_split_t *s = m->split_last, *end = s + m->n_split_last;

    /* The field fetched from its first line (§9.3): each line's bytes as
     * the VDG fetched them, in the mode it began, and the changes along
     * it from pixel x (§9.4). */
    if (m->vid_whole && m->vid_line == COCO_PIC_H) {
        out->mode = m->vid_mode[0];
        out->sam_v = m->vid_v[0];
        out->base = m->vid_base;
        memcpy(out->lines, m->vid_lines, sizeof out->lines);
        for (unsigned y = 0; y < COCO_PIC_H; y++) {
            coco_line_t *how = &out->how[y];
            *how = (coco_line_t){ .mode = m->vid_mode[y] };
            for (; s < end && s->line < y; s++) {}
            for (unsigned k = 0; s < end && s->line == y; s++) {
                if (s->x == 0) continue;   /* the line's mode as it began */
                /* More than a line has room for: the last takes the newest. */
                if (k == COCO_LINE_SPLITS) k--;
                else how->x[k] = s->x;
                how->m[k++] = s->mode;
            }
        }
        return;
    }

    /* Otherwise, after a power-on or a restored state, RAM as it is, each
     * line fetched by the VDG's mode as it began, with the SAM's dividers
     * chosen by V as it was then, the counter going on from where it
     * stood; a change along the line is drawn from its pixel, from the
     * bytes the line fetched, and its V is the next line's. */
    sam6883_vc_t vc;
    sam6883_vc_load(&vc, &m->sam);
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        coco_line_t *how = &out->how[y];
        for (; s < end && s->line == y && s->x == 0; s++) {
            mode = s->mode;
            v = s->sam_v;
        }
        *how = (coco_line_t){ .mode = mode };
        unsigned line_v = v;
        uint8_t line_mode = mode;
        for (unsigned k = 0; s < end && s->line == y; s++) {
            if (k == COCO_LINE_SPLITS) k--;
            else how->x[k] = s->x;
            how->m[k++] = mode = s->mode;
            v = s->sam_v;
        }
        fetch_bytes(m, &vc, mc6847_mode_info(line_mode)->bytes_per_line, line_v,
                    &out->lines[y * COCO_VDG_LINE_BYTES]);
    }
}

void coco_frame_plain(coco_frame_t *f) {
    f->splits = 0;
    for (unsigned y = 0; y < COCO_PIC_H; y++) f->how[y] = (coco_line_t){ .mode = f->mode };
}

uint8_t coco_frame_border_mode(const coco_frame_t *f) {
    /* Lines beginning in each mode, by its five bits. */
    uint8_t lines[32] = { 0 };
    for (unsigned y = 0; y < COCO_PIC_H; y++) lines[f->how[y].mode >> 3]++;
    unsigned best = f->how[0].mode >> 3;
    for (unsigned y = 1; y < COCO_PIC_H; y++)
        if (lines[f->how[y].mode >> 3] > lines[best]) best = f->how[y].mode >> 3;
    return (uint8_t)(best << 3);
}

/* ---- a state restored (snapshot.h, §12.5) ------------------------------ */

/* What a state does not carry because it follows from what it does: the
 * page table from the SAM, the ROMs and the trap's page; the CPU's lines
 * from the PIAs' outputs, and with them its attention (mc6809_set_irq
 * works it out from everything). Not state at all: the
 * keys, released, and the sound's sample grid, started again at the
 * restored tick with the bus's level (EL §8.5). */
void coco_restored(coco_t *m) {
    m->tape.op = TAPE_NONE;
    memset(m->keys, 0, sizeof(m->keys));
    joysticks_centre(m);
    m->n_split_now = m->n_split_last = 0;   /* a field brings them back (§9.4) */
    /* The lines already shown this field are not in the state: this
     * field's snapshot walks RAM, and the next is fetched (§9.3). */
    m->vid_whole = false;
    m->vid_base = sam6883_display_base(&m->sam);   /* as FS's rise loaded it */
    vid_protect(m);
    m->vid_shown = coco_vdg_mode(m);
    m->vid_next_at = UINT64_MAX;
    m->vid_line = 0;
    while (m->vid_line < COCO_PIC_H && line_fetch_tick(m, m->vid_line) <= coco_ticks(m))
        m->vid_line++;
    /* The signal from a byte's start, the relay as the PIA has it. */
    m->cas.motor = false;
    cassette_moved(m);
    cassette_motor(m, motor_on(m), coco_ticks(m));
    map(m);
    m->cpu.irq = m->cpu.firq = 0;
    update_irq(m);
    cart_restored(m);
    uint32_t now = (uint32_t)coco_ticks(m);
    coco_disk_restored(m);
    drivesnd_quiet(&m->pcm.drive);
    pcm_restart(&m->pcm, now);
    m->sound_out = sound_out(m, m->sound_level);
    m->sound_side = cart_side(m);
    pcm_set_level(&m->pcm, now, m->sound_out);
    pcm_set_side(&m->pcm, now, m->sound_side);
}

void coco_key_set(coco_t *m, int row, int col, bool down) {
    if (row < 0 || row >= COCO_KEY_ROWS || col < 0 || col >= COCO_KEY_COLS) return;
    if (down) m->keys[col] |= (uint8_t)(1u << row);
    else      m->keys[col] &= (uint8_t)~(1u << row);
}

void coco_joystick_set(coco_t *m, unsigned stick, int x, int y, bool fire) {
    if (stick >= COCO_JOYSTICKS) return;
    m->joy_axis[stick][0] = x < 0 ? 0u : x > 0 ? 65535u : COCO_JOY_CENTRE;
    m->joy_axis[stick][1] = y < 0 ? 0u : y > 0 ? 65535u : COCO_JOY_CENTRE;
    if (fire) m->joy_buttons |= (uint8_t)(1u << stick);
    else      m->joy_buttons &= (uint8_t)~(1u << stick);
}

/* JOYSTK reads place p when the axis is at or above the DAC's level for
 * p, (p << 10) | 512 (pia0a_pins), and below p + 1's: p << 10 | 1023 is
 * the middle of that step. */
_Static_assert(COCO_JOY_CENTRE == ((31u << 10) | 1023u), "the centre is place 31's middle");
_Static_assert(COCO_JOY_PLACES == 64u, "JOYSTK's six bits");

void coco_joystick_put(coco_t *m, unsigned stick, unsigned axis, unsigned place) {
    if (stick >= COCO_JOYSTICKS || axis > 1u) return;
    if (place >= COCO_JOY_PLACES) place = COCO_JOY_PLACES - 1u;
    m->joy_axis[stick][axis] = (uint16_t)((place << 10) | 1023u);
}

unsigned coco_joystick_place(const coco_t *m, unsigned stick, unsigned axis) {
    if (stick >= COCO_JOYSTICKS || axis > 1u) return 0;
    return m->joy_axis[stick][axis] >> 10;
}

void coco_copy(coco_t *dst, const coco_t *src) {
    memcpy(dst, src, sizeof(*dst));
    map(dst);
}
