"""
configgui.serial_io  --  the serial link + the RX line dispatcher.

SerialMixin owns everything between the USB-CDC port and the rest of the app:
opening / closing / auto-reconnecting the port, the background reader thread, the
main-thread RX pump, and the line dispatcher that routes each received line to the
right tab (`#T` telemetry, `#C` sniffer, `# section:` param headers, the schema
block, or the console). It also loads / saves the remembered port + preferences.

It is a mixin: ConsoleApp inherits it, so `self` is the whole app and the cross-tab
calls below (`_handle_telem_frame`, `_handle_sniffer_frame`, ...) resolve to the
other mixins.
"""

import json
import queue
import threading

import serial
import serial.tools.list_ports
from tkinter import messagebox

from .theme import UI
from .protocol import (
    BAUDS, TELEM_RATES, SNIFF_RATES, DEFAULT_TELEM_RATE, SETTINGS_PATH,
    RECONNECT_MS, _PARAM_SECTION_RE, _TELEM_STATUS_RE, _SNIFF_STATUS_RE,
    _VALUE_RE, _TIME_RE,
)


class SerialMixin:
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
        # Plot tab preferences (applied by the Plot tab as signals are discovered).
        self._plot_saved_signals = list(s.get("plot_signals", []))
        win = s.get("plot_window")
        if win and hasattr(self, "plot_window_var"):
            self.plot_window_var.set(str(win))
        if hasattr(self, "plot_norm_var"):
            self.plot_norm_var.set(bool(s.get("plot_normalise", False)))
        if hasattr(self, "_apply_saved_plot_signals"):
            self._apply_saved_plot_signals()
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
        # Plot tab preferences (guarded so a very early save still works).
        if hasattr(self, "_plot_selected"):
            data["plot_signals"] = sorted(self._plot_selected)
        if hasattr(self, "plot_window_var"):
            data["plot_window"] = self.plot_window_var.get()
        if hasattr(self, "plot_norm_var"):
            data["plot_normalise"] = bool(self.plot_norm_var.get())
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
        self._sniff_streaming = False
        self._update_telem_button()
        self._update_sniffer_button()
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

            # 1b) CAN-sniffer row -> update the bus grid, don't flood the console.
            if ln.startswith("#C"):
                self._handle_sniffer_frame(ln)
                continue

            # 1c) Parameter-section header from `list` -> file the parameters that
            # follow under this heading (params.def's own grouping, no edit here).
            m = _PARAM_SECTION_RE.match(ln)
            if m:
                self._current_param_section = self._register_param_section(m.group(1))
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
        m = _SNIFF_STATUS_RE.match(line)
        if m:
            self._sniff_streaming = (m.group(1).lower() == "on")
            rate = m.group(2)
            if rate in SNIFF_RATES:
                self.sniff_rate_var.set(rate)
            self._update_sniffer_button()
            return
        m = _VALUE_RE.match(line)
        if m:
            self._update_param(m.group(1), m.group(2))
            return
        m = _TIME_RE.match(line)
        if m:
            self.clock_var.set(m.group(1))
