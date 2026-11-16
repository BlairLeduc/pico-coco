/* test_tape.c — the tape trap with the real ROMs (design.md §12.3,
 * tape.h).
 *
 *   - CSAVE and CLOAD round-trip a program, in Color BASIC 1.3 with
 *     Extended and in a 4 KB Color BASIC 1.1; CSAVEM and CLOADM a block
 *     of memory; the image the trap writes is well-formed CAS.
 *   - The requests: a CLOAD with the deck empty asks for the file by
 *     name, a CLOAD of a name the tape lacks runs to its end, and giving
 *     up presses the reset button with the program kept.
 *   - The trap's state held to the ROM's own routine: BLKIN against the
 *     ROM's with only its byte reader and sync search hooked; BLKOUT,
 *     WRTLDR and CASON against the ROM's own, unhooked, which needs no
 *     signal to write.
 *   - A BASIC ROM that is not one of the two by SHA-1 is not trapped.
 *   - Every CAS in the media directory (PICO_COCO_MEDIA, else media/;
 *     gitignored, none committed) loads without an I/O error.
 */

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bus.h"
#include "cas.h"
#include "guest.h"
#include "test_util.h"

#define TAPE_CAP 0x10000u
static uint8_t g_tape[TAPE_CAP];
static guest_t g;

/* Fields until the bottom-most text on the screen is "OK", up to max. */
static bool wait_ok(guest_t *gg, int max) {
    for (int f = 0; f < max; f++) {
        for (int r = 15; r >= 0; r--) {
            const char *s = guest_row(&gg->m, r);
            if (!s[0]) continue;
            if (strcmp(s, "OK") == 0 && coco_tape_pending(&gg->m) == TAPE_NONE) return true;
            break;
        }
        guest_fields(gg, 1);
    }
    return false;
}

static bool screen_has(const coco_t *m, const char *s) {
    for (int r = 0; r < 16; r++)
        if (strstr(guest_row(m, r), s)) return true;
    return false;
}

static void boot_with_tape(guest_t *gg, unsigned ram_k, rom_id_t basic, bool ext) {
    guest_build_as(gg, ram_k, basic, ext);
    coco_tape_attach(&gg->m, g_tape, TAPE_CAP);
    guest_boot(gg);
}

/* The image's blocks: a namefile, data, an end, each checksum good. */
static int check_image(const uint8_t *img, uint32_t len, const char *name, unsigned *data_blocks) {
    cas_block_t b;
    uint32_t at = 0;
    unsigned kinds[3] = {0, 0, 0};
    while (cas_next_block(img, len, at, &b)) {
        CHECK(b.sum_ok, "block at %u: bad checksum", b.at);
        CHECK(b.at >= 1 && img[b.at - 1] == CAS_LEADER_BYTE, "block at %u: no $55 before", b.at);
        CHECK(b.next < len && img[b.next] == CAS_LEADER_BYTE, "block at %u: no $55 after", b.at);
        kinds[b.type == CAS_BLOCK_NAME ? 0 : b.type == CAS_BLOCK_DATA ? 1 : 2]++;
        at = b.next;
    }
    CHECK(kinds[0] == 1 && kinds[2] == 1, "%u namefiles, %u ends", kinds[0], kinds[2]);
    char got[CAS_NAME_LEN + 1];
    CHECK(cas_first_name(img, len, got) && strcmp(got, name) == 0, "name '%s'", got);
    *data_blocks = kinds[1];
    return 0;
}

/* ---- CSAVE and CLOAD ------------------------------------------------- */

static int round_trip(unsigned ram_k, rom_id_t basic, bool ext) {
    boot_with_tape(&g, ram_k, basic, ext);
    guest_type(&g, "10 PRINT \"HELLO\"\n");
    guest_type(&g, "20 FOR I=1 TO 3:PRINT I*7:NEXT\n");
    guest_type(&g, "CSAVE \"PROG\"\n");
    CHECK(wait_ok(&g, 300), "%u KB: CSAVE did not come back", ram_k);
    const coco_tape_t *t = &g.m.tape;
    CHECK(t->inserted && t->len > 0 && t->saved == 0, "%u KB: len %u saved %u", ram_k, t->len, t->saved);
    CHECK(!t->overflow, "overflow");
    unsigned data = 0;
    if (check_image(g_tape, t->len, "PROG", &data)) return 1;
    CHECK(data == 1, "%u data blocks for a two-line program", data);
    CHECK(t->blocks_out == 3, "%u blocks written", t->blocks_out);

    guest_type(&g, "NEW\n");
    guest_type(&g, "CLOAD \"PROG\"\n");
    CHECK(wait_ok(&g, 300), "%u KB: CLOAD did not come back", ram_k);
    CHECK(t->blocks_in == 3, "%u blocks read", t->blocks_in);
    guest_type(&g, "RUN\n");
    CHECK(wait_ok(&g, 300), "RUN did not end");
    CHECK(screen_has(&g.m, "HELLO") && screen_has(&g.m, " 21"), "the loaded program did not run");
    if (test_failures) guest_dump(&g.m, stderr);
    return 0;
}

static int machine_code(void) {
    boot_with_tape(&g, 64, ROM_COCO_BAS13, true);
    for (unsigned i = 0; i < 256; i++) bus_write(&g.m, (uint16_t)(0x3000u + i), (uint8_t)(i * 37u + 5u));
    guest_type(&g, "CSAVEM \"MC\",&H3000,&H30FF,&H3000\n");
    CHECK(wait_ok(&g, 300), "CSAVEM did not come back");
    for (unsigned i = 0; i < 256; i++) bus_write(&g.m, (uint16_t)(0x3000u + i), 0);
    guest_type(&g, "CLOADM \"MC\"\n");
    CHECK(wait_ok(&g, 300), "CLOADM did not come back");
    unsigned bad = 0;
    for (unsigned i = 0; i < 256; i++)
        if (bus_read(&g.m, (uint16_t)(0x3000u + i)) != (uint8_t)(i * 37u + 5u)) bad++;
    CHECK(bad == 0, "%u bytes differ after CLOADM", bad);
    unsigned data = 0;
    if (check_image(g_tape, g.m.tape.len, "MC", &data)) return 1;
    CHECK(data == 2, "%u data blocks for 256 bytes", data);
    return 0;
}

/* ---- the requests ---------------------------------------------------- */

static bool run_to_request(guest_t *gg, tape_op_t op, int max) {
    for (int f = 0; f < max; f++) {
        if (coco_tape_pending(&gg->m) == op) return true;
        guest_fields(gg, 1);
    }
    return false;
}

static int requests(void) {
    boot_with_tape(&g, 64, ROM_COCO_BAS13, true);
    guest_type(&g, "10 PRINT \"KEPT\"\n");
    guest_type(&g, "CSAVE \"KEEP\"\n");
    CHECK(wait_ok(&g, 300), "CSAVE did not come back");
    static uint8_t saved[TAPE_CAP];
    uint32_t saved_len = g.m.tape.len;
    memcpy(saved, g_tape, saved_len);

    /* A name the tape lacks: to the end, rewound, to the end again. */
    guest_type(&g, "CLOAD \"OTHER\"\n");
    CHECK(run_to_request(&g, TAPE_END, 300), "no TAPE_END for a missing name");
    uint32_t req = g.m.tape.requests;
    coco_tape_rewind(&g.m);
    coco_tape_resume(&g.m);
    guest_fields(&g, 5);
    CHECK(coco_tape_pending(&g.m) == TAPE_END && g.m.tape.requests == req + 1,
          "rewound, the search did not reach the end again");
    /* Stalled: the CPU stands, time passes. */
    uint64_t ins = g.m.cpu.instructions;
    guest_fields(&g, 3);
    CHECK(g.m.cpu.instructions == ins, "the CPU ran while stalled");
    coco_tape_give_up(&g.m);
    CHECK(wait_ok(&g, 300), "giving up did not come back to OK");
    guest_type(&g, "LIST\n");
    CHECK(screen_has(&g.m, "10 PRINT \"KEPT\""), "the program was lost by the reset");

    /* The deck empty: CLOAD asks for the name. */
    coco_tape_eject(&g.m);
    guest_type(&g, "NEW\n");
    guest_type(&g, "CLOAD \"KEEP\"\n");
    CHECK(run_to_request(&g, TAPE_FIND, 100), "no TAPE_FIND with the deck empty");
    CHECK(g.m.tape.want_len == 4 && memcmp(g.m.tape.want, "KEEP", 4) == 0,
          "want '%.*s' (%u)", (int)g.m.tape.want_len, (const char *)g.m.tape.want, g.m.tape.want_len);
    CHECK(cas_has_name(saved, saved_len, g.m.tape.want, g.m.tape.want_len), "cas_has_name");
    memcpy(g_tape, saved, saved_len);
    coco_tape_insert(&g.m, saved_len);
    coco_tape_resume(&g.m);
    CHECK(wait_ok(&g, 300), "CLOAD did not finish once the tape was in");
    guest_type(&g, "LIST\n");
    CHECK(screen_has(&g.m, "10 PRINT \"KEPT\""), "the found tape did not load");
    return 0;
}

/* ---- held to the ROM's own routine ----------------------------------- */

#define CALL  0x7EFDu   /* JSR entry, then BRA * at RET */
#define RET   0x7F00u
#define BUF   0x3000u

static guest_t ref;
static const uint8_t *feed;
static uint32_t feed_len, feed_at;
static uint8_t written[TAPE_CAP];
static uint32_t written_len;

/* The ROM's BLKIN with its bit reading replaced: the sync search finds
 * $3C, and the byte reader returns the next byte as it would. */
static void hook_blkin(coco_t *m) {
    mc6809_t *c = &m->cpu;
    if (c->pc == 0xA712u) {
        c->a = CAS_SYNC;
        c->pc = 0xA715u;
    } else if (c->pc == 0xA749u) {
        c->a = feed_at < feed_len ? feed[feed_at++] : 0;
        bus_write(m, 0x82u, 0);
        c->cc = (uint8_t)((c->cc & ~(MC6809_CC_N | MC6809_CC_V)) | MC6809_CC_Z);
        c->pc = (uint16_t)((bus_read(m, c->s) << 8) | bus_read(m, (uint16_t)(c->s + 1u)));
        c->s = (uint16_t)(c->s + 2u);
    }
}

/* The byte writer's entry, watched: A is the byte. */
static void hook_writer(coco_t *m) {
    if (m->cpu.pc == 0xA82Au && written_len < TAPE_CAP) written[written_len++] = m->cpu.a;
}

/* A JSR to `entry` with ($7E) at BUF, on a booted machine, returning to
 * a BRA * at RET: a call as BASIC makes it, which arms the trap. */
static void call(coco_t *m, uint16_t entry) {
    const uint8_t code[5] = { 0xBD, (uint8_t)(entry >> 8), (uint8_t)entry, 0x20, 0xFE };
    for (unsigned i = 0; i < sizeof code; i++) bus_write(m, (uint16_t)(CALL + i), code[i]);
    bus_write(m, 0x7Eu, BUF >> 8);
    bus_write(m, 0x7Fu, BUF & 0xFFu);
    /* The variables a routine sets, set to something else first. */
    for (uint16_t v = 0x80u; v <= 0x85u; v++) bus_write(m, v, 0x5Au);
    m->cpu.pc = CALL;
}

static bool run_to_ret(coco_t *m, uint32_t max_steps) {
    for (uint32_t i = 0; i < max_steps; i++) {
        if (m->cpu.pc == RET) return true;
        if (m->step_hook) {
            coco_run(m, 1);
        } else {
            coco_run(m, 16);
        }
    }
    return m->cpu.pc == RET;
}

/* Everything but time and what passing time sets, B and $83 when
 * `skip_b`, the stack below S, and $84. */
static int same_state(const coco_t *a, const coco_t *b, bool skip_b, const char *what) {
    const mc6809_t *x = &a->cpu, *y = &b->cpu;
    CHECK(x->pc == y->pc && x->a == y->a && x->x == y->x && x->y == y->y && x->u == y->u &&
          x->s == y->s && x->dp == y->dp && x->cc == y->cc,
          "%s: trap PC %04X A %02X X %04X Y %04X U %04X S %04X DP %02X CC %02X; "
          "ROM PC %04X A %02X X %04X Y %04X U %04X S %04X DP %02X CC %02X", what,
          x->pc, x->a, x->x, x->y, x->u, x->s, x->dp, x->cc,
          y->pc, y->a, y->x, y->y, y->u, y->s, y->dp, y->cc);
    CHECK(skip_b || x->b == y->b, "%s: B %02X against %02X", what, x->b, y->b);
    unsigned diffs = 0;
    for (uint32_t i = 0; i < COCO_ADDR_SPACE; i++) {
        if (i >= (uint32_t)x->s - 16u && i < x->s) continue;
        if (i == 0x84u || (skip_b && i == 0x83u)) continue;
        if (a->ram[i] != b->ram[i]) {
            if (diffs++ < 5) fprintf(stderr, "    %s: RAM[%04X] %02X against %02X\n", what, i, a->ram[i], b->ram[i]);
        }
    }
    CHECK(diffs == 0, "%s: %u bytes of RAM differ", what, diffs);
    /* The registers; not the flags, which the field's edges set as time
     * passes, and the ROM's routine takes longer. */
    const mc6821_port_t *pa[4] = { &a->pia0.a, &a->pia0.b, &a->pia1.a, &a->pia1.b };
    const mc6821_port_t *pb[4] = { &b->pia0.a, &b->pia0.b, &b->pia1.a, &b->pia1.b };
    for (int i = 0; i < 4; i++)
        CHECK(pa[i]->out == pb[i]->out && pa[i]->ddr == pb[i]->ddr &&
              (pa[i]->cr & 0x3Fu) == (pb[i]->cr & 0x3Fu),
              "%s: PIA port %d: out %02X ddr %02X cr %02X against %02X %02X %02X", what, i,
              pa[i]->out, pa[i]->ddr, pa[i]->cr, pb[i]->out, pb[i]->ddr, pb[i]->cr);
    CHECK(a->sound_level == b->sound_level, "%s: sound bus %u against %u", what, a->sound_level, b->sound_level);
    return 0;
}

/* A machine at the prompt, and its copy with the trap off. */
static void pair(void) {
    boot_with_tape(&g, 64, ROM_COCO_BAS13, true);
    g.m.cfg.tape_trap = false;
    coco_copy(&ref.m, &g.m);       /* the copy's map has no trap */
    g.m.cfg.tape_trap = true;
    ref.m.step_hook = NULL;
}

static int held_blkin(const uint8_t *blk, uint32_t n, uint8_t poison, const char *what) {
    pair();
    /* A tape of leader and the one block; poison makes the store fail
     * by pointing ($7E) at ROM. */
    memset(g_tape, CAS_LEADER_BYTE, 16);
    memcpy(&g_tape[16], blk, n);
    coco_tape_insert(&g.m, 16u + n);
    call(&g.m, 0xA70Bu);
    call(&ref.m, 0xA70Bu);
    if (poison) {
        bus_write(&g.m, 0x7Eu, 0xB0); bus_write(&ref.m, 0x7Eu, 0xB0);
    }
    CHECK(run_to_ret(&g.m, 1000000), "%s: the trapped BLKIN did not return", what);
    feed = blk + 1;
    feed_len = n - 1;
    feed_at = 0;
    ref.m.step_hook = hook_blkin;
    CHECK(run_to_ret(&ref.m, 1000000), "%s: the ROM's BLKIN did not return", what);
    ref.m.step_hook = NULL;
    return same_state(&g.m, &ref.m, true, what);
}

static int held_writes(void) {
    /* BLKOUT of a full block. */
    pair();
    coco_tape_insert(&g.m, 0);
    for (unsigned i = 0; i < 255; i++) {
        bus_write(&g.m, (uint16_t)(BUF + i), (uint8_t)(i ^ 0xA5u));
        bus_write(&ref.m, (uint16_t)(BUF + i), (uint8_t)(i ^ 0xA5u));
    }
    for (int k = 0; k < 2; k++) {
        coco_t *m = k ? &ref.m : &g.m;
        bus_write(m, 0x7Cu, 0x01);
        bus_write(m, 0x7Du, 0xFF);
    }
    call(&g.m, 0xA7F4u);
    call(&ref.m, 0xA7F4u);
    CHECK(run_to_ret(&g.m, 1000000), "the trapped BLKOUT did not return");
    written_len = 0;
    ref.m.step_hook = hook_writer;
    CHECK(run_to_ret(&ref.m, 100000000), "the ROM's BLKOUT did not return");
    ref.m.step_hook = NULL;
    CHECK(written_len == g.m.tape.len && memcmp(written, g_tape, written_len) == 0,
          "BLKOUT: the ROM wrote %u bytes, the trap %u", written_len, g.m.tape.len);
    if (same_state(&g.m, &ref.m, false, "BLKOUT")) return 1;

    /* WRTLDR with a short leader, which runs CASON and its delay. */
    pair();
    coco_tape_insert(&g.m, 0);
    for (int k = 0; k < 2; k++) {
        coco_t *m = k ? &ref.m : &g.m;
        bus_write(m, 0x92u, 0x00);
        bus_write(m, 0x93u, 0x09);
    }
    call(&g.m, 0xA7D8u);
    call(&ref.m, 0xA7D8u);
    CHECK(run_to_ret(&g.m, 1000000), "the trapped WRTLDR did not return");
    written_len = 0;
    ref.m.step_hook = hook_writer;
    CHECK(run_to_ret(&ref.m, 100000000), "the ROM's WRTLDR did not return");
    ref.m.step_hook = NULL;
    CHECK(written_len == 9 && g.m.tape.len == 9 && memcmp(written, g_tape, 9) == 0,
          "WRTLDR: the ROM wrote %u bytes, the trap %u", written_len, g.m.tape.len);
    if (same_state(&g.m, &ref.m, false, "WRTLDR")) return 1;

    /* CASON alone, as MOTOR ON calls it. */
    pair();
    call(&g.m, 0xA7CAu);
    call(&ref.m, 0xA7CAu);
    CHECK(run_to_ret(&g.m, 1000000), "the trapped CASON did not return");
    CHECK(run_to_ret(&ref.m, 100000000), "the ROM's CASON did not return");
    return same_state(&g.m, &ref.m, false, "CASON");
}

static int held(void) {
    uint8_t blk[3 + 255 + 1];
    blk[0] = CAS_SYNC; blk[1] = CAS_BLOCK_DATA; blk[2] = 200;
    for (unsigned i = 0; i < 200; i++) blk[3 + i] = (uint8_t)(i * 11u + 3u);
    blk[203] = cas_checksum(blk[1], blk[2], &blk[3]);
    if (held_blkin(blk, 204, 0, "BLKIN, a good block")) return 1;
    CHECK(g.m.tape.blocks_in == 1 && bus_read(&g.m, 0x81u) == 0, "a good block: CSRERR %02X",
          bus_read(&g.m, 0x81u));
    blk[203] ^= 0x40u;
    if (held_blkin(blk, 204, 0, "BLKIN, a bad checksum")) return 1;
    CHECK(bus_read(&g.m, 0x81u) == 1, "a bad checksum: CSRERR %02X", bus_read(&g.m, 0x81u));
    blk[203] ^= 0x40u;
    if (held_blkin(blk, 204, 1, "BLKIN, into ROM")) return 1;
    CHECK(bus_read(&g.m, 0x81u) == 2, "into ROM: CSRERR %02X", bus_read(&g.m, 0x81u));
    uint8_t eof[4] = { CAS_SYNC, CAS_BLOCK_EOF, 0, CAS_BLOCK_EOF };
    if (held_blkin(eof, 4, 0, "BLKIN, an end block")) return 1;
    return held_writes();
}

/* ---- a ROM the trap does not know ------------------------------------ */

static int unknown_rom(void) {
    static uint8_t img[COCO_ROM_HALF];
    memcpy(img, guest_rom_image(ROM_COCO_BAS13), sizeof img);
    img[0x1F00] ^= 0x01u;   /* a byte of no routine: the image is another */
    coco_config_t cfg;
    coco_config_default(&cfg);
    coco_init(&g.m, &cfg);
    coco_load_rom(&g.m, 0xA000u, img, sizeof img);
    coco_power_on(&g.m);
    keymatrix_init(&g.k);
    coco_tape_attach(&g.m, g_tape, TAPE_CAP);
    CHECK(!g.m.tape.rom_ok, "an altered image was taken for Color BASIC 1.3");
    CHECK(g.m.page[0xA7].read != NULL, "page $A7 is slow with no trap");
    guest_type(&g, "MOTOR ON\n");
    CHECK(g.m.page[0xA7].read != NULL && g.m.tape.hot == 0, "CASON armed the trap");
    guest_boot(&g);
    guest_type(&g, "CSAVE \"X\"\n");
    guest_fields(&g, 120);
    CHECK(g.m.tape.len == 0 && g.m.tape.blocks_out == 0, "an unknown ROM was trapped");
    return 0;
}

/* ---- the media directory --------------------------------------------- */

static int media(void) {
    const char *env = getenv("PICO_COCO_MEDIA");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/tapes", env ? env : PICO_COCO_SOURCE_DIR "/media");
    DIR *d = opendir(dir);
    if (!d) {
        printf("media: no %s, skipped\n", dir);
        return 0;
    }
    struct dirent *e;
    unsigned tried = 0;
    while ((e = readdir(d)) != NULL) {
        size_t nl = strlen(e->d_name);
        if (nl < 4 || (strcasecmp(e->d_name + nl - 4, ".cas") != 0)) continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        uint32_t n = (uint32_t)fread(g_tape, 1, TAPE_CAP, f);
        fclose(f);
        boot_with_tape(&g, 64, ROM_COCO_BAS13, true);
        coco_tape_insert(&g.m, n);
        /* The first namefile says which command loads it. */
        cas_block_t b;
        uint32_t at = 0;
        bool code = false;
        while (cas_next_block(g_tape, n, at, &b)) {
            if (b.type == CAS_BLOCK_NAME) { code = g_tape[b.data + CAS_NF_TYPE] == 2; break; }
            at = b.next;
        }
        guest_type(&g, code ? "CLOADM\n" : "CLOAD\n");
        bool ok = wait_ok(&g, 1200);
        tape_op_t op = coco_tape_pending(&g.m);
        bool err = screen_has(&g.m, "IO ERROR") || screen_has(&g.m, "?IO");
        printf("media: %-60s %s, %u blocks, %s%s, PC %04X\n", e->d_name, code ? "CLOADM" : "CLOAD ",
               g.m.tape.blocks_in, ok ? "OK" : op == TAPE_END ? "ran to the end" : "no OK (started itself?)",
               err ? ", I/O ERROR" : "", g.m.cpu.pc);
        CHECK(!err, "%s: I/O ERROR", e->d_name);
        CHECK(ok || op == TAPE_NONE, "%s: stalled with request %d", e->d_name, (int)op);
        tried++;
    }
    closedir(d);
    printf("media: %u tapes\n", tried);
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11)) {
        printf("skip: Color BASIC 1.3 and Extended 1.1 not in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (round_trip(64, ROM_COCO_BAS13, true)) return 1;
    if (guest_have_rom(ROM_COCO_BAS11) && round_trip(4, ROM_COCO_BAS11, false)) return 1;
    if (machine_code()) return 1;
    if (requests()) return 1;
    if (held()) return 1;
    if (unknown_rom()) return 1;
    if (media()) return 1;
    TEST_DONE();
}
