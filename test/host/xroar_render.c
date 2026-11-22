/* xroar_render.c — see xroar_render.h. Compiled with XRoar's include
 * paths, against its renderer from the pinned checkout (D2, D8). */

#include "top-config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "messenger.h"
#include "module.h"
#include "ui.h"
#include "vdg_palette.h"
#include "vo_render.h"

#include "xroar_render.h"

/* The line as XRoar's VDG hands it on: 912 samples at 14.31818 MHz, the
 * active area from sample 254 (VDG_tWHS + VDG_tBP + VDG_tLB, as
 * dragon/coco.c sets it), 640 of them from 190 in the viewport. */
#define LINE_SAMPLES 912
#define ACTIVE_X     254

/* ui.c lists XRoar's UI and file requester modules; none is used here. */
struct module filereq_null_module;
struct ui_module ui_null_module;

static struct vo_render *s_vr;
static uint32_t s_buf[240][640];
static int s_krbw = -1;

/* Each palette entry drawn as a colour no artefact has: index i as
 * R = i, G = 1, B = 2, set over XRoar's NTSC palette once it has
 * decided which entries are black or white. */
static void mark_palette(void) {
    for (int c = 0; c < 12; c++) s_vr->set_palette_entry(s_vr, VO_RENDER_PALETTE_CMP, c, c, 1, 2);
}

static void setup(void) {
    if (s_vr) return;
    ui_init();
    s_vr = vo_render_new(VO_RENDER_FMT_RGBA8);
    /* As dragon.c configures a CoCo: NTSC, the MC1372's lead and lag of
     * 100°, the "ideal" palette, the normal burst. */
    ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_3_579545, NULL);
    ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_NTSC, NULL);
    /* xroar.c's defaults, which it sends at start. */
    ui_update_state(-1, ui_tag_ntsc_scaling, 1, NULL);
    ui_update_state(-1, ui_tag_brightness, 52, NULL);
    ui_update_state(-1, ui_tag_contrast, 52, NULL);
    ui_update_state(-1, ui_tag_saturation, 50, NULL);
    ui_update_state(-1, ui_tag_hue, 0, NULL);
    vo_render_set_cmp_phase_offset(s_vr, 0);
    vo_render_set_cmp_lead_lag(s_vr, 0., 100.);
    struct vdg_palette *palette = vdg_palette_by_name("ideal");
    for (int c = 0; c < NUM_VDG_COLOURS; c++) {
        float y = palette->palette[c].y;
        float chb = palette->palette[c].chb;
        float b_y = (palette->palette[c].b - chb) * 0.6;
        float r_y = (palette->palette[c].a - chb) * 0.6;
        y = (palette->blank_y - y) / (palette->blank_y - palette->white_y);
        vo_render_set_cmp_palette(s_vr, c, y, b_y, r_y);
    }
    vo_render_set_cmp_burst_br(s_vr, 1, -0.25, 0.0);
    /* The active area with its top at line 0 of the viewport. */
    vo_render_set_active_area(s_vr, ACTIVE_X, 24, 512, 192);
    vo_render_set_buffer(s_vr, s_buf);
}

static void set_phase(int krbw) {
    if (krbw == s_krbw) return;
    /* dragon.c: TV input KBRW is phase 180, KRBW phase 0. */
    vo_render_set_cmp_phase(s_vr, krbw ? 0 : 180);
    mark_palette();
    s_krbw = krbw;
}

int xr_black_or_white(uint8_t index) {
    setup();
    return s_vr->cmp.is_black_or_white[index];
}

void xr_render_line(xr_ccr_t ccr, int krbw, const uint8_t half[512], uint8_t border,
                    xr_pixel_t out[256]) {
    setup();
    set_phase(krbw);
    static uint8_t line[LINE_SAMPLES];
    memset(line, border, sizeof line);
    memcpy(line + ACTIVE_X, half, 512);

    vo_render_vsync(s_vr);   /* the viewport's new place, line 0 next */
    if (ccr == XR_CCR_5BIT) s_vr->render_cmp_5bit(s_vr, 1, LINE_SAMPLES, line);
    else                    s_vr->render_cmp_2bit(s_vr, 1, LINE_SAMPLES, line);

    const uint32_t *row = &s_buf[0][ACTIVE_X - s_vr->viewport.x];
    for (unsigned x = 0; x < 256u; x++) {
        uint32_t a = row[2 * x], b = row[2 * x + 1];
        xr_pixel_t *p = &out[x];
        memset(p, 0, sizeof *p);
        if (a != b) {
            p->from_palette = 2;
            continue;
        }
        /* RGBA8: R in the top byte. */
        p->r = (uint8_t)(a >> 24);
        p->g = (uint8_t)(a >> 16);
        p->b = (uint8_t)(a >> 8);
        if (p->r < 12 && p->g == 1 && p->b == 2) {
            p->from_palette = 1;
            p->index = p->r;
        }
    }
}
