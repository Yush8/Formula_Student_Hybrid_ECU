"""
configgui.sniffer_tab  --  the CAN Bus (raw sniffer) tab.

SnifferMixin shows every CAN id the board sees on either bus, self-discovered from
the `#C` stream: latest 8 data bytes, a frame counter, a measured message rate, and
how long ago it was last seen. Read-only - it never sends CAN.

NEW (debug aid): a latched "oldest age seen" headline. The per-row Age column shows
the *current* staleness, which flickers; the headline holds the single WORST age
sampled this session (and which id it was) so you can judge how bad CAN delays /
dropouts actually get. It is GUI-side and session-scoped: it resets whenever the
sniffer stream is turned on or off, on Clear, and via its own Reset button. Raising
the snapshot rate (`Rate`) gives the latch finer resolution on short delays.
"""

import time

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_MONO, FONT_UI_SM
from .protocol import SNIFF_RATES, SNIFF_STALE_MS, _SNIFF_ROW_RE


class SnifferMixin:
    # ---- CAN Bus (sniffer) tab ----
    def _build_sniffer_panel(self, parent):
        """A live CAN sniffer: every id the board sees on either bus, with its
        latest 8 data bytes, a frame counter, a measured message rate and how long
        ago it was last seen. Rows self-discover from the `cansniff` stream, so no
        per-message setup is ever needed. This is a read-only view of the raw bus -
        it never sends CAN, and is independent of the model demux."""
        # Latched worst-age state (session-scoped; see the module docstring).
        self._sniff_peak_ms = 0
        self._sniff_peak_key = None

        # Control row.
        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", padx=8, pady=(4, 2))

        self.sniff_btn = ttk.Button(ctl, text="▶ Start stream", style="Accent.TButton",
                                    command=self.toggle_sniffer)
        self.sniff_btn.pack(side="left")

        ttk.Label(ctl, text="Rate:").pack(side="left", padx=(10, 2))
        self.sniff_rate_cb = ttk.Combobox(ctl, width=5, state="readonly",
                                          values=SNIFF_RATES, textvariable=self.sniff_rate_var)
        self.sniff_rate_cb.pack(side="left")
        self.sniff_rate_cb.bind("<<ComboboxSelected>>", lambda _e: self._on_sniff_rate_change())
        ttk.Label(ctl, text="Hz").pack(side="left", padx=(2, 8))

        self.sniff_clear_btn = ttk.Button(ctl, text="Clear", command=self.clear_sniffer)
        self.sniff_clear_btn.pack(side="left", padx=4)
        self.sniff_refresh_btn = ttk.Button(ctl, text="Snapshot", command=self.refresh_sniffer)
        self.sniff_refresh_btn.pack(side="left", padx=4)

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Label(ctl, text="Filter:").pack(side="left", padx=(0, 2))
        self.sniff_filter_var = tk.StringVar()
        fe = ttk.Entry(ctl, textvariable=self.sniff_filter_var, width=14)
        fe.pack(side="left")
        self.sniff_filter_var.trace_add("write", lambda *_: self._apply_sniff_filter())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self.sniff_filter_var.set("")).pack(side="left", padx=(2, 0))

        self.sniff_status = ttk.Label(ctl, text="idle", foreground=UI["muted"],
                                      background=UI["card"], font=FONT_MONO)
        self.sniff_status.pack(side="right")

        # Latched "oldest age" headline: the worst staleness sampled this session.
        peakbar = ttk.Frame(parent)
        peakbar.pack(fill="x", padx=8, pady=(0, 2))
        ttk.Label(peakbar, text="Oldest age seen (latched):",
                  style="Muted.TLabel").pack(side="left")
        self.sniff_peak_lbl = tk.Label(peakbar, text="—", font=("Consolas", 11, "bold"),
                                       background=UI["elev"], foreground=UI["muted"],
                                       padx=10, pady=2)
        self.sniff_peak_lbl.pack(side="left", padx=(6, 0))
        tk.Button(peakbar, text="Reset peak", command=self._reset_sniff_peak,
                  background=UI["elev"], foreground=UI["muted"],
                  activebackground=UI["border"], activeforeground=UI["fg"],
                  font=FONT_UI_SM, padx=8, pady=1, relief="flat", bd=0,
                  cursor="hand2", highlightthickness=0).pack(side="left", padx=(8, 0))
        ttk.Label(peakbar, style="Muted.TLabel",
                  text="  · holds the worst inter-frame gap seen; resets on stream on/off + Clear"
                  ).pack(side="left", padx=6)

        # Ids the firmware routes but that have never arrived: a node that is off
        # or unwired. Names come from can1/can2_messages.def via configgui.candb.
        self.sniff_missing_lbl = ttk.Label(peakbar, text="", foreground=UI["muted"],
                                           background=UI["card"], font=FONT_MONO)
        self.sniff_missing_lbl.pack(side="right")

        # The live trace grid: one row per (bus, id), updating in place.
        body = ttk.LabelFrame(parent, text="Live bus (every CAN id the board receives)")
        body.pack(fill="both", expand=True, padx=8, pady=(2, 4))

        cols = ("bus", "id", "name", "dlc", "data", "count", "rate", "age")
        self.tree_sniff = ttk.Treeview(body, columns=cols, show="headings",
                                       selectmode="browse")
        headings = {"bus": "Bus", "id": "ID", "name": "Name (from the .def)",
                    "dlc": "DLC", "data": "Data (hex)",
                    "count": "Count", "rate": "Hz", "age": "Age ms"}
        widths = {"bus": 45, "id": 70, "name": 200, "dlc": 45, "data": 260,
                  "count": 80, "rate": 60, "age": 70}
        anchors = {"data": "w", "id": "w", "name": "w"}
        for c in cols:
            self.tree_sniff.heading(c, text=headings[c])
            self.tree_sniff.column(c, width=widths[c], anchor=anchors.get(c, "center"),
                                   stretch=(c == "data"))
        self.tree_sniff.tag_configure("changed", foreground=UI["accent_hi"])
        self.tree_sniff.tag_configure("stale", foreground=UI["muted"])
        # An id the firmware routes nothing for is unexplained traffic. Worth
        # seeing, because a hex dump alone can never tell you it is unexpected.
        self.tree_sniff.tag_configure("unknown", foreground=UI["amber"])
        # Data column reads best monospaced. Copy the base Treeview layout onto the
        # custom style so the rows still render (a bare custom style can lose it).
        style = ttk.Style()
        try:
            style.layout("Sniff.Treeview", style.layout("Treeview"))
        except tk.TclError:
            pass
        style.configure("Sniff.Treeview", font=FONT_MONO)
        self.tree_sniff.configure(style="Sniff.Treeview")

        svsb = ttk.Scrollbar(body, orient="vertical", command=self.tree_sniff.yview)
        self.tree_sniff.configure(yscrollcommand=svsb.set)
        svsb.pack(side="right", fill="y")
        self.tree_sniff.pack(side="left", fill="both", expand=True, padx=4, pady=4)

        self.sniff_empty = ttk.Label(
            parent, style="Muted.TLabel",
            text="(connect, then press ▶ Start stream to watch every CAN id on both buses.)")
        self._sniff_empty_shown = False
        self._show_sniff_empty(True)

    def _show_sniff_empty(self, show):
        if show and not self._sniff_empty_shown:
            self.sniff_empty.pack(before=self.tree_sniff.master, padx=12, pady=2, anchor="w")
            self._sniff_empty_shown = True
        elif not show and self._sniff_empty_shown:
            self.sniff_empty.pack_forget()
            self._sniff_empty_shown = False

    # ---- CAN sniffer commands ----
    def toggle_sniffer(self):
        if self._sniff_streaming:
            self._send("cansniff off")
            self._reset_sniff_peak()          # fresh latch each session (on + off)
        else:
            self._reset_sniff_peak()
            self._send(f"cansniff rate {self.sniff_rate_var.get()}")
            self._send("cansniff on")
            self.notebook.select(self.tab_sniffer)

    def _on_sniff_rate_change(self):
        self._save_settings()
        if self._sniff_streaming:
            self._send(f"cansniff rate {self.sniff_rate_var.get()}")

    def clear_sniffer(self):
        """Tell the board to forget every captured id, and empty the local grid so
        it rebuilds from scratch (handy after moving a probe to another bus)."""
        if self.ser and self.ser.is_open:
            self._send("cansniff clear")
        for key in list(self._sniff_order):
            try:
                self.tree_sniff.delete(key)
            except tk.TclError:
                pass
        self._sniff_order.clear()
        self._sniff_meta.clear()
        self._sniff_data.clear()
        self._reset_sniff_peak()
        self._show_sniff_empty(True)

    def refresh_sniffer(self):
        """One-shot dump of the current table without streaming."""
        if self.ser and self.ser.is_open:
            self._send("cansniff list")

    def _update_sniffer_button(self):
        self.sniff_btn.config(text="■ Stop stream" if self._sniff_streaming
                              else "▶ Start stream")

    # ---- oldest-age latch ----
    def _reset_sniff_peak(self):
        self._sniff_peak_ms = 0
        self._sniff_peak_key = None
        if hasattr(self, "sniff_peak_lbl"):
            self._update_sniff_peak_label()

    def _update_sniff_peak_label(self):
        if self._sniff_peak_key is None:
            self.sniff_peak_lbl.config(text="—", foreground=UI["muted"])
            return
        bus_s, id_hex = self._sniff_peak_key.split(":")
        ms = self._sniff_peak_ms
        # Colour by severity so "how bad" reads at a glance.
        if ms > SNIFF_STALE_MS:
            colour = UI["red_hi"]
        elif ms > 200:
            colour = UI["amber"]
        else:
            colour = UI["green"]
        self.sniff_peak_lbl.config(
            text=f"{ms} ms   (bus{bus_s} 0x{id_hex})", foreground=colour)

    # ---- CAN sniffer: live bus rows ----
    def _handle_sniffer_frame(self, line):
        # "#C <bus> <idhex> <count> <age_ms> <dlc> <datahex>"
        m = _SNIFF_ROW_RE.match(line)
        if not m:
            return
        bus = int(m.group(1))
        id_hex = m.group(2).upper()
        count = int(m.group(3))
        age = int(m.group(4))
        dlc = int(m.group(5))
        data_hex = m.group(6).upper()
        key = f"{bus}:{id_hex}"
        now = time.time()

        # Guard against an unsigned-underflow glitch from the firmware: if a frame
        # arrives during the snapshot pass, `now - last_seen` can wrap to a value
        # near 2**32 (e.g. 4294967295). No real age lands in the top half of the
        # uint32 range, so treat those as "just arrived" (age 0) for both display
        # and the latch. (The firmware clamps this at source; this is belt-and-braces.)
        if age >= 0x80000000:
            age = 0

        # Latch the worst (oldest) age seen this session + which id it was.
        if age > self._sniff_peak_ms:
            self._sniff_peak_ms = age
            self._sniff_peak_key = key
            self._update_sniff_peak_label()

        # Measured message rate from the frame-count delta between snapshots.
        meta = self._sniff_meta.get(key)
        rate = 0.0
        if meta:
            dc = count - meta["count"]
            dt = now - meta["t"]
            if dt > 0 and dc >= 0:
                rate = dc / dt
        self._sniff_meta[key] = {"count": count, "t": now, "rate": rate}

        data_disp = " ".join(data_hex[i:i + 2] for i in range(0, len(data_hex), 2))
        rate_disp = f"{rate:.0f}" if rate >= 10 else f"{rate:.1f}"
        # The firmware's own name for this id, read straight from
        # can1/can2_messages.def - so it can never drift from what the board
        # actually routes. Blank means nothing routes it.
        name = self.candb.name_for(bus, int(id_hex, 16))
        values = (bus, f"0x{id_hex}", name or "(not routed)", dlc, data_disp,
                  count, rate_disp, age)

        changed = self._sniff_data.get(key) != data_disp
        self._sniff_data[key] = data_disp

        base = () if name else ("unknown",)
        if key in self._sniff_order:
            self.tree_sniff.item(key, values=values,
                                 tags=("changed",) if changed else base)
        else:
            self._show_sniff_empty(False)
            self.tree_sniff.insert("", "end", iid=key, values=values,
                                   tags=("changed",))
            self._sniff_order.append(key)
            self._apply_sniff_filter()  # (re)orders by bus then id and applies filter
        self._sniff_frames += 1

    def _sniff_sort_key(self, key):
        bus_s, id_s = key.split(":")
        try:
            return (int(bus_s), int(id_s, 16))
        except ValueError:
            return (0, 0)

    def _apply_sniff_filter(self):
        self._sniff_filter = self.sniff_filter_var.get().strip().lower()
        idx = 0
        for key in sorted(self._sniff_order, key=self._sniff_sort_key):
            if not self._sniff_filter or self._sniff_filter in key.lower():
                try:
                    self.tree_sniff.reattach(key, "", idx)
                    idx += 1
                except tk.TclError:
                    pass
            else:
                self.tree_sniff.detach(key)

    def _sniff_tick(self):
        # Once a second: measured row throughput + fade/grey rows by freshness.
        self._sniff_hz = self._sniff_frames
        self._sniff_frames = 0
        if self._sniff_streaming:
            self.sniff_status.config(
                text=f"streaming · {len(self._sniff_order)} ids · {self._sniff_hz} rows/s",
                foreground=UI["green"])
        elif self._sniff_order:
            self.sniff_status.config(
                text=f"stopped · {len(self._sniff_order)} ids", foreground=UI["muted"])
        else:
            self.sniff_status.config(text="idle", foreground=UI["muted"])
        # Drop the just-changed highlight; grey ids that have gone stale.
        for key in self._sniff_order:
            try:
                age = int(self.tree_sniff.set(key, "age"))
            except (ValueError, tk.TclError):
                age = 0
            if age > SNIFF_STALE_MS:
                tags = ("stale",)
            else:
                name = self.tree_sniff.set(key, "name")
                tags = () if name and name != "(not routed)" else ("unknown",)
            self.tree_sniff.item(key, tags=tags)
        self._sniff_update_missing()
        self.root.after(1000, self._sniff_tick)

    # ---- expected-but-absent ids ----
    def _sniff_seen_ids(self):
        """bus -> set of ids actually observed, for the missing-id check."""
        out = {1: set(), 2: set()}
        for key in self._sniff_order:
            bus_s, id_s = key.split(":")
            try:
                out.setdefault(int(bus_s), set()).add(int(id_s, 16))
            except ValueError:
                pass
        return out

    def _sniffer_seen_ids(self):
        return self._sniff_seen_ids()

    def _sniffer_export_rows(self):
        """Every row as plain tuples, for the debug bundle's can.csv."""
        rows = []
        for key in self._sniff_order:
            try:
                v = self.tree_sniff.item(key, "values")
                bus_s, id_s = key.split(":")
                rows.append((int(bus_s), int(id_s, 16), v[2], v[5], v[6], v[7], v[4]))
            except (ValueError, IndexError, tk.TclError):
                continue
        return rows

    def _sniff_update_missing(self):
        """An id the firmware expects but has never seen is usually a node that
        is off or unwired. The sniffer alone cannot tell you this, because it
        only ever shows what DID arrive."""
        if not self._sniff_streaming:
            return
        missing = self.candb.missing(self._sniff_seen_ids())
        if not missing:
            self.sniff_missing_lbl.config(
                text="all %d expected ids seen" % self.candb.count(),
                foreground=UI["green"])
            return
        # Ids only, grouped by bus: the names are in the grid already, and a long
        # list here would run off the end of the window.
        by_bus = {}
        for bus, cid, _name in missing:
            by_bus.setdefault(bus, []).append("0x%03X" % cid)
        parts = []
        for bus in sorted(by_bus):
            ids = by_bus[bus]
            shown = ", ".join(ids[:4]) + ("…" if len(ids) > 4 else "")
            parts.append("bus%d %s" % (bus, shown))
        self.sniff_missing_lbl.config(
            text="NOT SEEN (%d): %s" % (len(missing), "  ".join(parts)),
            foreground=UI["amber"])
