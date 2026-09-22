"""
configgui.sessions_tab  --  record a run, keep it, load it back.

A session is everything about one run, written to disk as it happens: every
signal, the event timeline, periodic health snapshots and the console text, with
a manifest saying what firmware produced it (see configgui/session.py for the
folder layout). Sessions are named, listed here newest-first, and loadable back
into the Plot tab for review.

Why this exists: without it, the only record of a run is whatever was still on
screen when it ended. With it, "it did the thing again" becomes a folder you can
open next week, diff against the run before, or attach to a message.

  RECORD      pick a name, press Record. The recorder locks the signal list at
              the start (a CSV cannot grow a column halfway down) and reports
              anything discovered later rather than dropping it silently.
  NOTES       what you were trying, typed while you remember it. Saved into the
              session's meta.json, and reproduced at the top of a debug bundle.
  LOAD        reads the session back into the Plot tab's capture buffer, so you
              review a recording with the same picker, watch sets and scrubbing
              as watching one live.
  EXPORT      writes the debug bundle (configgui/bundle.py) - the single file to
              hand to someone, or something, that was not there.

Deleting asks first, every time, and only ever touches folders inside sessions/.
"""

import os
import time

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from .theme import UI, FONT_MONO, FONT_UI_B
from .history import human_bytes
from . import session as S
from . import bundle as B


class SessionsMixin:
    # ---- Sessions tab ----
    def _build_sessions_panel(self, parent):
        self._session_rows = {}          # tree iid -> session meta dict
        self._loaded_session = None      # meta of the session currently loaded

        # ---- recording bar ----
        rec = ttk.LabelFrame(parent, text="Record")
        rec.pack(fill="x", padx=8, pady=(4, 2))
        bar = ttk.Frame(rec)
        bar.pack(fill="x", padx=6, pady=4)

        ttk.Label(bar, text="Name:").pack(side="left")
        self.session_name_var = tk.StringVar()
        ttk.Entry(bar, textvariable=self.session_name_var, width=22).pack(
            side="left", padx=(2, 8))

        self.session_rec_btn = tk.Button(
            bar, text="● RECORD", command=self._session_toggle,
            background=UI["red_hi"], foreground="#ffffff",
            activebackground="#b91c1c", activeforeground="#ffffff",
            disabledforeground="#5b6673", font=("Segoe UI", 10, "bold"),
            padx=12, pady=3, relief="flat", bd=0, cursor="hand2",
            highlightthickness=0)
        self.session_rec_btn.pack(side="left")

        self.session_rec_status = ttk.Label(
            bar, text="not recording", foreground=UI["muted"],
            background=UI["card"], font=FONT_MONO)
        self.session_rec_status.pack(side="left", padx=10)

        ttk.Button(bar, text="Export debug bundle…",
                   command=self.cmd_export_bundle).pack(side="right")

        # ---- notes ----
        note = ttk.Frame(rec)
        note.pack(fill="x", padx=6, pady=(0, 4))
        ttk.Label(note, text="Notes:", style="Muted.TLabel").pack(side="left",
                                                                 anchor="n")
        self.session_notes = tk.Text(note, height=2, font=FONT_MONO, relief="flat",
                                     background=UI["field"], foreground=UI["fg"],
                                     insertbackground=UI["fg"], highlightthickness=1,
                                     highlightbackground=UI["border"],
                                     padx=6, pady=3, wrap="word")
        self.session_notes.pack(side="left", fill="x", expand=True, padx=(4, 0))
        ttk.Label(rec, style="Muted.TLabel", justify="left",
                  text="   What you were trying, while you still remember. Saved with the "
                       "session and reproduced at the top of a debug bundle.").pack(
                           anchor="w", padx=6, pady=(0, 4))

        # ---- saved sessions ----
        body = ttk.LabelFrame(parent, text="Saved sessions  ·  newest first")
        body.pack(fill="both", expand=True, padx=8, pady=(2, 2))

        cols = ("when", "dur", "rows", "events", "size", "notes")
        t = ttk.Treeview(body, columns=cols, show="tree headings", selectmode="browse")
        t.heading("#0", text="Name")
        for key, label, width, anchor, stretch in (
                ("when", "Started", 145, "w", False),
                ("dur", "Length", 70, "e", False),
                ("rows", "Frames", 80, "e", False),
                ("events", "Events", 65, "e", False),
                ("size", "Size", 75, "e", False),
                ("notes", "Notes", 260, "w", True)):
            t.heading(key, text=label)
            t.column(key, width=width, anchor=anchor, stretch=stretch)
        t.column("#0", width=210, anchor="w", stretch=False)
        t.tag_configure("loaded", foreground=UI["accent_hi"], font=FONT_UI_B)
        t.tag_configure("incomplete", foreground=UI["amber"])
        t.bind("<Double-1>", lambda _e: self.cmd_session_load())

        vsb = ttk.Scrollbar(body, orient="vertical", command=t.yview)
        t.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        t.pack(side="left", fill="both", expand=True, padx=4, pady=4)
        self.sessions_tree = t

        act = ttk.Frame(parent)
        act.pack(fill="x", padx=8, pady=(0, 4))
        ttk.Button(act, text="Load into Plot", style="Accent.TButton",
                   command=self.cmd_session_load).pack(side="left")
        ttk.Button(act, text="Open folder",
                   command=self.cmd_session_open).pack(side="left", padx=6)
        ttk.Button(act, text="Load a log CSV…",
                   command=self.cmd_load_csv).pack(side="left")
        ttk.Button(act, text="Refresh", command=self._sessions_reload).pack(side="left",
                                                                           padx=6)
        ttk.Button(act, text="Delete…", command=self.cmd_session_delete).pack(side="right")
        self.sessions_status = ttk.Label(act, text="", foreground=UI["muted"],
                                         background=UI["card"], font=FONT_MONO)
        self.sessions_status.pack(side="right", padx=10)

        self._sessions_reload()
        self.root.after(1000, self._session_tick)

    # ---- recording ----
    def _session_toggle(self):
        if self.recorder.active:
            self._session_stop()
        else:
            self._session_start()

    def _session_start(self):
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return
        if not self._telem_order:
            messagebox.showwarning(
                "No signals yet",
                "The signal list is still empty, so a recording would have no "
                "columns.\n\nPress 'Refresh signals' on the Live Telemetry tab "
                "(or connect) and try again.")
            return

        meta = {
            "firmware": self._board_version or "(unknown - run `version`)",
            "telem_rate_hz": self.telem_rate_var.get(),
            "port": self._target_device,
            "notes": self.session_notes.get("1.0", "end").strip(),
        }
        path = self.recorder.start(self.session_name_var.get(),
                                   list(self._telem_order), meta)
        self.recorder.set_rate(self.telem_rate_var.get())
        self._telem_dropped = 0
        self._log("recording session -> %s\n" % os.path.basename(path), "sys")

        # A recording with the stream off would be an empty file, which is a
        # trap rather than a feature: start the stream too.
        if not self._telem_streaming:
            self._send(f"telem rate {self.telem_rate_var.get()}")
            self._send("telem on")
        self._session_update_button()

    def _session_stop(self):
        self.recorder.set_notes(self.session_notes.get("1.0", "end").strip())
        path = self.recorder.stop()
        if path:
            self._log("session saved -> %s\n" % os.path.basename(path), "sys")
        self._session_update_button()
        self._sessions_reload()

    def _session_update_button(self):
        # A red dot on the Sessions tab whenever a recording is running, so you
        # cannot forget one is going (or wonder whether it is).
        self._shell_badge("sessions", "● REC" if self.recorder.active else "",
                          UI["red_hi"])
        if self.recorder.active:
            self.session_rec_btn.config(text="■ STOP", background=UI["amber"],
                                        foreground="#1a1206")
        else:
            self.session_rec_btn.config(text="● RECORD", background=UI["red_hi"],
                                        foreground="#ffffff")
            self.session_rec_status.config(text="not recording",
                                           foreground=UI["muted"])

    def _session_tick(self):
        """Once a second: show what the running recording has captured. Seeing
        the row count climb is how you know it is actually recording."""
        if self.recorder.active:
            secs = time.time() - self.recorder.started_wall
            drops = self._telem_dropped
            text = "%s  %d:%02d  %s frames  %s events  %s" % (
                os.path.basename(self.recorder.path or ""),
                int(secs // 60), int(secs % 60),
                f"{self.recorder.rows:,}", f"{self.recorder.events:,}",
                human_bytes(self.recorder.size_bytes()))
            if drops:
                text += "  ·  %d LOST" % drops
            self.session_rec_status.config(
                text=text, foreground=UI["amber"] if drops else UI["green"])
        self.root.after(1000, self._session_tick)

    # ---- the saved list ----
    def _sessions_reload(self):
        self.sessions_tree.delete(*self.sessions_tree.get_children())
        self._session_rows = {}
        total = 0
        for i, meta in enumerate(S.list_sessions()):
            iid = "s%d" % i
            dur = meta.get("duration_s")
            tags = []
            if meta.get("incomplete") or dur is None:
                tags.append("incomplete")
            if self._loaded_session and meta["path"] == self._loaded_session.get("path"):
                tags.append("loaded")
            self.sessions_tree.insert(
                "", "end", iid=iid, text=meta.get("name", meta["folder"]),
                values=(meta.get("started", "?"),
                        _dur(dur),
                        f"{meta.get('rows', 0):,}",
                        meta.get("events", 0),
                        human_bytes(meta.get("size", 0)),
                        (meta.get("notes") or "").replace("\n", " ")[:120]),
                tags=tuple(tags))
            self._session_rows[iid] = meta
            total += meta.get("size", 0)
        n = len(self._session_rows)
        self.sessions_status.config(
            text="%d session%s  ·  %s on disk" % (n, "" if n == 1 else "s",
                                                  human_bytes(total)))

    def _session_selected(self):
        sel = self.sessions_tree.selection()
        return self._session_rows.get(sel[0]) if sel else None

    # ---- actions ----
    def cmd_session_load(self):
        meta = self._session_selected()
        if meta is None:
            messagebox.showinfo("Load session", "Pick a session first.")
            return
        if getattr(self, "plot", None) is None:
            messagebox.showinfo(
                "Load session",
                "Loading a session needs the Plot tab, which needs matplotlib.\n\n"
                "    pip install matplotlib")
            return
        try:
            hist, loaded_meta, events = S.load_session(meta["path"])
        except Exception as e:
            messagebox.showerror("Load session", "Could not read that session:\n\n%s" % e)
            return
        self._plot_load_history(hist, "session: " + meta.get("name", meta["folder"]))

        # Replay the recorded timeline too, so the Events tab matches the plot.
        self.events.clear()
        self.events_tree.delete(*self.events_tree.get_children())
        self._events_rowids = {}
        for t, tick, signal, old, new in events:
            self._events_append(self.events.add(tick, signal, old, new))

        self._loaded_session = dict(loaded_meta or {}, path=meta["path"])
        self._sessions_reload()
        self.notebook.select(self.tab_plot)

    def cmd_load_csv(self):
        """Load any decoded log CSV - notably hcu_logdecode.py output from the SD
        card - into the Plot tab, so card logs review exactly like USB sessions."""
        if getattr(self, "plot", None) is None:
            messagebox.showinfo("Load CSV", "This needs matplotlib for the Plot tab.")
            return
        path = filedialog.askopenfilename(
            title="Open a decoded log CSV",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")])
        if not path:
            return
        try:
            hist = S.load_csv(path)
        except Exception as e:
            messagebox.showerror("Load CSV", "Could not read that file:\n\n%s" % e)
            return
        if not hist.names():
            messagebox.showwarning(
                "Load CSV", "No numeric columns found in that file.")
            return
        self._plot_load_history(hist, "file: " + os.path.basename(path))
        self.notebook.select(self.tab_plot)

    def cmd_session_open(self):
        meta = self._session_selected()
        if meta is None:
            return
        try:
            os.startfile(meta["path"])        # Windows shell: open the folder
        except Exception as e:
            messagebox.showinfo("Open folder", "%s\n\n%s" % (meta["path"], e))

    def cmd_session_delete(self):
        meta = self._session_selected()
        if meta is None:
            return
        if self.recorder.active and meta["path"] == self.recorder.path:
            messagebox.showwarning("Delete session",
                                   "That session is still recording. Stop it first.")
            return
        if not messagebox.askyesno(
                "Delete session",
                "Permanently delete this session?\n\n%s\n\n"
                "This removes the folder and everything in it. It cannot be undone."
                % meta["path"]):
            return
        try:
            S.delete_session(meta["path"])
        except Exception as e:
            messagebox.showerror("Delete session", str(e))
            return
        if self._loaded_session and self._loaded_session.get("path") == meta["path"]:
            self._loaded_session = None
        self._sessions_reload()

    # ---- the debug bundle ----
    def cmd_export_bundle(self):
        """Everything about the current moment, in one file you can hand over."""
        path = filedialog.asksaveasfilename(
            title="Export debug bundle",
            initialfile=B.default_name(),
            defaultextension=".zip",
            filetypes=[("Zip archive", "*.zip")])
        if not path:
            return
        try:
            result = B.build(path, self._bundle_context())
        except Exception as e:
            messagebox.showerror("Export debug bundle", str(e))
            return
        messagebox.showinfo(
            "Debug bundle exported",
            "%s\n\n%s, %d files.\n\nIt opens with README.md, which summarises the "
            "firmware, the state, what fired and the recent events - so whoever "
            "reads it does not need the car." % (
                path, human_bytes(result["bytes"]), len(result["files"])))

    def _bundle_context(self):
        """Gather everything the bundle writer needs. Kept here (not in
        bundle.py) so the exporter stays independent of the GUI's internals."""
        hist = getattr(self, "_plot_history", None)
        window = None
        if hist is not None and hist.latest_time():
            t_hi = hist.latest_time()
            # Prefer the window around a trigger, since that is the interesting
            # moment; otherwise the most recent 30 s.
            centre = self.trigger.fired_t
            if centre:
                window = (centre - 15.0, min(t_hi, centre + 15.0))
            else:
                window = (t_hi - 30.0, t_hi)

        params = []
        for name in getattr(self, "_param_order", []):
            p = self.params.get(name)
            if p is not None:
                try:
                    params.append((name, p["value_var"].get()))
                except Exception:
                    pass

        schema = []
        for name in getattr(self, "_telem_order", []):
            try:
                schema.append((name, self.tree.set(name, "type"), 1))
            except Exception:
                pass

        return {
            "version": self._board_version,
            "health": self._health,
            "params": params,
            "schema": schema,
            "events": list(self.events.rows),
            "history": hist,
            "window": window,
            "can_rows": self._sniffer_export_rows(),
            "can_missing": self.candb.missing(self._sniffer_seen_ids()),
            "console": self.log.get("1.0", "end"),
            "notes": self.session_notes.get("1.0", "end").strip(),
            "session": self.recorder.path if self.recorder.active else "",
            "telem_rate": self.telem_rate_var.get(),
            "dropped": self._telem_dropped,
            "trigger_reason": self.trigger.reason,
        }


def _dur(secs):
    if secs is None:
        return "?"
    secs = int(secs)
    return "%d:%02d" % (secs // 60, secs % 60)
