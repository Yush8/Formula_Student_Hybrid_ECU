# HCU V2 — Status ledger

**The single source of truth for what is done and what is open.** Every other
doc describes the design; this one describes the progress. Update it when
something lands, and delete the line when it stops being true.

Last full review: **2026-09-22**.

---

## 1. Done and verified on hardware

| # | Item | Evidence |
|---|---|---|
| 1 | **Deterministic scheduler** — TIM6 100 Hz time base, run-latest, overrun counter | ARCHITECTURE §4 |
| 2 | **CAN subsystem** — dual FDCAN, `.def`-generated demux, bus-off auto-restart, tx/rx diagnostics | ARCHITECTURE §8 |
| 3 | **Parameter store + flash blob** — one-line `params.def`, `layout_id` auto-invalidation, clamping | ARCHITECTURE §6 |
| 4 | **USB-CDC console + `stats`** — trackside health without a debugger | ARCHITECTURE §7 |
| 5 | **Dual-core SD logging, end to end** — CM7 → D3-SRAM4 ring → CM4 → FatFs → `LOGxxxx.BIN` → CSV | verified with ST-Link detached, board power-cycled, populated CSV |
| 6 | **No-card-at-boot is graceful** — CM4 boots through, logging off, CM7 unaffected | verified both ways (card present and absent) |
| 7 | **Dual-core flash & debug procedure** — the M7→M4 attach/resume order | BENCH §2, verified |
| 8 | **Config GUI** — auto-discovering, five tabs, auto-reconnect | in daily use |
| 9 | **CAN TX fixed** — the queue-mode `GetTxFifoFreeLevel` bug that meant the HCU transmitted *nothing* | `TXBTO` 0→0x1F, ODrive heartbeats live, motors spin |
| 10 | **BMS power limiter** — verified in generated code, both drive and charge branches | CONTROL_STRATEGY §3 |

## 2. Built but NOT yet bench-verified

These are written, they compile and link, and the design has been reviewed — but
nobody has put a scope or a car on them yet. **Treat as unproven.**

| Item | What to check | Reference |
|---|---|---|
| **Independent AIR fail-safe** | (a) force a model hang → AIRs latch open after ~500 ms, `stall_latched` set, `AIR_LED` off; (b) `safety reset` re-arms only when the loop is alive again; (c) normal driving and `stats` queries never nuisance-trip (`stall_trips` stays 0) | ARCHITECTURE §5 |
| **USB live telemetry** | every signal updates; watch `g_sched_overruns` at high `telem rate` under load | ARCHITECTURE §7 |
| **CAN sniffer** | ids self-discover on both buses; `cansniff` reports no drops | ARCHITECTURE §7 |
| **ODrive control-mode switch** | mode changes only while IDLE; exactly one setpoint frame on the wire; power-cycle reverts to torque | CONTROL_STRATEGY §8 |
| **Torque slew limiter** | ramp time ≈ 0.20 s to full; a fault still cuts torque instantly | CONTROL_STRATEGY §7 |
| **Launch speed gate + brake plausibility** | 5/3 km/h hysteresis; T.4.3 latch holds until APPS ≤ 5 %; confirm `UnitDelay` init = 0 and that P_TRIP = 25 is a real hard brake in ×0.01 units | CONTROL_STRATEGY §6 |
| **Brake zero-offset fix** | full throttle now gives positive `Base_Torque_Demand` | CONTROL_STRATEGY §5 |
| **`g_sched_overruns == 0` under logging load** | *the* proof CM7 is isolated from SD stalls. Long run including card flush/GC stalls; it must stay 0. | ARCHITECTURE §9 |

## 3. Open — safety and correctness first

### 3.1 `SDC_Monitor` (PE2) is NOPULL — fail-dangerous

**Highest safety value open item.** PE2 has no pull resistor configured, so a
broken or disconnected sense wire **floats**. With HIGH = healthy, a float can
read as a false "healthy" in the model.

**Fix:** set PE2 to **pull-down** in CubeMX, so a broken wire reads LOW =
tripped = safe. This belongs with the pin definition, not in `air_safety.c` (the
C layer no longer reads the SDC). A one-line bridge reconfiguration is possible
instead if you want to avoid a regen — say the word.

### 3.2 `Params_Save()` stalls CM4

`Params_Save()` erases a sector in **flash bank 2**, and **CM4 executes from bank
2**. H7 read-while-write only works *across* banks, so CM4's instruction fetches
stall for the whole erase — the logger freezes and the ring can overflow.

Acceptable today because `save` is a stationary/pit action, but it is real
coupling. Two cheap mitigations worth doing:

1. **Shrink CM4's linker `FLASH` region** from 1024K to 896K
   (`CM4/STM32H745ZITX_FLASH.ld`). Today it covers the config sector at
   `0x081E0000`, so nothing stops CM4 silently growing into the config blob. With
   896K the linker errors instead.
2. **Refuse `save` while the model is armed** — `Params_Save()` could return a
   "refused: not stationary" when `g_air_safety.air_closed` is true.

### 3.3 Stack buffers handed to `CDC_Transmit_FS`

`CDC_Transmit_FS` does **not** copy — the USB endpoint reads the buffer after the
call returns. `telem.c` and `can_sniffer.c` already use `static` frame buffers
for exactly this reason, but these still use locals:

- `console.c`: `Console_Printf()` — `char buf[128]`
- `can.c`: `Can_DumpTx()` — `char b[192]`
- `telem.c`: `Telem_PrintStatus()` / the schema helper — `char buf[48]`, `[64]`
- `can_sniffer.c`: `CanSniffer_PrintStatus()` — `char buf[128]`

Symptom is garbled console output when an interrupt reuses the popped stack
frame mid-transfer. **Fixed for `Console_Printf`, `Can_DumpTx` and the status
helpers as part of the 2026-09-22 tidy** — see §6. Keep the rule in mind for any
new print path.

### 3.4 Type mismatches at the model boundary

| Port | Model type | Should be | Impact |
|---|---|---|---|
| `Bench_Speed_Bypass` | `real_T` (**double**) | `boolean` | drags float64 maths into a single-precision M7 model |
| `Reset_Req` | `real_T` (**double**) | `boolean` | same |
| `Left_Direction` / `Right_Direction` | `int8_T` | `int32` (to match `PARAM_I32`) | silent narrowing; safe today because clamped to ±1, but it contradicts the documented `PARAM_I32 ⇒ int32` rule |

All three are **Simulink-side** changes (Yusha's). No C change is needed for the
first two; the bridge assigns a bool/int either way.

## 4. Open — roadmap

| # | Item | Notes |
|---|---|---|
| 1 | **External STWD100 watchdog** | The kick hook is built in (`AIR_SAFETY_EXT_WDT`, compile-time **0**). It is the only layer that catches a total CPU/clock lockup. Fit the 0 Ω link, confirm the chip's timeout window, then enable. Too slow a kick resets a healthy MCU. |
| 2 | **IWDG** | Still TODO. Kick **only** when the control loop is stepping healthily. |
| 3 | **A/B flash for config** | Two sectors, write-and-verify before marking current, so a power loss mid-save keeps the previous good config. Would also help §3.2. |
| 4 | **SD config mirror** | Human-readable `key value` export/import on the card (reuse the console `set` parser); git-track setups per session. |
| 5 | **Composite USB CDC + MSC** | Pull SD logs without removing the card. **This is the intended path for SD/logging status over USB** — `g_sdlog_status` stays CM4-debugger-only until then, by explicit decision. Do not build a CM4→IPC status publish instead. |
| 6 | **MPU non-cacheable region for the shared ring** | Deferred; D-cache is OFF on CM7 and the M4 has none. If D-cache is ever enabled, add it via **CubeMX** (Cortex-M7 → MPU) so it survives regen. |
| 7 | **`f_expand` / pre-allocation of the log file** | Only if long sessions show FAT-walk stalls. CM4-only; never touches CM7. |
| 8 | **`LOG_ARRAY` macro** | For non-scalar logged signals (e.g. an 8-byte CAN payload as one field). ~5-minute add when first needed. |
| 9 | **Auto-set the RTC from a CAN time message** | Removes the manual `time set`, if such a message exists on the bus. |
| 10 | **Persist the clock across a reset** | `Clock_Init()` re-seeds from build time on *every* boot, so a mid-session reset silently rewinds log timestamps. An RTC backup register holding a magic would let it seed only when genuinely unset (survives a system reset, though not a power cycle without VBAT). |
| 11 | **Runtime per-signal telemetry masking** | The `.def` plus the GUI filter cover "show less" today. Add only if USB bandwidth ever bites. |
| 12 | **`MCU_Active` (PE7) de-assert on fault** | Could act as a second hardware AIR-enable gate. Needs the external wiring confirmed first. |
| 13 | **Steering plausibility** | Out-of-range or stale steering ⇒ force the TV split to 0. Good practice, not a rule; acceptable to defer while `TV_Gain = 0`. |
| 14 | **Resettable slew limiter** | Closes the "fault clears while pedal held → torque steps back up" residual. Only if the rig shows re-application shock. |

## 5. Open — peripherals initialised but not bridged

| Peripheral | Hardware | State |
|---|---|---|
| **SPI3** | ASM330LHHX IMU + INT (PD2/PD3, PC10/11/12) | inited by CubeMX; nothing reads it. Gives yaw **rate**, not ground speed. |
| **ADC1** | 10 k NTC thermistor (PC0) | inited; nothing reads it |
| **USART3** | — | inited; unused |

TIM2/TIM5 (wheel-speed input capture) were **freed** — rear encoders were
dropped for this year's car.

## 6. Recent housekeeping (2026-09-22)

- Documentation consolidated: `HANDOFF.md`, `EXTENDING_THE_HCU.md` and
  `DEBUG_DUALCORE.md` were absorbed into this `docs/` set and removed. The
  vehicle-control design, which previously existed only in AI session memory, is
  now in [CONTROL_STRATEGY.md](CONTROL_STRATEGY.md).
- Added `AGENTS.md` (+ a `CLAUDE.md` pointer) as the orientation page for any AI
  or new contributor.
- Fixed the stack-buffer-to-USB hazard in the console, CAN register dump, and
  the telemetry/sniffer status paths (§3.3).
- Corrected documented CAN bitrates (**FDCAN1 is 1 Mbit/s**, FDCAN2 500 kbit/s —
  the docs previously said 500 k for both).
- Corrected the stated CPU clock (**75 MHz**; the old text read as if the part
  ran at the VOS2 ceiling).
- Removed stale comments (`AirSafety_Init` does not touch the SDC pin; the
  config-sector justification referred to a CM4 "stub" that no longer exists).
- Condensed the ~150-line rationale preamble in `model_bridge.c` into short
  pointers, with the full reasoning moved to `CONTROL_STRATEGY.md`.
- Committed ~2.5 months of previously uncommitted firmware and GUI work.

## 7. Known drift worth a decision

`telem_signals.def` is documented as "pre-filled with every current Outport and
Inport", which is no longer true. These live model ports are **not** streamed:

`Steering_Angle`, `Sync_State`, `Vel_Scale`, `Brake_Zero_Offset`,
`Bench_Engine_Off`, `Bench_Speed_Bypass`, `Bench_Velocity_Mode`,
`Torque_Rate_Up`, `Torque_Rate_Down`, `Velocity_Left`, `Velocity_Right`,
`Set_Controller_Mode_0_req`, `Set_Controller_Mode_1_req`, `Start_Button_GUI`.

Two of them — `Steering_Angle` and `Sync_State` — are already referenced by the
GUI's grouping table, so they were clearly meant to be visible. Adding any of
them is one line each plus a CM7 rebuild; it costs USB bandwidth, so it is a
decision rather than a fix. Recommended additions before the first rolling test:
`Vehicle_Speed` (already present), `Steering_Angle`, `Sync_State`.
