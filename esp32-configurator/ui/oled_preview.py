"""
ESP32 Configurator - OLED Preview Widget
128x64 pixel-accurate OLED display simulation with position-based layout
"""

from PyQt6.QtWidgets import QWidget, QVBoxLayout, QGroupBox, QLabel, QHBoxLayout, QMenu
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QPainter, QColor, QPen, QAction
from typing import List, Dict

from core.font_renderer import GFXFontRenderer


class OLEDPreviewWidget(QWidget):
    """
    Widget displaying a pixel-accurate 128x64 OLED preview

    Features:
    - 128x64 pixel buffer (matches SSD1306/SH1106)
    - Adafruit GFX 5x7 font rendering
    - Position-based 2-column grid layout (0-11)
    - Row modes: 5 rows (13px) or 6 rows (10px compact)
    - Separate text and progress bar positions
    - Clock display options (center/left/right)
    - Scaled display for visibility (4x zoom)
    """

    def __init__(self, parent=None):
        super().__init__(parent)

        # OLED dimensions
        self.oled_width = 128
        self.oled_height = 64
        self.pixel_scale = 4  # 4x zoom for visibility

        # Pixel buffer [y][x] - 0=black, 1=white
        self.buffer = [[0 for _ in range(self.oled_width)] for _ in range(self.oled_height)]

        # Font renderer
        self.font_renderer = GFXFontRenderer()

        # Metrics to display
        self.metrics = []

        # Display settings (matching ESP32)
        self.row_mode = 1  # 0 = 5 rows (13px), 1 = 6 rows (10px compact)
        self.show_clock = False
        self.clock_position = 0  # 0 = Center, 1 = Left, 2 = Right
        self.clock_offset = 0  # X offset in pixels

        # Grid layout constants (matching ESP32)
        self.COL1_X = 0   # Left column X position
        self.COL2_X = 62  # Right column X position

        self._init_ui()

    def _init_ui(self):
        """Initialize the user interface"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        # Group box
        group = QGroupBox("OLED Preview (128×64)")
        group_layout = QVBoxLayout()

        # Info label
        info_layout = QHBoxLayout()
        self.info_label = QLabel(self.get_display_mode_info())
        self.info_label.setStyleSheet("color: #888888; font-size: 9pt;")
        info_layout.addWidget(self.info_label)
        info_layout.addStretch()

        self.metrics_count_label = QLabel("0 metrics | Right-click for options")
        self.metrics_count_label.setStyleSheet("color: #00d4ff; font-size: 9pt;")
        info_layout.addWidget(self.metrics_count_label)

        group_layout.addLayout(info_layout)

        # Preview canvas
        self.setMinimumSize(
            self.oled_width * self.pixel_scale + 2,
            self.oled_height * self.pixel_scale + 2
        )
        self.setMaximumSize(
            self.oled_width * self.pixel_scale + 2,
            self.oled_height * self.pixel_scale + 2
        )

        # Enable context menu
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._show_context_menu)

        group_layout.addWidget(self)
        group.setLayout(group_layout)

        layout_parent = QVBoxLayout()
        layout_parent.addWidget(group)
        self.setLayout(layout_parent)

    def paintEvent(self, event):
        """Paint the OLED display"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)

        # Draw border
        painter.setPen(QPen(QColor(0x00, 0xd4, 0xff), 1))
        painter.drawRect(0, 0, self.oled_width * self.pixel_scale + 1, self.oled_height * self.pixel_scale + 1)

        # Draw pixels
        for y in range(self.oled_height):
            for x in range(self.oled_width):
                if self.buffer[y][x]:
                    # White pixel
                    color = QColor(255, 255, 255)
                else:
                    # Black pixel (OLED background)
                    color = QColor(0, 0, 0)

                painter.fillRect(
                    x * self.pixel_scale + 1,
                    y * self.pixel_scale + 1,
                    self.pixel_scale,
                    self.pixel_scale,
                    color
                )

    def clear_buffer(self):
        """Clear the pixel buffer (all black)"""
        self.buffer = [[0 for _ in range(self.oled_width)] for _ in range(self.oled_height)]

    def set_metrics(self, metrics: List[Dict]):
        """
        Set metrics to display

        Args:
            metrics: List of metric dicts
        """
        self.metrics = metrics
        self._render_metrics()

        # Update metrics count
        visible_count = sum(1 for m in metrics if m.get("position", 255) != 255 or m.get("bar_position", 255) != 255)
        self.metrics_count_label.setText(f"{len(metrics)} metrics ({visible_count} visible)")

    def _render_metrics(self):
        """Render metrics to the buffer using position-based layout"""
        self.clear_buffer()

        if not self.metrics:
            # Show placeholder text
            self._draw_centered_text("No metrics", 28)
            self._draw_centered_text("selected", 36)
            self.update()
            return

        # Calculate row configuration (matching ESP32)
        MAX_ROWS = 5 if self.row_mode == 0 else 6

        if self.row_mode == 0:  # 5-row mode - optimized spacing
            start_y = 0
            # Use 13px spacing for readability, except with centered clock (11px to fit)
            row_height = 11 if (self.show_clock and self.clock_position == 0) else 13
        else:  # 6-row mode - compact layout
            start_y = 2
            row_height = 10

        # Draw clock if enabled
        if self.show_clock:
            clock_text = "12:34"  # Placeholder
            if self.clock_position == 0:  # Center
                x = 48 + self.clock_offset
                self.font_renderer.draw_text(self.buffer, x, start_y, clock_text, color=1)
                start_y += 10  # Clock height + gap
            elif self.clock_position == 1:  # Left column, first row
                x = self.COL1_X + self.clock_offset
                self.font_renderer.draw_text(self.buffer, x, start_y, clock_text, color=1)
            elif self.clock_position == 2:  # Right column, first row
                x = self.COL2_X + self.clock_offset
                self.font_renderer.draw_text(self.buffer, x, start_y, clock_text, color=1)

        # Render metrics in position-based grid
        for row in range(MAX_ROWS):
            y = start_y + (row * row_height)

            # Check for overflow
            if y + 8 > 64:
                break

            # Calculate position indices for this row
            left_pos = row * 2      # 0, 2, 4, 6, 8, 10
            right_pos = row * 2 + 1 # 1, 3, 5, 7, 9, 11

            # Skip first row left/right if clock is positioned there
            clock_in_left = (self.show_clock and self.clock_position == 1 and row == 0)
            clock_in_right = (self.show_clock and self.clock_position == 2 and row == 0)

            # Render left column (priority: bar first, then text)
            if not clock_in_left:
                rendered = False

                # Check for progress bar at this position
                for metric in self.metrics:
                    if metric.get("bar_position") == left_pos:
                        self._draw_progress_bar(metric, self.COL1_X, y, 60)
                        rendered = True
                        break

                # If no bar, check for text metric
                if not rendered:
                    for metric in self.metrics:
                        if metric.get("position") == left_pos:
                            self._draw_metric_text(metric, self.COL1_X, y, 60)
                            break

            # Render right column (priority: bar first, then text)
            if not clock_in_right:
                rendered = False

                # Check for progress bar at this position
                for metric in self.metrics:
                    if metric.get("bar_position") == right_pos:
                        self._draw_progress_bar(metric, self.COL2_X, y, 64)
                        rendered = True
                        break

                # If no bar, check for text metric
                if not rendered:
                    for metric in self.metrics:
                        if metric.get("position") == right_pos:
                            self._draw_metric_text(metric, self.COL2_X, y, 64)
                            break

        self.update()

    def _draw_metric_text(self, metric: Dict, x: int, y: int, width: int):
        """
        Draw metric text in compact format (matching ESP32)

        Args:
            metric: Metric dict
            x: X position
            y: Y position
            width: Available width
        """
        # Get label (custom label or display name)
        custom_label = metric.get("custom_label", "")
        display_name = metric.get("display_name", "")
        value = metric.get("current_value", 0)
        unit = metric.get("unit", "")

        # Use custom label if set, otherwise truncate display name
        if custom_label:
            label = custom_label[:10]  # Max 10 chars
        else:
            # Truncate display name to fit
            max_width = width - 30  # Leave room for value
            label = self.font_renderer.truncate_text(display_name, max_width)

        # Process label: convert '^' to spaces (matching ESP32)
        label = label.replace('^', ' ')

        # Format value
        if isinstance(value, (int, float)):
            if unit in ["C", "F"]:
                value_str = f"{int(value)}"
            elif unit == "%":
                value_str = f"{int(value)}"
            else:
                value_str = f"{value:.1f}" if value % 1 else f"{int(value)}"
        else:
            value_str = str(value)

        # Build text: "LABEL: VAL"
        text = f"{label}:{value_str}{unit}"

        # Check for companion metric
        companion_id = metric.get("companion_id", 0)
        if companion_id > 0:
            # Find companion metric
            for comp_metric in self.metrics:
                if comp_metric.get("id") == companion_id:
                    comp_value = comp_metric.get("current_value", 0)
                    comp_unit = comp_metric.get("unit", "")

                    # Format companion value
                    if isinstance(comp_value, (int, float)):
                        comp_str = f"{int(comp_value)}"
                    else:
                        comp_str = str(comp_value)

                    # Append to text
                    text += f" {comp_str}{comp_unit}"
                    break

        # Truncate if too long
        text = self.font_renderer.truncate_text(text, width)

        # Draw text
        self.font_renderer.draw_text(self.buffer, x, y, text, color=1)

    def _draw_progress_bar(self, metric: Dict, x: int, y: int, default_width: int):
        """
        Draw full-size progress bar (matching ESP32)

        Args:
            metric: Metric dict
            x: X position
            y: Y position
            default_width: Default available width (if no custom width set)
        """
        value = metric.get("current_value", 0)
        bar_min = metric.get("bar_min", 0)
        bar_max = metric.get("bar_max", 100)

        # Get custom bar width and offset (or use defaults)
        bar_width = metric.get("bar_width", default_width)
        bar_offset = metric.get("bar_offset", 0)

        # Apply offset
        x = x + bar_offset

        # Calculate fill percentage
        if bar_max > bar_min:
            percentage = (value - bar_min) / (bar_max - bar_min) * 100
            percentage = max(0, min(100, percentage))
        else:
            percentage = 0

        # Bar dimensions (7px tall with border)
        bar_height = 7

        # Draw bar outline
        for bx in range(bar_width):
            if x + bx < self.oled_width:
                self.buffer[y][x + bx] = 1
                self.buffer[y + bar_height - 1][x + bx] = 1
        for by in range(bar_height):
            if x < self.oled_width:
                self.buffer[y + by][x] = 1
            if x + bar_width - 1 < self.oled_width:
                self.buffer[y + by][x + bar_width - 1] = 1

        # Fill bar based on percentage
        fill_width = int((bar_width - 2) * percentage / 100)
        for bx in range(fill_width):
            if x + 1 + bx < self.oled_width:
                for by in range(1, bar_height - 1):
                    if y + by < self.oled_height:
                        self.buffer[y + by][x + 1 + bx] = 1

    def _draw_centered_text(self, text: str, y: int):
        """
        Draw centered text

        Args:
            text: Text to draw
            y: Y position
        """
        x = self.font_renderer.center_text_x(text, self.oled_width)
        self.font_renderer.draw_text(self.buffer, x, y, text, color=1)

    def set_row_mode(self, mode: int):
        """
        Set row display mode

        Args:
            mode: 0 = 5 rows (13px), 1 = 6 rows (10px compact)
        """
        self.row_mode = mode
        self._render_metrics()
        self._update_info_label()

    def set_clock_display(self, show: bool, position: int = 0, offset: int = 0):
        """
        Set clock display options

        Args:
            show: Show/hide clock
            position: 0 = Center, 1 = Left, 2 = Right
            offset: X offset in pixels
        """
        self.show_clock = show
        self.clock_position = position
        self.clock_offset = offset
        self._render_metrics()

    def _show_context_menu(self, pos):
        """Show context menu for display options"""
        menu = QMenu(self)

        # Row mode toggle
        row_mode_action = QAction(f"✓ {6 if self.row_mode == 1 else 5} Rows" if True else f"  {6 if self.row_mode == 1 else 5} Rows", self)
        row_mode_action.triggered.connect(lambda: self.set_row_mode(0 if self.row_mode == 1 else 1))
        menu.addAction(row_mode_action)

        # Clock display toggle
        clock_action = QAction("✓ Show Clock" if self.show_clock else "  Show Clock", self)
        clock_action.triggered.connect(lambda: self.set_clock_display(not self.show_clock, self.clock_position, self.clock_offset))
        menu.addAction(clock_action)

        menu.exec(self.mapToGlobal(pos))

    def _update_info_label(self):
        """Update info label with current display mode"""
        self.info_label.setText(self.get_display_mode_info())

    def get_display_mode_info(self) -> str:
        """Get current display mode info string"""
        rows = "5 Rows" if self.row_mode == 0 else "6 Rows"
        spacing = "13px" if self.row_mode == 0 else "10px"
        clock = "Clock On" if self.show_clock else "Clock Off"

        return f"{rows} ({spacing}) | {clock}"
