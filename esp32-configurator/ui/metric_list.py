"""
ESP32 Configurator - Metrics List Widget
Displays selected metrics with drag-drop reordering
"""

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QListWidget, QListWidgetItem,
    QGroupBox, QHBoxLayout, QPushButton, QMenu, QMessageBox, QInputDialog, QDialog
)
from PyQt6.QtCore import Qt, QMimeData, pyqtSignal
from PyQt6.QtGui import QDrag, QAction
import json

from models.metrics_model import MetricsModel
from ui.position_dialog import PositionDialog


class DroppableListWidget(QListWidget):
    """Custom QListWidget that accepts sensor drops"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.parent_widget = None  # Will be set by MetricsListWidget

    def dragEnterEvent(self, event):
        """Handle drag enter from sensor tree"""
        if event.mimeData().hasFormat("application/x-sensor"):
            event.acceptProposedAction()
        else:
            super().dragEnterEvent(event)

    def dragMoveEvent(self, event):
        """Handle drag move"""
        if event.mimeData().hasFormat("application/x-sensor"):
            event.acceptProposedAction()
        else:
            super().dragMoveEvent(event)

    def dropEvent(self, event):
        """Handle drop from sensor tree"""
        if event.mimeData().hasFormat("application/x-sensor"):
            # Parse sensor data
            sensor_data = event.mimeData().data("application/x-sensor").data()
            sensor = json.loads(sensor_data.decode())

            # Add to metrics via parent widget
            if self.parent_widget:
                self.parent_widget.add_metric_from_sensor(sensor)

            event.acceptProposedAction()
        else:
            super().dropEvent(event)


class MetricsListWidget(QWidget):
    """
    Widget displaying selected metrics with drag-drop support

    Features:
    - Accepts drops from sensor tree
    - Internal drag-drop reordering
    - Right-click context menu (delete)
    - Delete key support
    - 20-metric limit with counter
    """

    # Signal emitted when a metric is selected
    metricSelected = pyqtSignal(dict)

    def __init__(self, parent=None):
        super().__init__(parent)

        self.metrics_model = MetricsModel()
        self.row_mode = 1  # Default: 6 rows
        self._init_ui()
        self._connect_signals()

    def _init_ui(self):
        """Initialize the user interface"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        # Group box with metric counter
        self.group_box = QGroupBox("Selected Metrics (0/20)")
        group_layout = QVBoxLayout()

        # List widget (with drop support)
        self.list_widget = DroppableListWidget()
        self.list_widget.parent_widget = self  # Set parent reference for drop handling
        self.list_widget.setSelectionMode(QListWidget.SelectionMode.SingleSelection)
        self.list_widget.setDragDropMode(QListWidget.DragDropMode.InternalMove)
        self.list_widget.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.list_widget.setAcceptDrops(True)

        # Enable custom drag-drop handling
        self.list_widget.viewport().setAcceptDrops(True)
        self.list_widget.setDropIndicatorShown(True)

        # Context menu
        self.list_widget.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.list_widget.customContextMenuRequested.connect(self._show_context_menu)

        # Item clicked
        self.list_widget.itemClicked.connect(self._on_item_clicked)

        # Handle drop events
        self.list_widget.model().rowsMoved.connect(self._on_rows_moved)

        group_layout.addWidget(self.list_widget)

        # Button bar
        button_layout = QHBoxLayout()

        self.clear_button = QPushButton("Clear All")
        self.clear_button.setProperty("class", "secondary")
        self.clear_button.clicked.connect(self._clear_all)
        self.clear_button.setEnabled(False)
        button_layout.addWidget(self.clear_button)

        button_layout.addStretch()

        self.delete_button = QPushButton("Delete")
        self.delete_button.clicked.connect(self._delete_selected)
        self.delete_button.setEnabled(False)
        button_layout.addWidget(self.delete_button)

        group_layout.addLayout(button_layout)

        self.group_box.setLayout(group_layout)
        layout.addWidget(self.group_box)

        # Install event filter for Delete key
        self.list_widget.installEventFilter(self)

    def _connect_signals(self):
        """Connect model signals"""
        self.metrics_model.metricsChanged.connect(self._refresh_display)

    def add_metric_from_sensor(self, sensor: dict) -> bool:
        """
        Add a metric from a sensor dict

        Args:
            sensor: Sensor dict from sensor tree

        Returns:
            True if added, False if failed
        """
        success, error_message = self.metrics_model.add_metric(sensor)

        if not success:
            QMessageBox.warning(
                self,
                "Cannot Add Metric",
                error_message
            )
            return False

        return True

    def _refresh_display(self):
        """Refresh the list widget from model"""
        # Save current selection
        current_row = self.list_widget.currentRow()

        # Clear list
        self.list_widget.clear()

        # Populate from model
        metrics = self.metrics_model.get_metrics()
        for metric in metrics:
            self._add_list_item(metric)

        # Update counter
        count = self.metrics_model.get_count()
        max_count = self.metrics_model.MAX_METRICS
        self.group_box.setTitle(f"Selected Metrics ({count}/{max_count})")

        # Update button states
        self.clear_button.setEnabled(count > 0)
        self.delete_button.setEnabled(self.list_widget.currentRow() >= 0)

        # Restore selection
        if 0 <= current_row < self.list_widget.count():
            self.list_widget.setCurrentRow(current_row)

    def _add_list_item(self, metric: dict):
        """
        Add a metric to the list widget

        Args:
            metric: Metric dict
        """
        # Format display text
        display_name = metric.get("display_name", "")
        value = metric.get("current_value", "")
        unit = metric.get("unit", "")
        custom_label = metric.get("custom_label", "")

        if custom_label:
            # Show custom label if set
            text = f"{custom_label} - {value} {unit}"
        else:
            # Truncate display name to fit
            max_name_len = 40
            if len(display_name) > max_name_len:
                display_name = display_name[:max_name_len-3] + "..."
            text = f"{display_name} - {value} {unit}"

        # Create item
        item = QListWidgetItem(text)
        item.setData(Qt.ItemDataRole.UserRole, metric)

        # Add to list
        self.list_widget.addItem(item)

    def _on_rows_moved(self, parent, start, end, destination, row):
        """
        Handle rows moved (drag-drop reordering)

        Args:
            parent: Parent index (unused)
            start: Start row
            end: End row
            destination: Destination index (unused)
            row: Destination row
        """
        # Calculate destination index
        if row > start:
            to_index = row - 1
        else:
            to_index = row

        # Update model
        self.metrics_model.move_metric(start, to_index)

    def _on_item_clicked(self, item: QListWidgetItem):
        """
        Handle item clicked

        Args:
            item: Clicked item
        """
        # Enable delete button
        self.delete_button.setEnabled(True)

        # Emit signal
        metric = item.data(Qt.ItemDataRole.UserRole)
        if metric:
            self.metricSelected.emit(metric)

    def _delete_selected(self):
        """Delete the currently selected metric"""
        current_row = self.list_widget.currentRow()
        if current_row >= 0:
            # Get metric for confirmation
            item = self.list_widget.item(current_row)
            metric = item.data(Qt.ItemDataRole.UserRole)
            display_name = metric.get("display_name", "Unknown")

            # Confirm deletion
            reply = QMessageBox.question(
                self,
                "Delete Metric",
                f"Delete metric:\n{display_name}?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )

            if reply == QMessageBox.StandardButton.Yes:
                self.metrics_model.remove_metric(current_row)

    def _clear_all(self):
        """Clear all metrics"""
        if self.metrics_model.get_count() > 0:
            reply = QMessageBox.question(
                self,
                "Clear All Metrics",
                f"Clear all {self.metrics_model.get_count()} metrics?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )

            if reply == QMessageBox.StandardButton.Yes:
                self.metrics_model.clear_all()

    def _show_context_menu(self, pos):
        """
        Show context menu

        Args:
            pos: Click position
        """
        item = self.list_widget.itemAt(pos)
        if item:
            menu = QMenu(self)

            # Set position action
            position_action = QAction("Set Position...", self)
            position_action.triggered.connect(lambda: self._set_position(self.list_widget.row(item)))
            menu.addAction(position_action)

            menu.addSeparator()

            # Edit label action
            edit_action = QAction("Edit Custom Label...", self)
            edit_action.triggered.connect(lambda: self._edit_label(self.list_widget.row(item)))
            menu.addAction(edit_action)

            menu.addSeparator()

            # Delete action
            delete_action = QAction("Delete", self)
            delete_action.triggered.connect(self._delete_selected)
            menu.addAction(delete_action)

            menu.exec(self.list_widget.viewport().mapToGlobal(pos))

    def _set_position(self, index: int):
        """
        Set metric position

        Args:
            index: Metric index
        """
        # Get current metric
        metric = self.metrics_model.get_metric(index)
        if not metric:
            return

        # Get all metrics for companion selection
        all_metrics = self.metrics_model.get_metrics()

        # Show position dialog
        dialog = PositionDialog(metric, all_metrics, row_mode=self.row_mode, parent=self)

        if dialog.exec() == QDialog.DialogCode.Accepted:
            # Update metric position settings
            position = dialog.get_position()
            bar_position = dialog.get_bar_position()
            bar_min, bar_max = dialog.get_bar_range()
            companion_id = dialog.get_companion_id()
            bar_width, bar_offset = dialog.get_bar_size()

            self.metrics_model.update_position(index, position)
            self.metrics_model.update_bar_position(index, bar_position)
            self.metrics_model.update_bar_range(index, bar_min, bar_max)
            self.metrics_model.update_companion(index, companion_id)
            self.metrics_model.update_bar_size(index, bar_width, bar_offset)

    def _edit_label(self, index: int):
        """
        Edit custom label

        Args:
            index: Metric index
        """
        # Get current metric
        metric = self.metrics_model.get_metric(index)
        if not metric:
            return

        # Get current label
        current_label = metric.get("custom_label", "")
        display_name = metric.get("display_name", "")

        # Show input dialog
        new_label, ok = QInputDialog.getText(
            self,
            "Edit Custom Label",
            f"Enter custom label for:\n{display_name}\n\n"
            "(Max 10 characters for OLED display)\n"
            "Leave empty to use auto-generated label",
            text=current_label
        )

        if ok:
            # Truncate to 10 characters
            new_label = new_label[:10]

            # Update metric
            self.metrics_model.update_custom_label(index, new_label)

            # Show confirmation message
            if new_label:
                QMessageBox.information(
                    self,
                    "Label Updated",
                    f"Custom label set to: '{new_label}'"
                )
            else:
                QMessageBox.information(
                    self,
                    "Label Cleared",
                    "Using auto-generated label"
                )

    def eventFilter(self, obj, event):
        """Handle Delete key press"""
        if obj == self.list_widget and event.type() == event.Type.KeyPress:
            if event.key() == Qt.Key.Key_Delete:
                self._delete_selected()
                return True
        return super().eventFilter(obj, event)

    def get_metrics_model(self) -> MetricsModel:
        """Get the metrics model"""
        return self.metrics_model

    def get_metrics(self) -> list:
        """Get all metrics"""
        return self.metrics_model.get_metrics()

    def set_row_mode(self, mode: int):
        """Set row mode for position dialog"""
        self.row_mode = mode
