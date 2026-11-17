/* test_snapshot.c — our own save states, on our own ROM (design.md
 * §12.5; EL §8.5).
 *
 * A test ROM of some thirty instructions at $A000, so that CI runs it on
 * every push without a CoCo ROM: the field interrupt on PIA0 CB1,
 * counted; the DAC stepped through the sound multiplexer every pass of
 * a loop, so the bus moves; and a CWAI every 64th pass, so that states
 * are taken inside a wait. test_snapshot_rom does the same over the
 * real ROMs.
 *
 * The property that matters is executed, not compared: a machine saved
 * part-way through, restored into a machine that has been doing
 * something else and run on for 150 fields, must arrive exactly where
 * the original arrives (snap_util.h's comparison), straight after the
 * load as well, and sample for sample. A state one cycle out must not.
 * Then the refusals — damaged, short, foreign or newer files, fields no
 * running machine keeps, another ROM, RAM, cartridge or trap, a tape
 * request waiting — each leave the machine that refused it untouched.
 */

#include <string.h>

#include "coco.h"
#include "snap_util.h"
#include "test_util.h"

static uint8_t rom[0x2000];
static coco_t g, h, ahead, before, saved;
static mem_t snap;

#define IRQS  0x10u   /* the field interrupts, 16 bits */
#define PASS  0x22u   /* the loop's passes, 16 bits    */

/* With `cwai` false the CWAI is a BRA back to the loop: a machine that
 * never waits for its interrupt, whose phase against the field nothing
 * pulls back into step. */
static void build_rom(bool cwai) {
    static const uint8_t code[] = {
        0x10, 0xCE, 0x0F, 0x00,     /* $A000 LDS  #$0F00                    */
        0xB7, 0xFF, 0xDD,           /* $A004 STA  $FFDD   SAM M1: 64K       */
        0x86, 0x30,                 /* $A007 LDA  #$30                      */
        0xB7, 0xFF, 0x21,           /* $A009 STA  $FF21   PIA1 CRA: the DDR */
        0x86, 0xFC,                 /* $A00C LDA  #$FC                      */
        0xB7, 0xFF, 0x20,           /* $A00E STA  $FF20   PA2-PA7 out       */
        0x86, 0x34,                 /* $A011 LDA  #$34                      */
        0xB7, 0xFF, 0x21,           /* $A013 STA  $FF21   the data          */
        0x86, 0x3C,                 /* $A016 LDA  #$3C                      */
        0xB7, 0xFF, 0x23,           /* $A018 STA  $FF23   CB2 high: sound on */
        0x86, 0x34,                 /* $A01B LDA  #$34                      */
        0xB7, 0xFF, 0x01,           /* $A01D STA  $FF01   CA2 low           */
        0x86, 0x35,                 /* $A020 LDA  #$35                      */
        0xB7, 0xFF, 0x03,           /* $A022 STA  $FF03   CB2 low: the DAC; FS IRQ */
        0x1C, 0xEF,                 /* $A025 ANDCC #$EF                     */
        0x96, 0x20,                 /* $A027 LDA  <$20                      */
        0x8B, 0x04,                 /* $A029 ADDA #4                        */
        0x97, 0x20,                 /* $A02B STA  <$20                      */
        0xB7, 0xFF, 0x20,           /* $A02D STA  $FF20   the DAC           */
        0x9E, 0x22,                 /* $A030 LDX  <$22                      */
        0x30, 0x01,                 /* $A032 LEAX 1,X                       */
        0x9F, 0x22,                 /* $A034 STX  <$22                      */
        0x1F, 0x10,                 /* $A036 TFR  X,D                       */
        0xC4, 0x3F,                 /* $A038 ANDB #$3F                      */
        0x26, 0xEB,                 /* $A03A BNE  $A027                     */
        0x3C, 0xEF,                 /* $A03C CWAI #$EF                      */
        0x20, 0xE7,                 /* $A03E BRA  $A027                     */
    };
    static const uint8_t irq[] = {
        0xB6, 0xFF, 0x02,           /* $A050 LDA  $FF02   clear the flag    */
        0xDC, 0x10,                 /*       LDD  <$10                      */
        0xC3, 0x00, 0x01,           /*       ADDD #1                        */
        0xDD, 0x10,                 /*       STD  <$10                      */
        0x3B,                       /*       RTI                            */
    };
    memset(rom, 0x12, sizeof rom);  /* NOPs */
    memcpy(rom, code, sizeof code);
    memcpy(&rom[0x50], irq, sizeof irq);
    rom[0x1FF8] = 0xA0; rom[0x1FF9] = 0x50;   /* IRQ   */
    rom[0x1FFE] = 0xA0; rom[0x1FFF] = 0x00;   /* RESET */
    if (!cwai) { rom[0x3C] = 0x20; rom[0x3D] = 0xE9; }   /* BRA $A027 */
}

static void machine(coco_t *m, unsigned ram_k) {
    coco_config_t cfg;
    coco_config_default(&cfg);
    cfg.ram_k = ram_k;
    coco_init(m, &cfg);
    coco_load_rom(m, 0xA000u, rom, sizeof rom);
    coco_power_on(m);
    m->pcm.dc_block = false;
}

static unsigned word(const coco_t *m, uint16_t a) {
    return (unsigned)(m->ram[a] << 8 | m->ram[a + 1u]);
}

static int16_t sound_a[SNAP_SOUND_MAX], sound_b[SNAP_SOUND_MAX];

/* n fields, the sound drained after each onto out at *got, if out. */
static void fields_sound(coco_t *m, int n, int16_t *out, size_t *got) {
    size_t none = 0;
    for (int i = 0; i < n; i++) {
        coco_run_field(m);
        snap_sound_take(m, out, out ? got : &none);
    }
}

static void fields(coco_t *m, int n) {
    fields_sound(m, n, NULL, NULL);
}

/* The state section's byte at `off`, changed, with the CRC made good
 * again so that only the field is wrong. */
static void poke_state(unsigned off, uint8_t v) {
    snap.buf[SNAP_HEADER_LEN + off] = v;
    mem_recrc(&snap);
}

/* A good file, one byte of its state changed and its CRC made good:
 * refused `want` by check and by load, the machine untouched. Byte
 * offsets are snapshot.c's. */
static int refused(unsigned off, uint8_t v, snap_status_t want, const char *what) {
    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "%s: save", what);
    poke_state(off, v);
    coco_copy(&before, &h);
    snap_status_t c = mem_check(&snap, &h, NULL, NULL);
    snap_status_t l = mem_load(&snap, &h);
    CHECK(c == want && l == want, "%s: check %s, load %s, want %s", what, snapshot_status_str(c),
          snapshot_status_str(l), snapshot_status_str(want));
    CHECK(snap_same(&before, &h, what), "%s: the refusing machine changed", what);
    return 0;
}

/* A step at a time across each field's FS fall, the rest of the field
 * run whole, until `pred` holds at a boundary: only coco_run_field moves
 * to the next field, and only it or a PIA access sees FS fall. */
static bool step_to(coco_t *m, bool (*pred)(const coco_t *)) {
    for (int f = 0; f < 300; f++) {
        uint64_t fall = m->field_start + (uint64_t)COCO_FS_FALL_LINE * COCO_LINE_TICKS;
        while (coco_ticks(m) < fall + 2000u) {
            coco_run(m, 1);
            if (pred(m)) return true;
        }
        coco_run_field(m);
    }
    return false;
}

static bool rise_due(const coco_t *m) {
    for (unsigned p = 0; p < COCO_PIA_PORTS; p++)
        if (m->irq_due[p]) return true;
    return false;
}

static bool irq_held_masked(const coco_t *m) {
    return m->pia0.b.irq_out && (m->cpu.cc & MC6809_CC_I) && m->cpu.irq;
}

/* The machine loaded into, given values a running one would not have
 * now, so that a field the load forgets is not equal in both by chance
 * (EL §8.5). */
static void poison(coco_t *m) {
    /* Map type 1, all RAM, so that the page table is not the saved
     * machine's unless the load rebuilds it. */
    coco_slow_write(m, 0xFFDFu, 0);
    mc6809_t *c = &m->cpu;
    c->wait = MC6809_SYNC;   /* which the test ROM never executes */
    c->stale = !c->stale;
    c->latch ^= MC6809_LINE_IRQ | MC6809_LINE_NMI;
    c->active ^= MC6809_LINE_FIRQ | MC6809_LINE_NMI;
    c->nmi_line = !c->nmi_line;
    c->nmi_pending = !c->nmi_pending;
    c->nmi_armed = !c->nmi_armed;
    c->halt = !c->halt;
    c->irq ^= 0x80u;
    c->firq ^= 0x80u;
    c->undoc_count += 3u;
    c->undoc_pc ^= 0x1234u;
    c->undoc_op ^= 0x10FFu;
    c->instructions += 5u;
    mc6821_port_t *ports[4] = { &m->pia0.a, &m->pia0.b, &m->pia1.a, &m->pia1.b };
    for (unsigned i = 0; i < 4; i++) {
        ports[i]->out ^= 0x5Au;
        ports[i]->ddr ^= 0xA5u;
        ports[i]->cr ^= 0x3Cu;
        ports[i]->in ^= 0xFFu;
        ports[i]->c1 = !ports[i]->c1;
        ports[i]->c2 = !ports[i]->c2;
        ports[i]->irq_out = !ports[i]->irq_out;
        m->irq_due[i] = coco_ticks(m) + 7u + i;
    }
    m->sam.reg ^= 0x0207u;
    m->hs_seen += 99u;
    m->sound_level ^= 0x155u;
    m->sound_fed = !m->sound_fed;
    m->sound_edges += 11u;
    m->video_bits ^= 0x10001u;
    m->video_split = !m->video_split;
    m->split_fields += 2u;
    m->tape.hot = 0xA70Fu;
}

/* Save `m` at a boundary where it stands, run the original on, restore
 * into `h` (which has been doing something else) and meet: straight
 * after, after 150 fields, sample for sample. */
static int meet(coco_t *m, const char *what) {
    CHECK(mem_save(&snap, m, NULL) == SNAP_OK, "%s: save", what);
    coco_copy(&saved, m);
    snap_sound_start(m);
    size_t na = 0, nb = 0;
    fields_sound(m, 150, sound_a, &na);
    coco_copy(&ahead, m);

    machine(&h, 64);
    fields(&h, 11);
    coco_key_set(&h, 1, 2, true);
    poison(&h);
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_OK, "%s: check", what);
    CHECK(mem_load(&snap, &h) == SNAP_OK, "%s: load", what);
    {
        char w[64];
        snprintf(w, sizeof w, "%s, straight after", what);
        CHECK(snap_same(&saved, &h, w), "%s: not the saved machine straight after the load", what);
    }
    for (unsigned c = 0; c < COCO_KEY_COLS; c++) CHECK(h.keys[c] == 0, "%s: keys released", what);
    fields_sound(&h, 150, sound_b, &nb);
    CHECK(snap_same(&ahead, &h, what), "%s: apart after 150 fields", what);
    CHECK(snap_same_sound(sound_a, na, sound_b, nb, what), "%s: the sound differs", what);
    CHECK(na > 90000, "%s: %zu samples", what, na);
    return 0;
}

int main(void) {
    CHECK(snapshot_crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u,
          "CRC-32 of 123456789 is %08X", (unsigned)snapshot_crc32(0, (const uint8_t *)"123456789", 9));
    build_rom(true);

    /* ---- the test ROM runs as it should --------------------------------- */
    machine(&g, 64);
    fields(&g, 37);
    CHECK(word(&g, IRQS) >= 30, "the field interrupt should be counted: %u", word(&g, IRQS));
    CHECK(g.sound_edges > 1000, "the bus should move: %u edges", (unsigned)g.sound_edges);

    /* ---- at a field's end, the state's own size and contents ------------ */
    CHECK(mem_save(&snap, &g, NULL) == SNAP_OK, "save");
    CHECK(snap.len == SNAP_FILE_LEN, "a state is %zu bytes, want %u", snap.len, (unsigned)SNAP_FILE_LEN);
    {
        const uint8_t *st = snap.buf + SNAP_HEADER_LEN;
        CHECK(memcmp(snap.buf, "COCOSNAP", 8) == 0, "the magic");
        CHECK(memcmp(st + SNAP_STATE_LEN, g.ram, COCO_ADDR_SPACE) == 0, "RAM follows the state, as it stands");
        bool reserved = true;
        for (unsigned i = 232; i < SNAP_STATE_LEN; i++) reserved = reserved && st[i] == 0;
        CHECK(reserved, "reserved state bytes are written zero");
        bool found = false;
        for (size_t i = 0; i + 32u <= snap.len && !found; i++) found = memcmp(snap.buf + i, rom, 32u) == 0;
        CHECK(!found, "ROM bytes must not be in a state");
    }
    if (meet(&g, "a field's end")) return 1;

    /* ---- mid-field, inside CWAI, and with a rise due --------------------- */
    machine(&g, 64);
    fields(&g, 20);
    for (int i = 0; i < 200000 && g.cpu.wait != MC6809_CWAI; i++) coco_run(&g, 1);
    CHECK(g.cpu.wait == MC6809_CWAI, "no boundary in CWAI");
    if (meet(&g, "in CWAI")) return 1;

    /* Without the CWAI, whose wait no single step brings FS up to: the
     * loop's PIA write does (coco.c's before_pia). */
    build_rom(false);
    machine(&g, 64);
    fields(&g, 20);
    CHECK(step_to(&g, rise_due), "no boundary with a rise due");
    if (meet(&g, "a rise due")) return 1;

    /* Between the interrupt's entry and the handler's read of $FF02:
     * PIA0's IRQ held and the CPU masked, so that the CPU's lines and
     * attention, which a load works out and does not store, are not
     * zero. */
    machine(&g, 64);
    fields(&g, 15);
    CHECK(step_to(&g, irq_held_masked), "no boundary with IRQ held and masked");
    if (meet(&g, "IRQ held, masked")) return 1;
    build_rom(true);

    /* At the fast rate (SAM R1), so that the clock's base moves. */
    machine(&g, 64);
    fields(&g, 5);
    coco_slow_write(&g, 0xFFD9u, 0);   /* R1 set */
    fields(&g, 7);
    CHECK(g.tpc == 8u, "the fast rate: %u ticks a cycle", (unsigned)g.tpc);
    if (meet(&g, "the fast rate")) return 1;

    /* ---- a state one cycle out does not meet ------------------------------ */
    /* On the ROM without the CWAI: the wait for the interrupt would bring
     * the two back into step, as it does on the real machine. */
    build_rom(false);
    machine(&g, 64);
    fields(&g, 30);
    CHECK(mem_save(&snap, &g, NULL) == SNAP_OK, "save for the control");
    fields(&g, 150);
    machine(&h, 64);
    CHECK(mem_load(&snap, &h) == SNAP_OK, "load for the control");
    h.cpu.cycles++;
    fields(&h, 150);
    CHECK(!snap_same(&g, &h, "the control (one cycle out, expected to differ)"),
          "a machine one cycle out should not meet the original");
    build_rom(true);

    /* ---- the media round-trip --------------------------------------------- */
    {
        static snap_media_t md, back;
        memset(&md, 0, sizeof md);
        snprintf(md.tape, sizeof md.tape, "/coco/tapes/PIPES.CAS");
        snprintf(md.cart, sizeof md.cart, "/coco/carts/Doodle Bug.ccc");
        md.tape_pos = 12345u;
        md.tape_rewind = 7u;
        md.tape_chosen = true;
        CHECK(mem_save(&snap, &g, &md) == SNAP_OK, "save with media");
        memset(&back, 0x55, sizeof back);
        CHECK(mem_check(&snap, &g, NULL, &back) == SNAP_OK, "check with media");
        CHECK(memcmp(&md, &back, sizeof md) == 0, "the media come back as they went");
    }

    /* ---- refusals: the file -------------------------------------------- */
    machine(&g, 64);
    fields(&g, 25);
    coco_copy(&saved, &g);
    machine(&h, 64);
    fields(&h, 9);

    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    snap.buf[SNAP_HEADER_LEN + SNAP_STATE_LEN + 0x1234u] ^= 0x01u;
    coco_copy(&before, &h);
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_CORRUPT, "a flipped RAM bit is damage");
    CHECK(snap_same(&before, &h, "damaged"), "a check changed the machine");

    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    snap.len -= 100;
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_IO, "a short file");
    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    snap.buf[0] = 'X';
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_NOT_SNAPSHOT, "a foreign magic");
    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    snap.buf[8] = SNAP_VERSION + 1u;
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_NEWER, "a newer version");
    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    snap.buf[12] ^= 0x01u;
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_NOT_SNAPSHOT, "a wrong payload length");
    CHECK(snap_same(&before, &h, "file refusals"), "a refusal changed the machine");

    /* ---- refusals: fields no running machine keeps, each with a good CRC -- */
    if (refused(155, 8, SNAP_NOT_SNAPSHOT, "RAM of 8K")) return 1;
    if (refused(154, 7, SNAP_NOT_SNAPSHOT, "machine 7")) return 1;
    if (refused(154, 1, SNAP_NOT_SNAPSHOT, "a Dragon 32's, M3 to M11")) return 1;
    if (refused(156, 0x80, SNAP_NOT_SNAPSHOT, "an unknown fitting")) return 1;
    if (refused(31, 4, SNAP_NOT_SNAPSHOT, "a wait of 4")) return 1;
    if (refused(30, 0x80, SNAP_NOT_SNAPSHOT, "an unknown CPU flag")) return 1;
    if (refused(80, 12, SNAP_NOT_SNAPSHOT, "12 ticks a cycle")) return 1;
    if (refused(80, 8, SNAP_NOT_SNAPSHOT, "the fast rate with the SAM slow")) return 1;
    if (refused(152, 0x34, SNAP_NOT_SNAPSHOT, "a trap point off its page")) return 1;
    if (refused(137, 0x7F, SNAP_NOT_SNAPSHOT, "a sound level past full scale")) return 1;
    if (refused(89, 0x40, SNAP_NOT_SNAPSHOT, "a field that began long ago")) return 1;
    if (refused(97, 0x40, SNAP_NOT_SNAPSHOT, "HS seen far ahead")) return 1;
    if (refused(105, 0x40, SNAP_NOT_SNAPSHOT, "a rise due far ahead")) return 1;
    if (refused(79, 0x40, SNAP_NOT_SNAPSHOT, "cyc0 past the cycle count")) return 1;
    if (refused(14 + 7, 0x40, SNAP_NOT_SNAPSHOT, "a cycle count far past cyc0")) return 1;
    if (refused(161, 0x01, SNAP_NOT_SNAPSHOT, "a line of 2^24 ticks")) return 1;
    if (refused(42 + 4, 0x80, SNAP_NOT_SNAPSHOT, "an unknown PIA line")) return 1;

    /* The tick rate alone wrong: the clock's base moved to now, so that
     * nothing else about the time is out, and with the right rate, the
     * control, it loads. Offsets as above. */
    for (int wrong = 1; wrong >= 0; wrong--) {
        CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
        uint8_t *st = snap.buf + SNAP_HEADER_LEN;
        uint64_t now = coco_ticks(&saved);
        for (unsigned i = 0; i < 8; i++) {
            st[64 + i] = (uint8_t)(now >> (8 * i));                  /* tick0 */
            st[72 + i] = (uint8_t)(saved.cpu.cycles >> (8 * i));     /* cyc0  */
        }
        st[80] = wrong ? 8u : 16u;
        mem_recrc(&snap);
        snap_status_t want = wrong ? SNAP_NOT_SNAPSHOT : SNAP_OK;
        CHECK(mem_check(&snap, &h, NULL, NULL) == want, "the fast rate with the SAM slow, %s",
              wrong ? "refused" : "and the control, which loads");
    }

    /* ---- refusals: another machine, each having passed the rest ------------ */
    if (refused(162, 200, SNAP_OTHER_FIELD, "another field's lines")) return 1;
    if (refused(192, 0x00, SNAP_OTHER_ROM, "another BASIC")) return 1;
    {
        snap_info_t info;
        machine(&h, 16);
        CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
        coco_copy(&before, &h);
        CHECK(mem_check(&snap, &h, &info, NULL) == SNAP_OTHER_RAM, "into 16K");
        CHECK(mem_load(&snap, &h) == SNAP_OTHER_RAM, "loaded into 16K");
        CHECK(snap_same(&before, &h, "16K"), "the 16K machine changed");
        CHECK(info.ram_k == 64u && info.rom_present[1] &&
              !info.rom_present[0] && info.rom[1] == ROM_UNKNOWN && !info.cart && info.tape_trap,
              "the info names the machine: %u K", info.ram_k);
        /* Powered on as the info says, it loads. */
        machine(&h, info.ram_k);
        CHECK(mem_load(&snap, &h) == SNAP_OK, "loaded once powered on as its machine");
        CHECK(snap_same(&saved, &h, "powered on as its machine"), "not the saved machine");
    }
    {
        static uint8_t cart[0x2000];
        memset(cart, 0x39, sizeof cart);
        machine(&h, 64);
        coco_load_rom(&h, 0xC000u, cart, sizeof cart);
        coco_power_on(&h);
        CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
        CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_OTHER_MACHINE, "into a machine with a cartridge");
        coco_cart_autostart(&h, false);
        coco_copy(&before, &h);
        CHECK(mem_save(&snap, &h, NULL) == SNAP_OK, "save the cartridge's");
        cart[0] = 0x12;
        coco_load_rom(&h, 0xC000u, cart, sizeof cart);
        coco_cart_autostart(&h, false);
        CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_OTHER_MACHINE, "another cartridge");
        coco_copy(&h, &before);
        coco_cart_autostart(&h, true);
        CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_OTHER_MACHINE, "another CART line");
        machine(&h, 64);
        h.cfg.tape_trap = false;
        CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
        CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_OTHER_MACHINE, "without the trap");
    }

    /* ---- a tape request: no save, no load into it --------------------------- */
    machine(&h, 64);
    fields(&h, 3);
    h.tape.op = TAPE_END;
    CHECK(mem_save(&snap, &h, NULL) == SNAP_BUSY && snap.len == 0, "a save while a request waits");
    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    CHECK(mem_load(&snap, &h) == SNAP_BUSY, "a load into a machine waiting on the tape");
    h.tape.op = TAPE_NONE;

    /* ---- the tape's signal running: no save (cassette.h) ------------------- */
    machine(&h, 64);
    fields(&h, 3);
    coco_cassette_set_fast(&h, false);
    CHECK(mem_save(&snap, &h, NULL) == SNAP_OK, "a save with fast loading off and the relay open");
    coco_slow_write(&h, 0xFF21u, 0x3Cu);   /* PIA1 CA2 driven high: the relay */
    CHECK(coco_cassette_running(&h) && mem_save(&snap, &h, NULL) == SNAP_BUSY && snap.len == 0,
          "a save while the signal runs");
    coco_slow_write(&h, 0xFF21u, 0x34u);
    CHECK(mem_save(&snap, &h, NULL) == SNAP_OK, "a save once the relay opens");

    /* ---- torn between the passes ------------------------------------------- */
    machine(&h, 64);
    fields(&h, 4);
    CHECK(mem_save(&snap, &saved, NULL) == SNAP_OK, "save");
    CHECK(mem_check(&snap, &h, NULL, NULL) == SNAP_OK, "the first pass");
    snap.fail_at = SNAP_HEADER_LEN + SNAP_STATE_LEN + 100u;
    CHECK(mem_load(&snap, &h) == SNAP_TORN, "the second pass, cut in the RAM");
    snap.fail_at = 0;

    TEST_DONE();
}
