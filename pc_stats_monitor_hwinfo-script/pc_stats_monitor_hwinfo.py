"""
PC Stats Monitor - HWiNFO Edition v2.0
Dynamic sensor monitoring using HWiNFO64 shared memory
Sends metrics to ESP32 OLED display via UDP

Features:
- Direct HWiNFO64 shared memory access (faster than WMI)
- Support for FPS counters, voltage, and current sensors
- GUI configuration with up to 20 customizable metrics
- System tray support with pystray
- Windows autostart functionality
- 100% compatible with ESP32 JSON V2.0 protocol
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

# HWiNFO shared memory access
import ctypes
from ctypes import *
from ctypes import wintypes
import win32event
import win32api

# Windows API constants for shared memory
FILE_MAP_READ = 0x0004
INVALID_HANDLE_VALUE = -1

# Try to import pystray for system tray support
try:
    import pystray
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False

# ==================== CONFIGURATION ====================

# Configuration file path (separate from LibreHardwareMonitor version)
CONFIG_FILE = "monitor_config_hwinfo.json"

# Default configuration
DEFAULT_CONFIG = {
    "version": "2.0",
    "esp32_ip": "192.168.0.163",
    "udp_port": 4210,
    "update_interval": 3,
    "metrics": []
}

# Maximum metrics supported by ESP32
MAX_METRICS = 20

# HWiNFO shared memory constants
HWINFO_SENSORS_SM2 = "Global\\HWiNFO_SENS_SM2"
HWINFO_SENSORS_MUTEX = "Global\\HWiNFO_SM2_MUTEX"
HWINFO_SENSORS_STRING = b'HiWS'  # 'SiWH' in little-endian becomes 'HiWS' when read as bytes

# Global sensor database
sensor_database = {
    "system": [],        # psutil-based metrics (CPU%, RAM%, Disk%)
    "temperature": [],
    "voltage": [],       # NEW in HWiNFO
    "fan": [],
    "current": [],       # NEW in HWiNFO
    "load": [],
    "clock": [],
    "power": [],
    "data": [],          # Network/disk data (WMI only)
    "throughput": [],    # Network throughput (WMI only)
    "other": []
}

# Global sensor source tracking
sensor_source = None  # Will be "hwinfo" or "wmi"

# ==================== CTYPES STRUCTURES ====================

class SensorType:
    """HWiNFO sensor type enumeration"""
    SENSOR_TYPE_NONE = 0
    SENSOR_TYPE_TEMP = 1
    SENSOR_TYPE_VOLT = 2
    SENSOR_TYPE_FAN = 3
    SENSOR_TYPE_CURRENT = 4
    SENSOR_TYPE_POWER = 5
    SENSOR_TYPE_CLOCK = 6
    SENSOR_TYPE_USAGE = 7
    SENSOR_TYPE_OTHER = 8


class HWiNFO_SHARED_MEM_HEADER(ctypes.Structure):
    """HWiNFO shared memory header structure (44 bytes)"""
    _pack_ = 1  # CRITICAL: Prevent padding to match C++ layout
    _fields_ = [
        ("dwSignature", ctypes.c_uint32),              # Offset 0: 'SiWH' signature
        ("dwVersion", ctypes.c_uint32),                # Offset 4: Version
        ("dwRevision", ctypes.c_uint32),               # Offset 8: Revision
        ("poll_time", ctypes.c_int64),                 # Offset 12: Last poll (FILETIME)
        ("dwOffsetOfSensorSection", ctypes.c_uint32),  # Offset 20: Sensor array offset
        ("dwSizeOfSensorElement", ctypes.c_uint32),    # Offset 24: Size of sensor element
        ("dwNumSensorElements", ctypes.c_uint32),      # Offset 28: Number of sensors
        ("dwOffsetOfReadingSection", ctypes.c_uint32), # Offset 32: Readings array offset
        ("dwSizeOfReadingElement", ctypes.c_uint32),   # Offset 36: Size of reading element
        ("dwNumReadingElements", ctypes.c_uint32),     # Offset 40: Number of readings
    ]


class HWiNFO_SENSOR(ctypes.Structure):
    """Individual sensor (device) structure (~280 bytes)"""
    _pack_ = 1
    _fields_ = [
        ("dwSensorID", ctypes.c_uint32),              # Unique sensor ID
        ("dwSensorInst", ctypes.c_uint32),            # Sensor instance
        ("szSensorNameOrig", ctypes.c_char * 128),    # Original name
        ("szSensorNameUser", ctypes.c_char * 128),    # User-customized name
    ]


class HWiNFO_ELEMENT(ctypes.Structure):
    """Individual sensor reading structure (~344 bytes)"""
    _pack_ = 1
    _fields_ = [
        ("tReading", ctypes.c_uint32),                # SensorType enum
        ("dwSensorIndex", ctypes.c_uint32),           # Parent sensor index
        ("dwReadingID", ctypes.c_uint32),             # Unique reading ID
        ("szLabelOrig", ctypes.c_char * 128),         # Original label
        ("szLabelUser", ctypes.c_char * 128),         # User-customized label
        ("szUnit", ctypes.c_char * 16),               # Unit string
        ("Value", ctypes.c_double),                   # Current value
        ("ValueMin", ctypes.c_double),                # Minimum
        ("ValueMax", ctypes.c_double),                # Maximum
        ("ValueAvg", ctypes.c_double),                # Average
    ]


# ==================== HWINFO READER CLASS ====================

class HWiNFOReader:
    """Thread-safe HWiNFO shared memory reader"""

    def __init__(self):
        self.shmem_handle = None
        self.shmem_view = None
        self.shmem_size = 0x100000  # 1MB
        self.mutex = None
        self.header = None
        self.sensors = []
        self.readings = []
        self.kernel32 = ctypes.windll.kernel32

    def open(self):
        """
        Open shared memory and validate header

        Raises:
            RuntimeError: HWiNFO not running or shared memory disabled
            ValueError: Invalid signature or corrupted memory
        """
        try:
            # Open mutex for synchronization
            self.mutex = win32event.OpenMutex(
                win32event.SYNCHRONIZE,
                False,
                HWINFO_SENSORS_MUTEX
            )

            # Configure Windows API function prototypes
            self.kernel32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
            self.kernel32.OpenFileMappingW.restype = wintypes.HANDLE

            self.kernel32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
            self.kernel32.MapViewOfFile.restype = wintypes.LPVOID

            # Open the shared memory object
            self.shmem_handle = self.kernel32.OpenFileMappingW(
                FILE_MAP_READ,
                False,
                HWINFO_SENSORS_SM2
            )

            if not self.shmem_handle or self.shmem_handle == INVALID_HANDLE_VALUE:
                raise FileNotFoundError("Shared memory object not found")

            # Map view of file into our address space
            self.shmem_view = self.kernel32.MapViewOfFile(
                self.shmem_handle,
                FILE_MAP_READ,
                0,  # dwFileOffsetHigh
                0,  # dwFileOffsetLow
                self.shmem_size
            )

            if not self.shmem_view:
                self.kernel32.CloseHandle(self.shmem_handle)
                raise RuntimeError("Failed to map view of shared memory")

            # Parse header
            self._parse_header()

            # Validate signature (HWiNFO shared memory signature 'SiWH')
            # Valid signature should be non-zero (typically 0x48575369 or 0x53695748)
            if self.header.dwSignature == 0 or self.header.dwSignature == 0xFFFFFFFF:
                raise ValueError(
                    f"Invalid HWiNFO signature: 0x{self.header.dwSignature:08X}. "
                    "Is HWiNFO64 running with shared memory enabled?"
                )

            print(f"[OK] HWiNFO shared memory opened")
            print(f"  Version: {self.header.dwVersion}.{self.header.dwRevision}")
            print(f"  Sensors: {self.header.dwNumSensorElements}")
            print(f"  Readings: {self.header.dwNumReadingElements}")

            return True

        except FileNotFoundError:
            raise RuntimeError(
                "HWiNFO shared memory not found!\n\n"
                "SETUP INSTRUCTIONS:\n"
                "1. Make sure HWiNFO64 is running (not HWiNFO32)\n"
                "2. Click Settings (gear icon) in HWiNFO\n"
                "3. Go to: Safety tab\n"
                "4. Enable 'Shared Memory Support'\n"
                "5. Click OK and restart HWiNFO64\n"
                "6. Re-run this script\n\n"
                "NOTE: Free version has 12-hour limit. HWiNFO will remind you when it expires."
            )
        except Exception as e:
            raise RuntimeError(f"Failed to open HWiNFO shared memory: {e}")

    def _parse_header(self):
        """Parse shared memory header (with mutex lock)"""
        win32event.WaitForSingleObject(self.mutex, win32event.INFINITE)
        try:
            # Read header from memory view using ctypes
            header_ptr = ctypes.cast(self.shmem_view, ctypes.POINTER(HWiNFO_SHARED_MEM_HEADER))
            self.header = header_ptr.contents
        finally:
            win32event.ReleaseMutex(self.mutex)

    def read_sensors(self):
        """Read all sensor definitions (devices)"""
        win32event.WaitForSingleObject(self.mutex, win32event.INFINITE)
        try:
            sensors = []
            offset = self.header.dwOffsetOfSensorSection
            element_size = self.header.dwSizeOfSensorElement

            for i in range(self.header.dwNumSensorElements):
                # Calculate pointer to sensor at offset
                sensor_addr = self.shmem_view + offset + i * element_size
                # Read sensor data and make a copy
                sensor_bytes = ctypes.string_at(sensor_addr, ctypes.sizeof(HWiNFO_SENSOR))
                sensor = HWiNFO_SENSOR.from_buffer_copy(sensor_bytes)
                sensors.append(sensor)

            self.sensors = sensors
            return sensors
        finally:
            win32event.ReleaseMutex(self.mutex)

    def read_readings(self):
        """Read all sensor readings (values)"""
        win32event.WaitForSingleObject(self.mutex, win32event.INFINITE)
        try:
            readings = []
            offset = self.header.dwOffsetOfReadingSection
            element_size = self.header.dwSizeOfReadingElement

            for i in range(self.header.dwNumReadingElements):
                # Calculate pointer to reading at offset
                reading_addr = self.shmem_view + offset + i * element_size
                # Read reading data and make a copy
                reading_bytes = ctypes.string_at(reading_addr, ctypes.sizeof(HWiNFO_ELEMENT))
                reading = HWiNFO_ELEMENT.from_buffer_copy(reading_bytes)
                readings.append(reading)

            self.readings = readings
            return readings
        finally:
            win32event.ReleaseMutex(self.mutex)

    def get_reading_value(self, reading_id):
        """
        Get current value for a specific reading ID

        Args:
            reading_id: dwReadingID from HWiNFO_ELEMENT

        Returns:
            Float value or 0 if not found
        """
        # Re-read all readings to get fresh values
        self.read_readings()

        for reading in self.readings:
            if reading.dwReadingID == reading_id:
                return reading.Value

        return 0

    def close(self):
        """Clean up resources"""
        if self.shmem_view:
            # Configure UnmapViewOfFile argtypes if not already done
            if not hasattr(self.kernel32.UnmapViewOfFile, '_argtypes_set'):
                self.kernel32.UnmapViewOfFile.argtypes = [wintypes.LPVOID]
                self.kernel32.UnmapViewOfFile.restype = wintypes.BOOL
                self.kernel32.UnmapViewOfFile._argtypes_set = True

            self.kernel32.UnmapViewOfFile(ctypes.c_void_p(self.shmem_view))
            self.shmem_view = None
        if self.shmem_handle:
            self.kernel32.CloseHandle(self.shmem_handle)
            self.shmem_handle = None
        if self.mutex:
            win32api.CloseHandle(self.mutex)
            self.mutex = None


# ==================== HELPER FUNCTIONS ====================

def get_sensor_type_name(hwinfo_type):
    """Convert HWiNFO sensor type enum to category string"""
    type_map = {
        SensorType.SENSOR_TYPE_TEMP: "temperature",
        SensorType.SENSOR_TYPE_VOLT: "voltage",
        SensorType.SENSOR_TYPE_FAN: "fan",
        SensorType.SENSOR_TYPE_CURRENT: "current",
        SensorType.SENSOR_TYPE_POWER: "power",
        SensorType.SENSOR_TYPE_CLOCK: "clock",
        SensorType.SENSOR_TYPE_USAGE: "load",
        SensorType.SENSOR_TYPE_OTHER: "other"
    }
    return type_map.get(hwinfo_type, "other")


def map_hwinfo_unit(hwinfo_unit):
    """
    Map HWiNFO unit strings to ESP32 display format (max 4 chars)

    Args:
        hwinfo_unit: Raw unit string from HWiNFO

    Returns:
        Normalized unit string (max 4 chars for ESP32)
    """
    unit_map = {
        "°C": "C",
        "C": "C",
        "°F": "F",
        "V": "V",
        "mV": "mV",
        "A": "A",
        "mA": "mA",
        "W": "W",
        "MHz": "MHz",
        "RPM": "RPM",
        "%": "%",
        "KB/s": "KB/s",
        "MB/s": "MB/s",
        "GB": "GB",
        "TB": "TB",
        "FPS": "FPS",
        "ms": "ms",
        "°": "C",  # Assume Celsius if just degree symbol
    }

    # Exact match
    if hwinfo_unit in unit_map:
        return unit_map[hwinfo_unit]

    # Fallback: trim to 4 chars
    return hwinfo_unit.strip()[:4]


def generate_short_name_hwinfo(reading_label, sensor_name, sensor_type, unit):
    """
    Generate a short name (max 10 chars) for ESP32 display

    Args:
        reading_label: e.g., "Core 0 Temperature", "VCore Voltage"
        sensor_name: e.g., "CPU [#0]: Intel Core i9-12900K", "NVIDIA GeForce RTX 3080"
        sensor_type: SensorType enum value
        unit: Unit string

    Returns:
        String max 10 characters for ESP32 display

    Examples:
        ("Core 0 Temperature", "CPU [#0]: Intel i9", TEMP, "°C") → "CPU_C0"
        ("GPU Temperature", "NVIDIA RTX 3080", TEMP, "°C") → "GPU_TEMP"
        ("VCore", "Motherboard", VOLT, "V") → "VCORE"
        ("Fan #1", "Motherboard", FAN, "RPM") → "FAN1"
    """
    import re

    # Detect device type from sensor name
    device_prefix = ""
    sensor_lower = sensor_name.lower()
    label_lower = reading_label.lower()

    # Device detection
    if 'cpu' in sensor_lower or 'processor' in sensor_lower or 'intel' in sensor_lower or 'amd ryzen' in sensor_lower:
        device_prefix = "CPU_"
    elif 'gpu' in sensor_lower or 'nvidia' in sensor_lower or 'geforce' in sensor_lower or 'radeon' in sensor_lower or 'amd radeon' in sensor_lower:
        device_prefix = "GPU_"
    elif 'motherboard' in sensor_lower or 'mainboard' in sensor_lower or 'chipset' in sensor_lower:
        device_prefix = "MB_"
    elif 'nvme' in sensor_lower:
        device_prefix = "NVM"
    elif 'ssd' in sensor_lower:
        device_prefix = "SSD"
    elif 'hdd' in sensor_lower or 'hard disk' in sensor_lower or 'wd' in sensor_lower or 'seagate' in sensor_lower:
        device_prefix = "HDD"
    elif 'network' in sensor_lower or 'ethernet' in sensor_lower or 'nic' in sensor_lower or 'wi-fi' in sensor_lower:
        device_prefix = "NET"

    # Start with reading label
    name = reading_label.strip()

    # ==================== TEMPERATURE SENSORS ====================
    if sensor_type == SensorType.SENSOR_TYPE_TEMP:
        # Remove "Temperature" word
        name = name.replace("Temperature", "").replace("temperature", "")
        name = name.replace("Temp", "").replace("temp", "").strip()

        # Core numbering: "Core 0" → "C0", "Core #1" → "C1"
        if "core" in name.lower():
            match = re.search(r'core\s*#?(\d+)', name, re.IGNORECASE)
            if match:
                name = "C" + match.group(1)

        # Add device prefix
        if device_prefix and not name.upper().startswith(device_prefix.replace("_", "")):
            name = device_prefix + name

    # ==================== VOLTAGE SENSORS ====================
    elif sensor_type == SensorType.SENSOR_TYPE_VOLT:
        name = name.replace("Voltage", "").replace("voltage", "").strip()

        # Common voltage rail names
        if "vcore" in label_lower or "cpu core" in label_lower:
            name = "VCORE"
        elif "12v" in label_lower or "+12v" in label_lower:
            name = "12V"
        elif "5v" in label_lower or "+5v" in label_lower:
            name = "5V"
        elif "3.3v" in label_lower or "+3.3v" in label_lower or "3v3" in label_lower:
            name = "3.3V"
        elif "vsoc" in label_lower:
            name = "VSOC"
        elif "vddcr" in label_lower:
            name = "VDDCR"
        elif device_prefix:
            name = device_prefix + name

    # ==================== FAN SENSORS ====================
    elif sensor_type == SensorType.SENSOR_TYPE_FAN:
        # "Fan #1" → "FAN1", "CPU Fan" → "CPUFAN", "Pump" → "PUMP"
        name = name.replace("Fan #", "FAN").replace("fan #", "FAN")
        name = name.replace("Chassis", "CHS").replace("System", "SYS")
        name = name.replace("Pump", "PUMP").replace("pump", "PUMP")

        # If still has "Fan" word, keep it
        if "fan" in name.lower() and not name.upper().startswith("FAN"):
            name = name.replace("Fan", "FAN").replace("fan", "FAN")

    # ==================== CURRENT SENSORS ====================
    elif sensor_type == SensorType.SENSOR_TYPE_CURRENT:
        name = name.replace("Current", "").replace("current", "").strip()

        if device_prefix:
            name = device_prefix + "I"  # I for current (amperage)
        else:
            name = name + "_I"

    # ==================== LOAD/USAGE SENSORS ====================
    elif sensor_type == SensorType.SENSOR_TYPE_USAGE:
        name = name.replace("Load", "").replace("Usage", "").replace("Utilization", "").strip()

        # GPU-specific loads
        if "core" in label_lower and device_prefix == "GPU_":
            name = "GPU_CORE"
        elif "memory" in label_lower or "vram" in label_lower:
            if device_prefix == "GPU_":
                name = "GPU_MEM"
            else:
                name = "VRAM"
        elif "video engine" in label_lower:
            name = "GPU_VID"
        elif device_prefix:
            name = device_prefix + name.replace(" ", "")

    # ==================== POWER SENSORS ====================
    elif sensor_type == SensorType.SENSOR_TYPE_POWER:
        name = name.replace("Package", "PKG").replace("Power", "").strip()

        if "cpu" in label_lower:
            name = "CPU_PWR"
        elif "gpu" in label_lower:
            name = "GPU_PWR"
        elif device_prefix:
            name = device_prefix + "PWR"

    # ==================== CLOCK SENSORS ====================
    elif sensor_type == SensorType.SENSOR_TYPE_CLOCK:
        name = name.replace("Clock", "").replace("Frequency", "").strip()

        if device_prefix:
            name = device_prefix + "CLK"
        else:
            name = name + "CLK"

    # ==================== FPS DETECTION (SPECIAL CASE) ====================
    if "fps" in label_lower or unit.upper() == "FPS":
        if device_prefix == "GPU_":
            name = "GPU_FPS"
        else:
            name = "FPS"

    # ==================== CLEANUP ====================
    # Replace spaces with underscores
    name = name.replace("  ", " ").replace(" ", "_")

    # Remove special characters
    name = name.replace("/", "").replace("\\", "").replace("-", "")

    # Truncate if too long
    if len(name) > 10:
        # Try removing underscores first
        name = name.replace("_", "")
        if len(name) > 10:
            # Last resort: truncate
            name = name[:10]

    return name.upper() if name else "SENSOR"


# ==================== WMI HELPER FUNCTIONS (LibreHardwareMonitor) ====================

def generate_short_name_wmi(full_name, sensor_type, identifier=""):
    """
    Generate a short name (max 10 chars) for ESP32 display (WMI version)
    Similar to HWiNFO version but adapted for LibreHardwareMonitor identifiers
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

    name = full_name.strip()

    # Temperature sensors
    if sensor_type.lower() == "temperature":
        name = name.replace("Temperature", "").replace("temperature", "").strip()
        if device_prefix and not name.upper().startswith(device_prefix.replace("_", "")):
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

    # Fans
    elif sensor_type.lower() == "fan":
        name = name.replace("Fan #", "FAN").replace("fan #", "FAN")
        name = name.replace("Chassis", "CHS").replace("System", "SYS")

    # Loads
    elif sensor_type.lower() == "load":
        name = name.replace("Load", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

    # Power
    elif sensor_type.lower() == "power":
        name = name.replace("Package", "PKG").replace("Power", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

    # Data (network/disk usage in GB)
    elif sensor_type.lower() == "data":
        name = name.replace("Data", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

        # Network metrics: add Upload/Download suffix
        if device_prefix == "NET" and identifier:
            parts = identifier.split('/')
            name_lower = name.lower()
            if 'upload' not in name_lower and 'download' not in name_lower:
                if len(parts) >= 4:
                    data_index = parts[-1]
                    if data_index == '0':
                        name = name + "_D"  # Download
                    elif data_index == '1':
                        name = name + "_U"  # Upload

    # Throughput (network speeds in KB/s, MB/s)
    elif sensor_type.lower() == "throughput":
        name = name.replace("Speed", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name

        # Network throughput: add Upload/Download suffix
        if device_prefix == "NET" and identifier:
            parts = identifier.split('/')
            name_lower = name.lower()
            if 'upload' not in name_lower and 'download' not in name_lower:
                if len(parts) >= 4:
                    throughput_index = parts[-1]
                    if throughput_index == '0':
                        name = name + "_U"  # Upload
                    elif throughput_index == '1':
                        name = name + "_D"  # Download

    # Clean up
    name = name.replace("  ", " ").replace(" ", "_")

    # Truncate if too long
    if len(name) > 10:
        name = name.replace("_", "")
        if len(name) > 10:
            name = name[:10]

    return name if name else "SENSOR"


def get_unit_from_type_wmi(sensor_type):
    """Map LibreHardwareMonitor sensor type to display unit"""
    unit_map = {
        "Temperature": "C",
        "Load": "%",
        "Fan": "RPM",
        "Clock": "MHz",
        "Power": "W",
        "Voltage": "V",
        "Data": "GB",
        "Throughput": "KB/s"
    }
    return unit_map.get(sensor_type, "")


# ==================== SENSOR DISCOVERY ====================

def discover_sensors():
    """
    Discover all available sensors from HWiNFO64 and psutil

    Returns:
        bool: True if successful, False if HWiNFO unavailable
    """
    print("=" * 70)
    print("PC STATS MONITOR - HWiNFO64 EDITION - SENSOR DISCOVERY")
    print("=" * 70)

    # ==================== STEP 1: PSUTIL METRICS (UNCHANGED) ====================
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
        "display_name": "Disk C: Usage",
        "source": "psutil",
        "type": "percent",
        "unit": "%",
        "psutil_method": "disk_usage",
        "custom_label": "",
        "current_value": int(psutil.disk_usage('C:\\').percent)
    })

    print(f"  Found {len(sensor_database['system'])} system metrics")

    # ==================== STEP 2: HARDWARE SENSORS (AUTO-DETECT) ====================
    global sensor_source
    hwinfo_success = False

    # Try HWiNFO first (preferred - more sensors)
    print("\n[2/3] Discovering hardware sensors...")
    print("  [2a] Trying HWiNFO64...")

    try:
        hwinfo = HWiNFOReader()
        hwinfo.open()

        sensors = hwinfo.read_sensors()
        readings = hwinfo.read_readings()

        sensor_count = 0
        sensor_source = "hwinfo"
        print("  [OK] Using HWiNFO64 as sensor source")

        for reading in readings:
            # Skip invalid readings
            if reading.tReading == SensorType.SENSOR_TYPE_NONE:
                continue

            # Get parent sensor
            if reading.dwSensorIndex >= len(sensors):
                continue
            sensor = sensors[reading.dwSensorIndex]

            # Get sensor name (prefer user-customized)
            sensor_name = sensor.szSensorNameUser.decode('utf-8', errors='ignore').strip()
            if not sensor_name:
                sensor_name = sensor.szSensorNameOrig.decode('utf-8', errors='ignore').strip()

            # Get reading label (prefer user-customized)
            reading_label = reading.szLabelUser.decode('utf-8', errors='ignore').strip()
            if not reading_label:
                reading_label = reading.szLabelOrig.decode('utf-8', errors='ignore').strip()

            # Build display name for GUI
            display_name = f"{reading_label} [{sensor_name}]"

            # Get unit and map to ESP32 format
            unit = reading.szUnit.decode('utf-8', errors='ignore').strip()
            esp_unit = map_hwinfo_unit(unit)

            # Generate short name for ESP32 (max 10 chars)
            short_name = generate_short_name_hwinfo(
                reading_label,
                sensor_name,
                reading.tReading,
                unit
            )

            # Get current value
            current_value = int(reading.Value) if reading.Value else 0

            # Build sensor info dictionary
            sensor_info = {
                "name": short_name,
                "display_name": display_name,
                "source": "hwinfo",
                "type": get_sensor_type_name(reading.tReading),
                "unit": esp_unit,
                "hwinfo_reading_id": reading.dwReadingID,
                "hwinfo_sensor_id": sensor.dwSensorID,
                "custom_label": "",
                "current_value": current_value
            }

            # Categorize by sensor type
            if reading.tReading == SensorType.SENSOR_TYPE_TEMP:
                sensor_database["temperature"].append(sensor_info)
                sensor_count += 1
            elif reading.tReading == SensorType.SENSOR_TYPE_VOLT:
                sensor_database["voltage"].append(sensor_info)
                sensor_count += 1
            elif reading.tReading == SensorType.SENSOR_TYPE_FAN:
                sensor_database["fan"].append(sensor_info)
                sensor_count += 1
            elif reading.tReading == SensorType.SENSOR_TYPE_CURRENT:
                sensor_database["current"].append(sensor_info)
                sensor_count += 1
            elif reading.tReading == SensorType.SENSOR_TYPE_POWER:
                sensor_database["power"].append(sensor_info)
                sensor_count += 1
            elif reading.tReading == SensorType.SENSOR_TYPE_CLOCK:
                sensor_database["clock"].append(sensor_info)
                sensor_count += 1
            elif reading.tReading == SensorType.SENSOR_TYPE_USAGE:
                sensor_database["load"].append(sensor_info)
                sensor_count += 1
            else:
                sensor_database["other"].append(sensor_info)
                sensor_count += 1

        print(f"  Categorized {sensor_count} hardware sensors:")
        print(f"    - Temperatures:  {len(sensor_database['temperature'])}")
        print(f"    - Voltages:      {len(sensor_database['voltage'])}")
        print(f"    - Fans:          {len(sensor_database['fan'])}")
        print(f"    - Currents:      {len(sensor_database['current'])}")
        print(f"    - Loads:         {len(sensor_database['load'])}")
        print(f"    - Clocks:        {len(sensor_database['clock'])}")
        print(f"    - Power:         {len(sensor_database['power'])}")
        if len(sensor_database['other']) > 0:
            print(f"    - Other:         {len(sensor_database['other'])}")

        hwinfo.close()
        hwinfo_success = True

    except RuntimeError as e:
        print(f"  [INFO] HWiNFO64 not available: {e}")
    except Exception as e:
        print(f"  [INFO] HWiNFO64 error: {e}")

    # Fallback to LibreHardwareMonitor/WMI if HWiNFO failed
    if not hwinfo_success:
        print("  [2b] Falling back to LibreHardwareMonitor (WMI)...")
        try:
            import wmi
            w = wmi.WMI(namespace="root\\LibreHardwareMonitor")
            sensors_wmi = w.Sensor()

            sensor_count = 0
            sensor_source = "wmi"
            print("  [OK] Using LibreHardwareMonitor as sensor source")

            for sensor in sensors_wmi:
                # Generate short name for ESP32
                short_name = generate_short_name_wmi(sensor.Name, sensor.SensorType, sensor.Identifier)

                # Build display name with context
                display_name = sensor.Name
                identifier_parts = sensor.Identifier.split('/')
                if len(identifier_parts) > 1:
                    device_info = identifier_parts[1]
                    if device_info.lower() not in display_name.lower():
                        display_name = f"{sensor.Name} [{device_info}]"

                    # Network data/throughput disambiguation
                    if sensor.SensorType.lower() == "data" and ('nic' in device_info.lower() or 'network' in device_info.lower()):
                        if len(identifier_parts) >= 4:
                            data_index = identifier_parts[-1]
                            name_lower = sensor.Name.lower()
                            if 'upload' not in name_lower and 'download' not in name_lower:
                                if data_index == '0':
                                    display_name = f"{sensor.Name} - Download [{device_info}]"
                                elif data_index == '1':
                                    display_name = f"{sensor.Name} - Upload [{device_info}]"

                    elif sensor.SensorType.lower() == "throughput" and ('nic' in device_info.lower() or 'network' in device_info.lower()):
                        if len(identifier_parts) >= 4:
                            throughput_index = identifier_parts[-1]
                            name_lower = sensor.Name.lower()
                            if 'upload' not in name_lower and 'download' not in name_lower:
                                if throughput_index == '0':
                                    display_name = f"{sensor.Name} - Upload [{device_info}]"
                                elif throughput_index == '1':
                                    display_name = f"{sensor.Name} - Download [{device_info}]"

                # Get current value
                try:
                    current_value = int(sensor.Value) if sensor.Value else 0
                except:
                    current_value = 0

                sensor_info = {
                    "name": short_name,
                    "display_name": display_name,
                    "source": "wmi",
                    "type": sensor.SensorType.lower(),
                    "unit": get_unit_from_type_wmi(sensor.SensorType),
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
                elif sensor_type == "voltage":
                    sensor_database["voltage"].append(sensor_info)
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
            print(f"    - Temperatures:  {len(sensor_database['temperature'])}")
            print(f"    - Voltages:      {len(sensor_database['voltage'])}")
            print(f"    - Fans:          {len(sensor_database['fan'])}")
            print(f"    - Loads:         {len(sensor_database['load'])}")
            print(f"    - Clocks:        {len(sensor_database['clock'])}")
            print(f"    - Power:         {len(sensor_database['power'])}")
            if len(sensor_database['data']) > 0:
                print(f"    - Data:          {len(sensor_database['data'])}")
            if len(sensor_database['throughput']) > 0:
                print(f"    - Throughput:    {len(sensor_database['throughput'])}")
            if len(sensor_database['other']) > 0:
                print(f"    - Other:         {len(sensor_database['other'])}")

        except ImportError:
            print("  [ERROR] WMI not available. Install with: pip install pywin32 wmi")
            print("\n[ERROR] No hardware sensor source available!")
            print("  Please install either:")
            print("    1. HWiNFO64 with shared memory enabled, OR")
            print("    2. LibreHardwareMonitor + pywin32/wmi")
            return False
        except Exception as e:
            print(f"  [ERROR] LibreHardwareMonitor not accessible: {e}")
            print("  Make sure LibreHardwareMonitor is running!")
            return False

    print("\n" + "=" * 70)
    print("\n[INFO] NOTE: Sensor values shown in GUI are static (captured at launch)")
    print("  This helps you identify sensors and their typical ranges.")

    return True


# ==================== CONFIGURATION MANAGEMENT ====================

def load_config():
    """Load configuration from file"""
    if not os.path.exists(CONFIG_FILE):
        return None

    try:
        with open(CONFIG_FILE, 'r') as f:
            config = json.load(f)
        print(f"\n[OK] Loaded configuration from {CONFIG_FILE}")
        print(f"  Selected metrics: {len(config.get('metrics', []))}")
        return config
    except Exception as e:
        print(f"\n[X] Error loading config: {e}")
        return None


def save_config(config):
    """Save configuration to file"""
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(config, f, indent=2)
        print(f"\n[OK] Configuration saved to {CONFIG_FILE}")
        return True
    except Exception as e:
        print(f"\n[X] Error saving config: {e}")
        return False


# ==================== RUNTIME MONITORING ====================

# Global HWiNFO reader (opened once at startup, kept open during monitoring)
hwinfo_reader = None


def init_hwinfo_reader():
    """
    Initialize global HWiNFO reader for runtime value polling
    (Only called if sensor_source is "hwinfo")

    Returns:
        bool: True if successful
    """
    global hwinfo_reader
    try:
        hwinfo_reader = HWiNFOReader()
        hwinfo_reader.open()
        hwinfo_reader.read_sensors()
        hwinfo_reader.read_readings()
        return True
    except Exception as e:
        print(f"\nERROR: Failed to initialize HWiNFO reader: {e}")
        return False


def get_metric_value(metric_config):
    """
    Get current value for a configured metric

    Args:
        metric_config: Dictionary from monitor_config_hwinfo.json

    Returns:
        Integer value for ESP32 display
    """
    source = metric_config["source"]

    # ==================== PSUTIL METRICS ====================
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

    # ==================== HWINFO METRICS ====================
    elif source == "hwinfo":
        global hwinfo_reader
        try:
            # Re-read all readings to get fresh values
            hwinfo_reader.read_readings()

            # Find reading by ID
            reading_id = metric_config["hwinfo_reading_id"]
            for reading in hwinfo_reader.readings:
                if reading.dwReadingID == reading_id:
                    # Round to integer for ESP32
                    return int(reading.Value)

            # Not found (sensor disconnected?)
            return 0

        except Exception as e:
            print(f"  Warning: Failed to read HWiNFO metric {metric_config['name']}: {e}")
            return 0

    # ==================== WMI METRICS (LibreHardwareMonitor) ====================
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

    Args:
        sock: UDP socket
        config: Configuration dictionary

    Returns:
        bool: True if successful
    """
    # Build JSON payload (identical to v2.py)
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
    """Run monitoring loop in console mode"""
    print(f"\nMonitoring {len(config['metrics'])} metrics:")
    for m in config["metrics"]:
        label_info = f" (Label: {m['custom_label']})" if m.get('custom_label') else ""
        print(f"  {m['id']}. {m['display_name']} ({m['name']}){label_info} - {m['source']}")

    print(f"\nESP32 IP: {config['esp32_ip']}")
    print(f"UDP Port: {config['udp_port']}")
    print(f"Update Interval: {config['update_interval']}s")
    print("\nStarting monitoring... (Press Ctrl+C to stop)\n")

    # Initialize sensor reader based on source
    global sensor_source
    if sensor_source == "hwinfo":
        if not init_hwinfo_reader():
            print("\nFailed to start monitoring - HWiNFO not available")
            return
        print(f"Using sensor source: HWiNFO64\n")
    elif sensor_source == "wmi":
        print(f"Using sensor source: LibreHardwareMonitor (WMI)\n")
    else:
        print("\nERROR: No sensor source configured!")
        print("Please run with --configure first to discover sensors")
        return

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
        if hwinfo_reader:
            hwinfo_reader.close()


# ==================== GUI CLASS ====================

class MetricSelectorGUI:
    """
    Tkinter GUI for selecting metrics and configuring settings
    """
    def __init__(self, root, existing_config=None):
        self.root = root
        self.root.title("PC Monitor v2.0 (HWiNFO/LibreHW) - Configuration")
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
            text="PC Monitor Configuration (HWiNFO/LibreHW)",
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
            text="[i] Values are static from GUI launch",
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
            ("VOLTAGES", "voltage"),           # HWiNFO or WMI
            ("FANS & COOLING", "fan"),
            ("CURRENTS", "current"),            # HWiNFO only
            ("LOADS", "load"),
            ("CLOCKS", "clock"),
            ("POWER", "power"),
            ("NETWORK DATA", "data"),           # WMI only (GB)
            ("NETWORK SPEED", "throughput"),    # WMI only (KB/s)
            ("FPS & OTHER", "other")            # FPS counters, misc sensors
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
                return "[OK] Enabled"
            else:
                return "[X] Disabled"
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



# ==================== SYSTEM TRAY & AUTOSTART ====================

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

        print(f"\n[OK] Autostart enabled!")
        print(f"  Shortcut created: {shortcut_path}")
        return True
    else:
        # Remove shortcut
        if os.path.exists(shortcut_path):
            os.remove(shortcut_path)
            print(f"\n[OK] Autostart disabled!")
            print(f"  Shortcut removed: {shortcut_path}")
            return True
        else:
            print("\n[X] Autostart shortcut not found")
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

    # Initialize sensor reader based on source
    global sensor_source
    if sensor_source == "hwinfo":
        if not init_hwinfo_reader():
            print("\nFailed to start monitoring - HWiNFO not available")
            return
    # WMI doesn't need initialization (creates connection per query)

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



# ==================== MAIN ====================

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
            print(f"\n[X] Error setting up autostart: {e}")
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

        # Window is already destroyed by mainloop when closed normally
        try:
            root.destroy()
        except:
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
