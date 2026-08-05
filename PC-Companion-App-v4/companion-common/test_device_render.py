"""Tests for the firmware-accurate stats emulator (device_render.py).

Locks the text-builder (displayMetricCompact port) and verifies that a known
overlapping selection actually overlaps in the emulator - the bug the 1:1
preview exists to expose.
"""
import unittest

import device_render as d


class TextBuilder(unittest.TestCase):
    def test_normal_percent(self):
        self.assertEqual(d.build_metric_text("CPU", "%", 45), "CPU:45%")

    def test_trailing_percent_in_label_stripped(self):
        # firmware strips a trailing '%' from the label, keeps the unit
        self.assertEqual(d.build_metric_text("CPU%", "%", 45), "CPU:45%")

    def test_caret_becomes_space_and_moves_after_colon(self):
        # "CPU^" -> caret to space -> trailing space moved after the colon
        self.assertEqual(d.build_metric_text("CPU^", "C", 55), "CPU: 55C")

    def test_rpm_k_format(self):
        self.assertEqual(d.build_metric_text("FAN", "RPM", 1520, rpm_k=True), "FAN:1.5K")
        # below 1000 stays raw even with rpm_k
        self.assertEqual(d.build_metric_text("FAN", "RPM", 520, rpm_k=True), "FAN:520RPM")

    def test_kbps_decimal_and_mb(self):
        # value is x10 from Python -> /10
        self.assertEqual(d.build_metric_text("DL", "KB/s", 13), "DL:1.3KB/s")
        self.assertEqual(d.build_metric_text("DL", "KB/s", 13000, net_mb=True), "DL:1.3M")

    def test_companion_inline(self):
        self.assertEqual(d.build_companion_text("C", 55), " 55C")
        self.assertEqual(d.build_companion_text("KB/s", 14), " 1.4KB/s")


class Overlap(unittest.TestCase):
    def test_long_left_label_exceeds_column(self):
        # "CPUTemp:44C" = 11 chars * 6 = 66px, past COL2_X=62 -> overlaps right column
        self.assertGreater(d.text_pixel_width(d.build_metric_text("CPUTemp", "C", 44)), 62)
        # short label stays inside the column
        self.assertLessEqual(d.text_pixel_width(d.build_metric_text("CPU", "%", 5)), 62)

    def test_render_produces_overlap_pixels(self):
        # left "CPUTemp:44C" and a right-column metric at x=62 must share columns.
        metrics = {
            1: {"label": "CPUTemp", "unit": "C", "value": 44},
            2: {"label": "NetUP", "unit": "KB/s", "value": 13},
        }
        layout = {
            1: {"position": 4, "companionId": 0, "barPosition": 255,
                "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0},
            2: {"position": 5, "companionId": 0, "barPosition": 255,
                "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0},
        }
        img = d.render_stats_frame(metrics, layout, row_mode=0)
        self.assertEqual(img.size, (128, 64))
        # positions 4/5 are the left/right of row 2 (row = pos // 2). row_h=13 -> y=26.
        # Lit pixels must exist at x in [62, 66) on that row, proving the left text
        # bleeds into the right column's start.
        px = img.load()
        band = range(26, 34)
        lit_in_overlap = any(px[x, y] for x in range(62, 66) for y in band)
        self.assertTrue(lit_in_overlap, "expected left text to overlap right column")


class Modes(unittest.TestCase):
    def test_image_size_all_modes(self):
        metrics = {1: {"label": "CPU", "unit": "%", "value": 50}}
        layout = {1: {"position": 0, "companionId": 0, "barPosition": 255,
                      "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0}}
        for rm in (0, 1, 2, 3):
            img = d.render_stats_frame(metrics, layout, row_mode=rm)
            self.assertEqual(img.size, (128, 64))

    def test_bar_fills_with_value(self):
        metrics = {1: {"label": "CPU", "unit": "%", "value": 100}}
        layout = {1: {"position": 255, "companionId": 0, "barPosition": 0,
                      "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0}}
        img = d.render_stats_frame(metrics, layout, row_mode=0)
        px = img.load()
        # a full bar should light pixels inside the left column near the top
        self.assertTrue(any(px[x, y] for x in range(2, 58) for y in range(1, 7)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
