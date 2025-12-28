"""
ESP32 Configurator - Metrics Data Model
Manages the list of selected metrics (max 20)
"""

from typing import List, Dict, Optional
from PyQt6.QtCore import QObject, pyqtSignal


class MetricsModel(QObject):
    """
    Model for managing selected metrics

    Features:
    - Maximum 20 metrics
    - Reordering support
    - Add/remove metrics
    - Validation

    Signals:
    - metricsChanged: Emitted when metrics list changes
    - metricAdded: Emitted when a metric is added (metric_dict)
    - metricRemoved: Emitted when a metric is removed (index)
    - metricMoved: Emitted when a metric is reordered (from_index, to_index)
    """

    # Signals
    metricsChanged = pyqtSignal()
    metricAdded = pyqtSignal(dict)
    metricRemoved = pyqtSignal(int)
    metricMoved = pyqtSignal(int, int)

    MAX_METRICS = 20

    def __init__(self):
        super().__init__()
        self.metrics = []  # List of metric dicts

    def add_metric(self, sensor: Dict) -> tuple[bool, str]:
        """
        Add a metric from a sensor

        Args:
            sensor: Sensor dict from sensor model

        Returns:
            (success, error_message)
        """
        # Check limit
        if len(self.metrics) >= self.MAX_METRICS:
            return False, f"Maximum {self.MAX_METRICS} metrics reached"

        # Check for duplicates using source-specific unique identifiers
        source = sensor.get("source")
        for existing in self.metrics:
            if existing.get("source") == source:
                # For hwinfo: check reading_id (unique)
                if source == "hwinfo":
                    if (sensor.get("hwinfo_reading_id") == existing.get("hwinfo_reading_id") and
                        sensor.get("hwinfo_reading_id") is not None):
                        return False, "Metric already added"
                # For wmi: check identifier (unique)
                elif source == "wmi":
                    if (sensor.get("wmi_identifier") == existing.get("wmi_identifier") and
                        sensor.get("wmi_identifier")):
                        return False, "Metric already added"
                # For psutil: check name (already unique)
                elif source == "psutil":
                    if sensor.get("name") == existing.get("name"):
                        return False, "Metric already added"

        # Create metric dict (same format as monitor_config_hwinfo.json)
        metric = {
            "id": len(self.metrics) + 1,  # 1-based ID
            "name": sensor.get("name", ""),
            "display_name": sensor.get("display_name", ""),
            "source": sensor.get("source", ""),
            "type": sensor.get("type", ""),
            "unit": sensor.get("unit", ""),
            "custom_label": "",
            "current_value": sensor.get("current_value", 0),
            # Position-based display settings
            "position": 255,  # 0-11 for grid positions, 255 = not displayed
            "bar_position": 255,  # 0-11 for progress bar position, 255 = no bar
            "bar_min": 0,  # Minimum value for progress bar
            "bar_max": 100,  # Maximum value for progress bar (default for percentages)
            "companion_id": 0,  # ID of companion metric (0 = none, 1-20 = metric ID)
            "bar_width": 60,  # Progress bar width in pixels (default 60)
            "bar_offset": 0  # Progress bar X offset in pixels (default 0)
        }

        # Add source-specific fields
        if sensor.get("source") == "hwinfo":
            metric["hwinfo_reading_id"] = sensor.get("hwinfo_reading_id", 0)
        elif sensor.get("source") == "wmi":
            metric["wmi_identifier"] = sensor.get("wmi_identifier", "")

        # Add to list
        self.metrics.append(metric)

        # Emit signals
        self.metricAdded.emit(metric)
        self.metricsChanged.emit()

        return True, ""

    def remove_metric(self, index: int) -> bool:
        """
        Remove a metric by index

        Args:
            index: Index in metrics list

        Returns:
            True if removed, False if index invalid
        """
        if 0 <= index < len(self.metrics):
            self.metrics.pop(index)

            # Re-number IDs
            self._renumber_ids()

            # Emit signals
            self.metricRemoved.emit(index)
            self.metricsChanged.emit()

            return True
        return False

    def move_metric(self, from_index: int, to_index: int) -> bool:
        """
        Move a metric from one position to another

        Args:
            from_index: Source index
            to_index: Destination index

        Returns:
            True if moved, False if indices invalid
        """
        if (0 <= from_index < len(self.metrics) and
            0 <= to_index < len(self.metrics)):

            # Move item
            metric = self.metrics.pop(from_index)
            self.metrics.insert(to_index, metric)

            # Re-number IDs
            self._renumber_ids()

            # Emit signals
            self.metricMoved.emit(from_index, to_index)
            self.metricsChanged.emit()

            return True
        return False

    def clear_all(self):
        """Clear all metrics"""
        self.metrics = []
        self.metricsChanged.emit()

    def get_metrics(self) -> List[Dict]:
        """Get all metrics"""
        return self.metrics.copy()

    def get_metric(self, index: int) -> Optional[Dict]:
        """Get metric by index"""
        if 0 <= index < len(self.metrics):
            return self.metrics[index].copy()
        return None

    def get_count(self) -> int:
        """Get number of selected metrics"""
        return len(self.metrics)

    def is_full(self) -> bool:
        """Check if metrics list is full"""
        return len(self.metrics) >= self.MAX_METRICS

    def get_remaining(self) -> int:
        """Get number of remaining slots"""
        return self.MAX_METRICS - len(self.metrics)

    def update_value(self, index: int, value: float):
        """
        Update the current value of a metric

        Args:
            index: Metric index
            value: New value
        """
        if 0 <= index < len(self.metrics):
            self.metrics[index]["current_value"] = value
            self.metricsChanged.emit()

    def update_custom_label(self, index: int, label: str):
        """
        Update the custom label of a metric

        Args:
            index: Metric index
            label: Custom label (max 10 chars for OLED)
        """
        if 0 <= index < len(self.metrics):
            # Truncate to 10 chars (OLED display limit)
            self.metrics[index]["custom_label"] = label[:10]
            self.metricsChanged.emit()

    def update_position(self, index: int, position: int):
        """
        Update the display position of a metric

        Args:
            index: Metric index
            position: Position 0-11 or 255 (hidden)
        """
        if 0 <= index < len(self.metrics):
            self.metrics[index]["position"] = position
            self.metricsChanged.emit()

    def update_bar_position(self, index: int, bar_position: int):
        """
        Update the progress bar position of a metric

        Args:
            index: Metric index
            bar_position: Position 0-11 or 255 (no bar)
        """
        if 0 <= index < len(self.metrics):
            self.metrics[index]["bar_position"] = bar_position
            self.metricsChanged.emit()

    def update_bar_range(self, index: int, bar_min: int, bar_max: int):
        """
        Update the progress bar range of a metric

        Args:
            index: Metric index
            bar_min: Minimum value
            bar_max: Maximum value
        """
        if 0 <= index < len(self.metrics):
            self.metrics[index]["bar_min"] = bar_min
            self.metrics[index]["bar_max"] = bar_max
            self.metricsChanged.emit()

    def update_companion(self, index: int, companion_id: int):
        """
        Update the companion metric ID

        Args:
            index: Metric index
            companion_id: Companion metric ID (0 = none, 1-20 = metric ID)
        """
        if 0 <= index < len(self.metrics):
            self.metrics[index]["companion_id"] = companion_id
            self.metricsChanged.emit()

    def update_bar_size(self, index: int, bar_width: int, bar_offset: int):
        """
        Update the progress bar size and offset

        Args:
            index: Metric index
            bar_width: Bar width in pixels
            bar_offset: Bar X offset in pixels
        """
        if 0 <= index < len(self.metrics):
            self.metrics[index]["bar_width"] = bar_width
            self.metrics[index]["bar_offset"] = bar_offset
            self.metricsChanged.emit()

    def _renumber_ids(self):
        """Renumber all metric IDs sequentially (1-based)"""
        for i, metric in enumerate(self.metrics):
            metric["id"] = i + 1

    def to_config_dict(self) -> Dict:
        """
        Export metrics to config dict format (monitor_config_hwinfo.json)

        Returns:
            Dict with 'metrics' key
        """
        return {
            "metrics": self.metrics.copy()
        }

    def from_config_dict(self, config: Dict):
        """
        Import metrics from config dict

        Args:
            config: Dict with 'metrics' key
        """
        self.metrics = config.get("metrics", [])[:self.MAX_METRICS]
        self._renumber_ids()
        self.metricsChanged.emit()
