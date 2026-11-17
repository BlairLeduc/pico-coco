/* test_status.c — the perf line's text (status.h, design.md §7.5).
 * Needs no ROM. Adapted from pico-oric's.
 */

#include <string.h>

#include "status.h"
#include "test_util.h"

static char line[COCO_TEXT_COLS + 1];

/* The line with its padding taken off, for comparing. */
static const char *trimmed(void) {
    static char t[COCO_TEXT_COLS + 1];
    memcpy(t, line, sizeof t);
    for (int i = COCO_TEXT_COLS - 1; i >= 0 && t[i] == ' '; i--) t[i] = 0;
    return t;
}

int main(void) {
    perf_line_t p = { .busy1000 = 314, .head100 = 320, .present_us = 12570, .dropped = 0 };
    status_perf_format(&p, line);
    CHECK(strlen(line) == COCO_TEXT_COLS, "one whole line: %zu", strlen(line));
    CHECK(strcmp(trimmed(), "C0 31% 3.20x  LCD 12.6ms  Drop 0  UR 0 0") == 0, "a second: [%s]",
          line);

    p.dropped = 7;
    p.present_us = 940;
    status_perf_format(&p, line);
    CHECK(strcmp(trimmed(), "C0 31% 3.20x  LCD 0.9ms  Drop 7  UR 0 0") == 0, "dropping: [%s]",
          line);

    /* Underruns and late refills, the gaps closing to fit. */
    p.underruns = 128;
    p.late = 3;
    status_perf_format(&p, line);
    CHECK(strcmp(trimmed(), "C0 31% 3.20x LCD 0.9ms Drop 7 UR 128 3") == 0, "underrun: [%s]",
          line);
    p.underruns = p.late = 0;

    /* Rounded, not truncated: 31.5 % is 32 %, 12.55 ms is 12.6 ms. */
    p.busy1000 = 315;
    p.present_us = 12550;
    status_perf_format(&p, line);
    CHECK(strncmp(line, "C0 32% 3.20x  LCD 12.6ms", 24) == 0, "rounded: [%s]", line);

    /* Wide figures close the gaps rather than lose the last one. */
    perf_line_t wide = { .busy1000 = 1000, .head100 = 12345, .present_us = 23400,
                         .dropped = 999 };
    status_perf_format(&wide, line);
    CHECK(strcmp(trimmed(), "C0 100% 123.45x LCD 23.4ms Drop 999 UR 0") == 0,
          "the gaps closed, the tail cut: [%s]", line);

    /* Every figure past its widest: the line keeps its length, and a
     * count near 2^32 is clamped, not rounded round to nothing. */
    perf_line_t big = { .busy1000 = 0xFFFFFFFFu, .head100 = 0xFFFFFFFFu,
                        .present_us = 0xFFFFFFFFu, .dropped = 0xFFFFFFFFu,
                        .underruns = 0xFFFFFFFFu, .late = 0xFFFFFFFFu };
    status_perf_format(&big, line);
    CHECK(strlen(line) == COCO_TEXT_COLS, "still one line: [%s]", line);
    CHECK(strncmp(line, "C0 100% 999.99x LCD 999.9ms Drop 999 UR ", COCO_TEXT_COLS) == 0,
          "clamped: [%s]", line);

    perf_line_t zero = { 0 };
    status_perf_format(&zero, line);
    CHECK(strcmp(trimmed(), "C0 0% 0.00x  LCD 0.0ms  Drop 0  UR 0 0") == 0,
          "before the first second: [%s]", line);

    /* ---- the status line (§9.6, §14) ------------------------------- */
    status_format("", false, STATUS_DECK_IDLE, "", line);
    CHECK(strlen(line) == COCO_TEXT_COLS && trimmed()[0] == 0, "nothing: [%s]", line);
    status_format("/coco/tapes/PIPES.CAS", false, STATUS_DECK_IDLE, "", line);
    CHECK(strcmp(trimmed(), "TAPE: PIPES.CAS") == 0, "a tape: [%s]", line);
    status_format("/coco/tapes/PIPES.CAS", true, STATUS_DECK_IDLE, "", line);
    CHECK(strcmp(trimmed(), "TAPE: PIPES.CAS, NEW") == 0, "a recording: [%s]", line);
    status_format("", true, STATUS_DECK_IDLE, "", line);
    CHECK(strcmp(trimmed(), "TAPE: NEW TAPE, NEW") == 0, "a new tape, unnamed: [%s]", line);
    status_format("", false, STATUS_DECK_IDLE, "/coco/carts/EDTASM.CCC", line);
    CHECK(strcmp(line + COCO_TEXT_COLS - 16, "CART: EDTASM.CCC") == 0 && line[0] == ' ',
          "a cartridge on the right: [%s]", line);
    status_format("/coco/tapes/PIPES.CAS", false, STATUS_DECK_PLAYING, "", line);
    CHECK(strcmp(trimmed(), "TAPE: PIPES.CAS, PLAY") == 0, "the signal playing: [%s]", line);
    status_format("/coco/tapes/PIPES.CAS", true, STATUS_DECK_RECORDING, "", line);
    CHECK(strcmp(trimmed(), "TAPE: PIPES.CAS, REC") == 0, "the ROM writing: [%s]", line);
    status_format("", false, STATUS_DECK_RECORDING, "", line);
    CHECK(strcmp(trimmed(), "TAPE: NEW TAPE, REC") == 0, "writing a tape not yet made: [%s]", line);
    status_format("/coco/tapes/A VERY LONG TAPE NAME INDEED.CAS", true, STATUS_DECK_IDLE,
                  "/coco/carts/Dungeons of Daggorath (1982).ccc", line);
    CHECK(strlen(line) == COCO_TEXT_COLS, "both, long: one line [%s]", line);
    CHECK(strncmp(line, "TAPE: ", 6) == 0 && strstr(line, "CART: Dungeons of Da") &&
          line[COCO_TEXT_COLS - 21] == ' ', "both, long: each in its place [%s]", line);
    status_paused_format(line);
    CHECK(strcmp(trimmed(), "PAUSED: ANY KEY RESUMES") == 0, "paused: [%s]", line);

    TEST_DONE();
}
