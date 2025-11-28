#!/usr/bin/env python3
"""
Merge ESP32 binary files into a single flashable binary
"""

def merge_bins():
    # Define file positions (in bytes)
    bootloader_offset = 0x0
    partitions_offset = 0x8000
    firmware_offset = 0x10000

    # Read binary files
    with open('bootloader.bin', 'rb') as f:
        bootloader = f.read()

    with open('partitions.bin', 'rb') as f:
        partitions = f.read()

    with open('firmware.bin', 'rb') as f:
        firmware = f.read()

    # Calculate final size (firmware offset + firmware size)
    total_size = firmware_offset + len(firmware)

    # Create merged binary filled with 0xFF (flash erase value)
    merged = bytearray([0xFF] * total_size)

    # Write bootloader at offset 0x0
    merged[bootloader_offset:bootloader_offset + len(bootloader)] = bootloader

    # Write partitions at offset 0x8000
    merged[partitions_offset:partitions_offset + len(partitions)] = partitions

    # Write firmware at offset 0x10000
    merged[firmware_offset:firmware_offset + len(firmware)] = firmware

    # Write merged binary
    with open('firmware-complete.bin', 'wb') as f:
        f.write(merged)

    print("[OK] Created firmware-complete.bin")
    print(f"  Total size: {len(merged):,} bytes ({len(merged)/1024:.1f} KB)")
    print(f"  Bootloader: {len(bootloader):,} bytes at 0x{bootloader_offset:X}")
    print(f"  Partitions: {len(partitions):,} bytes at 0x{partitions_offset:X}")
    print(f"  Firmware:   {len(firmware):,} bytes at 0x{firmware_offset:X}")
    print("\nFlash with: esptool.py --chip esp32c3 --port COM3 write_flash 0x0 firmware-complete.bin")

if __name__ == "__main__":
    merge_bins()
