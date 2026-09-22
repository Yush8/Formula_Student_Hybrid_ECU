"""
configgui.plot_tab  --  the live Plot tab (pick signals, watch them graph).

PlotMixin lets you tick any scalar telemetry signals and watch them plotted live
on a scrolling strip chart as the stream comes in. It shares the ONE telemetry
stream (the same `#T` frames the Live Telemetry tab uses): TelemMixin taps every
sample into here via _plot_feed, and the selector is populated by _plot_note_signal
as signals are discovered - so, like the rest of the console, it needs no per-signal
setup. The chart itself is configgui.plot_widget.LivePlot (matplotlib).

If matplotlib isn't installed the tab shows a one-line "pip install matplotlib"
hint and every hook below no-ops, so the rest of the GUI is unaffected.
"""

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_MONO, PLOT_COLORS
from .protocol import TELEM_GROUP_ORDER, telem_group_for
from .plot_widget import LivePlot, MATPLOTLIB_OK, MATPLOTLIB_ERR

# Plot windows (seconds) offered in the combo, and how often the chart repaints.
PLOT_WINDOWS = ["5", "10", "15", "20", "30", "60"]
PLOT_REDRAW_MS = 100          # 10 fps - smooth enough, cheap enough


class PlotMixin:
    # ---- Plot tab ----
    def _build_plot_panel(self, parent):
        self.plot = None                 # stays None if matplotlib is missing
        self._plot_rows = {}             # name -> row widgets + state
        self._plot_groups = {}           # group label -> selector LabelFrame
        self._plot_known = {}            # name -> is-scalar (plottable) bool
        self._plot_selected = set()      # currently plotted signal names
        self._plot_free_colors = list(PLOT_COLORS)
        self._plot_paused = False
        if not hasattr(self, "_plot_saved_signals"):
            self._plot_saved_signals = []

        if not MATPLOTLIB_OK:
            msg = ("Live plotting needs matplotlib.\n\n"
                   "    pip install matplotlib\n\n"
                   "then restart the console. Everything else works without it.")
            box = ttk.LabelFrame(parent, text="Plot (unavailable)")
            box.pack(fill="both", expand=True, padx=8, pady=8)
            ttk.Label(box, text=msg, style="Muted.TLabel",
                      justify="left").pack(anchor="w", padx=16, pady=16)
            if MATPLOTLIB_ERR:
                ttk.Label(box, text=f"({MATPLOTLIB_ERR})", style="Muted.TLabel"
                          ).pack(anchor="w", padx=16)
            return

        self.plot_window_var = tk.StringVar(value="15")
        self.plot_norm_var = tk.BooleanVar(value=False)

        paned = ttk.Panedwindow(parent, orient="horizontal")
        paned.pack(fill="both", expand=True, padx=8, pady=4)
        left = ttk.Frame(paned)
        right = ttk.Frame(paned)
        paned.add(left, weight=0)
        paned.add(right, weight=1)
        # Give the selector a sensible starting width once the panes exist.
        self.root.after(120, lambda: self._plot_set_sash(paned))

        self._build_plot_selector(left)
        self._build_plot_main(right)

        # Start the repaint loop (harmless while nothing is selected).
        self.root.after(PLOT_REDRAW_MS, self._plot_loop)

    def _plot_set_sash(self, paned):
        try:
            paned.sashpos(0, 270)
        except tk.TclError:
            pass

    def _build_plot_selector(self, parent):
        box = ttk.LabelFrame(parent, text="Signals  ·  tick to plot")
        box.pack(fill="both", expand=True)

        ctl = ttk.Frame(box)
        ctl.pack(fill="x", padx=4, pady=(3, 2))
        ttk.Label(ctl, text="Filter:").pack(side="left")
        self._plot_filter_var = tk.StringVar()
        fe = ttk.Entry(ctl, textvariable=self._plot_filter_var, width=12)
        fe.pack(side="left", padx=(2, 0))
        self._plot_filter_var.trace_add("write", lambda *_: self._plot_apply_filter())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self._plot_filter_var.set("")).pack(side="left", padx=(2, 0))
        ttk.Button(ctl, text="Clear", width=6,
                   command=self._plot_clear_selection).pack(side="right")

        # Scrollable checkbutton list (grouped like the Telemetry tab).
        canvas = tk.Canvas(box, highlightthickness=0, background=UI["card"], width=250)
        vsb = ttk.Scrollbar(box, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True, padx=(2, 0), pady=2)
        self._plot_list = ttk.Frame(canvas)
        win = canvas.create_window((0, 0), window=self._plot_list, anchor="nw")
        self._plot_list.bind(
            "<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win, width=e.width))
        canvas.bind("<Enter>", lambda e: canvas.bind_all(
            "<MouseWheel>", lambda ev: canvas.yview_scroll(int(-ev.delta / 120), "units")))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))

        self._plot_hint = ttk.Label(
            self._plot_list, style="Muted.TLabel", justify="left",
            text="(connect — signals appear here\nas they're discovered)")
        self._plot_hint.pack(anchor="w", padx=6, pady=6)

    def _build_plot_main(self, parent):
        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", pady=(0, 2))

        self.plot_btn = ttk.Button(ctl, text="▶ Start stream", style="Accent.TButton",
                                   command=self._plot_toggle_stream)
        self.plot_btn.pack(side="left")

        ttk.Label(ctl, text="Window:").pack(side="left", padx=(10, 2))
        wcb = ttk.Combobox(ctl, width=4, state="readonly",
                           values=PLOT_WINDOWS, textvariable=self.plot_window_var)
        wcb.pack(side="left")
        wcb.bind("<<ComboboxSelected>>", lambda _e: self._save_settings())
        ttk.Label(ctl, text="s").pack(side="left", padx=(2, 8))

        ttk.Checkbutton(ctl, text="Normalise", variable=self.plot_norm_var,
                        command=self._plot_set_normalise).pack(side="left")

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        self.plot_pause_btn = ttk.Button(ctl, text="⏸ Pause", width=9,
                                         command=self._plot_toggle_pause)
        self.plot_pause_btn.pack(side="left", padx=(0, 4))
        self.plot_clear_btn = ttk.Button(ctl, text="Clear graph", width=11,
                                         command=self._plot_clear_data)
        self.plot_clear_btn.pack(side="left")

        self.plot_status = ttk.Label(ctl, text="0 plotted", foreground=UI["muted"],
                                     background=UI["card"], font=FONT_MONO)
        self.plot_status.pack(side="right")

        body = ttk.LabelFrame(parent, text="Live plot")
        body.pack(fill="both", expand=True, pady=(2, 0))
        self.plot = LivePlot(body)
        self.plot.set_normalise(self.plot_norm_var.get())
        self.plot.pack(fill="both", expand=True, padx=4, pady=4)

    # ---- selector population (hooks called by TelemMixin) ----
    def _plot_note_signal(self, name, length):
        """Record whether a discovered signal is a plottable scalar. Called by the
        Telemetry tab as signals are discovered; a no-op when plotting is off."""
        if self.plot is None:
            return
        try:
            scalar = int(length) <= 1
        except (TypeError, ValueError):
            scalar = True
        self._plot_known[name] = scalar

    def _plot_refresh_signals(self):
        """Reconcile the selector rows with the currently-known scalar signals, then
        apply any saved selection + the filter. Cheap + idempotent."""
        if self.plot is None:
            return
        # Add rows for new scalar signals (in discovery order).
        for name in self._telem_order:
            if self._plot_known.get(name, True) and name not in self._plot_rows:
                self._plot_ensure_row(name)
        # Remove rows for signals that vanished or turned out to be arrays.
        for name in list(self._plot_rows):
            if name not in self._telem_order or not self._plot_known.get(name, True):
                self._plot_remove_row(name)
        # Apply a remembered selection now that its rows may exist.
        for name in list(self._plot_saved_signals):
            if name in self._plot_rows and name not in self._plot_selected:
                self._plot_set_selected(name, True)
        self._plot_apply_filter()

    def _apply_saved_plot_signals(self):
        """Called from _load_settings: apply the remembered plot selection (rows
        that don't exist yet are applied later by _plot_refresh_signals)."""
        self._plot_refresh_signals()

    def _plot_group_frame(self, label):
        gf = self._plot_groups.get(label)
        if gf is not None:
            return gf
        if self._plot_hint is not None:
            self._plot_hint.destroy()
            self._plot_hint = None
        gf = ttk.LabelFrame(self._plot_list, text=label)
        self._plot_groups[label] = gf
        # Keep groups in the fixed telemetry order.
        for lab in TELEM_GROUP_ORDER:
            g = self._plot_groups.get(lab)
            if g is not None:
                g.pack_forget()
                g.pack(fill="x", padx=4, pady=(2, 2))
        return gf

    def _plot_ensure_row(self, name):
        if name in self._plot_rows:
            return
        label = telem_group_for(name)
        gf = self._plot_group_frame(label)
        row = ttk.Frame(gf)
        row.pack(fill="x", padx=2, pady=0)
        swatch = tk.Label(row, text=" ", width=2, background=UI["card"])
        swatch.pack(side="left", padx=(0, 4))
        var = tk.BooleanVar(value=False)
        cb = ttk.Checkbutton(row, text=name, variable=var,
                             command=lambda n=name: self._plot_toggle_signal(n))
        cb.pack(side="left")
        val = ttk.Label(row, text="", style="Muted.TLabel", font=FONT_MONO)
        val.pack(side="right", padx=(0, 4))
        self._plot_rows[name] = {"row": row, "swatch": swatch, "var": var,
                                 "cb": cb, "val": val, "group": label, "color": None}

    def _plot_remove_row(self, name):
        if name in self._plot_selected:
            self._plot_set_selected(name, False)
        row = self._plot_rows.pop(name, None)
        if row is not None:
            row["row"].destroy()

    # ---- selection ----
    def _plot_toggle_signal(self, name):
        row = self._plot_rows.get(name)
        if row is not None:
            self._plot_set_selected(name, bool(row["var"].get()))

    def _plot_set_selected(self, name, on):
        row = self._plot_rows.get(name)
        if row is None:
            return
        if on and name not in self._plot_selected:
            color = (self._plot_free_colors.pop(0) if self._plot_free_colors
                     else PLOT_COLORS[len(self._plot_selected) % len(PLOT_COLORS)])
            row["color"] = color
            row["swatch"].config(background=color)
            row["var"].set(True)
            self._plot_selected.add(name)
            self.plot.add_series(name, color)
        elif not on and name in self._plot_selected:
            self._plot_selected.discard(name)
            colour = row["color"]
            if colour and colour not in self._plot_free_colors:
                self._plot_free_colors.insert(0, colour)   # recycle
            row["color"] = None
            row["swatch"].config(background=UI["card"])
            row["var"].set(False)
            row["val"].config(text="")
            self.plot.remove_series(name)
        self._save_settings()

    def _plot_clear_selection(self):
        for name in list(self._plot_selected):
            self._plot_set_selected(name, False)

    def _plot_apply_filter(self):
        flt = self._plot_filter_var.get().strip().lower()
        group_vis = {}
        for name, row in self._plot_rows.items():
            show = (not flt) or (flt in name.lower())
            if show:
                row["row"].pack(fill="x", padx=2, pady=0)
            else:
                row["row"].pack_forget()
            group_vis[row["group"]] = group_vis.get(row["group"], False) or show
        for lab in TELEM_GROUP_ORDER:
            g = self._plot_groups.get(lab)
            if g is None:
                continue
            g.pack_forget()
            if (not flt) or group_vis.get(lab, False):
                g.pack(fill="x", padx=4, pady=(2, 2))

    # ---- controls ----
    def _plot_toggle_stream(self):
        """Start / stop the shared telemetry stream WITHOUT jumping to the Telemetry
        tab (unlike the Telemetry tab's own button), so you stay on the plot."""
        if self._telem_streaming:
            self._send("telem off")
        else:
            self._send(f"telem rate {self.telem_rate_var.get()}")
            self._send("telem on")

    def _plot_set_normalise(self):
        if self.plot is not None:
            self.plot.set_normalise(self.plot_norm_var.get())
        self._save_settings()

    def _plot_toggle_pause(self):
        self._plot_paused = not self._plot_paused
        self.plot_pause_btn.config(text="▶ Resume" if self._plot_paused else "⏸ Pause")

    def _plot_clear_data(self):
        if self.plot is not None:
            self.plot.clear_data()

    # ---- data feed (hook called by TelemMixin per sample) ----
    def _plot_feed(self, name, val, t):
        if self.plot is None or self._plot_paused:
            return
        if name not in self._plot_selected:
            return
        try:
            v = float(val)
        except (TypeError, ValueError):
            return
        self.plot.push(name, t, v)

    # ---- repaint loop ----
    def _plot_loop(self):
        if self.plot is not None:
            # Live value chips beside each plotted signal (exact streamed string).
            for name in self._plot_selected:
                row = self._plot_rows.get(name)
                if row is not None:
                    row["val"].config(text=self._telem_last.get(name) or "")
            self.plot_btn.config(text="■ Stop stream" if self._telem_streaming
                                 else "▶ Start stream")
            n = len(self._plot_selected)
            if self._plot_paused:
                self.plot_status.config(text=f"{n} plotted · paused",
                                        foreground=UI["amber"])
            elif self._telem_streaming:
                self.plot_status.config(text=f"{n} plotted · live",
                                        foreground=UI["green"])
            else:
                self.plot_status.config(text=f"{n} plotted · stream off",
                                        foreground=UI["muted"])
            if not self._plot_paused:
                self.plot.set_window(float(self.plot_window_var.get()))
                self.plot.redraw()
        self.root.after(PLOT_REDRAW_MS, self._plot_loop)
