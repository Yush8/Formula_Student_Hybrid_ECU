"""
configgui.health_tab  --  the board's own diagnostics, at a glance.

The firmware already knows far more about its health than it used to show: loop
overruns AND how close each step runs to its 10 ms budget, per-bus CAN state down
to the transmit error counter and the last protocol error, the SD ring's
occupancy and high-water mark, and the AIR fail-safe's arm/latch state. Until now
all of that arrived as text in the Console tab, only when you pressed a button.

This tab polls `stats json` on a timer and renders it as tiles that are either
green or not. Two things make it more useful than the raw numbers:

  RATES, NOT TOTALS.  "rx_lost 42" tells you nothing without knowing when. Each
  counter tile shows the change since the last poll beside the total, so a fault
  happening RIGHT NOW looks different from one that happened an hour ago.

  A STATED VERDICT.  Every tile says OK / WARN / FAIL using the same thresholds
  the firmware's own `stats` text uses, so you do not have to remember that TEC
  near 128 means nobody is acking your frames.

The loop-headroom panel is the one to watch before a session: overruns only tell
you the loop has ALREADY missed a deadline, whereas the step-time histogram shows
how much margin is left before it does.

Falls back gracefully: firmware without `stats json` just leaves the tab saying
so, and the human `stats` text still works in the Console tab.
"""

import time

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_MONO, FONT_UI_B, FONT_UI_SM

# How often to ask the board. 1 s is responsive without being chatty; the reply
# is one line, so this costs far less bandwidth than the telemetry stream.
HEALTH_POLL_MS = 1000

OK, WARN, FAIL, IDLE = "OK", "WARN", "FAIL", "--"
_COLOURS = {
    OK:   (UI["green_hi"], "#ffffff"),
    WARN: (UI["amber"],    "#1a1206"),
    FAIL: (UI["red_hi"],   "#ffffff"),
    IDLE: ("#39424e",      "#e6edf3"),
}


class HealthMixin:
    # ---- Health tab ----
    def _build_health_panel(self, parent):
        self._health_tiles = {}
        self._health_prev = {}        # counter -> value at the previous poll
        self._health_poll_on = tk.BooleanVar(value=True)

        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", padx=8, pady=(4, 2))
        ttk.Checkbutton(ctl, text="Auto-poll (1 s)", variable=self._health_poll_on,
                        command=self._health_toggle_poll).pack(side="left")
        ttk.Button(ctl, text="Poll now", command=self.cmd_stats_json).pack(
            side="left", padx=6)
        ttk.Button(ctl, text="Zero counters",
                   command=self._health_clear).pack(side="left")
        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Button(ctl, text="Board version", command=self.cmd_version).pack(side="left")

        self.health_age = ttk.Label(ctl, text="never polled", foreground=UI["muted"],
                                    background=UI["card"], font=FONT_MONO)
        self.health_age.pack(side="right")

        # ---- top row of verdict tiles ----
        tiles = ttk.Frame(parent)
        tiles.pack(fill="x", padx=8, pady=(2, 4))
        for key, label in (("loop", "CONTROL LOOP"), ("can1", "CAN 1"),
                           ("can2", "CAN 2"), ("log", "SD LOG"),
                           ("air", "AIR FAIL-SAFE")):
            self._health_tiles[key] = self._health_make_tile(tiles, label)

        # ---- detail panes ----
        body = ttk.Frame(parent)
        body.pack(fill="both", expand=True, padx=8, pady=(0, 4))
        body.columnconfigure(0, weight=1, uniform="h")
        body.columnconfigure(1, weight=1, uniform="h")
        body.rowconfigure(0, weight=1)

        self._health_detail = self._health_make_detail(
            body, "Counters  ·  total, and the change since the last poll", 0)
        self._health_timing = self._health_make_timing(body, 1)

        self._health_refresh()
        self.root.after(HEALTH_POLL_MS, self._health_poll)

    def _health_make_tile(self, parent, label):
        """One verdict tile: a big OK/WARN/FAIL band with a one-line reason."""
        box = tk.Frame(parent, background=UI["card"])
        box.pack(side="left", fill="both", expand=True, padx=(0, 6))
        band = tk.Label(box, text=IDLE, font=("Segoe UI", 11, "bold"),
                        background="#39424e", foreground="#e6edf3",
                        padx=8, pady=4)
        band.pack(fill="x")
        name = tk.Label(box, text=label, font=FONT_UI_SM,
                        background=UI["card"], foreground=UI["muted"])
        name.pack(anchor="w", pady=(2, 0))
        why = tk.Label(box, text="", font=FONT_MONO, justify="left", anchor="w",
                       background=UI["card"], foreground=UI["fg"], wraplength=210)
        why.pack(fill="x")
        return {"band": band, "why": why}

    def _health_make_detail(self, parent, title, col):
        box = ttk.LabelFrame(parent, text=title)
        box.grid(row=0, column=col, sticky="nsew", padx=(0, 4))
        cols = ("total", "delta")
        t = ttk.Treeview(box, columns=cols, show="tree headings", selectmode="none")
        t.heading("#0", text="Counter")
        t.heading("total", text="Total")
        t.heading("delta", text="Since last poll")
        t.column("#0", width=210, anchor="w", stretch=True)
        t.column("total", width=90, anchor="e", stretch=False)
        t.column("delta", width=110, anchor="e", stretch=False)
        t.tag_configure("group", background=UI["elev"],
                        foreground=UI["accent_hi"], font=FONT_UI_B)
        t.tag_configure("moved", foreground=UI["amber"])
        t.tag_configure("bad", foreground=UI["red_hi"])
        vsb = ttk.Scrollbar(box, orient="vertical", command=t.yview)
        t.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        t.pack(side="left", fill="both", expand=True, padx=4, pady=4)
        return t

    def _health_make_timing(self, parent, col):
        """Loop headroom: how long the step takes and how late it starts.

        This is the panel that answers "am I close to the edge?", which the
        overrun counter cannot - by the time overruns is non-zero you have
        already gone over."""
        box = ttk.LabelFrame(parent, text="Loop headroom  ·  step time and lateness")
        box.grid(row=0, column=col, sticky="nsew", padx=(4, 0))

        head = ttk.Frame(box)
        head.pack(fill="x", padx=6, pady=(4, 2))
        self.health_budget = ttk.Label(head, text="", style="Muted.TLabel")
        self.health_budget.pack(anchor="w")

        grid = ttk.Frame(box)
        grid.pack(fill="x", padx=6, pady=2)
        self._health_time_vals = {}
        for r, (key, label) in enumerate((("step_us", "Model_Step took"),
                                          ("late_us", "started late by"))):
            ttk.Label(grid, text=label, style="Muted.TLabel").grid(
                row=r, column=0, sticky="w", padx=(0, 8))
            v = ttk.Label(grid, text="—", font=FONT_MONO, foreground=UI["fg"],
                          background=UI["card"])
            v.grid(row=r, column=1, sticky="w")
            self._health_time_vals[key] = v

        ttk.Label(box, style="Muted.TLabel", justify="left",
                  text="Distribution of step time (how often it landed in each band):"
                  ).pack(anchor="w", padx=6, pady=(6, 0))
        self.health_hist = tk.Text(box, height=9, font=FONT_MONO, relief="flat",
                                   background=UI["field"], foreground=UI["fg"],
                                   highlightthickness=0, padx=8, pady=4,
                                   state="disabled", wrap="none")
        self.health_hist.pack(fill="both", expand=True, padx=6, pady=(2, 6))
        return box

    # ---- polling ----
    def _health_toggle_poll(self):
        if self._health_poll_on.get():
            self.cmd_stats_json()

    def _health_poll(self):
        if (self._health_poll_on.get() and self.ser and self.ser.is_open):
            self._send_quiet("stats json")
        self.root.after(HEALTH_POLL_MS, self._health_poll)

    def cmd_stats_json(self):
        if self.ser and self.ser.is_open:
            self._send_quiet("stats json")

    def _health_clear(self):
        """Zero the board's counters and forget our deltas, so the panel reads
        from now rather than from boot. Stays on this tab (unlike the Board bar's
        Clear stats, which shows you the freshly-zeroed text in the Console)."""
        if not (self.ser and self.ser.is_open):
            return
        self._send("stats clear")
        self._health_prev = {}
        self.root.after(200, self.cmd_stats_json)

    def cmd_version(self):
        self.notebook.select(self.tab_console)
        self._send("version")

    # ---- rendering ----
    def _health_refresh(self):
        """Redraw from self._health (set by the RX dispatcher). Safe to call with
        no data - it just shows the idle state."""
        h = self._health
        if not h:
            for t in self._health_tiles.values():
                self._health_set_tile(t, IDLE, "waiting for the board")
            return

        age = time.time() - self._health_at
        self.health_age.config(
            text="updated %.1fs ago" % age,
            foreground=UI["muted"] if age < 3 else UI["amber"])

        self._health_tile_loop(h)
        for bus in (1, 2):
            self._health_tile_can(h, bus)
        self._health_tile_log(h)
        self._health_tile_air(h)

        # A dot on the tab in the colour of the worst tile, so a CAN bus going
        # off is visible from whatever page you are on.
        verdicts = {t["band"].cget("text") for t in self._health_tiles.values()}
        if FAIL in verdicts:
            self._shell_badge("health", "●", UI["red_hi"])
        elif WARN in verdicts:
            self._shell_badge("health", "●", UI["amber"])
        else:
            self._shell_badge("health", "●", UI["green"])
        self._health_fill_counters(h)
        self._health_fill_timing(h)

    def _health_set_tile(self, tile, verdict, why):
        bg, fg = _COLOURS[verdict]
        tile["band"].config(text=verdict, background=bg, foreground=fg)
        tile["why"].config(text=why)

    def _health_tile_loop(self, h):
        over = int(h.get("overruns", 0) or 0)
        step = h.get("step_us") or {}
        budget = int(h.get("step_budget_us", 10000) or 10000)
        worst = int(step.get("max", 0) or 0)
        if over:
            v, why = FAIL, "%d missed deadlines" % over
        elif budget and worst > budget * 0.8:
            v, why = WARN, "worst step %d us of %d" % (worst, budget)
        else:
            v, why = OK, "worst step %d us of %d" % (worst, budget)
        self._health_set_tile(self._health_tiles["loop"], v, why)

    def _health_tile_can(self, h, bus):
        c = h.get("can%d" % bus) or {}
        if not c:
            return
        if c.get("bus_off"):
            v = FAIL
            why = "BUS-OFF " + ("(gave up)" if c.get("gave_up") else "(recovering)")
        elif c.get("rx_lost") or c.get("recoveries"):
            v, why = WARN, "recovered: lost %s, %s restarts" % (
                c.get("rx_lost"), c.get("recoveries"))
        elif c.get("error_passive"):
            v, why = WARN, "error-passive, TEC %s" % c.get("tec")
        elif int(c.get("tx_fail", 0) or 0):
            v, why = WARN, "%s sends rejected" % c.get("tx_fail")
        else:
            v, why = OK, "tx %s sent, %s pending" % (
                c.get("tx_done"), c.get("tx_pending"))
        self._health_set_tile(self._health_tiles["can%d" % bus], v, why)

    def _health_tile_log(self, h):
        lg = h.get("log") or {}
        if not lg:
            return
        ring, cap = int(lg.get("ring", 0) or 0), int(lg.get("ring_max", 1) or 1)
        if int(lg.get("drops", 0) or 0):
            v, why = FAIL, "%s records DROPPED" % lg.get("drops")
        elif cap and ring >= cap / 2:
            v, why = WARN, "ring backing up %d/%d" % (ring, cap)
        else:
            v, why = OK, "ring %d/%d, peak %s" % (ring, cap, lg.get("peak"))
        self._health_set_tile(self._health_tiles["log"], v, why)

    def _health_tile_air(self, h):
        air = h.get("air") or {}
        if not air:
            return
        if air.get("latched"):
            v, why = FAIL, "STALL LATCHED - AIRs forced open"
        elif not air.get("armed"):
            v, why = IDLE, "not armed (model has not stepped)"
        elif int(air.get("trips", 0) or 0):
            v, why = WARN, "recovered after %s trip(s)" % air.get("trips")
        else:
            v, why = OK, "model alive, AIR %s" % (
                "closed" if air.get("air_closed") else "open")
        self._health_set_tile(self._health_tiles["air"], v, why)

    # ---- counter table ----
    _COUNTERS = (
        ("Control loop", (("overruns", "missed deadlines"),
                          ("ticks", "ticks since boot"))),
        ("CAN 1", (("can1.rx_lost", "frames lost to overrun"),
                   ("can1.recoveries", "bus-off restarts"),
                   ("can1.tx_done", "frames sent"),
                   ("can1.tx_fail", "sends rejected"),
                   ("can1.tec", "transmit error counter"),
                   ("can1.tx_pending", "queued for transmit"))),
        ("CAN 2", (("can2.rx_lost", "frames lost to overrun"),
                   ("can2.recoveries", "bus-off restarts"),
                   ("can2.tx_done", "frames sent"),
                   ("can2.tx_fail", "sends rejected"),
                   ("can2.tec", "transmit error counter"),
                   ("can2.tx_pending", "queued for transmit"))),
        ("SD log", (("log.writes", "records written"),
                    ("log.drops", "records dropped"),
                    ("log.ring", "ring occupancy"),
                    ("log.peak", "ring high-water mark"))),
        ("AIR fail-safe", (("air.trips", "freeze latches"),
                           ("air.stall_age", "ticks since proof of life"),
                           ("air.calls", "supervisor ISR calls"))),
        ("Events", (("events.raised", "events raised"),
                    ("events.watched", "signals watched"))),
    )
    # Counters where any increase at all is bad news, not just activity.
    _BAD_IF_MOVING = {"overruns", "can1.rx_lost", "can2.rx_lost",
                      "can1.recoveries", "can2.recoveries",
                      "can1.tx_fail", "can2.tx_fail", "log.drops", "air.trips"}

    def _health_fill_counters(self, h):
        t = self._health_detail
        for group, items in self._COUNTERS:
            gid = "__g__" + group
            if not t.exists(gid):
                t.insert("", "end", iid=gid, text=group, values=("", ""),
                         open=True, tags=("group",))
            for key, label in items:
                val = _dig(h, key)
                if val is None:
                    continue
                prev = self._health_prev.get(key)
                delta = None if prev is None else val - prev
                self._health_prev[key] = val
                tags = ()
                if delta:
                    tags = ("bad",) if key in self._BAD_IF_MOVING else ("moved",)
                dtxt = "" if not delta else ("%+d" % delta)
                if not t.exists(key):
                    t.insert(gid, "end", iid=key, text="  " + label,
                             values=(val, dtxt), tags=tags)
                else:
                    t.item(key, values=(val, dtxt), tags=tags)

    # ---- timing panel ----
    def _health_fill_timing(self, h):
        budget = int(h.get("step_budget_us", 0) or 0)
        samples = int(h.get("step_samples", 0) or 0)
        self.health_budget.config(
            text="budget %d us per tick  ·  %s steps measured" % (budget, f"{samples:,}"))

        for key in ("step_us", "late_us"):
            d = h.get(key) or {}
            if d:
                self._health_time_vals[key].config(
                    text="now %5s   min %5s   avg %5s   max %5s us" % (
                        d.get("last"), d.get("min"), d.get("avg"), d.get("max")))

        hist = h.get("step_hist") or []
        edges = h.get("step_hist_edges_us") or []
        total = sum(hist) or 1
        lines = []
        lo = 0
        for i, count in enumerate(hist):
            hi = edges[i] if i < len(edges) else budget
            band = ("%d-%d us" % (lo, hi)) if i + 1 < len(hist) else (">%d us" % lo)
            pct = 100.0 * count / total
            bar = "#" * int(round(pct / 4.0))
            lines.append("%-14s %7d  %5.1f%%  %s" % (band, count, pct, bar))
            lo = hi
        self.health_hist.config(state="normal")
        self.health_hist.delete("1.0", "end")
        self.health_hist.insert("1.0", "\n".join(lines) if lines
                                else "(no timing data yet - the board must step at least once)")
        self.health_hist.config(state="disabled")


def _dig(obj, dotted):
    """Fetch "can1.rx_lost" out of the nested health dict."""
    cur = obj
    for part in dotted.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur if isinstance(cur, (int, float)) else None
