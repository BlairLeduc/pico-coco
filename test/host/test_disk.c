/* test_disk.c — the RS-DOS cartridge (design.md §12.4, §17 M13).
 *
 * Parts that need no ROM, so CI runs them:
 *
 *   mfm       the CRC against its known answer; a track built from
 *             sectors and taken apart again; marks in the data
 *   jvc       the header's rules and refusals
 *   wd1793    every command against an in-memory disk, timed by the
 *             turning disk to the tick, a CPU polling DRQ at a chosen
 *             pace; each timing has a control that must fail (wd1793.h),
 *             from pico-oric's test_disc
 *
 * The parts with Disk BASIC (disk11.rom and Extended Color BASIC's, by
 * SHA-1, in roms/; a skip without them is not a pass) are below them.
 */

#include <string.h>

#include "bus.h"
#include "disk_util.h"
#include "guest.h"
#include "snap_util.h"
#include "test_util.h"

#define L COCO_DISK_TRACK_LEN
#define B WD_BYTE_TICKS
/* The datasheet's 1 MHz times, in ticks, worked out here again. */
#define MS(ms) ((uint64_t)(ms) * 14318180u / 1000u)

/* ---- mfm --------------------------------------------------------------- */

static int test_mfm(void) {
    /* CRC-16/CCITT-FALSE's check value. */
    CHECK(mfm_crc_block(0xFFFFu, (const uint8_t *)"123456789", 9) == 0x29B1u, "crc %04X",
          mfm_crc_block(0xFFFFu, (const uint8_t *)"123456789", 9));

    static uint8_t trk[L], data[COCO_DISK_TRACK_DATA_MAX], back[COCO_DISK_TRACK_DATA_MAX];
    mfm_layout_t lay = { 18, 1, 1 };
    CHECK(mfm_layout_fits(lay), "18 x 256 fits");
    CHECK(!mfm_layout_fits((mfm_layout_t){ 19, 1, 1 }) || 19 * 256 > COCO_DISK_TRACK_DATA_MAX,
          "19 x 256");
    CHECK(!mfm_layout_fits((mfm_layout_t){ 10, 3, 1 }), "10 x 1024 does not fit");
    for (unsigned i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7u + i / 256u);
    /* Two sectors and a half there: the rest reads zeros. */
    mfm_build_track(trk, lay, 7, 1, data, 2u * 256u + 128u);
    mfm_sector_t sec[COCO_DISK_SECTORS_MAX];
    unsigned n = mfm_index(trk, sec, COCO_DISK_SECTORS_MAX);
    CHECK(n == 18, "%u sectors", n);
    bool seen[19] = { false };
    for (unsigned i = 0; i < n; i++) {
        const mfm_sector_t *s = &sec[i];
        CHECK(s->c == 7 && s->h == 1 && s->r >= 1 && s->r <= 18 && s->n == 1 && !seen[s->r],
              "sector %u's ID", i);
        seen[s->r] = true;
        CHECK(s->data == s->id + 44u, "sector %u's data mark at %u", i, s->data);
        uint16_t c = mfm_crc_block(0xFFFFu, &trk[s->id - 3u], 8);
        CHECK(c == (trk[s->id + 5u] << 8 | trk[s->id + 6u]), "sector %u's ID CRC", i);
        c = mfm_crc_block(0xFFFFu, &trk[s->data - 3u], 4u + 256u);
        CHECK(c == (trk[s->data + 257u] << 8 | trk[s->data + 258u]), "sector %u's data CRC", i);
        const uint8_t *p = &trk[s->data + 1u];
        unsigned k = s->r - 1u;
        for (unsigned b = 0; b < 256; b++) {
            uint8_t want = k * 256u + b < 2u * 256u + 128u ? data[k * 256u + b] : 0u;
            if (p[b] != want) {
                CHECK(0, "sector %u byte %u: %02X, want %02X", s->r, b, p[b], want);
                break;
            }
        }
    }
    bool got[COCO_DISK_SECTORS_MAX];
    memset(back, 0xEE, sizeof back);
    mfm_build_track(trk, lay, 3, 0, data, sizeof data);
    CHECK(mfm_extract_track(trk, lay, back, got) == 18 && memcmp(back, data, 18u * 256u) == 0,
          "extracted again");
    /* Data that looks like a mark is stepped over, not taken for an ID. */
    memcpy(&trk[sec[0].data + 10u], "\xA1\xA1\xA1\xFE\x03\x00\x63\x01", 8);
    CHECK(mfm_index(trk, sec, COCO_DISK_SECTORS_MAX) == 18, "a mark in the data");
    /* A sector missing: the others come back, its bytes are left. */
    mfm_build_track(trk, lay, 3, 0, data, sizeof data);
    n = mfm_index(trk, sec, COCO_DISK_SECTORS_MAX);
    unsigned gone = sec[4].r;
    trk[sec[4].id] = 0x00u;
    memset(back, 0xEE, sizeof back);
    CHECK(mfm_extract_track(trk, lay, back, got) == 17 && !got[gone - 1u] &&
          back[(gone - 1u) * 256u] == 0xEE, "a sector missing");
    /* Another numbering: none of these. */
    CHECK(mfm_extract_track(trk, (mfm_layout_t){ 18, 1, 0x40 }, back, got) == 0, "other numbers");
    return 0;
}

/* ---- jvc --------------------------------------------------------------- */

static int test_jvc(void) {
    jvc_geom_t g;
    /* A DSK: 35 tracks of 18 x 256 on one side. */
    CHECK(jvc_parse(NULL, 0, 161280u, &g) == NULL && g.header == 0 && g.sides == 1 &&
          g.lay.count == 18 && g.lay.n == 1 && g.lay.first == 1 && g.tracks == 35, "a DSK");
    CHECK(jvc_offset(&g, 2, 0) == 2u * 4608u, "a DSK's offsets");
    /* A short last track counts once a sector of it is there. */
    CHECK(jvc_parse(NULL, 0, 161280u + 256u, &g) == NULL && g.tracks == 36, "short by 17");
    CHECK(jvc_parse(NULL, 0, 4608u * 40u, &g) == NULL && g.tracks == 40, "40 tracks");
    /* A header of two: two sides, 40 tracks. */
    uint8_t h2[2] = { 18, 2 };
    CHECK(jvc_parse(h2, 2, 2u + 2u * 4608u * 40u, &g) == NULL && g.header == 2 && g.sides == 2 &&
          g.tracks == 40 && jvc_offset(&g, 1, 1) == 2u + 3u * 4608u, "two sides");
    /* 720K on one side is taken as two sides of 80. */
    CHECK(jvc_parse(NULL, 0, 4608u * 160u, &g) == NULL && g.sides == 2 && g.tracks == 80, "720K");
    /* Refused: attributes, a header too long, too many tracks, sizes. */
    uint8_t h5[5] = { 18, 1, 1, 1, 1 };
    CHECK(jvc_parse(h5, 5, 5u + 4608u, &g) != NULL, "sector attributes");
    CHECK(jvc_parse(h5, 6, 6u + 4608u, &g) != NULL, "a header of six");
    CHECK(jvc_parse(NULL, 0, 4608u * 2u * 81u, &g) != NULL, "81 two-sided tracks");
    uint8_t h3[3] = { 18, 1, 3 };
    CHECK(jvc_parse(h3, 3, 3u + 18u * 1024u, &g) != NULL, "18 x 1024");
    uint8_t h1[1] = { 0 };
    CHECK(jvc_parse(h1, 1, 1u + 4608u, &g) != NULL, "no sectors");
    /* The header written back. */
    uint8_t hdr[JVC_HEADER_MAX];
    CHECK(jvc_parse(h2, 2, 2u + 2u * 4608u * 40u, &g) == NULL, "again");
    jvc_header(hdr, &g);
    CHECK(hdr[0] == 18 && hdr[1] == 2, "the header written back");

    /* OS-9's LSN0 at the start of a headerless file (jvc.h): two sides of
     * 40 tracks, 1,440 sectors, as NitrOS-9's 360K disks are; the same
     * 368,640 bytes without it are one side of 80. */
    static uint8_t lsn0[256];
    const uint32_t k360 = 2u * 40u * 4608u;
    memset(lsn0, 0, sizeof lsn0);
    lsn0[1] = 0x05; lsn0[2] = 0xA0;            /* DD.TOT 1,440            */
    lsn0[3] = 18;                              /* DD.TKS                  */
    lsn0[0x10] = 0x03;                         /* DD.FMT: two sides, DD   */
    lsn0[0x12] = 18;                           /* DD.SPT                  */
    CHECK(jvc_parse(lsn0, 256, k360, &g) == NULL && g.os9 && g.header == 0 && g.sides == 2 &&
          g.tracks == 40 && g.lay.count == 18, "OS-9: two sides of 40");
    CHECK(jvc_offset(&g, 34, 0) == 68u * 4608u, "OS-9: track 34 after both sides of 33");
    static uint8_t plain[256];
    CHECK(jvc_parse(plain, 256, k360, &g) == NULL && !g.os9 && g.sides == 1 && g.tracks == 80,
          "control: no LSN0, one side of 80");
    /* Fewer sectors than the file holds, or DD.TKS and DD.SPT apart: not
     * OS-9's word. */
    lsn0[2] = 0x9F;
    CHECK(jvc_parse(lsn0, 256, k360, &g) == NULL && !g.os9 && g.sides == 1, "OS-9: DD.TOT short");
    lsn0[2] = 0xA0; lsn0[3] = 17;
    CHECK(jvc_parse(lsn0, 256, k360, &g) == NULL && !g.os9 && g.sides == 1, "OS-9: DD.TKS apart");
    lsn0[3] = 18;
    /* One side, said so: 35 tracks of a 161,280-byte system disk. */
    lsn0[1] = 0x02; lsn0[2] = 0x76; lsn0[0x10] = 0x02;
    CHECK(jvc_parse(lsn0, 256, 161280u, &g) == NULL && g.os9 && g.sides == 1 && g.tracks == 35,
          "OS-9: one side of 35");
    /* A header is believed over LSN0, and LSN0 is not looked for in a
     * file too short to hold it. */
    lsn0[1] = 0x05; lsn0[2] = 0xA0; lsn0[0x10] = 0x03;
    static uint8_t with_header[257];
    with_header[0] = 18;
    memcpy(with_header + 1, lsn0, 256);
    CHECK(jvc_parse(with_header, 257, 1u + k360, &g) == NULL && !g.os9 && g.sides == 1,
          "OS-9: a header wins");
    CHECK(jvc_parse(lsn0, 255, k360, &g) == NULL && !g.os9, "OS-9: LSN0 not read");
    /* Tracks too long for a disk: read as a DSK. */
    lsn0[3] = 40; lsn0[0x12] = 40;
    CHECK(jvc_parse(lsn0, 256, k360, &g) == NULL && !g.os9 && g.lay.count == 18, "OS-9: 40 sectors");

    /* A one-sided image made two-sided: a header of two, or the longer
     * one it has, or none for OS-9's. */
    jvc_geom_t w;
    CHECK(jvc_parse(NULL, 0, 161280u, &g) == NULL, "a DSK again");
    w = jvc_two_sided(&g);
    CHECK(w.sides == 2 && w.header == 2 && w.tracks == 35 && jvc_offset(&w, 1, 0) == 2u + 2u * 4608u,
          "two-sided: a DSK gets a header");
    uint8_t h4[4] = { 18, 1, 1, 1 };
    CHECK(jvc_parse(h4, 4, 4u + 4608u, &g) == NULL && jvc_two_sided(&g).header == 4,
          "two-sided: a longer header kept");
    lsn0[3] = 18; lsn0[0x12] = 18; lsn0[1] = 0x02; lsn0[2] = 0x76; lsn0[0x10] = 0x02;
    CHECK(jvc_parse(lsn0, 256, 161280u, &g) == NULL && jvc_two_sided(&g).header == 0,
          "two-sided: OS-9's stays headerless");
    /* Grown or cut, the shape stays and the tracks follow. */
    CHECK(jvc_parse(h2, 2, 2u + 2u * 4608u * 40u, &g) == NULL && jvc_resize(&g, 2u + 2u * 4608u * 41u) &&
          g.tracks == 41 && g.sides == 2, "resize: 41 tracks");
    CHECK(!jvc_resize(&g, 2u + 2u * 4608u * 81u), "resize: 81 refused");

    /* In memory, as dskio.c does on the card: side 1 of track 2 written
     * to a one-sided 35-track DSK. Every track's side 0 is where the new
     * shape says, side 1 zeros but the one written. */
    host_disk_t d;
    host_disk_blank(&d, 35, 0);
    for (uint32_t i = 0; i < d.len; i++) d.img[i] = (uint8_t)(i / 256u * 7u + i);
    host_disk_t before;
    host_disk_copy(&before, &d);
    static uint8_t trk[COCO_DISK_TRACK_LEN], sec[4608];
    for (unsigned i = 0; i < sizeof sec; i++) sec[i] = (uint8_t)(0xA5u ^ i);
    mfm_build_track(trk, d.g.lay, 2, 1, sec, sizeof sec);
    CHECK(host_disk_put(&d, 2, 1, trk), "widen: the put kept");
    CHECK(d.g.sides == 2 && d.g.header == 2 && d.g.tracks == 35 && d.len == 2u + 70u * 4608u &&
          d.img[0] == 18 && d.img[1] == 2, "widen: %u sides, %u tracks, %u bytes", d.g.sides,
          d.g.tracks, d.len);
    bool same = true, zero = true;
    for (unsigned c = 0; c < 35; c++) {
        same = same && !memcmp(d.img + jvc_offset(&d.g, c, 0), before.img + c * 4608u, 4608u);
        for (unsigned b = 0; c != 2 && b < 4608u; b++) zero = zero && d.img[jvc_offset(&d.g, c, 1) + b] == 0;
    }
    CHECK(same && zero, "widen: side 0 moved, side 1 empty");
    CHECK(!memcmp(d.img + jvc_offset(&d.g, 2, 1), sec, sizeof sec), "widen: the side written");
    jvc_geom_t again;
    CHECK(jvc_parse(d.img, d.len, d.len, &again) == NULL && again.sides == 2 && again.tracks == 35,
          "widen: the file reads back two-sided");
    host_disk_free(&d);
    host_disk_free(&before);
    return 0;
}

/* ---- wd1793 -------------------------------------------------------------- */

static wd1793_t s_f;
static host_disk_t s_disk[2];
static uint64_t t, s_intrq_at, s_serve_at;

/* Every data byte of the disk distinct, so a read that takes the wrong
 * sector or the wrong byte cannot pass. Two sides of 40 tracks, with a
 * header saying so. */
static uint8_t expect_byte(unsigned c, unsigned s, unsigned r, unsigned b) {
    return (uint8_t)(c * 7u + s * 101u + r * 31u + b);
}

static int pattern(host_disk_t *d) {
    memset(d, 0, sizeof *d);
    d->len = 2u + 2u * 40u * 4608u;
    d->img = (uint8_t *)malloc(d->len);
    d->img[0] = 18;
    d->img[1] = 2;
    CHECK(jvc_parse(d->img, d->len, d->len, &d->g) == NULL && d->g.sides == 2, "the pattern disk");
    for (unsigned c = 0; c < 40; c++)
        for (unsigned s = 0; s < 2; s++)
            for (unsigned r = 1; r <= 18; r++)
                for (unsigned b = 0; b < 256; b++)
                    d->img[jvc_offset(&d->g, c, s) + (r - 1u) * 256u + b] = expect_byte(c, s, r, b);
    return 0;
}

static void serve(void) {
    const wd_req_t *r = wd_request(&s_f);
    if (!r || t < s_serve_at) return;
    bool ok = true;
    if (r->put) {
        host_disk_t *d = &s_disk[r->put_drive];
        if (host_disk_put(d, r->put_cyl, r->put_side, s_f.buf)) d->puts++;
    }
    if (r->get) {
        host_disk_t *d = &s_disk[r->drive];
        if (d->img) {
            host_disk_get(d, r->cyl, r->side, s_f.buf);
            d->gets++;
        } else {
            ok = false;
        }
    }
    wd_served(&s_f, ok, t);
}

/* Run the chip's events up to `to`, each at its own tick, noting when
 * INTRQ rises. */
static void step_to(uint64_t to) {
    while (s_f.due <= to) {
        bool was = s_f.intrq;
        uint64_t e = s_f.due;
        wd_run(&s_f, e);
        if (!was && s_f.intrq) s_intrq_at = e;
    }
}

/* A CPU polling DRQ every `poll` ticks, moving bytes to dst or from src,
 * until INTRQ or `limit`; with neither, a CPU that never answers.
 * Requests are served as soon as asked, or from s_serve_at. Returns the
 * bytes moved. */
static size_t cpu(unsigned poll, uint8_t *dst, const uint8_t *src, size_t n, uint64_t limit) {
    size_t moved = 0;
    while (!s_f.intrq && t < limit) {
        serve();
        t += poll;
        step_to(t);
        if (s_f.intrq || !s_f.drq || (!dst && !src)) continue;
        if (dst) {
            uint8_t v = wd_read(&s_f, 3, t);
            if (moved < n) dst[moved] = v;
        } else {
            wd_write(&s_f, 3, moved < n ? src[moved] : 0x4Eu, t);
        }
        moved++;
    }
    step_to(t);
    return moved;
}

static void command(uint8_t v) {
    s_intrq_at = 0;
    wd_write(&s_f, 0, v, t);
    step_to(t);
}

static uint64_t kat(uint64_t when) { return (when + B - 1u) / B; }
static uint64_t next_k(uint64_t k0, unsigned pos) { return k0 + (pos + L - k0 % L) % L; }

/* Sector r's ID sync on track c of the pattern disk, as built. */
static unsigned sync_of(unsigned r) {
    static uint8_t trk[L];
    mfm_sector_t sec[COCO_DISK_SECTORS_MAX];
    host_disk_get(&s_disk[0], 0, 0, trk);
    unsigned n = mfm_index(trk, sec, COCO_DISK_SECTORS_MAX);
    for (unsigned i = 0; i < n; i++)
        if (sec[i].r == r) return sec[i].id - 3u;
    return 0;
}

/* The first ID to come round from tick `from`, its sync's byte count;
 * its sector in *r. */
static uint64_t first_id(uint64_t from, unsigned *r) {
    uint64_t ks = UINT64_MAX;
    for (unsigned s = 1; s <= 18; s++) {
        uint64_t k = next_k(kat(from), sync_of(s));
        if (k < ks) ks = k, *r = s;
    }
    return ks;
}

static void wd_fresh(void) {
    wd_init(&s_f);
    for (unsigned d = 0; d < 2; d++) {
        host_disk_free(&s_disk[d]);
        (void)pattern(&s_disk[d]);
        wd_insert(&s_f, d, false, 0);
    }
    t = 10000;
    s_serve_at = 0;
}

static bool read_sector_ok(const uint8_t *buf, size_t n, unsigned c, unsigned s, unsigned r) {
    if (n < 256) return false;
    for (unsigned b = 0; b < 256; b++)
        if (buf[b] != expect_byte(c, s, r, b)) return false;
    return true;
}

/* What a DOS hands Write Track to format track c, side h: 18 sectors of
 * 256 bytes, F5 for each A1 and F7 for the CRCs (datasheet). */
static size_t format_stream(uint8_t *fmt, unsigned c, unsigned h) {
    size_t k = 0;
#define P(v, cnt) do { for (unsigned q_ = 0; q_ < (cnt); q_++) fmt[k++] = (uint8_t)(v); } while (0)
    P(0x4E, 32);
    for (unsigned r = 1; r <= 18; r++) {
        P(0x00, 8); P(0xF5, 3); P(0xFE, 1); P(c, 1); P(h, 1); P(r, 1); P(1, 1); P(0xF7, 1);
        P(0x4E, 22); P(0x00, 12); P(0xF5, 3); P(0xFB, 1); P(0xFF, 256); P(0xF7, 1);
        P(0x4E, 24);
    }
#undef P
    return k;
}

/* The ID the next Read Address finds: C and H into c and h. False if
 * none came. */
static bool read_id(unsigned *c, unsigned *h) {
    uint8_t id[6];
    command(0xC0);
    size_t n = cpu(336, id, NULL, sizeof id, t + 15000000);
    if (n != sizeof id || (wd_read(&s_f, 0, t) & WD_ST_RNF)) return false;
    *c = id[0];
    *h = id[1];
    return true;
}

/* A poll of 21 6809 cycles a byte, and one of 40, slower than the
 * disk's 458 ticks. */
#define FAST 336u
#define SLOW 640u
#define TL(n) (t + (uint64_t)(n) * 15u)

static int test_wd1793(void) {
    static uint8_t buf[2 * L];
    size_t n;
    unsigned r0;

    /* Restore from track 5 at 6 ms a step: five steps, and a sixth
     * step's time once TR00 is seen (wd1793.c), then INTRQ. The control
     * is the slowest rate, which must not finish then. */
    wd_fresh();
    s_f.drv[0].cyl = 5;
    s_f.track = 5;
    uint64_t t0 = t;
    command(0x00);
    cpu(150, NULL, NULL, 0, TL(1000000));
    CHECK(s_intrq_at == t0 + 6u * MS(6), "restore at %llu, want %llu",
          (unsigned long long)(s_intrq_at - t0), (unsigned long long)(6u * MS(6)));
    CHECK(s_f.drv[0].cyl == 0 && s_f.track == 0, "restore: head %u track %u", s_f.drv[0].cyl,
          s_f.track);
    CHECK(wd_read(&s_f, 0, t) & WD_ST_TRACK0, "restore: TRACK 0 bit");
    s_f.drv[0].cyl = 5;
    t0 = t;
    command(0x03);
    cpu(150, NULL, NULL, 0, TL(1000000));
    CHECK(s_intrq_at == t0 + 6u * MS(30) && s_intrq_at != t0 + 6u * MS(6), "control: 30 ms a step");

    /* A Seek out whose track register is ahead of the head, as Tandy
     * OS-9's driver homes it, $FF to 0 with the head at 2: two steps, the
     * third finds TR00, and the track register is 0. Not 255 steps. */
    s_f.drv[0].cyl = 2;
    s_f.track = 0xFF;
    s_f.data = 0;
    t0 = t;
    command(0x10);
    cpu(150, NULL, NULL, 0, TL(1000000));
    CHECK(s_intrq_at == t0 + 3u * MS(6), "seek out to TR00 at %llu, want %llu",
          (unsigned long long)(s_intrq_at - t0), (unsigned long long)(3u * MS(6)));
    CHECK(s_f.drv[0].cyl == 0 && s_f.track == 0, "seek out to TR00: head %u track %u",
          s_f.drv[0].cyl, s_f.track);
    /* And it stops short of its data register: from $FF to 5, the head at
     * 2, it ends at TR00 with the track register 0. */
    s_f.drv[0].cyl = 2;
    s_f.track = 0xFF;
    s_f.data = 5;
    t0 = t;
    command(0x10);
    cpu(150, NULL, NULL, 0, TL(1000000));
    CHECK(s_intrq_at == t0 + 3u * MS(6) && s_f.drv[0].cyl == 0 && s_f.track == 0,
          "seek out short of its target: %llu, head %u track %u",
          (unsigned long long)(s_intrq_at - t0), s_f.drv[0].cyl, s_f.track);
    /* Step-out with update at track 0: no pulse, the track register 0. */
    s_f.track = 3;
    t0 = t;
    command(0x70);
    cpu(150, NULL, NULL, 0, TL(1000000));
    CHECK(s_f.drv[0].cyl == 0 && s_f.track == 0 && s_intrq_at == t0 + MS(6),
          "step-out at TR00: head %u track %u", s_f.drv[0].cyl, s_f.track);

    /* Seek with verify to 10: the steps, the 30 ms settle, then the first
     * ID to come round. */
    wd_fresh();
    t0 = t;
    s_f.data = 10;
    command(0x14);
    cpu(150, NULL, NULL, 0, TL(2000000));
    uint64_t settled = t0 + 10u * MS(6) + MS(30);
    uint64_t ks = first_id(settled, &r0);
    CHECK(s_intrq_at == (ks + 10u) * B, "verify at %llu, want %llu", (unsigned long long)s_intrq_at,
          (unsigned long long)((ks + 10u) * B));
    CHECK(s_f.drv[0].cyl == 10 && !(wd_read(&s_f, 0, t) & WD_ST_SEEK_ERR), "seek 10");
    /* The control: a track register that lies. The head goes to 15, and
     * no ID there says 5, so SEEK ERROR at the fifth index. */
    s_f.track = 0;
    s_f.data = 5;
    t0 = t;
    command(0x14);
    cpu(150, NULL, NULL, 0, TL(3000000));
    settled = t0 + 5u * MS(6) + MS(30);
    CHECK(s_f.drv[0].cyl == 15 && (wd_read(&s_f, 0, t) & WD_ST_SEEK_ERR), "seek error");
    CHECK(s_intrq_at == (next_k(kat(settled), 0) + 4u * L) * B, "seek error at %llu",
          (unsigned long long)s_intrq_at);

    /* Read sector 5 at Disk BASIC's pace: every byte, no LOST DATA, and
     * INTRQ as the field's CRC passes. */
    wd_fresh();
    s_f.sector = 5;
    t0 = t;
    command(0x80);
    n = cpu(FAST, buf, NULL, 256, TL(1000000));
    ks = next_k(kat(t0), sync_of(5));
    CHECK(n == 256 && read_sector_ok(buf, n, 0, 0, 5), "read: %zu bytes", n);
    CHECK(!(wd_read(&s_f, 0, t) & (WD_ST_LOST | WD_ST_RNF)), "read: status");
    CHECK(s_intrq_at == (ks + 306u) * B, "read ends at %llu, want %llu",
          (unsigned long long)s_intrq_at, (unsigned long long)((ks + 306u) * B));
    CHECK(s_disk[0].gets == 1, "read: %u tracks fetched", s_disk[0].gets);
    /* The control: a CPU slower than the disk loses bytes. */
    command(0x80);
    n = cpu(SLOW, buf, NULL, 256, TL(1000000));
    CHECK(n < 256 && (wd_read(&s_f, 0, t) & WD_ST_LOST), "control: slow CPU, %zu bytes", n);

    /* E: the 30 ms settle before the search. */
    t0 = t;
    command(0x84);
    cpu(FAST, buf, NULL, 256, TL(1000000));
    ks = next_k(kat(t0 + MS(30)), sync_of(5));
    CHECK(s_intrq_at == (ks + 306u) * B, "read with E");

    /* No such sector: RNF at the fifth index pulse. */
    s_f.sector = 19;
    t0 = t;
    command(0x80);
    cpu(FAST, buf, NULL, 256, TL(2000000));
    CHECK((wd_read(&s_f, 0, t) & WD_ST_RNF) && s_intrq_at == (next_k(kat(t0), 0) + 4u * L) * B,
          "RNF at %llu", (unsigned long long)s_intrq_at);
    /* Side compare (C, S = 1) on side 0's IDs: not there either. */
    s_f.sector = 1;
    command(0x8A);
    cpu(FAST, buf, NULL, 256, TL(2000000));
    CHECK(wd_read(&s_f, 0, t) & WD_ST_RNF, "side compare");

    /* Multiple: 16, 17 and 18 back to back, then RNF for 19. */
    s_f.sector = 16;
    command(0x90);
    n = cpu(FAST, buf, NULL, sizeof buf, TL(3000000));
    CHECK(n == 768 && read_sector_ok(buf, 256, 0, 0, 16) && read_sector_ok(buf + 512, 256, 0, 0, 18),
          "multiple: %zu bytes", n);
    CHECK((wd_read(&s_f, 0, t) & WD_ST_RNF) && s_f.sector == 19, "multiple ends at sector %u",
          s_f.sector);

    /* Read address: the next ID, C H R N and its CRC; C into the sector
     * register; INTRQ a byte after the ID passes. */
    t0 = t;
    command(0xC0);
    n = cpu(FAST, buf, NULL, 6, TL(1000000));
    ks = first_id(t0, &r0);
    CHECK(n == 6 && buf[0] == 0 && buf[1] == 0 && buf[2] == r0 && buf[3] == 1,
          "read address: %zu bytes, sector %u, want %u", n, buf[2], r0);
    CHECK(s_f.sector == 0 && s_intrq_at == (ks + 11u) * B, "read address's end");

    /* Read track: from the index, the whole track as it lies. */
    static uint8_t want[L];
    host_disk_get(&s_disk[0], 0, 0, want);
    t0 = t;
    command(0xE0);
    n = cpu(FAST, buf, NULL, sizeof buf, TL(1000000));
    CHECK(n == L && memcmp(buf, want, L) == 0, "read track: %zu bytes", n);
    CHECK(s_intrq_at == (next_k(kat(t0), 0) + L + 1u) * B, "read track ends past the index");

    /* Write sector 3: the bytes, the track posted back once the command
     * ends: not at once, but when the head unloads, 15 revolutions
     * after the command, and its sectors into the image; deleted mark
     * (a0), read back as RECTYPE. */
    static uint8_t data[256];
    for (unsigned b = 0; b < 256; b++) data[b] = (uint8_t)(255u - b);
    s_f.sector = 3;
    command(0xA1);
    n = cpu(FAST, NULL, data, 256, TL(1000000));
    CHECK(n == 256 && !(wd_read(&s_f, 0, t) & (WD_ST_LOST | WD_ST_RNF)), "write: %zu", n);
    CHECK(!wd_request(&s_f) && s_f.buf_dirty, "write: kept in hand, not posted at once");
    uint64_t unload = s_f.head_until;
    CHECK(unload == s_intrq_at + 15u * WD_REV_TICKS, "write: the head unloads 15 revolutions on");
    step_to(unload - 1u);
    CHECK(!wd_request(&s_f), "write: not posted a tick before the head unloads");
    step_to(unload);
    CHECK(wd_request(&s_f) && wd_request(&s_f)->put && !wd_request(&s_f)->get,
          "write: posted as the head unloads");
    t = unload;
    uint8_t *p = &s_f.buf[sync_of(3) + 48u];
    uint16_t crc = mfm_crc_block(0xFFFFu, p - 4, 260);
    CHECK(memcmp(p, data, 256) == 0 && p[-1] == 0xF8 && crc == (p[256] << 8 | p[257]),
          "write: on the track, the CRC good");
    serve();
    CHECK(s_disk[0].puts == 1, "write: %u tracks put", s_disk[0].puts);
    CHECK(memcmp(s_disk[0].img + jvc_offset(&s_disk[0].g, 0, 0) + 2u * 256u, data, 256) == 0,
          "write: in the image");
    command(0x80);
    n = cpu(FAST, buf, NULL, 256, TL(1000000));
    CHECK(n == 256 && memcmp(buf, data, 256) == 0 && (wd_read(&s_f, 0, t) & WD_ST_RECTYPE),
          "write: read back");
    /* The control: a CPU that writes nothing loses the sector before its
     * mark, which stays as it was. */
    s_f.sector = 4;
    uint8_t before[300];
    memcpy(before, &s_f.buf[sync_of(4)], sizeof before);
    command(0xA0);
    cpu(FAST, NULL, NULL, 0, TL(1000000));   /* src NULL and dst NULL: never writes */
    CHECK(wd_read(&s_f, 0, t) & WD_ST_LOST, "control: no data, LOST");
    CHECK(memcmp(before, &s_f.buf[sync_of(4)], sizeof before) == 0, "control: the sector untouched");
    serve();

    /* Write-protected: refused at once, nothing posted. */
    wd_fresh();
    s_f.drv[1].protect = true;
    wd_select(&s_f, 1, 0, t);
    s_f.sector = 1;
    t0 = t;
    command(0xA0);
    CHECK(s_f.intrq && s_intrq_at == t0 && (wd_read(&s_f, 0, t) & WD_ST_PROTECT) &&
          !wd_request(&s_f), "write protect");

    /* Another track: a read there fetches it; a written one goes back
     * first. Served late, the search starts from when it was served. */
    wd_fresh();
    s_f.sector = 1;
    command(0xA0);
    cpu(FAST, NULL, data, 256, TL(1000000));
    s_f.data = 1;
    s_serve_at = UINT64_MAX;
    command(0x10);
    cpu(150, NULL, NULL, 0, TL(1000000));
    s_serve_at = t + 50000u * 15u;
    const wd_req_t *rq;
    command(0x80);
    rq = wd_request(&s_f);
    CHECK(rq && rq->put && rq->put_cyl == 0 && rq->get && rq->cyl == 1, "put then get");
    cpu(FAST, buf, NULL, 256, TL(1000000));
    ks = next_k(kat(s_serve_at), sync_of(1));
    CHECK(read_sector_ok(buf, 256, 1, 0, 1) && s_intrq_at == (ks + 306u) * B,
          "served late: at %llu", (unsigned long long)s_intrq_at);
    CHECK(memcmp(s_disk[0].img + jvc_offset(&s_disk[0].g, 0, 0), data, 256) == 0, "put back");
    s_serve_at = 0;

    /* Write track: RS-DOS's format of track 1, side 1, into the image as
     * 18 sectors of $FF. */
    static uint8_t fmt[8000];
    size_t k = format_stream(fmt, 1, 1);
    CHECK(k <= L, "the format stream is %zu bytes", k);
    wd_select(&s_f, 0, 1, t);
    t0 = t;
    command(0xF0);
    n = cpu(FAST, NULL, fmt, k, TL(1000000));
    CHECK(!(wd_read(&s_f, 0, t) & WD_ST_LOST), "write track: no LOST DATA");
    CHECK(s_intrq_at == (next_k(kat(t0), 0) + L) * B, "write track ends at the index: %llu, want %llu",
          (unsigned long long)s_intrq_at, (unsigned long long)((next_k(kat(t0), 0) + L) * B));
    mfm_sector_t sec[COCO_DISK_SECTORS_MAX];
    unsigned ns = mfm_index(s_f.buf, sec, COCO_DISK_SECTORS_MAX);
    CHECK(ns == 18, "write track: %u sectors", ns);
    for (unsigned i = 0; i < ns; i++) {
        const uint8_t *trk = s_f.buf;
        uint16_t c = mfm_crc_block(0xFFFFu, &trk[sec[i].id - 3u], 8);
        CHECK(c == (trk[sec[i].id + 5u] << 8 | trk[sec[i].id + 6u]) && sec[i].r == i + 1u,
              "write track: ID %u", i);
        c = mfm_crc_block(0xFFFFu, &trk[sec[i].data - 3u], 260);
        CHECK(c == (trk[sec[i].data + 257u] << 8 | trk[sec[i].data + 258u]),
              "write track: data %u", i);
    }
    uint32_t puts = s_disk[0].puts;
    /* The port's flush (wd_flush), as before a disk changes. */
    CHECK(!wd_request(&s_f), "write track: kept in hand");
    wd_flush(&s_f);
    serve();
    const uint8_t *img = s_disk[0].img + jvc_offset(&s_disk[0].g, 1, 1);
    bool all_ff = true;
    for (unsigned i = 0; i < 4608u; i++) all_ff = all_ff && img[i] == 0xFF;
    CHECK(all_ff && s_disk[0].puts == puts + 1u, "write track: into the image");

    /* Force Interrupt: $D0 ends a read with no INTRQ; $D8 raises it, and
     * a status read leaves it; $D4 raises it at every index. */
    wd_fresh();
    s_f.sector = 9;
    command(0x80);
    cpu(FAST, buf, NULL, 20, TL(300000));   /* part of the way: INTRQ never comes */
    command(0xD0);
    CHECK(!wd_busy(&s_f) && !s_f.intrq, "$D0");
    command(0xD8);
    (void)wd_read(&s_f, 0, t);
    CHECK(s_f.intrq, "$D8 holds INTRQ through a status read");
    command(0xD0);
    CHECK(!s_f.intrq, "$D0 clears it");
    command(0xD4);
    uint64_t idx = next_k(kat(t), 0) * B;
    cpu(150, NULL, NULL, 0, TL(400000));
    CHECK(s_intrq_at == idx, "$D4 at the index: %llu, want %llu", (unsigned long long)s_intrq_at,
          (unsigned long long)idx);
    (void)wd_read(&s_f, 0, t);
    s_intrq_at = 0;
    cpu(150, NULL, NULL, 0, TL(400000));
    CHECK(s_intrq_at == idx + WD_REV_TICKS, "$D4 again a revolution on");
    command(0xD0);

    /* No disk: no index, nothing comes round; busy until $D0. */
    wd_eject(&s_f, 0, t);
    command(0x80);
    cpu(FAST, buf, NULL, 256, TL(3000000));
    CHECK(wd_busy(&s_f) && !s_f.intrq && s_f.due == WD_NEVER, "no disk: busy");
    command(0xD0);
    CHECK(!wd_busy(&s_f), "no disk: $D0 ends it");

    /* Beyond the image: asked for, and unformatted; a format there grows
     * the image. */
    wd_fresh();
    s_f.data = 50;
    command(0x10);
    cpu(150, NULL, NULL, 0, TL(1000000));
    s_f.sector = 1;
    command(0x80);
    CHECK(wd_request(&s_f) && wd_request(&s_f)->cyl == 50, "track 50: asked for");
    cpu(FAST, buf, NULL, 256, TL(2000000));
    CHECK(wd_read(&s_f, 0, t) & WD_ST_RNF, "track 50: RNF");
    k = format_stream(fmt, 50, 0);
    command(0xF0);
    cpu(FAST, NULL, fmt, k, TL(1000000));
    wd_flush(&s_f);
    serve();
    /* Side 0 of track 50 is the image's last, side 1 not yet there. */
    CHECK(s_disk[0].g.tracks == 51 && s_disk[0].len == 2u + (50u * 2u + 1u) * 4608u,
          "track 50 formatted: %u tracks, %u bytes", s_disk[0].g.tracks, s_disk[0].len);

    /* A 40-track drive's end stop (wd1793.h): a seek to 60 leaves the
     * head at 39 and the track register at 60, and takes 60 steps' time.
     * Read Address finds track 39's IDs. The control is the default
     * 80-track drive, whose head gets to 60, where the 40-track image has
     * nothing. */
    unsigned c, h;
    for (int stop = 1; stop >= 0; stop--) {
        wd_fresh();
        if (stop) wd_drive_type(&s_f, 0, (wd_type_t){ 2, 40 }, t);
        s_f.data = 60;
        t0 = t;
        command(0x10);
        cpu(150, NULL, NULL, 0, TL(1000000));
        CHECK(s_f.track == 60 && s_intrq_at == t0 + 60u * MS(6), "%s: seek 60 in 60 steps' time",
              stop ? "end stop" : "control");
        CHECK(s_f.drv[0].cyl == (stop ? 39 : 60), "%s: head at %u", stop ? "end stop" : "control",
              s_f.drv[0].cyl);
        bool found = read_id(&c, &h);
        CHECK(stop ? found && c == 39 && h == 0 : !found, "%s: read address at the head",
              stop ? "end stop" : "control");
    }
    /* Restore steps out as far as the head went, and no further. */
    wd_fresh();
    wd_drive_type(&s_f, 0, (wd_type_t){ 2, 40 }, t);
    s_f.data = 60;
    command(0x10);
    cpu(150, NULL, NULL, 0, TL(1000000));
    t0 = t;
    command(0x00);
    cpu(150, NULL, NULL, 0, TL(1000000));
    CHECK(s_f.drv[0].cyl == 0 && s_f.track == 0 && s_intrq_at == t0 + 40u * MS(6),
          "end stop: restore in 39 steps and TR00's");
    /* A type set with the head past its stop brings the head back. */
    s_f.drv[1].cyl = 70;
    wd_drive_type(&s_f, 1, (wd_type_t){ 1, 40 }, t);
    CHECK(s_f.drv[1].cyl == 39, "a new type: head at %u", s_f.drv[1].cyl);

    /* The side chosen after the command, as NitrOS-9's driver writes the
     * latch after the command register: the search looks again on the
     * new side, from then, and reads side 1's sector. The control keeps
     * side 0, and reads its. Side 0's track is in hand first, as it was
     * when NitrOS-9's boot read the wrong side. */
    for (int change = 1; change >= 0; change--) {
        wd_fresh();
        s_f.sector = 1;
        command(0x80);
        cpu(FAST, buf, NULL, 256, TL(1000000));
        s_f.sector = 7;
        command(0x80);
        t += 40;
        step_to(t);
        if (change) wd_select(&s_f, 0, 1, t);
        n = cpu(FAST, buf, NULL, 256, TL(1000000));
        CHECK(n == 256 && read_sector_ok(buf, n, 0, change ? 1u : 0u, 7), "%s: side %d's sector 7",
              change ? "side after command" : "control", change);
        CHECK(!(wd_read(&s_f, 0, t) & (WD_ST_LOST | WD_ST_RNF)), "%s: status",
              change ? "side after command" : "control");
    }
    /* Even once the side 0 search has given up: RNF for sector 1 with
     * side compare S = 1, then side 1 chosen, which has it. */
    wd_fresh();
    s_f.sector = 2;
    command(0x80);
    cpu(FAST, buf, NULL, 256, TL(1000000));
    s_f.sector = 1;
    command(0x8A);
    t += MS(1);
    step_to(t);
    CHECK(s_f.phase != 0 && wd_busy(&s_f) && (s_f.status & WD_ST_RNF), "side compare: given up on side 0");
    wd_select(&s_f, 0, 1, t);
    n = cpu(FAST, buf, NULL, 256, TL(2000000));
    CHECK(n == 256 && read_sector_ok(buf, n, 0, 1, 1) && !(wd_read(&s_f, 0, t) & WD_ST_RNF),
          "side compare: found once side 1 is chosen");
    /* And a drive chosen after the command: drive 1, from no disk. */
    wd_fresh();
    wd_eject(&s_f, 0, t);
    s_f.sector = 3;
    command(0x80);
    t += 40;
    step_to(t);
    wd_select(&s_f, 1, 0, t);
    n = cpu(FAST, buf, NULL, 256, TL(1000000));
    CHECK(n == 256 && read_sector_ok(buf, n, 0, 0, 3), "drive after command: drive 1's sector");

    /* A one-sided drive has no side select: side 1 of the latch reads
     * side 0's IDs, and a write there goes back to side 0. The control
     * is the two-sided drive, which reads side 1's. */
    for (int one = 1; one >= 0; one--) {
        wd_fresh();
        if (one) wd_drive_type(&s_f, 0, (wd_type_t){ 1, 80 }, t);
        wd_select(&s_f, 0, 1, t);
        bool found = read_id(&c, &h);
        CHECK(found && c == 0 && h == (one ? 0u : 1u), "%s: side 1 reads H %u",
              one ? "one-sided" : "control", h);
        s_f.sector = 2;
        command(0xA0);
        cpu(FAST, NULL, data, 256, TL(1000000));
        wd_flush(&s_f);
        rq = wd_request(&s_f);
        CHECK(rq && rq->put && rq->put_side == (one ? 0u : 1u), "%s: the write goes to side %u",
              one ? "one-sided" : "control", rq ? rq->put_side : 9u);
        serve();
    }
    return 0;
}

/* ---- Disk BASIC ---------------------------------------------------------- */

static guest_t g;
static host_disk_t *s_drives[COCO_DISK_DRIVES];

static void serve_fields(guest_t *gg) { host_disk_serve(&gg->m, s_drives); }

static bool screen_has(const char *s) {
    for (int r = 0; r < 16; r++)
        if (strstr(guest_row(&g.m, r), s)) return true;
    return false;
}

/* Fields until the bottom-most text on the screen is "OK" and the
 * controller is idle, up to max. */
static bool wait_ok(int max) {
    for (int f = 0; f < max; f++) {
        for (int r = 15; r >= 0; r--) {
            const char *s = guest_row(&g.m, r);
            if (!s[0]) continue;
            if (strcmp(s, "OK") == 0 && !coco_disk_busy(&g.m)) return true;
            break;
        }
        guest_fields(&g, 1);
    }
    return false;
}

static void clear_screen(void) {
    guest_type(&g, "CLS\r");
    guest_fields(&g, 5);
}

static bool disk_boot(unsigned ram_k) {
    guest_build_disk(&g, ram_k);
    g.after_field = serve_fields;
    for (unsigned d = 0; d < COCO_DISK_DRIVES; d++)
        if (s_drives[d]) host_disk_insert(&g.m, d, s_drives[d]);
    return guest_boot(&g);
}

/* The first track DSKINI writes, as it is put back. */
static uint8_t s_written[COCO_DISK_TRACK_LEN];
static bool s_have_written;
static void keep_track(const uint8_t *trk, unsigned c, unsigned h) {
    if (c == 0 && h == 0 && !s_have_written) {
        memcpy(s_written, trk, sizeof s_written);
        s_have_written = true;
    }
}

static const uint8_t *sector_of(const host_disk_t *d, unsigned c, unsigned r) {
    return d->img + jvc_offset(&d->g, c, 0) + (r - 1u) * 256u;
}

/* Type a line and wait for OK. */
static bool run(const char *line, int max) {
    clear_screen();
    guest_type(&g, line);
    return wait_ok(max);
}

/* DSKINI on an empty disk: OK, 35 tracks, the image RS-DOS's empty disk,
 * and the track it wrote the one mfm_build_track builds from it, byte for
 * byte, which is how the layout was read (mfmdisk.c). */
static int dskini(host_disk_t *d, unsigned drive) {
    s_have_written = false;
    host_disk_put_hook = drive == 0 ? keep_track : NULL;
    char cmd[16];
    snprintf(cmd, sizeof cmd, "DSKINI%u\r", drive);
    uint64_t t0 = coco_ticks(&g.m);
    bool ok = run(cmd, 60 * 120);
    host_disk_put_hook = NULL;
    CHECK(ok, "DSKINI%u: no OK", drive);
    if (!ok) guest_dump(&g.m, stderr);
    printf("DSKINI%u: %.1f s of guest time, %u tracks fetched, %u put\n", drive,
           (double)(coco_ticks(&g.m) - t0) / COCO_TICK_HZ, d->gets, d->puts);
    CHECK(d->len == 35u * 4608u && d->g.tracks == 35, "DSKINI%u: %u bytes", drive, d->len);
    /* The FAT, track 17 sector 2: 68 granules free; the directory,
     * sectors 3 to 11, empty; the data sectors, $FF. */
    const uint8_t *fat = sector_of(d, 17, 2);
    bool fat_ok = true;
    for (unsigned i = 0; i < 68; i++) fat_ok = fat_ok && fat[i] == 0xFF;
    CHECK(fat_ok, "DSKINI%u: the FAT is not empty", drive);
    CHECK(sector_of(d, 17, 3)[0] == 0xFF && sector_of(d, 0, 1)[0] == 0xFF &&
          sector_of(d, 34, 18)[255] == 0xFF, "DSKINI%u: the sectors", drive);
    if (drive == 0) {
        CHECK(s_have_written, "DSKINI0: no track 0 put");
        static uint8_t ff[4608], trk[L];
        memset(ff, 0xFF, sizeof ff);
        mfm_build_track(trk, (mfm_layout_t){ 18, 1, 1 }, 0, 0, ff, sizeof ff);
        unsigned diff = 0;
        while (diff < L && trk[diff] == s_written[diff]) diff++;
        CHECK(diff == L, "DSKINI0's track 0 differs from the built one at byte %u", diff);
    }
    return 0;
}

static host_disk_t s_formatted;   /* d0 at the end, PROG and SWAP on it */

static int test_disk_basic(void) {
    static host_disk_t d0, d1, d2;
    memset(s_drives, 0, sizeof s_drives);
    {
        coco_config_t cfg;
        coco_config_default(&cfg);
        cfg.rsdos = true;
        coco_init(&g.m, &cfg);
        coco_load_rom(&g.m, 0xA000u, guest_rom_image(ROM_COCO_BAS13), 8192);
        coco_load_rom(&g.m, 0xC000u, guest_rom_image(ROM_COCO_DISK11), 8192);
        coco_cart_autostart(&g.m, false);
        coco_power_on(&g.m);
        keymatrix_init(&g.k);
        g.after_field = NULL;
        /* Without Extended, Color BASIC boots alone: Extended is what
         * looks for Disk BASIC at $C000, so the RS-DOS cartridge needs it
         * (roms.h). */
        CHECK(guest_boot(&g) && screen_has("COLOR BASIC 1.3") && !screen_has("DISK"),
              "without Extended, not Color BASIC alone");
    }
    for (unsigned k = 16; k <= 32; k += 16) {
        CHECK(disk_boot(k) && screen_has("DISK EXTENDED COLOR BASIC 1.1"),
              "Disk BASIC did not boot in %u KB", k);
    }
    host_disk_new(&d0);
    s_drives[0] = &d0;
    CHECK(disk_boot(64), "Disk BASIC did not boot");
    CHECK(screen_has("DISK EXTENDED COLOR BASIC 1.1"), "no Disk BASIC banner");

    /* An empty disk: DIR finds nothing to read and says so. */
    clear_screen();
    guest_type(&g, "DIR\r");
    wait_ok(60 * 30);
    CHECK(screen_has("?IO ERROR"), "DIR of an unformatted disk: no ?IO ERROR");

    if (dskini(&d0, 0)) return 1;
    CHECK(run("DIR\r", 600) && !screen_has("ERROR"), "DIR of a new disk");

    /* SAVE, DIR, NEW, LOAD, LIST, RUN. */
    clear_screen();
    guest_type(&g, "10 PRINT \"HELLO\"\r20 FOR I=1 TO 3:PRINT I*7:NEXT\r");
    CHECK(run("SAVE\"PROG\"\r", 600), "SAVE: no OK");
    CHECK(run("DIR\r", 600), "DIR: no OK");
    CHECK(guest_find_row(&g.m, "PROG     BAS  0 B 1", 0) >= 0, "DIR does not list PROG");
    CHECK(run("NEW\r", 60) && run("LOAD\"PROG\"\r", 600), "LOAD: no OK");
    CHECK(run("LIST\r", 60) && screen_has("10 PRINT \"HELLO\"") && screen_has("20 FOR I=1 TO 3"),
          "LIST after LOAD");
    CHECK(run("RUN\r", 120) && screen_has("HELLO") && screen_has(" 21"), "RUN after LOAD");
    /* A file not there. */
    run("LOAD\"NONE\"\r", 600);
    CHECK(screen_has("?NE ERROR"), "LOAD of a missing file: no ?NE ERROR");
    /* KILL, and the directory is empty again. */
    CHECK(run("KILL\"PROG.BAS\"\r", 600) && !screen_has("ERROR"), "KILL: no OK");
    CHECK(run("DIR\r", 600) && !screen_has("PROG"), "KILL");
    CHECK(run("SAVE\"PROG\"\r", 600), "SAVE again");

    /* BACKUP 0 TO 1, onto a disk DSKINI1 has formatted: the images the
     * same, byte for byte. */
    host_disk_new(&d1);
    s_drives[1] = &d1;
    host_disk_insert(&g.m, 1, &d1);
    if (dskini(&d1, 1)) return 1;
    clear_screen();
    guest_type(&g, "BACKUP 0 TO 1\r");
    CHECK(wait_ok(60 * 120), "BACKUP: no OK");

    CHECK(d1.len == d0.len && memcmp(d1.img, d0.img, d0.len) == 0, "BACKUP: the copy differs");
    CHECK(run("DIR1\r", 600) && screen_has("PROG"), "DIR1 after BACKUP");

    /* Write-protected: SAVE onto it is refused. */
    d1.protect = true;
    host_disk_insert(&g.m, 1, &d1);
    run("SAVE\"PROG2:1\"\r", 600);
    CHECK(screen_has("?WP ERROR"), "SAVE to a protected disk: no ?WP ERROR");
    d1.protect = false;
    host_disk_insert(&g.m, 1, &d1);

    /* A disk swapped between fields: another directory. */
    host_disk_copy(&d2, &d0);
    CHECK(run("NEW\r", 60), "NEW");
    guest_type(&g, "10 PRINT 6*7\r");
    CHECK(run("SAVE\"SWAP\"\r", 600), "SAVE SWAP");
    s_drives[0] = &d2;
    host_disk_insert(&g.m, 0, &d2);
    CHECK(run("DIR\r", 600) && !screen_has("SWAP") && screen_has("PROG"), "DIR of the swapped disk");
    s_drives[0] = &d0;
    host_disk_insert(&g.m, 0, &d0);
    CHECK(run("DIR\r", 600) && screen_has("SWAP"), "DIR of the first disk again");

    host_disk_copy(&s_formatted, &d0);
    host_disk_free(&d0);
    host_disk_free(&d1);
    host_disk_free(&d2);
    return 0;
}

/* Save states with the cartridge (§12.5): refused while a command or a
 * track is in hand; taken between commands, mid-program, from a
 * machine reading the disk sector by sector; restored into a machine
 * doing something else, with its own copy of the disk, the track in
 * hand fetched again before it runs: every byte of state the same
 * straight after and 150 fields on. The control is the same with the
 * track in hand not fetched again, which must differ. */
static guest_t g2;
static host_disk_t *s_drives2[COCO_DISK_DRIVES];
static void serve_fields2(guest_t *gg) { host_disk_serve(&gg->m, s_drives2); }

static int snapshots(const host_disk_t *formatted, bool control) {
    static mem_t snap;
    static host_disk_t a, b;
    host_disk_copy(&a, formatted);
    memset(s_drives, 0, sizeof s_drives);
    s_drives[0] = &a;
    CHECK(disk_boot(64), "snapshots: no boot");
    clear_screen();
    guest_type(&g, "5 CLEAR 1000\r");
    guest_type(&g, "10 DSKI$ 0,17,3+(N AND 7),A$,B$:N=N+1:PRINT N;:GOTO 10\r");
    guest_fields(&g, 10);
    guest_type(&g, "RUN\r");

    /* A field boundary with a command running is refused; then one
     * between commands is taken. */
    bool refused = false, taken = false;
    for (int f = 0; f < 600 && !taken; f++) {
        guest_fields(&g, 1);
        snap_status_t st = mem_save(&snap, &g.m, NULL);
        if (coco_disk_busy(&g.m)) {
            CHECK(st == SNAP_BUSY, "saved with the disk busy: %s", snapshot_status_str(st));
            refused = true;
        } else if (refused && f > 120) {
            CHECK(st == SNAP_OK, "not saved between commands: %s", snapshot_status_str(st));
            taken = st == SNAP_OK;
        }
    }
    CHECK(refused && taken, "refused %d, taken %d", refused, taken);
    if (!(refused && taken)) guest_dump(&g.m, stderr);
    if (!taken) return 1;

    /* Another machine, at BASIC's prompt with something else on screen. */
    host_disk_copy(&b, &a);
    memset(s_drives2, 0, sizeof s_drives2);
    s_drives2[0] = &b;
    guest_build_disk(&g2, 64);
    g2.after_field = serve_fields2;
    host_disk_insert(&g2.m, 0, &b);
    CHECK(guest_boot(&g2), "the second machine did not boot");
    guest_type(&g2, "PRINT 12345\r");
    snap_info_t info;
    CHECK(mem_check(&snap, &g2.m, &info, NULL) == SNAP_OK && info.rsdos, "the state's check");
    CHECK(mem_load(&snap, &g2.m) == SNAP_OK, "the state's load");
    if (control) g2.m.dk.fdc.req.get = false;
    host_disk_serve(&g2.m, s_drives2);
    bool same = snap_same(&g.m, &g2.m, control ? "control, straight after" : "straight after");
    for (int f = 0; f < 150 && same; f++) {
        guest_fields(&g, 1);
        guest_fields(&g2, 1);
        same = snap_same(&g.m, &g2.m, "150 fields on");
    }
    if (control) {
        CHECK(!same, "control: the track not fetched again, and still the same");
    } else {
        CHECK(same, "the restored machine went its own way");
        CHECK(g.m.dk.fdc.sectors_read > 20, "it was not reading the disk: %u sectors",
              (unsigned)g.m.dk.fdc.sectors_read);
        printf("snapshots: %u sectors read by the end\n", (unsigned)g.m.dk.fdc.sectors_read);
    }
    host_disk_free(&a);
    host_disk_free(&b);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (test_mfm()) return 1;
    if (test_jvc()) return 1;
    if (test_wd1793()) return 1;

    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_COCO_EXTBAS11) ||
        !guest_have_rom(ROM_COCO_DISK11)) {
        printf("SKIP: Disk BASIC needs bas13, extbas11 and disk11 in %s\n", dir);
        if (test_failures) TEST_DONE();
        return TEST_SKIP_CODE;
    }
    if (test_disk_basic()) return 1;
    if (snapshots(&s_formatted, false)) return 1;
    if (snapshots(&s_formatted, true)) return 1;
    TEST_DONE();
}
