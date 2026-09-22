"""
configgui.params_tab  --  the Config (tunable parameters) tab.

ParamsMixin builds the auto-discovered parameter editor: it parses `list`, files
each parameter under its board-declared section (params.def order, via the
"# section:" headers), and shows a dense row per parameter with Current / New /
Set / Get. Nothing here needs editing when you add a parameter - it self-discovers.
"""

import os
import json
import datetime

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from .theme import UI, FONT_UI_SM, FONT_UI_B, FONT_MONO
from .protocol import PARAM_GROUP_ORDER, param_group_for


def _same(a, b):
    """Compare two parameter values numerically where possible, so "2" and
    "2.0000" are not reported as a difference."""
    try:
        return abs(float(a) - float(b)) < 1e-9
    except (TypeError, ValueError):
        return str(a).strip() == str(b).strip()


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

        # Tune snapshots. A setup is a set of numbers you arrived at over an
        # afternoon; without this the only record of it is the board's flash,
        # and the only way to answer "what did I change since lunch?" is memory.
        ttk.Button(ctl, text="Compare…", command=self.cmd_params_compare).pack(
            side="right", padx=(4, 0))
        ttk.Button(ctl, text="Load tune…", command=self.cmd_params_load).pack(side="right")
        ttk.Button(ctl, text="Save tune…", command=self.cmd_params_save).pack(
            side="right", padx=4)

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

    # ---- tune snapshots (save / compare / apply) ----
    def _params_snapshot(self):
        """Every parameter and its current value, as the board last reported it."""
        out = {}
        for name in self._param_order:
            p = self.params.get(name)
            if p is not None:
                out[name] = p["value_var"].get()
        return out

    def cmd_params_save(self):
        """Write the current tune to JSON, stamped with the firmware's params
        layout id so a later load can tell whether it still applies."""
        values = self._params_snapshot()
        if not values:
            messagebox.showinfo("Save tune",
                                "No parameters discovered yet - connect and "
                                "press 'Refresh all (list)' first.")
            return
        path = filedialog.asksaveasfilename(
            title="Save tune",
            defaultextension=".json",
            initialfile=datetime.datetime.now().strftime("tune_%Y-%m-%d_%H%M.json"),
            filetypes=[("Tune file", "*.json")])
        if not path:
            return
        blob = {
            "saved": datetime.datetime.now().isoformat(timespec="seconds"),
            "firmware": getattr(self, "_board_version", ""),
            "values": values,
        }
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(blob, fh, indent=2, sort_keys=True)
        except OSError as e:
            messagebox.showerror("Save tune", str(e))
            return
        self._log("tune saved -> %s (%d parameters)\n" % (path, len(values)), "sys")

    def _params_read_file(self, title):
        path = filedialog.askopenfilename(
            title=title, filetypes=[("Tune file", "*.json"), ("All files", "*.*")])
        if not path:
            return None, None
        try:
            with open(path, encoding="utf-8") as fh:
                blob = json.load(fh)
        except (OSError, ValueError) as e:
            messagebox.showerror(title, "Could not read that file:\n\n%s" % e)
            return None, None
        values = blob.get("values")
        if not isinstance(values, dict):
            messagebox.showerror(title, "That file has no parameter values in it.")
            return None, None
        return path, values

    def cmd_params_compare(self):
        """Show what differs between the board and a saved tune - the answer to
        'what did I change?', which is otherwise unanswerable."""
        path, saved = self._params_read_file("Compare tune")
        if saved is None:
            return
        live = self._params_snapshot()
        diffs, missing, extra = [], [], []
        for name, was in sorted(saved.items()):
            if name not in live:
                missing.append(name)
            elif not _same(live[name], was):
                diffs.append((name, was, live[name]))
        for name in sorted(live):
            if name not in saved:
                extra.append(name)
        self._params_show_diff(path, diffs, missing, extra, saved)

    def _params_show_diff(self, path, diffs, missing, extra, saved):
        win = tk.Toplevel(self.root)
        win.title("Compare with saved tune")
        win.configure(background=UI["card"])
        win.geometry("640x460")

        ttk.Label(win, text=os.path.basename(path), style="Muted.TLabel").pack(
            anchor="w", padx=10, pady=(8, 2))

        head = ("identical - the board matches this tune exactly" if not diffs
                else "%d parameter%s differ" % (len(diffs), "" if len(diffs) == 1 else "s"))
        tk.Label(win, text=head, font=FONT_UI_B, background=UI["card"],
                 foreground=UI["green"] if not diffs else UI["amber"]).pack(
                     anchor="w", padx=10)

        cols = ("saved", "board")
        t = ttk.Treeview(win, columns=cols, show="tree headings", selectmode="none")
        t.heading("#0", text="Parameter")
        t.heading("saved", text="In the file")
        t.heading("board", text="On the board")
        t.column("#0", width=260, anchor="w")
        t.column("saved", width=140, anchor="e")
        t.column("board", width=140, anchor="e")
        t.pack(fill="both", expand=True, padx=10, pady=6)
        for name, was, now in diffs:
            t.insert("", "end", text=name, values=(was, now))
        for name in missing:
            t.insert("", "end", text=name, values=("(in file)", "not on board"))
        for name in extra:
            t.insert("", "end", text=name, values=("not in file", "(on board)"))

        btn = ttk.Frame(win)
        btn.pack(fill="x", padx=10, pady=(0, 10))
        ttk.Button(btn, text="Close", command=win.destroy).pack(side="right")
        if diffs:
            ttk.Button(btn, text="Apply the file's values to the board",
                       style="Accent.TButton",
                       command=lambda: (self._params_apply(saved), win.destroy())
                       ).pack(side="right", padx=6)
        ttk.Label(btn, style="Muted.TLabel",
                  text="Applying writes to RAM only - press 'Save to flash' to keep it."
                  ).pack(side="left")

    def cmd_params_load(self):
        path, saved = self._params_read_file("Load tune")
        if saved is None:
            return
        live = self._params_snapshot()
        changing = [n for n, v in saved.items() if n in live and not _same(live[n], v)]
        if not changing:
            messagebox.showinfo("Load tune", "The board already matches that tune.")
            return
        if not messagebox.askyesno(
                "Load tune",
                "Write %d changed parameter%s to the board?\n\n"
                "This changes RAM only - press 'Save to flash' afterwards to keep "
                "it. Do it stationary, not while driving."
                % (len(changing), "" if len(changing) == 1 else "s")):
            return
        self._params_apply(saved)

    def _params_apply(self, values):
        """Write a tune parameter by parameter, exactly as typing `set` would.
        Only values that actually differ are sent, so this is quiet on the wire
        and the console log shows precisely what changed."""
        live = self._params_snapshot()
        sent = 0
        for name, val in sorted(values.items()):
            if name in live and not _same(live[name], val):
                self._send("set %s %s" % (name, val))
                sent += 1
        self._log("tune applied: %d parameter(s) written to RAM\n" % sent, "sys")
        self.root.after(300, self.rescan_params)

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
