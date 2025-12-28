"""
ESP32 Configurator - Sensor Poller
Background thread for real-time sensor value updates
"""

from PyQt6.QtCore import QThread, pyqtSignal
import time
import psutil

try:
    import wmi
    HAS_WMI = True
except ImportError:
    HAS_WMI = False

try:
    import win32event
    import win32api
    import mmap
    import ctypes
    HAS_HWINFO = True
except ImportError:
    HAS_HWINFO = False


class SensorPoller(QThread):
    """
    Background thread that polls sensor values

    Runs every 1 second and emits updated values
    """

    # Signal: sensor updated (source, identifier, value)
    sensorUpdated = pyqtSignal(str, str, float)

    def __init__(self, sensor_source: str, sensor_database: dict):
        super().__init__()

        self.sensor_source = sensor_source
        self.sensor_database = sensor_database
        self.running = False
        self.poll_interval = 1.0  # 1 second

        # HWiNFO reader (if using hwinfo)
        self.hwinfo_reader = None
        if sensor_source == "hwinfo" and HAS_HWINFO:
            from core.sensor_discovery import HWiNFOReader
            self.hwinfo_reader = HWiNFOReader()

        # WMI connection (if using wmi)
        self.wmi_connection = None
        if sensor_source == "wmi" and HAS_WMI:
            try:
                self.wmi_connection = wmi.WMI(namespace="root\\LibreHardwareMonitor")
            except Exception:
                pass

    def run(self):
        """Main thread loop"""
        self.running = True

        while self.running:
            try:
                # Update psutil metrics
                self._update_psutil_sensors()

                # Update hardware sensors
                if self.sensor_source == "hwinfo":
                    self._update_hwinfo_sensors()
                elif self.sensor_source == "wmi":
                    self._update_wmi_sensors()

            except Exception as e:
                print(f"[ERROR] Sensor polling error: {e}")

            # Sleep for poll interval
            time.sleep(self.poll_interval)

    def stop(self):
        """Stop the polling thread"""
        self.running = False
        self.wait()

    def _update_psutil_sensors(self):
        """Update psutil system metrics"""
        try:
            # CPU usage
            cpu_percent = psutil.cpu_percent(interval=0.1)
            self.sensorUpdated.emit("psutil", "CPU_USAGE", cpu_percent)

            # RAM usage
            ram = psutil.virtual_memory()
            ram_percent = ram.percent
            self.sensorUpdated.emit("psutil", "RAM_USAGE", ram_percent)

            # Disk usage
            disk = psutil.disk_usage('/')
            disk_percent = disk.percent
            self.sensorUpdated.emit("psutil", "DISK_USAGE", disk_percent)

            # Network throughput (placeholder - would need tracking)
            # For now, just emit 0
            self.sensorUpdated.emit("psutil", "NET_THROUGHPUT", 0.0)

        except Exception as e:
            print(f"[ERROR] psutil update failed: {e}")

    def _update_hwinfo_sensors(self):
        """Update HWiNFO sensor values"""
        if not self.hwinfo_reader:
            return

        try:
            # Read all sensors
            sensors = self.hwinfo_reader.read_sensors()

            if not sensors:
                return

            # Emit updates for each sensor
            for sensor in sensors:
                reading_id = sensor.get("hwinfo_reading_id")
                value = sensor.get("value", 0.0)

                if reading_id is not None:
                    # Emit with reading_id as identifier
                    self.sensorUpdated.emit("hwinfo", str(reading_id), value)

        except Exception as e:
            print(f"[ERROR] HWiNFO update failed: {e}")

    def _update_wmi_sensors(self):
        """Update WMI sensor values"""
        if not self.wmi_connection:
            return

        try:
            # Query all sensors
            sensors = self.wmi_connection.Sensor()

            for sensor in sensors:
                identifier = sensor.Identifier
                value = float(sensor.Value) if sensor.Value is not None else 0.0

                self.sensorUpdated.emit("wmi", identifier, value)

        except Exception as e:
            print(f"[ERROR] WMI update failed: {e}")

    def set_poll_interval(self, interval: float):
        """
        Set poll interval in seconds

        Args:
            interval: Seconds between polls (0.5-10.0)
        """
        self.poll_interval = max(0.5, min(10.0, interval))
