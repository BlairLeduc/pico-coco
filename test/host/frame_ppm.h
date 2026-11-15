/* frame_ppm.h — a snapshot drawn as an image, for the golden images, the
 * screens test_screen writes, and tools/vdg-ppm (design.md §9.6, §15.3).
 *
 * The picture is drawn by mc6847_render_line exactly as the presenter
 * will draw it, and widened from RGB565 to 8 bits a channel by repeating
 * the high bits, so full scale stays full scale.
 */
#ifndef PICO_COCO_TEST_FRAME_PPM_H
#define PICO_COCO_TEST_FRAME_PPM_H

#include <stdbool.h>
#include <stdint.h>

#include "coco.h"
#include "mc6847.h"

#define FRAME_RGB_BYTES (COCO_PIC_W * COCO_PIC_H * 3u)
#define FRAME_PPM_HEADER "P6\n256 192\n255\n"

/* Draw `f` through `v`, whose mode is set from the frame. */
void frame_rgb(mc6847_t *v, const coco_frame_t *f, uint8_t *rgb);

/* Write it as a binary PPM; false on an I/O error. */
bool frame_write_ppm(const char *path, const uint8_t *rgb);

/* Read one written so; false if missing or not a 256x192 P6. */
bool frame_read_ppm(const char *path, uint8_t *rgb);

#endif /* PICO_COCO_TEST_FRAME_PPM_H */
