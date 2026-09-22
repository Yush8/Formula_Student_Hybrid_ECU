"""
configgui.shell  --  the window frame: title row, tabs, connection pill, menu.

One slim row across the top, laid out the way desktop software is:

    ◆ HCU V2   Telemetry  Plot  Health  Events  CAN Bus  …      ● COM7 ▾   Board ▾

  TABS  are drawn here rather than by ttk.Notebook (whose own tab row is hidden),
        so they sit in the title row instead of eating a row of their own. The
        notebook still does the page switching; these are just its buttons. Tabs
        carry small badges - a red count on Events when something has faulted, a
        coloured dot on Health, a dot on Sessions while recording - so you can see
        trouble without being on that tab. Ctrl+1..8 and Ctrl+Tab switch tabs.

  CONNECTION  is a pill showing the port and its state. Auto-reconnect handles
        the normal case, so the port / baud / connect controls live in a small
        panel that drops down from the pill only when you actually want them.

  BOARD ▾  holds the board-wide actions that used to occupy a permanent row
        (refresh, ping, stats, save to flash, defaults, the clock, version) and the
        view options - findable when needed, out of the way otherwise.

The drop-down panels are drawn here too (Popover) rather than using tk.Menu,
because Windows draws native menus in the system's light colours, which clashes
with the dark theme. Popovers close on Escape, on a click anywhere else, or when
the window moves.
"""

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_UI, FONT_UI_SM, FONT_UI_B, FONT_MONO
from .protocol import BAUDS

# Header palette: the title row is the darkest surface, like a window title bar,
# so the tabs read as chrome and the pages below read as content.
HEAD_BG = UI["page"]
HOVER_BG = UI["elev"]

# (key, notebook attribute, label) in display order. The key is what the rest of
# the app uses to set a badge; the label is what you see.
TABS = [
    ("telem",    "tab_telem",    "Telemetry"),
    ("plot",     "tab_plot",     "Plot"),
    ("health",   "tab_health",   "Health"),
    ("events",   "tab_events",   "Events"),
    ("sniffer",  "tab_sniffer",  "CAN Bus"),
    ("sessions", "tab_sessions", "Sessions"),
    ("config",   "tab_config",   "Config"),
    ("console",  "tab_console",  "Console"),
]


# ---------------------------------------------------------------------------
# A dark drop-down panel anchored to a widget
# ---------------------------------------------------------------------------
class Popover:
    """A borderless panel that drops down from `anchor`, right-aligned to it.

    Built once and kept (withdrawn) rather than rebuilt per open, because the
    connection panel's widgets - the port and baud boxes - are read by the rest
    of the app even while the panel is closed."""

    _open = None                     # at most one popover open at a time

    def __init__(self, root, anchor):
        self.root = root
        self.anchor = anchor
        self.win = tk.Toplevel(root)
        self.win.withdraw()
        self.win.overrideredirect(True)
        self.win.transient(root)
        self.win.configure(background=UI["border"])           # 1 px outline
        self.body = tk.Frame(self.win, background=UI["card"])
        self.body.pack(fill="both", expand=True, padx=1, pady=1)
        self.shown = False
        self.on_show = None
        root.bind_all("<Button-1>", self._outside_click, add="+")
        root.bind("<Configure>", self._root_moved, add="+")
        root.bind_all("<Escape>", lambda _e: self.hide(), add="+")

    def toggle(self):
        self.hide() if self.shown else self.show()

    def show(self):
        if Popover._open is not None and Popover._open is not self:
            Popover._open.hide()
        if self.on_show:
            self.on_show()
        self.win.update_idletasks()
        a = self.anchor
        x = a.winfo_rootx() + a.winfo_width() - self.win.winfo_reqwidth()
        y = a.winfo_rooty() + a.winfo_height() + 2
        x = max(self.root.winfo_rootx() + 4, x)
        self.win.geometry("+%d+%d" % (x, y))
        self.win.deiconify()
        self.win.lift()
        self.shown = True
        Popover._open = self

    def hide(self):
        if self.shown:
            self.win.withdraw()
            self.shown = False
            if Popover._open is self:
                Popover._open = None

    # ---- closing on an outside click ----
    def _outside_click(self, event):
        if not self.shown:
            return
        w = event.widget
        # Tk-internal widgets (a combobox's drop-down list, for one) arrive as a
        # plain path string. They belong to our own controls, so never close on them.
        if isinstance(w, str):
            return
        try:
            if w.winfo_toplevel() is self.win:
                return
        except (tk.TclError, AttributeError):
            return
        if self._is_anchor(w):
            return                   # the anchor's own handler toggles it
        self.hide()

    def _is_anchor(self, w):
        while w is not None:
            if w is self.anchor:
                return True
            w = getattr(w, "master", None)
        return False

    def _root_moved(self, event):
        if event.widget is self.root:
            self.hide()


class MenuPopover(Popover):
    """A Popover holding a vertical list of actions, like a menu.

    Items can require a live connection: those are greyed out and inert while
    disconnected, so a menu full of board commands never pops a string of
    'not connected' warnings at you."""

    def __init__(self, root, anchor):
        super().__init__(root, anchor)
        self._items = []             # (row, label widget, hint widget, needs_link)
        self._connected = False

    def heading(self, text, right_var=None):
        row = tk.Frame(self.body, background=UI["card"])
        row.pack(fill="x", padx=12, pady=(8, 2))
        tk.Label(row, text=text.upper(), font=FONT_UI_SM, background=UI["card"],
                 foreground=UI["muted"]).pack(side="left")
        if right_var is not None:
            tk.Label(row, textvariable=right_var, font=FONT_MONO,
                     background=UI["card"], foreground=UI["accent_hi"]).pack(side="right")

    def separator(self):
        tk.Frame(self.body, height=1, background=UI["border"]).pack(
            fill="x", padx=8, pady=4)

    def item(self, text, command, hint="", needs_link=True):
        row = tk.Frame(self.body, background=UI["card"], cursor="hand2")
        row.pack(fill="x", padx=4)
        lbl = tk.Label(row, text=text, anchor="w", font=FONT_UI,
                       background=UI["card"], foreground=UI["fg"],
                       padx=10, pady=4)
        lbl.pack(side="left", fill="x", expand=True)
        hl = tk.Label(row, text=hint, font=FONT_UI_SM, background=UI["card"],
                      foreground=UI["muted"], padx=10)
        hl.pack(side="right")
        entry = (row, lbl, hl, needs_link)
        self._items.append(entry)

        def enabled():
            return self._connected or not needs_link

        def paint(bg):
            for w in (row, lbl, hl):
                w.configure(background=bg)

        def on_enter(_e):
            if enabled():
                paint(HOVER_BG)

        def on_leave(_e):
            paint(UI["card"])

        def on_click(_e):
            if not enabled():
                return
            paint(UI["card"])
            self.hide()
            command()

        for w in (row, lbl, hl):
            w.bind("<Enter>", on_enter)
            w.bind("<Leave>", on_leave)
            w.bind("<Button-1>", on_click)
        return lbl

    def check(self, text, var, command):
        """A toggle item: a tick mark that follows `var`."""
        box = tk.StringVar()

        def sync(*_):
            box.set("✓" if var.get() else " ")

        var.trace_add("write", sync)
        sync()
        row = tk.Frame(self.body, background=UI["card"], cursor="hand2")
        row.pack(fill="x", padx=4)
        mark = tk.Label(row, textvariable=box, width=2, font=FONT_UI_B,
                        background=UI["card"], foreground=UI["accent_hi"])
        mark.pack(side="left", padx=(8, 0))
        lbl = tk.Label(row, text=text, anchor="w", font=FONT_UI,
                       background=UI["card"], foreground=UI["fg"], padx=2, pady=4)
        lbl.pack(side="left", fill="x", expand=True)

        def paint(bg):
            for w in (row, mark, lbl):
                w.configure(background=bg)

        def on_click(_e):
            var.set(not var.get())
            command()

        for w in (row, mark, lbl):
            w.bind("<Enter>", lambda _e: paint(HOVER_BG))
            w.bind("<Leave>", lambda _e: paint(UI["card"]))
            w.bind("<Button-1>", on_click)

    def note(self, text):
        tk.Label(self.body, text=text, font=FONT_UI_SM, justify="left",
                 background=UI["card"], foreground=UI["muted"]).pack(
                     anchor="w", padx=14, pady=(2, 8))

    def set_connected(self, on):
        self._connected = on
        for _row, lbl, hl, needs_link in self._items:
            dim = needs_link and not on
            lbl.configure(foreground="#56616d" if dim else UI["fg"])


# ---------------------------------------------------------------------------
# The shell mixin
# ---------------------------------------------------------------------------
class ShellMixin:
    def _build_shell(self):
        """The title row. Called first, so it packs at the very top; the tab
        buttons are added later by _build_tab_strip, once the notebook exists."""
        self.show_drive_var = tk.BooleanVar(value=True)
        self.clock_var = tk.StringVar(value="—")
        self._tab_widgets = {}        # key -> dict(frame, text, badge, bar)
        self._tab_badges = {}         # key -> (text, colour) set before the strip exists

        head = tk.Frame(self.root, background=HEAD_BG)
        head.pack(fill="x")
        self.shell_head = head

        # identity
        title = tk.Frame(head, background=HEAD_BG)
        title.pack(side="left", padx=(14, 18))
        tk.Label(title, text="◆ HCU", font=("Segoe UI", 12, "bold"),
                 background=HEAD_BG, foreground=UI["accent"]).pack(side="left")
        tk.Label(title, text=" V2", font=("Segoe UI", 12, "bold"),
                 background=HEAD_BG, foreground=UI["fg"]).pack(side="left")

        # tab buttons go here (see _build_tab_strip)
        self.shell_tabs = tk.Frame(head, background=HEAD_BG)
        self.shell_tabs.pack(side="left", fill="y")

        # right side: Board menu, then the connection pill to its left
        self.menu_btn = self._shell_chip(head, "Board  ▾")
        self.menu_btn.pack(side="right", padx=(4, 10), pady=6)
        self.conn_pill = tk.Frame(head, background=HOVER_BG, cursor="hand2")
        self.conn_pill.pack(side="right", padx=4, pady=6)
        self.conn_dot = tk.Label(self.conn_pill, text="●", font=FONT_UI_B,
                                 background=HOVER_BG, foreground=UI["red_hi"],
                                 padx=0)
        self.conn_dot.pack(side="left", padx=(10, 4))
        self.conn_text = tk.Label(self.conn_pill, text="Not connected",
                                  font=FONT_UI_B, background=HOVER_BG,
                                  foreground=UI["fg"])
        self.conn_text.pack(side="left")
        self.conn_caret = tk.Label(self.conn_pill, text="  ▾", font=FONT_UI_SM,
                                   background=HOVER_BG, foreground=UI["muted"])
        self.conn_caret.pack(side="left", padx=(0, 10), ipady=4)
        # serial_io / chrome set the connection state through this name
        self.status_lbl = self.conn_text

        # hairline under the title row; the active tab's underline sits on it
        tk.Frame(self.root, height=1, background=UI["border"]).pack(fill="x")

        self._build_conn_popover()
        self._build_board_menu()
        self._shell_hover(self.conn_pill, HOVER_BG, UI["border"])
        for w in (self.conn_pill, self.conn_dot, self.conn_text, self.conn_caret):
            w.bind("<Button-1>", lambda _e: self.conn_pop.toggle())
        self._shell_keys()

    # ---- small helpers ----
    def _shell_chip(self, parent, text):
        chip = tk.Label(parent, text=text, font=FONT_UI_B, cursor="hand2",
                        background=HOVER_BG, foreground=UI["fg"], padx=12, pady=4)
        self._shell_hover(chip, HOVER_BG, UI["border"])
        return chip

    def _shell_hover(self, widget, normal, hover):
        """Hover highlight for a chip, including every label inside it."""
        parts = [widget] + list(widget.winfo_children())

        def paint(bg):
            for w in parts:
                w.configure(background=bg)

        for w in parts:
            w.bind("<Enter>", lambda _e: paint(hover), add="+")
            w.bind("<Leave>", lambda _e: paint(normal), add="+")

    # ---- the connection panel ----
    def _build_conn_popover(self):
        """Port, baud and connect - only shown when you open it. The rest of the
        app reads self.port_cb / self.baud_cb, so they are built now and simply
        live in a closed panel."""
        pop = Popover(self.root, self.conn_pill)
        self.conn_pop = pop
        b = pop.body

        tk.Label(b, text="CONNECTION", font=FONT_UI_SM, background=UI["card"],
                 foreground=UI["muted"]).grid(row=0, column=0, columnspan=3,
                                              sticky="w", padx=12, pady=(10, 6))

        ttk.Label(b, text="Port").grid(row=1, column=0, sticky="w", padx=(12, 6), pady=3)
        self.port_cb = ttk.Combobox(b, width=30, state="readonly")
        self.port_cb.grid(row=1, column=1, sticky="we", pady=3)
        ttk.Button(b, text="↻", width=3, command=self.refresh_ports).grid(
            row=1, column=2, padx=(4, 12), pady=3)

        ttk.Label(b, text="Baud").grid(row=2, column=0, sticky="w", padx=(12, 6), pady=3)
        self.baud_cb = ttk.Combobox(b, width=10, state="readonly", values=BAUDS)
        self.baud_cb.set("115200")
        self.baud_cb.grid(row=2, column=1, sticky="w", pady=3)

        ttk.Checkbutton(b, text="Reconnect automatically when the board appears",
                        variable=self.auto_reconnect_var,
                        command=self._save_settings).grid(
                            row=3, column=0, columnspan=3, sticky="w",
                            padx=12, pady=(6, 2))

        self.connect_btn = ttk.Button(b, text="Connect", style="Accent.TButton",
                                      command=self._shell_connect_clicked)
        self.connect_btn.grid(row=4, column=0, columnspan=3, sticky="we",
                              padx=12, pady=(8, 4))
        self.conn_note = tk.Label(b, text="", font=FONT_UI_SM, justify="left",
                                  background=UI["card"], foreground=UI["muted"],
                                  wraplength=300)
        self.conn_note.grid(row=5, column=0, columnspan=3, sticky="w",
                            padx=12, pady=(0, 10))
        pop.on_show = self._shell_conn_opening

    def _shell_conn_opening(self):
        """Refresh the port list each time the panel opens, so a board plugged in
        a moment ago is already there."""
        dev = getattr(self, "_target_device", None)
        linked = bool(self.ser and self.ser.is_open)
        if linked:
            # Show the port actually in use, and lock the boxes: changing them
            # would do nothing until the next connect, which only confuses.
            label = next((l for l, d in self._port_map.items() if d == dev), dev or "")
            self.port_cb.set(label)
            note = "Connected to %s. Disconnect to change port or baud." % dev
        else:
            self.refresh_ports()
            note = (("Remembered port: %s" % dev) if dev else
                    "Pick the board's COM port. It is remembered for next time.")
        state = "disabled" if linked else "readonly"
        self.port_cb.configure(state=state)
        self.baud_cb.configure(state=state)
        self.conn_note.configure(text=note)

    def _shell_connect_clicked(self):
        self.toggle_connection()
        if self.ser and self.ser.is_open:
            self.conn_pop.hide()

    def _shell_set_status(self, state, device=None):
        """One place that paints the pill: 'connected', 'disconnected' or
        'reconnecting'."""
        if state == "connected":
            dot, text = UI["green"], (device or "Connected")
        elif state == "reconnecting":
            dot, text = UI["amber"], "Reconnecting…"
        else:
            dot, text = UI["red_hi"], "Not connected"
        self.conn_dot.configure(foreground=dot)
        self.conn_text.configure(text=text)

    def _shell_set_connected(self, on):
        dev = None
        if on and self.ser is not None:
            dev = getattr(self.ser, "port", None) or self._target_device
        self._shell_set_status("connected" if on else "disconnected", dev)
        self.connect_btn.configure(text="Disconnect" if on else "Connect")
        self.board_menu.set_connected(on)

    # ---- the Board menu ----
    def _build_board_menu(self):
        m = MenuPopover(self.root, self.menu_btn)
        self.board_menu = m
        m.heading("Board", right_var=self.clock_var)
        m.item("Refresh parameters and signals", self.rescan_all, "F5")
        m.item("Ping", self.cmd_ping)
        m.item("Firmware version", self.cmd_version)
        m.separator()
        m.item("Show stats in Console", self.cmd_stats)
        m.item("Zero the stats counters", self.cmd_stats_clear)
        m.separator()
        m.item("Save parameters to flash…", self.cmd_save)
        m.item("Load built-in defaults…", self.cmd_defaults)
        m.separator()
        m.item("Set board clock to PC time", self.cmd_set_time_now)
        m.item("Read board clock", self.cmd_get_time)
        m.heading("View")
        m.check("Drive and state bar", self.show_drive_var, self._shell_apply_drive_bar)
        m.item("State and fault code reference", self._show_fault_codes,
               needs_link=False)
        m.separator()
        m.note("Ctrl+1 … 8 switch tabs   ·   Ctrl+Tab next tab   ·   F5 refresh")
        m.set_connected(False)
        self.menu_btn.bind("<Button-1>", lambda _e: m.toggle())

    def _shell_apply_drive_bar(self):
        """Show or hide the START / state strip. It stays in the same place when
        shown again - directly above the pages."""
        if self.show_drive_var.get():
            self.drive_bar.pack(fill="x", before=self.notebook)
        else:
            self.drive_bar.pack_forget()
        self._save_settings()

    # ---- tabs ----
    def _build_tab_strip(self):
        """One button per notebook page, drawn in the title row. Called once the
        notebook and its pages exist."""
        for i, (key, attr, label) in enumerate(TABS):
            page = getattr(self, attr)
            cell = tk.Frame(self.shell_tabs, background=HEAD_BG, cursor="hand2")
            cell.pack(side="left", fill="y")
            inner = tk.Frame(cell, background=HEAD_BG)
            inner.pack(side="top", fill="both", expand=True, padx=11)
            text = tk.Label(inner, text=label, font=FONT_UI_B,
                            background=HEAD_BG, foreground=UI["muted"])
            text.pack(side="left", pady=(9, 7))
            badge = tk.Label(inner, text="", font=("Segoe UI", 8, "bold"),
                             background=HEAD_BG, foreground=UI["fg"])
            badge.pack(side="left", padx=(4, 0), pady=(9, 7))
            bar = tk.Frame(cell, height=2, background=HEAD_BG)
            bar.pack(side="bottom", fill="x")
            parts = (cell, inner, text, badge)
            for w in parts:
                w.bind("<Button-1>", lambda _e, p=page: self.notebook.select(p))
                w.bind("<Enter>", lambda _e, k=key: self._tab_hover(k, True))
                w.bind("<Leave>", lambda _e, k=key: self._tab_hover(k, False))
            self._tab_widgets[key] = {"page": page, "text": text,
                                      "badge": badge, "bar": bar}
        self.notebook.bind("<<NotebookTabChanged>>", lambda _e: self._tab_restyle())
        for key, (txt, colour) in self._tab_badges.items():
            self._shell_badge(key, txt, colour)
        self._tab_restyle()

    def _tab_current(self):
        try:
            cur = self.notebook.nametowidget(self.notebook.select())
        except (tk.TclError, KeyError):
            return None
        for key, t in self._tab_widgets.items():
            if t["page"] is cur:
                return key
        return None

    def _tab_restyle(self):
        cur = self._tab_current()
        for key, t in self._tab_widgets.items():
            active = key == cur
            t["text"].configure(foreground=UI["fg"] if active else UI["muted"])
            t["bar"].configure(background=UI["accent"] if active else HEAD_BG)

    def _tab_hover(self, key, on):
        if key == self._tab_current():
            return
        t = self._tab_widgets.get(key)
        if t:
            t["text"].configure(foreground=UI["fg"] if on else UI["muted"])

    def _shell_badge(self, key, text="", colour=None):
        """Put a small badge on a tab: a count, or a dot. Empty text clears it.
        Safe to call before the strip exists - it is applied when it is built."""
        self._tab_badges[key] = (text, colour)
        t = getattr(self, "_tab_widgets", {}).get(key)
        if not t:
            return
        t["badge"].configure(text=text, foreground=colour or UI["fg"])

    # ---- keyboard ----
    def _shell_keys(self):
        for i in range(len(TABS)):
            self.root.bind_all("<Control-Key-%d>" % (i + 1),
                               lambda _e, n=i: self._tab_goto(n))
        self.root.bind_all("<Control-Tab>", lambda _e: self._tab_step(1))
        self.root.bind_all("<F5>", lambda _e: self._shell_refresh())
        for seq in ("<Control-Shift-Tab>", "<Control-ISO_Left_Tab>"):
            try:
                self.root.bind_all(seq, lambda _e: self._tab_step(-1))
            except tk.TclError:
                pass

    def _shell_refresh(self):
        if self.ser and self.ser.is_open:
            self.rescan_all()
        return "break"

    def _tab_goto(self, n):
        if 0 <= n < len(TABS) and TABS[n][0] in self._tab_widgets:
            self.notebook.select(self._tab_widgets[TABS[n][0]]["page"])
        return "break"

    def _tab_step(self, step):
        keys = [k for k, _a, _l in TABS]
        cur = self._tab_current()
        i = keys.index(cur) if cur in keys else 0
        self._tab_goto((i + step) % len(keys))
        return "break"
