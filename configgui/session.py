"""
configgui.session  --  recording a run to disk, and loading one back.

A SESSION is one folder under `sessions/`, named for when it started:

    sessions/2026-09-22_141233_shakedown/
        meta.json       what firmware, what signals, when, how long, your notes
        telemetry.csv   every signal, one row per frame, board tick + seconds
        events.csv      the #E timeline (exact, model-step resolution)
        health.jsonl    periodic `stats json` snapshots
        console.log     the raw console text for the run

That shape is deliberate: plain text you can open in Excel, diff, mail, or hand
to someone (or something) else, with a manifest beside it saying what produced
it. Nothing here is a proprietary blob.

WHAT MAKES THE TIME AXIS TRUSTWORTHY
Every row carries the board's own scheduler tick, not the PC's arrival time. The
PC clock sees USB buffering jitter of tens of milliseconds; the tick is the
hardware 100 Hz model step. `t` (seconds since boot) is derived from it, so two
signals in the same frame share one exact timestamp and a dropped frame shows up
as a gap in `tick` rather than silently compressing time.

LOADING
load_session() reads a session back into a SignalHistory - the same buffer the
live Plot tab draws from - so reviewing a recording uses the identical plot,
picker, watch sets and back-fill as watching one live. A decoded SD-card log
(hcu_logdecode.py output) loads through the same path, so card logs and USB
sessions are reviewed the same way.
"""

import io
import os
import csv
import json
import time
import shutil
import datetime

from .protocol import BOARD_TICK_HZ, SESSIONS_DIR
from .history import SignalHistory

# Rows are buffered and flushed on a timer rather than written per frame: at
# 100 Hz with ~200 columns a write per frame would put the filesystem in the
# tkinter main loop. Half a second of buffering costs ~50 rows if we crash.
FLUSH_INTERVAL_S = 0.5

META_NAME = "meta.json"
TELEM_NAME = "telemetry.csv"
EVENTS_NAME = "events.csv"
HEALTH_NAME = "health.jsonl"
CONSOLE_NAME = "console.log"


def _safe_name(text):
    """A folder-safe version of whatever the user typed as a session name."""
    keep = []
    for ch in (text or "").strip():
        keep.append(ch if (ch.isalnum() or ch in "-_") else "_")
    out = "".join(keep).strip("_")
    return out[:48]


def sessions_root():
    os.makedirs(SESSIONS_DIR, exist_ok=True)
    return SESSIONS_DIR


# ---------------------------------------------------------------------------
# Recording
# ---------------------------------------------------------------------------
class SessionRecorder:
    """Writes one session folder while a run is happening.

    Fed from the RX dispatcher: feed_frame() per telemetry frame, feed_event()
    per #E line, feed_health() per `stats json`, feed_console() per console line.
    Every one of those is a no-op when not recording, so the call sites stay
    unconditional and cheap.
    """

    def __init__(self):
        self.active = False
        self.path = None
        self.name = None
        self.started_wall = None
        self.started_tick = None
        self.last_tick = None
        self.rows = 0
        self.events = 0
        self.dropped = 0          # frames the board sent that we never saw
        self._columns = []        # locked at start: CSV needs a stable header
        self._column_set = frozenset()
        self._expected_step = 0.0  # ticks between frames, for drop detection
        self._late = set()        # signals discovered after the header was locked
        self._buf = []
        self._last_flush = 0.0
        self._telem_fh = None
        self._telem_csv = None
        self._events_fh = None
        self._health_fh = None
        self._console_fh = None
        self._meta = {}

    # ---- lifecycle ----
    def start(self, name, columns, meta):
        """Begin recording. `columns` is the signal list as known right now -
        it is LOCKED for the file, because a CSV cannot grow a column halfway
        down. Anything discovered later is recorded in meta as `late_signals`
        rather than silently dropped without trace."""
        if self.active:
            self.stop()
        stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
        label = _safe_name(name)
        folder = stamp + ("_" + label if label else "")
        self.path = os.path.join(sessions_root(), folder)
        os.makedirs(self.path, exist_ok=True)

        self.name = name or folder
        self.started_wall = time.time()
        self.started_tick = None
        self.last_tick = None
        self.rows = 0
        self.events = 0
        self.dropped = 0
        self._columns = list(columns)
        self._column_set = frozenset(self._columns)   # membership test per signal
        self._late = set()
        self._buf = []
        self._last_flush = time.time()

        self._telem_fh = io.open(os.path.join(self.path, TELEM_NAME), "w",
                                 encoding="utf-8", newline="")
        self._telem_csv = csv.writer(self._telem_fh)
        self._telem_csv.writerow(["tick", "t"] + self._columns)

        self._events_fh = io.open(os.path.join(self.path, EVENTS_NAME), "w",
                                  encoding="utf-8", newline="")
        csv.writer(self._events_fh).writerow(["tick", "t", "signal", "from", "to"])

        self._health_fh = io.open(os.path.join(self.path, HEALTH_NAME), "w",
                                  encoding="utf-8", newline="")
        self._console_fh = io.open(os.path.join(self.path, CONSOLE_NAME), "w",
                                   encoding="utf-8", newline="")

        self._meta = dict(meta or {})
        self._meta.update({
            "name": self.name,
            "folder": folder,
            "started": datetime.datetime.now().isoformat(timespec="seconds"),
            "tick_hz": BOARD_TICK_HZ,
            "columns": self._columns,
        })
        self._write_meta()
        self.active = True
        return self.path

    def stop(self):
        """Close the files and finalise meta. Safe to call when not recording."""
        if not self.active:
            return None
        self._flush(force=True)
        self._meta.update({
            "ended": datetime.datetime.now().isoformat(timespec="seconds"),
            "duration_s": round(time.time() - self.started_wall, 2),
            "rows": self.rows,
            "events": self.events,
            "dropped_frames": self.dropped,
            "late_signals": sorted(self._late),
        })
        self._write_meta()
        for fh in (self._telem_fh, self._events_fh, self._health_fh, self._console_fh):
            try:
                if fh:
                    fh.close()
            except OSError:
                pass
        self._telem_fh = self._telem_csv = None
        self._events_fh = self._health_fh = self._console_fh = None
        self.active = False
        return self.path

    def _write_meta(self):
        try:
            with io.open(os.path.join(self.path, META_NAME), "w",
                         encoding="utf-8") as fh:
                json.dump(self._meta, fh, indent=2)
        except OSError:
            pass

    def set_notes(self, notes):
        """Notes are the difference between a folder of numbers and a record of
        what you were actually trying when it went wrong."""
        self._meta["notes"] = notes
        if self.active or self.path:
            self._write_meta()

    # ---- feeding ----
    def feed_frame(self, tick, values):
        """One telemetry frame. `values` maps signal name -> the exact streamed
        string, so what lands in the CSV is what the board said, not a reformat."""
        if not self.active:
            return
        if self.started_tick is None:
            self.started_tick = tick
        # A gap in the tick sequence means frames never reached us. Count them:
        # a session that silently lost 5% of its frames should say so.
        if self.last_tick is not None and tick > self.last_tick:
            step = tick - self.last_tick
            if self._expected_step and step > self._expected_step * 1.5:
                self.dropped += int(round(step / self._expected_step)) - 1
        self.last_tick = tick

        for name in values:
            if name not in self._column_set:
                self._late.add(name)
        row = [tick, round(tick / BOARD_TICK_HZ, 4)]
        row.extend(values.get(c, "") for c in self._columns)
        self._buf.append(row)
        self.rows += 1
        self._flush()

    def feed_event(self, tick, signal, old, new):
        if not self.active or self._events_fh is None:
            return
        csv.writer(self._events_fh).writerow(
            [tick, round(tick / BOARD_TICK_HZ, 4), signal, old, new])
        self.events += 1

    def feed_health(self, obj):
        if not self.active or self._health_fh is None:
            return
        try:
            self._health_fh.write(json.dumps(obj) + "\n")
        except (OSError, TypeError, ValueError):
            pass

    def feed_console(self, text):
        if not self.active or self._console_fh is None:
            return
        try:
            self._console_fh.write(text)
        except OSError:
            pass

    # ---- internals ----
    def set_rate(self, rate_hz):
        """Expected tick delta between frames, used for drop detection. The
        board ticks at BOARD_TICK_HZ and streams every Nth tick."""
        try:
            r = float(rate_hz)
        except (TypeError, ValueError):
            r = 0.0
        self._expected_step = (BOARD_TICK_HZ / r) if r > 0 else 0.0

    def _flush(self, force=False):
        now = time.time()
        if not force and (now - self._last_flush) < FLUSH_INTERVAL_S:
            return
        self._last_flush = now
        if not self._buf or self._telem_csv is None:
            return
        try:
            self._telem_csv.writerows(self._buf)
            self._telem_fh.flush()
            if self._events_fh:
                self._events_fh.flush()
        except OSError:
            pass
        self._buf = []

    def size_bytes(self):
        if not self.path:
            return 0
        total = 0
        for fn in (TELEM_NAME, EVENTS_NAME, HEALTH_NAME, CONSOLE_NAME):
            try:
                total += os.path.getsize(os.path.join(self.path, fn))
            except OSError:
                pass
        return total


# ---------------------------------------------------------------------------
# Browsing + loading
# ---------------------------------------------------------------------------
def list_sessions():
    """Every recorded session, newest first, as dicts of its meta plus `path`."""
    root = sessions_root()
    out = []
    try:
        names = os.listdir(root)
    except OSError:
        return out
    for folder in names:
        path = os.path.join(root, folder)
        if not os.path.isdir(path):
            continue
        meta = {"name": folder, "folder": folder}
        try:
            with io.open(os.path.join(path, META_NAME), encoding="utf-8") as fh:
                meta.update(json.load(fh))
        except (OSError, ValueError):
            meta["incomplete"] = True      # killed mid-run, or not ours
        meta["path"] = path
        meta["size"] = _folder_size(path)
        out.append(meta)
    out.sort(key=lambda m: m.get("folder", ""), reverse=True)
    return out


def _folder_size(path):
    total = 0
    try:
        for fn in os.listdir(path):
            try:
                total += os.path.getsize(os.path.join(path, fn))
            except OSError:
                pass
    except OSError:
        pass
    return total


def delete_session(path):
    """Remove a session folder. Only ever called with an explicit confirmation
    from the Sessions tab - never automatically, never on a schedule."""
    root = os.path.abspath(sessions_root())
    target = os.path.abspath(path)
    if not target.startswith(root + os.sep):
        raise ValueError("refusing to delete outside the sessions folder")
    shutil.rmtree(target)


def load_session(path, depth=None):
    """Read a session's telemetry back into a SignalHistory for the Plot tab.

    Returns (history, meta, events) where events is a list of
    (t, tick, signal, old, new). Non-numeric columns (array signals) are skipped,
    exactly as the live feed skips them.
    """
    meta = {}
    try:
        with io.open(os.path.join(path, META_NAME), encoding="utf-8") as fh:
            meta = json.load(fh)
    except (OSError, ValueError):
        pass

    rows = _read_rows(os.path.join(path, TELEM_NAME))
    hist = _rows_to_history(rows, depth)
    events = _read_events(os.path.join(path, EVENTS_NAME))
    return hist, meta, events


def load_csv(path, depth=None):
    """Load any decoded log CSV (e.g. hcu_logdecode.py output) into a history.

    The time axis is taken from a `tick` or `t` column if present, otherwise the
    row index at the board tick rate - so a card log reviews exactly like a USB
    session even though the two were written by different code.
    """
    rows = _read_rows(path)
    return _rows_to_history(rows, depth)


def _read_rows(path):
    try:
        with io.open(path, encoding="utf-8", newline="") as fh:
            return list(csv.reader(fh))
    except OSError:
        return []


def _rows_to_history(rows, depth):
    if len(rows) < 2:
        return SignalHistory(depth or 600)
    header = [h.strip() for h in rows[0]]
    body = rows[1:]

    lower = [h.lower() for h in header]
    i_tick = lower.index("tick") if "tick" in lower else None
    i_t = lower.index("t") if "t" in lower else None
    if i_t is None and "time" in lower:
        i_t = lower.index("time")

    hist = SignalHistory(depth or max(600, len(body) + 16))

    # Which columns hold numbers? Sample the first rows rather than trusting the
    # header, so an array column ("1,2,3") is skipped just like it is live.
    usable = []
    for c, name in enumerate(header):
        if c in (i_tick, i_t):
            continue
        for r in body[:25]:
            if c < len(r) and r[c].strip():
                try:
                    float(r[c])
                    usable.append((c, name))
                except ValueError:
                    pass
                break

    for n, r in enumerate(body):
        if i_t is not None and i_t < len(r):
            try:
                t = float(r[i_t])
            except ValueError:
                t = n / BOARD_TICK_HZ
        elif i_tick is not None and i_tick < len(r):
            try:
                t = float(r[i_tick]) / BOARD_TICK_HZ
            except ValueError:
                t = n / BOARD_TICK_HZ
        else:
            t = n / BOARD_TICK_HZ
        for c, name in usable:
            if c < len(r) and r[c].strip():
                try:
                    hist.push(name, t, float(r[c]))
                except ValueError:
                    pass
    return hist


def _read_events(path):
    out = []
    for r in _read_rows(path)[1:]:
        if len(r) < 5:
            continue
        try:
            out.append((float(r[1]), int(r[0]), r[2], r[3], r[4]))
        except ValueError:
            continue
    return out
