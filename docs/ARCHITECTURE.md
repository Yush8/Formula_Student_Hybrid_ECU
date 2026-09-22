# HCU V2 — Architecture

How the firmware is built and *why*. This is the engineering reference: it
describes the design, not the progress. For progress see
[STATUS.md](STATUS.md); for the vehicle logic see
[CONTROL_STRATEGY.md](CONTROL_STRATEGY.md); for "how do I add a thing" see
[EXTENDING.md](EXTENDING.md).

---

## 1. Target and toolchain

| | |
|---|---|
| MCU | STM32H745ZIT3 — dual core (Cortex-M7 + Cortex-M4), 2 MB **dual-bank** flash |
| Actual CPU clock | **75 MHz** (HSE 25 MHz ÷ M4 × N24 ÷ P2). Voltage scale **VOS2**, whose ceiling is ~300 MHz — we are far under it on purpose; a 100 Hz control loop needs nothing more, and low clock = low noise and power. |
| HSE | 25 MHz **BYPASS** (MEMS oscillator, not a crystal) |
| LSE | 32.768 kHz crystal → RTC. **No VBAT backup fitted**, so the RTC resets every power cycle. |
| Power supply | **SMPS 1.8 V supplies LDO**. Anything else bricks boot. |
| USB | USB_OTG_FS device, clocked from **HSI48 + CRS**, independent of the PLL tree. Do not retune PLLs for USB. |
| FDCAN kernel clock | HSE, 25 MHz |
| Toolchain | STM32CubeIDE 2.1.1 + standalone CubeMX; two sub-projects share one `.ioc` |
| Model | MATLAB/Simulink + Embedded Coder, target `ert.tlc` |

**Core split.** CM7 runs the entire control loop. CM4 does **nothing but SD
logging** — see section 9. The HSEM boot handshake at the top of both `main.c`
files releases CM4 and must never be removed.

Peripherals are assigned to a *core context* in CubeMX. Control peripherals
(FDCAN1/2, TIM6, RTC, SPI3, ADC1, USART3, USB) are on the **CM7 context**;
SDMMC2 + FatFs are on the **CM4 context**. A pin or peripheral with no context
assignment generates no init code at all — that is the usual reason something
"disappeared" after a regen.

### `.ioc` settings that must not break

Verify these after every CubeMX *Generate Code*:

- Power supply = `PWR_SMPS_1V8_SUPPLIES_LDO`
- Voltage scaling = **VOS2**; HSE 25 MHz **BYPASS**; LSE 32.768 kHz crystal
- USB 48 MHz from **HSI48 + CRS** (RC48 in the clock tree, CRS sync = USB_FS)
- FDCAN kernel clock = **HSE**; FDCAN **AutoRetransmission = DISABLED** on both
  (control data must be fresh — a stale retransmitted frame is worse than none)
- TIM6 on the CM7 context: prescaler **74**, period **9999**, NVIC preemption **5**
- FDCAN1/2 IT0 NVIC preemption **0** (CAN Rx must pre-empt the control tick)

---

## 2. The philosophy, stated precisely

**CubeMX owns the project, the peripherals and `main.c`. Simulink owns all
logic. Hand-written C is a bridge.** It reads raw inputs into the model, steps
the model, and pushes raw outputs to hardware. It does not decode, scale, filter
or decide.

> *C is the postman; Simulink is the brain.*

Two consequences worth internalising:

1. **Hardware reality lives in C; meaning lives in Simulink.** Which pin, which
   polarity, which CAN id — C. What a HIGH on that pin *means* — Simulink. So a
   button's raw electrical level is passed to the model (`TRUE` = pin HIGH), and
   an active-low LED is inverted in C (`Y.led ? RESET : SET`).
2. **Bytes are not decoded in C.** A received CAN frame reaches the model as
   eight raw bytes plus an age; the unpack and scaling are visible Simulink
   blocks the team can see and tune. This is also why there is no DBC — see
   section 8.

---

## 3. The model interface and the bridge

Embedded Coder exposes three doors:

- **`HCU_V2_Simulink_U`** — one named field per root **Inport**. The bridge fills it.
- **`HCU_V2_Simulink_Y`** — one named field per root **Outport**. The bridge dispatches it.
- **Parameters** — deliberately *not* Simulink "parameters". Tunables live in
  firmware (`g_params`) and are copied into `U` each step as ordinary inports, so
  one struct serves the model, the console, the GUI and flash.

`HCU_V2_Simulink_initialize()` runs once at boot; `HCU_V2_Simulink_step()` runs
once per tick.

### `Model_Step()` = gather → step → dispatch

`CM7/Core/Src/model_bridge.c` is the only file that includes both `main.h` and
`HCU_V2_Simulink.h`. Every tick it does, in order:

1. **Gather** — `Can_Snapshot()`, read pins, copy `g_params` into `U`
2. **Step** — `HCU_V2_Simulink_step()`
3. **Dispatch** — write pins, hand the AIR request to the fail-safe, transmit CAN
4. **Log** — pack a `log_record_t` and drop it in the CM4 ring

The four one-line macros that keep the bridge a bridge:

| Macro | Does | You add |
|---|---|---|
| `CAN_FEED(busN, SLOT, port)` | copies 8 data bytes + `port_age` into `U` | 1 line per received message |
| `MODEL_PARAM(name)` | `U.name = g_params.name` | 1 line per tunable the model reads |
| `CAN_TX_GATED(bus, id, port)` | transmits `Y.port` only while `Y.port_req` is true | 1 line per one-shot command |
| `Can_Send(bus, id, data, len)` | transmits unconditionally | 1 line per continuous setpoint |

`MODEL_PARAM` is deliberately **not** auto-generated from `params.def`. Yusha
wants to control exactly which tunables reach the model; a firmware-only tunable
(a log rate, say) simply gets no line.

### The `*_FEED_READY` guards — why they exist

Embedded Coder **prunes unconnected root inports**. So if C references
`U.Bench_Speed_Bypass` before the Simulink inport exists *and is used*, the CM7
build fails at link with an undefined reference. Each feature that spans C and
Simulink therefore sits behind a compile-time flag at the top of
`model_bridge.c`:

```c
#define SPEED_GATE_FEED_READY   1     /* 0 = model has no such port yet */
```

The rule for each flag: **0 while the model lacks the port, 1 once the port
exists and is wired.** Each flag's block comment states exactly which ports the
model must have and what breaks if the flag is wrong. Current values and what
each one controls are listed in [CONTROL_STRATEGY.md](CONTROL_STRATEGY.md).

> One trap is worth repeating: leaving a flag at 0 while the ports *do* exist is
> not harmless. The inports then default to 0, and for the torque slew that means
> a rate limit of 0 — which freezes torque at 0. Fail-safe, but the car makes no
> torque and nothing says why.

### Adding a signal

1. Draw the Inport/Outport in Simulink, name it, set the type, regenerate.
2. One bridge line: `U.x = <read hw>;` or `<write hw> = Y.x;`

Same name in Simulink and C, always. Cost of a new signal = one port + one line.

---

## 4. Time base — the scheduler

`scheduler.c/.h`. A deterministic metronome, not a `HAL_GetTick` rate limiter.

**Ownership split:** CubeMX owns TIM6 (clock, prescaler, period, NVIC). The
scheduler owns only the policy. Simulink owns the maths.

TIM6 overflows at the model base rate. Its update ISR does two things and
nothing else: increment `g_sched_ticks`, and call `AirSafety_Supervise()`. The
superloop calls `Sched_StepDue()` each pass and runs **one** `Model_Step()` per
elapsed tick.

TIM6 math: APB1 37.5 MHz × 2 = 75 MHz timer clock; ÷75 (prescaler 74) = 1 µs;
÷10000 (period 9999) = **100 Hz = 10.000 ms exactly**.

**Run-latest, never catch-up.** If the loop falls behind, `Sched_StepDue()`
returns true once and adds the skipped ticks to `g_sched_overruns`. It never
bursts steps to catch up — that would compress time, which is wrong for control.
`g_sched_overruns == 0` means every deadline was met.

**The rate is stated in three places and they must agree:** the Simulink
fixed-step (0.01 s), the CubeMX TIM6 period, and `SCHED_RATE_HZ` in
`scheduler.h`. Changing the base rate is rare; day to day you touch none of them.

---

## 5. Safety — the independent controller-freeze fail-safe

`air_safety.c/.h`. Safety-critical. Read this section before changing anything
in it.

### The one job

If `HCU_V2_Simulink_step()` hangs with `AIR_Enable` stuck closed, the sinks stay
driven and the AIRs stay shut with nobody in control — and **the model cannot
detect its own death**. Only a watchdog outside the model can. That is the whole
purpose.

### What it deliberately does NOT do

It was trimmed to the freeze case only. An earlier draft also policed the
shutdown circuit; that was removed, for three reasons:

- A real SDC open **de-energises the AIR coils directly in hardware**
  (rules-mandated). The HCU sink is only series *operational* control and
  precharge sequencing. A C "force open on SDC open" acts after the hardware has
  already opened the AIR — redundant.
- The SDC line is *information* ("the whole shutdown chain is healthy, including
  the BMS discharge-OK relay"). Acting on it — zero torque, open AIR, re-run
  precharge on re-close, fail safe on a dead pack — is vehicle logic with
  context, so it lives in **Simulink**, which already reads `SDC_Monitor`.
- CAN-loss safing likewise stays in the model; it has `bus1_ok`/`bus2_ok` and
  every `_age`.

So the C layer owns exactly the one thing neither the model nor the hardware can
do for itself.

### A veto, never an actuator

```
final_air_closed = model_requests_closed AND armed AND model_alive AND NOT stall_latched
```

It can only ever *subtract* authority. It can never close a relay the model did
not ask for. A bug in here is at worst a nuisance open (safe); it can never weld
the tractive system live. That is why it owns the pins outright: `air_safety.c`
is the **sole writer** of `AIR_Sink` / `Precharge_Sink`. The bridge no longer
writes them — it calls `AirSafety_SetRequest(AIR_Enable, Pre_Charge_Enable)`,
which hands over the request *and* stamps a proof-of-life heartbeat.

### Where it runs

`AirSafety_Supervise()` is called from the **TIM6 update ISR**. The ISR
pre-empts the cooperative superloop, so an infinite loop in the model or a hung
console cannot stop it yanking the AIRs. It is ISR-safe by construction: GPIO
writes (`BSRR`, atomic) and integer compares only. No HAL blocking, no console.

### The trip

The heartbeat must advance within **`AIR_STALL_TRIP_MS` = 500 ms** (`const`,
never a parameter). If it does not, the supervisor declares a freeze, forces both
sinks open and **latches** until power-cycle or a stationary `safety reset`.

*Why 500 ms:* comfortably longer than any legitimate cooperative stall (a
no-host console dump can block the loop a few hundred ms), yet safe — a frozen
model stops the ODrive setpoint stream, and the ODrive rx-watchdog already zeroes
torque ~100 ms in. Propulsion is gone long before this fires. **The latch is the
final isolation backstop, not the torque cut.** A single missed tick never trips
it; the heartbeat only has to advance once per window.

*Why latched:* a model that froze and then resumed is in an unknown state and
must not silently re-close the AIRs.

*Pre-arm:* before the first model step the supervisor holds open with no stall
evaluation, so boot cannot false-trip.

**Polarity:** `AIR_Sink` / `Precharge_Sink` are low-side. HIGH = energised =
closed. The GPIO reset/boot default is LOW = open = safe.

### What it cannot catch

If the CPU, the clock or TIM6 itself dies, the supervisor dies with it and the
sink pins hold their last state. **Only the external STWD100** (on the `Watchdog`
pin, PA3) catches a total lockup. The kick hook is built in
(`AIR_SAFETY_EXT_WDT`, default **0** = off): when enabled it toggles PA3 every
`AIR_WDT_KICK_MS` **only while the loop is provably alive**. Stop kicking on a
freeze → the STWD100 times out → hardware drops the AIRs. Fit the 0 Ω link and
enable it *after* confirming the chip's timeout window — too slow a kick resets a
healthy MCU, which is the nuisance we are avoiding.

### Operator surface

- **`g_air_safety`** (volatile global; watch it like `g_can_stats`):
  `model_alive`, `armed`, `stall_latched`, `air_closed`, `pre_closed`,
  `stall_age_ticks`, `stall_trips`, `supervise_calls` (climbing ⇒ supervisor
  live). Not fed to the model.
- **LEDs:** `AIR_LED` / `Precharge_LED` lit = that sink is closed.
  `SDC_Monitor_LED` mirrors the SDC line directly in C, so it stays live even if
  the model freezes.
- **Console:** `safety`, `safety reset` (refused unless the loop is alive again),
  and an `AIR` line in `stats`.

---

## 6. Tunable parameters and flash

`params.c/.h` + `params.def`. One line in `params.def` generates the struct
field, the default, the safe range, the console commands, the GUI row and the
flash layout.

```c
PARAM_F32( name, default, min, max )   /* Simulink single */
PARAM_I32( name, default, min, max )   /* Simulink int32, or a boolean as 0/1 */
PARAM_SECTION( "Heading" )             /* GUI grouping marker — presentation only */
```

`PARAM_F32` picks the `float` field *and* the `P_F32` table tag together, so the
old "field size must match the tag" footgun cannot happen.

**`PARAM_SECTION` is presentation only.** The firmware emits `# section: <name>`
lines interleaved into `list`, and the GUI groups by them in `params.def` order —
so re-grouping never touches storage, flash or the layout fingerprint, and never
needs a Python edit.

### The flash blob

One packed `ConfigBlob_t = { magic, version, layout_id, params, crc }`.

| Field | Catches |
|---|---|
| `magic` | blank or junk flash |
| `version` | a *semantic* change; bumped by hand, rarely |
| `layout_id` | an FNV-1a hash of every parameter's name + type. **Changing `params.def` changes it**, so stale flash of a different shape is auto-rejected → safe defaults. This is why you never hand-bump a version for adding a parameter. |
| `crc` | CRC32 over everything before it — corruption and half-writes |

**Location: bank 2, last sector (`0x081E0000`).** CM7 runs from bank 1, and the
H7 only supports read-while-write *across* banks, so an erase here does not
stall the CM7 control loop.

> ⚠ **But CM4 executes from bank 2.** During a `Params_Save()` sector erase,
> CM4's instruction fetches stall for the duration of the erase — the logger
> freezes and the ring can overflow. This is acceptable because `save` is a
> stationary / pit action, but it is a real coupling and should stay documented.
> See [STATUS.md](STATUS.md) for the open mitigation.

**Load policy:** read → check magic, version, `layout_id`, CRC → load if all
pass, else compiled-in defaults; **always clamp afterward**. Any doubt → safe
defaults.

**Save policy:** build blob, CRC, erase then program. Single copy, so a power
loss mid-save reverts to defaults on the next boot — which is safe. Treat `save`
as a pit action.

Do **not** call `SCB_InvalidateDCache_by_Addr` after the write; it faulted on
this board's MPU and is not needed (config is read only at boot, cold cache).

**Safety-critical limits do not belong in `params.def`** — keep them `const` in
firmware so no console `set` and no edited flash image can move them.

---

## 7. The console, the GUI, and live telemetry

### The console

Line-based text over USB-CDC (USB_OTG_FS device, behind an ADuM4160 isolator;
DP/DM = PA12/PA11; hardware VBUS sensing disabled, PA9 is a plain GPIO reading
isolated VBUS). The firmware echoes characters, so turn PuTTY local echo off.

```
help  list  get <name>  set <name> <val>  save  defaults
time  time set YYYY-MM-DD HH:MM:SS
stats  stats clear      safety  safety reset
telem [on|off|rate <hz>|list]
cansniff [on|off|rate <hz>|clear|list]
canreg   ping
```

`console.c` is a thin UI over `params.c` — it iterates the generated table, so
a new `params.def` line appears with **no console edit**.

### The GUI (`ConfigGUI.py` + `configgui/`)

Drives that exact protocol over pyserial. `ConfigGUI.py` is a thin launcher; the
app lives in the `configgui/` package, one module per concern:

| Module | Concern |
|---|---|
| `theme.py` | dark palette, fonts, ttk styling |
| `protocol.py` | line regexes + the state / fault / grouping tables |
| `serial_io.py` | the serial link and the RX line dispatcher |
| `chrome.py` | header, bars, board actions, START / CLEAR FAULT |
| `params_tab.py` | Config tab |
| `telem_tab.py` | Live Telemetry tab |
| `plot_tab.py` + `plot_widget.py` | live strip charts (needs matplotlib) |
| `sniffer_tab.py` | CAN Bus tab |
| `app.py` | `ConsoleApp` — assembles the mixins. Composition only, no logic. |

**The GUI is self-discovering by design.** Parameters come from parsing `list`
(including the `# section:` headers), telemetry rows from `telem list` plus the
stream itself, CAN ids from the sniffer. **Adding a parameter or a signal needs
no Python edit, ever.** The only hand-kept tables in `protocol.py` are
presentation: `SUPERVISOR_STATES` (State_Enum → name + colour), `FAULT_CODES`
(Fault_Code → phrase), `DRIVE_MODES`, `HV_ENABLE_LAMPS` and the grouping
helpers. An unlisted value degrades gracefully ("STATE 7" in purple) rather than
breaking.

Two GUI controls are momentary *pulses* of a boolean parameter, so they behave
like a physical button press and can never stick on: **START** pulses
`Start_Button_GUI`, **CLEAR FAULT** pulses `Error_Reset_GUI` *and* issues
`safety reset`, so one press recovers whichever of the two latches tripped.

### Live telemetry (`telem.c` + `telem_signals.def`)

A human-readable stream of model signals over the same console. **Separate from
SD logging** — different file, different purpose: telemetry = watch live,
logging = write to card. A signal can be in either, both, or neither.

- **No type token in the `.def`.** The formatter and the schema label are chosen
  with C11 `_Generic` straight from the field's type in `HCU_V2_Simulink_{U,Y}`,
  so a wrong type is impossible. (`boolean_T` is `uint8_T`, so booleans print as
  0/1 — fine.)
- **`telem.c` is a read-only observer.** It is the only file besides the bridge
  that includes `HCU_V2_Simulink.h`; it changes nothing, just formats.
- **Driven from the superloop in slack time**, never from `Model_Step()` — so a
  slow or absent host can never cost a control tick. Off by default at boot.
- Frame format, one line: `#T tick=1234 User_LED_1=1 Torque_Scale_Factor=0.8571 APPS=12,0,255,...`
  Named, so the GUI self-discovers rows from the stream alone.
- The frame buffer is **`static`** because `CDC_Transmit_FS` does not copy — the
  USB endpoint reads the buffer after the call returns.

### CAN sniffer (`can_sniffer.c`)

A raw, all-id observer: every id the board receives on either bus, including ids
that are *not* in the `.def` lists, with the latest 8 bytes, a frame count, a
measured rate and an age. Captured in the Rx ISR, so counts are exact regardless
of the snapshot rate. Read-only — it never transmits, and it is completely
independent of the model demux. Only standard (11-bit) frames appear, because
the global filter rejects extended ids.

Stream row: `#C <bus> <id> <count> <age_ms> <dlc> <data>`.

---

## 8. CAN

Two FDCAN controllers, **classic CAN** (not FD), isolated transceivers.

| Logical bus | Peripheral | Role | Bitrate |
|---|---|---|---|
| bus 1 | FDCAN1 (PD0/PD1) | dash / ECU | **1 Mbit/s** |
| bus 2 | FDCAN2 (PB5/PB6) | tractive system / inverters | **500 kbit/s** |

The logical↔physical mapping is **two `#define`s at the top of `can.c`**
(`BUS1_HANDLE` / `BUS2_HANDLE`). Everything else in the firmware speaks in
logical bus 1 / bus 2. If the loom is ever cross-terminated, swap those two lines
and nothing else changes.

### Why there is no DBC — on purpose

A DBC file is a *decoding* artefact: it says which bits of which id mean what,
with what scale and offset. In this architecture **decoding is Simulink's job**,
and Simulink is the single source of truth for it. Introducing a DBC would:

- duplicate the scaling in a second place that can silently drift from the model;
- put decode logic on the C side of the line, which is exactly the boundary this
  project is organised around;
- add a generator/toolchain step to a workflow whose whole point is "one line in
  a `.def`, rebuild".

So C forwards **eight raw bytes plus an age**, and the unpack + scale is a
visible Simulink block the team can see, tune and review. The id list is the
only thing C needs to know, and it lives in `canN_messages.def`. The cost of this
choice is that the byte layouts live in the model and in this documentation
rather than in a machine-readable file; that is accepted. If interoperability
with other teams' tooling ever demands a DBC, generate it *from* the model, never
the other way round.

### No hardware acceptance filters — on purpose

Accept-all standard ids into RxFIFO0, then a **software (bus, id) linear scan**
in the ISR. Justified by few ids and light bus load, and it keeps the "add an id
= one `.def` line" promise (a hardware filter bank would need allocating and
maintaining). Extended ids and remote frames are rejected at the global filter.

Frame loss is effectively impossible: the Rx ISR drains the **entire** 16-deep
FIFO on every entry, so the hardware FIFO only ever has to absorb interrupt
latency, not the loop period. **Loop rate does not affect frame loss** — Rx is
fully decoupled in the ISR; loop rate only sets data freshness.

### Receiving

```c
/* 1. canN_messages.def  */  CAN_MSG( MY_NAME, 0x123 )
/* 2. model_bridge.c     */  CAN_FEED( bus1, MY_NAME, my_port );
/* 3. Simulink           */  inports  my_port (uint8[8])  and  my_port_age (uint32)
```

The enum, the routing table and the buffers are all generated from the `.def`
lists, so they cannot desync. (This replaced a hand-kept enum + table that
drifted and caused an out-of-bounds write — the origin of the whole `.def`
pattern.)

`Can_Snapshot()` masks **only the two CAN Rx IRQs** (not PRIMASK, so the
scheduler keeps its timing), copies every slot plus `age_ms` and per-bus
`bus_ok`. `age_ms == UINT32_MAX` means *never seen* — which is why an unfed
inport cannot masquerade as fresh data. The model fails safe on stale `age_ms`
or `bus_ok == false`.

### Transmitting

`Can_Send(bus, id, data, len)` queues a classic frame; returns false if the
queue is full. Two patterns in the bridge:

- **Continuous setpoints** (torque / velocity) — plain `Can_Send` every tick.
  The ODrive rx-watchdog *wants* these repeated; if the stream stops it disarms.
- **One-shot / confirmed commands** — `CAN_TX_GATED`, which sends only while the
  model raises `<port>_req`. The model holds the flag until the command is
  confirmed (e.g. by the heartbeat) then drops it. A frozen model therefore goes
  silent rather than spamming a stale frame.

> **Do not gate `Can_Send` on `HAL_FDCAN_GetTxFifoFreeLevel()`.** Tx is in Queue
> mode (`TXBC.TFQM = 1`), where `TXFQS.TFFL` always reads 0 by hardware design.
> That guard once rejected every frame and the HCU transmitted nothing at all
> while looking perfectly healthy. `HAL_FDCAN_AddMessageToTxFifoQ()` checks the
> correct full flag itself. See [TRAPS.md](TRAPS.md) §3.

### Bus-off recovery

`Can_Service()` runs once per superloop pass. On bus-off it re-joins the bus,
rate-limited (100 ms × 50 ≈ 5 s), then **gives up**, leaving `bus_ok = false` so
the model fails safe instead of thrashing forever. Recovery needs
`HAL_FDCAN_Stop()` *then* `Start()` — a bare `Start` fails because the HAL
software state stays `BUSY` after a hardware bus-off.

### Diagnostics

`g_can_stats` (rx-lost, recoveries, tx-fail, tx-done) plus a live `Can_Health()`
register read (bus-off, error-passive, TEC, last error code, TX pending). Both
surfaced by `stats`; `canreg` dumps the raw TX-critical FDCAN registers.
**Deliberately not fed into the model.**

---

## 9. SD logging — the dual-core split

**CM4 owns logging; the CM7 control loop never touches the SD card.**

### Why (do not undo this)

The control loop is a hard 100 Hz — a 10 ms budget per tick. SD cards stall
unpredictably on internal flush/GC: tens to ~250 ms worst case on cheap cards.
FatFs is **synchronous by design** (the diskio layer busy-waits on card-ready),
so on a single core you cannot make a write both synchronous and non-blocking. A
blocking `f_write` anywhere in the CM7 superloop would blow ticks. Moving SD to
CM4 gives the control loop a hard timing guarantee: **a card stall can never
cost a control tick.**

### The contract

One shared header, `Shared/hcu_ipc.h`, included by **both** cores — same
anti-desync philosophy as the `.def` lists. It defines `g_hcu_ipc` at a fixed
address in **D3 SRAM4 (`0x38000000`)**, which both cores address identically.

> D2 SRAM is a **trap** for shared memory: CM7 sees it at `0x30000000`, CM4 at
> `0x10000000`. D3 SRAM4 is not aliased. Use it.

**Lock-free SPSC ring.** CM7 is the sole writer of `head`; CM4 is the sole writer
of `tail`. Both are 32-bit aligned with a single writer, so accesses are atomic
on both cores; the only ordering needed is a `__DMB()` between a record's bytes
and the index that publishes it. Indices are free-running counters; the slot is
`index & HCU_LOG_RING_MASK`. **No per-record HSEM** — CM4 just polls the ring in
its superloop. The boot HSEM handshake is untouched.

Ring = 256 records ≈ **2.5 s** of stall headroom at 100 Hz, comfortably over the
worst-case card stall.

**Cache:** D-cache is currently OFF on CM7 and the M4 has none, so there is no
coherency dance today. If D-cache is ever enabled, mark the D3 region
non-cacheable **via CubeMX** (Cortex-M7 → MPU) so it survives regen — do not
hand-edit the generated `MPU_Config()`.

### Producer (CM7, `logger.c`)

`Log_Init()` sets up the ring — indices and version first, a barrier, then
**magic last**, so a consumer that sees the magic is guaranteed to see a fully
initialised struct. `Log_Write(rec)` is one line in the bridge dispatch:
RAM-only, deterministic, never blocks. Ring full ⇒ **drop the newest** and count
it in `g_log_stats.drops`, which is the trackside "logging is failing" signal.

### Consumer (CM4, `sd_logger.c`)

Polling superloop drains the ring into a 512-byte sector buffer in CM4-local D2
RAM (same domain as SDMMC2 and its IDMA, so it is DMA-reachable), then
`f_write` / `f_sync` (~1 Hz) with size-based rotation at 8 MB to `LOGxxxx.BIN`.

Mount is **attempted, not asserted**: no card ⇒ logging silently disabled, CM7
utterly unaffected. `sd_card_init()` is the single, idempotent run-time init
path (`DeInit` → `Init` → 4-bit) — the leading `DeInit` means a previous failed
attempt can never poison a later one. See [TRAPS.md](TRAPS.md) §4 for why that
matters and what `DISABLE_SD_INIT` is doing in `sd_diskio.c`.

### Record format

Fixed-size **packed binary**, `tick` and `overruns` prepended automatically
(scheduler diagnostics, not model signals), then one field per line in
`Shared/log_signals.def`. Packed, so on-disk bytes are field-after-field —
trivial and endian-clean to decode.

Each file starts with a 16-byte header: magic `HDLG`, `version`
(= `HCU_IPC_VERSION`), `record_size`, and the datetime. `record_size` is the
cross-check that catches a decoder/firmware layout mismatch.

**Edit `log_signals.def` ⇒ rebuild BOTH cores.** The layout is shared. Old logs
then decode with a `record_size` mismatch warning — keep the matching `.def` per
season (it is git-tracked).

### Wall-clock

The RTC is on LSE with **no VBAT backup**, so it resets every power cycle. CM7
owns the RTC and publishes the datetime into `g_hcu_ipc.now` at ~1 Hz through a
**seqlock** (`seq` odd while writing, even when stable); CM4 reads it for
`get_fattime()` and the file header and never touches the RTC peripheral.
Filenames use a **sequence counter** (`LOGxxxx.BIN`, lowest free index) as the
collision-proof primary key, so logging is robust regardless of the clock; the
datetime is only in the header. Boot default is the firmware build time; set the
real time with `time set …` or the GUI's one-click button at the start of a
session.

### Decoder

`hcu_logdecode.py` (repo root, stdlib only) reads the same `log_signals.def`
plus the file header and emits CSV, so the columns always match what the
firmware recorded.

```bash
python hcu_logdecode.py LOG0000.BIN
```

### CM4 diagnostics

`g_sdlog_status` (volatile, watch in the debugger): `loops` (heartbeat),
`mounted`, `ring_seen`, `records`, `head_seen` / `tail_now`, `file_index`,
`file_bytes`, `last_fr`. **First thing to read when triaging the logger** — the
decision table is in [BENCH.md](BENCH.md) §4.

---

## 10. Simulink → IDE codegen workflow

- Embedded Coder, system target **`ert.tlc`**, Hardware board **None**,
  **Nonreusable function**, "Generate example main" **OFF**, "Generate code only"
  **ON**, fixed-step **discrete** solver at base rate, Hardware = ARM Cortex-M7.
- MATLAB must write straight into the project — this is the **only** path the
  firmware compiles:

  ```matlab
  Simulink.fileGenControl('set','CodeGenFolder','<...>/CM7/Model','createDir',true)
  ```

  `CodeGenFolder` resets every MATLAB session, so it is pinned in
  `Documents\MATLAB\startup.m`.
- In CubeIDE the generated folder must be a **Source Location**
  (Project Properties → C/C++ General → Paths and Symbols → *Source Location*),
  **not just an Include path**. Include path resolves headers; Source Location is
  what compiles the `.c`. Missing it = `undefined reference` at link.
  `.def` files are include-only — they need the include path but **not** a
  Source Location.
- **Commit generated code; never hand-edit it.** Filenames are stable, so a
  regen overwrites in place. `slprj/` is the throwaway cache and is gitignored;
  `*_ert_rtw/` is tracked on purpose.

---

## 11. Module map (CM7)

**Generated by CubeMX — edit only inside `USER CODE` markers:**

- `Core/Src/main.c` — init order in `USER CODE BEGIN 2`:
  `Params → Console → CanSniffer → Can → Model → Log → Clock → Telem → AirSafety → Sched`.
  Sched is last on purpose: the TIM6 ISR drives the fail-safe, so everything it
  touches must already exist. Superloop (`USER CODE BEGIN 3`):
  `Console_Poll(); Can_Service(); Clock_Service(); Telem_Service(); CanSniffer_Service(); if (Sched_StepDue()) Model_Step();`
- `USB_DEVICE/App/usbd_cdc_if.c` — RX ring buffer + `CDC_Read()` in USER CODE;
  `CDC_Transmit_FS()` is the TX primitive.

**Hand-written, outside regen:**

| Module | Role |
|---|---|
| `model_bridge.c/.h` | the adapter. Only file that includes both `main.h` and the model header. |
| `scheduler.c/.h` | TIM6 tick policy + the ISR that runs the fail-safe |
| `air_safety.c/.h` | independent freeze fail-safe; sole writer of the AIR/precharge sinks |
| `can.c/.h` + `can1/2_messages.def` | dual-FDCAN demux and tx |
| `can_sniffer.c/.h` | raw all-id observer (diagnostic, read-only) |
| `params.c/.h` + `params.def` | tunable store, clamping, flash blob |
| `console.c/.h` | USB-CDC command console; exposes `Console_Out()` |
| `telem.c/.h` + `telem_signals.def` | live model-signal stream |
| `logger.c/.h` | SD-log producer; defines `g_hcu_ipc` in `.shared_ram` |
| `clock.c/.h` | RTC seed / publish / `time` command |

**Generated by Simulink — never hand-edit:**
`Model/HCU_V2_Simulink_ert_rtw/*`.

**CM4:** `sd_logger.c/.h` (consumer; also defines `g_hcu_ipc` in `.shared_ram`,
owns run-time SD init, and backs `get_fattime()`).

**Both linker scripts** carry a matching `.shared_ram (NOLOAD) > RAM_D3` section
at `0x38000000`. These are **hand-edited** — CubeMX does not manage cross-core
sections — and CM4 also needed `RAM_D3` adding to its `MEMORY` block.
`hcu_ipc.h` is reached by a **source-relative include**
(`#include "../../../Shared/hcu_ipc.h"`), not an `-I` path, because CubeMX wipes
include paths on regen.

---

## 12. Peripherals initialised but not yet bridged

`SPI3` (ASM330LHHX IMU + INT on PD2/PD3) and `ADC1` (10 k NTC thermistor on PC0)
are configured by CubeMX and have pins assigned, but nothing in the bridge reads
them yet. `USART3` is likewise initialised and unused. TIM2/TIM5 (wheel-speed
input capture) were **freed** — rear wheel-speed encoders were dropped for this
year's car. See [STATUS.md](STATUS.md).
