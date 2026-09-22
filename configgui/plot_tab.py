"""
configgui.plot_tab  --  the live Plot tab (pick signals, watch them graph).

PlotMixin lets you tick any scalar telemetry signals and watch them plotted live
on a scrolling strip chart as the stream comes in. It shares the ONE telemetry
stream (the same `#T` frames the Live Telemetry tab uses): TelemMixin taps every
sample into here via _plot_feed, and the selector is populated by _plot_note_signal
as signals are discovered - so, like the rest of the console, it needs no per-signal
setup. The chart itself is configgui.plot_widget.LivePlot (matplotlib).

Two rules govern what you see, and both are deliberate:

  MEMORY.  Every scalar signal is captured continuously into
  configgui.history.SignalHistory - ticked or not, paused or not. Ticking a
  signal back-fills its last N seconds; Pause pins the view at the instant you
  pressed it while the capture keeps running underneath, so Resume continues the
  trace with no gap and you can even tick a *new* signal while frozen. How deep
  that buffer goes is yours to set (the History combo), and its real cost in MB
  is printed next to it.

  SELECTION.  Your tick list is never changed behind your back. A remembered
  selection is applied ONCE, as each row appears; after that only you (or a watch
  set you pick) move the ticks. A signal that vanishes on a reconnect is requeued
  so it returns if the board offers it again, but a signal you un-tick stays
  un-ticked. "Pin" locks the lot: nothing automatic touches it at all. Named
  watch sets keep several arrangements (say "Torque debug" and "BMS") so you can
  swap between them from the combo.

If matplotlib isn't installed the tab shows a one-line "pip install matplotlib"
hint and every hook below no-ops, so the rest of the GUI is unaffected.
"""

import time

import tkinter as tk
from tkinter import ttk, simpledialog, messagebox

from .theme import UI, FONT_MONO, PLOT_COLORS
from .protocol import (
    TELEM_GROUP_ORDER, telem_group_for, SESSIONS_DIR,
    PLOT_HISTORY, PLOT_HISTORY_S, DEFAULT_HISTORY,
)
from .plot_widget import LivePlot, MATPLOTLIB_OK, MATPLOTLIB_ERR
from .history import SignalHistory, human_bytes
from .trigger import OPS, SPECIALS, ANY_FAULT

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
        self._plot_color_hint = {}       # name -> colour it had last time
        self._plot_paused = False
        self._plot_freeze_t = None       # right-edge timestamp while paused
        self._plot_history = None        # the LIVE capture buffer
        self._plot_review = None         # a loaded session, when reviewing one
        self._plot_review_label = ""
        self._plot_mem_tick = 0
        # Signals to tick as soon as their row exists. Consumed ONE-SHOT: a name
        # is dropped the moment it is applied, so nothing you un-tick can return.
        if not hasattr(self, "_plot_pending"):
            self._plot_pending = []
        if not hasattr(self, "_plot_sets"):
            self._plot_sets = {}         # watch-set name -> [signal names]

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
        self.plot_hist_var = tk.StringVar(value=DEFAULT_HISTORY)
        self.plot_lock_var = tk.BooleanVar(value=False)
        self.plot_set_var = tk.StringVar(value="")
        self._plot_history = SignalHistory(self._plot_depth_samples())

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

        # Watch sets: save the current ticks under a name and swap arrangements.
        sets = ttk.Frame(box)
        sets.pack(fill="x", padx=4, pady=(3, 1))
        ttk.Label(sets, text="Set:").pack(side="left")
        self.plot_set_cb = ttk.Combobox(sets, width=12, state="readonly",
                                        textvariable=self.plot_set_var, values=[])
        self.plot_set_cb.pack(side="left", padx=(2, 2))
        self.plot_set_cb.bind("<<ComboboxSelected>>",
                              lambda _e: self._plot_apply_set(self.plot_set_var.get()))
        ttk.Button(sets, text="Save", width=5,
                   command=self._plot_save_set).pack(side="left")
        ttk.Button(sets, text="✕", width=2,
                   command=self._plot_delete_set).pack(side="left", padx=(2, 0))

        ctl = ttk.Frame(box)
        ctl.pack(fill="x", padx=4, pady=(1, 2))
        ttk.Label(ctl, text="Filter:").pack(side="left")
        self._plot_filter_var = tk.StringVar()
        fe = ttk.Entry(ctl, textvariable=self._plot_filter_var, width=10)
        fe.pack(side="left", padx=(2, 0))
        self._plot_filter_var.trace_add("write", lambda *_: self._plot_apply_filter())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self._plot_filter_var.set("")).pack(side="left", padx=(2, 0))
        ttk.Button(ctl, text="Clear", width=6,
                   command=self._plot_clear_selection).pack(side="right")
        # Pin: hard stop on every automatic tick/untick, for when you have the
        # arrangement you want and a reconnect must not touch it.
        ttk.Checkbutton(ctl, text="Pin", variable=self.plot_lock_var,
                        command=self._plot_set_lock).pack(side="right", padx=(0, 4))

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

        # How much the capture buffer keeps, and what that costs in RAM.
        ttk.Label(ctl, text="History:").pack(side="left", padx=(0, 2))
        hcb = ttk.Combobox(ctl, width=6, state="readonly",
                           values=[lab for lab, _ in PLOT_HISTORY],
                           textvariable=self.plot_hist_var)
        hcb.pack(side="left")
        hcb.bind("<<ComboboxSelected>>", lambda _e: self._plot_set_depth())
        self.plot_mem = ttk.Label(ctl, text="", foreground=UI["muted"],
                                  background=UI["card"], font=FONT_MONO)
        self.plot_mem.pack(side="left", padx=(4, 8))

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

        # ---- capture trigger ----
        # An intermittent fault is hard to catch because you have to be watching
        # at the moment it happens. Arm a condition and the plot freezes itself
        # on the instant it fires, with the full history either side already in
        # the buffer. See configgui/trigger.py.
        trg = ttk.Frame(parent)
        trg.pack(fill="x", pady=(2, 0))
        ttk.Label(trg, text="Trigger:", style="Muted.TLabel").pack(side="left")
        self.trig_sig_cb = ttk.Combobox(trg, width=22, values=list(SPECIALS))
        self.trig_sig_cb.set(ANY_FAULT)
        self.trig_sig_cb.pack(side="left", padx=(4, 2))
        self.trig_op_cb = ttk.Combobox(trg, width=7, state="readonly", values=OPS)
        self.trig_op_cb.set(">")
        self.trig_op_cb.pack(side="left", padx=2)
        self.trig_val_var = tk.StringVar(value="0")
        ttk.Entry(trg, textvariable=self.trig_val_var, width=8).pack(side="left", padx=2)

        self.trig_rearm_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(trg, text="Re-arm", variable=self.trig_rearm_var).pack(
            side="left", padx=(6, 2))
        self.trig_snap_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(trg, text="Auto-export", variable=self.trig_snap_var).pack(
            side="left", padx=2)

        self.trig_btn = ttk.Button(trg, text="Arm", width=7,
                                   command=self._trigger_toggle)
        self.trig_btn.pack(side="left", padx=(6, 0))
        self.trig_status = ttk.Label(trg, text="disarmed", foreground=UI["muted"],
                                     background=UI["card"], font=FONT_MONO)
        self.trig_status.pack(side="left", padx=8)

        # ---- review bar (only visible while a recorded session is loaded) ----
        self.plot_review_bar = ttk.Frame(parent)
        self.plot_review_lbl = ttk.Label(self.plot_review_bar, text="",
                                         foreground=UI["accent_hi"],
                                         background=UI["card"], font=FONT_MONO)
        self.plot_review_lbl.pack(side="left", padx=(0, 8))
        ttk.Button(self.plot_review_bar, text="Return to live", width=14,
                   command=self._plot_return_live).pack(side="right")
        self.plot_scrub = ttk.Scale(self.plot_review_bar, orient="horizontal",
                                    from_=0.0, to=1.0,
                                    command=self._plot_scrub_moved)
        self.plot_scrub.pack(side="left", fill="x", expand=True, padx=6)

        body = ttk.LabelFrame(parent, text="Live plot")
        body.pack(fill="both", expand=True, pady=(2, 0))
        self.plot_body = body
        self.plot = LivePlot(body)
        self.plot.set_normalise(self.plot_norm_var.get())
        self.plot.pack(fill="both", expand=True, padx=4, pady=4)

    # ---- capture depth ----
    def _plot_depth_samples(self):
        """Samples to keep per signal = chosen seconds x the current stream rate."""
        secs = PLOT_HISTORY_S.get(self.plot_hist_var.get(), 120)
        try:
            rate = float(self.telem_rate_var.get())
        except (TypeError, ValueError):
            rate = 20.0
        return int(secs * max(rate, 1.0))

    def _plot_set_depth(self):
        """Resize the capture buffer (History combo, or the telemetry rate moved).
        Existing samples are kept as far as the new depth allows."""
        if self._plot_history is None:
            return
        self._plot_history.set_depth(self._plot_depth_samples())
        self._plot_update_mem()
        self._save_settings()

    def _plot_update_mem(self):
        if self._plot_history is not None and hasattr(self, "plot_mem"):
            self.plot_mem.config(text=human_bytes(self._plot_history.bytes_used()))

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
        apply any queued selection + the filter. Cheap + idempotent."""
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
        self._plot_apply_pending()
        self._plot_apply_filter()

    def _plot_apply_pending(self, force=False):
        """Tick queued signals whose rows now exist, and DROP each one as it is
        applied. One-shot is the whole point: a queued name can tick a row at most
        once, so un-ticking is final and a schema refresh can't resurrect anything.
        `force` is for an explicit user action (picking a watch set), which is
        allowed through the Pin lock; automatic callers are not."""
        if not force and self.plot_lock_var.get():
            return                    # pinned: leave the ticks strictly alone
        still = []
        for name in self._plot_pending:
            if name in self._plot_rows:
                if name not in self._plot_selected:
                    self._plot_set_selected(name, True)
            else:
                still.append(name)    # row not discovered yet - keep waiting
        self._plot_pending = still

    def _apply_saved_plot_signals(self):
        """Called from _load_settings: adopt the remembered preferences (watch
        sets, lock, capture depth) and queue the remembered selection."""
        if self.plot is None:
            return
        self.plot_set_cb["values"] = sorted(self._plot_sets)
        self._plot_history.set_depth(self._plot_depth_samples())
        self._plot_update_mem()
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
            # The signal went away (schema prune on reconnect) - it was NOT
            # un-ticked, so requeue it and it returns if the board offers it again.
            if name not in self._plot_pending:
                self._plot_pending.append(name)
            self._plot_set_selected(name, False)
        row = self._plot_rows.pop(name, None)
        if row is not None:
            row["row"].destroy()

    # ---- selection ----
    def _plot_toggle_signal(self, name):
        """The only user-driven entry point. An explicit untick also drops the
        name from the queue, so nothing can put it back."""
        row = self._plot_rows.get(name)
        if row is None:
            return
        on = bool(row["var"].get())
        if not on and name in self._plot_pending:
            self._plot_pending.remove(name)
        self._plot_set_selected(name, on)

    def _plot_set_selected(self, name, on):
        row = self._plot_rows.get(name)
        if row is None:
            return
        if on and name not in self._plot_selected:
            # Prefer the colour this signal had last time, so a trace you know
            # keeps looking the same across ticks, reconnects and set changes.
            hint = self._plot_color_hint.get(name)
            if hint in self._plot_free_colors:
                self._plot_free_colors.remove(hint)
                color = hint
            elif self._plot_free_colors:
                color = self._plot_free_colors.pop(0)
            else:
                color = PLOT_COLORS[len(self._plot_selected) % len(PLOT_COLORS)]
            row["color"] = color
            self._plot_color_hint[name] = color
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
        self._plot_pending = []
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

    # ---- watch sets ----
    def _plot_save_set(self):
        """Store the current ticks under a name (overwrites a set of that name)."""
        if not self._plot_selected:
            messagebox.showinfo("Watch set", "Tick some signals first.")
            return
        name = simpledialog.askstring("Save watch set", "Name for this set:",
                                      initialvalue=self.plot_set_var.get(),
                                      parent=self.root)
        if not name or not name.strip():
            return
        name = name.strip()
        self._plot_sets[name] = sorted(self._plot_selected)
        self.plot_set_cb["values"] = sorted(self._plot_sets)
        self.plot_set_var.set(name)
        self._save_settings()

    def _plot_delete_set(self):
        name = self.plot_set_var.get()
        if name not in self._plot_sets:
            return
        del self._plot_sets[name]
        self.plot_set_cb["values"] = sorted(self._plot_sets)
        self.plot_set_var.set("")
        self._save_settings()

    def _plot_apply_set(self, name):
        """Make the ticks match a saved set exactly. An explicit user action, so
        it is allowed through the Pin lock; signals not yet discovered are queued
        and tick themselves when their row appears."""
        names = self._plot_sets.get(name)
        if names is None:
            return
        target = set(names)
        for n in list(self._plot_selected):
            if n not in target:
                self._plot_set_selected(n, False)
        self._plot_pending = [n for n in names if n not in self._plot_selected]
        self._plot_apply_pending(force=True)
        self._save_settings()

    def _plot_set_lock(self):
        self._save_settings()

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
        """Freeze-frame, not a stop: the capture keeps recording while paused, so
        the view resumes without a gap and a signal ticked while frozen still
        draws (its history is already there)."""
        self._plot_paused = not self._plot_paused
        if self._plot_paused:
            latest = self._plot_history.latest_time() if self._plot_history else 0.0
            self._plot_freeze_t = latest or time.time()
        else:
            self._plot_freeze_t = None
        self.plot_pause_btn.config(text="▶ Resume" if self._plot_paused else "⏸ Pause")

    def _plot_clear_data(self):
        """Drop the whole capture buffer (every signal), not just the visible
        traces - this is the one control that actually forgets data."""
        if self._plot_history is not None:
            self._plot_history.clear()
        if self._plot_paused:
            self._plot_toggle_pause()      # nothing left to freeze on
        self._plot_freeze_t = None
        self._plot_update_mem()

    # ---- capture trigger ----
    def _trigger_toggle(self):
        if self.trigger.armed:
            self.trigger.disarm()
        else:
            self.trigger.configure(self.trig_sig_cb.get(), self.trig_op_cb.get(),
                                   self.trig_val_var.get(),
                                   self.trig_rearm_var.get())
            self.trigger.on_fire(self._trigger_fired)
            self.trigger.arm()
        self._trigger_update()

    def _trigger_update(self):
        if self.trigger.armed:
            self.trig_btn.config(text="Disarm")
            self.trig_status.config(text="ARMED · " + self.trigger.describe(),
                                    foreground=UI["amber"])
        elif self.trigger.fire_count:
            self.trig_btn.config(text="Arm")
            self.trig_status.config(
                text="fired ×%d · %s" % (self.trigger.fire_count, self.trigger.reason),
                foreground=UI["red_hi"])
        else:
            self.trig_btn.config(text="Arm")
            self.trig_status.config(text="disarmed", foreground=UI["muted"])

    def _trigger_feed(self, values, t):
        """Every sample of every frame passes through here while armed."""
        if not self.trigger.armed:
            return
        for name, val in values.items():
            if self.trigger.test(name, val, t):
                break

    def _trigger_fired(self, trig, t, reason):
        """Freeze on the instant, mark it, and optionally export it. The history
        either side is already captured, which is the whole point: by the time
        you notice, the evidence would otherwise be gone."""
        self._plot_goto(t, "trigger: " + reason)
        self._log("TRIGGER %s  at %.3f s\n" % (reason, t), "sys")
        self._trigger_update()
        if self.trig_snap_var.get():
            self.root.after(50, self._trigger_autoexport)

    def _trigger_autoexport(self):
        """Write the bundle without a dialog - the point of auto-export is that
        it happens when nobody is at the keyboard."""
        import os
        from . import bundle as B
        try:
            os.makedirs(SESSIONS_DIR, exist_ok=True)
            path = os.path.join(SESSIONS_DIR, B.default_name())
            B.build(path, self._bundle_context())
            self._log("trigger bundle saved -> %s\n" % os.path.basename(path), "sys")
        except Exception as e:
            self._log("trigger bundle failed: %s\n" % e, "sys")

    # ---- review mode (a loaded session / log) ----
    def _plot_source(self):
        """Which buffer is on screen. Live capture keeps running underneath a
        review, so 'Return to live' loses nothing and costs nothing."""
        return self._plot_review if self._plot_review is not None else self._plot_history

    def _plot_load_history(self, hist, label):
        self._plot_review = hist
        self._plot_review_label = label
        self._plot_known.update({n: True for n in hist.names()})
        for n in hist.names():
            if n not in self._telem_order:
                self._telem_order.append(n)
        self._plot_refresh_signals()

        lo, hi = self._plot_span(hist)
        self.plot_scrub.config(from_=lo, to=hi)
        self.plot_scrub.set(hi)
        self._plot_paused = True
        self._plot_freeze_t = hi
        self.plot_pause_btn.config(text="▶ Resume")
        self.plot_review_lbl.config(text="REVIEWING  %s  (%.1f s)" % (label, hi - lo))
        self.plot_review_bar.pack(fill="x", pady=(2, 0), before=self.plot_body)
        self.plot_body.config(text="Reviewing a recording  ·  " + label)

    def _plot_return_live(self):
        self._plot_review = None
        self._plot_review_label = ""
        self.plot_review_bar.pack_forget()
        self.plot_body.config(text="Live plot")
        self._plot_paused = False
        self._plot_freeze_t = None
        self.plot_pause_btn.config(text="⏸ Pause")

    def _plot_span(self, hist):
        """Earliest and latest timestamp across every series in a buffer."""
        lo, hi = None, 0.0
        for n in hist.names():
            ts, _ = hist.window(n, 0.0, 1e18)
            if len(ts):
                lo = float(ts[0]) if lo is None else min(lo, float(ts[0]))
                hi = max(hi, float(ts[-1]))
        return (lo if lo is not None else 0.0), hi

    def _plot_scrub_moved(self, value):
        if self._plot_review is None:
            return
        try:
            self._plot_freeze_t = float(value)
        except (TypeError, ValueError):
            return
        self._plot_paused = True

    def _plot_goto(self, t, reason=""):
        """Freeze the view at a given board time - used by a trigger firing and
        by clicking a row in the Events timeline."""
        if self.plot is None:
            return
        self._plot_paused = True
        self._plot_freeze_t = t
        self.plot_pause_btn.config(text="▶ Resume")
        if self._plot_review is not None:
            try:
                self.plot_scrub.set(t)
            except tk.TclError:
                pass
        if reason:
            self.plot_status.config(text=reason[:48], foreground=UI["amber"])

    # ---- data feed (hook called by TelemMixin per sample) ----
    def _plot_feed(self, name, val, t):
        """Record EVERY scalar sample, ticked or not, paused or not. That is what
        back-fill and gapless resume are built on; non-numeric (array) values are
        skipped."""
        if self._plot_history is None:
            return
        try:
            v = float(val)
        except (TypeError, ValueError):
            return
        self._plot_history.push(name, t, v)

    # ---- repaint loop ----
    def _plot_loop(self):
        if self.plot is not None:
            # Live value chips beside each plotted signal (exact streamed string).
            # Frozen while paused so the numbers agree with the frozen chart.
            if not self._plot_paused:
                for name in self._plot_selected:
                    row = self._plot_rows.get(name)
                    if row is not None:
                        row["val"].config(text=self._telem_last.get(name) or "")
            self.plot_btn.config(text="■ Stop stream" if self._telem_streaming
                                 else "▶ Start stream")
            n = len(self._plot_selected)
            if self._plot_review is not None:
                self.plot_status.config(
                    text="%d plotted . REVIEW" % n, foreground=UI["accent_hi"])
            elif self._plot_paused:
                age = max(0.0, (self._plot_history.latest_time() or 0.0)
                          - (self._plot_freeze_t or 0.0))
                self.plot_status.config(text=f"{n} plotted · frozen -{age:.0f}s",
                                        foreground=UI["amber"])
            elif self._telem_streaming:
                self.plot_status.config(text=f"{n} plotted · live",
                                        foreground=UI["green"])
            else:
                self.plot_status.config(text=f"{n} plotted · stream off",
                                        foreground=UI["muted"])
            self.plot.set_window(float(self.plot_window_var.get()))
            self.plot.redraw(self._plot_source(), self._plot_freeze_t)
            # The buffer only grows as new signals appear; a second is plenty.
            self._plot_mem_tick = (self._plot_mem_tick + 1) % 10
            if self._plot_mem_tick == 0:
                self._plot_update_mem()
        self.root.after(PLOT_REDRAW_MS, self._plot_loop)
