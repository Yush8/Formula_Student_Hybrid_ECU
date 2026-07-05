#!/usr/bin/env python3
"""
HCU V2 Console
==============
A friendly GUI front-end for the USB-CDC command console on the Formula
Student hybrid controller. It speaks the *exact* same text protocol you
already use in PuTTY, so the firmware needs no changes:

    list                          -> dump all tunable parameters
    get <name>                    -> read one
    set <name> <value>            -> write one   (ints accept 0x.. hex too)
    save                          -> commit current values to flash
    defaults                      -> reset to built-in defaults (RAM only)
    time                          -> show the board's wall-clock
    time set YYYY-MM-DD HH:MM:SS  -> set the wall-clock
    stats                         -> trackside health (loop / CAN / logging)
    stats clear                   -> zero the stats counters
    telem on|off                  -> start/stop the live model-signal stream
    telem rate <hz>               -> set the stream rate (1..100 Hz)
    telem list                    -> schema of the streamed signals
    ping                          -> pong

Run it:
    pip install pyserial
    python ConfigGUI.py

(tkinter ships with Python. On Debian/Ubuntu: sudo apt install python3-tk)

NOTE: only one program can own a COM port at a time -- close PuTTY before
connecting here, and vice-versa.

>>> NOTHING TO MAINTAIN HERE WHEN YOU ADD A PARAMETER OR A TELEMETRY SIGNAL. <<<
  * The tunables are discovered live by parsing `list`, so any parameter you add
    in CM7/Core/Inc/params.def shows up in the Config tab automatically.
  * The live signals are discovered live by parsing `telem list` (and the stream
    itself), so any signal you add in CM7/Core/Inc/telem_signals.def shows up in
    the Live Telemetry tab automatically.
No edit to this file is ever needed for either.
"""

import os
import re
import json
import time
import queue
import threading
import datetime
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

import serial
import serial.tools.list_ports

BAUDS = ["9600", "19200", "38400", "57600", "115200"]  # cosmetic for a CDC port
TELEM_RATES = ["1", "2", "5", "10", "20", "50", "100"]  # Hz choices
DEFAULT_TELEM_RATE = "20"

# ---------------------------------------------------------------------------
# Visual theme  (cosmetics only - nothing here changes behaviour)
# ---------------------------------------------------------------------------
# A single dark "trackside" palette applied to every widget so the console
# reads as one cohesive dashboard instead of default-grey tkinter. Tweak the
# hex values freely to taste; no logic depends on them.
UI = {
    "page":      "#0d1117",   # window background / gutters between cards
    "card":      "#161b22",   # panels, labelframes, tab bodies
    "field":     "#0d1117",   # inset inputs, tree + console field
    "elev":      "#1c232b",   # raised chips: buttons, headings, banners
    "border":    "#30363d",   # hairline outlines / separators
    "fg":        "#e6edf3",   # primary text
    "muted":     "#8b98a5",   # secondary / hint text
    "accent":    "#3b82f6",   # primary blue
    "accent_hi": "#60a5fa",   # hover / highlighted values
    "accent_ac": "#2563eb",   # pressed
    "green":     "#22c55e",   # go / energised
    "green_hi":  "#16a34a",
    "green_ac":  "#15803d",   # pressed START
    "amber":     "#f59e0b",   # transitional states / reconnecting
    "red":       "#ef4444",
    "red_hi":    "#f87171",   # disconnected text on dark
    "purple":    "#8957e5",   # unknown supervisor state
}

FONT_UI    = ("Segoe UI", 10)
FONT_UI_SM = ("Segoe UI", 9)
FONT_UI_B  = ("Segoe UI", 10, "bold")
FONT_MONO  = ("Consolas", 10)

# The green START button pulses this boolean parameter high, then back to 0, so it
# behaves like a momentary press of the physical PCB start button (and can never
# stick "on"). In Simulink it is simply OR'd with the physical Start_Button inport,
# so it runs the identical ready-to-drive sequence and every interlock still
# applies. The name MUST match the params.def / Simulink Inport name.
START_PARAM = "Start_Button_GUI"
START_PULSE_MS = 400          # how long the "press" is held before it auto-releases

# Where we remember the last port / baud / auto-reconnect choice between runs.
SETTINGS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
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

# A telemetry schema line, e.g. "Torque_Scale_Factor f32 1" or "APPS u8 8"
_SCHEMA_RE = re.compile(r'^([A-Za-z_]\w*)\s+([a-z0-9]+)\s+(\d+)\s*$')

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

# HV-actuator enable outputs shown as compact lamps beside the state name
# (green = energised / closed, grey = open). These are ordinary boolean model
# Outports already in telem_signals.def. (telemetry signal name, short label)
HV_ENABLE_LAMPS = [
    ("AIR_Enable",        "AIR"),
    ("Pre_Charge_Enable", "PRE-CHG"),
    ("Inverter_Enable",   "INVERTER"),
]


class ConsoleApp:
    def __init__(self, root):
        self.root = root
        self.ser = None
        self.reader = None
        self.reader_stop = threading.Event()
        self.rx_queue = queue.Queue()
        self.rx_buffer = ""

        # name -> dict(value_var, entry_var, widgets)
        self.params = {}
        self._param_order = []
        self._param_buttons = []

        # ---- live telemetry state ----
        self._telem_order = []        # signal names in discovery order
        self._telem_last = {}         # name -> last displayed value (skip no-ops)
        self._telem_streaming = False
        self._schema_collecting = False
        self._telem_frames = 0        # frames since the last Hz sample
        self._telem_hz = 0.0
        self._telem_filter = ""

        self._port_map = {}
        self._target_device = None       # device we (try to) stay connected to
        self._reconnect_after = None     # pending root.after id for reconnect
        self._user_disconnected = False  # True after an explicit Disconnect

        root.title("HCU V2  ·  Console")
        root.minsize(900, 760)
        self._apply_theme()

        self.auto_reconnect_var = tk.BooleanVar(value=True)
        self.telem_rate_var = tk.StringVar(value=DEFAULT_TELEM_RATE)

        self._build_header()
        self._build_connection_bar()
        self._build_actions_bar()
        self._build_drive_bar()
        self._build_state_banner()
        self._build_notebook()
        self._set_connected(False)

        self.refresh_ports()
        self._load_settings()
        self.root.after(50, self._poll_rx)
        self.root.after(1000, self._telem_tick)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        # If we remembered a port and auto-reconnect is on, connect on startup.
        if self.auto_reconnect_var.get() and self._target_device:
            self.root.after(200, self._try_reconnect)

    # ---------------- theme / chrome ----------------
    def _apply_theme(self):
        """One dark 'trackside' palette applied to every widget so the console
        reads as a single dashboard. Pure cosmetics - no behaviour depends on
        anything in here."""
        root = self.root
        st = ttk.Style()
        try:
            st.theme_use("clam")     # the only built-in theme we can fully recolour
        except tk.TclError:
            pass

        page, card, field = UI["page"], UI["card"], UI["field"]
        elev, border      = UI["elev"], UI["border"]
        fg, muted, accent = UI["fg"], UI["muted"], UI["accent"]

        root.configure(background=page)
        self._enable_dark_titlebar()

        st.configure(".", background=card, foreground=fg, fieldbackground=field,
                     bordercolor=border, lightcolor=card, darkcolor=card,
                     troughcolor=field, focuscolor=elev, font=FONT_UI)

        # Frames: cards are 'card', the window itself + gutters are 'page'.
        st.configure("TFrame", background=card)
        st.configure("Page.TFrame", background=page)
        st.configure("Header.TFrame", background=card)

        # Labels.
        st.configure("TLabel", background=card, foreground=fg, font=FONT_UI)
        st.configure("Muted.TLabel", background=card, foreground=muted, font=FONT_UI_SM)
        st.configure("H1.TLabel", background=card, foreground=fg,
                     font=("Segoe UI", 16, "bold"))
        st.configure("H1Accent.TLabel", background=card, foreground=accent,
                     font=("Segoe UI", 16, "bold"))
        st.configure("H2.TLabel", background=card, foreground=muted, font=FONT_UI_SM)

        # Card panels.
        st.configure("TLabelframe", background=card, bordercolor=border,
                     relief="solid", borderwidth=1)
        st.configure("TLabelframe.Label", background=card, foreground=accent,
                     font=FONT_UI_B)

        # Buttons: readable 'chip' by default, accent variant for primary actions.
        st.configure("TButton", background="#21262d", foreground=fg,
                     bordercolor=border, relief="solid", borderwidth=1,
                     padding=(12, 6), font=FONT_UI, focuscolor="#21262d")
        st.map("TButton",
               background=[("disabled", card), ("pressed", UI["accent_ac"]),
                           ("active", "#2b333d")],
               foreground=[("disabled", muted)],
               bordercolor=[("active", accent), ("focus", accent),
                            ("disabled", border)])

        st.configure("Accent.TButton", background=accent, foreground="#ffffff",
                     bordercolor=accent, relief="solid", borderwidth=1,
                     focuscolor=accent, font=FONT_UI_B)
        st.map("Accent.TButton",
               background=[("disabled", "#22303f"), ("pressed", UI["accent_ac"]),
                           ("active", UI["accent_hi"])],
               foreground=[("disabled", muted)],
               bordercolor=[("disabled", "#22303f"), ("active", UI["accent_hi"])])

        # Checkbutton.
        st.configure("TCheckbutton", background=card, foreground=fg, focuscolor=card)
        st.map("TCheckbutton",
               background=[("active", card)],
               foreground=[("disabled", muted)],
               indicatorcolor=[("selected", accent), ("!selected", field)])

        # Text entry.
        st.configure("TEntry", fieldbackground=field, foreground=fg,
                     bordercolor=border, insertcolor=fg, padding=5)
        st.map("TEntry", bordercolor=[("focus", accent)],
               fieldbackground=[("disabled", card)])

        # Combobox (+ its popup listbox, which is a classic-tk widget).
        st.configure("TCombobox", fieldbackground=field, background=elev,
                     foreground=fg, arrowcolor=fg, bordercolor=border, padding=4)
        st.map("TCombobox",
               fieldbackground=[("readonly", field), ("disabled", card)],
               foreground=[("disabled", muted)],
               arrowcolor=[("disabled", muted)],
               bordercolor=[("focus", accent), ("active", accent)])
        root.option_add("*TCombobox*Listbox.background", card)
        root.option_add("*TCombobox*Listbox.foreground", fg)
        root.option_add("*TCombobox*Listbox.selectBackground", accent)
        root.option_add("*TCombobox*Listbox.selectForeground", "#ffffff")
        root.option_add("*TCombobox*Listbox.font", FONT_UI)

        # Notebook: selected tab flows into the card body below it.
        st.configure("TNotebook", background=page, bordercolor=border,
                     tabmargins=(4, 6, 4, 0))
        st.configure("TNotebook.Tab", background=page, foreground=muted,
                     padding=(18, 9), font=FONT_UI_B, bordercolor=border)
        st.map("TNotebook.Tab",
               background=[("selected", card)],
               foreground=[("selected", fg), ("active", fg)])

        # Treeview (the live-telemetry grid).
        st.configure("Treeview", background=field, fieldbackground=field,
                     foreground=fg, bordercolor=border, borderwidth=0, rowheight=26)
        st.configure("Treeview.Heading", background=elev, foreground=muted,
                     relief="flat", font=FONT_UI_B, padding=(8, 7))
        st.map("Treeview",
               background=[("selected", "#1f6feb")],
               foreground=[("selected", "#ffffff")])
        st.map("Treeview.Heading", background=[("active", "#28303a")])

        # Scrollbars + separators.
        for orient in ("Vertical", "Horizontal"):
            st.configure(f"{orient}.TScrollbar", background=elev, troughcolor=page,
                         bordercolor=page, arrowcolor=muted, relief="flat")
            st.map(f"{orient}.TScrollbar", background=[("active", border)])
        st.configure("TSeparator", background=border)

    def _enable_dark_titlebar(self):
        """Ask Windows (DWM) to paint this window's title bar dark to match the
        UI. Harmless no-op on non-Windows or older builds."""
        try:
            import ctypes
            self.root.update_idletasks()
            hwnd = ctypes.windll.user32.GetParent(self.root.winfo_id())
            flag = ctypes.c_int(1)
            for attr in (20, 19):   # DWMWA_USE_IMMERSIVE_DARK_MODE (new, then old)
                ctypes.windll.dwmapi.DwmSetWindowAttribute(
                    hwnd, attr, ctypes.byref(flag), ctypes.sizeof(flag))
        except Exception:
            pass

    def _build_header(self):
        """A slim title band so the tool opens with an identity instead of a bare
        row of buttons. Cosmetic only."""
        band = ttk.Frame(self.root, style="Header.TFrame")
        band.pack(fill="x")
        inner = ttk.Frame(band, style="Header.TFrame")
        inner.pack(fill="x", padx=16, pady=(12, 10))

        title = ttk.Frame(inner, style="Header.TFrame")
        title.pack(side="left")
        ttk.Label(title, text="◆ HCU", style="H1Accent.TLabel").pack(side="left")
        ttk.Label(title, text=" V2", style="H1.TLabel").pack(side="left")
        ttk.Label(title, text="   CONSOLE", style="H2.TLabel").pack(
            side="left", pady=(7, 0))

        ttk.Label(inner,
                  text="Formula Student · Hybrid Control Unit · trackside console",
                  style="H2.TLabel").pack(side="right", pady=(7, 0))

        tk.Frame(self.root, height=2, background=UI["accent"]).pack(fill="x")

    # ---------------- UI construction ----------------
    def _build_connection_bar(self):
        f = ttk.LabelFrame(self.root, text="Connection")
        f.pack(fill="x", padx=8, pady=(8, 4))

        ttk.Label(f, text="Port:").grid(row=0, column=0, padx=4, pady=6, sticky="w")
        self.port_cb = ttk.Combobox(f, width=26, state="readonly")
        self.port_cb.grid(row=0, column=1, padx=4, pady=6)
        ttk.Button(f, text="↻", width=3, command=self.refresh_ports).grid(row=0, column=2, padx=2)

        ttk.Label(f, text="Baud:").grid(row=0, column=3, padx=4)
        self.baud_cb = ttk.Combobox(f, width=8, state="readonly", values=BAUDS)
        self.baud_cb.set("115200")
        self.baud_cb.grid(row=0, column=4, padx=4)

        self.connect_btn = ttk.Button(f, text="Connect", style="Accent.TButton",
                                       command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, padx=6)

        ttk.Checkbutton(f, text="Auto-reconnect", variable=self.auto_reconnect_var,
                        command=self._save_settings).grid(row=0, column=6, padx=6)

        self.status_lbl = ttk.Label(f, text="● disconnected",
                                    font=FONT_UI_B, foreground=UI["red_hi"])
        self.status_lbl.grid(row=0, column=7, padx=8)

    def _build_actions_bar(self):
        f = ttk.LabelFrame(self.root, text="Board")
        f.pack(fill="x", padx=8, pady=4)

        bar = ttk.Frame(f)
        bar.pack(fill="x", padx=4, pady=6)

        self.refresh_all_btn = ttk.Button(bar, text="Refresh all (list)", command=self.rescan_all)
        self.refresh_all_btn.pack(side="left")
        self.ping_btn = ttk.Button(bar, text="Ping", command=self.cmd_ping)
        self.ping_btn.pack(side="left", padx=6)

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)

        # Health readout. Output lands in the Console tab (multi-line).
        self.stats_btn = ttk.Button(bar, text="Stats", command=self.cmd_stats)
        self.stats_btn.pack(side="left")
        self.clearstats_btn = ttk.Button(bar, text="Clear stats", command=self.cmd_stats_clear)
        self.clearstats_btn.pack(side="left", padx=6)

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)

        self.save_btn = ttk.Button(bar, text="Save to flash", command=self.cmd_save)
        self.save_btn.pack(side="left")
        self.defaults_btn = ttk.Button(bar, text="Load defaults", command=self.cmd_defaults)
        self.defaults_btn.pack(side="left", padx=6)

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)

        self.settime_btn = ttk.Button(bar, text="Set clock → PC time", command=self.cmd_set_time_now)
        self.settime_btn.pack(side="left")
        self.gettime_btn = ttk.Button(bar, text="Show time", command=self.cmd_get_time)
        self.gettime_btn.pack(side="left", padx=6)
        self.clock_var = tk.StringVar(value="—")
        ttk.Label(bar, text="Board clock:", style="Muted.TLabel").pack(
            side="left", padx=(8, 2))
        ttk.Label(bar, textvariable=self.clock_var, foreground=UI["accent_hi"],
                  background=UI["card"], font=FONT_MONO).pack(side="left")

    # ---- Drive control (the big START button) ----
    def _build_drive_bar(self):
        """A big, obvious START button that mirrors the physical PCB start button.
        Clicking it PULSES the Start_Button_GUI parameter (1, then back to 0 a
        moment later) so the board sees a momentary press. In Simulink that param
        is OR'd with the physical Start_Button, so it drives the identical
        ready-to-drive sequence and every interlock (HV / precharge / APPS /
        engine-sync / AIR fail-safe) still applies - it can request start, never
        bypass a safety gate."""
        f = ttk.LabelFrame(self.root, text="Drive")
        f.pack(fill="x", padx=8, pady=4)

        bar = ttk.Frame(f)
        bar.pack(fill="x", padx=4, pady=6)

        # tk.Button (not ttk) so we can colour it like a real start button.
        self.start_btn = tk.Button(
            bar, text="▶  START", command=self.cmd_start,
            background=UI["green"], foreground="#ffffff",
            activebackground=UI["green_ac"], activeforeground="#ffffff",
            disabledforeground="#5b6673", font=("Segoe UI", 13, "bold"),
            padx=28, pady=10, relief="flat", bd=0, cursor="hand2",
            highlightthickness=0)
        self.start_btn.pack(side="left")

        ttk.Label(bar, style="Muted.TLabel",
                  text="  Mirrors the PCB start button (momentary press). All HV / "
                       "precharge / APPS / engine-sync interlocks still apply."
                  ).pack(side="left", padx=8)

    # ---- Controller-state banner (always visible, above the tabs) ----
    def _build_state_banner(self):
        """A big, always-on readout of the Safety_Supervisor state (State_Enum,
        decoded to a name) plus compact lamps for the HV-actuator enables
        (AIR / pre-charge / inverter). Sits above the notebook so it stays visible
        on every tab, and lights up automatically once the board streams these
        signals - no data, no problem, it just says 'waiting for stream'."""
        f = ttk.LabelFrame(self.root, text="Controller state (live)")
        f.pack(fill="x", padx=8, pady=4)

        inner = ttk.Frame(f)
        inner.pack(fill="x", padx=6, pady=6)

        self.state_lbl = tk.Label(inner, text="—  waiting for stream",
                                  anchor="center", font=("Segoe UI", 18, "bold"),
                                  background=UI["elev"], foreground=UI["muted"],
                                  padx=12, pady=12)
        self.state_lbl.pack(side="left", fill="x", expand=True)

        # One green/grey lamp per HV-actuator enable, right of the state name.
        self.enable_lamps = {}       # signal name -> (label widget, short text)
        for sig, label in HV_ENABLE_LAMPS:
            lamp = tk.Label(inner, text=f"{label} —", width=11, anchor="center",
                            font=("Segoe UI", 10, "bold"),
                            background="#39424e", foreground="#ffffff",
                            padx=6, pady=12)
            lamp.pack(side="left", padx=(8, 0))
            self.enable_lamps[sig] = (lamp, label)

    def _update_state_banner(self, raw_value):
        try:
            code = int(float(raw_value))
        except (TypeError, ValueError):
            return
        name, bg, fg = SUPERVISOR_STATES.get(
            code, (f"STATE {code}", UI["purple"], "#ffffff"))
        self.state_lbl.config(text=f"SUPERVISOR:  {name}", background=bg, foreground=fg)

    def _update_enable_lamp(self, sig, raw_value):
        lamp, label = self.enable_lamps[sig]
        try:
            on = int(float(raw_value)) != 0
        except (TypeError, ValueError):
            return
        if on:
            lamp.config(text=f"{label} ON", background=UI["green_hi"])  # energised
        else:
            lamp.config(text=f"{label} off", background="#39424e")      # open

    def _reset_state_banner(self):
        self.state_lbl.config(text="—  waiting for stream",
                              background=UI["elev"], foreground=UI["muted"])
        for lamp, label in self.enable_lamps.values():
            lamp.config(text=f"{label} —", background="#39424e")

    def _build_notebook(self):
        nb = ttk.Notebook(self.root)
        nb.pack(fill="both", expand=True, padx=8, pady=4)
        self.notebook = nb

        self.tab_config = ttk.Frame(nb)
        self.tab_telem = ttk.Frame(nb)
        self.tab_console = ttk.Frame(nb)
        nb.add(self.tab_telem, text="Live Telemetry")
        nb.add(self.tab_config, text="Config")
        nb.add(self.tab_console, text="Console")

        self._build_telem_panel(self.tab_telem)
        self._build_var_panel(self.tab_config)
        self._build_console_panel(self.tab_console)

    # ---- Config (parameters) tab ----
    def _build_var_panel(self, parent):
        f = ttk.LabelFrame(parent, text="Config parameters (auto-discovered from the board)")
        f.pack(fill="both", expand=True, padx=8, pady=6)

        # Scrollable area so the panel copes with any number of parameters.
        canvas = tk.Canvas(f, highlightthickness=0, background=UI["card"])
        vsb = ttk.Scrollbar(f, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True, padx=4, pady=4)

        self.var_grid = ttk.Frame(canvas)
        self._grid_window = canvas.create_window((0, 0), window=self.var_grid, anchor="nw")
        self._var_canvas = canvas

        self.var_grid.bind(
            "<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind(
            "<Configure>", lambda e: canvas.itemconfigure(self._grid_window, width=e.width))
        # Mouse wheel while hovering the list.
        canvas.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", self._on_var_wheel))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))

        for c, h in enumerate(["Parameter", "Current", "New value", "", ""]):
            ttk.Label(self.var_grid, text=h, foreground=UI["muted"],
                      font=FONT_UI_B).grid(
                row=0, column=c, padx=4, pady=(2, 6), sticky="w")

        self.empty_lbl = ttk.Label(
            self.var_grid, style="Muted.TLabel",
            text="(connect, then press 'Refresh all (list)' to discover parameters)")
        self.empty_lbl.grid(row=1, column=0, columnspan=5, padx=4, pady=6, sticky="w")

    def _on_var_wheel(self, event):
        self._var_canvas.yview_scroll(int(-event.delta / 120), "units")

    # ---- Live Telemetry tab ----
    def _build_telem_panel(self, parent):
        # Control row.
        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", padx=8, pady=(8, 2))

        self.telem_btn = ttk.Button(ctl, text="▶ Start stream", style="Accent.TButton",
                                    command=self.toggle_telem)
        self.telem_btn.pack(side="left")

        ttk.Label(ctl, text="Rate:").pack(side="left", padx=(10, 2))
        self.telem_rate_cb = ttk.Combobox(ctl, width=5, state="readonly",
                                          values=TELEM_RATES, textvariable=self.telem_rate_var)
        self.telem_rate_cb.pack(side="left")
        self.telem_rate_cb.bind("<<ComboboxSelected>>", lambda _e: self._on_rate_change())
        ttk.Label(ctl, text="Hz").pack(side="left", padx=(2, 8))

        self.telem_refresh_btn = ttk.Button(ctl, text="Refresh signals",
                                            command=self.fetch_telem_schema)
        self.telem_refresh_btn.pack(side="left", padx=4)

        ttk.Separator(ctl, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Label(ctl, text="Filter:").pack(side="left", padx=(0, 2))
        self.filter_var = tk.StringVar()
        fe = ttk.Entry(ctl, textvariable=self.filter_var, width=18)
        fe.pack(side="left")
        self.filter_var.trace_add("write", lambda *_: self._apply_filter())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self.filter_var.set("")).pack(side="left", padx=(2, 0))

        self.telem_status = ttk.Label(ctl, text="idle", foreground=UI["muted"],
                                      background=UI["card"], font=FONT_MONO)
        self.telem_status.pack(side="right")

        # The live grid: every streamed signal, value updating in place.
        body = ttk.LabelFrame(parent, text="Live values (every signal the board streams)")
        body.pack(fill="both", expand=True, padx=8, pady=(2, 8))

        cols = ("value", "type")
        self.tree = ttk.Treeview(body, columns=cols, show="tree headings",
                                 selectmode="browse")
        self.tree.heading("#0", text="Signal")
        self.tree.heading("value", text="Value")
        self.tree.heading("type", text="Type")
        self.tree.column("#0", width=260, anchor="w", stretch=False)
        self.tree.column("value", width=360, anchor="w")
        self.tree.column("type", width=70, anchor="center", stretch=False)
        self.tree.tag_configure("changed", foreground=UI["accent_hi"])
        self.tree.tag_configure("stale", foreground=UI["muted"])

        tvsb = ttk.Scrollbar(body, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=tvsb.set)
        tvsb.pack(side="right", fill="y")
        self.tree.pack(side="left", fill="both", expand=True, padx=4, pady=4)

        self.telem_empty = ttk.Label(
            parent, style="Muted.TLabel",
            text="(connect; signals auto-discover. Press ▶ Start stream to watch values update.)")
        # shown/removed dynamically
        self._telem_empty_shown = False
        self._show_telem_empty(True)

    def _show_telem_empty(self, show):
        if show and not self._telem_empty_shown:
            self.telem_empty.pack(before=self.tree.master, padx=12, pady=2, anchor="w")
            self._telem_empty_shown = True
        elif not show and self._telem_empty_shown:
            self.telem_empty.pack_forget()
            self._telem_empty_shown = False

    # ---- Console tab ----
    def _build_console_panel(self, parent):
        f = ttk.Frame(parent)
        f.pack(fill="both", expand=True, padx=8, pady=6)

        self.log = scrolledtext.ScrolledText(f, height=12, wrap="word",
                                             font=FONT_MONO, state="disabled",
                                             background="#0b0e13", foreground=UI["fg"],
                                             insertbackground=UI["fg"],
                                             selectbackground=UI["accent"],
                                             selectforeground="#ffffff",
                                             relief="flat", borderwidth=0,
                                             padx=8, pady=6)
        self.log.pack(fill="both", expand=True, padx=4, pady=4)
        self.log.tag_config("tx", foreground=UI["accent_hi"])
        self.log.tag_config("rx", foreground=UI["fg"])
        self.log.tag_config("sys", foreground=UI["muted"])

        row = ttk.Frame(f)
        row.pack(fill="x", padx=4, pady=(0, 4))
        ttk.Label(row, text="Raw:").pack(side="left")
        self.raw_var = tk.StringVar()
        raw = ttk.Entry(row, textvariable=self.raw_var)
        raw.pack(side="left", fill="x", expand=True, padx=4)
        raw.bind("<Return>", lambda _evt: self.send_raw())
        ttk.Button(row, text="Send", command=self.send_raw).pack(side="left")

    # ---------------- settings persistence ----------------
    def _load_settings(self):
        try:
            with open(SETTINGS_PATH, "r", encoding="utf-8") as fh:
                s = json.load(fh)
        except (OSError, ValueError):
            return
        self.auto_reconnect_var.set(bool(s.get("auto_reconnect", True)))
        baud = str(s.get("baud", "115200"))
        if baud in BAUDS:
            self.baud_cb.set(baud)
        rate = str(s.get("telem_rate", DEFAULT_TELEM_RATE))
        if rate in TELEM_RATES:
            self.telem_rate_var.set(rate)
        dev = s.get("last_port")
        if dev:
            self._target_device = dev
            # Preselect it in the combobox if it's currently present.
            for label, device in self._port_map.items():
                if device == dev:
                    self.port_cb.set(label)
                    break

    def _save_settings(self):
        data = {
            "auto_reconnect": bool(self.auto_reconnect_var.get()),
            "baud": self.baud_cb.get(),
            "telem_rate": self.telem_rate_var.get(),
            "last_port": self._target_device,
        }
        try:
            with open(SETTINGS_PATH, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=2)
        except OSError:
            pass

    # ---------------- serial ----------------
    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        labels = [f"{p.device}  ({p.description})" for p in ports]
        self._port_map = {lbl: p.device for lbl, p in zip(labels, ports)}
        self.port_cb["values"] = labels
        if labels and not self.port_cb.get():
            self.port_cb.set(labels[0])

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self._user_disconnected = True
            self.disconnect()
        else:
            self.connect()

    def connect(self, device=None, silent=False):
        if device is None:
            sel = self.port_cb.get()
            if not sel:
                messagebox.showwarning("No port", "Pick a COM port first.")
                return
            device = self._port_map.get(sel, sel)
        try:
            self.ser = serial.Serial(device, int(self.baud_cb.get()), timeout=0.1)
        except serial.SerialException as e:
            if not silent:
                messagebox.showerror("Connection failed", str(e))
            self.ser = None
            return

        self._user_disconnected = False
        self._target_device = device
        self._cancel_reconnect()
        self._save_settings()

        self.reader_stop.clear()
        self.reader = threading.Thread(target=self._read_loop, args=(self.ser,), daemon=True)
        self.reader.start()
        self._set_connected(True)
        self._log(f"connected to {device}\n", "sys")
        self.send_raw_text("")   # blank line -> fresh prompt
        self.rescan_params()     # discover the current parameters
        self.fetch_telem_schema()  # discover the live signals
        self.cmd_get_time()      # show the board clock

    def disconnect(self):
        self.reader_stop.set()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self._telem_streaming = False
        self._update_telem_button()
        self._reset_state_banner()
        self._set_connected(False)
        self._log("disconnected\n", "sys")

    def _handle_serial_error(self):
        """Reader thread reported a dropped link."""
        self._log("serial error — disconnected\n", "sys")
        self.disconnect()
        if self.auto_reconnect_var.get() and not self._user_disconnected:
            self._schedule_reconnect()

    def _schedule_reconnect(self):
        if self._reconnect_after is not None:
            return
        if not (self.auto_reconnect_var.get() and self._target_device):
            return
        self.status_lbl.config(text="● reconnecting…", foreground=UI["amber"])
        self._reconnect_after = self.root.after(RECONNECT_MS, self._try_reconnect)

    def _cancel_reconnect(self):
        if self._reconnect_after is not None:
            self.root.after_cancel(self._reconnect_after)
            self._reconnect_after = None

    def _try_reconnect(self):
        self._reconnect_after = None
        if self.ser and self.ser.is_open:
            return
        if not (self.auto_reconnect_var.get() and self._target_device):
            return
        # Only attempt if the target port is actually present right now.
        present = any(p.device == self._target_device
                      for p in serial.tools.list_ports.comports())
        if present:
            self.connect(device=self._target_device, silent=True)
        if not (self.ser and self.ser.is_open):
            self._schedule_reconnect()   # keep trying

    def _read_loop(self, ser):
        while not self.reader_stop.is_set():
            try:
                data = ser.read(512)
            except Exception:
                if not self.reader_stop.is_set():
                    self.rx_queue.put(("__error__", None))
                break
            if data:
                self.rx_queue.put(("data", data.decode("utf-8", errors="replace")))

    # ---------------- command senders ----------------
    def _send(self, text):
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return
        try:
            self.ser.write((text + "\r\n").encode("utf-8"))
            if text:
                self._log(f"> {text}\n", "tx")
        except Exception as e:
            self._log(f"send error: {e}\n", "sys")
            self._handle_serial_error()

    def cmd_list(self):
        self._send("list")

    def rescan_params(self):
        """Forget the current rows and rebuild them from a fresh `list`. This is
        how a parameter you removed from params.def disappears, and a new one
        appears, with no edit to this script."""
        self._clear_params()
        self.cmd_list()

    def rescan_all(self):
        """Re-discover both parameters and telemetry signals."""
        self.rescan_params()
        self.fetch_telem_schema()

    def cmd_ping(self):
        self.notebook.select(self.tab_console)
        self._send("ping")

    def cmd_stats(self):
        self.notebook.select(self.tab_console)
        self._send("stats")

    def cmd_stats_clear(self):
        self._send("stats clear")
        self.root.after(200, self.cmd_stats)   # show the freshly-zeroed counters

    def cmd_get(self, name):
        self._send(f"get {name}")

    def cmd_set(self, name):
        val = self.params[name]["entry_var"].get().strip()
        if val:
            self._send(f"set {name} {val}")

    def cmd_save(self):
        if messagebox.askyesno("Save to flash",
                               "Write the current values to flash?\n\n"
                               "(Do this stationary / in the pit, not while driving.)"):
            self._send("save")

    def cmd_defaults(self):
        if messagebox.askyesno("Load defaults",
                               "Reset all parameters to built-in defaults?\n\n"
                               "This changes RAM only -- press 'Save to flash' "
                               "afterwards to make it permanent."):
            self._send("defaults")
            self.root.after(200, self.cmd_list)   # refresh the displayed values

    def cmd_get_time(self):
        self._send("time")

    def cmd_set_time_now(self):
        now = datetime.datetime.now()
        self._send(now.strftime("time set %Y-%m-%d %H:%M:%S"))

    # ---- start button ----
    def cmd_start(self):
        """Pulse the GUI start request: drive it high, then auto-release to 0 a
        moment later so it behaves like a momentary press of the physical start
        button and can never stick 'on'."""
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return
        self._send(f"set {START_PARAM} 1")
        self.start_btn.config(background=UI["green_ac"])          # pressed look
        self.root.after(START_PULSE_MS, self._start_release)

    def _start_release(self):
        self._send(f"set {START_PARAM} 0")
        # Only restore the resting colour if the button is still enabled (connected).
        if str(self.start_btn["state"]) != "disabled":
            self.start_btn.config(background=UI["green"])

    # ---- telemetry commands ----
    def fetch_telem_schema(self):
        """Ask the board for its signal schema so the live grid shows every
        signal (even ones that rarely change) before/without streaming."""
        if not (self.ser and self.ser.is_open):
            return
        self._send("telem list")

    def toggle_telem(self):
        if self._telem_streaming:
            self._send("telem off")
        else:
            self._send(f"telem rate {self.telem_rate_var.get()}")
            self._send("telem on")
            self.notebook.select(self.tab_telem)

    def _on_rate_change(self):
        self._save_settings()
        if self._telem_streaming:
            self._send(f"telem rate {self.telem_rate_var.get()}")

    def send_raw(self):
        text = self.raw_var.get().strip()
        self.raw_var.set("")
        self.send_raw_text(text)

    def send_raw_text(self, text):
        self._send(text)

    # ---------------- rx handling ----------------
    def _poll_rx(self):
        try:
            while True:
                kind, payload = self.rx_queue.get_nowait()
                if kind == "__error__":
                    self._handle_serial_error()
                    continue
                self.rx_buffer += payload
                self._drain_lines()
        except queue.Empty:
            pass
        self.root.after(50, self._poll_rx)

    def _drain_lines(self):
        self.rx_buffer = self.rx_buffer.replace("\r\n", "\n").replace("\r", "\n")
        *lines, self.rx_buffer = self.rx_buffer.split("\n")
        for ln in lines:
            ln = ln.strip()
            if ln in ("", ">"):
                continue

            # 1) Live telemetry frame -> update the grid, don't flood the console.
            if ln.startswith("#T"):
                self._handle_telem_frame(ln)
                continue

            # 2) Telemetry schema block ("telem signals:" ... "end").
            if ln == "telem signals:":
                self._schema_collecting = True
                self._begin_schema()
                continue
            if self._schema_collecting:
                if ln == "end":
                    self._schema_collecting = False
                    self._finish_schema()
                    continue
                if self._parse_schema_line(ln):
                    continue
                # not a schema line: fall through to normal handling

            # 3) Everything else: log it and try to interpret it.
            self._log(ln + "\n", "rx")
            self._maybe_update(ln)

    def _maybe_update(self, line):
        m = _TELEM_STATUS_RE.match(line)
        if m:
            self._telem_streaming = (m.group(1).lower() == "on")
            rate = m.group(2)
            if rate in TELEM_RATES:
                self.telem_rate_var.set(rate)
            self._update_telem_button()
            return
        m = _VALUE_RE.match(line)
        if m:
            self._update_param(m.group(1), m.group(2))
            return
        m = _TIME_RE.match(line)
        if m:
            self.clock_var.set(m.group(1))

    # ---------------- telemetry: schema + live frames ----------------
    def _begin_schema(self):
        self._schema_seen = set()

    def _parse_schema_line(self, line):
        m = _SCHEMA_RE.match(line)
        if not m:
            return False
        name, typ, length = m.group(1), m.group(2), int(m.group(3))
        self._schema_seen.add(name)
        self._ensure_telem_row(name, typ, length)
        return True

    def _finish_schema(self):
        # Drop rows that no longer exist on the board (a signal removed from the
        # .def). Only prune when we actually received a non-empty schema.
        if not getattr(self, "_schema_seen", None):
            return
        for name in list(self._telem_order):
            if name not in self._schema_seen:
                self._remove_telem_row(name)
        self._apply_filter()

    def _ensure_telem_row(self, name, typ="?", length=1):
        if name in self._telem_order:
            if typ != "?":
                self.tree.set(name, "type", typ if length <= 1 else f"{typ}[{length}]")
            return
        self._show_telem_empty(False)
        disp_type = typ if length <= 1 else f"{typ}[{length}]"
        self.tree.insert("", "end", iid=name, text=name,
                         values=("—", disp_type), tags=("stale",))
        self._telem_order.append(name)
        self._telem_last[name] = None
        self._apply_filter_one(name)

    def _remove_telem_row(self, name):
        try:
            self.tree.delete(name)
        except tk.TclError:
            pass
        if name in self._telem_order:
            self._telem_order.remove(name)
        self._telem_last.pop(name, None)

    def _handle_telem_frame(self, line):
        # "#T tick=123 User_LED_1=1 APPS=12,0,255,..."
        self._telem_frames += 1
        for tok in line.split()[1:]:
            if "=" not in tok:
                continue
            name, _, val = tok.partition("=")
            if not name:
                continue
            if name not in self._telem_order:
                self._ensure_telem_row(name)     # self-discover if no schema yet
            if self._telem_last.get(name) != val:
                self._telem_last[name] = val
                self.tree.set(name, "value", val)
                self.tree.item(name, tags=("changed",))
                # Mirror the supervisor state + HV enables into the banner.
                # (APPS_Implausibility / BMS_Fault just appear in the grid below.)
                if name == STATE_SIGNAL:
                    self._update_state_banner(val)
                elif name in self.enable_lamps:
                    self._update_enable_lamp(name, val)

    def _telem_tick(self):
        # Once a second: refresh the measured stream rate and fade unchanged rows.
        self._telem_hz = self._telem_frames
        self._telem_frames = 0
        if self._telem_streaming:
            self.telem_status.config(
                text=f"streaming · {self._telem_hz} fps · {len(self._telem_order)} signals",
                foreground=UI["green"])
        elif self._telem_order:
            self.telem_status.config(
                text=f"stopped · {len(self._telem_order)} signals", foreground=UI["muted"])
        else:
            self.telem_status.config(text="idle", foreground=UI["muted"])
        # Fade rows back to neutral so only just-changed values stay highlighted.
        for name in self._telem_order:
            if self.tree.set(name, "value") != "—":
                self.tree.item(name, tags=())
        self.root.after(1000, self._telem_tick)

    def _update_telem_button(self):
        self.telem_btn.config(text="■ Stop stream" if self._telem_streaming
                              else "▶ Start stream")

    def _apply_filter(self):
        flt = self.filter_var.get().strip().lower()
        self._telem_filter = flt
        # Rebuild visible order: matching rows attached in discovery order.
        for name in self._telem_order:
            self._apply_filter_one(name)

    def _apply_filter_one(self, name):
        flt = self._telem_filter
        if not flt or flt in name.lower():
            self.tree.reattach(name, "", "end")
        else:
            self.tree.detach(name)

    # ---------------- parameter rows ----------------
    def _clear_params(self):
        for name in self._param_order:
            for w in self.params[name]["widgets"]:
                w.destroy()
        self.params.clear()
        self._param_order.clear()
        self._param_buttons.clear()
        self.empty_lbl.grid()   # show the hint again until rows arrive

    def _update_param(self, name, value):
        self._ensure_param_row(name)
        self.params[name]["value_var"].set(value)

    def _ensure_param_row(self, name):
        if name in self.params:
            return
        self.empty_lbl.grid_remove()
        row = len(self._param_order) + 1

        lbl = ttk.Label(self.var_grid, text=name)
        lbl.grid(row=row, column=0, padx=4, pady=3, sticky="w")

        cur = tk.StringVar(value="—")
        cur_lbl = ttk.Label(self.var_grid, textvariable=cur, width=12, anchor="w",
                            foreground=UI["accent_hi"], font=FONT_MONO)
        cur_lbl.grid(row=row, column=1, padx=4, sticky="w")

        ev = tk.StringVar()
        entry = ttk.Entry(self.var_grid, textvariable=ev, width=14)
        entry.grid(row=row, column=2, padx=4)
        entry.bind("<Return>", lambda _evt, n=name: self.cmd_set(n))

        sb = ttk.Button(self.var_grid, text="Set", width=6, command=lambda n=name: self.cmd_set(n))
        sb.grid(row=row, column=3, padx=2)
        gb = ttk.Button(self.var_grid, text="Get", width=6, command=lambda n=name: self.cmd_get(n))
        gb.grid(row=row, column=4, padx=2)

        connected = bool(self.ser and self.ser.is_open)
        for b in (sb, gb):
            b.config(state="normal" if connected else "disabled")
            self._param_buttons.append(b)

        self.params[name] = {"value_var": cur, "entry_var": ev,
                             "widgets": [lbl, cur_lbl, entry, sb, gb]}
        self._param_order.append(name)

    # ---------------- misc ----------------
    def _set_connected(self, on):
        self.connect_btn.config(text="Disconnect" if on else "Connect")
        self.status_lbl.config(
            text="● connected" if on else "● disconnected",
            foreground=UI["green"] if on else UI["red_hi"])
        state = "normal" if on else "disabled"
        for b in (self.start_btn,
                  self.refresh_all_btn, self.ping_btn, self.stats_btn,
                  self.clearstats_btn, self.save_btn, self.defaults_btn,
                  self.settime_btn, self.gettime_btn,
                  self.telem_btn, self.telem_refresh_btn):
            b.config(state=state)
        for b in self._param_buttons:
            b.config(state=state)
        if not on:
            self._update_telem_button()

    def _log(self, text, tag="rx"):
        self.log.config(state="normal")
        self.log.insert("end", text, tag)
        self.log.see("end")
        self.log.config(state="disabled")

    def on_close(self):
        self._user_disconnected = True
        self._cancel_reconnect()
        # be polite: stop the stream so the board isn't left chattering
        if self.ser and self.ser.is_open and self._telem_streaming:
            try:
                self.ser.write(b"telem off\r\n")
            except Exception:
                pass
        self.disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    ConsoleApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
