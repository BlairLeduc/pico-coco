/* rsdos.c — the RS-DOS controller cartridge (rsdos.h). */

#include "rsdos.h"

#include "cart.h"
#include "coco.h"
#include "hot.h"

/* After anything that can move INTRQ, DRQ or the latch: the lines the
 * cartridge drives, by XRoar's rule (rsdos.h), and the port's onto the
 * CPU. */
static void COCO_HOT1(lines)(coco_t *m, cart_t *c) {
    rsdos_t *d = &m->port.dk;
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
    c->nmi = d->nmi;
    c->halt = d->halt_enable && !d->fdc.drq;
    cart_lines(m);
}

void rsdos_lines(coco_t *m, cart_t *c) {
    lines(m, c);
}

/* The drives' sound brought up to now before an event is handed to it
 * (drivesnd.h), stamped as the sound bus's changes are, with the tick at
 * the start of the writing instruction (coco.c). Off, the PCM is not
 * touched, so the guest's sound is sample for sample what it was. */
static drivesnd_t *drive_sound(coco_t *m) {
    drivesnd_t *s = &m->pcm.drive;
    if (!s->on) return NULL;
    pcm_advance(&m->pcm, (uint32_t)coco_ticks(m));
    return s;
}

static void latch(coco_t *m, cart_t *c, uint8_t v, uint64_t now) {
    rsdos_t *d = &m->port.dk;
    if ((v ^ d->latch) & DK_MOTOR) {
        drivesnd_t *s = drive_sound(m);
        if (s) drivesnd_motor(s, (v & DK_MOTOR) != 0);
    }
    unsigned drive = 0, side = (v & DK_DS3) ? 1u : 0u;
    if (v & DK_DS0)      drive = 0;
    else if (v & DK_DS1) drive = 1;
    else if (v & DK_DS2) drive = 2;
    else if (v & DK_DS3) drive = 3, side = 0;   /* then not the side */
    d->latch = v;
    wd_select(&d->fdc, drive, side, now);
    lines(m, c);   /* INTRQ as of now, before the latch's own effects */
    if ((v & DK_DDEN) && d->intrq) d->nmi = true;
    d->halt_enable = (v & DK_HALT) && !d->intrq;
    lines(m, c);
}

uint8_t COCO_HOT1(rsdos_read)(coco_t *m, cart_t *c, uint16_t a, uint8_t bus, uint64_t now) {
    if (!(a & 0x08u)) return bus;
    uint8_t v = wd_read(&m->port.dk.fdc, a & 3u, now);
    lines(m, c);
    return v;
}

void COCO_HOT1(rsdos_write)(coco_t *m, cart_t *c, uint16_t a, uint8_t v, uint64_t now) {
    if (a & 0x08u) {
        wd1793_t *f = &m->port.dk.fdc;
        wd_write(f, a & 3u, v, now);
        if (f->pulse_n) {
            drivesnd_t *s = drive_sound(m);
            if (s) drivesnd_steps(s, (uint32_t)f->pulse_from, f->pulse_n, f->pulse_period);
            f->pulse_n = 0;
        }
        lines(m, c);
    } else {
        latch(m, c, v, now);
    }
    /* A command's first event, or HALT, may fall inside this run slice. */
    mc6809_stop(&m->cpu);
}

void rsdos_reset(coco_t *m, cart_t *c) {
    rsdos_t *d = &m->port.dk;
    uint64_t now = coco_ticks(m);
    wd_reset(&d->fdc);
    d->intrq = false;
    d->nmi = false;
    d->halt_enable = false;
    latch(m, c, 0, now);
}

void COCO_HOT1(rsdos_run)(coco_t *m, cart_t *c, uint64_t now) {
    wd_run(&m->port.dk.fdc, now);
    lines(m, c);
}

/* ---- the port's -------------------------------------------------------- */

wd1793_t *coco_disk_fdc(coco_t *m) {
    cart_t *c = cart_rsdos(m);
    return c ? &m->port.dk.fdc : NULL;
}

const wd1793_t *coco_disk_fdc_c(const coco_t *m) {
    const cart_t *c = cart_rsdos_c(m);
    return c ? &m->port.dk.fdc : NULL;
}

const wd_req_t *coco_disk_request(const coco_t *m) {
    const wd1793_t *f = coco_disk_fdc_c(m);
    return f ? wd_request(f) : NULL;
}

void coco_disk_served(coco_t *m, bool ok) {
    cart_t *c = cart_rsdos(m);
    if (!c) return;
    wd_served(&m->port.dk.fdc, ok, coco_ticks(m));
    lines(m, c);
}

void coco_disk_insert(coco_t *m, unsigned drive, bool protect) {
    cart_t *c = cart_rsdos(m);
    if (!c) return;
    wd_insert(&m->port.dk.fdc, drive, protect, coco_ticks(m));
    lines(m, c);
}

void coco_disk_eject(coco_t *m, unsigned drive) {
    cart_t *c = cart_rsdos(m);
    if (!c) return;
    wd_eject(&m->port.dk.fdc, drive, coco_ticks(m));
    lines(m, c);
}

void coco_disk_drive_type(coco_t *m, unsigned drive, wd_type_t type) {
    cart_t *c = cart_rsdos(m);
    if (!c) return;
    wd_drive_type(&m->port.dk.fdc, drive, type, coco_ticks(m));
    lines(m, c);
}

bool coco_disk_busy(const coco_t *m) {
    const wd1793_t *f = coco_disk_fdc_c(m);
    return f && (wd_busy(f) || wd_request(f) != NULL || (f->buf_dirty && !f->buf_void));
}

void coco_disk_flush(coco_t *m) {
    cart_t *c = cart_rsdos(m);
    if (!c) return;
    wd_flush(&m->port.dk.fdc);
    lines(m, c);
}

/* Whether the controller's latch has the drives' motors on. */
static bool motor_on(const coco_t *m) {
    const cart_t *c = cart_rsdos_c(m);
    return c && (m->port.dk.latch & DK_MOTOR);
}

void coco_disk_sounds(coco_t *m, bool on) {
    drivesnd_t *s = &m->pcm.drive;
    if (on == s->on) return;
    pcm_advance(&m->pcm, (uint32_t)coco_ticks(m));
    drivesnd_enable(s, on);
    drivesnd_motor(s, motor_on(m));
}

void coco_disk_restored(coco_t *m) {
    drivesnd_motor(&m->pcm.drive, motor_on(m));
}

size_t coco_disk_park_sound(coco_t *m, int16_t *dst, size_t n) {
    drivesnd_t *s = &m->pcm.drive;
    uint32_t now = (uint32_t)coco_ticks(m);
    for (size_t i = 0; i < n; i++) {
        int32_t v = drivesnd_sounding(s) ? drivesnd_next(s, now) : 0;
        dst[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return n;
}

void coco_disk_recordings(coco_t *m, const drivesnd_samples_t *rec) {
    drivesnd_set_samples(&m->pcm.drive, rec);
}

int coco_disk_active(const coco_t *m) {
    const wd1793_t *f = coco_disk_fdc_c(m);
    if (!f || !wd_head_loaded(f, coco_ticks(m))) return -1;
    return f->drive;
}
