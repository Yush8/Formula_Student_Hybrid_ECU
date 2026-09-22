# HCU V2 — Traps and recovery

Failures whose error message points somewhere other than the cause. Every entry
here cost someone real time at least once. Read the **Tell** column first.

---

## 1. CubeMX *Generate Code* corrupts the CubeIDE `.project` files

**This is the big one. It happens on nearly every regen, on BOTH cores.**

A known CubeMX dual-core bug rewrites `CM7/.project` and `CM4/.project` linked
resources. It has two faces; **both fail at the link step**, which is why the
error never mentions the project file.

| Face | Symptom |
|---|---|
| HAL / USB library vanishes | a wall of `undefined reference to HAL_*` / `USBD_*`. The regen flattened the shared-driver **folder-links** into dozens of loose **file-links**, which CDT's makefile generator ignores. |
| HAL / USB duplicated | 700+ `multiple definition of …`. The regen added `type=1` duplicate file-links *next to* the folder-link, so everything compiles twice. |
| FatFs duplicated (CM4) | `multiple definition of f_open / disk_read / …`. The regen added phantom file-links (`diskio.c ff.c ff_gen_drv.c syscall.c`) beside `Middlewares/Third_Party/FatFs/src`. |

**Tell:** the failing build reports **"Incremental Build … ~600 ms"** (link only),
and `Debug/sources.mk` `SUBDIRS` is missing `Drivers/STM32H7xx_HAL_Driver/Src`
and the USB-library directories. CDT will not regenerate the stale `Debug/`
makefiles on an incremental build, which is why deleting `Debug/` is part of the
fix.

### Recovery — the standing procedure after ANY regen

1. Restore **both** snapshots:
   ```
   copy CM7\.project.known-good CM7\.project
   copy CM4\.project.known-good CM4\.project
   ```
2. **Delete both `Debug/` folders entirely** (forces makefile regeneration).
3. **Close and re-open both projects** in CubeIDE. This step is not optional —
   CubeIDE caches `.project` and will otherwise ignore your restore.
4. Build.

**Notes:**

- CM4's known-good legitimately uses 24 per-file HAL `type=1` links (the M4
  subset). **That is not corruption** — the 4 FatFs strays are.
- CM7's known-good has all driver/middleware sources as proper `type=2`
  folder-links pointing at their `…/Src` directories, with no FatFs entries.
- **Re-snapshot `.project.known-good`** whenever you deliberately add a peripheral
  that pulls in a new HAL module — the linked-source set changes then.
- After any regen, also re-verify the must-not-break `.ioc` settings in
  [ARCHITECTURE.md](ARCHITECTURE.md) §1.

---

## 2. `undefined reference` to a model symbol after adding a Simulink port

Two distinct causes:

**(a) Embedded Coder pruned the inport.** An unconnected root inport is removed
from `ExtU`, so C referencing it fails at link. Connect the port to something
that *uses* it, regenerate, then enable the matching `*_FEED_READY` flag in
`model_bridge.c`.

**(b) The generated folder is an Include path but not a Source Location.** These
are different CubeIDE settings. Include path resolves headers; **Source
Location** is what compiles the `.c`. Missing it = `undefined reference` at link.
Project Properties → C/C++ General → Paths and Symbols → *Source Location*.

`.def` files are **include-only** — they need the include path (`Core/Inc`) but
**not** a Source Location.

---

## 3. The HCU receives CAN fine but transmits nothing

**Root cause:** FDCAN Tx is in **Queue mode** (`TXBC.TFQM = 1`). In that mode
`TXFQS.TFFL` (the "free level") **always reads 0 by hardware design.** Old code
gated every send on `HAL_FDCAN_GetTxFifoFreeLevel(h) == 0`, so it bailed before
queuing anything, ever.

**Why it hid for so long:** RX is a completely separate path, so bus health
looked fine, LEDs looked fine, and the model looked alive. The only thing
missing was every outgoing frame — and the symptom presented as "the motors
won't spin / the ODrives stay in IDLE".

**Fix (in place):** no free-level pre-check. `HAL_FDCAN_AddMessageToTxFifoQ()`
checks the correct full flag (`TXFQS.TFQF`), which is valid in both FIFO and
Queue mode.

**How to confirm TX is really leaving the chip:**

- `stats` → the per-bus `tx` line. **`sent` climbing** = frames actually
  transmitted. `pend` stuck non-zero = the engine is not sending.
- `TEC ≈ 128` with `ERR-PASSIVE` and `lec ACK!` = we transmit but nobody
  acknowledges — a dead TX wire, wrong bus, or a lone node. CAN's ACK-error rule
  parks a lone transmitter at error-passive rather than bus-off.
- `TEC 0` with `lec none/nc` = we are not putting frames on the bus at all.
- `canreg` dumps the raw registers: `CCCR.INIT` should be 0; `TXBC.TFQS` should
  be non-zero; `TXBRP` bits stuck set = the engine never sends them; `PSR.ACT`
  never 3 = it never keys the transmitter.

---

## 4. SD logging breaks after a regen (card present)

**Cause:** the card being initialised **twice** — once by `MX_SDMMC2_SD_Init`,
once by `f_mount` → `BSP_SD_Init`. A failed first init leaves the HAL handle
dirty and every later mount sticks at `FR_NOT_READY`.

**The fix, which must survive:**

- `CM4/FATFS/Target/sd_diskio.c` — **`#define DISABLE_SD_INIT`** (inside the
  `disableSDInit` USER CODE marker). FatFs's `SD_initialize()` then no longer
  calls `BSP_SD_Init()`, so the card is brought up in exactly one place.
- `CM4/Core/Src/sd_logger.c` — `sd_card_init()` is the sole run-time init:
  `HAL_SD_DeInit()` → `HAL_SD_Init()` → `ConfigWideBusOperation(4-bit)`. The
  leading `DeInit` makes it **idempotent**, so a previous failed attempt can
  never poison a later one.

**If logging breaks with a card present after a regen, check `DISABLE_SD_INIT`
first.** USER CODE markers normally survive a regen — but verify.

**Related — no card at boot:** `CM4/Core/Src/main.c` sets a
`static volatile uint8_t s_sd_boot_init` flag in `SDMMC2_Init 1` and clears it in
`SDMMC2_Init 2`. `Error_Handler()` `return`s **only while that flag is set**, so
the boot SD init can fail forward with no card while `Error_Handler()` keeps its
halt-everything behaviour for every other caller. An earlier "just make
Error_Handler return" fix was reverted because it broke card-present logging.

---

## 5. `printf("%f")` prints nothing

newlib-nano's `printf` family has **no floating-point support**. A `%f` does not
just print wrong — it silently swallows the rest of the format string.

Format floats by hand: `f32_to_str()` in `params.c`, `fmt_real()` in `telem.c`.

---

## 6. Console output is garbled

`CDC_Transmit_FS` **does not copy its buffer.** `USBD_CDC_SetTxBuffer` merely
stores the pointer, and the USB endpoint reads from it *after* your function has
returned. A local buffer's stack frame is popped immediately, and any interrupt
(TIM6 at 100 Hz, FDCAN) reuses that memory mid-transfer.

**Every buffer handed to `Console_Out` / `CDC_Transmit_FS` must be `static`.**
`telem.c` and `can_sniffer.c` were always written this way; the console print
helpers were fixed on 2026-09-22.

---

## 7. `invalid storage class for function …` in `main.c`

An **unbalanced brace**, almost always a missing `}` at the
`USER CODE BEGIN 3` / `while(1)` boundary. The compiler reports it against the
*next* function, plus `expected declaration … at end of input`. Look at the
braces, not at the function it names.

---

## 8. Bus-off will not clear

A bare `HAL_FDCAN_Start()` fails after a hardware bus-off: the silicon sets
`CCCR.INIT`, but the HAL's software state stays `BUSY`, so `Start`'s
"must be READY" check rejects it.

**You need `HAL_FDCAN_Stop()` then `HAL_FDCAN_Start()`.** `Stop` is valid while
BUSY and drives the handle back to READY. Configuration, filters and the message
RAM all survive the Stop/Start. This is what `Can_Service()` does.

---

## 9. Torque is stuck at zero and nothing says why

Work down this list:

1. **`Brake_Zero_Offset` too low.** The brake sensor rests around 3.0; if the
   offset does not clear it, the model takes the regen branch and the accelerator
   branch never runs. Symptom: full throttle gives **negative**
   `Base_Torque_Demand`. See [CONTROL_STRATEGY.md](CONTROL_STRATEGY.md) §5.
2. **A `*_FEED_READY` flag is 0 while the model has the ports.** For the torque
   slew this means the rate limits default to 0, the limiter clamps every delta
   to 0, and torque freezes at 0 — fail-safe, but silent.
3. **Launch speed gate.** On a jack the car never rolls. Set
   `Bench_Speed_Bypass = 1`.
4. **Engine-sync gate.** With the engine off the chart holds in RELAY_SWAP. Set
   `Bench_Engine_Off = 1`.
5. **Power limiter gated to `s = 0`** — BMS frame or either encoder stale
   (≥ 150 ms), or `Pack_Voltage ≤ 0`. Watch `Torque_Scale_Factor` in telemetry.
6. **Brake-plausibility latch set.** It holds until APPS ≤ 5 % regardless of the
   brake. Lift off fully.
7. **The AIR fail-safe has latched.** `stats` → `AIR FAIL stall-latched`. Clear
   with `safety reset` (refused unless the loop is alive again) or the GUI
   CLEAR FAULT button.
8. **Nothing is transmitting at all** — trap §3.

---

## 10. Saved settings did not take effect

- **Changing a default in `params.def` does not invalidate flash.** The
  `layout_id` fingerprint covers names and types, not values — a previous `save`
  still loads the old value. Do `set <name> <value>; save`, or `defaults; save`.
- **Adding, removing or renaming a parameter *does*** change `layout_id`, so the
  old blob is rejected and you get safe defaults. That is intended — just `save`
  once after reflashing.
- A power loss mid-`save` reverts to defaults on the next boot (single-copy
  blob). That is the safe failure, by design.

---

## 11. Things that look like bugs but are decisions

Before "fixing" any of these, read the linked section.

| Looks wrong | Actually | Where |
|---|---|---|
| No CAN acceptance filters | deliberate — few ids, light load, keeps "add an id = one line" | ARCHITECTURE §8 |
| No DBC file | deliberate — decoding is Simulink's job; a DBC would duplicate scaling outside the model | ARCHITECTURE §8 |
| C never checks the SDC | deliberate — hardware opens the AIR coils directly, and the model reads `SDC_Monitor` itself | ARCHITECTURE §5 |
| `g_can_stats` etc. never reach the model | deliberate policy — the model gets `_age` and `bus_ok`, nothing else | ARCHITECTURE §8 |
| The freeze latch does not auto-clear | deliberate — a model that froze and resumed is in an unknown state | ARCHITECTURE §5 |
| `MODEL_PARAM` lines are hand-written, not generated | deliberate — the user controls what reaches the model | ARCHITECTURE §3 |
| The external watchdog kick is compiled out | deliberate — enable only after confirming the STWD100's window | STATUS §4 |
| `TV_Gain` defaults to 0 | deliberate — TV inert until tuned on a skidpad | CONTROL_STRATEGY §4 |
| `g_sdlog_status` is not on the console | deliberate deferral — USB MSC is the intended path | STATUS §4 |
