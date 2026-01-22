#!/usr/bin/env python3
"""
Create firmware binaries for both OTA update and full web flasher installation.

Usage:
    python create_firmware.py v1.4.0 0    # Version v1.4.0, OLED 0.96inch
    python create_firmware.py v1.4.0 1    # Version v1.4.0, OLED 1.3inch

Output files (in release/{version}/ folder):
    release/v1.4.0/firmware-v1.4.0-OLED_0.96inch.bin           (full flash)
    release/v1.4.0/OTA_ONLY_firmware-v1.4.0-OLED_0.96inch.bin  (OTA update)
"""

import argparse
import os
import sys

# Binary file paths
BOOTLOADER = '.pio/build/esp32-c3-devkitm-1/bootloader.bin'
PARTITIONS = '.pio/build/esp32-c3-devkitm-1/partitions.bin'
FIRMWARE = '.pio/build/esp32-c3-devkitm-1/firmware.bin'
RELEASES_DIR = 'release'

# Flash offsets (standard for ESP32-C3)
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

# OLED type mapping
OLED_TYPES = {
    '0': '0.96inch',
    '1': '1.3inch'
}


def get_output_filenames(version, oled_type):
    """Generate output filenames based on version and OLED type."""
    oled_name = OLED_TYPES.get(oled_type, '0.96inch')
    base_name = f"{version}-OLED_{oled_name}"

    # Create version-specific folder under release/
    output_dir = os.path.join(RELEASES_DIR, version)

    merged_name = f"firmware-{base_name}.bin"
    ota_name = f"OTA_ONLY_firmware-{base_name}.bin"

    return (
        output_dir,
        os.path.join(output_dir, merged_name),
        os.path.join(output_dir, ota_name)
    )


def create_ota_binary(output_path):
    """Create OTA-only firmware binary (just firmware.bin copy)."""

    if not os.path.exists(FIRMWARE):
        print(f"Error: {FIRMWARE} not found!")
        print("Please build the firmware first: platformio run")
        return False

    firmware_size = os.path.getsize(FIRMWARE)

    print(f"\nCreating OTA firmware: {output_path}")
    print(f"  Source: {FIRMWARE}")
    print(f"  Size: {firmware_size} bytes ({firmware_size / 1024:.1f} KB)")

    with open(FIRMWARE, 'rb') as infile:
        with open(output_path, 'wb') as outfile:
            outfile.write(infile.read())

    return True


def create_merged_binary(output_path):
    """Merge bootloader, partitions, and firmware into single binary."""

    # Check if all input files exist
    for filepath in [BOOTLOADER, PARTITIONS, FIRMWARE]:
        if not os.path.exists(filepath):
            print(f"Error: {filepath} not found!")
            print("Please build the firmware first: platformio run")
            return False

    print(f"\nCreating merged firmware: {output_path}")
    print(f"  Bootloader: {BOOTLOADER} @ 0x{BOOTLOADER_OFFSET:X}")
    print(f"  Partitions: {PARTITIONS} @ 0x{PARTITIONS_OFFSET:X}")
    print(f"  Firmware:   {FIRMWARE} @ 0x{FIRMWARE_OFFSET:X}")

    with open(output_path, 'wb') as outfile:
        # Write bootloader at 0x0
        with open(BOOTLOADER, 'rb') as f:
            bootloader_data = f.read()
            outfile.write(bootloader_data)
            bootloader_size = len(bootloader_data)

        # Pad to partitions offset (0x8000)
        padding_size = PARTITIONS_OFFSET - bootloader_size
        outfile.write(b'\xFF' * padding_size)

        # Write partitions at 0x8000
        with open(PARTITIONS, 'rb') as f:
            partitions_data = f.read()
            outfile.write(partitions_data)
            partitions_size = len(partitions_data)

        # Pad to firmware offset (0x10000)
        current_pos = PARTITIONS_OFFSET + partitions_size
        padding_size = FIRMWARE_OFFSET - current_pos
        outfile.write(b'\xFF' * padding_size)

        # Write firmware at 0x10000
        with open(FIRMWARE, 'rb') as f:
            firmware_data = f.read()
            outfile.write(firmware_data)

    total_size = os.path.getsize(output_path)
    print(f"  Total size: {total_size} bytes ({total_size / 1024:.1f} KB)")

    return True


def main():
    parser = argparse.ArgumentParser(
        description='Create firmware binaries for OTA and web flasher',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
    python create_firmware.py v1.4.0 0    # 0.96inch OLED
    python create_firmware.py v1.4.1 1    # 1.3inch OLED
        '''
    )
    parser.add_argument(
        'version',
        help='Firmware version (e.g., v1.4.0)'
    )
    parser.add_argument(
        'oled_type',
        choices=['0', '1'],
        help='OLED type: 0 = 0.96inch, 1 = 1.3inch'
    )

    args = parser.parse_args()

    # Get output filenames and directory
    output_dir, merged_path, ota_path = get_output_filenames(args.version, args.oled_type)

    # Create release directory if needed (may already exist for 2nd OLED type)
    os.makedirs(output_dir, exist_ok=True)

    oled_name = OLED_TYPES[args.oled_type]
    print("=" * 60)
    print(f"Creating firmware binaries")
    print(f"  Version: {args.version}")
    print(f"  OLED: {oled_name}")
    print("=" * 60)

    # Create both binaries
    success_merged = create_merged_binary(merged_path)
    success_ota = create_ota_binary(ota_path)

    if success_merged and success_ota:
        print("\n" + "=" * 60)
        print("SUCCESS! Both firmware files created:")
        print("=" * 60)
        print(f"\n  Web Flasher (full):  {merged_path}")
        print(f"  OTA Update:          {ota_path}")

        print(f"\nWEB FLASHER USAGE:")
        print(f"  - Open: https://espressif.github.io/esptool-js/")
        print(f"  - Flash {merged_path} at offset 0x0")

        print(f"\nOTA UPDATE USAGE:")
        print(f"  - Open device web page")
        print(f"  - Upload {ota_path} in Firmware Update section")

        return 0
    else:
        print("\nERROR: Failed to create one or more firmware files")
        return 1


if __name__ == '__main__':
    sys.exit(main())
