"""
configgui.bundle  --  "here is everything about what just went wrong", in one file.

Press one button and get a single .zip holding the whole picture of the moment:

    README.md          a written summary: firmware, state, what fired, what is
                       unhealthy, the last events - readable on its own
    health.json        the board's `stats json` (loop timing, CAN, SD, AIR)
    version.txt        `version` output: build stamp and .def fingerprints
    params.csv         every parameter and its current value
    events.csv         the decoded event timeline
    window.csv         every signal over the capture window, board tick + seconds
    signals.txt        the telemetry schema (name, type, length)
    can.csv            every CAN id seen, named where the firmware routes it
    console.log        the raw console text

The point is that this is the thing you attach. Describing a fault in prose
loses the timing, the counters and the ordering; this loses nothing, and both a
human and an AI can read it without access to the car.

README.md is written FIRST and deliberately answers the questions someone
helping you would ask anyway: what firmware, what was the state, what fired,
what is unhealthy, what happened just before. Everything else is the evidence.
"""

import io
import os
import csv
import json
import time
import zipfile
import datetime

from .protocol import (
    SUPERVISOR_STATES, FAULT_CODES, STATE_SIGNAL, FAULT_SIGNAL, BOARD_TICK_HZ,
)


def default_name():
    return "hcu_debug_" + datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S") + ".zip"


def build(path, ctx):
    """Write the bundle to `path`.

    `ctx` is a plain dict assembled by the GUI, so this module needs no access to
    the app object and stays testable:

        version       str   raw `version` output
        health        dict  parsed `stats json` (or {})
        params        list  [(name, value)]
        schema        list  [(name, type, length)]
        events        list  decoded event rows (events_model)
        history       SignalHistory (optional)
        window        (t_lo, t_hi) board seconds to export (optional)
        can_rows      list  [(bus, id_int, name, count, rate, age_ms, data)]
        can_missing   list  [(bus, id_int, name)] expected but never seen
        console       str   the console log text
        notes         str   whatever you typed in the box
        session       str   path of the session being recorded, if any
        telem_rate    str
        dropped       int   telemetry frames known lost
    """
    t0 = time.time()
    written = []
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("README.md", _readme(ctx)); written.append("README.md")

        if ctx.get("health"):
            z.writestr("health.json", json.dumps(ctx["health"], indent=2))
            written.append("health.json")
        if ctx.get("version"):
            z.writestr("version.txt", ctx["version"]); written.append("version.txt")

        if ctx.get("params"):
            z.writestr("params.csv", _csv(["name", "value"], ctx["params"]))
            written.append("params.csv")
        if ctx.get("schema"):
            z.writestr("signals.txt", "\n".join(
                "%s %s %s" % row for row in ctx["schema"]))
            written.append("signals.txt")

        if ctx.get("events"):
            rows = [(r["tick"], r["t"], r["severity"], r["signal"], r["label"],
                     r["old"], r["new"], r["old_text"], r["new_text"])
                    for r in ctx["events"]]
            z.writestr("events.csv", _csv(
                ["tick", "t", "severity", "signal", "label",
                 "from", "to", "from_text", "to_text"], rows))
            written.append("events.csv")

        win = _window_csv(ctx)
        if win:
            z.writestr("window.csv", win); written.append("window.csv")

        if ctx.get("can_rows"):
            z.writestr("can.csv", _csv(
                ["bus", "id", "name", "count", "rate_hz", "age_ms", "data"],
                [(b, "0x%03X" % i, n, c, r, a, d)
                 for b, i, n, c, r, a, d in ctx["can_rows"]]))
            written.append("can.csv")

        if ctx.get("console"):
            z.writestr("console.log", ctx["console"]); written.append("console.log")

    return {"path": path, "files": written,
            "bytes": os.path.getsize(path),
            "seconds": round(time.time() - t0, 2)}


# ---------------------------------------------------------------------------
def _readme(ctx):
    L = []
    add = L.append
    add("# HCU debug bundle")
    add("")
    add("Exported %s from the HCU V2 console." %
        datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    add("")

    if ctx.get("notes"):
        add("## What I was doing")
        add("")
        add(ctx["notes"].strip())
        add("")

    # ---- the headline: what state was the car in ----
    add("## State at export")
    add("")
    hist = ctx.get("history")
    state_txt = fault_txt = "unknown"
    if hist is not None:
        s = hist.value_at(STATE_SIGNAL, hist.latest_time())
        f = hist.value_at(FAULT_SIGNAL, hist.latest_time())
        if s is not None:
            state_txt = "%s (%d)" % (
                SUPERVISOR_STATES.get(int(s), ("STATE %d" % int(s),))[0], int(s))
        if f is not None:
            fault_txt = "%s (%d)" % (FAULT_CODES.get(int(f), "code %d" % int(f)),
                                     int(f))
    add("- supervisor: **%s**" % state_txt)
    add("- fault code: **%s**" % fault_txt)
    if ctx.get("trigger_reason"):
        add("- trigger fired: **%s**" % ctx["trigger_reason"])
    if ctx.get("session"):
        add("- recording session: `%s`" % os.path.basename(ctx["session"]))
    add("")

    # ---- health verdict, stated rather than left to be read out of JSON ----
    h = ctx.get("health") or {}
    if h:
        add("## Board health")
        add("")
        for line in _health_lines(h):
            add("- " + line)
        add("")

    if ctx.get("dropped"):
        add("> **%d telemetry frames were lost between the board and this PC.** "
            "Gaps in window.csv are missing data, not a stalled board."
            % ctx["dropped"])
        add("")

    # ---- what happened, most recent last ----
    events = ctx.get("events") or []
    if events:
        bad = [r for r in events if r["severity"] == "error"]
        add("## Events")
        add("")
        add("%d recorded, %d of them errors. Most recent last; `t` is seconds "
            "since the board booted." % (len(events), len(bad)))
        add("")
        add("```")
        for r in events[-40:]:
            add("%9.3f  %-5s %-26s %s -> %s" % (
                r["t"], r["severity"].upper(), r["label"],
                r["old_text"], r["new_text"]))
        add("```")
        add("")

    miss = ctx.get("can_missing") or []
    if miss:
        add("## CAN ids the firmware expects but never saw")
        add("")
        for bus, cid, name in miss:
            add("- bus%d `0x%03X` %s" % (bus, cid, name))
        add("")

    add("## Files")
    add("")
    add("| file | what it holds |")
    add("|---|---|")
    for name, what in (
            ("health.json", "`stats json`: loop timing, CAN, SD ring, AIR fail-safe"),
            ("version.txt", "firmware build stamp and .def fingerprints"),
            ("params.csv", "every tunable parameter and its value at export"),
            ("events.csv", "the full decoded event timeline"),
            ("window.csv", "every signal over the exported window (board tick + seconds)"),
            ("signals.txt", "the telemetry schema: name, type, length"),
            ("can.csv", "every CAN id seen, named where the firmware routes it"),
            ("console.log", "raw console text for the session")):
        add("| `%s` | %s |" % (name, what))
    add("")
    add("Time base: every `t` is derived from the board's own %g Hz scheduler "
        "tick, not the PC clock, so timings are the board's." % BOARD_TICK_HZ)
    return "\n".join(L) + "\n"


def _health_lines(h):
    """Turn the health JSON into plain verdicts. Someone reading the bundle
    should not have to know what TEC or LEC mean to see that CAN2 is dead."""
    out = []
    over = h.get("overruns", 0)
    out.append("loop: %s (%s overruns over %s ticks)" %
               ("OK" if not over else "**MISSED DEADLINES**", over, h.get("ticks", "?")))
    step, late = h.get("step_us") or {}, h.get("late_us") or {}
    if step:
        out.append("step time: %s us avg, %s us worst, budget %s us" %
                   (step.get("avg"), step.get("max"), h.get("step_budget_us")))
    if late:
        out.append("step lateness: %s us avg, %s us worst" %
                   (late.get("avg"), late.get("max")))
    for bus in ("can1", "can2"):
        c = h.get(bus) or {}
        if not c:
            continue
        if c.get("bus_off"):
            verdict = "**BUS-OFF**" + (" (gave up)" if c.get("gave_up") else " (recovering)")
        elif c.get("rx_lost") or c.get("recoveries"):
            verdict = "recovered after trouble"
        else:
            verdict = "OK"
        out.append("%s: %s - rx_lost %s, recoveries %s, tx_done %s, tx_fail %s, "
                   "TEC %s, pending %s" %
                   (bus.upper(), verdict, c.get("rx_lost"), c.get("recoveries"),
                    c.get("tx_done"), c.get("tx_fail"), c.get("tec"),
                    c.get("tx_pending")))
    lg = h.get("log") or {}
    if lg:
        out.append("SD log: %s - drops %s, ring %s/%s (peak %s)" %
                   ("**DROPPING RECORDS**" if lg.get("drops") else "OK",
                    lg.get("drops"), lg.get("ring"), lg.get("ring_max"),
                    lg.get("peak")))
    air = h.get("air") or {}
    if air:
        if air.get("latched"):
            verdict = "**STALL LATCHED - AIRs forced open**"
        elif not air.get("armed"):
            verdict = "not armed (model has not stepped)"
        elif air.get("trips"):
            verdict = "recovered after %s trip(s)" % air.get("trips")
        else:
            verdict = "OK"
        out.append("AIR fail-safe: %s" % verdict)
    return out


def _window_csv(ctx):
    """Every recorded signal over the requested window, as a wide CSV on one
    shared time axis - the same shape a session's telemetry.csv has."""
    hist = ctx.get("history")
    if hist is None:
        return ""
    names = sorted(hist.names())
    if not names:
        return ""
    t_hi = hist.latest_time()
    win = ctx.get("window")
    t_lo, t_hi = win if win else (t_hi - 30.0, t_hi)

    # Build one row per distinct timestamp, so signals line up by time rather
    # than each being written as its own independent series.
    stamps = set()
    series = {}
    for n in names:
        ts, vs = hist.window(n, t_lo, t_hi)
        if not len(ts):
            continue
        col = {}
        for t, v in zip(ts, vs):
            key = round(float(t), 4)
            col[key] = v
            stamps.add(key)
        series[n] = col
    if not stamps:
        return ""

    cols = [n for n in names if n in series]
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(["tick", "t"] + cols)
    for t in sorted(stamps):
        w.writerow([int(round(t * BOARD_TICK_HZ)), "%.4f" % t] +
                   [_val(series[n].get(t)) for n in cols])
    return buf.getvalue()


def _val(v):
    if v is None:
        return ""
    f = float(v)
    return "%d" % int(f) if f == int(f) else "%.6g" % f


def _csv(header, rows):
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(header)
    for r in rows:
        w.writerow(list(r))
    return buf.getvalue()
