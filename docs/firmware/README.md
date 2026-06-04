# Firmware binaries for the web flasher

These files are produced automatically by `release.py` at the repo root
(`python release.py`), which builds both OLED variants, merges each into a
"Full" image, and drops them here with the matching `VERSION`. You normally
don't add them by hand - the manual steps below are just for reference.

The flasher reads the version string from `VERSION` (a single line, e.g. `v1.5.0`)
and expects these full-image binaries alongside it:

    firmware/latest/
      VERSION                              ← one line, e.g. v1.5.0
      SmallOLED-ssd1306-v1.5.0-Full.bin    ← 0.96" SSD1306  AND  2.42" SSD1309
      SmallOLED-sh1106-v1.5.0-Full.bin     ← 1.3" SH1106 (separate image)

Notes
-----
- The filename pattern is `SmallOLED-<id>-<VERSION>-Full.bin`, where `<id>` is the
  `firmware` value in the `BOARDS` map in `flasher.js`.
- SSD1306 (0.96") and SSD1309 (2.42") deliberately point at the SAME `ssd1306` image.
- "Full" means a merged image (bootloader + partitions + app) flashed at offset 0x0.
  ESP Web Tools writes it to 0x0 — exactly what the README's manual `0x0` step does.
- To publish a new release, bump `VERSION` and add the matching `-Full.bin` files.
  No code change needed.

How to build a merged image (from the project, esptool):

    esptool.py --chip esp32c3 merge_bin -o SmallOLED-ssd1306-v1.5.0-Full.bin \
      0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
