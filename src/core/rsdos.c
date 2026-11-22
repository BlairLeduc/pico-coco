/* rsdos.c — the RS-DOS controller cartridge (rsdos.h). */

#include "rsdos.h"

#include "coco.h"
#include "hot.h"

/* After anything that can move INTRQ, DRQ or the latch: the lines onto
 * the CPU, by XRoar's rule (rsdos.h). */
static void COCO_HOT1(lines)(coco_t *m) {
    rsdos_t *d = &m->dk;
    bool intrq = d->fdc.intrq;
    if (intrq != d->intrq) {
        d->intrq = intrq;
        if (intrq) {
            d->halt_enable = false;
            if (d->latch & DK_DDEN) d->nmi = true;
        } else {
            d->nmi = false;
        }
    }
    mc6809_set_nmi(&m->cpu, d->nmi);
    mc6809_set_halt(&m->cpu, d->halt_enable && !d->fdc.drq);
}

void rsdos_lines(coco_t *m) {
    lines(m);
}

static void latch(coco_t *m, uint8_t v, uint64_t now) {
    rsdos_t *d = &m->dk;
    unsigned drive = 0, side = (v & DK_DS3) ? 1u : 0u;
    if (v & DK_DS0)      drive = 0;
    else if (v & DK_DS1) drive = 1;
    else if (v & DK_DS2) drive = 2;
    else if (v & DK_DS3) drive = 3, side = 0;   /* then not the side */
    d->latch = v;
    wd_select(&d->fdc, drive, side, now);
    lines(m);   /* INTRQ as of now, before the latch's own effects */
    if ((v & DK_DDEN) && d->intrq) d->nmi = true;
    d->halt_enable = (v & DK_HALT) && !d->intrq;
    lines(m);
}

uint8_t COCO_HOT1(rsdos_read)(coco_t *m, uint16_t a, uint8_t bus, uint64_t now) {
    if (!(a & 0x08u)) return bus;
    uint8_t v = wd_read(&m->dk.fdc, a & 3u, now);
    lines(m);
    return v;
}

void COCO_HOT1(rsdos_write)(coco_t *m, uint16_t a, uint8_t v, uint64_t now) {
    if (a & 0x08u) {
        wd_write(&m->dk.fdc, a & 3u, v, now);
        lines(m);
    } else {
        latch(m, v, now);
    }
    /* A command's first event, or HALT, may fall inside this run slice. */
    mc6809_stop(&m->cpu);
}

void rsdos_reset(coco_t *m) {
    rsdos_t *d = &m->dk;
    uint64_t now = coco_ticks(m);
    wd_reset(&d->fdc);
    d->intrq = false;
    d->nmi = false;
    d->halt_enable = false;
    latch(m, 0, now);
}

void COCO_HOT1(rsdos_run)(coco_t *m, uint64_t now) {
    wd_run(&m->dk.fdc, now);
    lines(m);
}

/* ---- the port's -------------------------------------------------------- */

const wd_req_t *coco_disk_request(const coco_t *m) {
    return m->cfg.rsdos ? wd_request(&m->dk.fdc) : NULL;
}

void coco_disk_served(coco_t *m, bool ok) {
    wd_served(&m->dk.fdc, ok, coco_ticks(m));
    lines(m);
}

void coco_disk_insert(coco_t *m, unsigned drive, bool protect) {
    wd_insert(&m->dk.fdc, drive, protect, coco_ticks(m));
    lines(m);
}

void coco_disk_eject(coco_t *m, unsigned drive) {
    wd_eject(&m->dk.fdc, drive, coco_ticks(m));
    lines(m);
}

void coco_disk_drive_type(coco_t *m, unsigned drive, wd_type_t type) {
    wd_drive_type(&m->dk.fdc, drive, type, coco_ticks(m));
    lines(m);
}

bool coco_disk_busy(const coco_t *m) {
    const wd1793_t *f = &m->dk.fdc;
    return m->cfg.rsdos && (wd_busy(f) || wd_request(f) != NULL || (f->buf_dirty && !f->buf_void));
}

void coco_disk_flush(coco_t *m) {
    if (!m->cfg.rsdos) return;
    wd_flush(&m->dk.fdc);
    lines(m);
}

int coco_disk_active(const coco_t *m) {
    if (!m->cfg.rsdos || !wd_head_loaded(&m->dk.fdc, coco_ticks(m))) return -1;
    return m->dk.fdc.drive;
}
