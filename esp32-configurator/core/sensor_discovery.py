"""
ESP32 Configurator - Sensor Discovery Module
Discovers sensors from HWiNFO64 or LibreHardwareMonitor (WMI)

Adapted from pc_stats_monitor_hwinfo.py with minimal modifications
"""

import psutil
import ctypes
from ctypes import wintypes
import win32event
import win32api
import re


# ==================== CONSTANTS ====================

# HWiNFO shared memory constants
HWINFO_SENSORS_SM2 = "Global\\HWiNFO_SENS_SM2"
HWINFO_SENSORS_MUTEX = "Global\\HWiNFO_SM2_MUTEX"
FILE_MAP_READ = 0x0004
INVALID_HANDLE_VALUE = -1


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

            # Validate signature
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
                "6. Re-run this application"
            )
        except Exception as e:
            raise RuntimeError(f"Failed to open HWiNFO shared memory: {e}")

    def _parse_header(self):
        """Parse shared memory header (with mutex lock)"""
        win32event.WaitForSingleObject(self.mutex, win32event.INFINITE)
        try:
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
                sensor_addr = self.shmem_view + offset + i * element_size
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
                reading_addr = self.shmem_view + offset + i * element_size
                reading_bytes = ctypes.string_at(reading_addr, ctypes.sizeof(HWiNFO_ELEMENT))
                reading = HWiNFO_ELEMENT.from_buffer_copy(reading_bytes)
                readings.append(reading)

            self.readings = readings
            return readings
        finally:
            win32event.ReleaseMutex(self.mutex)

    def get_reading_value(self, reading_id):
        """Get current value for a specific reading ID"""
        self.read_readings()
        for reading in self.readings:
            if reading.dwReadingID == reading_id:
                return reading.Value
        return 0

    def close(self):
        """Clean up resources"""
        if self.shmem_view:
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
    """Map HWiNFO unit strings to ESP32 display format (max 4 chars)"""
    unit_map = {
        "°C": "C", "C": "C", "°F": "F", "V": "V", "mV": "mV",
        "A": "A", "mA": "mA", "W": "W", "MHz": "MHz", "RPM": "RPM",
        "%": "%", "KB/s": "KB/s", "MB/s": "MB/s", "GB": "GB",
        "TB": "TB", "FPS": "FPS", "ms": "ms", "°": "C",
    }
    return unit_map.get(hwinfo_unit, hwinfo_unit.strip()[:4])


def generate_short_name_hwinfo(reading_label, sensor_name, sensor_type, unit):
    """
    Generate a short name (max 10 chars) for ESP32 display

    Args:
        reading_label: e.g., "Core 0 Temperature"
        sensor_name: e.g., "CPU [#0]: Intel Core i9"
        sensor_type: SensorType enum value
        unit: Unit string

    Returns:
        String max 10 characters
    """
    # Detect device type
    device_prefix = ""
    sensor_lower = sensor_name.lower()
    label_lower = reading_label.lower()

    if 'cpu' in sensor_lower or 'processor' in sensor_lower or 'intel' in sensor_lower or 'amd ryzen' in sensor_lower:
        device_prefix = "CPU_"
    elif 'gpu' in sensor_lower or 'nvidia' in sensor_lower or 'geforce' in sensor_lower or 'radeon' in sensor_lower:
        device_prefix = "GPU_"
    elif 'motherboard' in sensor_lower or 'mainboard' in sensor_lower or 'chipset' in sensor_lower:
        device_prefix = "MB_"
    elif 'nvme' in sensor_lower:
        device_prefix = "NVM"
    elif 'ssd' in sensor_lower:
        device_prefix = "SSD"
    elif 'hdd' in sensor_lower or 'hard disk' in sensor_lower:
        device_prefix = "HDD"
    elif 'network' in sensor_lower or 'ethernet' in sensor_lower or 'nic' in sensor_lower:
        device_prefix = "NET"

    name = reading_label.strip()

    # Temperature sensors
    if sensor_type == SensorType.SENSOR_TYPE_TEMP:
        name = name.replace("Temperature", "").replace("temperature", "")
        name = name.replace("Temp", "").replace("temp", "").strip()
        if "core" in name.lower():
            match = re.search(r'core\s*#?(\d+)', name, re.IGNORECASE)
            if match:
                name = "C" + match.group(1)
        if device_prefix and not name.upper().startswith(device_prefix.replace("_", "")):
            name = device_prefix + name

    # Voltage sensors
    elif sensor_type == SensorType.SENSOR_TYPE_VOLT:
        name = name.replace("Voltage", "").replace("voltage", "").strip()
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

    # Fan sensors
    elif sensor_type == SensorType.SENSOR_TYPE_FAN:
        name = name.replace("Fan #", "FAN").replace("fan #", "FAN")
        name = name.replace("Chassis", "CHS").replace("System", "SYS")
        name = name.replace("Pump", "PUMP").replace("pump", "PUMP")
        if "fan" in name.lower() and not name.upper().startswith("FAN"):
            name = name.replace("Fan", "FAN").replace("fan", "FAN")

    # Current sensors
    elif sensor_type == SensorType.SENSOR_TYPE_CURRENT:
        name = name.replace("Current", "").replace("current", "").strip()
        name = (device_prefix + "I") if device_prefix else (name + "_I")

    # Load/Usage sensors
    elif sensor_type == SensorType.SENSOR_TYPE_USAGE:
        name = name.replace("Load", "").replace("Usage", "").replace("Utilization", "").strip()
        if "core" in label_lower and device_prefix == "GPU_":
            name = "GPU_CORE"
        elif "memory" in label_lower or "vram" in label_lower:
            name = "GPU_MEM" if device_prefix == "GPU_" else "VRAM"
        elif "video engine" in label_lower:
            name = "GPU_VID"
        elif device_prefix:
            name = device_prefix + name.replace(" ", "")

    # Power sensors
    elif sensor_type == SensorType.SENSOR_TYPE_POWER:
        name = name.replace("Package", "PKG").replace("Power", "").strip()
        if "cpu" in label_lower:
            name = "CPU_PWR"
        elif "gpu" in label_lower:
            name = "GPU_PWR"
        elif device_prefix:
            name = device_prefix + "PWR"

    # Clock sensors
    elif sensor_type == SensorType.SENSOR_TYPE_CLOCK:
        name = name.replace("Clock", "").replace("Frequency", "").strip()
        name = (device_prefix + "CLK") if device_prefix else (name + "CLK")

    # FPS detection
    if "fps" in label_lower or unit.upper() == "FPS":
        name = "GPU_FPS" if device_prefix == "GPU_" else "FPS"

    # Cleanup
    name = name.replace("  ", " ").replace(" ", "_")
    name = name.replace("/", "").replace("\\", "").replace("-", "")

    # Truncate if too long
    if len(name) > 10:
        name = name.replace("_", "")
        if len(name) > 10:
            name = name[:10]

    return name.upper() if name else "SENSOR"


def generate_short_name_wmi(full_name, sensor_type, identifier=""):
    """Generate short name for WMI sensors"""
    device_prefix = ""
    device_index = ""

    if identifier:
        parts = identifier.split('/')
        if len(parts) > 1:
            device = parts[1].lower()
            if 'cpu' in device:
                device_prefix = "CPU_"
            elif 'gpu' in device or 'nvidia' in device or 'amd' in device:
                device_prefix = "GPU_"
            elif 'motherboard' in device or 'mainboard' in device:
                device_prefix = "MB_"
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
            elif 'nic' in device or 'network' in device or 'ethernet' in device:
                device_prefix = "NET"
                if len(parts) > 2 and parts[2].isdigit():
                    device_index = parts[2]

    name = full_name.strip()

    # Temperature
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

    # Data (network/disk GB)
    elif sensor_type.lower() == "data":
        name = name.replace("Data", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name
        if device_prefix == "NET" and identifier:
            parts = identifier.split('/')
            name_lower = name.lower()
            if 'upload' not in name_lower and 'download' not in name_lower:
                if len(parts) >= 4:
                    data_index = parts[-1]
                    name = name + ("_D" if data_index == '0' else "_U")

    # Throughput (network KB/s, MB/s)
    elif sensor_type.lower() == "throughput":
        name = name.replace("Speed", "").strip()
        if device_prefix:
            name = device_prefix + device_index + "_" + name if device_index else device_prefix + name
        if device_prefix == "NET" and identifier:
            parts = identifier.split('/')
            name_lower = name.lower()
            if 'upload' not in name_lower and 'download' not in name_lower:
                if len(parts) >= 4:
                    throughput_index = parts[-1]
                    name = name + ("_U" if throughput_index == '0' else "_D")

    # Cleanup
    name = name.replace("  ", " ").replace(" ", "_")
    if len(name) > 10:
        name = name.replace("_", "")
        if len(name) > 10:
            name = name[:10]

    return name if name else "SENSOR"


def get_unit_from_type_wmi(sensor_type):
    """Map LibreHardwareMonitor sensor type to display unit"""
    unit_map = {
        "Temperature": "C", "Load": "%", "Fan": "RPM", "Clock": "MHz",
        "Power": "W", "Voltage": "V", "Data": "GB", "Throughput": "KB/s"
    }
    return unit_map.get(sensor_type, "")


# ==================== SENSOR DISCOVERY ====================

def discover_sensors():
    """
    Discover all available sensors from HWiNFO64, LibreHardwareMonitor, or psutil

    Returns:
        tuple: (sensor_database dict, sensor_source string, success bool)
            sensor_database: Dict of categorized sensors
            sensor_source: "hwinfo", "wmi", or None
            success: True if sensors were discovered
    """
    sensor_database = {
        "system": [],
        "temperature": [],
        "voltage": [],
        "fan": [],
        "current": [],
        "load": [],
        "clock": [],
        "power": [],
        "data": [],
        "throughput": [],
        "other": []
    }

    print("=" * 70)
    print("ESP32 CONFIGURATOR - SENSOR DISCOVERY")
    print("=" * 70)

    # Step 1: psutil metrics
    print("\n[1/3] Discovering system metrics (psutil)...")
    psutil.cpu_percent(interval=0.1)

    sensor_database["system"].extend([
        {
            "name": "CPU",
            "display_name": "CPU Usage",
            "source": "psutil",
            "type": "percent",
            "unit": "%",
            "psutil_method": "cpu_percent",
            "custom_label": "",
            "current_value": int(psutil.cpu_percent(interval=0))
        },
        {
            "name": "RAM",
            "display_name": "RAM Usage",
            "source": "psutil",
            "type": "percent",
            "unit": "%",
            "psutil_method": "virtual_memory.percent",
            "custom_label": "",
            "current_value": int(psutil.virtual_memory().percent)
        },
        {
            "name": "RAM_GB",
            "display_name": "RAM Used (GB)",
            "source": "psutil",
            "type": "memory",
            "unit": "GB",
            "psutil_method": "virtual_memory.used",
            "custom_label": "",
            "current_value": int(psutil.virtual_memory().used / (1024**3))
        },
        {
            "name": "DISK",
            "display_name": "Disk C: Usage",
            "source": "psutil",
            "type": "percent",
            "unit": "%",
            "psutil_method": "disk_usage",
            "custom_label": "",
            "current_value": int(psutil.disk_usage('C:\\').percent)
        }
    ])
    print(f"  Found {len(sensor_database['system'])} system metrics")

    # Step 2: Hardware sensors (HWiNFO or WMI)
    sensor_source = None
    hwinfo_success = False

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
            if reading.tReading == SensorType.SENSOR_TYPE_NONE:
                continue

            if reading.dwSensorIndex >= len(sensors):
                continue

            sensor = sensors[reading.dwSensorIndex]

            # Get names
            sensor_name = sensor.szSensorNameUser.decode('utf-8', errors='ignore').strip()
            if not sensor_name:
                sensor_name = sensor.szSensorNameOrig.decode('utf-8', errors='ignore').strip()

            reading_label = reading.szLabelUser.decode('utf-8', errors='ignore').strip()
            if not reading_label:
                reading_label = reading.szLabelOrig.decode('utf-8', errors='ignore').strip()

            display_name = f"{reading_label} [{sensor_name}]"

            unit = reading.szUnit.decode('utf-8', errors='ignore').strip()
            esp_unit = map_hwinfo_unit(unit)

            short_name = generate_short_name_hwinfo(
                reading_label,
                sensor_name,
                reading.tReading,
                unit
            )

            current_value = int(reading.Value) if reading.Value else 0

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

            # Categorize
            category = get_sensor_type_name(reading.tReading)
            sensor_database[category].append(sensor_info)
            sensor_count += 1

        print(f"  Categorized {sensor_count} hardware sensors:")
        for cat in ["temperature", "voltage", "fan", "current", "load", "clock", "power", "other"]:
            if len(sensor_database[cat]) > 0:
                print(f"    - {cat.title()}: {len(sensor_database[cat])}")

        hwinfo.close()
        hwinfo_success = True

    except RuntimeError as e:
        print(f"  [INFO] HWiNFO64 not available: {e}")
    except Exception as e:
        print(f"  [INFO] HWiNFO64 error: {e}")

    # Fallback to WMI
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
                short_name = generate_short_name_wmi(sensor.Name, sensor.SensorType, sensor.Identifier)

                display_name = sensor.Name
                identifier_parts = sensor.Identifier.split('/')
                if len(identifier_parts) > 1:
                    device_info = identifier_parts[1]
                    if device_info.lower() not in display_name.lower():
                        display_name = f"{sensor.Name} [{device_info}]"

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

                category = sensor.SensorType.lower()
                if category in sensor_database:
                    sensor_database[category].append(sensor_info)
                    sensor_count += 1

            print(f"  Found {sensor_count} hardware sensors:")
            for cat in ["temperature", "voltage", "fan", "load", "clock", "power", "data", "throughput", "other"]:
                if len(sensor_database[cat]) > 0:
                    print(f"    - {cat.title()}: {len(sensor_database[cat])}")

        except ImportError:
            print("  [ERROR] WMI not available. Install with: pip install wmi")
            print("\n[ERROR] No hardware sensor source available!")
            return sensor_database, None, False
        except Exception as e:
            print(f"  [ERROR] LibreHardwareMonitor not accessible: {e}")
            print("  Make sure LibreHardwareMonitor is running!")
            return sensor_database, None, False

    print("\n" + "=" * 70)
    print(f"[OK] Discovery complete - {sensor_source.upper() if sensor_source else 'NO SOURCE'}")
    print("=" * 70)

    return sensor_database, sensor_source, True
