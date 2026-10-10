// =============================================================================
// Quartz - The Computer
// -----------------------------------------------------------------------------
// bus.h - the virtual BUS: the one uniform interface every peripheral is
// reached through (video, video-text, keyboard, mouse, audio, up to two
// disks, network, a real-time clock).
//
// Quartz used to give every peripheral its own private opcodes (OP_GFX_*,
// OP_TXT_*, OP_IN_*, OP_SVFS_*, OP_NET_*, OP_SOUND). That's gone: a program
// now talks to ALL of these through exactly six opcodes (OP_BUS_RESET/
// OP_BUS_STATUS/OP_BUS_IN/OP_BUS_OUT/OP_BUS_INB/OP_BUS_OUTB, see vm.h) plus a
// (device, port) address, the same way a real machine's peripherals sit
// behind a handful of bus instructions instead of one CPU opcode per device.
//
//   OP_BUS_RESET(dev)                    -> reinitialize a device
//   OP_BUS_STATUS(dev, dst)              -> dst = 1 if dev is present/ready
//   OP_BUS_IN (dev, port, dst)           -> dst = read register `port`
//   OP_BUS_OUT(dev, port, val)           -> write `val` to register `port`
//   OP_BUS_INB (dev, port, addr, len, dst)  -> bulk transfer device -> RAM
//   OP_BUS_OUTB(dev, port, addr, len, dst)  -> bulk transfer RAM -> device
//
// IN/OUT move a single value through a device "register" (a control/status
// port, e.g. screen width, or a staged X/Y/color waiting for a command).
// INB/OUTB move a block of bytes between VM RAM and the device (a pixel
// rect, a text-cell block, a disk sector run, a network packet, a sound
// clip) - `dst` reports how many bytes/items were actually transferred, or
// a negative/zero value on failure (see each dispatcher in bus.c).
//
// Several devices (VIDEO, VIDEO_TEXT) work like a tiny DMA controller:
// OUT first stages parameters (X/Y/W/H/OFFSET/...) into per-VM registers
// (the `Bus` struct below), then a command port (VIDEO_CMD_FRAME, ...) or
// the OUTB call itself performs the actual transfer using those staged
// values. This mirrors how real bus-attached hardware is programmed
// (write control registers, then trigger), and is why `Bus` has to be part
// of per-VM state rather than bus.c global state - two VM instances must
// never share staged registers.
// =============================================================================

#ifndef BUS_H
#define BUS_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Device IDs (the `dev` operand of every OP_BUS_* instruction)
// ---------------------------------------------------------------------------
enum {
    BUS_DEV_VIDEO = 0,      // pixel framebuffer
    BUS_DEV_VIDEO_TEXT,     // text-mode terminal grid
    BUS_DEV_KEYBOARD,
    BUS_DEV_MOUSE,
    BUS_DEV_AUDIO,
    BUS_DEV_DISK0,          // first raw disk drive (0..2 disks may be attached)
    BUS_DEV_DISK1,          // second raw disk drive
    BUS_DEV_NETWORK,
    BUS_DEV_RTC,            // real-time clock (wall-clock date/time, read-only)
    BUS_DEV_COUNT
};

// Number of independent disk drives the machine has slots for. Each is its
// own BUS device (BUS_DEV_DISK0/BUS_DEV_DISK1) with its own staging
// registers - there is no shared "select a disk" register, the same way two
// physical drives would sit at two different bus addresses.
#define DISK_DRIVE_COUNT 2

// ---------------------------------------------------------------------------
// BUS_DEV_VIDEO ports
// ---------------------------------------------------------------------------
enum {
    VIDEO_PORT_WIDTH = 0,   // IN,  ro - current screen width in pixels
    VIDEO_PORT_HEIGHT,      // IN,  ro - current screen height in pixels
    VIDEO_PORT_X,           // OUT, staging - target rect / pixel X
    VIDEO_PORT_Y,           // OUT, staging - target rect / pixel Y
    VIDEO_PORT_W,           // OUT, staging - target rect width  (OP_BUS_OUTB VIDEO_CMD_FRAME)
    VIDEO_PORT_H,           // OUT, staging - target rect height (OP_BUS_OUTB VIDEO_CMD_FRAME)
    VIDEO_PORT_OFFSET,      // OUT, staging - byte offset into the framebuffer (VIDEO_CMD_RAW)
    VIDEO_CMD_PIX,          // OUT, val=0xAABBGGRR - plot one pixel at (X,Y)
    VIDEO_CMD_CLEAR,        // OUT, val ignored     - clear screen to black
    VIDEO_CMD_CLEARC,       // OUT, val=0xAABBGGRR  - clear screen to color
    VIDEO_CMD_VSYNC,        // OUT, val ignored     - yield until next host frame
    VIDEO_CMD_FRAME,        // OUTB - blit RGBA8 rect (W*H*4 bytes) at (X,Y)
    VIDEO_CMD_RAW           // OUTB - raw framebuffer write at byte OFFSET
};

// ---------------------------------------------------------------------------
// BUS_DEV_VIDEO_TEXT ports
// ---------------------------------------------------------------------------
enum {
    VTXT_PORT_COLS = 0,     // IN,  ro - terminal width in columns
    VTXT_PORT_ROWS,         // IN,  ro - terminal height in rows
    VTXT_PORT_X,            // OUT, staging - cell column
    VTXT_PORT_Y,            // OUT, staging - cell row
    VTXT_PORT_W,            // OUT, staging - block width  (VTXT_CMD_FRAME)
    VTXT_PORT_H,            // OUT, staging - block height (VTXT_CMD_FRAME)
    VTXT_PORT_CODEPOINT,    // OUT, staging - Unicode codepoint (VTXT_CMD_CHAR)
    VTXT_PORT_FG,           // OUT, staging - 0xAABBGGRR foreground (VTXT_CMD_CHAR)
    VTXT_PORT_BG,           // OUT, staging - 0xAABBGGRR background (VTXT_CMD_CHAR)
    VTXT_PORT_CURSOR_X,     // IN,  ro - current cursor column
    VTXT_PORT_CURSOR_Y,     // IN,  ro - current cursor row
    VTXT_CMD_CHAR,          // OUT - write one cell at (X,Y) using CODEPOINT/FG/BG
    VTXT_CMD_CLEAR,         // OUT - clear the terminal
    VTXT_CMD_CURSET,        // OUT - move the cursor to (X,Y)
    VTXT_CMD_CURENA,        // OUT, val=0/1/2 - cursor style (0=off,1=block,2=bar)
    VTXT_CMD_FRAME          // OUTB - blit a W*H block of cells (10 bytes/cell) at (X,Y)
};

// ---------------------------------------------------------------------------
// BUS_DEV_KEYBOARD ports
// ---------------------------------------------------------------------------
enum {
    KBD_PORT_POLL = 0       // INB, len=8 - up to 4 currently-held keycodes (u16 LE each);
                            // dst = number of keys actually written (0..4)
};

// ---------------------------------------------------------------------------
// BUS_DEV_MOUSE ports
// ---------------------------------------------------------------------------
enum {
    MOUSE_PORT_MODE = 0,    // OUT, val=0 (pixel coords) or 1 (text-cell coords)
    MOUSE_PORT_X,           // IN - X / column, in whatever unit MODE selects
    MOUSE_PORT_Y,           // IN - Y / row,    in whatever unit MODE selects
    MOUSE_PORT_BUTTONS      // IN - button bitmask (bit0=left, bit1=right, bit2=middle)
};

// ---------------------------------------------------------------------------
// BUS_DEV_AUDIO ports
// ---------------------------------------------------------------------------
enum {
    AUDIO_PORT_PLAY = 0     // OUTB - play a WAV clip straight from RAM[addr..addr+len)
};

// ---------------------------------------------------------------------------
// BUS_DEV_DISK0 / BUS_DEV_DISK1 ports - raw, sector-addressed block devices
// (no filesystem). Both drives share the exact same port layout below; which
// physical drive you're talking to is purely a matter of which `dev` you
// pass (BUS_DEV_DISK0 or BUS_DEV_DISK1), same as SATA port 0 vs. port 1.
// A machine may have 0, 1, or 2 disks attached (see quartz --disk/--disk1);
// an unattached drive just reports DISK_PORT_SECTOR_COUNT == 0 and fails
// every read/write.
// ---------------------------------------------------------------------------
enum {
    DISK_PORT_LBA = 0,      // OUT - stage the target sector number (0-based) for the
                            // next DISK_CMD_READ/DISK_CMD_WRITE
    DISK_PORT_SECTOR_SIZE,  // IN  - bytes per sector (0 if no disk attached)
    DISK_PORT_SECTOR_COUNT, // IN  - total sectors on the attached image (0 if none)
    DISK_PORT_STATUS,       // IN  - status of the last read/write: 0=idle/never run,
                            // 1=ok, 2=error (out of range / short file / not attached)
    DISK_CMD_READ,          // INB  - read `len` bytes starting at LBA into RAM[addr..)
    DISK_CMD_WRITE          // OUTB - write `len` bytes from RAM[addr..) starting at LBA
};

// ---------------------------------------------------------------------------
// BUS_DEV_NETWORK ports
// ---------------------------------------------------------------------------
enum {
    NET_PORT_MAXPACKET = 0, // IN - largest packet the attached card will carry (0 if none)
    NET_PORT_POLL,          // IN - 1 if a packet is waiting to be received, else 0
    NET_PORT_STATUS,        // IN - status of the last send/recv: 0=idle, 1=ok,
                            // 2=packet too large, 3=disconnected/error
    NET_CMD_SEND,           // OUTB - send RAM[addr..addr+len) as one packet
    NET_CMD_RECV            // INB  - receive one packet into RAM[addr..addr+len)
};

// ---------------------------------------------------------------------------
// BUS_DEV_RTC ports - real-time clock. Read-only: the host's wall-clock date
// and time, broken out field by field so a program can read exactly the
// part it needs (just the day, just the minute, ...) with one OP_BUS_IN
// each, instead of decoding a packed timestamp. Always present - see
// bus_status() in bus.c - since the host always has a clock.
// ---------------------------------------------------------------------------
enum {
    RTC_PORT_YEAR = 0,      // IN - full year, e.g. 2026
    RTC_PORT_MONTH,         // IN - 1..12
    RTC_PORT_DAY,           // IN - 1..31 (day of the month)
    RTC_PORT_HOUR,          // IN - 0..23
    RTC_PORT_MINUTE,        // IN - 0..59
    RTC_PORT_SECOND,        // IN - 0..59
    RTC_PORT_WEEKDAY,       // IN - 0..6, 0 = Sunday
    RTC_PORT_MILLISECOND,   // IN - 0..999, sub-second part of SECOND
    RTC_PORT_EPOCH          // IN - whole-second Unix timestamp, for programs that
                            // want everything at once instead of field by field
};

// ---------------------------------------------------------------------------
// Per-VM staging registers. Devices that behave like little DMA controllers
// (VIDEO, VIDEO_TEXT) keep their pending parameters here between the OUT
// calls that set them up and the command/OUTB call that consumes them.
// Embedded directly in struct VM (see vm.h) - never shared between VMs.
// ---------------------------------------------------------------------------
typedef struct {
    // BUS_DEV_VIDEO staging
    uint16_t video_x, video_y, video_w, video_h;
    uint32_t video_offset;

    // BUS_DEV_VIDEO_TEXT staging
    uint16_t text_x, text_y, text_w, text_h;
    uint16_t text_codepoint;
    uint32_t text_fg, text_bg;

    // BUS_DEV_MOUSE
    uint8_t mouse_text_mode;   // 0 = pixel coords, 1 = text-cell coords

    // BUS_DEV_DISK0 / BUS_DEV_DISK1 - indexed [0] for DISK0, [1] for DISK1
    uint64_t disk_lba[DISK_DRIVE_COUNT];
    int      disk_status[DISK_DRIVE_COUNT]; // last DISK_CMD_READ/WRITE result per drive

    // BUS_DEV_NETWORK
    int      net_status;       // last NET_CMD_SEND/RECV result (see NET_PORT_STATUS)
} Bus;

typedef struct VM VM;  // full definition in vm.h; bus.c only ever sees it through here.

// Dispatch entry points, one per OP_BUS_* opcode - see vm.c for the operand
// fetch/register-file glue that calls these. `ok` (where present) is set to
// 0 if `dev`/`port` names something the device doesn't support, so vm.c can
// decide how to report that back to the program (see each opcode's comment
// in vm.h/bus.h for the exact failure convention).
void    bus_reset(VM *vm, int dev);
int     bus_status(VM *vm, int dev);
int64_t bus_in (VM *vm, int dev, int port, int *ok);
int     bus_out(VM *vm, int dev, int port, int64_t val);
int64_t bus_inb (VM *vm, int dev, int port, uint32_t addr, uint32_t len, int *ok);
int64_t bus_outb(VM *vm, int dev, int port, uint32_t addr, uint32_t len, int *ok);

#endif
