"""
    RAW Disk utility
"""

import os
import re
import argparse

SECTOR_SIZE = 512

def parse_size(value):
    match = re.fullmatch(r"(\d+)([KMG]?)", value.upper())
    if not match:
        raise argparse.ArgumentTypeError("Invalid size. Use for example 8000, 512K, 20M or 1G.")
    number = int(match.group(1))
    unit = match.group(2)
    if unit == "K":
        number *= 1024
    elif unit == "M":
        number *= 1024 * 1024
    elif unit == "G":
        number *= 1024 * 1024 * 1024
    return number

def create_disk(filename, size):
    sectors = (size + SECTOR_SIZE - 1) // SECTOR_SIZE
    disk_size = sectors * SECTOR_SIZE

    print("Creating disk:")
    print("  file:     {}".format(filename))
    print("  size:     {} Bytes".format(disk_size))
    print("  sectors:  {}".format(sectors))
    print("  sector:   {} Bytes".format(SECTOR_SIZE))

    with open(filename, "wb") as f:
        zero_sector = b"\x00" * SECTOR_SIZE
        for i in range(sectors):
            f.write(zero_sector)
    print("Done")

def write_file(filename, lba, input_file):
    if lba < 0: raise ValueError("LBA cannot be negative.")
    if not os.path.isfile(input_file):
        raise FileNotFoundError("File '{}' does not exist.".format(input_file))
    file_size = os.path.getsize(input_file)
    offset = lba * SECTOR_SIZE
    end_offset = offset + file_size
    disk_size = os.path.getsize(filename)

    if end_offset > disk_size:
        raise ValueError(
            "File does not fit on the disk.\n"
            "  disk size:  {} B\n"
            "  write:      offset {} + {} Bytes = {} Bytes".format(
                disk_size,
                offset,
                file_size,
                end_offset
            )
        )

    print("Writing file:")
    print("  disk:    {}".format(filename))
    print("  file:    {}".format(input_file))
    print("  LBA:     {}".format(lba))
    print("  offset:  {} Bytes".format(offset))
    print("  size:    {} Bytes".format(file_size))

    with open(input_file, "rb") as src:
        data = src.read()

    with open(filename, "r+b") as disk:
        disk.seek(offset)
        disk.write(data)
    print("Done")

def info_disk(filename):
    if not os.path.isfile(filename):
        raise FileNotFoundError("Disk image '{}' does not exist.".format(filename))
    disk_size = os.path.getsize(filename)
    if disk_size % SECTOR_SIZE != 0:
        raise ValueError(
            "Disk size {} Bytes is not a multiple of the {} Bytes sector size.".format(
                disk_size,
                SECTOR_SIZE
            )
        )
    total_sectors = disk_size // SECTOR_SIZE
    free_sectors = 0
    used_sectors = 0
    zero_sector = b"\x00" * SECTOR_SIZE

    with open(filename, "rb") as disk:
        for lba in range(total_sectors):
            data = disk.read(SECTOR_SIZE)
            if data == zero_sector:
                free_sectors += 1
            else:
                used_sectors += 1

    free_bytes = free_sectors * SECTOR_SIZE
    used_bytes = used_sectors * SECTOR_SIZE

    print("Disk info:")
    print("  file:             {}".format(filename))
    print("  size:             {} B".format(disk_size))
    print("  sector size:      {} B".format(SECTOR_SIZE))
    print("  total sectors:    {}".format(total_sectors))
    print("  used sectors:     {}".format(used_sectors))
    print("  free sectors:     {}".format(free_sectors))
    print("  used space:       {} B".format(used_bytes))
    print("  free space:       {} B".format(free_bytes))


def main():
    parser = argparse.ArgumentParser(description="RAW Disk image utility")
    parser.add_argument("filename", help="disk image filename")
    parser.add_argument("--create", type=parse_size, metavar="SIZE",
        help="create a RAW disk, e.g. 8000, 512K, 20M or 1G"
    )
    parser.add_argument("--write", nargs=2, metavar=("LBA", "FILE"),
        help="write a binary file starting at the specified LBA"
    )
    parser.add_argument("--info", action="store_true",
        help="show disk information and free sectors"
    )
    args = parser.parse_args()
    if args.create is not None:
        create_disk(args.filename, args.create)
    if args.write is not None:
        lba = int(args.write[0])
        input_file = args.write[1]
        write_file(args.filename, lba, input_file)
    if args.info:
        info_disk(args.filename)


if __name__ == "__main__":
    main()
