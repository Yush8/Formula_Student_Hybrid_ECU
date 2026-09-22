# Read this first (orientation for an AI working on the HCU)

You are working on **HCU_V2** — the firmware for a Formula Student hybrid control
unit. Read this page, then the one or two docs it points you at. Do not start by
grepping the tree; the map below is faster and will stop you breaking things.

---

## 1. The one rule that governs everything

> **Simulink owns ALL logic. Hand-written C is a thin "postman" that carries raw
> values between the hardware and the model. C never decodes, scales, or makes a
> vehicle decision.**

Ownership is split by person, and this is not negotiable:

| Layer | Owner | Meaning for you |
|---|---|---|
| `HCU_V2_Simulink.slx` — all control logic, state machines, maths | **Yusha (the user)** | You may *read* the generated code to verify an interface. You do not design vehicle logic; you propose it and he builds it in Simulink. |
| `CM7/Core/*`, `CM4/Core/*` — the C firmware | **You (the AI)** | Yusha does not want to write or repeatedly touch C. It must be set-and-forget. |
| `HCU_V2.ioc` — CubeMX pins/peripherals/clocks | **Yusha, in CubeMX** | Never hand-edit generated init code. If something needs a pin or peripheral change, say so and let him regenerate. A regen has a known failure mode — see [docs/TRAPS.md](docs/TRAPS.md) §1 before he runs one. |

The practical consequence: **when a feature could live in C or in Simulink, it
goes in Simulink.** C's job is to make the edit surface for a new signal exactly
one line.

## 2. The `.def` pattern — how anything gets added

Five files are the entire user-facing edit surface. Each is an X-macro list; the
struct, the table, the console, the GUI, the flash layout and the Python decoder
are all *generated* from it, so they cannot drift out of sync.

| To add… | Edit this one file | Rebuild |
|---|---|---|
| a tunable parameter | `CM7/Core/Inc/params.def` | CM7 |
| a received CAN message | `CM7/Core/Inc/can1_messages.def` / `can2_messages.def` | CM7 |
| a signal logged to SD | `Shared/log_signals.def` | **both cores** |
| a signal watched live on the PC | `CM7/Core/Inc/telem_signals.def` | CM7 |
| an I/O pin / sensor | one line in `model_bridge.c` + a Simulink port | CM7 |

**If a change you are about to make adds a hand-maintained list that must match
another hand-maintained list, stop — you are breaking the pattern.** Generate it
from a `.def` instead. This rule exists because an earlier hand-kept CAN enum
drifted and caused an out-of-bounds write.

## 3. Where everything is

```
HCU_V2/
├── AGENTS.md            <- you are here   (CLAUDE.md just points at this)
├── README.md            the human-facing front page
├── docs/                ALL project documentation (see section 4)
├── HCU_V2.ioc           CubeMX project — Yusha's, edited only in CubeMX
├── HCU_V2_Simulink.slx  the model — Yusha's
│
├── Shared/              included by BOTH cores
│   ├── hcu_ipc.h          the CM7<->CM4 contract (ring + record + 0x38000000)
│   └── log_signals.def    what gets written to the SD card
│
├── CM7/                 THE CONTROL CORE (almost all work happens here)
│   ├── Core/Inc/*.def     the edit surface (params, can1, can2, telem)
│   ├── Core/Src/
│   │   ├── main.c           CubeMX-generated; hand code ONLY in USER CODE markers
│   │   ├── model_bridge.c   THE BRIDGE: gather -> step -> dispatch. Start here.
│   │   ├── scheduler.c      TIM6 100 Hz time base + the ISR that runs the fail-safe
│   │   ├── air_safety.c     independent controller-freeze fail-safe (safety-critical)
│   │   ├── can.c            dual-FDCAN demux/tx
│   │   ├── can_sniffer.c    raw all-id bus observer (diagnostic)
│   │   ├── params.c         tunable store + flash blob
│   │   ├── console.c        USB-CDC text console
│   │   ├── telem.c          live signal stream to the GUI
│   │   ├── logger.c         SD-log producer (writes the shared ring)
│   │   └── clock.c          RTC + publishes wall-clock to CM4
│   └── Model/HCU_V2_Simulink_ert_rtw/   GENERATED. Never hand-edit.
│
├── CM4/                 THE LOGGING CORE (SD card only)
│   └── Core/Src/sd_logger.c  drains the ring -> FatFs -> LOGxxxx.BIN
│
├── ConfigGUI.py         launcher for the PC-side GUI
├── configgui/           the GUI package (one module per tab)
└── hcu_logdecode.py     LOGxxxx.BIN -> CSV
```

## 4. Which doc to read for what

| Question | Doc |
|---|---|
| How does this firmware work? What is where and why? | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Why does the car behave this way? Torque, regen, TV, limits, safety gates | [docs/CONTROL_STRATEGY.md](docs/CONTROL_STRATEGY.md) |
| What is done, what is open, what is next? | [docs/STATUS.md](docs/STATUS.md) |
| How do I add a parameter / CAN id / logged signal? (user-facing) | [docs/EXTENDING.md](docs/EXTENDING.md) |
| How do I build, flash, debug two cores, and triage on the bench? | [docs/BENCH.md](docs/BENCH.md) |
| It broke and the error makes no sense | [docs/TRAPS.md](docs/TRAPS.md) |

**Read `docs/STATUS.md` before proposing work.** It is the single ledger of what
is done vs open; everything else describes the design, not the progress.

## 5. How to run the thing

**Firmware** — STM32CubeIDE 2.1.1, two sub-projects (`HCU_V2_CM7`, `HCU_V2_CM4`)
sharing one `.ioc`. You cannot build from this shell; builds are Yusha's, in the
IDE. Rebuild rules: touched `log_signals.def` ⇒ **both cores**; anything else
⇒ CM7 only. Flash/debug order for two cores is non-obvious and will orphan the
M4 if you get it wrong — [docs/BENCH.md](docs/BENCH.md) section 2.

**PC tools** (these you *can* run):

```bash
pip install pyserial
```

```bash
python ConfigGUI.py
```

```bash
python hcu_logdecode.py LOG0000.BIN
```

`matplotlib` is optional and only enables the Plot tab. The board is one USB-CDC
virtual COM port. Only one program may own it — close PuTTY before the GUI and
vice-versa. Every GUI action is also a typed command (`list`, `set kp 2`, `save`,
`telem on`, `cansniff on`, `stats`, `safety`, …).

## 6. Rules that will save you an afternoon

1. **newlib-nano `printf` has no `%f`.** Format floats by hand (`f32_to_str` in
   `params.c`, `fmt_real` in `telem.c`). A `%f` prints nothing and silently eats
   the rest of the line.
2. **`CDC_Transmit_FS` does not copy its buffer** — the USB endpoint reads it
   *after* you return. Any buffer handed to `Console_Out`/`CDC_Transmit_FS` must
   be `static`, never a local.
3. **Safety trips are `const` in firmware, never in `params.def`.** No console
   `set` and no edited flash image may move a trip threshold (`AIR_STALL_TRIP_MS`,
   the torque slew rate, the model's staleness windows).
4. **Debug-only globals are never fed to the model.** `g_can_stats`,
   `g_sched_overruns`, `g_air_safety`, `g_log_stats`, `g_sdlog_status` are for
   the debugger and `stats`. The model gets `_age` and `bus_ok` and nothing else.
5. **Never reference a Simulink root port from C before the port exists.**
   Embedded Coder prunes unconnected inports, so the build fails at link. That is
   what every `*_FEED_READY` macro in `model_bridge.c` is for — see
   [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) section 3.
6. **Hand code in a CubeMX file survives only inside `/* USER CODE */` markers.**
7. **Do not propose an IWDG kick, an SDC check in C, or a CAN acceptance filter
   without reading why they were deliberately excluded** — ARCHITECTURE sections
   5 and 8. These are decisions, not omissions.

## 7. Working style Yusha expects

- One foolproof edit point beats clever code. Hidden machinery is fine *if* the
  surface stays one line.
- He brings up hardware with the debugger and the GUI — watch variables, `stats`,
  live telemetry — not by adding printf.
- He wants the *reasoning* recorded, not just the change. When a decision is
  made it belongs in `docs/`, not only in a commit message.
- Terse, dense UI. The GUI should show many rows at once; small fonts are fine.
