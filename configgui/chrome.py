"""
configgui.chrome  --  the top chrome + the global board actions.

ChromeMixin builds the always-on frame around the tabs (title band, connection
bar, board-action bar, and the merged Drive + controller-state banner), assembles
the notebook, and holds the board-wide command buttons (list / ping / stats /
save / defaults / clock) plus the momentary START and CLEAR-FAULT controls. The
per-tab bodies live in their own mixins; this file only wires the shell together.
"""

import datetime

import tkinter as tk
from tkinter import ttk, messagebox

from .theme import UI, FONT_UI_SM, FONT_UI_B, FONT_MONO
from .protocol import (
    BAUDS, START_PARAM, START_PULSE_MS, RESET_PARAM, RESET_PULSE_MS,
    HV_ENABLE_LAMPS, SUPERVISOR_STATES, FAULT_CODES,
    DRIVE_MODE_SIGNAL, DRIVE_MODES,
)


class ChromeMixin:
    # ---------------- header / bars ----------------
    def _build_header(self):
        """A slim title band so the tool opens with an identity instead of a bare
        row of buttons. Cosmetic only."""
        band = ttk.Frame(self.root, style="Header.TFrame")
        band.pack(fill="x")
        inner = ttk.Frame(band, style="Header.TFrame")
        inner.pack(fill="x", padx=16, pady=(4, 3))

        title = ttk.Frame(inner, style="Header.TFrame")
        title.pack(side="left")
        ttk.Label(title, text="◆ HCU", style="H1Accent.TLabel").pack(side="left")
        ttk.Label(title, text=" V2", style="H1.TLabel").pack(side="left")
        ttk.Label(title, text="   CONSOLE", style="H2.TLabel").pack(
            side="left", pady=(4, 0))

        ttk.Label(inner,
                  text="Formula Student · Hybrid Control Unit · trackside console",
                  style="H2.TLabel").pack(side="right", pady=(4, 0))

        tk.Frame(self.root, height=2, background=UI["accent"]).pack(fill="x")

    def _build_connection_bar(self):
        f = ttk.LabelFrame(self.root, text="Connection")
        f.pack(fill="x", padx=8, pady=(4, 2))

        ttk.Label(f, text="Port:").grid(row=0, column=0, padx=4, pady=3, sticky="w")
        self.port_cb = ttk.Combobox(f, width=26, state="readonly")
        self.port_cb.grid(row=0, column=1, padx=4, pady=3)
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
        f.pack(fill="x", padx=8, pady=2)

        bar = ttk.Frame(f)
        bar.pack(fill="x", padx=4, pady=2)

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

    # ---- Drive control + controller-state banner (one compact row) ----
    def _build_drive_state_bar(self):
        """The START button, the live Safety_Supervisor state, and the HV-actuator
        lamps, all on ONE line. Merging Drive + Controller-state keeps the top
        chrome short so the telemetry / config lists below get the vertical space
        (this is the trackside view - you want to see many rows at once).

        START PULSES the Start_Button_GUI parameter (1, then back to 0) so the
        board sees a momentary press; in Simulink it is OR'd with the physical
        Start_Button, so every interlock (HV / precharge / APPS / engine-sync /
        AIR fail-safe) still applies - it can request start, never bypass a gate.
        The state readout + lamps light up once the board streams the signals."""
        f = ttk.LabelFrame(self.root, text="Drive  ·  controller state (live)")
        f.pack(fill="x", padx=8, pady=2)

        bar = ttk.Frame(f)
        bar.pack(fill="x", padx=4, pady=3)

        # tk.Button (not ttk) so we can colour it like a real start button.
        self.start_btn = tk.Button(
            bar, text="▶ START", command=self.cmd_start,
            background=UI["green"], foreground="#ffffff",
            activebackground=UI["green_ac"], activeforeground="#ffffff",
            disabledforeground="#5b6673", font=("Segoe UI", 10, "bold"),
            padx=12, pady=4, relief="flat", bd=0, cursor="hand2",
            highlightthickness=0)
        self.start_btn.pack(side="left")

        # CLEAR FAULT: amber, momentary - pulses Error_Reset_GUI AND sends `safety
        # reset`, so one press acknowledges a latched supervisor ERROR (chart leaves
        # Error_State) and clears the independent AIR fail-safe stall latch. The chart
        # still requires the fault to be gone and lands in HV_OFF, so this can never
        # jump the car into DRIVE - it only saves you a power-cycle.
        self.reset_btn = tk.Button(
            bar, text="⟲ CLEAR FAULT", command=self.cmd_clear_fault,
            background=UI["amber"], foreground="#1a1206",
            activebackground="#b45309", activeforeground="#ffffff",
            disabledforeground="#5b6673", font=("Segoe UI", 10, "bold"),
            padx=10, pady=4, relief="flat", bd=0, cursor="hand2",
            highlightthickness=0)
        self.reset_btn.pack(side="left", padx=(6, 0))

        # Small, muted reference button: opens a popup listing the State_Enum and
        # Fault_Code legends so an operator can read what a banner code means. It is a
        # static reference, so it stays enabled even when disconnected (not in the
        # connect/disconnect list below).
        self.faults_btn = tk.Button(
            bar, text="ⓘ codes", command=self._show_fault_codes,
            background=UI["elev"], foreground=UI["muted"],
            activebackground=UI["border"], activeforeground=UI["fg"],
            font=FONT_UI_SM, padx=8, pady=4, relief="flat", bd=0,
            cursor="hand2", highlightthickness=0)
        self.faults_btn.pack(side="left", padx=(6, 0))

        # Remembered so the banner can show the state name AND the fault reason
        # together: State_Enum and Fault_Code arrive as separate telemetry fields.
        self._last_state_code = None
        self._last_fault_code = 0

        # The live state name fills the middle, so it stays big and obvious.
        self.state_lbl = tk.Label(bar, text="—  waiting for stream",
                                  anchor="center", font=("Segoe UI", 12, "bold"),
                                  background=UI["elev"], foreground=UI["muted"],
                                  padx=10, pady=5)
        self.state_lbl.pack(side="left", fill="x", expand=True, padx=8)

        # One green/grey lamp per HV-actuator enable, right of the state name.
        self.enable_lamps = {}       # signal name -> (label widget, short text)
        for sig, label in HV_ENABLE_LAMPS:
            lamp = tk.Label(bar, text=f"{label} —", width=10, anchor="center",
                            font=("Segoe UI", 9, "bold"),
                            background="#39424e", foreground="#ffffff",
                            padx=4, pady=5)
            lamp.pack(side="left", padx=(6, 0))
            self.enable_lamps[sig] = (lamp, label)

        # Control-mode chip: TORQUE (race, green) vs VELOCITY (bench spin, amber).
        # Driven by Velocity_Mode_Active - always visible so the wheels-off velocity
        # mode can never be mistaken for the race torque mode at a glance.
        self.mode_chip = tk.Label(bar, text="MODE —", width=10, anchor="center",
                                  font=("Segoe UI", 9, "bold"),
                                  background="#39424e", foreground="#ffffff",
                                  padx=4, pady=5)
        self.mode_chip.pack(side="left", padx=(6, 0))

    def _update_mode_chip(self, raw_value):
        try:
            code = int(float(raw_value))
        except (TypeError, ValueError):
            return
        text, bg, fg = DRIVE_MODES.get(
            code, (f"MODE {code}", UI["purple"], "#ffffff"))
        self.mode_chip.config(text=text, background=bg, foreground=fg)

    def _update_state_banner(self, raw_value):
        try:
            self._last_state_code = int(float(raw_value))
        except (TypeError, ValueError):
            return
        self._refresh_state_banner()

    def _update_fault_code(self, raw_value):
        try:
            self._last_fault_code = int(float(raw_value))
        except (TypeError, ValueError):
            return
        self._refresh_state_banner()

    def _refresh_state_banner(self):
        """Render the state name, and - only in ERROR - append the decoded reason,
        e.g. 'SUPERVISOR:  ERROR / FAULT  -  BMS zero-limit (DCL & CCL = 0)'."""
        code = self._last_state_code
        if code is None:
            return
        name, bg, fg = SUPERVISOR_STATES.get(
            code, (f"STATE {code}", UI["purple"], "#ffffff"))
        text = f"SUPERVISOR:  {name}"
        if code == 6 and self._last_fault_code:      # ERROR: show WHY, not just THAT
            reason = FAULT_CODES.get(self._last_fault_code,
                                     f"code {self._last_fault_code}")
            text = f"SUPERVISOR:  {name}  -  {reason}"
        self.state_lbl.config(text=text, background=bg, foreground=fg)

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
        self._last_state_code = None
        self._last_fault_code = 0
        self.state_lbl.config(text="—  waiting for stream",
                              background=UI["elev"], foreground=UI["muted"])
        for lamp, label in self.enable_lamps.values():
            lamp.config(text=f"{label} —", background="#39424e")
        self.mode_chip.config(text="MODE —", background="#39424e",
                              foreground="#ffffff")

    # ---------------- notebook assembly ----------------
    def _build_notebook(self):
        nb = ttk.Notebook(self.root)
        nb.pack(fill="both", expand=True, padx=8, pady=(2, 4))
        self.notebook = nb

        self.tab_config = ttk.Frame(nb)
        self.tab_telem = ttk.Frame(nb)
        self.tab_plot = ttk.Frame(nb)
        self.tab_sniffer = ttk.Frame(nb)
        self.tab_console = ttk.Frame(nb)
        nb.add(self.tab_telem, text="Live Telemetry")
        nb.add(self.tab_plot, text="Plot")
        nb.add(self.tab_sniffer, text="CAN Bus")
        nb.add(self.tab_config, text="Config")
        nb.add(self.tab_console, text="Console")

        self._build_telem_panel(self.tab_telem)
        self._build_plot_panel(self.tab_plot)
        self._build_sniffer_panel(self.tab_sniffer)
        self._build_var_panel(self.tab_config)
        self._build_console_panel(self.tab_console)

    def _build_console_panel(self, parent):
        from tkinter import scrolledtext
        f = ttk.Frame(parent)
        f.pack(fill="both", expand=True, padx=8, pady=4)

        self.log = scrolledtext.ScrolledText(f, height=10, wrap="word",
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
        raw.pack(side="left", fill="x", expand=True, padx=6)
        raw.bind("<Return>", lambda _e: self.send_raw())
        ttk.Button(row, text="Send", command=self.send_raw).pack(side="left")

    # ---------------- board command senders ----------------
    def cmd_list(self):
        self._send("list")

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

    # ---- clear-fault button ----
    def cmd_clear_fault(self):
        """Acknowledge a latched fault without power-cycling. Clears BOTH latches:
        the supervisor's Error_State (via the Error_Reset_GUI pulse -> Reset_Req) and
        the independent AIR fail-safe stall latch (via `safety reset`). Each ignores
        the request if it wasn't the one latched, and the chart still refuses to leave
        ERROR until the underlying fault has cleared - so this can only ever drop the
        car to HV_OFF, never re-arm it."""
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return
        self._send("safety reset")                # clear the freeze/stall latch (if any)
        self._send(f"set {RESET_PARAM} 1")        # request the model leave Error_State
        self.reset_btn.config(background="#b45309")               # pressed look
        self.root.after(RESET_PULSE_MS, self._clear_fault_release)

    def _clear_fault_release(self):
        self._send(f"set {RESET_PARAM} 0")
        if str(self.reset_btn["state"]) != "disabled":
            self.reset_btn.config(background=UI["amber"])

    def _show_fault_codes(self):
        """Popup reference: the supervisor State_Enum and Fault_Code legends, so an
        operator can look up what a code on the live banner means. Static reference -
        works offline. Both tables come straight from SUPERVISOR_STATES / FAULT_CODES,
        so they can never drift from what the banner decodes."""
        if getattr(self, "_codes_win", None) and self._codes_win.winfo_exists():
            self._codes_win.lift()                 # already open - just raise it
            return
        win = tk.Toplevel(self.root)
        self._codes_win = win
        win.title("Supervisor states & fault codes")
        win.configure(background=UI["card"])
        win.resizable(False, False)

        def section(title, items):
            lf = tk.LabelFrame(win, text=title, background=UI["card"],
                               foreground=UI["fg"], font=FONT_UI_B,
                               bd=1, relief="groove", padx=8, pady=4)
            lf.pack(fill="x", padx=10, pady=(10, 0))
            for row, (code, text) in enumerate(items):
                tk.Label(lf, text=str(code), width=4, anchor="e", font=FONT_MONO,
                         background=UI["card"], foreground=UI["accent_hi"]).grid(
                             row=row, column=0, sticky="e", padx=(0, 10), pady=1)
                tk.Label(lf, text=text, anchor="w", font=("Segoe UI", 9),
                         background=UI["card"], foreground=UI["fg"]).grid(
                             row=row, column=1, sticky="w", pady=1)

        section("Controller states  ·  State_Enum",
                [(c, SUPERVISOR_STATES[c][0]) for c in sorted(SUPERVISOR_STATES)])
        section("Fault reasons  ·  Fault_Code  (appended to the ERROR banner)",
                [(c, FAULT_CODES[c]) for c in sorted(FAULT_CODES)])

        tk.Button(win, text="Close", command=win.destroy,
                  background=UI["elev"], foreground=UI["fg"], relief="flat",
                  activebackground=UI["border"], activeforeground=UI["fg"],
                  padx=14, pady=4, cursor="hand2", highlightthickness=0).pack(pady=10)

    # ---------------- connection state + logging ----------------
    def _set_connected(self, on):
        self.connect_btn.config(text="Disconnect" if on else "Connect")
        self.status_lbl.config(
            text="● connected" if on else "● disconnected",
            foreground=UI["green"] if on else UI["red_hi"])
        state = "normal" if on else "disabled"
        for b in (self.start_btn, self.reset_btn,
                  self.refresh_all_btn, self.ping_btn, self.stats_btn,
                  self.clearstats_btn, self.save_btn, self.defaults_btn,
                  self.settime_btn, self.gettime_btn,
                  self.telem_btn, self.telem_refresh_btn,
                  self.sniff_btn, self.sniff_clear_btn, self.sniff_refresh_btn):
            b.config(state=state)
        # Plot-tab buttons only exist when matplotlib is installed.
        for name in ("plot_btn", "plot_pause_btn", "plot_clear_btn"):
            b = getattr(self, name, None)
            if b is not None:
                b.config(state=state)
        for b in self._param_buttons:
            b.config(state=state)
        if not on:
            self._update_telem_button()
            self._update_sniffer_button()

    def _log(self, text, tag="rx"):
        self.log.config(state="normal")
        self.log.insert("end", text, tag)
        self.log.see("end")
        self.log.config(state="disabled")

    def on_close(self):
        self._user_disconnected = True
        self._cancel_reconnect()
        # be polite: stop the streams so the board isn't left chattering
        if self.ser and self.ser.is_open:
            try:
                if self._telem_streaming:
                    self.ser.write(b"telem off\r\n")
                if self._sniff_streaming:
                    self.ser.write(b"cansniff off\r\n")
            except Exception:
                pass
        self.disconnect()
        self.root.destroy()
