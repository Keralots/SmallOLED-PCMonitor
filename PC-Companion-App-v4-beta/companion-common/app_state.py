"""Single owner for state shared across the v4 threads.

The HTTP server (a thread per request), the UDP monitor loop, and sensor
rescans all read and write the same data: the active config, the live metric
values shown in the preview, and the device-reachability flag. Every access
goes through this RLock-guarded manager so a rescan or save can never race the
sender thread mid-cycle.

All getters hand back deep copies, so a caller can read/iterate without holding
the lock and without seeing a later mutation.
"""

import copy
import threading


def _metric_signature(config):
    """A tuple that changes whenever the set/order/identity of metrics changes.

    Used to decide when the monitor must drop its cached last-good values (the
    metric ids may now point at different sensors)."""
    out = []
    for m in (config or {}).get("metrics", []):
        out.append((m.get("id"), m.get("name", ""), m.get("source", ""),
                    m.get("wmi_identifier", ""), m.get("psutil_method", "")))
    return tuple(out)


class AppState:
    def __init__(self, config=None):
        self._lock = threading.RLock()
        self._config = config or {}
        self._values = {}            # id -> last value (int) for the live preview
        self._generation = 0         # bumped when the metric signature changes
        self._sig = _metric_signature(self._config)
        self._device_reachable = None
        self._monitoring = False
        # Sensor-source description for the status readout, set by the server
        # after discovery/rescan (e.g. "REST API", "WMI", "psutil only").
        self._source_text = "unknown"

    # ---- config -----------------------------------------------------------
    def get_config(self):
        with self._lock:
            return copy.deepcopy(self._config)

    def set_config(self, config):
        """Replace the active config. Returns the (possibly bumped) generation."""
        with self._lock:
            self._config = copy.deepcopy(config or {})
            new_sig = _metric_signature(self._config)
            if new_sig != self._sig:
                self._sig = new_sig
                self._generation += 1
                self._values = {k: v for k, v in self._values.items()
                                if k in {m.get("id") for m in self._config.get("metrics", [])}}
            return self._generation

    def generation(self):
        with self._lock:
            return self._generation

    def metric_count(self):
        with self._lock:
            return len(self._config.get("metrics", []))

    # ---- live values ------------------------------------------------------
    def update_values(self, values_by_id):
        with self._lock:
            self._values.update(values_by_id or {})

    def get_values(self):
        with self._lock:
            return dict(self._values)

    # ---- device / source status ------------------------------------------
    def set_reachable(self, reachable):
        with self._lock:
            self._device_reachable = reachable

    def set_monitoring(self, on):
        with self._lock:
            self._monitoring = bool(on)

    def set_source_text(self, text):
        with self._lock:
            self._source_text = text or "unknown"

    def status(self):
        """A plain dict for /api/status (and the sidebar readout)."""
        with self._lock:
            return {
                "deviceReachable": bool(self._device_reachable),
                "deviceIp": self._config.get("esp32_ip", ""),
                "source": self._source_text,
                "metricCount": len(self._config.get("metrics", [])),
                "monitoring": self._monitoring,
            }
