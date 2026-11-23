/* dirlist.h — one folder of the card as the Disks page lists it, and the
 * paths it moves between (design.md §12.4, §14).
 *
 * The port reads the folder (dskio.c) into a dirlist_t; this keeps the
 * names in one pool, sorts them, folders first, then images, each in
 * name order without regard to case, and works out the paths: into a
 * folder, up out of one, never above the root the page starts from. No
 * I/O, so the host holds it (test_dirlist).
 */
#ifndef PICO_COCO_DIRLIST_H
#define PICO_COCO_DIRLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define DIRLIST_FOLDER  0x01u   /* a folder, not an image              */
#define DIRLIST_RDONLY  0x02u   /* the read-only attribute: protected  */

typedef struct {
    const char *name;           /* in the list's pool                  */
    uint32_t    size;
    uint8_t     flags;
} dirlist_entry_t;

/* Never copied: its entries point into its own pool. */
typedef struct {
    dirlist_entry_t e[COCO_DISK_LIST_MAX];
    unsigned n;                 /* entries held                        */
    unsigned total;             /* entries offered, held or not        */
    size_t   used;              /* of the pool                         */
    char     pool[COCO_DIR_POOL];
} dirlist_t;

void dirlist_clear(dirlist_t *l);

/* An entry, counted in total whether or not it fits; false if the list
 * or the pool is full and it was not kept. */
bool dirlist_add(dirlist_t *l, const char *name, uint8_t flags, uint32_t size);

/* Folders first, then images, each by name without regard to case. */
void dirlist_sort(dirlist_t *l);

/* dir/name into out, of cap bytes. False, and out empty, if it does not
 * fit. */
bool dirlist_join(char *out, size_t cap, const char *dir, const char *name);

/* Is path the root or inside it? A path that only begins with the root's
 * letters ("/coco/disksX") is not; case does not matter, as on FAT. */
bool dirlist_within(const char *path, const char *root);

/* path, inside root, one folder up: true if it moved, false at the root
 * or outside it, which leave path as it was. The folder left is copied
 * into left (cap bytes), if not NULL, so the page can select it. */
bool dirlist_up(char *path, const char *root, char *left, size_t cap);

/* The part of path below root, without a leading '/': "" at the root,
 * and path itself if it is not inside root. */
const char *dirlist_below(const char *path, const char *root);

/* The folder a file's path is in, into out (cap bytes); "" for a bare
 * name. */
void dirlist_folder_of(char *out, size_t cap, const char *path);

#endif /* PICO_COCO_DIRLIST_H */
