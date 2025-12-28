"""
Adafruit GFX Font Renderer
Renders text using 5x7 bitmap font exactly like ESP32 Adafruit_GFX
"""

from typing import Tuple
from resources.adafruit_gfx_font import FONT_5X7, CHAR_WIDTH, CHAR_HEIGHT, CHAR_SPACING


class GFXFontRenderer:
    """
    Renders text using Adafruit GFX 5x7 bitmap font

    Matches the behavior of Adafruit_GFX library on ESP32
    """

    def __init__(self):
        self.char_width = CHAR_WIDTH
        self.char_height = CHAR_HEIGHT
        self.char_spacing = CHAR_SPACING

    def get_text_width(self, text: str) -> int:
        """
        Calculate pixel width of text string

        Args:
            text: Text to measure

        Returns:
            Width in pixels
        """
        if not text:
            return 0

        # Each character is CHAR_WIDTH + CHAR_SPACING, except last char
        return len(text) * (self.char_width + self.char_spacing) - self.char_spacing

    def get_text_height(self) -> int:
        """
        Get text height (always 7 pixels for this font)

        Returns:
            Height in pixels
        """
        return self.char_height

    def draw_char(self, buffer: list, x: int, y: int, char: str, color: int = 1):
        """
        Draw a single character to the buffer

        Args:
            buffer: 2D list [y][x] representing pixel buffer
            x: X position (left edge)
            y: Y position (top edge)
            char: Single character to draw
            color: Pixel color (1=white, 0=black)
        """
        # Get ASCII code
        ascii_code = ord(char)

        # Get font data (use space if character not in font)
        char_data = FONT_5X7.get(ascii_code, FONT_5X7[0x20])

        # Draw each column
        for col in range(self.char_width):
            column_data = char_data[col]

            # Draw each pixel in column
            for row in range(self.char_height):
                # Check if bit is set
                if column_data & (1 << row):
                    # Calculate pixel position
                    px = x + col
                    py = y + row

                    # Draw pixel if within bounds
                    if 0 <= px < len(buffer[0]) and 0 <= py < len(buffer):
                        buffer[py][px] = color

    def draw_text(self, buffer: list, x: int, y: int, text: str, color: int = 1):
        """
        Draw text string to the buffer

        Args:
            buffer: 2D list [y][x] representing pixel buffer
            x: X position (left edge)
            y: Y position (top edge)
            text: Text to draw
            color: Pixel color (1=white, 0=black)
        """
        current_x = x

        for char in text:
            # Draw character
            self.draw_char(buffer, current_x, y, char, color)

            # Move to next character position
            current_x += self.char_width + self.char_spacing

    def truncate_text(self, text: str, max_width: int) -> str:
        """
        Truncate text to fit within max_width pixels

        Args:
            text: Text to truncate
            max_width: Maximum width in pixels

        Returns:
            Truncated text (with "..." if truncated)
        """
        if self.get_text_width(text) <= max_width:
            return text

        # Try with ellipsis
        ellipsis = "..."
        ellipsis_width = self.get_text_width(ellipsis)

        if max_width < ellipsis_width:
            # Not enough space even for ellipsis
            return ""

        # Find how many characters fit
        available_width = max_width - ellipsis_width
        char_count = 0

        for i in range(len(text)):
            width = self.get_text_width(text[:i+1])
            if width <= available_width:
                char_count = i + 1
            else:
                break

        return text[:char_count] + ellipsis

    def wrap_text(self, text: str, max_width: int) -> list:
        """
        Wrap text to multiple lines

        Args:
            text: Text to wrap
            max_width: Maximum width in pixels per line

        Returns:
            List of text lines
        """
        words = text.split(' ')
        lines = []
        current_line = ""

        for word in words:
            # Try adding word to current line
            test_line = current_line + (" " if current_line else "") + word
            if self.get_text_width(test_line) <= max_width:
                current_line = test_line
            else:
                # Start new line
                if current_line:
                    lines.append(current_line)
                current_line = word

        # Add last line
        if current_line:
            lines.append(current_line)

        return lines

    def center_text_x(self, text: str, display_width: int) -> int:
        """
        Calculate X position to center text horizontally

        Args:
            text: Text to center
            display_width: Display width in pixels

        Returns:
            X position for centered text
        """
        text_width = self.get_text_width(text)
        return max(0, (display_width - text_width) // 2)
