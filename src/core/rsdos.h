/* rsdos.h — the RS-DOS controller cartridge (design.md §12.4).
 *
 * A WD1793 (wd1793.h), Disk BASIC's 8 KB ROM at $C000 and a latch, on
 * the cartridge port's SCS select, $FF40-$FF5F. As XRoar's rsdos.c
 * decodes them, and Disk BASIC Unravelled describes them (§18: medium,
 * until each is read off the ROM):
 *
 *   $FF48-$FF4B  the WD1793's registers, A3 set; $FF4C-$FF4F and
 *                $FF58-$FF5F repeat them
 *   $FF40 write  the latch, A3 clear, every such address:
 *       bits 0-2 drive select 0, 1, 2                      DK_DS0-2
 *       bit 3    the drives' motors                        DK_MOTOR
 *       bit 4    write precompensation (not modelled)
 *       bit 5    double density, and INTRQ onto NMI        DK_DDEN
 *       bit 6    drive select 3, or with another, side 1   DK_DS3
 *       bit 7    HALT enable                               DK_HALT
 *   reads of the latch's addresses: the bus as it was (open_bus)
 *
 * The first select bit set, in the order 0, 1, 2, 3, names the drive,
 * and none names drive 0, as XRoar has it. HALT is asserted while it is
 * enabled and DRQ is not: the CPU stands until the next byte is ready,
 * which is how Disk BASIC moves a sector, a byte per pass of a loop with
 * no test. INTRQ, a command's end, clears the enable, so the CPU runs on
 * into the NMI that INTRQ raises, which ends the loop. The NMI line goes
 * high as INTRQ rises with bit 5 set, or as the latch sets bit 5 with
 * INTRQ high, and low as INTRQ falls: XRoar's rule.
 *
 * The motor bit is kept and not acted on: the disk turns whether or not
 * it is set, as XRoar has it (§12.4, §18). It is heard, when the drives'
 * sounds are on, and so are the WD1793's step pulses (drivesnd.h).
 */
#ifndef PICO_COCO_RSDOS_H
#define PICO_COCO_RSDOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivesnd.h"
#include "wd1793.h"

struct coco_s;
struct cart_s;

#define DK_DS0    0x01u
#define DK_DS1    0x02u
#define DK_DS2    0x04u
#define DK_MOTOR  0x08u
#define DK_DDEN   0x20u
#define DK_DS3    0x40u
#define DK_HALT   0x80u

/* The cartridge's own state, in its cart_t (cart.h). */
typedef struct {
    wd1793_t fdc;
    uint8_t  latch;
    bool     halt_enable;  /* bit 7, until INTRQ                      */
    bool     intrq;        /* the chip's INTRQ as last seen           */
    bool     nmi;          /* the cartridge's NMI line                */
} rsdos_t;

/* Whether the cartridge answers this address of the I/O page. */
static inline bool rsdos_decodes(unsigned off) { return off - 0x40u < 0x20u; }

/* The cartridge's side of cart.h, for the cartridge `c` in machine `m`.
 * Each sets the lines `c` drives and puts the port's onto the CPU
 * (cart_lines). */

/* An access at tick now; `bus` is what an address nothing drives reads. */
uint8_t rsdos_read(struct coco_s *m, struct cart_s *c, uint16_t a, uint8_t bus, uint64_t now);
void    rsdos_write(struct coco_s *m, struct cart_s *c, uint16_t a, uint8_t v, uint64_t now);

/* The RESET line: the latch cleared, the chip's command dropped. */
void rsdos_reset(struct coco_s *m, struct cart_s *c);

/* The chip's events up to tick now, and the lines they move. */
void rsdos_run(struct coco_s *m, struct cart_s *c, uint64_t now);

/* NMI and HALT worked out again from the chip and the latch, after a
 * state is restored (cart_restored). */
void rsdos_lines(struct coco_s *m, struct cart_s *c);

/* The drives' motor heard as the controller's latch has it, after a
 * state is restored (coco_restored); off with no controller. */
void coco_disk_restored(struct coco_s *m);

/* ---- the port's -------------------------------------------------------- */

/* Each of these acts on the RS-DOS controller in the port (cart_rsdos),
 * and does nothing, or answers as an idle controller would, without one. */

/* The controller's WD1793, or NULL with none: the port reads and fills
 * its track buffer, and the tests look at its drives. */
wd1793_t       *coco_disk_fdc(struct coco_s *m);
const wd1793_t *coco_disk_fdc_c(const struct coco_s *m);

/* The track the controller is waiting on, or NULL; served at a field
 * boundary with the guest parked (§12.4). */
const wd_req_t *coco_disk_request(const struct coco_s *m);

/* buf has been put back and filled (wd_served). */
void coco_disk_served(struct coco_s *m, bool ok);

/* A disk in a drive, or out; the port puts a written track back first. */
void coco_disk_insert(struct coco_s *m, unsigned drive, bool protect);
void coco_disk_eject(struct coco_s *m, unsigned drive);

/* A drive's type (wd_drive_type); the port puts a written track back
 * first. */
void coco_disk_drive_type(struct coco_s *m, unsigned drive, wd_type_t type);

/* A command in progress, a track waiting to go, or a written track in
 * hand: a snapshot now would lose it (§12.5). */
bool coco_disk_busy(const struct coco_s *m);

/* A written track in hand asked to go back now (wd_flush), for the port
 * to serve before a power-on, a disk changed or a state saved. */
void coco_disk_flush(struct coco_s *m);

/* The drives' sounds, the steps and the motor, on or off (drivesnd.h).
 * Off by default, and after every coco_init. */
void coco_disk_sounds(struct coco_s *m, bool on);

/* `n` samples of the drives' sounds alone, for a park that waits on the
 * card for a track (park.c): the guest is stopped, but the drive goes on
 * turning, as a real one does while the CPU waits on its head. Guest
 * time does not pass, so no step falls due that was not due already;
 * the motor's level and the steps' rings go on, and the next field's
 * samples follow from where these leave them. The guest's own sound is
 * left out, as silence is what a park plays. Zeros with the sounds off.
 * Core 0, while core 1 serves the track, which never touches the PCM. */
size_t coco_disk_park_sound(struct coco_s *m, int16_t *dst, size_t n);

/* The drives' recorded sounds, or NULL for the synthesised ones
 * (drivesnd_set_samples); coco_init leaves them synthesised. */
void coco_disk_recordings(struct coco_s *m, const drivesnd_samples_t *rec);

/* The drive whose head is loaded, for the status line, or -1. */
int  coco_disk_active(const struct coco_s *m);

#endif /* PICO_COCO_RSDOS_H */
