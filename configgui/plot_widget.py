"""
configgui.plot_widget  --  a live strip-chart embedded in tkinter (matplotlib).

A small, reusable scrolling plot. It owns the figure, one line per selected
signal, the legend and the axes - but NOT the data: samples live in
configgui.history.SignalHistory, which records every signal continuously. Each
redraw() asks the history for the slice it needs. That split is what lets the
Plot tab back-fill a freshly-ticked signal and freeze the view on Pause without
losing a single sample.

It keeps a rolling time window, autoscales Y (or normalises each series to 0..1
so signals of very different magnitude are comparable), and draws on the dark
theme so it matches the rest of the console.

matplotlib is imported defensively: if it isn't installed the rest of the GUI
still runs and the Plot tab shows a one-line "pip install matplotlib" hint
(see MATPLOTLIB_OK / MATPLOTLIB_ERR and the Plot tab). Nothing else depends on it.
"""

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


class LivePlot(tk.Frame):
    """A scrolling multi-series strip chart. Construct only when MATPLOTLIB_OK.

    Usage:
        p = LivePlot(parent)
        p.add_series("Torque_Request_Left", "#60a5fa")
        p.set_window(15.0)
        p.redraw(history)              # live: right edge follows the clock
        p.redraw(history, t_end=t)     # frozen: right edge pinned at t (Pause)
    """

    def __init__(self, master, **kw):
        super().__init__(master, **kw)
        self._series = {}          # name -> {"color": str, "line": Line2D}
        self._window = 15.0        # seconds shown

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
        self._series[name] = {"color": color, "line": line}
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

    # ---- config ----
    def set_window(self, seconds):
        self._window = float(seconds)

    def set_normalise(self, on):
        self._normalise = bool(on)

    _normalise = False

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

    def redraw(self, history, t_end=None):
        """Repaint from `history`. t_end=None tracks the live right edge; passing
        a timestamp pins it there, which is how Pause freezes the view without
        interrupting the capture underneath. Cheap to call on a timer."""
        if not self._series:
            self.canvas.draw_idle()
            return

        if t_end is None:
            # Right edge = "now" ON THE BOARD'S CLOCK. Samples are stamped with
            # the board's seconds-since-boot, so mixing in the PC's epoch time
            # here would put the visible window billions of seconds away from the
            # data and draw nothing. projected_now() advances the newest sample by
            # however long ago it arrived, so an idle plot still scrolls.
            t_end = history.projected_now()
        t_lo = t_end - self._window

        y_lo, y_hi = None, None
        for name, s in self._series.items():
            ts, vs = history.window(name, t_lo, t_end)
            xs = ts - t_end if len(ts) else ts     # 0 at the right edge, negative left

            if self._normalise and len(vs):
                lo, hi = float(vs.min()), float(vs.max())
                span = (hi - lo) or 1.0
                plot_ys = (vs - lo) / span
            else:
                plot_ys = vs

            s["line"].set_data(xs, plot_ys)

            if len(plot_ys):
                lo, hi = float(plot_ys.min()), float(plot_ys.max())
                y_lo = lo if y_lo is None else min(y_lo, lo)
                y_hi = hi if y_hi is None else max(y_hi, hi)

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
