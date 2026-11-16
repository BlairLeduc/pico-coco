# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §17 holds each milestone's scope
and done-when criteria; this file keeps the full record (EL §14.3).

## M7: the CoCo on the device

On branch `m7`, 2026-11-15 (`design.md` §17). On the owner's board.
**Done**: every criterion of §17's row met, the last by the owner.

**Built.** Core 1's ROM job (`card.c`) hashes every file in
`/coco/roms/` as before and reads again the images the CoCo boots from,
each chosen by SHA-1: Color BASIC 1.3 (1.1 without it) for $A000 and
Extended Color BASIC 1.1 for $8000. `roms.c` decides what can start and
draws the missing-ROM page, which offers a Color BASIC alone on ENTER;
a card put in while the page is up is read again, with pico-oric's
claim between the cores (`handoff.h`). Core 0 (`core0.c`, from
pico-oric's M7) loads the ROMs, powers on, and runs `coco_run_field` a
field at a time against an absolute deadline in master-oscillator ticks
(`COCO_TICK_HZ`), keys from the keyboard and the UART into `keymatrix`
before each field, the snapshot into pico-oric's pool after it, Alt+K
the reset button. Core 1 presents through the core's `present_plan` and
`mc6847_render_line` (`display.c`, from pico-atom's), with the border
drawn when its colour changes and the perf line at the top in the VDG's
glyphs (`status.c`, from pico-oric's, and `test_status`). New in the
core: `textpage.c`, the screen read as text (the boot's `OK`, the
UART's screen dump, and now the host harness's `guest_row`) and pages
written as frames (`test_textpage`; `test_boot` holds a page written
from a booted screen to the snapshot of that screen, byte for byte on
every line); `fieldlog.c`, a field's end as a line. Tools:
`coco-fields` (the host's 150 fields), `page-ppm` (the pages, to look
at), `uart-screen.sh`, `perf-run.sh` and `perf-summary.sh` (from
pico-oric's). `test_boot` boots Color BASIC 1.1 in 64 KB as well, the
page's fallback without 1.3.

**Checked on the board**, 2026-11-15: the owner's Pimoroni Pico Plus 2 W
(RP2350B, chip revision 2, `7458DC82A89AAC12`), 150 MHz, core rail
~1100 mV, die 19–20 °C, the `-DPICO_COCO_UART_PORT=0` image, Pico SDK
2.3.1, Arm GNU 15.2.Rel1, tier 2, flashed over SWD and read over UART0,
the card with the owner's five ROMs. Captures in `out/m7/`, not
committed.

- **Extended Color BASIC boots**: `OK` at field 77, 18,433,344 ticks
  (1.287 s of guest time) after power-on, 1.274 s of wall time after
  it, and 1.933 s after the board's reset: 35 ms to start core 1,
  424 ms in `lcd_init`, 192 ms for the card (a 15 ms warm mount, five
  files hashed, two read again in 32 ms), then the ROM's own boot.
- **The first 150 fields equal the host's.** A build with
  `-DPICO_COCO_FIELD_LOG=ON` recorded the registers, cycle,
  instruction and tick counts at the end of each of the first 150
  fields and logged them after (none dropped); `coco-fields` on the
  host, the same loop with the same ROMs, printed the same 150 lines
  (`cmp`), field 150 ending at 35,876,256 ticks.
- **Typed over the UART**: `PRINT 2+3` printed 5, and `10 FOR I=1 TO
  3:PRINT I*I:NEXT` with `RUN` printed 1, 4 and 9, read back with
  `uart-screen.sh`.
- **Real time.** Five workloads, one boot each, 8 or 9 five-second
  windows after the program started (`out/m7/t2`): rt 1.000 in every
  window, no field late, no slip, no snapshot dropped, no undocumented
  opcode. Core 0 busy and host cycles an instruction:

  | Workload | busy | guest | host cyc/insn | E cyc/insn | snapshot |
  |---|---:|---:|---:|---:|---:|
  | idle, at `OK` | 20.9 % | 17.5 % | 119.4 | 4.06 | 3.39 % |
  | compute, a float loop | 16.7 % | 13.3 % | 95.8 | 4.29 | 3.39 % |
  | scroll, `PRINT` without newline | 21.5 % | 18.0 % | 139.5 | 4.61 | 3.39 % |
  | pmode4, `LINE` and `PCLS` in PMODE 4 | 16.6 % | 13.1 % | 108.9 | 4.94 | 3.45 % |
  | set, random `SET` blocks | 16.7 % | 13.3 % | 92.2 | 4.13 | 3.39 % |

  Against M2's gate, 88–113 host cycles on the CPU alone (§7.5), the
  whole machine with core 1 presenting takes 92–140; the 12–13 % share
  the gate projected is 13–18 % of guest time here, and 17–22 % with
  the keys and the snapshot. Idle is the busiest but one: BASIC's
  keyboard scan, which reads the PIAs through the slow path.
  Presents: at most 12.5 ms, a full repaint, in text; 22.0 ms in PMODE
  4, where `PCLS` repaints every band. pmode4 counted one split field
  (§9.4), in the second it switched to graphics.
- **The perf line costs nothing measured**: without it (the control,
  same sitting, idle and scroll) busy read 20.9 % and 21.4 %, against
  20.9 % and 21.5 %.
- **The snapshot costs 3.4 % of core 0**, about 565 µs a field against
  §9.3's estimate of tens of microseconds. Moved to tier 1
  (`coco_video_snapshot`, `coco_vram_index`, the SAM counter's
  functions) it read 3.17 % against 3.39 % in the same sitting, so the
  cost is the work, not flash. Not kept: M11's perf pass has it.

**Measured**: stack high-water marks, core 0 1,352 of 2,048 bytes (892
in M6; the log's 512-byte line under the heartbeat's formatting), core
1 920. Images: 116,616 bytes of text and 165,356 of bss with the UART
(either port), 113,908 and 163,280 without.

**Looked at on the host**: the missing-ROM page in each case (no card,
a card that does not mount, no `/coco/roms/`, nothing known, Color
BASIC 1.3 alone, 1.1 alone), rendered by `page-ppm` (`out/m7/page-*`).
The first draft ran the offer into the footer on the 1.3 page; the
footer is two lines now.

**Checked by the owner** on the Plus 2 W, 2026-11-15, §17's last
criteria: the banner and `OK` on the panel; a program typed on the
PicoCalc's keyboard and run; `PMODE 4,1:SCREEN 1,1:PCLS:CIRCLE(128,96),80`
drawn in artefact colours inside the buff border; and, booted with the
card out, the page saying there is no SD card, and the machine starting
when the card went back in.

**Not checked:**

- The build without the UART on the board (it builds).
- The page's ENTER on the board.
- A card pulled while the guest runs (logged only: card work waits for
  M9's park).
- Alt+K, the reset button, on the board.
- Artefact colours against XRoar's or a real set's, beyond the owner's
  eye on one drawing.
- Alt+0 arriving as `0` with Alt held (§11.3), still unseen.

All 18 host tests pass.

## M6: board bring-up and the card

On branch `m6`, 2026-11-15 (`design.md` §17). On the owner's board.

**Built.** pico-oric's port drivers copied and renamed, unchanged since
its own M6: `southbridge.*`, `kbd.*`, `log.*`, `lcd.*`, `sd.*`,
`storage.*`, and `diskio.c` and `fatfs/ffconf.h` as its M6 had them (the
shell's clock for file times comes with card writes). FatFs comes from
the SDK's TinyUSB copy, as the Oric's does. Adapted from the Oric's M6:
`display.*`, the corner-coded test pattern on the picture's 256×192 at
(32, 64) and the blit timings; `card.*`, which lists `/coco/roms/`,
hashes each file a sector at a time and names it by SHA-1, its FatFs
objects static (below), its slot debounced for 500 ms (below); `core1.*`,
southbridge, LCD, card, then the 30 Hz keyboard poll, the slot and the
log, with a card that went in and found no volume tried again up to three
times, 500 ms apart, while it stays in; `handoff.*`; and `main.c`, where
core 0 logs every key event, from the keyboard or the UART, with the cell
keymatrix would press, and a heartbeat. New: both stacks painted at boot
and their high-water marks on the heartbeat (§16, `board.c`).
`tools/uart-type.sh` copied. `test_romset` hashes in `COCO_CARD_CHUNK`
pieces, the size `card.c` reads. `romset_identify_digest` was already here
from M3.

**Checked on the board**, 2026-11-15: the owner's Pimoroni Pico Plus 2 W
(RP2350B, chip revision 2, `7458DC82A89AAC12`), 150 MHz, core rail
~1100 mV, die 19–20 °C, the `-DPICO_COCO_UART_PORT=0` image (its DS3231
holds GP4/GP5), Pico SDK 2.3.1, Arm GNU 15.2.Rel1, flashed over SWD and
read over UART0. Captures in `out/m6/`, not committed.

- **The test pattern**, by the owner's eye: the grey frame on all four
  panel edges, the white border on the picture's rectangle, red, green,
  blue and yellow in their corners.
- **Every key**, pressed by the owner, alone, with Shift, on the Alt layer,
  F1–F10, and some held: 654 events over 130 heartbeats, each counted by
  core 1 and logged by core 0, and every release paired with a press of
  the same physical key once letters, shifted punctuation and the
  MCU's Shift chords are folded to their key, as `keymap_picocalc_canonical`
  does. 72 physical keys; none left down. Holding Right or `n` sent more
  presses and one release, and Shift and the F-keys sent held events, as
  hardware-notes §6.2 has it. F7–F9, `` ` ``, `{ | } ~` and Caps Lock
  reach no cell, as the table means.
- **UART keys**, typed with `uart-type.sh` (`a A ! @ 0`, CR, Ctrl-A, F1,
  Esc, `^`), arrive as the keyboard's own events, Shift and Ctrl around
  the key, each logged with the cell keymatrix binds: `@` row 0 col 0
  with SHIFT taken away, `A` row 0 col 1 with SHIFT taken away.
- **The card.** With no `/coco/roms` it says so. With the owner's five
  ROMs there, each was recognised by SHA-1 as its own image, at boot and
  after each of two swaps without a reset, each swap seen as out then in.
- **Zero I²C errors and zero ring overflows** in every run, the longest
  being the owner's 650 s.

**Measured** (one boot each unless given): an I²C register read 4.86 ms;
an empty keyboard poll 4.85–4.86 ms; at 75 MHz, the 256×192 picture in one
colour 11.48 ms, row by row 11.51 ms, one 256-pixel row 68 µs, the whole
panel 23.90 ms; a cold mount 195–197 ms (three), a warm one 14.6–15.6 ms;
an 8 KiB ROM opened, read and hashed 12.4–12.8 ms, the 16 KiB one
21.1–21.6 ms. Stack high-water marks: core 0 892 of 2,048 bytes, core 1
780 with no ROM directory and 824 hashing five files. Images: 53,756 bytes
of text and 8,952 of bss with the UART (either port), 50,016 and 6,880
without.

**Found and fixed on the board:**

- **Core 1's stack reached 1,660 of its 2,048 bytes** on a run that
  opened no file. `card.c` had a `FIL` (with its 512-byte sector), a
  `DIR` and a `FILINFO` as locals, and the compiler inlined the helpers
  that declare them into the job's frame. Made static, 780; with five
  files hashed, 824. hardware-notes §2.3.
- **The first swap failed**: the card was seen out and in, and the job
  250 ms after card detect settled found no volume (`FR_NOT_READY` after
  3.8 ms: no reply to ten `CMD0`s). Card detect did not move again. The
  debounce is now 500 ms, which hardware-notes §7.1 had found reliable on
  later drivers, and two swaps after it mounted first time.
- **The key log looked keys up by canonical code**, so `@` showed as the
  CoCo's `2`. keymatrix binds a press by its code; the log does too.
- **The first poll of a boot read 16 stale events** in 83.6 ms: presses
  made while M2's bench image, which never polls, was running.
  hardware-notes §6.1.

**The board.** §17 asked for a Plus 2 W and a Pico 2 W. The owner,
2026-11-15: the Plus 2 W is the one board, and the second board is M16's
release run. §17's row now says so.

**Not checked:**

- A Pico 2 W, and the default UART1 build on any board (M16).
- The build without the UART on the board (it builds).
- The card retry: with the 500 ms debounce no swap needed it, so the path
  that tries a silent card again has not run.
- A card pulled during a job; card writes, which nothing does yet (M9).
- `lcd_init` sleeps on core 1, harmless while core 0 waits at boot, as
  it does here; it must stay before the guest starts (M7).
- The presenter, the status and perf lines, the guest (M7); audio (M8).

All 16 host tests pass.

**CI** on GitHub, 2026-11-15, PR #7: green on both builds. The firmware
job built both images, with and without the UART, with FatFs taken from
the SDK checkout's TinyUSB submodule; the host job ran all 16 tests.

## M5: the keyboard, on the host

On branch `m5`, 2026-11-15 (`design.md` §11, §17). Host only.

**Built.** `src/core/keymatrix.c` and `keymatrix.h`, pico-oric's held-key
set adapted to the CoCo: the queue that never loses a release, canonical
codes, the binding fixed at press, the replay paced for the ROM, the
Alt layer, the menu's requests and the text path for the UART. What
changed is the guest's: an entry may take the guest's SHIFT away
(`KM_NOSHIFT`) as well as add it, the latest key down that says anything
about SHIFT decides it, held keys are kept in the order they went down,
both Shifts are the one SHIFT, and Ctrl reaches nothing. The guest's
SHIFT changes a field ahead of the key that changes it.
`src/core/keymap_picocalc.c`, the table, written from the sweep below
(§11.3). `config.h` gains the queue's sizes and the replay's 4 fields
held and 2 up. The host harness types on a CoCo through PicoCalc events
and `keymatrix_field` a field, as the firmware will, and on a Dragon by
M3's table, straight into the matrix. `test_keymap` (no ROM) and
`test_keyboard` (the ROMs) are new. No layout code was copied: layouts
come with the keyboard joystick in M12.

**Checked on the workstation** (macOS, Apple clang 21.0.0; the owner's
ROMs):

- **The matrix, by the ROMs** (§11.2, §18). Every one of the 56 cells
  pressed at the prompt, alone and with SHIFT, in Color BASIC 1.1, Color
  BASIC 1.3 and Extended Color BASIC 1.1 in 64 KB, and what POLCAT
  (through its vector at $A000) returned read back. The three ROMs agree
  in every cell. Letters are the same with SHIFT in upper case; SHIFT+@
  is $13; SHIFT+0 returns nothing and flips CASFLG at $011A; the arrows
  are $5E $0A $08 $09, and with SHIFT $5F $5B $15 $5D; CLEAR is $0C, and
  $5C with SHIFT; BREAK is $03 either way. The four cells of row 6
  without a key decode bytes past the ROM's table. Kept as the sweep in
  `test_keyboard`.
- **Every table entry typed through the replay into each ROM**: the 111
  entries with a cell, each as the PicoCalc would send it (inside Shift
  where the PicoCalc shifts it, inside Alt for the Alt layer), and POLCAT
  returned exactly the one code the table means; Alt+0 returned nothing
  and flipped CASFLG. On the screen: Backspace rubs out, End and
  Alt+Left erase the line, Tab clears the screen, Esc stops a running
  program with `BREAK IN 10`, and Alt+0 twice turns lower case on and
  off.
- **The hold and the gap** (§11.1). "AABBAB112" typed straight into the
  matrix from 240 phases a thousand ticks apart: 1.3 types it at 2 fields
  held and 1 up, 1.1 and Extended need 3 held. The controls fail: a field
  fewer held, and no field up. 48 phases at 5,000 ticks found Extended's
  need as 2; the window where it loses a key is a few thousand ticks
  wide. A key held 300 fields types once. Through the replay a line of 10
  keys arriving in one poll takes 59 fields, 5.9 a key.
- **The long program.** 60 lines of `n PRINT n*n`, each line arriving in
  one poll, through the replay: all 60 stored in each ROM (counted along
  the line links from TXTTAB at $19), and `RUN` printed through 3600.
  Straight into the matrix at each ROM's minimum it stored 60, 60 and 59
  (Extended, typed at 2 fields, below its 3); at the replay's 4 and 2, 60;
  at 1 field held, the control, 7, 8 and 10.
- **Type-ahead**: XYZ typed during a loop, then `INKEY$` twice reads Z
  and nothing, and the prompt is empty. The BREAK check keeps the last
  key it saw.
- **The tests bite.** Planted one at a time: `[` on UP failed both tests;
  `@` without `KM_NOSHIFT` failed both; no lead before a key that changes
  SHIFT failed `test_keymap` (8) and passed `test_keyboard`, as the ROMs
  at the prompt never caught a scan straddling the field boundary; the
  replay at 2 held failed `test_keyboard` (4); no gap failed it (12); Ctrl
  ignored failed `test_keymap`. Removing a held key out of order failed
  neither, until a test of three keys held and the first let go was
  added to `test_keymap`, which then failed.
- **The trace diff still agrees**: Extended Color BASIC in 64 KB through
  `PRINT 2+3`, 2,187,479 instructions (its typing is XRoar's, not the
  replay).
- All 16 host tests pass. Both firmware builds are clean and unchanged
  in size (22,772 and 20,940 bytes of text): the image does not yet run
  the machine or link the keyboard. The new sources compile under the Arm
  GCC 15.2 with `-Wall -Wextra -Werror`, but for `guest.c`, whose
  `dirent.h` newlib lacks, as before.

**The Dragon is frozen until M14** (the owner, 2026-11-15). M5's "the
Dragon's matrix as a second table" moved to M14; `test_boot` still types
into the Dragon 32 by M3's table, and passes.

**Not checked:**

- Anything on a device: the southbridge's events through the queue from
  core 1, and a person typing (M6, M7). That Alt+0 and Alt+`@` arrive as
  `0` and `@` with Alt held is read from the MCU's source, not seen.
- What games read: SHIFT alone, keys held together, a scan between the
  field's boundary and the key's lead. The joystick buttons sharing the
  rows (M12).
- Lower case typed: Alt+0 is shown to flip CASFLG, not letters typed in
  lower case afterwards.
- Disk BASIC's and other ROMs' keyboard routines (M13).

**CI** on GitHub, 2026-11-15, PR #6: green on both builds. The host job
(Ubuntu, GCC) ran all 16 tests: `test_keymap` ran, and `test_keyboard`,
`test_boot` and `test_screen` skipped, as they must with no ROMs. So the
sweep, the hold and gap and the long program are checked on the
workstation only. The firmware job built both images, with and without
the UART.

## M4: video, on the host

On branch `m4`, 2026-11-15 (`design.md` §9, §13.1, §15.2, §17). Host
only.

**Built.** `src/core/sam6883.c`, the SAM's video address counter, its
dividers and its clears on HS (§9.2). `mc6847.c` and `mc6847.h`, adapted
from pico-atom's for the CoCo's wiring: semigraphics 4 and 6 by GM0, INV
on bit 6, the external case showing the byte's own bits, the twelve
colour indices in XRoar's order, the artefact colours by XRoar's simple
rule in two phases, the mono palette and the dark backgrounds.
`mc6847_font.c`, pico-atom's copy of XRoar's character ROM, unchanged
(`THIRD-PARTY.md`). In `coco.c`: `coco_video_snapshot`, the 192 lines of
fetched bytes through the counter (§9.3); `coco_run_field` running on to
the next field's first active line, where the snapshot is taken (§13.1);
the count of fields whose video bits changed during active lines
(§9.4). `present.c`, the presenter's plan of dirty bands, and
`snappool.c`, pico-oric's pool. On the host: `test_vdg_oracle` with its
driver `xroar_vdg.c`, which links XRoar's `mc6847.c` and `mc6883.c` from
the pinned checkout as the CPU oracle links its `mc6809.c`;
`test_mc6847`, `test_video`, `test_present`, `test_screen`,
`test_mc6847_golden` and `tools/vdg-ppm.c`, with 25 golden images in
`test/golden/`.

**Checked on the workstation** (macOS, Apple clang 21.0.0; XRoar 1.13,
`af501b71`; the owner's ROMs):

- **The picture agrees with XRoar's, pixel for pixel.** For each of the
  eight SAM modes against each of the ten VDG modes (alphanumeric with
  INT/EXT low and high, and the eight graphics modes), in both colour
  sets, with random RAM, a random display offset (half of them wrapping
  past $FFFF) and the page bit at random: the snapshot drawn by the
  renderer, with a palette of its own indices, against XRoar's MC6847
  fed by its MC6883 wired as XRoar's `dragon.c` wires them. 160 fields,
  each of 49,152 pixels against both of XRoar's half-pixels, and none
  differed. Then 20 more fields with artefacts in both phases, against
  XRoar's simple renderer's rule applied to XRoar's VDG output: none
  differed (`test_vdg_oracle`, 0.3 s).
- **The oracle bites.** Planted one at a time, each failed it: V=1's X
  divider as 2; V=4's Y divider as 3; the clear of B1–B3 without B3's
  fall; the clear of B1–B4 without B4's fall; no fetches past the
  picture's 32 or 16; INV ignored; SG6's colour from bits 6–5; SG4's
  halves split at line 5; KRBW drawn as KBRW; the two-colour
  background black with CSS low; the page bit in the VDG's address
  (which passed until the test set P at random, and then failed); the
  external case drawn from the font; RG3 fetched as a 32-byte mode. A
  no-op planted the same way passed. Apple's make compares times to
  the second, so a plant written and undone within one second left a
  stale object and a false failure once; the script now waits.
- **The ROMs settle the VDG's wiring** (§18): Extended Color BASIC 1.1,
  64 KB, PMODE 0 to 4 under SCREEN 1,0 and 1,1 from a program, each
  giving the datasheet's matched pair (RG2/3, CG3/4, RG3/5, CG6/6,
  RG6/6), CSS as SCREEN's colour set, and page 1 at $0600. The boot
  screen's snapshot is alphanumeric, SAM mode 0, at $0400, and each of its
  16 rows reads as the text in RAM (`test_screen`).
- **The field's shape** (EL §5.6): a guest loop waiting on FS escapes
  once in each of 119 fields, and its 58-line redraw is complete in all
  120 snapshots; snapshots taken where FS rises, the control, catch it
  half done in 120 of 120. Planted: ending the field at FS's rise tore
  snapshots (`test_video`).
- **The counter's addresses**, read back through two snapshots, in the
  nine matched modes and in semigraphics 8, 12 and 24 (a new row every
  3, 2 and 1 lines), and RG6 from $FE00 reaching $0000 at line 16
  (`test_video`). Planted: V=2's Y divider as 2 failed CG2 and SG8.
- **§9.4's count**: CSS toggled throughout marks 10 of 10 fields; the
  same bits rewritten, the control, none. Planted: not marking failed it.
- **The presenter's plan**: a simulated panel updated from dirty bands
  alone equals a full render after each of 1,000 random changes (bytes;
  a third the mode or colour set; some a change of artefacts reported by
  `present_invalidate`). The control, blind to the mode, left stale
  pixels after 995. Planted: dropping the mode test, and dropping the
  invalidation, each failed (`test_present`). The pool's transitions
  and 100,000 random steps of both cores, as pico-oric's.
- **Golden images looked at**: the 25 scenes, by Claude and then by the
  owner on 2026-11-15, on a contact sheet; the owner found them right. Every glyph plain and inverse, '@' in zeroed
  cells, the semigraphics bytes; the external case's raw bits and SG6
  in blue and red, magenta and orange; in each graphics mode the frame
  on all four edges, the diagonal and the bars in the right colour set;
  semigraphics 8, 12 and 24; artefact bands giving black, blue, red,
  white, swapped in KRBW. A one-pixel change planted in RG6's scene
  failed the test at that pixel. Screens drawn by BASIC were looked at
  too: the boot screen, SET's semigraphics 4 in nine colours, PMODE 3 in
  both colour sets, PMODE 4 with a circle and stripes plain and in both
  artefact phases (`out/screens/`, not committed).
- **The trace diff still agrees**: Extended Color BASIC in 64 KB through
  `PRINT 2+3`, 2,187,479 instructions, and the Dragon 32 through boot,
  2,188,868, with the field ending at the active line.
- All 14 host tests pass; both firmware builds are clean and unchanged
  in size (22,772 and 20,940 bytes of text), since the image does not yet
  run the machine. The test sources were also compiled by the Arm GCC
  15.2 under `-Wall -Wextra -Werror` to catch GCC-only warnings before
  CI: two `%u` given a `uint32_t` were fixed.

**The Dragon is frozen until M14** (the owner, 2026-11-15). M4 added
nothing for it beyond keeping M3's field ending at its active line.

**Not checked:**

- Anything on a board: the snapshot's cost on core 0, the presenter on
  the panel, the band and window costs (M7).
- XRoar's own rendering, colour for colour: the oracle compares colour
  indices, and the palette is a rendering choice (§9.5). Artefacts are
  held to XRoar's simple rule, not its finer renderers, and to no
  photograph of a real set.
- A change of V within a field, and the counter's glitches XRoar models
  for it; mid-line changes of CSS or mode, which XRoar renders and this
  does not (§9.4). The list of changes within a field is not built,
  only the count.
- INT/EXT from GM0 by a ROM; the MC6847T1; the SAM's fast rates' effect
  on video.
- The PAL machines' picture (frozen).

**CI** on GitHub, 2026-11-15, PR #5: green on both builds. The host job
(Ubuntu, GCC) ran all 14 tests: `test_vdg_oracle` and
`test_mc6809_oracle` ran against XRoar 1.13, `test_mc6847_golden`
passed on the committed images, and `test_boot` and `test_screen`
skipped, as they must with no ROMs. So the ROMs' screens and the trace
diff are checked on the workstation only. The firmware job built both
images, with and without the UART.

## M3: the machine on the host

On branch `m3`, 2026-11-14 (`design.md` §8, §13, §15.2, §17). Host
only, besides the bench re-measured on the board.

**Built.** `src/core/coco.c`, the machine: the page table built from the
SAM's map, with RAM reached through the SAM's row and column and the
chips' organisation, a page taking the fast path only where its 256
bytes lie in order; the I/O page's decode; the PIAs' wiring; the
keyboard matrix; and the field loop in master-oscillator ticks, split
where FS falls. `mc6821.c`, the generic PIA, with tRS3 on its IRQ
outputs. `sam6883.h`, the SAM's register. `sha1` and `romset` from
pico-oric, with the owner's five ROMs. The Dragon 32 as the second
wiring: its 16 KB ROM, its PAL field, PIA1 port B and the PIA decode. In the CPU: `io_at`, the cycle
of an instruction a slow-path access falls on, and
`mc6809_lines_late`, for a change made in an instruction's last cycle.
On the host: `test/host/guest.c`, the harness, from pico-oric's;
`tools/trace/coco-trace.c`, `trace-diff.py` and `trace-coco.sh`, the
trace diff against XRoar. The bench now uses the machine's RAM.

**Checked on the workstation** (macOS, Apple clang 21.0.0; XRoar 1.13
headless, `af501b71`; the owner's `bas13.rom`, `extbas11.rom`,
`disk11.rom`, `bas11.rom` and `d32.rom`, whose CRC-32s are in XRoar's
list):

- **The CoCo boots its real ROMs**: Color BASIC 1.3 alone and with
  Extended Color BASIC 1.1, in 16, 32 and 64 KB, each to its banner and
  `OK` in 0.63–1.28 guest seconds, read from video RAM. Each answers
  `PRINT 2+3` with 5, typed through the matrix. Each takes every letter,
  digit and shifted symbol through the matrix and prints it back. Each
  gives `PRINT MEM`: 14631, 31015 and 31015 alone; 8487, 24871 and 24871
  with Extended (`test_boot`).
- **A 4 KB CoCo boots Color BASIC 1.1**, and BASIC 1.1 boots in 16 KB,
  each answering the same typed lines, with `PRINT MEM` 2343 and 14631
  (`test_boot`).
- **4 KB with BASIC 1.3 does not boot, and should not.** The ROM drives
  PIA0 PB6 low and reads the strap on PIA1 PB2, which a 4K machine ties
  low, and takes 64K mode, which 4K chips cannot serve. It hangs sizing
  memory. XRoar hangs the same way, and the trace diff holds the two
  together for ten seconds. 4K machines shipped with BASIC 1.0 and 1.1.
- **The Dragon 32 boots on the host**, the second wiring: Dragon BASIC
  to its banner and `OK` in 1.29 guest seconds in 32 KB, and expanded to
  64 KB. It answers `PRINT 2+3`, takes every letter, digit and shifted
  symbol through its own matrix and prints them back, and gives
  `PRINT MEM` 24871 (`test_boot`). Its field is XRoar's PAL one, 312
  lines with the first 287 (`design.md` §13.1).
- **The trace diff against XRoar runs through boot and typing.** Every
  configuration, 4, 16, 32 and 64 KB, alone and with Extended, agrees
  instruction for instruction, every register, for ten guest seconds:
  1.9–2.2 million instructions each, through `PRINT 2+3` and `PRINT MEM`
  typed. So do BASIC 1.1 in 4, 16, 32 and 64 KB, and the Dragon 32 in
  16, 32 and 64 KB, and all twelve again for 1,500 fields through a typed
  program (5.3–6.4 million instructions each). Fifty guest seconds of a
  program counting to 20,000 also agree on the Dragon 32 in 32 KB (10.7
  million instructions) and on BASIC 1.1 in 4 KB (10.9 million). Fifty guest seconds of a typed program printing `I;I*I;TIMER`
  300 times also agree: 10.9 million instructions on 64 KB with Extended,
  11.0 million on 16 KB. Typing goes in on both sides as XRoar's `-type`
  puts it in: POLCAT returns each character and the 1 ms delay returns at
  once, in no time. Each ROM's addresses are XRoar's for that image:
  POLCAT is `$A1C1` in BASIC 1.1 and `$A1CB` in 1.3, and the Dragon's
  are `$BBE5`, `$BBC5` and its key scan at `$851B`.
- **What the trace diff settled**, each by a divergence and its cure
  (`design.md` §7.1, §8.1, §8.3, §13.1, §18):

  | First divergence | Cause | Now |
  |---|---|---|
  | `LDA 2,X` reading `$8000`, no Extended: `$27` against `$FF` | An empty socket reads the floating bus | The byte at `$FFFF`, as a dead cycle leaves it |
  | Extended reading `$FF22`: PB0 high | Port B has no pull-ups | Undriven port B inputs read low |
  | 4K reading `$FF20`: PA0 high | The cassette input | Low with no tape |
  | Extended reading `$FF22`: PB1 low | The single-bit sound's feedback | High while silent (M8) |
  | Dragon reading `$FF22`, 23 instructions in: PB0 and PB1 high | XRoar's absent printer is never busy; its sound feeds PB1 back only once it has updated | PB0 low; PB1 low until the first FS rise or the sound enabled, then high (M8) |
  | The field interrupt an instruction early, then late | The CPU's latch at its last cycle, and tRS3 | Edges and rises seen a cycle late; tRS3 14 ticks |
  | `TST 2,U` and `ROL 2,U` on `$FF02` as FS falls | A read in the edge's instruction clears its flag | PIA accesses brought up to their cycle (`io_at`) |
  | Long runs, a read in an instruction's last cycle | The CPU has already latched the line | `mc6809_lines_late` |

  Along the way, a one-cycle phase fitted to the field matched the
  ten-second runs. It was tRS3 misread, and is gone. A filter in the
  diff tool skipped XRoar's lines containing `[`, so indirect
  instructions looked like divergences until it was dropped.
- **Bugs planted one at a time**, each against the trace diff (16 KB
  Extended, ten seconds, or the 50-second program) and the host tests:

  | Bug | Trace diff | Host tests |
  |---|---|---|
  | FS a line early | parts at 125,092 | pass |
  | No tRS3 | parts at 175,886 | pass |
  | No last-cycle rule | agrees ten seconds; parts at 1,037,379 in the 50-second run | pass |
  | Port B pulled up | parts at 124,839 | pass |
  | V and W swapped in the harness's matrix | (not its concern) | `test_boot` fails, every size |
  | The Dragon typed through the CoCo's matrix | (not its concern) | `test_boot` fails, the Dragon |
  | The Dragon's first field full length | parts at 258,247 | `test_machine` fails |
  | The Dragon on NTSC's field | parts at 258,173 | `test_machine` fails |
  | PB1 fed back from power-on | parts at 23 on the Dragon; the CoCo agrees | pass |
  | The Dragon's PB0 pulled up | parts at 23 | pass |
  | The expanded Dragon's PIAs repeating | (BASIC does not look) | `test_machine` fails |

  The PB1 plant shows why the Dragon earned its place in M3: no CoCo
  ROM reads the pin before the first field ends.

  Before the every-key line was added, the V and W swap passed
  `test_boot`, since `PRINT 2+3` and `PRINT MEM` type neither.
- **The host tests pass**, eight of eight: `test_machine`, with a
  dozen-instruction test ROM, needing no real ROM, covers the SAM's 16
  bit pairs, RAM through the SAM in 16K and 64K modes with the page bit
  and map type, the slow path where no chip answers, sixty FS interrupts
  in sixty fields with CB1 disabled as the control, 262 HS interrupts a
  field with CA1 disabled as the control, the Dragon's 312-line field and
  287-line first, its 50 FS and 3,120 HS interrupts, its PIA decode with
  and without expansion, the keyboard's rows and columns through PIA0,
  the copy and the run contract. `test_mc6821` covers the
  PIA from its datasheet, each rule beside a control. `test_romset`
  covers SHA-1, the table against `roms/README.md`, and each image found.
  The oracle, the behaviour tests and the bench still pass with the CPU's
  changes, and the bench's counts are unchanged.
- **Power-on**: the warm-start flag is `$71` against `$55`, which zeros
  fail; `RND`'s seed comes from the ROM. No seed is taken from RAM
  (`design.md` §13.3).
- Both firmware builds are clean with the machine in the core library.

**On the board**, 2026-11-14, the owner's Pico Plus 2 W: the bench, with
this milestone's CPU and the machine's RAM, 87.6 host cycles an
instruction on basic and 113.9 on the mix (M2: 88.1 and 113.3), 12.4 %
and 13.3 % of core 0, counts equal to the host's. So `io_at` and the
last-cycle rule cost nothing measurable. Capture in `out/m3/`, not
committed.

**CI** on GitHub, 2026-11-14, PR #4: green on both builds. The host job
(Ubuntu, GCC) ran all eight tests: `test_machine` passed on its test ROM,
`test_romset` passed what it can check without images, the oracle ran
against XRoar 1.13, and `test_boot` skipped, as it must with no ROMs.
So the boot and the trace diff are checked on the workstation only. The
firmware job built both images, with and without the UART. Green again
after the Dragon and 4 KB work (`a9cfc32`), with the Dragon's field and
decode in `test_machine`.

**Review**, 2026-11-15: the owner's review of PR #4, ten findings.

| Finding | Outcome |
|---|---|
| A SAM rate change partway through a slice overshoots it | Fixed: the instruction ends the slice (`mc6809_stop`). `test_machine` runs fast for 150 lines, then slow; unfixed, the first field ends 282,336 ticks in, not 238,944 |
| A PIA control write within a slice does not shorten it | Fixed likewise, as is an edge that schedules a rise. `test_machine` enables CA1 partway through a slice; unfixed, 33 line interrupts in that field, not 260 |
| The IRQ output ignores C2 once C1 is enabled | Taken from the datasheet: each flag gated by its enable. XRoar's `write_cr` behaves as the old code did; no trace tells them apart. `test_mc6821` covers both cases |
| A rise due after its interrupt is disabled still comes | Fixed: it stays down, as the datasheet gates it. XRoar's rise comes regardless; no trace tells them apart. `test_mc6821` |
| P1 applied in map type 1 | Kept as XRoar has it. The CoCo 2 service manual says P1 is unused on the machine and should be kept clear, so no software relies on it; recorded in `design.md` §18 |
| An empty or short trace reports agreement | Fixed: either side empty, or XRoar's shorter than ours, fails |
| The trace driver's breakpoint judged interrupts from the lines now, not the latched ones | Fixed: `mc6809_fetches_next`, the core's own boundary decision |
| Fixed capacities written as literals | Moved to `config.h`: ROM, cartridge, PIA ports, the matrix |
| `map()` translates every byte of every page | Nine translations a page: the translation is a bit map. `test_machine` holds it to the byte by byte check in every size, mode, page bit and map type; seven bits checked instead of eight fails 768 pages |
| `romset_identify` reads as a per-image loop | Rewritten as a size check, one hash, one lookup |

After the fixes, all twelve configurations agree with XRoar for 1,500
fields with typing and a program, and the 50-second programs on 64 KB
Extended (10.9 million instructions), the Dragon 32 (12.9 million) and
4 KB BASIC 1.1 (10.9 million).

The stop costs on the board, measured on the owner's Pico Plus 2 W
against a control in the same sitting: a flag checked each step gave
90.1 and 116.0 host cycles an instruction on basic and the mix, the
control 87.6 and 113.2; the run's limit as a field the machine zeroes
gives 88.1 and 114.8, which is kept. Captures in `out/review/`, not
committed.

**Not checked:**

- What the MC6883 does with P1 set in map type 1 (above): unused on the
  CoCo 2, by its service manual.
- The Dragon's PAL field against its schematic: it is XRoar's
  placement, held by the trace diff (M14). Likewise PIA1 PB0 and the
  start of PB1's feedback, which are XRoar's behaviour.
- The Dragon's arrows, CLEAR and BREAK, as the CoCo's (M5).
- BASIC 1.0, and `dplus49b.rom` (DOSplus 4.9B, in XRoar's list), which
  is in `roms/` but not in the table until DragonDOS (M14).
- The SAM's fast rates (R0's address-dependent one runs at rate 0; M11)
  and its video counter (M4); open bus after the indexed modes whose last
  cycle before the access is not a dead one; PIA C2 pulse modes.
- Keyboard ghosting and the column lines driven back by rows; the
  arrows, CLEAR, BREAK, hold and gap (M5).
- HALT released with a line raised while halted (from M1).

## M2: the gate, the CPU alone on the board

On branch `m2`, 2026-11-14 (`design.md` §7.5, §17).

**Built.** `src/bench/`, adapted from pico-oric's: two 6809 workloads on a
flat bus, run by `test_bench` on the host and by a bench image,
`pico-coco-bench`, on the board. **basic** is Color BASIC's character
fetch in the direct page where the ROM copies it, walking a line of sums,
with a five-byte float add: written to the ROM's shape, not copied from
it. **mix** is 4,000 random documented instructions in a loop (254
distinct opcodes, every addressing mode, indexed postbytes drawn from all
the documented ones; the code ends at $65F0), with its code's pages
sending writes to the slow path so a random store cannot rewrite it. Each
run is ten guest seconds (600 NTSC fields), in slices of one field with
the overshoot carried, through `mc6809_run`. The CPU now counts the
instructions it executes (`mc6809_t.instructions`). A build option,
`PICO_COCO_IDX_OUTLINE`, calls the indexed decoder instead of inlining
it, for the measurement M1 carried.

**Checked on the workstation** (macOS, Apple clang 21.0.0; Arm GNU
15.2.Rel1 and Pico SDK 2.3.1 for the firmware):

- `test_bench` passes: basic stores 45, 45 and 65 as packed floats and
  makes 820 passes of its line in ten guest seconds; the mix runs no
  undocumented opcode and two runs from a fresh load end identical,
  registers and memory; each run ends within one instruction of its
  length; and the counts equal `bench.h`'s, which the firmware checks the
  board's against. With the decoder inlined and out of line, all four
  host tests pass, the oracle included.
- **Bugs planted one at a time** in a scratch copy: the digit 9 in the
  float table made 9.5 fails the sums (45.5 for statements 1 and 2); the
  mix's seed changed by one fails the counts (1,759,768 instructions for
  1,760,687). So equal counts on the board are evidence that the same
  code ran.
- Every firmware build is clean, with and without the UART and with
  UART0 for the owner's board. `pico-coco.elf` does not link the CPU yet,
  so its size is M0's.

**On the board**, 2026-11-14: the owner's Pimoroni Pico Plus 2 W
(RP2350B, chip revision 2, `7458DC82A89AAC12`) at 150 MHz, core rail
~1100 mV, die 18–20 °C, one core and the XIP cache to itself; images
built with `-DPICO_COCO_UART_PORT=0`, flashed over SWD, read over UART0.
Each image ran at least two passes of both workloads, and **every pass
of every image counted the host's cycles and instructions** (8,960,403
cycles; 2,118,076 instructions for basic at 4.230 cycles each, 1,760,687
for the mix at 5.089). Host cycles per guest instruction, and the share
of core 0 a guest at 0.895 MHz would take:

| Image | text | basic | | mix | |
|---|---:|---:|---:|---:|---:|
| tier 0, decoder inlined (the control) | 115,108 | 99.5 | 14.0 % | 863.7 | 101.3 % |
| tier 1, decoder inlined | 114,956 | 99.2 | 14.0 % | 866.0 | 101.5 % |
| tier 2, decoder inlined | 114,964 | 83.3 | 11.7 % | 104.9 | 12.3 % |
| tier 0, decoder out of line | 62,956 | 88.5 | 12.5 % | 537.9 | 63.1 % |
| tier 2, decoder out of line | 62,796 | 88.1 | 12.4 % | 113.3 | 13.3 % |

Pass to pass, each figure held to 0.02 %. `arm-none-eabi-nm` showed each
tier's functions where they belong: tier 1 moves `attention` (1.9 KB);
tier 2 also moves `mc6809_run` (71.0 KB inlined, 26.7 KB out of line),
`page2`, `page3` and, out of line, `ea_indexed`. The run loop's
disassembly at the default: the step inlined, one load and test of
`attn`, the fetch, the cycle table, a `tbh` dispatch. Captures in
`out/m2/`, not committed.

**The gate passes with the first lever applied** (`design.md` §7.5).
Against the estimate of 200–350 host cycles an instruction and 27–47 %
of core 0, the BASIC-shaped loop took 83–100 cycles. The mix, from flash,
took 864 and failed at 101 %: a 71 KB interpreter thrashes a 16 KB cache
when the program reaches every opcode. From SRAM both pass at 12–13 %.
So **the firmware's default tier is now 2**, and **the decoder is out of
line by default**: 34 KiB in SRAM, within `design.md` §16's 30–45 KB,
against 88 KiB for 6–8 % fewer cycles; and 63 % on the mix at tier 0
against 101 % should SRAM ever be short. A fresh build directory with no
options gave tier 2 and the decoder out of line, and on the board
repeated that row: 88.1 and 113.3. Neither 300 MHz nor the loop's shape
was needed. The finding is in `hardware-notes.md` §9.8 and
`emulator-lessons.md` §3.4.

**CI** on GitHub, 2026-11-14, PR #3: green on both builds. The host job
(Ubuntu, GCC) ran all four tests, `test_bench` among them, so the counts
in `bench.h` hold under GCC as under Apple clang, and the oracle ran
against XRoar 1.13, not skipped. The firmware job built the bench image
beside the emulator's, with and without the UART.

**Not checked:**

- A second board: the Pico 2 W, which the release must also run on
  (`design.md` D3), and the UART1 build on a board without a clock.
- The shared cache: core 1 presenting, the audio IRQ and the card all
  fetch through it. M11 measures the tiers again with the whole emulator,
  as the Z80's did (`hardware-notes.md` §9.8).
- The real ROM's interpreter, whose opcode spread decides where between
  the two workloads a CoCo falls: M3 on the host, M7 on the device.
- The loop's bounds check on the opcode (`cmp #255` before the `tbh`),
  left because the opcode is held in 16 bits for the trap's record: two
  instructions per instruction, for M11.
- From M1, still: HALT released with a line raised while halted; the
  cycle within an instruction of a slow-path access (M3).

## M1: the 6809 on the host, the XRoar oracle

On branch `m1`, 2026-11-14. Host only: no board step belongs to M1
(`design.md` §17).

**Built.** `src/core/mc6809.c` and `.h`, a new MC6809E core in the
family's style (`design.md` §7.2): one switch per opcode page, the cycle
table from the datasheet, the indexed postbyte decoded by one inlined
function, flags in small helpers; IRQ and FIRQ as source masks, NMI as a
latched edge armed by any load of S; SYNC and CWAI polled through a
one-cycle latch, and skipped in one go when no poll could change
anything; HALT; undocumented opcodes trapped and counted, consumed as a
NOP (D12); `mc6809_run` as the seam's contract (§6). `src/core/coco.h`
and `bus.h` hold the page table and its fast path; the slow path is the
caller's until M3. `tools/trace/build-xroar.sh` clones XRoar at the 1.13
tag (`af501b71`) into `reference/` and builds it headless in `out/xroar`.
`test_mc6809_oracle` holds the core to XRoar's `mc6809.c` through
`test/host/xroar_cpu.c`; `test_mc6809_behaviour` covers what the oracle
cannot see. CI fetches the checkout and fails if the oracle skips.

**Checked on the workstation** (macOS, Apple clang 21.0.0, CMake 4.4.5;
Arm GNU 15.2.Rel1 and Pico SDK 2.3.1 for the firmware):

- **The oracle passes**, 3,424,027 comparisons: every one of the 266
  documented opcodes, 2,048 trials each over random registers, with every
  indexed postbyte and every `TFR`/`EXG` register pair eight times; 8,512
  more on the page table's fast path over a real 64 KB, memory compared
  whole; 39,680 interrupt trials (20,059 taken) against masks and NMI's
  arming; 27,007 SYNC and 26,958 CWAI trials, more than half with the
  lines changed at a chosen cycle of the wait, every instruction boundary
  compared to a horizon of 160 cycles; 3,999 resets. Compared: every
  register, NMI's arming, the cycle count, every write in order, and our
  reads as a subsequence of XRoar's. About 0.6 s.
- **No datasheet cycle count or flag rule was changed to pass.** The first
  run agreed on every instruction trial. What did not agree at first was
  the waits: a line raised during an interrupt entry, or as SYNC ended,
  was taken one instruction early. XRoar decides the next boundary on the
  lines latched before the entry; the core now does too (`design.md`
  §7.1). The rest of the early differences were the harness's own (code
  past the planted NOPs, a NOP run wrapping over the vectors).
- **The behaviour tests pass**: the run contract (never less than asked,
  never more than one instruction over, the returns summing to the CPU's
  count, `run(1)` over a 4-cycle instruction returning 4); reset; NMI
  held until S is loaded, not retaken while the level stays high, retaken
  on a second edge; FIRQ over IRQ, 3 bytes and E clear against 12 and E
  set, RTI by the stacked E, with E cleared by hand as the control; SYNC
  with IRQ masked and not, with a line already asserted (4 cycles), and
  not ended by an unarmed NMI; CWAI kept waiting by a masked FIRQ, ended
  by IRQ without stacking again, and by NMI through `CWAI #$FF`; HALT;
  the trap on each page, an undocumented postbyte and a mixed `TFR`, with
  a documented control beside each; `CLR` reading before it writes, with
  `STA` writing only as the control.
- **The skip is held to a stepping machine.** A program idling in SYNC
  with IRQ masked and in CWAI for FIRQ, over 400 line changes at random
  cycles, run once in slices with the skip and once one step or one poll
  at a time: registers, latch, cycle count and its counters equal at
  every change, after 100 or more wake-ups. The control, the stepping
  machine one cycle late, parts from it.
- **Bugs planted one at a time** in a scratch copy, each run against both
  tests:

  | Bug | Oracle | Behaviour |
  |---|---|---|
  | Half-carry from bit 3 instead of bit 4 | fails: 16 opcodes, 7,175 trials | passes |
  | A taken long branch a cycle short | fails: 14 opcodes, 14,647 | passes |
  | `n16,R` indexed a cycle short | fails: 56 opcodes, 3,641 | passes |
  | `MUL`'s C from bit 15 instead of bit 7 | fails: 1 opcode, 1,079 | passes |
  | NMI taken before S is loaded | fails: 12,026 | fails |
  | No post-entry latch (lines now, not latched) | fails: 89 | passes |
  | `CLR` writes without reading | **passes** | fails |
  | SYNC's skip ignoring the lines | **passes** | fails: the skip parts |
  | CWAI's skip ignoring the lines | **passes** | fails: the skip parts |
  | `mc6809_run` stopping 4 cycles short | passes | fails |

  The oracle steps one instruction at a time, so it never reaches the
  skip; and its read check is a subsequence, so a missing read passes it.
  Those are the behaviour tests' to catch, and they do.
- **The skip, without XRoar**: configured with no checkout, the oracle
  builds as a stand-in that CTest reports **skipped**, with the script to
  run in its output. Fetched fresh with `--checkout-only` into a scratch
  directory, the oracle built and passed.
- **The headless XRoar builds**: `tools/trace/build-xroar.sh` configured
  1.13 with no GUI, audio or OpenGL, built it, and ran `xroar -help`.
- **Both firmware builds are clean** with the core library compiled for
  the RP2350 under `-Werror`. The firmware does not link the CPU yet
  (M2), so its sizes are M0's: 22,772 text (UART on) and 20,940 (off).
- **The interpreter's size on the RP2350** (`-O3`, `arm-none-eabi-size`
  of the core library): 151,072 bytes at first, because `mc6809_step` and
  `mc6809_run` each inlined their own copy of the step. `mc6809_step` is
  now `mc6809_run(m, 1)`, which is the same thing, and the size is
  88,208, `mc6809_run` 71,000 of it. With the indexed decoder out of line,
  measured in a scratch copy, 36,160. Carried to M2 (`design.md` §7.5).
- **A published 6809 test of standing**: none found (`design.md` §18).

**CI** on GitHub, 2026-11-14, PR #2: green on both builds once the host
build's one GCC-only error was fixed (`test_util.h`'s counter unused in
the oracle; Apple clang does not warn). The host job fetched XRoar 1.13
(`af501b71`), and `test_mc6809_oracle` ran and passed in 0.74 s on
Ubuntu's GCC, not skipped; `test_mc6809_behaviour` passed beside it.

**Not checked:**

- Anything on a board: the CPU's speed and its counts against the host's
  are M2's gate.
- The HD6309: M15.
- What the CPU does after HALT is released with a line raised while
  halted: by a reading of its source, XRoar decides on the lines latched
  before the HALT, and the core decides on the lines then. Not run, since
  the oracle does not drive HALT;
  RS-DOS's HALT pacing in M13 is where it would show.
- The cycle within an instruction on which a slow-path access falls, for
  the PIAs (`design.md` §7.2): M3.
- The indexed decoder inlined against out of line, for speed: M2.

## M0: skeleton, both builds, CI

On branch `m0`, 2026-11-14. The layout of `design.md` §6 with what M0
needs and no emulation: CMake with a host build (`-DPICO_COCO_HOST=ON`,
CTest) and a `pico2` firmware build, both under `-Wall -Wextra -Werror`;
`src/core/config.h` and `hot.h`; `test/host/test_util.h` and
`test_skeleton`; the version header from `git describe` at build time;
`arm-none-eabi-size` printed on every build; CI building the host core
and its tests, the firmware, and the firmware without the UART;
`CLAUDE.md`, `THIRD-PARTY.md`, `roms/README.md`, `README.md`.

**Taken from pico-oric**, at its head of 2026-11-14 (`92fe8aa`) and
renamed `ORIC` → `COCO`: `cmake/`, `board.*`, `hot.h`, `test_util.h`,
`tools/build.sh`, `flash.sh`, `uart-log.sh`, `ci.yml`, `LICENSE`. `main.c`
is the Oric's M0 banner and heartbeat. Document references in their
comments changed to this project's sections.

**The siblings' files sorted.** Every tracked file in pico-atom and
pico-oric checked against `design.md` §5's lists; the ones the lists did
not name are sorted in a new table at the end of §5.

**Checked on the workstation** (macOS, Apple clang for the host; Arm GNU
toolchain 15.2.Rel1 and Pico SDK 2.3.1 from `~/.pico-sdk` for the
firmware):

- the host build is clean and `test_skeleton` passes, 1 of 1;
- **an SDK include in a core header fails the host build**:
  `#include "pico/stdlib.h"` planted in `src/core/config.h` stopped
  `test_skeleton.c` with `'pico/stdlib.h' file not found`, and the build
  was green again with it removed (EL §2.1);
- both firmware builds are clean. Sizes, `arm-none-eabi-size`:

  | Build | text | data | bss |
  |---|---:|---:|---:|
  | `build/pico`, UART on | 22,764 | 0 | 860 |
  | `build/pico-release`, UART off | 20,932 | 0 | 844 |

**On the board**, 2026-11-14: a Pimoroni Pico Plus 2 W, as the owner
identified it (RP2350B, QFN-80, chip revision 2, module id
`7458DC82A89AAC12`), in the owner's PicoCalc, with a DS3231 on
`i2c0` at GP4/GP5. That clock takes UART1's pins, so a build option was
added, `PICO_COCO_UART_PORT`: 1, the default (D7), is UART1 on GP4/GP5;
0 is UART0 on GP2/GP3 through `UART_AUX` (hardware-notes.md §1.1). The
`-DPICO_COCO_UART_PORT=0` image (22,772 text, 860 bss), flashed over SWD
with `tools/flash.sh`, printed its banner and a heartbeat a second through
the Debug Probe: `build target : board=pico2 platform=rp2350`, `package :
QFN-80 (RP2350B), temperature on ADC input 8`, `clk_sys=150000000 Hz
clk_peri=150000000 Hz`, core rail ~1100 mV, die 18 °C, firmware
`ed8a9fa-dirty` (the uncommitted M0 tree). Capture in `out/m0/banner.log`,
not committed.

**CI** on GitHub, 2026-11-14, PR #1: green on both builds, the host
core and its tests (Ubuntu, GCC) and the `pico2` firmware with and without
the UART (Arm GNU 13.2.Rel1, Pico SDK 2.3.1).

**Not checked:**

- The default UART1 build's banner on a board: this board's GP4/GP5 carry
  the clock. Over SWD, that image's UART1 was enabled at 115,200 baud on
  GP4/GP5 with its FIFO draining, but no capture was made.
