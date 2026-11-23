/* dirlist.c — see dirlist.h. */

#include "dirlist.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int name_cmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = toupper((unsigned char)*a), cb = toupper((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

static int entry_cmp(const void *pa, const void *pb) {
    const dirlist_entry_t *a = pa, *b = pb;
    bool fa = (a->flags & DIRLIST_FOLDER) != 0, fb = (b->flags & DIRLIST_FOLDER) != 0;
    if (fa != fb) return fa ? -1 : 1;
    int c = name_cmp(a->name, b->name);
    return c ? c : strcmp(a->name, b->name);
}

void dirlist_clear(dirlist_t *l) {
    l->n = l->total = 0;
    l->used = 0;
}

bool dirlist_add(dirlist_t *l, const char *name, uint8_t flags, uint32_t size) {
    l->total++;
    size_t len = strlen(name) + 1u;
    if (l->n >= COCO_DISK_LIST_MAX || len > sizeof l->pool - l->used) return false;
    char *p = l->pool + l->used;
    memcpy(p, name, len);
    l->used += len;
    l->e[l->n++] = (dirlist_entry_t){ .name = p, .size = size, .flags = flags };
    return true;
}

void dirlist_sort(dirlist_t *l) {
    if (l->n > 1u) qsort(l->e, l->n, sizeof l->e[0], entry_cmp);
}

bool dirlist_join(char *out, size_t cap, const char *dir, const char *name) {
    size_t d = strlen(dir), n = strlen(name);
    /* A root of "/" takes no second slash. */
    bool slash = d == 0 || dir[d - 1] != '/';
    if (d + (slash ? 1u : 0u) + n + 1u > cap) {
        if (cap) out[0] = 0;
        return false;
    }
    memmove(out, dir, d);
    if (slash) out[d++] = '/';
    memcpy(out + d, name, n + 1u);
    return true;
}

bool dirlist_within(const char *path, const char *root) {
    size_t r = strlen(root);
    for (size_t i = 0; i < r; i++)
        if (toupper((unsigned char)path[i]) != toupper((unsigned char)root[i])) return false;
    return path[r] == 0 || path[r] == '/';
}

bool dirlist_up(char *path, const char *root, char *left, size_t cap) {
    if (!dirlist_within(path, root)) return false;
    size_t r = strlen(root), n = strlen(path);
    while (n > r && path[n - 1] == '/') n--;   /* a trailing slash */
    if (n <= r) return false;
    size_t cut = n;
    while (cut > r && path[cut - 1] != '/') cut--;
    if (left && cap) {
        size_t len = n - cut < cap - 1u ? n - cut : cap - 1u;
        memcpy(left, path + cut, len);
        left[len] = 0;
    }
    /* Back to the slash before the folder left, never into the root. */
    if (cut > r) cut--;
    path[cut] = 0;
    return true;
}

const char *dirlist_below(const char *path, const char *root) {
    if (!dirlist_within(path, root)) return path;
    const char *p = path + strlen(root);
    while (*p == '/') p++;
    return p;
}

void dirlist_folder_of(char *out, size_t cap, const char *path) {
    if (!cap) return;
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path) : 0u;
    if (slash == path) n = 1u;   /* a file in "/" */
    if (n >= cap) n = cap - 1u;
    memcpy(out, path, n);
    out[n] = 0;
}
