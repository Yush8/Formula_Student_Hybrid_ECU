"""
configgui.candb  --  names for the CAN ids the sniffer sees.

The sniffer shows raw hex ids, which is honest but unreadable. The firmware
already knows the names: CM7/Core/Inc/can1_messages.def and can2_messages.def
list every id the model routes, one `CAN_MSG(SLOT_NAME, 0xID)` per line. The
console lives in the same repo, so it simply READS THOSE FILES - no second table
to maintain, and no way for the GUI's names to drift from the firmware's.

That also gives the sniffer something better than a name: it knows which ids the
firmware EXPECTS. An id arriving that nothing routes is unexplained traffic; an
expected id that stops arriving is a dead node. Both are worth seeing at a
glance, and neither is visible in a plain hex dump.

If the .def files cannot be found (console copied somewhere else), everything
degrades to today's behaviour: raw ids, no labels, no warnings.
"""

import os
import re

# CAN_MSG( SLOT_NAME , 0xID )  - whitespace-tolerant, comments already stripped.
_CAN_MSG_RE = re.compile(
    r"^\s*CAN_MSG\s*\(\s*([A-Za-z_]\w*)\s*,\s*(0[xX][0-9A-Fa-f]+|\d+)\s*\)")

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_DEF_DIR = os.path.join(_REPO, "CM7", "Core", "Inc")

DEF_FILES = {
    1: os.path.join(_DEF_DIR, "can1_messages.def"),
    2: os.path.join(_DEF_DIR, "can2_messages.def"),
}


class CanDb:
    """id -> name, per bus, plus the set of ids the firmware expects."""

    def __init__(self):
        self.names = {1: {}, 2: {}}      # bus -> {id int: slot name}
        self.loaded = False
        self.source = ""

    def load(self):
        """(Re)read both .def files. Cheap and idempotent; safe to call again
        after you edit a .def, so the console picks up a new id without a
        restart."""
        found = []
        for bus, path in DEF_FILES.items():
            table = {}
            try:
                with open(path, encoding="utf-8") as fh:
                    text = fh.read()
            except OSError:
                self.names[bus] = {}
                continue
            for line in _strip_comments(text).splitlines():
                m = _CAN_MSG_RE.match(line)
                if m:
                    table[int(m.group(2), 0)] = m.group(1)
            self.names[bus] = table
            if table:
                found.append("%s (%d ids)" % (os.path.basename(path), len(table)))
        self.loaded = bool(found)
        self.source = ", ".join(found) if found else "no .def files found"
        return self.loaded

    # ---- queries ----
    def name_for(self, bus, can_id):
        """The firmware's slot name for this id, or "" if it routes no such id."""
        try:
            return self.names.get(int(bus), {}).get(int(can_id), "")
        except (TypeError, ValueError):
            return ""

    def is_known(self, bus, can_id):
        return bool(self.name_for(bus, can_id))

    def expected(self, bus):
        """Every id the firmware routes on this bus."""
        return set(self.names.get(int(bus), {}))

    def count(self):
        return sum(len(t) for t in self.names.values())

    def missing(self, seen_by_bus):
        """Expected ids that have NOT been seen. `seen_by_bus` maps bus -> set of
        ids observed. A populated result usually means a node is off or unwired -
        the sniffer alone can never tell you this, because it only shows what
        arrived."""
        out = []
        for bus in (1, 2):
            seen = set(seen_by_bus.get(bus, ()))
            for can_id in sorted(self.expected(bus) - seen):
                out.append((bus, can_id, self.name_for(bus, can_id)))
        return out


def _strip_comments(text):
    """Remove /* block */ and // line comments so a commented-out CAN_MSG line
    is not read as a live one."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text
