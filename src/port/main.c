/* main.c — bring-up, then the two cores' loops (design.md §6, §17 M7).
 *
 * Core 0 owns the 6809 and the machine; core 1 owns the LCD, the
 * southbridge, the card and the log's way out (§6). main() sets the
 * clock, logs the banner, paints the stacks, starts core 1 and waits
 * while it brings up the panel and loads the ROMs off the card (§12.1),
 * then loads them into the machine, powers it on (§13.3) and becomes
 * core 0's loop. Without Extended Color BASIC's two ROMs, core 1 shows
 * the page that says why, and reads the card again when one goes in;
 * with a Color BASIC alone on the card, ENTER starts that instead
 * (roms.h).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#if PICO_COCO_AUDIO
#include "audio.h"
#endif
#include "board.h"
#include "card.h"
#include "coco.h"
#include "core0.h"
#include "core1.h"
#include "handoff.h"
#include "kbd.h"
#include "keymatrix.h"
#include "log.h"
#include "pico_coco_version.h"
#include "roms.h"

/* The guest lives in .bss, not the heap: src/core/ has no allocator, and
 * keeping it static is what makes the §16 budget a link-time fact. */
static coco_t      g_coco;
static keymatrix_t g_keys;   /* core 0's, like g_coco */

/* Whether ENTER came, from the keyboard or the UART. Every event is
 * taken: the page's keys are not the guest's. */
static bool enter_pressed(void) {
    bool hit = false;
    uint8_t state, code;
    while (kbd_pop(&state, &code))
        if (state == KEY_EV_PRESSED && code == PICOCALC_KEY_ENTER) hit = true;
#if PICO_COCO_UART
    int c = getchar_timeout_us(0);
    if (c == '\r' || c == '\n') hit = true;
#endif
    return hit;
}

/* Wait for a BASIC to start: at once if the card had Extended Color
 * BASIC's ROMs, else, with the page up (core 1's), until a card put in
 * has them, or, while the page offers a Color BASIC alone, for ENTER;
 * then claim the images from core 1 (handoff.h). */
static boot_basic_t wait_for_basic(void) {
    uint32_t seen = g_boot.generation - 1u;
    for (;;) {
        uint32_t gen = g_boot.generation;
        __dmb();
        boot_basic_t can = roms_choice(&g_boot.job);
        bool now = roms_start_now(&g_boot.job, g_boot.settings.extended);
        if (gen != seen && !now) {
            seen = gen;
            log_printf("  guest        : not started: no %s%s%s\n",
                       roms_basic_name(BOOT_EXTENDED),
                       can != BOOT_NONE ? "; ENTER starts " : "",
                       can != BOOT_NONE ? roms_basic_name(can) : "");
        }
        bool enter = enter_pressed();
        boot_basic_t pick = now ? can : (enter && can != BOOT_NONE) ? can : BOOT_NONE;
        if (pick != BOOT_NONE) {
            g_boot.claimed = true;
            __dmb();
            while (g_boot.busy) tight_loop_contents();
            __dmb();
            /* A job may have finished between the look and the claim. */
            if (roms_choice(&g_boot.job) == pick) return pick;
            g_boot.claimed = false;
            __dmb();
        }
        sleep_ms(10);
    }
}

int main(void) {
    /* The clock first, so the UART's divider is worked out from the
     * clock that stays. */
    bool clocks_ok = board_init_clocks();
    stdio_init_all();
    board_identify(&g_board);

    /* A startup banner plus consecutive heartbeats is more useful boot
     * evidence than a single line (hardware-notes.md §2.7). Core 1 is not
     * running yet, so printf may block. */
    board_log_banner(&g_board);
    printf("  firmware     : %s\n", PICO_COCO_VERSION);
    if (!clocks_ok) {
        printf("  WARNING: clk_sys is not at 150 MHz; SPI and audio rates will "
               "not be the ones this build assumes\n");
    }

    /* Before core 1 has a stack to use (design.md §16). */
    board_stacks_paint();
    keymatrix_init(&g_keys);
    handoff_init();

    /* Core 1 brings up the panel and reads the card while core 0 waits
     * (§6). */
    multicore_launch_core1(core1_main);
    while (!g_c1.ready) sleep_ms(1);
    __dmb();

    /* From here on core 0 logs through the ring core 1 drains. */
    log_printf("  i2c          : %lu Hz, southbridge version %ld, a register read %lu us\n",
               (unsigned long)g_c1.i2c_hz, (long)g_c1.sb_version,
               (unsigned long)g_bringup.i2c_us);
    log_printf("  lcd          : spi %lu Hz\n", (unsigned long)g_c1.spi_hz);
    log_printf("  core 1       : started at %lu.%03lu ms, ready at %lu.%03lu ms: lcd_init %lu us, "
               "the card's ROMs %lu us\n",
               (unsigned long)(g_bringup.start_us / 1000u),
               (unsigned long)(g_bringup.start_us % 1000u),
               (unsigned long)(g_boot.ready_us / 1000u), (unsigned long)(g_boot.ready_us % 1000u),
               (unsigned long)g_bringup.lcd_us, (unsigned long)g_bringup.card_us);

    /* Returns at once when the ROMs are already here. */
    bool page = !roms_start_now(&g_boot.job, g_boot.settings.extended);
    boot_basic_t basic = wait_for_basic();
    if (page) {
        log_printf("  guest        : %s\n", basic == BOOT_EXTENDED
                   ? "its ROMs are on the card now" : "ENTER: a Color BASIC alone");
        keymatrix_init(&g_keys);
    }

    /* Audio last in bring-up order (hardware-notes.md §10), on core 0,
     * whose IRQ the refill is, and after core 1's LCD has claimed its
     * fixed DMA channel; the PCM then takes the rate the PWM really has
     * (design.md §10). */
#if PICO_COCO_AUDIO
    audio_init();
    uint32_t rate_num, rate_den;
    audio_rate(&rate_num, &rate_den);
#endif
    /* The machine the settings name (design.md §12.6, §14): load the
     * ROMs and the cartridge, then power on (§13.3, coco.h). */
    machine_t mc = {
        .ram_k = g_boot.settings.ram_k,
        .extended = basic == BOOT_EXTENDED && g_boot.settings.extended,
        .cart = g_boot.cart_len > 0,
    };
    /* The BASIC that RAM runs (roms.h): 4 KB is Color BASIC 1.1 alone. A
     * 4 KB machine the card cannot start boots with 16 KB instead. */
    const char *why;
    mc.basic = roms_basic_for(&g_boot.job, mc.ram_k, &mc.extended, &why);
    if (mc.basic == ROM_UNKNOWN && mc.ram_k <= 4u) {
        log_printf("  guest        : ram = 4: %s; 16 KB instead\n", why);
        mc.ram_k = 16u;
        mc.extended = basic == BOOT_EXTENDED && g_boot.settings.extended;
        mc.basic = roms_basic_for(&g_boot.job, mc.ram_k, &mc.extended, &why);
    }
    if (why) log_printf("  guest        : %s\n", why);
    /* The RS-DOS cartridge, where Disk BASIC can run (roms.h). */
    if (mc.cart && card_is_rsdos(g_boot.cart_path)) {
        const char *no = roms_rsdos_why(mc.ram_k, mc.extended);
        if (no) {
            log_printf("  guest        : cart = rsdos: %s; no cartridge\n", no);
            mc.cart = false;
            g_boot.cart_len = 0;
            g_boot.cart_path[0] = 0;
        } else {
            mc.rsdos = true;
        }
    }
    core0_power_on(&g_coco, &mc);
    coco_config_t cfg = g_coco.cfg;
    log_printf("  guest        : CoCo, %u KB, %s (%s%s%s), %lu ticks a field, "
               "hot code in SRAM to tier %u (hot.h)%s\n",
               cfg.ram_k, roms_basic_name(mc.extended ? BOOT_EXTENDED
                                          : mc.basic == ROM_COCO_BAS11 ? BOOT_COLOR11 : BOOT_COLOR13),
               romset_images[mc.basic].file,
               mc.extended ? ", extbas11.rom" : "", cfg.cart == CART_RSDOS ? ", RS-DOS with disk11.rom" : "",
               (unsigned long)COCO_FIELD_TICKS,
               (unsigned)PICO_COCO_RAM_TIER,
               PICO_COCO_FIELD_LOG ? ", logging the first fields" : "");
#if PICO_COCO_AUDIO
    log_printf("  audio        : PWM GP26/GP27, %lu/%lu Hz (%lu.%02lu kHz), %lu ticks per %lu "
               "samples, ring %u slots, queue %u; pacing on the queue\n",
               (unsigned long)rate_num, (unsigned long)rate_den,
               (unsigned long)(rate_num / rate_den / 1000u),
               (unsigned long)(rate_num / rate_den % 1000u / 10u),
               (unsigned long)g_coco.pcm.num, (unsigned long)g_coco.pcm.den,
               (unsigned)COCO_DMA_RING_SLOTS, (unsigned)COCO_PCM_QUEUE_LEN);
#else
    log_printf("  audio        : off; pacing on the microsecond timer\n");
#endif

    core0_run(&g_coco, &g_keys);
}
