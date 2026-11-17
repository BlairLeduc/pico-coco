/* frame_ppm.c — see frame_ppm.h. */

#include "frame_ppm.h"

#include <stdio.h>
#include <string.h>

const mc6847_t *frame_vdg(void *ctx, uint8_t mode) {
    mc6847_set_mode(ctx, mode);
    return ctx;
}

void frame_rgb(mc6847_t *v, const coco_frame_t *f, uint8_t *rgb) {
    uint16_t px[COCO_PIC_W], tmp[COCO_PIC_W];
    for (unsigned y = 0; y < COCO_PIC_H; y++) {
        present_render_line(f, y, frame_vdg, v, px, tmp);   /* each line as it is drawn (§9.4) */
        for (unsigned x = 0; x < COCO_PIC_W; x++) {
            uint16_t c = px[x];
            unsigned r = (c >> 11) & 0x1Fu, g = (c >> 5) & 0x3Fu, b = c & 0x1Fu;
            uint8_t *o = &rgb[(y * COCO_PIC_W + x) * 3u];
            o[0] = (uint8_t)(r << 3 | r >> 2);
            o[1] = (uint8_t)(g << 2 | g >> 4);
            o[2] = (uint8_t)(b << 3 | b >> 2);
        }
    }
}

bool frame_write_ppm(const char *path, const uint8_t *rgb) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    bool ok = fputs(FRAME_PPM_HEADER, fp) >= 0 &&
              fwrite(rgb, 1, FRAME_RGB_BYTES, fp) == FRAME_RGB_BYTES;
    return fclose(fp) == 0 && ok;
}

bool frame_read_ppm(const char *path, uint8_t *rgb) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    char header[sizeof FRAME_PPM_HEADER - 1u];
    bool ok = fread(header, 1, sizeof header, fp) == sizeof header &&
              memcmp(header, FRAME_PPM_HEADER, sizeof header) == 0 &&
              fread(rgb, 1, FRAME_RGB_BYTES, fp) == FRAME_RGB_BYTES;
    fclose(fp);
    return ok;
}
