"""
ESP32 Configurator - Sensor Data Model
Organizes sensors in a hierarchical structure for tree view display
Device → Category → Sensor
"""

from typing import Dict, List, Optional
from collections import defaultdict


class SensorModel:
    """
    Hierarchical sensor data model
    Organizes sensors by: Device → Category → Sensor
    """

    def __init__(self):
        self.sensor_database = {}
        self.sensor_source = None
        self.hierarchy = {}  # Device → Category → [Sensors]

    def load_sensors(self, sensor_database: Dict, sensor_source: Optional[str]):
        """
        Load sensors from discovery and build hierarchical structure

        Args:
            sensor_database: Dict from discover_sensors()
            sensor_source: "hwinfo", "wmi", or None
        """
        self.sensor_database = sensor_database
        self.sensor_source = sensor_source

        # Build hierarchy: Device → Category → Sensors
        self.hierarchy = self._build_hierarchy()

    def _build_hierarchy(self) -> Dict:
        """
        Build hierarchical structure from flat sensor database

        Returns:
            Dict: {device_name: {category: [sensors]}}
        """
        hierarchy = defaultdict(lambda: defaultdict(list))

        for category, sensors in self.sensor_database.items():
            for sensor in sensors:
                # Extract device name from display_name
                device_name = self._extract_device_name(sensor)

                # Add sensor to hierarchy
                hierarchy[device_name][category].append(sensor)

        return dict(hierarchy)

    def _extract_device_name(self, sensor: Dict) -> str:
        """
        Extract device name from sensor display_name

        Examples:
            "Core 0 Temperature [CPU [#0]: Intel i9]" → "CPU [#0]: Intel i9"
            "GPU Temperature [NVIDIA RTX 3080]" → "NVIDIA RTX 3080"
            "CPU Usage" → "System"
            "Fan #1 [Motherboard]" → "Motherboard"
        """
        display_name = sensor.get("display_name", "")
        source = sensor.get("source", "")

        # psutil metrics → "System"
        if source == "psutil":
            return "System"

        # Extract device from brackets: "Label [Device]"
        if "[" in display_name and "]" in display_name:
            # Find last bracketed section (most specific device)
            parts = display_name.split("[")
            if len(parts) > 1:
                device = parts[-1].rstrip("]")
                return device.strip()

        # Fallback: use source
        return sensor_source.upper() if sensor_source else "Unknown"

    def get_devices(self) -> List[str]:
        """Get list of all device names"""
        return sorted(self.hierarchy.keys())

    def get_categories_for_device(self, device: str) -> List[str]:
        """Get list of categories for a specific device"""
        if device in self.hierarchy:
            return sorted(self.hierarchy[device].keys())
        return []

    def get_sensors_for_device_category(self, device: str, category: str) -> List[Dict]:
        """Get list of sensors for a device/category combination"""
        if device in self.hierarchy and category in self.hierarchy[device]:
            # Sort by display name
            return sorted(
                self.hierarchy[device][category],
                key=lambda s: s.get("display_name", "")
            )
        return []

    def get_all_sensors_flat(self) -> List[Dict]:
        """Get all sensors as a flat list"""
        all_sensors = []
        for category, sensors in self.sensor_database.items():
            all_sensors.extend(sensors)
        return all_sensors

    def get_sensor_count(self) -> int:
        """Get total sensor count"""
        return len(self.get_all_sensors_flat())

    def search_sensors(self, query: str) -> List[Dict]:
        """
        Search sensors by query string

        Args:
            query: Search string (case-insensitive)

        Returns:
            List of matching sensors
        """
        if not query:
            return self.get_all_sensors_flat()

        query_lower = query.lower()
        results = []

        for sensor in self.get_all_sensors_flat():
            # Search in display_name, name, and unit
            if (query_lower in sensor.get("display_name", "").lower() or
                query_lower in sensor.get("name", "").lower() or
                query_lower in sensor.get("unit", "").lower()):
                results.append(sensor)

        return results

    def get_category_display_name(self, category: str) -> str:
        """Get user-friendly category name"""
        category_names = {
            "system": "System Metrics",
            "temperature": "Temperatures",
            "voltage": "Voltages",
            "fan": "Fans & Cooling",
            "current": "Currents",
            "load": "Loads",
            "clock": "Clocks",
            "power": "Power",
            "data": "Network Data (GB)",
            "throughput": "Network Speed (KB/s)",
            "other": "FPS & Other"
        }
        return category_names.get(category, category.title())

    def get_sensor_by_key(self, source: str, name: str) -> Optional[Dict]:
        """
        Get a specific sensor by source and name

        Args:
            source: "hwinfo", "wmi", or "psutil"
            name: Sensor short name

        Returns:
            Sensor dict or None
        """
        for sensor in self.get_all_sensors_flat():
            if sensor.get("source") == source and sensor.get("name") == name:
                return sensor
        return None

    def get_summary(self) -> str:
        """Get a summary string of sensor counts"""
        total = self.get_sensor_count()
        devices = len(self.get_devices())
        source_name = self.sensor_source.upper() if self.sensor_source else "None"

        return (
            f"Source: {source_name} | "
            f"Total Sensors: {total} | "
            f"Devices: {devices}"
        )
