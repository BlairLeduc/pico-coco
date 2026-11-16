/* menu.h — the emulator's menu and pause (design.md §14; EL §10).
 *
 * Core 1, with the guest parked (park.h): the machine is core 1's for as
 * long as either lasts, and so is the keyboard: core 1 polls the
 * southbridge and takes the events itself. What the menu changes for
 * core 0 (the volume, a reset, a restart) goes through g_ui, which core
 * 0 applies when it has the machine back (EL §2.5).
 *
 * The menu is a text page in the VDG's own glyphs (textpage.h), 32 cells
 * by 16 rows in capitals, and closing it invalidates the presenter, so
 * the next snapshot is drawn whole. Its pages and rows are the family's,
 * pico-oric's (menu.c says where they differ). Its status row names the
 * first problem: the settings file's first bad line.
 *
 * Adapted from pico-oric's.
 */
#ifndef PICO_COCO_MENU_H
#define PICO_COCO_MENU_H

#include <stdbool.h>

#include "coco.h"

/* `page` is the frame the menu draws into, core 1's, shared with the
 * missing-ROM page, which is never up at the same time. Core 1, at boot,
 * after the settings are read into g_boot. */
void menu_init(coco_frame_t *page);

/* Run the menu until it is closed. `page` is KM_PAGE_MAIN or the page a
 * function key or Alt+H asked for, which closing then returns from to
 * the guest. `alt` is whether Alt was down when it was asked for, so
 * that Alt+M closes it only as a chord. */
void menu_run(coco_t *m, unsigned page, bool alt);

/* Pause: the guest's last frame stays, the backlight is dimmed, and the
 * status line says so, whether or not it is on. Any key resumes and is
 * not typed; a modifier alone does not, nor does the pause chord's own
 * repeat. Alt+M, Alt+H and the function keys go to the menu instead:
 * returns -1 to resume, or the page, with whether Alt was down in *alt. */
int pause_run(bool alt_held, bool *alt);

#endif /* PICO_COCO_MENU_H */
