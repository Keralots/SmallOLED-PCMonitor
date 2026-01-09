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

# Configuration file path
CONFIG_FILE = "monitor_config.json"

# Default configuration
DEFAULT_CONFIG = {
    "version": "2.0",
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


def extract_sensors_from_tree(node, sensor_list=None):
    """
    Recursively extract all sensors from LibreHardwareMonitor REST API tree structure.
    The API returns a hierarchical tree where actual sensors have a 'SensorId' field.
    """
    if sensor_list is None:
        sensor_list = []

    # If this node has a SensorId, it's an actual sensor
    if "SensorId" in node:
        sensor_list.append(node)

    # Recursively process children
    if "Children" in node and isinstance(node["Children"], list):
        for child in node["Children"]:
            extract_sensors_from_tree(child, sensor_list)

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
                try:
                    # Extract numeric value from string like "45.0 °C" or "12.1 %"
                    value_match = re.search(r'[-+]?\d*\.?\d+', str(sensor_value))
                    if value_match:
                        sensor_value = float(value_match.group())
                    else:
                        sensor_value = 0
                except:
                    sensor_value = 0

                # Determine unit based on type
                unit_map = {
                    "temperature": "°C",
                    "fan": "RPM",
                    "load": "%",
                    "clock": "MHz",
                    "power": "W",
                    "voltage": "V",
                    "data": "GB",
                    "smalldata": "MB",
                    "control": "%",
                    "level": "%",
                }
                sensor_unit = unit_map.get(sensor_type, "")

                # Generate short name from sensor_id (same format as WMI)
                short_name = generate_short_name_from_id(sensor_id, sensor_type)

                # Build display name with device context
                identifier_parts = sensor_id.split('/')
                if len(identifier_parts) > 1:
                    device_info = identifier_parts[1]
                    if device_info.lower() not in sensor_name.lower():
                        display_name = f"{sensor_name} [{device_info}]"
                    else:
                        display_name = sensor_name
                else:
                    display_name = sensor_name

                sensor_info = {
                    "name": short_name,
                    "display_name": display_name,
                    "source": "wmi",  # Keep as "wmi" for compatibility
                    "type": sensor_type,
                    "unit": sensor_unit,
                    "wmi_identifier": sensor_id,
                    "wmi_sensor_name": sensor_name,
                    "custom_label": "",
                    "current_value": int(sensor_value)
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
                elif sensor_type in ("data", "smalldata"):
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
    """
    sensor_id = metric_config.get("wmi_identifier", "")
    if not sensor_id:
        return 0

    url = f"http://{host}:{port}/data.json"

    try:
        req = urllib_request.Request(url, method='GET')
        req.add_header('User-Agent', 'PC-Stats-Monitor/2.0')

        with urllib_request.urlopen(req, timeout=2) as response:
            if response.status != 200:
                return 0

            data = response.read().decode('utf-8')
            root = json.loads(data)

            # Extract sensors from tree structure
            sensors = extract_sensors_from_tree(root)

            # Find matching sensor by SensorId
            for sensor in sensors:
                if sensor.get("SensorId", "") == sensor_id:
                    value = sensor.get("Value", "0")
                    # Parse value from string (e.g., "45.0 °C" -> 45.0)
                    try:
                        value_match = re.search(r'[-+]?\d*\.?\d+', str(value))
                        if value_match:
                            return int(float(value_match.group()))
                    except:
                        pass
                    return 0

            return 0

    except Exception:
        return 0


def generate_short_name_from_id(sensor_id, sensor_type):
    """
    Generate short name from sensor_id (REST API format)
    Compatible with generate_short_name() for WMI
    """
    # Extract context from sensor_id (e.g., /lpc/it8689e/0/fan/0 -> LPC_FAN)
    parts = sensor_id.split('/')
    if len(parts) >= 4:
        device = parts[1]  # e.g., "intelcpu", "gpu-nvidia", "lpc", "nic"
        sensor_idx = parts[3]  # e.g., "0", "1"

        # CPU
        if "cpu" in device.lower():
            if sensor_type == "load":
                return "CPU" if sensor_idx == "0" else f"CPU{sensor_idx}"
            elif sensor_type == "temperature":
                return f"CPUT" if sensor_idx == "0" else f"CPU{sensor_idx}T"
            elif sensor_type == "power":
                return f"CPUW" if sensor_idx == "0" else f"CPU{sensor_idx}W"
            elif sensor_type == "clock":
                return f"CPUCLK" if sensor_idx == "0" else f"CPU{sensor_idx}CLK"
        # GPU
        elif "gpu" in device.lower() or "nvidia" in device.lower() or "amd" in device.lower():
            if sensor_type == "load":
                return "GPU^^"
            elif sensor_type == "temperature":
                return "GPUT"
            elif sensor_type == "power":
                return "GPUW"
            elif sensor_type == "clock":
                return "GPUCLK"
            elif sensor_type == "fan":
                return "GPUFAN"
        # Fan
        elif sensor_type == "fan":
            return f"FAN" if sensor_idx == "0" else f"FAN{sensor_idx}"
        # Memory/RAM
        elif "memory" in device.lower() or "ram" in device.lower():
            if sensor_type == "data":
                return "RAMUSED"
            elif sensor_type == "load":
                return "RAM^^"
        # Network
        elif "nic" in device.lower() or "network" in device.lower():
            if sensor_type == "throughput":
                # Check if index indicates upload or download
                if len(parts) >= 5:
                    throughput_idx = parts[4]
                    if throughput_idx == "0":
                        return "NET_U"
                    elif throughput_idx == "1":
                        return "NET_D"
            elif sensor_type == "data":
                if len(parts) >= 5:
                    data_idx = parts[4]
                    if data_idx == "0":
                        return "NET_D"
                    elif data_idx == "1":
                        return "NET_U"
        # HDD/SSD
        elif "hdd" in device.lower() or "ssd" in device.lower() or "nvme" in device.lower():
            if sensor_type == "temperature":
                return "HDDT" if "hdd" in device.lower() else "SSDT"

    # Fallback: generate from sensor_id
    # Remove special chars and truncate
    name = sensor_id.replace("/", "_").replace("-", "")[:10].upper()
    return name if name else "SENSOR"


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
        for sensor in sensors:
            # Generate short name for ESP32 display
            short_name = generate_short_name(sensor.Name, sensor.SensorType, sensor.Identifier)

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

            sensor_info = {
                "name": short_name,
                "display_name": display_name,
                "source": "wmi",
                "type": sensor.SensorType.lower(),
                "unit": get_unit_from_type(sensor.SensorType),
                "wmi_identifier": sensor.Identifier,
                "wmi_sensor_name": sensor.Name,
                "custom_label": "",
                "current_value": current_value
            }

            # Categorize sensor
            sensor_type = sensor.SensorType.lower()
            if sensor_type == "temperature":
                sensor_database["temperature"].append(sensor_info)
                sensor_count += 1
            elif sensor_type == "fan":
                sensor_database["fan"].append(sensor_info)
                sensor_count += 1
            elif sensor_type == "load":
                sensor_database["load"].append(sensor_info)
                sensor_count += 1
            elif sensor_type == "clock":
                sensor_database["clock"].append(sensor_info)
                sensor_count += 1
            elif sensor_type == "power":
                sensor_database["power"].append(sensor_info)
                sensor_count += 1
            elif sensor_type == "data":
                sensor_database["data"].append(sensor_info)
                sensor_count += 1
            elif sensor_type == "throughput":
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


# AutoConfigurator class removed - will be revisited later

class AutoConfigurator_REMOVED:
    """
    Intelligent metric selection based on available sensors
    Similar to esp32-configurator/core/auto_configurator.py
    """

    def __init__(self):
        # Template for 5-row layout - comprehensive with smart fallbacks
        self.template_5row = [
            # (label, search_terms, fallback_terms, position, bar_position, bar_range)
            # Row 1: Position 0 (left) - shown when clock on right or no clock
            ("FAN", ["fan"], ["pump", "rpm"], 0, 255, None),

            # Row 2: CPU with temp companion and bar
            ("CPU^^", ["cpu usage", "cpu load", "total cpu", "processor"], ["cpu"], 2, 3, (0, 100)),
            ("CPUT", ["cpu temp", "cpu package", "core temperature"], ["temperature"], 2, 255, None),

            # Row 3: GPU with temp companion and bar
            ("GPU^^", ["gpu usage", "gpu load", "gpu core load", "graphics"], ["gpu"], 4, 5, (0, 100)),
            ("GPUT", ["gpu temp", "gpu core temp"], ["gpu hot spot"], 4, 255, None),

            # Row 4: RAM with bar
            ("RAM^^", ["ram", "memory"], ["ram_gb"], 6, 7, (0, 100)),

            # Row 5: CPU Power + GPU Power/Clock (with comprehensive fallbacks)
            ("CPUW", ["cpu power", "package power"], ["power", "watt", "fan"], 8, 255, None),
            ("GPUW", ["gpu power"], ["gpu clock", "clock", "fan #2", "disk"], 9, 255, None),
        ]

        # Template for 6-row layout - even more comprehensive
        self.template_6row = [
            # Row 1: Position 0 (left) - shown when clock on right or no clock
            ("FAN", ["fan"], ["pump", "rpm"], 0, 255, None),

            # Row 2: CPU with temp companion and bar
            ("CPU^^", ["cpu usage", "cpu load", "total cpu", "processor"], ["cpu"], 2, 3, (0, 100)),
            ("CPUT", ["cpu temp", "cpu package", "core temperature"], ["temperature"], 2, 255, None),

            # Row 3: GPU with temp companion and bar
            ("GPU^^", ["gpu usage", "gpu load", "gpu core load", "graphics"], ["gpu"], 4, 5, (0, 100)),
            ("GPUT", ["gpu temp", "gpu core temp"], ["gpu hot spot"], 4, 255, None),

            # Row 4: RAM with bar
            ("RAM^^", ["ram", "memory"], ["ram_gb"], 6, 7, (0, 100)),

            # Row 5: CPU Power + GPU Power/Clock
            ("CPUW", ["cpu power", "package power"], ["power", "watt", "fan"], 8, 255, None),
            ("GPUW", ["gpu power"], ["gpu clock", "clock", "fan #2"], 9, 255, None),

            # Row 6: Disks or additional sensors
            ("DSK1", ["disk"], ["ssd", "nvme", "hdd", "storage"], 10, 255, None),
            ("DSK2", ["drive"], ["fan #3", "chassis"], 11, 255, None),
        ]

    def create_auto_config(self, available_sensors, row_mode=1, clock_position=2):
        """
        Create automatic configuration

        Args:
            available_sensors: Flattened list from sensor_database
            row_mode: 0=5-row, 1=6-row
            clock_position: 0=Center, 1=Left, 2=Right, 3=None

        Returns:
            (metrics_list, description_text)
        """
        template = self.template_5row if row_mode == 0 else self.template_6row
        metrics = []
        matched_count = 0
        substitutions = []

        for item in template:
            if len(item) == 6:
                label, search_terms, fallback_terms, position, bar_position, bar_range = item
            else:
                continue

            # Adjust position if clock conflicts with metrics
            # clock_position: 0=Center, 1=Left, 2=Right
            if (clock_position == 1 and position == 0) or (clock_position == 2 and position == 1):
                position = 255  # Hide metrics that conflict with clock

            # Try primary search terms
            sensor = self._find_sensor(available_sensors, search_terms)

            # Try fallback if needed
            if not sensor and fallback_terms:
                sensor = self._find_sensor(available_sensors, fallback_terms)
                if sensor:
                    substitutions.append(f"{label} -> {sensor['name']}")

            if sensor:
                matched_count += 1

                # Create metric config
                metric = {
                    "id": len(metrics) + 1,
                    "name": sensor["name"],
                    "display_name": sensor.get("display_name", sensor["name"]),
                    "source": sensor["source"],
                    "type": sensor.get("type", "other"),
                    "unit": sensor.get("unit", ""),
                    "custom_label": label,
                    "current_value": sensor.get("current_value", 0),
                    "position": position,
                    "bar_position": bar_position,
                    "companion_id": 0,
                    "bar_min": bar_range[0] if bar_range else 0,
                    "bar_max": bar_range[1] if bar_range else 100,
                    "bar_width": 50,  # Compact bars
                    "bar_offset": 10,  # Aligned offset
                }

                # Copy source-specific identifiers
                if "psutil_method" in sensor:
                    metric["psutil_method"] = sensor["psutil_method"]
                if "wmi_identifier" in sensor:
                    metric["wmi_identifier"] = sensor["wmi_identifier"]

                metrics.append(metric)

        # Set up companion metrics (CPU%+Temp, GPU%+Temp)
        companion_count = self._setup_companions(metrics)

        # Generate description
        mode_name = "5-row" if row_mode == 0 else "6-row"
        description = f"Auto-configured {mode_name} layout:\n"
        description += f"✓ Matched {matched_count}/{len(template)} metrics\n"

        if companion_count > 0:
            description += f"✨ {companion_count} companion metric(s) configured\n"

        if substitutions:
            description += f"⚠ Substitutions made:\n"
            for sub in substitutions:
                description += f"  • {sub}\n"

        return metrics, description

    def _find_sensor(self, sensors, search_terms, exclude_terms=None):
        """
        Find sensor by partial name match (case-insensitive)
        with exclusion logic to avoid wrong sensors
        """
        if not search_terms:
            return None

        # Common exclusions to avoid codec/decoder sensors
        default_exclusions = ["codec", "decoder", "d3d", "encode", "decode", "video encoder", "video decoder"]
        if exclude_terms:
            exclusions = default_exclusions + exclude_terms
        else:
            exclusions = default_exclusions

        candidates = []

        for sensor in sensors:
            name = sensor.get("name", "").lower()
            display_name = sensor.get("display_name", "").lower()

            # Skip excluded sensors
            if any(excl in name or excl in display_name for excl in exclusions):
                continue

            # Check if any search term matches
            for term in search_terms:
                term_lower = term.lower()
                if term_lower in name or term_lower in display_name:
                    # Score the match (higher is better)
                    score = 0
                    if term_lower == name or term_lower == display_name:
                        score = 100  # Exact match
                    elif name.startswith(term_lower) or display_name.startswith(term_lower):
                        score = 50  # Starts with term
                    else:
                        score = 10  # Contains term

                    # Bonus for "core" or "total" in name (likely main sensor)
                    if "core" in name or "total" in name or "core" in display_name or "total" in display_name:
                        score += 20

                    candidates.append((sensor, score))
                    break

        # Return best match
        if candidates:
            candidates.sort(key=lambda x: x[1], reverse=True)
            return candidates[0][0]

        return None

    def _setup_companions(self, metrics):
        """Set up companion metrics (CPU%+Temp, GPU%+Temp)"""
        companions = {
            "CPU^^": "CPUT",
            "GPU^^": "GPUT",
        }

        configured_count = 0

        for primary_label, companion_label in companions.items():
            primary = None
            companion = None

            for metric in metrics:
                if metric.get("custom_label") == primary_label:
                    primary = metric
                elif metric.get("custom_label") == companion_label:
                    companion = metric

            if primary and companion:
                primary["companion_id"] = companion["id"]
                companion["position"] = 255  # Hide companion (shown with primary)
                configured_count += 1

        return configured_count


# ESP32ConfigExporter class removed - will be revisited later

class ESP32ConfigExporter_REMOVED:
    """
    Export configuration to ESP32 format (array-based)
    Based on esp32-configurator/core/config_manager.py
    """

    @staticmethod
    def export_to_esp32_format(metrics, settings):
        """
        Convert metrics list to ESP32 array-based format

        Args:
            metrics: List of metric dicts
            settings: Dict with row_mode, show_clock, clock_position, clock_offset

        Returns:
            ESP32 config dict
        """
        MAX_METRICS = 20

        # Initialize arrays
        metric_labels = [""] * MAX_METRICS
        metric_names = [""] * MAX_METRICS
        metric_order = list(range(MAX_METRICS))
        metric_companions = [0] * MAX_METRICS
        metric_positions = [255] * MAX_METRICS
        metric_bar_positions = [255] * MAX_METRICS
        metric_bar_min = [0] * MAX_METRICS
        metric_bar_max = [100] * MAX_METRICS
        metric_bar_widths = [60] * MAX_METRICS
        metric_bar_offsets = [0] * MAX_METRICS

        # Populate arrays from metrics
        for i, metric in enumerate(metrics[:MAX_METRICS]):
            metric_labels[i] = metric.get("custom_label", "")[:15]
            metric_names[i] = metric.get("name", "")[:15]
            metric_companions[i] = metric.get("companion_id", 0)
            metric_positions[i] = metric.get("position", 255)
            metric_bar_positions[i] = metric.get("bar_position", 255)
            metric_bar_min[i] = metric.get("bar_min", 0)
            metric_bar_max[i] = metric.get("bar_max", 100)
            metric_bar_widths[i] = metric.get("bar_width", 60)
            metric_bar_offsets[i] = metric.get("bar_offset", 0)

        # Build ESP32 config
        esp32_config = {
            "clockStyle": 0,
            "gmtOffset": 1,
            "daylightSaving": True,
            "use24Hour": True,
            "dateFormat": 0,
            "clockPosition": settings.get("clock_position", 1),
            "clockOffset": settings.get("clock_offset", 0),
            "showClock": settings.get("show_clock", True),
            "displayRowMode": settings.get("row_mode", 1),
            "useRpmKFormat": False,
            "metricLabels": metric_labels,
            "metricNames": metric_names,
            "metricOrder": metric_order,
            "metricCompanions": metric_companions,
            "metricPositions": metric_positions,
            "metricBarPositions": metric_bar_positions,
            "metricBarMin": metric_bar_min,
            "metricBarMax": metric_bar_max,
            "metricBarWidths": metric_bar_widths,
            "metricBarOffsets": metric_bar_offsets
        }

        return esp32_config

    @staticmethod
    def save_esp32_config(file_path, metrics, settings):
        """Save ESP32 config to JSON file"""
        try:
            esp32_config = ESP32ConfigExporter_REMOVED.export_to_esp32_format(metrics, settings)
            with open(file_path, 'w') as f:
                json.dump(esp32_config, f, indent=2)
            return True, f"ESP32 config saved to {file_path}"
        except Exception as e:
            return False, f"Failed to save ESP32 config: {str(e)}"


# ESP32Uploader class removed - will be revisited later

class ESP32Uploader_REMOVED:
    """
    Upload configuration to ESP32 via HTTP POST
    Based on esp32-configurator/core/esp32_uploader.py
    Uses urllib (no external dependencies)
    """

    @staticmethod
    def test_connection(esp32_ip):
        """
        Test if ESP32 is reachable

        Returns:
            (success, message)
        """
        try:
            import urllib.request
            import urllib.error

            url = f"http://{esp32_ip}/"
            req = urllib.request.Request(url, method='GET')

            with urllib.request.urlopen(req, timeout=3) as response:
                if response.status == 200:
                    return True, f"ESP32 is reachable at {esp32_ip}"
                else:
                    return False, f"Unexpected response: {response.status}"

        except urllib.error.URLError as e:
            return False, f"Cannot reach ESP32:\n{str(e.reason)}"
        except Exception as e:
            return False, f"Connection test failed: {str(e)}"

    @staticmethod
    def upload_config(esp32_ip, esp32_config):
        """
        Upload config to ESP32 /api/import endpoint

        Args:
            esp32_ip: IP address
            esp32_config: ESP32 format dict

        Returns:
            (success, message)
        """
        try:
            import urllib.request
            import urllib.error

            # Build URL
            url = f"http://{esp32_ip}/api/import"

            # Convert to JSON
            json_data = json.dumps(esp32_config).encode('utf-8')

            # Create request
            req = urllib.request.Request(
                url,
                data=json_data,
                headers={
                    'Content-Type': 'application/json',
                    'Content-Length': str(len(json_data))
                },
                method='POST'
            )

            # Send with timeout
            with urllib.request.urlopen(req, timeout=5) as response:
                response_data = response.read().decode('utf-8')

                try:
                    result = json.loads(response_data)
                    if result.get("success"):
                        return True, "Configuration uploaded to ESP32!"
                    else:
                        msg = result.get("message", "Unknown error")
                        return False, f"ESP32 rejected config: {msg}"
                except json.JSONDecodeError:
                    return True, "Configuration uploaded (response not JSON)"

        except urllib.error.HTTPError as e:
            return False, f"HTTP error {e.code}: {e.reason}"

        except urllib.error.URLError as e:
            return False, f"Cannot reach ESP32 at {esp32_ip}:\n{str(e.reason)}\n\nCheck:\n- ESP32 powered on\n- Same network\n- Correct IP"

        except Exception as e:
            return False, f"Upload failed: {str(e)}"


def load_config():
    """
    Load configuration from file
    """
    if not os.path.exists(CONFIG_FILE):
        return None

    try:
        with open(CONFIG_FILE, 'r') as f:
            config = json.load(f)
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
        shortcut.Arguments = f'"{script_path}" --minimized'
        shortcut.WorkingDirectory = os.path.dirname(script_path)
        shortcut.IconLocation = python_exe
        shortcut.save()

        print(f"\n✓ Autostart enabled!")
        print(f"  Shortcut created: {shortcut_path}")
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

        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        # Create checkboxes by category
        row = 0
        col = 0

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

        for cat_title, cat_key in categories:
            if not sensor_database.get(cat_key):
                continue

            # Category header
            cat_frame = tk.Frame(scrollable_frame, bg="#f0f0f0", relief=tk.RIDGE, borderwidth=2)
            cat_frame.grid(row=row, column=col, sticky="nsew", padx=5, pady=5)

            cat_label = tk.Label(
                cat_frame,
                text=cat_title,
                font=("Arial", 11, "bold"),
                bg="#f0f0f0",
                fg="#333333"
            )
            cat_label.pack(pady=5)

            # Sensors in category
            for sensor in sensor_database[cat_key]:
                var = tk.BooleanVar()

                # Create sensor row frame
                sensor_frame = tk.Frame(cat_frame, bg="#f0f0f0")
                sensor_frame.pack(fill=tk.X, padx=10, pady=2)

                # Checkbox with current value
                value_text = f" - {sensor['current_value']}{sensor['unit']}" if sensor.get('current_value') is not None else ""
                cb = tk.Checkbutton(
                    sensor_frame,
                    text=f"{sensor['display_name']} ({sensor['name']}){value_text}",
                    variable=var,
                    bg="#f0f0f0",
                    anchor="w",
                    command=lambda s=sensor, v=var: self.on_checkbox_toggle(s, v)
                )
                cb.pack(side=tk.TOP, fill=tk.X)

                # Custom label entry (small, below checkbox)
                label_frame = tk.Frame(sensor_frame, bg="#f0f0f0")
                label_frame.pack(side=tk.TOP, fill=tk.X, padx=20)

                tk.Label(label_frame, text="Label:", bg="#f0f0f0", fg="#666", font=("Arial", 8)).pack(side=tk.LEFT)
                label_entry = tk.Entry(label_frame, width=15, font=("Arial", 8))
                label_entry.pack(side=tk.LEFT, padx=5)

                # Store reference to label entry and label frame
                # Use display_name to ensure uniqueness (multiple sensors can have same 'name')
                sensor_key = f"{sensor['source']}_{sensor['display_name']}"
                self.label_entries[sensor_key] = {
                    'entry': label_entry,
                    'frame': label_frame
                }

                self.checkboxes.append((cb, sensor, var, sensor_frame))

            col += 1
            if col >= 3:
                col = 0
                row += 1

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
                sensor_key = f"{sensor['source']}_{sensor['display_name']}"
                metric_key = f"{metric['source']}_{metric['display_name']}"

                if sensor_key == metric_key:
                    # Explicitly add to selected_metrics (duplicate check in on_checkbox_toggle prevents double-adds)
                    if sensor not in self.selected_metrics:
                        self.selected_metrics.append(sensor)

                    # Set checkbox (this will trigger on_checkbox_toggle which handles showing label entry)
                    var.set(True)

                    # Set custom label if exists
                    if metric.get('custom_label') and sensor_key in self.label_entries:
                        self.label_entries[sensor_key]['entry'].insert(0, metric['custom_label'])
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

    def update_counter(self):
        count = len(self.selected_metrics)
        self.counter_label.config(text=f"Selected: {count}/{MAX_METRICS}")

        if count >= MAX_METRICS:
            self.counter_label.config(fg="#ff6666")
        else:
            self.counter_label.config(fg="#ffffff")

        # Update preview
        preview = " | ".join([f"{i+1}. {m['name']}" for i, m in enumerate(self.selected_metrics[:MAX_METRICS])])
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
            "version": "2.0",
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
            sensor_key = f"{sensor['source']}_{sensor['display_name']}"
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
            return get_metric_value_via_http(metric_config, rest_api_host, rest_api_port)

        # Use WMI for older LibreHardwareMonitor versions
        try:
            import wmi
            w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
            identifier = metric_config["wmi_identifier"]

            sensors = w.Sensor(Identifier=identifier)
            if sensors:
                return int(sensors[0].Value)
        except:
            pass

    return 0


def send_metrics(sock, config):
    """
    Collect metric values and send to ESP32
    """
    # Build JSON payload
    payload = {
        "version": "2.0",
        "timestamp": datetime.now().strftime('%H:%M'),
        "metrics": []
    }

    for metric_config in config["metrics"]:
        value = get_metric_value(metric_config)

        # Use custom label if set, otherwise use generated name
        display_name = metric_config.get("custom_label", "")
        if not display_name:
            display_name = metric_config["name"]

        metric_data = {
            "id": metric_config["id"],
            "name": display_name,
            "value": value,
            "unit": metric_config["unit"]
        }
        payload["metrics"].append(metric_data)

    # Send via UDP
    try:
        message = json.dumps(payload).encode('utf-8')
        sock.sendto(message, (config["esp32_ip"], config["udp_port"]))

        # Print status
        timestamp = payload["timestamp"]
        metrics_str = " | ".join([f"{m['name']}:{m['value']}{m['unit']}" for m in payload["metrics"][:4]])
        if len(payload["metrics"]) > 4:
            metrics_str += f" ... +{len(payload['metrics'])-4} more"
        print(f"[{timestamp}] {metrics_str}")

        return True
    except Exception as e:
        print(f"Error sending data: {e}")
        return False


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
    """Run monitoring loop in background with system tray icon"""
    if not TRAY_AVAILABLE:
        print("\nWARNING: pystray not available, running in console mode")
        print("Install with: pip install pystray pillow")
        run_monitoring(config)
        return

    # Create monitoring thread
    import threading

    stop_event = threading.Event()

    def monitoring_thread():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        psutil.cpu_percent(interval=1)

        while not stop_event.is_set():
            send_metrics(sock, config)
            time.sleep(config["update_interval"])

        sock.close()

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
    """Run monitoring loop in console mode"""
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

    # Main monitoring loop
    try:
        while True:
            send_metrics(sock, config)
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
    args = parser.parse_args()

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

    # Run monitoring (minimized or console)
    if args.minimized:
        run_minimized(config)
    else:
        run_monitoring(config)


if __name__ == "__main__":
    main()
