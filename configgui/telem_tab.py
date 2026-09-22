"""
configgui.telem_tab  --  the Live Telemetry tab (schema + live #T frames).

TelemMixin discovers every streamed signal (from `telem list` and from the stream
itself), files each under a labelled, collapsible group, and updates values in
place as `#T` frames arrive. It also mirrors the supervisor state / HV-enable
signals into the top banner, and taps each frame into the Plot tab (via the small
_plot_feed / _plot_note_signal hooks) so plotting shares the one telemetry stream.
"""

import time

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_UI_B, FONT_MONO
from .protocol import (
    TELEM_RATES, TELEM_GROUP_ORDER, telem_group_for,
    STATE_SIGNAL, FAULT_SIGNAL, DRIVE_MODE_SIGNAL, _SCHEMA_RE,
)


class TelemMixin:
    # ---- Live Telemetry tab ----
    def _build_telem_panel(self, parent):
        # Control row.
        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", padx=8, pady=(4, 2))

        self.telem_btn = ttk.Button(ctl, text="▶ Start stream", style="Accent.TButton",
                                    command=self.toggle_telem)
        self.telem_btn.pack(side="left")

        ttk.Label(ctl, text="Rate:").pack(side="left", padx=(10, 2))
        self.telem_rate_cb = ttk.Combobox(ctl, width=5, state="readonly",
                                          values=TELEM_RATES, textvariable=self.telem_rate_var)
        self.telem_rate_cb.pack(side="left")
        self.telem_rate_cb.bind("<<ComboboxSelected>>", lambda _e: self._on_rate_change())
        ttk.Label(ctl, text="Hz").pack(side="left", padx=(2, 8))

        self.telem_refresh_btn = ttk.Button(ctl, text="Refresh signals",
                                            command=self.fetch_telem_schema)
        self.telem_refresh_btn.pack(side="left", padx=4)

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Button(ctl, text="⊞ Expand", width=8,
                   command=lambda: self._telem_set_open(True)).pack(side="left", padx=(0, 2))
        ttk.Button(ctl, text="⊟ Collapse", width=9,
                   command=lambda: self._telem_set_open(False)).pack(side="left")

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Label(ctl, text="Filter:").pack(side="left", padx=(0, 2))
        self.filter_var = tk.StringVar()
        fe = ttk.Entry(ctl, textvariable=self.filter_var, width=18)
        fe.pack(side="left")
        self.filter_var.trace_add("write", lambda *_: self._apply_filter())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self.filter_var.set("")).pack(side="left", padx=(2, 0))

        self.telem_status = ttk.Label(ctl, text="idle", foreground=UI["muted"],
                                      background=UI["card"], font=FONT_MONO)
        self.telem_status.pack(side="right")

        # The live grid: every streamed signal, filed under a labelled group and
        # updating in place. Groups are collapsible parent rows (see _relayout_telem).
        body = ttk.LabelFrame(parent, text="Live values (grouped by function · every signal the board streams)")
        body.pack(fill="both", expand=True, padx=8, pady=(2, 4))

        cols = ("value", "type")
        self.tree = ttk.Treeview(body, columns=cols, show="tree headings",
                                 selectmode="browse")
        self.tree.heading("#0", text="Signal")
        self.tree.heading("value", text="Value")
        self.tree.heading("type", text="Type")
        self.tree.column("#0", width=300, anchor="w", stretch=False)
        self.tree.column("value", width=360, anchor="w")
        self.tree.column("type", width=70, anchor="center", stretch=False)
        self.tree.tag_configure("changed", foreground=UI["accent_hi"])
        self.tree.tag_configure("stale", foreground=UI["muted"])
        # Group header rows: a raised, bold, accent-coloured band per section.
        self.tree.tag_configure("group", background=UI["elev"],
                                foreground=UI["accent_hi"], font=FONT_UI_B)

        tvsb = ttk.Scrollbar(body, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=tvsb.set)
        tvsb.pack(side="right", fill="y")
        self.tree.pack(side="left", fill="both", expand=True, padx=4, pady=4)

        self.telem_empty = ttk.Label(
            parent, style="Muted.TLabel",
            text="(connect; signals auto-discover. Press ▶ Start stream to watch values update.)")
        # shown/removed dynamically
        self._telem_empty_shown = False
        self._show_telem_empty(True)

    def _show_telem_empty(self, show):
        if show and not self._telem_empty_shown:
            self.telem_empty.pack(before=self.tree.master, padx=12, pady=2, anchor="w")
            self._telem_empty_shown = True
        elif not show and self._telem_empty_shown:
            self.telem_empty.pack_forget()
            self._telem_empty_shown = False

    # ---- telemetry commands ----
    def fetch_telem_schema(self):
        """Ask the board for its signal schema so the live grid shows every
        signal (even ones that rarely change) before/without streaming."""
        if not (self.ser and self.ser.is_open):
            return
        self._send("telem list")

    def toggle_telem(self):
        if self._telem_streaming:
            self._send("telem off")
        else:
            self._send(f"telem rate {self.telem_rate_var.get()}")
            self._send("telem on")
            self.notebook.select(self.tab_telem)

    def _on_rate_change(self):
        self._save_settings()
        if self._telem_streaming:
            self._send(f"telem rate {self.telem_rate_var.get()}")

    # ---- schema ----
    def _begin_schema(self):
        self._schema_seen = set()

    def _parse_schema_line(self, line):
        m = _SCHEMA_RE.match(line)
        if not m:
            return False
        name, typ, length = m.group(1), m.group(2), int(m.group(3))
        self._schema_seen.add(name)
        self._ensure_telem_row(name, typ, length)
        return True

    def _finish_schema(self):
        # Drop rows that no longer exist on the board (a signal removed from the
        # .def). Only prune when we actually received a non-empty schema.
        if not getattr(self, "_schema_seen", None):
            return
        for name in list(self._telem_order):
            if name not in self._schema_seen:
                self._remove_telem_row(name)
        self._relayout_telem()
        self._plot_refresh_signals()

    def _telem_group_parent(self, label):
        """Return the tree iid of a group header row, creating it if needed."""
        parent = self._telem_groups.get(label)
        if parent is None:
            parent = f"__grp__{label}"
            self.tree.insert("", "end", iid=parent, text=label,
                             values=("", ""), open=True, tags=("group",))
            self._telem_groups[label] = parent
        return parent

    def _ensure_telem_row(self, name, typ="?", length=1):
        if name in self._telem_order:
            if typ != "?":
                self.tree.set(name, "type", typ if length <= 1 else f"{typ}[{length}]")
                self._plot_note_signal(name, length)
            return
        self._show_telem_empty(False)
        label = telem_group_for(name, length > 1)
        parent = self._telem_group_parent(label)
        disp_type = typ if length <= 1 else f"{typ}[{length}]"
        self.tree.insert(parent, "end", iid=name, text=name,
                         values=("—", disp_type), tags=("stale",))
        self._telem_order.append(name)
        self._telem_last[name] = None
        self._telem_members.setdefault(label, []).append(name)
        self._plot_note_signal(name, length)
        # During a schema dump we get one call per signal; defer the (re)sort to
        # _finish_schema so we lay the whole grid out once instead of N times.
        if not self._schema_collecting:
            self._relayout_telem()
            self._plot_refresh_signals()

    def _remove_telem_row(self, name):
        try:
            self.tree.delete(name)
        except tk.TclError:
            pass
        if name in self._telem_order:
            self._telem_order.remove(name)
        self._telem_last.pop(name, None)
        for members in self._telem_members.values():
            if name in members:
                members.remove(name)
                break

    def _handle_telem_frame(self, line):
        # "#T tick=123 User_LED_1=1 APPS=12,0,255,..."
        self._telem_frames += 1
        t = time.time()
        for tok in line.split()[1:]:
            if "=" not in tok:
                continue
            name, _, val = tok.partition("=")
            if not name:
                continue
            if name not in self._telem_order:
                self._ensure_telem_row(name)     # self-discover if no schema yet
            # Tap every sample into the Plot tab (cheap no-op if not selected /
            # not numeric); done for unchanged values too so a flat trace stays live.
            self._plot_feed(name, val, t)
            # Banner elements (state / fault / mode chip / HV lamps) reflect the
            # LATEST value every frame, NOT only on a change. A constant signal
            # (e.g. Velocity_Mode_Active held at 1) only "changes" once, and if that
            # first edge is ever missed - row pre-seeded during schema discovery, or
            # the widget built after the first frames - the banner would stay blank
            # until forced to change. These updaters are cheap + idempotent, so
            # re-asserting them each frame is safe and keeps the banner truthful.
            if name == STATE_SIGNAL:
                self._update_state_banner(val)
            elif name == FAULT_SIGNAL:
                self._update_fault_code(val)
            elif name == DRIVE_MODE_SIGNAL:
                self._update_mode_chip(val)
            elif name in self.enable_lamps:
                self._update_enable_lamp(name, val)
            # The tree row itself only needs touching (and the "changed" flash) when
            # the value actually moves.
            if self._telem_last.get(name) != val:
                self._telem_last[name] = val
                self.tree.set(name, "value", val)
                self.tree.item(name, tags=("changed",))

    def _telem_tick(self):
        # Once a second: refresh the measured stream rate and fade unchanged rows.
        self._telem_hz = self._telem_frames
        self._telem_frames = 0
        if self._telem_streaming:
            self.telem_status.config(
                text=f"streaming · {self._telem_hz} fps · {len(self._telem_order)} signals",
                foreground=UI["green"])
        elif self._telem_order:
            self.telem_status.config(
                text=f"stopped · {len(self._telem_order)} signals", foreground=UI["muted"])
        else:
            self.telem_status.config(text="idle", foreground=UI["muted"])
        # Fade rows back to neutral so only just-changed values stay highlighted.
        for name in self._telem_order:
            if self.tree.set(name, "value") != "—":
                self.tree.item(name, tags=())
        self.root.after(1000, self._telem_tick)

    def _update_telem_button(self):
        self.telem_btn.config(text="■ Stop stream" if self._telem_streaming
                              else "▶ Start stream")

    def _apply_filter(self):
        self._telem_filter = self.filter_var.get().strip().lower()
        self._relayout_telem()

    def _relayout_telem(self):
        """Lay the tree out as ordered, sorted, filtered groups. Idempotent: it
        reattaches existing rows (never destroys them), so it's cheap to call on
        every discovery / filter change and it always leaves the grid tidy."""
        flt = self._telem_filter
        pos = 0
        for label in TELEM_GROUP_ORDER:
            members = self._telem_members.get(label)
            if not members:
                # Group emptied out (e.g. its last signal was pruned): hide its
                # header row if we ever created one.
                gone = self._telem_groups.get(label)
                if gone is not None:
                    self.tree.detach(gone)
                continue
            parent = self._telem_group_parent(label)
            visible = sorted((n for n in members if not flt or flt in n.lower()),
                             key=str.lower)
            vis_set = set(visible)
            for idx, n in enumerate(visible):
                self.tree.reattach(n, parent, idx)
            for n in members:
                if n not in vis_set:
                    self.tree.detach(n)
            if visible:
                self.tree.reattach(parent, "", pos)
                pos += 1
                self.tree.item(parent, values=(f"{len(visible)} signals", ""))
            else:
                self.tree.detach(parent)   # whole group filtered out

    def _telem_set_open(self, is_open):
        for parent in self._telem_groups.values():
            try:
                self.tree.item(parent, open=is_open)
            except tk.TclError:
                pass
