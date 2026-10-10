# The Quartz BUS (`OP_BUS_*`)

Quartz used to give every peripheral its own private opcodes
(`OP_GFX_*`, `OP_TXT_*`, `OP_IN_*`, `OP_SVFS_*`, `OP_NET_*`, `OP_SOUND`).
Those are gone. A program now talks to **every** peripheral — the pixel
framebuffer, the text terminal, the keyboard, the mouse, the audio
device, up to two raw disks, the network card, and a real-time clock —
through exactly **six**
opcodes plus a `(device, port)` address, the same way a real computer's
peripherals sit behind a handful of bus instructions instead of getting
one CPU opcode each.

`OP_VIDEO`, `OP_VIDEO_INFO` and `OP_BEEP` are the only "device-flavored"
opcodes that stayed outside the bus — they configure/ping the base
machine itself (display mode, system bell) rather than a specific
attached peripheral.

Implementation: [`src/bus.h`](src/bus.h) / [`src/bus.c`](src/bus.c) (the
dispatcher) and the six `case OP_BUS_*` handlers in [`src/vm.c`](src/vm.c).

---

## 1. The six instructions

| Opcode          | Value | Operands                          | Effect |
|-----------------|:-----:|------------------------------------|--------|
| `OP_BUS_RESET`  | 59    | `dev`                              | Reinitialize a device to a known state. |
| `OP_BUS_STATUS` | 60    | `dev, dst`                         | `dst = 1` if `dev` is present/ready, else `0`. |
| `OP_BUS_IN`     | 61    | `dev, port, dst`                   | `dst` = read register `port` of device `dev`. |
| `OP_BUS_OUT`    | 62    | `dev, port, val`                   | Write `val` to register `port` of device `dev`. |
| `OP_BUS_INB`    | 63    | `dev, port, addr, len, dst`        | Bulk transfer **device → RAM**: read up to `len` bytes from `dev`/`port` into `RAM[addr..addr+len)`. `dst` = bytes/items actually transferred, or `-1` on failure. |
| `OP_BUS_OUTB`   | 64    | `dev, port, addr, len, dst`        | Bulk transfer **RAM → device**: write `RAM[addr..addr+len)` to `dev`/`port`. `dst` = bytes/items actually transferred, or `-1` on failure. |

Every operand above is a **register index** (one byte, `0..15`, see
`NUM_REGS`) — there are no immediate-operand variants. Encoding on the
wire is simply `opcode` followed by one byte per operand, in the order
listed:

| Opcode          | Byte layout                                      | Size |
|-----------------|---------------------------------------------------|:----:|
| `OP_BUS_RESET`  | `[op][dev]`                                        | 2 B |
| `OP_BUS_STATUS` | `[op][dev][dst]`                                   | 3 B |
| `OP_BUS_IN`     | `[op][dev][port][dst]`                             | 4 B |
| `OP_BUS_OUT`    | `[op][dev][port][val]`                             | 4 B |
| `OP_BUS_INB`    | `[op][dev][port][addr][len][dst]`                  | 6 B |
| `OP_BUS_OUTB`   | `[op][dev][port][addr][len][dst]`                  | 6 B |

`dev` and `port` are read out of whatever register you loaded them
into (typically with `OP_MOVI`) — they are plain integers, not opcode
operands with their own encoding.

### IN/OUT vs. INB/OUTB

- **`IN`/`OUT`** move a single 64-bit value through a device
  *register* — a control/status port (e.g. screen width) or a staged
  parameter (e.g. a pending pixel's X coordinate) waiting for a
  command.
- **`INB`/`OUTB`** move a *block* of bytes between VM RAM and the
  device — a pixel rect, a run of disk sectors, a network packet, a
  sound clip. `dst` reports how many bytes/items were actually moved.

### Failure conventions

- `OP_BUS_IN`: unknown `(dev, port)` → `dst = 0`.
- `OP_BUS_OUT`: unknown `(dev, port)` is silently ignored (no result
  register to report into).
- `OP_BUS_STATUS`: unknown `dev` → `dst = 0` (same as "not present").
- `OP_BUS_INB` / `OP_BUS_OUTB`: unknown `(dev, port)`, or a transfer
  the device rejected (e.g. wrong length) → `dst = -1`. **Before**
  that check even runs, the VM faults immediately (execution stops)
  if `[addr, addr+len)` doesn't fit inside RAM — a bad buffer is
  treated as a programming error, not a recoverable device error.
- `OP_BUS_RESET`: never fails; an unknown/no-op device just does
  nothing.

### The DMA-controller pattern (`VIDEO`, `VIDEO_TEXT`)

Some devices behave like a tiny DMA controller: you can't fit a
pixel-rect blit's parameters (`x, y, w, h`, plus a RAM pointer and
length) into one instruction, so instead you **stage** the extra
parameters into per-device registers with `OUT`, then trigger the
actual transfer with a command port (via `OUT` for a zero-payload
command, or via `OUTB`/`INB` for one that also moves a data block).
This mirrors how real bus-attached hardware is programmed — write
control registers, then trigger — and is exactly how `BUS_DEV_VIDEO`'s
`VIDEO_CMD_FRAME` and `BUS_DEV_DISK0`'s `DISK_PORT_LBA` staging work
below.

Staged registers are **per-VM** state (the `Bus` struct embedded in
`struct VM`), never shared between VM instances or reset by anything
other than `OP_BUS_RESET` on that device.

---

## 2. Devices

| Device               | ID  | What it is |
|----------------------|:---:|------------|
| `BUS_DEV_VIDEO`       | 0   | The pixel framebuffer. |
| `BUS_DEV_VIDEO_TEXT`  | 1   | The text-mode terminal grid. |
| `BUS_DEV_KEYBOARD`    | 2   | Keyboard input. |
| `BUS_DEV_MOUSE`       | 3   | Mouse input. |
| `BUS_DEV_AUDIO`       | 4   | Audio clip playback. |
| `BUS_DEV_DISK0`       | 5   | First raw, sector-addressed disk drive (no filesystem — see [`src/disk.h`](src/disk.h)). |
| `BUS_DEV_DISK1`       | 6   | Second raw disk drive — a machine may have 0, 1, or 2 disks attached. |
| `BUS_DEV_NETWORK`     | 7   | The network card (`src/vmnet.h`). |
| `BUS_DEV_RTC`         | 8   | Real-time clock — the host's current wall-clock date/time, read-only. |

`BUS_DEV_VIDEO`, `BUS_DEV_VIDEO_TEXT`, `BUS_DEV_KEYBOARD`,
`BUS_DEV_MOUSE`, `BUS_DEV_AUDIO` and `BUS_DEV_RTC` are always present
(`OP_BUS_STATUS` on them always returns `1`) — they're part of the
base machine. `BUS_DEV_DISK0`, `BUS_DEV_DISK1` and `BUS_DEV_NETWORK` are
optional and report `0` until something is attached (`--disk`/`--disk1`/
`--net` on the `quartz` command line, or the equivalent host API).

---

## 3. Port maps

### 3.1 `BUS_DEV_VIDEO` — pixel framebuffer

| Port               | ID | Access | Meaning |
|--------------------|:--:|:------:|---------|
| `VIDEO_PORT_WIDTH`  | 0  | IN, ro | Current screen width in pixels. |
| `VIDEO_PORT_HEIGHT` | 1  | IN, ro | Current screen height in pixels. |
| `VIDEO_PORT_X`      | 2  | OUT, staging | Target rect / pixel X. |
| `VIDEO_PORT_Y`      | 3  | OUT, staging | Target rect / pixel Y. |
| `VIDEO_PORT_W`      | 4  | OUT, staging | Target rect width (`VIDEO_CMD_FRAME`). |
| `VIDEO_PORT_H`      | 5  | OUT, staging | Target rect height (`VIDEO_CMD_FRAME`). |
| `VIDEO_PORT_OFFSET` | 6  | OUT, staging | Byte offset into the framebuffer (`VIDEO_CMD_RAW`). |
| `VIDEO_CMD_PIX`     | 7  | OUT, `val=0xAABBGGRR` | Plot one pixel at the staged `(X, Y)`. |
| `VIDEO_CMD_CLEAR`   | 8  | OUT, `val` ignored | Clear the screen to black. |
| `VIDEO_CMD_CLEARC`  | 9  | OUT, `val=0xAABBGGRR` | Clear the screen to a color. |
| `VIDEO_CMD_VSYNC`   | 10 | OUT, `val` ignored | Yield until the next host frame. |
| `VIDEO_CMD_FRAME`   | 11 | OUTB | Blit an RGBA8 rect (`W*H*4` bytes) at the staged `(X, Y)`. `len` **must** equal `W*H*4` or the call fails. |
| `VIDEO_CMD_RAW`     | 12 | OUTB | Raw framebuffer write of `len` bytes at the staged byte `OFFSET`. Fails if `OFFSET + len` runs past the framebuffer. |

Color format everywhere on this device is packed little-endian as
`val = r | (g << 8) | (b << 16) | (a << 24)`.

### 3.2 `BUS_DEV_VIDEO_TEXT` — text terminal

| Port                  | ID | Access | Meaning |
|-----------------------|:--:|:------:|---------|
| `VTXT_PORT_COLS`       | 0  | IN, ro | Terminal width in columns. |
| `VTXT_PORT_ROWS`       | 1  | IN, ro | Terminal height in rows. |
| `VTXT_PORT_X`          | 2  | OUT, staging | Cell column. |
| `VTXT_PORT_Y`          | 3  | OUT, staging | Cell row. |
| `VTXT_PORT_W`          | 4  | OUT, staging | Block width (`VTXT_CMD_FRAME`). |
| `VTXT_PORT_H`          | 5  | OUT, staging | Block height (`VTXT_CMD_FRAME`). |
| `VTXT_PORT_CODEPOINT`  | 6  | OUT, staging | Unicode codepoint (`VTXT_CMD_CHAR`). |
| `VTXT_PORT_FG`         | 7  | OUT, staging | Foreground color, `0xAABBGGRR` (`VTXT_CMD_CHAR`). |
| `VTXT_PORT_BG`         | 8  | OUT, staging | Background color, `0xAABBGGRR` (`VTXT_CMD_CHAR`). |
| `VTXT_PORT_CURSOR_X`   | 9  | IN, ro | Current cursor column. |
| `VTXT_PORT_CURSOR_Y`   | 10 | IN, ro | Current cursor row. |
| `VTXT_CMD_CHAR`        | 11 | OUT | Write one cell at the staged `(X, Y)` using `CODEPOINT`/`FG`/`BG`. |
| `VTXT_CMD_CLEAR`       | 12 | OUT | Clear the terminal. |
| `VTXT_CMD_CURSET`      | 13 | OUT | Move the cursor to the staged `(X, Y)`. |
| `VTXT_CMD_CURENA`      | 14 | OUT, `val=0/1/2` | Cursor style: `0`=off, `1`=block, `2`=bar. |
| `VTXT_CMD_FRAME`       | 15 | OUTB | Blit a `W*H` block of cells (10 bytes/cell) at the staged `(X, Y)`. `len` **must** equal `W*H*10`. |

### 3.3 `BUS_DEV_KEYBOARD`

| Port           | ID | Access | Meaning |
|----------------|:--:|:------:|---------|
| `KBD_PORT_POLL` | 0  | INB, `len=8` | Up to 4 currently-held keycodes (`u16` little-endian each). `dst` = number of keys actually written, `0..4`. |

### 3.4 `BUS_DEV_MOUSE`

| Port                 | ID | Access | Meaning |
|----------------------|:--:|:------:|---------|
| `MOUSE_PORT_MODE`     | 0  | OUT | `0` = pixel coordinates, `1` = text-cell coordinates. Set once before reading X/Y. |
| `MOUSE_PORT_X`        | 1  | IN  | X / column, in whichever unit `MODE` selects. |
| `MOUSE_PORT_Y`        | 2  | IN  | Y / row, in whichever unit `MODE` selects. |
| `MOUSE_PORT_BUTTONS`  | 3  | IN  | Button bitmask: bit 0 = left, bit 1 = right, bit 2 = middle. |

### 3.5 `BUS_DEV_AUDIO`

| Port              | ID | Access | Meaning |
|-------------------|:--:|:------:|---------|
| `AUDIO_PORT_PLAY`  | 0  | OUTB | Play a WAV clip straight from `RAM[addr..addr+len)`. |

### 3.6 `BUS_DEV_DISK0` / `BUS_DEV_DISK1` — raw, sector-addressed block devices

No filesystem lives in the VM or the host runtime any more (SVFS is
gone). A disk image is a flat file of fixed-size sectors
(`DISK_SECTOR_SIZE` = 512 bytes, see [`src/disk.h`](src/disk.h)); build
whatever filesystem you want on top of it in software, or none at all.

A machine may have **0, 1, or 2 disks** attached, each its own
independent bus device — `BUS_DEV_DISK0` and `BUS_DEV_DISK1` — with its
own staging LBA, its own status, and its own backing image. There is no
shared "select a disk" register: which drive you're talking to is
purely a matter of which `dev` id you pass to `OP_BUS_*`, the same way
a real machine addresses two drives at two different bus/port numbers.
Both drives share the exact same port layout below.

| Port                     | ID | Access | Meaning |
|--------------------------|:--:|:------:|---------|
| `DISK_PORT_LBA`           | 0  | OUT | Stage the target sector number (0-based) for the next `DISK_CMD_READ`/`DISK_CMD_WRITE` **on this drive**. |
| `DISK_PORT_SECTOR_SIZE`   | 1  | IN  | Bytes per sector (`0` if this drive has no disk attached). |
| `DISK_PORT_SECTOR_COUNT`  | 2  | IN  | Total sectors on this drive's attached image (`0` if none). |
| `DISK_PORT_STATUS`        | 3  | IN  | Result of the last read/write on this drive: `0`=idle/never run, `1`=ok, `2`=error (out of range, short file, or not attached). |
| `DISK_CMD_READ`           | 4  | INB | Read `len` bytes starting at this drive's staged LBA into `RAM[addr..)`. |
| `DISK_CMD_WRITE`          | 5  | OUTB | Write `len` bytes from `RAM[addr..)` to this drive's image starting at its staged LBA. |

`len` does **not** need to be a multiple of the sector size, but the
read/write must not run past the end of the image.

Drive 0 is attached with `quartz --disk <path>`, drive 1 with
`quartz --disk1 <path>` — either, both, or neither may be given (see
[`src/qdisk.c`](src/qdisk.c) for the `qdisk create`/`qdisk info` CLI
that makes/inspects raw images).

### 3.7 `BUS_DEV_NETWORK`

| Port                    | ID | Access | Meaning |
|-------------------------|:--:|:------:|---------|
| `NET_PORT_MAXPACKET`     | 0  | IN  | Largest packet the attached card will carry (`0` if none attached). |
| `NET_PORT_POLL`          | 1  | IN  | `1` if a packet is waiting to be received, else `0`. |
| `NET_PORT_STATUS`        | 2  | IN  | Result of the last send/recv: `0`=idle, `1`=ok, `2`=packet too large, `3`=disconnected/error. |
| `NET_CMD_SEND`           | 3  | OUTB | Send `RAM[addr..addr+len)` as one packet. |
| `NET_CMD_RECV`           | 4  | INB | Receive one packet into `RAM[addr..addr+len)`. Never blocks: if nothing is waiting, `dst = 0` immediately. |

A network card is attached with `quartz --net <socket_path>`, and a
disconnect (from either side) or an oversized packet is reported
through `NET_PORT_STATUS` rather than an instruction fault.

### 3.8 `BUS_DEV_RTC` — real-time clock

Read-only. Reports the host's current wall-clock date/time (local time
zone), broken out field by field so a program can read exactly the
part it needs — just the day, just the minute — with a single
`OP_BUS_IN` each, instead of decoding a packed timestamp. Always
present (`OP_BUS_STATUS` → `1`); there is nothing to attach or
configure.

| Port                    | ID | Access | Meaning |
|-------------------------|:--:|:------:|---------|
| `RTC_PORT_YEAR`          | 0  | IN | Full year, e.g. `2026`. |
| `RTC_PORT_MONTH`         | 1  | IN | `1..12`. |
| `RTC_PORT_DAY`           | 2  | IN | `1..31`, day of the month. |
| `RTC_PORT_HOUR`          | 3  | IN | `0..23`. |
| `RTC_PORT_MINUTE`        | 4  | IN | `0..59`. |
| `RTC_PORT_SECOND`        | 5  | IN | `0..59`. |
| `RTC_PORT_WEEKDAY`       | 6  | IN | `0..6`, `0` = Sunday. |
| `RTC_PORT_MILLISECOND`   | 7  | IN | `0..999`, sub-second part of `SECOND`. |
| `RTC_PORT_EPOCH`         | 8  | IN | Whole-second Unix timestamp, for when a program wants everything at once instead of field by field. |

Each `OP_BUS_IN` re-reads the clock, so two calls a few milliseconds
apart may (rarely) straddle a rollover — e.g. reading `SECOND` right
before it ticks over and `MINUTE` right after. If a program needs a
perfectly consistent snapshot of several fields together, read
`RTC_PORT_EPOCH` once and derive the rest from that instead of reading
each field separately.

---

## 4. Examples

Pseudo-assembly below; `r0..r15` are the 16 general-purpose registers.
Immediate loads are `MOVI <type> <reg>, <value>`.

### 4.1 Plot a red pixel at (10, 20)

```
MOVI  U32  r1, 0            ; r1 = BUS_DEV_VIDEO
MOVI  U32  r2, 2            ; r2 = VIDEO_PORT_X
MOVI  U32  r3, 10
BUS_OUT    r1, r2, r3       ; stage X = 10

MOVI  U32  r2, 3            ; r2 = VIDEO_PORT_Y
MOVI  U32  r3, 20
BUS_OUT    r1, r2, r3       ; stage Y = 20

MOVI  U32  r2, 7            ; r2 = VIDEO_CMD_PIX
MOVI  U32  r3, 0x000000FF   ; 0xAABBGGRR = opaque red
BUS_OUT    r1, r2, r3       ; plot the pixel
```

### 4.2 Is disk 0 attached? What about disk 1?

```
MOVI  U32  r1, 5            ; r1 = BUS_DEV_DISK0
BUS_STATUS r1, r0           ; r0 = 1 if drive 0 is attached, else 0

MOVI  U32  r1, 6            ; r1 = BUS_DEV_DISK1
BUS_STATUS r1, r0           ; r0 = 1 if drive 1 is attached, else 0
```

### 4.3 Read sector 0 of disk 0 into RAM at 0x1000

```
MOVI  U32  r1, 5            ; r1 = BUS_DEV_DISK0
MOVI  U32  r2, 0            ; r2 = DISK_PORT_LBA
MOVI  U32  r3, 0
BUS_OUT    r1, r2, r3       ; stage LBA = 0 (on drive 0)

MOVI  U32  r4, 4            ; r4 = DISK_CMD_READ
MOVI  U32  r5, 0x1000       ; r5 = destination address
MOVI  U32  r6, 512          ; r6 = length (one sector)
BUS_INB    r1, r4, r5, r6, r7   ; r7 = bytes actually read, or -1 on failure
```

Talking to disk 1 instead is identical except `r1 = 6`
(`BUS_DEV_DISK1`) — its LBA, status, and image are completely
independent of disk 0's.

### 4.4 Poll the keyboard

```
MOVI  U32  r1, 2            ; r1 = BUS_DEV_KEYBOARD
MOVI  U32  r2, 0            ; r2 = KBD_PORT_POLL
MOVI  U32  r3, 0x2000       ; r3 = destination address (needs 8 bytes)
MOVI  U32  r4, 8            ; r4 = length
BUS_INB    r1, r2, r3, r4, r5   ; r5 = number of keys held (0..4), keys at RAM[0x2000..0x2008)
```

### 4.5 Read just the current minute — and, separately, the day

```
MOVI  U32  r1, 8            ; r1 = BUS_DEV_RTC
MOVI  U32  r2, 4            ; r2 = RTC_PORT_MINUTE
BUS_IN     r1, r2, r0       ; r0 = current minute (0..59)

MOVI  U32  r2, 2            ; r2 = RTC_PORT_DAY
BUS_IN     r1, r2, r0       ; r0 = current day of the month (1..31)
```

---

## 5. Migration notes (old opcode → new BUS call)

| Old opcode          | New equivalent |
|---------------------|-----------------|
| `OP_GFX_PIX`         | Stage `VIDEO_PORT_X`/`_Y`, then `OUT VIDEO_CMD_PIX`. |
| `OP_GFX_FRAME`       | Stage `VIDEO_PORT_X/Y/W/H`, then `OUTB VIDEO_CMD_FRAME`. |
| `OP_GFX_RAW`         | Stage `VIDEO_PORT_OFFSET`, then `OUTB VIDEO_CMD_RAW`. |
| `OP_GFX_CLEAR`       | `OUT VIDEO_CMD_CLEAR`. |
| `OP_GFX_CLEARC`      | `OUT VIDEO_CMD_CLEARC`. |
| `OP_GFX_GETRES`      | `IN VIDEO_PORT_WIDTH` / `IN VIDEO_PORT_HEIGHT`. |
| `OP_VSYNC`           | `OUT VIDEO_CMD_VSYNC`. |
| `OP_TXT_CHAR`        | Stage `VTXT_PORT_X/Y/CODEPOINT/FG/BG`, then `OUT VTXT_CMD_CHAR`. |
| `OP_TXT_FRAME`       | Stage `VTXT_PORT_X/Y/W/H`, then `OUTB VTXT_CMD_FRAME`. |
| `OP_TXT_GETRES`      | `IN VTXT_PORT_COLS` / `IN VTXT_PORT_ROWS`. |
| `OP_TXT_CLEAR`       | `OUT VTXT_CMD_CLEAR`. |
| `OP_TXT_CURSET`      | Stage `VTXT_PORT_X/Y`, then `OUT VTXT_CMD_CURSET`. |
| `OP_TXT_CURGET`      | `IN VTXT_PORT_CURSOR_X` / `IN VTXT_PORT_CURSOR_Y`. |
| `OP_TXT_CURENA`      | `OUT VTXT_CMD_CURENA`. |
| `OP_IN_MOUSE`        | `OUT MOUSE_PORT_MODE, 0`, then `IN MOUSE_PORT_X/_Y/_BUTTONS`. |
| `OP_IN_TXT_MOUSE`    | `OUT MOUSE_PORT_MODE, 1`, then `IN MOUSE_PORT_X/_Y/_BUTTONS`. |
| `OP_IN_KEY`          | `INB KBD_PORT_POLL`. |
| `OP_IN_POLL`/`OP_IN_TXT_POLL` | No longer needed — reading the mouse/keyboard ports is always current. |
| `OP_SOUND`           | `OUTB AUDIO_PORT_PLAY`. |
| `OP_SVFS_*`          | Gone — there is no filesystem in the VM any more. Use `DISK_CMD_READ`/`DISK_CMD_WRITE` and build a filesystem in software if you need one. |
| `OP_NET_ATTACHED`    | `BUS_STATUS BUS_DEV_NETWORK`. |
| `OP_NET_SEND`        | `OUTB NET_CMD_SEND`. |
| `OP_NET_RECV`        | `INB NET_CMD_RECV`. |
| `OP_NET_POLL`        | `IN NET_PORT_POLL`. |
| `OP_NET_MAXPACKET`   | `IN NET_PORT_MAXPACKET`. |

`OP_VIDEO`, `OP_VIDEO_INFO` and `OP_BEEP` are unchanged — they never
moved to the bus. The old single `OP_SVFS_ATTACHED`/`BUS_DEV_DISK`
model becomes two independent checks, one per drive: `BUS_STATUS
BUS_DEV_DISK0` and `BUS_STATUS BUS_DEV_DISK1`.

`BUS_DEV_RTC` has no old-opcode equivalent — it's a new device, not a
port of anything Quartz had before.
