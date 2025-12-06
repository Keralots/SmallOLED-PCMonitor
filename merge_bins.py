#!/usr/bin/env python3
"""
Merge ESP32 bootloader, partitions, and firmware into a single binary.
This creates a complete firmware image that can be flashed at address 0x0.
"""

import os
import sys

# Binary file paths
BOOTLOADER = '.pio/build/esp32-c3-devkitm-1/bootloader.bin'
PARTITIONS = '.pio/build/esp32-c3-devkitm-1/partitions.bin'
FIRMWARE = '.pio/build/esp32-c3-devkitm-1/firmware.bin'
OUTPUT = 'firmware/PCMonitor-WebFlasher.bin'

# Flash offsets (standard for ESP32-C3)
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

def merge_binaries():
    """Merge bootloader, partitions, and firmware into single binary."""

    # Check if all input files exist
    for filepath in [BOOTLOADER, PARTITIONS, FIRMWARE]:
        if not os.path.exists(filepath):
            print(f"Error: {filepath} not found!")
            print("Please build the firmware first: platformio run")
            return False

    # Create output directory if needed
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)

    print("Merging binaries...")
    print(f"  Bootloader: {BOOTLOADER} @ 0x{BOOTLOADER_OFFSET:X}")
    print(f"  Partitions: {PARTITIONS} @ 0x{PARTITIONS_OFFSET:X}")
    print(f"  Firmware:   {FIRMWARE} @ 0x{FIRMWARE_OFFSET:X}")

    with open(OUTPUT, 'wb') as outfile:
        # Write bootloader at 0x0
        with open(BOOTLOADER, 'rb') as f:
            bootloader_data = f.read()
            outfile.write(bootloader_data)
            bootloader_size = len(bootloader_data)
            print(f"  Bootloader size: {bootloader_size} bytes")

        # Pad to partitions offset (0x8000)
        padding_size = PARTITIONS_OFFSET - bootloader_size
        outfile.write(b'\xFF' * padding_size)

        # Write partitions at 0x8000
        with open(PARTITIONS, 'rb') as f:
            partitions_data = f.read()
            outfile.write(partitions_data)
            partitions_size = len(partitions_data)
            print(f"  Partitions size: {partitions_size} bytes")

        # Pad to firmware offset (0x10000)
        current_pos = PARTITIONS_OFFSET + partitions_size
        padding_size = FIRMWARE_OFFSET - current_pos
        outfile.write(b'\xFF' * padding_size)

        # Write firmware at 0x10000
        with open(FIRMWARE, 'rb') as f:
            firmware_data = f.read()
            outfile.write(firmware_data)
            firmware_size = len(firmware_data)
            print(f"  Firmware size: {firmware_size} bytes")

        total_size = os.path.getsize(OUTPUT)
        print(f"\n{'='*60}")
        print(f"SUCCESS! Complete firmware created: {OUTPUT}")
        print(f"Total size: {total_size} bytes ({total_size / 1024:.1f} KB)")
        print(f"{'='*60}")

        print(f"\nFLASH OPTIONS:")
        print(f"\n1. WEB FLASHER (Easiest - No software needed!):")
        print(f"   - Open: https://espressif.github.io/esptool-js/")
        print(f"   - Connect ESP32-C3 via USB")
        print(f"   - Click 'Connect' and select your device")
        print(f"   - Add file: {OUTPUT}")
        print(f"   - Flash offset: 0x0")
        print(f"   - Click 'Program'")

        print(f"\n2. COMMAND LINE (esptool):")
        print(f"   esptool.py --chip esp32c3 write_flash 0x0 {OUTPUT}")

        print(f"\n3. PLATFORMIO:")
        print(f"   platformio run --target upload")

        print(f"\nNOTE: This single .bin file contains everything:")
        print(f"      Bootloader + Partitions + Firmware")
        print(f"      Flash it at address 0x0 for a complete installation")

    return True

if __name__ == '__main__':
    success = merge_binaries()
    sys.exit(0 if success else 1)
