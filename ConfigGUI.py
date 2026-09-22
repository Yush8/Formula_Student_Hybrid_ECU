#!/usr/bin/env python3
"""
HCU V2 Console  --  launcher
============================
A friendly GUI front-end for the USB-CDC command console on the Formula Student
hybrid controller. It speaks the *exact* same text protocol you already use in
PuTTY, so the firmware needs no changes:

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
    cansniff on|off               -> start/stop the raw CAN bus sniffer stream
    cansniff rate <hz>            -> set the snapshot rate (1..50 Hz)
    cansniff clear                -> forget all captured CAN ids
    cansniff list                 -> dump the captured id table once
    ping                          -> pong

Run it:
    pip install pyserial            # required
    pip install matplotlib          # optional - only for the live Plot tab
    python ConfigGUI.py

(tkinter ships with Python. On Debian/Ubuntu: sudo apt install python3-tk)

NOTE: only one program can own a COM port at a time -- close PuTTY before
connecting here, and vice-versa.

>>> NOTHING TO MAINTAIN HERE WHEN YOU ADD A PARAMETER OR A TELEMETRY SIGNAL. <<<
  * Tunables are discovered live from `list`  (add them in params.def).
  * Live signals are discovered live from `telem list` + the stream itself
    (add them in telem_signals.def). They also show up in the Plot tab's picker.
  * The CAN Bus tab is a sniffer: it self-discovers every id the board sees.
No edit here is ever needed for any of them.

---------------------------------------------------------------------------
The app used to be one large file; it now lives in the `configgui/` package so
each concern is easy to find and extend:

    configgui/theme.py        the dark palette, fonts and ttk styling
    configgui/protocol.py     the line regexes + the state/fault/grouping tables
    configgui/serial_io.py    the serial link + the RX line dispatcher
    configgui/chrome.py       header, bars, board actions, START / CLEAR FAULT
    configgui/params_tab.py   the Config (parameters) tab
    configgui/telem_tab.py    the Live Telemetry tab
    configgui/plot_tab.py     the live Plot tab (pick signals -> graph them)
    configgui/plot_widget.py  the matplotlib strip-chart widget
    configgui/sniffer_tab.py  the CAN Bus (raw sniffer) tab
    configgui/app.py          ConsoleApp: assembles the mixins above

This file is only the entry point, so `python ConfigGUI.py` still works exactly
as before.
---------------------------------------------------------------------------
"""

from configgui.app import main

if __name__ == "__main__":
    main()
