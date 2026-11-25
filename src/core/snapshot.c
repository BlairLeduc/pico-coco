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
static uint8_t s_port[SNAP_PORT_LEN];
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
    /* SHA-1 of the ROM halves at $8000 and $A000 and of slot 1's
     * window, zero where there is none. */
    S_ROM_LO = 172, S_ROM_HI = 192, S_CART = 212,
    /* The RS-DOS controller (rsdos.h), in any slot, between commands: its
     * latch and lines, the WD1793's registers, which drive and side the
     * latch selects, each drive's head and when the head unloads. The
     * disks are the port's, and the track in hand is fetched again. */
    S_DK_LATCH = 232, S_DK_FLAGS = 233,
    S_DK_STATUS = 234, S_DK_TRACK, S_DK_SECTOR, S_DK_DATA, S_DK_CMD,
    S_DK_DRIVE = 239, S_DK_SIDE = 240,
    S_DK_CYL = 241,               /* COCO_DISK_DRIVES bytes              */
    S_DK_HEAD_UNTIL = 245,        /* 8 bytes                             */
    /* Which track the chip had in hand, asked for again on a load and
     * served before the guest runs, so that the next command finds it
     * as the saved machine's did (wd_restored). */
    S_DK_BUF = 253, S_DK_BUF_CYL = 254,
    S_END = 255,                  /* the rest is reserved, written zero  */
};

_Static_assert(S_PIA + COCO_PIA_PORTS * S_PIA_PORT_LEN == S_SAM, "the PIAs overrun");
_Static_assert(S_IRQ_DUE + 8 * COCO_PIA_PORTS == S_FIELDS, "the rises due overrun");
_Static_assert(S_CART + SHA1_DIGEST_LEN == S_DK_LATCH, "the hashes overrun");
_Static_assert(S_DK_CYL + COCO_DISK_DRIVES == S_DK_HEAD_UNTIL, "the heads overrun");
_Static_assert(S_DK_HEAD_UNTIL + 8 == S_DK_BUF, "the cartridge overruns");
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

/* M_CART and M_AUTOSTART are slot 1's, the port's without a Multi-Pak;
 * M_RSDOS is the controller's, in whichever slot. Version 1 had no
 * Multi-Pak, so they meant the same there. */
#define M_ROM_LO       0x01u
#define M_ROM_HI       0x02u
#define M_CART         0x04u
#define M_AUTOSTART    0x08u
#define M_TAPE_TRAP    0x10u
#define M_RSDOS        0x20u
#define M_ALL          0x3Fu

/* ---- the port section (version 2) -------------------------------------- */

enum {
    P_FLAGS = 0,                  /* PF_ bits below                      */
    P_SWITCH = 1,                 /* the front switch, 0 to 3            */
    P_CTS = 2, P_SCS = 3,         /* $FF7F as it stands                  */
    /* Each slot's, P_SLOT_LEN bytes from P_SLOT: its kind (cart_kind_t),
     * PS_ flags, its window's SHA-1, zero with no ROM, and a SmartWatch's
     * set, seconds ahead of the host's clock (smartwatch.h). */
    P_SLOT = 16, P_SLOT_LEN = 32,
    PS_KIND = 0, PS_FLAGS = 1, PS_HASH = 2, PS_OFFSET = 22, PS_END = 30,
    P_END = P_SLOT + COCO_MPI_SLOTS * P_SLOT_LEN,   /* the rest reserved   */
};
_Static_assert(PS_HASH + SHA1_DIGEST_LEN == PS_OFFSET, "a slot's hash overruns");
_Static_assert(PS_END <= P_SLOT_LEN, "a slot overruns");
_Static_assert(P_END <= SNAP_PORT_LEN, "the port section has outgrown its length");

#define PF_MPI         0x01u
#define PF_SWITCH_ON   0x02u      /* no write to $FF7F since RESET       */
#define PSF_ROM        0x01u
#define PSF_AUTOSTART  0x02u
/* A SmartWatch's set: within the years it holds of any host time. */
#define SET_MAX        ((int64_t)(SMARTWATCH_LAST - SMARTWATCH_FIRST) + (int64_t)UINT32_MAX)

/* The cartridge's lines and the chip's flags. */
#define D_HALT_ENABLE  0x01u
#define D_INTRQ        0x02u
#define D_NMI          0x04u
#define D_FDC_INTRQ    0x08u
#define D_FDC_DRQ      0x10u
#define D_TYPE1        0x20u
#define D_HOLD_INTRQ   0x40u
#define D_INDEX_INTRQ  0x80u

/* The media section (snapshot.h): the paths, then the deck's place,
 * then version 2's slots 2 to 4. */
#define MD_TAPE        0u
#define MD_CART        COCO_PATH_MAX
#define MD_DISK        (2u * COCO_PATH_MAX)
#define MD_PATHS       (2u + COCO_SNAP_DRIVES)
#define MD_TAPE_POS    (MD_PATHS * COCO_PATH_MAX)
#define MD_TAPE_REWIND (MD_TAPE_POS + 4u)
#define MD_FLAGS       (MD_TAPE_POS + 8u)
#define MD_SLOTS       (MD_FLAGS + 8u)
#define MD_MORE        (COCO_MPI_SLOTS - 1u)
_Static_assert(MD_SLOTS == SNAP_MEDIA_V1_LEN, "version 1's media section's length");
_Static_assert(MD_SLOTS + MD_MORE * COCO_PATH_MAX == SNAP_MEDIA_LEN, "the media section's length");

#define DK_CHOSEN      0x01u

/* Ticks a boundary can stand past the point it was due at: the longest
 * instruction with an interrupt's entry, at the slow rate, with room to
 * spare. A field is a long way from it. */
#define SLACK_TICKS    4096u

static uint8_t fitted_of(const coco_t *m) {
    const cart_t *c = &m->port.slot[0];
    return (uint8_t)((m->rom_present[0] ? M_ROM_LO : 0) | (m->rom_present[1] ? M_ROM_HI : 0) |
                     (c->rom ? M_CART : 0) | (c->rom && c->autostart ? M_AUTOSTART : 0) |
                     (m->cfg.tape_trap ? M_TAPE_TRAP : 0) |
                     (m->port.rsdos_slot >= 0 ? M_RSDOS : 0));
}

/* Slot s's window as the pages map it, the room repeated (cart.h); zero
 * with no ROM, whatever the pool holds. */
static void window_hash(const coco_t *m, unsigned s, uint8_t out[SHA1_DIGEST_LEN]) {
    const cart_t *c = &m->port.slot[s];
    memset(out, 0, SHA1_DIGEST_LEN);
    if (!c->rom) return;
    sha1_t h;
    sha1_init(&h);
    for (uint32_t at = 0; at < COCO_CART_SIZE; at += c->rom_room)
        sha1_update(&h, m->cart + c->rom_at, c->rom_room);
    sha1_final(&h, out);
}

/* The three hashes as a state has them: zero for a half or a slot with
 * nothing in it, whatever coco_t's bytes hold there. */
static void rom_hashes(const coco_t *m, uint8_t *st) {
    memset(st + S_ROM_LO, 0, 2u * SHA1_DIGEST_LEN);
    if (m->rom_present[0]) sha1(m->rom, COCO_ROM_HALF, st + S_ROM_LO);
    if (m->rom_present[1]) sha1(m->rom + COCO_ROM_HALF, COCO_ROM_HALF, st + S_ROM_HI);
    window_hash(m, 0, st + S_CART);
}

/* The port section as the machine has it. */
static void port_encode(const coco_t *m, uint8_t pt[SNAP_PORT_LEN]) {
    const cart_port_t *p = &m->port;
    memset(pt, 0, SNAP_PORT_LEN);
    pt[P_FLAGS] = (uint8_t)((p->mpi ? PF_MPI : 0) | (p->switch_on ? PF_SWITCH_ON : 0));
    pt[P_SWITCH] = p->switch_slot;
    pt[P_CTS] = p->cts;
    pt[P_SCS] = p->scs;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const cart_t *c = &p->slot[s];
        uint8_t *q = pt + P_SLOT + s * P_SLOT_LEN;
        q[PS_KIND] = (uint8_t)c->kind;
        q[PS_FLAGS] = (uint8_t)((c->rom ? PSF_ROM : 0) | (c->rom && c->autostart ? PSF_AUTOSTART : 0));
        window_hash(m, s, q + PS_HASH);
        if (c->kind == CART_SMARTWATCH) put64(q + PS_OFFSET, (uint64_t)c->u.clock.offset);
    }
}

/* Version 1's port, from its state section: one cartridge, no Multi-Pak. */
static void port_from_v1(const uint8_t st[SNAP_STATE_LEN], uint8_t pt[SNAP_PORT_LEN]) {
    uint8_t f = st[S_FITTED];
    memset(pt, 0, SNAP_PORT_LEN);
    pt[P_FLAGS] = PF_SWITCH_ON;
    uint8_t *q = pt + P_SLOT;
    q[PS_KIND] = (uint8_t)((f & M_RSDOS) ? CART_RSDOS : (f & M_CART) ? CART_ROM : CART_NONE);
    q[PS_FLAGS] = (uint8_t)(((f & M_CART) ? PSF_ROM : 0) | ((f & M_AUTOSTART) ? PSF_AUTOSTART : 0));
    memcpy(q + PS_HASH, st + S_CART, SHA1_DIGEST_LEN);
}

static bool zero(const uint8_t *p, size_t n) {
    while (n--)
        if (*p++) return false;
    return true;
}

/* Could a machine have had this port, with this state section? */
static bool port_plausible(const uint8_t st[SNAP_STATE_LEN], const uint8_t pt[SNAP_PORT_LEN]) {
    uint8_t f = st[S_FITTED];
    bool mpi = (pt[P_FLAGS] & PF_MPI) != 0;
    if (pt[P_FLAGS] & ~(PF_MPI | PF_SWITCH_ON)) return false;
    if ((pt[P_SWITCH] | pt[P_CTS] | pt[P_SCS]) >= COCO_MPI_SLOTS) return false;
    if (!mpi && (pt[P_SWITCH] | pt[P_CTS] | pt[P_SCS])) return false;
    if (!zero(pt + 4, P_SLOT - 4) || !zero(pt + P_END, SNAP_PORT_LEN - P_END)) return false;
    unsigned rsdos = 0;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const uint8_t *q = pt + P_SLOT + s * P_SLOT_LEN;
        uint8_t k = q[PS_KIND], fl = q[PS_FLAGS];
        if (!mpi && s > 0) {
            if (!zero(q, P_SLOT_LEN)) return false;   /* only slot 1 without one */
            continue;
        }
        if (k >= CART_MPI || (fl & ~(PSF_ROM | PSF_AUTOSTART))) return false;
        if ((fl & PSF_AUTOSTART) && !(fl & PSF_ROM)) return false;
        if (k == CART_NONE && fl) return false;
        if (!(fl & PSF_ROM) && !zero(q + PS_HASH, SHA1_DIGEST_LEN)) return false;
        int64_t set = (int64_t)get64(q + PS_OFFSET);
        if (set && k != CART_SMARTWATCH) return false;
        if (set > SET_MAX || set < -SET_MAX) return false;
        if (!zero(q + PS_END, P_SLOT_LEN - PS_END)) return false;
        rsdos += k == CART_RSDOS;
    }
    /* One controller, which the state section says is there; and slot 1
     * as the state section has it. */
    if (rsdos > 1u || (rsdos == 1u) != ((f & M_RSDOS) != 0)) return false;
    const uint8_t *q = pt + P_SLOT;
    if (((q[PS_FLAGS] & PSF_ROM) != 0) != ((f & M_CART) != 0) ||
        ((q[PS_FLAGS] & PSF_AUTOSTART) != 0) != ((f & M_AUTOSTART) != 0) ||
        memcmp(q + PS_HASH, st + S_CART, SHA1_DIGEST_LEN) != 0)
        return false;
    return true;
}

/* The port as a power-on chooses it: the Multi-Pak and its switch, and
 * each slot's kind, ROM and CART line; not $FF7F or a set clock, which
 * the machine moves as it runs and a load puts back. */
static bool same_port(const uint8_t a[SNAP_PORT_LEN], const uint8_t b[SNAP_PORT_LEN]) {
    if (((a[P_FLAGS] ^ b[P_FLAGS]) & PF_MPI) || a[P_SWITCH] != b[P_SWITCH]) return false;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        const uint8_t *p = a + P_SLOT + s * P_SLOT_LEN, *q = b + P_SLOT + s * P_SLOT_LEN;
        if (memcmp(p, q, PS_OFFSET) != 0) return false;   /* kind, flags, hash */
    }
    return true;
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

    if (m->port.rsdos_slot >= 0) {
        const rsdos_t *d = &m->port.dk;
        const wd1793_t *f = &d->fdc;
        st[S_DK_LATCH] = d->latch;
        st[S_DK_FLAGS] = (uint8_t)((d->halt_enable ? D_HALT_ENABLE : 0) | (d->intrq ? D_INTRQ : 0) |
                                   (d->nmi ? D_NMI : 0) | (f->intrq ? D_FDC_INTRQ : 0) |
                                   (f->drq ? D_FDC_DRQ : 0) | (f->type1 ? D_TYPE1 : 0) |
                                   (f->hold_intrq ? D_HOLD_INTRQ : 0) |
                                   (f->index_intrq ? D_INDEX_INTRQ : 0));
        st[S_DK_STATUS] = f->status;
        st[S_DK_TRACK] = f->track;
        st[S_DK_SECTOR] = f->sector;
        st[S_DK_DATA] = f->data;
        st[S_DK_CMD] = f->cmd;
        st[S_DK_DRIVE] = f->drive;
        st[S_DK_SIDE] = (uint8_t)(f->side | (f->step_in ? 0x80u : 0u));
        for (unsigned i = 0; i < COCO_DISK_DRIVES; i++) st[S_DK_CYL + i] = f->drv[i].cyl;
        put64(st + S_DK_HEAD_UNTIL, f->head_until);
        if (f->buf_ok && !f->buf_void) {
            st[S_DK_BUF] = (uint8_t)(0x80u | f->buf_drive | (f->buf_side << 2));
            st[S_DK_BUF_CYL] = f->buf_cyl;
        }
    }
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
    put_path(out + MD_CART, md->cart[0]);
    for (unsigned d = 0; d < COCO_SNAP_DRIVES; d++) put_path(out + MD_DISK + d * COCO_PATH_MAX, md->disk[d]);
    put32(out + MD_TAPE_POS, md->tape_pos);
    put32(out + MD_TAPE_REWIND, md->tape_rewind);
    out[MD_FLAGS] = md->tape_chosen ? DK_CHOSEN : 0;
    for (unsigned s = 1; s < COCO_MPI_SLOTS; s++)
        put_path(out + MD_SLOTS + (s - 1u) * COCO_PATH_MAX, md->cart[s]);
}

/* Every path ends inside its field, and the flags are known ones;
 * version 1's section, `len`, has no slots 2 to 4. */
static bool media_plausible(const uint8_t in[SNAP_MEDIA_LEN], size_t len) {
    for (unsigned i = 0; i < MD_PATHS; i++)
        if (!memchr(in + i * COCO_PATH_MAX, 0, COCO_PATH_MAX)) return false;
    for (size_t at = MD_SLOTS; at < len; at += COCO_PATH_MAX)
        if (!memchr(in + at, 0, COCO_PATH_MAX)) return false;
    return (in[MD_FLAGS] & ~DK_CHOSEN) == 0;
}

static void media_decode(const uint8_t in[SNAP_MEDIA_LEN], size_t len, snap_media_t *md) {
    memset(md, 0, sizeof *md);
    memcpy(md->tape, in + MD_TAPE, COCO_PATH_MAX);
    memcpy(md->cart[0], in + MD_CART, COCO_PATH_MAX);
    for (unsigned d = 0; d < COCO_SNAP_DRIVES; d++)
        memcpy(md->disk[d], in + MD_DISK + d * COCO_PATH_MAX, COCO_PATH_MAX);
    md->tape_pos = get32(in + MD_TAPE_POS);
    md->tape_rewind = get32(in + MD_TAPE_REWIND);
    md->tape_chosen = (in[MD_FLAGS] & DK_CHOSEN) != 0;
    for (unsigned s = 1; s < COCO_MPI_SLOTS && MD_SLOTS + s * COCO_PATH_MAX <= len; s++)
        memcpy(md->cart[s], in + MD_SLOTS + (s - 1u) * COCO_PATH_MAX, COCO_PATH_MAX);
}

/* ---- the stream -------------------------------------------------------- */

snap_status_t snapshot_save(const coco_t *m, const snap_media_t *media, snap_write_fn write,
                            void *ctx) {
    /* Nor while the signal runs: its place within a byte is not state
     * (cassette.h). */
    if (m->tape.op != TAPE_NONE || coco_cassette_running(m)) return SNAP_BUSY;
    /* Nor while the disk controller has a command or a track in hand:
     * neither is state (rsdos.h). */
    if (coco_disk_busy(m)) return SNAP_BUSY;

    uint8_t *st = s_st;
    state_encode(m, st);
    port_encode(m, s_port);
    media_encode(media, s_media);

    /* The CRC goes in the header, ahead of what it covers; the machine is
     * the buffer, and nothing changes it in between. */
    uint32_t crc = snapshot_crc32(0, st, SNAP_STATE_LEN);
    crc = snapshot_crc32(crc, s_port, SNAP_PORT_LEN);
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
    if (!write(ctx, s_port, SNAP_PORT_LEN)) return SNAP_IO;
    for (uint32_t off = 0; off < sizeof m->ram; off += 4096u)
        if (!write(ctx, m->ram + off, 4096u)) return SNAP_IO;
    if (!write(ctx, s_media, SNAP_MEDIA_LEN)) return SNAP_IO;
    return SNAP_OK;
}

/* The header, and the file's version, 1 or 2. */
static snap_status_t read_header(snap_read_fn read, void *ctx, uint32_t *crc, unsigned *version) {
    uint8_t hdr[SNAP_HEADER_LEN];
    if (!read(ctx, hdr, sizeof hdr)) return SNAP_IO;
    if (memcmp(hdr, magic, sizeof magic) != 0) return SNAP_NOT_SNAPSHOT;
    unsigned v = get16(hdr + 8);
    if (v > SNAP_VERSION) return SNAP_NEWER;
    uint32_t want = v == 1u ? SNAP_PAYLOAD_V1_LEN : SNAP_PAYLOAD_LEN;
    if (v == 0 || get16(hdr + 10) != SNAP_HEADER_LEN || get32(hdr + 12) != want)
        return SNAP_NOT_SNAPSHOT;
    *crc = get32(hdr + 16);
    *version = v;
    return SNAP_OK;
}

/* The state section, and the port section, version 1's made from it;
 * *crc runs over what was read. */
static bool read_sections(snap_read_fn read, void *ctx, unsigned version, uint32_t *crc) {
    if (!read(ctx, s_st, SNAP_STATE_LEN)) return false;
    *crc = snapshot_crc32(0, s_st, SNAP_STATE_LEN);
    if (version == 1u) {
        port_from_v1(s_st, s_port);
        return true;
    }
    if (!read(ctx, s_port, SNAP_PORT_LEN)) return false;
    *crc = snapshot_crc32(*crc, s_port, SNAP_PORT_LEN);
    return true;
}

static void info_of(const uint8_t st[SNAP_STATE_LEN], const uint8_t pt[SNAP_PORT_LEN], snap_info_t *info) {
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
    info->rsdos = (f & M_RSDOS) != 0;
    info->mpi = (pt[P_FLAGS] & PF_MPI) != 0;
    info->mpi_switch = pt[P_SWITCH] & 3u;
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        uint8_t k = pt[P_SLOT + s * P_SLOT_LEN + PS_KIND];
        info->slot[s] = k < CART_MPI ? (cart_kind_t)k : CART_NONE;
    }
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

    /* The cartridge, between commands: not busy, a drive and side the
     * latch can select, heads within the drives' reach, and the head's
     * unloading no later than a command's end could have put it. */
    if (st[S_FITTED] & M_RSDOS) {
        if ((st[S_DK_STATUS] & WD_ST_BUSY) || st[S_DK_DRIVE] >= COCO_DISK_DRIVES ||
            (st[S_DK_SIDE] & 0x7Fu) > 1u)
            return false;
        for (unsigned i = 0; i < COCO_DISK_DRIVES; i++)
            if (st[S_DK_CYL + i] >= COCO_DISK_TRACKS_MAX) return false;
        int64_t unload = dist(get64(st + S_DK_HEAD_UNTIL), now);
        if (unload > (int64_t)(16u * WD_REV_TICKS)) return false;
        if ((st[S_DK_BUF] & 0x78u) || st[S_DK_BUF_CYL] >= COCO_DISK_TRACKS_MAX) return false;
    } else {
        for (unsigned i = S_DK_LATCH; i < S_END; i++)
            if (st[i]) return false;
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
static snap_status_t compatible(const coco_t *m, const uint8_t st[SNAP_STATE_LEN],
                                const uint8_t pt[SNAP_PORT_LEN]) {
    if (!plausible(st) || !port_plausible(st, pt)) return SNAP_NOT_SNAPSHOT;
    if (!same_field(st)) return SNAP_OTHER_FIELD;
    static uint8_t mine[SNAP_STATE_LEN];
    static uint8_t mine_port[SNAP_PORT_LEN];
    rom_hashes(m, mine);
    uint8_t fitted = fitted_of(m);
    if (((st[S_FITTED] ^ fitted) & (M_ROM_LO | M_ROM_HI)) ||
        memcmp(st + S_ROM_LO, mine + S_ROM_LO, 2u * SHA1_DIGEST_LEN) != 0)
        return SNAP_OTHER_ROM;
    if (st[S_RAM_K] != (uint8_t)m->cfg.ram_k) return SNAP_OTHER_RAM;
    port_encode(m, mine_port);
    if (st[S_FITTED] != fitted || !same_port(pt, mine_port)) return SNAP_OTHER_MACHINE;
    return SNAP_OK;
}

snap_status_t snapshot_check(const coco_t *m, snap_read_fn read, void *ctx, snap_info_t *info,
                             snap_media_t *media) {
    uint32_t want, crc;
    unsigned v;
    snap_status_t r = read_header(read, ctx, &want, &v);
    if (r != SNAP_OK) return r;

    if (!read_sections(read, ctx, v, &crc)) return SNAP_IO;
    for (uint32_t left = COCO_ADDR_SPACE; left; left -= sizeof s_piece) {
        if (!read(ctx, s_piece, sizeof s_piece)) return SNAP_IO;
        crc = snapshot_crc32(crc, s_piece, sizeof s_piece);
    }
    size_t mlen = v == 1u ? SNAP_MEDIA_V1_LEN : SNAP_MEDIA_LEN;
    memset(s_media, 0, SNAP_MEDIA_LEN);
    if (!read(ctx, s_media, mlen)) return SNAP_IO;
    crc = snapshot_crc32(crc, s_media, mlen);
    if (crc != want) return SNAP_CORRUPT;
    info_of(s_st, s_port, info);
    if (!media_plausible(s_media, mlen)) return SNAP_NOT_SNAPSHOT;
    if (media) media_decode(s_media, mlen, media);
    return compatible(m, s_st, s_port);
}

snap_status_t snapshot_load(coco_t *m, snap_read_fn read, void *ctx) {
    if (m->tape.op != TAPE_NONE) return SNAP_BUSY;
    uint32_t want, crc;
    unsigned v;
    snap_status_t r = read_header(read, ctx, &want, &v);
    if (r != SNAP_OK) return r;
    if (!read_sections(read, ctx, v, &crc)) return SNAP_IO;
    const uint8_t *st = s_st, *pt = s_port;
    r = compatible(m, st, pt);
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
    m->hs_change = 0;
    for (unsigned i = 0; i < COCO_PIA_PORTS; i++) m->irq_due[i] = get64(st + S_IRQ_DUE + 8u * i);
    m->due_first = 0;
    m->fields = get32(st + S_FIELDS);
    m->sound_level = get32(st + S_SOUND_LEVEL);
    m->sound_edges = get32(st + S_SOUND_EDGES);
    m->video_bits = get32(st + S_VIDEO_BITS);
    m->split_fields = get32(st + S_SPLIT_FIELDS);
    m->sound_fed = (st[S_M_FLAGS] & F_SOUND_FED) != 0;
    m->video_split = (st[S_M_FLAGS] & F_VIDEO_SPLIT) != 0;
    m->tape.hot = get16(st + S_TAPE_HOT);

    if (m->port.rsdos_slot >= 0) {
        rsdos_t *d = &m->port.dk;
        wd1793_t *f = &d->fdc;
        uint8_t df = st[S_DK_FLAGS];
        d->latch = st[S_DK_LATCH];
        d->halt_enable = (df & D_HALT_ENABLE) != 0;
        d->intrq = (df & D_INTRQ) != 0;
        d->nmi = (df & D_NMI) != 0;
        f->intrq = (df & D_FDC_INTRQ) != 0;
        f->drq = (df & D_FDC_DRQ) != 0;
        f->type1 = (df & D_TYPE1) != 0;
        f->hold_intrq = (df & D_HOLD_INTRQ) != 0;
        f->index_intrq = (df & D_INDEX_INTRQ) != 0;
        f->status = st[S_DK_STATUS];
        f->track = st[S_DK_TRACK];
        f->sector = st[S_DK_SECTOR];
        f->data = st[S_DK_DATA];
        f->cmd = st[S_DK_CMD];
        f->drive = st[S_DK_DRIVE];
        f->side = st[S_DK_SIDE] & 1u;
        f->step_in = (st[S_DK_SIDE] & 0x80u) != 0;
        for (unsigned i = 0; i < COCO_DISK_DRIVES; i++) {
            uint8_t cyl = st[S_DK_CYL + i];
            f->drv[i].cyl = cyl < f->drv[i].type.tracks ? cyl : (uint8_t)(f->drv[i].type.tracks - 1u);
        }
        f->head_until = get64(st + S_DK_HEAD_UNTIL);
        uint8_t b = st[S_DK_BUF];
        wd_restored(f, coco_ticks(m), (b & 0x80u) != 0, b & 3u, (b >> 2) & 1u, st[S_DK_BUF_CYL]);
    }

    /* The port: $FF7F as it stood, and each SmartWatch's set, its key
     * shut (smartwatch.h). Its kinds and ROMs are the machine's already
     * (compatible). */
    cart_port_t *p = &m->port;
    if (p->mpi) {
        p->cts = pt[P_CTS];
        p->scs = pt[P_SCS];
        p->switch_on = (pt[P_FLAGS] & PF_SWITCH_ON) != 0;
    }
    for (unsigned s = 0; s < COCO_MPI_SLOTS; s++) {
        cart_t *c = &p->slot[s];
        if (c->kind != CART_SMARTWATCH) continue;
        smartwatch_t *w = &c->u.clock;
        w->open = w->wrote = false;
        w->key = w->bit = 0;
        w->offset = (int64_t)get64(pt + P_SLOT + s * P_SLOT_LEN + PS_OFFSET);
    }

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
    case SNAP_BUSY:          return "THE TAPE OR DISK IS BUSY";
    case SNAP_NO_ROM:        return "A ROM IT NEEDS IS MISSING";
    case SNAP_NO_CART:       return "ITS CARTRIDGE IS MISSING";
    case SNAP_UNSAVED:       return "A RECORDING IS NOT SAVED";
    case SNAP_TORN:          return "CUT SHORT WHILE LOADING";
    }
    return "?";
}
