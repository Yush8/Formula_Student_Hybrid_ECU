"""
configgui.theme  --  the dark "trackside" look, in one place.

Pure cosmetics: the palette, the fonts, the ttk style setup and the Windows
dark-titlebar hook. Nothing here changes behaviour, so tweak the hex values and
fonts freely. Split out of the old single-file GUI so every tab draws from the
same source of truth (and the new plot tab can colour its series to match).
"""

import tkinter as tk
from tkinter import ttk, font as tkfont

__all__ = [
    "UI", "FONT_UI", "FONT_UI_SM", "FONT_UI_B", "FONT_MONO", "PLOT_COLORS",
    "apply_theme", "enable_dark_titlebar",
]

# ---------------------------------------------------------------------------
# Palette  (cosmetics only - nothing here changes behaviour)
# ---------------------------------------------------------------------------
# A single dark "trackside" palette applied to every widget so the console reads
# as one cohesive dashboard instead of default-grey tkinter. Tweak the hex values
# freely to taste; no logic depends on them.
UI = {
    "page":      "#0d1117",   # window background / gutters between cards
    "card":      "#161b22",   # panels, labelframes, tab bodies
    "field":     "#0d1117",   # inset inputs, tree + console field
    "elev":      "#1c232b",   # raised chips: buttons, headings, banners
    "border":    "#30363d",   # hairline outlines / separators
    "fg":        "#e6edf3",   # primary text
    "muted":     "#8b98a5",   # secondary / hint text
    "accent":    "#3b82f6",   # primary blue
    "accent_hi": "#60a5fa",   # hover / highlighted values
    "accent_ac": "#2563eb",   # pressed
    "green":     "#22c55e",   # go / energised
    "green_hi":  "#16a34a",
    "green_ac":  "#15803d",   # pressed START
    "amber":     "#f59e0b",   # transitional states / reconnecting
    "red":       "#ef4444",
    "red_hi":    "#f87171",   # disconnected text on dark
    "purple":    "#8957e5",   # unknown supervisor state
}

# Compact by design: the point of this console is to see MANY rows at once on a
# laptop, so the base fonts are small and the paddings tight throughout. Bump
# these two points if you ever want it roomier again - everything scales off them.
FONT_UI    = ("Segoe UI", 9)
FONT_UI_SM = ("Segoe UI", 8)
FONT_UI_B  = ("Segoe UI", 9, "bold")
FONT_MONO  = ("Consolas", 9)

# Distinct, dark-theme-legible series colours for the live Plot tab. Assigned to
# signals in this order as they are added, cycling if you plot more than this many
# at once. Chosen to stay separable against the "field" background.
PLOT_COLORS = [
    "#60a5fa",  # blue
    "#22c55e",  # green
    "#f59e0b",  # amber
    "#f472b6",  # pink
    "#a78bfa",  # violet
    "#2dd4bf",  # teal
    "#fb7185",  # rose
    "#facc15",  # yellow
    "#38bdf8",  # sky
    "#c084fc",  # purple
    "#4ade80",  # light green
    "#fca5a5",  # salmon
]


def apply_theme(root):
    """One dark 'trackside' palette applied to every widget so the console reads
    as a single dashboard. Pure cosmetics - no behaviour depends on anything in
    here. Call once, early, on the Tk root."""
    st = ttk.Style()
    try:
        st.theme_use("clam")     # the only built-in theme we can fully recolour
    except tk.TclError:
        pass

    page, card, field = UI["page"], UI["card"], UI["field"]
    elev, border      = UI["elev"], UI["border"]
    fg, muted, accent = UI["fg"], UI["muted"], UI["accent"]

    root.configure(background=page)
    enable_dark_titlebar(root)

    st.configure(".", background=card, foreground=fg, fieldbackground=field,
                 bordercolor=border, lightcolor=card, darkcolor=card,
                 troughcolor=field, focuscolor=elev, font=FONT_UI)

    # Frames: cards are 'card', the window itself + gutters are 'page'.
    st.configure("TFrame", background=card)
    st.configure("Page.TFrame", background=page)
    st.configure("Header.TFrame", background=card)

    # Labels.
    st.configure("TLabel", background=card, foreground=fg, font=FONT_UI)
    st.configure("Muted.TLabel", background=card, foreground=muted, font=FONT_UI_SM)
    st.configure("H1.TLabel", background=card, foreground=fg,
                 font=("Segoe UI", 12, "bold"))
    st.configure("H1Accent.TLabel", background=card, foreground=accent,
                 font=("Segoe UI", 12, "bold"))
    st.configure("H2.TLabel", background=card, foreground=muted, font=FONT_UI_SM)

    # Card panels.
    st.configure("TLabelframe", background=card, bordercolor=border,
                 relief="solid", borderwidth=1)
    st.configure("TLabelframe.Label", background=card, foreground=accent,
                 font=FONT_UI_B)

    # Buttons: readable 'chip' by default, accent variant for primary actions.
    st.configure("TButton", background="#21262d", foreground=fg,
                 bordercolor=border, relief="solid", borderwidth=1,
                 padding=(8, 3), font=FONT_UI, focuscolor="#21262d")
    st.map("TButton",
           background=[("disabled", card), ("pressed", UI["accent_ac"]),
                       ("active", "#2b333d")],
           foreground=[("disabled", muted)],
           bordercolor=[("active", accent), ("focus", accent),
                        ("disabled", border)])

    # Compact variant for the dense per-parameter Set/Get buttons on the Config
    # tab, so each variable row is as short as possible (see the ask: fit many
    # variables on screen at once). Inherits the base button colours.
    st.configure("Compact.TButton", background="#21262d", foreground=fg,
                 bordercolor=border, relief="solid", borderwidth=1,
                 padding=(6, 0), font=FONT_UI_SM, focuscolor="#21262d")
    st.map("Compact.TButton",
           background=[("disabled", card), ("pressed", UI["accent_ac"]),
                       ("active", "#2b333d")],
           foreground=[("disabled", muted)],
           bordercolor=[("active", accent), ("focus", accent),
                        ("disabled", border)])

    st.configure("Accent.TButton", background=accent, foreground="#ffffff",
                 bordercolor=accent, relief="solid", borderwidth=1,
                 focuscolor=accent, font=FONT_UI_B)
    st.map("Accent.TButton",
           background=[("disabled", "#22303f"), ("pressed", UI["accent_ac"]),
                       ("active", UI["accent_hi"])],
           foreground=[("disabled", muted)],
           bordercolor=[("disabled", "#22303f"), ("active", UI["accent_hi"])])

    # Checkbutton.
    st.configure("TCheckbutton", background=card, foreground=fg, focuscolor=card)
    st.map("TCheckbutton",
           background=[("active", card)],
           foreground=[("disabled", muted)],
           indicatorcolor=[("selected", accent), ("!selected", field)])

    # Text entry.
    st.configure("TEntry", fieldbackground=field, foreground=fg,
                 bordercolor=border, insertcolor=fg, padding=2)
    st.map("TEntry", bordercolor=[("focus", accent)],
           fieldbackground=[("disabled", card)])
    # Compact variant for the dense per-parameter "New value" fields.
    st.configure("Compact.TEntry", fieldbackground=field, foreground=fg,
                 bordercolor=border, insertcolor=fg, padding=1)
    st.map("Compact.TEntry", bordercolor=[("focus", accent)],
           fieldbackground=[("disabled", card)])

    # Combobox (+ its popup listbox, which is a classic-tk widget).
    st.configure("TCombobox", fieldbackground=field, background=elev,
                 foreground=fg, arrowcolor=fg, bordercolor=border, padding=2)
    st.map("TCombobox",
           fieldbackground=[("readonly", field), ("disabled", card)],
           foreground=[("disabled", muted)],
           arrowcolor=[("disabled", muted)],
           bordercolor=[("focus", accent), ("active", accent)])
    root.option_add("*TCombobox*Listbox.background", card)
    root.option_add("*TCombobox*Listbox.foreground", fg)
    root.option_add("*TCombobox*Listbox.selectBackground", accent)
    root.option_add("*TCombobox*Listbox.selectForeground", "#ffffff")
    root.option_add("*TCombobox*Listbox.font", FONT_UI)

    # Notebook: selected tab flows into the card body below it.
    st.configure("TNotebook", background=page, bordercolor=border,
                 tabmargins=(4, 6, 4, 0))
    st.configure("TNotebook.Tab", background=page, foreground=muted,
                 padding=(12, 5), font=FONT_UI_B, bordercolor=border)
    st.map("TNotebook.Tab",
           background=[("selected", card)],
           foreground=[("selected", fg), ("active", fg)])

    # Treeview (the live-telemetry grid). rowheight is THE lever for "how many
    # rows fit at once" - derive it from the font so rows are as tight as
    # possible WITHOUT clipping the text at any display scaling (DPI). A fixed
    # pixel height looks fine on one monitor and clips on a scaled laptop.
    row_px = tkfont.Font(font=FONT_UI).metrics("linespace") + 3
    st.configure("Treeview", background=field, fieldbackground=field,
                 foreground=fg, bordercolor=border, borderwidth=0,
                 rowheight=row_px)
    st.configure("Treeview.Heading", background=elev, foreground=muted,
                 relief="flat", font=FONT_UI_B, padding=(6, 3))
    st.map("Treeview",
           background=[("selected", "#1f6feb")],
           foreground=[("selected", "#ffffff")])
    st.map("Treeview.Heading", background=[("active", "#28303a")])

    # Scrollbars + separators.
    for orient in ("Vertical", "Horizontal"):
        st.configure(f"{orient}.TScrollbar", background=elev, troughcolor=page,
                     bordercolor=page, arrowcolor=muted, relief="flat")
        st.map(f"{orient}.TScrollbar", background=[("active", border)])
    st.configure("TSeparator", background=border)


def enable_dark_titlebar(root):
    """Ask Windows (DWM) to paint this window's title bar dark to match the UI.
    Harmless no-op on non-Windows or older builds."""
    try:
        import ctypes
        root.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
        flag = ctypes.c_int(1)
        for attr in (20, 19):   # DWMWA_USE_IMMERSIVE_DARK_MODE (new, then old)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(
                hwnd, attr, ctypes.byref(flag), ctypes.sizeof(flag))
    except Exception:
        pass
