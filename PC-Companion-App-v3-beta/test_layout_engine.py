"""Unit tests for the pure auto-layout engine (layout_engine.py).

The engine encodes geometry that must stay in sync with the firmware renderer
(src/metrics/metrics.cpp) and the bind-by-id contract with /api/import. These
tests lock that behaviour. Run: python test_layout_engine.py  (or via pytest).
"""
import unittest

import layout_engine as le
from constants import MAX_METRICS


def metric(mid, name, unit="", mtype="", label=None, value=0):
    return {"id": mid, "name": name, "unit": unit, "type": mtype,
            "label": label or name, "current_value": value}


class SlotGeometry(unittest.TestCase):
    def test_slot_count(self):
        self.assertEqual(le.slot_count(le.ROWMODE_5x2), 10)
        self.assertEqual(le.slot_count(le.ROWMODE_6x2), 12)
        self.assertEqual(le.slot_count(le.ROWMODE_LARGE2), 2)
        self.assertEqual(le.slot_count(le.ROWMODE_LARGE3), 3)

    def test_two_column_x(self):
        self.assertEqual(le.slot_geometry(le.ROWMODE_5x2, 0)[0], 0)
        self.assertEqual(le.slot_geometry(le.ROWMODE_5x2, 1)[0], 62)
        self.assertIsNone(le.slot_geometry(le.ROWMODE_5x2, 99))

    def test_clock_blocked_slot(self):
        self.assertEqual(le.clock_blocked_slot(le.ROWMODE_5x2, 1), 0)   # left clock
        self.assertEqual(le.clock_blocked_slot(le.ROWMODE_5x2, 2), 1)   # right clock
        self.assertIsNone(le.clock_blocked_slot(le.ROWMODE_5x2, 0))     # centered
        self.assertIsNone(le.clock_blocked_slot(le.ROWMODE_LARGE2, 1))  # large: never


class CompactTemplate(unittest.TestCase):
    def test_companion_pairing(self):
        metrics = [metric(1, "CPU", "%", "load"), metric(2, "CPUT", "C", "temperature"),
                   metric(3, "GPU", "%", "load"), metric(4, "GPUT", "C", "temperature")]
        rm, layout, hidden = le.auto_layout(metrics, "compact")
        self.assertEqual(rm, le.ROWMODE_5x2)
        self.assertEqual(hidden, 0)
        # load metric becomes primary, temperature rides along as companion
        self.assertEqual(layout[1]["companionId"], 2)
        self.assertEqual(layout[3]["companionId"], 4)
        # companions occupy no slot of their own
        self.assertEqual(layout[2]["position"], 255)
        self.assertEqual(layout[4]["position"], 255)
        # primaries take sequential slots
        self.assertEqual(layout[1]["position"], 0)
        self.assertEqual(layout[3]["position"], 1)

    def test_overflow_is_reported(self):
        metrics = [metric(i, "X%d" % i, "", "other") for i in range(1, 14)]
        rm, layout, hidden = le.auto_layout(metrics, "everything")
        self.assertEqual(rm, le.ROWMODE_6x2)  # 13 singles -> 12-slot mode
        self.assertEqual(hidden, 1)            # 13th does not fit


class BarsTemplate(unittest.TestCase):
    def test_percent_and_temp_get_bars(self):
        metrics = [metric(1, "CPU", "%", "load"), metric(2, "CPUT", "C", "temperature")]
        rm, layout, _ = le.auto_layout(metrics, "bars")
        self.assertEqual((layout[1]["position"], layout[1]["barPosition"]), (0, 1))
        self.assertEqual((layout[1]["barMin"], layout[1]["barMax"]), (0, 100))
        self.assertEqual((layout[2]["position"], layout[2]["barPosition"]), (2, 3))


class BigTemplate(unittest.TestCase):
    def test_two_metrics_use_large2(self):
        metrics = [metric(1, "CPU", "%", "load"), metric(2, "GPU", "%", "load")]
        rm, _, _ = le.auto_layout(metrics, "big")
        self.assertEqual(rm, le.ROWMODE_LARGE2)


class DevicePayload(unittest.TestCase):
    def test_bind_by_id_contract(self):
        metrics = [metric(1, "CPU", "%", "load"), metric(2, "CPUT", "C", "temperature")]
        rm, layout, _ = le.auto_layout(metrics, "compact")
        p = le.build_device_layout_json(rm, layout)
        # arrays are MAX_METRICS long, indexed by id-1
        self.assertEqual(len(p["metricPositions"]), MAX_METRICS)
        # names pushed EMPTY so the firmware binds the layout to live metrics by id
        self.assertEqual(p["metricNames"], [""] * MAX_METRICS)
        self.assertEqual(p["displayRowMode"], rm)
        self.assertEqual(p["metricPositions"][0], 0)     # id 1 -> slot 0
        self.assertEqual(p["metricCompanions"][0], 2)    # id 1 companion is id 2

    def test_empty_selection(self):
        rm, layout, hidden = le.auto_layout([], "compact")
        self.assertEqual(hidden, 0)
        p = le.build_device_layout_json(rm, layout)
        self.assertEqual(p["metricPositions"], [255] * MAX_METRICS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
