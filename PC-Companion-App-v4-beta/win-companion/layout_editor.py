"""Drag-and-drop layout editor dialog for the PC Companion App.

Tkinter modal dialog to manually edit the device layout. Hosts the template
picker, the schematic drag/drop grid, a pixel-exact live 1:1 device preview
(device_render), inline rename, per-bar width/offset controls, an "OK" that
pushes the layout config to the device (window stays open), and a "Pull from
device" that loads the device's current working config back in to edit.

The editor never streams value frames to the OLED - it only POSTs the layout
config once on OK; the main monitor loop owns value streaming. That is what
keeps a transient sensor read from flashing an API error on the device.

Depends on layout_engine for layout logic and device_render for the 1:1 preview.
Live sensor values (for the in-dialog preview only) are provided by the caller
via `device_session` (a callable) so this module does not import the main app
(avoids a cycle).
"""
import json
import threading
from datetime import datetime

import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from PIL import Image, ImageTk

import device_render
from layout_engine import (
    ROWMODE_5x2, ROWMODE_6x2, ROWMODE_LARGE2, ROWMODE_LARGE3,
    slot_count, slot_geometry, clock_blocked_slot,
    auto_layout, build_device_layout_json, push_layout_to_device,
    fetch_device_export, parse_device_layout,
    LAYOUT_TEMPLATES, _bar_bounds, _ROWMODE_LABELS,
)


class LayoutEditorDialog:
    """Modal dialog: template picker + drag/drop grid + live 1:1 preview + push."""

    SCALE = 3                 # schematic editor grid scale
    CANVAS_W = 128 * SCALE    # 384
    CANVAS_H = 64 * SCALE     # 192
    PSCALE = 3                # live 1:1 preview scale
    PREVIEW_W = 128 * PSCALE  # 384
    PREVIEW_H = 64 * PSCALE   # 192

    def __init__(self, parent, metrics, row_mode, layout_by_id, template_key,
                 show_clock=False, clock_position=0, device_session=None, fmt=None,
                 on_save=None):
        self.result = None
        self.metrics = metrics
        self.metrics_by_id = {m["id"]: m for m in metrics}
        self.row_mode = row_mode
        self.layout = layout_by_id
        self.template_key = template_key
        self.show_clock = show_clock
        self.clock_position = clock_position
        self.selected_id = None
        self._drag_data = None
        # Callback(result_dict) -> bool: persist the layout to disk (Save button).
        self._on_save_cb = on_save

        # Live preview / device-push state.
        # device_session: {"collect": callable(last_good)->(payload,values,last_good)|None,
        #                  "esp_ip": str, "udp_port": int}  or None when no device.
        self.device_session = device_session
        if device_session is not None:
            self._ds_ip = device_session.get("esp_ip")
            self._ds_port = device_session.get("udp_port", 4210)
        fmt = fmt or {}
        self.clock_offset = int(fmt.get("clock_offset", 0))
        self._init_rpm_k = bool(fmt.get("rpm_k", False))
        self._init_net_mb = bool(fmt.get("net_mb", False))
        self._live_values = {}
        self._last_good = {}
        self._live_photo = None
        self._rename_entry = None
        self._stop_event = threading.Event()
        self._worker = None

        self.win = tk.Toplevel(parent)
        self.win.title("Customize Layout")
        self.win.configure(bg="#1e1e1e")
        self.win.transient(parent)
        self.win.grab_set()
        self.win.resizable(True, True)
        self.win.minsize(780, 740)

        self._build_ui()
        self._redraw()

        self.win.protocol("WM_DELETE_WINDOW", self._on_close)
        self.win.bind("<F2>", lambda _e: self._start_rename())

        if self.device_session is not None:
            self._start_worker()

        self.win.wait_window()

    # ---- UI construction ----

    def _build_ui(self):
        top = tk.Frame(self.win, bg="#1e1e1e")
        top.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)

        left = tk.Frame(top, bg="#1e1e1e")
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # Template picker (moved here from the main window).
        tpl = tk.Frame(left, bg="#1e1e1e")
        tpl.pack(fill=tk.X, padx=4, pady=(4, 2))
        tk.Label(tpl, text="Template (reset):", bg="#1e1e1e", fg="#ffffff",
                 font=("Arial", 10)).pack(side=tk.LEFT, padx=(0, 6))
        self._tpl_labels = [label for _k, label in LAYOUT_TEMPLATES]
        self._tpl_key_by_label = {label: k for k, label in LAYOUT_TEMPLATES}
        self._tpl_label_by_key = {k: label for k, label in LAYOUT_TEMPLATES}
        self.template_var = tk.StringVar(
            value=self._tpl_label_by_key.get(self.template_key, self._tpl_labels[0]))
        tpl_combo = ttk.Combobox(tpl, textvariable=self.template_var, state="readonly",
                                 values=self._tpl_labels, width=22)
        tpl_combo.pack(side=tk.LEFT)
        tpl_combo.bind("<<ComboboxSelected>>", lambda _e: self._on_template_change())

        # Schematic editor grid (drag/drop).
        hint_row = tk.Frame(left, bg="#1e1e1e")
        hint_row.pack(fill=tk.X, padx=4)
        tk.Label(hint_row, text="Drag to arrange (double-click = bar, Shift-click = companion, F2 = rename)",
                 bg="#1e1e1e", fg="#888888", font=("Arial", 8)).pack(side=tk.LEFT)
        self._help_btn = tk.Button(hint_row, text="How to use ▾", command=self._toggle_help,
                                   bg="#37474f", fg="#ffffff", font=("Arial", 8),
                                   relief=tk.FLAT, padx=6, pady=0)
        self._help_btn.pack(side=tk.RIGHT)
        self._help_text = (
            "How to use:\n"
            "• Drag a metric from the palette below onto a row to place it.\n"
            "• Drag a placed metric to move it; drag it off the grid to remove it.\n"
            "• Double-click a metric = add/remove its progress bar.\n"
            "• Shift-click another metric = make it a companion (shares the row).\n"
            "• F2 or the Name box = rename a metric (max 10 chars).\n"
            "• Bar Width / Offset = size the bar and shift it right, in pixels.\n"
            "• Pull from device = load the layout already on the device to edit it.\n"
            "• Export/Import file = back up the layout to a file and restore it later.\n"
            "• Save to device = push the layout to the device (window stays open).\n"
            "• Save = store the layout in the app.  Close = exit."
        )
        self._help_label = tk.Label(left, text=self._help_text, bg="#23272e", fg="#cfd8dc",
                                    font=("Arial", 8), justify=tk.LEFT, anchor="w")
        self._help_visible = False
        cf = tk.Frame(left, bg="#1e1e1e")
        cf.pack(fill=tk.X, padx=4, pady=2)
        self.canvas = tk.Canvas(
            cf, width=self.CANVAS_W, height=self.CANVAS_H,
            bg="#000000", highlightthickness=1, highlightbackground="#555555"
        )
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self._on_canvas_click)
        self.canvas.bind("<Double-Button-1>", self._on_canvas_dblclick)
        self.canvas.bind("<B1-Motion>", self._on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_canvas_release)
        self.canvas.bind("<Shift-Button-1>", self._on_canvas_shift_click)

        # Palette
        pf = tk.LabelFrame(left, text=" Unplaced metrics (drag onto the grid) ",
                           bg="#2d2d2d", fg="#aaaaaa", font=("Arial", 9))
        pf.pack(fill=tk.X, padx=4, pady=(2, 4))
        self.palette_frame = tk.Frame(pf, bg="#2d2d2d")
        self.palette_frame.pack(fill=tk.X, padx=4, pady=4)

        # Live 1:1 device preview (pixel-exact, live values).
        prev = tk.LabelFrame(left, text=" Live device preview (1:1, real values) ",
                             bg="#2d2d2d", fg="#00d4ff", font=("Arial", 9))
        prev.pack(fill=tk.X, padx=4, pady=(0, 4))
        self.preview_canvas = tk.Canvas(
            prev, width=self.PREVIEW_W, height=self.PREVIEW_H,
            bg="#000000", highlightthickness=1, highlightbackground="#555555"
        )
        self.preview_canvas.pack(padx=4, pady=4)
        self.live_status = tk.Label(prev, text="", bg="#2d2d2d", fg="#888888",
                                    font=("Arial", 8))
        self.live_status.pack(anchor="w", padx=6, pady=(0, 2))

        # Right panel
        right = tk.Frame(top, bg="#2d2d2d", width=240)
        right.pack(side=tk.RIGHT, fill=tk.Y, padx=(4, 0), pady=4)
        right.pack_propagate(False)

        # Row mode
        rm_frame = tk.Frame(right, bg="#2d2d2d")
        rm_frame.pack(fill=tk.X, padx=8, pady=(8, 4))
        tk.Label(rm_frame, text="Row mode:", bg="#2d2d2d", fg="#ffffff",
                 font=("Arial", 10)).pack(anchor="w")
        self._rm_labels = [lbl for _, lbl in _ROWMODE_LABELS]
        self._rm_by_label = {lbl: rm for rm, lbl in _ROWMODE_LABELS}
        self._rm_label_by_mode = {rm: lbl for rm, lbl in _ROWMODE_LABELS}
        self.rm_var = tk.StringVar(value=self._rm_label_by_mode.get(self.row_mode, self._rm_labels[0]))
        rm_combo = ttk.Combobox(right, textvariable=self.rm_var, state="readonly",
                                values=self._rm_labels, width=18)
        rm_combo.pack(padx=8, anchor="w")
        rm_combo.bind("<<ComboboxSelected>>", lambda _: self._on_row_mode_change())

        # Clock toggle
        clk_frame = tk.Frame(right, bg="#2d2d2d")
        clk_frame.pack(fill=tk.X, padx=8, pady=(6, 0))
        self.clock_var = tk.IntVar(value=1 if self.show_clock else 0)
        tk.Checkbutton(clk_frame, text="Show clock", variable=self.clock_var,
                       bg="#2d2d2d", fg="#ffffff", selectcolor="#444444",
                       activebackground="#2d2d2d", font=("Arial", 10),
                       command=self._on_clock_toggle).pack(anchor="w")
        _CLK_POS_LABELS = ["Center", "Left slot", "Right slot"]
        self.clk_pos_var = tk.StringVar(value=_CLK_POS_LABELS[self.clock_position])
        self.clk_pos_combo = ttk.Combobox(right, textvariable=self.clk_pos_var,
                                          state="readonly", values=_CLK_POS_LABELS, width=12)
        self.clk_pos_combo.pack(padx=8, anchor="w")
        self.clk_pos_combo.bind("<<ComboboxSelected>>", lambda _: self._on_clock_pos_change())
        self._CLK_POS_LABELS = _CLK_POS_LABELS

        # Clock X offset (pixels), same idea as the device web UI's clockOffset.
        off_row = tk.Frame(right, bg="#2d2d2d")
        off_row.pack(fill=tk.X, padx=8, anchor="w", pady=(2, 0))
        tk.Label(off_row, text="Clock offset (px):", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).pack(side=tk.LEFT)
        self.clk_off_var = tk.StringVar(value=str(self.clock_offset))
        self.clk_off_spin = tk.Spinbox(off_row, from_=-64, to=64, width=5,
                                       textvariable=self.clk_off_var,
                                       bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff",
                                       buttonbackground="#3a3a3a",
                                       command=self._on_clock_offset_change)
        self.clk_off_spin.pack(side=tk.LEFT, padx=4)
        self.clk_off_spin.bind("<KeyRelease>", lambda _e: self._on_clock_offset_change())
        if not self.show_clock:
            self.clk_pos_combo.config(state=tk.DISABLED)
            self.clk_off_spin.config(state=tk.DISABLED)

        # Display-format flags (affect both the preview and the device).
        self.rpm_k_var = tk.IntVar(value=1 if self._init_rpm_k else 0)
        tk.Checkbutton(right, text="RPM in K (1.2K)", variable=self.rpm_k_var,
                       bg="#2d2d2d", fg="#ffffff", selectcolor="#444444",
                       activebackground="#2d2d2d", font=("Arial", 9),
                       command=self._render_live_preview).pack(anchor="w", padx=8, pady=(6, 0))
        self.net_mb_var = tk.IntVar(value=1 if self._init_net_mb else 0)
        tk.Checkbutton(right, text="Network in MB/s", variable=self.net_mb_var,
                       bg="#2d2d2d", fg="#ffffff", selectcolor="#444444",
                       activebackground="#2d2d2d", font=("Arial", 9),
                       command=self._render_live_preview).pack(anchor="w", padx=8)

        tk.Frame(right, bg="#444444", height=1).pack(fill=tk.X, padx=8, pady=8)

        # Selected-metric detail panel
        self.detail_frame = tk.Frame(right, bg="#2d2d2d")
        self.detail_frame.pack(fill=tk.X, padx=8)
        self.detail_name_label = tk.Label(self.detail_frame, text="(click a metric)",
                                          bg="#2d2d2d", fg="#00d4ff",
                                          font=("Arial", 11, "bold"), anchor="w")
        self.detail_name_label.pack(fill=tk.X, pady=(0, 4))

        # Inline name edit - applies live as you type (also F2 on the grid).
        name_row = tk.Frame(self.detail_frame, bg="#2d2d2d")
        name_row.pack(fill=tk.X, pady=(0, 4))
        tk.Label(name_row, text="Name:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).pack(side=tk.LEFT)
        self.name_var = tk.StringVar(value="")
        name_entry = tk.Entry(name_row, textvariable=self.name_var, width=12,
                              bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff")
        name_entry.pack(side=tk.LEFT, padx=4)
        name_entry.bind("<KeyRelease>", lambda _e: self._commit_name_entry())
        name_entry.bind("<Return>", lambda _e: self._commit_name_entry())

        # Progress bar: choose the exact slot (row) to put the bar in.
        bar_row = tk.Frame(self.detail_frame, bg="#2d2d2d")
        bar_row.pack(fill=tk.X, pady=(4, 2))
        tk.Label(bar_row, text="Bar in slot:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).pack(side=tk.LEFT)
        self.bar_slot_var = tk.StringVar(value="None")
        self.bar_slot_combo = ttk.Combobox(bar_row, textvariable=self.bar_slot_var,
                                            state="readonly", width=13)
        self.bar_slot_combo.pack(side=tk.LEFT, padx=4)
        self.bar_slot_combo.bind("<<ComboboxSelected>>", lambda _: self._on_bar_slot_change())

        bar_opts = tk.Frame(self.detail_frame, bg="#2d2d2d")
        bar_opts.pack(fill=tk.X, pady=(2, 4))
        tk.Label(bar_opts, text="Min:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).grid(row=0, column=0, sticky="w")
        self.bar_min_var = tk.StringVar(value="0")
        min_e = tk.Entry(bar_opts, textvariable=self.bar_min_var, width=5,
                         bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff")
        min_e.grid(row=0, column=1, padx=4)
        min_e.bind("<KeyRelease>", lambda _e: self._on_bar_minmax_change())
        tk.Label(bar_opts, text="Max:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).grid(row=0, column=2, sticky="w")
        self.bar_max_var = tk.StringVar(value="100")
        max_e = tk.Entry(bar_opts, textvariable=self.bar_max_var, width=5,
                         bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff")
        max_e.grid(row=0, column=3, padx=4)
        max_e.bind("<KeyRelease>", lambda _e: self._on_bar_minmax_change())

        # Bar size in pixels: Width (length) + Offset (shift right from the slot's
        # left edge), mirroring the device web UI's barWidth / barOffsetX fields.
        tk.Label(bar_opts, text="Width:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).grid(row=1, column=0, sticky="w", pady=(4, 0))
        self.bar_width_var = tk.StringVar(value="60")
        w_e = tk.Entry(bar_opts, textvariable=self.bar_width_var, width=5,
                       bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff")
        w_e.grid(row=1, column=1, padx=4, pady=(4, 0))
        w_e.bind("<KeyRelease>", lambda _e: self._on_bar_size_change())
        tk.Label(bar_opts, text="Offset:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).grid(row=1, column=2, sticky="w", pady=(4, 0))
        self.bar_off_var = tk.StringVar(value="0")
        o_e = tk.Entry(bar_opts, textvariable=self.bar_off_var, width=5,
                       bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff")
        o_e.grid(row=1, column=3, padx=4, pady=(4, 0))
        o_e.bind("<KeyRelease>", lambda _e: self._on_bar_size_change())

        tk.Frame(self.detail_frame, bg="#444444", height=1).pack(fill=tk.X, pady=6)
        tk.Label(self.detail_frame, text="Companion:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 10)).pack(anchor="w")
        self.companion_var = tk.StringVar(value="None")
        self.companion_combo = ttk.Combobox(self.detail_frame, textvariable=self.companion_var,
                                            state="readonly", width=18)
        self.companion_combo.pack(anchor="w", pady=(2, 0))
        self.companion_combo.bind("<<ComboboxSelected>>", lambda _: self._on_companion_change())

        self.detail_frame.pack_forget()

        # Bottom buttons
        tk.Frame(right, bg="#2d2d2d").pack(fill=tk.BOTH, expand=True)
        btn_frame = tk.Frame(right, bg="#2d2d2d")
        btn_frame.pack(fill=tk.X, padx=8, pady=8)

        # Pull the device's working config in to edit it (device is source of truth).
        self.pull_btn = tk.Button(btn_frame, text="Pull from device",
                                  command=self._on_pull, bg="#37474f", fg="#ffffff",
                                  font=("Arial", 10), relief=tk.FLAT, pady=3)
        self.pull_btn.pack(fill=tk.X, pady=(0, 4))
        # Backup / restore the layout to a JSON file (selection-independent: binds
        # by name on import, same format the device /api/import accepts).
        io_row = tk.Frame(btn_frame, bg="#2d2d2d")
        io_row.pack(fill=tk.X, pady=(0, 4))
        tk.Button(io_row, text="Export file", command=self._on_export_file,
                  bg="#37474f", fg="#ffffff", font=("Arial", 9),
                  relief=tk.FLAT, pady=3).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 2))
        tk.Button(io_row, text="Import file", command=self._on_import_file,
                  bg="#37474f", fg="#ffffff", font=("Arial", 9),
                  relief=tk.FLAT, pady=3).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(2, 0))
        tk.Button(btn_frame, text="Reset to template", command=self._on_reset,
                  bg="#555555", fg="#ffffff", font=("Arial", 10),
                  relief=tk.FLAT, padx=8, pady=3).pack(fill=tk.X, pady=(0, 4))
        # Wipe the device's stored layout (incl. stale bindings) and push a fresh
        # auto-layout from the current metrics.
        self.reset_dev_btn = tk.Button(btn_frame, text="Reset device layout",
                                       command=self._on_reset_device,
                                       bg="#6d4c41", fg="#ffffff", font=("Arial", 10),
                                       relief=tk.FLAT, padx=8, pady=3)
        self.reset_dev_btn.pack(fill=tk.X, pady=(0, 8))

        # "Save to device" pushes the layout to the device and KEEPS the window
        # open, so editing never streams value frames to it (no error flash).
        self.push_btn = tk.Button(btn_frame, text="Save to device", command=self._on_ok,
                                  bg="#2e7d32", fg="#ffffff", font=("Arial", 11, "bold"),
                                  relief=tk.FLAT, pady=4)
        self.push_btn.pack(fill=tk.X, pady=(0, 4))
        # Save persists the layout to the app's config so it survives a restart.
        self.save_btn = tk.Button(btn_frame, text="Save", command=self._on_save,
                                  bg="#0277bd", fg="#ffffff", font=("Arial", 10, "bold"),
                                  relief=tk.FLAT, pady=3)
        self.save_btn.pack(fill=tk.X, pady=(0, 4))
        tk.Button(btn_frame, text="Close", command=self._on_close,
                  bg="#455a64", fg="#ffffff", font=("Arial", 10, "bold"),
                  relief=tk.FLAT, pady=3).pack(fill=tk.X)

        if self.device_session is None:
            self.pull_btn.config(state=tk.DISABLED)
            self.reset_dev_btn.config(state=tk.DISABLED)
        if self._on_save_cb is None:
            self.save_btn.config(state=tk.DISABLED)

    # ---- Drawing ----

    def _redraw(self):
        self._draw_canvas()
        self._draw_palette()
        self._update_detail_panel()
        self._render_live_preview()

    def _refresh_views(self):
        """Redraw grid + palette + 1:1 preview WITHOUT rebuilding the detail-panel
        entries, so live typing in Name/Min/Max keeps focus and cursor."""
        self._draw_canvas()
        self._draw_palette()
        self._render_live_preview()

    def _geo(self, slot):
        return slot_geometry(self.row_mode, slot, self.show_clock, self.clock_position)

    def _draw_canvas(self):
        c = self.canvas
        c.delete("all")
        S = self.SCALE
        is_large = self.row_mode >= 2
        n_slots = slot_count(self.row_mode)
        blocked = clock_blocked_slot(self.row_mode, self.clock_position) if self.show_clock else None

        # Draw clock indicator
        if self.show_clock:
            if self.clock_position == 0:
                if is_large:
                    cy = 0
                else:
                    cy = 0 if self.row_mode == 0 else 2
                c.create_text(64 * S, cy * S, text="12:34", anchor="n",
                              fill="#ffcc00", font=("Courier New", 8))
            elif blocked is not None:
                geo = self._geo(blocked)
                if geo:
                    x, y, col_w = geo
                    rh = 16 if is_large else (13 if self.row_mode == 0 else 10)
                    c.create_rectangle(x * S, y * S, (x + col_w) * S, (y + rh) * S,
                                       fill="#2a2a00", outline="#666600", dash=(3, 3))
                    c.create_text((x + col_w // 2) * S, (y + rh // 2) * S,
                                  text="CLOCK", fill="#ffcc00", font=("Courier New", 8))

        # Draw slot outlines
        for s in range(n_slots):
            if s == blocked:
                continue
            geo = self._geo(s)
            if geo is None:
                continue
            x, y, col_w = geo
            row_h = 16 if is_large else (13 if self.row_mode == 0 else 10)
            c.create_rectangle(x * S, y * S, (x + col_w) * S, (y + row_h) * S,
                               outline="#333333", dash=(2, 2), tags=f"slot_{s}")

        # Draw bars
        for mid, e in self.layout.items():
            bp = e.get("barPosition", 255)
            if bp == 255:
                continue
            geo = self._geo(bp)
            if geo is None:
                continue
            x, y, col_w = geo
            bar_h = 16 if is_large else (13 if self.row_mode == 0 else 10)
            # Match device_render: offset shifts the bar right, clamp to 128px.
            bx = x + e.get("barOffsetX", 0)
            width = e.get("barWidth", 60)
            if bx + width > 128:
                width = 128 - bx
            if width <= 0 or bx < 0 or bx >= 128:
                continue
            fill_color = "#005577" if mid == self.selected_id else "#003344"
            c.create_rectangle(bx * S, y * S, (bx + width) * S, (y + bar_h) * S,
                               outline="#00aacc", fill=fill_color, tags=f"bar_{mid}")
            m = self.metrics_by_id.get(mid, {})
            bmin, bmax = e.get("barMin", 0), e.get("barMax", 100)
            rng = (bmax - bmin) or 100
            val = self._live_values.get(mid, m.get("current_value", 0)) or 0
            frac = max(0.0, min(1.0, (val - bmin) / rng))
            fill_w = int((width - 2) * frac)
            if fill_w > 0:
                c.create_rectangle((bx + 1) * S, (y + 1) * S,
                                   (bx + 1 + fill_w) * S, (y + bar_h - 1) * S,
                                   fill="#00aacc", outline="")

        # Draw text metrics
        font = ("Courier New", 13, "bold") if is_large else ("Courier New", 8)
        for mid, e in self.layout.items():
            pos = e.get("position", 255)
            if pos == 255:
                continue
            geo = self._geo(pos)
            if geo is None:
                continue
            x, y, _col_w = geo
            m = self.metrics_by_id.get(mid, {})
            label = (e.get("label") or m.get("name", ""))[:10]
            fg = "#00ff00" if mid == self.selected_id else "#ffffff"
            c.create_text(x * S + 2, y * S + 1, text=label, anchor="nw",
                          fill=fg, font=font, tags=f"text_{mid}")
            comp_id = e.get("companionId", 0)
            if comp_id and comp_id in self.metrics_by_id:
                comp = self.metrics_by_id[comp_id]
                comp_label = (self.layout.get(comp_id, {}).get("label") or comp.get("name", ""))[:6]
                if is_large:
                    c.create_text(128 * S - 2, y * S + 1, text=comp_label, anchor="ne",
                                  fill="#888888", font=font)
                else:
                    c.create_text((x + _col_w) * S - 2, y * S + 1, text=comp_label,
                                  anchor="ne", fill="#888888", font=font)

        if not any(e.get("position", 255) != 255 or e.get("barPosition", 255) != 255
                   for e in self.layout.values()):
            c.create_text(self.CANVAS_W // 2, self.CANVAS_H // 2,
                          text="Drag metrics from the palette below",
                          fill="#666666", font=("Courier New", 10))

    def _draw_palette(self):
        for w in self.palette_frame.winfo_children():
            w.destroy()
        unplaced = []
        for m in self.metrics:
            mid = m["id"]
            e = self.layout.get(mid, {})
            if e.get("position", 255) != 255:
                continue
            # skip companions (they are hidden, not unplaced for the user)
            is_companion = False
            for oid, oe in self.layout.items():
                if oe.get("companionId", 0) == mid and oe.get("position", 255) != 255:
                    is_companion = True
                    break
            if is_companion:
                continue
            unplaced.append(m)

        if not unplaced:
            tk.Label(self.palette_frame, text="(all metrics placed)",
                     bg="#2d2d2d", fg="#666666", font=("Arial", 9)).pack(anchor="w")
            return

        for m in unplaced:
            mid = m["id"]
            label = (self.layout.get(mid, {}).get("label") or m.get("name", ""))[:10]
            chip = tk.Label(
                self.palette_frame, text=f" {label} ",
                bg="#3a3a3a", fg="#ffffff", font=("Courier New", 9),
                relief=tk.RAISED, padx=4, pady=2, cursor="hand2"
            )
            chip.pack(side=tk.LEFT, padx=2, pady=2)
            chip.bind("<Button-1>", lambda ev, _mid=mid: self._start_palette_drag(ev, _mid))
            chip.bind("<B1-Motion>", self._on_palette_motion)
            chip.bind("<ButtonRelease-1>", self._on_palette_release)

    def _update_detail_panel(self):
        if self.selected_id is None or self.selected_id not in self.layout:
            self.detail_frame.pack_forget()
            return
        self.detail_frame.pack(fill=tk.X, padx=8)
        e = self.layout[self.selected_id]
        m = self.metrics_by_id.get(self.selected_id, {})
        label = e.get("label") or m.get("name", "?")
        self.detail_name_label.config(text=label)
        self.name_var.set(label)

        # Bar-slot dropdown: None + the free slots (plus this metric's current bar
        # slot). A bar can go in any empty row, independent of the text/companion.
        cur_bar = e.get("barPosition", 255)
        free = self._available_bar_slots(self.selected_id)
        self._bar_slot_by_label = {self._slot_label(s): s for s in free}
        self.bar_slot_combo["values"] = ["None"] + [self._slot_label(s) for s in free]
        self.bar_slot_var.set(self._slot_label(cur_bar) if cur_bar != 255 else "None")
        self.bar_min_var.set(str(e.get("barMin", 0)))
        self.bar_max_var.set(str(e.get("barMax", 100)))
        self.bar_width_var.set(str(e.get("barWidth", 60)))
        self.bar_off_var.set(str(e.get("barOffsetX", 0)))

        # Companion dropdown
        choices = ["None"]
        for om in self.metrics:
            if om["id"] == self.selected_id:
                continue
            olabel = (self.layout.get(om["id"], {}).get("label") or om.get("name", ""))[:10]
            choices.append(f"{om['id']}: {olabel}")
        self.companion_combo["values"] = choices
        comp_id = e.get("companionId", 0)
        if comp_id and comp_id in self.metrics_by_id:
            clabel = (self.layout.get(comp_id, {}).get("label")
                      or self.metrics_by_id[comp_id].get("name", ""))[:10]
            self.companion_var.set(f"{comp_id}: {clabel}")
        else:
            self.companion_var.set("None")

    # ---- Live 1:1 preview (device_render) ----

    def _render_metrics_for_preview(self):
        """Build {id: {label, unit, value}} using live values (or current_value)."""
        out = {}
        for m in self.metrics:
            mid = m["id"]
            val = self._live_values.get(mid)
            if val is None:
                val = m.get("current_value", 0) or 0
            label = (self.layout.get(mid, {}).get("label")
                     or m.get("label") or m.get("name") or "")
            out[mid] = {"label": label, "unit": m.get("unit", ""), "value": int(val or 0)}
        return out

    def _render_live_preview(self):
        if getattr(self, "preview_canvas", None) is None:
            return
        try:
            if not self.preview_canvas.winfo_exists():
                return
        except Exception:
            return
        img = device_render.render_stats_frame(
            self._render_metrics_for_preview(), self.layout, self.row_mode,
            show_clock=self.show_clock, clock_position=self.clock_position,
            clock_offset=self.clock_offset,
            rpm_k=bool(self.rpm_k_var.get()), net_mb=bool(self.net_mb_var.get()),
            timestamp=datetime.now().strftime("%H:%M"),
        )
        img = img.resize((self.PREVIEW_W, self.PREVIEW_H), Image.NEAREST)
        self._live_photo = ImageTk.PhotoImage(img)
        self.preview_canvas.delete("all")
        self.preview_canvas.create_image(0, 0, anchor="nw", image=self._live_photo)

    # ---- Live values worker (off the UI thread) ----

    def _start_worker(self):
        self._latest = None       # (values, payload) most recently collected
        self._seq = 0             # bumped by the worker on each new result
        self._applied_seq = 0     # last seq the poller applied
        self._poll_id = None
        self._worker = threading.Thread(target=self._worker_loop, daemon=True)
        self._worker.start()
        self._poll_live()         # main-thread poller (Tk lives here)

    def _worker_loop(self):
        """Off-thread sensor collection feeding the in-dialog 1:1 preview only.
        Sensor I/O can block ~1s, so it runs on its own thread and performs NO Tk
        calls - it just stores the latest values for the main-thread poller. It
        does NOT stream to the device (the main monitor loop owns that), so the
        editor can never flash a transient API error on the OLED.

        thread_init/thread_done (supplied by the caller) initialize COM for WMI on
        THIS thread - without it every LHM sensor reads 0 here while psutil keeps
        working."""
        thread_init = self.device_session.get("thread_init")
        thread_done = self.device_session.get("thread_done")
        if thread_init:
            try:
                thread_init()
            except Exception:
                pass
        try:
            while not self._stop_event.is_set():
                try:
                    result = self.device_session["collect"](self._last_good)
                    if result is not None:
                        _payload, values, self._last_good = result
                        self._latest = values
                        self._seq += 1
                except Exception:
                    pass
                self._stop_event.wait(1.0)
        finally:
            if thread_done:
                try:
                    thread_done()
                except Exception:
                    pass

    def _poll_live(self):
        """Main thread: apply the worker's newest values to the 1:1 preview.
        Reschedules itself. Never touches the device."""
        if self._stop_event.is_set():
            return
        latest, seq = self._latest, self._seq
        if latest is not None and seq != self._applied_seq:
            self._applied_seq = seq
            self._live_values = latest or {}
            self._render_live_preview()
        self._poll_id = self.win.after(400, self._poll_live)

    def _stop_worker(self):
        self._stop_event.set()
        if getattr(self, "_poll_id", None) is not None:
            try:
                self.win.after_cancel(self._poll_id)
            except Exception:
                pass
            self._poll_id = None

    # ---- Slot hit-testing ----

    def _slot_at_pixel(self, px, py):
        """Return the slot index at canvas pixel (px, py), or None.
        Returns None for clock-blocked slots."""
        S = self.SCALE
        is_large = self.row_mode >= 2
        row_h = 16 if is_large else (13 if self.row_mode == 0 else 10)
        blocked = clock_blocked_slot(self.row_mode, self.clock_position) if self.show_clock else None
        for s in range(slot_count(self.row_mode)):
            if s == blocked:
                continue
            geo = self._geo(s)
            if geo is None:
                continue
            x, y, col_w = geo
            if x * S <= px < (x + col_w) * S and y * S <= py < (y + row_h) * S:
                return s
        return None

    def _metric_at_slot(self, slot):
        """Return the metric id whose text or bar occupies `slot`, or None."""
        for mid, e in self.layout.items():
            if e.get("position", 255) == slot:
                return mid
            if e.get("barPosition", 255) == slot:
                return mid
        return None

    def _find_free_slot(self, prefer_near=None):
        """First free slot, preferring one adjacent to `prefer_near`."""
        n = slot_count(self.row_mode)
        blocked = clock_blocked_slot(self.row_mode, self.clock_position) if self.show_clock else None
        occupied = set()
        if blocked is not None:
            occupied.add(blocked)
        for e in self.layout.values():
            p = e.get("position", 255)
            if p != 255:
                occupied.add(p)
            bp = e.get("barPosition", 255)
            if bp != 255:
                occupied.add(bp)
        if prefer_near is not None:
            if self.row_mode < 2:
                partner = prefer_near ^ 1
                if partner < n and partner not in occupied:
                    return partner
            for delta in (1, -1, 2, -2):
                cand = prefer_near + delta
                if 0 <= cand < n and cand not in occupied:
                    return cand
        for s in range(n):
            if s not in occupied:
                return s
        return None

    # ---- Mutation helpers (enforce invariants) ----

    def _bump_occupant(self, slot):
        """If something occupies `slot`, send it to unplaced (position/bar 255)."""
        for mid, e in self.layout.items():
            if e.get("position", 255) == slot:
                e["position"] = 255
                e["companionId"] = 0
            if e.get("barPosition", 255) == slot:
                e["barPosition"] = 255

    def _place_metric(self, mid, target_slot):
        e = self.layout[mid]
        old_pos = e.get("position", 255)
        if old_pos == target_slot:
            return
        self._bump_occupant(target_slot)
        e["position"] = target_slot
        e["order"] = target_slot

    def _remove_metric(self, mid):
        e = self.layout[mid]
        e["position"] = 255
        e["barPosition"] = 255
        e["companionId"] = 0
        # also clear anything that has this as a companion
        for oid, oe in self.layout.items():
            if oe.get("companionId", 0) == mid:
                oe["companionId"] = 0

    def _toggle_bar(self, mid):
        e = self.layout[mid]
        if e.get("barPosition", 255) != 255:
            e["barPosition"] = 255
        else:
            pos = e.get("position", 255)
            if pos == 255:
                return
            free = self._find_free_slot(prefer_near=pos)
            if free is None:
                return
            self._bump_occupant(free)
            e["barPosition"] = free
            m = self.metrics_by_id.get(mid, {})
            bounds = _bar_bounds(m)
            if bounds:
                e["barMin"], e["barMax"] = bounds
            else:
                e["barMin"], e["barMax"] = 0, 100

    def _set_companion(self, primary_id, companion_id):
        pe = self.layout[primary_id]
        old_comp = pe.get("companionId", 0)
        if old_comp == companion_id:
            return
        pe["companionId"] = companion_id
        if companion_id and companion_id in self.layout:
            ce = self.layout[companion_id]
            ce["position"] = 255
            ce["barPosition"] = 255
            ce["companionId"] = 0

    def _clamp_to_row_mode(self):
        """Push out-of-range positions to unplaced after a row-mode change."""
        n = slot_count(self.row_mode)
        for mid, e in self.layout.items():
            if e.get("position", 255) != 255 and e["position"] >= n:
                e["position"] = 255
                e["companionId"] = 0
            if e.get("barPosition", 255) != 255 and e["barPosition"] >= n:
                e["barPosition"] = 255

    # ---- Inline rename ----

    def _start_rename(self, mid=None):
        """Edit a placed metric's name directly on the grid (F2 / programmatic)."""
        mid = mid if mid is not None else self.selected_id
        if mid is None or mid not in self.layout:
            return
        e = self.layout[mid]
        pos = e.get("position", 255)
        if pos == 255:
            return
        geo = self._geo(pos)
        if geo is None:
            return
        self._cancel_rename()
        x, y, col_w = geo
        S = self.SCALE
        cur = (e.get("label") or self.metrics_by_id.get(mid, {}).get("name", ""))
        var = tk.StringVar(value=cur)
        ent = tk.Entry(self.canvas, textvariable=var, bg="#222222", fg="#00ff00",
                       insertbackground="#00ff00", font=("Courier New", 9),
                       relief=tk.SOLID, borderwidth=1)
        self.canvas.create_window(x * S + 1, y * S + 1, anchor="nw",
                                  window=ent, width=col_w * S, tags="rename")
        ent.focus_set()
        ent.select_range(0, "end")
        ent.bind("<Return>", lambda _e: self._commit_rename(mid, var.get()))
        ent.bind("<Escape>", lambda _e: self._cancel_rename())
        ent.bind("<FocusOut>", lambda _e: self._commit_rename(mid, var.get()))
        self._rename_entry = ent

    def _commit_rename(self, mid, text):
        if self._rename_entry is None:
            return
        self._cancel_rename()
        if mid in self.layout:
            self.layout[mid]["label"] = (text or "").strip()[:10]
        self._redraw()

    def _cancel_rename(self):
        if self._rename_entry is not None:
            try:
                self._rename_entry.destroy()
            except Exception:
                pass
            self._rename_entry = None
            self.canvas.delete("rename")

    def _commit_name_entry(self):
        if self.selected_id is None or self.selected_id not in self.layout:
            return
        name = self.name_var.get().strip()[:10]
        self.layout[self.selected_id]["label"] = name
        self.detail_name_label.config(text=name or "(unnamed)")
        self._refresh_views()  # light: keep the Name entry focused while typing

    # ---- Canvas event handlers ----

    def _on_canvas_click(self, event):
        slot = self._slot_at_pixel(event.x, event.y)
        if slot is None:
            self.selected_id = None
            self._redraw()
            return
        mid = self._metric_at_slot(slot)
        if mid is not None:
            e = self.layout[mid]
            # select only text metrics, not bare bars
            if e.get("position", 255) == slot:
                self.selected_id = mid
                self._drag_data = {"mid": mid, "type": "canvas", "started": False,
                                   "start_x": event.x, "start_y": event.y}
            elif e.get("barPosition", 255) == slot:
                # find the metric whose bar this is - select that metric
                self.selected_id = mid
                self._drag_data = None
            self._redraw()
        else:
            self.selected_id = None
            self._drag_data = None
            self._redraw()

    def _on_canvas_dblclick(self, event):
        slot = self._slot_at_pixel(event.x, event.y)
        if slot is None:
            return
        mid = self._metric_at_slot(slot)
        if mid is None:
            return
        e = self.layout[mid]
        if e.get("position", 255) == slot:
            self.selected_id = mid
            self._toggle_bar(mid)
            self._redraw()

    def _on_canvas_drag(self, event):
        if self._drag_data is None:
            return
        if not self._drag_data.get("started"):
            dx = abs(event.x - self._drag_data["start_x"])
            dy = abs(event.y - self._drag_data["start_y"])
            if dx + dy < 8:
                return
            self._drag_data["started"] = True
        self.canvas.delete("drag_indicator")
        self.canvas.create_oval(event.x - 6, event.y - 6, event.x + 6, event.y + 6,
                                fill="#00d4ff", outline="", tags="drag_indicator")

    def _on_canvas_release(self, event):
        if self._drag_data is None or not self._drag_data.get("started"):
            self._drag_data = None
            return
        mid = self._drag_data["mid"]
        self._drag_data = None
        self.canvas.delete("drag_indicator")
        target = self._slot_at_pixel(event.x, event.y)
        if target is None:
            self._remove_metric(mid)
        else:
            self._place_metric(mid, target)
        self.selected_id = mid
        self._redraw()

    def _on_canvas_shift_click(self, event):
        if self.selected_id is None:
            return
        slot = self._slot_at_pixel(event.x, event.y)
        if slot is None:
            return
        mid = self._metric_at_slot(slot)
        if mid is None or mid == self.selected_id:
            return
        e = self.layout[mid]
        if e.get("position", 255) == slot:
            self._set_companion(self.selected_id, mid)
            self._redraw()

    # ---- Palette drag handlers ----

    def _start_palette_drag(self, event, mid):
        self.selected_id = mid
        self._drag_data = {
            "mid": mid, "type": "palette", "started": True,
            "widget": event.widget,
        }
        self._redraw()

    def _on_palette_motion(self, event):
        if self._drag_data is None or self._drag_data.get("type") != "palette":
            return
        cx = self.canvas.winfo_rootx()
        cy = self.canvas.winfo_rooty()
        mx = event.widget.winfo_rootx() + event.x
        my = event.widget.winfo_rooty() + event.y
        rx, ry = mx - cx, my - cy
        self.canvas.delete("drag_indicator")
        if 0 <= rx <= self.CANVAS_W and 0 <= ry <= self.CANVAS_H:
            self.canvas.create_oval(rx - 6, ry - 6, rx + 6, ry + 6,
                                    fill="#00d4ff", outline="", tags="drag_indicator")

    def _on_palette_release(self, event):
        if self._drag_data is None or self._drag_data.get("type") != "palette":
            self._drag_data = None
            return
        mid = self._drag_data["mid"]
        self._drag_data = None
        self.canvas.delete("drag_indicator")
        cx = self.canvas.winfo_rootx()
        cy = self.canvas.winfo_rooty()
        mx = event.widget.winfo_rootx() + event.x
        my = event.widget.winfo_rooty() + event.y
        rx, ry = mx - cx, my - cy
        if 0 <= rx <= self.CANVAS_W and 0 <= ry <= self.CANVAS_H:
            target = self._slot_at_pixel(rx, ry)
            if target is not None:
                self._place_metric(mid, target)
        self.selected_id = mid
        self._redraw()

    # ---- Side-panel handlers ----

    def _slot_label(self, s):
        """Human label for a slot index, e.g. 'Row 2 L' or (large mode) 'Row 2'."""
        if self.row_mode >= 2:
            return f"Row {s + 1}"
        return f"Row {s // 2 + 1} {'L' if s % 2 == 0 else 'R'}"

    def _available_bar_slots(self, mid):
        """Free slots (no text and no bar), plus this metric's current bar slot.
        A bar is independent of the metric's own text slot + inline companion, so
        any empty row is a valid target."""
        n = slot_count(self.row_mode)
        blocked = clock_blocked_slot(self.row_mode, self.clock_position) if self.show_clock else None
        occupied = set()
        if blocked is not None:
            occupied.add(blocked)
        cur_bar = self.layout.get(mid, {}).get("barPosition", 255)
        for e in self.layout.values():
            p = e.get("position", 255)
            if p != 255:
                occupied.add(p)
            bp = e.get("barPosition", 255)
            if bp != 255:
                occupied.add(bp)
        free = [s for s in range(n) if s not in occupied]
        if cur_bar != 255 and cur_bar not in free:
            free.append(cur_bar)
        return sorted(free)

    def _on_bar_slot_change(self):
        """Place (or clear) the selected metric's progress bar in the chosen slot."""
        if self.selected_id is None:
            return
        e = self.layout[self.selected_id]
        label = self.bar_slot_var.get()
        if label == "None":
            e["barPosition"] = 255
        else:
            slot = self._bar_slot_by_label.get(label)
            if slot is None:
                return
            self._bump_occupant(slot)
            e["barPosition"] = slot
            e.setdefault("barWidth", 60)
            e.setdefault("barOffsetX", 0)
            # Seed sensible bounds for %/temperature if still at defaults.
            if e.get("barMin", 0) == 0 and e.get("barMax", 100) == 100:
                bounds = _bar_bounds(self.metrics_by_id.get(self.selected_id, {}))
                if bounds:
                    e["barMin"], e["barMax"] = bounds
        self._redraw()

    def _on_bar_minmax_change(self):
        """Apply the bar Min/Max live as the user types."""
        if self.selected_id is None:
            return
        e = self.layout[self.selected_id]
        try:
            e["barMin"] = int(self.bar_min_var.get())
        except ValueError:
            pass
        try:
            e["barMax"] = int(self.bar_max_var.get())
        except ValueError:
            pass
        self._refresh_views()

    def _on_bar_size_change(self):
        """Apply the bar Width/Offset (pixels) live as the user types."""
        if self.selected_id is None:
            return
        e = self.layout[self.selected_id]
        try:
            e["barWidth"] = max(1, min(128, int(self.bar_width_var.get())))
        except ValueError:
            pass
        try:
            e["barOffsetX"] = max(0, min(127, int(self.bar_off_var.get())))
        except ValueError:
            pass
        self._refresh_views()

    def _on_companion_change(self):
        if self.selected_id is None:
            return
        val = self.companion_var.get()
        if val == "None":
            self._set_companion(self.selected_id, 0)
        else:
            try:
                comp_id = int(val.split(":")[0])
            except (ValueError, IndexError):
                return
            self._set_companion(self.selected_id, comp_id)
        self._redraw()

    def _on_template_change(self):
        self.template_key = self._tpl_key_by_label.get(self.template_var.get(), "compact")
        self._on_reset()

    def _on_row_mode_change(self):
        label = self.rm_var.get()
        self.row_mode = self._rm_by_label.get(label, ROWMODE_5x2)
        self._clamp_to_row_mode()
        self._redraw()

    def _on_clock_toggle(self):
        self.show_clock = bool(self.clock_var.get())
        state = "readonly" if self.show_clock else tk.DISABLED
        self.clk_pos_combo.config(state=state)
        self.clk_off_spin.config(state=tk.NORMAL if self.show_clock else tk.DISABLED)
        blocked = clock_blocked_slot(self.row_mode, self.clock_position) if self.show_clock else None
        if blocked is not None:
            self._bump_occupant(blocked)
        self._redraw()

    def _on_clock_pos_change(self):
        idx = self._CLK_POS_LABELS.index(self.clk_pos_var.get()) if self.clk_pos_var.get() in self._CLK_POS_LABELS else 0
        self.clock_position = idx
        blocked = clock_blocked_slot(self.row_mode, self.clock_position) if self.show_clock else None
        if blocked is not None:
            self._bump_occupant(blocked)
        self._redraw()

    def _on_clock_offset_change(self):
        """Apply the clock X offset (px) live to the preview (and the push)."""
        try:
            self.clock_offset = max(-64, min(64, int(self.clk_off_var.get())))
        except (ValueError, tk.TclError):
            return
        self._render_live_preview()

    def _on_reset(self):
        row_mode, layout, _hidden = auto_layout(self.metrics, self.template_key)
        self.row_mode = row_mode
        self.layout = layout
        self.show_clock = False
        self.clock_position = 0
        self.clock_var.set(0)
        self.clk_pos_var.set(self._CLK_POS_LABELS[0])
        self.clk_pos_combo.config(state=tk.DISABLED)
        self.clk_off_spin.config(state=tk.DISABLED)
        self.rm_var.set(self._rm_label_by_mode.get(self.row_mode, self._rm_labels[0]))
        self.selected_id = None
        self._redraw()

    def _on_reset_device(self):
        """Push a fresh auto-layout to the DEVICE only, to recover a stale or
        scrambled device. Leaves this window's layout and live preview untouched
        (use 'Reset to template' to reset the editor itself)."""
        if self.device_session is None:
            return
        if not messagebox.askyesno(
                "Reset device layout",
                "Reset the layout ON THE DEVICE to a fresh auto-layout from your "
                "current metrics?\n\nThis only changes the device screen - your edits "
                "in this window and the live preview are left as-is.",
                parent=self.win):
            return
        # Build the auto-layout locally and push it; do NOT mutate self.layout.
        row_mode, layout, _hidden = auto_layout(self.metrics, self.template_key)
        names = {m["id"]: (m.get("label") or m.get("name") or "") for m in self.metrics}
        payload = build_device_layout_json(
            row_mode, layout, show_clock=False, clock_position=0,
            rpm_k=bool(self.rpm_k_var.get()), net_mb=bool(self.net_mb_var.get()),
            clock_offset=self.clock_offset, metric_names=names)
        self.reset_dev_btn.config(state=tk.DISABLED, text="Resetting...")
        self.live_status.config(text="Resetting device layout...", fg="#ffb454")
        ip = self.device_session_ip

        def worker():
            try:
                ok, detail = push_layout_to_device(ip, payload)
            except Exception as e:
                ok, detail = False, str(e)
            self.win.after(0, lambda: self._reset_device_done(ok, detail))

        threading.Thread(target=worker, daemon=True).start()

    def _reset_device_done(self, ok, detail):
        self.reset_dev_btn.config(state=tk.NORMAL, text="Reset device layout")
        if ok:
            self.live_status.config(text="Device layout reset (your edits kept)", fg="#00ff88")
        else:
            self.live_status.config(text=f"Reset failed: {str(detail)[:40]}", fg="#ff6666")

    # ---- Apply (OK) / Pull / Close ----

    def _on_ok(self):
        """Commit the layout and push it to the device. The window stays open so
        the user can keep tweaking; only the layout config is sent (one POST), no
        value-frame streaming, so the device never flashes a transient API error."""
        self._commit_result()
        if self.device_session is None:
            self.live_status.config(text="No device - layout kept in app only", fg="#ffb454")
            return
        # Names the device receives over UDP, so its name guard binds each slot to
        # the right metric even if the selection's id order has drifted.
        metric_names = {m["id"]: (m.get("label") or m.get("name") or "")
                        for m in self.metrics}
        payload = build_device_layout_json(
            self.row_mode, self.layout, self.show_clock, self.clock_position,
            rpm_k=bool(self.rpm_k_var.get()), net_mb=bool(self.net_mb_var.get()),
            clock_offset=self.clock_offset, metric_names=metric_names)
        self.push_btn.config(state=tk.DISABLED, text="Pushing...")
        self.live_status.config(text="Pushing layout...", fg="#ffb454")

        def worker():
            try:
                ok, detail = push_layout_to_device(self.device_session_ip, payload)
            except Exception as e:
                ok, detail = False, str(e)
            self.win.after(0, lambda: self._push_done(ok, detail))

        threading.Thread(target=worker, daemon=True).start()

    def _push_done(self, ok, detail):
        self.push_btn.config(state=tk.NORMAL, text="Save to device")
        if ok:
            self.live_status.config(text="Pushed to device - layout applied", fg="#00ff88")
        else:
            self.live_status.config(text=f"Push failed: {str(detail)[:40]}", fg="#ff6666")

    def _on_pull(self):
        """Pull the device's current (working) config and load it for editing, so
        the user can correct a layout that already lives on the device."""
        if self.device_session is None:
            return
        ip = self.device_session_ip
        metrics = list(self.metrics)
        self.pull_btn.config(state=tk.DISABLED, text="Pulling...")
        self.live_status.config(text="Pulling config from device...", fg="#ffb454")

        def worker():
            try:
                data = fetch_device_export(ip)
                parsed = parse_device_layout(data, metrics)
                err = None
            except Exception as e:
                parsed, err = None, str(e)
            self.win.after(0, lambda: self._pull_done(parsed, err))

        threading.Thread(target=worker, daemon=True).start()

    def _pull_done(self, parsed, err):
        self.pull_btn.config(state=tk.NORMAL, text="Pull from device")
        if parsed is None:
            self.live_status.config(text=f"Pull failed: {str(err)[:40]}", fg="#ff6666")
            return
        self._apply_parsed(parsed)
        self.live_status.config(text="Pulled config from device - editing it now", fg="#00ff88")

    def _apply_parsed(self, parsed):
        """Adopt a parsed layout (from Pull or an imported file) and sync controls."""
        self.row_mode = parsed["row_mode"]
        self.layout = parsed["layout"]
        self.show_clock = parsed["show_clock"]
        self.clock_position = parsed["clock_position"]
        self.clock_offset = parsed["clock_offset"]
        self.rm_var.set(self._rm_label_by_mode.get(self.row_mode, self._rm_labels[0]))
        self.clock_var.set(1 if self.show_clock else 0)
        pos = self.clock_position if 0 <= self.clock_position < len(self._CLK_POS_LABELS) else 0
        self.clk_pos_var.set(self._CLK_POS_LABELS[pos])
        self.clk_pos_combo.config(state="readonly" if self.show_clock else tk.DISABLED)
        self.clk_off_var.set(str(self.clock_offset))
        self.clk_off_spin.config(state=tk.NORMAL if self.show_clock else tk.DISABLED)
        self.rpm_k_var.set(1 if parsed["rpm_k"] else 0)
        self.net_mb_var.set(1 if parsed["net_mb"] else 0)
        self.selected_id = None
        self._redraw()

    def _on_export_file(self):
        """Save the current layout to a JSON file (device /api/import format, with
        names) so it can be restored later regardless of selection order."""
        path = filedialog.asksaveasfilename(
            parent=self.win, title="Save layout to file", defaultextension=".json",
            filetypes=[("Layout JSON", "*.json"), ("All files", "*.*")],
            initialfile="oled_layout.json")
        if not path:
            return
        self._flush_bar_edits()
        names = {m["id"]: (m.get("label") or m.get("name") or "") for m in self.metrics}
        data = build_device_layout_json(
            self.row_mode, self.layout, self.show_clock, self.clock_position,
            rpm_k=bool(self.rpm_k_var.get()), net_mb=bool(self.net_mb_var.get()),
            clock_offset=self.clock_offset, metric_names=names)
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            self.live_status.config(text="Layout saved to file", fg="#00ff88")
        except Exception as e:
            self.live_status.config(text=f"Save failed: {str(e)[:40]}", fg="#ff6666")

    def _on_import_file(self):
        """Load a layout from a JSON file and bind it to the current metrics by
        name (so it survives a different selection order)."""
        path = filedialog.askopenfilename(
            parent=self.win, title="Import layout from file",
            filetypes=[("Layout JSON", "*.json"), ("All files", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            parsed = parse_device_layout(data, list(self.metrics))
        except Exception as e:
            self.live_status.config(text=f"Import failed: {str(e)[:40]}", fg="#ff6666")
            return
        self._apply_parsed(parsed)
        self.live_status.config(text="Layout imported - Save to device to apply", fg="#00ff88")

    # ---- helpers for session host/port ----

    @property
    def device_session_ip(self):
        return getattr(self, "_ds_ip", None)

    @property
    def device_session_port(self):
        return getattr(self, "_ds_port", 4210)

    def _flush_bar_edits(self):
        if self.selected_id and self.selected_id in self.layout:
            e = self.layout[self.selected_id]
            for var, key in ((self.bar_min_var, "barMin"), (self.bar_max_var, "barMax"),
                             (self.bar_width_var, "barWidth"), (self.bar_off_var, "barOffsetX")):
                try:
                    e[key] = int(var.get())
                except ValueError:
                    pass

    # ---- finish ----

    def _commit_result(self):
        """Snapshot the current layout into self.result so the caller keeps it.
        Called by both OK (apply) and Close, so the app never loses the edits."""
        self._flush_bar_edits()
        self.result = {
            "row_mode": self.row_mode,
            "layout": self.layout,
            "show_clock": self.show_clock,
            "clock_position": self.clock_position,
            "rpm_k": bool(self.rpm_k_var.get()),
            "net_mb": bool(self.net_mb_var.get()),
            "clock_offset": self.clock_offset,
        }

    def _toggle_help(self):
        """Show/hide the short how-to text under the 'How to use' button."""
        self._help_visible = not self._help_visible
        if self._help_visible:
            self._help_label.pack(fill=tk.X, padx=4, pady=(2, 2), before=self.canvas.master)
            self._help_btn.config(text="How to use ▴")
        else:
            self._help_label.pack_forget()
            self._help_btn.config(text="How to use ▾")

    def _on_save(self):
        """Persist the layout to the app's config (survives a restart)."""
        self._commit_result()
        if self._on_save_cb is None:
            self.live_status.config(text="Save unavailable", fg="#ffb454")
            return
        try:
            ok = bool(self._on_save_cb(self.result))
        except Exception as e:
            ok, _ = False, e
        if ok:
            self.live_status.config(text="Settings saved", fg="#00ff88")
        else:
            self.live_status.config(text="Save failed", fg="#ff6666")

    def _on_close(self):
        self._commit_result()
        self._stop_worker()
        self.win.destroy()
