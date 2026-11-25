/* snap_util.h — what test_snapshot and test_snapshot_rom share: a
 * stream in memory, and the comparison of two machines (design.md
 * §12.5; EL §8.5). From pico-oric's, with the CoCo's fields.
 *
 * The comparison reads coco_t, not the format, so a field the format
 * leaves out shows as a difference rather than as two equal omissions.
 * It covers every field that is machine state: what is not (the port's
 * pcm, the deck's buffer and place, which are the port's media, the
 * counters for the heartbeat, the keys) is named where it is skipped.
 */
#ifndef PICO_COCO_TEST_SNAP_UTIL_H
#define PICO_COCO_TEST_SNAP_UTIL_H

#include <stdio.h>
#include <string.h>

#include "coco.h"
#include "snapshot.h"

typedef struct {
    uint8_t buf[SNAP_FILE_LEN + 16u];
    size_t  len, pos;
    size_t  fail_at;     /* fail the call that would cross this; 0 never */
} mem_t;

static inline bool mem_write(void *ctx, const uint8_t *src, size_t n) {
    mem_t *s = ctx;
    if (s->len + n > sizeof s->buf) return false;
    if (s->fail_at && s->len + n > s->fail_at) return false;
    memcpy(s->buf + s->len, src, n);
    s->len += n;
    return true;
}

static inline bool mem_read(void *ctx, uint8_t *dst, size_t n) {
    mem_t *s = ctx;
    if (s->pos + n > s->len) return false;
    if (s->fail_at && s->pos + n > s->fail_at) return false;
    memcpy(dst, s->buf + s->pos, n);
    s->pos += n;
    return true;
}

static inline snap_status_t mem_save(mem_t *s, const coco_t *m, const snap_media_t *md) {
    s->len = 0;
    return snapshot_save(m, md, mem_write, s);
}

static inline snap_status_t mem_check(mem_t *s, const coco_t *m, snap_info_t *info,
                                      snap_media_t *md) {
    s->pos = 0;
    return snapshot_check(m, mem_read, s, info, md);
}

static inline snap_status_t mem_load(mem_t *s, coco_t *m) {
    s->pos = 0;
    return snapshot_load(m, mem_read, s);
}

/* The payload's CRC again, after a test has changed a byte of it on
 * purpose (snapshot.h's header). */
static inline void mem_recrc(mem_t *s) {
    uint32_t crc = snapshot_crc32(0, s->buf + SNAP_HEADER_LEN, s->len - SNAP_HEADER_LEN);
    for (unsigned i = 0; i < 4; i++) s->buf[16 + i] = (uint8_t)(crc >> (8 * i));
}

/* The sound, compared sample for sample: a restore starts the sample
 * grid at the restored tick (coco_restored), so the original's grid is
 * started there too, after draining what it had made, and the DC
 * blocker is off in both, its state being the port's and not the
 * machine's. */
#define SNAP_SOUND_MAX (160u * 800u)

static inline void snap_sound_start(coco_t *m) {
    int16_t scratch[COCO_AUDIO_BUF_LEN];
    pcm_advance(&m->pcm, (uint32_t)coco_ticks(m));
    while (coco_audio_drain(m, scratch, COCO_AUDIO_BUF_LEN)) {}
    pcm_restart(&m->pcm, (uint32_t)coco_ticks(m));
}

/* After a field: what it made, onto out at *n. */
static inline void snap_sound_take(coco_t *m, int16_t *out, size_t *n) {
    int16_t scratch[COCO_AUDIO_BUF_LEN];
    size_t k;
    while ((k = coco_audio_drain(m, scratch, COCO_AUDIO_BUF_LEN)) != 0) {
        if (out && *n + k <= SNAP_SOUND_MAX) memcpy(out + *n, scratch, k * sizeof scratch[0]);
        *n += k;
    }
}

static inline bool snap_same_sound(const int16_t *a, size_t na, const int16_t *b, size_t nb,
                                   const char *what) {
    if (na != nb || na > SNAP_SOUND_MAX) {
        fprintf(stderr, "    %s: %zu samples vs %zu\n", what, na, nb);
        return false;
    }
    for (size_t i = 0; i < na; i++) {
        if (a[i] != b[i]) {
            fprintf(stderr, "    %s: sample %zu of %zu: %d vs %d\n", what, i, na, a[i], b[i]);
            return false;
        }
    }
    return true;
}

#define SNAP_DIFF(cond, ...) \
    do { if (cond) { fprintf(stderr, "    %s: ", what); fprintf(stderr, __VA_ARGS__); \
                     fputc('\n', stderr); return false; } } while (0)

static inline bool snap_same_port(const mc6821_port_t *p, const mc6821_port_t *q) {
    return p->out == q->out && p->ddr == q->ddr && p->cr == q->cr && p->in == q->in &&
           p->c1 == q->c1 && p->c2 == q->c2 && p->irq_out == q->irq_out;
}

/* Every byte of machine state the same. */
static inline bool snap_same(const coco_t *a, const coco_t *b, const char *what) {
    for (size_t i = 0; i < COCO_ADDR_SPACE; i++)
        SNAP_DIFF(a->ram[i] != b->ram[i], "RAM #%04zX %02X vs %02X", i, a->ram[i], b->ram[i]);

    const mc6809_t *x = &a->cpu, *y = &b->cpu;
    SNAP_DIFF(x->pc != y->pc || x->x != y->x || x->y != y->y || x->u != y->u || x->s != y->s ||
              x->a != y->a || x->b != y->b || x->dp != y->dp || x->cc != y->cc,
              "CPU PC %04X/%04X S %04X/%04X A %02X/%02X B %02X/%02X CC %02X/%02X",
              x->pc, y->pc, x->s, y->s, x->a, y->a, x->b, y->b, x->cc, y->cc);
    SNAP_DIFF(x->cycles != y->cycles, "cycles %llu vs %llu",
              (unsigned long long)x->cycles, (unsigned long long)y->cycles);
    SNAP_DIFF(x->instructions != y->instructions, "instructions %llu vs %llu",
              (unsigned long long)x->instructions, (unsigned long long)y->instructions);
    SNAP_DIFF(x->irq != y->irq || x->firq != y->firq || x->nmi_line != y->nmi_line ||
              x->nmi_pending != y->nmi_pending || x->nmi_armed != y->nmi_armed ||
              x->halt != y->halt, "interrupt lines IRQ %02X/%02X FIRQ %02X/%02X",
              x->irq, y->irq, x->firq, y->firq);
    SNAP_DIFF(x->wait != y->wait || x->latch != y->latch || x->active != y->active ||
              x->stale != y->stale || x->attn != y->attn,
              "wait %u/%u latch %02X/%02X active %02X/%02X stale %d/%d attn %u/%u",
              x->wait, y->wait, x->latch, y->latch, x->active, y->active, x->stale, y->stale,
              x->attn, y->attn);
    SNAP_DIFF(x->undoc_count != y->undoc_count || x->undoc_pc != y->undoc_pc ||
              x->undoc_op != y->undoc_op, "undocumented opcodes");

    SNAP_DIFF(!snap_same_port(&a->pia0.a, &b->pia0.a), "PIA0 A");
    SNAP_DIFF(!snap_same_port(&a->pia0.b, &b->pia0.b), "PIA0 B");
    SNAP_DIFF(!snap_same_port(&a->pia1.a, &b->pia1.a), "PIA1 A");
    SNAP_DIFF(!snap_same_port(&a->pia1.b, &b->pia1.b), "PIA1 B");
    SNAP_DIFF(a->sam.reg != b->sam.reg, "SAM %04X vs %04X", a->sam.reg, b->sam.reg);

    SNAP_DIFF(coco_ticks(a) != coco_ticks(b), "ticks %llu vs %llu",
              (unsigned long long)coco_ticks(a), (unsigned long long)coco_ticks(b));
    SNAP_DIFF(a->tick0 != b->tick0 || a->cyc0 != b->cyc0 || a->tpc != b->tpc, "the clock's base");
    SNAP_DIFF(a->field_start != b->field_start, "field start %llu vs %llu",
              (unsigned long long)a->field_start, (unsigned long long)b->field_start);
    SNAP_DIFF(a->hs_seen != b->hs_seen, "HS seen %llu vs %llu",
              (unsigned long long)a->hs_seen, (unsigned long long)b->hs_seen);
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++)
        SNAP_DIFF(a->irq_due[i] != b->irq_due[i], "rise due on port %u: %llu vs %llu", i,
                  (unsigned long long)a->irq_due[i], (unsigned long long)b->irq_due[i]);
    SNAP_DIFF(a->fields != b->fields, "fields %u vs %u", (unsigned)a->fields, (unsigned)b->fields);
    SNAP_DIFF(a->sound_fed != b->sound_fed || a->sound_level != b->sound_level ||
              a->sound_edges != b->sound_edges, "sound level %u/%u edges %u/%u",
              (unsigned)a->sound_level, (unsigned)b->sound_level, (unsigned)a->sound_edges,
              (unsigned)b->sound_edges);
    SNAP_DIFF(a->video_bits != b->video_bits || a->video_split != b->video_split ||
              a->split_fields != b->split_fields, "the video bits");

    /* The trap's own state; the deck is the port's media. Its counts are
     * for the heartbeat. */
    SNAP_DIFF(a->tape.hot != b->tape.hot || a->tape.op != b->tape.op || a->tape.rom_ok != b->tape.rom_ok,
              "tape trap hot %04X/%04X", a->tape.hot, b->tape.hot);

    /* The port (cart.h): the Multi-Pak, $FF7F, and each slot. */
    const cart_port_t *pa = &a->port, *pb = &b->port;
    SNAP_DIFF(pa->mpi != pb->mpi || pa->switch_slot != pb->switch_slot ||
              pa->rsdos_slot != pb->rsdos_slot || pa->pool_used != pb->pool_used, "the port");
    SNAP_DIFF(pa->cts != pb->cts || pa->scs != pb->scs || pa->switch_on != pb->switch_on,
              "$FF7F: CTS %u/%u SCS %u/%u, switch %d/%d", pa->cts, pb->cts, pa->scs, pb->scs,
              pa->switch_on, pb->switch_on);
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const cart_t *p = &pa->slot[s], *q = &pb->slot[s];
        SNAP_DIFF(p->kind != q->kind || p->rom != q->rom || p->autostart != q->autostart ||
                  p->rom_at != q->rom_at || p->rom_room != q->rom_room,
                  "slot %u: kind %d/%d", s + 1u, (int)p->kind, (int)q->kind);
        SNAP_DIFF(p->nmi != q->nmi || p->halt != q->halt, "slot %u's lines", s + 1u);
        SNAP_DIFF(p->kind == CART_SMARTWATCH && (p->u.clock.offset != q->u.clock.offset ||
                  p->u.clock.open != q->u.clock.open || p->u.clock.key != q->u.clock.key),
                  "slot %u's SmartWatch: set %lld/%lld", s + 1u, (long long)p->u.clock.offset,
                  (long long)q->u.clock.offset);
    }

    /* The RS-DOS controller, and the chip's track in hand, which is not
     * state but is fetched again (wd_restored) and must be the same. */
    if (a->port.rsdos_slot >= 0) {
        const rsdos_t *p = &a->port.dk, *q = &b->port.dk;
        const wd1793_t *f = &p->fdc, *h = &q->fdc;
        SNAP_DIFF(p->latch != q->latch || p->halt_enable != q->halt_enable || p->intrq != q->intrq ||
                  p->nmi != q->nmi, "the latch %02X/%02X", p->latch, q->latch);
        SNAP_DIFF(f->status != h->status || f->track != h->track || f->sector != h->sector ||
                  f->data != h->data || f->cmd != h->cmd || f->intrq != h->intrq || f->drq != h->drq,
                  "the WD1793's registers: status %02X/%02X", f->status, h->status);
        SNAP_DIFF(f->type1 != h->type1 || f->hold_intrq != h->hold_intrq ||
                  f->index_intrq != h->index_intrq || f->step_in != h->step_in ||
                  f->head_until != h->head_until || f->phase != h->phase || f->due != h->due,
                  "the WD1793's command state: phase %u/%u", f->phase, h->phase);
        SNAP_DIFF(f->drive != h->drive || f->side != h->side, "the drive selected");
        for (unsigned i = 0; i < COCO_DISK_DRIVES; i++)
            SNAP_DIFF(f->drv[i].cyl != h->drv[i].cyl || f->drv[i].loaded != h->drv[i].loaded ||
                      f->drv[i].protect != h->drv[i].protect, "drive %u", i);
        SNAP_DIFF(f->buf_ok != h->buf_ok || f->buf_drive != h->buf_drive || f->buf_side != h->buf_side ||
                  f->buf_cyl != h->buf_cyl || f->nsec != h->nsec ||
                  (f->buf_ok && memcmp(f->buf, h->buf, sizeof f->buf) != 0) ||
                  f->req.get != h->req.get || f->req.put != h->req.put, "the track in hand");
    }

    /* The page table, each pointer as an offset into its own machine. */
    for (unsigned p = 0; p < COCO_PAGE_COUNT; p++) {
        const uint8_t *ra = a->page[p].read, *rb = b->page[p].read;
        const uint8_t *wa = a->page[p].write, *wb = b->page[p].write;
        SNAP_DIFF((ra == NULL) != (rb == NULL) || (wa == NULL) != (wb == NULL) ||
                  (ra && ra - (const uint8_t *)a != rb - (const uint8_t *)b) ||
                  (wa && wa - (const uint8_t *)a != wb - (const uint8_t *)b), "page $%02X", p);
    }
    return true;
}

#endif /* PICO_COCO_TEST_SNAP_UTIL_H */
