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
        # names carry the layout labels so the firmware's name guard binds each
        # slot to the metric that actually owns that id (no scramble on drift).
        self.assertEqual(p["metricNames"][0], "CPU")
        self.assertEqual(p["metricNames"][1], "CPUT")
        self.assertEqual(p["displayRowMode"], rm)
        self.assertEqual(p["metricPositions"][0], 0)     # id 1 -> slot 0
        self.assertEqual(p["metricCompanions"][0], 2)    # id 1 companion is id 2

    def test_metric_names_override(self):
        # Explicit UDP display names win over the layout label (e.g. raw name
        # longer than the 10-char label, or custom_label set after a rename).
        rm, layout, _ = le.auto_layout([metric(1, "CPU", "%", "load")], "compact")
        p = le.build_device_layout_json(rm, layout, metric_names={1: "NET_LONG_RAWNAME"})
        self.assertEqual(p["metricNames"][0], "NET_LONG_RAWNAME")

    def test_empty_selection(self):
        rm, layout, hidden = le.auto_layout([], "compact")
        self.assertEqual(hidden, 0)
        p = le.build_device_layout_json(rm, layout)
        self.assertEqual(p["metricPositions"], [255] * MAX_METRICS)


class ParseDeviceLayout(unittest.TestCase):
    def _export(self, names, positions, companions):
        # Minimal /api/export-shaped dict for the fields the parser reads.
        pad = lambda a, d: a + [d] * (MAX_METRICS - len(a))
        return {
            "displayRowMode": 0, "showClock": False, "clockPosition": 0,
            "useRpmKFormat": False, "useNetworkMBFormat": False, "clockOffset": 0,
            "metricNames": pad(list(names), ""),
            "metricLabels": pad(list(names), ""),
            "metricOrder": pad([0] * len(names), 0),
            "metricCompanions": pad(list(companions), 0),
            "metricPositions": pad(list(positions), 255),
            "metricBarPositions": pad([255] * len(names), 255),
            "metricBarMin": pad([0] * len(names), 0),
            "metricBarMax": pad([100] * len(names), 100),
            "metricBarWidths": pad([60] * len(names), 60),
            "metricBarOffsets": pad([0] * len(names), 0),
        }

    def test_roundtrip_with_build(self):
        layout = {
            1: {"label": "CPU", "order": 0, "companionId": 2, "position": 0,
                "barPosition": 255, "barMin": 0, "barMax": 100,
                "barWidth": 60, "barOffsetX": 0},
            3: {"label": "GPU", "order": 2, "companionId": 0, "position": 2,
                "barPosition": 3, "barMin": 20, "barMax": 90,
                "barWidth": 40, "barOffsetX": 10},
        }
        p = le.build_device_layout_json(1, layout, show_clock=True, clock_position=2,
                                        rpm_k=True, net_mb=False, clock_offset=-3)
        # build_device_layout_json blanks metricNames, so bind falls back to labels.
        metrics = [{"id": 1, "name": "CPU"}, {"id": 3, "name": "GPU"}]
        parsed = le.parse_device_layout(p, metrics)
        self.assertEqual(parsed["row_mode"], 1)
        self.assertTrue(parsed["show_clock"])
        self.assertEqual(parsed["clock_position"], 2)
        self.assertEqual(parsed["clock_offset"], -3)
        self.assertEqual(parsed["layout"][3]["barWidth"], 40)
        self.assertEqual(parsed["layout"][3]["barOffsetX"], 10)
        self.assertEqual(parsed["layout"][3]["position"], 2)

    def test_binds_by_name_when_selection_reordered(self):
        # Device configured as CPU,PUMP,CPUTemp with CPU's companion = id3 (CPUTemp).
        data = self._export(
            names=["CPU", "PUMP", "CPUTemp"],
            positions=[0, 2, 255],
            companions=[3, 0, 0],
        )
        # App's CURRENT selection is reordered: CPUTemp moved up to id2.
        metrics = [{"id": 1, "name": "CPU"}, {"id": 2, "name": "CPUTemp"},
                   {"id": 3, "name": "PUMP"}]
        parsed = le.parse_device_layout(data, metrics)
        lay = parsed["layout"]
        # CPU keeps its position and its companion re-points to CPUTemp's NEW id (2).
        self.assertEqual(lay[1]["position"], 0)
        self.assertEqual(lay[1]["companionId"], 2)
        # PUMP (now id3) still gets the device's PUMP slot, not CPUTemp's.
        self.assertEqual(lay[3]["position"], 2)
        self.assertEqual(lay[2]["position"], 255)

    def test_missing_arrays_use_defaults(self):
        # Old firmware / partial export must not crash; defaults fill the gaps.
        parsed = le.parse_device_layout({}, [{"id": 1, "name": "A"}, {"id": 2, "name": "B"}])
        self.assertEqual(parsed["row_mode"], 0)
        self.assertEqual(parsed["layout"][1]["position"], 255)
        self.assertEqual(parsed["layout"][2]["barWidth"], 60)
        self.assertEqual(parsed["layout"][2]["barOffsetX"], 0)


class RemapLayoutByName(unittest.TestCase):
    def test_new_metric_does_not_inherit_predecessor(self):
        # Saved: id2 = GPU placed at slot 4. Now id2 is a NEW metric (PUMP); GPU
        # is gone. PUMP must NOT inherit GPU's slot/label.
        saved = {
            1: {"metricName": "CPU", "label": "CPU", "position": 0, "companionId": 0,
                "barPosition": 255, "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0},
            2: {"metricName": "GPU", "label": "GPU", "position": 4, "companionId": 0,
                "barPosition": 255, "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0},
        }
        metrics = [{"id": 1, "name": "CPU", "label": "CPU"},
                   {"id": 2, "name": "PUMP", "label": "PUMP"}]
        out = le.remap_layout_by_name(saved, metrics)
        self.assertEqual(out[1]["position"], 0)        # CPU keeps its slot
        self.assertEqual(out[2]["position"], 255)      # PUMP starts unplaced
        self.assertEqual(out[2]["metricName"], "PUMP")

    def test_reorder_keeps_entries_and_remaps_companion(self):
        # CPU(id1)+companion CPUTemp(id2). Selection reorders: CPUTemp becomes id3.
        saved = {
            1: {"metricName": "CPU", "label": "CPU", "position": 0, "companionId": 2,
                "barPosition": 255, "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0},
            2: {"metricName": "CPUTemp", "label": "CPUTemp", "position": 255, "companionId": 0,
                "barPosition": 255, "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0},
        }
        metrics = [{"id": 1, "name": "CPU", "label": "CPU"},
                   {"id": 2, "name": "RAM", "label": "RAM"},
                   {"id": 3, "name": "CPUTemp", "label": "CPUTemp"}]
        out = le.remap_layout_by_name(saved, metrics)
        self.assertEqual(out[1]["companionId"], 3)     # companion follows CPUTemp's new id
        self.assertEqual(out[2]["position"], 255)      # RAM (new) is unplaced
        self.assertEqual(out[3]["metricName"], "CPUTemp")

    def test_legacy_entry_without_metricname_falls_back_to_label(self):
        saved = {1: {"label": "CPU", "position": 2, "companionId": 0,
                     "barPosition": 255, "barMin": 0, "barMax": 100, "barWidth": 60, "barOffsetX": 0}}
        out = le.remap_layout_by_name(saved, [{"id": 1, "name": "CPU", "label": "CPU"}])
        self.assertEqual(out[1]["position"], 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
