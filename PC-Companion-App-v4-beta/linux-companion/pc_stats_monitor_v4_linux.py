#!/usr/bin/env python3
"""PC Companion v4 - Linux core.

Same web UI + tray architecture as the Windows app, but with Linux sensor
discovery (psutil + lm-sensors + NVIDIA pynvml + AMD sysfs) and systemd-user
autostart. The shared, OS-neutral modules (server, app_window, app_state,
layout_engine, device_render, webui/) live in ../companion-common; the Linux
sensor backend is vendored as linux_sensors.py (the proven v2 Linux discovery).

Run from source (no build step):
    pip install psutil pywebview pynvml      # pynvml optional (NVIDIA)
    python3 pc_stats_monitor_v4_linux.py
A native window needs the GTK/WebKit libs (Debian/Ubuntu:
    sudo apt install gir1.2-webkit2-4.1 python3-gi); without them the UI opens
in the default browser at http://127.0.0.1:8736 .
"""

import argparse
import json
import os
import socket
import sys
import threading
import time
from datetime import datetime

import psutil

# Shared OS-neutral modules live alongside this folder in ../companion-common.
_HERE = os.path.dirname(os.path.abspath(__file__))
_COMMON_DIR = os.path.join(_HERE, "..", "companion-common")
if os.path.isdir(_COMMON_DIR) and _COMMON_DIR not in sys.path:
    sys.path.insert(0, _COMMON_DIR)

# Vendored Linux sensor backend (discovery + per-metric value reading).
import linux_sensors as lx  # noqa: E402

# Optional system tray (pystray needs AppIndicator/GTK on some desktops).
try:
    import pystray
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except Exception:
    TRAY_AVAILABLE = False

# Windows-only; defined so the shared modules' guards short-circuit cleanly.
PYTHONCOM_AVAILABLE = False
use_rest_api = False  # Linux reads sensors directly; no LHM REST fallback.

# There is only one sensor source here, so the shared web UI hides the picker.
SUPPORTS_SOURCE_SELECT = False

# ---------------------------------------------------------------------------
# Paths / config
# ---------------------------------------------------------------------------
def get_data_dir():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    d = os.path.join(base, "PCStatsMonitor")
    try:
        os.makedirs(d, exist_ok=True)
    except Exception:
        d = _HERE
    return d


DATA_DIR = get_data_dir()
CONFIG_FILE = os.path.join(DATA_DIR, "monitor_config.json")

DEFAULT_CONFIG = {
    "version": "4.0",
    "esp32_ip": "192.168.0.163",
    "udp_port": 4210,
    "update_interval": 3,
    "metrics": [],
}

# Status codes (must match ESP32 config.h)
STATUS_OK = 1
STATUS_API_ERROR = 2
STATUS_LHM_NOT_RUNNING = 3
STATUS_LHM_STARTING = 4
STATUS_UNKNOWN_ERROR = 5

# The shared sensor DB is the vendored backend's dict (same object).
sensor_database = lx.sensor_database


def load_config():
    if not os.path.exists(CONFIG_FILE):
        return None
    try:
        with open(CONFIG_FILE, "r") as f:
            return json.load(f)
    except Exception as e:
        print("Error loading config: %s" % e)
        return None


def save_config(config):
    try:
        with open(CONFIG_FILE, "w") as f:
            json.dump(config, f, indent=2)
        return True
    except Exception as e:
        print("Error saving config: %s" % e)
        return False


# ---------------------------------------------------------------------------
# Sensor discovery + source hooks (consumed by the shared server / app_window)
# ---------------------------------------------------------------------------
def discover_sensors():
    lx.discover_sensors()


def ensure_discovered(rescan=False):
    if any(sensor_database[k] for k in sensor_database) and not rescan:
        return
    for k in list(sensor_database.keys()):
        sensor_database[k] = []
    try:
        lx.discover_sensors()
    except Exception as e:
        print("Discovery error: %s" % e)


def _hw_count():
    return sum(len(v) for k, v in sensor_database.items() if k != "system")


def source_text():
    n = _hw_count()
    return ("lm-sensors / psutil (%d sensors)" % n) if n > 0 else "psutil only (CPU/RAM/disk)"


def source_banner():
    n = _hw_count()
    if n > 0:
        return {"level": "ok", "text": "Hardware sensors available via lm-sensors / psutil - %d sensors." % n}
    return {"level": "warn", "text": "Only CPU / RAM / Disk available. Install lm-sensors (and 'pip install pynvml' for NVIDIA) for temps/fans/GPU, then Rescan."}


def detect_source(state):
    try:
        ensure_discovered(False)
    except Exception:
        pass
    state.set_source_text(source_text())


def is_lhm_process_running():
    return False  # N/A on Linux; only referenced by the (skipped) REST branch.


# ---------------------------------------------------------------------------
# Metric values (delegate to the vendored backend) + UDP payload assembly
# ---------------------------------------------------------------------------
def get_metric_value(metric_config, snapshot=None):
    try:
        return lx.get_metric_value(metric_config)
    except Exception:
        return None


def build_snapshot(config, force=False):
    return None  # Linux reads each sensor live; no per-cycle snapshot needed.


def collect_metrics(config, snapshot, last_good_values=None, status_code=STATUS_OK):
    """Collect values and assemble the UDP payload (pure; no socket).
    Identical policy to the Windows core - shared monitor loop calls it."""
    if last_good_values is None:
        last_good_values = {}
    has_fresh_data = False
    stale_count = 0
    payload = {"version": "2.2", "status": status_code, "timestamp": "", "metrics": []}
    values_by_id = {}
    for metric_config in config["metrics"]:
        value = get_metric_value(metric_config, snapshot)
        metric_id = metric_config["id"]
        if value is not None:
            last_good_values[metric_id] = value
            has_fresh_data = True
        else:
            value = last_good_values.get(metric_id, 0)
            stale_count += 1
        values_by_id[metric_id] = value
        display_name = metric_config.get("custom_label", "") or metric_config["name"]
        payload["metrics"].append({
            "id": metric_id, "name": display_name, "value": value, "unit": metric_config["unit"],
        })
    total = len(config["metrics"])
    if total > 0 and stale_count >= total and status_code == STATUS_OK:
        status_code = STATUS_API_ERROR
    elif stale_count > 0 and stale_count >= total * 0.5 and status_code == STATUS_OK:
        status_code = STATUS_API_ERROR
    payload["status"] = status_code
    payload["timestamp"] = datetime.now().strftime("%H:%M") if has_fresh_data else ""
    return payload, values_by_id, has_fresh_data, last_good_values, stale_count


# ---------------------------------------------------------------------------
# Autostart (systemd --user unit)
# ---------------------------------------------------------------------------
_SERVICE_NAME = "pcstatsmonitor.service"


def _service_path():
    return os.path.expanduser("~/.config/systemd/user/" + _SERVICE_NAME)


def is_autostart_enabled():
    return os.path.exists(_service_path())


def setup_autostart(enable=True):
    import subprocess
    path = _service_path()
    if enable:
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            unit = (
                "[Unit]\n"
                "Description=PC Stats Monitor (SmallOLED companion)\n"
                "After=graphical-session.target\n\n"
                "[Service]\n"
                "Type=simple\n"
                "ExecStart=%s %s --minimized --startup-delay 10\n"
                "Restart=on-failure\n\n"
                "[Install]\n"
                "WantedBy=default.target\n"
            ) % (sys.executable, os.path.abspath(__file__))
            with open(path, "w") as f:
                f.write(unit)
            for cmd in (["daemon-reload"], ["enable", _SERVICE_NAME], ["start", _SERVICE_NAME]):
                subprocess.run(["systemctl", "--user"] + cmd, check=False)
        except Exception as e:
            print("autostart enable failed: %s" % e)
            return False
        return True
    try:
        for cmd in (["disable", _SERVICE_NAME], ["stop", _SERVICE_NAME]):
            subprocess.run(["systemctl", "--user"] + cmd, check=False)
        if os.path.exists(path):
            os.remove(path)
        subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
    except Exception as e:
        print("autostart disable failed: %s" % e)
    return True


# ---------------------------------------------------------------------------
# Tray icon (shared app_window uses it when TRAY_AVAILABLE)
# ---------------------------------------------------------------------------
def create_tray_icon():
    if not TRAY_AVAILABLE:
        return None
    image = Image.new("RGB", (64, 64), color="black")
    dc = ImageDraw.Draw(image)
    dc.rectangle([16, 16, 48, 48], fill="cyan")
    return image


# ---------------------------------------------------------------------------
# Single-instance coordination (socket lock + IPC) - same as the Windows core
# ---------------------------------------------------------------------------
SINGLE_INSTANCE_HOST = "127.0.0.1"
SINGLE_INSTANCE_PORT = 42100
_SINGLE_INSTANCE_MAGIC = b"PCMON1"
_single_instance_sock = None
_reload_event = threading.Event()
_show_event = threading.Event()


def _detect_running_primary():
    try:
        with socket.create_connection((SINGLE_INSTANCE_HOST, SINGLE_INSTANCE_PORT), timeout=1) as conn:
            conn.sendall(b"ping")
            conn.settimeout(1)
            return conn.recv(32).startswith(_SINGLE_INSTANCE_MAGIC)
    except OSError:
        return False


def acquire_single_instance():
    global _single_instance_sock
    if _detect_running_primary():
        return "secondary"
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind((SINGLE_INSTANCE_HOST, SINGLE_INSTANCE_PORT))
        sock.listen(5)
    except OSError:
        sock.close()
        return "standalone"
    _single_instance_sock = sock
    return "primary"


def start_single_instance_listener():
    server = _single_instance_sock
    if server is None:
        return

    def listener():
        while True:
            try:
                conn, _ = server.accept()
            except OSError:
                break
            try:
                conn.settimeout(2)
                data = conn.recv(64) or b""
                if b"reload" in data:
                    _reload_event.set()
                if b"show" in data:
                    _show_event.set()
                conn.sendall(_SINGLE_INSTANCE_MAGIC + b" ok")
            except OSError:
                pass
            finally:
                try:
                    conn.close()
                except OSError:
                    pass

    threading.Thread(target=listener, daemon=True, name="single-instance").start()


def signal_primary_show():
    try:
        with socket.create_connection((SINGLE_INSTANCE_HOST, SINGLE_INSTANCE_PORT), timeout=2) as conn:
            conn.sendall(b"show")
            conn.settimeout(2)
            try:
                conn.recv(32)
            except OSError:
                pass
        return True
    except OSError:
        return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="PC Stats Monitor v4 (Linux)")
    parser.add_argument("--configure", action="store_true", help="Open the configuration window")
    parser.add_argument("--edit", action="store_true", help="Open the configuration window")
    parser.add_argument("--autostart", choices=["enable", "disable"], help="Enable/disable systemd autostart")
    parser.add_argument("--minimized", action="store_true", help="Start hidden in the tray")
    parser.add_argument("--startup-delay", type=int, default=0, help="Delay before starting (seconds)")
    args = parser.parse_args()

    if args.startup_delay > 0:
        print("Waiting %ds for the desktop/services to start..." % args.startup_delay)
        time.sleep(args.startup_delay)

    if args.autostart:
        ok = setup_autostart(args.autostart == "enable")
        print("Autostart %s%s." % (args.autostart + "d", "" if ok else " (failed)"))
        return

    print("\n" + "=" * 60)
    print("  PC STATS MONITOR v4 (Linux) - web UI + tray")
    print("=" * 60 + "\n")

    role = acquire_single_instance()
    if role == "secondary":
        print("PC Monitor is already running - showing its window.")
        signal_primary_show()
        return
    if role == "primary":
        start_single_instance_listener()

    import app_window
    start_hidden = args.minimized and not (args.configure or args.edit)
    app_window.run(sys.modules[__name__], start_hidden=start_hidden, notify_startup=args.minimized)


if __name__ == "__main__":
    main()
