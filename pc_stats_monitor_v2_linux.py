"""
PC Stats Monitor v2.0 - Linux Edition
Dynamic Sensor Selection with GUI Configuration
Uses psutil for hardware sensor discovery
"""

import psutil
import socket
import time
import json
import os
import sys
import argparse
from datetime import datetime

# Tkinter is imported conditionally only when GUI mode is needed
# This allows the script to run on headless systems without tkinter

# Configuration file path
CONFIG_FILE = "monitor_config_linux.json"

# Default configuration
DEFAULT_CONFIG = {
    "version": "2.0-linux",
    "esp32_ip": "192.168.1.197",
    "udp_port": 4210,
    "update_interval": 3,
    "metrics": []
}

# Maximum metrics supported by ESP32
MAX_METRICS = 20

# Global sensor database
sensor_database = {
    "system": [],      # psutil-based metrics (CPU%, RAM%, Disk%)
    "temperature": [], # Hardware temperatures
    "fan": [],         # Fan speeds
    "data": [],        # Network data (total uploaded/downloaded)
    "throughput": [],  # Network throughput (current upload/download speed)
    "other": []
}

# Network tracking for speed calculation
network_last_sample = {}
network_last_time = None


def discover_sensors():
    """
    Discover all available sensors from psutil on Linux
    """
    print("=" * 60)
    print("PC STATS MONITOR v2.0 - LINUX EDITION - SENSOR DISCOVERY")
    print("=" * 60)

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

    sensor_database["system"].append({
        "name": "DISK",
        "display_name": "Disk / Usage",
        "source": "psutil",
        "type": "percent",
        "unit": "%",
        "psutil_method": "disk_usage",
        "custom_label": "",
        "current_value": int(psutil.disk_usage('/').percent)
    })

    print(f"  Found {len(sensor_database['system'])} system metrics")

    # Discover hardware sensors (temperatures)
    print("\n[2/3] Discovering hardware sensors (temperatures & fans)...")
    sensor_count = 0

    try:
        # Temperature sensors
        if hasattr(psutil, "sensors_temperatures"):
            temps = psutil.sensors_temperatures()

            for sensor_name, entries in temps.items():
                for entry in entries:
                    # Generate short name
                    short_name = generate_short_name_linux(entry.label, "temperature", sensor_name)

                    # Generate display name
                    display_name = f"{entry.label} [{sensor_name}]" if entry.label else f"Sensor [{sensor_name}]"

                    # Get current value
                    try:
                        current_value = int(entry.current) if entry.current else 0
                    except:
                        current_value = 0

                    sensor_info = {
                        "name": short_name,
                        "display_name": display_name,
                        "source": "psutil_temp",
                        "type": "temperature",
                        "unit": "C",
                        "sensor_key": sensor_name,
                        "sensor_label": entry.label,
                        "custom_label": "",
                        "current_value": current_value
                    }

                    sensor_database["temperature"].append(sensor_info)
                    sensor_count += 1

        # Fan sensors
        if hasattr(psutil, "sensors_fans"):
            fans = psutil.sensors_fans()

            for sensor_name, entries in fans.items():
                for entry in entries:
                    # Generate short name
                    short_name = generate_short_name_linux(entry.label, "fan", sensor_name)

                    # Generate display name
                    display_name = f"{entry.label} [{sensor_name}]" if entry.label else f"Fan [{sensor_name}]"

                    # Get current value
                    try:
                        current_value = int(entry.current) if entry.current else 0
                    except:
                        current_value = 0

                    sensor_info = {
                        "name": short_name,
                        "display_name": display_name,
                        "source": "psutil_fan",
                        "type": "fan",
                        "unit": "RPM",
                        "sensor_key": sensor_name,
                        "sensor_label": entry.label,
                        "custom_label": "",
                        "current_value": current_value
                    }

                    sensor_database["fan"].append(sensor_info)
                    sensor_count += 1

        print(f"  Found {sensor_count} hardware sensors:")
        print(f"    - Temperatures: {len(sensor_database['temperature'])}")
        print(f"    - Fans: {len(sensor_database['fan'])}")

    except Exception as e:
        print(f"  WARNING: Could not access hardware sensors: {e}")

    # Discover network metrics
    print("\n[3/3] Discovering network metrics...")
    net_count = 0

    try:
        net_io = psutil.net_io_counters(pernic=True)

        for interface, stats in net_io.items():
            # Skip loopback
            if interface == "lo":
                continue

            # Total Data Uploaded
            sensor_database["data"].append({
                "name": f"{interface[:7]}_U",
                "display_name": f"Data Uploaded - {interface}",
                "source": "psutil_net",
                "type": "data",
                "unit": "MB",
                "interface": interface,
                "metric": "bytes_sent",
                "custom_label": "",
                "current_value": int(stats.bytes_sent / (1024**2))  # MB
            })

            # Total Data Downloaded
            sensor_database["data"].append({
                "name": f"{interface[:7]}_D",
                "display_name": f"Data Downloaded - {interface}",
                "source": "psutil_net",
                "type": "data",
                "unit": "MB",
                "interface": interface,
                "metric": "bytes_recv",
                "custom_label": "",
                "current_value": int(stats.bytes_recv / (1024**2))  # MB
            })

            # Upload Speed (will be calculated dynamically)
            sensor_database["throughput"].append({
                "name": f"{interface[:7]}_U",
                "display_name": f"Upload Speed - {interface}",
                "source": "psutil_net_speed",
                "type": "throughput",
                "unit": "MB/s",
                "interface": interface,
                "metric": "bytes_sent",
                "custom_label": "",
                "current_value": 0  # Will be calculated
            })

            # Download Speed (will be calculated dynamically)
            sensor_database["throughput"].append({
                "name": f"{interface[:7]}_D",
                "display_name": f"Download Speed - {interface}",
                "source": "psutil_net_speed",
                "type": "throughput",
                "unit": "MB/s",
                "interface": interface,
                "metric": "bytes_recv",
                "custom_label": "",
                "current_value": 0  # Will be calculated
            })

            net_count += 4

        print(f"  Found {net_count} network metrics:")
        print(f"    - Data: {len(sensor_database['data'])}")
        print(f"    - Throughput: {len(sensor_database['throughput'])}")

    except Exception as e:
        print(f"  WARNING: Could not access network stats: {e}")

    print("\n" + "=" * 60)
    print("\nℹ NOTE: Sensor values in GUI are static (captured at launch time)")
    print("  This helps you identify active sensors and their typical readings.")


def generate_short_name_linux(label, sensor_type, sensor_key=""):
    """
    Generate a short name (max 10 chars) for ESP32 display - Linux version
    """
    # Start with label or sensor key
    name = label if label else sensor_key

    # Add prefixes based on sensor key
    if sensor_type == "temperature":
        if "coretemp" in sensor_key.lower():
            if "package" in label.lower():
                name = "CPU_PKG"
            elif "core" in label.lower():
                # Extract core number
                import re
                match = re.search(r'core (\d+)', label.lower())
                if match:
                    name = f"CPU_C{match.group(1)}"
                else:
                    name = "CPU_Temp"
            else:
                name = "CPU_Temp"
        elif "k10temp" in sensor_key.lower():
            name = "CPU_Tctl" if "tctl" in label.lower() else "CPU_Temp"
        elif "nvidia" in sensor_key.lower() or "gpu" in sensor_key.lower():
            name = "GPU_Temp"
        elif "nvme" in sensor_key.lower():
            name = "NVMe_Temp"
        else:
            name = label[:10] if label else "TEMP"

    elif sensor_type == "fan":
        if "fan" in label.lower():
            import re
            match = re.search(r'(\d+)', label)
            if match:
                name = f"FAN{match.group(1)}"
            else:
                name = "FAN"
        else:
            name = label[:10] if label else "FAN"

    # Clean up
    name = name.replace(" ", "_").replace("-", "_")

    # Truncate if too long
    if len(name) > 10:
        name = name[:10]

    return name if name else "SENSOR"


# ============================================================================
# CLI Helper Functions - Validation
# ============================================================================

def validate_ip(ip: str) -> str:
    """
    Validate IPv4 address format
    """
    if not ip:
        raise ValueError("IP address cannot be empty")

    parts = ip.split('.')
    if len(parts) != 4:
        raise ValueError("Invalid IP format (must be xxx.xxx.xxx.xxx)")

    for part in parts:
        try:
            num = int(part)
            if num < 0 or num > 255:
                raise ValueError("IP octets must be 0-255")
        except ValueError:
            raise ValueError("IP octets must be numbers")

    return ip


def validate_port(port_str: str) -> int:
    """
    Validate UDP port number
    """
    try:
        port = int(port_str)
        if port < 1 or port > 65535:
            raise ValueError("Port must be between 1 and 65535")
        return port
    except ValueError:
        raise ValueError("Port must be a valid number (1-65535)")


def validate_interval(interval_str: str) -> float:
    """
    Validate update interval
    """
    try:
        interval = float(interval_str)
        if interval < 0.5:
            raise ValueError("Interval must be at least 0.5 seconds")
        return interval
    except ValueError:
        raise ValueError("Interval must be a valid number (>= 0.5)")


# ============================================================================
# CLI Helper Functions - Input
# ============================================================================

def prompt_with_default(prompt: str, default: str, validator=None) -> str:
    """
    Prompt user with a default value
    Returns default if user presses Enter, otherwise validates and returns input
    """
    while True:
        try:
            user_input = input(f"{prompt} [{default}]: ").strip()

            # Use default if empty
            if not user_input:
                value = default
            else:
                value = user_input

            # Validate if validator provided
            if validator:
                return str(validator(value))
            else:
                return value

        except ValueError as e:
            print(f"  ✗ Error: {e}")
            continue
        except KeyboardInterrupt:
            print("\n\nConfiguration cancelled.")
            return None


def parse_number_list(input_str: str, max_num: int) -> list:
    """
    Parse comma-separated list of numbers with validation
    Returns list of integers or raises ValueError
    """
    if not input_str.strip():
        raise ValueError("Please enter at least one metric number")

    # Split by comma and parse
    numbers = []
    for part in input_str.split(','):
        try:
            num = int(part.strip())
            if num < 1 or num > max_num:
                raise ValueError(f"Number {num} is out of range (1-{max_num})")
            numbers.append(num)
        except ValueError as e:
            if "invalid literal" in str(e):
                raise ValueError(f"'{part.strip()}' is not a valid number")
            else:
                raise

    # Check for duplicates
    if len(numbers) != len(set(numbers)):
        raise ValueError("Duplicate numbers detected")

    # Check max limit
    if len(numbers) > MAX_METRICS:
        raise ValueError(f"Too many metrics selected (max {MAX_METRICS})")

    return numbers


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


def setup_autostart_systemd(enable=True):
    """
    Setup systemd service for autostart on Linux
    """
    import subprocess

    service_name = "pc-monitor"
    service_file = f"{service_name}.service"
    user_service_dir = os.path.expanduser("~/.config/systemd/user")
    service_path = os.path.join(user_service_dir, service_file)

    script_path = os.path.abspath(__file__)
    script_dir = os.path.dirname(script_path)

    if enable:
        # Create systemd user service directory if it doesn't exist
        os.makedirs(user_service_dir, exist_ok=True)

        # Generate service file content
        service_content = f"""[Unit]
Description=PC Stats Monitor for ESP32
After=network.target

[Service]
Type=simple
WorkingDirectory={script_dir}
ExecStart={sys.executable} {script_path}
Restart=on-failure
RestartSec=10

[Install]
WantedBy=default.target
"""

        try:
            # Write service file
            with open(service_path, 'w') as f:
                f.write(service_content)

            # Reload systemd daemon
            subprocess.run(["systemctl", "--user", "daemon-reload"], check=True)

            # Enable service
            subprocess.run(["systemctl", "--user", "enable", service_name], check=True)

            # Start service
            subprocess.run(["systemctl", "--user", "start", service_name], check=True)

            print(f"\n✓ Autostart enabled via systemd!")
            print(f"  Service file: {service_path}")
            print(f"  Service name: {service_name}")
            print(f"\nUseful commands:")
            print(f"  Check status: systemctl --user status {service_name}")
            print(f"  View logs:    journalctl --user -u {service_name} -f")
            print(f"  Stop service: systemctl --user stop {service_name}")
            return True

        except subprocess.CalledProcessError as e:
            print(f"\n✗ Error setting up systemd service: {e}")
            print("  Make sure systemd is available on your system")
            return False
        except Exception as e:
            print(f"\n✗ Error: {e}")
            return False

    else:
        # Disable autostart
        try:
            # Stop service
            subprocess.run(["systemctl", "--user", "stop", service_name],
                         stderr=subprocess.DEVNULL)

            # Disable service
            subprocess.run(["systemctl", "--user", "disable", service_name],
                         stderr=subprocess.DEVNULL)

            # Remove service file
            if os.path.exists(service_path):
                os.remove(service_path)

            # Reload systemd daemon
            subprocess.run(["systemctl", "--user", "daemon-reload"],
                         stderr=subprocess.DEVNULL)

            print(f"\n✓ Autostart disabled!")
            print(f"  Service stopped and removed: {service_name}")
            return True

        except Exception as e:
            print(f"\n✗ Error removing service: {e}")
            return False


def check_autostart_status():
    """
    Check if autostart is enabled
    """
    import subprocess

    service_name = "pc-monitor"

    try:
        result = subprocess.run(
            ["systemctl", "--user", "is-enabled", service_name],
            capture_output=True,
            text=True
        )
        return result.returncode == 0 and "enabled" in result.stdout.lower()
    except:
        return False


# ============================================================================
# GUI Configuration Mode (requires tkinter)
# ============================================================================

class MetricSelectorGUI:
    """
    Tkinter GUI for selecting metrics and configuring settings - Linux version
    Note: tkinter is imported when this class is instantiated (see main())
    """
    def __init__(self, root, existing_config=None):
        self.root = root
        self.root.title("PC Monitor v2.0 - Linux Configuration")
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
            text="PC Monitor Configuration - Linux",
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
        self.ip_var = tk.StringVar(value=self.config.get("esp32_ip", "192.168.1.197"))
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
        tk.Label(settings_frame, text="Systemd Autostart:", bg="#2d2d2d", fg="#ffffff", font=("Arial", 10)).grid(row=1, column=0, padx=10, pady=5, sticky="e")

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
        if check_autostart_status():
            return "✓ Enabled"
        else:
            return "✗ Disabled"

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
        """Enable autostart via systemd"""
        try:
            success = setup_autostart_systemd(enable=True)
            if success:
                self.update_autostart_status()
                messagebox.showinfo("Success", "Autostart enabled!\n\nThe service will start automatically on system boot.\n\nUseful commands:\n• Check status: systemctl --user status pc-monitor\n• View logs: journalctl --user -u pc-monitor -f")
            else:
                messagebox.showerror("Error", "Failed to enable autostart.\nCheck console for details.")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to enable autostart:\n{str(e)}\n\nMake sure systemd is available on your system.")

    def disable_autostart(self):
        """Disable autostart via systemd"""
        try:
            success = setup_autostart_systemd(enable=False)
            if success:
                self.update_autostart_status()
                messagebox.showinfo("Success", "Autostart disabled!\n\nThe service has been stopped and removed.")
            else:
                messagebox.showwarning("Warning", "Could not disable autostart.\nCheck console for details.")
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
            "version": "2.0-linux",
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


# ============================================================================
# CLI Configuration Mode
# ============================================================================

def display_all_sensors_cli():
    """
    Display all available sensors with sequential numbering for CLI selection
    Returns a mapping of numbers to sensor objects
    """
    print("\n" + "=" * 70)
    print("  AVAILABLE METRICS")
    print("=" * 70)

    sensor_map = {}
    counter = 1

    categories = [
        ("SYSTEM METRICS", "system"),
        ("TEMPERATURES", "temperature"),
        ("FANS & COOLING", "fan"),
        ("NETWORK DATA", "data"),
        ("NETWORK THROUGHPUT", "throughput")
    ]

    for cat_title, cat_key in categories:
        sensors = sensor_database.get(cat_key, [])
        if not sensors:
            continue

        print(f"\n{cat_title}:")
        print("-" * 70)

        for sensor in sensors:
            # Format: [#] Display Name (short_name) - CurrentValue Unit
            value_str = f" - {sensor['current_value']}{sensor['unit']}" if sensor.get('current_value') is not None else ""
            print(f"  [{counter:2d}] {sensor['display_name']} ({sensor['name']}){value_str}")

            sensor_map[counter] = sensor
            counter += 1

    print("\n" + "=" * 70)
    print(f"Total available metrics: {len(sensor_map)}")
    print("=" * 70 + "\n")

    return sensor_map


def configure_cli_mode(existing_config=None):
    """
    CLI configuration mode for selecting metrics and settings
    Returns configuration dictionary or None if cancelled
    """
    print("\n" + "=" * 70)
    print("  CLI CONFIGURATION MODE")
    print("=" * 70)

    # Determine defaults from existing config or DEFAULT_CONFIG
    if existing_config:
        default_ip = existing_config.get("esp32_ip", DEFAULT_CONFIG["esp32_ip"])
        default_port = str(existing_config.get("udp_port", DEFAULT_CONFIG["udp_port"]))
        default_interval = str(existing_config.get("update_interval", DEFAULT_CONFIG["update_interval"]))
    else:
        default_ip = DEFAULT_CONFIG["esp32_ip"]
        default_port = str(DEFAULT_CONFIG["udp_port"])
        default_interval = str(DEFAULT_CONFIG["update_interval"])

    # Step 1: Configure ESP32 connection settings
    print("\nStep 1: ESP32 Connection Settings")
    print("-" * 70)

    esp_ip = prompt_with_default("ESP32 IP address", default_ip, validate_ip)
    if esp_ip is None:  # Cancelled
        return None

    port = prompt_with_default("UDP Port", default_port, validate_port)
    if port is None:  # Cancelled
        return None

    interval = prompt_with_default("Update Interval (seconds)", default_interval, validate_interval)
    if interval is None:  # Cancelled
        return None

    # Step 2: Metrics selection (check if editing existing config)
    print("\n" + "=" * 70)
    print("Step 2: Select Metrics to Monitor")
    print("=" * 70)

    selected_sensors = []
    keep_metrics = None  # Track if user wants to keep existing metrics

    # If editing existing config with metrics, show current selection
    if existing_config and existing_config.get("metrics"):
        print("\nCurrent metric selection:")
        print("-" * 70)
        for i, metric in enumerate(existing_config["metrics"]):
            label_info = f" - Label: \"{metric['custom_label']}\"" if metric.get('custom_label') else ""
            print(f"  {i+1}. {metric['display_name']} ({metric['name']}){label_info}")
        print("-" * 70)

        keep_metrics = input("\nKeep current metrics? (y/n) [y]: ").strip().lower()

        if keep_metrics in ['', 'y', 'yes']:
            # Keep existing metrics
            selected_sensors = existing_config["metrics"]
            print(f"\n✓ Keeping {len(selected_sensors)} existing metric(s)")
        else:
            # Re-select metrics
            print("\nRe-selecting metrics...")

    # If no existing metrics or user wants to re-select
    if not selected_sensors:
        sensor_map = display_all_sensors_cli()

        if not sensor_map:
            print("\n✗ No sensors found! Cannot continue.")
            return None

        # Step 3: Get metric selection
        print(f"Select up to {MAX_METRICS} metrics by entering their numbers separated by commas.")
        print(f"Example: 1,2,5,8")

        while True:
            try:
                selection = input(f"\nEnter metric numbers (comma-separated) [max {MAX_METRICS}]: ").strip()
                if not selection:
                    print("  ✗ Error: Please enter at least one metric number")
                    continue

                # Parse and validate
                numbers = parse_number_list(selection, len(sensor_map))

                # Get selected sensors
                selected_sensors = [sensor_map[num] for num in numbers]

                print(f"\n✓ Selected {len(selected_sensors)} metric(s)")
                break

            except ValueError as e:
                print(f"  ✗ Error: {e}")
                continue
            except KeyboardInterrupt:
                print("\n\nConfiguration cancelled.")
                return None

    # Step 3: Custom labels (only if newly selected metrics or user chose to re-select)
    custom_labels = {}
    kept_existing_metrics = existing_config and existing_config.get("metrics") and keep_metrics in ['', 'y', 'yes']

    if not kept_existing_metrics:
        print("\n" + "=" * 70)
        print("Step 3: Custom Labels (Optional)")
        print("=" * 70)
        print("You can assign custom labels (max 10 characters) to each metric.")
        print("Custom labels will be displayed on the ESP32 OLED.")

        configure_labels = input("\nConfigure custom labels? (y/n) [n]: ").strip().lower()

        if configure_labels in ['y', 'yes']:
            print("\nEnter custom label for each metric (press Enter to use default):\n")

            for i, sensor in enumerate(selected_sensors):
                default_name = sensor['name']
                label = input(f"  [{i+1}] {sensor['display_name']} (default: {default_name}): ").strip()

                if label:
                    # Truncate to 10 chars
                    label = label[:10]
                    sensor_key = f"{sensor['source']}_{sensor['name']}"
                    custom_labels[sensor_key] = label
                    print(f"      → Will use: '{label}'")
                else:
                    print(f"      → Will use default: '{default_name}'")

    # Step 4: Build configuration
    config = {
        "version": "2.0-linux",
        "esp32_ip": esp_ip,
        "udp_port": int(port),
        "update_interval": float(interval),
        "metrics": []
    }

    # Add metrics
    if kept_existing_metrics:
        # Keep existing metrics with their IDs and custom labels
        config["metrics"] = selected_sensors
    else:
        # Build new metrics with custom labels
        for i, sensor in enumerate(selected_sensors):
            metric_config = sensor.copy()
            metric_config["id"] = i + 1

            # Apply custom label if set
            sensor_key = f"{sensor['source']}_{sensor['name']}"
            if sensor_key in custom_labels:
                metric_config["custom_label"] = custom_labels[sensor_key]

            config["metrics"].append(metric_config)

    # Step 6: Show summary and confirm
    print("\n" + "=" * 70)
    print("  CONFIGURATION SUMMARY")
    print("=" * 70)
    print(f"\nESP32 IP:        {config['esp32_ip']}")
    print(f"UDP Port:        {config['udp_port']}")
    print(f"Update Interval: {config['update_interval']}s")
    print(f"\nSelected Metrics ({len(config['metrics'])}):")
    print("-" * 70)

    for metric in config["metrics"]:
        label_info = f" - Label: \"{metric['custom_label']}\"" if metric.get('custom_label') else ""
        print(f"  {metric['id']}. {metric['display_name']} ({metric['name']}){label_info}")

    print("=" * 70)

    confirm = input("\nSave and start monitoring? (y/n) [y]: ").strip().lower()
    if confirm in ['', 'y', 'yes']:
        if save_config(config):
            print("\n✓ Configuration saved successfully!")

            # Ask about autostart (systemd service)
            print("\n" + "=" * 70)
            print("  SYSTEMD AUTOSTART (OPTIONAL)")
            print("=" * 70)
            print("You can enable automatic monitoring on system boot using systemd.")
            print("This will create a user service that starts automatically.")

            # Check current autostart status
            if check_autostart_status():
                print("\nℹ  Autostart is currently: ENABLED")
                autostart_choice = input("Keep autostart enabled? (y/n) [y]: ").strip().lower()
                if autostart_choice not in ['', 'y', 'yes']:
                    # Disable autostart - then run script normally
                    print("\nDisabling autostart...")
                    if setup_autostart_systemd(False):
                        print("✓ Autostart disabled")
                    else:
                        print("✗ Failed to disable autostart")
                    print("\nStarting monitoring in foreground mode...")
                    return config  # Run monitoring normally
                else:
                    # Keep autostart enabled - start service and exit
                    print("\n✓ Autostart remains enabled")
                    print("\nStarting monitoring service...")
                    import subprocess
                    try:
                        subprocess.run(["systemctl", "--user", "start", "pc-monitor"], check=True)
                        print("✓ Monitoring service started successfully!")
                        print("\nThe service is now running in the background.")
                        print("\nUseful commands:")
                        print("  • Check status: systemctl --user status pc-monitor")
                        print("  • View logs:    journalctl --user -u pc-monitor -f")
                        print("  • Stop service: systemctl --user stop pc-monitor")
                        print("  • Restart:      systemctl --user restart pc-monitor")
                        print("\nExiting configuration...")
                        sys.exit(0)  # Exit cleanly
                    except subprocess.CalledProcessError:
                        print("✗ Failed to start service")
                        print("  Starting monitoring in foreground mode instead...")
                        return config  # Fallback to normal execution
            else:
                print("\nℹ  Autostart is currently: DISABLED")
                autostart_choice = input("Enable autostart? (y/n) [n]: ").strip().lower()
                if autostart_choice in ['y', 'yes']:
                    # Enable autostart - start service and exit
                    print("\nEnabling autostart...")
                    if setup_autostart_systemd(True):
                        print("✓ Autostart enabled successfully!")
                        print("\nStarting monitoring service...")
                        import subprocess
                        try:
                            subprocess.run(["systemctl", "--user", "start", "pc-monitor"], check=True)
                            print("✓ Monitoring service started successfully!")
                            print("\nThe service is now running in the background and will")
                            print("start automatically on system boot.")
                            print("\nUseful commands:")
                            print("  • Check status: systemctl --user status pc-monitor")
                            print("  • View logs:    journalctl --user -u pc-monitor -f")
                            print("  • Stop service: systemctl --user stop pc-monitor")
                            print("  • Restart:      systemctl --user restart pc-monitor")
                            print("\nExiting configuration...")
                            sys.exit(0)  # Exit cleanly
                        except subprocess.CalledProcessError:
                            print("✗ Failed to start service")
                            print("  Starting monitoring in foreground mode instead...")
                            return config  # Fallback to normal execution
                    else:
                        print("✗ Failed to enable autostart")
                        print("\nStarting monitoring in foreground mode...")
                        return config  # Run monitoring normally
                else:
                    # No autostart - run script normally
                    print("\nStarting monitoring in foreground mode...")
                    return config  # Run monitoring normally
        else:
            print("\n✗ Failed to save configuration!")
            return None
    else:
        print("\nConfiguration discarded.")
        return None


def get_metric_value(metric_config):
    """
    Get current value for a configured metric - Linux version
    """
    global network_last_sample, network_last_time

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
            return int(psutil.disk_usage('/').percent)

    elif source == "psutil_temp":
        try:
            temps = psutil.sensors_temperatures()
            sensor_key = metric_config["sensor_key"]
            sensor_label = metric_config["sensor_label"]

            if sensor_key in temps:
                for entry in temps[sensor_key]:
                    if entry.label == sensor_label:
                        return int(entry.current)
        except:
            pass

    elif source == "psutil_fan":
        try:
            fans = psutil.sensors_fans()
            sensor_key = metric_config["sensor_key"]
            sensor_label = metric_config["sensor_label"]

            if sensor_key in fans:
                for entry in fans[sensor_key]:
                    if entry.label == sensor_label:
                        return int(entry.current)
        except:
            pass

    elif source == "psutil_net":
        try:
            net_io = psutil.net_io_counters(pernic=True)
            interface = metric_config["interface"]
            metric_name = metric_config["metric"]

            if interface in net_io:
                value = getattr(net_io[interface], metric_name)
                return int(value / (1024**2))  # Convert to MB
        except:
            pass

    elif source == "psutil_net_speed":
        try:
            current_time = time.time()
            net_io = psutil.net_io_counters(pernic=True)
            interface = metric_config["interface"]
            metric_name = metric_config["metric"]

            if interface in net_io:
                current_bytes = getattr(net_io[interface], metric_name)

                # Calculate speed if we have previous sample
                key = f"{interface}_{metric_name}"
                if key in network_last_sample and network_last_time:
                    time_delta = current_time - network_last_time
                    bytes_delta = current_bytes - network_last_sample[key]

                    if time_delta > 0:
                        # MB/s
                        speed = (bytes_delta / time_delta) / (1024**2)
                        return int(speed) if speed >= 1 else 0

                # Update last sample
                network_last_sample[key] = current_bytes

                return 0
        except:
            pass

    return 0


def send_metrics(sock, config):
    """
    Collect metric values and send to ESP32 - Linux version
    """
    global network_last_time

    # Update network sample time
    network_last_time = time.time()

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


def run_monitoring(config):
    """Run monitoring loop in console mode - Linux version"""
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
    Main entry point - Linux version
    """
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='PC Stats Monitor v2.0 - Linux Edition')
    parser.add_argument('--configure', action='store_true', help='Force configuration GUI')
    parser.add_argument('--edit', action='store_true', help='Edit existing configuration')
    parser.add_argument('--cli', '--nogui', action='store_true', dest='cli_mode',
                       help='Configure and run in CLI mode (no GUI required)')
    parser.add_argument('--autostart', choices=['enable', 'disable'], help='Enable/disable systemd autostart')
    args = parser.parse_args()

    # Handle autostart
    if args.autostart:
        try:
            success = setup_autostart_systemd(args.autostart == 'enable')
            if success and args.autostart == 'enable':
                print("\nTIP: The service will start automatically on system boot")
                print("     Use systemctl --user status pc-monitor to check status")
        except Exception as e:
            print(f"\n✗ Error setting up autostart: {e}")
            print("  Make sure systemd is available on your system")
        return

    print("\n" + "=" * 60)
    print("  PC STATS MONITOR v2.0 - LINUX EDITION")
    print("  Dynamic Sensor Monitoring with GUI Configuration")
    print("=" * 60 + "\n")

    # Check for config file
    config = load_config()

    # Determine if configuration is needed
    needs_config = args.configure or args.edit or config is None

    # Auto-detect headless environment (optional)
    if needs_config and not args.cli_mode:
        try:
            if not os.environ.get('DISPLAY'):
                print("\n⚠  No DISPLAY environment variable detected (headless system).")
                print("   Switching to CLI mode automatically.")
                print("   Use --configure to force GUI mode on systems with display.\n")
                args.cli_mode = True
        except:
            pass

    # Configuration mode
    if needs_config:
        if args.cli_mode:
            # CLI configuration mode
            if config is None:
                print("\nNo configuration found. Starting CLI configuration...")
            else:
                print("\nOpening CLI configuration editor...")

            # Discover sensors
            discover_sensors()

            # Run CLI configuration
            config = configure_cli_mode(config if args.edit else None)

            if config is None:
                print("\nConfiguration cancelled. Exiting.")
                return

        else:
            # GUI configuration mode
            if config is None:
                print("\nNo configuration found. Starting GUI...")
            else:
                print("\nOpening configuration editor...")

            # Import tkinter only when GUI mode is needed
            try:
                import tkinter as tk
                from tkinter import ttk, messagebox
            except ImportError:
                print("\n✗ Error: tkinter is not installed!")
                print("  On headless systems, use CLI mode instead:")
                print("  python3 pc_stats_monitor_v2_linux.py --cli")
                print("\n  To install tkinter:")
                print("  apt install python3-tk")
                return

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

    # Run monitoring
    run_monitoring(config)


if __name__ == "__main__":
    main()
