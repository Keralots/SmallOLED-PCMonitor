"""
ESP32 Configurator - Main Window
Professional PyQt6 application for ESP32 OLED display configuration
"""

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QLabel, QPushButton, QStatusBar,
    QMenuBar, QMenu, QMessageBox, QGroupBox, QFileDialog
)
from PyQt6.QtCore import Qt, QSize, QThread, pyqtSignal
from PyQt6.QtGui import QAction

from ui.settings_panel import SettingsPanelWidget
from ui.sensor_tree import SensorTreeWidget
from ui.metric_list import MetricsListWidget
from ui.oled_preview import OLEDPreviewWidget
from ui.preferences_dialog import PreferencesDialog
from core.sensor_discovery import discover_sensors
from core.sensor_poller import SensorPoller
from core.config_manager import ConfigManager


class MainWindow(QMainWindow):
    """
    Main application window for ESP32 Configurator

    Layout:
        - Menu bar (File, Edit, Help)
        - Central widget with 3-panel layout:
          - Left: Available sensors tree (placeholder)
          - Middle: Drag-drop zone
          - Right: Selected metrics list + OLED preview + settings
        - Status bar
    """

    def __init__(self):
        super().__init__()

        self.setWindowTitle("ESP32 OLED Configurator")
        self.setMinimumSize(QSize(1400, 900))

        # Initialize configuration
        self.config = {
            "esp32_ip": "192.168.0.163",
            "udp_port": 4210,
            "update_interval": 3,
            "metrics": []
        }

        # Display preferences
        self.row_mode = 1  # 0 = 5 rows, 1 = 6 rows
        self.show_clock = False
        self.clock_position = 0  # 0 = Center, 1 = Left, 2 = Right
        self.clock_offset = 0

        # Sensor data
        self.sensor_database = {}
        self.sensor_source = None
        self.sensor_poller = None  # Background polling thread

        self._create_menu_bar()
        self._create_central_widget()
        self._create_status_bar()

        # Discover sensors on startup
        self._discover_sensors()

    def _create_menu_bar(self):
        """Create the application menu bar"""
        menubar = self.menuBar()

        # File Menu
        file_menu = menubar.addMenu("&File")

        # New Configuration
        new_action = QAction("&New Configuration", self)
        new_action.setShortcut("Ctrl+N")
        new_action.setStatusTip("Create a new ESP32 configuration")
        new_action.triggered.connect(self._new_configuration)
        file_menu.addAction(new_action)

        # Open Configuration
        open_action = QAction("&Open Configuration...", self)
        open_action.setShortcut("Ctrl+O")
        open_action.setStatusTip("Load an existing configuration from file")
        open_action.triggered.connect(self._open_configuration)
        file_menu.addAction(open_action)

        # Save Configuration
        save_action = QAction("&Save Configuration", self)
        save_action.setShortcut("Ctrl+S")
        save_action.setStatusTip("Save current configuration to file")
        save_action.triggered.connect(self._save_configuration)
        file_menu.addAction(save_action)

        # Save As
        save_as_action = QAction("Save &As...", self)
        save_as_action.setShortcut("Ctrl+Shift+S")
        save_as_action.setStatusTip("Save configuration to a new file")
        save_as_action.triggered.connect(self._save_configuration_as)
        file_menu.addAction(save_as_action)

        file_menu.addSeparator()

        # Export Configuration
        export_action = QAction("&Export...", self)
        export_action.setStatusTip("Export configuration as JSON")
        export_action.triggered.connect(self._export_configuration)
        file_menu.addAction(export_action)

        # Import Configuration
        import_action = QAction("&Import...", self)
        import_action.setStatusTip("Import configuration from JSON")
        import_action.triggered.connect(self._import_configuration)
        file_menu.addAction(import_action)

        file_menu.addSeparator()

        # Exit
        exit_action = QAction("E&xit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.setStatusTip("Exit the application")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # Edit Menu
        edit_menu = menubar.addMenu("&Edit")

        # Clear Selection
        clear_action = QAction("&Clear Selection", self)
        clear_action.setShortcut("Ctrl+D")
        clear_action.setStatusTip("Clear all selected metrics")
        clear_action.triggered.connect(self._clear_selection)
        edit_menu.addAction(clear_action)

        edit_menu.addSeparator()

        # Preferences
        preferences_action = QAction("&Preferences...", self)
        preferences_action.setStatusTip("Open application preferences")
        preferences_action.triggered.connect(self._show_preferences)
        edit_menu.addAction(preferences_action)

        # Help Menu
        help_menu = menubar.addMenu("&Help")

        # User Guide
        guide_action = QAction("&User Guide", self)
        guide_action.setShortcut("F1")
        guide_action.setStatusTip("Open user guide")
        guide_action.triggered.connect(self._show_user_guide)
        help_menu.addAction(guide_action)

        # About
        about_action = QAction("&About ESP32 Configurator", self)
        about_action.setStatusTip("About this application")
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)

    def _create_central_widget(self):
        """Create the main central widget with dual-panel layout"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(10, 10, 10, 10)
        main_layout.setSpacing(10)

        # Title Label
        title_label = QLabel("ESP32 OLED Display Configurator")
        title_label.setStyleSheet("""
            QLabel {
                font-size: 18pt;
                font-weight: bold;
                color: #00d4ff;
                padding: 10px;
            }
        """)
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        main_layout.addWidget(title_label)

        # Main Splitter (3-way horizontal split)
        main_splitter = QSplitter(Qt.Orientation.Horizontal)

        # Left Panel - Available Sensors (Placeholder)
        left_panel = self._create_left_panel()
        main_splitter.addWidget(left_panel)

        # Middle Panel - Drag Zone (Placeholder)
        middle_panel = self._create_middle_panel()
        main_splitter.addWidget(middle_panel)

        # Right Panel - Selected Metrics + OLED Preview + Settings
        right_panel = self._create_right_panel()
        main_splitter.addWidget(right_panel)

        # Set initial splitter sizes (480px, 40px, 880px for 1400px window)
        main_splitter.setSizes([480, 40, 880])

        main_layout.addWidget(main_splitter, stretch=1)

        # Connect metrics model to OLED preview
        self._connect_oled_preview()

        # Bottom Button Bar
        button_layout = QHBoxLayout()
        button_layout.addStretch()

        cancel_button = QPushButton("Cancel")
        cancel_button.setProperty("class", "secondary")
        cancel_button.setMinimumSize(QSize(120, 36))
        cancel_button.clicked.connect(self.close)
        button_layout.addWidget(cancel_button)

        save_button = QPushButton("Save Configuration")
        save_button.setMinimumSize(QSize(150, 36))
        save_button.clicked.connect(self._save_and_close)
        button_layout.addWidget(save_button)

        main_layout.addLayout(button_layout)

    def _create_left_panel(self) -> QWidget:
        """Create the left panel (Available Sensors tree)"""
        # Create sensor tree widget
        self.sensor_tree = SensorTreeWidget()

        # Connect signals
        self.sensor_tree.sensorDoubleClicked.connect(self._on_sensor_selected)

        return self.sensor_tree

    def _create_middle_panel(self) -> QWidget:
        """Create the middle panel (Drag-drop zone - placeholder)"""
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)

        # Drag zone placeholder
        placeholder = QLabel("⇄")
        placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        placeholder.setStyleSheet("""
            QLabel {
                color: #00d4ff;
                font-size: 24pt;
                font-weight: bold;
            }
        """)
        placeholder.setToolTip("Drag sensors from left to right")
        layout.addWidget(placeholder)

        return panel

    def _create_right_panel(self) -> QWidget:
        """Create the right panel (Selected Metrics + OLED Preview + Settings)"""
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        # Selected Metrics List
        self.metrics_list = MetricsListWidget()
        layout.addWidget(self.metrics_list, stretch=1)

        # OLED Preview
        self.oled_preview = OLEDPreviewWidget()
        layout.addWidget(self.oled_preview)

        # Settings Panel
        self.settings_panel = SettingsPanelWidget()
        layout.addWidget(self.settings_panel)

        return panel

    def _create_status_bar(self):
        """Create the application status bar"""
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready - No sensor source detected yet", 0)

    def _connect_oled_preview(self):
        """Connect OLED preview to metrics model"""
        # Get metrics model
        metrics_model = self.metrics_list.get_metrics_model()

        # Connect metricsChanged signal to update OLED preview
        metrics_model.metricsChanged.connect(self._update_oled_preview)

        # Initial update
        self._update_oled_preview()

    def _update_oled_preview(self):
        """Update OLED preview with current metrics"""
        metrics = self.metrics_list.get_metrics()
        self.oled_preview.set_metrics(metrics)

    # Menu Actions (Placeholders for Phase 6)

    def _new_configuration(self):
        """Create a new configuration"""
        QMessageBox.information(
            self,
            "New Configuration",
            "This feature will be implemented in Phase 6 (Configuration & Polish)"
        )

    def _open_configuration(self):
        """Open an existing configuration"""
        QMessageBox.information(
            self,
            "Open Configuration",
            "This feature will be implemented in Phase 6 (Configuration & Polish)"
        )

    def _save_configuration(self):
        """Save current configuration"""
        # Get settings and metrics
        settings = self.settings_panel.get_settings_dict()
        metrics = self.metrics_list.get_metrics()

        # Save to default file
        success, message = ConfigManager.save_config(
            ConfigManager.DEFAULT_CONFIG_PATH,
            settings,
            metrics
        )

        if success:
            QMessageBox.information(self, "Save Configuration", message)
            self.status_bar.showMessage("Configuration saved", 3000)
        else:
            QMessageBox.warning(self, "Save Failed", message)

    def _save_configuration_as(self):
        """Save configuration to a new file"""
        # Get file path from user
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save Configuration As",
            "esp32_config.json",
            "JSON Files (*.json);;All Files (*)"
        )

        if file_path:
            # Get settings and metrics
            settings = self.settings_panel.get_settings_dict()
            metrics = self.metrics_list.get_metrics()

            # Save to file
            success, message = ConfigManager.save_config(file_path, settings, metrics)

            if success:
                QMessageBox.information(self, "Save Configuration", message)
                self.status_bar.showMessage(f"Configuration saved to {file_path}", 3000)
            else:
                QMessageBox.warning(self, "Save Failed", message)

    def _export_configuration(self):
        """Export configuration as JSON"""
        # Same as Save As
        self._save_configuration_as()

    def _import_configuration(self):
        """Import configuration from JSON"""
        # Get file path from user
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Import Configuration",
            "",
            "JSON Files (*.json);;All Files (*)"
        )

        if file_path:
            # Load configuration
            success, config, message = ConfigManager.load_config(file_path)

            if success:
                # Apply settings
                self.settings_panel.set_esp32_ip(config.get("esp32_ip", "192.168.0.163"))
                self.settings_panel.set_udp_port(config.get("udp_port", 4210))
                self.settings_panel.set_update_interval(config.get("update_interval", 3))

                # Clear existing metrics
                self.metrics_list.get_metrics_model().clear_all()

                # Add imported metrics
                metrics = config.get("metrics", [])
                for metric in metrics:
                    # Convert metric dict to sensor dict format
                    sensor = {
                        "name": metric.get("name", ""),
                        "display_name": metric.get("display_name", ""),
                        "source": metric.get("source", ""),
                        "type": metric.get("type", ""),
                        "unit": metric.get("unit", ""),
                        "custom_label": metric.get("custom_label", ""),
                        "current_value": metric.get("current_value", 0)
                    }

                    # Add source-specific fields
                    if metric.get("source") == "hwinfo":
                        sensor["hwinfo_reading_id"] = metric.get("hwinfo_reading_id", 0)
                    elif metric.get("source") == "wmi":
                        sensor["wmi_identifier"] = metric.get("wmi_identifier", "")

                    # Add to metrics list
                    self.metrics_list.add_metric_from_sensor(sensor)

                QMessageBox.information(
                    self,
                    "Import Configuration",
                    f"{message}\n\nImported {len(metrics)} metrics"
                )
                self.status_bar.showMessage(f"Configuration imported from {file_path}", 3000)
            else:
                QMessageBox.warning(self, "Import Failed", message)

    def _clear_selection(self):
        """Clear all selected metrics"""
        if self.metrics_list.get_metrics_model().get_count() > 0:
            reply = QMessageBox.question(
                self,
                "Clear Selection",
                f"Clear all {self.metrics_list.get_metrics_model().get_count()} selected metrics?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            if reply == QMessageBox.StandardButton.Yes:
                self.metrics_list.get_metrics_model().clear_all()
                self.status_bar.showMessage("All metrics cleared", 3000)
        else:
            QMessageBox.information(
                self,
                "Clear Selection",
                "No metrics selected"
            )

    def _show_preferences(self):
        """Show application preferences"""
        dialog = PreferencesDialog(self)

        # Set current values
        dialog.set_row_mode(self.row_mode)
        dialog.set_clock_settings(self.show_clock, self.clock_position, self.clock_offset)

        if dialog.exec() == PreferencesDialog.DialogCode.Accepted:
            # Get new values
            self.row_mode = dialog.get_row_mode()
            self.show_clock, self.clock_position, self.clock_offset = dialog.get_clock_settings()

            # Apply to OLED preview
            self.oled_preview.set_row_mode(self.row_mode)
            self.oled_preview.set_clock_display(self.show_clock, self.clock_position, self.clock_offset)

            # Apply to metrics list (for position dialog)
            self.metrics_list.set_row_mode(self.row_mode)

            # Update status bar
            self.statusBar().showMessage("Preferences updated", 3000)

    def _show_user_guide(self):
        """Show user guide"""
        QMessageBox.information(
            self,
            "User Guide",
            "User guide will be available in Phase 6 (Documentation)\n\n"
            "For now, see the plan document:\n"
            "C:\\Users\\rafal\\.claude\\plans\\compressed-yawning-plum.md"
        )

    def _show_about(self):
        """Show about dialog"""
        QMessageBox.about(
            self,
            "About ESP32 Configurator",
            "<h2>ESP32 OLED Configurator</h2>"
            "<p>Version 1.0.0 (Phase 1 - Skeleton)</p>"
            "<p>Professional PyQt6 GUI for SmallOLED-PCMonitor ESP32 displays</p>"
            "<p><b>Features:</b></p>"
            "<ul>"
            "<li>Drag-and-drop sensor assignment</li>"
            "<li>Live OLED preview (128×64 pixel-accurate)</li>"
            "<li>Real-time sensor value updates</li>"
            "<li>200+ sensor support (HWiNFO64/LibreHardwareMonitor)</li>"
            "</ul>"
            "<p><b>Author:</b> SmallOLED-PCMonitor Project</p>"
            "<p><b>License:</b> MIT</p>"
            "<p><b>GitHub:</b> <a href='https://github.com/Keralots/SmallOLED-PCMonitor'>"
            "SmallOLED-PCMonitor</a></p>"
        )

    def _discover_sensors(self):
        """Discover available sensors from HWiNFO/WMI"""
        self.status_bar.showMessage("Discovering sensors...", 0)

        # Run sensor discovery
        sensor_database, sensor_source, success = discover_sensors()

        if success:
            # Store sensor data
            self.sensor_database = sensor_database
            self.sensor_source = sensor_source

            # Load into sensor tree
            self.sensor_tree.load_sensors(sensor_database, sensor_source)

            # Start sensor polling thread
            self._start_sensor_polling()

            # Update status bar
            if sensor_source:
                total_sensors = sum(len(sensors) for sensors in sensor_database.values())
                self.status_bar.showMessage(
                    f"Ready - {total_sensors} sensors discovered from {sensor_source.upper()}",
                    0
                )
            else:
                self.status_bar.showMessage(
                    "Ready - No sensor source detected (HWiNFO64/LibreHardwareMonitor not running)",
                    0
                )
        else:
            # Discovery failed
            self.status_bar.showMessage(
                "Ready - Sensor discovery failed (check HWiNFO64/LibreHardwareMonitor)",
                0
            )
            QMessageBox.warning(
                self,
                "Sensor Discovery",
                "Failed to discover sensors.\n\n"
                "Make sure HWiNFO64 or LibreHardwareMonitor is running.\n\n"
                "For HWiNFO64:\n"
                "• Enable 'Shared Memory Support' in Settings\n"
                "• Restart HWiNFO64 after enabling\n\n"
                "For LibreHardwareMonitor:\n"
                "• Run as Administrator\n"
                "• Ensure WMI access is enabled"
            )

    def _start_sensor_polling(self):
        """Start background sensor polling thread"""
        if self.sensor_poller:
            self.sensor_poller.stop()

        # Create and start poller
        self.sensor_poller = SensorPoller(self.sensor_source, self.sensor_database)
        self.sensor_poller.sensorUpdated.connect(self._on_sensor_value_updated)
        self.sensor_poller.start()

    def _on_sensor_value_updated(self, source: str, identifier: str, value: float):
        """
        Handle sensor value update from poller

        Args:
            source: Sensor source (hwinfo/wmi/psutil)
            identifier: Unique sensor identifier
            value: New value
        """
        # Update metrics model
        metrics_model = self.metrics_list.get_metrics_model()
        metrics = metrics_model.get_metrics()

        for i, metric in enumerate(metrics):
            if metric.get("source") == source:
                # Check if this is the metric to update
                if source == "hwinfo":
                    if str(metric.get("hwinfo_reading_id")) == identifier:
                        metrics_model.update_value(i, value)
                        break
                elif source == "wmi":
                    if metric.get("wmi_identifier") == identifier:
                        metrics_model.update_value(i, value)
                        break
                elif source == "psutil":
                    if metric.get("name") == identifier:
                        metrics_model.update_value(i, value)
                        break

    def _on_sensor_selected(self, sensor: dict):
        """Handle sensor double-click - add to metrics list"""
        # Add sensor to metrics list
        success = self.metrics_list.add_metric_from_sensor(sensor)

        if success:
            # Show brief confirmation in status bar
            self.status_bar.showMessage(
                f"Added: {sensor.get('display_name', '')}",
                3000  # Show for 3 seconds
            )

    def _save_and_close(self):
        """Save configuration and close"""
        # Validate settings
        is_valid, error_message = self.settings_panel.validate()
        if not is_valid:
            QMessageBox.warning(
                self,
                "Invalid Settings",
                error_message
            )
            return

        # Get settings and metrics
        settings = self.settings_panel.get_settings_dict()
        metrics = self.metrics_list.get_metrics()

        # Save configuration
        success, message = ConfigManager.save_config(
            ConfigManager.DEFAULT_CONFIG_PATH,
            settings,
            metrics
        )

        if success:
            QMessageBox.information(
                self,
                "Configuration Saved",
                f"Configuration saved successfully!\n\n"
                f"ESP32 IP: {settings['esp32_ip']}\n"
                f"UDP Port: {settings['udp_port']}\n"
                f"Update Interval: {settings['update_interval']}s\n"
                f"Metrics: {len(metrics)} selected\n\n"
                f"Saved to: {ConfigManager.DEFAULT_CONFIG_PATH}"
            )
            self.close()
        else:
            QMessageBox.warning(self, "Save Failed", message)

    def closeEvent(self, event):
        """Handle window close event"""
        # Stop sensor polling thread
        if self.sensor_poller:
            self.sensor_poller.stop()

        # Accept close event
        event.accept()
