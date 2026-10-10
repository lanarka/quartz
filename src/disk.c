// =============================================================================
// Quartz - The Computer
// -----------------------------------------------------------------------------
// disk.c - see disk.h. A raw disk image is opened for update ("r+b") so both
// reads and writes work against the same handle; nothing is ever buffered
// beyond stdio's own buffering, and every write is followed by an fflush()
// so the host-side file always reflects the last completed OP_BUS_OUTB
// DISK_CMD_WRITE even if the VM (or host) is killed right after.
// =============================================================================

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "disk.h"

int disk_open(DiskDrive *drive, const char *path) {
    memset(drive, 0, sizeof(*drive));
    if (!path || !path[0]) return 0;

    FILE *f = fopen(path, "r+b");
    if (!f) return 0;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (size <= 0 || (size % (long)DISK_SECTOR_SIZE) != 0) {
        fclose(f);
        return 0;
    }
    rewind(f);

    drive->f = f;
    drive->sector_count = (uint64_t)size / DISK_SECTOR_SIZE;
    return 1;
}

void disk_close(DiskDrive *drive) {
    if (drive->f) {
        fflush(drive->f);
        fclose(drive->f);
        drive->f = NULL;
    }
    drive->sector_count = 0;
}

// Shared bounds check for disk_read/disk_write: the requested [lba*SECTOR,
// lba*SECTOR + len) byte range must fit entirely inside the image.
static int disk_bounds_ok(const DiskDrive *drive, uint64_t lba, uint32_t len) {
    if (!drive->f) return 0;
    if (len == 0) return 1;
    uint64_t start = lba * (uint64_t)DISK_SECTOR_SIZE;
    uint64_t total = drive->sector_count * (uint64_t)DISK_SECTOR_SIZE;
    if (start >= total) return 0;
    if (start + (uint64_t)len > total) return 0;
    return 1;
}

int64_t disk_read(DiskDrive *drive, uint64_t lba, uint8_t *buf, uint32_t len) {
    if (!disk_bounds_ok(drive, lba, len)) return -1;
    if (len == 0) return 0;

    uint64_t offset = lba * (uint64_t)DISK_SECTOR_SIZE;
    if (fseeko(drive->f, (off_t)offset, SEEK_SET) != 0) return -1;

    size_t got = fread(buf, 1, len, drive->f);
    if (got != len) return -1;
    return (int64_t)got;
}

int64_t disk_write(DiskDrive *drive, uint64_t lba, const uint8_t *buf, uint32_t len) {
    if (!disk_bounds_ok(drive, lba, len)) return -1;
    if (len == 0) return 0;

    uint64_t offset = lba * (uint64_t)DISK_SECTOR_SIZE;
    if (fseeko(drive->f, (off_t)offset, SEEK_SET) != 0) return -1;

    size_t put = fwrite(buf, 1, len, drive->f);
    fflush(drive->f);
    if (put != len) return -1;
    return (int64_t)put;
}
