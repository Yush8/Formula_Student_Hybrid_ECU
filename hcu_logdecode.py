#!/usr/bin/env python3
"""
hcu_logdecode.py  --  decode HCU SD-card log files (LOGxxxx.BIN) to CSV.

The firmware writes fixed-size binary records to the card. The record layout is
defined in ONE place - Shared/log_signals.def - which this script reads too, so
the decoder always matches the firmware with no separate config to keep in sync.
(Same single-source-of-truth idea as the CAN .def lists.)

Usage:
    python hcu_logdecode.py LOG0000.BIN                 # -> LOG0000.csv
    python hcu_logdecode.py LOG0000.BIN -o out.csv      # explicit output
    python hcu_logdecode.py *.BIN                       # one .csv per input

No third-party packages - just the Python standard library.
"""

import os
import re
import sys
import csv
import struct
import argparse

# ---- C type  ->  (struct format char, size) -------------------------------
# Mirrors the <type> column in log_signals.def. Little-endian, packed.
_CTYPES = {
    "uint8_t":  ("B", 1), "int8_t":  ("b", 1),
    "uint16_t": ("H", 2), "int16_t": ("h", 2),
    "uint32_t": ("I", 4), "int32_t": ("i", 4),
    "uint64_t": ("Q", 8), "int64_t": ("q", 8),
    "float":    ("f", 4), "double":  ("d", 8),
}

# On-disk file header (sdlog_header_t in CM4 sd_logger.c). 16 bytes, little-endian.
_HDR_FMT = "<IHHHBBBBBB"          # magic, version, record_size, year, mon,day,hr,min,sec,pad
_HDR_SIZE = struct.calcsize(_HDR_FMT)
_FILE_MAGIC = 0x474C4448          # 'HDLG'

# A LOG_Y(...) / LOG_U(...) line:  capture the <type> and the <PortName>.
_SIG_RE = re.compile(r'^\s*LOG_[YU]\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)')


def find_def(explicit=None):
    """Locate log_signals.def (next to this script's Shared/ folder by default)."""
    if explicit:
        return explicit
    here = os.path.dirname(os.path.abspath(__file__))
    cand = os.path.join(here, "Shared", "log_signals.def")
    if not os.path.isfile(cand):
        sys.exit(f"could not find log_signals.def (looked in {cand}); pass --def <path>")
    return cand


def parse_def(path):
    """Return (record_fmt, field_names) from log_signals.def. tick is implicit."""
    fmt = "<I"                    # leading uint32 tick (always first)
    names = ["tick"]
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = _SIG_RE.match(line)
            if not m:
                continue          # comments / blank / anything else -> ignored
            ctype, name = m.group(1), m.group(2)
            if ctype not in _CTYPES:
                sys.exit(f"{path}: unknown type '{ctype}' for signal '{name}'")
            fmt += _CTYPES[ctype][0]
            names.append(name)
    return fmt, names


def decode_file(bin_path, fmt, names, out_path):
    rec_size = struct.calcsize(fmt)
    with open(bin_path, "rb") as f:
        raw_hdr = f.read(_HDR_SIZE)
        if len(raw_hdr) < _HDR_SIZE:
            sys.exit(f"{bin_path}: too short to be a log file")
        magic, version, hdr_rec, year, mon, day, hr, mi, se, _pad = struct.unpack(_HDR_FMT, raw_hdr)
        if magic != _FILE_MAGIC:
            sys.exit(f"{bin_path}: bad magic 0x{magic:08X} (not an HCU log file)")
        if hdr_rec != rec_size:
            print(f"WARNING: {bin_path} record_size={hdr_rec} but log_signals.def "
                  f"gives {rec_size}. The .def likely changed since this log was "
                  f"recorded - decode may be wrong. Use the matching .def.",
                  file=sys.stderr)

        print(f"{bin_path}: v{version}, recorded "
              f"{year:04d}-{mon:02d}-{day:02d} {hr:02d}:{mi:02d}:{se:02d}, "
              f"{rec_size} B/record")

        n = 0
        with open(out_path, "w", newline="", encoding="utf-8") as out:
            w = csv.writer(out)
            w.writerow(names)
            while True:
                chunk = f.read(rec_size)
                if len(chunk) < rec_size:
                    break         # trailing partial record (power-loss tail) -> stop
                w.writerow(struct.unpack(fmt, chunk))
                n += 1
        print(f"  -> {out_path}  ({n} records)")


def main():
    ap = argparse.ArgumentParser(description="Decode HCU LOGxxxx.BIN files to CSV.")
    ap.add_argument("files", nargs="+", help="LOGxxxx.BIN file(s)")
    ap.add_argument("-o", "--out", help="output CSV (only with a single input)")
    ap.add_argument("--def", dest="defpath", help="path to log_signals.def")
    args = ap.parse_args()

    fmt, names = parse_def(find_def(args.defpath))
    print(f"layout: {len(names)} columns {names} ({struct.calcsize(fmt)} B/record)")

    if args.out and len(args.files) != 1:
        sys.exit("-o/--out only makes sense with a single input file")

    for path in args.files:
        out = args.out or (os.path.splitext(path)[0] + ".csv")
        decode_file(path, fmt, names, out)


if __name__ == "__main__":
    main()
