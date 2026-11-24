/* paneltest.h — a page for measuring the panel's response by eye
 * (design.md §9.5; temporary, until the palettes are corrected for the
 * panel).
 *
 * Four bands, red, green, blue and grey, each a field of one-pixel lines
 * at full and at zero, half the light whatever the panel's curve, with
 * six solid patches set in it. The patch that matches the lines from arm's
 * length names the channel's gamma, which its label gives. A fifth band
 * holds the six darkest greys on black, to see where the panel loses
 * them. Drawn straight to the panel over the menu's picture: the caller
 * invalidates the presenter when the page goes.
 */
#ifndef PICO_COCO_PANELTEST_H
#define PICO_COCO_PANELTEST_H

/* The text row a band's labels go on, and their text: TEXT_COLS
 * characters, the gamma under each patch. */
#define PANELTEST_BANDS 4
int paneltest_label_row(unsigned band);
void paneltest_label(unsigned band, char *line, unsigned size);

/* The bands and patches, after the page's text has been presented. */
void paneltest_draw(void);

#endif /* PICO_COCO_PANELTEST_H */
