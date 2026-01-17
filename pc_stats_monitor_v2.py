"""
PC Stats Monitor v2.0 - Dynamic Sensor Selection
Flexible monitoring system with GUI configuration and up to 12 customizable metrics
"""

import psutil
import socket
import time
import json
import os
import sys
import argparse
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox
from urllib import request as urllib_request
from urllib import error as urllib_error
import re

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

# Configuration file path - use absolute path to work correctly from any working directory
# This fixes autostart issues where Windows ignores WorkingDirectory in shortcuts
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(SCRIPT_DIR, "monitor_config.json")

# Default configuration
DEFAULT_CONFIG = {
    "version": "2.1",
    "esp32_ip": "192.168.0.163",
    "udp_port": 4210,
    "update_interval": 3,
    "metrics": []
}

# Maximum metrics supported by ESP32
MAX_METRICS = 20  # Increased from 12 to support companion metrics

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
        req.add_header('User-Agent', 'PC-Stats-Monitor/2.0')

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
        req.add_header('User-Agent', 'PC-Stats-Monitor/2.0')

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
                # For throughput, also detect if value is in MB/s or KB/s
                original_value_str = str(sensor_value)
                throughput_in_mb = False
                try:
                    # Extract numeric value from string like "45.0 °C" or "12.1 %"
                    value_match = re.search(r'[-+]?\d*\.?\d+', original_value_str)
                    if value_match:
                        sensor_value = float(value_match.group())
                    else:
                        sensor_value = 0

                    # Check if throughput is in MB/s (need to convert to KB/s for ESP32)
                    if sensor_type == "throughput":
                        if "MB/s" in original_value_str or "mb/s" in original_value_str.lower():
                            throughput_in_mb = True
                            # Convert MB/s to KB/s for consistency
                            sensor_value = sensor_value * 1024
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

                # Categorize sensor
                if sensor_type == "temperature":
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
        req.add_header('User-Agent', 'PC-Stats-Monitor/2.0')

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
                                # Check if value is in MB/s and convert to KB/s
                                if "MB/s" in value_str or "mb/s" in value_str.lower():
                                    float_value = float_value * 1024
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
    print("PC STATS MONITOR v2.0 - SENSOR DISCOVERY")
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
        input()
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

            # Categorize sensor
            if sensor_type_lower == "temperature":
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
            input("Press Enter to continue to configuration GUI...")
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


def setup_autostart(enable=True):
    """
    Add/remove script to Windows startup folder
    """
    import winshell
    from win32com.client import Dispatch

    startup_folder = winshell.startup()
    shortcut_path = os.path.join(startup_folder, "PC Monitor.lnk")

    if enable:
        # Create shortcut
        shell = Dispatch('WScript.Shell')
        shortcut = shell.CreateShortCut(shortcut_path)

        # Use pythonw.exe to run without console window
        python_exe = sys.executable.replace("python.exe", "pythonw.exe")
        script_path = os.path.abspath(__file__)

        shortcut.TargetPath = python_exe
        # Include 10s startup delay to wait for LibreHardwareMonitor to initialize
        shortcut.Arguments = f'"{script_path}" --minimized --startup-delay 10'
        shortcut.WorkingDirectory = os.path.dirname(script_path)
        shortcut.IconLocation = python_exe
        shortcut.save()

        print(f"\n✓ Autostart enabled!")
        print(f"  Shortcut created: {shortcut_path}")
        print(f"  Startup delay: 10 seconds (waiting for LHM to start)")
        return True
    else:
        # Remove shortcut
        if os.path.exists(shortcut_path):
            os.remove(shortcut_path)
            print(f"\n✓ Autostart disabled!")
            print(f"  Shortcut removed: {shortcut_path}")
            return True
        else:
            print("\n✗ Autostart shortcut not found")
            return False


# AutoConfigPreviewDialog class removed - will be revisited later


class MetricSelectorGUI:
    """
    Tkinter GUI for selecting metrics and configuring settings
    """
    def __init__(self, root, existing_config=None):
        self.root = root
        self.root.title("PC Monitor v2.0 - Configuration")
        self.root.geometry("1200x800")
        self.root.resizable(False, False)

        self.selected_metrics = []
        self.checkboxes = []
        self.label_entries = {}

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

        # Settings frame (ESP IP, Port, Interval)
        settings_frame = tk.Frame(self.root, bg="#2d2d2d")
        settings_frame.pack(fill=tk.X, padx=10, pady=5)

        # ESP IP
        tk.Label(settings_frame, text="ESP32 IP:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=0, padx=10, pady=3, sticky="e")
        self.ip_var = tk.StringVar(value=self.config.get("esp32_ip", "192.168.0.163"))
        tk.Entry(settings_frame, textvariable=self.ip_var, width=20).grid(row=0, column=1, padx=5, pady=3, sticky="w")

        # UDP Port
        tk.Label(settings_frame, text="UDP Port:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=2, padx=10, pady=3, sticky="e")
        self.port_var = tk.StringVar(value=str(self.config.get("udp_port", 4210)))
        tk.Entry(settings_frame, textvariable=self.port_var, width=10).grid(row=0, column=3, padx=5, pady=3, sticky="w")

        # Update Interval
        tk.Label(settings_frame, text="Update Interval (seconds):", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=4, padx=10, pady=3, sticky="e")
        self.interval_var = tk.StringVar(value=str(self.config.get("update_interval", 3)))
        tk.Entry(settings_frame, textvariable=self.interval_var, width=10).grid(row=0, column=5, padx=5, pady=3, sticky="w")

        # Autostart section (second row)
        tk.Label(settings_frame, text="Windows Autostart:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=1, column=0, padx=10, pady=3, sticky="e")

        autostart_frame = tk.Frame(settings_frame, bg="#2d2d2d")
        autostart_frame.grid(row=1, column=1, columnspan=2, padx=5, pady=3, sticky="w")

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

        self.search_var = tk.StringVar()
        self.search_var.trace_add('write', lambda *_: self.on_search())
        search_entry = tk.Entry(counter_frame, textvariable=self.search_var, width=20)
        search_entry.pack(side=tk.LEFT, padx=5)

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

        # Store reference to bind mousewheel to new widgets later
        self.canvas = canvas
        self.on_mousewheel = on_mousewheel

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        # Create checkboxes by category
        categories = [
            ("SYSTEM METRICS", "system"),
            ("TEMPERATURES", "temperature"),
            ("FANS & COOLING", "fan"),
            ("LOADS", "load"),
            ("CLOCKS", "clock"),
            ("POWER", "power"),
            ("NETWORK DATA", "data"),
            ("NETWORK THROUGHPUT", "throughput")
        ]

        # Pre-calculate visible categories to optimize layout
        visible_categories = [(title, key) for title, key in categories if sensor_database.get(key)]
        num_cols = min(3, len(visible_categories))  # Use fewer columns if fewer categories

        # Create column container frames
        column_frames = []
        for col in range(num_cols):
            scrollable_frame.columnconfigure(col, weight=1, uniform="columns")
            col_frame = tk.Frame(scrollable_frame, bg="#ffffff")
            col_frame.grid(row=0, column=col, sticky="nsew", padx=0, pady=0)
            col_frame.bind("<MouseWheel>", on_mousewheel)
            column_frames.append(col_frame)

        # Distribute categories into columns (vertical packing within each column)
        for idx, (cat_title, cat_key) in enumerate(visible_categories):
            col = idx % num_cols
            parent_frame = column_frames[col]

            # Category header
            cat_frame = tk.Frame(parent_frame, bg="#f0f0f0", relief=tk.RIDGE, borderwidth=2)
            cat_frame.pack(fill=tk.X, padx=5, pady=5, anchor="n")

            cat_label = tk.Label(
                cat_frame,
                text=cat_title,
                font=("Arial", 11, "bold"),
                bg="#f0f0f0",
                fg="#333333"
            )
            cat_label.pack(pady=5)

            # Bind mousewheel to category frame and label
            cat_frame.bind("<MouseWheel>", on_mousewheel)
            cat_label.bind("<MouseWheel>", on_mousewheel)

            # Sensors in category
            for sensor in sensor_database[cat_key]:
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
                sensor_frame = tk.Frame(cat_frame, bg=frame_bg)
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
                label_entry = tk.Entry(label_frame, width=15, font=("Arial", 8))
                label_entry.pack(side=tk.LEFT, padx=5)

                # Update preview when label text changes
                label_entry.bind("<KeyRelease>", lambda e: self.update_counter())

                # Store reference to label entry and label frame
                # Use wmi_identifier (sensor path) as key - most reliable and unique
                sensor_key = sensor.get('wmi_identifier') or f"{sensor['source']}_{sensor['display_name']}"
                self.label_entries[sensor_key] = {
                    'entry': label_entry,
                    'frame': label_frame
                }

                # Bind mousewheel to all created widgets
                for widget in [sensor_frame, cb, label_frame, label_entry]:
                    widget.bind("<MouseWheel>", on_mousewheel)

                self.checkboxes.append((cb, sensor, var, sensor_frame))

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

        # Update counter
        self.update_counter()

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

    def clear_all(self):
        for cb, sensor, var, frame in self.checkboxes:
            var.set(False)
        self.selected_metrics.clear()
        self.update_counter()

    def on_search(self, *args):
        search_term = self.search_var.get().lower()
        for cb, sensor, var, frame in self.checkboxes:
            if search_term in sensor['display_name'].lower() or search_term in sensor['name'].lower():
                cb.config(bg="#ffffcc")
                frame.config(bg="#ffffcc")
            else:
                cb.config(bg="#f0f0f0")
                frame.config(bg="#f0f0f0")

    def get_autostart_status_text(self):
        """Check if autostart is enabled"""
        try:
            import winshell
            startup_folder = winshell.startup()
            shortcut_path = os.path.join(startup_folder, "PC Monitor.lnk")
            if os.path.exists(shortcut_path):
                return "✓ Enabled"
            else:
                return "✗ Disabled"
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

    def save_and_start(self):
        if len(self.selected_metrics) == 0:
            messagebox.showwarning("No Selection", "Please select at least one metric!")
            return

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
            return

        # Build config
        config = {
            "version": "2.1",
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

        if save_config(config):
            messagebox.showinfo("Success", f"Configuration saved!\n{len(self.selected_metrics)} metrics will be monitored.")
            self.root.quit()
        else:
            messagebox.showerror("Error", "Failed to save configuration!")


def get_metric_value(metric_config):
    """
    Get current value for a configured metric

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

    elif source == "wmi":
        # Check if we should use REST API instead (LHM 0.9.5+ workaround)
        if use_rest_api:
            # Skip HTTP requests when API is known to be down (avoids timeout delays)
            if not lhm_health_monitor.is_healthy:
                return None  # Use cached value instead of waiting for timeout
            return get_metric_value_via_http(metric_config, rest_api_host, rest_api_port)

        # Use WMI for older LibreHardwareMonitor versions
        try:
            import wmi
            w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
            identifier = metric_config["wmi_identifier"]

            sensors = w.Sensor(Identifier=identifier)
            if sensors:
                value = float(sensors[0].Value)
                # For throughput: multiply by 10 to preserve 1 decimal place
                # ESP32 will divide by 10 when displaying
                if metric_config.get("unit", "") == "KB/s":
                    value = value * 10
                return int(value)
        except:
            pass
        return None  # WMI failed

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

    for metric_config in config["metrics"]:
        value = get_metric_value(metric_config)
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


def run_minimized(config):
    """Run monitoring loop in background with system tray icon and LHM recovery"""
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
        os.system(f'"{sys.executable}" "{os.path.abspath(__file__)}" --edit')

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

    # Run tray icon (blocking)
    icon.run()


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


def main():
    """
    Main entry point
    """
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='PC Stats Monitor v2.0')
    parser.add_argument('--configure', action='store_true', help='Force configuration GUI')
    parser.add_argument('--edit', action='store_true', help='Edit existing configuration')
    parser.add_argument('--autostart', choices=['enable', 'disable'], help='Enable/disable autostart')
    parser.add_argument('--minimized', action='store_true', help='Run minimized to system tray')
    parser.add_argument('--startup-delay', type=int, default=0,
                        help='Delay in seconds before starting (useful for autostart to wait for LHM)')
    args = parser.parse_args()

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
            print("  Make sure pywin32 is installed: pip install pywin32")
        return

    print("\n" + "=" * 60)
    print("  PC STATS MONITOR v2.0")
    print("  Dynamic Sensor Monitoring with GUI Configuration")
    print("=" * 60 + "\n")

    # Check for config file
    config = load_config()

    # Force configuration or edit mode
    if args.configure or args.edit or config is None:
        if config is None:
            print("\nNo configuration found. Starting GUI...")
        else:
            print("\nOpening configuration editor...")

        # Discover sensors
        discover_sensors()

        # Show GUI
        root = tk.Tk()
        app = MetricSelectorGUI(root, config if args.edit else None)
        root.mainloop()
        # Only destroy if window still exists (user might have closed it via X button)
        try:
            if root.winfo_exists():
                root.destroy()
        except tk.TclError:
            pass  # Window already destroyed

        # Reload config after GUI
        config = load_config()
        if config is None:
            print("\nNo configuration saved. Exiting.")
            return

    # Validate config
    if not config.get("metrics"):
        print("\nNo metrics configured. Run with --edit to configure.")
        return

    # Initialize REST API detection if not already done (important for running without --edit)
    global use_rest_api
    if not use_rest_api:
        print("Checking sensor connectivity...")

        # Retry logic for startup - WMI can take longer to initialize than REST API
        max_retries = 5 if args.minimized else 1  # More retries for autostart
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

    # Run monitoring (minimized or console)
    if args.minimized:
        run_minimized(config)
    else:
        run_monitoring(config)


if __name__ == "__main__":
    main()
