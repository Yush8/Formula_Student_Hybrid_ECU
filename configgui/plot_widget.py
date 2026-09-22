"""
configgui.plot_widget  --  a live strip-chart embedded in tkinter (matplotlib).

A small, reusable scrolling plot: you add named series (one per telemetry signal
you want to watch), push (timestamp, value) samples in as they stream, and call
redraw() on a timer. It keeps a rolling time window, autoscales Y (or normalises
each series to 0..1 so signals of very different magnitude are comparable), and
draws on the dark theme so it matches the rest of the console.

matplotlib is imported defensively: if it isn't installed the rest of the GUI
still runs and the Plot tab shows a one-line "pip install matplotlib" hint
(see MATPLOTLIB_OK / MATPLOTLIB_ERR and the Plot tab). Nothing else depends on it.
"""

import time
from collections import deque

import tkinter as tk

from .theme import UI

try:
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    MATPLOTLIB_OK = True
    MATPLOTLIB_ERR = ""
except Exception as _e:                       # pragma: no cover - environment dependent
    MATPLOTLIB_OK = False
    MATPLOTLIB_ERR = str(_e)

# Hard cap on stored samples per series (memory bound); the visible window is
# trimmed from this on every redraw. At 100 Hz this is ~3 minutes of history,
# far more than any window the user can pick.
_MAX_POINTS = 20000


class LivePlot(tk.Frame):
    """A scrolling multi-series strip chart. Construct only when MATPLOTLIB_OK.

    Usage:
        p = LivePlot(parent)
        p.add_series("Torque_Request_Left", "#60a5fa")
        p.push("Torque_Request_Left", t, value)   # t = time.time()
        p.redraw()                                 # call on a timer
    """

    def __init__(self, master, **kw):
        super().__init__(master, **kw)
        self._series = {}          # name -> {color, t: deque, v: deque, line}
        self._window = 15.0        # seconds shown
        self._normalise = False
        self._t_ref = time.time()  # so the x axis reads in seconds since plot open

        self.fig = Figure(figsize=(6, 3.2), dpi=100, facecolor=UI["card"])
        self.ax = self.fig.add_subplot(111)
        self._style_axes()

        self.canvas = FigureCanvasTkAgg(self.fig, master=self)
        self.canvas.get_tk_widget().configure(background=UI["card"],
                                              highlightthickness=0)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        self.fig.tight_layout(pad=1.4)

    # ---- styling ----
    def _style_axes(self):
        ax = self.ax
        ax.set_facecolor(UI["field"])
        for spine in ax.spines.values():
            spine.set_color(UI["border"])
        ax.tick_params(colors=UI["muted"], labelsize=8)
        ax.grid(True, color=UI["border"], linewidth=0.5, alpha=0.5)
        ax.set_xlabel("seconds  (now →)", color=UI["muted"], fontsize=8)
        ax.margins(x=0)

    # ---- series management ----
    def has_series(self, name):
        return name in self._series

    def series_names(self):
        return list(self._series.keys())

    def add_series(self, name, color):
        if name in self._series:
            return
        (line,) = self.ax.plot([], [], color=color, linewidth=1.4,
                               label=name, animated=False)
        self._series[name] = {"color": color,
                              "t": deque(maxlen=_MAX_POINTS),
                              "v": deque(maxlen=_MAX_POINTS),
                              "line": line}
        self._rebuild_legend()

    def remove_series(self, name):
        s = self._series.pop(name, None)
        if s is None:
            return
        try:
            s["line"].remove()
        except Exception:
            pass
        self._rebuild_legend()

    def clear_series(self):
        for name in list(self._series):
            self.remove_series(name)

    def clear_data(self):
        """Drop all samples but keep the selected series (a fresh time base)."""
        self._t_ref = time.time()
        for s in self._series.values():
            s["t"].clear()
            s["v"].clear()

    # ---- data + config ----
    def push(self, name, t, v):
        s = self._series.get(name)
        if s is None:
            return
        s["t"].append(t)
        s["v"].append(v)

    def latest(self, name):
        s = self._series.get(name)
        if not s or not s["v"]:
            return None
        return s["v"][-1]

    def set_window(self, seconds):
        self._window = float(seconds)

    def set_normalise(self, on):
        self._normalise = bool(on)

    # ---- rendering ----
    def _rebuild_legend(self):
        """Rebuild the in-figure legend only when the series set changes (never
        per frame - that would be slow). Live values live in the side chips."""
        old = self.ax.get_legend()
        if old is not None:
            old.remove()
        if self._series:
            leg = self.ax.legend(loc="upper left", fontsize=7, ncol=2,
                                 framealpha=0.85, facecolor=UI["elev"],
                                 edgecolor=UI["border"], labelcolor=UI["fg"])
            if leg is not None:
                leg.set_zorder(5)

    def redraw(self):
        """Recompute the rolling window and repaint. Cheap to call on a timer."""
        if not self._series:
            self.canvas.draw_idle()
            return

        # Right edge = the newest sample across all series (fall back to wall clock
        # so an idle plot still scrolls instead of freezing at the last frame).
        t_now = self._t_ref
        for s in self._series.values():
            if s["t"]:
                t_now = max(t_now, s["t"][-1])
        t_now = max(t_now, time.time() - 0)   # keep advancing while idle
        t_lo = t_now - self._window

        y_lo, y_hi = None, None
        for s in self._series.values():
            ts, vs = s["t"], s["v"]
            # Visible slice: walk back from the end while within the window.
            xs, ys = [], []
            for i in range(len(ts) - 1, -1, -1):
                if ts[i] < t_lo:
                    break
                xs.append(ts[i] - t_now)      # 0 at the right edge, negative to left
                ys.append(vs[i])
            xs.reverse()
            ys.reverse()

            if self._normalise and ys:
                lo, hi = min(ys), max(ys)
                span = (hi - lo) or 1.0
                plot_ys = [(y - lo) / span for y in ys]
            else:
                plot_ys = ys

            s["line"].set_data(xs, plot_ys)

            for y in plot_ys:
                y_lo = y if y_lo is None else min(y_lo, y)
                y_hi = y if y_hi is None else max(y_hi, y)

        self.ax.set_xlim(-self._window, 0)

        if self._normalise:
            self.ax.set_ylim(-0.05, 1.05)
            self.ax.set_ylabel("normalised  (per signal)", color=UI["muted"],
                               fontsize=8)
        else:
            if y_lo is None:
                y_lo, y_hi = 0.0, 1.0
            if y_hi - y_lo < 1e-9:            # flat line: give it some air
                pad = abs(y_hi) * 0.05 + 0.5
                y_lo, y_hi = y_lo - pad, y_hi + pad
            else:
                pad = (y_hi - y_lo) * 0.08
                y_lo, y_hi = y_lo - pad, y_hi + pad
            self.ax.set_ylim(y_lo, y_hi)
            self.ax.set_ylabel("value", color=UI["muted"], fontsize=8)

        self.canvas.draw_idle()
