"""Drag-and-drop layout editor dialog for the PC Companion App.

Tkinter modal dialog to manually edit the device layout. Extracted from
pc_stats_monitor_v3.py; depends only on layout_engine for layout logic.
"""
import tkinter as tk
from tkinter import ttk

from layout_engine import (
    ROWMODE_5x2, ROWMODE_6x2, ROWMODE_LARGE2, ROWMODE_LARGE3,
    slot_count, slot_geometry, clock_blocked_slot,
    auto_layout, _bar_bounds, _ROWMODE_LABELS,
)


class LayoutEditorDialog:
    """Modal dialog for manually editing the device layout via drag-and-drop."""

    SCALE = 4
    CANVAS_W = 128 * SCALE   # 512
    CANVAS_H = 64 * SCALE    # 256
    CHIP_H = 28
    PALETTE_PAD = 4

    def __init__(self, parent, metrics, row_mode, layout_by_id, template_key,
                 show_clock=False, clock_position=0):
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

        self.win = tk.Toplevel(parent)
        self.win.title("Customize Layout")
        self.win.configure(bg="#1e1e1e")
        self.win.transient(parent)
        self.win.grab_set()
        self.win.resizable(True, True)
        self.win.minsize(700, 520)

        self._build_ui()
        self._redraw()

        self.win.protocol("WM_DELETE_WINDOW", self._on_cancel)
        self.win.wait_window()

    # ---- UI construction ----

    def _build_ui(self):
        top = tk.Frame(self.win, bg="#1e1e1e")
        top.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)

        left = tk.Frame(top, bg="#1e1e1e")
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # Canvas
        cf = tk.Frame(left, bg="#1e1e1e")
        cf.pack(fill=tk.X, padx=4, pady=4)
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
        pf.pack(fill=tk.X, padx=4, pady=(0, 4))
        self.palette_frame = tk.Frame(pf, bg="#2d2d2d")
        self.palette_frame.pack(fill=tk.X, padx=4, pady=4)

        # Right panel
        right = tk.Frame(top, bg="#2d2d2d", width=220)
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
        if not self.show_clock:
            self.clk_pos_combo.config(state=tk.DISABLED)

        tk.Frame(right, bg="#444444", height=1).pack(fill=tk.X, padx=8, pady=8)

        # Selected-metric detail panel
        self.detail_frame = tk.Frame(right, bg="#2d2d2d")
        self.detail_frame.pack(fill=tk.X, padx=8)
        self.detail_name_label = tk.Label(self.detail_frame, text="(click a metric)",
                                          bg="#2d2d2d", fg="#00d4ff",
                                          font=("Arial", 11, "bold"), anchor="w")
        self.detail_name_label.pack(fill=tk.X, pady=(0, 4))

        self.bar_var = tk.IntVar(value=0)
        self.bar_check = tk.Checkbutton(self.detail_frame, text="Progress bar",
                                        variable=self.bar_var, bg="#2d2d2d", fg="#ffffff",
                                        selectcolor="#444444", activebackground="#2d2d2d",
                                        font=("Arial", 10), command=self._on_bar_toggle)
        self.bar_check.pack(anchor="w")

        bar_opts = tk.Frame(self.detail_frame, bg="#2d2d2d")
        bar_opts.pack(fill=tk.X, pady=(2, 4))
        tk.Label(bar_opts, text="Min:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).grid(row=0, column=0, sticky="w")
        self.bar_min_var = tk.StringVar(value="0")
        tk.Entry(bar_opts, textvariable=self.bar_min_var, width=5,
                 bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff").grid(row=0, column=1, padx=4)
        tk.Label(bar_opts, text="Max:", bg="#2d2d2d", fg="#aaaaaa",
                 font=("Arial", 9)).grid(row=0, column=2, sticky="w")
        self.bar_max_var = tk.StringVar(value="100")
        tk.Entry(bar_opts, textvariable=self.bar_max_var, width=5,
                 bg="#3a3a3a", fg="#ffffff", insertbackground="#ffffff").grid(row=0, column=3, padx=4)

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

        tk.Button(btn_frame, text="Reset to template", command=self._on_reset,
                  bg="#555555", fg="#ffffff", font=("Arial", 10),
                  relief=tk.FLAT, padx=8, pady=3).pack(fill=tk.X, pady=(0, 4))
        tk.Button(btn_frame, text="OK", command=self._on_ok,
                  bg="#00d4ff", fg="#000000", font=("Arial", 11, "bold"),
                  relief=tk.FLAT, pady=4).pack(fill=tk.X, pady=(0, 4))
        tk.Button(btn_frame, text="Cancel", command=self._on_cancel,
                  bg="#666666", fg="#ffffff", font=("Arial", 10),
                  relief=tk.FLAT, pady=3).pack(fill=tk.X)

    # ---- Drawing ----

    def _redraw(self):
        self._draw_canvas()
        self._draw_palette()
        self._update_detail_panel()

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
            width = min(e.get("barWidth", 60), col_w)
            fill_color = "#005577" if mid == self.selected_id else "#003344"
            c.create_rectangle(x * S, y * S, (x + width) * S, (y + bar_h) * S,
                               outline="#00aacc", fill=fill_color, tags=f"bar_{mid}")
            m = self.metrics_by_id.get(mid, {})
            bmin, bmax = e.get("barMin", 0), e.get("barMax", 100)
            rng = (bmax - bmin) or 100
            val = m.get("current_value", 0) or 0
            frac = max(0.0, min(1.0, (val - bmin) / rng))
            fill_w = int((width - 2) * frac)
            if fill_w > 0:
                c.create_rectangle((x + 1) * S, (y + 1) * S,
                                   (x + 1 + fill_w) * S, (y + bar_h - 1) * S,
                                   fill="#00aacc", outline="")

        # Draw text metrics
        font = ("Courier New", 15, "bold") if is_large else ("Courier New", 9)
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

        has_bar = e.get("barPosition", 255) != 255
        self.bar_var.set(1 if has_bar else 0)
        self.bar_min_var.set(str(e.get("barMin", 0)))
        self.bar_max_var.set(str(e.get("barMax", 100)))

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

    def _on_bar_toggle(self):
        if self.selected_id is None:
            return
        e = self.layout[self.selected_id]
        want_bar = self.bar_var.get()
        has_bar = e.get("barPosition", 255) != 255
        if want_bar and not has_bar:
            self._toggle_bar(self.selected_id)
        elif not want_bar and has_bar:
            self._toggle_bar(self.selected_id)
        try:
            e["barMin"] = int(self.bar_min_var.get())
        except ValueError:
            pass
        try:
            e["barMax"] = int(self.bar_max_var.get())
        except ValueError:
            pass
        self._redraw()

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

    def _on_row_mode_change(self):
        label = self.rm_var.get()
        self.row_mode = self._rm_by_label.get(label, ROWMODE_5x2)
        self._clamp_to_row_mode()
        self._redraw()

    def _on_clock_toggle(self):
        self.show_clock = bool(self.clock_var.get())
        self.clk_pos_combo.config(state="readonly" if self.show_clock else tk.DISABLED)
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

    def _on_reset(self):
        row_mode, layout, _hidden = auto_layout(self.metrics, self.template_key)
        self.row_mode = row_mode
        self.layout = layout
        self.show_clock = False
        self.clock_position = 0
        self.clock_var.set(0)
        self.clk_pos_var.set(self._CLK_POS_LABELS[0])
        self.clk_pos_combo.config(state=tk.DISABLED)
        self.rm_var.set(self._rm_label_by_mode.get(self.row_mode, self._rm_labels[0]))
        self.selected_id = None
        self._redraw()

    def _on_ok(self):
        # Apply any pending bar min/max edits for the selected metric
        if self.selected_id and self.selected_id in self.layout:
            e = self.layout[self.selected_id]
            try:
                e["barMin"] = int(self.bar_min_var.get())
            except ValueError:
                pass
            try:
                e["barMax"] = int(self.bar_max_var.get())
            except ValueError:
                pass
        self.result = {
            "row_mode": self.row_mode,
            "layout": self.layout,
            "show_clock": self.show_clock,
            "clock_position": self.clock_position,
        }
        self.win.destroy()

    def _on_cancel(self):
        self.result = None
        self.win.destroy()


