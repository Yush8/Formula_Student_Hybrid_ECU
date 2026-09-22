# Formula Student Hybrid ECU — HCU V2

Dual-core STM32H745 firmware for the Formula Student hybrid control unit.
CubeMX + STM32CubeIDE + Simulink (Embedded Coder).

**Simulink owns all the logic. The C is a thin "postman" bridge between the
hardware and the model.** CM7 runs the control loop at a hard 100 Hz; CM4 does
nothing but SD logging, so a card stall can never cost a control tick.

---

## Start here

| I want to… | Read |
|---|---|
| **Add a parameter, CAN id, logged signal or I/O pin** | **[docs/EXTENDING.md](docs/EXTENDING.md)** — everything is one `.def` line |
| Understand how the firmware works | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Understand why the car behaves as it does | [docs/CONTROL_STRATEGY.md](docs/CONTROL_STRATEGY.md) |
| Know what is done and what is open | [docs/STATUS.md](docs/STATUS.md) |
| Build, flash, debug two cores, triage | [docs/BENCH.md](docs/BENCH.md) |
| Work out why something broke | [docs/TRAPS.md](docs/TRAPS.md) |
| Hand the project to an AI assistant | [AGENTS.md](AGENTS.md) |

---

## The edit surface

Adding anything to this firmware is **one line in one file**. The struct, the
table, the console, the GUI, the flash layout and the log decoder are all
generated from that line, so they cannot drift out of sync.

| To add | Edit | Rebuild |
|---|---|---|
| a tunable parameter | `CM7/Core/Inc/params.def` | CM7 |
| a received CAN message | `CM7/Core/Inc/can1_messages.def` / `can2_messages.def` | CM7 |
| a signal logged to SD | `Shared/log_signals.def` | **both cores** |
| a signal watched live | `CM7/Core/Inc/telem_signals.def` | CM7 |

---

## PC tools

```bash
pip install pyserial
```

```bash
python ConfigGUI.py
```

The trackside GUI: **Live Telemetry** (watch every model signal update),
**Plot** (live strip charts, needs `matplotlib`), **CAN Bus** (raw all-id
sniffer), **Config** (auto-discovered tunables), **Console** (raw commands,
`stats`). Sets the clock, saves to flash, auto-reconnects. Nothing in it needs
editing when you add a parameter or a signal — it discovers them from the board.

```bash
python hcu_logdecode.py LOG0000.BIN
```

Decodes an SD-card log to CSV, reading the same `log_signals.def` the firmware
was built from.

> Only one program can own the COM port — close PuTTY before the GUI, and
> vice-versa. Every GUI action is also a typed command.

---

## Layout

```
CM7/      control core — the bridge, CAN, params, console, telemetry, safety
CM4/      logging core — drains the shared ring to the SD card
Shared/   the inter-core contract + what gets logged
configgui/ the PC-side GUI package (ConfigGUI.py is just a launcher)
docs/     all documentation
```
