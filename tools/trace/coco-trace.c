/* coco-trace.c — this core's half of the trace diff against XRoar
 * (design.md §15.2, EL §11.4).
 *
 *   coco-trace [-bas bas13|bas11] [-ram K] [-ext] [-fields N] [-type STRING]
 *              [-cart rsdos|mpi] [-mpi-slot N] [-mpi-load-cart N=rsdos] > ours.trace
 *
 * The cartridge options are XRoar's own, so trace-coco.sh gives the same
 * to both (design.md §12.8): the disk controller in the port, or a
 * Multi-Pak with its switch at slot N (0 to 3) and the disk controller in
 * a slot. Disk BASIC needs Extended (-ext).
 *
 * Boots a CoCo on the real ROMs (found as test/host/guest.c finds them)
 * and prints a line after each instruction: its address and every
 * register after it, in the form XRoar's -trace prints, so that
 * tools/trace/trace-diff.py can hold the two together. Interrupt entries
 * and wait cycles print nothing, as XRoar's instruction lines do not.
 *
 * Typing goes in as XRoar's -type puts it in, so the two take the same
 * path through the ROM (give ENTER as a carriage return): while text is queued, reaching POLCAT returns the
 * next character in A with Z clear, and reaching the 1 ms delay returns at
 * once, each as an RTS that takes no time, at a boundary where no
 * interrupt is due (XRoar's auto_kbd.c and breakpoints, read 2026-11-14).
 * The matrix path is test_boot's to test, not this tool's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"

/* Each BASIC's entry points that XRoar's -type breaks at, from its symbol
 * table (§18): POLCAT returns a character, and the 1 ms delay returns
 * at once. */
typedef struct {
    uint16_t polcat, delay_1ms;
} intercepts_t;

static const intercepts_t bas13_at = { 0xA1CBu, 0xA7D3u };
static const intercepts_t bas11_at = { 0xA1C1u, 0xA7D3u };

typedef struct {
    uint16_t pc;          /* PC before the step that is about to run */
    uint64_t insns;
    const char *type;     /* what is still to type, NULL when done   */
    intercepts_t at;
} trace_t;

static guest_t g;
static trace_t t;

static uint16_t rd(coco_t *m, uint16_t a) {
    return (uint16_t)((coco_slow_read(m, a) << 8) | coco_slow_read(m, (uint16_t)(a + 1u)));
}

static void rts(coco_t *m) {
    m->cpu.pc = rd(m, m->cpu.s);
    m->cpu.s = (uint16_t)(m->cpu.s + 2u);
}

static void hook(coco_t *m) {
    mc6809_t *c = &m->cpu;
    if (c->instructions != t.insns) {
        t.insns = c->instructions;
        printf("%04x| cc=%02x a=%02x b=%02x dp=%02x x=%04x y=%04x u=%04x s=%04x\n",
               t.pc, c->cc, c->a, c->b, c->dp, c->x, c->y, c->u, c->s);
    }
    /* XRoar's breakpoints, while typing. */
    /* A breakpoint fires only before a fetch, as XRoar checks them; the
     * core's own decision says whether one comes next (mc6809.h). */
    while (t.type && mc6809_fetches_next(c)) {
        if (c->pc == t.at.polcat) {
            c->a = 0;
            c->cc |= MC6809_CC_Z;
            if (*t.type) {
                c->a = (uint8_t)*t.type;   /* as XRoar: no translation */
                c->cc &= (uint8_t)~MC6809_CC_Z;
                t.type++;
            }
            if (!*t.type) t.type = NULL;
            rts(m);
        } else if (c->pc == t.at.delay_1ms) {
            rts(m);
        } else {
            break;
        }
    }
    t.pc = c->pc;
}

int main(int argc, char **argv) {
    unsigned ram_k = 0, fields = 300, mpi_slot = 0;
    bool ext = false, bas11 = false;
    const char *type = NULL, *cart = "";
    cart_kind_t kinds[COCO_MPI_SLOTS] = { CART_NONE, CART_NONE, CART_NONE, CART_NONE };
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram_k = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ext")) ext = true;
        else if (!strcmp(argv[i], "-fields") && i + 1 < argc) fields = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-type") && i + 1 < argc) type = argv[++i];
        else if (!strcmp(argv[i], "-bas") && i + 1 < argc) bas11 = !strcmp(argv[++i], "bas11");
        else if (!strcmp(argv[i], "-cart") && i + 1 < argc) cart = argv[++i];
        else if (!strcmp(argv[i], "-mpi-slot") && i + 1 < argc) mpi_slot = (unsigned)atoi(argv[++i]) & 3u;
        else if (!strcmp(argv[i], "-mpi-load-cart") && i + 1 < argc &&
                 argv[i + 1][0] >= '0' && argv[i + 1][0] <= '3' && argv[i + 1][1] == '=' &&
                 !strcmp(argv[i + 1] + 2, "rsdos"))
            kinds[argv[++i][0] - '0'] = CART_RSDOS;
        else {
            fprintf(stderr, "usage: %s [-bas bas13|bas11] [-ram K] [-ext] [-fields N] [-type STRING]"
                            " [-cart rsdos|mpi] [-mpi-slot N] [-mpi-load-cart N=rsdos]\n", argv[0]);
            return 2;
        }
    }
    const char *dir;
    guest_find_roms(&dir);
    rom_id_t basic = bas11 ? ROM_COCO_BAS11 : ROM_COCO_BAS13;
    if (!guest_have_rom(basic)) {
        fprintf(stderr, "coco-trace: no %s in %s\n", romset_images[basic].file, dir);
        return 1;
    }
    if (!ram_k) ram_k = 64;
    bool disk = !strcmp(cart, "rsdos"), mpi = !strcmp(cart, "mpi");
    if ((disk || mpi) && (bas11 || !ext || !guest_have_rom(ROM_COCO_EXTBAS11) ||
                          !guest_have_rom(ROM_COCO_DISK11))) {
        fprintf(stderr, "coco-trace: a cartridge needs -ext, bas13 and disk11.rom\n");
        return 1;
    }
    if (disk) guest_build_disk(&g, ram_k);
    else if (mpi) guest_build_mpi(&g, ram_k, kinds, mpi_slot, NULL, NULL);
    else guest_build_as(&g, ram_k, basic, ext);
    t.at = bas11 ? bas11_at : bas13_at;
    t.pc = g.m.cpu.pc;
    t.insns = g.m.cpu.instructions;
    t.type = (type && *type) ? type : NULL;
    g.m.step_hook = hook;
    for (unsigned f = 0; f < fields; f++) coco_run_field(&g.m);
    fflush(stdout);
    for (int r = 0; r < 16; r++) fprintf(stderr, "|%-32s|\n", guest_row(&g.m, r));
    return 0;
}
