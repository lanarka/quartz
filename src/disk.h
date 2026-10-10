// =============================================================================
// Quartz - The Computer
// -----------------------------------------------------------------------------
// disk.h - raw, sector-addressed virtual disk (replaces the old SVFS
// filesystem entirely: there is no directory structure, no files, no
// formatting step here - a "disk image" is just a flat, seekable blob of
// bytes, and the only unit of I/O is a fixed-size sector).
//
// A Quartz program is free to build any filesystem it likes on top of this
// (or none at all) - that is now firmly software running on the VM, not
// something the VM or its host ships. See src/qdisk.c for the tiny CLI tool
// that creates a blank, zero-filled image of a given number of sectors.
// =============================================================================

#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include <stdio.h>

#define DISK_SECTOR_SIZE 512u   // bytes per sector - fixed, like a classic hard disk/floppy

typedef struct {
    FILE     *f;
    uint64_t  sector_count;    // total sectors in the attached image
} DiskDrive;

// Opens an existing raw disk image at `path`. The file's size must be a
// non-zero multiple of DISK_SECTOR_SIZE; sector_count is derived from it.
// Returns 1 on success (drive->f/sector_count populated), 0 on failure
// (missing file, empty file, or size not a whole number of sectors).
int disk_open(DiskDrive *drive, const char *path);

// Flushes and closes the image. Safe to call on an already-closed/never-
// opened DiskDrive.
void disk_close(DiskDrive *drive);

// Reads `len` bytes starting at sector `lba` into `buf`. `len` does not need
// to be a sector multiple, but the read must not run past the end of the
// image. Returns the number of bytes read (== len) on success, or -1 on
// failure (drive not open, LBA/len out of range, short host read).
int64_t disk_read(DiskDrive *drive, uint64_t lba, uint8_t *buf, uint32_t len);

// Writes `len` bytes from `buf` to the image starting at sector `lba`, then
// flushes. Same bounds rules as disk_read(). Returns the number of bytes
// written (== len) on success, or -1 on failure.
int64_t disk_write(DiskDrive *drive, uint64_t lba, const uint8_t *buf, uint32_t len);

#endif
