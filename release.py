#!/usr/bin/env python3
"""
End-to-end release builder for the SmallOLED-PCMonitor web flasher.

Runs the whole release pipeline for the browser flasher at docs/:
    1. Reads FIRMWARE_VERSION from src/config/config.h  ->  v<ver>
    2. Locates the PlatformIO CLI (PATH, then the standard penv install)
    3. Builds every chip/OLED variant in a single PlatformIO invocation
       (C3 and S3 x oled-096 = SSD1306/SSD1309, oled-13 = SH1106,
       oled-154 = CH1116)
    4. Merges bootloader + partitions + app into a single "Full" image per
       variant (flashed at 0x0, what ESP Web Tools writes)
    5. Copies the Full.bin images into docs/firmware/latest/ as
       SmallOLED-<id>-v<ver>-Full.bin and writes the VERSION file the page reads
    6. Writes the GitHub Release images into release/v<ver>/ with screen-size
       names non-technical users recognise:
         firmware-v<ver>-<tag>.bin           (new device, full 0x0 image)
         OTA_ONLY_firmware-v<ver>-<tag>.bin  (existing device, web UI update)
       C3 tags stay OLED_<size> as before; S3 tags are ESP32-S3-OLED_<size>.

The web flasher reads firmware id from the BOARDS map in docs/flasher.js:
SSD1306 (0.96") and SSD1309 (2.42") deliberately share the `ssd1306` image;
SH1106 (1.3") has its own `sh1106` image. S3 ids carry a `-s3` suffix.

Usage:
    python release.py                 # build + package every variant
    python release.py --skip-build    # package whatever .pio/build already has
    python release.py v1.6.0          # override the version string
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Variants published by the web flasher.
# (PlatformIO env, firmware id, release name tag, label).
# The firmware id must match the `firmware` field in docs/flasher.js (used for
# the docs/ flasher images). The name tag drives the user-facing release/
# filenames (firmware-v<ver>-<tag>.bin) that go on the GitHub Release.
VARIANTS = [
    ("oled-096",    "ssd1306",    "OLED_0.96inch",          'C3 0.96" SSD1306  (also 2.42" SSD1309)'),
    ("oled-13",     "sh1106",     "OLED_1.3inch",           'C3 1.3" SH1106'),
    ("oled-154",    "ch1116",     "OLED_1.54inch",          'C3 1.54" CH1116'),
    ("oled-096-s3", "ssd1306-s3", "ESP32-S3-OLED_0.96inch", 'S3 0.96" SSD1306  (also 2.42" SSD1309)'),
    ("oled-13-s3",  "sh1106-s3",  "ESP32-S3-OLED_1.3inch",  'S3 1.3" SH1106'),
    ("oled-154-s3", "ch1116-s3",  "ESP32-S3-OLED_1.54inch", 'S3 1.54" CH1116'),
]

# Flash offsets for the ESP32-C3 and ESP32-S3 (bootloader at 0x0 on both).
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

REPO_ROOT = Path(__file__).resolve().parent
CONFIG_H = REPO_ROOT / "src" / "config" / "config.h"
DOCS_LATEST = REPO_ROOT / "docs" / "firmware" / "latest"


def read_version() -> str:
    """Extract FIRMWARE_VERSION from src/config/config.h, normalised to v<ver>."""
    if not CONFIG_H.exists():
        sys.exit(f"error: {CONFIG_H} not found")
    pat = re.compile(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"')
    for line in CONFIG_H.read_text(encoding="utf-8").splitlines():
        m = pat.search(line)
        if m:
            ver = m.group(1).strip()
            return ver if ver.startswith("v") else "v" + ver
    sys.exit("error: FIRMWARE_VERSION not found in src/config/config.h")


def locate_pio() -> str:
    """Locate the PlatformIO CLI executable."""
    for name in ("pio", "pio.exe", "platformio", "platformio.exe"):
        found = shutil.which(name)
        if found:
            return found
    candidates = [
        Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
        Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe",
        Path.home() / ".platformio" / "penv" / "bin" / "pio",
        Path.home() / ".platformio" / "penv" / "bin" / "platformio",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    sys.exit(
        "error: pio executable not found.\n"
        "  Tried PATH and the standard ~/.platformio/penv locations.\n"
        "  Install PlatformIO Core or add it to PATH."
    )


def run(cmd, cwd=REPO_ROOT):
    """Run a subprocess, exit on failure."""
    print(f"\n$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"error: command failed with exit code {result.returncode}")


def build_envs(pio_path: str):
    """Build every variant env in a single PlatformIO invocation."""
    cmd = [pio_path, "run"]
    for env, *_ in VARIANTS:
        cmd.extend(["-e", env])
    run(cmd)


def build_dir(env: str) -> Path:
    return REPO_ROOT / ".pio" / "build" / env


def merge_full_bin(env: str, out_path: Path):
    """Merge bootloader + partitions + firmware into a single 0x0 image."""
    bd = build_dir(env)
    bootloader = bd / "bootloader.bin"
    partitions = bd / "partitions.bin"
    firmware = bd / "firmware.bin"
    for p in (bootloader, partitions, firmware):
        if not p.exists():
            sys.exit(f"error: {p} not found - run a build first (omit --skip-build).")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as out:
        bl = bootloader.read_bytes()
        out.write(bl)
        out.write(b"\xFF" * (PARTITIONS_OFFSET - len(bl)))

        pt = partitions.read_bytes()
        out.write(pt)
        out.write(b"\xFF" * (FIRMWARE_OFFSET - (PARTITIONS_OFFSET + len(pt))))

        out.write(firmware.read_bytes())

    size = out_path.stat().st_size
    print(f"  Full: {out_path.relative_to(REPO_ROOT)} ({size / 1024:.1f} KB)")


def copy_ota_bin(env: str, out_path: Path):
    """Copy firmware.bin verbatim as the OTA update image."""
    firmware = build_dir(env) / "firmware.bin"
    if not firmware.exists():
        sys.exit(f"error: {firmware} not found - run a build first (omit --skip-build).")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(firmware, out_path)
    size = out_path.stat().st_size
    print(f"  OTA:  {out_path.relative_to(REPO_ROOT)} ({size / 1024:.1f} KB)")


def write_version_file(version: str):
    DOCS_LATEST.mkdir(parents=True, exist_ok=True)
    (DOCS_LATEST / "VERSION").write_text(version + "\n", encoding="utf-8")


def find_old_full_bins(version: str):
    """List Full.bin files in docs/firmware/latest/ not for this version."""
    if not DOCS_LATEST.exists():
        return []
    pat = re.compile(r"^SmallOLED-(.+)-(v[^-]+)-Full\.bin$")
    old = []
    for f in DOCS_LATEST.iterdir():
        m = pat.match(f.name)
        if m and m.group(2) != version:
            old.append(f.name)
    return sorted(old)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version", nargs="?", default=None,
                        help="Version override (default: read from config.h)")
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip the PlatformIO build (assume .pio/build is current)")
    args = parser.parse_args()

    version = args.version or read_version()
    if not version.startswith("v"):
        version = "v" + version

    ota_dir = REPO_ROOT / "release" / version

    print(f"SmallOLED web-flasher release: {version}")
    print("Variants: " + ", ".join(f"{env} -> {fid}" for env, fid, *_ in VARIANTS))

    if not args.skip_build:
        pio = locate_pio()
        print(f"PlatformIO: {pio}")
        build_envs(pio)
    else:
        print("Skipping build (--skip-build)")

    print("\n--- Web flasher images (docs/firmware/latest/) ---")
    full_names = []
    for env, fid, _tag, _label in VARIANTS:
        out = DOCS_LATEST / f"SmallOLED-{fid}-{version}-Full.bin"
        merge_full_bin(env, out)
        full_names.append(out.name)
    write_version_file(version)
    print(f"  VERSION  ({version})")

    print(f"\n--- GitHub Release images (release/{version}/) ---")
    rel_names = []
    for env, fid, tag, _label in VARIANTS:
        # Full 0x0 image for new devices - same bytes as the docs flasher image,
        # named by chip + screen size so non-technical users pick the right one.
        full_src = DOCS_LATEST / f"SmallOLED-{fid}-{version}-Full.bin"
        full_out = ota_dir / f"firmware-{version}-{tag}.bin"
        full_out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(full_src, full_out)
        print(f"  Full: {full_out.relative_to(REPO_ROOT)} ({full_out.stat().st_size / 1024:.1f} KB)")
        rel_names.append(full_out.name)
        # OTA-only image for existing devices (web UI update).
        ota_out = ota_dir / f"OTA_ONLY_firmware-{version}-{tag}.bin"
        copy_ota_bin(env, ota_out)
        rel_names.append(ota_out.name)

    print("\n" + "=" * 60)
    print(f"Release {version} ready.")
    print("=" * 60)

    old = find_old_full_bins(version)
    if old:
        print("\nOlder Full.bin files still in docs/firmware/latest/ "
              "(remove with `git rm` when no longer needed):")
        for name in old:
            print(f"  {name}")

    print("\nNext steps:")
    print("  git add docs/firmware/latest/ src/config/config.h")
    print(f'  git commit -m "release {version}"')
    print(f"  gh release create {version} release/{version}/*.bin --notes \"...\"")
    print("  git push")
    print("\nCompanion app (only if it changed since the last release):")
    print("  cd PC-Companion-App-v4/win-companion && build_exe.bat")
    print(f"  gh release upload {version} "
          "PC-Companion-App-v4/win-companion/dist/pc_stats_monitor_v4.exe")


if __name__ == "__main__":
    main()
