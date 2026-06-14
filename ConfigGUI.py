#!/usr/bin/env python3
"""
HCU V2 Config Console
=====================
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
    ping                          -> pong

Run it:
    pip install pyserial
    python ConfigGUI.py

(tkinter ships with Python. On Debian/Ubuntu: sudo apt install python3-tk)

NOTE: only one program can own a COM port at a time -- close PuTTY before
connecting here, and vice-versa.

>>> NOTHING TO MAINTAIN HERE WHEN YOU ADD A PARAMETER. <<<
The list of tunables is discovered live from the board (it parses the `list`
output), so any parameter you add in CM7/Core/Inc/params.def shows up here
automatically -- no edit to this file is ever needed.
"""

import os
import re
import json
import queue
import threading
import datetime
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

import serial
import serial.tools.list_ports

BAUDS = ["9600", "19200", "38400", "57600", "115200"]  # cosmetic for a CDC port

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

        self._port_map = {}
        self._target_device = None       # device we (try to) stay connected to
        self._reconnect_after = None     # pending root.after id for reconnect
        self._user_disconnected = False  # True after an explicit Disconnect

        root.title("HCU V2  --  Config Console")
        root.minsize(720, 640)
        try:
            ttk.Style().theme_use("clam")
        except tk.TclError:
            pass

        self.auto_reconnect_var = tk.BooleanVar(value=True)

        self._build_connection_bar()
        self._build_actions_bar()
        self._build_var_panel()
        self._build_console_panel()
        self._set_connected(False)

        self.refresh_ports()
        self._load_settings()
        self.root.after(50, self._poll_rx)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        # If we remembered a port and auto-reconnect is on, connect on startup.
        if self.auto_reconnect_var.get() and self._target_device:
            self.root.after(200, self._try_reconnect)

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

        self.connect_btn = ttk.Button(f, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, padx=6)

        ttk.Checkbutton(f, text="Auto-reconnect", variable=self.auto_reconnect_var,
                        command=self._save_settings).grid(row=0, column=6, padx=6)

        self.status_lbl = ttk.Label(f, text="● disconnected", foreground="#c0392b")
        self.status_lbl.grid(row=0, column=7, padx=8)

    def _build_actions_bar(self):
        f = ttk.LabelFrame(self.root, text="Board")
        f.pack(fill="x", padx=8, pady=4)

        bar = ttk.Frame(f)
        bar.pack(fill="x", padx=4, pady=6)

        self.refresh_all_btn = ttk.Button(bar, text="Refresh all (list)", command=self.rescan_params)
        self.refresh_all_btn.pack(side="left")
        self.ping_btn = ttk.Button(bar, text="Ping", command=self.cmd_ping)
        self.ping_btn.pack(side="left", padx=6)

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)

        # Health readout. Output lands in the Console box below (multi-line).
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
        ttk.Label(bar, text="Board clock:").pack(side="left", padx=(8, 2))
        ttk.Label(bar, textvariable=self.clock_var, foreground="#1f6aa5",
                  font=("Consolas", 10)).pack(side="left")

    def _build_var_panel(self):
        f = ttk.LabelFrame(self.root, text="Config parameters (auto-discovered from the board)")
        f.pack(fill="both", expand=True, padx=8, pady=4)

        # Scrollable area so the panel copes with any number of parameters.
        canvas = tk.Canvas(f, highlightthickness=0, height=150)
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
        canvas.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", self._on_mousewheel))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))

        for c, h in enumerate(["Parameter", "Current", "New value", "", ""]):
            ttk.Label(self.var_grid, text=h, font=("TkDefaultFont", 9, "bold")).grid(
                row=0, column=c, padx=4, pady=2, sticky="w")

        self.empty_lbl = ttk.Label(
            self.var_grid, foreground="#888888",
            text="(connect, then press 'Refresh all (list)' to discover parameters)")
        self.empty_lbl.grid(row=1, column=0, columnspan=5, padx=4, pady=6, sticky="w")

    def _on_mousewheel(self, event):
        self._var_canvas.yview_scroll(int(-event.delta / 120), "units")

    def _build_console_panel(self):
        f = ttk.LabelFrame(self.root, text="Console")
        f.pack(fill="both", expand=True, padx=8, pady=(4, 8))

        self.log = scrolledtext.ScrolledText(f, height=10, wrap="word",
                                             font=("Consolas", 10), state="disabled",
                                             background="#1e1e1e", foreground="#d4d4d4",
                                             insertbackground="#d4d4d4")
        self.log.pack(fill="both", expand=True, padx=4, pady=4)
        self.log.tag_config("tx", foreground="#4fc1ff")
        self.log.tag_config("rx", foreground="#d4d4d4")
        self.log.tag_config("sys", foreground="#888888")

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
        self.cmd_get_time()      # show the board clock

    def disconnect(self):
        self.reader_stop.set()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
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
        self.status_lbl.config(text="● reconnecting…", foreground="#d68910")
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
                data = ser.read(256)
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

    def cmd_ping(self):
        self._send("ping")

    def cmd_stats(self):
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
            self._log(ln + "\n", "rx")
            self._maybe_update(ln)

    def _maybe_update(self, line):
        m = _VALUE_RE.match(line)
        if m:
            self._update_param(m.group(1), m.group(2))
            return
        m = _TIME_RE.match(line)
        if m:
            self.clock_var.set(m.group(1))

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
                            foreground="#1f6aa5")
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
            foreground="#27ae60" if on else "#c0392b")
        state = "normal" if on else "disabled"
        for b in (self.refresh_all_btn, self.ping_btn, self.stats_btn,
                  self.clearstats_btn, self.save_btn, self.defaults_btn,
                  self.settime_btn, self.gettime_btn):
            b.config(state=state)
        for b in self._param_buttons:
            b.config(state=state)

    def _log(self, text, tag="rx"):
        self.log.config(state="normal")
        self.log.insert("end", text, tag)
        self.log.see("end")
        self.log.config(state="disabled")

    def on_close(self):
        self._user_disconnected = True
        self._cancel_reconnect()
        self.disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    ConsoleApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
