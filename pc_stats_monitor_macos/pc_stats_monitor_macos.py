"""
PC Stats Monitor v2.1 - macOS Edition
======================================
System monitoring for macOS with ESP32 OLED display support.

Features:
- psutil-based system metrics (CPU, RAM, Disk, Network)
- ioreg-based hardware sensors (temperature, fans - hardware dependent)
- Tkinter GUI for configuration with custom labels
- rumps menu bar integration for background operation
- Auto-detach from terminal when running minimized
- LaunchAgent autostart support
- UDP protocol v2.1 compatible with ESP32 firmware

Version 2.1 Improvements:
- Custom labels now visible in preview (live updates)
- Auto-detach from terminal - no longer stays open
- Improved GUI layout and user experience
- Better network metrics handling

Usage:
    python3 pc_stats_monitor_macos.py           # First run - opens GUI
    python3 pc_stats_monitor_macos.py --configure   # Reconfigure
    python3 pc_stats_monitor_macos.py --minimized   # Run in menu bar (auto-detaches)
    python3 pc_stats_monitor_macos.py --autostart enable  # Enable autostart

Requirements:
    pip3 install psutil rumps

Hardware Compatibility:
    - Intel Macs: CPU temperature, fan speeds available
    - Apple Silicon (M1/M2/M3): Limited sensors, focus on CPU/RAM/Network
"""

import psutil
import socket
import time
import json
import os
import sys
import argparse
import subprocess
import platform
from datetime import datetime
import tkinter as tk
from tkinter import ttk, messagebox
import re

# Try to import rumps for menu bar support
try:
    import rumps
    RUMPS_AVAILABLE = True
except ImportError:
    RUMPS_AVAILABLE = False
    print("Note: rumps not available. Install with: pip3 install rumps")

# Configuration file path
CONFIG_FILE = "monitor_config_macos.json"

# Default configuration
DEFAULT_CONFIG = {
    "version": "2.1",
    "esp32_ip": "192.168.0.163",
    "udp_port": 4210,
    "update_interval": 3,
    "metrics": []
}

# Maximum metrics supported by ESP32
MAX_METRICS = 20

# Global sensor database
sensor_database = {
    "system": [],    # psutil-based metrics (CPU%, RAM%, Disk%)
    "temperature": [],
    "fan": [],
    "load": [],
    "data": [],       # Network data (uploaded/downloaded GB)
    "throughput": [], # Network throughput (upload/download speed)
    "other": []
}

# Global variables for network stats (for throughput calculation)
_network_stats_prev = {}
_network_stats_time = None

# Detect hardware type
def detect_hardware_type():
    """
    Detect if running on Intel or Apple Silicon Mac
    Returns: 'intel' or 'apple_silicon' or 'unknown'
    """
    try:
        arch = platform.machine()
        if arch == 'x86_64':
            return 'intel'
        elif arch in ('arm64', 'arm'):
            return 'apple_silicon'
    except:
        pass
    return 'unknown'


HARDWARE_TYPE = detect_hardware_type()


def get_macos_version():
    """Get macOS version string"""
    return platform.mac_ver()[0]


def get_ioreg_sensors():
    """
    Get hardware sensors via ioreg command
    Returns: dict with temperature and fan sensors
    """
    sensors = {
        "temperatures": [],
        "fans": []
    }

    try:
        # Try to get sensor data from ioreg
        result = subprocess.run(
            ['ioreg', '-rn', 'IOHWSensor'],
            capture_output=True, text=True, timeout=5
        )

        output = result.stdout

        # Parse temperature sensors
        # Typical keys: "temperature", "TC0D", "TCXC", "CPU", "GPU"
        temp_pattern = r'"([^"]*)"\s*=\s*<{[^}]*"type"\s*=\s*"temperature"[^}]*"location"\s*=\s*"([^"]*)"[^}]*"current-value"\s*=\s*(\d+)'
        for match in re.finditer(temp_pattern, output):
            sensor_name = match.group(2)
            raw_value = int(match.group(3))
            # Convert from hundredths of degree Celsius to degrees
            temp_c = raw_value / 100.0
            sensors["temperatures"].append({
                "name": sensor_name,
                "value": temp_c
            })

        # Alternative parsing for different ioreg formats
        lines = output.split('\n')
        for line in lines:
            # Look for temperature keys
            if '"temperature"' in line.lower() or 'TC0D' in line or 'TCXC' in line:
                # Try to extract numeric value
                temp_match = re.search(r'(\d{4,5})', line)
                if temp_match:
                    temp_val = int(temp_match.group(1))
                    if temp_val > 1000:  # Likely in hundredths
                        temp_val = temp_val / 100.0
                    sensors["temperatures"].append({
                        "name": "CPU",
                        "value": temp_val
                    })
                    break  # Use first temperature found

        # Parse fan sensors
        # Typical keys: "fan", "F0Ac", "F1Ac"
        for line in lines:
            if '"fan"' in line.lower() or '"fan speed"' in line.lower():
                fan_match = re.search(r'(\d{3,5})', line)
                if fan_match:
                    fan_rpm = int(fan_match.group(1))
                    if 100 < fan_rpm < 10000:  # Valid RPM range
                        sensors["fans"].append({
                            "name": "FAN",
                            "value": fan_rpm
                        })
                        break  # Use first fan found

    except (subprocess.TimeoutExpired, subprocess.SubprocessError, FileNotFoundError):
        pass
    except Exception as e:
        print(f"Note: Could not read ioreg sensors: {e}")

    return sensors


def get_network_stats():
    """
    Get network statistics for throughput and data tracking
    Returns: dict with upload/download speeds and total data
    """
    global _network_stats_prev, _network_stats_time

    net_io = psutil.net_io_counters()
    current_time = time.time()

    stats = {
        "upload_speed_bps": 0,
        "download_speed_bps": 0,
        "total_upload_gb": 0,
        "total_download_gb": 0
    }

    # Total data in GB
    stats["total_upload_gb"] = net_io.bytes_sent / (1024**3)
    stats["total_download_gb"] = net_io.bytes_recv / (1024**3)

    # Calculate throughput if we have previous data
    if _network_stats_prev and _network_stats_time:
        time_delta = current_time - _network_stats_time
        if time_delta > 0:
            sent_delta = net_io.bytes_sent - _network_stats_prev["bytes_sent"]
            recv_delta = net_io.bytes_recv - _network_stats_prev["bytes_recv"]

            stats["upload_speed_bps"] = sent_delta / time_delta
            stats["download_speed_bps"] = recv_delta / time_delta

    # Store current stats for next call
    _network_stats_prev = {
        "bytes_sent": net_io.bytes_sent,
        "bytes_recv": net_io.bytes_recv
    }
    _network_stats_time = current_time

    return stats


def discover_sensors():
    """
    Discover all available sensors on macOS
    """
    print("=" * 60)
    print(f"PC STATS MONITOR v2.1 - SENSOR DISCOVERY (macOS {get_macos_version()})")
    print("=" * 60)
    print(f"Hardware Type: {HARDWARE_TYPE.replace('_', ' ').title()}")

    # Add psutil system metrics
    print("\n[1/3] Discovering system metrics (psutil)...")

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

    # Disk usage for root partition
    sensor_database["system"].append({
        "name": "DISK",
        "display_name": "Disk Usage",
        "source": "psutil",
        "type": "percent",
        "unit": "%",
        "psutil_method": "disk_usage",
        "custom_label": "",
        "current_value": int(psutil.disk_usage('/').percent)
    })

    print(f"  Found {len(sensor_database['system'])} system metrics")

    # Discover hardware sensors via ioreg
    print("\n[2/3] Discovering hardware sensors (ioreg)...")

    ioreg_sensors = get_ioreg_sensors()

    # Add temperature sensors
    for temp in ioreg_sensors["temperatures"]:
        sensor_database["temperature"].append({
            "name": "CPUT" if temp["name"] == "CPU" else f"{temp['name'][:8].upper()}T",
            "display_name": f"{temp['name']} Temperature",
            "source": "ioreg",
            "type": "temperature",
            "unit": "°C",
            "custom_label": "",
            "current_value": int(temp["value"])
        })

    # Add fan sensors
    for i, fan in enumerate(ioreg_sensors["fans"]):
        sensor_database["fan"].append({
            "name": f"FAN" if i == 0 else f"FAN{i+1}",
            "display_name": f"{fan['name']} Speed",
            "source": "ioreg",
            "type": "fan",
            "unit": "RPM",
            "custom_label": "",
            "current_value": int(fan["value"])
        })

    print(f"  Found {len(sensor_database['temperature'])} temperature sensors")
    print(f"  Found {len(sensor_database['fan'])} fan sensors")

    # Discover network metrics
    print("\n[3/3] Discovering network metrics...")

    # Initialize network stats for throughput
    get_network_stats()
    time.sleep(1)  # Wait for delta calculation
    net_stats = get_network_stats()

    # Data totals
    sensor_database["data"].append({
        "name": "NET_D",
        "display_name": "Network Downloaded (GB)",
        "source": "psutil",
        "type": "data",
        "unit": "GB",
        "psutil_method": "net_io_recv",
        "custom_label": "",
        "current_value": int(net_stats["total_download_gb"])
    })

    sensor_database["data"].append({
        "name": "NET_U",
        "display_name": "Network Uploaded (GB)",
        "source": "psutil",
        "type": "data",
        "unit": "GB",
        "psutil_method": "net_io_sent",
        "custom_label": "",
        "current_value": int(net_stats["total_upload_gb"])
    })

    # Throughput (speed) - in KB/s
    sensor_database["throughput"].append({
        "name": "NET_DS",
        "display_name": "Download Speed (KB/s)",
        "source": "psutil",
        "type": "throughput",
        "unit": "KB/s",
        "psutil_method": "net_speed_down",
        "custom_label": "",
        "current_value": int(net_stats["download_speed_bps"] / 1024)
    })

    sensor_database["throughput"].append({
        "name": "NET_US",
        "display_name": "Upload Speed (KB/s)",
        "source": "psutil",
        "type": "throughput",
        "unit": "KB/s",
        "psutil_method": "net_speed_up",
        "custom_label": "",
        "current_value": int(net_stats["upload_speed_bps"] / 1024)
    })

    print(f"  Found {len(sensor_database['data'])} data metrics")
    print(f"  Found {len(sensor_database['throughput'])} throughput metrics")

    print("\n" + "=" * 60)
    print("\nNote: Sensor values in GUI are from launch time.")
    print("  Network speeds update after 1 second of monitoring.")


def get_unit_from_type(sensor_type):
    """Map sensor type to display unit"""
    unit_map = {
        "Temperature": "°C",
        "Load": "%",
        "Fan": "RPM",
        "Control": "%",
        "Data": "GB",
        "Throughput": "KB/s"
    }
    return unit_map.get(sensor_type, "")


def load_config():
    """Load configuration from file with version checking"""
    if not os.path.exists(CONFIG_FILE):
        return None

    try:
        with open(CONFIG_FILE, 'r') as f:
            config = json.load(f)

        # Version check - show info if old version
        config_version = config.get("version", "1.0")
        if config_version < "2.1":
            print(f"\n✓ Loaded configuration from {CONFIG_FILE} (v{config_version})")
            print(f"  Selected metrics: {len(config.get('metrics', []))}")
            print(f"\n  Note: Config is from older version. Consider reconfiguring")
            print(f"        to benefit from latest improvements (v2.1):")
            print(f"        - Custom labels now shown in preview")
            print(f"        - Improved background execution")
            print(f"        - Better GUI layout")
        else:
            print(f"\n✓ Loaded configuration from {CONFIG_FILE}")
            print(f"  Selected metrics: {len(config.get('metrics', []))}")
        return config
    except Exception as e:
        print(f"\n✗ Error loading config: {e}")
        return None


def save_config(config):
    """Save configuration to file"""
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=2)
        print(f"\nConfiguration saved to {CONFIG_FILE}")
        return True
    except Exception as e:
        print(f"\nError saving config: {e}")
        return False


def setup_autostart(enable=True):
    """
    Add/remove script to macOS LaunchAgents
    Creates/removes a plist file in ~/Library/LaunchAgents/
    """
    launch_agents_dir = os.path.expanduser("~/Library/LaunchAgents")
    plist_label = "com.pctools.monitor"
    plist_path = os.path.join(launch_agents_dir, f"{plist_label}.plist")

    # Get absolute path of this script
    script_path = os.path.abspath(__file__)
    python_exe = sys.executable

    plist_content = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>{plist_label}</string>
    <key>ProgramArguments</key>
    <array>
        <string>{python_exe}</string>
        <string>{script_path}</string>
        <string>--minimized</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <false/>
</dict>
</plist>
"""

    if enable:
        # Create LaunchAgents directory if it doesn't exist
        os.makedirs(launch_agents_dir, exist_ok=True)

        # Write plist file
        try:
            with open(plist_path, 'w') as f:
                f.write(plist_content)

            # Load the launch agent
            subprocess.run(['launchctl', 'load', plist_path], check=True, capture_output=True)

            print(f"\nAutostart enabled!")
            print(f"  Plist file: {plist_path}")
            return True
        except subprocess.CalledProcessError as e:
            print(f"\nError loading launch agent: {e}")
            print(f"  You may need to run: launchctl load {plist_path}")
            # Still return True if file was created
            return os.path.exists(plist_path)
        except Exception as e:
            print(f"\nError enabling autostart: {e}")
            return False
    else:
        # Remove plist file
        try:
            if os.path.exists(plist_path):
                # Unload the launch agent first
                subprocess.run(['launchctl', 'unload', plist_path],
                             capture_output=True, check=False)
                os.remove(plist_path)
                print(f"\nAutostart disabled!")
                print(f"  Removed: {plist_path}")
                return True
            else:
                print("\nAutostart plist file not found")
                return False
        except Exception as e:
            print(f"\nError disabling autostart: {e}")
            return False


class MetricSelectorGUI:
    """
    Tkinter GUI for selecting metrics and configuring settings
    """
    def __init__(self, root, existing_config=None):
        self.root = root
        self.root.title("PC Monitor v2.1 - macOS Configuration")
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
            text="PC Monitor Configuration (macOS)",
            font=("Arial", 18, "bold"),
            bg="#1e1e1e",
            fg="#00d4ff"
        )
        title_label.pack(pady=8)

        # Settings frame (ESP IP, Port, Interval)
        settings_frame = tk.Frame(self.root, bg="#2d2d2d")
        settings_frame.pack(fill=tk.X, padx=10, pady=5)

        # ESP IP
        tk.Label(settings_frame, text="ESP32 IP:", bg="#2d2d2d", fg="#ffffff",
                font=("Arial", 10)).grid(row=0, column=0, padx=10, pady=3, sticky="e")
        self.ip_var = tk.StringVar(value=self.config.get("esp32_ip", "192.168.0.163"))
        tk.Entry(settings_frame, textvariable=self.ip_var, width=20).grid(
            row=0, column=1, padx=5, pady=3, sticky="w")

        # UDP Port
        tk.Label(settings_frame, text="UDP Port:", bg="#2d2d2d", fg="#ffffff",
                font=("Arial", 10)).grid(row=0, column=2, padx=10, pady=3, sticky="e")
        self.port_var = tk.StringVar(value=str(self.config.get("udp_port", 4210)))
        tk.Entry(settings_frame, textvariable=self.port_var, width=10).grid(
            row=0, column=3, padx=5, pady=3, sticky="w")

        # Update Interval
        tk.Label(settings_frame, text="Update Interval (seconds):", bg="#2d2d2d",
                fg="#ffffff", font=("Arial", 10)).grid(row=0, column=4, padx=10, pady=3, sticky="e")
        self.interval_var = tk.StringVar(value=str(self.config.get("update_interval", 3)))
        tk.Entry(settings_frame, textvariable=self.interval_var, width=10).grid(
            row=0, column=5, padx=5, pady=3, sticky="w")

        # Autostart section (second row)
        tk.Label(settings_frame, text="macOS Autostart:", bg="#2d2d2d", fg="#ffffff",
                font=("Arial", 10)).grid(row=1, column=0, padx=10, pady=3, sticky="e")

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

        # Info note about hardware type
        info_label = tk.Label(
            counter_frame,
            text=f"Hardware: {HARDWARE_TYPE.replace('_', ' ').title()} | Values static from launch",
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
                value_text = f" - {sensor['current_value']}{sensor['unit']}" \
                    if sensor.get('current_value') is not None else ""
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

                tk.Label(label_frame, text="Label:", bg="#f0f0f0", fg="#666",
                        font=("Arial", 8)).pack(side=tk.LEFT)
                label_entry = tk.Entry(label_frame, width=15, font=("Arial", 8))
                label_entry.pack(side=tk.LEFT, padx=5)

                # Update preview when label text changes
                label_entry.bind("<KeyRelease>", lambda e: self.update_counter())

                # Store reference to label entry
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
                    # Add to selected_metrics
                    if sensor not in self.selected_metrics:
                        self.selected_metrics.append(sensor)

                    # Set checkbox
                    var.set(True)

                    # Set custom label if exists
                    if metric.get('custom_label') and sensor_key in self.label_entries:
                        self.label_entries[sensor_key]['entry'].insert(0, metric['custom_label'])
                    break

        # Force update after all metrics loaded
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
            # Check for duplicates
            if sensor not in self.selected_metrics:
                self.selected_metrics.append(sensor)
        else:
            if sensor in self.selected_metrics:
                self.selected_metrics.remove(sensor)

        self.update_counter()

    def get_display_label_for_metric(self, sensor):
        """Get custom label if set, otherwise return sensor name"""
        sensor_key = f"{sensor['source']}_{sensor['display_name']}"
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
        preview = " | ".join([f"{i+1}. {self.get_display_label_for_metric(m)}"
                              for i, m in enumerate(self.selected_metrics[:MAX_METRICS])])
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
        plist_path = os.path.expanduser("~/Library/LaunchAgents/com.pctools.monitor.plist")
        if os.path.exists(plist_path):
            return "Enabled"
        else:
            return "Disabled"

    def get_autostart_status_color(self):
        """Get color for autostart status"""
        status = self.get_autostart_status_text()
        if status == "Enabled":
            return "#00ff00"
        else:
            return "#ff6666"

    def update_autostart_status(self):
        """Update the autostart status label"""
        self.autostart_status.config(
            text=self.get_autostart_status_text(),
            fg=self.get_autostart_status_color()
        )

    def enable_autostart(self):
        """Enable autostart"""
        success = setup_autostart(enable=True)
        if success:
            self.update_autostart_status()
            messagebox.showinfo("Success",
                "Autostart enabled!\n\n"
                "The script will run in menu bar on macOS startup.\n"
                "Click the menu bar icon to configure or quit.")
        else:
            messagebox.showerror("Error", "Failed to enable autostart")

    def disable_autostart(self):
        """Disable autostart"""
        success = setup_autostart(enable=False)
        if success:
            self.update_autostart_status()
            messagebox.showinfo("Success", "Autostart disabled!")
        else:
            messagebox.showwarning("Warning", "Autostart plist not found")

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
            sensor_key = f"{sensor['source']}_{sensor['display_name']}"
            if sensor_key in self.label_entries:
                custom_label = self.label_entries[sensor_key]['entry'].get().strip()
                if custom_label:
                    metric_config["custom_label"] = custom_label[:10]  # Max 10 chars

            config["metrics"].append(metric_config)

        if save_config(config):
            messagebox.showinfo("Success",
                f"Configuration saved!\n{len(self.selected_metrics)} metrics will be monitored.")
            self.root.quit()
        else:
            messagebox.showerror("Error", "Failed to save configuration!")


def get_metric_value(metric_config):
    """
    Get current value for a configured metric
    """
    source = metric_config["source"]

    if source == "psutil":
        method = metric_config.get("psutil_method", "")

        if method == "cpu_percent":
            return int(psutil.cpu_percent(interval=0))
        elif method == "virtual_memory.percent":
            return int(psutil.virtual_memory().percent)
        elif method == "virtual_memory.used":
            return int(psutil.virtual_memory().used / (1024**3))  # GB
        elif method == "disk_usage":
            return int(psutil.disk_usage('/').percent)
        elif method == "net_io_recv":
            return int(psutil.net_io_counters().bytes_recv / (1024**3))
        elif method == "net_io_sent":
            return int(psutil.net_io_counters().bytes_sent / (1024**3))
        elif method == "net_speed_down":
            net_stats = get_network_stats()
            return int(net_stats["download_speed_bps"] / 1024)  # KB/s
        elif method == "net_speed_up":
            net_stats = get_network_stats()
            return int(net_stats["upload_speed_bps"] / 1024)  # KB/s

    elif source == "ioreg":
        # Refresh ioreg sensors and find matching value
        ioreg_sensors = get_ioreg_sensors()
        sensor_name = metric_config.get("display_name", "")

        if metric_config.get("type") == "temperature":
            for temp in ioreg_sensors["temperatures"]:
                if sensor_name in temp["name"] or temp["name"] in sensor_name:
                    return int(temp["value"])
        elif metric_config.get("type") == "fan":
            for i, fan in enumerate(ioreg_sensors["fans"]):
                if sensor_name in fan["name"] or fan["name"] in sensor_name:
                    return int(fan["value"])

    return 0


def send_metrics(sock, config):
    """
    Collect metric values and send to ESP32 via UDP
    """
    # Build JSON payload (Protocol v2.0)
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
        metrics_str = " | ".join([f"{m['name']}:{m['value']}{m['unit']}"
                                   for m in payload["metrics"][:4]])
        if len(payload["metrics"]) > 4:
            metrics_str += f" ... +{len(payload['metrics'])-4} more"
        print(f"[{timestamp}] {metrics_str}")

        return True
    except Exception as e:
        print(f"Error sending data: {e}")
        return False


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

    # Warm up psutil and network stats
    psutil.cpu_percent(interval=1)
    get_network_stats()
    time.sleep(1)

    # Main monitoring loop
    try:
        while True:
            send_metrics(sock, config)
            time.sleep(config["update_interval"])
    except KeyboardInterrupt:
        print("\n\nMonitoring stopped.")
    finally:
        sock.close()


def run_minimized(config):
    """Run monitoring loop in background with menu bar app (rumps)"""
    if not RUMPS_AVAILABLE:
        print("\nWARNING: rumps not available, running in console mode")
        print("Install with: pip3 install rumps")
        run_monitoring(config)
        return

    import threading

    # Create stop event for monitoring thread
    stop_event = threading.Event()

    def monitoring_thread():
        """Background thread that sends metrics"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Warm up
        psutil.cpu_percent(interval=1)
        get_network_stats()
        time.sleep(1)

        while not stop_event.is_set():
            send_metrics(sock, config)
            time.sleep(config["update_interval"])

        sock.close()

    # Start monitoring thread
    thread = threading.Thread(target=monitoring_thread, daemon=True)
    thread.start()

    # Create rumps app
    app = rumps.App(
        "PC Monitor",
        icon=None,  # Will use default icon
        quit_button=None
    )

    @app.menu("Configure")
    def configure_callback(_):
        """Open configuration GUI"""
        # Use subprocess to launch config GUI in background
        subprocess.Popen([sys.executable, os.path.abspath(__file__), '--configure'],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @app.menu("Quit")
    def quit_callback(_):
        """Stop monitoring and quit"""
        stop_event.set()
        time.sleep(0.5)  # Let thread finish
        app.quit()

    # Add status info
    app.menu = [
        rumps.MenuItem(f"Monitoring {len(config['metrics'])} metrics", None),
        None,  # Separator
        "Configure",
        "Quit"
    ]

    # Only print messages when running directly (not detached or from LaunchAgent)
    if sys.stdin.isatty() and not os.environ.get('__PYVENV_LAUNCHER__'):
        print(f"\n✓ Running in menu bar. Click the menu bar icon to control.")
        print("✓ You can now close this Terminal window.\n")

    app.run()


def main():
    """
    Main entry point
    """
    # Parse command line arguments
    parser = argparse.ArgumentParser(
        description='PC Stats Monitor v2.1 - macOS Edition',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    python3 pc_stats_monitor_macos.py           # First run - opens GUI
    python3 pc_stats_monitor_macos.py --configure   # Reconfigure
    python3 pc_stats_monitor_macos.py --minimized   # Run in menu bar (auto-detaches)
    python3 pc_stats_monitor_macos.py --autostart enable  # Enable autostart
        """
    )
    parser.add_argument('--configure', action='store_true',
                       help='Force configuration GUI')
    parser.add_argument('--edit', action='store_true',
                       help='Edit existing configuration')
    parser.add_argument('--autostart', choices=['enable', 'disable'],
                       help='Enable/disable autostart (LaunchAgent)')
    parser.add_argument('--minimized', action='store_true',
                       help='Run minimized to menu bar')
    parser.add_argument('--detached', action='store_true',
                       help=argparse.SUPPRESS)  # Internal flag for background process
    args = parser.parse_args()

    # Auto-detach from terminal when running minimized (unless already detached)
    if args.minimized and not args.detached and sys.stdin.isatty():
        # Running from terminal - relaunch as background process
        print("\n" + "=" * 60)
        print("  Launching PC Monitor in background...")
        print("=" * 60)
        print("\n✓ The app will appear in your menu bar (top right)")
        print("✓ You can close this Terminal window now")
        print("\nTo configure later, run:")
        print(f"  python3 {os.path.basename(__file__)} --configure\n")

        # Relaunch with --detached flag
        subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), '--minimized', '--detached'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            start_new_session=True  # Fully detach from parent
        )
        return

    # Handle autostart
    if args.autostart:
        success = setup_autostart(args.autostart == 'enable')
        if success:
            if args.autostart == 'enable':
                print("\nAutostart enabled!")
                print("  The script will run in menu bar on macOS startup.")
                print("  Click the menu bar icon to configure or quit.")
        else:
            print(f"\nError setting up autostart")
        return

    print("\n" + "=" * 60)
    print("  PC STATS MONITOR v2.1 - macOS Edition")
    print("  System Monitoring with GUI Configuration")
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

        # Clean up window
        try:
            if root.winfo_exists():
                root.destroy()
        except tk.TclError:
            pass

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
