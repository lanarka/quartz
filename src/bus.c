// =============================================================================
// Quartz - The Computer
// -----------------------------------------------------------------------------
// bus.c - implements the six bus_*() entry points declared in bus.h.
//
// This is the ONLY file that knows how a (dev, port) pair maps onto an
// actual peripheral. vm.c never calls rt_*()/disk_*()/vmnet_*() directly
// any more (except for OP_VIDEO/OP_VIDEO_INFO/OP_BEEP, which stayed core
// opcodes rather than becoming bus devices - see vm.h) - it only fetches
// register operands off the instruction stream and forwards them here.
//
// Every dispatcher takes `int *ok` (bus_in/bus_inb/bus_outb) so callers can
// tell "the operation ran and returned a real value" apart from "dev/port
// was not recognized" without overloading the return value itself, since 0
// is often a perfectly valid result (e.g. "0 keys held").
// =============================================================================

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include "bus.h"
#include "vm.h"
#include "runtime.h"
#include "disk.h"
#include "vmnet.h"

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

static rt_color unpack_color(uint32_t c) {
    rt_color col = {
        (uint8_t)(c & 0xFF),
        (uint8_t)((c >> 8) & 0xFF),
        (uint8_t)((c >> 16) & 0xFF),
        (uint8_t)((c >> 24) & 0xFF)
    };
    return col;
}

static VmNetChannel *vm_net(VM *self) {
    return (VmNetChannel *)self->net;
}

// Fills `*out` with the host's current wall-clock date/time (local time zone)
// and `*ms_out` with the sub-second part in milliseconds. Used only by
// BUS_DEV_RTC (see bus_in() below) - the RTC is read-only and always
// reflects "right now", there is nothing to stage or configure.
static void rtc_now(struct tm *out, int *ms_out) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t secs = tv.tv_sec;
    localtime_r(&secs, out);
    *ms_out = (int)(tv.tv_usec / 1000);
}

// Maps a `dev` id to a disk slot (0 or 1), or -1 if `dev` isn't a disk at all.
static int disk_slot_for(int dev) {
    if (dev == BUS_DEV_DISK0) return 0;
    if (dev == BUS_DEV_DISK1) return 1;
    return -1;
}

static DiskDrive *vm_disk(VM *self, int dev) {
    int slot = disk_slot_for(dev);
    if (slot < 0) return NULL;
    return (DiskDrive *)self->disk[slot];
}

// ---------------------------------------------------------------------------
// bus_reset - reinitialize a device to a known state. Devices that have no
// meaningful "reset" (keyboard, mouse, audio) just no-op.
// ---------------------------------------------------------------------------
void bus_reset(VM *vm, int dev) {
    switch (dev) {
        case BUS_DEV_VIDEO:
            rt_clear(vm->rt);
            break;
        case BUS_DEV_VIDEO_TEXT:
            rt_txtclear(vm->rt);
            break;
        case BUS_DEV_DISK0:
        case BUS_DEV_DISK1: {
            int slot = disk_slot_for(dev);
            vm->bus.disk_lba[slot] = 0;
            vm->bus.disk_status[slot] = 0;
            break;
        }
        case BUS_DEV_NETWORK:
            vm->bus.net_status = 0;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// bus_status - is the device present/usable right now?
// ---------------------------------------------------------------------------
int bus_status(VM *vm, int dev) {
    switch (dev) {
        case BUS_DEV_VIDEO:
        case BUS_DEV_VIDEO_TEXT:
        case BUS_DEV_KEYBOARD:
        case BUS_DEV_MOUSE:
        case BUS_DEV_AUDIO:
        case BUS_DEV_RTC:
            return 1; // always present - part of the base machine
        case BUS_DEV_DISK0:
            return vm->disk[0] != NULL;
        case BUS_DEV_DISK1:
            return vm->disk[1] != NULL;
        case BUS_DEV_NETWORK:
            return vm->net != NULL;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// bus_in - read a single register/port.
// ---------------------------------------------------------------------------
int64_t bus_in(VM *vm, int dev, int port, int *ok) {
    *ok = 1;
    switch (dev) {
        case BUS_DEV_VIDEO:
            switch (port) {
                case VIDEO_PORT_WIDTH:  return (int64_t)vm->screen_w;
                case VIDEO_PORT_HEIGHT: return (int64_t)vm->screen_h;
                case VIDEO_PORT_X:      return (int64_t)vm->bus.video_x;
                case VIDEO_PORT_Y:      return (int64_t)vm->bus.video_y;
                case VIDEO_PORT_W:      return (int64_t)vm->bus.video_w;
                case VIDEO_PORT_H:      return (int64_t)vm->bus.video_h;
                case VIDEO_PORT_OFFSET: return (int64_t)vm->bus.video_offset;
                default: break;
            }
            break;

        case BUS_DEV_VIDEO_TEXT:
            switch (port) {
                case VTXT_PORT_COLS: return (int64_t)vm->term_w;
                case VTXT_PORT_ROWS: return (int64_t)vm->term_h;
                case VTXT_PORT_CURSOR_X: {
                    uint16_t x, y;
                    rt_curget(vm->rt, &x, &y);
                    return (int64_t)x;
                }
                case VTXT_PORT_CURSOR_Y: {
                    uint16_t x, y;
                    rt_curget(vm->rt, &x, &y);
                    return (int64_t)y;
                }
                default: break;
            }
            break;

        case BUS_DEV_MOUSE:
            switch (port) {
                case MOUSE_PORT_X: {
                    uint16_t x, y; uint8_t b;
                    if (vm->bus.mouse_text_mode) rt_txt_mouse(vm->rt, &x, &y, &b);
                    else                         rt_mouse(vm->rt, &x, &y, &b);
                    return (int64_t)x;
                }
                case MOUSE_PORT_Y: {
                    uint16_t x, y; uint8_t b;
                    if (vm->bus.mouse_text_mode) rt_txt_mouse(vm->rt, &x, &y, &b);
                    else                         rt_mouse(vm->rt, &x, &y, &b);
                    return (int64_t)y;
                }
                case MOUSE_PORT_BUTTONS: {
                    uint16_t x, y; uint8_t b;
                    if (vm->bus.mouse_text_mode) rt_txt_mouse(vm->rt, &x, &y, &b);
                    else                         rt_mouse(vm->rt, &x, &y, &b);
                    return (int64_t)b;
                }
                default: break;
            }
            break;

        case BUS_DEV_DISK0:
        case BUS_DEV_DISK1: {
            int slot = disk_slot_for(dev);
            DiskDrive *d = vm_disk(vm, dev);
            switch (port) {
                case DISK_PORT_SECTOR_SIZE:  return d ? (int64_t)DISK_SECTOR_SIZE : 0;
                case DISK_PORT_SECTOR_COUNT: return d ? (int64_t)d->sector_count : 0;
                case DISK_PORT_STATUS:       return (int64_t)vm->bus.disk_status[slot];
                default: break;
            }
            break;
        }

        case BUS_DEV_NETWORK: {
            VmNetChannel *ch = vm_net(vm);
            switch (port) {
                case NET_PORT_MAXPACKET: return (int64_t)(ch ? vmnet_channel_max_packet(ch) : 0);
                case NET_PORT_POLL:      return (int64_t)(ch && vmnet_poll_readable(ch, 0) > 0 ? 1 : 0);
                case NET_PORT_STATUS:    return (int64_t)vm->bus.net_status;
                default: break;
            }
            break;
        }

        case BUS_DEV_RTC: {
            struct tm t; int ms;
            rtc_now(&t, &ms);
            switch (port) {
                case RTC_PORT_YEAR:        return (int64_t)(t.tm_year + 1900);
                case RTC_PORT_MONTH:       return (int64_t)(t.tm_mon + 1);
                case RTC_PORT_DAY:         return (int64_t)t.tm_mday;
                case RTC_PORT_HOUR:        return (int64_t)t.tm_hour;
                case RTC_PORT_MINUTE:      return (int64_t)t.tm_min;
                case RTC_PORT_SECOND:      return (int64_t)t.tm_sec;
                case RTC_PORT_WEEKDAY:     return (int64_t)t.tm_wday;
                case RTC_PORT_MILLISECOND: return (int64_t)ms;
                case RTC_PORT_EPOCH:       return (int64_t)time(NULL);
                default: break;
            }
            break;
        }

        default:
            break;
    }
    *ok = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// bus_out - write a single register/port, or fire a zero-payload command.
// ---------------------------------------------------------------------------
int bus_out(VM *vm, int dev, int port, int64_t val) {
    switch (dev) {
        case BUS_DEV_VIDEO:
            switch (port) {
                case VIDEO_PORT_X:      vm->bus.video_x = (uint16_t)val; return 1;
                case VIDEO_PORT_Y:      vm->bus.video_y = (uint16_t)val; return 1;
                case VIDEO_PORT_W:      vm->bus.video_w = (uint16_t)val; return 1;
                case VIDEO_PORT_H:      vm->bus.video_h = (uint16_t)val; return 1;
                case VIDEO_PORT_OFFSET: vm->bus.video_offset = (uint32_t)val; return 1;
                case VIDEO_CMD_PIX: {
                    rt_color c = unpack_color((uint32_t)val);
                    rt_pix(vm->rt, vm->bus.video_x, vm->bus.video_y, c.r, c.g, c.b, c.a);
                    return 1;
                }
                case VIDEO_CMD_CLEAR:
                    rt_clear(vm->rt);
                    return 1;
                case VIDEO_CMD_CLEARC: {
                    rt_color c = unpack_color((uint32_t)val);
                    rt_clear_color(vm->rt, c.r, c.g, c.b, c.a);
                    return 1;
                }
                case VIDEO_CMD_VSYNC:
                    vm->tick_count++;
                    vm->vsync_yield = 1;
                    return 1;
                default: break;
            }
            break;

        case BUS_DEV_VIDEO_TEXT:
            switch (port) {
                case VTXT_PORT_X:         vm->bus.text_x = (uint16_t)val; return 1;
                case VTXT_PORT_Y:         vm->bus.text_y = (uint16_t)val; return 1;
                case VTXT_PORT_W:         vm->bus.text_w = (uint16_t)val; return 1;
                case VTXT_PORT_H:         vm->bus.text_h = (uint16_t)val; return 1;
                case VTXT_PORT_CODEPOINT: vm->bus.text_codepoint = (uint16_t)val; return 1;
                case VTXT_PORT_FG:        vm->bus.text_fg = (uint32_t)val; return 1;
                case VTXT_PORT_BG:        vm->bus.text_bg = (uint32_t)val; return 1;
                case VTXT_CMD_CHAR: {
                    rt_color fg = unpack_color(vm->bus.text_fg);
                    rt_color bg = unpack_color(vm->bus.text_bg);
                    rt_char(vm->rt, vm->bus.text_x, vm->bus.text_y, vm->bus.text_codepoint, fg, bg);
                    return 1;
                }
                case VTXT_CMD_CLEAR:
                    rt_txtclear(vm->rt);
                    return 1;
                case VTXT_CMD_CURSET:
                    rt_curset(vm->rt, vm->bus.text_x, vm->bus.text_y);
                    return 1;
                case VTXT_CMD_CURENA:
                    rt_curena(vm->rt, (uint8_t)val);
                    return 1;
                default: break;
            }
            break;

        case BUS_DEV_MOUSE:
            if (port == MOUSE_PORT_MODE) { vm->bus.mouse_text_mode = (uint8_t)(val != 0); return 1; }
            break;

        case BUS_DEV_DISK0:
        case BUS_DEV_DISK1:
            if (port == DISK_PORT_LBA) { vm->bus.disk_lba[disk_slot_for(dev)] = (uint64_t)val; return 1; }
            break;

        default:
            break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// bus_inb - bulk transfer device -> RAM[addr..addr+len). Caller (vm.c) has
// already verified that [addr, addr+len) lies inside VM RAM.
// ---------------------------------------------------------------------------
int64_t bus_inb(VM *vm, int dev, int port, uint32_t addr, uint32_t len, int *ok) {
    *ok = 1;
    switch (dev) {
        case BUS_DEV_KEYBOARD:
            if (port == KBD_PORT_POLL) {
                int count; int keys[4] = {0, 0, 0, 0};
                uint32_t need = 4 * 2;
                if (len < need) break;
                rt_keyboard(vm->rt, &count, keys, 4);
                for (uint32_t i = 0; i < 4; i++) {
                    uint16_t k = (uint16_t)keys[i];
                    vm->ram[addr + i * 2]     = (uint8_t)(k & 0xFF);
                    vm->ram[addr + i * 2 + 1] = (uint8_t)(k >> 8);
                }
                return (int64_t)count;
            }
            break;

        case BUS_DEV_DISK0:
        case BUS_DEV_DISK1: {
            if (port != DISK_CMD_READ) break;
            int slot = disk_slot_for(dev);
            DiskDrive *d = vm_disk(vm, dev);
            if (!d) { vm->bus.disk_status[slot] = 2; return 0; }
            int64_t n = disk_read(d, vm->bus.disk_lba[slot], &vm->ram[addr], len);
            vm->bus.disk_status[slot] = (n >= 0) ? 1 : 2;
            return (n >= 0) ? n : 0;
        }

        case BUS_DEV_NETWORK: {
            if (port != NET_CMD_RECV) break;
            VmNetChannel *ch = vm_net(vm);
            if (!ch) { vm->bus.net_status = 0; return 0; }

            // Never call the (otherwise blocking) vmnet_recv() unless a 0ms
            // poll first confirms a packet is actually waiting, so this can
            // never stall the VM's step loop.
            if (vmnet_poll_readable(ch, 0) <= 0) {
                vm->bus.net_status = 0;
                return 0;
            }

            ssize_t n = vmnet_recv(ch, &vm->ram[addr], len);
            if (n > 0) {
                vm->bus.net_status = 1;
                return (int64_t)n;
            } else if (n == 0) {
                vmnet_channel_close(ch);
                free(vm->net);
                vm->net = NULL;
                vm->bus.net_status = 3;
                return 0;
            } else {
                vm->bus.net_status = 3;
                return 0;
            }
        }

        default:
            break;
    }
    *ok = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// bus_outb - bulk transfer RAM[addr..addr+len) -> device. Caller (vm.c) has
// already verified that [addr, addr+len) lies inside VM RAM.
// ---------------------------------------------------------------------------
int64_t bus_outb(VM *vm, int dev, int port, uint32_t addr, uint32_t len, int *ok) {
    *ok = 1;
    switch (dev) {
        case BUS_DEV_VIDEO: {
            if (port == VIDEO_CMD_FRAME) {
                uint64_t need = (uint64_t)vm->bus.video_w * (uint64_t)vm->bus.video_h * 4ull;
                if (need != (uint64_t)len) break;
                rt_frame(vm->rt, &vm->ram[addr], vm->bus.video_x, vm->bus.video_y,
                          vm->bus.video_h, vm->bus.video_w);
                return (int64_t)len;
            }
            if (port == VIDEO_CMD_RAW) {
                uint32_t fb_size = (uint32_t)vm->screen_w * (uint32_t)vm->screen_h * 4u;
                if ((uint64_t)vm->bus.video_offset + (uint64_t)len > (uint64_t)fb_size) break;
                rt_raw(vm->rt, &vm->ram[addr], len, vm->bus.video_offset);
                return (int64_t)len;
            }
            break;
        }

        case BUS_DEV_VIDEO_TEXT: {
            if (port != VTXT_CMD_FRAME) break;
            uint64_t need = (uint64_t)vm->bus.text_w * (uint64_t)vm->bus.text_h * 10ull;
            if (need != (uint64_t)len) break;
            rt_txtframe(vm->rt, &vm->ram[addr], vm->bus.text_x, vm->bus.text_y,
                         vm->bus.text_h, vm->bus.text_w);
            return (int64_t)len;
        }

        case BUS_DEV_AUDIO:
            if (port == AUDIO_PORT_PLAY) {
                if (len == 0) return 0;
                rt_sound(vm->rt, &vm->ram[addr], len);
                return (int64_t)len;
            }
            break;

        case BUS_DEV_DISK0:
        case BUS_DEV_DISK1: {
            if (port != DISK_CMD_WRITE) break;
            int slot = disk_slot_for(dev);
            DiskDrive *d = vm_disk(vm, dev);
            if (!d) { vm->bus.disk_status[slot] = 2; return 0; }
            int64_t n = disk_write(d, vm->bus.disk_lba[slot], &vm->ram[addr], len);
            vm->bus.disk_status[slot] = (n >= 0) ? 1 : 2;
            return (n >= 0) ? n : 0;
        }

        case BUS_DEV_NETWORK: {
            if (port != NET_CMD_SEND) break;
            VmNetChannel *ch = vm_net(vm);
            if (!ch) { vm->bus.net_status = 0; return 0; }

            if (vmnet_send(ch, &vm->ram[addr], len) == 0) {
                vm->bus.net_status = 1;
                return (int64_t)len;
            } else if (errno == EMSGSIZE) {
                vm->bus.net_status = 2;
                return 0;
            } else {
                vmnet_channel_close(ch);
                free(vm->net);
                vm->net = NULL;
                vm->bus.net_status = 3;
                return 0;
            }
        }

        default:
            break;
    }
    *ok = 0;
    return 0;
}
