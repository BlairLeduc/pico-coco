/* coco-corpus.c — the archive run: tapes and cartridges loaded and run on
 * the host, one line each (design.md §15.4), adapted from pico-oric's
 * oric-corpus.
 *
 *   coco-corpus [-f FIELDS] [-s DIR] [-ram 4|16|32|64] [-basic bas13|bas11] [-noext] IMAGE...
 *
 * An IMAGE is a .cas, a directory of a title's tapes (the deck starts
 * with the first by name), or a .ccc or .rom cartridge. Each runs on a
 * machine of its own, Extended Color BASIC 1.1 on Color BASIC 1.3 in
 * 64 KB unless told otherwise (-basic bas11 for the Color BASIC of the
 * first machines, under which some titles were written), the ROMs found as the host tests find
 * them (test/host/guest.h).
 *
 * A tape is put in the deck and loaded as its first file asks, CLOAD for
 * BASIC and CLOADM for machine code, served by the trap (tape.h) as the
 * firmware's tapeio serves it: a read past the tape's end rewinds it
 * once, and a second gives up, pressing the reset button. When the tape
 * has gone quiet and BASIC is at OK, the last file is started, RUN or
 * EXEC. A cartridge is plugged in with its CART line on the Q clock and
 * the machine powered on, which starts it (§12.2). Half way through,
 * Space is pressed, for the titles that wait for a key.
 *
 * A field that trapped an undocumented opcode or postbyte (D12), or
 * changed the video bits during its active lines (§9.4), is run again
 * from its start a step at a time, from a copy taken before it: every
 * undocumented one is counted by opcode, with the first PC each was seen
 * at and the instructions before the run's first; every change of the
 * video bits on an active line is noted by which bits changed (CSS, the
 * VDG's mode, the SAM's V or F). The copy's run is the same instructions
 * as the field's, checked by its cycle count.
 *
 * One line per image on stdout, tab-separated:
 *   name  kind  outcome  blocks  largest  undoc  opcodes  split  changed
 *   lines  mode  note  changes  dropped
 *   joyr  joyl  cols
 * kind is cas, cas.gz or ccc; largest the largest file's data bytes on
 * the tape, or the cartridge's size; split the fields with a change on
 * an active line; changed the bits that changed there (css, mode, samv,
 * samf); lines the first and last active line a change fell on; mode
 * the VDG's at the end; changes the most a field's picture was drawn
 * by, and dropped those past COCO_SPLIT_MAX in a field (§9.4); joyr and
 * joyl the reads of each stick's comparator after the DAC moved, and
 * cols the keyboard's columns, as two hex digits, that were driven low
 * alone and the rows read sixteen times or more (§11.3's survey: what a
 * title reads for itself, not through BASIC). With -s, the last frame as DIR/<name>.ppm and
 * the text screen, with the lead-in to the first undocumented opcode,
 * as DIR/<name>.txt.
 */

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cas.h"
#include "frame_ppm.h"
#include "guest.h"
#include "keymatrix.h"
#include "mc6847.h"

#define DECK_MAX (1u << 20)

static guest_t s_booted, g;
static coco_t  s_field_start, s_replay;
static uint8_t s_deck[DECK_MAX];
static uint8_t s_cart[COCO_CART_SIZE + 1];
static long    s_field;

/* ---- what is noted ------------------------------------------------------ */

#define LEAD_IN 24u

typedef struct {
    uint16_t pc;
    uint8_t  op[4];
    uint8_t  a, b, dp, cc;
    uint16_t x, y, u, s;
} step_t;

/* Undocumented ones by opcode ($00-$FF, $10xx at 256+, $11xx at 512+). */
#define OPS 768u

typedef struct {
    uint32_t count[OPS];
    uint16_t first_pc[OPS];
    uint32_t total;
    long     first_field;
    step_t   lead_in[LEAD_IN];
    unsigned lead_len;
    /* §9.4 */
    uint32_t split;
    unsigned changed;            /* CH_ bits                             */
    int      line_lo, line_hi;
    unsigned regions;            /* the most in a field                  */
    /* Replays whose cycle count did not meet the field's. */
    unsigned replay_bad;
} run_log_t;

#define CH_CSS  1u
#define CH_MODE 2u
#define CH_SAMV 4u
#define CH_SAMF 8u

static run_log_t s_rl;

typedef struct {
    unsigned requests, rewinds;
    bool     gave_up;
    long     last_field;         /* the last field a block moved at      */
    uint32_t blocks_seen;
} tape_log_t;

static tape_log_t s_tl;

/* ---- the replay ----------------------------------------------------------- */

static step_t   s_ring[LEAD_IN];
static unsigned s_ring_n;
static uint32_t s_undoc_seen, s_bits_seen;

static uint32_t video_bits(const coco_t *m) {
    const mc6821_port_t *b = &m->pia1.b;
    return ((uint32_t)mc6847_mode_from_pb((uint8_t)(b->out & b->ddr)) << 16) |
           (m->sam.reg & (SAM_V_MASK | SAM_F_MASK));
}

/* A byte as the CPU would fetch it, without an access's side effects:
 * through the page table, or $FF where the slow path would answer. */
static uint8_t peek(const coco_t *m, uint16_t a) {
    const page_t *p = &m->page[a >> 8];
    return p->read ? p->read[a & 0xFFu] : 0xFFu;
}

static unsigned op_index(uint16_t op) {
    if ((op >> 8) == 0x10u) return 256u + (op & 0xFFu);
    if ((op >> 8) == 0x11u) return 512u + (op & 0xFFu);
    return op & 0xFFu;
}

/* The state before the next step, for the lead-in. */
static void note_step(const coco_t *m) {
    const mc6809_t *c = &m->cpu;
    step_t *st = &s_ring[s_ring_n++ % LEAD_IN];
    st->pc = c->pc;
    for (unsigned k = 0; k < 4; k++) st->op[k] = peek(m, (uint16_t)(c->pc + k));
    st->a = c->a; st->b = c->b; st->dp = c->dp; st->cc = c->cc;
    st->x = c->x; st->y = c->y; st->u = c->u; st->s = c->s;
}

static void replay_hook(coco_t *m) {
    const mc6809_t *c = &m->cpu;
    if (c->undoc_count != s_undoc_seen) {
        s_undoc_seen = c->undoc_count;
        unsigned i = op_index(c->undoc_op);
        if (!s_rl.count[i]) s_rl.first_pc[i] = c->undoc_pc;
        s_rl.count[i]++;
        s_rl.total++;
        if (s_rl.first_field < 0) {
            s_rl.first_field = s_field;
            s_rl.lead_len = s_ring_n < LEAD_IN ? s_ring_n : LEAD_IN;
            for (unsigned k = 0; k < s_rl.lead_len; k++)
                s_rl.lead_in[k] = s_ring[(s_ring_n - s_rl.lead_len + k) % LEAD_IN];
        }
    }
    uint32_t bits = video_bits(m);
    if (bits != s_bits_seen) {
        uint32_t d = bits ^ s_bits_seen;
        s_bits_seen = bits;
        uint64_t line = (coco_ticks(m) - m->field_start) / COCO_LINE_TICKS;
        if (line >= COCO_ACTIVE_LINE && line < COCO_ACTIVE_LINE + COCO_PIC_H) {
            int l = (int)(line - COCO_ACTIVE_LINE);
            if (d & ((uint32_t)VDG_CSS << 16)) s_rl.changed |= CH_CSS;
            if (d & ((uint32_t)(VDG_AG | VDG_GM_MASK) << 16)) s_rl.changed |= CH_MODE;
            if (d & SAM_V_MASK) s_rl.changed |= CH_SAMV;
            if (d & SAM_F_MASK) s_rl.changed |= CH_SAMF;
            if (s_rl.line_lo < 0 || l < s_rl.line_lo) s_rl.line_lo = l;
            if (l > s_rl.line_hi) s_rl.line_hi = l;
        }
    }
    note_step(m);
}

static void replay(void) {
    coco_copy(&s_replay, &s_field_start);
    s_undoc_seen = s_replay.cpu.undoc_count;
    s_bits_seen = s_replay.video_bits;
    s_ring_n = 0;
    note_step(&s_replay);
    s_replay.step_hook = replay_hook;
    coco_run_field(&s_replay);
    if (s_replay.cpu.cycles != g.m.cpu.cycles) s_rl.replay_bad++;
}

/* ---- running ------------------------------------------------------------- */

static void serve(coco_t *m) {
    tape_op_t op = coco_tape_pending(m);
    if (op == TAPE_NONE) return;
    s_tl.requests++;
    if (op == TAPE_END && s_tl.rewinds == 0 && m->tape.inserted) {
        s_tl.rewinds++;
        coco_tape_rewind(m);
        coco_tape_resume(m);
        return;
    }
    s_tl.gave_up = true;
    coco_tape_give_up(m);
}

static void field(void) {
    keymatrix_field(&g.k, &g.m);
    coco_copy(&s_field_start, &g.m);
    uint32_t undoc = g.m.cpu.undoc_count, split = g.m.split_fields;
    coco_run_field(&g.m);
    static int16_t pcm[4096];
    while (coco_audio_drain(&g.m, pcm, sizeof pcm / sizeof pcm[0]) == sizeof pcm / sizeof pcm[0]) {}
    if (g.m.cpu.undoc_count != undoc || g.m.split_fields != split) replay();
    if (g.m.n_split_last > s_rl.regions) s_rl.regions = g.m.n_split_last;
    uint32_t blocks = g.m.tape.blocks_in + g.m.tape.blocks_out;
    if (blocks != s_tl.blocks_seen) {
        s_tl.blocks_seen = blocks;
        s_tl.last_field = s_field;
    }
    serve(&g.m);
    s_field++;
}

static void settle(void) {
    for (int f = 0; f < 200 && !keymatrix_idle(&g.k); f++) field();
    for (int f = 0; f < 4; f++) field();
}

/* As guest_type: letters in lower case, which types the same capitals
 * with fewer events; '\n' is ENTER. */
static void type(const char *s) {
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 'a');
        picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
        unsigned n = keymap_picocalc_text(ch, ev);
        for (unsigned i = 0; i < n; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
        settle();
    }
}

static bool screen_has(const char *s) {
    for (int r = 0; r < 16; r++)
        if (strstr(guest_row(&g.m, r), s)) return true;
    return false;
}

/* The bottom-most text is OK, BASIC's prompt, and no request stands. */
static bool at_ok(void) {
    if (coco_tape_pending(&g.m) != TAPE_NONE) return false;
    for (int r = 15; r >= 0; r--) {
        const char *s = guest_row(&g.m, r);
        if (s[0]) return strcmp(s, "OK") == 0;
    }
    return false;
}

/* ---- images --------------------------------------------------------------- */

typedef enum { IMG_CAS, IMG_CAS_GZ, IMG_CCC } img_kind_t;

static uint32_t read_file(const char *path, uint8_t *buf, uint32_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t n = (uint32_t)fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

/* A directory's tapes, joined in name order into the deck: a title in
 * parts asks for each by name, and the trap finds the next file where the
 * tape stands. */
/* strdup is POSIX, which glibc hides under -std=c11 (CI, 2026-11-16). */
static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static uint32_t load_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    char *names[64];
    unsigned n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 64) {
        size_t l = strlen(e->d_name);
        if (l > 4 && strcasecmp(e->d_name + l - 4, ".cas") == 0) names[n++] = dup_str(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof names[0], cmp_str);
    uint32_t len = 0;
    for (unsigned i = 0; i < n; i++) {
        char p[2048];
        snprintf(p, sizeof p, "%s/%s", path, names[i]);
        len += read_file(p, s_deck + len, DECK_MAX - len);
        free(names[i]);
    }
    return len;
}

/* The tape's first namefile, and its largest file's data bytes. */
typedef struct {
    bool     named;
    uint8_t  type;           /* 0 BASIC, 1 data, 2 machine code      */
    uint32_t blocks, largest, files;
    bool     bad_sum;
} tape_scan_t;

static void scan_tape(const uint8_t *img, uint32_t len, tape_scan_t *t) {
    memset(t, 0, sizeof *t);
    cas_block_t b;
    uint32_t at = 0, cur = 0;
    while (cas_next_block(img, len, at, &b)) {
        t->blocks++;
        if (!b.sum_ok) t->bad_sum = true;
        if (b.type == CAS_BLOCK_NAME) {
            if (!t->named && b.len >= CAS_NAMEFILE_LEN) {
                t->named = true;
                t->type = img[b.data + CAS_NF_TYPE];
            }
            t->files++;
            cur = 0;
        } else if (b.type == CAS_BLOCK_DATA) {
            cur += b.len;
            if (cur > t->largest) t->largest = cur;
        }
        at = b.next;
    }
}

static const char *base_name(const char *path) {
    static char b[1024];
    snprintf(b, sizeof b, "%s", path);
    size_t n = strlen(b);
    while (n > 1 && b[n - 1] == '/') b[--n] = 0;
    const char *dot = strrchr(b, '.');
    const char *slash = strrchr(b, '/');
    if (dot && (!slash || dot > slash) &&
        (!strcasecmp(dot, ".cas") || !strcasecmp(dot, ".ccc") || !strcasecmp(dot, ".rom")))
        b[dot - b] = 0;
    return slash ? strrchr(b, '/') + 1 : b;
}

/* ---- the report ----------------------------------------------------------- */

static void shot(const char *dir, const char *name) {
    static coco_frame_t fr;
    static mc6847_t vdg;
    static bool vdg_ready;
    static uint8_t rgb[FRAME_RGB_BYTES];
    if (!vdg_ready) {
        mc6847_init(&vdg);
        vdg_ready = true;
    }
    char path[2048];
    coco_video_snapshot(&g.m, &fr);
    frame_rgb(&vdg, &fr, rgb);
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    frame_write_ppm(path, rgb);
    snprintf(path, sizeof path, "%s/%s.txt", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    guest_dump(&g.m, f);
    fprintf(f, "\nPC $%04X at field %ld\n", g.m.cpu.pc, s_field);
    fprintf(f, "\nThe frame: VDG mode $%02X, SAM V%u, from $%04X, drawn by %u change(s) of the "
               "last field's (§9.4)\n", fr.mode, fr.sam_v, fr.base, fr.splits);
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        const coco_line_t *h = &fr.how[y];
        if (y && !h->x[0] && h->mode == fr.how[y - 1u].mode && !fr.how[y - 1u].x[0]) continue;
        fprintf(f, "  line %3u: $%02X", y, h->mode);
        for (unsigned k = 0; k < COCO_LINE_SPLITS && h->x[k]; k++)
            fprintf(f, ", $%02X from pixel %u", h->m[k], h->x[k]);
        fprintf(f, "\n");
    }
    if (s_rl.first_field >= 0) {
        fprintf(f, "\nThe first undocumented opcode, at field %ld, and the steps before it "
                   "(PC, bytes, A B DP CC X Y U S before):\n", s_rl.first_field);
        for (unsigned i = 0; i < s_rl.lead_len; i++) {
            const step_t *st = &s_rl.lead_in[i];
            fprintf(f, "  %04X  %02X %02X %02X %02X   %02X %02X %02X %02X %04X %04X %04X %04X\n",
                    st->pc, st->op[0], st->op[1], st->op[2], st->op[3], st->a, st->b, st->dp,
                    st->cc, st->x, st->y, st->u, st->s);
        }
    }
    fclose(f);
}

static void changed_str(char *out, size_t cap) {
    static const char *const names[] = { "css", "mode", "samv", "samf" };
    size_t o = 0;
    out[0] = 0;
    for (unsigned i = 0; i < 4; i++)
        if (s_rl.changed >> i & 1u)
            o += (size_t)snprintf(out + o, cap - o, "%s%s", o ? "," : "", names[i]);
    if (!o) snprintf(out, cap, "-");
}

static void ops_str(char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    for (unsigned i = 0; i < OPS && o < cap; i++) {
        if (!s_rl.count[i]) continue;
        char op[8];
        if (i >= 512u) snprintf(op, sizeof op, "11%02X", i - 512u);
        else if (i >= 256u) snprintf(op, sizeof op, "10%02X", i - 256u);
        else snprintf(op, sizeof op, "%02X", i);
        o += (size_t)snprintf(out + o, cap - o, "%s$%s:%u@$%04X", o ? "," : "", op,
                              (unsigned)s_rl.count[i], (unsigned)s_rl.first_pc[i]);
    }
    if (!out[0]) snprintf(out, cap, "-");
}

static int run_one(const char *path, long fields, const char *shots) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "%s: cannot open\n", path);
        return 1;
    }
    size_t pl = strlen(path);
    bool cart = !S_ISDIR(st.st_mode) && pl > 4 &&
                (!strcasecmp(path + pl - 4, ".ccc") || !strcasecmp(path + pl - 4, ".rom"));
    img_kind_t kind = cart ? IMG_CCC : IMG_CAS;
    uint32_t len = 0, largest = 0, blocks = 0;
    tape_scan_t ts = { 0 };
    char note[96] = "-";

    memset(&s_rl, 0, sizeof s_rl);
    memset(&s_tl, 0, sizeof s_tl);
    s_rl.first_field = -1;
    s_rl.line_lo = s_rl.line_hi = -1;
    s_tl.last_field = -1;
    s_field = 0;

    const char *outcome = "?";
    if (cart) {
        len = read_file(path, s_cart, sizeof s_cart);
        largest = len;
        if (len == 0 || len > COCO_CART_SIZE) {
            snprintf(note, sizeof note, "%u bytes: not a 16 KB cartridge", len);
            printf("%s\tccc\trefused\t0\t%u\t0\t-\t0\t-\t-\t-\t%s\n", base_name(path), len, note);
            fflush(stdout);
            return 0;
        }
        coco_copy(&g.m, &s_booted.m);
        g.k = s_booted.k;
        coco_load_rom(&g.m, 0xC000u, s_cart, len);
        coco_cart_autostart(&g.m, true);
        coco_power_on(&g.m);
        keymatrix_init(&g.k);
    } else {
        len = S_ISDIR(st.st_mode) ? load_dir(path) : read_file(path, s_deck, DECK_MAX);
        if (len >= 2 && s_deck[0] == 0x1Fu && s_deck[1] == 0x8Bu) {
            kind = IMG_CAS_GZ;
            printf("%s\tcas.gz\trefused\t0\t%u\t0\t-\t0\t-\t-\t-\tgzipped\n", base_name(path), len);
            fflush(stdout);
            return 0;
        }
        scan_tape(s_deck, len, &ts);
        largest = ts.largest;
        blocks = ts.blocks;
        if (len == DECK_MAX) snprintf(note, sizeof note, "tape over %u bytes", DECK_MAX);
        else if (ts.bad_sum) snprintf(note, sizeof note, "a bad checksum on the tape");
        coco_copy(&g.m, &s_booted.m);
        g.k = s_booted.k;
        coco_tape_attach(&g.m, s_deck, DECK_MAX);
        coco_tape_insert(&g.m, len);
        type(ts.named && ts.type == 2 ? "CLOADM\n" : "CLOAD\n");
    }

    bool started = cart, spaced = false;
    while (s_field < fields) {
        field();
        /* Quiet for two seconds after the last block, at OK: start the
         * last file if it did not start itself. */
        if (!started && s_tl.last_field >= 0 && s_field - s_tl.last_field > 120 && at_ok()) {
            started = true;
            bool io = screen_has("?IO ERROR") || screen_has("IO ERROR");
            if (!io && !s_tl.gave_up) type(ts.named && ts.type == 2 ? "EXEC\n" : "RUN\n");
        }
        if (!spaced && s_field >= fields / 2) {
            spaced = true;
            picocalc_event_t ev[COCO_KEY_TEXT_EVENTS];
            unsigned n = keymap_picocalc_text(' ', ev);
            for (unsigned i = 0; i < n; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
            settle();
        }
    }

    if (cart) {
        outcome = at_ok() ? "at-ok" : "started";
    } else {
        bool io = screen_has("IO ERROR");
        outcome = g.m.tape.blocks_in == 0 ? "no-load"
                : io                      ? "io-error"
                : s_tl.gave_up            ? "gave-up"
                : at_ok()                 ? "at-ok"
                : "loaded";
    }
    if (s_rl.replay_bad && note[0] == '-')
        snprintf(note, sizeof note, "%u replays out of step", s_rl.replay_bad);
    char ops[OPS * 16], changed[32], lines[16] = "-";
    ops_str(ops, sizeof ops);
    changed_str(changed, sizeof changed);
    if (s_rl.line_lo >= 0) snprintf(lines, sizeof lines, "%d-%d", s_rl.line_lo, s_rl.line_hi);
    uint8_t mode = coco_vdg_mode(&g.m);
    char mstr[16];
    snprintf(mstr, sizeof mstr, "%s%u%s", (mode & VDG_AG) ? "g" : "a",
             (unsigned)((mode & VDG_GM_MASK) >> VDG_GM_SHIFT), (mode & VDG_CSS) ? "c" : "");
    unsigned cols = 0;
    for (unsigned c = 0; c < COCO_KEY_COLS; c++)
        if (g.m.key_col_reads[c] >= 16u) cols |= 1u << c;
    const char *name = base_name(path);
    printf("%s\t%s\t%s\t%u\t%u\t%u\t%s\t%u\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%u\t%02X\n", name,
           kind == IMG_CCC ? "ccc" : "cas", outcome, cart ? 0u : blocks, largest,
           (unsigned)s_rl.total, ops, (unsigned)g.m.split_fields, changed, lines, mstr, note, s_rl.regions,
           (unsigned)g.m.split_dropped, (unsigned)g.m.joy_samples[COCO_JOY_RIGHT],
           (unsigned)g.m.joy_samples[COCO_JOY_LEFT], cols);
    fflush(stdout);
    if (shots) shot(shots, name);
    return 0;
}

int main(int argc, char **argv) {
    long fields = 3600;
    unsigned ram = 64;
    bool ext = true;
    rom_id_t basic = ROM_COCO_BAS13;
    const char *shots = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) fields = atol(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-noext")) ext = false;
        else if (!strcmp(argv[i], "-basic") && i + 1 < argc)
            basic = !strcmp(argv[++i], "bas11") ? ROM_COCO_BAS11 : ROM_COCO_BAS13;
        else {
            fprintf(stderr, "usage: coco-corpus [-f FIELDS] [-s DIR] [-ram 4|16|32|64] "
                            "[-basic bas13|bas11] [-noext] IMAGE...\n");
            return 2;
        }
    }
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(basic) ||
        (ext && !guest_have_rom(ROM_COCO_EXTBAS11))) {
        fprintf(stderr, "coco-corpus: the ROMs are not in %s\n", dir);
        return 77;
    }
    guest_build_as(&s_booted, ram, basic, ext);
    if (!guest_boot(&s_booted)) {
        fprintf(stderr, "coco-corpus: BASIC did not reach OK\n");
        return 1;
    }
    int bad = 0;
    for (; i < argc; i++) bad |= run_one(argv[i], fields, shots);
    return bad;
}
