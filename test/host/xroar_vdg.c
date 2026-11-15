/* xroar_vdg.c — the driver for XRoar's VDG and SAM (xroar_vdg.h).
 *
 * The VDG and SAM are wired as XRoar's dragon.c wires them: HS and FS to
 * the SAM's counter, the VDG's fetch through the SAM's vdg_bytes, GM0 to
 * INT/EXT and D7, D6 to A/S and INV. Also the symbols those files need
 * from the rest of XRoar: part_new and part_is_a, xmalloc, the global the
 * event list hangs from, and the serialiser's and the debugger's, which
 * nothing here uses.
 */

#include "top-config.h"

#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "debug.h"
#include "events.h"
#include "part.h"
#include "serialise.h"
#include "xalloc.h"
#include "xroar.h"
#include "mc6847/mc6847.h"
#include "mc6883.h"

#include "xroar_vdg.h"

/* ---- what the parts need from the rest of XRoar ---------------------- */

void *part_new(size_t psize) {
    void *p = calloc(1, psize);
    if (!p) abort();
    return p;
}

void *xmalloc(size_t s) {
    void *p = malloc(s);
    if (!p) abort();
    return p;
}

/* Only the SN74LS785 variant asks; this is an MC6883. */
bool part_is_a(struct part *p, const char *is_a) {
    (void)p;
    return strcmp(is_a, "SN74LS783") == 0;
}

/* Snapshots: never taken here. */
void ser_write_tag(struct ser_handle *sh, int tag, size_t length) {
    (void)sh; (void)tag; (void)length;
    abort();
}
void ser_write_uint16_untagged(struct ser_handle *sh, uint16_t v) {
    (void)sh; (void)v;
    abort();
}
uint16_t ser_read_uint16(struct ser_handle *sh) {
    (void)sh;
    abort();
}
uint32_t ser_read_vuint32(struct ser_handle *sh) {
    (void)sh;
    abort();
}

struct xroar xroar;

const struct debug_feature_type debug_feature_type_uint8;
const struct debug_feature_type debug_feature_type_uint16;
const struct debug_feature_type debug_feature_type_code_ptr16;

extern const struct partdb_entry mc6847_part;
extern const struct partdb_entry mc6883_part;

/* ---- the wiring ------------------------------------------------------ */

static struct MC6847 *vdg;
static struct MC6883 *sam;
static const uint8_t *g_ram;
static uint8_t g_pb;
static int line = -1;         /* the VDG's line, from FS's rise          */
static int fields_seen;
static uint8_t (*g_out)[XV_HALF_PIXELS];

static void hs(void *sptr, _Bool level) {
    (void)sptr;
    sam->vdg_hsync(sam, level);
}

static void fs(void *sptr, _Bool level) {
    (void)sptr;
    sam->vdg_fsync(sam, level);
    if (level) {
        line = 0;
        fields_seen++;
    }
}

/* dragon.c's vdg_fetch_handler, with the machine's RAM flat in 64K mode:
 * the row is A7-A0 and the column A15-A8. */
static void fetch(void *sptr, uint16_t A, int nbytes, uint16_t *dest) {
    (void)sptr;
    (void)A;
    uint16_t attr = (uint16_t)((g_pb & 0x10u) << 6);   /* GM0 -> ¬INT/EXT */
    while (nbytes > 0) {
        int n = sam->vdg_bytes(sam, nbytes);
        unsigned a = ((sam->Vcol & 0xFFu) << 8) | (sam->Vrow & 0xFFu);
        for (int i = 0; i < n; i++) {
            uint16_t D = (uint16_t)(g_ram[(a + (unsigned)i) & 0xFFFFu] | attr);
            D |= (uint16_t)((D & 0xC0u) << 2);   /* D7, D6 -> ¬A/S, INV */
            if (dest) *dest++ = D;
        }
        nbytes -= n;
    }
}

static void render(void *sptr, unsigned burst, unsigned npixels, const uint8_t *data) {
    (void)sptr;
    (void)burst;
    (void)npixels;
    if (line < 0) return;
    int y = line - VDG_ACTIVE_AREA_START;
    if (fields_seen == 2 && data && y >= 0 && y < XV_LINES)
        memcpy(g_out[y], data + VDG_ACTIVE_LINE_START, XV_HALF_PIXELS);
    line++;
}

static void open_parts(void) {
    xroar.machine_events = event_list_new();
    vdg = (struct MC6847 *)mc6847_part.funcs->allocate();
    mc6847_part.funcs->initialise(&vdg->part, NULL);
    sam = (struct MC6883 *)mc6883_part.funcs->allocate();
    mc6883_part.funcs->finish(&sam->part);
    vdg->signal_hs = DELEGATE_AS1(void, bool, hs, NULL);
    vdg->signal_fs = DELEGATE_AS1(void, bool, fs, NULL);
    vdg->fetch_data = DELEGATE_AS3(void, uint16, int, uint16p, fetch, NULL);
    vdg->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render, NULL);
    mc6847_part.funcs->finish(&vdg->part);
}

void xv_field(const uint8_t *ram, uint16_t sam_reg, uint8_t pb,
              uint8_t out[XV_LINES][XV_HALF_PIXELS]) {
    if (!vdg) open_parts();
    g_ram = ram;
    g_pb = pb;
    g_out = out;

    /* The register, a bit at a time through its address pairs. */
    sam_reg = (uint16_t)((sam_reg & ~0x6000u) | 0x4000u);
    for (unsigned bit = 0; bit < 16; bit++)
        (void)sam->mem_cycle(sam, 0, (uint16_t)(0xFFC0u + 2u * bit + ((sam_reg >> bit) & 1u)));

    unsigned vmode = pb & 0xF8u;
    vmode |= (vmode & 0x10u) << 4;   /* ¬INT/EXT = GM0, as dragon.c */
    mc6847_reset(vdg);
    mc6847_set_mode(vdg, vmode);
    line = -1;
    fields_seen = 0;
    /* Run to the third FS rise: the first starts a field whose counter was
     * loaded with this F, the second a field wholly in this mode. */
    while (fields_seen < 3)
        event_run_queue(xroar.machine_events, VDG_LINE_DURATION);
}
