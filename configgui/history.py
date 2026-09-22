"""
configgui.history  --  the always-on rolling capture behind the Plot tab.

SignalHistory records EVERY scalar telemetry signal the board streams, whether or
not it is ticked for plotting and whether or not the plot is paused. That is what
makes the Plot tab behave like a scope instead of a live-only feed:

  * ticking a signal back-fills its last N seconds instead of starting blank;
  * Pause is a true freeze-frame - the capture keeps running underneath, so
    Resume continues the trace with no gap;
  * you can tick a *new* signal while paused and see what it was doing.

Storage is one pair of contiguous float64 arrays per signal (time + value), sized
at 2x the requested depth. When the write cursor hits the end we memmove the
newest half to the front - amortised O(1) per sample, and reads stay plain
ordered slices, so a redraw is a searchsorted plus two array views. The cost of
that trick is the 2x allocation; see bytes_used(), which the Plot tab shows you.

numpy is imported defensively for symmetry with plot_widget: this class is only
ever constructed when matplotlib imported cleanly (which implies numpy), but the
module stays importable either way.
"""

import time

try:
    import numpy as np
    NUMPY_OK = True
except Exception:                             # pragma: no cover - environment dependent
    np = None
    NUMPY_OK = False

# Depth is requested in samples; clamp so a silly rate/window combination can
# neither starve the plot nor eat the machine. 600 = 30 s at the 20 Hz default;
# 120000 = 20 min at 100 Hz (~30 MB per *hundred* signals, at 2x allocation).
MIN_DEPTH = 600
MAX_DEPTH = 120000

_BYTES_PER_SAMPLE = 16                        # float64 time + float64 value


class SignalHistory:
    """Rolling capture of every scalar signal. Not thread-safe: fed and read from
    the tkinter main thread only (the serial reader hands frames over a queue)."""

    def __init__(self, depth):
        self._depth = _clamp_depth(depth)
        self._sig = {}          # name -> {"t": ndarray, "v": ndarray, "w": int}
        self._t_last = 0.0      # newest timestamp seen across all signals
        self._wall_last = 0.0   # PC clock when that newest sample arrived

    # ---- configuration ----
    @property
    def depth(self):
        return self._depth

    def set_depth(self, depth):
        """Resize every ring, keeping the newest samples that still fit. Called
        when the History combo or the telemetry rate changes."""
        depth = _clamp_depth(depth)
        if depth == self._depth:
            return
        self._depth = depth
        cap = depth * 2
        for s in self._sig.values():
            keep = min(s["w"], depth)
            t = np.empty(cap, dtype=np.float64)
            v = np.empty(cap, dtype=np.float64)
            if keep:
                t[:keep] = s["t"][s["w"] - keep:s["w"]]
                v[:keep] = s["v"][s["w"] - keep:s["w"]]
            s["t"], s["v"], s["w"] = t, v, keep

    # ---- writing ----
    def push(self, name, t, v):
        """Append one sample. Hot path: called once per signal per frame (~4k/s at
        the 20 Hz default, ~19k/s at 100 Hz), so it stays deliberately small."""
        s = self._sig.get(name)
        if s is None:
            cap = self._depth * 2
            s = {"t": np.empty(cap, dtype=np.float64),
                 "v": np.empty(cap, dtype=np.float64), "w": 0}
            self._sig[name] = s
        w = s["w"]
        if w >= s["t"].size:
            # Ring is full: shift the newest `depth` samples down to the front so
            # the data stays contiguous and ordered. Amortised O(1) per sample.
            keep = self._depth
            s["t"][:keep] = s["t"][w - keep:w]
            s["v"][:keep] = s["v"][w - keep:w]
            w = keep
        s["t"][w] = t
        s["v"][w] = v
        s["w"] = w + 1
        if t > self._t_last:
            self._t_last = t
            self._wall_last = time.time()

    # ---- reading ----
    def window(self, name, t_lo, t_hi):
        """Ordered (times, values) views for t_lo <= t <= t_hi. Empty arrays when
        the signal is unknown or has nothing in that span."""
        s = self._sig.get(name)
        if s is None or s["w"] == 0:
            return _EMPTY, _EMPTY
        ts = s["t"][:s["w"]]
        lo = int(np.searchsorted(ts, t_lo, side="left"))
        hi = int(np.searchsorted(ts, t_hi, side="right"))
        if hi <= lo:
            return _EMPTY, _EMPTY
        return ts[lo:hi], s["v"][:s["w"]][lo:hi]

    def value_at(self, name, t):
        """The last value recorded at or before `t` (None if there isn't one)."""
        s = self._sig.get(name)
        if s is None or s["w"] == 0:
            return None
        i = int(np.searchsorted(s["t"][:s["w"]], t, side="right")) - 1
        return None if i < 0 else float(s["v"][i])

    def latest_time(self):
        return self._t_last

    def projected_now(self):
        """Where "now" is on THIS buffer's time axis.

        Timestamps are the board's seconds-since-boot, not the PC's epoch clock,
        so a live plot cannot use time.time() for its right-hand edge - the two
        axes are billions of seconds apart and the visible window would contain
        no data at all. Instead, take the newest sample and advance it by however
        long ago it arrived, which keeps an idle plot scrolling in the board's
        own time base. Returns 0.0 before the first sample."""
        if self._t_last <= 0.0:
            return 0.0
        return self._t_last + max(0.0, time.time() - self._wall_last)

    def span(self, name):
        """Seconds of history held for one signal (0.0 if fewer than 2 samples)."""
        s = self._sig.get(name)
        if s is None or s["w"] < 2:
            return 0.0
        return float(s["t"][s["w"] - 1] - s["t"][0])

    def names(self):
        return list(self._sig)

    # ---- housekeeping ----
    def clear(self):
        """Drop every sample (the 'Clear graph' button) but keep the allocations."""
        for s in self._sig.values():
            s["w"] = 0
        self._t_last = 0.0
        self._wall_last = 0.0

    def forget(self, name):
        self._sig.pop(name, None)

    def bytes_used(self):
        """Allocated bytes, i.e. what this buffer actually costs you right now."""
        return sum(s["t"].size for s in self._sig.values()) * _BYTES_PER_SAMPLE


def _clamp_depth(depth):
    return max(MIN_DEPTH, min(MAX_DEPTH, int(depth)))


def human_bytes(n):
    if n < 1024 * 1024:
        return f"{n / 1024:.0f} kB"
    return f"{n / (1024 * 1024):.1f} MB"


_EMPTY = np.empty(0, dtype=np.float64) if NUMPY_OK else ()
