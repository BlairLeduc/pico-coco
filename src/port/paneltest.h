/* paneltest.h — a page for measuring the panel's response by eye
 * (design.md §9.5; temporary, until the palettes are corrected for the
 * panel).
 *
 * Twelve tests: red, green, blue and grey, each at a quarter, a half and
 * three quarters of the light. A test fills the picture with one-pixel
 * lines in the channel, one in four, two in four or three in four at
 * full and the rest at zero, which is that share of the light whatever
 * the panel's curve, with a solid patch in the middle whose code Left and
 * Right step until it matches the lines from arm's length. The codes
 * found, three points on each channel's curve, are shown and logged.
 * With the panel's curve on (display_set_curve), the patch is drawn
 * through it, and each test should match at its first code, a gamma of
 * 2.2's. After the tests, a view: a grey ramp and the VDG's colours as
 * the screen draws them, to hold beside the same on another display.
 * Drawn straight to the panel over the menu's picture: the caller
 * invalidates the presenter when the page goes.
 */
#ifndef PICO_COCO_PANELTEST_H
#define PICO_COCO_PANELTEST_H

/* Back to the first test, each patch at a gamma of 2.2's code. */
void paneltest_reset(void);

/* Left/Right step the patch (dx), Up/Down change the test (dy). */
void paneltest_key(int dx, int dy);

/* The text rows the page shows: the summary's two rows, and the test in
 * hand for the status row; each `size` with its NUL. */
#define PANELTEST_SUMMARY_ROW 12
void paneltest_summary(unsigned row, char *line, unsigned size);
void paneltest_status(char *line, unsigned size);

/* Every code found, to the log. */
void paneltest_log(void);

/* The lines and the patch, after the page's text has been presented. */
void paneltest_draw(void);

#endif /* PICO_COCO_PANELTEST_H */
