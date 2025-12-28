"""
ESP32 OLED Configurator
Professional PyQt6 GUI for SmallOLED-PCMonitor ESP32 displays

Features:
- Drag-and-drop sensor assignment
- Live OLED preview (128×64 pixel-accurate)
- Real-time sensor value updates (1Hz background thread)
- 200+ sensor support (HWiNFO64/LibreHardwareMonitor)
- Coexists with pc_stats_monitor_hwinfo.py

Author: SmallOLED-PCMonitor Project
License: MIT
Version: 1.0.0 (Phase 1 - Skeleton)
"""

import sys
import os
from pathlib import Path

from PyQt6.QtWidgets import QApplication, QMessageBox
from PyQt6.QtCore import Qt

from ui.main_window import MainWindow


def load_stylesheet(app: QApplication) -> bool:
    """
    Load the dark theme stylesheet

    Args:
        app: QApplication instance

    Returns:
        True if stylesheet loaded successfully, False otherwise
    """
    try:
        # Get the directory containing this script
        script_dir = Path(__file__).parent

        # Path to stylesheet
        qss_path = script_dir / "resources" / "styles" / "dark_theme.qss"

        if not qss_path.exists():
            print(f"Warning: Stylesheet not found at {qss_path}")
            return False

        # Read and apply stylesheet
        with open(qss_path, 'r', encoding='utf-8') as f:
            stylesheet = f.read()

        app.setStyleSheet(stylesheet)
        print(f"[OK] Loaded dark theme from {qss_path}")
        return True

    except Exception as e:
        print(f"Error loading stylesheet: {e}")
        return False


def check_dependencies() -> tuple[bool, str]:
    """
    Check if all required dependencies are installed

    Returns:
        Tuple of (success, error_message)
    """
    missing = []

    # Check PyQt6
    try:
        import PyQt6
    except ImportError:
        missing.append("PyQt6 (pip install PyQt6)")

    # Check psutil
    try:
        import psutil
    except ImportError:
        missing.append("psutil (pip install psutil)")

    # Check pywin32
    try:
        import win32event
        import win32api
    except ImportError:
        missing.append("pywin32 (pip install pywin32)")

    # Check wmi
    try:
        import wmi
    except ImportError:
        missing.append("wmi (pip install wmi)")

    if missing:
        error_msg = (
            "Missing required dependencies:\n\n"
            + "\n".join(f"  - {dep}" for dep in missing)
            + "\n\nPlease install them with:\n"
            + "  pip install -r requirements.txt"
        )
        return False, error_msg

    return True, ""


def main():
    """Main application entry point"""
    print("=" * 70)
    print("  ESP32 OLED CONFIGURATOR")
    print("  Professional PyQt6 GUI for SmallOLED-PCMonitor")
    print("  Version 1.0.0 (Phase 1 - Skeleton)")
    print("=" * 70)
    print()

    # Check dependencies
    print("[1/3] Checking dependencies...")
    deps_ok, error_msg = check_dependencies()
    if not deps_ok:
        print("[ERROR] Dependencies check failed!")
        print(error_msg)
        sys.exit(1)
    print("[OK] All dependencies installed")

    # Create QApplication
    print("\n[2/3] Initializing Qt application...")
    app = QApplication(sys.argv)
    app.setApplicationName("ESP32 Configurator")
    app.setOrganizationName("SmallOLED-PCMonitor")
    print("[OK] Qt application initialized")

    # Load dark theme
    print("\n[3/3] Loading dark theme...")
    if not load_stylesheet(app):
        print("[WARNING] Running with default theme (dark theme not loaded)")

    # Create and show main window
    print("\n[OK] Launching main window...")
    print()

    window = MainWindow()
    window.show()

    # Run application event loop
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
