/* test_mc6821.c — the PIA as the generic part, its datasheet the test plan
 * (design.md §8.3, EL §4.2).
 *
 * Reset; register select and the DDR; the latch apart from the pins; C1's
 * edge, its polarity and the flag cleared by a data read and not by a
 * control read; the IRQ output raised by mc6821_raise after an enabled
 * edge (tRS3, timed by the machine), cleared by a data read, raised at
 * once by enabling with the flag set and lowered by disabling; C2 as an
 * input with its own flag, and as an output set by hand. Each rule beside
 * its control.
 */

#include "mc6821.h"
#include "test_util.h"

static mc6821_t p;

int main(void) {
    /* ---- reset: every register zero, the DDR selected ---------------- */
    mc6821_reset(&p);
    mc6821_write(&p, 0, 0x0F);          /* CRA bit 2 clear: this is DDRA */
    CHECK(p.a.ddr == 0x0F && p.a.out == 0x00, "a write after reset went to the data register");
    CHECK(mc6821_read(&p, 0) == 0x0F, "DDRA reads back as %02X", mc6821_read(&p, 0));

    /* ---- the latch apart from the pins ------------------------------- */
    mc6821_write(&p, 1, MC6821_CR_DATA);
    mc6821_write(&p, 0, 0xA5);
    p.a.in = 0x3C;
    CHECK(mc6821_read(&p, 0) == 0x35, "outputs from the latch, inputs from the pins: %02X, not 35",
          mc6821_read(&p, 0));
    CHECK(mc6821_out(&p.a) == 0xF5, "output pins %02X: the latch where output, high where input",
          mc6821_out(&p.a));

    /* ---- C1: the edge, its polarity, and the flag -------------------- */
    mc6821_reset(&p);
    mc6821_write(&p, 3, MC6821_CR_DATA);           /* falling edge, IRQ off */
    p.b.c1 = true;
    CHECK(!mc6821_set_c1(&p.b, false), "an edge with the interrupt off asked to raise IRQ");
    CHECK(p.b.cr & MC6821_CR_IRQ1, "a falling edge did not set the flag");
    CHECK(mc6821_read(&p, 3) & MC6821_CR_IRQ1, "reading CRB cleared the flag");
    (void)mc6821_read(&p, 2);
    CHECK(!(mc6821_read(&p, 3) & MC6821_CR_IRQ1), "reading the data register left the flag");
    mc6821_set_c1(&p.b, true);                     /* control: the rising edge */
    CHECK(!(p.b.cr & MC6821_CR_IRQ1), "a rising edge set a falling-edge flag");
    mc6821_write(&p, 3, MC6821_CR_DATA | MC6821_CR_C1_RISE);
    mc6821_set_c1(&p.b, false);
    mc6821_set_c1(&p.b, true);
    CHECK(p.b.cr & MC6821_CR_IRQ1, "a rising edge did not set a rising-edge flag");

    /* ---- the IRQ output ---------------------------------------------- */
    mc6821_reset(&p);
    mc6821_write(&p, 3, MC6821_CR_DATA | MC6821_CR_C1_IRQ);
    p.b.c1 = true;
    CHECK(mc6821_set_c1(&p.b, false), "an enabled edge did not ask to raise IRQ");
    CHECK(!mc6821_irq(&p.b), "IRQ rose at the edge, not tRS3 after it");
    mc6821_raise(&p.b);
    CHECK(mc6821_irq(&p.b), "IRQ not raised");
    (void)mc6821_read(&p, 2);
    CHECK(!mc6821_irq(&p.b), "a data read did not lower IRQ");

    mc6821_reset(&p);
    mc6821_write(&p, 3, MC6821_CR_DATA);           /* IRQ off */
    p.b.c1 = true;
    mc6821_set_c1(&p.b, false);
    CHECK(!mc6821_irq(&p.b), "IRQ with its interrupt disabled");
    mc6821_write(&p, 3, MC6821_CR_DATA | MC6821_CR_C1_IRQ);
    CHECK(mc6821_irq(&p.b), "enabling with the flag set did not raise IRQ");
    mc6821_write(&p, 3, MC6821_CR_DATA);
    CHECK(!mc6821_irq(&p.b), "disabling did not lower IRQ");
    CHECK(p.b.cr & MC6821_CR_IRQ1, "disabling cleared the flag");

    /* ---- C2 as an input ---------------------------------------------- */
    mc6821_reset(&p);
    mc6821_write(&p, 1, MC6821_CR_DATA | MC6821_CR_C2_B3);   /* C2 falling, enabled */
    p.a.c2 = true;
    CHECK(mc6821_set_c2(&p.a, false), "an enabled C2 edge did not ask to raise IRQ");
    CHECK(p.a.cr & MC6821_CR_IRQ2, "a C2 edge did not set its flag");
    CHECK(!(p.a.cr & MC6821_CR_IRQ1), "a C2 edge set C1's flag");
    mc6821_write(&p, 1, MC6821_CR_DATA | MC6821_CR_C2_OUT | MC6821_CR_C2_B4);
    CHECK(!(p.a.cr & MC6821_CR_IRQ2), "C2 made an output kept its flag");

    /* ---- C2 as an output set by hand --------------------------------- */
    mc6821_write(&p, 1, MC6821_CR_DATA | MC6821_CR_C2_OUT | MC6821_CR_C2_B4 | MC6821_CR_C2_B3);
    CHECK(mc6821_c2_out(&p.a), "C2 set high by hand reads low");
    mc6821_write(&p, 1, MC6821_CR_DATA | MC6821_CR_C2_OUT | MC6821_CR_C2_B4);
    CHECK(!mc6821_c2_out(&p.a), "C2 set low by hand reads high");

    TEST_DONE();
}
