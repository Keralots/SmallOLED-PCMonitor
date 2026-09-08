"""The device portal's brand mark, drawn with PIL for the tray and the .exe.

Mirrors .brand-mark in the firmware's PORTAL_CSS: an accent-green rounded square
holding a 2x2 checker, one diagonal solid white and the other half-strength.
"""

try:
    from PIL import Image, ImageDraw
    AVAILABLE = True
except Exception:
    Image = ImageDraw = None
    AVAILABLE = False

ACCENT = (31, 138, 91)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)


def render(size=64, accent=ACCENT):
    """RGBA icon of the given edge length, or None without PIL."""
    if not AVAILABLE:
        return None

    # 4x supersample: the rounded corners and the checker gap are sub-pixel at
    # tray sizes, and PIL has no antialiased rounded rectangle.
    scale = 4
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    dc = ImageDraw.Draw(img)

    # Proportions from the CSS: 28px tile, 7px radius, 13px glyph, 5.5px cells.
    radius = round(s * 7.0 / 28.0)
    dc.rounded_rectangle((0, 0, s - 1, s - 1), radius=radius,
                         fill=tuple(accent) + (255,))

    cell = s * 5.5 / 28.0
    gap = s * 2.0 / 28.0
    glyph = cell * 2 + gap
    left = (s - glyph) / 2.0
    top = (s - glyph) / 2.0
    step = cell + gap
    for row in range(2):
        for col in range(2):
            x = left + col * step
            y = top + row * step
            alpha = 255 if row == col else 140
            dc.rectangle((round(x), round(y), round(x + cell) - 1, round(y + cell) - 1),
                         fill=(255, 255, 255, alpha))

    return img.resize((size, size), Image.LANCZOS)


def save_ico(path, sizes=ICO_SIZES):
    """Write a multi-resolution .ico for the PyInstaller build."""
    if not AVAILABLE:
        raise RuntimeError("Pillow is required to build the icon")
    base = render(max(sizes))
    base.save(path, format="ICO", sizes=[(n, n) for n in sizes])
    return path


if __name__ == "__main__":
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "win-companion", "icon.ico")
    print("wrote", save_ico(os.path.abspath(out)))
