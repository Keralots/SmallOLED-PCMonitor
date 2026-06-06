"""Pixel-accurate emulator of the OLED stats screen.

Renders a 128x64 monochrome frame that matches the firmware byte-for-byte, so the
companion-app preview shows exactly what the device shows (including the text
overlaps that happen when a left-column label is wider than 62px).

This is a direct port of src/metrics/metrics.cpp:
  displayStatsCompactGrid() + displayMetricCompact() + drawProgressBar()
plus the Adafruit_GFX classic-font text engine (write() / drawChar()): 6px advance
at size 1, 12px at size 2, transparent background (set pixels only, so overlapping
text unions), wrap-on in normal modes / clip in large modes.

The embedded font is the exact Adafruit_GFX `glcdfont` table (256 chars x 5 cols),
transcribed from the library the firmware links against.

Pure module: depends only on Pillow. No tkinter. Unit-tested by test_device_render.py.
"""

# Adafruit_GFX classic 5x7 font (glcdfont): 256 glyphs x 5 column-bytes, LSB = top row.
# Transcribed verbatim from the library the firmware links against (1280 bytes).
# Integrity is asserted at import below against the known byte count.
_FONT = bytes.fromhex(
    "00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800"
    "ffe7c3e7ff0018241800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a"
    "7f3e1c1c08081c1c3e7f14227f22145f5f005f5f06097f017f006689956a606060606094a2ffa294"
    "08047e040810207e201008082a1c08081c2a08081e101010100c1e0c1e0c30383e3830060e3e0e06"
    "000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300"
    "001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804"
    "023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109"
    "073649494936464949291e0000140000004034000000081422411414141414004122140802015909"
    "063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151"
    "737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e414141"
    "3e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f403840"
    "3f631408146303047804036159494d43007f4141410204081020004141417f0402010204404040404"
    "0000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c"
    "787f0804047800447d40002040403d007f1028440000417f40007c047804787c08040478384444443"
    "8fc1824241818242418fc7c08040408485454542404043f44243c4040207c1c2040201c3c4030403c"
    "44281028444c9090907c4464544c4400083641000000770000004136080002010204023c2623263c"
    "1ea1a161123a4040207a385454555921555579412254547842215554784020545579400c1e527212"
    "3955555559395454545939555454580000457c410002457d420001457c407d1211127df0282528f0"
    "7c545545002054547c547c0a097f4932494949323a4444443a324a4848303a4141217a3a42402078"
    "009da0a07d3d4242423d3d4040403d3c24ff2424487e4943662b2ffc2f2bff0929f620c0887e0903"
    "20545479410000447d413048484a32384040227a007a0a0a727d0d19317d2629292f282629292926"
    "30484d4020380808080808080808382f10c8acba2f102834fa00007b000008142a142222142a1408"
    "5500550055aa55aa55aaff55ff55ff000000ff00101010ff00141414ff001010ff00ff1010f010f0"
    "141414fc001414f700ff0000ff00ff1414f404fc141417101f10101f101f1414141f00101010f000"
    "0000001f101010101f10101010f010000000ff101010101010101010ff10000000ff140000ff00ff"
    "00001f10170000fc04f414141710171414f404f40000ff00f714141414141414f700f71414141714"
    "10101f101f141414f4141010f010f000001f101f0000001f14000000fc140000f010f01010ff10ff"
    "141414ff141010101f00000000f010fffffffffff0f0f0f0f0ffffff0000000000ffff0f0f0f0f0f"
    "3844443844fc4a4a4a347e02020606027e027e0263554941633844443c04407e201e2006027e0202"
    "99a5e7a5991c2a492a1c4c7201724c304a4d4d303048784830bc625a463d3e494949007e0101017e"
    "2a2a2a2a2a44445f444440514a444040444a51400000ff0103e080ff000008086b6b083612362436"
    "060f090f06000018180000001010003040ff0101001f01011e00191d1712003c3c3c3c0000000000"
)

assert len(_FONT) == 1280, "glcdfont table corrupted (expected 1280 bytes)"


class _FB:
    """A 128x64 1-bit framebuffer backed by a flat 0/255 bytearray."""

    def __init__(self, w=128, h=64):
        self.w = w
        self.h = h
        self.buf = bytearray(w * h)  # 0 = off, 255 = on

    def px(self, x, y, v=255):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.buf[y * self.w + x] = v

    def fill_rect(self, x, y, w, h, v=255):
        for yy in range(y, y + h):
            if 0 <= yy < self.h:
                row = yy * self.w
                for xx in range(x, x + w):
                    if 0 <= xx < self.w:
                        self.buf[row + xx] = v

    def draw_rect(self, x, y, w, h, v=255):
        for xx in range(x, x + w):
            self.px(xx, y, v)
            self.px(xx, y + h - 1, v)
        for yy in range(y, y + h):
            self.px(x, yy, v)
            self.px(x + w - 1, yy, v)

    def to_image(self):
        from PIL import Image
        return Image.frombytes("L", (self.w, self.h), bytes(self.buf))


def _draw_char(fb, x, y, ch, size):
    """Adafruit_GFX drawChar for the classic font (transparent background)."""
    o = ord(ch)
    if o > 255:
        o = ord("?")
    # Skip glyphs fully off-screen (matches Adafruit's early-out).
    if x >= fb.w or y >= fb.h or (x + 6 * size - 1) < 0 or (y + 8 * size - 1) < 0:
        return
    base = o * 5
    for col in range(5):
        line = _FONT[base + col]
        for row in range(8):
            if (line >> row) & 1:
                if size == 1:
                    fb.px(x + col, y + row, 255)
                else:
                    fb.fill_rect(x + col * size, y + row * size, size, size, 255)
    # 6th column is the inter-character gap (blank) - nothing to draw.


def _write(fb, x, y, text, size, wrap):
    """Adafruit_GFX write(): advance 6*size per char; wrap resets cursor_x to 0.

    Returns the final (cursor_x, cursor_y) so callers can right-align companions.
    """
    cx, cy = x, y
    for ch in text:
        if ch == "\n":
            cx = 0
            cy += size * 8
            continue
        if ch == "\r":
            continue
        if wrap and (cx + size * 6) > fb.w:
            cx = 0
            cy += size * 8
        _draw_char(fb, cx, cy, ch, size)
        cx += size * 6
    return cx, cy


def build_metric_text(label, unit, value, rpm_k=False, net_mb=False):
    """Port of displayMetricCompact's text assembly (primary metric only).

    label: device label (custom label or name). value: int (KB/s pre-multiplied
    x10, as sent over UDP). Returns the exact string the firmware prints.
    """
    display_label = (label or "").replace("^", " ")  # convertCaretToSpaces
    stripped = display_label.rstrip(" ")
    trailing = len(display_label) - len(stripped)
    display_label = stripped
    if display_label.endswith("%"):
        display_label = display_label[:-1]
    spaces = " " * min(trailing, 10)

    if rpm_k and unit == "RPM" and value >= 1000:
        return "%s:%s%.1fK" % (display_label, spaces, value / 1000.0)
    if unit == "KB/s":
        actual = value / 10.0
        if net_mb:
            return "%s:%s%.1fM" % (display_label, spaces, actual / 1000.0)
        return "%s:%s%.1f%s" % (display_label, spaces, actual, unit)
    return "%s:%s%d%s" % (display_label, spaces, value, unit)


def build_companion_text(unit, value, net_mb=False):
    """Port of the companion snippet (leading space, value+unit only)."""
    if unit == "KB/s":
        cv = value / 10.0
        if net_mb:
            return " %.1fM" % (cv / 1000.0)
        return " %.1f%s" % (cv, unit)
    return " %d%s" % (value, unit)


def text_pixel_width(text, size=1):
    """Width in device pixels of a printed string (6px/char advance)."""
    return len(text) * 6 * size


def render_stats_frame(metrics_by_id, layout, row_mode, show_clock=False,
                       clock_position=0, clock_offset=0, rpm_k=False,
                       net_mb=False, timestamp="12:34"):
    """Render the stats screen exactly as the firmware would.

    metrics_by_id: {id: {"label"/"name", "unit", "value"(int)}}.
    layout:        {id: {"position","companionId","barPosition","barMin","barMax",
                          "barWidth","barOffsetX","label"(optional override)}}.
    Returns a PIL "L" image (128x64, 0/255). Caller scales for display.
    """
    fb = _FB()
    is_large = row_mode >= 2
    text_height = 16 if is_large else 8

    def meta(mid):
        m = metrics_by_id.get(mid, {})
        e = layout.get(mid, {})
        label = e.get("label") or m.get("label") or m.get("name") or ""
        return label, m.get("unit", ""), int(m.get("value", 0) or 0)

    def draw_bar(x, y, e, value, unit):
        actual_x = x + e.get("barOffsetX", 0)
        actual_w = e.get("barWidth", 60)
        if actual_x >= 128 or actual_x < 0:
            return
        if actual_x + actual_w > 128:
            actual_w = 128 - actual_x
        if actual_w <= 0:
            return
        bmin = e.get("barMin", 0)
        bmax = e.get("barMax", 100)
        rng = bmax - bmin
        if rng <= 0:
            rng = 100
        dv = value // 10 if unit == "KB/s" else value
        vir = max(bmin, min(dv, bmax)) - bmin
        fill_w = (vir * (actual_w - 2)) // rng
        bar_h = 16 if is_large else 8
        fb.draw_rect(actual_x, y, actual_w, bar_h, 255)
        if fill_w > 0:
            fb.fill_rect(actual_x + 1, y + 1, fill_w, bar_h - 2, 255)

    def render_text_metric(x, y, e, size, wrap, large):
        label, unit, val = meta(_id_of(e))
        text = build_metric_text(label, unit, val, rpm_k, net_mb)
        comp_id = e.get("companionId", 0)
        has_comp = comp_id and comp_id in metrics_by_id
        if has_comp and not large:
            _, cunit, cval = meta(comp_id)
            text += build_companion_text(cunit, cval, net_mb)
        cx, cy = _write(fb, x, y, text, size, wrap)
        if has_comp and large:
            _, cunit, cval = meta(comp_id)
            comp = build_companion_text(cunit, cval, net_mb)[1:]  # drop leading space
            comp_x = 128 - len(comp) * 12
            if comp_x < cx + 4:
                comp_x = cx + 4
            _write(fb, comp_x, y, comp, size, wrap)

    # map a layout entry back to its id (entries are unique per id)
    _entry_id = {id(e): mid for mid, e in layout.items()}

    def _id_of(e):
        return _entry_id.get(id(e))

    def slot_text(pos):
        for mid, e in layout.items():
            if e.get("position", 255) == pos:
                return e
        return None

    def slot_bar(pos):
        for mid, e in layout.items():
            if e.get("barPosition", 255) == pos:
                return mid, e
        return None

    if is_large:
        max_rows = 2 if row_mode == 2 else 3
        start_y = 8 if row_mode == 2 else 4
        if show_clock:
            _write(fb, 48 + clock_offset, 0, timestamp, 1, False)
            start_y = 12 if row_mode == 2 else 10
        row_h = 32 if row_mode == 2 else (18 if show_clock else 20)
        for row in range(max_rows):
            y = start_y + row * row_h
            if y + text_height > 64:
                break
            pos = row
            bar = slot_bar(pos)
            if bar is not None:
                mid, e = bar
                _, unit, val = meta(mid)
                draw_bar(0, y, e, val, unit)
                continue
            e = slot_text(pos)
            if e is not None:
                render_text_metric(0, y, e, 2, False, True)
    else:
        COL1_X, COL2_X = 0, 62
        max_rows = 5 if row_mode == 0 else 6
        if row_mode == 0:
            start_y = 0
            row_h = 11 if (show_clock and clock_position == 0) else 13
        else:
            start_y = 2
            row_h = 10
        if show_clock:
            if clock_position == 0:
                _write(fb, 48 + clock_offset, start_y, timestamp, 1, True)
                start_y += 10
            elif clock_position == 1:
                _write(fb, COL1_X + clock_offset, start_y, timestamp, 1, True)
            elif clock_position == 2:
                _write(fb, COL2_X + clock_offset, start_y, timestamp, 1, True)
        for row in range(max_rows):
            y = start_y + row * row_h
            if y + 8 > 64:
                break
            left_pos, right_pos = row * 2, row * 2 + 1
            clock_left = show_clock and clock_position == 1 and row == 0
            clock_right = show_clock and clock_position == 2 and row == 0
            for col_x, pos, blocked in ((COL1_X, left_pos, clock_left),
                                        (COL2_X, right_pos, clock_right)):
                if blocked:
                    continue
                bar = slot_bar(pos)
                if bar is not None:
                    mid, e = bar
                    _, unit, val = meta(mid)
                    draw_bar(col_x, y, e, val, unit)
                    continue
                e = slot_text(pos)
                if e is not None:
                    render_text_metric(col_x, y, e, 1, True, False)

    return fb.to_image()
