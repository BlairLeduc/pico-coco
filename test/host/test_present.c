/* test_present.c — the presenter's plan and the snapshot pool
 * (design.md §9.3, EL §2.4, §5.3), after pico-atom's and pico-oric's.
 *
 *  - The plan: a simulated panel brought up to date from the dirty bands
 *    alone, over a thousand random changes (bytes, a third of them the
 *    VDG's mode or colour set, some a change of artefacts that only
 *    present_invalidate can report), must equal a full render after
 *    each. The control is a plan that diffs bytes only, ignoring the
 *    mode, which must leave stale pixels (EL §5.3).
 *  - The pool: pico-oric's transitions, and a long random interleaving
 *    of both cores' calls in which core 0 always gets a buffer, never one
 *    core 1 holds, and every publish is taken or counted dropped.
 */

#include <string.h>

#include "mc6847_font.h"
#include "present.h"
#include "snappool.h"
#include "test_util.h"

static coco_frame_t f;
static present_t pr;
static present_band_t bands[COCO_BANDS];
static mc6847_t v;
static uint16_t panel[COCO_PIC_H][COCO_PIC_W], full[COCO_PIC_W];
static snappool_t pool;

static uint32_t rng_state = 2026u;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Send the dirty bands to the panel, as core 1 will. */
static void send(void) {
    uint16_t line[COCO_PIC_W];
    mc6847_set_mode(&v, f.mode);
    for (unsigned b = 0; b < COCO_BANDS; b++) {
        if (!bands[b].dirty) continue;
        for (unsigned y = b * COCO_BAND_ROWS; y < (b + 1u) * COCO_BAND_ROWS; y++) {
            mc6847_render_line(&v, &f.lines[y * COCO_VDG_LINE_BYTES], y, line);
            for (unsigned x = bands[b].x0; x <= bands[b].x1; x++) panel[y][x] = line[x];
        }
    }
}

/* Stale pixels on the panel against a full render. */
static unsigned stale(void) {
    unsigned n = 0;
    mc6847_set_mode(&v, f.mode);
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        mc6847_render_line(&v, &f.lines[y * COCO_VDG_LINE_BYTES], y, full);
        for (unsigned x = 0; x < COCO_PIC_W; x++) n += panel[y][x] != full[x];
    }
    return n;
}

static uint8_t random_mode(void) {
    uint8_t m = (uint8_t)(rng() & 0xF8u);
    return (m & VDG_AG) ? m : (uint8_t)(m & (VDG_GM0 | VDG_CSS));
}

/* A thousand changes; `bytes_only` is the control. Returns the number of
 * presents that left stale pixels. */
static unsigned trial(bool bytes_only) {
    memset(&f, 0, sizeof f);
    memset(&pr, 0, sizeof pr);
    mc6847_set_artefact(&v, VDG_ARTEFACT_OFF);
    for (unsigned i = 0; i < COCO_SNAP_VRAM_MAX; i++) f.lines[i] = (uint8_t)rng();
    f.mode = 0;
    present_plan(&pr, &f, bands);
    send();
    unsigned bad = 0;
    for (unsigned step = 0; step < 1000u; step++) {
        unsigned kind = rng() % 9u;
        if (kind < 3u) {
            f.mode = random_mode();
        } else if (kind == 3u) {
            /* Artefacts on or off: no byte and no mode changes. */
            mc6847_set_artefact(&v, v.artefact == VDG_ARTEFACT_OFF ? VDG_ARTEFACT_KBRW
                                                                   : VDG_ARTEFACT_OFF);
            if (!bytes_only) present_invalidate(&pr);
        } else {
            unsigned k = 1u + rng() % 40u;
            for (unsigned j = 0; j < k; j++) f.lines[rng() % COCO_SNAP_VRAM_MAX] = (uint8_t)rng();
        }
        if (bytes_only) pr.mode = f.mode;   /* the control: blind to the mode */
        present_plan(&pr, &f, bands);
        send();
        if (stale()) bad++;
    }
    return bad;
}

int main(void) {
    mc6847_init(&v);
    mc6847_set_font(&v, font_6847);

    /* ---- the plan ---------------------------------------------------- */
    {
        unsigned bad = trial(false);
        CHECK(bad == 0, "%u of 1000 presents left stale pixels", bad);
        unsigned control = trial(true);
        CHECK(control > 0, "the control, blind to the mode, should leave stale pixels");
        printf("plan: 0 stale; the control stale after %u of 1000\n", control);

        /* A frame with nothing changed sends nothing; one byte, one band. */
        memset(&pr, 0, sizeof pr);
        f.mode = VDG_AG | 0x70u;
        CHECK(present_plan(&pr, &f, bands) == COCO_BANDS, "the first present sends every band");
        CHECK(present_plan(&pr, &f, bands) == 0, "an unchanged frame sends nothing");
        f.lines[100u * COCO_VDG_LINE_BYTES + 3u] ^= 1u;
        CHECK(present_plan(&pr, &f, bands) == 1 && bands[100u / COCO_BAND_ROWS].dirty &&
                  bands[12].x0 == 24 && bands[12].x1 == 31,
              "one byte of line 100: band 12, pixels 24-31");
    }

    /* ---- the pool ---------------------------------------------------- */
    {
        snappool_t *p = &pool;
        snappool_init(p);
        CHECK(snappool_take(p) < 0, "nothing to take from an empty pool");
        int a = snappool_claim(p);
        CHECK(a >= 0, "claim from an empty pool");
        CHECK(snappool_take(p) < 0, "a filling buffer is invisible to core 1");
        snappool_publish(p, a);
        int r = snappool_take(p);
        CHECK(r == a, "core 1 takes the published buffer");
        int b = snappool_claim(p);
        snappool_publish(p, b);
        int c = snappool_claim(p);
        CHECK(c >= 0, "core 0 never runs out of buffers (the third buffer)");
        snappool_publish(p, c);
        CHECK(p->dropped == 1, "the older ready frame is dropped: %u", (unsigned)p->dropped);
        int d = snappool_claim(p);
        CHECK(d == b, "the superseded buffer is free again");
        snappool_release(p, r);
        CHECK(snappool_take(p) == c, "core 1 gets the newest frame");
        snappool_publish(p, r);
        snappool_release(p, d);
        CHECK(p->state[r] == SNAP_FREE && p->state[d] == SNAP_FILLING,
              "out-of-state calls are ignored");

        snappool_init(p);
        int filling = snappool_claim(p), rendering = -1;
        uint32_t taken = 0;
        for (unsigned step = 0; step < 100000u; step++) {
            if (rng() & 1u) {
                snappool_publish(p, filling);
                filling = snappool_claim(p);
                CHECK(filling >= 0 && filling != rendering,
                      "step %u: core 0 claimed %d while core 1 renders %d", step, filling,
                      rendering);
            } else if (rendering < 0) {
                rendering = snappool_take(p);
                if (rendering >= 0) taken++;
            } else {
                snappool_release(p, rendering);
                rendering = -1;
            }
        }
        unsigned pending = 0;
        for (unsigned i = 0; i < COCO_SNAP_BUFFERS; i++) pending += p->state[i] == SNAP_READY;
        CHECK(taken + p->dropped + pending == p->published, "published %u = %u + %u + %u",
              (unsigned)p->published, (unsigned)taken, (unsigned)p->dropped, pending);
    }

    TEST_DONE();
}
