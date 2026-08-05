"""v4 lifecycle: one persistent process.

pywebview owns the main thread (required on Windows); the HTTP server, the UDP
monitor loop, the system-tray icon and the single-instance listener all run on
background threads. Closing the window hides it to the tray; tray "Quit" is the
real exit. Worker threads never poke the window directly - they go through the
gui_* chokepoint (the EdgeChromium backend marshals show/hide/destroy onto the
GUI thread internally).
"""

import json
import socket
import threading
import time
import webbrowser

from app_state import AppState
import server as srv

try:
    import webview
    WEBVIEW_AVAILABLE = True
except Exception:
    webview = None
    WEBVIEW_AVAILABLE = False

_window = None
_quitting = False
_stop = threading.Event()
_httpd = None
_core = None
_state = None


# ---------------------------------------------------------------------------
# GUI chokepoint - the only place window methods are called
# ---------------------------------------------------------------------------
def gui_show():
    if _window is None:
        return
    try:
        _window.show()
        try:
            _window.restore()  # un-minimize if needed
        except Exception:
            pass
    except Exception as e:
        print("window show failed: %s" % e)


def gui_hide():
    if _window is None:
        return
    try:
        _window.hide()
    except Exception as e:
        print("window hide failed: %s" % e)


def gui_quit():
    global _quitting
    _quitting = True
    _stop.set()
    if _httpd is not None:
        try:
            _httpd.shutdown()
        except Exception:
            pass
    if _window is not None:
        try:
            _window.destroy()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# JS bridge: native save/open dialogs (a Blob <a download> is a no-op inside the
# embedded WebView2, so Export/Import in the window go through here; the browser
# fallback in portal.js handles plain-browser mode).
# ---------------------------------------------------------------------------
_FILE_TYPES = ('JSON files (*.json)', 'All files (*.*)')


class _JsApi:
    def save_text(self, text, name):
        try:
            res = _window.create_file_dialog(
                webview.SAVE_DIALOG, save_filename=(name or "config.json"), file_types=_FILE_TYPES)
            if not res:
                return {"ok": False, "cancelled": True}
            path = res if isinstance(res, str) else res[0]
            with open(path, "w", encoding="utf-8") as f:
                f.write(text or "")
            return {"ok": True, "path": path}
        except Exception as e:
            return {"ok": False, "error": str(e)}

    def open_text(self):
        try:
            res = _window.create_file_dialog(
                webview.OPEN_DIALOG, allow_multiple=False, file_types=_FILE_TYPES)
            if not res:
                return {"ok": False, "cancelled": True}
            path = res[0] if isinstance(res, (list, tuple)) else res
            with open(path, "r", encoding="utf-8") as f:
                return {"ok": True, "text": f.read(), "path": path}
        except Exception as e:
            return {"ok": False, "error": str(e)}


# ---------------------------------------------------------------------------
# Sensor source detection (for the monitor) - REST first, then WMI
# ---------------------------------------------------------------------------
def _detect_source():
    # Platform-specific: the core probes its sensor source and sets the status
    # text (Windows: REST/WMI; Linux: psutil/lm-sensors availability).
    try:
        _core.detect_source(_state)
    except Exception as e:
        print("source detection failed: %s" % e)


# ---------------------------------------------------------------------------
# UDP monitor loop (reads the live config from AppState each tick)
# ---------------------------------------------------------------------------
def _monitor_loop():
    core = _core
    if core.PYTHONCOM_AVAILABLE:
        try:
            core.pythoncom.CoInitialize()
        except Exception:
            pass
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        core.psutil.cpu_percent(interval=1)
    except Exception:
        pass
    last_good = {}
    gen = _state.generation()
    last_lhm_check = 0.0
    last_reach_check = 0.0

    while not _stop.is_set():
        cfg = _state.get_config()
        g = _state.generation()
        if g != gen:
            gen = g
            last_good = {}
        metrics = cfg.get("metrics") or []
        if not metrics or not cfg.get("esp32_ip"):
            _state.set_monitoring(False)
            _stop.wait(1.0)
            continue
        _state.set_monitoring(True)
        now = time.time()

        status = core.STATUS_OK
        if core.use_rest_api and not core.lhm_health_monitor.is_healthy:
            status = core.STATUS_API_ERROR if core.is_lhm_process_running() else core.STATUS_LHM_NOT_RUNNING
            if now - last_lhm_check >= 5:
                last_lhm_check = now
                if core.is_lhm_process_running():
                    ok, _c, _e = core.check_rest_api_connectivity(core.rest_api_host, core.rest_api_port)
                    if ok:
                        core.lhm_health_monitor.record_success()
                        status = core.STATUS_OK

        try:
            snapshot = core.build_snapshot(cfg)
            payload, values, _fresh, last_good, _stale = core.collect_metrics(cfg, snapshot, last_good, status)
            sock.sendto(json.dumps(payload).encode("utf-8"), (cfg["esp32_ip"], cfg["udp_port"]))
            _state.update_values(values)
        except Exception as e:
            print("send error: %s" % e)

        # Light reachability probe for the status readout (every ~10s).
        if now - last_reach_check >= 10:
            last_reach_check = now
            try:
                with socket.create_connection((cfg["esp32_ip"], 80), timeout=0.5):
                    _state.set_reachable(True)
            except OSError:
                _state.set_reachable(False)

        # Pace on a deadline, not a fixed sleep: sleeping the full interval after
        # the work made the real period `work + interval` (a 1s setting sent every
        # ~2.5s once the sensor sweep is counted). Subtract the elapsed work so the
        # period is the interval, falling back to a small floor when a cycle
        # overruns so a slow sensor source can't spin us into a tight send loop.
        interval = max(0.2, float(cfg.get("update_interval", 3)))
        _stop.wait(max(0.05, interval - (time.time() - now)))

    try:
        sock.close()
    except Exception:
        pass
    if core.PYTHONCOM_AVAILABLE:
        try:
            core.pythoncom.CoUninitialize()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Tray + single-instance show watcher
# ---------------------------------------------------------------------------
def _make_tray():
    core = _core
    if not core.TRAY_AVAILABLE:
        return None
    img = core.create_tray_icon()

    def on_configure(icon, item):
        gui_show()

    def on_quit(icon, item):
        try:
            icon.stop()
        except Exception:
            pass
        gui_quit()

    return core.pystray.Icon(
        "pc_monitor", img, "PC Monitor (v4)",
        menu=core.pystray.Menu(
            core.pystray.MenuItem("Configure", on_configure, default=True),
            core.pystray.MenuItem("Quit", on_quit),
        ),
    )


def _start_background(tray_icon, notify_startup):
    """Runs once the GUI loop is live (passed to webview.start)."""
    # Show requests from a second launch (single-instance IPC).
    def show_watcher():
        ev = getattr(_core, "_show_event", None)
        while not _stop.is_set():
            if ev is not None and ev.wait(0.5):
                ev.clear()
                gui_show()
            elif ev is None:
                _stop.wait(0.5)
    threading.Thread(target=show_watcher, daemon=True, name="show-watcher").start()

    if tray_icon is not None:
        def run_tray():
            try:
                tray_icon.run(setup=lambda i: _tray_ready(i, notify_startup))
            except Exception as e:
                print("tray failed: %s" % e)
        threading.Thread(target=run_tray, daemon=True, name="tray").start()


def _tray_ready(icon, notify_startup):
    try:
        icon.visible = True
        if notify_startup:
            icon.notify("Monitoring your PC in the background. Right-click to configure or quit.",
                        "PC Monitor is running")
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Entry point (called from core.main on the primary launch)
# ---------------------------------------------------------------------------
def run(core, start_hidden=False, notify_startup=False):
    global _core, _state, _httpd, _window
    _core = core

    config = core.load_config() or dict(core.DEFAULT_CONFIG)
    _state = AppState(config)

    _httpd, port = srv.make_server(core, _state)
    srv.serve(_httpd)
    url = "http://127.0.0.1:%d/" % port
    print("Config UI at %s" % url)

    threading.Thread(target=_detect_source, daemon=True, name="detect-source").start()
    threading.Thread(target=_monitor_loop, daemon=True, name="monitor").start()

    if not WEBVIEW_AVAILABLE:
        # No native window backend: serve and open the browser instead.
        print("pywebview not available - open the UI in your browser.")
        try:
            webbrowser.open(url)
        except Exception:
            pass
        try:
            while not _stop.wait(1.0):
                pass
        except KeyboardInterrupt:
            gui_quit()
        return

    tray_icon = _make_tray()
    _window = webview.create_window(
        "SmallOLED PC Companion", url,
        width=1180, height=820, min_size=(900, 600), hidden=start_hidden,
        js_api=_JsApi())

    def on_closing():
        # Hide to tray instead of quitting, unless a real Quit is in progress.
        if _quitting:
            return True
        gui_hide()
        return False
    _window.events.closing += on_closing

    webview.start(lambda: _start_background(tray_icon, notify_startup))
    # webview.start returns when the window is destroyed (tray Quit).
    _stop.set()
