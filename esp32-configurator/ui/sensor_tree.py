"""
ESP32 Configurator - Sensor Tree Widget
Displays hierarchical sensor tree with search functionality
Device → Category → Sensor
"""

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QLineEdit, QTreeWidget,
    QTreeWidgetItem, QLabel, QHBoxLayout, QGroupBox
)
from PyQt6.QtCore import Qt, pyqtSignal, QMimeData
from PyQt6.QtGui import QIcon, QDrag
import json

from models.sensor_model import SensorModel


class DraggableTreeWidget(QTreeWidget):
    """Custom QTreeWidget with sensor drag support"""

    def startDrag(self, supportedActions):
        """Override startDrag to create custom mime data"""
        item = self.currentItem()
        if not item:
            return

        # Only allow dragging sensor items (not devices or categories)
        sensor = item.data(0, Qt.ItemDataRole.UserRole)
        if not sensor:
            return

        # Create mime data with sensor JSON
        mime_data = QMimeData()
        sensor_json = json.dumps(sensor)
        mime_data.setData("application/x-sensor", sensor_json.encode())

        # Create drag object
        drag = QDrag(self)
        drag.setMimeData(mime_data)

        # Execute drag
        drag.exec(Qt.DropAction.CopyAction)


class SensorTreeWidget(QWidget):
    """
    Widget displaying available sensors in a hierarchical tree

    Hierarchy: Device → Category → Sensor
    Features: Search/filter, live values, drag-enabled
    """

    # Signal emitted when a sensor is double-clicked
    sensorDoubleClicked = pyqtSignal(dict)

    def __init__(self, parent=None):
        super().__init__(parent)

        self.sensor_model = SensorModel()
        self._init_ui()

    def _init_ui(self):
        """Initialize the user interface"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        # Group box
        group = QGroupBox("Available Sensors")
        group_layout = QVBoxLayout()

        # Search bar
        search_layout = QHBoxLayout()
        search_label = QLabel("Search:")
        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("Type to filter sensors...")
        self.search_edit.textChanged.connect(self._on_search)
        search_layout.addWidget(search_label)
        search_layout.addWidget(self.search_edit, stretch=1)
        group_layout.addLayout(search_layout)

        # Sensor count label
        self.count_label = QLabel("No sensors discovered yet")
        self.count_label.setStyleSheet("color: #888888; font-style: italic;")
        group_layout.addWidget(self.count_label)

        # Tree widget (with drag support)
        self.tree = DraggableTreeWidget()
        self.tree.setHeaderLabels(["Sensor", "Value", "Unit"])
        self.tree.setColumnWidth(0, 300)
        self.tree.setColumnWidth(1, 80)
        self.tree.setColumnWidth(2, 60)
        self.tree.setAlternatingRowColors(True)
        self.tree.itemDoubleClicked.connect(self._on_item_double_clicked)
        self.tree.setToolTip("Drag sensors to the metrics list or double-click to add")

        # Enable dragging
        self.tree.setDragEnabled(True)
        self.tree.setDragDropMode(QTreeWidget.DragDropMode.DragOnly)

        group_layout.addWidget(self.tree)

        group.setLayout(group_layout)
        layout.addWidget(group)

    def load_sensors(self, sensor_database: dict, sensor_source: str):
        """
        Load sensors from discovery results

        Args:
            sensor_database: Dict from discover_sensors()
            sensor_source: "hwinfo", "wmi", or None
        """
        # Load into model
        self.sensor_model.load_sensors(sensor_database, sensor_source)

        # Update count label
        summary = self.sensor_model.get_summary()
        self.count_label.setText(summary)

        # Populate tree
        self._populate_tree()

    def _populate_tree(self):
        """Populate tree widget with hierarchical sensor data"""
        self.tree.clear()

        # Get devices
        devices = self.sensor_model.get_devices()

        for device in devices:
            # Create device node
            device_item = QTreeWidgetItem(self.tree)
            device_item.setText(0, device)
            device_item.setExpanded(False)  # Collapsed by default

            # Bold font for devices
            font = device_item.font(0)
            font.setBold(True)
            device_item.setFont(0, font)

            # Get categories for this device
            categories = self.sensor_model.get_categories_for_device(device)

            for category in categories:
                # Create category node
                category_display = self.sensor_model.get_category_display_name(category)
                category_item = QTreeWidgetItem(device_item)
                category_item.setText(0, category_display)
                category_item.setExpanded(False)

                # Italic font for categories
                font = category_item.font(0)
                font.setItalic(True)
                category_item.setFont(0, font)

                # Get sensors for this device/category
                sensors = self.sensor_model.get_sensors_for_device_category(device, category)

                for sensor in sensors:
                    # Create sensor node
                    sensor_item = QTreeWidgetItem(category_item)
                    sensor_item.setText(0, sensor.get("display_name", ""))
                    sensor_item.setText(1, str(sensor.get("current_value", "")))
                    sensor_item.setText(2, sensor.get("unit", ""))

                    # Store sensor data for later retrieval
                    sensor_item.setData(0, Qt.ItemDataRole.UserRole, sensor)

                    # Color-code by value (optional enhancement)
                    self._apply_sensor_styling(sensor_item, sensor)

                # Show sensor count in category
                sensor_count = len(sensors)
                category_item.setText(0, f"{category_display} ({sensor_count})")

            # Show category count in device
            category_count = len(categories)
            device_item.setText(0, f"{device} ({category_count} categories)")

    def _apply_sensor_styling(self, item: QTreeWidgetItem, sensor: dict):
        """
        Apply styling to sensor items based on type/value

        Args:
            item: Tree widget item
            sensor: Sensor data dict
        """
        sensor_type = sensor.get("type", "")

        # Color hints for different sensor types
        if sensor_type == "temperature":
            value = sensor.get("current_value", 0)
            if value > 80:
                item.setForeground(1, Qt.GlobalColor.red)
            elif value > 60:
                item.setForeground(1, Qt.GlobalColor.yellow)
            else:
                item.setForeground(1, Qt.GlobalColor.green)

    def _on_search(self, query: str):
        """
        Handle search query changes

        Args:
            query: Search string
        """
        if not query:
            # Show all sensors
            self._populate_tree()
            return

        # Search and show results
        results = self.sensor_model.search_sensors(query)

        self.tree.clear()

        if not results:
            # No results
            no_results_item = QTreeWidgetItem(self.tree)
            no_results_item.setText(0, "No sensors found")
            no_results_item.setForeground(0, Qt.GlobalColor.gray)
            return

        # Create flat list of results (no hierarchy in search mode)
        results_item = QTreeWidgetItem(self.tree)
        results_item.setText(0, f"Search Results ({len(results)})")
        results_item.setExpanded(True)

        font = results_item.font(0)
        font.setBold(True)
        results_item.setFont(0, font)

        for sensor in results:
            sensor_item = QTreeWidgetItem(results_item)
            sensor_item.setText(0, sensor.get("display_name", ""))
            sensor_item.setText(1, str(sensor.get("current_value", "")))
            sensor_item.setText(2, sensor.get("unit", ""))

            # Store sensor data
            sensor_item.setData(0, Qt.ItemDataRole.UserRole, sensor)

            # Apply styling
            self._apply_sensor_styling(sensor_item, sensor)

    def _on_item_double_clicked(self, item: QTreeWidgetItem, column: int):
        """
        Handle double-click on tree item

        Args:
            item: Clicked item
            column: Clicked column
        """
        # Get sensor data from item
        sensor = item.data(0, Qt.ItemDataRole.UserRole)

        if sensor:
            # Emit signal with sensor data
            self.sensorDoubleClicked.emit(sensor)

    def get_selected_sensor(self) -> dict:
        """
        Get currently selected sensor

        Returns:
            Sensor dict or None
        """
        current_item = self.tree.currentItem()
        if current_item:
            return current_item.data(0, Qt.ItemDataRole.UserRole)
        return None

    def clear(self):
        """Clear the tree"""
        self.tree.clear()
        self.count_label.setText("No sensors discovered yet")

    def expand_all(self):
        """Expand all tree nodes"""
        self.tree.expandAll()

    def collapse_all(self):
        """Collapse all tree nodes"""
        self.tree.collapseAll()
