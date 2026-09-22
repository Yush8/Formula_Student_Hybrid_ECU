"""
configgui.events_model  --  the event timeline store.

Holds the `#E` lines the board emits the instant a watched discrete signal
changes, and turns each one into a row you can read without a decoder ring:

    18.12 s   SUPERVISOR      READY -> ERROR
    18.12 s   Fault_Code      no fault -> BMS zero-limit (DCL & CCL = 0)
    18.12 s   Inverter_Enable 1 -> 0

The decoding matters. An event that says "State_Enum 5 -> 6" makes you go and
look 6 up; one that says "READY -> ERROR" is the answer. The tables it decodes
against are the SAME ones the banner uses (protocol.SUPERVISOR_STATES /
FAULT_CODES), so the timeline and the banner can never disagree.

Severity is assigned here too, so the Events tab can colour a row and the rest
of the GUI can ask "has anything bad happened?" without re-deriving the rules:

    ERROR  something latched or failed - a fault code, an AIR stall latch,
           a first dropped frame. These are the rows you look at first.
    WARN   a state machine leaving a driving state, an enable dropping out.
    INFO   everything else - normal progress through the state machine.

This is GUI-side only. The board decides WHAT is an event; this decides how to
show it.
"""

from .protocol import (
    SUPERVISOR_STATES, FAULT_CODES, STATE_SIGNAL, FAULT_SIGNAL,
    DRIVE_MODE_SIGNAL, DRIVE_MODES, BOARD_TICK_HZ, event_label,
)

# Keep the timeline bounded: a long session with a chattering signal should not
# grow the GUI without limit. The full history is in the session's events.csv.
MAX_EVENTS = 4000

ERROR, WARN, INFO = "error", "warn", "info"

# Signals whose mere change is worth flagging, regardless of value.
_ALWAYS_ERROR = {
    "air.stall_latched", "sched.first_overrun", "log.first_drop",
    "can1.first_rx_lost", "can2.first_rx_lost",
    "can1.first_recovery", "can2.first_recovery",
    "can1.first_tx_fail", "can2.first_tx_fail",
}
_FAULT_FLAGS = {"APPS_Implausibility", "BMS_Fault"}
_ENABLES = {"AIR_Enable", "Pre_Charge_Enable", "Inverter_Enable",
            "air.AIR_closed", "air.precharge_closed"}


def _num(text):
    try:
        return int(float(text))
    except (TypeError, ValueError):
        return None


def decode(signal, raw):
    """Turn a raw event value into something readable, using the same tables the
    banner uses so the two can never drift apart."""
    n = _num(raw)
    if n is None:
        return raw
    if signal == STATE_SIGNAL:
        return SUPERVISOR_STATES.get(n, ("STATE %d" % n,))[0]
    if signal == FAULT_SIGNAL:
        return FAULT_CODES.get(n, "code %d" % n)
    if signal == DRIVE_MODE_SIGNAL:
        return DRIVE_MODES.get(n, ("MODE %d" % n,))[0]
    return raw


def severity(signal, old, new):
    """How loudly should this row shout? See the module docstring."""
    if signal in _ALWAYS_ERROR:
        # A latch going ON is the event; going back off is just the recovery.
        return ERROR if _num(new) else INFO
    if signal == FAULT_SIGNAL:
        return ERROR if _num(new) else INFO
    if signal in _FAULT_FLAGS:
        return ERROR if _num(new) else INFO
    if signal == STATE_SIGNAL:
        return ERROR if _num(new) == 6 else INFO     # 6 = ERROR / FAULT
    if signal in _ENABLES:
        # Dropping an enable mid-run is worth noticing; asserting one is normal.
        return WARN if (_num(old) and not _num(new)) else INFO
    if signal in ("bus1_ok", "bus2_ok", "SDC_Monitor"):
        return WARN if (_num(old) and not _num(new)) else INFO
    return INFO


class EventLog:
    """An ordered list of decoded events, newest last.

    Rows are dicts so the Events tab, the session recorder and the debug bundle
    can all read the same object without each re-parsing the wire format.
    """

    def __init__(self):
        self.rows = []
        self._listeners = []

    def clear(self):
        self.rows = []
        self._notify(None)

    def on_add(self, fn):
        """Register a callback(row) - used by the Events tab to append a line
        without polling, and by a trigger to fire on a matching event."""
        self._listeners.append(fn)

    def _notify(self, row):
        for fn in self._listeners:
            try:
                fn(row)
            except Exception:
                pass          # a broken listener must never kill the stream

    def add(self, tick, signal, old, new, wall=None):
        row = {
            "tick": int(tick),
            "t": round(int(tick) / BOARD_TICK_HZ, 4),
            "wall": wall,
            "signal": signal,
            "label": event_label(signal),
            "old": old,
            "new": new,
            "old_text": decode(signal, old),
            "new_text": decode(signal, new),
            "severity": severity(signal, old, new),
        }
        self.rows.append(row)
        if len(self.rows) > MAX_EVENTS:
            del self.rows[:len(self.rows) - MAX_EVENTS]
        self._notify(row)
        return row

    # ---- queries ----
    def worst(self):
        """The most severe level present, for a headline chip."""
        levels = {r["severity"] for r in self.rows}
        if ERROR in levels:
            return ERROR
        if WARN in levels:
            return WARN
        return INFO

    def counts(self):
        out = {ERROR: 0, WARN: 0, INFO: 0}
        for r in self.rows:
            out[r["severity"]] = out.get(r["severity"], 0) + 1
        return out

    def around(self, t, before=5.0, after=5.0):
        """Events in a time window - what the debug bundle puts beside a fault."""
        return [r for r in self.rows if (t - before) <= r["t"] <= (t + after)]

    def format_line(self, row):
        """One readable line, used by the console log and the debug bundle."""
        return "%9.3f s  %-6s %-24s %s -> %s" % (
            row["t"], row["severity"].upper(), row["label"],
            row["old_text"], row["new_text"])
