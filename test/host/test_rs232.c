/* test_rs232.c — the Deluxe RS-232 Program Pak in a CoCo (cart.h,
 * acia6551.h, design.md §12.11): its 6551 at $FF68-$FF6B, its /IRQ on
 * the CART line to PIA1 CB1 and so to FIRQ, as NitrOS-9's sc6551 takes
 * it, and the port's rings. With a ROM of the test's own at $A000, no
 * ROM from the card:
 *
 *   reset   the SAM to 64 KB; the 6551 at 9600 baud, 8N1, DTR on and its receiver's
 *           interrupt; PIA1 CB1's interrupt enabled on a falling edge;
 *           FIRQ unmasked; then a loop
 *   FIRQ    the status read (IRQ cleared), and with RDRF the character
 *           read, sent back and counted at $0400; PIA1 port B read (its
 *           flag cleared); RTI
 *
 * Checked: every character from the line comes back, in order, each by
 * the FIRQ, no faster than the line's rate; the pak in a Multi-Pak slot
 * other than CTS raises nothing, as the Multi-Pak takes CART from the
 * CTS slot alone, and its interrupt arrives once $FF7F selects it; a
 * save state taken with characters in flight runs on alike in another
 * machine, and one taken without a pak will not load into one with it;
 * RESET clears the chip. With the pak's ROM in the ROM directory
 * (rs232.rom), it is known by its SHA-1 and its slot becomes the pak;
 * and with Extended Color BASIC, its program, EXEC &HC000, shows its
 * menu, and its terminal at 300 baud shows a line from the host and
 * sends the keys typed.
 *
 * Planted one at a time (2026-11-25), each failed this test: CART taken
 * from the pak in any slot, not the CTS slot alone (test_os9's controls
 * fail too); the receive ring left out of the save state; and RESET not
 * reaching the 6551.
 */
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "coco.h"
#include "guest.h"
#include "snap_util.h"
#include "test_util.h"

static coco_t s_m, s_n;
static uint8_t s_rom[0x2000];
static mem_t s_snap;

static void rom_put(uint16_t at, const uint8_t *b, size_t n) {
    memcpy(&s_rom[at - 0xA000u], b, n);
}

static void make_rom(void) {
    static const uint8_t reset[] = {
        0xB7, 0xFF, 0xDD,             /* STA  $FFDD             SAM: 64 KB */
        0x10, 0xCE, 0x7F, 0x00,       /* LDS  #$7F00                       */
        0x86, 0x1E, 0xB7, 0xFF, 0x6B, /* LDA  #$1E; STA $FF6B   9600 8N1    */
        0x86, 0x09, 0xB7, 0xFF, 0x6A, /* LDA  #$09; STA $FF6A   DTR, RX IRQ */
        0x86, 0x05, 0xB7, 0xFF, 0x23, /* LDA  #$05; STA $FF23   CB1 IRQ, fall */
        0x1C, 0xAF,                   /* ANDCC #$AF                        */
        0x20, 0xFE,                   /* BRA  *                            */
    };
    static const uint8_t firq[] = {
        0x34, 0x02,                   /* PSHS A                            */
        0xB6, 0xFF, 0x69,             /* LDA  $FF69                        */
        0x85, 0x08,                   /* BITA #$08                         */
        0x27, 0x09,                   /* BEQ  done                         */
        0xB6, 0xFF, 0x68,             /* LDA  $FF68                        */
        0xB7, 0xFF, 0x68,             /* STA  $FF68                        */
        0x7C, 0x04, 0x00,             /* INC  $0400                        */
        0xB6, 0xFF, 0x22,             /* done: LDA $FF22                   */
        0x35, 0x02,                   /* PULS A                            */
        0x3B,                         /* RTI                               */
    };
    memset(s_rom, 0x12, sizeof s_rom);   /* NOPs */
    rom_put(0xA000, reset, sizeof reset);
    rom_put(0xA020, firq, sizeof firq);
    static const uint8_t vec_firq[] = { 0xA0, 0x20 }, vec_reset[] = { 0xA0, 0x00 };
    rom_put(0xBFF6, vec_firq, 2);
    rom_put(0xBFFE, vec_reset, 2);
}

/* A CoCo with the pak alone in the port, or in slot `pak` of a Multi-Pak
 * whose switch is at `sw`. */
static void build(coco_t *m, bool mpi, unsigned pak, unsigned sw) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    if (mpi) {
        cfg.cart = CART_MPI;
        cfg.mpi_switch = (uint8_t)sw;
        cfg.slot[pak] = CART_RS232;
    } else {
        cfg.cart = CART_RS232;
    }
    coco_init(m, &cfg);
    coco_load_rom(m, 0xA000u, s_rom, sizeof s_rom);
    coco_power_on(m);
}

static void fields(coco_t *m, int n) {
    for (int i = 0; i < n; i++) (void)coco_run_field(m);
}

static int take(coco_t *m, uint8_t *out, int max) {
    int n = 0, c;
    while (n < max && (c = coco_serial_get(m)) >= 0) out[n++] = (uint8_t)c;
    return n;
}

static const char k_msg[] = "THE QUICK BROWN FOX, 0123456789!";

static int test_echo(void) {
    build(&s_m, false, 0, 0);
    fields(&s_m, 2);
    CHECK(coco_serial(&s_m) != NULL, "no 6551");
    CHECK(coco_serial(&s_m)->command == 0x09 && coco_serial(&s_m)->control == 0x1E,
          "the program did not set the 6551 up: %02X %02X", coco_serial(&s_m)->command,
          coco_serial(&s_m)->control);
    CHECK(s_m.ram[0x400] == 0, "a FIRQ before a character");
    size_t n = strlen(k_msg);
    uint64_t t0 = coco_ticks(&s_m);
    for (size_t i = 0; i < n; i++) CHECK(coco_serial_put(&s_m, (uint8_t)k_msg[i]), "put %zu", i);
    /* 32 characters at 9600 baud, ten bits each: 33 ms, two fields. */
    uint64_t need = (uint64_t)n * 10u * COCO_TICK_HZ / 9600u;
    uint8_t got[64];
    int k = 0;
    while (coco_ticks(&s_m) - t0 < need - COCO_FIELD_TICKS / 2u) {
        (void)coco_run(&s_m, COCO_FIELD_TICKS / 4u);
        k += take(&s_m, got + k, (int)sizeof got - k);
    }
    CHECK(k < (int)n, "%d characters back in %llu ticks, faster than the line", k,
          (unsigned long long)(coco_ticks(&s_m) - t0));
    fields(&s_m, 4);
    k += take(&s_m, got + k, (int)sizeof got - k);
    CHECK(k == (int)n && memcmp(got, k_msg, n) == 0, "%d characters back: %.*s", k, k, (const char *)got);
    CHECK(s_m.ram[0x400] == n, "%u FIRQs counted, not %zu", s_m.ram[0x400], n);
    CHECK(!s_m.port.cart_low && !acia_irq(coco_serial(&s_m)), "CART left low");
    return 0;
}

static int test_mpi_cts(void) {
    /* The pak in slot 2 (index 1), the switch at slot 1: its interrupt
     * does not reach CB1. */
    build(&s_m, true, 1, 0);
    fields(&s_m, 2);
    coco_serial_put(&s_m, 'Z');
    fields(&s_m, 2);
    CHECK(s_m.ram[0x400] == 0, "a FIRQ from a slot other than CTS");
    CHECK(coco_serial(&s_m)->status & ACIA_SR_RDRF, "the character not received");
    CHECK(acia_irq(coco_serial(&s_m)) && !s_m.port.cart_low, "the pak's IRQ: %d, CART %d",
          acia_irq(coco_serial(&s_m)), s_m.port.cart_low);
    /* $FF7F's CTS to slot 2: its line, low, reaches CB1 as an edge. */
    bus_write(&s_m, 0xFF7F, 0x11);
    fields(&s_m, 1);
    CHECK(s_m.ram[0x400] == 1, "no FIRQ once the pak was selected: %u", s_m.ram[0x400]);
    CHECK(coco_serial_get(&s_m) == 'Z', "the character not echoed");
    /* And with the switch at its slot from the start. */
    build(&s_m, true, 1, 1);
    fields(&s_m, 2);
    coco_serial_put(&s_m, 'Y');
    fields(&s_m, 2);
    CHECK(s_m.ram[0x400] == 1 && coco_serial_get(&s_m) == 'Y', "the switch's slot: no echo");
    return 0;
}

static int test_snapshot(void) {
    build(&s_m, false, 0, 0);
    fields(&s_m, 2);
    for (size_t i = 0; i < strlen(k_msg); i++) coco_serial_put(&s_m, (uint8_t)k_msg[i]);
    (void)coco_run_field(&s_m);   /* half the characters in flight */
    CHECK(s_m.port.acia.rx.head != s_m.port.acia.rx.tail, "nothing left in flight");
    CHECK(mem_save(&s_snap, &s_m, NULL) == SNAP_OK, "save");
    coco_t *saved = &s_n;
    coco_copy(saved, &s_m);
    /* Into a machine doing something else: the pak's, run further. */
    build(&s_m, false, 0, 0);
    fields(&s_m, 9);
    coco_serial_put(&s_m, '#');
    CHECK(mem_check(&s_snap, &s_m, NULL, NULL) == SNAP_OK, "check");
    CHECK(mem_load(&s_snap, &s_m) == SNAP_OK, "load");
    CHECK(snap_same(saved, &s_m, "straight after"), "not the saved machine");
    fields(saved, 3);
    fields(&s_m, 3);
    CHECK(snap_same(saved, &s_m, "three fields on"), "the two parted");
    uint8_t a[64], b[64];
    int na = take(saved, a, 64), nb = take(&s_m, b, 64);
    CHECK(na == nb && memcmp(a, b, (size_t)na) == 0, "the characters sent after differ");
    /* A state with no pak will not load into a machine with one, nor the
     * reverse. */
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&s_n, &cfg);
    coco_load_rom(&s_n, 0xA000u, s_rom, sizeof s_rom);
    coco_power_on(&s_n);
    fields(&s_n, 2);
    CHECK(mem_check(&s_snap, &s_n, NULL, NULL) == SNAP_OTHER_MACHINE, "the pak's state into a CoCo without one");
    CHECK(mem_save(&s_snap, &s_n, NULL) == SNAP_OK, "save without a pak");
    CHECK(mem_check(&s_snap, &s_m, NULL, NULL) == SNAP_OTHER_MACHINE, "a state without a pak into one with it");
    /* An event far from the state's time is refused: the 6551's due
     * moved an hour on. */
    CHECK(mem_save(&s_snap, &s_m, NULL) == SNAP_OK, "save again");
    unsigned at = SNAP_HEADER_LEN + SNAP_STATE_LEN + SNAP_PORT_LEN + SNAP_SSC_LEN + 1u + 6u + 1u + 1u + 8u + 8u +
                  1u + 1u + 8u + 7u;
    s_snap.buf[at] ^= 0x01u;   /* the due's top byte */
    mem_recrc(&s_snap);
    CHECK(mem_check(&s_snap, &s_m, NULL, NULL) == SNAP_NOT_SNAPSHOT, "a 6551 due far off accepted");
    return 0;
}

static int test_reset(void) {
    build(&s_m, false, 0, 0);
    fields(&s_m, 2);
    coco_serial_put(&s_m, 'Q');
    (void)coco_run(&s_m, 100);
    coco_reset(&s_m);
    const acia_t *a = coco_serial(&s_m);
    CHECK(a->status == ACIA_SR_TDRE && a->command == 0 && a->control == 0, "RESET left the 6551 %02X %02X %02X",
          a->status, a->command, a->control);
    CHECK(!s_m.port.cart_low && s_m.pia1.b.c1, "RESET left CART low");
    return 0;
}

static int test_rom(void) {
    const char *dir;
    guest_find_roms(&dir);
    if (!guest_have_rom(ROM_RS232)) {
        printf("rs232.rom not in %s: its SHA-1 not checked (skipped)\n", dir);
        return 0;
    }
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&s_m, &cfg);
    CHECK(coco_load_rom(&s_m, 0xC000u, guest_rom_image(ROM_RS232), romset_images[ROM_RS232].size), "the ROM");
    CHECK(s_m.port.slot[0].kind == CART_RS232 && s_m.port.rs232_slot == 0 && !s_m.port.slot[0].autostart,
          "the pak's ROM not known: kind %d", (int)s_m.port.slot[0].kind);
    if (!guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("no Extended Color BASIC in %s: the pak's program not run (skipped)\n", dir);
        return 0;
    }
    /* Its program, from Extended Color BASIC: EXEC &HC000, its menu,
     * BREAK to its terminal, 300 baud 8N1 as it starts; a line from the
     * host shown, and keys typed sent. */
    static guest_t g;
    coco_init(&g.m, &cfg);
    coco_load_rom(&g.m, 0xA000u, guest_rom_image(ROM_COCO_BAS13), romset_images[ROM_COCO_BAS13].size);
    coco_load_rom(&g.m, 0x8000u, guest_rom_image(ROM_COCO_EXTBAS11), romset_images[ROM_COCO_EXTBAS11].size);
    coco_load_rom(&g.m, 0xC000u, guest_rom_image(ROM_RS232), romset_images[ROM_RS232].size);
    coco_power_on(&g.m);
    keymatrix_init(&g.k);
    CHECK(guest_boot(&g), "Extended Color BASIC did not start beside the pak");
    guest_type(&g, "EXEC &HC000\r");
    guest_fields(&g, 60);
    CHECK(guest_find_row(&g.m, "DELUXE RS-232 PROGRAM PAK", 0) >= 0 &&
          guest_find_row(&g.m, "B: BAUD RATE: 300", 0) >= 0, "the pak's menu");
    guest_press(&g, PICOCALC_KEY_ESC, false, false);   /* BREAK */
    guest_fields(&g, 30);
    const acia_t *a = coco_serial(&g.m);
    CHECK(a->control == 0x16 && a->command == 0x09 && acia_baud(a) == 300u,
          "the terminal's 6551: control %02X command %02X", a->control, a->command);
    for (const char *p = "HELLO FROM THE HOST\r"; *p; p++) {
        CHECK(coco_serial_put(&g.m, (uint8_t)*p), "put");
        guest_fields(&g, 1);
    }
    guest_fields(&g, 20);
    CHECK(guest_find_row(&g.m, "HELLO FROM THE HOST", 0) >= 0, "the host's line not shown");
    guest_type(&g, "hi pak");
    guest_fields(&g, 30);
    char out[64];
    int n = 0, c;
    while ((c = coco_serial_get(&g.m)) >= 0 && n < (int)sizeof out - 1) out[n++] = (char)c;
    out[n] = 0;
    CHECK(!strcmp(out, "HI PAK"), "the terminal sent \"%s\"", out);
    return 0;
}

int main(void) {
    make_rom();
    if (test_echo()) return 1;
    if (test_mpi_cts()) return 1;
    if (test_snapshot()) return 1;
    if (test_reset()) return 1;
    if (test_rom()) return 1;
    TEST_DONE();
}
