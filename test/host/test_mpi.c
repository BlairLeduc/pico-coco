/* test_mpi.c — the Multi-Pak (design.md §12.8, cart.h): its register at
 * $FF7F, CTS and SCS each to their slot, CART from the CTS slot alone,
 * NMI and HALT from any slot, RESET to the front switch; and the port
 * without one, where $FF7F is nothing. Driven through the bus as the
 * CPU drives it, with ROMs of the test's own: no ROM from the card.
 *
 * Planted one at a time, each failed this test (2026-11-24):
 *   - $FF7F's fields swapped in cart_io_write (CTS from bits 1-0)
 *   - SCS sent to slot 0 always, not the SCS slot
 *   - cart_on_q looking at slot 0, not the CTS slot
 *   - cart_lines taking the SCS slot's NMI and HALT alone
 *   - cart_reset_select leaving the slots where they were
 *   - $FF7F decoded without a Multi-Pak
 */
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "coco.h"
#include "smartwatch.h"
#include "test_util.h"

static coco_t s_m;

/* A ROM of 8 KB, every byte `fill`. */
static uint8_t s_rom[4][0x2000];

static int build(bool mpi, unsigned sw, const cart_kind_t kinds[4], bool roms[4]) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    if (mpi) {
        cfg.cart = CART_MPI;
        cfg.mpi_switch = (uint8_t)sw;
        for (unsigned s = 0; s < 4; s++) cfg.slot[s] = kinds[s];
    } else {
        cfg.cart = kinds[0];
    }
    coco_init(&s_m, &cfg);
    for (unsigned s = 0; s < 4; s++) {
        if (!roms || !roms[s]) continue;
        memset(s_rom[s], 0x10 + (int)s, sizeof s_rom[s]);
        CHECK(mpi ? coco_load_slot_rom(&s_m, s, s_rom[s], sizeof s_rom[s])
                  : coco_load_rom(&s_m, 0xC000u, s_rom[s], sizeof s_rom[s]),
              "slot %u's ROM refused", s);
    }
    coco_power_on(&s_m);
    return 0;
}

static const cart_kind_t k_roms[4] = { CART_ROM, CART_ROM, CART_ROM, CART_ROM };

/* The register and CTS: each slot's ROM at $C000 as it is selected. */
static int test_cts(void) {
    bool roms[4] = { true, true, true, true };
    build(true, 2, k_roms, roms);
    CHECK(bus_read(&s_m, 0xFF7F) == 0x22, "after RESET, the switch's slot: $FF7F %02X",
          bus_read(&s_m, 0xFF7F));
    CHECK(bus_read(&s_m, 0xC000) == 0x12 && bus_read(&s_m, 0xFEFF) == 0x12,
          "the switch's slot's ROM: %02X", bus_read(&s_m, 0xC000));
    for (unsigned s = 0; s < 4; s++) {
        bus_write(&s_m, 0xFF7F, (uint8_t)(s << 4 | 1u));
        CHECK(bus_read(&s_m, 0xC000) == 0x10 + s && bus_read(&s_m, 0xE123) == 0x10 + s,
              "CTS slot %u: $C000 %02X", s, bus_read(&s_m, 0xC000));
        CHECK(bus_read(&s_m, 0xFF7F) == (uint8_t)(s << 4 | 1u), "$FF7F read back %02X",
              bus_read(&s_m, 0xFF7F));
    }
    bus_write(&s_m, 0xFF7F, 0xFFu);
    CHECK(bus_read(&s_m, 0xFF7F) == 0x33, "the other bits kept: %02X", bus_read(&s_m, 0xFF7F));

    /* An empty slot selected: the window reads the bus. */
    bool some[4] = { true, false, false, false };
    build(true, 0, k_roms, some);
    bus_write(&s_m, 0xFF7F, 0x10);
    CHECK(s_m.page[0xC0].read == NULL, "an empty CTS slot still mapped");
    return 0;
}

/* RESET puts both fields back to the switch. */
static int test_reset(void) {
    bool roms[4] = { true, true, true, true };
    build(true, 1, k_roms, roms);
    bus_write(&s_m, 0xFF7F, 0x30);
    CHECK(bus_read(&s_m, 0xC000) == 0x13, "slot 3 not selected");
    coco_reset(&s_m);
    CHECK(bus_read(&s_m, 0xFF7F) == 0x11 && bus_read(&s_m, 0xC000) == 0x11,
          "RESET: $FF7F %02X, $C000 %02X", bus_read(&s_m, 0xFF7F), bus_read(&s_m, 0xC000));
    return 0;
}

/* SCS: the WD1793's track register reached only through the controller's
 * slot; a write elsewhere goes nowhere. */
static int test_scs(void) {
    const cart_kind_t kinds[4] = { CART_NONE, CART_NONE, CART_RSDOS, CART_NONE };
    build(true, 2, kinds, NULL);
    bus_write(&s_m, 0xFF49, 0x5A);   /* the track register */
    CHECK(bus_read(&s_m, 0xFF49) == 0x5A, "the controller's slot: track %02X",
          bus_read(&s_m, 0xFF49));
    bus_write(&s_m, 0xFF7F, 0x20);   /* SCS slot 0, CTS still 2 */
    uint8_t bus = bus_read(&s_m, 0xFF4A);
    bus_write(&s_m, 0xFF49, 0xA5);
    CHECK(bus_read(&s_m, 0xFF49) == bus, "another slot's SCS reached the controller");
    bus_write(&s_m, 0xFF7F, 0x22);
    CHECK(bus_read(&s_m, 0xFF49) == 0x5A, "the track register changed from another slot: %02X",
          bus_read(&s_m, 0xFF49));
    return 0;
}

/* The SmartWatch on its slot's SCS (§12.7): the key sent a bit a read,
 * $FF58 a 0 and $FF59 a 1, then its seconds read from $FF5C. */
static uint8_t clock_read(unsigned reg_wanted) {
    static const uint8_t key[8] = { 0xC5, 0x3A, 0xA3, 0x5C, 0xC5, 0x3A, 0xA3, 0x5C };
    (void)bus_read(&s_m, 0xFF5C);
    for (unsigned i = 0; i < 64; i++) (void)bus_read(&s_m, (uint16_t)(0xFF58u | ((key[i / 8] >> (i % 8)) & 1u)));
    uint8_t want = 0;
    for (unsigned r = 0; r < 8; r++) {
        uint8_t v = 0;
        for (unsigned b = 0; b < 8; b++) v |= (uint8_t)((bus_read(&s_m, 0xFF5C) & 1u) << b);
        if (r == reg_wanted) want = v;
    }
    return want;
}

static int test_smartwatch(void) {
    const cart_kind_t kinds[4] = { CART_NONE, CART_RSDOS, CART_NONE, CART_SMARTWATCH };
    build(true, 1, kinds, NULL);
    cart_t *c = cart_smartwatch(&s_m);
    CHECK(c != NULL && c == &s_m.port.slot[3], "the SmartWatch's slot");
    /* 2026-11-24 13:45:37 */
    smartwatch_time_t t = { 2026, 11, 24, 13, 45, 37 };
    smartwatch_set_host(&c->u.clock, (uint32_t)smartwatch_seconds(&t));
    (void)clock_read(1);
    CHECK(!c->u.clock.open && c->u.clock.key == 0,
          "the SmartWatch heard SCS with the switch's slot (1) selected");
    bus_write(&s_m, 0xFF7F, 0x13);   /* SCS slot 3, as clock_bb selects it */
    CHECK(clock_read(1) == 0x37, "seconds through slot 3: %02X", clock_read(1));
    CHECK(clock_read(2) == 0x45, "minutes: %02X", clock_read(2));
    CHECK(clock_read(7) == 0xC6, "the year, 126 as OS-9 sums it: %02X", clock_read(7));
    return 0;
}

/* CART from the CTS slot alone: a ROM's line on Q only while selected. */
static int test_cart_line(void) {
    bool roms[4] = { true, false, false, false };
    build(true, 1, k_roms, roms);
    CHECK(!cart_on_q(&s_m.port), "slot 0's CART seen with slot 1 selected");
    bus_write(&s_m, 0xFF7F, 0x01);
    CHECK(cart_on_q(&s_m.port), "slot 0's CART not seen with it selected");
    /* That BASIC starts it, and only then, is test_cart's, on the ROMs. */
    return 0;
}

/* NMI and HALT from any slot: the controller's HALT enable holds the CPU
 * though another slot has SCS. */
static int test_lines(void) {
    const cart_kind_t kinds[4] = { CART_NONE, CART_RSDOS, CART_NONE, CART_NONE };
    build(true, 1, kinds, NULL);
    CHECK(!s_m.cpu.halt, "HALT after RESET");
    bus_write(&s_m, 0xFF40, 0x80);   /* the latch: HALT enabled, DRQ low */
    CHECK(s_m.cpu.halt, "the controller's HALT not on the CPU");
    bus_write(&s_m, 0xFF7F, 0x10);   /* SCS to slot 0 */
    /* The controller acts while another slot has SCS, as it may while
     * clock_bb holds slot 4 (§12.7): its lines are worked out again. */
    coco_disk_insert(&s_m, 0, false);
    CHECK(s_m.cpu.halt, "HALT from a slot without SCS dropped");
    bus_write(&s_m, 0xFF7F, 0x11);
    bus_write(&s_m, 0xFF40, 0x00);
    CHECK(!s_m.cpu.halt, "HALT not released");
    return 0;
}

/* Without a Multi-Pak, $FF7F is nothing: a write moves nothing, a read is
 * the bus, and SCS reaches the port's cartridge. */
static int test_no_mpi(void) {
    const cart_kind_t kinds[4] = { CART_RSDOS, CART_NONE, CART_NONE, CART_NONE };
    build(false, 0, kinds, NULL);
    uint8_t bus = bus_read(&s_m, 0xFF7E);
    CHECK(bus_read(&s_m, 0xFF7F) == bus, "$FF7F answered without a Multi-Pak");
    bus_write(&s_m, 0xFF7F, 0x33);
    bus_write(&s_m, 0xFF49, 0x77);
    CHECK(bus_read(&s_m, 0xFF49) == 0x77, "the port's controller lost after a write to $FF7F");
    return 0;
}

/* One RS-DOS controller at most: a second is fitted empty. */
static int test_one_controller(void) {
    const cart_kind_t kinds[4] = { CART_RSDOS, CART_NONE, CART_NONE, CART_RSDOS };
    build(true, 0, kinds, NULL);
    CHECK(s_m.port.rsdos_slot == 0 && s_m.port.slot[3].kind == CART_NONE,
          "two controllers: slot %d, slot 3 kind %d", s_m.port.rsdos_slot, s_m.port.slot[3].kind);
    return 0;
}

/* The pool: room for Disk BASIC, a 16 KB game and 8 KB more, and no more. */
static int test_pool(void) {
    static uint8_t big[0x4000];
    memset(big, 0x55, sizeof big);
    build(true, 0, k_roms, NULL);
    CHECK(coco_load_slot_rom(&s_m, 0, big, sizeof big), "16 KB refused");
    CHECK(coco_load_slot_rom(&s_m, 1, s_rom[0], sizeof s_rom[0]), "8 KB refused");
    CHECK(coco_load_slot_rom(&s_m, 2, s_rom[0], sizeof s_rom[0]), "the last 8 KB refused");
    CHECK(!coco_load_slot_rom(&s_m, 3, s_rom[0], sizeof s_rom[0]), "past the pool taken");
    /* A 2 KB image repeats through the window. */
    static uint8_t small[0x800];
    for (unsigned i = 0; i < sizeof small; i++) small[i] = (uint8_t)i;
    CHECK(coco_load_slot_rom(&s_m, 2, small, sizeof small), "2 KB into slot 2's room refused");
    bus_write(&s_m, 0xFF7F, 0x20);
    CHECK(bus_read(&s_m, 0xC801) == 0x01 && bus_read(&s_m, 0xF7FF) == 0xFF,
          "2 KB not mirrored: %02X %02X", bus_read(&s_m, 0xC801), bus_read(&s_m, 0xF7FF));
    return 0;
}

/* The port's path (§12.8): a ROM read into the pool before the power-on,
 * which coco_init leaves, then placed: no second copy. */
static int test_placed(void) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.cart = CART_MPI;
    cfg.slot[1] = CART_ROM;
    cfg.mpi_switch = 1;
    for (unsigned i = 0; i < 0x1000u; i++) s_m.cart[0x4000u + i] = (uint8_t)(i * 7u);
    cart_pool_finish(&s_m.cart[0x4000u], 0x1000u);
    CHECK(cart_room(0x1000u) == 0x1000u && cart_room(0x1800u) == COCO_CART_SIZE &&
          cart_room(0x80u) == COCO_PAGE_SIZE && cart_room(0x4001u) == 0,
          "rooms: %u %u %u", cart_room(0x1000u), cart_room(0x1800u), cart_room(0x80u));
    coco_init(&s_m, &cfg);
    CHECK(s_m.cart[0x4007u] == 49u, "coco_init cleared the pool");
    CHECK(coco_slot_rom_placed(&s_m, 1, 0x4000u, 0x1000u), "the placed ROM refused");
    coco_power_on(&s_m);
    CHECK(bus_read(&s_m, 0xC007) == 49u && bus_read(&s_m, 0xD007) == 49u,
          "the placed 4 KB, mirrored: %02X %02X", bus_read(&s_m, 0xC007), bus_read(&s_m, 0xD007));
    CHECK(!coco_slot_rom_placed(&s_m, 2, COCO_CART_POOL - 0x800u, 0x1000u),
          "a room past the pool's end taken");
    return 0;
}

int main(void) {
    test_cts();
    test_reset();
    test_scs();
    test_smartwatch();
    test_cart_line();
    test_lines();
    test_no_mpi();
    test_one_controller();
    test_pool();
    test_placed();
    TEST_DONE();
}
