/* keymatrix.c — the held-key set and the matrix it drives (design.md
 * §11.1), adapted from pico-oric's. */

#include "keymatrix.h"

#include <string.h>

void keymatrix_init(keymatrix_t *k) {
    memset(k, 0, sizeof(*k));
}

void keymatrix_set_layout(keymatrix_t *k, const keylayout_t *l) {
    k->layout = l;
}

static void enqueue(keymatrix_t *k, uint8_t state, uint8_t code, uint8_t canon) {
    unsigned tail = (k->q_head + k->q_len) % COCO_KEY_EVENT_QUEUE;
    k->queue[tail] = (keymatrix_event_t){ state, code, canon };
    k->q_len++;
}

/* Held-state identity is the physical key, not the code (EL §7.1). */
static int find_open(const keymatrix_t *k, uint8_t canon) {
    for (int i = 0; i < k->n_open; i++) {
        if (k->open[i] == canon) return i;
    }
    return -1;
}

void keymatrix_event(keymatrix_t *k, uint8_t state, uint8_t code) {
    if (code == PICOCALC_KEY_ALT) k->ev_alt = state != KEY_EV_RELEASED;
    uint8_t canon = (code == PICOCALC_KEY_INSERT && k->ev_alt)
                        ? (uint8_t)'i' : keymap_picocalc_canonical(code);
    int o = find_open(k, canon);

    if (state == KEY_EV_RELEASED) {
        /* A release for a press that was refused, or that came before
         * keymatrix_init, has nothing to undo. */
        if (o < 0) return;
        k->open[o] = k->open[--k->n_open];
        enqueue(k, state, code, canon);   /* its slot was kept (below) */
        return;
    }
    if (state != KEY_EV_PRESSED && state != KEY_EV_HELD) return;
    if (o >= 0) return;            /* already down: auto-repeat, or held */

    /* Room for this press, its release, and every release still owed:
     * q_len + n_open never exceeds the queue. */
    if (k->q_len + k->n_open + 2u > COCO_KEY_EVENT_QUEUE) {
        k->dropped++;
        return;
    }
    k->open[k->n_open++] = canon;
    enqueue(k, state, code, canon);
}

static int find_held(const keymatrix_t *k, uint8_t canon) {
    for (int i = 0; i < k->n; i++) {
        if (k->held[i].canon == canon) return i;
    }
    return -1;
}

/* The entry for a code, chosen once at press time. With Alt down it is
 * the Alt layer and nothing else; an Alt chord with no binding maps to
 * nothing, so the layer never leaks a character (§11.3). */
static const keymap_t *find(uint8_t code, uint8_t layer) {
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->code == code && (e->flags & KM_ALT) == layer) return e;
    }
    return NULL;
}

/* With Alt down the Alt layer only, so no layout can take the menu,
 * pause or reset away. Otherwise the game layout first, by physical key,
 * then the standard map (§11.3); Ctrl, which is CLEAR, changes nothing
 * here. */
static const keymap_t *lookup(const keymatrix_t *k, uint8_t code) {
    /* A function key pressed with Alt still held arrives as itself
     * (hardware-notes.md §6.2), so it is matched whatever the modifiers. */
    bool fkey = code >= PICOCALC_KEY_F1 && code <= PICOCALC_KEY_F10;
    if (k->alt) {
        const keymap_t *e = find(code, KM_ALT);
        return e || !fkey ? e : find(code, 0);
    }
    if (k->layout) {
        uint8_t canon = keymap_picocalc_canonical(code);
        for (unsigned i = 0; i < k->layout->n; i++)
            if (k->layout->bind[i].code == canon) return &k->layout->bind[i];
    }
    return find(code, 0);
}

static bool is_modifier(uint8_t code) {
    return code == PICOCALC_KEY_ALT || code == PICOCALC_KEY_CTRL ||
           code == PICOCALC_KEY_SHIFT_L || code == PICOCALC_KEY_SHIFT_R;
}

/* The guest's SHIFT the held set asks for: the latest key down whose
 * binding adds or removes it decides, and otherwise the host's Shift. */
static bool shift_wanted(const keymatrix_t *k) {
    for (int i = k->n - 1; i >= 0; i--) {
        uint8_t f = k->held[i].map.flags;
        if (f & KM_SHIFT) return true;
        if (f & KM_NOSHIFT) return false;
    }
    return k->shift != 0;
}

/* Apply the event at the head of the queue, or say why it must wait. */
static bool apply_head(keymatrix_t *k) {
    keymatrix_event_t ev = k->queue[k->q_head];
    bool down = (ev.state == KEY_EV_PRESSED || ev.state == KEY_EV_HELD);

    if (is_modifier(ev.code)) {
        /* Modifiers report held events while down (hardware-notes.md
         * §6.2). Either Shift is the CoCo's SHIFT, which games read on
         * its own; it is held as long as a key, and a key pressed with it
         * waits a field behind it (EL §7.1). Ctrl is the CoCo's CLEAR,
         * OS-9's control key, held so too (§11.3). Alt reaches no cell. */
        if (ev.code == PICOCALC_KEY_SHIFT_L || ev.code == PICOCALC_KEY_SHIFT_R) {
            uint8_t bit = ev.code == PICOCALC_KEY_SHIFT_L ? 1u : 2u;
            bool was = k->shift != 0;
            uint8_t now = down ? (uint8_t)(k->shift | bit) : (uint8_t)(k->shift & ~bit);
            if (down && !was) {
                k->shift_fields = 0;
                if (k->gap < 1) k->gap = 1;
            }
            if (!now && was && k->shift_fields < COCO_KEY_MIN_FIELDS) return false;
            k->shift = now;
        }
        if (ev.code == PICOCALC_KEY_ALT)  k->alt  = down;
        if (ev.code == PICOCALC_KEY_CTRL) {
            if (down && !k->ctrl) {
                k->ctrl_fields = 0;
                if (k->gap < 1) k->gap = 1;
            }
            if (!down && k->ctrl && k->ctrl_fields < COCO_KEY_MIN_FIELDS) return false;
            k->ctrl = down;
        }
        return true;
    }

    uint8_t canon = ev.canon;
    int h = find_held(k, canon);

    if (ev.state == KEY_EV_RELEASED) {
        if (h < 0) return true;                  /* never mapped */
        if (k->held[h].fields < COCO_KEY_MIN_FIELDS + k->held[h].lead) return false;
        /* Kept in order: the latest down decides SHIFT. */
        memmove(&k->held[h], &k->held[h + 1], (size_t)(k->n - h - 1) * sizeof k->held[0]);
        k->n--;
        k->gap = COCO_KEY_GAP_FIELDS;
        return true;
    }
    if (!down) return true;                      /* unknown state */

    /* Auto-repeat arrives as more presses (hardware-notes.md §6.2). The
     * key is already down, and the ROM does not repeat it. */
    if (h >= 0) return true;
    if (k->gap > 0) return false;

    const keymap_t *e = lookup(k, ev.code);
    if (!e) return true;
    if (e->flags & KM_MENU) { k->menu_request = true; k->menu_page = e->row; }
    if (e->flags & KM_PAUSE) k->pause_request = true;
    if (e->flags & KM_SHOT) k->shot_request = true;
    if (e->flags & KM_RESET) k->reset_request = true;
    if (k->n >= COCO_KEY_HELD_MAX) return true;  /* more keys than fingers */

    k->held[k->n++] = (keymatrix_held_t){ .canon = canon, .map = *e };
    /* A key that changes the guest's SHIFT waits a field behind it. */
    k->held[k->n - 1].lead = shift_wanted(k) != k->shift_shown ? 1u : 0u;
    return true;
}

void keymatrix_field(keymatrix_t *k, coco_t *m) {
    while (k->q_len > 0 && apply_head(k)) {
        k->q_head = (uint8_t)((k->q_head + 1u) % COCO_KEY_EVENT_QUEUE);
        k->q_len--;
    }

    memset(m->keys, 0, sizeof(m->keys));
    /* Each stick's lines that are held, a bit a KJ_ line (§11.4). */
    uint8_t joy[COCO_JOYSTICKS] = { 0 };
    for (uint8_t i = 0; i < k->n; i++) {
        keymatrix_held_t *h = &k->held[i];
        const keymap_t *e = &h->map;
        bool shown = h->fields >= h->lead;
        if (h->fields < UINT8_MAX) h->fields++;
        if ((e->flags & KM_JOY) && e->row < COCO_JOYSTICKS) joy[e->row] |= (uint8_t)(1u << e->col);
        if ((e->flags & KM_NOCELL) || !shown) continue;
        m->keys[e->col] |= (uint8_t)(1u << e->row);
    }
    /* Opposite directions held together leave the axis at its centre. */
    for (unsigned s = 0; s < COCO_JOYSTICKS; s++)
        coco_joystick_set(m, s, ((joy[s] >> KJ_RIGHT) & 1) - ((joy[s] >> KJ_LEFT) & 1),
                          ((joy[s] >> KJ_DOWN) & 1) - ((joy[s] >> KJ_UP) & 1),
                          (joy[s] >> KJ_FIRE) & 1u);
    if (k->shift && k->shift_fields < UINT8_MAX) k->shift_fields++;
    k->shift_shown = shift_wanted(k);
    if (k->shift_shown) m->keys[CK_COL_SHIFT] |= 1u << CK_ROW_SHIFT;
    if (k->ctrl) {
        if (k->ctrl_fields < UINT8_MAX) k->ctrl_fields++;
        m->keys[CK_COL_CLEAR] |= 1u << CK_ROW_CLEAR;
    }
    if (k->gap > 0) k->gap--;
}
