"""
ESP32 Configurator - Position Assignment Dialog
Allows user to assign grid positions and settings to metrics
"""

from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
    QPushButton, QGroupBox, QSpinBox, QScrollArea, QWidget
)
from PyQt6.QtCore import Qt


class PositionDialog(QDialog):
    """
    Dialog for assigning position, bar position, bar range, companion, and bar size to a metric
    """

    def __init__(self, metric: dict, all_metrics: list, row_mode: int = 1, parent=None):
        super().__init__(parent)

        self.metric = metric
        self.all_metrics = all_metrics
        self.row_mode = row_mode  # 0 = 5 rows, 1 = 6 rows

        self.setWindowTitle("Metric Settings")
        self.setMinimumWidth(500)
        self.setMinimumHeight(600)

        self._init_ui()

    def _init_ui(self):
        """Initialize the user interface"""
        # Main layout
        main_layout = QVBoxLayout(self)

        # Metric name label
        name_label = QLabel(f"<b>{self.metric.get('display_name', 'Unknown')}</b>")
        name_label.setStyleSheet("font-size: 12pt; color: #00d4ff;")
        main_layout.addWidget(name_label)

        # Unit label
        unit = self.metric.get("unit", "")
        if unit:
            unit_label = QLabel(f"Unit: {unit}")
            unit_label.setStyleSheet("color: #888888;")
            main_layout.addWidget(unit_label)

        main_layout.addSpacing(10)

        # Scrollable content area
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        content_widget = QWidget()
        layout = QVBoxLayout(content_widget)

        # Text position group
        text_group = QGroupBox("Text Position")
        text_layout = QVBoxLayout()

        self.position_combo = QComboBox()
        self._populate_position_combo(self.position_combo)
        self.position_combo.setCurrentIndex(
            self._position_to_index(self.metric.get("position", 255))
        )
        text_layout.addWidget(self.position_combo)

        text_group.setLayout(text_layout)
        layout.addWidget(text_group)

        # Companion metric group
        companion_group = QGroupBox("Companion Metric")
        companion_layout = QVBoxLayout()

        # Companion combo
        companion_label = QLabel("Display additional metric on same line:")
        companion_label.setWordWrap(True)
        companion_label.setStyleSheet("color: #888888; font-size: 9pt;")
        companion_layout.addWidget(companion_label)

        self.companion_combo = QComboBox()
        self._populate_companion_combo()
        companion_layout.addWidget(self.companion_combo)

        companion_group.setLayout(companion_layout)
        layout.addWidget(companion_group)

        # Progress bar group
        bar_group = QGroupBox("Progress Bar")
        bar_layout = QVBoxLayout()

        # Bar position
        bar_pos_layout = QHBoxLayout()
        bar_pos_layout.addWidget(QLabel("Position:"))
        self.bar_position_combo = QComboBox()
        self._populate_position_combo(self.bar_position_combo)
        self.bar_position_combo.setCurrentIndex(
            self._position_to_index(self.metric.get("bar_position", 255))
        )
        bar_pos_layout.addWidget(self.bar_position_combo)
        bar_layout.addLayout(bar_pos_layout)

        # Bar range
        range_layout = QHBoxLayout()
        range_layout.addWidget(QLabel("Range:"))

        self.bar_min_spin = QSpinBox()
        self.bar_min_spin.setRange(-10000, 10000)
        self.bar_min_spin.setValue(self.metric.get("bar_min", 0))
        range_layout.addWidget(self.bar_min_spin)

        range_layout.addWidget(QLabel("to"))

        self.bar_max_spin = QSpinBox()
        self.bar_max_spin.setRange(-10000, 10000)
        self.bar_max_spin.setValue(self.metric.get("bar_max", 100))
        range_layout.addWidget(self.bar_max_spin)

        bar_layout.addLayout(range_layout)

        # Bar width
        width_layout = QHBoxLayout()
        width_layout.addWidget(QLabel("Width (pixels):"))
        self.bar_width_spin = QSpinBox()
        self.bar_width_spin.setRange(10, 128)
        self.bar_width_spin.setValue(self.metric.get("bar_width", 60))
        width_layout.addWidget(self.bar_width_spin)
        width_layout.addStretch()
        bar_layout.addLayout(width_layout)

        # Bar offset
        offset_layout = QHBoxLayout()
        offset_layout.addWidget(QLabel("X Offset (pixels):"))
        self.bar_offset_spin = QSpinBox()
        self.bar_offset_spin.setRange(0, 64)
        self.bar_offset_spin.setValue(self.metric.get("bar_offset", 0))
        offset_layout.addWidget(self.bar_offset_spin)
        offset_layout.addStretch()
        bar_layout.addLayout(offset_layout)

        # Help text
        help_label = QLabel("Custom range allows non-percentage metrics to have progress bars.\n"
                          "Example: Temperature 0-100°C, Fan speed 0-2000 RPM\n\n"
                          "Width: 60px for left column, 64px for right column (default)\n"
                          "Offset: Shift bar right by N pixels")
        help_label.setWordWrap(True)
        help_label.setStyleSheet("color: #888888; font-size: 9pt; padding: 5px;")
        bar_layout.addWidget(help_label)

        bar_group.setLayout(bar_layout)
        layout.addWidget(bar_group)

        # Set content widget
        scroll.setWidget(content_widget)
        main_layout.addWidget(scroll)

        # Buttons
        button_layout = QHBoxLayout()
        button_layout.addStretch()

        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        button_layout.addWidget(cancel_btn)

        ok_btn = QPushButton("OK")
        ok_btn.clicked.connect(self.accept)
        ok_btn.setDefault(True)
        button_layout.addWidget(ok_btn)

        main_layout.addLayout(button_layout)

    def _populate_position_combo(self, combo: QComboBox):
        """Populate position combo box with row/column options"""
        # None option
        combo.addItem("None (Hidden)", 255)

        # Add positions based on row mode
        max_rows = 5 if self.row_mode == 0 else 6

        for row in range(max_rows):
            left_pos = row * 2
            right_pos = row * 2 + 1

            combo.addItem(f"Row {row + 1} - Left", left_pos)
            combo.addItem(f"Row {row + 1} - Right", right_pos)

    def _populate_companion_combo(self):
        """Populate companion metric combo box"""
        # None option
        self.companion_combo.addItem("None", 0)

        # Add all other metrics
        current_id = self.metric.get("id", 0)
        for metric in self.all_metrics:
            if metric.get("id") != current_id:
                name = metric.get("display_name", metric.get("name", "Unknown"))
                unit = metric.get("unit", "")
                label = f"{name} ({unit})" if unit else name
                self.companion_combo.addItem(label, metric.get("id"))

        # Set current selection
        current_companion = self.metric.get("companion_id", 0)
        index = self.companion_combo.findData(current_companion)
        if index >= 0:
            self.companion_combo.setCurrentIndex(index)

    def _position_to_index(self, position: int) -> int:
        """Convert position value to combo box index"""
        # Index 0 is "None (255)"
        if position == 255:
            return 0

        # Positions 0-11 map to indices 1-12 (or 1-10 for 5-row mode)
        # Row 1 Left (0) -> index 1
        # Row 1 Right (1) -> index 2
        # Row 2 Left (2) -> index 3
        # etc.
        return position + 1

    def get_position(self) -> int:
        """Get selected text position"""
        return self.position_combo.currentData()

    def get_bar_position(self) -> int:
        """Get selected bar position"""
        return self.bar_position_combo.currentData()

    def get_bar_range(self) -> tuple[int, int]:
        """Get bar range (min, max)"""
        return (self.bar_min_spin.value(), self.bar_max_spin.value())

    def get_companion_id(self) -> int:
        """Get selected companion metric ID"""
        return self.companion_combo.currentData()

    def get_bar_size(self) -> tuple[int, int]:
        """Get bar size (width, offset)"""
        return (self.bar_width_spin.value(), self.bar_offset_spin.value())
