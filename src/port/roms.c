/* roms.c — the boot's BASIC, and the page shown without it (roms.h). */

#include "roms.h"

#include <stdio.h>

#include "textpage.h"

boot_basic_t roms_choice(const card_job_t *job) {
    if (job->basic == ROM_COCO_BAS13 && job->extended) return BOOT_EXTENDED;
    if (job->basic == ROM_COCO_BAS13) return BOOT_COLOR13;
    if (job->basic == ROM_COCO_BAS11) return BOOT_COLOR11;
    return BOOT_NONE;
}

bool roms_start_now(const card_job_t *job, bool want_extended) {
    boot_basic_t b = roms_choice(job);
    return b == BOOT_EXTENDED || (!want_extended && b != BOOT_NONE);
}

const char *roms_basic_name(boot_basic_t b) {
    switch (b) {
    case BOOT_EXTENDED: return "EXTENDED COLOR BASIC 1.1";
    case BOOT_COLOR13:  return "COLOR BASIC 1.3";
    case BOOT_COLOR11:  return "COLOR BASIC 1.1";
    case BOOT_NONE:     break;
    }
    return "NO BASIC";
}

/* One ROM's row: its file name, and whether the card has it, MISSING in
 * inverse so that it stands out in a page of one colour. */
static void file_row(coco_frame_t *f, int row, const card_job_t *job, rom_id_t id) {
    textpage_put(f, row, 2, romset_images[id].file, false);
    if (job->found[id]) textpage_put(f, row, 16, "FOUND", false);
    else textpage_put(f, row, 16, "MISSING", true);
}

void roms_page(const card_job_t *job, coco_frame_t *f) {
    char s[TEXT_COLS + 1];
    textpage_clear(f);
    textpage_line(f, 0, " PICO-COCO", true);

    int row = 2;
    if (job->state != CARD_MOUNTED) {
        textpage_put(f, row++, 1, job->state == CARD_NONE ? "THERE IS NO SD CARD."
                                                         : "THE SD CARD DID NOT MOUNT.", false);
        row++;
        textpage_put(f, row++, 1, "ON A FAT CARD, PUT THE ROMS IN", false);
        textpage_put(f, row++, 1, "/COCO/ROMS/, THEN PUT THE CARD", false);
        textpage_put(f, row++, 1, "IN: THE MACHINE STARTS.", false);
    } else {
        textpage_put(f, row++, 1, "EXTENDED COLOR BASIC NEEDS", false);
        textpage_put(f, row++, 1, "THESE TWO IN /COCO/ROMS/:", false);
        row++;
        file_row(f, row++, job, ROM_COCO_BAS13);
        file_row(f, row++, job, ROM_COCO_EXTBAS11);
        if (!job->dir) {
            textpage_put(f, row++, 1, "THE CARD HAS NO /COCO/ROMS/.", false);
        } else if (job->unknown) {
            snprintf(s, sizeof s, "%u FILE%s NOT RECOGNISED.", job->unknown,
                     job->unknown == 1 ? "" : "S");
            textpage_put(f, row++, 1, s, false);
        }
        row++;
        boot_basic_t alt = roms_choice(job);
        if (alt != BOOT_NONE) {
            textpage_put(f, row++, 1, "PRESS ENTER TO START", false);
            snprintf(s, sizeof s, "%s ALONE, OR", roms_basic_name(alt));
            textpage_put(f, row++, 1, s, false);
            textpage_put(f, row++, 1, "PUT THEM ON THE CARD AND THE", false);
            textpage_put(f, row++, 1, "CARD BACK IN.", false);
        } else {
            textpage_put(f, row++, 1, "PUT THEM ON THE CARD, AND THE", false);
            textpage_put(f, row++, 1, "CARD BACK IN: THE MACHINE", false);
            textpage_put(f, row++, 1, "STARTS.", false);
        }
    }

    /* Row 12 is the last the longest case above uses. */
    row = 14;
    textpage_put(f, row++, 1, "ROMS ARE KNOWN BY SHA-1. SEE", false);
    textpage_put(f, row++, 1, "README.MD FOR WHERE TO GET THEM", false);
}
