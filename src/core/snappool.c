/* snappool.c — the core 0 -> core 1 video handoff (design.md §9.3, EL §2.4). */

#include "snappool.h"

#include <string.h>

void snappool_init(snappool_t *p) {
    memset(p, 0, sizeof(*p));
    for (unsigned i = 0; i < COCO_SNAP_BUFFERS; i++) p->state[i] = SNAP_FREE;
}

static int find(const snappool_t *p, snap_state_t s) {
    for (unsigned i = 0; i < COCO_SNAP_BUFFERS; i++)
        if (p->state[i] == s) return (int)i;
    return -1;
}

int snappool_claim(snappool_t *p) {
    int i = find(p, SNAP_FREE);
    if (i >= 0) p->state[i] = SNAP_FILLING;
    return i;
}

void snappool_publish(snappool_t *p, int i) {
    if (i < 0 || p->state[i] != SNAP_FILLING) return;

    /* At most one buffer is ever ready, so "the newest ready one" is
     * just "the ready one", and core 1 needs no sequence numbers to pick
     * between them. */
    for (unsigned j = 0; j < COCO_SNAP_BUFFERS; j++) {
        if (p->state[j] == SNAP_READY) {
            p->state[j] = SNAP_FREE;
            p->dropped++;
        }
    }
    p->state[i] = SNAP_READY;
    p->published++;
}

int snappool_take(snappool_t *p) {
    int i = find(p, SNAP_READY);
    if (i >= 0) p->state[i] = SNAP_RENDERING;
    return i;
}

void snappool_release(snappool_t *p, int i) {
    if (i >= 0 && p->state[i] == SNAP_RENDERING) p->state[i] = SNAP_FREE;
}
