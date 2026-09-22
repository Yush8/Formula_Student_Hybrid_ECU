"""
configgui  --  the HCU V2 trackside console GUI, as a package.

Split out of the old single-file ConfigGUI.py so each concern lives in its own
module (theme, protocol, the tab mixins, the live-plot widget). Run it with the
thin launcher at the repo root:

    python ConfigGUI.py

or as a module:

    python -m configgui
"""

from .app import ConsoleApp, main

__all__ = ["ConsoleApp", "main"]
