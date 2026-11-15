/* test_machine.c — the machine without the real ROMs (design.md §8, §13).
 *
 * A test ROM of a dozen instructions stands in for BASIC, so this runs in
 * CI. It checks the SAM's bit pairs, the RAM through the SAM in each size,
 * the page table's slow path where the chips do not answer, the field
 * interrupt (FS on PIA0 CB1, sixty in sixty fields, with CB1 disabled as
 * the control), the line interrupt (HS on PIA0 CA1, 262 a field, the run
 * sliced at each line), the Dragon's PAL field (312 lines, the first 287)
 * and its PIA decode, the keyboard's rows and columns through PIA0, the
 * copy, and the run contract.
 */

#include <string.h>

#include "bus.h"
#include "coco.h"
#include "test_util.h"

static coco_t ma, mb;
static uint8_t rom[0x2000];

/* The test ROM at $A000: LDS, SAM to 64K mode, CRB and CRA from `crb` and
 * `cra`, interrupts on, wait; two handlers counting into $10-$11, one for
 * FS (reads $FF02) and one for HS (reads $FF00), the IRQ vector at one. */
static void build_rom(uint8_t crb, uint8_t cra, bool hs_handler) {
    static const uint8_t main_code[] = {
        0x10, 0xCE, 0x0F, 0x00,     /* $A000 LDS  #$0F00             */
        0xB7, 0xFF, 0xDD,           /* $A004 STA  $FFDD  (M1: 64K)   */
        0x86, 0x00,                 /* $A007 LDA  #crb               */
        0xB7, 0xFF, 0x03,           /* $A009 STA  $FF03              */
        0x86, 0x00,                 /* $A00C LDA  #cra               */
        0xB7, 0xFF, 0x01,           /* $A00E STA  $FF01              */
        0x1C, 0xEF,                 /* $A011 ANDCC #$EF              */
        0x20, 0xFE,                 /* $A013 BRA  *                  */
    };
    static const uint8_t count[] = {
        0xDC, 0x10,                 /* LDD  <$10                     */
        0xC3, 0x00, 0x01,           /* ADDD #1                       */
        0xDD, 0x10,                 /* STD  <$10                     */
        0x3B,                       /* RTI                           */
    };
    memset(rom, 0x12, sizeof rom);  /* NOPs */
    memcpy(rom, main_code, sizeof main_code);
    rom[0x08] = crb;
    rom[0x0D] = cra;
    /* $A020: FS's handler; $A030: HS's. */
    rom[0x20] = 0xB6; rom[0x21] = 0xFF; rom[0x22] = 0x02;
    memcpy(&rom[0x23], count, sizeof count);
    rom[0x30] = 0xB6; rom[0x31] = 0xFF; rom[0x32] = 0x00;
    memcpy(&rom[0x33], count, sizeof count);
    rom[0x1FF8] = 0xA0; rom[0x1FF9] = hs_handler ? 0x30 : 0x20;   /* IRQ   */
    rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;                       /* RESET */
}

static coco_machine_t which = COCO_MACHINE_COCO;

static void machine(coco_t *m, unsigned ram_k) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.machine = which;
    cfg.ram_k = ram_k;
    coco_init(m, &cfg);
    coco_load_rom(m, 0xA000, rom, sizeof rom);
    coco_power_on(m);
}

static unsigned counter(const coco_t *m) {
    return (unsigned)(m->ram[0x10] << 8 | m->ram[0x11]);
}

/* Interrupts counted over `fields`, after two to settle. */
static unsigned count_irqs(uint8_t crb, uint8_t cra, bool hs, int fields) {
    build_rom(crb, cra, hs);
    machine(&ma, 64);
    coco_run_field(&ma);
    coco_run_field(&ma);
    unsigned c0 = counter(&ma);
    for (int f = 0; f < fields; f++) coco_run_field(&ma);
    return counter(&ma) - c0;
}

int main(void) {
    /* ---- the SAM's bit pairs ----------------------------------------- */
    build_rom(0x04, 0x04, false);
    machine(&ma, 64);
    for (unsigned bit = 0; bit < 16; bit++) {
        uint16_t a = (uint16_t)(0xFFC0u + 2u * bit);
        uint16_t before = ma.sam.reg;
        bus_write(&ma, (uint16_t)(a + 1u), 0x00);
        CHECK(ma.sam.reg == (uint16_t)(before | (1u << bit)), "$%04X did not set bit %u", a + 1, bit);
        bus_write(&ma, a, 0xFF);
        CHECK(ma.sam.reg == before, "$%04X did not clear bit %u", a, bit);
    }

    /* ---- RAM through the SAM ----------------------------------------- */
    {
        machine(&ma, 64);
        bus_write(&ma, 0xFFDD, 0);                 /* M = 2: 64K */
        bool ident = true, paged = true;
        for (uint32_t a = 0; a < 0x8000u; a++) ident &= coco_ram_index(&ma, (uint16_t)a) == (int32_t)a;
        CHECK(ident, "64K mode, page 0: the lower 32K is not the chips' first half");
        bus_write(&ma, 0xFFD5, 0);                 /* P = 1 */
        for (uint32_t a = 0; a < 0x8000u; a++)
            paged &= coco_ram_index(&ma, (uint16_t)a) == (int32_t)(a | 0x8000u);
        CHECK(paged, "64K mode, page 1: the lower 32K is not the chips' second half");
        bus_write(&ma, 0xFFD4, 0);
        bus_write(&ma, 0xFFDF, 0);                 /* TY = 1 */
        CHECK(coco_ram_index(&ma, 0xC123) == 0xC123, "map type 1: $C123 is not RAM");
        CHECK(ma.page[0xC1].write != NULL, "map type 1: $C1xx does not write to RAM");
        bus_write(&ma, 0xFFDE, 0);
        CHECK(ma.page[0xC1].write == NULL && ma.page[0xA1].read == &ma.rom[0x2100],
              "map type 0: $A1xx is not the ROM");

        machine(&ma, 16);
        bus_write(&ma, 0xFFDB, 0);                 /* M = 1: 16K */
        bool flat = true;
        for (uint32_t a = 0; a < 0x4000u; a++) flat &= coco_ram_index(&ma, (uint16_t)a) == (int32_t)a;
        CHECK(flat, "16K: $0000-$3FFF is not the chips in order");
        CHECK(coco_ram_index(&ma, 0x4000) < 0, "16K: $4000 answered, with no second bank");
        CHECK(ma.page[0x00].read != NULL && ma.page[0x40].read == NULL,
              "16K: the page table does not send $40xx to the slow path");
        CHECK(bus_read(&ma, 0x4000) == 0xFF, "16K: no chip at $4000, read %02X", bus_read(&ma, 0x4000));
        bus_write(&ma, 0x4000, 0x12);              /* goes nowhere */
        CHECK(bus_read(&ma, 0x0000) == 0x00, "a write to $4000 reached $0000");

        machine(&ma, 32);
        bus_write(&ma, 0xFFDD, 0);
        bus_write(&ma, 0xFFDF, 0);
        CHECK(coco_ram_index(&ma, 0x8123) == coco_ram_index(&ma, 0x0123),
              "32K chips in map type 1: $8123 does not repeat $0123");
    }

    /* ---- the field interrupt ----------------------------------------- */
    {
        unsigned n = count_irqs(0x05, 0x04, false, 60);
        CHECK(n == 60, "FS interrupts in 60 fields: %u", n);
        unsigned none = count_irqs(0x04, 0x04, false, 60);
        CHECK(none == 0, "with CB1's interrupt disabled, %u interrupts", none);
    }

    /* ---- the line interrupt ------------------------------------------ */
    {
        unsigned n = count_irqs(0x04, 0x05, true, 10);
        CHECK(n == 2620, "HS interrupts in 10 fields: %u, not 262 a field", n);
        unsigned none = count_irqs(0x04, 0x04, true, 10);
        CHECK(none == 0, "with CA1's interrupt disabled, %u interrupts", none);
    }

    /* ---- the Dragon's PAL field --------------------------------------- */
    {
        which = COCO_MACHINE_DRAGON32;
        build_rom(0x05, 0x04, false);
        machine(&ma, 32);
        /* Ticks from power-on, the reset's own cycles among them. */
        (void)coco_run_field(&ma);
        uint64_t first = coco_ticks(&ma);
        uint32_t second = coco_run_field(&ma);
        CHECK(first >= 287u * 912u && first < 287u * 912u + 20u * 16u,
              "the Dragon's first field ended at %llu, not 287 lines in", (unsigned long long)first);
        CHECK(second > 312u * 912u - 20u * 16u && second < 312u * 912u + 20u * 16u,
              "the Dragon's field ran %u ticks, not 312 lines", second);
        unsigned n = count_irqs(0x05, 0x04, false, 50);
        CHECK(n == 50, "the Dragon's FS interrupts in 50 fields: %u", n);
        n = count_irqs(0x04, 0x05, true, 10);
        CHECK(n == 3120, "the Dragon's HS interrupts in 10 fields: %u, not 312 a field", n);
        which = COCO_MACHINE_COCO;
        build_rom(0x05, 0x04, false);
        machine(&ma, 64);
        (void)coco_run_field(&ma);
        first = coco_ticks(&ma);
        CHECK(first >= 262u * 912u && first < 262u * 912u + 20u * 16u,
              "the CoCo's first field ended at %llu, not 262 lines in", (unsigned long long)first);
    }

    /* ---- the Dragon's PIA decode -------------------------------------- */
    {
        which = COCO_MACHINE_DRAGON32;
        build_rom(0x04, 0x04, false);
        machine(&ma, 32);
        bus_write(&ma, 0xFF21, 0x34);
        CHECK(bus_read(&ma, 0xFF25) == 0x34, "an unexpanded Dragon 32's PIA1 does not repeat with A2 set");
        machine(&ma, 64);
        bus_write(&ma, 0xFF21, 0x34);
        CHECK(bus_read(&ma, 0xFF21) == 0x34 && bus_read(&ma, 0xFF25) != 0x34,
              "an expanded Dragon's PIA1 answered with A2 set");
        bus_write(&ma, 0xFF25, 0x00);
        CHECK(bus_read(&ma, 0xFF21) == 0x34, "a write with A2 set reached an expanded Dragon's PIA1");
        which = COCO_MACHINE_COCO;
    }

    /* ---- the keyboard through PIA0 ----------------------------------- */
    {
        build_rom(0x04, 0x04, false);
        machine(&ma, 64);
        bus_write(&ma, 0xFF03, 0x00);              /* DDRB */
        bus_write(&ma, 0xFF02, 0xFF);              /* columns out */
        bus_write(&ma, 0xFF03, 0x04);
        bus_write(&ma, 0xFF01, 0x04);
        coco_key_set(&ma, 2, 3, true);
        bus_write(&ma, 0xFF02, (uint8_t)~(1u << 3));
        CHECK(!(bus_read(&ma, 0xFF00) & (1u << 2)), "row 2 not low with its key down and column 3 low");
        bus_write(&ma, 0xFF02, (uint8_t)~(1u << 4));
        CHECK(bus_read(&ma, 0xFF00) & (1u << 2), "row 2 low with column 4 driven instead");
        bus_write(&ma, 0xFF02, (uint8_t)~(1u << 3));
        coco_key_set(&ma, 2, 3, false);
        CHECK((bus_read(&ma, 0xFF00) & 0x7F) == 0x7F, "a row low with no key down");
        CHECK(bus_read(&ma, 0xFF20 + 0x00) == bus_read(&ma, 0xFF24),
              "the CoCo's PIA1 does not repeat with A2 set");
    }

    /* ---- the copy ---------------------------------------------------- */
    {
        build_rom(0x05, 0x04, false);
        machine(&ma, 64);
        coco_run_field(&ma);
        coco_copy(&mb, &ma);
        bus_write(&mb, 0x0200, 0x5A);
        CHECK(ma.ram[0x0200] == 0x00 && mb.ram[0x0200] == 0x5A,
              "a write to the copy went to the original (the page table points into it)");
        for (int f = 0; f < 5; f++) { coco_run_field(&ma); coco_run_field(&mb); }
        CHECK(counter(&ma) == counter(&mb) && ma.cpu.cycles == mb.cpu.cycles,
              "the copy and the original parted: %u against %u", counter(&mb), counter(&ma));
    }

    /* ---- the run contract -------------------------------------------- */
    {
        build_rom(0x04, 0x04, false);
        machine(&ma, 64);
        uint64_t t0 = coco_ticks(&ma);
        uint32_t ran = coco_run(&ma, 1000);
        CHECK(ran >= 1000 && ran < 1000 + 20 * 16, "asked 1000 ticks, ran %u", ran);
        CHECK(coco_ticks(&ma) - t0 == ran, "the return is not the time that passed");
    }

    TEST_DONE();
}
