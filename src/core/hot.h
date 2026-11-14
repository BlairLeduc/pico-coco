/* hot.h — which of the core's functions run from SRAM (design.md §7.5,
 * hardware-notes.md §9.2, §9.8).
 *
 * On the device, flash code is fetched through a 16 KiB XIP cache that
 * both cores share, and an interpreter alone is larger than that. The
 * SDK's linker script copies every `.time_critical*` input section into
 * SRAM at boot, so naming a function's section is all it takes to move
 * it. That is the SDK's convention, not an SDK dependency: nothing here
 * includes a Pico header.
 *
 * Moves come in tiers, and PICO_COCO_RAM_TIER says how many are taken.
 * Taken from pico-oric, where tier 2 won once core 1 was presenting; here
 * it is a lever for M2's gate and M11's perf pass, each tier measured
 * against tier 0 before it is the default (design.md §7.5, §17). A host
 * build leaves it at 0 and gets ordinary functions.
 *
 *   COCO_HOT1(name)  what the interpreter calls out to: the bus slow path,
 *                    the SAM, the PIAs.
 *   COCO_HOT2(name)  the interpreter and its loop.
 *
 * Whole files are the wrong unit (hardware-notes.md §9.2): mark the
 * function, not the module, and check the symbol moved from 0x1... to
 * 0x2... with arm-none-eabi-nm after the build (hardware-notes.md §9.8).
 */
#ifndef PICO_COCO_HOT_H
#define PICO_COCO_HOT_H

#ifndef PICO_COCO_RAM_TIER
#define PICO_COCO_RAM_TIER 0
#endif

#define COCO_IN_RAM_(name) __attribute__((section(".time_critical.coco_" #name))) name

#if PICO_COCO_RAM_TIER >= 1
#define COCO_HOT1(name) COCO_IN_RAM_(name)
#else
#define COCO_HOT1(name) name
#endif

#if PICO_COCO_RAM_TIER >= 2
#define COCO_HOT2(name) COCO_IN_RAM_(name)
#else
#define COCO_HOT2(name) name
#endif

#endif /* PICO_COCO_HOT_H */
