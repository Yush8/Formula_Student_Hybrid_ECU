"""
configgui.events_tab  --  the Events timeline: what changed, and when.

The Live Telemetry tab answers "what is this signal now". This one answers the
question you actually ask after something goes wrong: "what happened, in what
order, just before it?"

Every row is one `#E` line from the board, raised on the model step the instant a
watched discrete signal changed - so the ordering is exact. Values are decoded
through the same tables the banner uses, which is the difference between

    State_Enum 5 -> 6                and        SUPERVISOR   READY -> ERROR
    Fault_Code 0 -> 20                          Fault_Code   no fault -> BMS zero-limit

Rows are coloured by severity (see events_model): red for something that latched
or failed, amber for an enable dropping out or a state machine leaving drive,
plain for normal progress. "Errors only" hides the rest, which on a busy startup
is the difference between five rows and fifty.

Clicking a row jumps the Plot tab to that instant - the timeline tells you WHEN,
the plot tells you WHAT the analogue signals were doing at that moment, and going
between them is one click rather than a hunt.
"""

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_MONO, FONT_UI_B
from .events_model import ERROR, WARN


class EventsMixin:
    # ---- Events tab ----
    def _build_events_panel(self, parent):
        self._events_errors_only = tk.BooleanVar(value=False)
        self._events_follow = tk.BooleanVar(value=True)
        self._events_filter_var = tk.StringVar()

        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", padx=8, pady=(4, 2))

        ttk.Button(ctl, text="Fetch board history",
                   command=self.cmd_events_list).pack(side="left")
        ttk.Button(ctl, text="Clear", command=self._events_clear).pack(
            side="left", padx=6)

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Checkbutton(ctl, text="Errors only", variable=self._events_errors_only,
                        command=self._events_relayout).pack(side="left")
        ttk.Checkbutton(ctl, text="Follow", variable=self._events_follow).pack(
            side="left", padx=6)

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Label(ctl, text="Filter:").pack(side="left", padx=(0, 2))
        fe = ttk.Entry(ctl, textvariable=self._events_filter_var, width=18)
        fe.pack(side="left")
        self._events_filter_var.trace_add("write", lambda *_: self._events_relayout())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self._events_filter_var.set("")).pack(
                       side="left", padx=(2, 0))

        self.events_status = ttk.Label(ctl, text="no events", foreground=UI["muted"],
                                       background=UI["card"], font=FONT_MONO)
        self.events_status.pack(side="right")

        body = ttk.LabelFrame(
            parent, text="Timeline  ·  every discrete change, in the order the board saw it")
        body.pack(fill="both", expand=True, padx=8, pady=(2, 4))

        cols = ("t", "sev", "what", "from", "to")
        t = ttk.Treeview(body, columns=cols, show="headings", selectmode="browse")
        for key, label, width, anchor, stretch in (
                ("t", "Board time", 90, "e", False),
                ("sev", "", 54, "center", False),
                ("what", "Signal", 230, "w", False),
                ("from", "From", 170, "w", True),
                ("to", "To", 170, "w", True)):
            t.heading(key, text=label)
            t.column(key, width=width, anchor=anchor, stretch=stretch)
        t.tag_configure(ERROR, foreground=UI["red_hi"], font=FONT_UI_B)
        t.tag_configure(WARN, foreground=UI["amber"])
        t.tag_configure("info", foreground=UI["fg"])
        t.bind("<<TreeviewSelect>>", self._events_on_select)

        vsb = ttk.Scrollbar(body, orient="vertical", command=t.yview)
        t.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        t.pack(side="left", fill="both", expand=True, padx=4, pady=4)
        self.events_tree = t

        ttk.Label(parent, style="Muted.TLabel",
                  text="   Click a row to freeze the Plot tab at that instant.").pack(
                      anchor="w", padx=12, pady=(0, 4))

        self._events_rowids = {}       # tree iid -> event row

    # ---- commands ----
    def cmd_events_list(self):
        """Ask the board to re-emit its stored recent events. The firmware keeps
        a small ring, so after a reconnect the timeline is not empty - you still
        see what happened while the console was closed."""
        if self.ser and self.ser.is_open:
            self._send("events list")

    def _events_clear(self):
        self.events.clear()
        self.events_tree.delete(*self.events_tree.get_children())
        self._events_rowids = {}
        self._events_update_status()

    # ---- feed (called by the RX dispatcher via serial_io) ----
    def _events_append(self, row):
        if not self._events_visible(row):
            self._events_update_status()
            return
        iid = self._events_insert(row)
        if self._events_follow.get() and iid:
            self.events_tree.see(iid)
        self._events_update_status()

    def _events_insert(self, row):
        iid = "ev%d" % (len(self._events_rowids) + 1)
        self.events_tree.insert(
            "", "end", iid=iid,
            values=("%.3f s" % row["t"],
                    {"error": "FAIL", "warn": "WARN"}.get(row["severity"], ""),
                    row["label"], row["old_text"], row["new_text"]),
            tags=(row["severity"],))
        self._events_rowids[iid] = row
        return iid

    def _events_visible(self, row):
        if self._events_errors_only.get() and row["severity"] != ERROR:
            return False
        flt = self._events_filter_var.get().strip().lower()
        if not flt:
            return True
        hay = " ".join((row["label"], row["signal"],
                        str(row["old_text"]), str(row["new_text"]))).lower()
        return flt in hay

    def _events_relayout(self):
        """Rebuild from the model when a filter changes. The model keeps every
        event; the tree only ever shows the ones passing the filter, so toggling
        'Errors only' never loses anything."""
        self.events_tree.delete(*self.events_tree.get_children())
        self._events_rowids = {}
        last = None
        for row in self.events.rows:
            if self._events_visible(row):
                last = self._events_insert(row)
        if self._events_follow.get() and last:
            self.events_tree.see(last)
        self._events_update_status()

    def _events_update_status(self):
        c = self.events.counts()
        total = len(self.events.rows)
        shown = len(self._events_rowids)
        # Badge on the tab, so a fault is visible from whatever page you are on.
        if c.get(ERROR):
            self._shell_badge("events", str(c[ERROR]), UI["red_hi"])
        elif c.get(WARN):
            self._shell_badge("events", str(c[WARN]), UI["amber"])
        else:
            self._shell_badge("events", "")
        if not total:
            self.events_status.config(text="no events", foreground=UI["muted"])
            return
        text = "%d shown / %d  ·  %d errors  %d warnings" % (
            shown, total, c.get(ERROR, 0), c.get(WARN, 0))
        colour = (UI["red_hi"] if c.get(ERROR) else
                  UI["amber"] if c.get(WARN) else UI["green"])
        self.events_status.config(text=text, foreground=colour)

    # ---- navigation ----
    def _events_on_select(self, _evt=None):
        """Freeze the plot at the selected event. This is the bridge between
        'when did it happen' and 'what did everything else look like'."""
        sel = self.events_tree.selection()
        if not sel:
            return
        row = self._events_rowids.get(sel[0])
        if row is None or getattr(self, "plot", None) is None:
            return
        self._plot_goto(row["t"], "event: %s %s -> %s" % (
            row["label"], row["old_text"], row["new_text"]))
