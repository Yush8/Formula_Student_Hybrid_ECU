"""
configgui.params_tab  --  the Config (tunable parameters) tab.

ParamsMixin builds the auto-discovered parameter editor: it parses `list`, files
each parameter under its board-declared section (params.def order, via the
"# section:" headers), and shows a dense row per parameter with Current / New /
Set / Get. Nothing here needs editing when you add a parameter - it self-discovers.
"""

import tkinter as tk
from tkinter import ttk

from .theme import UI, FONT_UI_SM, FONT_MONO
from .protocol import PARAM_GROUP_ORDER, param_group_for


class ParamsMixin:
    # ---- Config (parameters) tab ----
    def _build_var_panel(self, parent):
        # Control row: filter + a reminder of what the sections mean.
        ctl = ttk.Frame(parent)
        ctl.pack(fill="x", padx=8, pady=(4, 2))
        ttk.Label(ctl, text="Filter:").pack(side="left", padx=(0, 2))
        self.param_filter_var = tk.StringVar()
        pe = ttk.Entry(ctl, textvariable=self.param_filter_var, width=20)
        pe.pack(side="left")
        self.param_filter_var.trace_add("write", lambda *_: self._apply_param_filter())
        ttk.Button(ctl, text="✕", width=2,
                   command=lambda: self.param_filter_var.set("")).pack(side="left", padx=(2, 0))
        ttk.Label(ctl, style="Muted.TLabel",
                  text="  grouped by function · type in a field and press Enter (or Set) to write"
                  ).pack(side="left", padx=6)

        f = ttk.LabelFrame(parent, text="Config parameters (auto-discovered from the board)")
        f.pack(fill="both", expand=True, padx=8, pady=(2, 4))

        # Scrollable area so the panel copes with any number of parameters.
        canvas = tk.Canvas(f, highlightthickness=0, background=UI["card"])
        vsb = ttk.Scrollbar(f, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True, padx=4, pady=4)

        # Sections stack vertically inside this container (one LabelFrame each).
        self.var_container = ttk.Frame(canvas)
        self._grid_window = canvas.create_window((0, 0), window=self.var_container, anchor="nw")
        self._var_canvas = canvas

        self.var_container.bind(
            "<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind(
            "<Configure>", lambda e: canvas.itemconfigure(self._grid_window, width=e.width))
        # Mouse wheel while hovering the list.
        canvas.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", self._on_var_wheel))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))

        self.empty_lbl = ttk.Label(
            self.var_container, style="Muted.TLabel",
            text="(connect, then press 'Refresh all (list)' to discover parameters)")
        self.empty_lbl.pack(anchor="w", padx=8, pady=8)

    def _register_param_section(self, label):
        """Record a parameter-section label in the order the board declares it
        (params.def file order, via the '# section:' headers), so the Config tab
        shows sections in that same sequence. Idempotent; returns the label."""
        if label not in self._param_section_order:
            self._param_section_order.append(label)
        return label

    def _param_section_sequence(self):
        """Top-to-bottom order for the parameter sections. Prefer the order the
        board declared them in; fall back to the static PARAM_GROUP_ORDER for any
        section the board didn't declare (old firmware / heuristic). A literal
        'Other' catch-all always sinks to the bottom."""
        seq = [s for s in self._param_section_order if s in self._param_sections]
        for s in PARAM_GROUP_ORDER:                 # static fallback ordering
            if s in self._param_sections and s not in seq:
                seq.append(s)
        for s in self._param_sections:              # anything still unplaced
            if s not in seq:
                seq.append(s)
        if "Other" in seq:                          # keep the catch-all last
            seq = [s for s in seq if s != "Other"] + ["Other"]
        return seq

    def _ensure_param_section(self, label):
        """Return the section (a titled LabelFrame + its inner grid) for a group,
        creating it (and re-laying the sections in order) if it doesn't exist."""
        sec = self._param_sections.get(label)
        if sec is not None:
            return sec
        frame = ttk.LabelFrame(self.var_container, text=label)
        grid = ttk.Frame(frame)
        grid.pack(fill="x", padx=6, pady=(1, 3))
        for c, h in enumerate(["Parameter", "Current", "New value", "", ""]):
            ttk.Label(grid, text=h, foreground=UI["muted"], font=FONT_UI_SM).grid(
                row=0, column=c, padx=4, pady=(1, 2), sticky="w")
        grid.columnconfigure(0, minsize=170)
        sec = {"frame": frame, "grid": grid, "count": 0}
        self._param_sections[label] = sec
        # Re-pack every section so groups sit in their fixed PARAM_GROUP_ORDER
        # sequence, whatever order the board reported the parameters in.
        self._relayout_params()
        return sec

    def _relayout_params(self):
        """Stack the sections in the board-declared order (see
        _param_section_sequence) and apply the current filter, hiding rows (and
        whole sections) that don't match. Idempotent: it only re-packs /
        grid_removes existing widgets, never destroys them."""
        flt = self.param_filter_var.get().strip().lower() if hasattr(
            self, "param_filter_var") else ""

        # 1) per-row visibility, and which sections keep at least one visible row.
        section_visible = {}
        for name in self._param_order:
            p = self.params[name]
            show = (not flt) or (flt in name.lower())
            for w in p["widgets"]:
                if show:
                    w.grid()
                else:
                    w.grid_remove()
            g = p["group"]
            section_visible[g] = section_visible.get(g, False) or show

        # 2) re-pack sections in order; drop any that filtered down to nothing.
        for label in self._param_section_sequence():
            sec = self._param_sections[label]
            sec["frame"].pack_forget()
            if (not flt) or section_visible.get(label, False):
                sec["frame"].pack(fill="x", padx=6, pady=(2, 3))

    def _apply_param_filter(self):
        self._relayout_params()

    def _on_var_wheel(self, event):
        self._var_canvas.yview_scroll(int(-event.delta / 120), "units")

    # ---- discovery / rescan ----
    def rescan_params(self):
        """Forget the current rows and rebuild them from a fresh `list`. This is
        how a parameter you removed from params.def disappears, and a new one
        appears, with no edit to this script."""
        self._clear_params()
        self.cmd_list()

    def _clear_params(self):
        for name in self._param_order:
            for w in self.params[name]["widgets"]:
                w.destroy()
        self.params.clear()
        self._param_order.clear()
        self._param_buttons.clear()
        for sec in self._param_sections.values():
            sec["frame"].destroy()
        self._param_sections.clear()
        self._param_section_order.clear()
        self._current_param_section = None
        self.empty_lbl.pack(anchor="w", padx=8, pady=8)  # show the hint again

    # ---- get / set one parameter ----
    def cmd_get(self, name):
        self._send(f"get {name}")

    def cmd_set(self, name):
        val = self.params[name]["entry_var"].get().strip()
        if val:
            self._send(f"set {name} {val}")

    def _update_param(self, name, value):
        self._ensure_param_row(name)
        self.params[name]["value_var"].set(value)

    def _ensure_param_row(self, name):
        if name in self.params:
            return
        self.empty_lbl.pack_forget()

        # Prefer the section the board reported for this parameter (params.def's
        # own grouping, streamed as a "# section:" header just before it); fall
        # back to the local heuristic only for firmware that doesn't report one.
        label = self._current_param_section or param_group_for(name)
        self._register_param_section(label)
        sec = self._ensure_param_section(label)
        grid = sec["grid"]
        row = sec["count"] + 1          # row 0 is the section's column header

        lbl = ttk.Label(grid, text=name)
        lbl.grid(row=row, column=0, padx=4, pady=1, sticky="w")

        cur = tk.StringVar(value="—")
        cur_lbl = ttk.Label(grid, textvariable=cur, width=12, anchor="w",
                            foreground=UI["accent_hi"], font=FONT_MONO)
        cur_lbl.grid(row=row, column=1, padx=4, pady=1, sticky="w")

        ev = tk.StringVar()
        entry = ttk.Entry(grid, textvariable=ev, width=14, style="Compact.TEntry")
        entry.grid(row=row, column=2, padx=4, pady=1)
        entry.bind("<Return>", lambda _evt, n=name: self.cmd_set(n))

        sb = ttk.Button(grid, text="Set", width=6, style="Compact.TButton",
                        command=lambda n=name: self.cmd_set(n))
        sb.grid(row=row, column=3, padx=2, pady=1)
        gb = ttk.Button(grid, text="Get", width=6, style="Compact.TButton",
                        command=lambda n=name: self.cmd_get(n))
        gb.grid(row=row, column=4, padx=2, pady=1)

        connected = bool(self.ser and self.ser.is_open)
        for b in (sb, gb):
            b.config(state="normal" if connected else "disabled")
            self._param_buttons.append(b)

        sec["count"] += 1
        self.params[name] = {"value_var": cur, "entry_var": ev, "group": label,
                             "widgets": [lbl, cur_lbl, entry, sb, gb]}
        self._param_order.append(name)
        # A filter may be active while parameters stream in: keep it applied.
        if self.param_filter_var.get().strip():
            self._relayout_params()
