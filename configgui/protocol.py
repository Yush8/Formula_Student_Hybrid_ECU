"""
configgui.protocol  --  the text protocol + the presentation tables.

Everything that describes the board's USB console protocol and how the GUI files
what it hears: the command rates, the line regexes, the supervisor-state / fault
decode tables, and the best-effort grouping helpers for the Telemetry and Config
tabs. No widgets here - just constants and pure functions - so every tab shares
one definition and they can never drift apart.

>>> Adding a parameter or a telemetry signal needs NO edit here: the board
    auto-discovers them. These tables are only presentation (grouping / decode).
"""

import os
import re

from .theme import UI

__all__ = [
    "BAUDS", "TELEM_RATES", "DEFAULT_TELEM_RATE", "SNIFF_RATES",
    "DEFAULT_SNIFF_RATE", "SNIFF_STALE_MS",
    "PLOT_HISTORY", "PLOT_HISTORY_S", "DEFAULT_HISTORY",
    "START_PARAM", "START_PULSE_MS", "RESET_PARAM", "RESET_PULSE_MS",
    "SETTINGS_PATH", "RECONNECT_MS",
    "_VALUE_RE", "_TIME_RE", "_TELEM_STATUS_RE", "_SNIFF_STATUS_RE",
    "_SNIFF_ROW_RE", "_SCHEMA_RE", "_PARAM_SECTION_RE",
    "_EVENT_RE", "_STATS_JSON_RE", "_VERSION_RE",
    "BOARD_TICK_HZ", "SESSIONS_DIR", "EVENT_LABELS", "event_label",
    "STATE_SIGNAL", "SUPERVISOR_STATES", "FAULT_SIGNAL", "FAULT_CODES",
    "DRIVE_MODE_SIGNAL", "DRIVE_MODES",
    "HV_ENABLE_LAMPS", "TELEM_GROUP_ORDER", "PARAM_GROUP_ORDER",
    "telem_group_for", "param_group_for",
]

BAUDS = ["9600", "19200", "38400", "57600", "115200"]  # cosmetic for a CDC port
TELEM_RATES = ["1", "2", "5", "10", "20", "50", "100"]  # Hz choices
DEFAULT_TELEM_RATE = "20"
SNIFF_RATES = ["1", "2", "5", "10", "20", "50"]  # CAN-sniffer snapshot rates (Hz)
DEFAULT_SNIFF_RATE = "10"
SNIFF_STALE_MS = 1000    # grey a CAN id out if not seen for this long

# The board stamps every telemetry frame and every event with the scheduler
# tick, which runs at the model step rate. Dividing by this turns a tick into
# seconds-since-boot, and THAT is the time axis the Plot tab and the session
# recorder use - not the PC's arrival time, which carries USB buffering jitter
# and would quietly make every timing measurement a lie.
# >>> MUST equal SCHED_RATE_HZ in CM7/Core/Inc/scheduler.h <<<
BOARD_TICK_HZ = 100.0

# How deep the Plot tab's capture buffer keeps every signal: label -> seconds.
# Deeper = further back you can look (and back-fill a freshly-ticked signal),
# at a proportional cost in RAM, which the Plot tab prints beside the combo.
PLOT_HISTORY = [("30 s", 30), ("1 min", 60), ("2 min", 120),
                ("5 min", 300), ("10 min", 600)]
PLOT_HISTORY_S = dict(PLOT_HISTORY)
DEFAULT_HISTORY = "2 min"

# The green START button pulses this boolean parameter high, then back to 0, so it
# behaves like a momentary press of the physical PCB start button (and can never
# stick "on"). In Simulink it is simply OR'd with the physical Start_Button inport,
# so it runs the identical ready-to-drive sequence and every interlock still
# applies. The name MUST match the params.def / Simulink Inport name.
START_PARAM = "Start_Button_GUI"
START_PULSE_MS = 400          # how long the "press" is held before it auto-releases

# The amber CLEAR-FAULT button pulses this boolean param (same momentary pattern as
# START) to acknowledge a latched supervisor ERROR and let the chart leave Error_State.
# It ALSO issues `safety reset` to clear the independent AIR fail-safe stall latch, so
# one press recovers whichever latched. Name MUST match params.def / the C OR feed.
RESET_PARAM = "Error_Reset_GUI"
RESET_PULSE_MS = 400

# Where we remember the last port / baud / auto-reconnect choice between runs.
# Recorded sessions live beside ConfigGUI.py, one folder per session (see
# configgui/session.py). Kept next to the settings file so everything the
# console owns sits in one place you can zip up and send.
SESSIONS_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "sessions")

# Friendly names for the firmware-internal events (the ones events.c raises
# itself rather than reading from event_signals.def). Model signals are shown
# under their own name, so they never need an entry here.
EVENT_LABELS = {
    "air.stall_latched":     "AIR fail-safe stall latch",
    "air.armed":             "AIR fail-safe armed",
    "air.AIR_closed":        "AIR sink closed",
    "air.precharge_closed":  "Pre-charge sink closed",
    "sched.first_overrun":   "FIRST loop overrun",
    "log.first_drop":        "FIRST dropped log record",
    "can1.first_rx_lost":    "CAN1 first lost frame",
    "can2.first_rx_lost":    "CAN2 first lost frame",
    "can1.first_recovery":   "CAN1 first bus-off recovery",
    "can2.first_recovery":   "CAN2 first bus-off recovery",
    "can1.first_tx_fail":    "CAN1 first TX failure",
    "can2.first_tx_fail":    "CAN2 first TX failure",
}


def event_label(name):
    """Human name for an event signal (falls back to the raw signal name)."""
    return EVENT_LABELS.get(name, name)


# This lives beside ConfigGUI.py (the repo root = the parent of this package dir),
# so the existing ConfigGUI.settings.json keeps working after the package split.
SETTINGS_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "ConfigGUI.settings.json")

# How often to retry the connection when auto-reconnect is on (milliseconds).
RECONNECT_MS = 2000

# A board reply that carries a parameter value, e.g.
#   "kp = 1.500"   |   "ok: kp = 2.000"   |   "ok (clamped): torque_limit = 240"
# The value must look numeric (starts with a sign or digit), which keeps prose
# lines like the help text from being mistaken for parameters.
_VALUE_RE = re.compile(
    r'^(?:ok(?:\s*\(clamped\))?:\s*)?'      # optional "ok:" / "ok (clamped):"
    r'([A-Za-z_]\w*)\s*=\s*'                # parameter name
    r'([+-]?\d+(?:\.\d+)?)\s*$'             # numeric value
)

# A wall-clock reply, e.g. "2026-06-14 12:00:00" or "time set: 2026-06-14 ..."
_TIME_RE = re.compile(
    r'(?:time set:\s*)?'
    r'(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})\s*$'
)

# A telemetry status line, e.g. "telem on  rate 20 Hz  signals 33"
_TELEM_STATUS_RE = re.compile(
    r'^telem\s+(on|off)\s+rate\s+(\d+)\s*Hz\s+signals\s+(\d+)', re.IGNORECASE)

# A CAN-sniffer status line, e.g. "cansniff on  rate 10 Hz  bus1 7/64 ids ..."
_SNIFF_STATUS_RE = re.compile(
    r'^cansniff\s+(on|off)\s+rate\s+(\d+)\s*Hz', re.IGNORECASE)

# A CAN-sniffer stream row: "#C <bus> <idhex> <count> <age_ms> <dlc> <datahex>"
_SNIFF_ROW_RE = re.compile(
    r'^#C\s+(\d+)\s+([0-9A-Fa-f]+)\s+(\d+)\s+(\d+)\s+(\d+)\s*([0-9A-Fa-f]*)\s*$')

# A telemetry schema line, e.g. "Torque_Scale_Factor f32 1" or "APPS u8 8"
_SCHEMA_RE = re.compile(r'^([A-Za-z_]\w*)\s+([a-z0-9]+)\s+(\d+)\s*$')

# A parameter-section header emitted by `list`, e.g. "# section: Torque Vectoring".
# The board interleaves these with the "name = value" lines to tell us how
# params.def groups its parameters, so the Config tab needs no per-parameter edit.
_PARAM_SECTION_RE = re.compile(r'^#\s*section:\s*(.+?)\s*$', re.IGNORECASE)

# An event line from the board's event recorder:
#   "#E <tick> <name> <old> <new>"
# Emitted on the model step the instant a watched discrete signal changes, so
# the ORDER of events is exact rather than accurate-to-one-telemetry-frame.
_EVENT_RE = re.compile(
    r'^#E\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$')

# The machine-readable health dump from `stats json`: "#J {...}".
_STATS_JSON_RE = re.compile(r'^#J\s+(\{.*\})\s*$')

# The first line of `version`, e.g. "HCU V2 firmware 2.4.0".
_VERSION_RE = re.compile(r'^HCU V2 firmware\s+(\S+)\s*$')

# ---- Controller-state banner ------------------------------------------------
# The Safety_Supervisor state machine streams its current state as a plain number
# in the "State_Enum" telemetry signal; this table turns that number into a human
# name + colour for the always-on banner. The NUMBERS here MUST match the value
# the model writes to State_Enum in each Safety_Supervisor state (see the mapping
# block in CM7/Core/Inc/telem_signals.def). Add/rename freely - an unlisted
# number just shows as "STATE <n>" in purple, so nothing breaks.
STATE_SIGNAL = "State_Enum"
SUPERVISOR_STATES = {          # code: (display name, background, foreground)
    0:  ("INIT / UNKNOWN", "#39424e",       "#e6edf3"),  # slate - chart not run yet
    1:  ("HV OFF",         "#39424e",       "#e6edf3"),  # slate - shutdown / no HV
    2:  ("STANDBY",        UI["accent_ac"], "#ffffff"),  # blue  - HV up, idle, ready
    3:  ("PRE-CHARGE",     UI["amber"],     "#1a1206"),  # amber - bus charging
    4:  ("RELAY SWAP",     UI["amber"],     "#1a1206"),  # amber - AIR closing
    5:  ("DRIVE",          UI["green_hi"],  "#ffffff"),  # green - armed, motors live
    6:  ("ERROR / FAULT",  "#da3633",       "#ffffff"),  # red   - latched fault
}

# ---- Fault-reason decode (the "why" behind an ERROR) ------------------------
# When State_Enum == 6 the model also streams "Fault_Code", the reason it latched
# ERROR (stamped on the transition into Error_State). This table turns that number
# into a human phrase appended to the red banner, e.g. "ERROR / FAULT - BMS zero-
# limit". The NUMBERS must match the codes the Stateflow chart writes (see the note
# in CM7/Core/Inc/telem_signals.def). An unlisted code shows as "code <n>", so
# adding a new fault cause never breaks the GUI - you just file its number here.
FAULT_SIGNAL = "Fault_Code"
FAULT_CODES = {
    0:  "no fault",
    # --- live today ---
    20: "BMS zero-limit (DCL & CCL = 0)",
    40: "pre-charge timeout",
    # --- reserved for when you route these into Error_State ---
    21: "BMS frame stale",
    30: "APPS implausible (2-channel)",
    31: "APPS + brake plausibility",
    32: "APPS frame stale",
    41: "relay-swap / engine-sync timeout",
    50: "ODrive fault",
    51: "ODrive frame stale",
    60: "CAN bus-off",
    10: "SDC open",
}

# ---- Control-mode chip (torque vs velocity) --------------------------------
# The model streams Velocity_Mode_Active (0/1) = which ODrive control mode the
# firmware is actually commanding: torque (race) or velocity (a jacked, wheels-off
# bench spin). The banner turns it into a coloured chip beside the HV lamps so an
# operator can NEVER miss that the car is in the bench velocity mode. Same pattern
# as SUPERVISOR_STATES - an unlisted value just shows "MODE <n>" in purple.
DRIVE_MODE_SIGNAL = "Velocity_Mode_Active"
DRIVE_MODES = {                # code: (chip text, background, foreground)
    0: ("TORQUE",   UI["green_hi"], "#ffffff"),   # race - normal
    1: ("VELOCITY", UI["amber"],    "#1a1206"),   # bench - wheels-off spin
}

# HV-actuator enable outputs shown as compact lamps beside the state name
# (green = energised / closed, grey = open). These are ordinary boolean model
# Outports already in telem_signals.def. (telemetry signal name, short label)
HV_ENABLE_LAMPS = [
    ("AIR_Enable",        "AIR"),
    ("Pre_Charge_Enable", "PRE-CHG"),
    ("Inverter_Enable",   "INVERTER"),
]

# ---------------------------------------------------------------------------
# Logical grouping  (organisation only - nothing here changes behaviour)
# ---------------------------------------------------------------------------
# The board still auto-discovers every signal / parameter for us; these two
# helpers just sort each one into a labelled section so the Live Telemetry and
# Config tabs read as tidy, titled groups instead of one long alphabetical wall
# you have to scroll and hunt through. A name that matches nothing lands in
# "Other", so a brand-new signal is never hidden - it just isn't filed yet, and
# filing it is a one-line edit here (no firmware change).

# Fixed top-to-bottom order the telemetry groups are shown in.
TELEM_GROUP_ORDER = [
    "Loop & System",
    "Supervisor & Safety",
    "Driver & ECU Inputs",
    "Torque & Vectoring",
    "BMS & Power",
    "ODrive & Motors",
    "Raw CAN & Bus Health",
    "Bench / Test Bypasses",
    "Other",
]

# FALLBACK ordering for the Config (parameter) sections, used only when the board
# doesn't report sections (older firmware). New firmware sends "# section:" headers
# in `list`, and the GUI then shows the sections in params.def's own order instead
# of this list. Kept in sync with params.def's headings as a sensible default.
PARAM_GROUP_ORDER = [
    "Controller / PID",
    "BMS Power Limiter",
    "Regen / Charge Limiter",
    "Torque Vectoring",
    "Bench / Test",
    "Drive / Start",
    "Other",
]


def telem_group_for(name, is_array=False):
    """Sort one telemetry signal into a functional group (first match wins)."""
    if name in ("tick", "overruns") or name.startswith("User_LED"):
        return "Loop & System"
    # Raw 8-byte CAN payloads + their freshness stamps + the bus-health flags.
    if is_array or name.endswith("_age") or name in ("bus1_ok", "bus2_ok"):
        return "Raw CAN & Bus Health"
    if name in ("State_Enum", "Fault_Code", "Engine_Synced", "Sync_State",
                "APPS_Implausibility", "BMS_Fault", "SDC_Monitor", "AIR_Enable",
                "Pre_Charge_Enable", "Inverter_Enable", "Start_Button",
                "Start_Button_GUI", "Reset_Req", "Error_Reset_GUI"):
        return "Supervisor & Safety"
    if name.startswith("Bench_") or name in ("Vel_Scale", "Velocity_Mode_Active"):
        return "Bench / Test Bypasses"
    if (name in ("DCL", "CCL", "Pack_Voltage", "Drive_Efficiency", "BMS_Margin",
                 "Left_Direction", "Right_Direction")
            or name.startswith("Bus_") or name.startswith("BMS_")
            or name.startswith("Regen_") or name.startswith("Motor_")):
        return "BMS & Power"
    if name in ("APPS_Clean", "Vehicle_Speed", "Steering_Angle", "Brake_Pressure"):
        return "Driver & ECU Inputs"
    if ("Torque" in name or name in ("Power_Demand", "Power_Budget", "Delta_Torque",
                                     "TV_Gain", "Steering_Centre", "Steering_Deadzone",
                                     "Max_Torque_Split")):
        return "Torque & Vectoring"
    if (name.startswith("ODrive") or name.startswith("Velocity")
            or name.startswith("Set_Axis_State")):
        return "ODrive & Motors"
    return "Other"


def param_group_for(name):
    """FALLBACK grouping for one parameter, used only when the board doesn't report
    a section for it (older firmware). New firmware declares the section in
    params.def (PARAM_SECTION) and sends it in `list`, which takes precedence."""
    if name in ("kp", "ki", "torque_limit", "Parameter_1"):
        return "Controller / PID"
    if name in ("Drive_Efficiency", "BMS_Margin", "Motor_Torque_Max",
                "Left_Direction", "Right_Direction"):
        return "BMS Power Limiter"
    if name.startswith("Regen_") or name == "Motor_Regen_Max":
        return "Regen / Charge Limiter"
    if name in ("TV_Gain", "Steering_Centre", "Steering_Deadzone", "Max_Torque_Split"):
        return "Torque Vectoring"
    if name.startswith("Bench_") or name == "Vel_Scale":
        return "Bench / Test"
    if "Start" in name:
        return "Drive / Start"
    return "Other"
