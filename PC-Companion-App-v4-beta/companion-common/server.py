"""Localhost HTTP server for the v4 web UI.

Serves the extracted ESP portal (webui/) and implements the same REST contract
its JavaScript already calls (/metrics, /save, /api/export, /api/import,
/api/status) plus the PC-only endpoints the device portal has no reason to have
(/api/sensors, /api/select, /api/connection, /api/test, /api/autostart).

`core` is the live pc_stats_monitor_v4 module object, passed in by app_window so
we share its globals (sensor_database, use_rest_api, ...) instead of importing a
second copy. All shared mutable state goes through the AppState `state` (RLock).
"""

import copy
import json
import os
import socket
import sys
import threading
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

from constants import MAX_METRICS
from layout_engine import (
    auto_layout,
    build_device_layout_json,
    fetch_device_export,
    parse_device_layout,
    push_layout_to_device,
    remap_layout_by_name,
)

_CTYPES = {".html": "text/html; charset=utf-8",
           ".css": "text/css; charset=utf-8",
           ".js": "application/javascript; charset=utf-8"}


def webui_dir():
    """Bundled assets live next to this file in script mode, and in the
    PyInstaller _MEIPASS extraction dir when frozen - NOT in get_data_dir()
    (that is the writable %APPDATA% config/log dir, no assets there)."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, "webui")


# ---------------------------------------------------------------------------
# Sensor discovery / identity
# ---------------------------------------------------------------------------
def sensor_key(s):
    """Stable identity for a sensor across rescans (matches v3)."""
    return s.get("wmi_identifier") or "%s_%s" % (s.get("source", ""), s.get("display_name", ""))


# These three delegate to the platform core (Windows / Linux implement them), so
# this file stays OS-neutral and shared across both companion folders.
def ensure_discovered(core, rescan=False):
    core.ensure_discovered(rescan)


def source_text(core):
    return core.source_text()


def source_banner(core):
    return core.source_banner()


def flatten_sensors(core, selected_keys, placed_keys=None):
    """All discovered sensors as flat dicts for the picker, in category order.
    `placed_keys`: keys of sensors whose metric has a slot on the device screen
    (layout position != 255) - used to colour the 'selected' chips."""
    placed_keys = placed_keys or set()
    out = []
    order = ["system", "gpu", "temperature", "fan", "load", "clock", "power", "data", "throughput", "other"]
    for cat in order:
        for s in core.sensor_database.get(cat, []):
            k = sensor_key(s)
            out.append({
                "key": k,
                "name": s.get("name", ""),
                "display_name": s.get("display_name", s.get("name", "")),
                "unit": s.get("unit", ""),
                "type": s.get("type", ""),
                "source": s.get("source", ""),
                "category": cat,
                "current_value": s.get("current_value", 0),
                "active": bool(s.get("is_active_nic")),
                "selected": k in selected_keys,
                "placed": k in placed_keys,
            })
    return out


def placed_sensor_keys(config):
    """Keys of sensors actually visible on the device screen: those with a slot
    (position != 255) AND those shown as a companion of a placed metric (companions
    have no slot of their own but still render next to their primary)."""
    _rm, layout, _sc, _cp, _r, _n, _co = resolve_layout(config)
    by_id = {m["id"]: m for m in config.get("metrics", [])}

    def keyof(mid):
        m = by_id.get(mid)
        if not m:
            return None
        return m.get("wmi_identifier") or "%s_%s" % (m.get("source", ""), m.get("display_name", ""))

    keys = set()
    for mid, e in layout.items():
        if e.get("position", 255) != 255:
            k = keyof(mid)
            if k:
                keys.add(k)
            comp = e.get("companionId", 0)  # the companion is visible too
            if comp:
                ck = keyof(comp)
                if ck:
                    keys.add(ck)
    return keys


def find_sensor_by_key(core, key):
    for cat in core.sensor_database:
        for s in core.sensor_database[cat]:
            if sensor_key(s) == key:
                return s
    return None


# ---------------------------------------------------------------------------
# Layout: bind the saved/auto layout to the config's current metric ids
# ---------------------------------------------------------------------------
def _layout_input(config):
    metrics = []
    for m in config.get("metrics", [])[:MAX_METRICS]:
        metrics.append({
            "id": m["id"],
            "name": m.get("name", ""),
            "type": m.get("type", ""),
            "unit": m.get("unit", ""),
            "label": (m.get("custom_label") or m.get("name") or ""),
            "current_value": m.get("current_value", 0),
            "is_active_nic": m.get("is_active_nic", False),
        })
    return metrics


def resolve_layout(config):
    """Return (row_mode, layout_by_id, show_clock, clock_position, rpm_k, net_mb,
    clock_offset). Reuses the saved layout (rebound by name) or auto-layout."""
    metrics = _layout_input(config)
    saved = config.get("layout")
    if isinstance(saved, dict) and isinstance(saved.get("layout"), dict):
        inner = {}
        for k, v in saved["layout"].items():
            try:
                inner[int(k)] = v
            except (ValueError, TypeError):
                continue
        layout = remap_layout_by_name(copy.deepcopy(inner), metrics)
        return (int(saved.get("row_mode", 0)), layout,
                bool(saved.get("show_clock", False)), int(saved.get("clock_position", 0)),
                saved.get("rpm_k"), saved.get("net_mb"), saved.get("clock_offset"))
    row_mode, layout, _ = auto_layout(metrics, "compact")
    return row_mode, layout, False, 0, None, None, None


def metrics_payload(core, state):
    """The /metrics response: ESP-shaped metric list + display block + values."""
    config = state.get_config()
    values = state.get_values()
    row_mode, layout, show_clock, clock_pos, rpm_k, net_mb, clock_off = resolve_layout(config)
    by_id = {m["id"]: m for m in config.get("metrics", [])}
    out = []
    for mid, e in layout.items():
        m = by_id.get(mid)
        if not m:
            continue
        val = values.get(mid)
        if val is None:
            val = int(m.get("current_value", 0) or 0)
        out.append({
            "id": mid,
            "name": m.get("name", ""),
            "label": (e.get("label") or m.get("custom_label") or "")[:10],
            "unit": m.get("unit", ""),
            "value": int(val),
            "displayOrder": e.get("order", 0),
            "companionId": e.get("companionId", 0),
            "position": e.get("position", 255),
            "barPosition": e.get("barPosition", 255),
            "barMin": e.get("barMin", 0),
            "barMax": e.get("barMax", 100),
            "barWidth": e.get("barWidth", 60),
            "barOffsetX": e.get("barOffsetX", 0),
        })
    out.sort(key=lambda x: x["displayOrder"])
    return {
        "time": datetime.now().strftime("%H:%M"),
        "display": {
            "rowMode": row_mode, "showClock": show_clock, "clockPosition": clock_pos,
            "clockOffset": clock_off or 0, "rpmK": bool(rpm_k), "netMB": bool(net_mb),
        },
        "metrics": out,
    }


# ---------------------------------------------------------------------------
# Mutating actions
# ---------------------------------------------------------------------------
def apply_select(core, state, keys):
    """Rebuild config.metrics from the chosen sensor keys, preserving any custom
    label already set for a still-selected sensor (keyed by identity). Layout is
    left intact - it rebinds by name on the next /save and /metrics."""
    ensure_discovered(core)
    config = state.get_config()
    prev_label = {}
    for m in config.get("metrics", []):
        prev_label[m.get("wmi_identifier") or "%s_%s" % (m.get("source", ""), m.get("display_name", ""))] = m.get("custom_label", "")
    metrics = []
    for i, key in enumerate(keys[:MAX_METRICS]):
        s = find_sensor_by_key(core, key)
        if not s:
            continue
        m = copy.deepcopy(s)
        m["id"] = len(metrics) + 1
        lbl = prev_label.get(key, "")
        if lbl:
            m["custom_label"] = lbl[:10]
        metrics.append(m)
    config["metrics"] = metrics
    # In-memory only (the monitor reflects it live for the preview); persisted to
    # disk on Save & push, or discarded by Revert. Avoids surprise auto-saves.
    state.set_config(config)
    return {"success": True, "count": len(metrics)}


def apply_save(core, state, form):
    """Persist layout/labels from the editor form, copy label_N back to each
    metric's custom_label (the UDP name), then best-effort push to the device."""
    config = state.get_config()
    metrics = config.get("metrics", [])

    def f(name, default=""):
        v = form.get(name)
        return v[0] if isinstance(v, list) and v else default

    row_mode = int(f("rowMode", "0") or 0)
    show_clock = "showClock" in form
    clock_position = int(f("clockPosition", "0") or 0)
    clock_offset = max(-20, min(20, int(f("clockOffset", "0") or 0)))  # device caps at +/-20
    rpm_k = "rpmKFormat" in form       # Number formats (real checkboxes)
    net_mb = "netMBFormat" in form

    layout = {}
    for m in metrics:
        mid = m["id"]
        label = (f("label_%d" % mid, "") or "").strip()[:10]
        if label:
            m["custom_label"] = label          # r2-5: UDP sender uses custom_label
        elif "custom_label" in m:
            m["custom_label"] = ""
        layout[mid] = {
            "label": label or m.get("name", ""),
            "metricName": m.get("name", ""),
            "order": int(f("order_%d" % mid, "0") or 0),
            "companionId": int(f("companion_%d" % mid, "0") or 0),
            "position": int(f("position_%d" % mid, "255") or 255),
            "barPosition": int(f("barPosition_%d" % mid, "255") or 255),
            "barMin": int(f("barMin_%d" % mid, "0") or 0),
            "barMax": int(f("barMax_%d" % mid, "100") or 100),
            "barWidth": int(f("barWidth_%d" % mid, "60") or 60),
            "barOffsetX": int(f("barOffset_%d" % mid, "0") or 0),
        }

    config["layout"] = {
        "row_mode": row_mode, "layout": layout, "source": "custom",
        "show_clock": show_clock, "clock_position": clock_position,
        "rpm_k": rpm_k, "net_mb": net_mb,
        "clock_offset": clock_offset,
    }
    config["metrics"] = metrics
    core.save_config(config)
    state.set_config(config)

    # Push to the device so the screen matches. Best-effort and fire-and-forget:
    # a save must not block on a ~4s connect timeout when the device is offline.
    names = {m["id"]: (m.get("custom_label") or m.get("name") or "") for m in metrics}
    payload = build_device_layout_json(
        row_mode, layout, show_clock, clock_position,
        rpm_k=config["layout"]["rpm_k"], net_mb=config["layout"]["net_mb"],
        clock_offset=config["layout"]["clock_offset"], metric_names=names)
    ip = config.get("esp32_ip", "")

    def _push():
        try:
            push_layout_to_device(ip, payload, timeout=4)
        except Exception as e:
            print("Layout push skipped (device unreachable?): %s" % e)
    threading.Thread(target=_push, daemon=True, name="layout-push").start()
    return {"success": True, "networkChanged": False}


def apply_connection(core, state, form):
    def f(name, default=""):
        v = form.get(name)
        return v[0] if isinstance(v, list) and v else default
    ip = f("esp32_ip", "").strip()
    try:
        port = int(f("udp_port", "4210"))
        interval = float(f("update_interval", "3"))
        if not ip:
            raise ValueError("Device IP cannot be empty.")
        if port < 1 or port > 65535:
            raise ValueError("Port must be 1-65535.")
        if interval < 0.5:
            raise ValueError("Update interval must be at least 0.5 seconds.")
    except ValueError as e:
        return {"success": False, "message": str(e)}
    config = state.get_config()
    config["esp32_ip"] = ip
    config["udp_port"] = port
    config["update_interval"] = interval
    core.save_config(config)
    state.set_config(config)
    return {"success": True, "esp32_ip": ip, "udp_port": port, "update_interval": interval}


def do_test(core, state, form):
    def f(name, default=""):
        v = form.get(name)
        return v[0] if isinstance(v, list) and v else default
    ip = f("esp32_ip", "").strip()
    try:
        port = int(f("udp_port", "4210"))
    except ValueError:
        port = 4210
    if not ip:
        return {"reachable": False, "message": "Enter the device IP first."}
    reachable, detail = False, ""
    try:
        with socket.create_connection((ip, 80), timeout=2):
            reachable = True
    except OSError as e:
        detail = str(e)
    try:
        us = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        us.sendto(b'{"ping":1}', (ip, port))
        us.close()
    except OSError:
        pass
    state.set_reachable(reachable)
    if reachable:
        return {"reachable": True, "message": "Reachable at %s (web port 80 responded; UDP probe sent to %d)." % (ip, port)}
    return {"reachable": False, "message": "Could not reach %s on port 80. %s Check power, network/subnet, and the IP." % (ip, detail)}


def apply_revert(core, state):
    """Discard unsaved in-memory changes by reloading the last-saved config from
    disk (or defaults if none)."""
    cfg = core.load_config() or dict(core.DEFAULT_CONFIG)
    state.set_config(cfg)
    return {"success": True}


def apply_pull(core, state):
    """Pull the device's current layout (GET /api/export) and bind it to the
    app's CURRENT metrics BY NAME (parse_device_layout), so positions/bars/
    companions match what the device is actually showing. Mirrors v3's
    'Pull from device'."""
    config = state.get_config()
    ip = config.get("esp32_ip", "")
    if not ip:
        return {"success": False, "message": "Set the device IP on the Connection page first."}
    try:
        data = fetch_device_export(ip, timeout=4)
    except Exception as e:
        return {"success": False, "message": "Could not reach the device at %s (%s)." % (ip, e)}
    parsed = parse_device_layout(data, _layout_input(config))
    config["layout"] = {
        "row_mode": parsed["row_mode"], "layout": parsed["layout"], "source": "device",
        "show_clock": parsed["show_clock"], "clock_position": parsed["clock_position"],
        "rpm_k": parsed["rpm_k"], "net_mb": parsed["net_mb"], "clock_offset": parsed["clock_offset"],
    }
    core.save_config(config)
    state.set_config(config)
    return {"success": True, "message": "Pulled the current layout from the device."}


def apply_template(core, state, key):
    """Apply a v3 quick-layout template (compact/bars/big/everything) to the
    selected metrics via auto_layout, preserving clock/format toggles. Saved
    locally; the user clicks Save & push to send it to the device."""
    config = state.get_config()
    metrics = _layout_input(config)
    if not metrics:
        return {"success": False, "message": "Select some sensors first."}
    if key not in ("compact", "bars", "big", "everything"):
        key = "compact"
    row_mode, layout, _hidden = auto_layout(metrics, key)
    prev = config.get("layout") if isinstance(config.get("layout"), dict) else {}
    config["layout"] = {
        "row_mode": row_mode, "layout": layout, "source": key,
        "show_clock": prev.get("show_clock", False), "clock_position": prev.get("clock_position", 0),
        "rpm_k": prev.get("rpm_k"), "net_mb": prev.get("net_mb"), "clock_offset": prev.get("clock_offset"),
    }
    core.save_config(config)
    state.set_config(config)
    return {"success": True, "message": "Applied the '%s' template - review, then Save & push." % key}


def apply_import(core, state, cfg):
    if not isinstance(cfg, dict) or "metrics" not in cfg:
        return {"success": False, "message": "Not a PC Monitor configuration (no metrics)."}
    config = state.get_config()
    out = {
        "version": "4.0",
        "esp32_ip": cfg.get("esp32_ip", config.get("esp32_ip", "")),
        "udp_port": int(cfg.get("udp_port", config.get("udp_port", 4210))),
        "update_interval": float(cfg.get("update_interval", config.get("update_interval", 3))),
        "metrics": cfg.get("metrics", []),
    }
    # Re-number ids 1..N so the layout binds cleanly.
    for i, m in enumerate(out["metrics"][:MAX_METRICS]):
        m["id"] = i + 1
    out["metrics"] = out["metrics"][:MAX_METRICS]
    if isinstance(cfg.get("layout"), dict):
        out["layout"] = cfg["layout"]
    core.save_config(out)
    state.set_config(out)
    return {"success": True, "message": "Configuration imported."}


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------
class _Handler(BaseHTTPRequestHandler):
    CORE = None
    STATE = None

    def log_message(self, *a):
        pass  # quiet; the app has its own logging

    # -- helpers --
    def _json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _asset(self, name):
        path = os.path.join(webui_dir(), name)
        if not os.path.isfile(path):
            self.send_error(404, "not found")
            return
        with open(path, "rb") as fh:
            body = fh.read()
        ext = os.path.splitext(name)[1].lower()
        self.send_response(200)
        self.send_header("Content-Type", _CTYPES.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        try:
            n = int(self.headers.get("Content-Length", 0))
        except (TypeError, ValueError):
            n = 0
        return self.rfile.read(n) if n > 0 else b""

    def _form(self):
        return parse_qs(self._body().decode("utf-8", "replace"))

    def _jsonbody(self):
        try:
            return json.loads(self._body().decode("utf-8", "replace"))
        except Exception:
            return None

    # -- routes --
    def do_GET(self):
        core, state = self.CORE, self.STATE
        path = urlparse(self.path).path
        qs = parse_qs(urlparse(self.path).query)
        try:
            if path in ("/", "/index.html"):
                return self._asset("index.html")
            if path == "/portal.css":
                return self._asset("portal.css")
            if path == "/portal.js":
                return self._asset("portal.js")
            if path == "/metrics":
                return self._json(metrics_payload(core, state))
            if path == "/api/status":
                return self._json(state.status())
            if path == "/api/info":
                return self._json({"version": "4.0", "ip": state.get_config().get("esp32_ip", "")})
            if path == "/api/sensors":
                ensure_discovered(core, rescan=("rescan" in qs))
                state.set_source_text(source_text(core))
                cfg = state.get_config()
                sel = {m.get("wmi_identifier") or "%s_%s" % (m.get("source", ""), m.get("display_name", ""))
                       for m in cfg.get("metrics", [])}
                placed = placed_sensor_keys(cfg)
                return self._json({"sensors": flatten_sensors(core, sel, placed), "max": MAX_METRICS,
                                   "banner": source_banner(core), "source": source_text(core)})
            if path == "/api/export":
                return self._json(state.get_config())
            if path == "/api/autostart":
                return self._json({"enabled": bool(core.is_autostart_enabled())})
            self.send_error(404, "not found")
        except Exception as e:
            self._json({"success": False, "message": str(e)}, 500)

    def do_POST(self):
        core, state = self.CORE, self.STATE
        path = urlparse(self.path).path
        try:
            if path == "/save":
                return self._json(apply_save(core, state, self._form()))
            if path == "/api/select":
                data = self._jsonbody() or {}
                return self._json(apply_select(core, state, data.get("keys", [])))
            if path == "/api/connection":
                return self._json(apply_connection(core, state, self._form()))
            if path == "/api/test":
                return self._json(do_test(core, state, self._form()))
            if path == "/api/import":
                return self._json(apply_import(core, state, self._jsonbody()))
            if path == "/api/pull":
                return self._json(apply_pull(core, state))
            if path == "/api/revert":
                return self._json(apply_revert(core, state))
            if path == "/api/template":
                return self._json(apply_template(core, state, self._form().get("key", ["compact"])[0]))
            if path == "/api/autostart":
                enable = (self._form().get("enable", ["0"])[0] == "1")
                try:
                    core.setup_autostart(enable)
                except Exception as e:
                    print("Autostart toggle failed: %s" % e)
                return self._json({"enabled": bool(core.is_autostart_enabled())})
            self.send_error(404, "not found")
        except Exception as e:
            self._json({"success": False, "message": str(e)}, 500)


def make_server(core, state, host="127.0.0.1", port=8736):
    """Build (and bind) the HTTP server. Falls back to an ephemeral port if the
    preferred one is taken. Returns (httpd, port)."""
    _Handler.CORE = core
    _Handler.STATE = state
    try:
        httpd = ThreadingHTTPServer((host, port), _Handler)
    except OSError:
        httpd = ThreadingHTTPServer((host, 0), _Handler)
        port = httpd.server_address[1]
    httpd.daemon_threads = True
    return httpd, port


def serve(httpd):
    threading.Thread(target=httpd.serve_forever, daemon=True, name="http-server").start()
