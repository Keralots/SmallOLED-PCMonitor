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
from tkinter import ttk, messagebox, scrolledtext

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
    try:
        import wmi
        w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
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
        print(f"  WARNING: Could not access LibreHardwareMonitor: {e}")
        print("  Make sure LibreHardwareMonitor is running!")

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


class AutoConfigurator:
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


class ESP32ConfigExporter:
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
            esp32_config = ESP32ConfigExporter.export_to_esp32_format(metrics, settings)
            with open(file_path, 'w') as f:
                json.dump(esp32_config, f, indent=2)
            return True, f"ESP32 config saved to {file_path}"
        except Exception as e:
            return False, f"Failed to save ESP32 config: {str(e)}"


class ESP32Uploader:
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


class AutoConfigPreviewDialog:
    """
    Preview dialog for auto-configuration
    Shows layout preview and user preferences before applying
    """

    def __init__(self, parent, available_sensors):
        self.parent = parent
        self.available_sensors = available_sensors
        self.result = None  # Will store user's choice

        # User preferences
        self.row_mode = 1  # Default: 6-row
        self.clock_position = 1  # Default: Right
        self.show_clock = True

        # Create dialog
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Auto-Configure Preview")
        self.dialog.geometry("900x750")
        self.dialog.resizable(False, False)
        self.dialog.transient(parent)
        self.dialog.grab_set()

        self._create_widgets()
        self._update_preview()

    def _create_widgets(self):
        # Title
        title_frame = tk.Frame(self.dialog, bg="#1e1e1e", height=50)
        title_frame.pack(fill=tk.X)
        title_frame.pack_propagate(False)

        title_label = tk.Label(
            title_frame,
            text="Auto-Configuration Preview",
            font=("Arial", 16, "bold"),
            bg="#1e1e1e",
            fg="#00d4ff"
        )
        title_label.pack(pady=12)

        # Preferences section
        pref_frame = tk.LabelFrame(
            self.dialog,
            text="Layout Preferences",
            bg="#2d2d2d",
            fg="#ffffff",
            font=("Arial", 11, "bold"),
            padx=20,
            pady=15
        )
        pref_frame.pack(fill=tk.X, padx=20, pady=10)

        # Row mode
        row_frame = tk.Frame(pref_frame, bg="#2d2d2d")
        row_frame.pack(fill=tk.X, pady=5)

        tk.Label(
            row_frame,
            text="Display Mode:",
            bg="#2d2d2d",
            fg="#ffffff",
            font=("Arial", 10)
        ).pack(side=tk.LEFT, padx=(0, 10))

        self.row_mode_var = tk.IntVar(value=1)
        tk.Radiobutton(
            row_frame,
            text="5-row (13px spacing, more readable)",
            variable=self.row_mode_var,
            value=0,
            bg="#2d2d2d",
            fg="#ffffff",
            selectcolor="#444444",
            command=self._update_preview
        ).pack(side=tk.LEFT, padx=10)

        tk.Radiobutton(
            row_frame,
            text="6-row (10px spacing, more metrics)",
            variable=self.row_mode_var,
            value=1,
            bg="#2d2d2d",
            fg="#ffffff",
            selectcolor="#444444",
            command=self._update_preview
        ).pack(side=tk.LEFT, padx=10)

        # Clock position
        clock_frame = tk.Frame(pref_frame, bg="#2d2d2d")
        clock_frame.pack(fill=tk.X, pady=5)

        tk.Label(
            clock_frame,
            text="Clock Position:",
            bg="#2d2d2d",
            fg="#ffffff",
            font=("Arial", 10)
        ).pack(side=tk.LEFT, padx=(0, 10))

        self.clock_position_var = tk.IntVar(value=2)

        for pos_val, pos_name in [(0, "Center"), (1, "Left"), (2, "Right"), (3, "None")]:
            tk.Radiobutton(
                clock_frame,
                text=pos_name,
                variable=self.clock_position_var,
                value=pos_val,
                bg="#2d2d2d",
                fg="#ffffff",
                selectcolor="#444444",
                command=self._update_preview
            ).pack(side=tk.LEFT, padx=5)

        # Preview section
        preview_frame = tk.LabelFrame(
            self.dialog,
            text="Configuration Preview",
            bg="#2d2d2d",
            fg="#ffffff",
            font=("Arial", 11, "bold"),
            padx=15,
            pady=10
        )
        preview_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        # Scrollable preview text
        self.preview_text = scrolledtext.ScrolledText(
            preview_frame,
            width=100,
            height=15,
            font=("Courier", 9),
            bg="#1e1e1e",
            fg="#00ff00",
            wrap=tk.WORD
        )
        self.preview_text.pack(fill=tk.BOTH, expand=True)

        # Instructions section
        info_frame = tk.LabelFrame(
            self.dialog,
            text="ESP32 Configuration Instructions",
            bg="#2d2d2d",
            fg="#ffaa00",
            font=("Arial", 10, "bold"),
            padx=15,
            pady=10
        )
        info_frame.pack(fill=tk.X, padx=20, pady=5)

        instructions = (
            "This tool will automatically:\n"
            "  1. Save configuration to monitor_config.json (for this PC monitor)\n"
            "  2. Save configuration to pc-monitor-configv2.json (for ESP32)\n"
            "  3. Attempt to upload to ESP32 via HTTP\n\n"
            "If upload fails, you can manually:\n"
            "  • Open ESP32 web interface (http://your-esp32-ip)\n"
            "  • Go to 'Import/Export' section\n"
            "  • Upload pc-monitor-configv2.json file"
        )

        tk.Label(
            info_frame,
            text=instructions,
            bg="#2d2d2d",
            fg="#cccccc",
            font=("Arial", 9),
            justify=tk.LEFT
        ).pack()

        # Buttons
        button_frame = tk.Frame(self.dialog, bg="#1e1e1e", height=60)
        button_frame.pack(fill=tk.X)
        button_frame.pack_propagate(False)

        tk.Button(
            button_frame,
            text="Cancel",
            command=self._on_cancel,
            bg="#666666",
            fg="#ffffff",
            font=("Arial", 11),
            relief=tk.FLAT,
            padx=20,
            pady=5
        ).pack(side=tk.LEFT, padx=20, pady=10)

        tk.Button(
            button_frame,
            text="Apply Configuration",
            command=self._on_apply,
            bg="#00d4ff",
            fg="#000000",
            font=("Arial", 11, "bold"),
            relief=tk.FLAT,
            padx=20,
            pady=5
        ).pack(side=tk.RIGHT, padx=20, pady=10)

    def _update_preview(self):
        """Update preview based on current preferences"""
        row_mode = self.row_mode_var.get()
        clock_position = self.clock_position_var.get()

        # Generate auto-config
        configurator = AutoConfigurator()
        metrics, description = configurator.create_auto_config(
            self.available_sensors,
            row_mode=row_mode,
            clock_position=clock_position
        )

        # Store for later
        self.metrics = metrics
        self.description = description

        # Build preview text
        preview = f"{description}\n\n"
        preview += "=" * 70 + "\n"
        preview += "METRICS CONFIGURATION:\n"
        preview += "=" * 70 + "\n\n"

        for i, metric in enumerate(metrics):
            preview += f"{i+1}. {metric['custom_label']:10s} ({metric['name']})\n"
            preview += f"   Source: {metric['source']:10s} | Type: {metric['type']}\n"
            preview += f"   Position: {metric['position']:3d}"
            if metric['bar_position'] != 255:
                preview += f" | Bar: position {metric['bar_position']}"
            if metric['companion_id'] > 0:
                preview += f" | Companion: metric #{metric['companion_id']}"
            preview += "\n\n"

        # Update text widget
        self.preview_text.delete(1.0, tk.END)
        self.preview_text.insert(1.0, preview)

    def _on_cancel(self):
        """User cancelled"""
        self.result = None
        self.dialog.destroy()

    def _on_apply(self):
        """User applied configuration"""
        self.result = {
            "metrics": self.metrics,
            "row_mode": self.row_mode_var.get(),
            "clock_position": self.clock_position_var.get(),
            "show_clock": self.clock_position_var.get() != 3
        }
        self.dialog.destroy()

    def show(self):
        """Show dialog and return result"""
        self.dialog.wait_window()
        return self.result


class MetricSelectorGUI:
    """
    Tkinter GUI for selecting metrics and configuring settings
    """
    def __init__(self, root, existing_config=None):
        self.root = root
        self.root.title("PC Monitor v2.0 - Configuration")
        self.root.geometry("1200x850")
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
        title_frame = tk.Frame(self.root, bg="#1e1e1e", height=60)
        title_frame.pack(fill=tk.X)
        title_frame.pack_propagate(False)

        title_label = tk.Label(
            title_frame,
            text="PC Monitor Configuration",
            font=("Arial", 18, "bold"),
            bg="#1e1e1e",
            fg="#00d4ff"
        )
        title_label.pack(pady=15)

        # Settings frame (ESP IP, Port, Interval)
        settings_frame = tk.Frame(self.root, bg="#2d2d2d")
        settings_frame.pack(fill=tk.X, padx=10, pady=5)

        # ESP IP
        tk.Label(settings_frame, text="ESP32 IP:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=0, padx=10, pady=5, sticky="e")
        self.ip_var = tk.StringVar(value=self.config.get("esp32_ip", "192.168.0.163"))
        tk.Entry(settings_frame, textvariable=self.ip_var, width=20).grid(row=0, column=1, padx=5, pady=5, sticky="w")

        # UDP Port
        tk.Label(settings_frame, text="UDP Port:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=2, padx=10, pady=5, sticky="e")
        self.port_var = tk.StringVar(value=str(self.config.get("udp_port", 4210)))
        tk.Entry(settings_frame, textvariable=self.port_var, width=10).grid(row=0, column=3, padx=5, pady=5, sticky="w")

        # Update Interval
        tk.Label(settings_frame, text="Update Interval (seconds):", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=0, column=4, padx=10, pady=5, sticky="e")
        self.interval_var = tk.StringVar(value=str(self.config.get("update_interval", 3)))
        tk.Entry(settings_frame, textvariable=self.interval_var, width=10).grid(row=0, column=5, padx=5, pady=5, sticky="w")

        # Autostart section (second row)
        tk.Label(settings_frame, text="Windows Autostart:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=1, column=0, padx=10, pady=5, sticky="e")

        autostart_frame = tk.Frame(settings_frame, bg="#2d2d2d")
        autostart_frame.grid(row=1, column=1, columnspan=2, padx=5, pady=5, sticky="w")

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

        # Auto Configure button
        auto_btn = tk.Button(
            counter_frame,
            text="⚡ Auto Configure",
            command=self.auto_configure,
            bg="#00ff88",
            fg="#000000",
            font=("Arial", 10, "bold"),
            relief=tk.FLAT,
            padx=15
        )
        auto_btn.pack(side=tk.RIGHT, padx=10)

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

                # Store reference to label entry
                sensor_key = f"{sensor['source']}_{sensor['name']}"
                self.label_entries[sensor_key] = label_entry

                self.checkboxes.append((cb, sensor, var, sensor_frame))

            col += 1
            if col >= 3:
                col = 0
                row += 1

        # Preview frame
        preview_frame = tk.Frame(self.root, bg="#2d2d2d", height=80)
        preview_frame.pack(fill=tk.X)
        preview_frame.pack_propagate(False)

        preview_label = tk.Label(
            preview_frame,
            text="SELECTED PREVIEW (names sent to ESP32):",
            font=("Arial", 9, "bold"),
            bg="#2d2d2d",
            fg="#888888"
        )
        preview_label.pack(anchor="w", padx=10, pady=(10, 0))

        self.preview_text = tk.Label(
            preview_frame,
            text="",
            font=("Courier", 9),
            bg="#2d2d2d",
            fg="#00ff00",
            anchor="w",
            justify=tk.LEFT
        )
        self.preview_text.pack(fill=tk.X, padx=10, pady=(0, 10))

        # Bottom buttons
        button_frame = tk.Frame(self.root, bg="#1e1e1e", height=60)
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
        cancel_btn.pack(side=tk.LEFT, padx=20, pady=10)

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
        save_btn.pack(side=tk.RIGHT, padx=20, pady=10)

        # Update counter
        self.update_counter()

    def load_existing_metrics(self, metrics):
        """Load existing metric selections when editing config"""
        for metric in metrics:
            # Find matching sensor and check it
            for cb, sensor, var, frame in self.checkboxes:
                sensor_key = f"{sensor['source']}_{sensor['name']}"
                metric_key = f"{metric['source']}_{metric['name']}"

                if sensor_key == metric_key:
                    var.set(True)
                    self.selected_metrics.append(sensor)

                    # Set custom label if exists
                    if metric.get('custom_label') and sensor_key in self.label_entries:
                        self.label_entries[sensor_key].insert(0, metric['custom_label'])
                    break

        self.update_counter()

    def on_checkbox_toggle(self, sensor, var):
        if var.get():
            if len(self.selected_metrics) >= MAX_METRICS:
                messagebox.showwarning(
                    "Limit Reached",
                    f"Maximum {MAX_METRICS} metrics allowed!\nDeselect some metrics first."
                )
                var.set(False)
                return
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

    def auto_configure(self):
        """
        Show auto-configuration preview dialog
        """
        # Flatten sensor database
        all_sensors = []
        for category in sensor_database.values():
            all_sensors.extend(category)

        if len(all_sensors) == 0:
            messagebox.showwarning(
                "No Sensors",
                "No sensors available for auto-configuration!\n\n"
                "Make sure LibreHardwareMonitor is running."
            )
            return

        # Show preview dialog
        preview_dialog = AutoConfigPreviewDialog(self.root, all_sensors)
        result = preview_dialog.show()

        if result is None:
            # User cancelled
            return

        # Apply configuration
        metrics = result["metrics"]
        row_mode = result["row_mode"]
        clock_position = result["clock_position"]
        show_clock = result["show_clock"]

        # Clear existing selections
        self.clear_all()

        # Apply metrics to GUI (check corresponding checkboxes)
        for metric in metrics:
            # Find matching checkbox
            for cb, sensor, var, frame in self.checkboxes:
                sensor_key = f"{sensor['source']}_{sensor['name']}"
                metric_key = f"{metric['source']}_{metric['name']}"

                if sensor_key == metric_key:
                    var.set(True)
                    self.selected_metrics.append(sensor)

                    # Set custom label
                    if metric.get('custom_label') and sensor_key in self.label_entries:
                        self.label_entries[sensor_key].delete(0, tk.END)
                        self.label_entries[sensor_key].insert(0, metric['custom_label'])
                    break

        self.update_counter()

        # Save and upload
        self._save_auto_config(metrics, row_mode, clock_position, show_clock)

    def _save_auto_config(self, metrics, row_mode, clock_position, show_clock):
        """
        Save auto-configured settings and upload to ESP32
        """
        # Get ESP32 IP from GUI
        esp32_ip = self.ip_var.get().strip()
        udp_port = int(self.port_var.get())
        update_interval = float(self.interval_var.get())

        # Build local config (monitor_config.json)
        local_config = {
            "version": "2.0",
            "esp32_ip": esp32_ip,
            "udp_port": udp_port,
            "update_interval": update_interval,
            "metrics": metrics
        }

        # Save local config
        if not save_config(local_config):
            messagebox.showerror("Error", "Failed to save local configuration!")
            return

        # Build ESP32 config
        # Add 20px offset when clock is on right to align with progress bars
        clock_offset = 20 if clock_position == 2 else 0

        settings = {
            "row_mode": row_mode,
            "clock_position": clock_position,
            "show_clock": show_clock,
            "clock_offset": clock_offset
        }

        esp32_config = ESP32ConfigExporter.export_to_esp32_format(metrics, settings)

        # Save ESP32 config to file
        esp32_config_path = "pc-monitor-configv2.json"
        success, msg = ESP32ConfigExporter.save_esp32_config(
            esp32_config_path,
            metrics,
            settings
        )

        if not success:
            messagebox.showerror("Error", f"Failed to save ESP32 config:\n{msg}")
            return

        # Try to upload to ESP32
        print(f"\nAttempting to upload configuration to ESP32 at {esp32_ip}...")
        success, msg = ESP32Uploader.upload_config(esp32_ip, esp32_config)

        if success:
            messagebox.showinfo(
                "Success!",
                f"Auto-configuration complete!\n\n"
                f"✓ Local config saved to {CONFIG_FILE}\n"
                f"✓ ESP32 config saved to {esp32_config_path}\n"
                f"✓ Configuration uploaded to ESP32\n\n"
                f"{len(metrics)} metrics configured."
            )
        else:
            # Upload failed - show manual instructions
            messagebox.showwarning(
                "Upload Failed",
                f"Configuration saved locally, but ESP32 upload failed:\n\n{msg}\n\n"
                f"✓ Local config: {CONFIG_FILE}\n"
                f"✓ ESP32 config: {esp32_config_path}\n\n"
                f"MANUAL UPLOAD REQUIRED:\n"
                f"1. Open http://{esp32_ip} in browser\n"
                f"2. Go to Import/Export section\n"
                f"3. Upload {esp32_config_path}\n"
                f"4. Save settings on ESP32"
            )

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
            sensor_key = f"{sensor['source']}_{sensor['name']}"
            if sensor_key in self.label_entries:
                custom_label = self.label_entries[sensor_key].get().strip()
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
        root.destroy()

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
