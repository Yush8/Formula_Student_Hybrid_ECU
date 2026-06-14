# Formula_Student_Hybrid_ECU

HCU_V2 — dual-core STM32H745 firmware for the Formula Student hybrid control unit.
CubeMX + STM32CubeIDE + Simulink (Embedded Coder) + Claude. Simulink owns all the
logic; the C is a thin "postman" bridge between the hardware and the model.

## Docs
- **[EXTENDING_THE_HCU.md](EXTENDING_THE_HCU.md)** — the Simulink-only guide: how to
  add a tunable parameter, a CAN message, a logged signal or an I/O pin, all as
  one `.def` line. **Start here to add anything.**
- **[HANDOFF.md](HANDOFF.md)** — architecture & conventions (for a deep dive / a new chat).
- **[DEBUG_DUALCORE.md](DEBUG_DUALCORE.md)** — bench flash/debug procedure for the two cores.

## Tools (off-target, on your PC)
- `ConfigGUI.py` — USB config console GUI. Auto-discovers the board's parameters,
  set the clock, save/defaults, auto-reconnect. `pip install pyserial; python ConfigGUI.py`.
- `hcu_logdecode.py` — decode SD-card `LOGxxxx.BIN` logs to CSV.
