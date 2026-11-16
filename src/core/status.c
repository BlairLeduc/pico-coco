/* status.c — what the perf and status lines say (status.h, design.md §7.5,
 * §9.6). */

#include "status.h"

#include <stdio.h>
#include <string.h>

static unsigned clamp(uint64_t v, unsigned max) {
    return v > max ? max : (unsigned)v;
}

/* v / d to the nearest, in 64 bits: rounding a count near the top of
 * its 32 must not wrap it to nothing (found in pico-oric's M7). */
static uint64_t nearest(uint32_t v, uint32_t d) {
    return ((uint64_t)v + d / 2u) / d;
}

void status_perf_format(const perf_line_t *p, char out[COCO_TEXT_COLS + 1]) {
    enum { W = COCO_TEXT_COLS };
    unsigned pct = clamp(nearest(p->busy1000, 10u), 100u);
    unsigned head = clamp(p->head100, 999u * 100u + 99u);
    unsigned ms10 = clamp(nearest(p->present_us, 100u), 9999u);
    /* Every figure clamped, the line is at most 53 characters with two
     * spaces between the groups and 49 with one; with figures as they
     * run, 40 and 37. Past 40 the tail is cut. */
    char text[64];
    int n = 0;
    for (unsigned gap = 2; gap >= 1; gap--) {
        const char *sp = gap == 2 ? "  " : " ";
        n = snprintf(text, sizeof text, "C0 %u%% %u.%02ux%sLCD %u.%ums%sDrop %u%sUR %u %u", pct,
                     head / 100u, head % 100u, sp, ms10 / 10u, ms10 % 10u, sp,
                     clamp(p->dropped, 999u), sp, clamp(p->underruns, 9999u),
                     clamp(p->late, 999u));
        if (n >= 0 && (size_t)n <= W) break;
    }
    size_t len = n < 0 ? 0u : (size_t)n < W ? (size_t)n : W;
    memset(out, ' ', W);
    memcpy(out, text, len);
    out[W] = 0;
}

static const char *base_name(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

void status_format(const char *tape, bool unsaved, const char *cart,
                   char out[COCO_TEXT_COLS + 1]) {
    enum { W = COCO_TEXT_COLS, CART_MAX = 14 };
    memset(out, ' ', W);
    out[W] = 0;

    /* The cartridge first, its name capped, so the tape has what is left. */
    char right[W + 1] = "";
    int rn = 0;
    if (cart && cart[0]) {
        rn = snprintf(right, sizeof right, "CART: %.*s", CART_MAX, base_name(cart));
        if (rn < 0) rn = 0;
        if (rn > W) rn = W;
        memcpy(out + W - rn, right, (size_t)rn);
    }

    char left[W + 1] = "";
    int ln = 0;
    if ((tape && tape[0]) || unsaved) {
        const char *tail = unsaved ? ", NEW" : "";
        const char *name = tape && tape[0] ? base_name(tape) : "NEW TAPE";
        int fixed = (int)strlen("TAPE: ") + (int)strlen(tail) + (rn ? rn + 1 : 0);
        int room = fixed < W ? W - fixed : 0;
        ln = snprintf(left, sizeof left, "TAPE: %.*s%s", room, name, tail);
        if (ln < 0) ln = 0;
        if (ln > W - (rn ? rn + 1 : 0)) ln = W - (rn ? rn + 1 : 0);
        memcpy(out, left, (size_t)ln);
    }
}

void status_paused_format(char out[COCO_TEXT_COLS + 1]) {
    enum { W = COCO_TEXT_COLS };
    static const char text[] = "PAUSED: ANY KEY RESUMES";
    memset(out, ' ', W);
    memcpy(out, text, sizeof text - 1u);
    out[W] = 0;
}
