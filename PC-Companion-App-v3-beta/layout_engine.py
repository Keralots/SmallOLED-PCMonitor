"""Auto-layout engine for the PC Companion App.

Pure layout logic (no tkinter, no GUI): turns selected sensors into a
complete device layout and pushes it to the ESP32. Geometry mirrors the
firmware renderer in src/metrics/metrics.cpp. Extracted from
pc_stats_monitor_v3.py; unit-tested by test_layout_engine.py.
"""
import json
from urllib import request as urllib_request

from constants import MAX_METRICS


# ===========================================================================
# Auto-layout engine
#
# Turns the user's selected sensors into a complete device layout (row mode +
# per-metric position / companion / progress-bar settings) so the Python app
# can configure the OLED in one click, with no trip to the device web UI.
#
# Everything here is pure (no GUI) and keyed by metric id (the same id the app
# assigns sequentially and sends over UDP), so the pushed layout binds to the
# live metrics by id. Position math mirrors the firmware renderer in
# src/metrics/metrics.cpp (row-major: position = row*2 + col in grid modes,
# sequential in large modes). See the approved plan for the full mapping.
# ===========================================================================

# Display row modes (must match firmware displayRowMode)
ROWMODE_5x2 = 0   # 5 rows x 2 cols -> 10 slots, normal text
ROWMODE_6x2 = 1   # 6 rows x 2 cols -> 12 slots, normal text
ROWMODE_LARGE2 = 2  # 2 large rows -> 2 slots
ROWMODE_LARGE3 = 3  # 3 large rows -> 3 slots

_ROWMODE_MAXSLOTS = {0: 10, 1: 12, 2: 2, 3: 3}


def slot_count(row_mode):
    return _ROWMODE_MAXSLOTS.get(row_mode, 10)


def slot_geometry(row_mode, slot, show_clock=False, clock_position=0):
    """Device-pixel (x, y, col_width) for a slot, matching metrics.cpp."""
    if slot < 0 or slot >= _ROWMODE_MAXSLOTS.get(row_mode, 10):
        return None
    if row_mode >= 2:
        if show_clock:
            if row_mode == 2:
                start_y, row_h = 12, 32
            else:
                start_y, row_h = 10, 18
        else:
            if row_mode == 2:
                start_y, row_h = 8, 32
            else:
                start_y, row_h = 4, 20
        return (0, start_y + slot * row_h, 128)
    # normal 2-column modes
    if row_mode == 0:
        start_y = 0
        row_h = 11 if (show_clock and clock_position == 0) else 13
    else:
        start_y = 2
        row_h = 10
    if show_clock and clock_position == 0:
        start_y += 10
    row, col = slot // 2, slot % 2
    x = 0 if col == 0 else 62
    col_w = 60 if col == 0 else 64
    return (x, start_y + row * row_h, col_w)


def clock_blocked_slot(row_mode, clock_position):
    """Slot index blocked by a left/right clock, or None."""
    if row_mode >= 2 or clock_position == 0:
        return None
    return 0 if clock_position == 1 else 1


# Template ids (internal) and their human labels (UI dropdown)
LAYOUT_TEMPLATES = [
    ("compact", "Compact (fit the most)"),
    ("bars", "Bars for % and temps"),
    ("big", "Big glance (2-3 large)"),
    ("everything", "Everything (one per slot)"),
]

_GROUP_RANK = {"CPU": 0, "GPU": 1, "RAM": 2, "NET": 3, "DISK": 4, "OTHER": 5}


def _device_key(name):
    """Coarse device/subsystem bucket for a metric, from its short name prefix."""
    n = (name or "").upper()
    if n.startswith("CPU") or n.startswith("CCD"):
        return "CPU"
    if n.startswith(("GPU", "VRAM", "GCLK", "VCLK")):
        return "GPU"
    if n.startswith("RAM"):
        return "RAM"
    if n.startswith(("NET", "NTD")):
        return "NET"
    if n.startswith(("HDD", "SSD", "NVM", "DISK")):
        return "DISK"
    return "OTHER"


def _is_percent(m):
    return m.get("unit") == "%"


def _is_temp(m):
    return m.get("type") == "temperature" or m.get("unit") == "C"


def _is_throughput(m):
    return m.get("unit") == "KB/s"


def _is_memory_amount(m):
    """A RAM/VRAM amount metric (e.g. RAM_GB), used to pair with a RAM % metric."""
    return m.get("type") in ("memory", "data") and m.get("unit") in ("GB", "MB")


def _metric_rank(m):
    """Sort key for placement/importance: group first, then percent < temp < other."""
    group = _GROUP_RANK.get(_device_key(m.get("name", "")), 5)
    if _is_percent(m):
        sub = 0
    elif _is_temp(m):
        sub = 1
    else:
        sub = 2
    return (group, sub)


def _bar_bounds(m):
    """(min, max) for a progress bar on this metric, or None if a bar makes no sense."""
    if _is_percent(m):
        return (0, 100)
    if _is_temp(m):
        return (0, 100)
    return None


def _throughput_direction(name):
    """'up', 'down', or '' inferred from a throughput metric's short name."""
    n = (name or "").upper()
    if n.endswith("_U") or "UP" in n or "SENT" in n or "TX" in n:
        return "up"
    if n.endswith("_D") or "DOWN" in n or "RECV" in n or "RX" in n:
        return "down"
    return ""


def _find_companion(primary, group_metrics, used_ids):
    """Pick a natural companion for `primary` from the same device group.

    Rules: a load/% metric pairs with a temperature (else a RAM amount); an
    upload throughput pairs with the matching download (else any other
    throughput in the group). Returns the companion dict or None.
    """
    candidates = [m for m in group_metrics
                  if m["id"] != primary["id"] and m["id"] not in used_ids]

    if _is_percent(primary):
        for m in candidates:
            if _is_temp(m):
                return m
        for m in candidates:
            if _is_memory_amount(m):
                return m
        return None

    if _is_throughput(primary):
        want = "down" if _throughput_direction(primary["name"]) == "up" else None
        if want:
            for m in candidates:
                if _is_throughput(m) and _throughput_direction(m["name"]) == want:
                    return m
        for m in candidates:
            if _is_throughput(m):
                return m
        return None

    return None


def _pair_companions(metrics):
    """Group metrics into [(primary, companion_or_None), ...] in rank order.

    `metrics` is expected pre-sorted by _metric_rank so the load/% metric in a
    group is seen before its temperature and becomes the primary.
    """
    by_group = {}
    for m in metrics:
        by_group.setdefault(_device_key(m.get("name", "")), []).append(m)

    used = set()
    units = []
    for m in metrics:
        if m["id"] in used:
            continue
        companion = _find_companion(m, by_group[_device_key(m.get("name", ""))], used)
        used.add(m["id"])
        if companion:
            used.add(companion["id"])
        units.append((m, companion))
    return units


def _default_entry(m):
    """A hidden-by-default layout entry for one metric."""
    return {
        "label": (m.get("label") or m.get("name") or "")[:10],
        "order": 0,
        "position": 255,
        "companionId": 0,
        "barPosition": 255,
        "barMin": 0,
        "barMax": 100,
        "barWidth": 60,
        "barOffsetX": 0,
    }


def _layout_singles(metrics, layout, pair):
    """Compact/Everything: one slot per unit (a unit is a metric, optionally with
    a companion riding along in the same slot). No progress bars."""
    units = _pair_companions(metrics) if pair else [(m, None) for m in metrics]
    n_units = len(units)
    row_mode = ROWMODE_5x2 if n_units <= _ROWMODE_MAXSLOTS[ROWMODE_5x2] else ROWMODE_6x2
    maxslots = _ROWMODE_MAXSLOTS[row_mode]

    hidden = 0
    slot = 0
    for primary, companion in units:
        if slot >= maxslots:
            hidden += 1 + (1 if companion else 0)
            continue
        e = layout[primary["id"]]
        e["position"] = slot
        e["order"] = slot
        if companion:
            e["companionId"] = companion["id"]  # companion stays hidden (own slot 255)
        slot += 1
    return row_mode, layout, hidden


def _layout_bars(metrics, layout):
    """Bars template: each %/temperature metric takes a full row -> text in the
    left column + its own progress bar in the right column. Other metrics fill
    remaining single slots. No companions (a bar already conveys the value)."""
    bar_metrics = [m for m in metrics if _bar_bounds(m) is not None]
    other = [m for m in metrics if _bar_bounds(m) is None]

    slots_needed = len(bar_metrics) * 2 + len(other)
    row_mode = ROWMODE_5x2 if slots_needed <= _ROWMODE_MAXSLOTS[ROWMODE_5x2] else ROWMODE_6x2
    maxslots = _ROWMODE_MAXSLOTS[row_mode]

    hidden = 0
    slot = 0
    for m in bar_metrics:
        left = slot if slot % 2 == 0 else slot + 1  # snap text to a row's left column
        if left + 1 >= maxslots:
            hidden += 1
            continue
        e = layout[m["id"]]
        e["position"] = left
        e["barPosition"] = left + 1
        bmin, bmax = _bar_bounds(m)
        e["barMin"], e["barMax"] = bmin, bmax
        e["order"] = left
        slot = left + 2

    for m in other:
        if slot >= maxslots:
            hidden += 1
            continue
        e = layout[m["id"]]
        e["position"] = slot
        e["order"] = slot
        slot += 1
    return row_mode, layout, hidden


def _layout_big(metrics, layout):
    """Big glance: 2-3 most important metrics in large text, each with a companion
    (e.g. its temperature) right-aligned on the same row. No bars."""
    units = _pair_companions(metrics)
    row_mode = ROWMODE_LARGE2 if len(units) <= 2 else ROWMODE_LARGE3
    maxslots = _ROWMODE_MAXSLOTS[row_mode]

    hidden = 0
    slot = 0
    for primary, companion in units:
        if slot >= maxslots:
            hidden += 1 + (1 if companion else 0)
            continue
        e = layout[primary["id"]]
        e["position"] = slot
        e["order"] = slot
        if companion:
            e["companionId"] = companion["id"]
        slot += 1
    return row_mode, layout, hidden


def auto_layout(metrics, template):
    """Build a device layout from selected metrics.

    `metrics`: list of dicts, each with id, name, type, unit, label (and
    optionally current_value). Returns (row_mode, layout_by_id, hidden_count)
    where layout_by_id[id] holds position/companionId/bar* for that metric.
    """
    ranked = sorted(metrics, key=_metric_rank)
    layout = {m["id"]: _default_entry(m) for m in ranked}

    if not ranked:
        return ROWMODE_5x2, layout, 0
    if template == "bars":
        return _layout_bars(ranked, layout)
    if template == "big":
        return _layout_big(ranked, layout)
    if template == "everything":
        return _layout_singles(ranked, layout, pair=False)
    return _layout_singles(ranked, layout, pair=True)  # compact (default)


def build_device_layout_json(row_mode, layout_by_id, show_clock=False, clock_position=0):
    """Assemble the /api/import payload from a layout.

    Arrays are MAX_METRICS long, indexed by (id-1). metricNames is pushed EMPTY
    so the firmware binds the layout to whatever names arrive on those ids on
    the next UDP packet (initial-bind-by-id).
    """
    n = MAX_METRICS
    payload = {
        "displayRowMode": row_mode,
        "showClock": show_clock,
        "clockPosition": clock_position,
        "metricLabels": [""] * n,
        "metricNames": [""] * n,
        "metricOrder": [0] * n,
        "metricCompanions": [0] * n,
        "metricPositions": [255] * n,
        "metricBarPositions": [255] * n,
        "metricBarMin": [0] * n,
        "metricBarMax": [100] * n,
        "metricBarWidths": [60] * n,
        "metricBarOffsets": [0] * n,
    }
    for mid, e in layout_by_id.items():
        idx = mid - 1
        if idx < 0 or idx >= n:
            continue
        payload["metricLabels"][idx] = (e.get("label") or "")[:10]
        payload["metricOrder"][idx] = e.get("order", 0)
        payload["metricCompanions"][idx] = e.get("companionId", 0)
        payload["metricPositions"][idx] = e.get("position", 255)
        payload["metricBarPositions"][idx] = e.get("barPosition", 255)
        payload["metricBarMin"][idx] = e.get("barMin", 0)
        payload["metricBarMax"][idx] = e.get("barMax", 100)
        payload["metricBarWidths"][idx] = e.get("barWidth", 60)
        payload["metricBarOffsets"][idx] = e.get("barOffsetX", 0)
    return payload


def push_layout_to_device(esp32_ip, payload, timeout=4):
    """POST the layout JSON to the device's /api/import. Returns (ok, detail).

    Raises urllib/OSError on a network failure (the caller handles it).
    """
    url = f"http://{esp32_ip}/api/import"
    data = json.dumps(payload).encode("utf-8")
    req = urllib_request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    with urllib_request.urlopen(req, timeout=timeout) as resp:
        body = resp.read().decode("utf-8", "replace")
        ok = resp.status == 200 and '"success":true' in body.replace(" ", "")
        return ok, body


_ROWMODE_LABELS = [
    (ROWMODE_5x2, "5 rows x 2 cols"),
    (ROWMODE_6x2, "6 rows x 2 cols"),
    (ROWMODE_LARGE2, "Large (2 rows)"),
    (ROWMODE_LARGE3, "Large (3 rows)"),
]


