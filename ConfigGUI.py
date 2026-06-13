#!/usr/bin/env python3
"""
HCU V2 Config Console
=====================
A friendly GUI front-end for the USB-CDC command console on the Formula
Student hybrid controller. It speaks the *exact* same text protocol you
already use in PuTTY, so the firmware needs no changes:

    list                 -> dump all variables
    get <name>           -> read one
    set <name> <value>   -> write one   (ints accept 0x.. hex too)
    ping                 -> pong

Run it:
    pip install pyserial
    python hcu_console.py

(tkinter ships with Python. On Debian/Ubuntu: sudo apt install python3-tk)

NOTE: only one program can own a COM port at a time — close PuTTY before
connecting here, and vice-versa.

To add a config variable later, add one line to VARS below to match the
table in console.c. That's the only change needed.
"""

import queue
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

import serial
import serial.tools.list_ports

# Mirror of the firmware's config table (console.c).
# kind is only a hint for the input field; the board does the real parsing.
VARS = [
    ("torque_limit", "int"),
    ("kp",           "float"),
    ("ki",           "float"),
    ("log_rate",     "int"),
    ("can_id",       "int"),
]

BAUDS = ["9600", "19200", "38400", "57600", "115200"]  # cosmetic for CDC


class ConsoleApp:
    def __init__(self, root):
        self.root = root
        self.ser = None
        self.reader = None
        self.reader_stop = threading.Event()
        self.rx_queue = queue.Queue()
        self.rx_buffer = ""
        self.value_vars = {}   # name -> StringVar for the "current" label
        self.entry_vars = {}   # name -> StringVar for the "new value" entry
        self._port_map = {}

        root.title("HCU V2  —  Config Console")
        root.minsize(660, 580)
        try:
            ttk.Style().theme_use("clam")
        except tk.TclError:
            pass

        self._build_connection_bar()
        self._build_var_panel()
        self._build_console_panel()
        self._set_connected(False)

        self.refresh_ports()
        self.root.after(50, self._poll_rx)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

    # ---------------- UI construction ----------------
    def _build_connection_bar(self):
        f = ttk.LabelFrame(self.root, text="Connection")
        f.pack(fill="x", padx=8, pady=(8, 4))

        ttk.Label(f, text="Port:").grid(row=0, column=0, padx=4, pady=6, sticky="w")
        self.port_cb = ttk.Combobox(f, width=26, state="readonly")
        self.port_cb.grid(row=0, column=1, padx=4, pady=6)
        ttk.Button(f, text="\u21bb", width=3, command=self.refresh_ports).grid(row=0, column=2, padx=2)

        ttk.Label(f, text="Baud:").grid(row=0, column=3, padx=4)
        self.baud_cb = ttk.Combobox(f, width=8, state="readonly", values=BAUDS)
        self.baud_cb.set("115200")
        self.baud_cb.grid(row=0, column=4, padx=4)

        self.connect_btn = ttk.Button(f, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, padx=6)

        self.status_lbl = ttk.Label(f, text="\u25cf disconnected", foreground="#c0392b")
        self.status_lbl.grid(row=0, column=6, padx=8)

    def _build_var_panel(self):
        f = ttk.LabelFrame(self.root, text="Config variables")
        f.pack(fill="x", padx=8, pady=4)

        bar = ttk.Frame(f)
        bar.pack(fill="x", padx=4, pady=4)
        self.refresh_all_btn = ttk.Button(bar, text="Refresh all (list)", command=self.cmd_list)
        self.refresh_all_btn.pack(side="left")
        self.ping_btn = ttk.Button(bar, text="Ping", command=self.cmd_ping)
        self.ping_btn.pack(side="left", padx=6)

        grid = ttk.Frame(f)
        grid.pack(fill="x", padx=4, pady=4)
        for c, h in enumerate(["Variable", "Current", "New value", "", ""]):
            ttk.Label(grid, text=h, font=("TkDefaultFont", 9, "bold")).grid(
                row=0, column=c, padx=4, pady=2, sticky="w")

        self.set_buttons, self.get_buttons = [], []
        for i, (name, _kind) in enumerate(VARS, start=1):
            ttk.Label(grid, text=name).grid(row=i, column=0, padx=4, pady=3, sticky="w")

            cur = tk.StringVar(value="\u2014")
            self.value_vars[name] = cur
            ttk.Label(grid, textvariable=cur, width=12, anchor="w",
                      foreground="#1f6aa5").grid(row=i, column=1, padx=4, sticky="w")

            ev = tk.StringVar()
            self.entry_vars[name] = ev
            e = ttk.Entry(grid, textvariable=ev, width=14)
            e.grid(row=i, column=2, padx=4)
            e.bind("<Return>", lambda _evt, n=name: self.cmd_set(n))

            sb = ttk.Button(grid, text="Set", width=6, command=lambda n=name: self.cmd_set(n))
            sb.grid(row=i, column=3, padx=2)
            gb = ttk.Button(grid, text="Get", width=6, command=lambda n=name: self.cmd_get(n))
            gb.grid(row=i, column=4, padx=2)
            self.set_buttons.append(sb)
            self.get_buttons.append(gb)

    def _build_console_panel(self):
        f = ttk.LabelFrame(self.root, text="Console")
        f.pack(fill="both", expand=True, padx=8, pady=(4, 8))

        self.log = scrolledtext.ScrolledText(f, height=12, wrap="word",
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
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        sel = self.port_cb.get()
        if not sel:
            messagebox.showwarning("No port", "Pick a COM port first.")
            return
        device = self._port_map.get(sel, sel)
        try:
            self.ser = serial.Serial(device, int(self.baud_cb.get()), timeout=0.1)
        except serial.SerialException as e:
            messagebox.showerror("Connection failed", str(e))
            self.ser = None
            return
        self.reader_stop.clear()
        self.reader = threading.Thread(target=self._read_loop, args=(self.ser,), daemon=True)
        self.reader.start()
        self._set_connected(True)
        self._log(f"connected to {device}\n", "sys")
        self.send_raw_text("")   # blank line -> fresh prompt
        self.cmd_list()          # pull current values

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
            self.disconnect()

    def cmd_list(self):
        self._send("list")

    def cmd_ping(self):
        self._send("ping")

    def cmd_get(self, name):
        self._send(f"get {name}")

    def cmd_set(self, name):
        val = self.entry_vars[name].get().strip()
        if val:
            self._send(f"set {name} {val}")

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
                    self._log("serial error \u2014 disconnected\n", "sys")
                    self.disconnect()
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
            self._maybe_update_var(ln)

    def _maybe_update_var(self, line):
        s = line
        if s.lower().startswith("ok:"):
            s = s[3:].strip()
        if "=" in s:
            left, _, right = s.partition("=")
            name = left.strip()
            if name in self.value_vars:
                self.value_vars[name].set(right.strip())

    # ---------------- misc ----------------
    def _set_connected(self, on):
        self.connect_btn.config(text="Disconnect" if on else "Connect")
        self.status_lbl.config(
            text="\u25cf connected" if on else "\u25cf disconnected",
            foreground="#27ae60" if on else "#c0392b")
        state = "normal" if on else "disabled"
        for b in self.set_buttons + self.get_buttons:
            b.config(state=state)
        self.refresh_all_btn.config(state=state)
        self.ping_btn.config(state=state)

    def _log(self, text, tag="rx"):
        self.log.config(state="normal")
        self.log.insert("end", text, tag)
        self.log.see("end")
        self.log.config(state="disabled")

    def on_close(self):
        self.disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    ConsoleApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()