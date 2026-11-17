/* snapshot.c — our own save states (design.md §12.5; the format is in
 * snapshot.h). pico-oric's, with the CoCo's fields. */

#include "snapshot.h"

#include <string.h>

#include "mc6809.h"
#include "sha1.h"

static const uint8_t magic[8] = { 'C', 'O', 'C', 'O', 'S', 'N', 'A', 'P' };

/* The state section, a piece of RAM in transit and the media, here
 * rather than on the caller's stack: the port calls these on core 1,
 * whose stack is the SDK's 2 KiB, with the card's driver below them. One
 * call at a time. */
static uint8_t s_st[SNAP_STATE_LEN];
static uint8_t s_piece[256];
static uint8_t s_media[SNAP_MEDIA_LEN];

/* ---- CRC-32 ---------------------------------------------------------- */

uint32_t snapshot_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---- little-endian fields ---------------------------------------------- */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }
static uint64_t get64(const uint8_t *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }

/* ---- the state section ------------------------------------------------ */

enum {
    /* The 6809 (mc6809.h). Its IRQ and FIRQ follow from the PIAs, and
     * its attention from everything, so neither is stored. */
    S_PC = 0, S_X = 2, S_Y = 4, S_U = 6, S_S = 8,
    S_A = 10, S_B, S_DP, S_CC,
    S_CYCLES = 14,                /* 8 bytes                             */
    S_INSTRUCTIONS = 22,          /* 8 bytes                             */
    S_CPU_FLAGS = 30,             /* C_ bits below                       */
    S_WAIT = 31, S_LATCH = 32, S_ACTIVE = 33,
    S_UNDOC_COUNT = 34,           /* 4 bytes                             */
    S_UNDOC_PC = 38, S_UNDOC_OP = 40,
    /* The PIAs (mc6821.h): PIA0 A, B, PIA1 A, B, each out ddr cr in and
     * the P_ lines. */
    S_PIA = 42,
    S_PIA_PORT_LEN = 5,
    /* The SAM (sam6883.h). */
    S_SAM = 62,                   /* 2 bytes                             */
    /* Time (coco.h): the tick count at the cycle count cyc0, the ticks a
     * cycle takes, the field's start, HS's account, the rises due. */
    S_TICK0 = 64, S_CYC0 = 72,    /* 8 bytes each                        */
    S_TPC = 80,
    S_FIELD_START = 82, S_HS_SEEN = 90,
    S_IRQ_DUE = 98,               /* COCO_PIA_PORTS x 8 bytes            */
    S_FIELDS = 130,
    S_SOUND_LEVEL = 134, S_SOUND_EDGES = 138,
    S_VIDEO_BITS = 142, S_SPLIT_FIELDS = 146,
    S_M_FLAGS = 150,              /* F_ bits below                       */
    S_TAPE_HOT = 152,             /* the trap point page $A7 is slow for */
    /* The machine it runs on: what a power-on chooses. S_MACHINE is 0,
     * the CoCo; M3 to M11 also wrote 1 for a Dragon 32 (D6). */
    S_MACHINE = 154, S_RAM_K = 155,
    S_FITTED = 156,               /* M_ bits below                       */
    /* The field's shape (§13.1): the build's, config.h's constants.
     * S_FIELD_LEAD is 0; it was a PAL field's (D6). */
    S_LINE_TICKS = 158,           /* 4 bytes                             */
    S_FIELD_LINES = 162, S_FS_FALL = 164, S_ACTIVE_LINE = 166,
    S_HS_LOW = 168, S_FIELD_LEAD = 170,
    /* SHA-1 of the ROM halves at $8000 and $A000 and of the cartridge's
     * window, zero where there is none. */
    S_ROM_LO = 172, S_ROM_HI = 192, S_CART = 212,
    S_END = 232,                  /* the rest is reserved, written zero  */
};

_Static_assert(S_PIA + COCO_PIA_PORTS * S_PIA_PORT_LEN == S_SAM, "the PIAs overrun");
_Static_assert(S_IRQ_DUE + 8 * COCO_PIA_PORTS == S_FIELDS, "the rises due overrun");
_Static_assert(S_CART + SHA1_DIGEST_LEN == S_END, "the hashes overrun");
_Static_assert(S_END <= SNAP_STATE_LEN, "the state section has outgrown its length");

#define C_NMI_LINE     0x01u
#define C_NMI_PENDING  0x02u
#define C_NMI_ARMED    0x04u
#define C_HALT         0x08u
#define C_STALE        0x10u

/* A port's lines, as they are, so that zero is not their reset value. */
#define P_C1           0x01u
#define P_C2           0x02u
#define P_IRQ          0x04u

#define F_SOUND_FED    0x01u
#define F_VIDEO_SPLIT  0x02u

#define M_ROM_LO       0x01u
#define M_ROM_HI       0x02u
#define M_CART         0x04u
#define M_AUTOSTART    0x08u
#define M_TAPE_TRAP    0x10u
#define M_ALL          0x1Fu

/* The media section (snapshot.h): the paths, then the deck's place. */
#define MD_TAPE        0u
#define MD_CART        COCO_PATH_MAX
#define MD_DISK        (2u * COCO_PATH_MAX)
#define MD_PATHS       (2u + COCO_SNAP_DRIVES)
#define MD_TAPE_POS    (MD_PATHS * COCO_PATH_MAX)
#define MD_TAPE_REWIND (MD_TAPE_POS + 4u)
#define MD_FLAGS       (MD_TAPE_POS + 8u)
_Static_assert(MD_FLAGS + 8u == SNAP_MEDIA_LEN, "the media section's length");

#define DK_CHOSEN      0x01u

/* Ticks a boundary can stand past the point it was due at: the longest
 * instruction with an interrupt's entry, at the slow rate, with room to
 * spare. A field is a long way from it. */
#define SLACK_TICKS    4096u

static uint8_t fitted_of(const coco_t *m) {
    return (uint8_t)((m->rom_present[0] ? M_ROM_LO : 0) | (m->rom_present[1] ? M_ROM_HI : 0) |
                     (m->cart_present ? M_CART : 0) |
                     (m->cart_present && m->cart_autostart ? M_AUTOSTART : 0) |
                     (m->cfg.tape_trap ? M_TAPE_TRAP : 0));
}

/* The three hashes as a state has them: zero for a half or a slot with
 * nothing in it, whatever coco_t's bytes hold there. */
static void rom_hashes(const coco_t *m, uint8_t *st) {
    memset(st + S_ROM_LO, 0, 3u * SHA1_DIGEST_LEN);
    if (m->rom_present[0]) sha1(m->rom, COCO_ROM_HALF, st + S_ROM_LO);
    if (m->rom_present[1]) sha1(m->rom + COCO_ROM_HALF, COCO_ROM_HALF, st + S_ROM_HI);
    if (m->cart_present) sha1(m->cart, COCO_CART_SIZE, st + S_CART);
}

static mc6821_port_t *port_of(coco_t *m, unsigned i) {
    mc6821_t *p = i < 2u ? &m->pia0 : &m->pia1;
    return (i & 1u) ? &p->b : &p->a;
}

static const mc6821_port_t *cport_of(const coco_t *m, unsigned i) {
    const mc6821_t *p = i < 2u ? &m->pia0 : &m->pia1;
    return (i & 1u) ? &p->b : &p->a;
}

static void state_encode(const coco_t *m, uint8_t st[SNAP_STATE_LEN]) {
    memset(st, 0, SNAP_STATE_LEN);

    const mc6809_t *c = &m->cpu;
    put16(st + S_PC, c->pc);
    put16(st + S_X, c->x);
    put16(st + S_Y, c->y);
    put16(st + S_U, c->u);
    put16(st + S_S, c->s);
    st[S_A] = c->a; st[S_B] = c->b; st[S_DP] = c->dp; st[S_CC] = c->cc;
    put64(st + S_CYCLES, c->cycles);
    put64(st + S_INSTRUCTIONS, c->instructions);
    st[S_CPU_FLAGS] = (uint8_t)((c->nmi_line ? C_NMI_LINE : 0) | (c->nmi_pending ? C_NMI_PENDING : 0) |
                                (c->nmi_armed ? C_NMI_ARMED : 0) | (c->halt ? C_HALT : 0) |
                                (c->stale ? C_STALE : 0));
    st[S_WAIT] = c->wait;
    st[S_LATCH] = c->latch;
    st[S_ACTIVE] = c->active;
    put32(st + S_UNDOC_COUNT, c->undoc_count);
    put16(st + S_UNDOC_PC, c->undoc_pc);
    put16(st + S_UNDOC_OP, c->undoc_op);

    for (unsigned i = 0; i < COCO_PIA_PORTS; i++) {
        const mc6821_port_t *p = cport_of(m, i);
        uint8_t *q = st + S_PIA + i * S_PIA_PORT_LEN;
        q[0] = p->out; q[1] = p->ddr; q[2] = p->cr; q[3] = p->in;
        q[4] = (uint8_t)((p->c1 ? P_C1 : 0) | (p->c2 ? P_C2 : 0) | (p->irq_out ? P_IRQ : 0));
    }
    put16(st + S_SAM, m->sam.reg);

    put64(st + S_TICK0, m->tick0);
    put64(st + S_CYC0, m->cyc0);
    st[S_TPC] = (uint8_t)m->tpc;
    put64(st + S_FIELD_START, m->field_start);
    put64(st + S_HS_SEEN, m->hs_seen);
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++) put64(st + S_IRQ_DUE + 8u * i, m->irq_due[i]);
    put32(st + S_FIELDS, m->fields);
    put32(st + S_SOUND_LEVEL, m->sound_level);
    put32(st + S_SOUND_EDGES, m->sound_edges);
    put32(st + S_VIDEO_BITS, m->video_bits);
    put32(st + S_SPLIT_FIELDS, m->split_fields);
    st[S_M_FLAGS] = (uint8_t)((m->sound_fed ? F_SOUND_FED : 0) | (m->video_split ? F_VIDEO_SPLIT : 0));
    put16(st + S_TAPE_HOT, m->tape.hot);

    st[S_MACHINE] = 0u;
    st[S_RAM_K] = (uint8_t)m->cfg.ram_k;
    st[S_FITTED] = fitted_of(m);
    put32(st + S_LINE_TICKS, COCO_LINE_TICKS);
    put16(st + S_FIELD_LINES, COCO_FIELD_LINES);
    put16(st + S_FS_FALL, COCO_FS_FALL_LINE);
    put16(st + S_ACTIVE_LINE, COCO_ACTIVE_LINE);
    put16(st + S_HS_LOW, COCO_HS_LOW_TICKS);
    put16(st + S_FIELD_LEAD, 0u);
    rom_hashes(m, st);
}

/* A path into its field, NUL-padded; one too long for the field is
 * written empty, which a port never keeps (COCO_PATH_MAX). */
static void put_path(uint8_t *p, const char *path) {
    memset(p, 0, COCO_PATH_MAX);
    size_t n = strlen(path);
    if (n < COCO_PATH_MAX) memcpy(p, path, n);
}

static void media_encode(const snap_media_t *md, uint8_t out[SNAP_MEDIA_LEN]) {
    memset(out, 0, SNAP_MEDIA_LEN);
    if (!md) return;
    put_path(out + MD_TAPE, md->tape);
    put_path(out + MD_CART, md->cart);
    for (unsigned d = 0; d < COCO_SNAP_DRIVES; d++) put_path(out + MD_DISK + d * COCO_PATH_MAX, md->disk[d]);
    put32(out + MD_TAPE_POS, md->tape_pos);
    put32(out + MD_TAPE_REWIND, md->tape_rewind);
    out[MD_FLAGS] = md->tape_chosen ? DK_CHOSEN : 0;
}

/* Every path ends inside its field, and the flags are known ones. */
static bool media_plausible(const uint8_t in[SNAP_MEDIA_LEN]) {
    for (unsigned i = 0; i < MD_PATHS; i++)
        if (!memchr(in + i * COCO_PATH_MAX, 0, COCO_PATH_MAX)) return false;
    return (in[MD_FLAGS] & ~DK_CHOSEN) == 0;
}

static void media_decode(const uint8_t in[SNAP_MEDIA_LEN], snap_media_t *md) {
    memset(md, 0, sizeof *md);
    memcpy(md->tape, in + MD_TAPE, COCO_PATH_MAX);
    memcpy(md->cart, in + MD_CART, COCO_PATH_MAX);
    for (unsigned d = 0; d < COCO_SNAP_DRIVES; d++)
        memcpy(md->disk[d], in + MD_DISK + d * COCO_PATH_MAX, COCO_PATH_MAX);
    md->tape_pos = get32(in + MD_TAPE_POS);
    md->tape_rewind = get32(in + MD_TAPE_REWIND);
    md->tape_chosen = (in[MD_FLAGS] & DK_CHOSEN) != 0;
}

/* ---- the stream -------------------------------------------------------- */

snap_status_t snapshot_save(const coco_t *m, const snap_media_t *media, snap_write_fn write,
                            void *ctx) {
    /* Nor while the signal runs: its place within a byte is not state
     * (cassette.h). */
    if (m->tape.op != TAPE_NONE || coco_cassette_running(m)) return SNAP_BUSY;

    uint8_t *st = s_st;
    state_encode(m, st);
    media_encode(media, s_media);

    /* The CRC goes in the header, ahead of what it covers; the machine is
     * the buffer, and nothing changes it in between. */
    uint32_t crc = snapshot_crc32(0, st, SNAP_STATE_LEN);
    crc = snapshot_crc32(crc, m->ram, sizeof m->ram);
    crc = snapshot_crc32(crc, s_media, SNAP_MEDIA_LEN);

    uint8_t hdr[SNAP_HEADER_LEN];
    memcpy(hdr, magic, sizeof magic);
    put16(hdr + 8, SNAP_VERSION);
    put16(hdr + 10, SNAP_HEADER_LEN);
    put32(hdr + 12, SNAP_PAYLOAD_LEN);
    put32(hdr + 16, crc);

    if (!write(ctx, hdr, sizeof hdr)) return SNAP_IO;
    if (!write(ctx, st, SNAP_STATE_LEN)) return SNAP_IO;
    for (uint32_t off = 0; off < sizeof m->ram; off += 4096u)
        if (!write(ctx, m->ram + off, 4096u)) return SNAP_IO;
    if (!write(ctx, s_media, SNAP_MEDIA_LEN)) return SNAP_IO;
    return SNAP_OK;
}

static snap_status_t read_header(snap_read_fn read, void *ctx, uint32_t *crc) {
    uint8_t hdr[SNAP_HEADER_LEN];
    if (!read(ctx, hdr, sizeof hdr)) return SNAP_IO;
    if (memcmp(hdr, magic, sizeof magic) != 0) return SNAP_NOT_SNAPSHOT;
    unsigned v = get16(hdr + 8);
    if (v > SNAP_VERSION) return SNAP_NEWER;
    if (v == 0 || get16(hdr + 10) != SNAP_HEADER_LEN || get32(hdr + 12) != SNAP_PAYLOAD_LEN)
        return SNAP_NOT_SNAPSHOT;
    *crc = get32(hdr + 16);
    return SNAP_OK;
}

static void info_of(const uint8_t st[SNAP_STATE_LEN], snap_info_t *info) {
    if (!info) return;
    uint8_t f = st[S_FITTED];
    info->ram_k = st[S_RAM_K];
    info->rom_present[0] = (f & M_ROM_LO) != 0;
    info->rom_present[1] = (f & M_ROM_HI) != 0;
    info->rom[0] = info->rom_present[0] ? romset_identify_digest(st + S_ROM_LO, COCO_ROM_HALF) : ROM_UNKNOWN;
    info->rom[1] = info->rom_present[1] ? romset_identify_digest(st + S_ROM_HI, COCO_ROM_HALF) : ROM_UNKNOWN;
    info->cart = (f & M_CART) != 0;
    info->cart_autostart = (f & M_AUTOSTART) != 0;
    info->tape_trap = (f & M_TAPE_TRAP) != 0;
}

/* A signed distance between two tick counts. */
static int64_t dist(uint64_t a, uint64_t b) {
    return (int64_t)(a - b);
}

/* Could a machine have saved this? A file passes its CRC whoever wrote
 * it, and the machine trusts its fields: a time far from the field's
 * keeps core 0 in one run for hours, a tick rate other than the SAM's
 * runs the field at the wrong length, and a trap point off its page
 * slows the wrong page (EL §8.5, the Oric's lesson). So every field the
 * code divides, indexes, loops or waits by must be in the range a
 * running machine keeps it in. */
static bool plausible(const uint8_t st[SNAP_STATE_LEN]) {
    unsigned ram_k = st[S_RAM_K];
    if (ram_k != 4u && ram_k != 16u && ram_k != 32u && ram_k != 64u) return false;
    if (st[S_MACHINE] != 0u || (st[S_FITTED] & ~M_ALL)) return false;   /* a Dragon's is not ours */
    if ((st[S_FITTED] & M_AUTOSTART) && !(st[S_FITTED] & M_CART)) return false;
    if (st[S_CPU_FLAGS] & ~(C_NMI_LINE | C_NMI_PENDING | C_NMI_ARMED | C_HALT | C_STALE)) return false;
    if (st[S_WAIT] > MC6809_HCF || (st[S_M_FLAGS] & ~(F_SOUND_FED | F_VIDEO_SPLIT))) return false;
    if ((st[S_LATCH] | st[S_ACTIVE]) & ~(MC6809_LINE_IRQ | MC6809_LINE_FIRQ | MC6809_LINE_NMI))
        return false;
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++)
        if (st[S_PIA + i * S_PIA_PORT_LEN + 4u] & ~(P_C1 | P_C2 | P_IRQ)) return false;
    if (get32(st + S_SOUND_LEVEL) > COCO_SOUND_LEVEL_MAX) return false;
    uint16_t hot = get16(st + S_TAPE_HOT);
    if (hot != 0 && (hot >> 8) != TAPE_PAGE) return false;

    /* The ticks a cycle takes are the SAM's rate's (coco.c's retime). */
    unsigned rate = (get16(st + S_SAM) & SAM_R_MASK) >> 11;
    if (st[S_TPC] != ((rate & 2u) ? 8u : 16u)) return false;

    /* The field's shape is checked against the build's (compatible);
     * here, only that the arithmetic on it is sound. */
    uint32_t line = get32(st + S_LINE_TICKS);
    uint32_t lines = get16(st + S_FIELD_LINES);
    if (line == 0 || line > 0x10000u || lines == 0) return false;
    uint64_t field = (uint64_t)line * lines;

    /* Now, from the cycle count: no further from cyc0 than 2^48 cycles,
     * nearly ten years of guest time at the fast rate. */
    uint64_t cycles = get64(st + S_CYCLES), cyc0 = get64(st + S_CYC0);
    if (cyc0 > cycles || cycles - cyc0 > ((uint64_t)1 << 48)) return false;
    uint64_t now = get64(st + S_TICK0) + (cycles - cyc0) * st[S_TPC];

    /* Within the field that began at field_start, or a boundary past its
     * end; HS accounted to about now; each rise due, or none, near now. */
    int64_t into = dist(now, get64(st + S_FIELD_START));
    if (into < 0 || into > (int64_t)(field + SLACK_TICKS)) return false;
    int64_t hs = dist(get64(st + S_HS_SEEN), now);
    if (hs < -(int64_t)field || hs > (int64_t)SLACK_TICKS) return false;
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++) {
        uint64_t due = get64(st + S_IRQ_DUE + 8u * i);
        if (!due) continue;
        int64_t d = dist(due, now);
        if (d < -(int64_t)field || d > (int64_t)SLACK_TICKS) return false;
    }
    return true;
}

/* The field's shape, the build's (config.h). */
static bool same_field(const uint8_t st[SNAP_STATE_LEN]) {
    return get32(st + S_LINE_TICKS) == COCO_LINE_TICKS && get16(st + S_FIELD_LINES) == COCO_FIELD_LINES &&
           get16(st + S_FS_FALL) == COCO_FS_FALL_LINE && get16(st + S_ACTIVE_LINE) == COCO_ACTIVE_LINE &&
           get16(st + S_HS_LOW) == COCO_HS_LOW_TICKS && get16(st + S_FIELD_LEAD) == 0u;
}

/* Would this state resume on this machine? The field first, which is
 * the build's: a state refused for anything after it resumes on a
 * machine powered on as its info says. Then the BASIC ROMs, the RAM,
 * and the cartridge, its CART line and the trap. */
static snap_status_t compatible(const coco_t *m, const uint8_t st[SNAP_STATE_LEN]) {
    if (!plausible(st)) return SNAP_NOT_SNAPSHOT;
    if (!same_field(st)) return SNAP_OTHER_FIELD;
    static uint8_t mine[SNAP_STATE_LEN];
    rom_hashes(m, mine);
    uint8_t fitted = fitted_of(m);
    if (((st[S_FITTED] ^ fitted) & (M_ROM_LO | M_ROM_HI)) ||
        memcmp(st + S_ROM_LO, mine + S_ROM_LO, 2u * SHA1_DIGEST_LEN) != 0)
        return SNAP_OTHER_ROM;
    if (st[S_RAM_K] != (uint8_t)m->cfg.ram_k) return SNAP_OTHER_RAM;
    if (st[S_FITTED] != fitted || memcmp(st + S_CART, mine + S_CART, SHA1_DIGEST_LEN) != 0)
        return SNAP_OTHER_MACHINE;
    return SNAP_OK;
}

snap_status_t snapshot_check(const coco_t *m, snap_read_fn read, void *ctx, snap_info_t *info,
                             snap_media_t *media) {
    uint32_t want;
    snap_status_t r = read_header(read, ctx, &want);
    if (r != SNAP_OK) return r;

    uint8_t *st = s_st;
    if (!read(ctx, st, SNAP_STATE_LEN)) return SNAP_IO;
    uint32_t crc = snapshot_crc32(0, st, SNAP_STATE_LEN);
    for (uint32_t left = COCO_ADDR_SPACE; left; left -= sizeof s_piece) {
        if (!read(ctx, s_piece, sizeof s_piece)) return SNAP_IO;
        crc = snapshot_crc32(crc, s_piece, sizeof s_piece);
    }
    if (!read(ctx, s_media, SNAP_MEDIA_LEN)) return SNAP_IO;
    crc = snapshot_crc32(crc, s_media, SNAP_MEDIA_LEN);
    if (crc != want) return SNAP_CORRUPT;
    info_of(st, info);
    if (!media_plausible(s_media)) return SNAP_NOT_SNAPSHOT;
    if (media) media_decode(s_media, media);
    return compatible(m, st);
}

snap_status_t snapshot_load(coco_t *m, snap_read_fn read, void *ctx) {
    if (m->tape.op != TAPE_NONE) return SNAP_BUSY;
    uint32_t want;
    snap_status_t r = read_header(read, ctx, &want);
    if (r != SNAP_OK) return r;
    uint8_t *st = s_st;
    if (!read(ctx, st, SNAP_STATE_LEN)) return SNAP_IO;
    r = compatible(m, st);
    if (r != SNAP_OK) return r;

    /* From here the machine changes. snapshot_check has read these same
     * bytes and found them whole; a read failing now is the card going
     * away between the passes, and leaves a machine that needs a power-on.
     * The media follow the RAM; they are the port's, which has them from
     * snapshot_check, so the load stops at the RAM. */
    if (!read(ctx, m->ram, sizeof m->ram)) return SNAP_TORN;

    mc6809_t *c = &m->cpu;
    c->pc = get16(st + S_PC);
    c->x = get16(st + S_X);
    c->y = get16(st + S_Y);
    c->u = get16(st + S_U);
    c->s = get16(st + S_S);
    c->a = st[S_A]; c->b = st[S_B]; c->dp = st[S_DP]; c->cc = st[S_CC];
    c->cycles = get64(st + S_CYCLES);
    c->instructions = get64(st + S_INSTRUCTIONS);
    uint8_t cf = st[S_CPU_FLAGS];
    c->nmi_line = (cf & C_NMI_LINE) != 0;
    c->nmi_pending = (cf & C_NMI_PENDING) != 0;
    c->nmi_armed = (cf & C_NMI_ARMED) != 0;
    c->halt = (cf & C_HALT) != 0;
    c->stale = (cf & C_STALE) != 0;
    c->wait = st[S_WAIT];
    c->latch = st[S_LATCH];
    c->active = st[S_ACTIVE];
    c->undoc_count = get32(st + S_UNDOC_COUNT);
    c->undoc_pc = get16(st + S_UNDOC_PC);
    c->undoc_op = get16(st + S_UNDOC_OP);
    c->io_at = 0;
    c->run_limit = 0;

    for (unsigned i = 0; i < COCO_PIA_PORTS; i++) {
        mc6821_port_t *p = port_of(m, i);
        const uint8_t *q = st + S_PIA + i * S_PIA_PORT_LEN;
        p->out = q[0]; p->ddr = q[1]; p->cr = q[2]; p->in = q[3];
        p->c1 = (q[4] & P_C1) != 0;
        p->c2 = (q[4] & P_C2) != 0;
        p->irq_out = (q[4] & P_IRQ) != 0;
    }
    m->sam.reg = get16(st + S_SAM);

    m->tick0 = get64(st + S_TICK0);
    m->cyc0 = get64(st + S_CYC0);
    m->tpc = st[S_TPC];
    m->field_start = get64(st + S_FIELD_START);
    m->hs_seen = get64(st + S_HS_SEEN);
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++) m->irq_due[i] = get64(st + S_IRQ_DUE + 8u * i);
    m->fields = get32(st + S_FIELDS);
    m->sound_level = get32(st + S_SOUND_LEVEL);
    m->sound_edges = get32(st + S_SOUND_EDGES);
    m->video_bits = get32(st + S_VIDEO_BITS);
    m->split_fields = get32(st + S_SPLIT_FIELDS);
    m->sound_fed = (st[S_M_FLAGS] & F_SOUND_FED) != 0;
    m->video_split = (st[S_M_FLAGS] & F_VIDEO_SPLIT) != 0;
    m->tape.hot = get16(st + S_TAPE_HOT);

    coco_restored(m);
    return SNAP_OK;
}

const char *snapshot_status_str(snap_status_t st) {
    switch (st) {
    case SNAP_OK:            return "OK";
    case SNAP_IO:            return "READ OR WRITE FAILED";
    case SNAP_NOT_SNAPSHOT:  return "NOT A SAVE STATE";
    case SNAP_NEWER:         return "FROM A NEWER VERSION";
    case SNAP_CORRUPT:       return "DAMAGED (CRC)";
    case SNAP_OTHER_FIELD:   return "ANOTHER FIELD TIMING";
    case SNAP_OTHER_ROM:     return "FOR ANOTHER ROM";
    case SNAP_OTHER_RAM:     return "FOR ANOTHER RAM SIZE";
    case SNAP_OTHER_MACHINE: return "FOR ANOTHER MACHINE";
    case SNAP_BUSY:          return "THE TAPE IS BUSY";
    case SNAP_NO_ROM:        return "A ROM IT NEEDS IS MISSING";
    case SNAP_NO_CART:       return "ITS CARTRIDGE IS MISSING";
    case SNAP_UNSAVED:       return "A RECORDING IS NOT SAVED";
    case SNAP_TORN:          return "CUT SHORT WHILE LOADING";
    }
    return "?";
}
