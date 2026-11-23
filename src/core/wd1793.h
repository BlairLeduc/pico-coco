/* wd1793.h — the Western Digital 1793 floppy disk controller the RS-DOS
 * cartridge carries (design.md §12.4), from pico-oric's, whose design is
 * kept whole (EL §8.4).
 *
 * The chip as Disk BASIC drives it: command and status at $FF48, track
 * $FF49, sector $FF4A, data $FF4B (rsdos.h); INTRQ ends every command
 * and DRQ asks for each byte. The cartridge clocks it at 1 MHz (an 8 MHz
 * crystal divided by 8), as the Oric's Microdisc does, so every time in
 * the datasheet's 2 MHz column is doubled: steps of 6, 12, 20 or 30 ms,
 * a 30 ms head settle, as XRoar has them (§18). All four types of
 * command are modelled, and the data-mark, record-not-found and
 * lost-data rules; CRCs are not checked (below).
 *
 * The disk turns, in master-oscillator ticks (§13.2). A track is
 * COCO_DISK_TRACK_LEN bytes (mfmdisk.h), a byte every WD_BYTE_TICKS, 32
 * us, so a revolution is 2,862,500 ticks, 300 rpm; byte 0 is the index
 * hole. The byte under the head at tick t is byte (t / 458) mod 6250 of
 * the track, for every drive alike. A search waits for its ID to come
 * round, a transfer moves a byte every 32 us and loses one the CPU has
 * not taken in time, and a sector that is not there is given up after
 * five index pulses: nothing is sped up.
 *
 * A sector's INTRQ comes as its CRC passes the head; a read address's
 * and a read track's, whose last byte is their last, a byte's time
 * after it, so that a CPU ending its loop on INTRQ has taken that byte.
 * The datasheet gives no figure.
 *
 * Every timed step is an event at an exact tick: `due` is the next, and
 * the run loop ends its slice there (coco.c). Register accesses bring
 * the chip to their own tick first. Events run at their own times, not
 * the boundary's, so a byte is never late because an instruction ran
 * past it.
 *
 * The chip holds no disk. It knows each drive's type and holds one raw
 * track, the one it last needed. A command that needs another posts a
 * request and waits, busy, as a drive does while its head settles; the
 * port serves it at the next field boundary with the guest parked
 * (EL §8.4), building the track from the image's sectors, or an
 * unformatted one where the image has none, and the search starts from
 * the tick it is served at. A track the chip has written to is posted
 * back before any other track is read into its place, when the head
 * unloads (15 revolutions after the last command), or when the port asks
 * (wd_flush), and the port takes its sectors back into the image
 * (jvc.h). Not after each command: Disk BASIC writes a sector at a time,
 * and a park for every one made BACKUP crawl on the board (§12.4).
 *
 * Each drive has a type, its sides and tracks, apart from the disk in
 * it, and is modelled as the mechanism is. The head stops at the drive's
 * last track, and steps past it are lost; the track register counts them
 * all the same, so a command there finds the last track's IDs. A
 * one-sided drive has no side select: either side of the latch is the
 * disk's first. Nothing is refused, as a real drive refuses nothing.
 *
 * Not modelled: CRC errors, which the images cannot carry; density, as
 * RS-DOS's disks are all MFM; READY, which the cartridge ties active, so
 * a drive with no disk in it has no index pulses, and a command on it
 * runs until a Force Interrupt, as a real one does (§12.4).
 */
#ifndef PICO_COCO_WD1793_H
#define PICO_COCO_WD1793_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "mfmdisk.h"

/* Status bits. Type I commands show the drive's own; types II and III
 * show the transfer's. */
#define WD_ST_NOT_READY  0x80u
#define WD_ST_PROTECT    0x40u
#define WD_ST_HEAD       0x20u   /* type I: head loaded                 */
#define WD_ST_RECTYPE    0x20u   /* read sector: a deleted data mark     */
#define WD_ST_WRITE_FAULT 0x20u  /* write: unused here                   */
#define WD_ST_SEEK_ERR   0x10u   /* type I                               */
#define WD_ST_RNF        0x10u   /* types II, III                        */
#define WD_ST_CRC        0x08u
#define WD_ST_TRACK0     0x04u   /* type I                               */
#define WD_ST_LOST       0x04u   /* types II, III                        */
#define WD_ST_INDEX      0x02u   /* type I                               */
#define WD_ST_DRQ        0x02u   /* types II, III                        */
#define WD_ST_BUSY       0x01u

/* A byte's time: 14,318,180 / 31,250 ticks, rounded, as XRoar has it. */
#define WD_BYTE_TICKS    458u
#define WD_REV_TICKS     ((uint64_t)COCO_DISK_TRACK_LEN * WD_BYTE_TICKS)
#define WD_NEVER         UINT64_MAX

/* A drive's mechanism: its sides, 1 or 2, and tracks, 1 to
 * COCO_DISK_TRACKS_MAX. */
typedef struct {
    uint8_t sides, tracks;
} wd_type_t;

typedef struct {
    bool       loaded;
    bool       protect;
    wd_type_t  type;
    uint8_t    cyl;        /* the head, physically               */
} wd_drive_t;

/* What the chip is waiting on the port for. Either or both: a written
 * track goes back first, then the wanted one comes in. */
typedef struct {
    bool    put;            /* buf back to put_drive/side/cyl      */
    uint8_t put_drive, put_side, put_cyl;
    bool    get;            /* then drive/side/cyl into buf        */
    uint8_t drive, side, cyl;
} wd_req_t;

typedef struct {
    /* The registers, INTRQ and DRQ. */
    uint8_t status, track, sector, data, cmd;
    bool    intrq, drq;
    bool    type1;          /* the last command was type I: live status */
    bool    hold_intrq;     /* Force Interrupt's immediate: until the next */
    bool    index_intrq;    /* Force Interrupt's: INTRQ at every index */
    uint64_t index_due;     /* its next, or WD_NEVER                */
    bool    step_in;        /* the last step's direction           */
    uint64_t head_until;    /* the head unloads at this tick       */

    /* The drive and side the latch selects (rsdos.c). */
    uint8_t drive, side;
    wd_drive_t drv[COCO_DISK_DRIVES];

    /* The command in progress. */
    uint8_t  phase;
    uint64_t due;           /* the next event, or WD_NEVER          */
    uint64_t from;          /* a search starts at this tick         */
    uint16_t pos;           /* the next byte of a transfer          */
    uint16_t len;           /* its length                           */
    uint16_t at;            /* where it starts in buf               */
    uint64_t k0;            /* the byte under the head when it began */
    uint16_t crc;           /* write track's generator              */
    bool     crc_low;       /* write track: the CRC's second byte next */

    /* The track in hand, which sectors it holds, and the request. */
    uint8_t  buf[COCO_DISK_TRACK_LEN];
    bool     buf_ok, buf_void, buf_dirty;
    uint8_t  buf_drive, buf_side, buf_cyl;
    mfm_sector_t sec[COCO_DISK_SECTORS_MAX];
    uint8_t  nsec;
    wd_req_t req;

    /* The last type I command's step pulses, for the drives' sound
     * (drivesnd.h): pulse_n of them, the first at pulse_from, a step's
     * time apart. Set by the command, taken by the cartridge (rsdos.c),
     * which zeroes pulse_n; not state. */
    uint64_t pulse_from;
    uint32_t pulse_period;
    uint16_t pulse_n;

    /* For the heartbeat: sectors moved, and tracks formatted. */
    uint32_t sectors_read, sectors_written, tracks_written;
} wd1793_t;

/* Power on: no disks, heads at track 0, nothing in progress, and every
 * drive the default type (COCO_DISK_DRIVE_SIDES, _TRACKS). */
void wd_init(wd1793_t *f);

/* The MR pin, on the CoCo's RESET line: the command in progress is
 * dropped. Disks, heads and the track in hand stay. */
void wd_reset(wd1793_t *f);

uint8_t wd_read(wd1793_t *f, unsigned reg, uint64_t now);
void    wd_write(wd1793_t *f, unsigned reg, uint8_t v, uint64_t now);

/* The events due at or before now, each at its own tick. */
void wd_run(wd1793_t *f, uint64_t now);

/* The latch's drive and side, from now. */
void wd_select(wd1793_t *f, unsigned drive, unsigned side, uint64_t now);

/* The request the chip is waiting on, or NULL. */
static inline const wd_req_t *wd_request(const wd1793_t *f) {
    return (f->req.put || f->req.get) ? &f->req : NULL;
}

/* The port has served the request at tick now: put back, and filled buf,
 * with an unformatted track where the image has none. ok false if the
 * card failed it: a failed read is an unformatted track whose writes are
 * not kept, a failed write is lost. */
void wd_served(wd1793_t *f, bool ok, uint64_t now);

/* A disk in a drive, or none. The port puts back a written track first
 * (wd_request); the track in hand is dropped if it was that drive's. */
void wd_insert(wd1793_t *f, unsigned drive, bool protect, uint64_t now);
void wd_eject(wd1793_t *f, unsigned drive, uint64_t now);

/* The drive's type. A head beyond its last track is put there. As for
 * wd_insert, the port puts back a written track first. */
void wd_drive_type(wd1793_t *f, unsigned drive, wd_type_t type, uint64_t now);

/* After a state's registers are put back (snapshot.c): no command, and
 * Force Interrupt's index interrupt, if set, at the next index from now.
 * The track the saved chip had in hand, if `had`, is asked for again, for
 * the port to serve before the guest runs; otherwise none is in hand. */
void wd_restored(wd1793_t *f, uint64_t now, bool had, unsigned drive, unsigned side, unsigned cyl);

/* A written track in hand asked to go back now, for the port, before
 * anything that would lose it: a disk changed, a power-on, a state saved
 * (dskio.h). */
void wd_flush(wd1793_t *f);

/* Busy on a command: a snapshot now would lose it. */
static inline bool wd_busy(const wd1793_t *f) { return (f->status & WD_ST_BUSY) != 0; }

/* The head is loaded: a command runs, or one ended within the unload
 * delay. The status line shows the drive while it is (status.h). */
static inline bool wd_head_loaded(const wd1793_t *f, uint64_t now) {
    return wd_busy(f) || now < f->head_until;
}

#endif /* PICO_COCO_WD1793_H */
