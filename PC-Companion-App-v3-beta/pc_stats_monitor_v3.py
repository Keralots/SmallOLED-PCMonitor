"""
PC Stats Monitor v3.0 - Dynamic Sensor Selection
Flexible monitoring system with GUI configuration and up to 12 customizable metrics
"""

import psutil
import socket
import time
import json
import os
import sys
import argparse
import subprocess
import threading
import webbrowser
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from urllib import request as urllib_request
from urllib import error as urllib_error
import re
import copy

# Try to import pystray for system tray support
try:
    import pystray
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False

# Try to import pythoncom for COM initialization (needed for WMI with pythonw.exe)
try:
    import pythoncom
    PYTHONCOM_AVAILABLE = True
except ImportError:
    PYTHONCOM_AVAILABLE = False

# ---------------------------------------------------------------------------
# Frozen-build awareness (PyInstaller .exe) vs. plain-script execution.
#
# When packaged with PyInstaller --onefile, __file__ points at a temporary
# _MEIxxxx extraction folder that Windows deletes when the process exits, and
# a --windowed build has sys.stdout/stderr/stdin == None. Both of those break
# the original script (config saved into a vanishing temp dir; print()/input()
# crashing). We detect frozen mode here and adjust paths + I/O accordingly.
# ---------------------------------------------------------------------------
IS_FROZEN = getattr(sys, "frozen", False)

if IS_FROZEN:
    # The real .exe path - NOT the _MEIxxxx temp dir that __file__ resolves to.
    EXE_PATH = sys.executable
    APP_DIR = os.path.dirname(sys.executable)
else:
    EXE_PATH = os.path.abspath(__file__)
    APP_DIR = os.path.dirname(os.path.abspath(__file__))


def get_data_dir():
    """
    Per-user writable directory for the config file and log.

    Frozen .exe -> %APPDATA%\\PCStatsMonitor so the config survives the .exe
    living in a read-only spot (Program Files) and survives the .exe being
    moved. Plain script -> next to the .py, matching the original behavior.
    """
    if IS_FROZEN:
        base = os.environ.get("APPDATA") or APP_DIR
        data_dir = os.path.join(base, "PCStatsMonitor")
    else:
        data_dir = APP_DIR
    try:
        os.makedirs(data_dir, exist_ok=True)
    except Exception:
        data_dir = APP_DIR
    return data_dir


DATA_DIR = get_data_dir()
CONFIG_FILE = os.path.join(DATA_DIR, "monitor_config.json")
LOG_FILE = os.path.join(DATA_DIR, "monitor.log")


def _setup_frozen_io():
    """
    A --windowed PyInstaller build has no console: sys.stdout/stderr are None
    and this script's many print() calls would crash it. Redirect them to a log
    file (handy for debugging autostart), and give stdin a dummy read handle so
    a stray input() raises EOFError cleanly instead of AttributeError.
    """
    if not IS_FROZEN:
        return
    try:
        log = open(LOG_FILE, "a", encoding="utf-8", buffering=1)
        if sys.stdout is None:
            sys.stdout = log
        if sys.stderr is None:
            sys.stderr = log
    except Exception:
        try:
            devnull = open(os.devnull, "w")
            if sys.stdout is None:
                sys.stdout = devnull
            if sys.stderr is None:
                sys.stderr = devnull
        except Exception:
            pass
    if sys.stdin is None:
        try:
            sys.stdin = open(os.devnull, "r")
        except Exception:
            pass


_setup_frozen_io()


# Set True to make pause() never block (used by the GUI "Rescan" button, which
# re-runs discovery and must not stop on a console "Press Enter" prompt).
_SUPPRESS_PAUSE = False


def pause(message="Press Enter to continue..."):
    """input() that no-ops in a frozen / no-console build instead of crashing."""
    if IS_FROZEN or _SUPPRESS_PAUSE or not sys.stdin or not sys.stdin.isatty():
        return
    try:
        input(message)
    except (EOFError, RuntimeError):
        pass


def _splash_close():
    """Dismiss the PyInstaller startup splash screen, if this is a --splash build.

    The .exe is --onefile, so double-clicking it spends a few seconds extracting
    and discovering sensors before any window appears - long enough that users
    think nothing happened and double-click again. PyInstaller's splash shows a
    "Reading sensors, please wait..." box immediately (during extraction, before
    our Python even runs); we close it here the moment the real UI (config window
    or tray icon) is ready. Safe no-op for plain-script runs, where pyi_splash
    does not exist.
    """
    try:
        import pyi_splash  # injected by PyInstaller only in a --splash build
        if pyi_splash.is_alive():
            pyi_splash.close()
    except Exception:
        pass

# Default configuration
DEFAULT_CONFIG = {
    "version": "3.0",
    "esp32_ip": "192.168.0.163",
    "udp_port": 4210,
    "update_interval": 3,
    "metrics": []
}

# Maximum metrics supported by ESP32
from constants import MAX_METRICS
from layout_engine import (
    LAYOUT_TEMPLATES,
    auto_layout,
    slot_geometry,
    build_device_layout_json,
    push_layout_to_device,
)
from layout_editor import LayoutEditorDialog

# Global sensor database
sensor_database = {
    "system": [],    # psutil-based metrics (CPU%, RAM%, Disk%)
    "temperature": [],
    "fan": [],
    "load": [],
    "clock": [],
    "power": [],
    "data": [],       # Network/disk data (uploaded/downloaded GB)
    "throughput": [], # Network throughput (upload/download speed KB/s, MB/s)
    "gpu": [],        # GPU metrics (usage, memory, freq, fan, power) — from LHM
    "other": []
}

# Global variable for discovered WMI namespace (can be auto-detected)
discovered_wmi_namespace = "root\\LibreHardwareMonitor"  # Default

# Global variables for REST API (alternative to WMI for LHM 0.9.5+)
rest_api_host = "localhost"
rest_api_port = 8085
use_rest_api = False  # Auto-detected; True when WMI fails but REST API works


class LHMHealthMonitor:
    """
    Monitors LibreHardwareMonitor REST API health.
    Tracks consecutive failures and provides exponential backoff for recovery.
    """
    def __init__(self):
        self.consecutive_failures = 0
        self.last_success_time = time.time()
        self.is_healthy = True
        self.last_warning_time = 0

    def record_success(self):
        """Record a successful API call"""
        if not self.is_healthy:
            print("  ✓ LHM connection restored!")
        self.consecutive_failures = 0
        self.last_success_time = time.time()
        self.is_healthy = True

    def record_failure(self):
        """Record a failed API call"""
        self.consecutive_failures += 1
        if self.consecutive_failures >= 2:  # Trigger faster (was 3)
            if self.is_healthy:
                print("  ⚠ LHM REST API unhealthy - entering recovery mode")
            self.is_healthy = False

    def get_retry_delay(self):
        """Exponential backoff: 3s, 6s, 12s, max 30s"""
        if self.consecutive_failures <= 1:
            return 3
        delay = min(3 * (2 ** (self.consecutive_failures - 1)), 30)
        return delay

    def should_print_warning(self):
        """Limit warning messages to once per 30 seconds"""
        now = time.time()
        if now - self.last_warning_time >= 30:
            self.last_warning_time = now
            return True
        return False


# Global health monitor instance
lhm_health_monitor = LHMHealthMonitor()


def is_lhm_process_running():
    """Check if LibreHardwareMonitor process is running"""
    lhm_names = ["librehardwaremonitor", "libre hardware monitor"]
    for proc in psutil.process_iter(['name']):
        try:
            proc_name = proc.info['name'].lower()
            if any(name in proc_name for name in lhm_names):
                return True
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return False


def discover_wmi_namespaces():
    """
    Quick check for WMI namespace (simplified for 0.9.5+ compatibility)
    Returns: (list_of_namespaces, working_namespace)
    """
    print("\nChecking WMI namespace...")

    namespace = "root\\LibreHardwareMonitor"
    try:
        import wmi
        w = wmi.WMI(namespace=namespace)
        sensors = list(w.Sensor())

        if len(sensors) > 0:
            print(f"  ✓ WMI working with {len(sensors)} sensors")
            return [namespace], namespace
        else:
            print(f"  ⚠ WMI accessible but 0 sensors (LibreHardwareMonitor 0.9.5+ issue)")
            print(f"  → Will use REST API fallback")
            return [], None
    except Exception as e:
        print(f"  ✗ WMI not accessible: {str(e)[:60]}")
        print(f"  → Will use REST API fallback")
        return [], None


def get_librehardwaremonitor_version():
    """
    Try to detect LibreHardwareMonitor version
    Returns: version string or None
    """
    version = None

    # Method 1: Check executable version
    try:
        import win32api
        import win32con
        import os

        # Common installation paths
        possible_paths = [
            r"C:\Program Files\LibreHardwareMonitor\LibreHardwareMonitor.exe",
            r"C:\Program Files (x86)\LibreHardwareMonitor\LibreHardwareMonitor.exe",
            r"C:\Users\Public\Desktop\LibreHardwareMonitor.exe",
        ]

        # Also check PATH
        try:
            import shutil
            exe_path = shutil.which("LibreHardwareMonitor.exe")
            if exe_path:
                possible_paths.insert(0, exe_path)
        except:
            pass

        for path in possible_paths:
            if os.path.exists(path):
                try:
                    info = win32api.GetFileVersionInfo(path, "\\")
                    ms = info['FileVersionMS']
                    ls = info['FileVersionLS']
                    version = f"{win32api.HIWORD(ms)}.{win32api.LOWORD(ms)}.{win32api.HIWORD(ls)}.{win32api.LOWORD(ls)}"
                    return version
                except:
                    pass
    except:
        pass

    # Method 2: Try to get version from WMI
    try:
        import wmi
        w = wmi.WMI()
        # Try to find LibreHardwareMonitor process and get its version
        import psutil
        for proc in psutil.process_iter(['name', 'exe']):
            try:
                if 'librehardwaremonitor' in proc.info['name'].lower():
                    exe_path = proc.info['exe']
                    if exe_path and os.path.exists(exe_path):
                        import win32api
                        info = win32api.GetFileVersionInfo(exe_path, "\\")
                        ms = info['FileVersionMS']
                        ls = info['FileVersionLS']
                        version = f"{win32api.HIWORD(ms)}.{win32api.LOWORD(ms)}.{win32api.HIWORD(ls)}.{win32api.LOWORD(ls)}"
                        return version
            except:
                pass
    except:
        pass

    return None


def extract_sensors_from_tree(node, sensor_list=None, parent_hardware=None):
    """
    Recursively extract all sensors from LibreHardwareMonitor REST API tree structure.
    The API returns a hierarchical tree where actual sensors have a 'SensorId' field.
    Tracks parent hardware name to provide better context for sensors.
    """
    if sensor_list is None:
        sensor_list = []

    # Check if this node is a hardware device (has children but no SensorId)
    # Hardware nodes have Text like "Intel Ethernet I219-V" or "NVIDIA GeForce RTX 3080"
    current_hardware = parent_hardware
    if "Children" in node and "SensorId" not in node:
        # This might be a hardware node - use its name as parent for children
        if node.get("Text") and node.get("Text") != "Sensor":
            current_hardware = node.get("Text")

    # If this node has a SensorId, it's an actual sensor
    if "SensorId" in node:
        # Add parent hardware name to sensor for better identification
        sensor_copy = node.copy()
        if current_hardware:
            sensor_copy["_parent_hardware"] = current_hardware
        sensor_list.append(sensor_copy)

    # Recursively process children
    if "Children" in node and isinstance(node["Children"], list):
        for child in node["Children"]:
            extract_sensors_from_tree(child, sensor_list, current_hardware)

    return sensor_list


def check_rest_api_connectivity(host, port):
    """
    Check if LibreHardwareMonitor REST API is accessible
    Returns: (success, sensor_count, error_message)
    """
    url = f"http://{host}:{port}/data.json"

    try:
        req = urllib_request.Request(url, method='GET')
        req.add_header('User-Agent', 'PC-Stats-Monitor/3.0')

        with urllib_request.urlopen(req, timeout=3) as response:
            if response.status == 200:
                data = response.read().decode('utf-8')
                root = json.loads(data)

                # Extract sensors from tree structure
                sensors = extract_sensors_from_tree(root)

                if len(sensors) > 0:
                    return True, len(sensors), None
                else:
                    return False, 0, "REST API returned no sensors"
            else:
                return False, 0, f"HTTP {response.status}"

    except urllib_error.HTTPError as e:
        return False, 0, f"HTTP error {e.code}"
    except urllib_error.URLError as e:
        return False, 0, f"Connection failed: {e.reason}"
    except json.JSONDecodeError as e:
        return False, 0, f"Invalid JSON response: {e}"
    except Exception as e:
        return False, 0, f"Unexpected error: {e}"


def discover_sensors_via_http(host, port):
    """
    Discover sensors via LibreHardwareMonitor REST API
    This is used when WMI fails (LHM 0.9.5+)
    """
    global sensor_database

    url = f"http://{host}:{port}/data.json"

    try:
        req = urllib_request.Request(url, method='GET')
        req.add_header('User-Agent', 'PC-Stats-Monitor/3.0')

        with urllib_request.urlopen(req, timeout=5) as response:
            if response.status != 200:
                print(f"  ✗ HTTP error {response.status}")
                return False

            data = response.read().decode('utf-8')
            root = json.loads(data)

            # Extract sensors from tree structure
            sensors = extract_sensors_from_tree(root)

            # Reset name tracker to ensure fresh unique names
            reset_generated_names()

            sensor_count = 0
            for sensor in sensors:
                # Map REST API fields to our sensor_database format
                sensor_id = sensor.get("SensorId", "")
                sensor_name = sensor.get("Text", "Unknown")
                sensor_type = sensor.get("Type", "").lower()
                sensor_value = sensor.get("Value", "0")

                # Skip if missing critical fields
                if not sensor_id or not sensor_name:
                    continue

                # Parse value from string (e.g., "45.0 °C" -> 45.0)
                original_value_str = str(sensor_value)
                try:
                    # Extract numeric value from string like "45.0 °C" or "12.1 %"
                    value_match = re.search(r'[-+]?\d*\.?\d+', original_value_str)
                    if value_match:
                        sensor_value = float(value_match.group())
                    else:
                        sensor_value = 0

                    # Normalize throughput to KB/s for ESP32
                    if sensor_type == "throughput":
                        value_upper = original_value_str.upper()
                        if "GB/S" in value_upper:
                            # GB/s → KB/s: multiply by 1024*1024
                            sensor_value = sensor_value * 1024 * 1024
                        elif "MB/S" in value_upper:
                            # MB/s → KB/s: multiply by 1024
                            sensor_value = sensor_value * 1024
                        elif "KB/S" in value_upper:
                            # Already KB/s, no conversion needed
                            pass
                        elif "B/S" in value_upper or not any(x in value_upper for x in ['/', 'S']):
                            # B/s or raw bytes → KB/s: divide by 1024
                            sensor_value = sensor_value / 1024
                        # Multiply by 10 to preserve 1 decimal place (ESP32 will divide by 10)
                        sensor_value = sensor_value * 10
                except:
                    sensor_value = 0

                # Determine unit based on type
                unit_map = {
                    "temperature": "C",  # No degree symbol - OLED can't display it
                    "fan": "RPM",
                    "load": "%",
                    "clock": "MHz",
                    "power": "W",
                    "voltage": "V",
                    "data": "GB",
                    "smalldata": "MB",
                    "control": "%",
                    "level": "%",
                    "throughput": "KB/s",
                }
                sensor_unit = unit_map.get(sensor_type, "")

                # Generate short name from sensor_id and sensor_name for uniqueness
                short_name = generate_short_name_from_id(sensor_id, sensor_type, sensor_name)

                # Build display name with device context
                identifier_parts = sensor_id.split('/')
                parent_hardware = sensor.get("_parent_hardware", "")

                # For network sensors, use parent hardware name (actual NIC name)
                if "nic" in sensor_id.lower() and parent_hardware:
                    # Use friendly NIC name instead of GUID
                    display_name = f"{sensor_name} [{parent_hardware}]"
                elif len(identifier_parts) > 1:
                    device_info = identifier_parts[1]
                    if device_info.lower() not in sensor_name.lower():
                        display_name = f"{sensor_name} [{device_info}]"
                    else:
                        display_name = sensor_name
                else:
                    display_name = sensor_name

                # Check if this is an active network interface (has traffic)
                is_active_nic = False
                if "nic" in sensor_id.lower() and sensor_type == "throughput":
                    if sensor_value > 0:
                        is_active_nic = True

                # Reclassify ambiguous types based on device context
                # Memory metrics are tagged as "data" but should be in "system"
                device_id_lower = sensor_id.lower()
                sensor_name_lower = sensor_name.lower()

                if sensor_type in ("data", "smalldata"):
                    # Check if this is memory-related (not network data)
                    if ("memory" in device_id_lower or "ram" in device_id_lower or
                        "vram" in device_id_lower or
                        ("gpu" in device_id_lower and ("memory" in sensor_name_lower or "vram" in sensor_name_lower))):
                        # Reclassify memory as system metric
                        sensor_type = "memory"

                sensor_info = {
                    "name": short_name,
                    "display_name": display_name,
                    "source": "wmi",  # Keep as "wmi" for compatibility
                    "type": sensor_type,
                    "unit": sensor_unit,
                    "wmi_identifier": sensor_id,
                    "wmi_sensor_name": sensor_name,
                    "custom_label": "",
                    "current_value": int(sensor_value),
                    "is_active_nic": is_active_nic,  # True if network interface has traffic
                    "parent_hardware": parent_hardware  # Hardware name (useful for NICs)
                }

                # Categorize sensor — GPU sensors go to dedicated "gpu" category
                if _is_gpu_sensor(sensor_id):
                    sensor_database["gpu"].append(sensor_info)
                elif sensor_type == "temperature":
                    sensor_database["temperature"].append(sensor_info)
                elif sensor_type == "fan":
                    sensor_database["fan"].append(sensor_info)
                elif sensor_type == "load":
                    sensor_database["load"].append(sensor_info)
                elif sensor_type == "clock":
                    sensor_database["clock"].append(sensor_info)
                elif sensor_type == "power":
                    sensor_database["power"].append(sensor_info)
                elif sensor_type == "memory":  # Reclassified memory metrics
                    sensor_database["system"].append(sensor_info)
                elif sensor_type in ("data", "smalldata"):  # Now only actual network data
                    sensor_database["data"].append(sensor_info)
                elif sensor_type == "throughput":
                    sensor_database["throughput"].append(sensor_info)
                else:
                    sensor_database["other"].append(sensor_info)

                sensor_count += 1

            if sensor_count > 0:
                print(f"  ✓ Found {sensor_count} hardware sensors via REST API:")
                print(f"    - GPU:         {len(sensor_database['gpu'])}")
                print(f"    - Temperatures: {len(sensor_database['temperature'])}")
                print(f"    - Fans: {len(sensor_database['fan'])}")
                print(f"    - Loads: {len(sensor_database['load'])}")
                print(f"    - Clocks: {len(sensor_database['clock'])}")
                print(f"    - Power: {len(sensor_database['power'])}")
                print(f"    - Data: {len(sensor_database['data'])}")
                print(f"    - Throughput: {len(sensor_database['throughput'])}")
                if len(sensor_database['other']) > 0:
                    print(f"    - Other: {len(sensor_database['other'])}")
                return True
            else:
                print("  ⚠ REST API returned 0 sensors")
                return False

    except urllib_error.HTTPError as e:
        print(f"  ✗ HTTP error {e.code}")
        return False
    except urllib_error.URLError as e:
        print(f"  ✗ Connection failed: {e.reason}")
        return False
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return False


def get_metric_value_via_http(metric_config, host, port):
    """
    Get sensor value via LibreHardwareMonitor REST API
    Used when use_rest_api = True

    Returns: int value on success, None on failure (to distinguish from real zeros)
    """
    global lhm_health_monitor

    sensor_id = metric_config.get("wmi_identifier", "")
    if not sensor_id:
        return None

    url = f"http://{host}:{port}/data.json"
    is_throughput = metric_config.get("unit", "") == "KB/s"

    try:
        req = urllib_request.Request(url, method='GET')
        req.add_header('User-Agent', 'PC-Stats-Monitor/3.0')

        with urllib_request.urlopen(req, timeout=1) as response:  # 1s timeout for fast failure detection
            if response.status != 200:
                lhm_health_monitor.record_failure()
                return None

            data = response.read().decode('utf-8')
            root = json.loads(data)

            # Extract sensors from tree structure
            sensors = extract_sensors_from_tree(root)

            # Find matching sensor by SensorId
            for sensor in sensors:
                if sensor.get("SensorId", "") == sensor_id:
                    value = sensor.get("Value", "0")
                    value_str = str(value)
                    # Parse value from string (e.g., "45.0 °C" -> 45.0)
                    try:
                        value_match = re.search(r'[-+]?\d*\.?\d+', value_str)
                        if value_match:
                            float_value = float(value_match.group())
                            # For throughput: multiply by 10 to preserve 1 decimal place
                            # ESP32 will divide by 10 when displaying
                            if is_throughput:
                                # Normalize throughput to KB/s for ESP32
                                value_upper = value_str.upper()
                                if "GB/S" in value_upper:
                                    # GB/s → KB/s
                                    float_value = float_value * 1024 * 1024
                                elif "MB/S" in value_upper:
                                    # MB/s → KB/s
                                    float_value = float_value * 1024
                                elif "KB/S" in value_upper:
                                    # Already KB/s
                                    pass
                                elif "B/S" in value_upper or not any(x in value_upper for x in ['/', 'S']):
                                    # B/s or raw bytes → KB/s
                                    float_value = float_value / 1024
                                float_value = float_value * 10
                            lhm_health_monitor.record_success()
                            return int(float_value)
                    except:
                        pass
                    # Sensor found but value parsing failed
                    lhm_health_monitor.record_success()  # API is working
                    return 0

            # Sensor not found in response
            lhm_health_monitor.record_success()  # API is working
            return 0

    except Exception:
        lhm_health_monitor.record_failure()
        return None


# Global tracker for generated names to ensure uniqueness
_generated_names = set()


def reset_generated_names():
    """Reset the name tracker - call before sensor discovery"""
    global _generated_names
    _generated_names = set()


def _make_unique_name(base_name):
    """Ensure name is unique by adding suffix if needed"""
    global _generated_names

    # Truncate base to max 10 chars
    base_name = base_name[:10]

    if base_name not in _generated_names:
        _generated_names.add(base_name)
        return base_name

    # Add numeric suffix to make unique
    for i in range(1, 100):
        candidate = f"{base_name[:8]}{i}" if len(base_name) > 8 else f"{base_name}{i}"
        candidate = candidate[:10]
        if candidate not in _generated_names:
            _generated_names.add(candidate)
            return candidate

    return base_name  # Fallback


def _extract_context_suffix(sensor_name):
    """Extract context suffix from sensor name"""
    name_lower = sensor_name.lower()

    # Memory/data context
    if "used" in name_lower:
        return "_U"
    elif "available" in name_lower or "avail" in name_lower:
        return "_A"
    elif "capacity" in name_lower:
        return "_CAP"
    elif "total" in name_lower:
        return "_TOT"
    elif "free" in name_lower:
        return "_F"

    # Temperature context
    elif "core max" in name_lower:
        return "_MAX"
    elif "core avg" in name_lower or "average" in name_lower:
        return "_AVG"
    elif "hotspot" in name_lower:
        return "_HOT"
    elif "junction" in name_lower:
        return "_JNC"

    return ""


def generate_short_name_from_id(sensor_id, sensor_type, sensor_name=""):
    """
    Generate unique short name from sensor_id and sensor_name (REST API format)
    Uses sensor_name context to differentiate similar sensors
    """
    parts = sensor_id.split('/')
    name_lower = sensor_name.lower()

    # Get context suffix from sensor name
    context = _extract_context_suffix(sensor_name)

    if len(parts) >= 4:
        device = parts[1]  # e.g., "intelcpu", "gpu-nvidia", "lpc", "nic"
        device_lower = device.lower()
        device_idx = parts[2] if len(parts) > 2 else "0"
        sensor_idx = parts[-1]  # Last part is usually the sensor index

        # CPU sensors
        if "cpu" in device_lower:
            if sensor_type == "load":
                if "total" in name_lower or sensor_idx == "0":
                    base = "CPU"
                else:
                    base = f"CPU_C{sensor_idx}"
            elif sensor_type == "temperature":
                if "core max" in name_lower:
                    base = "CPU_MAX"
                elif "core avg" in name_lower or "average" in name_lower:
                    base = "CPU_AVG"
                elif "core" in name_lower and "p-core" not in name_lower and "e-core" not in name_lower:
                    base = f"CPUT{sensor_idx}"
                elif "p-core" in name_lower:
                    base = f"CPUP{sensor_idx}"
                elif "e-core" in name_lower:
                    base = f"CPUE{sensor_idx}"
                elif "ccd" in name_lower:
                    base = f"CCD{sensor_idx}T"
                else:
                    base = f"CPUT{sensor_idx}" if sensor_idx != "0" else "CPUT"
            elif sensor_type == "power":
                if "package" in name_lower:
                    base = "CPU_PKG"
                elif "core" in name_lower:
                    base = "CPU_COR"
                else:
                    base = f"CPUW{sensor_idx}" if sensor_idx != "0" else "CPUW"
            elif sensor_type == "clock":
                base = f"CPUCLK{sensor_idx}" if sensor_idx != "0" else "CPUCLK"
            else:
                base = f"CPU_{sensor_idx}"
            return _make_unique_name(base)

        # GPU sensors
        elif "gpu" in device_lower or "nvidia" in device_lower or "amd" in device_lower:
            gpu_idx = "" if device_idx == "0" else device_idx
            if sensor_type == "load":
                if "memory" in name_lower or "vram" in name_lower:
                    base = f"VRAM{gpu_idx}"
                elif "core" in name_lower or sensor_idx == "0":
                    base = f"GPU{gpu_idx}"
                else:
                    base = f"GPU{gpu_idx}_{sensor_idx}"
            elif sensor_type == "temperature":
                if "hotspot" in name_lower:
                    base = f"GPU{gpu_idx}_HOT"
                elif "memory" in name_lower or "vram" in name_lower:
                    base = f"VRAM{gpu_idx}T"
                else:
                    base = f"GPUT{gpu_idx}"
            elif sensor_type == "power":
                base = f"GPUW{gpu_idx}"
            elif sensor_type == "clock":
                if "memory" in name_lower:
                    base = f"VCLK{gpu_idx}"
                else:
                    base = f"GCLK{gpu_idx}"
            elif sensor_type == "fan":
                base = f"GPUF{gpu_idx}_{sensor_idx}" if sensor_idx != "0" else f"GPUF{gpu_idx}"
            elif sensor_type in ("data", "smalldata"):
                # GPU memory data
                base = f"VRAM{gpu_idx}{context}"
            else:
                base = f"GPU{gpu_idx}_{sensor_idx}"
            return _make_unique_name(base)

        # LPC/Motherboard sensors (VRM, PCH, System temps, etc.)
        elif "lpc" in device_lower or "motherboard" in device_lower or "mainboard" in device_lower:
            if sensor_type == "temperature":
                if "vrm" in name_lower:
                    base = "VRM_T"
                elif "mos" in name_lower:
                    base = "MOS_T"
                elif "pch" in name_lower:
                    base = "PCH_T"
                elif "cpu" in name_lower and "socket" in name_lower:
                    base = "CPUS_T"
                elif "system" in name_lower:
                    base = f"SYS{sensor_idx}T"
                elif "pcie" in name_lower or "pci" in name_lower:
                    base = f"PCIE{sensor_idx}T"
                elif "m.2" in name_lower or "m2" in name_lower:
                    base = f"M2_{sensor_idx}T"
                elif "chipset" in name_lower:
                    base = "CHIP_T"
                else:
                    # Generic LPC temperature
                    base = f"MB{sensor_idx}T"
            elif sensor_type == "fan":
                if "cpu" in name_lower:
                    base = "CPUF"
                elif "pump" in name_lower:
                    base = "PUMP"
                elif "chassis" in name_lower:
                    base = f"CHS{sensor_idx}F"
                elif "system" in name_lower:
                    # Extract fan number from name like "System Fan #1"
                    fan_match = re.search(r'#(\d+)', sensor_name)
                    if fan_match:
                        base = f"SYS{fan_match.group(1)}F"
                    else:
                        base = f"SYS{sensor_idx}F"
                elif "aux" in name_lower:
                    base = f"AUX{sensor_idx}F"
                else:
                    base = f"FAN{sensor_idx}"
            elif sensor_type == "voltage":
                if "vcore" in name_lower or "cpu" in name_lower:
                    base = "VCORE"
                elif "vram" in name_lower or "memory" in name_lower:
                    base = "VMEM"
                elif "+12v" in name_lower or "12v" in name_lower:
                    base = "V12"
                elif "+5v" in name_lower or "5v" in name_lower:
                    base = "V5"
                elif "+3.3v" in name_lower or "3.3v" in name_lower:
                    base = "V3_3"
                else:
                    base = f"V{sensor_idx}"
            elif sensor_type == "control":
                base = f"CTL{sensor_idx}"
            else:
                base = f"LPC{sensor_idx}"
            return _make_unique_name(base)

        # Memory/RAM sensors
        elif "memory" in device_lower or "ram" in device_lower:
            if sensor_type in ("data", "smalldata", "memory"):
                if "vram" in name_lower:
                    base = f"VRAM{context}"
                elif "capacity" in name_lower:
                    base = f"RAM_CAP"
                elif "used" in name_lower:
                    base = "RAM_U"
                elif "available" in name_lower or "avail" in name_lower:
                    base = "RAM_A"
                else:
                    base = f"RAM{context}" if context else f"RAM{sensor_idx}"
            elif sensor_type == "load":
                base = "RAM"
            else:
                base = f"RAM{sensor_idx}"
            return _make_unique_name(base)

        # Network sensors
        elif "nic" in device_lower or "network" in device_lower:
            net_idx = "" if device_idx == "0" else device_idx
            if sensor_type == "throughput":
                if "upload" in name_lower or "sent" in name_lower:
                    base = f"NET{net_idx}_U"
                elif "download" in name_lower or "received" in name_lower:
                    base = f"NET{net_idx}_D"
                else:
                    # Use sensor index: 0=upload, 1=download typically
                    if sensor_idx == "0":
                        base = f"NET{net_idx}_U"
                    else:
                        base = f"NET{net_idx}_D"
            elif sensor_type == "data":
                if "upload" in name_lower or "sent" in name_lower:
                    base = f"NTD{net_idx}_U"
                elif "download" in name_lower or "received" in name_lower:
                    base = f"NTD{net_idx}_D"
                else:
                    base = f"NTD{net_idx}_{sensor_idx}"
            else:
                base = f"NET{net_idx}_{sensor_idx}"
            return _make_unique_name(base)

        # Storage (HDD/SSD/NVMe)
        elif "hdd" in device_lower or "ssd" in device_lower or "nvme" in device_lower:
            drv_idx = "" if device_idx == "0" else device_idx
            if "hdd" in device_lower:
                prefix = f"HDD{drv_idx}"
            elif "ssd" in device_lower:
                prefix = f"SSD{drv_idx}"
            else:
                prefix = f"NVM{drv_idx}"

            if sensor_type == "temperature":
                base = f"{prefix}T"
            elif sensor_type == "load":
                base = f"{prefix}%"
            elif sensor_type == "data":
                base = f"{prefix}D"
            else:
                base = f"{prefix}_{sensor_idx}"
            return _make_unique_name(base)

    # Fallback: Create descriptive name from sensor_name + sensor_id
    if sensor_name:
        # Use first word of sensor name + type abbreviation
        words = sensor_name.replace("-", " ").replace("_", " ").split()
        if words:
            base = words[0][:4].upper()
            type_suffix = {"temperature": "T", "fan": "F", "load": "%",
                          "power": "W", "voltage": "V", "clock": "C"}.get(sensor_type, "")
            base = f"{base}{type_suffix}"
            return _make_unique_name(base)

    # Last resort fallback
    if len(parts) >= 2:
        device = parts[1].replace("-", "")[:4].upper()
        return _make_unique_name(f"{device}{sensor_idx}")


def _is_gpu_sensor(identifier):
    """
    Return True if the LHM sensor identifier belongs to a GPU device.
    Uses the same device-part logic as generate_short_name_from_id so that
    AMD CPU sensors (amdcpu) are NOT mistaken for GPU sensors.
    """
    parts = identifier.lower().split('/')
    if len(parts) < 2:
        return False
    device = parts[1]
    # "cpu" check must come first to exclude intelcpu / amdcpu
    if "cpu" in device:
        return False
    return "gpu" in device or "nvidia" in device or "amd" in device


def check_wmi_connectivity():
    """
    Diagnostics: Check if LibreHardwareMonitor WMI namespace is accessible
    Returns: (success, error_message, suggestion)
    """
    print("\n" + "-" * 60)
    print("DIAGNOSTICS: Checking LibreHardwareMonitor connectivity...")
    print("-" * 60)

    # Check 1: Verify required modules are installed
    print("\n[Check 1/4] Verifying required Python modules...")
    try:
        import wmi
        print("  ✓ pywin32 and wmi modules are installed")
    except ImportError as e:
        missing = str(e).split("'")[1] if "'" in str(e) else "pywin32/wmi"
        return False, f"Missing required module: {missing}", (
            "FIX: Install required modules:\n"
            "   pip install pywin32 wmi\n\n"
            "   Or run: python -m pip install pywin32 wmi"
        )

    # Check 2: Verify LibreHardwareMonitor process is running
    print("\n[Check 2/4] Checking if LibreHardwareMonitor is running...")
    try:
        import psutil
        found_lhm = False
        lhm_names = ["librehardwaremonitor", "libre hardware monitor",
                     "hwmonitor", "hardware monitor"]
        for proc in psutil.process_iter(['name']):
            try:
                proc_name = proc.info['name'].lower()
                if any(name in proc_name for name in lhm_names):
                    found_lhm = True
                    print(f"  ✓ Found LibreHardwareMonitor process: {proc.info['name']}")
                    break
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

        # Try to detect version if process is running
        if found_lhm:
            version = get_librehardwaremonitor_version()
            if version:
                print(f"  → Detected LibreHardwareMonitor version: {version}")
                # Check for known problematic versions
                version_parts = version.split('.')
                if len(version_parts) >= 2:
                    major = int(version_parts[0])
                    minor = int(version_parts[1])
                    # Version 0.9.5+ has known WMI issues
                    if major == 0 and minor >= 9 and len(version_parts) >= 3:
                        patch = int(version_parts[2])
                        if patch >= 5:
                            print("\n  ⚠⚠⚠ WARNING: LibreHardwareMonitor 0.9.5+ has BROKEN WMI support! ⚠⚠⚠")
                            print("  → Version 0.9.5 introduced a bug that breaks WMI sensor reporting")
                            print("  → See: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/issues/2088")
                            print("  → RECOMMENDED: Downgrade to 0.9.4 from GitHub Releases page")
            else:
                print(f"  → Could not detect version (please report this)")

        if not found_lhm:
            return False, "LibreHardwareMonitor is not running", (
                "FIX: Start LibreHardwareMonitor first\n\n"
                "   1. Launch LibreHardwareMonitor\n"
                "   2. IMPORTANT: Right-click and select 'Run as Administrator'\n"
                "   3. Keep it running while using this script"
            )
    except Exception as e:
        print(f"  ⚠ Could not check processes: {e}")

    # Check 3: Auto-discover WMI namespace
    print("\n[Check 3/4] Auto-discovering WMI namespace...")
    global discovered_wmi_namespace

    # Run auto-discovery
    _found_namespaces, working_namespace = discover_wmi_namespaces()

    if working_namespace:
        # Update global namespace with the one that works
        discovered_wmi_namespace = working_namespace
        print(f"\n  → Using namespace: {working_namespace}")
    else:
        # Auto-discovery failed - likely LibreHardwareMonitor 0.9.5+ with broken WMI
        # Try REST API fallback before giving up (LHM 0.9.5+ workaround)
        print(f"\n  ⚠ No working WMI namespace found (LHM 0.9.5+ issue)")
        print(f"  → Trying REST API fallback...")
        print(f"  → Checking http://{rest_api_host}:{rest_api_port}/data.json")

        global use_rest_api
        rest_success, rest_count, rest_error = check_rest_api_connectivity(rest_api_host, rest_api_port)

        # Debug: print REST API check result
        if rest_error:
            print(f"  ✗ REST API failed: {rest_error}")

        if rest_success and rest_count > 0:
            # REST API works! Use it instead of WMI
            use_rest_api = True
            print(f"\n  ✓✓✓ REST API WORKS! Using REST API instead of WMI ✓✓✓")
            print(f"  → Found {rest_count} sensors via REST API")
            print(f"  → This bypasses the WMI bug in LibreHardwareMonitor 0.9.5+")
            print(f"  → Make sure 'Remote Web Server' is enabled in LibreHardwareMonitor")
            print(f"     (Options → Remote Web Server → Run)")
            # Skip remaining checks - REST API is working
            return True, None, None

        # REST API also failed - provide simplified troubleshooting
        return False, "No working WMI namespace found", (
            "FIX: LibreHardwareMonitor 0.9.5+ has broken WMI support.\n\n"
            "SOLUTION (3 steps):\n\n"
            "1. Enable REST API in LibreHardwareMonitor:\n"
            "   Options → Remote Web Server → Run (port 8085)\n\n"
            "2. If that doesn't work, downgrade to 0.9.4:\n"
            "   https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases\n\n"
            "3. If still failing, add Windows Defender exclusion:\n"
            "   Add exclusion for LibreHardwareMonitor folder in Windows Security"
        )

    # Check 4: Verify we can actually read sensor data
    print("\n[Check 4/4] Testing sensor data access...")
    try:
        import wmi
        w = wmi.WMI(namespace=discovered_wmi_namespace)
        sensors = list(w.Sensor())

        if len(sensors) == 0:
            # CRITICAL: Namespace exists but no sensors found
            # Try REST API fallback before giving up (LHM 0.9.5+ workaround)
            print(f"  ⚠ WMI returned 0 sensors - trying REST API fallback...")
            print(f"  → Checking http://{rest_api_host}:{rest_api_port}/data.json")

            rest_success, rest_count, rest_error = check_rest_api_connectivity(rest_api_host, rest_api_port)

            if rest_success and rest_count > 0:
                # REST API works! Use it instead of WMI
                use_rest_api = True
                print(f"\n  ✓✓✓ REST API WORKS! Using REST API instead of WMI ✓✓✓")
                print(f"  → Found {rest_count} sensors via REST API")
                print(f"  → This bypasses the WMI bug in LibreHardwareMonitor 0.9.5+")
                print(f"  → Make sure 'Remote Web Server' is enabled in LibreHardwareMonitor")
                print(f"     (Options → Remote Web Server → Run)")
                return True, None, None

            # REST API also failed - show comprehensive error with all options
            error_msg = "WMI namespace accessible but contains 0 sensors!"
            if rest_error:
                error_msg += f"\nREST API also failed: {rest_error}"

            return False, error_msg, (
                "FIX: LibreHardwareMonitor driver is not providing sensor data.\n\n"
                "SOLUTION (3 steps):\n\n"
                "1. Enable REST API in LibreHardwareMonitor:\n"
                "   Options → Remote Web Server → Run (port 8085)\n\n"
                "2. If that doesn't work, downgrade to 0.9.4:\n"
                "   https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases\n\n"
                "3. If still failing, check for conflicts:\n"
                "   Close HWiNFO64, HWMonitor, AIDA64, or add Windows Defender exclusion"
            )

        print(f"  ✓ Sensor data is readable ({len(sensors)} sensors found)")
    except Exception as e:
        return False, f"Cannot read sensor data: {e}", (
            "FIX: Sensor access error\n\n"
            "   This may be a permission issue.\n"
            "   Try running both LibreHardwareMonitor and this script as Administrator."
        )

    print("\n" + "-" * 60)
    print("✓ All diagnostics passed!")
    print("-" * 60)
    return True, None, None


def print_troubleshooting_header(error_title):
    """Print a formatted troubleshooting header"""
    print("\n" + "!" * 60)
    print(f"ERROR: {error_title}")
    print("!" * 60)
    print("\nTROUBLESHOOTING STEPS:")
    print("-" * 60)


def print_troubleshooting_footer():
    """Print a formatted troubleshooting footer"""
    print("-" * 60)
    print("\nIf the problem persists, please share a screenshot of this")
    print("entire error message when reporting the issue.")
    print("\n" + "!" * 60 + "\n")


def discover_sensors():
    """
    Discover all available sensors from LibreHardwareMonitor and psutil
    """
    print("=" * 60)
    print("PC STATS MONITOR v3.0 - SENSOR DISCOVERY")
    print("=" * 60)

    # Add psutil system metrics
    print("\n[1/2] Discovering system metrics (psutil)...")

    # Warm up psutil for accurate readings
    psutil.cpu_percent(interval=0.1)
    sensor_database["system"].append({
        "name": "CPU",
        "display_name": "CPU Usage",
        "source": "psutil",
        "type": "percent",
        "unit": "%",
        "psutil_method": "cpu_percent",
        "custom_label": "",
        "current_value": int(psutil.cpu_percent(interval=0))
    })

    sensor_database["system"].append({
        "name": "RAM",
        "display_name": "RAM Usage",
        "source": "psutil",
        "type": "percent",
        "unit": "%",
        "psutil_method": "virtual_memory.percent",
        "custom_label": "",
        "current_value": int(psutil.virtual_memory().percent)
    })

    sensor_database["system"].append({
        "name": "RAM_GB",
        "display_name": "RAM Used (GB)",
        "source": "psutil",
        "type": "memory",
        "unit": "GB",
        "psutil_method": "virtual_memory.used",
        "custom_label": "",
        "current_value": int(psutil.virtual_memory().used / (1024**3))
    })

    sensor_database["system"].append({
        "name": "DISK",
        "display_name": "Disk C: Usage",
        "source": "psutil",
        "type": "percent",
        "unit": "%",
        "psutil_method": "disk_usage",
        "custom_label": "",
        "current_value": int(psutil.disk_usage('C:\\').percent)
    })

    print(f"  Found {len(sensor_database['system'])} system metrics")

    # Discover LibreHardwareMonitor sensors
    print("\n[2/2] Discovering hardware sensors (LibreHardwareMonitor)...")

    # Run diagnostics first to provide helpful error messages
    success, error_msg, suggestion = check_wmi_connectivity()

    if not success:
        print_troubleshooting_header(error_msg)
        print(suggestion)
        print_troubleshooting_footer()
        print("\n⚠ WARNING: Hardware sensors will NOT be available.")
        print("  Only system metrics (CPU, RAM, Disk) can be monitored.")
        print("\n  Press Enter to continue with system metrics only...")
        pause()
        return

    # Check if we should use REST API instead of WMI (LHM 0.9.5+ workaround)
    if use_rest_api:
        print("\n→ Using REST API for sensor discovery (LibreHardwareMonitor 0.9.5+ mode)")
        if not discover_sensors_via_http(rest_api_host, rest_api_port):
            print("\n⚠ REST API discovery failed. No hardware sensors available.")
            print("  Only system metrics (CPU, RAM, Disk) can be monitored.")
        print("\n" + "=" * 60)
        return

    # If diagnostics passed and not using REST API, proceed with WMI sensor discovery
    try:
        import wmi
        # Use the auto-discovered namespace
        w = wmi.WMI(namespace=discovered_wmi_namespace)
        sensors = w.Sensor()

        sensor_count = 0
        # Reset name tracker to ensure fresh unique names
        reset_generated_names()

        for sensor in sensors:
            # Generate short name for ESP32 display (using same function as REST API)
            short_name = generate_short_name_from_id(sensor.Identifier, sensor.SensorType.lower(), sensor.Name)

            # Enhance display name with identifier context for GUI
            display_name = sensor.Name
            identifier_parts = sensor.Identifier.split('/')
            if len(identifier_parts) > 1:
                device_info = identifier_parts[1]
                # Add device context to display name for clarity
                if device_info.lower() not in display_name.lower():
                    display_name = f"{sensor.Name} [{device_info}]"

                # Special handling for network data metrics (upload/download disambiguation)
                if sensor.SensorType.lower() == "data" and ('nic' in device_info.lower() or 'network' in device_info.lower()):
                    # Extract data metric index to distinguish upload/download
                    # /nic/0/data/0 = Download, /nic/0/data/1 = Upload, etc.
                    if len(identifier_parts) >= 4:
                        data_index = identifier_parts[-1]

                        # Check if name already has Upload/Download
                        name_lower = sensor.Name.lower()
                        if 'upload' not in name_lower and 'download' not in name_lower and 'rx' not in name_lower and 'tx' not in name_lower:
                            # Add Upload/Download based on data index
                            if data_index == '0':
                                display_name = f"{sensor.Name} - Download [{device_info}]"
                            elif data_index == '1':
                                display_name = f"{sensor.Name} - Upload [{device_info}]"
                            else:
                                display_name = f"{sensor.Name} #{data_index} [{device_info}]"

                # Special handling for network throughput metrics (upload/download disambiguation)
                elif sensor.SensorType.lower() == "throughput" and ('nic' in device_info.lower() or 'network' in device_info.lower()):
                    # Extract throughput metric index to distinguish upload/download
                    # /nic/0/throughput/0 = Upload Speed, /nic/0/throughput/1 = Download Speed
                    if len(identifier_parts) >= 4:
                        throughput_index = identifier_parts[-1]

                        # Check if name already has Upload/Download
                        name_lower = sensor.Name.lower()
                        if 'upload' not in name_lower and 'download' not in name_lower and 'rx' not in name_lower and 'tx' not in name_lower:
                            # Add Upload/Download based on throughput index
                            if throughput_index == '0':
                                display_name = f"{sensor.Name} - Upload [{device_info}]"
                            elif throughput_index == '1':
                                display_name = f"{sensor.Name} - Download [{device_info}]"
                            else:
                                display_name = f"{sensor.Name} #{throughput_index} [{device_info}]"

            # Get current sensor value
            try:
                current_value = int(sensor.Value) if sensor.Value else 0
            except:
                current_value = 0

            # Check if this is an active network interface (has traffic)
            is_active_nic = False
            sensor_type_lower = sensor.SensorType.lower()
            if "nic" in sensor.Identifier.lower() and sensor_type_lower == "throughput":
                if current_value > 0:
                    is_active_nic = True

            sensor_info = {
                "name": short_name,
                "display_name": display_name,
                "source": "wmi",
                "type": sensor_type_lower,
                "unit": get_unit_from_type(sensor.SensorType),
                "wmi_identifier": sensor.Identifier,
                "wmi_sensor_name": sensor.Name,
                "custom_label": "",
                "current_value": current_value,
                "is_active_nic": is_active_nic
            }

            # Categorize sensor — GPU sensors go to dedicated "gpu" category
            if _is_gpu_sensor(sensor.Identifier):
                sensor_database["gpu"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "temperature":
                sensor_database["temperature"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "fan":
                sensor_database["fan"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "load":
                sensor_database["load"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "clock":
                sensor_database["clock"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "power":
                sensor_database["power"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "data":
                sensor_database["data"].append(sensor_info)
                sensor_count += 1
            elif sensor_type_lower == "throughput":
                sensor_database["throughput"].append(sensor_info)
                sensor_count += 1
            else:
                sensor_database["other"].append(sensor_info)
                sensor_count += 1

        print(f"  Found {sensor_count} hardware sensors:")
        print(f"    - GPU:          {len(sensor_database['gpu'])}")
        print(f"    - Temperatures: {len(sensor_database['temperature'])}")
        print(f"    - Fans: {len(sensor_database['fan'])}")
        print(f"    - Loads: {len(sensor_database['load'])}")
        print(f"    - Clocks: {len(sensor_database['clock'])}")
        print(f"    - Power: {len(sensor_database['power'])}")
        print(f"    - Data: {len(sensor_database['data'])}")
        print(f"    - Throughput: {len(sensor_database['throughput'])}")
        if len(sensor_database['other']) > 0:
            print(f"    - Other: {len(sensor_database['other'])}")

    except ImportError:
        print("  WARNING: pywin32/wmi not installed. Hardware sensors unavailable.")
        print("  Install with: pip install pywin32 wmi")
    except Exception as e:
        # Fallback error handling (should rarely trigger since diagnostics run first)
        print_troubleshooting_header(f"Unexpected error during sensor discovery: {e}")
        print("This error occurred after initial diagnostics passed.")
        print("\nPossible causes:")
        print("  - LibreHardwareMonitor was closed after diagnostics")
        print("  - WMI connection was lost during sensor enumeration")
        print("  - System resource limitation")
        print("\nPlease try again:")
        print("  1. Make sure LibreHardwareMonitor is running as Administrator")
        print("  2. Restart this script")
        print_troubleshooting_footer()

    print("\n" + "=" * 60)
    print("\nℹ NOTE: Sensor values in GUI are static (captured at launch time)")
    print("  This helps you identify active sensors and their typical readings.")


def generate_short_name(full_name, sensor_type, identifier=""):
    """
    Generate a short name (max 10 chars) for ESP32 display with context
    """
    # Extract context from identifier path (e.g., /hdd/0/temperature/0 -> HDD0)
    device_prefix = ""
    device_index = ""

    if identifier:
        parts = identifier.split('/')
        if len(parts) > 1:
            device = parts[1].lower()

            # CPU/GPU/Motherboard prefixes
            if 'cpu' in device:
                device_prefix = "CPU_"
            elif 'gpu' in device or 'nvidia' in device or 'amd' in device:
                device_prefix = "GPU_"
            elif 'motherboard' in device or 'mainboard' in device:
                device_prefix = "MB_"
            # Storage devices (HDD, SSD, NVMe)
            elif 'hdd' in device or 'storage' in device:
                device_prefix = "HDD"
                # Extract drive number if present (e.g., /hdd/0 -> HDD0)
                if len(parts) > 2 and parts[2].isdigit():
                    device_index = parts[2]
            elif 'ssd' in device:
                device_prefix = "SSD"
                if len(parts) > 2 and parts[2].isdigit():
                    device_index = parts[2]
            elif 'nvme' in device:
                device_prefix = "NVM"
                if len(parts) > 2 and parts[2].isdigit():
                    device_index = parts[2]
            # Network adapters
            elif 'nic' in device or 'network' in device or 'ethernet' in device:
                device_prefix = "NET"
                if len(parts) > 2 and parts[2].isdigit():
                    device_index = parts[2]

    # Keep the original name but clean it up
    name = full_name.strip()

    # For temperature sensors, add device prefix
    if sensor_type.lower() == "temperature":
        # Remove "Temperature" word
        name = name.replace("Temperature", "").replace("temperature", "").strip()
        # Add device prefix if not already there
        if device_prefix and not name.upper().startswith(device_prefix.replace("_", "")):
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

    # For fans, preserve numbers and context
    elif sensor_type.lower() == "fan":
        # Keep "Fan #1" as "FAN1", "Pump" as "PUMP", etc.
        name = name.replace("Fan #", "FAN").replace("fan #", "FAN")
        name = name.replace("Chassis", "CHS").replace("System", "SYS")

    # For loads, add context
    elif sensor_type.lower() == "load":
        name = name.replace("Load", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

    # For power
    elif sensor_type.lower() == "power":
        name = name.replace("Package", "PKG").replace("Power", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

    # For data (network/disk usage)
    elif sensor_type.lower() == "data":
        name = name.replace("Data", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

        # For network metrics, add Upload/Download suffix if not already in name
        if device_prefix == "NET" and identifier:
            parts = identifier.split('/')
            name_lower = name.lower()
            # Check if upload/download not already specified
            if 'upload' not in name_lower and 'download' not in name_lower and 'u' not in name_lower.split('_')[-1] and 'd' not in name_lower.split('_')[-1]:
                # Extract data metric index: /nic/0/data/0 = Download, /nic/0/data/1 = Upload
                if len(parts) >= 4:
                    data_index = parts[-1]
                    if data_index == '0':
                        name = name + "_D"  # Download
                    elif data_index == '1':
                        name = name + "_U"  # Upload

    # For throughput (network speeds)
    elif sensor_type.lower() == "throughput":
        name = name.replace("Speed", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

        # For network throughput, add Upload/Download suffix
        if device_prefix == "NET" and identifier:
            parts = identifier.split('/')
            name_lower = name.lower()
            # Check if upload/download not already specified
            if 'upload' not in name_lower and 'download' not in name_lower and 'u' not in name_lower.split('_')[-1] and 'd' not in name_lower.split('_')[-1]:
                # Extract throughput metric index: /nic/0/throughput/0 = Upload, /nic/0/throughput/1 = Download
                if len(parts) >= 4:
                    throughput_index = parts[-1]
                    if throughput_index == '0':
                        name = name + "_U"  # Upload
                    elif throughput_index == '1':
                        name = name + "_D"  # Download

    # Clean up
    name = name.replace("  ", " ").replace(" ", "_")

    # Truncate if too long, but try to preserve meaning
    if len(name) > 10:
        # Remove underscores first to save space
        name = name.replace("_", "")
        if len(name) > 10:
            name = name[:10]

    return name if name else "SENSOR"


def get_unit_from_type(sensor_type):
    """
    Map sensor type to display unit
    """
    unit_map = {
        "Temperature": "C",
        "Load": "%",
        "Fan": "RPM",
        "Clock": "MHz",
        "Power": "W",
        "Voltage": "V",
        "Data": "GB",
        "Throughput": "KB/s"  # Network throughput speeds
    }
    return unit_map.get(sensor_type, "")


def load_config():
    """
    Load configuration from file with version checking
    """
    if not os.path.exists(CONFIG_FILE):
        return None

    try:
        with open(CONFIG_FILE, 'r') as f:
            config = json.load(f)

        # Version check - force reconfiguration for old versions
        config_version = config.get("version", "1.0")
        if config_version < "2.1":
            print("\n" + "=" * 60)
            print("  CONFIGURATION UPDATE REQUIRED")
            print("=" * 60)
            print(f"\n⚠ Configuration format updated (v{config_version} → v2.1)")
            print("\nMajor bug fixes in this version:")
            print("  ✓ Fixed duplicate metric names (HDDT, RAMUSED, etc.)")
            print("  ✓ Fixed memory metrics appearing in wrong category")
            print("  ✓ Removed companion markers (^^) from display")
            print("  ✓ Custom labels now visible in preview")
            print("  ✓ Improved GUI layout")
            print("\n→ You will need to reconfigure your metrics in the GUI.")

            # Backup old config
            backup_path = CONFIG_FILE.replace(".json", f"_v{config_version}_backup.json")
            try:
                import shutil
                shutil.copy(CONFIG_FILE, backup_path)
                print(f"\n  Old config backed up to: {backup_path}")
            except Exception as e:
                print(f"\n  Warning: Could not backup config: {e}")

            # Delete old config to force reconfiguration
            try:
                os.remove(CONFIG_FILE)
                print(f"  Old config removed: {CONFIG_FILE}")
            except Exception as e:
                print(f"  Warning: Could not remove old config: {e}")

            print("\n" + "=" * 60 + "\n")
            pause("Press Enter to continue to configuration GUI...")
            return None

        print(f"\n✓ Loaded configuration from {CONFIG_FILE}")
        print(f"  Selected metrics: {len(config.get('metrics', []))}")
        return config
    except Exception as e:
        print(f"\n✗ Error loading config: {e}")
        return None


def save_config(config):
    """
    Save configuration to file
    """
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=2)
        print(f"\n✓ Configuration saved to {CONFIG_FILE}")
        return True
    except Exception as e:
        print(f"\n✗ Error saving config: {e}")
        return False


# Windows "run at login" registry location (per-user, no admin needed).
AUTOSTART_REG_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
AUTOSTART_VALUE_NAME = "PCStatsMonitor"

# Legacy Startup-folder shortcut from older script-based installs. We clean it
# up when toggling autostart so users don't end up launching twice.
_LEGACY_SHORTCUT_NAME = "PC Monitor.lnk"


def _autostart_command():
    """
    The exact command Windows runs at login.

    Frozen .exe -> just the .exe (it already runs windowed/no-console).
    Plain script -> pythonw.exe + the script path so no console window appears.
    The 10s startup delay gives LibreHardwareMonitor time to come up first.
    """
    if IS_FROZEN:
        return f'"{EXE_PATH}" --minimized --startup-delay 10'
    pythonw = sys.executable.replace("python.exe", "pythonw.exe")
    return f'"{pythonw}" "{EXE_PATH}" --minimized --startup-delay 10'


def _remove_legacy_shortcut():
    """Best-effort removal of the old Startup-folder .lnk, if winshell is present."""
    try:
        import winshell
        old = os.path.join(winshell.startup(), _LEGACY_SHORTCUT_NAME)
        if os.path.exists(old):
            os.remove(old)
            print(f"  Removed legacy startup shortcut: {old}")
    except Exception:
        pass  # winshell not installed / nothing to clean - that's fine


def is_autostart_enabled():
    """Return True if our HKCU Run value exists."""
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, AUTOSTART_REG_KEY, 0,
                            winreg.KEY_QUERY_VALUE) as key:
            winreg.QueryValueEx(key, AUTOSTART_VALUE_NAME)
            return True
    except FileNotFoundError:
        return False
    except Exception:
        return False


def setup_autostart(enable=True):
    """
    Enable/disable "run at Windows login" via the per-user HKCU Run key.

    This needs no admin rights and no extra packages (winreg is stdlib). The
    monitor reads sensors over LibreHardwareMonitor's REST API, so it does not
    need to be elevated itself - only LHM does (its own setting).
    """
    import winreg

    if enable:
        command = _autostart_command()
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, AUTOSTART_REG_KEY) as key:
            winreg.SetValueEx(key, AUTOSTART_VALUE_NAME, 0, winreg.REG_SZ, command)
        _remove_legacy_shortcut()
        print(f"\n✓ Autostart enabled!")
        print(f"  Registry: HKCU\\{AUTOSTART_REG_KEY}\\{AUTOSTART_VALUE_NAME}")
        print(f"  Command:  {command}")
        print(f"  Startup delay: 10 seconds (waiting for LHM to start)")
        return True
    else:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, AUTOSTART_REG_KEY, 0,
                                winreg.KEY_SET_VALUE) as key:
                winreg.DeleteValue(key, AUTOSTART_VALUE_NAME)
            print(f"\n✓ Autostart disabled!")
        except FileNotFoundError:
            print("\n✓ Autostart was not enabled (nothing to remove)")
        _remove_legacy_shortcut()
        return True


# AutoConfigPreviewDialog class removed - will be revisited later


class MetricSelectorGUI:
    """
    Tkinter GUI for selecting metrics and configuring settings
    """
    def __init__(self, root, existing_config=None):
        self.root = root
        self.root.title("PC Monitor v3.0 - Configuration")
        self.root.geometry("1200x800")
        self.root.resizable(True, True)
        self.root.minsize(1000, 700)

        self.selected_metrics = []
        self.checkboxes = []
        self.label_entries = {}
        self.sections = []          # [{'frame': section_frame, 'rows': [checkbox tuples]}]
        self._search_after_id = None  # debounce handle for filter-mode search
        self._preview_after_id = None  # debounce handle for the auto-layout preview
        self.current_layout = None     # {"row_mode", "layout", "source"}

        # Load existing config if available
        if existing_config:
            self.config = existing_config
        else:
            self.config = DEFAULT_CONFIG.copy()

        self.create_widgets()

        # Load existing selections if editing
        if existing_config and existing_config.get("metrics"):
            self.load_existing_metrics(existing_config["metrics"])

    def create_widgets(self):
        # Title
        title_frame = tk.Frame(self.root, bg="#1e1e1e", height=45)
        title_frame.pack(fill=tk.X)
        title_frame.pack_propagate(False)

        title_label = tk.Label(
            title_frame,
            text="PC Monitor Configuration",
            font=("Arial", 18, "bold"),
            bg="#1e1e1e",
            fg="#00d4ff"
        )
        title_label.pack(pady=8)

        # Sensor-source status panel (LibreHardwareMonitor health + guidance).
        # This is the heart of the friendlier first-run experience: it tells the
        # user at a glance whether hardware sensors are available and what to do
        # if they're not, with a Rescan button so they don't have to restart.
        source_frame = tk.Frame(self.root, bg="#23272e")
        source_frame.pack(fill=tk.X, padx=10, pady=(0, 4))

        self.source_status_label = tk.Label(
            source_frame,
            text="Checking sensor source...",
            bg="#23272e",
            fg="#888888",
            font=("Arial", 10, "bold"),
            anchor="w",
            justify=tk.LEFT,
            wraplength=820
        )
        self.source_status_label.pack(side=tk.LEFT, padx=10, pady=6, fill=tk.X, expand=True)

        tk.Button(
            source_frame, text="↻ Rescan sensors", command=self.rescan_sensors,
            bg="#00d4ff", fg="#000000", font=("Arial", 9, "bold"),
            relief=tk.FLAT, padx=10, pady=2
        ).pack(side=tk.LEFT, padx=4, pady=6)

        tk.Button(
            source_frame, text="Get LibreHardwareMonitor", command=self.open_lhm_download,
            bg="#444444", fg="#ffffff", font=("Arial", 9),
            relief=tk.FLAT, padx=10, pady=2
        ).pack(side=tk.LEFT, padx=4, pady=6)

        tk.Button(
            source_frame, text="REST API help", command=self.show_rest_api_help,
            bg="#444444", fg="#ffffff", font=("Arial", 9),
            relief=tk.FLAT, padx=10, pady=2
        ).pack(side=tk.LEFT, padx=(4, 10), pady=6)

        # Settings area: two labeled groups side by side (Connection / Startup)
        settings_container = tk.Frame(self.root, bg="#1e1e1e")
        settings_container.pack(fill=tk.X, padx=10, pady=5)

        # --- ESP32 Connection group ---
        conn_frame = tk.LabelFrame(
            settings_container, text=" ESP32 Connection ",
            bg="#2d2d2d", fg="#00d4ff", font=("Arial", 10, "bold"),
            bd=1, relief=tk.GROOVE, labelanchor="nw"
        )
        conn_frame.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 8))

        # ESP IP
        tk.Label(conn_frame, text="ESP32 IP:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=0, padx=10, pady=4, sticky="e")
        self.ip_var = tk.StringVar(value=self.config.get("esp32_ip", "192.168.0.163"))
        tk.Entry(conn_frame, textvariable=self.ip_var, width=16).grid(row=0, column=1, padx=5, pady=4, sticky="w")

        # UDP Port
        tk.Label(conn_frame, text="UDP Port:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=2, padx=10, pady=4, sticky="e")
        self.port_var = tk.StringVar(value=str(self.config.get("udp_port", 4210)))
        tk.Entry(conn_frame, textvariable=self.port_var, width=8).grid(row=0, column=3, padx=5, pady=4, sticky="w")

        # Update Interval
        tk.Label(conn_frame, text="Interval (s):", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=4, padx=10, pady=4, sticky="e")
        self.interval_var = tk.StringVar(value=str(self.config.get("update_interval", 3)))
        tk.Entry(conn_frame, textvariable=self.interval_var, width=8).grid(row=0, column=5, padx=5, pady=4, sticky="w")

        # Test Connection button (verifies the ESP32 is reachable on the network)
        self.test_conn_btn = tk.Button(
            conn_frame, text="Test Connection", command=self.test_connection,
            bg="#00d4ff", fg="#000000", font=("Arial", 9), relief=tk.FLAT, padx=10, pady=2
        )
        self.test_conn_btn.grid(row=1, column=0, columnspan=6, padx=10, pady=(0, 6), sticky="w")

        # --- Startup group ---
        startup_frame = tk.LabelFrame(
            settings_container, text=" Startup ",
            bg="#2d2d2d", fg="#00d4ff", font=("Arial", 10, "bold"),
            bd=1, relief=tk.GROOVE, labelanchor="nw"
        )
        startup_frame.pack(side=tk.LEFT, fill=tk.Y)

        tk.Label(startup_frame, text="Windows Autostart:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=0, padx=10, pady=4, sticky="e")

        autostart_frame = tk.Frame(startup_frame, bg="#2d2d2d")
        autostart_frame.grid(row=0, column=1, padx=5, pady=4, sticky="w")

        self.autostart_status = tk.Label(
            autostart_frame,
            text=self.get_autostart_status_text(),
            bg="#2d2d2d",
            fg=self.get_autostart_status_color(),
            font=("Arial", 10, "bold")
        )
        self.autostart_status.pack(side=tk.LEFT, padx=5)

        enable_btn = tk.Button(
            autostart_frame,
            text="Enable",
            command=self.enable_autostart,
            bg="#00d4ff",
            fg="#000000",
            font=("Arial", 9),
            relief=tk.FLAT,
            padx=10,
            pady=2
        )
        enable_btn.pack(side=tk.LEFT, padx=5)

        disable_btn = tk.Button(
            autostart_frame,
            text="Disable",
            command=self.disable_autostart,
            bg="#ff6666",
            fg="#000000",
            font=("Arial", 9),
            relief=tk.FLAT,
            padx=10,
            pady=2
        )
        disable_btn.pack(side=tk.LEFT, padx=5)

        # Counter frame
        counter_frame = tk.Frame(self.root, bg="#2d2d2d", height=50)
        counter_frame.pack(fill=tk.X)
        counter_frame.pack_propagate(False)

        self.counter_label = tk.Label(
            counter_frame,
            text=f"Selected: 0/{MAX_METRICS}",
            font=("Arial", 12),
            bg="#2d2d2d",
            fg="#ffffff"
        )
        self.counter_label.pack(side=tk.LEFT, padx=20, pady=10)

        # Search box
        search_label = tk.Label(counter_frame, text="Search:", bg="#2d2d2d", fg="#ffffff")
        search_label.pack(side=tk.LEFT, padx=(50, 5))

        # filter_var must exist before on_search runs (search trace below)
        self.filter_var = tk.BooleanVar(value=False)

        self.search_var = tk.StringVar()
        self.search_var.trace_add('write', lambda *_: self._on_search_var_changed())
        search_entry = tk.Entry(counter_frame, textvariable=self.search_var, width=20)
        search_entry.pack(side=tk.LEFT, padx=5)

        # When checked, non-matching sensors are hidden instead of just dimmed
        filter_check = tk.Checkbutton(
            counter_frame, text="Hide non-matching", variable=self.filter_var,
            command=self.on_filter_toggle, bg="#2d2d2d", fg="#ffffff",
            selectcolor="#444444", activebackground="#2d2d2d",
            activeforeground="#ffffff", font=("Arial", 9)
        )
        filter_check.pack(side=tk.LEFT, padx=(8, 0))

        # Info note about static values
        info_label = tk.Label(
            counter_frame,
            text="ℹ Values are static from GUI launch",
            bg="#2d2d2d",
            fg="#888888",
            font=("Arial", 9, "italic")
        )
        info_label.pack(side=tk.LEFT, padx=(20, 0))

        # Buttons
        clear_btn = tk.Button(
            counter_frame,
            text="Clear All",
            command=self.clear_all,
            bg="#444444",
            fg="#ffffff",
            relief=tk.FLAT,
            padx=10
        )
        clear_btn.pack(side=tk.RIGHT, padx=20)

        # Main scrollable frame
        main_frame = tk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        canvas = tk.Canvas(main_frame, bg="#ffffff")
        scrollbar = ttk.Scrollbar(main_frame, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas, bg="#ffffff")

        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        # Create window that fills canvas width
        canvas_window = canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        # Make scrollable_frame fill canvas width
        def on_canvas_configure(event):
            canvas.itemconfig(canvas_window, width=event.width)
        canvas.bind("<Configure>", on_canvas_configure)

        # Mouse wheel scrolling
        def on_mousewheel(event):
            canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        # Bind mousewheel to canvas and all children
        def bind_mousewheel(widget):
            widget.bind("<MouseWheel>", on_mousewheel)
            for child in widget.winfo_children():
                bind_mousewheel(child)

        canvas.bind("<MouseWheel>", on_mousewheel)
        scrollable_frame.bind("<MouseWheel>", on_mousewheel)

        # Store references so build_sensor_checkboxes() / rescan can reuse them
        self.canvas = canvas
        self.on_mousewheel = on_mousewheel
        self.scrollable_frame = scrollable_frame

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        # Build the per-category checkbox list (extracted so Rescan can rebuild it)
        self.build_sensor_checkboxes()

        # Preview frame
        preview_frame = tk.Frame(self.root, bg="#2d2d2d", height=40)
        preview_frame.pack(fill=tk.X)
        preview_frame.pack_propagate(False)

        preview_label = tk.Label(
            preview_frame,
            text="SELECTED PREVIEW (names sent to ESP32):",
            font=("Arial", 9, "bold"),
            bg="#2d2d2d",
            fg="#888888"
        )
        preview_label.pack(anchor="w", padx=10, pady=(5, 0))

        self.preview_text = tk.Label(
            preview_frame,
            text="",
            font=("Courier", 9),
            bg="#2d2d2d",
            fg="#00ff00",
            anchor="w",
            justify=tk.LEFT
        )
        self.preview_text.pack(fill=tk.X, padx=10, pady=(0, 5))

        # ---- Auto Layout: pick a template, see a live device preview, push it ----
        auto_frame = tk.LabelFrame(
            self.root, text=" Auto Layout (configure the screen in one click) ",
            bg="#2d2d2d", fg="#00d4ff", font=("Arial", 10, "bold"),
            bd=1, relief=tk.GROOVE, labelanchor="nw"
        )
        auto_frame.pack(fill=tk.X, padx=10, pady=(0, 4))

        # Left: controls.  Right: 128x64 device preview at 2x.
        auto_controls = tk.Frame(auto_frame, bg="#2d2d2d")
        auto_controls.pack(side=tk.LEFT, fill=tk.Y, padx=10, pady=6)

        tk.Label(auto_controls, text="Template:", bg="#2d2d2d", fg="#ffffff",
                 font=("Arial", 10)).grid(row=0, column=0, sticky="w", padx=(0, 6))

        self._template_labels = [label for _key, label in LAYOUT_TEMPLATES]
        self._template_key_by_label = {label: key for key, label in LAYOUT_TEMPLATES}
        self.template_var = tk.StringVar(value=self._template_labels[0])
        template_combo = ttk.Combobox(
            auto_controls, textvariable=self.template_var, state="readonly",
            values=self._template_labels, width=24
        )
        template_combo.grid(row=0, column=1, sticky="w")
        template_combo.bind("<<ComboboxSelected>>", lambda _e: self.refresh_layout_preview())

        tk.Label(
            auto_controls,
            text="The device is laid out automatically and pushed over\n"
                 "the network. No need to open the device web page.",
            bg="#2d2d2d", fg="#888888", font=("Arial", 9), justify=tk.LEFT, anchor="w"
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(4, 2))

        self.overflow_label = tk.Label(
            auto_controls, text="", bg="#2d2d2d", fg="#ffb454",
            font=("Arial", 9, "bold"), justify=tk.LEFT, anchor="w"
        )
        self.overflow_label.grid(row=2, column=0, columnspan=2, sticky="w")

        self.apply_layout_btn = tk.Button(
            auto_controls, text="Apply layout + Start monitoring",
            command=self.apply_layout_and_start,
            bg="#00d4ff", fg="#000000", font=("Arial", 11, "bold"),
            relief=tk.FLAT, padx=16, pady=5
        )
        self.apply_layout_btn.grid(row=3, column=0, sticky="w", pady=(6, 0))

        self.customize_layout_btn = tk.Button(
            auto_controls, text="Customize layout...",
            command=self.open_layout_editor,
            bg="#555555", fg="#ffffff", font=("Arial", 10),
            relief=tk.FLAT, padx=10, pady=5, state=tk.DISABLED
        )
        self.customize_layout_btn.grid(row=3, column=1, sticky="w", padx=(8, 0), pady=(6, 0))

        # Right: the preview canvas (128x64 scaled by PREVIEW_SCALE)
        self.PREVIEW_SCALE = 2
        preview_right = tk.Frame(auto_frame, bg="#2d2d2d")
        preview_right.pack(side=tk.RIGHT, padx=10, pady=6)
        tk.Label(preview_right, text="Device preview (128x64)", bg="#2d2d2d",
                 fg="#888888", font=("Arial", 8)).pack(anchor="e")
        self.preview_canvas = tk.Canvas(
            preview_right, width=128 * self.PREVIEW_SCALE, height=64 * self.PREVIEW_SCALE,
            bg="#000000", highlightthickness=1, highlightbackground="#555555"
        )
        self.preview_canvas.pack()

        # Bottom buttons
        button_frame = tk.Frame(self.root, bg="#1e1e1e", height=50)
        button_frame.pack(fill=tk.X)
        button_frame.pack_propagate(False)

        cancel_btn = tk.Button(
            button_frame,
            text="Cancel",
            command=self.root.quit,
            bg="#666666",
            fg="#ffffff",
            font=("Arial", 12),
            relief=tk.FLAT,
            padx=20,
            pady=5
        )
        cancel_btn.pack(side=tk.LEFT, padx=20, pady=8)

        import_btn = tk.Button(
            button_frame,
            text="Import Settings",
            command=self.import_settings,
            bg="#555555",
            fg="#ffffff",
            font=("Arial", 11),
            relief=tk.FLAT,
            padx=15,
            pady=5
        )
        import_btn.pack(side=tk.LEFT, padx=8, pady=8)

        export_btn = tk.Button(
            button_frame,
            text="Export Settings",
            command=self.export_settings,
            bg="#555555",
            fg="#ffffff",
            font=("Arial", 11),
            relief=tk.FLAT,
            padx=15,
            pady=5
        )
        export_btn.pack(side=tk.LEFT, padx=8, pady=8)

        save_btn = tk.Button(
            button_frame,
            text="Save & Start Monitoring",
            command=self.save_and_start,
            bg="#00d4ff",
            fg="#000000",
            font=("Arial", 12, "bold"),
            relief=tk.FLAT,
            padx=20,
            pady=5
        )
        save_btn.pack(side=tk.RIGHT, padx=20, pady=8)

        # Save without starting the monitor loop
        save_only_btn = tk.Button(
            button_frame,
            text="Save",
            command=self.save_only,
            bg="#3a3a3a",
            fg="#ffffff",
            font=("Arial", 12),
            relief=tk.FLAT,
            padx=20,
            pady=5
        )
        save_only_btn.pack(side=tk.RIGHT, padx=(8, 0), pady=8)

        # Update counter + sensor-source status
        self.update_counter()
        self.refresh_source_status()

    def build_sensor_checkboxes(self):
        """(Re)build the sensor list from sensor_database in balanced columns.

        Sensors flow continuously into up to 3 columns balanced by height, so a
        single very large category (e.g. GPU) is split across columns instead of
        making one column enormous while the others sit empty. When a category
        spills into the next column its header is repeated with a "(cont.)" tag.

        Extracted from create_widgets so the 'Rescan sensors' button can rebuild
        the list in place after the user starts or configures LibreHardwareMonitor.
        """
        on_mousewheel = self.on_mousewheel
        scrollable_frame = self.scrollable_frame

        # Clear any previous content (rescan) and reset trackers
        for child in scrollable_frame.winfo_children():
            child.destroy()
        self.checkboxes = []
        self.label_entries = {}
        self.sections = []

        categories = [
            ("SYSTEM METRICS", "system"),
            ("GPU METRICS", "gpu"),
            ("TEMPERATURES", "temperature"),
            ("FANS & COOLING", "fan"),
            ("LOADS", "load"),
            ("CLOCKS", "clock"),
            ("POWER", "power"),
            ("NETWORK DATA", "data"),
            ("NETWORK THROUGHPUT", "throughput")
        ]
        visible_categories = [(title, key) for title, key in categories if sensor_database.get(key)]

        # Flatten to an ordered stream of layout items (headers + sensor rows),
        # each with an approximate height weight used to balance the columns.
        HEADER_W, SENSOR_W = 2, 3
        items = []  # list of (kind, payload, weight); kind in {"header", "sensor"}
        total_sensors = 0
        for title, key in visible_categories:
            items.append(("header", title, HEADER_W))
            for sensor in sensor_database[key]:
                items.append(("sensor", sensor, SENSOR_W))
                total_sensors += 1

        num_cols = min(3, max(1, total_sensors))

        # Create equal-width column container frames
        column_frames = []
        for col in range(num_cols):
            scrollable_frame.columnconfigure(col, weight=1, uniform="columns")
            col_frame = tk.Frame(scrollable_frame, bg="#ffffff")
            col_frame.grid(row=0, column=col, sticky="nsew", padx=0, pady=0)
            col_frame.bind("<MouseWheel>", on_mousewheel)
            column_frames.append(col_frame)

        target = sum(w for _, _, w in items) / num_cols if items else 0  # ideal weight/column

        # Assign items to columns, balancing by accumulated weight. A category that
        # crosses a column boundary gets its header repeated (continued=True).
        col = 0
        col_weight = 0.0
        current_title = None
        col_items = [[] for _ in range(num_cols)]  # each: (kind, payload, continued)

        for kind, payload, weight in items:
            if kind == "header":
                current_title = payload
                # Don't strand a header at the very bottom of a column
                if col < num_cols - 1 and col_weight >= target:
                    col += 1
                    col_weight = 0.0
                col_items[col].append(("header", payload, False))
                col_weight += weight
            else:
                # Wrap to the next column when full, repeating the category header
                if col < num_cols - 1 and col_weight >= target:
                    col += 1
                    col_weight = 0.0
                    if current_title is not None:
                        col_items[col].append(("header", current_title, True))
                        col_weight += HEADER_W
                col_items[col].append(("sensor", payload, False))
                col_weight += weight

        # Render each column's items into bordered section frames. Each section
        # is tracked in self.sections so the search filter can hide a header
        # whose rows are all filtered out.
        for col in range(num_cols):
            parent_frame = column_frames[col]
            current_section = None
            for kind, payload, continued in col_items[col]:
                if kind == "header":
                    section_frame = tk.Frame(parent_frame, bg="#f0f0f0", relief=tk.RIDGE, borderwidth=2)
                    section_frame.pack(fill=tk.X, padx=5, pady=5, anchor="n")
                    header_text = payload + ("  (cont.)" if continued else "")
                    cat_label = tk.Label(
                        section_frame, text=header_text,
                        font=("Arial", 11, "bold"), bg="#f0f0f0", fg="#333333"
                    )
                    cat_label.pack(pady=5)
                    section_frame.bind("<MouseWheel>", on_mousewheel)
                    cat_label.bind("<MouseWheel>", on_mousewheel)
                    current_section = {'frame': section_frame, 'rows': []}
                    self.sections.append(current_section)
                else:
                    if current_section is None:
                        # Safety net: a sensor with no preceding header
                        section_frame = tk.Frame(parent_frame, bg="#f0f0f0", relief=tk.RIDGE, borderwidth=2)
                        section_frame.pack(fill=tk.X, padx=5, pady=5, anchor="n")
                        current_section = {'frame': section_frame, 'rows': []}
                        self.sections.append(current_section)
                    self._build_sensor_row(current_section['frame'], payload)
                    current_section['rows'].append(self.checkboxes[-1])

    def _build_sensor_row(self, parent, sensor):
        """Create one sensor checkbox + custom-label row inside `parent`.

        Registers the row in self.checkboxes and self.label_entries and wires up
        the live label character counter. Shared by the column layout builder.
        """
        on_mousewheel = self.on_mousewheel
        var = tk.BooleanVar()

        # Highlight active network interfaces
        is_active = sensor.get('is_active_nic', False)
        if is_active:
            frame_bg = "#d4ffd4"  # Light green background
            text_color = "#006600"  # Dark green text
            active_marker = " ★"  # Star to mark active
        else:
            frame_bg = "#f0f0f0"
            text_color = "#000000"
            active_marker = ""

        # Create sensor row frame
        sensor_frame = tk.Frame(parent, bg=frame_bg)
        sensor_frame.pack(fill=tk.X, padx=10, pady=2)

        # Checkbox with current value
        value_text = f" - {sensor['current_value']}{sensor['unit']}" if sensor.get('current_value') is not None else ""
        cb = tk.Checkbutton(
            sensor_frame,
            text=f"{sensor['display_name']} ({sensor['name']}){value_text}{active_marker}",
            variable=var,
            bg=frame_bg,
            fg=text_color,
            selectcolor="#ffffff",
            anchor="w",
            command=lambda s=sensor, v=var: self.on_checkbox_toggle(s, v)
        )
        cb.pack(side=tk.TOP, fill=tk.X)

        # Custom label entry (small, below checkbox)
        label_frame = tk.Frame(sensor_frame, bg=frame_bg)
        label_frame.pack(side=tk.TOP, fill=tk.X, padx=20)

        tk.Label(label_frame, text="Label:", bg=frame_bg, fg="#666", font=("Arial", 8)).pack(side=tk.LEFT)
        label_entry = tk.Entry(label_frame, width=12, font=("Arial", 8))
        label_entry.pack(side=tk.LEFT, padx=5)

        # Live character counter (ESP32 shows max 10 chars; longer is truncated on save)
        counter_label = tk.Label(label_frame, text="0/10", bg=frame_bg, fg="#999999", font=("Arial", 8))
        counter_label.pack(side=tk.LEFT)

        # Store reference to label entry, frame and counter (key by sensor path)
        sensor_key = sensor.get('wmi_identifier') or f"{sensor['source']}_{sensor['display_name']}"
        info = {
            'entry': label_entry,
            'frame': label_frame,
            'counter': counter_label
        }
        self.label_entries[sensor_key] = info

        # Update counter + preview when label text changes
        label_entry.bind("<KeyRelease>", lambda e, i=info: self._on_label_key(i))

        # Bind mousewheel to all created widgets
        for widget in [sensor_frame, cb, label_frame, label_entry, counter_label]:
            widget.bind("<MouseWheel>", on_mousewheel)

        self.checkboxes.append((cb, sensor, var, sensor_frame))

    def rescan_sensors(self):
        """Re-run discovery after the user starts/enables LHM, then rebuild the
        list in place - preserving current selections and typed labels."""
        global _SUPPRESS_PAUSE, use_rest_api

        # Remember current selections + typed labels (keyed by sensor identity)
        prev_selected = set()
        for s in self.selected_metrics:
            prev_selected.add(s.get('wmi_identifier') or f"{s['source']}_{s['display_name']}")
        prev_labels = {}
        for key, info in self.label_entries.items():
            txt = info['entry'].get().strip()
            if txt:
                prev_labels[key] = txt

        self.source_status_label.config(text="↻ Rescanning sensors, please wait...", fg="#00d4ff")
        self.root.update_idletasks()

        # Reset the global database and re-discover (suppress console prompts)
        for k in list(sensor_database.keys()):
            sensor_database[k] = []
        use_rest_api = False
        _SUPPRESS_PAUSE = True
        try:
            discover_sensors()
        except Exception as e:
            print(f"Rescan error: {e}")
        finally:
            _SUPPRESS_PAUSE = False

        # Rebuild and restore prior selections + labels
        self.selected_metrics = []
        self.build_sensor_checkboxes()
        for cb, sensor, var, frame in self.checkboxes:
            key = sensor.get('wmi_identifier') or f"{sensor['source']}_{sensor['display_name']}"
            if key in prev_labels and key in self.label_entries:
                self.label_entries[key]['entry'].delete(0, tk.END)
                self.label_entries[key]['entry'].insert(0, prev_labels[key])
            if key in prev_selected:
                var.set(True)
                if sensor not in self.selected_metrics:
                    self.selected_metrics.append(sensor)

        self.refresh_all_label_counters()
        self.refresh_source_status()
        self.update_counter()

    def compute_source_status(self):
        """Return (text, color) describing the current hardware-sensor source."""
        hw_count = sum(len(v) for k, v in sensor_database.items() if k != "system")
        if hw_count > 0:
            src = "REST API" if use_rest_api else "WMI"
            return (f"✓ LibreHardwareMonitor connected via {src}  -  "
                    f"{hw_count} hardware sensors available.", "#33dd55")
        if not is_lhm_process_running():
            return ("✗ LibreHardwareMonitor is not running. Only CPU / RAM / Disk are available.   "
                    "Start it (Run as Administrator), then click Rescan.", "#ff6b6b")
        return ("⚠ LibreHardwareMonitor is running but exposes no sensors.   "
                "Enable Options - Remote Web Server, then click Rescan (see \"REST API help\").", "#ffcc44")

    def refresh_source_status(self):
        """Update the sensor-source status banner."""
        try:
            text, color = self.compute_source_status()
            self.source_status_label.config(text=text, fg=color)
        except Exception:
            pass

    def open_lhm_download(self):
        """Open the LibreHardwareMonitor releases page in the default browser."""
        webbrowser.open("https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases")

    def show_rest_api_help(self):
        """Explain how to enable the REST API / always-on operation."""
        messagebox.showinfo(
            "Enable LibreHardwareMonitor REST API",
            "To expose hardware sensors (temps, fans, GPU, power) to this app:\n\n"
            "1. Open LibreHardwareMonitor (Run as Administrator).\n"
            "2. Menu: Options -> Remote Web Server -> Run.\n"
            "   (Default port 8085 - this app expects 8085.)\n"
            "3. Keep LibreHardwareMonitor running.\n"
            "4. Back here, click \"Rescan sensors\".\n\n"
            "For always-on operation, also enable in LibreHardwareMonitor Options:\n"
            "  - Run On Windows Startup\n"
            "  - Start Minimized / Minimize To Tray\n"
            "  - Remote Web Server (so the API is ready at boot)\n\n"
            "Note: older LibreHardwareMonitor 0.9.4 works via WMI without the\n"
            "Remote Web Server; 0.9.5+ needs the REST API enabled."
        )

    def load_existing_metrics(self, metrics):
        """Load existing metric selections when editing config"""
        for metric in metrics:
            # Find matching sensor and check it
            for cb, sensor, var, frame in self.checkboxes:
                # Primary match: use wmi_identifier (sensor path) - most reliable
                if sensor.get('wmi_identifier') and metric.get('wmi_identifier'):
                    if sensor['wmi_identifier'] == metric['wmi_identifier']:
                        match = True
                    else:
                        continue
                else:
                    # Fallback: match by source + display_name
                    sensor_key = f"{sensor['source']}_{sensor['display_name']}"
                    metric_key = f"{metric['source']}_{metric['display_name']}"
                    match = (sensor_key == metric_key)

                if match:
                    # Explicitly add to selected_metrics (duplicate check in on_checkbox_toggle prevents double-adds)
                    if sensor not in self.selected_metrics:
                        self.selected_metrics.append(sensor)

                    # Set checkbox (this will trigger on_checkbox_toggle which handles showing label entry)
                    var.set(True)

                    # Set custom label if exists - use wmi_identifier as key
                    label_key = sensor.get('wmi_identifier') or f"{sensor['source']}_{sensor['display_name']}"
                    if metric.get('custom_label') and label_key in self.label_entries:
                        self.label_entries[label_key]['entry'].insert(0, metric['custom_label'])
                    break

        # Refresh label character counters for any labels just inserted
        self.refresh_all_label_counters()

        # Force update after all metrics loaded to ensure preview refreshes
        self.root.after(100, self.update_counter)

    def on_checkbox_toggle(self, sensor, var):
        if var.get():
            if len(self.selected_metrics) >= MAX_METRICS:
                messagebox.showwarning(
                    "Limit Reached",
                    f"Maximum {MAX_METRICS} metrics allowed!\nDeselect some metrics first."
                )
                var.set(False)
                return
            # Check for duplicates before appending
            if sensor not in self.selected_metrics:
                self.selected_metrics.append(sensor)
        else:
            if sensor in self.selected_metrics:
                self.selected_metrics.remove(sensor)

        self.update_counter()

    def get_display_label_for_metric(self, sensor):
        """Get custom label if set, otherwise return sensor name"""
        sensor_key = sensor.get('wmi_identifier') or f"{sensor['source']}_{sensor['display_name']}"
        if sensor_key in self.label_entries:
            custom = self.label_entries[sensor_key]['entry'].get().strip()
            if custom:
                return custom[:10]  # Enforce 10 char limit
        return sensor['name']

    def update_counter(self):
        count = len(self.selected_metrics)
        self.counter_label.config(text=f"Selected: {count}/{MAX_METRICS}")

        if count >= MAX_METRICS:
            self.counter_label.config(fg="#ff6666")
        else:
            self.counter_label.config(fg="#ffffff")

        # Update preview - now shows custom labels
        preview = " | ".join([f"{i+1}. {self.get_display_label_for_metric(m)}" for i, m in enumerate(self.selected_metrics[:MAX_METRICS])])
        self.preview_text.config(text=preview if preview else "(none selected)")

        # Keep the auto-layout device preview in sync with the selection.
        self.schedule_preview_refresh()

    # ---- Auto Layout: input building, preview, and one-click push ----

    def _build_layout_input(self):
        """Metric dicts (with sequential ids + resolved labels) for the layout engine.

        Mirrors the id assignment in build_config_from_gui so the pushed layout
        binds to the live UDP metrics by id.
        """
        metrics = []
        for i, sensor in enumerate(self.selected_metrics[:MAX_METRICS]):
            metrics.append({
                "id": i + 1,
                "name": sensor.get("name", ""),
                "type": sensor.get("type", ""),
                "unit": sensor.get("unit", ""),
                "label": self.get_display_label_for_metric(sensor),
                "current_value": sensor.get("current_value", 0),
                "is_active_nic": sensor.get("is_active_nic", False),
            })
        return metrics

    def _current_template_key(self):
        return self._template_key_by_label.get(self.template_var.get(), "compact")

    def schedule_preview_refresh(self):
        """Debounce preview re-renders so rapid selection changes stay snappy."""
        if getattr(self, "preview_canvas", None) is None:
            return  # widgets not built yet
        if self._preview_after_id is not None:
            self.root.after_cancel(self._preview_after_id)
        self._preview_after_id = self.root.after(200, self.refresh_layout_preview)

    def refresh_layout_preview(self):
        """Run the engine for the current selection + template and draw the preview."""
        self._preview_after_id = None
        if getattr(self, "preview_canvas", None) is None:
            return
        metrics = self._build_layout_input()
        tpl_key = self._current_template_key()
        row_mode, layout_by_id, hidden = auto_layout(metrics, tpl_key)
        self.current_layout = {
            "row_mode": row_mode,
            "layout": layout_by_id,
            "source": tpl_key,
            "show_clock": False,
            "clock_position": 0,
        }
        metrics_by_id = {m["id"]: m for m in metrics}
        self._render_preview(row_mode, layout_by_id, metrics_by_id)
        if hidden > 0:
            self.overflow_label.config(
                text=f"⚠ {hidden} metric(s) won't fit and are hidden.\n"
                     "Pick another template or deselect some sensors."
            )
        else:
            self.overflow_label.config(text="")
        self._update_customize_btn_state()

    def _preview_value_str(self, m):
        """A short VALUE+UNIT string for the preview (illustrative, not live)."""
        unit = m.get("unit", "")
        if unit == "KB/s":
            return "1.2K"  # neutral placeholder (current_value encoding varies)
        cv = m.get("current_value", 0) or 0
        return f"{cv}{unit}"

    def _slot_xy(self, row_mode, slot):
        sc = self.current_layout.get("show_clock", False) if self.current_layout else False
        cp = self.current_layout.get("clock_position", 0) if self.current_layout else 0
        return slot_geometry(row_mode, slot, sc, cp)

    def _render_preview(self, row_mode, layout_by_id, metrics_by_id):
        """Draw the generated layout onto the 128x64 preview canvas (scaled)."""
        c = self.preview_canvas
        c.delete("all")
        S = self.PREVIEW_SCALE
        is_large = row_mode >= 2
        font = ("Courier New", 15, "bold") if is_large else ("Courier New", 8)
        text_h = 16 if is_large else 8

        # Progress bars first (they own their slot, overriding text there).
        for mid, e in layout_by_id.items():
            if e.get("barPosition", 255) == 255:
                continue
            geo = self._slot_xy(row_mode, e["barPosition"])
            if geo is None:
                continue
            x, y, col_w = geo
            bar_h = 16 if is_large else 8
            width = min(e.get("barWidth", 60), col_w)
            c.create_rectangle(x * S, y * S, (x + width) * S, (y + bar_h) * S,
                               outline="#ffffff")
            m = metrics_by_id.get(mid, {})
            bmin, bmax = e.get("barMin", 0), e.get("barMax", 100)
            rng = (bmax - bmin) or 100
            val = m.get("current_value", 0) or 0
            frac = max(0.0, min(1.0, (val - bmin) / rng))
            fill_w = int((width - 2) * frac)
            if fill_w > 0:
                c.create_rectangle((x + 1) * S, (y + 1) * S,
                                   (x + 1 + fill_w) * S, (y + bar_h - 1) * S,
                                   fill="#ffffff", outline="")

        # Text metrics (with companions appended / right-aligned).
        for mid, e in layout_by_id.items():
            if e.get("position", 255) == 255:
                continue
            geo = self._slot_xy(row_mode, e["position"])
            if geo is None:
                continue
            x, y, _col_w = geo
            m = metrics_by_id.get(mid, {})
            label = (e.get("label") or m.get("name", ""))[:10]
            text = f"{label}:{self._preview_value_str(m)}"
            max_chars = 10 if is_large else 11
            comp_id = e.get("companionId", 0)
            comp = metrics_by_id.get(comp_id) if comp_id else None
            if comp is not None and not is_large:
                text = (text + f" {self._preview_value_str(comp)}")[:max_chars + 6]
            else:
                text = text[:max_chars]
            c.create_text(x * S + 1, y * S, text=text, anchor="nw",
                          fill="#ffffff", font=font)
            # Large modes: companion is right-aligned on the same row.
            if comp is not None and is_large:
                c.create_text(128 * S - 1, y * S, text=self._preview_value_str(comp),
                              anchor="ne", fill="#ffffff", font=font)

        # Empty-state hint.
        if not any(e.get("position", 255) != 255 or e.get("barPosition", 255) != 255
                   for e in layout_by_id.values()):
            c.create_text(64 * S, 32 * S, text="(no metrics selected)",
                          fill="#666666", font=("Courier New", 9))

    def apply_layout_and_start(self):
        """Validate, push current_layout to the device, then start monitoring."""
        config = self.build_config_from_gui()
        if config is None:
            return

        if self.current_layout is None:
            metrics = self._build_layout_input()
            row_mode, layout_by_id, _hidden = auto_layout(metrics, self._current_template_key())
            show_clock, clock_position = False, 0
        else:
            row_mode = self.current_layout["row_mode"]
            layout_by_id = self.current_layout["layout"]
            show_clock = self.current_layout.get("show_clock", False)
            clock_position = self.current_layout.get("clock_position", 0)
        payload = build_device_layout_json(row_mode, layout_by_id, show_clock, clock_position)

        if not save_config(config):
            messagebox.showerror("Error", "Failed to save configuration!")
            return

        esp_ip = config["esp32_ip"]
        self.apply_layout_btn.config(state=tk.DISABLED, text="Applying...")
        self.root.update_idletasks()

        def worker():
            try:
                ok, detail = push_layout_to_device(esp_ip, payload)
            except Exception as e:  # urllib/OSError -> device unreachable etc.
                ok, detail = False, str(e)
            self.root.after(0, lambda: self._apply_layout_done(ok, detail, esp_ip))

        threading.Thread(target=worker, daemon=True).start()

    def _apply_layout_done(self, ok, detail, esp_ip):
        """Report the push result on the UI thread; start monitoring on success."""
        self.apply_layout_btn.config(state=tk.NORMAL, text="Apply layout + Start monitoring")
        if ok:
            messagebox.showinfo(
                "Layout applied - running in the background",
                f"The device at {esp_ip} was configured and PC Monitor is now "
                "sending data.\n\n"
                "It runs in the system tray (the up-arrow ^ area by the clock) - "
                "right-click the icon to reconfigure or quit."
            )
            self.root.quit()  # same as Save & Start: hand off to the monitor loop
        else:
            messagebox.showwarning(
                "Could not apply layout",
                f"Reached the apply step but the device did not confirm:\n\n{detail}\n\n"
                "Your settings were saved locally. Check that the ESP32 IP is "
                "correct and the device is on the same network (try Test "
                "Connection), or configure the layout from the device web page."
            )

    def _update_customize_btn_state(self):
        if getattr(self, "customize_layout_btn", None) is None:
            return
        has_metrics = bool(self.selected_metrics)
        self.customize_layout_btn.config(state=tk.NORMAL if has_metrics else tk.DISABLED)

    def open_layout_editor(self):
        if not self.selected_metrics or self.current_layout is None:
            return
        metrics = self._build_layout_input()
        metrics_by_id = {m["id"]: m for m in metrics}
        layout_copy = copy.deepcopy(self.current_layout["layout"])
        row_mode = self.current_layout["row_mode"]
        tpl_key = self.current_layout["source"]
        show_clock = self.current_layout.get("show_clock", False)
        clock_position = self.current_layout.get("clock_position", 0)
        dlg = LayoutEditorDialog(self.root, metrics, row_mode, layout_copy, tpl_key,
                                 show_clock, clock_position)
        if dlg.result is not None:
            self.current_layout = {
                "row_mode": dlg.result["row_mode"],
                "layout": dlg.result["layout"],
                "source": "custom",
                "show_clock": dlg.result.get("show_clock", False),
                "clock_position": dlg.result.get("clock_position", 0),
            }
            self.template_var.set("Custom")
            self._render_preview(
                self.current_layout["row_mode"],
                self.current_layout["layout"],
                metrics_by_id,
            )
            self.overflow_label.config(text="")

    def clear_all(self):
        for cb, sensor, var, frame in self.checkboxes:
            var.set(False)
        self.selected_metrics.clear()
        self.update_counter()

    def _sensor_matches_search(self, sensor, search_term):
        """True if the sensor matches the (lowercased) search term."""
        if not search_term:
            return True
        return (search_term in sensor['display_name'].lower()
                or search_term in sensor['name'].lower())

    def _row_base_bg(self, sensor):
        """The row's normal (non-search) background: green for an active NIC."""
        return "#d4ffd4" if sensor.get('is_active_nic') else "#f0f0f0"

    def _on_search_var_changed(self):
        """Search box changed. Recolor immediately in highlight mode; debounce
        the heavier hide/show work in filter mode so typing stays responsive."""
        if self.filter_var.get():
            if self._search_after_id is not None:
                self.root.after_cancel(self._search_after_id)
            self._search_after_id = self.root.after(150, self._apply_filter)
        else:
            self._recolor_only()

    def _recolor_only(self):
        """Highlight matches without changing visibility (all rows stay shown).
        Non-matching rows fall back to their base color (preserving NIC green)."""
        term = self.search_var.get().lower()
        for cb, sensor, var, frame in self.checkboxes:
            base = self._row_base_bg(sensor)
            if term and self._sensor_matches_search(sensor, term):
                cb.config(bg="#ffffcc")
                frame.config(bg="#ffffcc")
            else:
                cb.config(bg=base)
                frame.config(bg=base)

    def _apply_filter(self):
        """Filter mode: show only matching rows and hide any section whose rows
        are all filtered out. Re-packs in original order to preserve layout."""
        self._search_after_id = None
        term = self.search_var.get().lower()

        # Clean slate so visible rows/sections keep their original order
        for section in self.sections:
            section['frame'].pack_forget()
            for cb, sensor, var, frame in section['rows']:
                frame.pack_forget()

        for section in self.sections:
            visible = []
            for cb, sensor, var, frame in section['rows']:
                base = self._row_base_bg(sensor)
                cb.config(bg=base)
                frame.config(bg=base)
                if self._sensor_matches_search(sensor, term):
                    visible.append(frame)
            if visible:
                section['frame'].pack(fill=tk.X, padx=5, pady=5, anchor="n")
                for frame in visible:
                    frame.pack(fill=tk.X, padx=10, pady=2)

        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def on_filter_toggle(self):
        """Handle the 'Hide non-matching' checkbox toggling."""
        if self.filter_var.get():
            self._apply_filter()
        else:
            # Turning filter OFF: restore every section + row in original order
            for section in self.sections:
                section['frame'].pack_forget()
                for cb, sensor, var, frame in section['rows']:
                    frame.pack_forget()
            for section in self.sections:
                section['frame'].pack(fill=tk.X, padx=5, pady=5, anchor="n")
                for cb, sensor, var, frame in section['rows']:
                    frame.pack(fill=tk.X, padx=10, pady=2)
            self.canvas.configure(scrollregion=self.canvas.bbox("all"))
            self._recolor_only()

    def _update_label_counter(self, info):
        """Refresh one custom-label character counter (red once over 10)."""
        counter = info.get('counter')
        if counter is None:
            return
        n = len(info['entry'].get())
        counter.config(text=f"{n}/10", fg="#cc4444" if n > 10 else "#999999")

    def _on_label_key(self, info):
        """KeyRelease handler for a custom-label entry."""
        self._update_label_counter(info)
        self.update_counter()

    def refresh_all_label_counters(self):
        """Refresh every custom-label counter (after load/import/rescan)."""
        for info in self.label_entries.values():
            self._update_label_counter(info)

    def get_autostart_status_text(self):
        """Check if autostart is enabled (HKCU Run key)"""
        try:
            return "✓ Enabled" if is_autostart_enabled() else "✗ Disabled"
        except Exception:
            return "? Unknown"

    def get_autostart_status_color(self):
        """Get color for autostart status"""
        status = self.get_autostart_status_text()
        if "Enabled" in status:
            return "#00ff00"
        elif "Disabled" in status:
            return "#ff6666"
        else:
            return "#888888"

    def update_autostart_status(self):
        """Update the autostart status label"""
        self.autostart_status.config(
            text=self.get_autostart_status_text(),
            fg=self.get_autostart_status_color()
        )

    def enable_autostart(self):
        """Enable autostart"""
        try:
            success = setup_autostart(enable=True)
            if success:
                self.update_autostart_status()
                messagebox.showinfo("Success", "Autostart enabled!\n\nThe script will run minimized to system tray on Windows startup.\nRight-click the tray icon to configure or quit.")
            else:
                messagebox.showerror("Error", "Failed to enable autostart")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to enable autostart:\n{str(e)}\n\nMake sure pywin32 is installed:\npip install pywin32")

    def disable_autostart(self):
        """Disable autostart"""
        try:
            success = setup_autostart(enable=False)
            if success:
                self.update_autostart_status()
                messagebox.showinfo("Success", "Autostart disabled!")
            else:
                messagebox.showwarning("Warning", "Autostart shortcut not found")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to disable autostart:\n{str(e)}")

    def build_config_from_gui(self, require_metrics=True):
        """Validate the GUI inputs and assemble a config dict.

        Returns the config dict on success, or None after showing an error
        dialog. Shared by Save & Start and Export Settings so both produce an
        identical config structure.
        """
        if require_metrics and len(self.selected_metrics) == 0:
            messagebox.showwarning("No Selection", "Please select at least one metric!")
            return None

        # Validate settings
        try:
            esp_ip = self.ip_var.get().strip()
            udp_port = int(self.port_var.get())
            update_interval = float(self.interval_var.get())

            if not esp_ip:
                raise ValueError("ESP32 IP cannot be empty")
            if udp_port < 1 or udp_port > 65535:
                raise ValueError("Invalid port number")
            if update_interval < 0.5:
                raise ValueError("Update interval must be at least 0.5 seconds")
        except ValueError as e:
            messagebox.showerror("Invalid Settings", str(e))
            return None

        # Build config
        config = {
            "version": "3.0",
            "esp32_ip": esp_ip,
            "udp_port": udp_port,
            "update_interval": update_interval,
            "metrics": []
        }

        # Assign IDs and add custom labels
        for i, sensor in enumerate(self.selected_metrics):
            metric_config = sensor.copy()
            metric_config["id"] = i + 1

            # Get custom label if set
            sensor_key = sensor.get('wmi_identifier') or f"{sensor['source']}_{sensor['display_name']}"
            if sensor_key in self.label_entries:
                custom_label = self.label_entries[sensor_key]['entry'].get().strip()
                if custom_label:
                    metric_config["custom_label"] = custom_label[:10]  # Max 10 chars

            config["metrics"].append(metric_config)

        return config

    def save_and_start(self):
        config = self.build_config_from_gui()
        if config is None:
            return

        if save_config(config):
            messagebox.showinfo(
                "Saved - running in the background",
                f"Configuration saved! {len(self.selected_metrics)} metric(s) will be monitored.\n\n"
                "PC Monitor now runs in the background. Look for its icon in the "
                "system tray (the up-arrow ^ area next to the clock) - right-click "
                "it to reconfigure or quit.\n\n"
                "There is no need to launch it again; double-clicking the app while "
                "it is running just reopens this window."
            )
            self.root.quit()
        else:
            messagebox.showerror("Error", "Failed to save configuration!")

    def export_settings(self):
        """Export the current GUI configuration to a user-chosen JSON file.

        Uses the same validation/build path as Save & Start, then writes the
        result to a location the user picks via a Save-As dialog so the config
        can be backed up or copied to another PC.
        """
        config = self.build_config_from_gui()
        if config is None:
            return

        default_name = f"pcmonitor_config_{datetime.now().strftime('%Y%m%d')}.json"
        path = filedialog.asksaveasfilename(
            title="Export Configuration",
            defaultextension=".json",
            initialfile=default_name,
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
        )
        if not path:
            return  # User cancelled the dialog

        try:
            with open(path, 'w') as f:
                json.dump(config, f, indent=2)
        except Exception as e:
            messagebox.showerror("Export Failed", f"Could not write file:\n{e}")
            return

        messagebox.showinfo(
            "Export Complete",
            f"Configuration exported to:\n{path}\n\n"
            f"{len(config['metrics'])} metric(s) saved."
        )

    def import_settings(self):
        """Import a configuration JSON file and apply it to the GUI.

        Connection settings (IP/port/interval) are applied immediately, and the
        saved metric selection is re-checked against the sensors currently
        available. Metrics that aren't present right now (e.g. LHM not running)
        are reported but skipped; the user can Rescan and import again.
        """
        path = filedialog.askopenfilename(
            title="Import Configuration",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
        )
        if not path:
            return  # User cancelled the dialog

        try:
            with open(path, 'r') as f:
                config = json.load(f)
        except Exception as e:
            messagebox.showerror("Import Failed", f"Could not read file:\n{e}")
            return

        if not isinstance(config, dict) or "metrics" not in config:
            messagebox.showerror(
                "Import Failed",
                "This file does not look like a PC Monitor configuration "
                "(no \"metrics\" section found)."
            )
            return

        # Version awareness: warn (but allow) if the file was saved by a
        # different config format than this app expects.
        app_version = DEFAULT_CONFIG.get("version", "3.0")
        file_version = str(config.get("version", "unknown"))
        if file_version != app_version:
            proceed = messagebox.askyesno(
                "Version Mismatch",
                f"This configuration was saved with version {file_version}, "
                f"but this app uses version {app_version}.\n\n"
                "Importing may not work as expected (metric formats can differ "
                "between versions).\n\nImport anyway?"
            )
            if not proceed:
                return

        # Apply connection settings (fall back to current values if missing)
        self.ip_var.set(str(config.get("esp32_ip", self.ip_var.get())))
        self.port_var.set(str(config.get("udp_port", self.port_var.get())))
        self.interval_var.set(str(config.get("update_interval", self.interval_var.get())))

        # Reset current selections + typed labels before applying the imported set
        self.clear_all()
        for info in self.label_entries.values():
            info['entry'].delete(0, tk.END)

        imported = config.get("metrics", [])
        self.load_existing_metrics(imported)
        self.config = config

        matched = len(self.selected_metrics)
        total = len(imported)
        msg = (f"Imported settings from:\n{path}\n\n"
               f"{matched} of {total} metric(s) matched available sensors.")
        if matched < total:
            msg += ("\n\nUnmatched metrics aren't currently available "
                    "(e.g. LibreHardwareMonitor not running). Start it, click "
                    "\"Rescan sensors\", then import again to restore them.")
        messagebox.showinfo("Import Complete", msg)

    def save_only(self):
        """Save the configuration to disk without launching the monitor loop.

        Lets the user persist tweaks (and have them picked up next launch)
        without immediately starting to send data, unlike Save & Start.
        """
        config = self.build_config_from_gui()
        if config is None:
            return

        if save_config(config):
            messagebox.showinfo(
                "Saved",
                f"Configuration saved ({len(config['metrics'])} metric(s)).\n\n"
                "Click \"Save & Start Monitoring\" to begin sending data to the ESP32."
            )
        else:
            messagebox.showerror("Error", "Failed to save configuration!")

    def test_connection(self):
        """Check whether the configured ESP32 is reachable on the network.

        Reachability is probed by TCP-connecting to the device's web server
        (port 80); a harmless UDP probe is also sent to the configured data
        port. Runs off the UI thread so the window stays responsive.
        """
        ip = self.ip_var.get().strip()
        if not ip:
            messagebox.showwarning("Test Connection", "Enter an ESP32 IP address first.")
            return
        try:
            udp_port = int(self.port_var.get())
        except ValueError:
            udp_port = 4210

        self.test_conn_btn.config(state=tk.DISABLED, text="Testing...")
        self.root.update_idletasks()

        def worker():
            reachable = False
            detail = ""
            try:
                # Web server (port 80) responding = device is up and reachable
                with socket.create_connection((ip, 80), timeout=2):
                    reachable = True
            except OSError as e:
                detail = str(e)
            # Fire-and-forget UDP probe to the configured data port
            try:
                usock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                usock.sendto(b'{"ping":1}', (ip, udp_port))
                usock.close()
            except OSError:
                pass
            self.root.after(0, lambda: self._test_connection_done(ip, udp_port, reachable, detail))

        threading.Thread(target=worker, daemon=True).start()

    def _test_connection_done(self, ip, udp_port, reachable, detail):
        """Report the Test Connection result back on the UI thread."""
        self.test_conn_btn.config(state=tk.NORMAL, text="Test Connection")
        if reachable:
            messagebox.showinfo(
                "Test Connection",
                f"✓ ESP32 reachable at {ip}\n\n"
                f"Web interface (port 80) responded, and a UDP probe was sent "
                f"to port {udp_port}."
            )
        else:
            messagebox.showwarning(
                "Test Connection",
                f"✗ Could not reach {ip} on port 80.\n\n"
                f"{detail}\n\n"
                "Check that the device is powered on, on the same network, and "
                "that the IP address is correct. (If the web interface is "
                "disabled the device may still receive UDP data.)"
            )


def _parse_rest_value(value_str, is_throughput):
    """
    Parse a LibreHardwareMonitor REST value string (e.g. "45.0 C", "12.3 MB/s")
    into the int the ESP32 expects. Throughput is normalized to KB/s and scaled
    by 10 to preserve one decimal place (the ESP32 divides by 10 when showing).
    Returns 0 if the string holds no number.
    """
    try:
        match = re.search(r'[-+]?\d*\.?\d+', value_str)
        if not match:
            return 0
        value = float(match.group())
        if is_throughput:
            value_upper = value_str.upper()
            if "GB/S" in value_upper:
                value = value * 1024 * 1024          # GB/s -> KB/s
            elif "MB/S" in value_upper:
                value = value * 1024                 # MB/s -> KB/s
            elif "KB/S" in value_upper:
                pass                                 # already KB/s
            elif "B/S" in value_upper or not any(x in value_upper for x in ['/', 'S']):
                value = value / 1024                 # B/s or raw bytes -> KB/s
            value = value * 10
        return int(value)
    except Exception:
        return 0


def build_rest_snapshot(host, port):
    """
    Fetch /data.json ONCE and return {SensorId: raw_value_string}, or None on
    failure. Lets a whole update cycle resolve every REST metric from a single
    request instead of one HTTP GET + full-tree parse per metric.
    """
    url = f"http://{host}:{port}/data.json"
    try:
        req = urllib_request.Request(url, method='GET')
        req.add_header('User-Agent', 'PC-Stats-Monitor/3.0')
        with urllib_request.urlopen(req, timeout=1) as response:  # 1s for fast failure detection
            if response.status != 200:
                lhm_health_monitor.record_failure()
                return None
            data = response.read().decode('utf-8')
            root = json.loads(data)
            sensors = extract_sensors_from_tree(root)
            snapshot = {}
            for sensor in sensors:
                sid = sensor.get("SensorId", "")
                if sid:
                    snapshot[sid] = str(sensor.get("Value", "0"))
            lhm_health_monitor.record_success()
            return snapshot
    except Exception:
        lhm_health_monitor.record_failure()
        return None


# Cached WMI connection so we don't rebind COM (an expensive operation) on every
# metric of every cycle. Reset to None on error to force a clean reconnect.
_wmi_connection = None


def build_wmi_snapshot():
    """
    Enumerate all LibreHardwareMonitor WMI sensors ONCE and return
    {Identifier: float_value}, or None on failure. Reuses a cached connection.
    """
    global _wmi_connection
    try:
        import wmi
        if _wmi_connection is None:
            _wmi_connection = wmi.WMI(namespace="root\\LibreHardwareMonitor")
        snapshot = {}
        for sensor in _wmi_connection.Sensor():
            try:
                snapshot[sensor.Identifier] = float(sensor.Value)
            except Exception:
                pass
        return snapshot
    except Exception:
        _wmi_connection = None  # Force reconnect next cycle
        return None


def get_metric_value(metric_config, snapshot=None):
    """
    Get current value for a configured metric.

    For hardware (LHM) metrics, `snapshot` is the per-cycle map built once by
    send_metrics (REST: {SensorId: value_string}, WMI: {Identifier: float}), so
    the whole sensor source is fetched/parsed only once per cycle. A snapshot of
    None means the source was unavailable this cycle -> return None (stale) so
    the caller falls back to the last good value.

    Returns: int value on success, None on failure (for WMI/REST API sources)
    """
    source = metric_config["source"]

    if source == "psutil":
        method = metric_config["psutil_method"]

        if method == "cpu_percent":
            return int(psutil.cpu_percent(interval=0))
        elif method == "virtual_memory.percent":
            return int(psutil.virtual_memory().percent)
        elif method == "virtual_memory.used":
            return int(psutil.virtual_memory().used / (1024**3))  # GB
        elif method == "disk_usage":
            return int(psutil.disk_usage('C:\\').percent)
        return None

    if source == "wmi":
        if snapshot is None:
            return None  # Source unavailable this cycle -> use cached value

        if use_rest_api:
            sensor_id = metric_config.get("wmi_identifier", "")
            if sensor_id and sensor_id in snapshot:
                is_throughput = metric_config.get("unit", "") == "KB/s"
                return _parse_rest_value(snapshot[sensor_id], is_throughput)
            # API responded but this sensor wasn't present
            return 0

        # WMI snapshot: {Identifier: float}
        identifier = metric_config.get("wmi_identifier", "")
        if identifier in snapshot:
            value = snapshot[identifier]
            # For throughput: WMI returns B/s, convert to KB/s and scale by 10
            if metric_config.get("unit", "") == "KB/s":
                value = value / 1024  # B/s -> KB/s
                value = value * 10    # Preserve 1 decimal place
            return int(value)
        return None  # Sensor not present

    return None


# Status codes (must match ESP32 config.h)
STATUS_OK = 1
STATUS_API_ERROR = 2
STATUS_LHM_NOT_RUNNING = 3
STATUS_LHM_STARTING = 4
STATUS_UNKNOWN_ERROR = 5


def send_metrics(sock, config, last_good_values=None, status_code=STATUS_OK):
    """
    Collect metric values and send to ESP32

    Args:
        sock: UDP socket
        config: Configuration dictionary
        last_good_values: Dict to track last known good values per metric ID
        status_code: LHM status code (1=OK, 2=API error, 3=LHM not running, etc.)

    Returns:
        Tuple of (success: bool, last_good_values: dict, has_fresh_data: bool)
    """
    if last_good_values is None:
        last_good_values = {}

    # Track if we got any fresh data
    has_fresh_data = False
    stale_count = 0

    # Build JSON payload with status code
    payload = {
        "version": "2.2",
        "status": status_code,  # LHM health status code
        "timestamp": "",  # Will be set based on data freshness
        "metrics": []
    }

    # Fetch the hardware-sensor source ONCE per cycle (instead of once per
    # metric). All wmi-source metrics then resolve from this single snapshot.
    snapshot = None
    if any(m.get("source") == "wmi" for m in config["metrics"]):
        if use_rest_api:
            # Skip the request entirely while the API is known down (fast path);
            # the monitoring loop handles periodic recovery probing separately.
            if lhm_health_monitor.is_healthy:
                snapshot = build_rest_snapshot(rest_api_host, rest_api_port)
        else:
            snapshot = build_wmi_snapshot()

    for metric_config in config["metrics"]:
        value = get_metric_value(metric_config, snapshot)
        metric_id = metric_config["id"]

        if value is not None:
            # Fresh data - update cache
            last_good_values[metric_id] = value
            has_fresh_data = True
        else:
            # Stale data - use cached value if available
            value = last_good_values.get(metric_id, 0)
            stale_count += 1

        # Use custom label if set, otherwise use generated name
        display_name = metric_config.get("custom_label", "")
        if not display_name:
            display_name = metric_config["name"]

        metric_data = {
            "id": metric_id,
            "name": display_name,
            "value": value,
            "unit": metric_config["unit"]
        }
        payload["metrics"].append(metric_data)

    # Override status if data is stale (even if health monitor says OK)
    # This catches the case where API starts failing but health monitor hasn't triggered yet
    total_metrics = len(config["metrics"])
    if total_metrics > 0 and stale_count >= total_metrics:
        # All metrics are stale - definitely an API error
        if status_code == STATUS_OK:
            status_code = STATUS_API_ERROR
        payload["status"] = status_code
    elif stale_count > 0 and stale_count >= total_metrics * 0.5:
        # More than half metrics stale - likely API issue
        if status_code == STATUS_OK:
            status_code = STATUS_API_ERROR
        payload["status"] = status_code

    # Set timestamp only if we have fresh data
    # Empty timestamp signals ESP32 that data may be stale
    if has_fresh_data:
        payload["timestamp"] = datetime.now().strftime('%H:%M')
    else:
        payload["timestamp"] = ""  # Signal stale data to ESP32

    # Send via UDP
    try:
        message = json.dumps(payload).encode('utf-8')
        sock.sendto(message, (config["esp32_ip"], config["udp_port"]))

        # Print status with stale indicator and status code
        timestamp = payload["timestamp"] if payload["timestamp"] else "STALE"
        metrics_str = " | ".join([f"{m['name']}:{m['value']}{m['unit']}" for m in payload["metrics"][:4]])
        if len(payload["metrics"]) > 4:
            metrics_str += f" ... +{len(payload['metrics'])-4} more"

        stale_indicator = f" [!{stale_count} stale]" if stale_count > 0 else ""

        # Status code indicator
        status_names = {
            STATUS_OK: "",
            STATUS_API_ERROR: " [API ERR]",
            STATUS_LHM_NOT_RUNNING: " [LHM DOWN]",
            STATUS_LHM_STARTING: " [LHM STARTING]",
            STATUS_UNKNOWN_ERROR: " [ERROR]"
        }
        status_indicator = status_names.get(status_code, f" [STATUS:{status_code}]")

        print(f"[{timestamp}] {metrics_str}{stale_indicator}{status_indicator}")

        return True, last_good_values, has_fresh_data
    except Exception as e:
        print(f"Error sending data: {e}")
        return False, last_good_values, has_fresh_data


def create_tray_icon():
    """Create a simple system tray icon"""
    if not TRAY_AVAILABLE:
        return None

    # Create a simple icon
    def create_image():
        width = 64
        height = 64
        image = Image.new('RGB', (width, height), color='black')
        dc = ImageDraw.Draw(image)
        dc.rectangle([16, 16, 48, 48], fill='cyan')
        return image

    return create_image()


def run_minimized(config, notify_startup=False):
    """Run monitoring loop in background with system tray icon and LHM recovery.

    When notify_startup is True, a tray balloon announces that the app is now
    running in the background, so a first-time user does not assume the closed
    config window meant it failed to start.
    """
    global lhm_health_monitor

    if not TRAY_AVAILABLE:
        print("\nWARNING: pystray not available, running in console mode")
        print("Install with: pip install pystray pillow")
        run_monitoring(config)
        return

    # Create monitoring thread
    import threading

    stop_event = threading.Event()

    def monitoring_thread():
        global lhm_health_monitor
        nonlocal config

        # Initialize COM in this thread (required for WMI with pythonw.exe)
        if PYTHONCOM_AVAILABLE:
            try:
                pythoncom.CoInitialize()
            except Exception:
                pass

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        psutil.cpu_percent(interval=1)

        last_good_values = {}
        last_lhm_check = time.time()

        while not stop_event.is_set():
            # A later launch may have saved new settings and asked us to reload.
            if _reload_event.is_set():
                _reload_event.clear()
                new_config = load_config()
                if new_config and new_config.get("metrics"):
                    config = new_config
                    last_good_values = {}  # metric IDs may have changed
                    print(f"\n↻ Settings reloaded - monitoring {len(config['metrics'])} metric(s)")

            current_time = time.time()

            # Determine current status code based on health monitor state
            current_status = STATUS_OK

            if use_rest_api and not lhm_health_monitor.is_healthy:
                # Unhealthy - determine specific error status
                if is_lhm_process_running():
                    current_status = STATUS_API_ERROR
                else:
                    current_status = STATUS_LHM_NOT_RUNNING

                # Try to recover periodically (every 5 seconds)
                if current_time - last_lhm_check >= 5:
                    last_lhm_check = current_time
                    if is_lhm_process_running():
                        success_check, count, _ = check_rest_api_connectivity(rest_api_host, rest_api_port)
                        if success_check:
                            lhm_health_monitor.record_success()
                            current_status = STATUS_OK

            # Send metrics with status code
            success, last_good_values, has_fresh = send_metrics(sock, config, last_good_values, current_status)

            # Always use normal update interval to keep ESP32 alive
            time.sleep(config["update_interval"])

        sock.close()

        # Uninitialize COM when thread exits
        if PYTHONCOM_AVAILABLE:
            try:
                pythoncom.CoUninitialize()
            except Exception:
                pass

    # Create tray icon
    def on_quit(icon, item):
        stop_event.set()
        icon.stop()

    def on_show_config(icon, item):
        # Relaunch ourselves in edit mode. In a frozen .exe sys.executable IS
        # the app (no script path); as a script we pass the .py explicitly.
        # subprocess.Popen avoids the console window os.system would spawn.
        if IS_FROZEN:
            subprocess.Popen([EXE_PATH, "--edit"])
        else:
            subprocess.Popen([sys.executable, EXE_PATH, "--edit"])

    icon = pystray.Icon(
        "pc_monitor",
        create_tray_icon(),
        "PC Monitor",
        menu=pystray.Menu(
            pystray.MenuItem("Configure", on_show_config),
            pystray.MenuItem("Quit", on_quit)
        )
    )

    # Start monitoring thread
    thread = threading.Thread(target=monitoring_thread, daemon=True)
    thread.start()

    # Run tray icon (blocking). The setup callback fires once the icon is live,
    # which is the right moment to pop the "running in the background" balloon.
    def on_icon_ready(icon):
        icon.visible = True
        if notify_startup:
            try:
                icon.notify(
                    "Monitoring your PC in the background. Right-click this icon "
                    "to reconfigure or quit.",
                    "PC Monitor is running"
                )
            except Exception:
                pass

    icon.run(setup=on_icon_ready)


def run_monitoring(config):
    """Run monitoring loop in console mode with LHM health monitoring and recovery"""
    global lhm_health_monitor

    print(f"\nMonitoring {len(config['metrics'])} metrics:")
    for m in config["metrics"]:
        label_info = f" (Label: {m['custom_label']})" if m.get('custom_label') else ""
        print(f"  {m['id']}. {m['display_name']} ({m['name']}){label_info} - {m['source']}")

    print(f"\nESP32 IP: {config['esp32_ip']}")
    print(f"UDP Port: {config['udp_port']}")
    print(f"Update Interval: {config['update_interval']}s")
    print("\nStarting monitoring... (Press Ctrl+C to stop)\n")

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Warm up psutil
    psutil.cpu_percent(interval=1)

    # Initialize tracking variables
    last_good_values = {}
    last_lhm_check = time.time()

    # Main monitoring loop with recovery logic
    try:
        while True:
            # A later launch may have saved new settings and asked us to reload.
            if _reload_event.is_set():
                _reload_event.clear()
                new_config = load_config()
                if new_config and new_config.get("metrics"):
                    config = new_config
                    last_good_values = {}  # metric IDs may have changed
                    print(f"\n↻ Settings reloaded - monitoring {len(config['metrics'])} metric(s)")

            current_time = time.time()

            # Determine current status code based on health monitor state
            # (health state is updated during send_metrics -> get_metric_value calls)
            current_status = STATUS_OK

            if use_rest_api and not lhm_health_monitor.is_healthy:
                # Unhealthy - determine specific error status
                if is_lhm_process_running():
                    current_status = STATUS_API_ERROR
                else:
                    current_status = STATUS_LHM_NOT_RUNNING

                # Try to recover periodically (every 5 seconds for quick feedback)
                if current_time - last_lhm_check >= 5:
                    last_lhm_check = current_time

                    if is_lhm_process_running():
                        # Process is running, try to reconnect
                        success_check, count, _ = check_rest_api_connectivity(rest_api_host, rest_api_port)
                        if success_check:
                            print(f"\n  ✓ LHM REST API restored ({count} sensors available)")
                            lhm_health_monitor.record_success()
                            current_status = STATUS_OK
                        else:
                            if lhm_health_monitor.should_print_warning():
                                print(f"  ⚠ LHM process found but REST API not responding")
                    else:
                        if lhm_health_monitor.should_print_warning():
                            print("  ⚠ Waiting for LibreHardwareMonitor to restart...")

            # Send metrics with status code (will use cached values if LHM is down)
            success, last_good_values, has_fresh = send_metrics(sock, config, last_good_values, current_status)

            # Always use normal update interval to keep ESP32 alive
            time.sleep(config["update_interval"])

    except KeyboardInterrupt:
        print("\n\nMonitoring stopped.")
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Single-instance coordination
#
# The app normally lives in the system tray. Without a guard, double-clicking
# the .exe again (very common when the tray icon goes unnoticed) would spin up
# a SECOND monitor + tray icon, silently duplicating the UDP traffic. We use a
# fixed localhost TCP port as both a lock and a tiny IPC channel:
#   - The running monitor ("primary") binds the port and listens for commands.
#   - A later launch detects the primary via a handshake, hands off, and exits:
#       * a plain double-click opens the config window, then asks the primary to
#         reload the saved config;
#       * an edit launch (--edit / tray "Configure") just asks it to reload.
# Reloading lets settings changed while the monitor is running take effect live,
# with no duplicate process and no restart.
# ---------------------------------------------------------------------------
SINGLE_INSTANCE_HOST = "127.0.0.1"
# Fixed localhost port: lock + IPC for the one running monitor. Kept below 49152
# (the dynamic range) so it avoids Windows' reserved WinNAT/Hyper-V port blocks,
# which can otherwise make bind() fail with WSAEACCES. If it is ever unavailable,
# acquire_single_instance() falls back to "standalone" and the app still runs.
SINGLE_INSTANCE_PORT = 42100
_SINGLE_INSTANCE_MAGIC = b"PCMON1"

# Held for the process lifetime so the OS keeps the lock; released on exit.
_single_instance_sock = None
# Set by the IPC listener when another launch saved new settings; the monitor
# loop reloads monitor_config.json on its next tick.
_reload_event = threading.Event()


def _detect_running_primary():
    """Return True if OUR already-running monitor answers on the port.

    Uses a handshake (not merely "is the port open") so an unrelated program
    that happens to hold the port is not mistaken for our instance.
    """
    try:
        with socket.create_connection((SINGLE_INSTANCE_HOST, SINGLE_INSTANCE_PORT),
                                      timeout=1) as conn:
            conn.sendall(b"ping")
            conn.settimeout(1)
            return conn.recv(32).startswith(_SINGLE_INSTANCE_MAGIC)
    except OSError:
        return False


def acquire_single_instance():
    """Decide this launch's role with respect to the single running monitor.

    Returns one of:
      "primary"    - no monitor was running; we grabbed the lock and should run
                     it (the listening socket is kept in _single_instance_sock).
      "secondary"  - our monitor is already running; this launch should hand off.
      "standalone" - the port is held by something that is not us; run unguarded
                     (degraded: no duplicate protection, but the app still works).
    """
    global _single_instance_sock

    if _detect_running_primary():
        return "secondary"

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind((SINGLE_INSTANCE_HOST, SINGLE_INSTANCE_PORT))
        sock.listen(5)
    except OSError:
        sock.close()
        return "standalone"  # port busy, but the handshake says it is not us

    _single_instance_sock = sock
    return "primary"


def start_single_instance_listener():
    """Serve handshake pings and 'reload' requests from later launches."""
    server = _single_instance_sock
    if server is None:
        return

    def listener():
        while True:
            try:
                conn, _ = server.accept()
            except OSError:
                break  # socket closed on shutdown
            try:
                conn.settimeout(2)  # don't let a silent peer wedge the listener
                data = conn.recv(64) or b""
                if b"reload" in data:
                    _reload_event.set()
                conn.sendall(_SINGLE_INSTANCE_MAGIC + b" ok")
            except OSError:
                pass
            finally:
                try:
                    conn.close()
                except OSError:
                    pass

    threading.Thread(target=listener, daemon=True, name="single-instance").start()


def signal_primary_reload():
    """Ask the running monitor to re-read monitor_config.json. Best-effort."""
    try:
        with socket.create_connection((SINGLE_INSTANCE_HOST, SINGLE_INSTANCE_PORT),
                                      timeout=2) as conn:
            conn.sendall(b"reload")
            conn.settimeout(2)
            try:
                conn.recv(32)  # wait for the ack so we do not race our own exit
            except OSError:
                pass
        return True
    except OSError:
        return False


def show_config_gui(existing_config, edit_mode):
    """Discover sensors and run the configuration window (blocking).

    Returns the configuration on disk after the window closes (the user may
    have saved changes), or None if there is still no usable configuration.
    `edit_mode` pre-loads the existing config into the form for editing.
    """
    discover_sensors()

    root = tk.Tk()
    MetricSelectorGUI(root, existing_config if edit_mode else None)
    # Paint the window, then dismiss the startup splash so there is no flash of
    # empty desktop between the two.
    root.update()
    _splash_close()
    root.mainloop()
    # Only destroy if the window still exists (user might have closed it via X).
    try:
        if root.winfo_exists():
            root.destroy()
    except tk.TclError:
        pass  # Window already destroyed

    return load_config()


def main():
    """
    Main entry point
    """
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='PC Stats Monitor v3.0')
    parser.add_argument('--configure', action='store_true', help='Force configuration GUI')
    parser.add_argument('--edit', action='store_true', help='Edit existing configuration')
    parser.add_argument('--autostart', choices=['enable', 'disable'], help='Enable/disable autostart')
    parser.add_argument('--minimized', action='store_true', help='Run minimized to system tray')
    parser.add_argument('--startup-delay', type=int, default=0,
                        help='Delay in seconds before starting (useful for autostart to wait for LHM)')
    args = parser.parse_args()

    # Background autostart launch: drop the splash right away so it doesn't sit
    # over the desktop during the startup delay (the tray icon is the real UI).
    if args.minimized:
        _splash_close()

    # Apply startup delay if specified (useful for Windows autostart)
    if args.startup_delay > 0:
        print(f"Waiting {args.startup_delay}s for system services to start...")
        time.sleep(args.startup_delay)

    # Initialize COM for WMI access (required when running with pythonw.exe / no console)
    # Without this, WMI fails silently when launched from shortcuts
    if PYTHONCOM_AVAILABLE:
        try:
            pythoncom.CoInitialize()
        except Exception:
            pass  # Already initialized or not needed

    # Handle autostart
    if args.autostart:
        try:
            success = setup_autostart(args.autostart == 'enable')
            if success:
                print("\nTIP: The script will run minimized to system tray on startup")
                print("     Right-click the tray icon to configure or quit")
        except Exception as e:
            print(f"\n✗ Error setting up autostart: {e}")
        _splash_close()
        return

    print("\n" + "=" * 60)
    print("  PC STATS MONITOR v3.0")
    print("  Dynamic Sensor Monitoring with GUI Configuration")
    print("=" * 60 + "\n")

    # Check for config file
    config = load_config()

    # An explicit request to open the configuration window: first run (no config
    # yet), or the user / tray menu asked to (re)configure.
    explicit_gui = args.configure or args.edit or config is None

    if explicit_gui:
        if config is None:
            print("\nNo configuration found. Starting GUI...")
        else:
            print("\nOpening configuration editor...")

        config = show_config_gui(config, edit_mode=args.edit)
        if config is None or not config.get("metrics"):
            print("\nNo configuration saved. Exiting.")
            _splash_close()
            return

        # Settings are saved now. If a monitor is already running in the tray,
        # hand the new config to it instead of starting a duplicate process.
        role = acquire_single_instance()
        if role == "secondary":
            print("\nPC Monitor is already running - applying the new settings to it.")
            signal_primary_reload()
            _splash_close()
            return
        # role == "primary" / "standalone" -> fall through and run the monitor.
    else:
        # Plain launch (double-click, or autostart at login). If our monitor is
        # already running, the user just started us again - rather than silently
        # spawning a second tray icon, open the configuration window normally.
        role = acquire_single_instance()
        if role == "secondary":
            print("\nPC Monitor is already running - opening its configuration window.")
            config = show_config_gui(config, edit_mode=True)
            # The running monitor still owns the lock; hand off any changes.
            if config and config.get("metrics"):
                signal_primary_reload()
            _splash_close()
            return
        # role == "primary" / "standalone" -> run the monitor below.

    # Validate config
    if not config.get("metrics"):
        print("\nNo metrics configured. Run with --edit to configure.")
        _splash_close()
        return

    # Initialize REST API detection if not already done (important for running without --edit)
    global use_rest_api
    if not use_rest_api:
        print("Checking sensor connectivity...")

        # Retry logic for startup - WMI can take longer to initialize than REST API
        max_retries = 5 if (args.minimized or IS_FROZEN) else 1  # More retries for autostart
        retry_delay = 3  # seconds between retries

        for attempt in range(max_retries):
            # Try REST API first (for LHM 0.9.5+)
            rest_success, rest_count, _ = check_rest_api_connectivity(rest_api_host, rest_api_port)
            if rest_success and rest_count > 0:
                use_rest_api = True
                print(f"  ✓ Using REST API ({rest_count} sensors available)")
                break
            else:
                # Try WMI (for LHM 0.9.4 and earlier)
                wmi_success, wmi_error, _ = check_wmi_connectivity()
                if wmi_success:
                    print(f"  ✓ Using WMI")
                    break
                else:
                    if attempt < max_retries - 1:
                        print(f"  ⚠ Sensor source not ready, retrying in {retry_delay}s... ({attempt + 1}/{max_retries})")
                        time.sleep(retry_delay)
                    else:
                        print("  ⚠ No sensor source available - will use psutil fallback")

    # We are the running monitor instance. Start the IPC listener so later
    # launches can hand off (reload settings / open config) instead of
    # duplicating us. (Skipped in the rare "standalone" port-conflict case.)
    if role == "primary":
        start_single_instance_listener()

    # Run monitoring. A frozen .exe has no console window, so default to the
    # system tray (run_minimized) unless we're a plain script in a terminal.
    _splash_close()  # tray icon / console is the UI from here on
    notify_startup = not args.minimized  # stay quiet during autostart-at-boot
    if args.minimized or IS_FROZEN:
        run_minimized(config, notify_startup=notify_startup)
    else:
        run_monitoring(config)


if __name__ == "__main__":
    main()
