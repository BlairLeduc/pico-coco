/* coco-fields.c — the machine at the end of each of its first fields, on
 * the host (design.md §15.5, EL §13.3).
 *
 *   build/host/coco-fields                      Extended Color BASIC, 64 KB
 *   build/host/coco-fields -basic bas13 -noext  Color BASIC 1.3 alone
 *   build/host/coco-fields -ram 16 -fields 600
 *
 * The board, built with -DPICO_COCO_FIELD_LOG=ON, logs the same lines
 * for the same machine (src/port/core0.c); the two lists must be equal.
 * The loop is core 0's with no key typed: keymatrix_field, then
 * coco_run_field, then the record. Finds the ROMs as the host tests do
 * (test/host/guest.h). Host build only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fieldlog.h"
#include "guest.h"
#include "keymatrix.h"

static guest_t g;

static void usage(void) {
    fprintf(stderr, "usage: coco-fields [-ram 4|16|32|64] [-basic bas13|bas11] [-noext] "
                    "[-fields N]\n");
    exit(2);
}

int main(int argc, char **argv) {
    unsigned ram = 64, fields = COCO_FIELD_LOG_MAX;
    rom_id_t basic = ROM_COCO_BAS13;
    bool ext = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-ram") && i + 1 < argc) ram = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-fields") && i + 1 < argc) fields = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-noext")) ext = false;
        else if (!strcmp(argv[i], "-basic") && i + 1 < argc) {
            const char *b = argv[++i];
            if (!strcmp(b, "bas13")) basic = ROM_COCO_BAS13;
            else if (!strcmp(b, "bas11")) basic = ROM_COCO_BAS11;
            else usage();
        } else usage();
    }

    const char *dir;
    guest_find_roms(&dir);
    if (!guest_have_rom(basic) || (ext && !guest_have_rom(ROM_COCO_EXTBAS11))) {
        fprintf(stderr, "coco-fields: the ROMs are not in %s\n", dir);
        return 1;
    }
    guest_build_as(&g, COCO_MACHINE_COCO, ram, basic, ext);

    char line[COCO_FIELD_LOG_LINE];
    for (unsigned f = 0; f < fields; f++) {
        keymatrix_field(&g.k, &g.m);
        coco_run_field(&g.m);
        fieldlog_t r;
        fieldlog_take(&r, &g.m);
        fieldlog_format(&r, line, sizeof line);
        printf("%s\n", line);
    }
    return 0;
}
