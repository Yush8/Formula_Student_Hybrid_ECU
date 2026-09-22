"""
configgui.trigger  --  scope-style capture triggers.

An intermittent fault is hard to catch because you have to be watching the
screen at the moment it happens. A trigger watches for you: arm a condition,
and when it fires the Plot tab freezes on that instant, the timeline marks it,
and (if you asked) the surrounding window is written out as a snapshot.

Conditions are deliberately small and readable, because a condition you cannot
reason about at 2am is worse than none:

    <signal>  >  <value>      crosses above (edge, not level)
    <signal>  <  <value>      crosses below
    <signal>  ==  <value>     becomes equal
    <signal>  !=  <value>     stops being equal
    <signal>  changes         any change at all
    any fault                 Fault_Code becomes non-zero
    any error                 anything the Events tab would colour red

All of them are EDGE triggered: they fire on the transition into the condition,
not every sample while it holds. A level trigger on "Fault_Code != 0" would fire
a hundred times a second for as long as the fault lasted, which is useless.

This module decides only WHEN to fire. What happens next (freeze, snapshot,
bundle) is the Plot tab's business, via the on_fire callback.
"""

from .protocol import FAULT_SIGNAL

# The comparison operators offered in the GUI, in menu order.
OPS = [">", "<", ">=", "<=", "==", "!=", "changes"]

# Pseudo-signals: conditions that are not about one named signal.
ANY_FAULT = "· any fault"
ANY_ERROR = "· any red event"
SPECIALS = [ANY_FAULT, ANY_ERROR]


class Trigger:
    """One armed condition. Evaluate live samples through test(); call
    note_event() for board events so the event-based conditions can fire."""

    def __init__(self):
        self.signal = ""
        self.op = ">"
        self.value = 0.0
        self.armed = False
        self.rearm = False        # fire repeatedly, or stop after the first hit
        self.fired_t = None       # board time of the last fire
        self.fire_count = 0
        self.reason = ""          # human text describing why it fired
        self._prev = None         # last sample, so we can detect the EDGE
        self._on_fire = None

    # ---- configuration ----
    def configure(self, signal, op, value, rearm=False):
        self.signal = signal
        self.op = op
        try:
            self.value = float(value)
        except (TypeError, ValueError):
            self.value = 0.0
        self.rearm = bool(rearm)
        self._prev = None
        return self

    def on_fire(self, fn):
        self._on_fire = fn

    def arm(self):
        self.armed = True
        self._prev = None          # a fresh arm must not fire on stale history
        self.reason = ""
        return self

    def disarm(self):
        self.armed = False
        return self

    def describe(self):
        if self.signal in SPECIALS:
            return self.signal.lstrip("· ")
        if not self.signal:
            return "(no condition)"
        if self.op == "changes":
            return "%s changes" % self.signal
        return "%s %s %g" % (self.signal, self.op, self.value)

    # ---- evaluation ----
    def test(self, name, value, t):
        """One live sample. Returns True if this sample fired the trigger."""
        if not self.armed or self.signal in SPECIALS or name != self.signal:
            return False
        try:
            v = float(value)
        except (TypeError, ValueError):
            return False

        prev, self._prev = self._prev, v
        if prev is None:
            return False                      # need two samples to see an edge

        if self.op == "changes":
            if v == prev:
                return False
        elif self._holds(prev) or not self._holds(v):
            return False                      # not a transition INTO the condition

        return self._fire(t, "%s: %s -> %s" % (self.describe(), _fmt(prev), _fmt(v)))

    def note_event(self, row):
        """One decoded board event (see events_model). Lets the pseudo-signal
        conditions fire, and lets a named condition fire off an event for a
        signal that is not in the telemetry stream."""
        if not self.armed:
            return False
        if self.signal == ANY_FAULT:
            if row["signal"] == FAULT_SIGNAL and _truthy(row["new"]):
                return self._fire(row["t"], "fault: %s" % row["new_text"])
            return False
        if self.signal == ANY_ERROR:
            if row["severity"] == "error":
                return self._fire(row["t"], "%s: %s -> %s" % (
                    row["label"], row["old_text"], row["new_text"]))
            return False
        return False

    def _holds(self, v):
        """Does the condition hold for this value? `changes` has no level - it is
        handled by the edge comparison in test() - so it never reaches here."""
        if self.op == ">":
            return v > self.value
        if self.op == "<":
            return v < self.value
        if self.op == ">=":
            return v >= self.value
        if self.op == "<=":
            return v <= self.value
        if self.op == "==":
            return v == self.value
        if self.op == "!=":
            return v != self.value
        return False

    def _fire(self, t, reason):
        self.fired_t = t
        self.fire_count += 1
        self.reason = reason
        if not self.rearm:
            self.armed = False
        else:
            self._prev = None
        if self._on_fire:
            try:
                self._on_fire(self, t, reason)
            except Exception:
                pass              # a failing action must not wedge the stream
        return True


def _fmt(v):
    return ("%g" % v) if v == v else "nan"


def _truthy(raw):
    try:
        return float(raw) != 0.0
    except (TypeError, ValueError):
        return False
