/* test_dirlist.c — the Disks page's folder list and paths (dirlist.h,
 * design.md §12.4, §14).
 *
 *  - Sorting: folders first, then images, each by name without regard
 *    to case, as the CoCo SDC image's 8.3 names and a user's own mix.
 *  - Paths: into a folder and up out of it, five deep as the SDC image
 *    goes, never above the root, and a path that only begins with the
 *    root's letters taken for outside it.
 */

#include <stdio.h>
#include <string.h>

#include "dirlist.h"
#include "test_util.h"

static dirlist_t l;

int main(void) {
    /* ---- sorting ------------------------------------------------------ */
    {
        static const struct { const char *name; uint8_t flags; } in[] = {
            { "ZAXXON.DSK", 0 }, { "games", DIRLIST_FOLDER }, { "arkanoid.dsk", 0 },
            { "COCO3", DIRLIST_FOLDER }, { "Apps", DIRLIST_FOLDER }, { "10METER.DSK", 0 },
            { "Dallas Quest, The (Tandy).dsk", DIRLIST_RDONLY }, { "A", DIRLIST_FOLDER },
            { "Hall of the King II - The Inner Chambers (Sundog Systems).dsk", 0 },
        };
        static const char *const want[] = {
            "A", "Apps", "COCO3", "games", "10METER.DSK", "arkanoid.dsk",
            "Dallas Quest, The (Tandy).dsk",
            "Hall of the King II - The Inner Chambers (Sundog Systems).dsk", "ZAXXON.DSK",
        };
        unsigned n = sizeof in / sizeof in[0];
        dirlist_clear(&l);
        for (unsigned i = 0; i < n; i++)
            CHECK(dirlist_add(&l, in[i].name, in[i].flags, i), "add %s", in[i].name);
        dirlist_sort(&l);
        CHECK(l.n == n && l.total == n, "held %u of %u", l.n, l.total);
        for (unsigned i = 0; i < n; i++)
            CHECK(strcmp(l.e[i].name, want[i]) == 0, "sorted %u: %s, want %s", i, l.e[i].name,
                  want[i]);
        CHECK(l.e[6].flags == DIRLIST_RDONLY && l.e[6].size == 6u,
              "the flags and size travel with the name");
    }

    /* ---- full: the list's count, then the pool ------------------------ */
    {
        dirlist_clear(&l);
        char name[16];
        unsigned kept = 0;
        for (unsigned i = 0; i < COCO_DISK_LIST_MAX + 5u; i++) {
            snprintf(name, sizeof name, "D%05u.DSK", i);
            kept += dirlist_add(&l, name, 0, 0);
        }
        CHECK(kept == COCO_DISK_LIST_MAX && l.n == COCO_DISK_LIST_MAX &&
                  l.total == COCO_DISK_LIST_MAX + 5u,
              "a full list keeps %u of %u, counted", l.n, l.total);
        dirlist_clear(&l);
        static char longname[200];
        memset(longname, 'X', sizeof longname - 1u);
        kept = 0;
        for (unsigned i = 0; i < 100u; i++) kept += dirlist_add(&l, longname, 0, 0);
        CHECK(kept == COCO_DIR_POOL / sizeof longname && l.used <= COCO_DIR_POOL,
              "a full pool keeps %u long names in %zu bytes", kept, l.used);
        CHECK(l.total == 100u, "and counts them all");
        CHECK(strlen(l.e[kept - 1u].name) == sizeof longname - 1u, "the last kept is whole");
    }

    /* ---- joining ------------------------------------------------------ */
    char p[COCO_PATH_MAX];
    CHECK(dirlist_join(p, sizeof p, "/coco/disks", "GAMES") && strcmp(p, "/coco/disks/GAMES") == 0,
          "join: %s", p);
    CHECK(dirlist_join(p, sizeof p, "/", "coco") && strcmp(p, "/coco") == 0, "join at /: %s", p);
    {
        char small[16];
        CHECK(!dirlist_join(small, sizeof small, "/coco/disks", "GAMES") && small[0] == 0,
              "too long: refused, empty");
        CHECK(dirlist_join(small, sizeof small, "/coco/disks", "ABC") &&
                  strcmp(small, "/coco/disks/ABC") == 0, "exactly fits: %s", small);
    }

    /* ---- within, below ------------------------------------------------ */
    const char *root = "/coco/disks";
    CHECK(dirlist_within("/coco/disks", root), "the root is within itself");
    CHECK(dirlist_within("/COCO/Disks/GAMES/A", root), "inside, in another case");
    CHECK(!dirlist_within("/coco/disksX/GAMES", root), "only the root's letters: outside");
    CHECK(!dirlist_within("/coco", root), "above: outside");
    CHECK(strcmp(dirlist_below("/coco/disks", root), "") == 0, "below the root: nothing");
    CHECK(strcmp(dirlist_below("/coco/disks/GAMES/A/AGD", root), "GAMES/A/AGD") == 0,
          "below: GAMES/A/AGD");
    CHECK(strcmp(dirlist_below("/elsewhere/x", root), "/elsewhere/x") == 0,
          "outside: the path itself");

    /* ---- up, five deep and back --------------------------------------- */
    {
        static const char *const parts[] = { "MAGS", "RAINBOW", "ADVENT", "BOOK1", "PART2" };
        strcpy(p, root);
        for (unsigned i = 0; i < 5; i++) {
            char q[COCO_PATH_MAX];
            CHECK(dirlist_join(q, sizeof q, p, parts[i]), "down %u", i);
            strcpy(p, q);
        }
        CHECK(strcmp(p, "/coco/disks/MAGS/RAINBOW/ADVENT/BOOK1/PART2") == 0, "five deep: %s", p);
        for (int i = 4; i >= 0; i--) {
            char left[COCO_PATH_MAX];
            CHECK(dirlist_up(p, root, left, sizeof left), "up from depth %d", i + 1);
            CHECK(strcmp(left, parts[i]) == 0, "left %s, want %s", left, parts[i]);
        }
        CHECK(strcmp(p, root) == 0, "back at the root: %s", p);
        CHECK(!dirlist_up(p, root, NULL, 0) && strcmp(p, root) == 0, "never above the root");
        strcpy(p, "/coco/disks/GAMES/");
        CHECK(dirlist_up(p, root, NULL, 0) && strcmp(p, root) == 0, "a trailing slash: %s", p);
        strcpy(p, "/coco/tapes/X");
        CHECK(!dirlist_up(p, root, NULL, 0) && strcmp(p, "/coco/tapes/X") == 0,
              "outside the root: left alone");
    }

    /* ---- the folder a file is in -------------------------------------- */
    dirlist_folder_of(p, sizeof p, "/coco/disks/GAMES/D/DALLAS.DSK");
    CHECK(strcmp(p, "/coco/disks/GAMES/D") == 0, "folder of: %s", p);
    dirlist_folder_of(p, sizeof p, "games.dsk");
    CHECK(strcmp(p, "") == 0, "a bare name: none");
    dirlist_folder_of(p, sizeof p, "/x.dsk");
    CHECK(strcmp(p, "/") == 0, "in /: %s", p);

    TEST_DONE();
}
