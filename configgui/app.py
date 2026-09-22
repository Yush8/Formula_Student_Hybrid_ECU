"""
configgui.app  --  ConsoleApp: the whole GUI, assembled from the tab mixins.

This is the composition root. ConsoleApp inherits one mixin per concern (serial
link, top chrome + board actions, and the Config / Telemetry / Plot / CAN-Bus
tabs); __init__ sets up the shared state and wires the build order. The behaviour
lives in the mixins - keep this file about assembly, not logic.
"""

import queue
import threading

import tkinter as tk

from .theme import apply_theme
from .protocol import DEFAULT_TELEM_RATE, DEFAULT_SNIFF_RATE
from .serial_io import SerialMixin
from .chrome import ChromeMixin
from .params_tab import ParamsMixin
from .telem_tab import TelemMixin
from .sniffer_tab import SnifferMixin
from .plot_tab import PlotMixin


class ConsoleApp(SerialMixin, ChromeMixin, ParamsMixin, TelemMixin,
                 SnifferMixin, PlotMixin):
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
        self._param_sections = {}     # group label -> dict(frame, grid, count)
        self._param_section_order = []  # section labels in board-declared order
        self._current_param_section = None  # section being filled during a `list`

        # ---- live telemetry state ----
        self._telem_order = []        # signal names in discovery order
        self._telem_last = {}         # name -> last displayed value (skip no-ops)
        self._telem_groups = {}       # group label -> parent row iid in the tree
        self._telem_members = {}      # group label -> [signal names in that group]
        self._telem_streaming = False
        self._schema_collecting = False
        self._telem_frames = 0        # frames since the last Hz sample
        self._telem_hz = 0.0
        self._telem_filter = ""

        # ---- CAN sniffer state ----
        self._sniff_order = []        # row keys ("bus:IDHEX") in discovery order
        self._sniff_meta = {}         # key -> {count, t, rate} for rate calc
        self._sniff_data = {}         # key -> last data string (change highlight)
        self._sniff_streaming = False
        self._sniff_frames = 0        # rows received since the last sample
        self._sniff_hz = 0
        self._sniff_filter = ""

        self._port_map = {}
        self._target_device = None       # device we (try to) stay connected to
        self._reconnect_after = None     # pending root.after id for reconnect
        self._user_disconnected = False  # True after an explicit Disconnect

        root.title("HCU V2  ·  Console")
        root.minsize(880, 540)
        root.geometry("1180x780")     # comfortable first open (the Plot tab wants width)
        apply_theme(root)

        self.auto_reconnect_var = tk.BooleanVar(value=True)
        self.telem_rate_var = tk.StringVar(value=DEFAULT_TELEM_RATE)
        self.sniff_rate_var = tk.StringVar(value=DEFAULT_SNIFF_RATE)

        self._build_header()
        self._build_connection_bar()
        self._build_actions_bar()
        self._build_drive_state_bar()
        self._build_notebook()
        self._set_connected(False)

        self.refresh_ports()
        self._load_settings()
        self.root.after(50, self._poll_rx)
        self.root.after(1000, self._telem_tick)
        self.root.after(1000, self._sniff_tick)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        # If we remembered a port and auto-reconnect is on, connect on startup.
        if self.auto_reconnect_var.get() and self._target_device:
            self.root.after(200, self._try_reconnect)


def main():
    root = tk.Tk()
    ConsoleApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
