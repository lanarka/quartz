"""
	Create bootsector data
"""
import sys
import os

BOOT_MAGICK = [0xB0, 0x01]

KERNEL_START_LBA = 2
KERNEL_ENTRYPOINT = 0x00000000

def to_bytes_le(val, size=4):
    val &= (1 << (size * 8)) - 1
    return [(val >> (8 * i)) & 0xFF for i in range(size)]

def write_raw(output_path, code):
    with open(output_path, 'wb') as f:
        f.write(bytes(code))

def create_bootsector(output, kernel):
	kernel_size = os.path.getsize(kernel)
	data = []
	data += BOOT_MAGICK
	data += [KERNEL_START_LBA]
	data += to_bytes_le(kernel_size)
	data += to_bytes_le(KERNEL_ENTRYPOINT)
	write_raw(output, data)


create_bootsector(
	sys.argv[1], # Output bootsector file
	sys.argv[2], # Input kernel file
)
