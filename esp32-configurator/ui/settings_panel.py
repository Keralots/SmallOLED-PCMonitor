"""
ESP32 Configurator - Settings Panel Widget
Provides ESP32 IP, UDP port, and update interval configuration
"""

from PyQt6.QtWidgets import (
    QWidget, QGroupBox, QFormLayout, QLineEdit,
    QSpinBox, QPushButton, QHBoxLayout, QVBoxLayout,
    QLabel, QMessageBox
)
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QRegularExpressionValidator
from PyQt6.QtCore import QRegularExpression
import socket


class SettingsPanelWidget(QWidget):
    """
    Widget for configuring ESP32 connection settings

    Signals:
        settingsChanged: Emitted when settings are modified
    """

    settingsChanged = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)

        # Default values
        self.esp32_ip = "192.168.0.163"
        self.udp_port = 4210
        self.update_interval = 3

        self._init_ui()

    def _init_ui(self):
        """Initialize the user interface"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        # Settings Group Box
        settings_group = QGroupBox("ESP32 Connection Settings")
        form_layout = QFormLayout()

        # ESP32 IP Address
        self.ip_edit = QLineEdit(self.esp32_ip)
        self.ip_edit.setPlaceholderText("192.168.0.163")
        self.ip_edit.setMaxLength(15)

        # IP address validation (simple regex)
        ip_regex = QRegularExpression(
            r"^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$"
        )
        ip_validator = QRegularExpressionValidator(ip_regex)
        self.ip_edit.setValidator(ip_validator)
        self.ip_edit.textChanged.connect(self.settingsChanged.emit)

        form_layout.addRow("ESP32 IP Address:", self.ip_edit)

        # UDP Port
        self.port_spin = QSpinBox()
        self.port_spin.setRange(1, 65535)
        self.port_spin.setValue(self.udp_port)
        self.port_spin.setSuffix(" ")
        self.port_spin.valueChanged.connect(self.settingsChanged.emit)

        form_layout.addRow("UDP Port:", self.port_spin)

        # Update Interval
        self.interval_spin = QSpinBox()
        self.interval_spin.setRange(1, 60)
        self.interval_spin.setValue(self.update_interval)
        self.interval_spin.setSuffix(" seconds")
        self.interval_spin.valueChanged.connect(self.settingsChanged.emit)

        form_layout.addRow("Update Interval:", self.interval_spin)

        settings_group.setLayout(form_layout)
        layout.addWidget(settings_group)

        # Test Connection Button
        button_layout = QHBoxLayout()
        button_layout.addStretch()

        self.test_button = QPushButton("Test Connection")
        self.test_button.setToolTip("Send a test UDP packet to verify ESP32 is reachable")
        self.test_button.clicked.connect(self._test_connection)
        button_layout.addWidget(self.test_button)

        layout.addLayout(button_layout)
        layout.addStretch()

    def _test_connection(self):
        """Test UDP connection to ESP32"""
        ip = self.get_esp32_ip()
        port = self.get_udp_port()

        # Validate IP
        if not ip:
            QMessageBox.warning(
                self,
                "Invalid IP",
                "Please enter a valid IP address."
            )
            return

        try:
            # Create test UDP socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(2)

            # Send test message
            test_message = b'{"version":"2.0","test":true}'
            sock.sendto(test_message, (ip, port))

            # Show success
            QMessageBox.information(
                self,
                "Connection Test",
                f"Test packet sent to {ip}:{port}\n\n"
                "Note: This only verifies the packet was sent.\n"
                "Check your ESP32 display to confirm receipt."
            )

            sock.close()

        except socket.timeout:
            QMessageBox.warning(
                self,
                "Connection Timeout",
                f"No response from {ip}:{port}\n\n"
                "The ESP32 may not be reachable or on a different network."
            )
        except Exception as e:
            QMessageBox.critical(
                self,
                "Connection Error",
                f"Failed to send test packet:\n{str(e)}"
            )

    def get_esp32_ip(self) -> str:
        """Get the configured ESP32 IP address"""
        return self.ip_edit.text().strip()

    def get_udp_port(self) -> int:
        """Get the configured UDP port"""
        return self.port_spin.value()

    def set_esp32_ip(self, ip: str):
        """Set the ESP32 IP address"""
        self.ip_edit.setText(ip)

    def set_udp_port(self, port: int):
        """Set the UDP port"""
        self.port_spin.setValue(port)

    def set_update_interval(self, interval: int):
        """Set the update interval"""
        self.interval_spin.setValue(interval)

    def get_update_interval(self) -> int:
        """Get the configured update interval in seconds"""
        return self.interval_spin.value()

    def set_esp32_ip(self, ip: str):
        """Set the ESP32 IP address"""
        self.ip_edit.setText(ip)

    def set_udp_port(self, port: int):
        """Set the UDP port"""
        self.port_spin.setValue(port)

    def set_update_interval(self, interval: int):
        """Set the update interval"""
        self.interval_spin.setValue(interval)

    def validate(self) -> tuple[bool, str]:
        """
        Validate all settings

        Returns:
            Tuple of (is_valid, error_message)
        """
        ip = self.get_esp32_ip()

        # Validate IP address
        if not ip:
            return False, "ESP32 IP address cannot be empty"

        # Simple IP format check
        parts = ip.split('.')
        if len(parts) != 4:
            return False, "Invalid IP address format"

        try:
            for part in parts:
                num = int(part)
                if num < 0 or num > 255:
                    return False, "Invalid IP address (values must be 0-255)"
        except ValueError:
            return False, "Invalid IP address format"

        # Port and interval are already validated by spin boxes
        return True, ""

    def get_settings_dict(self) -> dict:
        """
        Get all settings as a dictionary

        Returns:
            Dictionary with esp32_ip, udp_port, update_interval
        """
        return {
            "esp32_ip": self.get_esp32_ip(),
            "udp_port": self.get_udp_port(),
            "update_interval": self.get_update_interval()
        }

    def load_settings_dict(self, settings: dict):
        """
        Load settings from a dictionary

        Args:
            settings: Dictionary with esp32_ip, udp_port, update_interval
        """
        if "esp32_ip" in settings:
            self.set_esp32_ip(settings["esp32_ip"])
        if "udp_port" in settings:
            self.set_udp_port(settings["udp_port"])
        if "update_interval" in settings:
            self.set_update_interval(settings["update_interval"])
