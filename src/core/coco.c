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
    cfg->ram_k = 64;
    cfg->tape_trap = true;
    cfg->rsdos = false;
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

int32_t coco_ram_index(const coco_t *m, uint16_t a) {
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

/* Rebuilt when a bit that chooses the map changes: the SAM's M, P and TY,
 * and a ROM loaded. A RAM page takes the fast path when its 256 bytes lie
 * in order in one chip bank; otherwise every byte is translated on the
 * slow path. The page of tape routines takes the slow path while a
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
            pg->write = flat ? &m->ram[i0] : NULL;
        } else if (p < 0xC0u) {
            pg->read = rom_page(m, p);
            pg->write = NULL;
        } else {
            pg->read = m->cart_present ? &m->cart[(p - 0xC0u) * COCO_PAGE_SIZE] : NULL;
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
        mc6809_stop(&m->cpu);   /* a new stop point for the slice (coco_run) */
    }
}

static void raise_due(coco_t *m, uint64_t t) {
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++)
        if (m->irq_due[i] && m->irq_due[i] <= t) {
            mc6821_raise_c1(pia_port(m, i));   /* only C1s are wired */
            m->irq_due[i] = 0;
        }
}

static uint64_t next_due(const coco_t *m) {
    uint64_t d = UINT64_MAX;
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++)
        if (m->irq_due[i] && m->irq_due[i] < d) d = m->irq_due[i];
    return d;
}

/* A cartridge's CART line tied to the Q clock reaches PIA1 CB1 as an
 * edge every cycle (§12.2), so its flag is set again a cycle after
 * anything clears it: a read of port B's data, the RESET line, or a
 * change of CB1's polarity. XRoar toggles the line every 100 ms instead
 * (its cart.c), and BASIC, which waits for the flag, cannot tell. */
static void cart_q(coco_t *m, uint64_t t) {
    if (!m->cart_present || !m->cart_autostart) return;
    bool active = (m->pia1.b.cr & MC6821_CR_C1_RISE) != 0;
    c1_edge(m, 3, !active, t);
    c1_edge(m, 3, active, t);
}

/* HS, on PIA0 CA1, is brought up to date lazily (§8.3, EL §4.3): lines run
 * from tick 0, since fields follow one another whole, and HS is low for
 * the first COCO_HS_LOW_TICKS of each. */
static bool hs_level(uint64_t t) {
    return (t % COCO_LINE_TICKS) >= COCO_HS_LOW_TICKS;
}

/* The first HS edge after `t` of the kind CA1 is set to take. */
static uint64_t hs_next_edge(const coco_t *m, uint64_t t) {
    uint64_t off = (m->pia0.a.cr & MC6821_CR_C1_RISE) ? COCO_HS_LOW_TICKS : 0u;
    if (t < off) return off;
    return ((t - off) / COCO_LINE_TICKS + 1u) * COCO_LINE_TICKS + off;
}

static void hs_catch_up(coco_t *m, uint64_t t) {
    if (t <= m->hs_seen) return;
    uint64_t e = hs_next_edge(m, m->hs_seen);
    if (e <= t) {
        bool active = (m->pia0.a.cr & MC6821_CR_C1_RISE) != 0;
        c1_edge(m, 0, !active, e);
        c1_edge(m, 0, active, e);
    }
    c1_edge(m, 0, hs_level(t), t);
    m->hs_seen = t;
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
static void before_pia(coco_t *m) {
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
uint8_t coco_vdg_mode(const coco_t *m) {
    const mc6821_port_t *b = &m->pia1.b;
    return mc6847_mode_from_pb((uint8_t)(b->out & b->ddr));
}

static uint32_t video_bits(const coco_t *m) {
    return ((uint32_t)coco_vdg_mode(m) << 16) | (m->sam.reg & (SAM_V_MASK | SAM_F_MASK));
}

/* After a write that may change the video bits: if they changed while the
 * VDG was showing an active line, this field shows more than one mode,
 * and it is counted. A change of the VDG's mode or the SAM's is kept
 * with the line and the pixel it fell on, by the tick of the write
 * (§9.4): before the line's pixels, the line's first; after them, the
 * next line's. The display offset is loaded only as the field begins,
 * so a change of it alone is not kept. */
static void note_video(coco_t *m, uint64_t t) {
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
        return (uint32_t)sound_gain[src][s] * in + (uint32_t)sound_offset[src][s] * 126u;
    }
    if (sbs) return (uint32_t)sbs_offset[sbs_high] * 126u;
    return m->sound_level;
}

/* After a write that can move the bus: the level, stamped with the tick
 * at the start of the writing instruction (EL §6.1). XRoar's sound
 * updates, and feeds PB1 back, at each FS rise and when the single bit
 * or the multiplexer is first enabled (its sound.c, dragon.c; §18). */
static void COCO_HOT1(update_sound)(coco_t *m) {
    const mc6821_port_t *pb = &m->pia1.b;
    if ((pb->ddr & 0x02u) || cb2_level(pb)) m->sound_fed = true;
    uint32_t level = sound_level_now(m);
    if (level == m->sound_level) return;
    pcm_set_level(&m->pcm, (uint32_t)coco_ticks(m), level);
    m->sound_level = level;
    m->sound_edges++;
}

void coco_audio_set_rate(coco_t *m, uint32_t rate_num, uint32_t rate_den) {
    uint32_t now = (uint32_t)coco_ticks(m);
    pcm_advance(&m->pcm, now);
    /* What is waiting and the DC blocker stay as they are, so the change
     * makes no step. */
    pcm_set_rate(&m->pcm, now, COCO_SOUND_LEVEL_MAX, COCO_TICK_HZ, rate_num, rate_den);
}

size_t coco_audio_drain(coco_t *m, int16_t *dst, size_t max) {
    return pcm_drain(&m->pcm, dst, max);
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
    if (m->cfg.rsdos && rsdos_decodes(off)) return rsdos_read(m, a, open_bus(m), access_tick(m));
    if (m->clock.fitted && (off & 0xF8u) == (SMARTWATCH_BASE & 0xF8u))
        return smartwatch_read(&m->clock, a, open_bus(m));
    return open_bus(m);  /* the rest unused */
}

static void io_write(coco_t *m, uint16_t a, uint8_t v) {
    unsigned off = a & 0xFFu;
    unsigned reg = a & 3u;
    if (off < 0x20u) {
        before_pia(m);
        mc6821_write(&m->pia0, reg, v);
        if (reg & 1u) update_sound(m);   /* CA2 and CB2 choose the source */
        update_irq(m);
        if (reg & 1u) mc6809_stop(&m->cpu);   /* CA1's enable moves the slice's end */
        return;
    }
    if (off < 0x40u) {
        before_pia(m);
        mc6821_write(&m->pia1, reg, v);
        if (reg == 3u) cart_q(m, access_tick(m) + m->tpc);
        if (reg == 1u) cassette_motor(m, motor_on(m), access_tick(m));
        else if (reg == 0u && !m->cas.fast)
            cassette_dac(m, mc6821_out(&m->pia1.a) & 0xFCu, access_tick(m));
        update_sound(m);
        note_video(m, access_tick(m) + COCO_PIA_VIDEO_TICKS);
        update_irq(m);
        if (reg & 1u) mc6809_stop(&m->cpu);
        return;
    }
    if (m->cfg.rsdos && rsdos_decodes(off)) {
        rsdos_write(m, a, v, access_tick(m));
        return;
    }
    if (off >= 0xC0u && off < 0xE0u) {
        uint16_t changed = sam6883_write(&m->sam, a);
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

/* Both sticks let go: centred, as XRoar reads a port with nothing in it,
 * and no button. */
static void joysticks_centre(coco_t *m) {
    for (unsigned s = 0; s < COCO_JOYSTICKS; s++)
        m->joy_axis[s][0] = m->joy_axis[s][1] = COCO_JOY_CENTRE;
    m->joy_buttons = 0;
}

void coco_init(coco_t *m, const coco_config_t *cfg) {
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    set_ram_org(m);

    m->tpc = 16u;
    m->cas.fast = true;
    joysticks_centre(m);
    mc6809_init(&m->cpu);
    wd_init(&m->dk.fdc);
    pcm_init(&m->pcm, 0, COCO_SOUND_LEVEL_MAX, COCO_TICK_HZ, 0, 0);
    map(m);
}

bool coco_load_rom(coco_t *m, uint16_t base, const uint8_t *data, size_t len) {
    if ((base == 0x8000u || base == 0xA000u) && len == COCO_ROM_HALF) {
        unsigned half = base == 0xA000u;
        memcpy(&m->rom[half * COCO_ROM_HALF], data, len);
        m->rom_present[half] = true;
    } else if (base == 0xC000u && len > 0 && len <= COCO_CART_SIZE) {
        /* A ROM smaller than the window ignores the address lines above
         * it, so a power-of-two image repeats through the window, as
         * XRoar's ROM bank does; any other size is followed by $FF. */
        memset(m->cart, 0xFF, sizeof(m->cart));
        memcpy(m->cart, data, len);
        if ((len & (len - 1u)) == 0)
            for (size_t at = len; at < COCO_CART_SIZE; at += len) memcpy(&m->cart[at], data, len);
        m->cart_present = true;
        m->cart_autostart = true;
    } else {
        return false;
    }
    if (m->rom_present[1]) tape_rom_loaded(m, &m->rom[COCO_ROM_HALF], COCO_ROM_HALF);
    map(m);
    return true;
}

void coco_cart_remove(coco_t *m) {
    memset(m->cart, 0xFF, sizeof(m->cart));
    m->cart_present = false;
    m->cart_autostart = false;
    map(m);
}

void coco_cart_autostart(coco_t *m, bool on) {
    m->cart_autostart = on;
}

void coco_reset(coco_t *m) {
    mc6821_reset(&m->pia0);
    mc6821_reset(&m->pia1);
    sam6883_reset(&m->sam);
    tape_reset(m);
    map(m);
    retime(m);
    uint64_t t = coco_ticks(m);
    cassette_motor(m, false, t);
    memset(m->irq_due, 0, sizeof(m->irq_due));
    m->hs_seen = t;
    m->pia0.a.c1 = hs_level(t);
    m->pia0.b.c1 = t - m->field_start < (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
    cart_q(m, t + m->tpc);
    update_irq(m);
    update_sound(m);   /* the bus holds what it had: nothing drives it */
    mc6809_reset(m);
    if (m->cfg.rsdos) rsdos_reset(m);
    else {
        mc6809_set_halt(&m->cpu, false);
        mc6809_set_nmi(&m->cpu, false);
    }
}

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
    m->sound_edges = 0;
    /* The tick count starts again, and the next sample with it. */
    pcm_restart(&m->pcm, 0);
    pcm_set_level(&m->pcm, 0, 0);
    m->video_split = false;
    m->split_fields = 0;
    m->n_split_now = m->n_split_last = 0;
    m->split_dropped = 0;
    coco_reset(m);
    m->video_bits = video_bits(m);
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
        /* The disk controller's next event: a byte ready, which ends a
         * HALT, or INTRQ, which is the NMI (rsdos.h). */
        if (m->cfg.rsdos) {
            if (m->dk.fdc.due <= now) {
                rsdos_run(m, now);
                continue;
            }
            if (m->dk.fdc.due < stop) stop = m->dk.fdc.due;
        }
        if (stop <= now) stop = now + 1u;
        uint32_t cycles = (uint32_t)((stop - now + m->tpc - 1u) / m->tpc);
        if (__builtin_expect(m->step_hook != NULL, 0)) {
            (void)mc6809_run(m, 1u);
            m->step_hook(m);
        } else {
            (void)mc6809_run(m, cycles);
        }
        hs_catch_up(m, coco_ticks(m));
        if (m->cfg.rsdos && m->dk.fdc.due <= coco_ticks(m)) rsdos_run(m, coco_ticks(m));
    }
    return (uint32_t)(coco_ticks(m) - start);
}

/* FS falls at its line, set at the first boundary at or after it (or
 * sooner, by an access to a PIA), and rises as the next field begins; the
 * run goes on to that field's first active line, where the snapshot is
 * taken (EL §5.6). */
uint32_t COCO_HOT2(coco_run_field)(coco_t *m) {
    uint64_t start = coco_ticks(m);
    uint64_t fall = m->field_start + (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
    uint64_t end = m->field_start + (uint64_t)COCO_FIELD_LINES * COCO_LINE_TICKS;

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
    if (m->video_split) m->split_fields++;
    m->video_split = false;
    /* The changes the field just shown had are the coming one's (§9.4). */
    memcpy(m->split_last, m->split_now, m->n_split_now * sizeof m->split_now[0]);
    m->n_split_last = m->n_split_now;
    m->n_split_now = 0;

    uint64_t active = end + (uint64_t)COCO_ACTIVE_LINE * COCO_LINE_TICKS;
    now = coco_ticks(m);
    if (now < active) (void)coco_run(m, (uint32_t)(active - now));
    /* The samples up to here, for the port to drain with the snapshot. */
    pcm_advance(&m->pcm, (uint32_t)coco_ticks(m));
    return (uint32_t)(coco_ticks(m) - start);
}

/* ---- the snapshot (§9.3) ---------------------------------------------- */

/* The VDG fetches through the right border as well, 42 bytes a line in
 * its 32-byte modes and 22 in its 16-byte ones, and the SAM's counter
 * counts them all; the picture shows the first 32 or 16. XRoar's VDG
 * (§18: medium). In matched modes the extra bytes change nothing; in
 * some mismatched ones they move the X divider. Only the active lines
 * fetch, and HS clears part of the counter before each. */
/* In SRAM from tier 1 with the counter it walks: from flash it cost core
 * 0 4.8 % at the idle prompt once it drew each line's mode (§9.4;
 * docs/milestones.md, M12). */
void COCO_HOT1(coco_video_snapshot)(const coco_t *m, coco_frame_t *out) {
    uint8_t mode = coco_vdg_mode(m);
    unsigned v = sam6883_v(&m->sam);

    out->mode = mode;
    out->sam_v = (uint8_t)v;
    out->base = sam6883_display_base(&m->sam);
    out->field = m->fields;
    /* The last field's changes (§9.4): a title that splits the screen
     * does it every field, at the same places. A line is fetched by the
     * VDG's mode as it begins, with the SAM's dividers chosen by V as it
     * was then, the counter going on from where it stood; a change along
     * the line is drawn from its pixel, from the bytes the line fetched,
     * and its V is the next line's. */
    out->splits = m->n_split_last;
    const coco_split_t *s = m->split_last, *end = s + m->n_split_last;

    /* The bytes a line fetches, worked out when its mode is a new one. */
    uint8_t fetch_mode = mode;
    unsigned shown = mc6847_mode_info(mode)->bytes_per_line;
    unsigned fetched = shown == 32u ? 42u : 22u;

    sam6883_vc_t vc;
    sam6883_vc_load(&vc, &m->sam);
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        uint8_t *dst = &out->lines[y * COCO_VDG_LINE_BYTES];
        coco_line_t *how = &out->how[y];
        for (; s < end && s->line == y && s->x == 0; s++) {
            mode = s->mode;
            v = s->sam_v;
        }
        *how = (coco_line_t){ .mode = mode };
        vc.v = (uint8_t)v;
        if (mode != fetch_mode) {
            fetch_mode = mode;
            shown = mc6847_mode_info(mode)->bytes_per_line;
            fetched = shown == 32u ? 42u : 22u;
        }
        for (unsigned k = 0; s < end && s->line == y; s++) {
            /* More than a line has room for: the last takes the newest. */
            if (k == COCO_LINE_SPLITS) k--;
            else how->x[k] = s->x;
            how->m[k++] = mode = s->mode;
            v = s->sam_v;
        }
        sam6883_vc_hsync(&vc);
        unsigned got = 0;
        while (got < fetched) {
            uint16_t a;
            unsigned n = sam6883_vc_fetch(&vc, fetched - got, &a);
            if (got < shown) {
                unsigned k = n < shown - got ? n : shown - got;
                /* Sixteen bytes from a 16-byte boundary lie in order in
                 * the chips: the low four address bits are row bits. */
                int32_t i = coco_vram_index(m, a);
                if (i < 0) memset(dst + got, 0xFF, k);
                else       memcpy(dst + got, &m->ram[i], k);
            }
            got += n;
        }
        if (shown < COCO_VDG_LINE_BYTES) memset(dst + shown, 0, COCO_VDG_LINE_BYTES - shown);
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
    /* The signal from a byte's start, the relay as the PIA has it. */
    m->cas.motor = false;
    cassette_moved(m);
    cassette_motor(m, motor_on(m), coco_ticks(m));
    map(m);
    m->cpu.irq = m->cpu.firq = 0;
    update_irq(m);
    if (m->cfg.rsdos) rsdos_lines(m);
    uint32_t now = (uint32_t)coco_ticks(m);
    pcm_restart(&m->pcm, now);
    pcm_set_level(&m->pcm, now, m->sound_level);
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

void coco_copy(coco_t *dst, const coco_t *src) {
    memcpy(dst, src, sizeof(*dst));
    map(dst);
}
