"""
ESP32 Configurator - Preferences Dialog
Global display and configuration settings
"""

from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
    QPushButton, QGroupBox, QSpinBox, QCheckBox, QTabWidget, QWidget
)
from PyQt6.QtCore import Qt


class PreferencesDialog(QDialog):
    """
    Dialog for global application preferences
    """

    def __init__(self, parent=None):
        super().__init__(parent)

        self.setWindowTitle("Preferences")
        self.setMinimumWidth(500)
        self.setMinimumHeight(400)

        # Default values
        self.row_mode = 1  # 0 = 5 rows, 1 = 6 rows
        self.show_clock = False
        self.clock_position = 0  # 0 = Center, 1 = Left, 2 = Right
        self.clock_offset = 0

        self._init_ui()

    def _init_ui(self):
        """Initialize the user interface"""
        layout = QVBoxLayout(self)

        # Tab widget
        tabs = QTabWidget()

        # Display tab
        display_tab = QWidget()
        display_layout = QVBoxLayout(display_tab)

        # Row mode group
        row_group = QGroupBox("Display Layout")
        row_layout = QVBoxLayout()

        row_label = QLabel("Row Mode:")
        row_layout.addWidget(row_label)

        self.row_mode_combo = QComboBox()
        self.row_mode_combo.addItem("5 Rows (13px spacing - Optimized)", 0)
        self.row_mode_combo.addItem("6 Rows (10px spacing - Compact)", 1)
        self.row_mode_combo.setCurrentIndex(self.row_mode)
        row_layout.addWidget(self.row_mode_combo)

        help_label = QLabel("5-row mode provides better readability.\n"
                          "6-row mode fits more metrics on screen.")
        help_label.setWordWrap(True)
        help_label.setStyleSheet("color: #888888; font-size: 9pt; padding: 5px;")
        row_layout.addWidget(help_label)

        row_group.setLayout(row_layout)
        display_layout.addWidget(row_group)

        # Clock group
        clock_group = QGroupBox("Clock Display")
        clock_layout = QVBoxLayout()

        # Show clock checkbox
        self.show_clock_check = QCheckBox("Show timestamp on OLED")
        self.show_clock_check.setChecked(self.show_clock)
        self.show_clock_check.toggled.connect(self._on_clock_toggled)
        clock_layout.addWidget(self.show_clock_check)

        # Clock position
        pos_layout = QHBoxLayout()
        pos_layout.addWidget(QLabel("Position:"))

        self.clock_position_combo = QComboBox()
        self.clock_position_combo.addItem("Center (Top)", 0)
        self.clock_position_combo.addItem("Left Column, Row 1", 1)
        self.clock_position_combo.addItem("Right Column, Row 1", 2)
        self.clock_position_combo.setCurrentIndex(self.clock_position)
        self.clock_position_combo.setEnabled(self.show_clock)
        pos_layout.addWidget(self.clock_position_combo)

        clock_layout.addLayout(pos_layout)

        # Clock offset
        offset_layout = QHBoxLayout()
        offset_layout.addWidget(QLabel("X Offset:"))

        self.clock_offset_spin = QSpinBox()
        self.clock_offset_spin.setRange(-32, 32)
        self.clock_offset_spin.setValue(self.clock_offset)
        self.clock_offset_spin.setSuffix(" pixels")
        self.clock_offset_spin.setEnabled(self.show_clock)
        offset_layout.addWidget(self.clock_offset_spin)
        offset_layout.addStretch()

        clock_layout.addLayout(offset_layout)

        clock_help = QLabel("Timestamp format: HH:MM (24-hour)\n"
                          "Offset shifts clock left/right for fine positioning.")
        clock_help.setWordWrap(True)
        clock_help.setStyleSheet("color: #888888; font-size: 9pt; padding: 5px;")
        clock_layout.addWidget(clock_help)

        clock_group.setLayout(clock_layout)
        display_layout.addWidget(clock_group)

        display_layout.addStretch()

        tabs.addTab(display_tab, "Display")

        # Future tabs can be added here
        # - "Network" tab for ESP32 connection settings
        # - "Advanced" tab for other options

        layout.addWidget(tabs)

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

        layout.addLayout(button_layout)

    def _on_clock_toggled(self, checked: bool):
        """Handle clock checkbox toggle"""
        self.clock_position_combo.setEnabled(checked)
        self.clock_offset_spin.setEnabled(checked)

    def get_row_mode(self) -> int:
        """Get selected row mode (0 = 5 rows, 1 = 6 rows)"""
        return self.row_mode_combo.currentData()

    def get_clock_settings(self) -> tuple[bool, int, int]:
        """Get clock settings (show, position, offset)"""
        return (
            self.show_clock_check.isChecked(),
            self.clock_position_combo.currentData(),
            self.clock_offset_spin.value()
        )

    def set_row_mode(self, mode: int):
        """Set row mode"""
        self.row_mode = mode
        self.row_mode_combo.setCurrentIndex(mode)

    def set_clock_settings(self, show: bool, position: int, offset: int):
        """Set clock settings"""
        self.show_clock = show
        self.clock_position = position
        self.clock_offset = offset

        self.show_clock_check.setChecked(show)
        self.clock_position_combo.setCurrentIndex(position)
        self.clock_offset_spin.setValue(offset)
        self._on_clock_toggled(show)
