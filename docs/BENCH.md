# HCU V2 — Bench procedures

Build, flash, run, debug two cores, and triage when something misbehaves.

ST app note **AN5286** ("STM32H7x5/x7 dual-core microcontroller debugging") is
the authoritative reference for the launch-configuration layout below.

---

## 1. Build and flash

Two sub-projects share one `.ioc`: `HCU_V2_CM7` (control) and `HCU_V2_CM4`
(logging). Both are built in STM32CubeIDE 2.1.1.

| You changed | Rebuild | Reflash |
|---|---|---|
| `params.def`, `telem_signals.def`, `canN_messages.def`, the bridge, any CM7 `.c` | CM7 | CM7 |
| `Shared/log_signals.def` or `Shared/hcu_ipc.h` | **both cores** | **both** |
| any CM4 `.c` | CM4 | CM4 |
| the Simulink model (after codegen to `CM7/Model`) | CM7 | CM7 |

After a build fix use **Run/Debug** — that downloads. Plain *Build* only
compiles.

**Flash map:** CM7 = bank 1 `0x08000000`; CM4 = bank 2 `0x08100000`; the
parameter blob = bank 2 last sector `0x081E0000`.

### To just RUN both cores (no debugger)

1. Make sure **both banks are programmed** — see the trap in §2.
2. Detach the ST-Link, power-cycle (or press RESET). Both boot from flash, CM7
   releases CM4 over HSEM, logging runs on its own.

This is how end-to-end logging was verified. **Rebuild CM4 ⇒ reflash CM4**, or
you are running stale code against fresh symbols.

---

## 2. Live debug on both cores

Goal: breakpoints and live watch on **both** cores at once — e.g. watch CM4's
`g_sdlog_status` while CM7's control loop runs.

### 2.1 The one thing to understand

**CM4 boots asleep.** Out of reset the M4 immediately enters STOP (D2 domain) and
sits there until CM7 releases it over a hardware semaphore:

- `CM4/Core/Src/main.c` → `HAL_PWREx_EnterSTOPMode(...)` — the M4 parks here.
- `CM7/Core/Src/main.c` → `HAL_HSEM_Release(HSEM_ID_0, 0)` — CM7 wakes it, after
  `SystemClock_Config()`.

Two facts force the whole procedure:

- **The wake is ONE-SHOT.** `HAL_HSEM_Release` runs exactly once at boot, then
  CM7 drops into `while(1)` and never releases again.
- **Attaching the CM4 debugger RESETS the M4.** It lands at the top of `main`,
  runs down into its boot STOP, and the debugger holds it there.

Therefore **CM7's single release must happen *after* the CM4 debugger has
attached** — and then you must resume the M4 too, because its debugger is
holding it. Get it wrong (resume CM7 fully, *then* attach CM4) and the
attach-reset re-sleeps the M4 with the one-shot release already spent: it
orphans at `0xA05F0000` and only a power-cycle recovers it.

> **`0xA05F0000` means "core powered down" (M4 asleep), not a crash.** Seeing it
> *mid-sequence* is expected. Seeing it *stuck* after resuming both cores means
> it orphaned. Any variable read while PC = `0xA05F0000` is **garbage from a dead
> domain** — trust only a *changing* `g_sdlog_status.loops`.

### 2.2 One-time setup

**The trap:** the CM4 launch has **Download = OFF** and the CM7 launch only
downloads its own `.elf`. As shipped, *nothing flashes CM4's bank 2.* That is
fine until you rebuild CM4 and forget to reflash — then you debug stale flash
with fresh symbols, breakpoints land on the wrong lines, and you chase ghosts.

**Option A — recommended (flash both banks from the CM7 launch):**

1. `Run → Debug Configurations… → HCU_V2_CM7 Debug → Startup` tab.
2. Under the load list, **Add…** → project `HCU_V2_CM4`, file
   `Debug/HCU_V2_CM4.elf`.
3. For that CM4 entry tick **Download = ON**, **Load symbols = OFF**.
4. Order the list so the **CM7 entry is last** (flash CM4 first, then CM7).
   **Apply.**

Launching the CM7 config alone now freshly programs **both** banks every time.
Leave the CM4 config with Download OFF — it only attaches.

**Option B — manual:** after **every** CM4 rebuild, flash CM4 before attaching —
temporarily tick Download in the CM4 config for one run, or program
`Debug/HCU_V2_CM4.elf` to bank 2 (`0x08100000`) with STM32CubeProgrammer.

**Also required — make the CM4 attach passive.** This is the one that bites:

> `Debug Configurations → HCU_V2_CM4 Debug → Startup` →
> **uncheck "Set breakpoint at: main"**.

The CM4 config must attach to the running M4 and leave it alone — no reset, no
run-to-main. "Stop at main" tells the debugger to *run the application to main*,
i.e. restart the M4, which is fatal here. On a correct passive attach you land in
the running `while(1)` / `SdLog_Service`, **never at `main`**.

Already correct in both `.launch` files — do not change these:

| | CM7 | CM4 |
|---|---|---|
| Access port | AP0 | AP3 |
| Reset | connect-under-reset | **none** |
| Halt all on reset | ON | OFF |
| Download | ON | **OFF** |
| Stop at main | fine | **turn OFF** (above) |
| Shared ST-Link | ON | ON |
| Low-power debug | ON | ON (keeps the sleeping M4 reachable) |
| GDB port | 61234 | 61238 |

### 2.3 The routine — every session

> **Debug M7 → Debug M4 → Resume M7 → Resume M4.** The order is not negotiable.

0. **Build both** projects so both `.elf`s are fresh (build CM4 explicitly — the
   CM7 launch only builds CM7). Power-cycle the board, card in, for a clean start.
1. Launch **CM7** debug. It downloads (both banks with Option A) and **halts at
   `main`**. **Do not resume yet.**
2. Launch **CM4** debug. It attaches — this resets the M4, which runs down into
   its boot STOP and is held there. **`0xA05F0000` here is correct.** Leave it.
3. **Resume CM7 (F8).** Its single `HAL_HSEM_Release` fires; the wake is now
   *pending* for the M4. (The M4 is still held by its own debugger — expected.)
4. **Resume CM4 (F8).** The M4 leaves STOP, takes the pending wake, and runs
   through `HAL_Init → SdLog_Init → while(1)`. `g_sdlog_status.loops` starts
   climbing. You are live.
5. Add `g_sdlog_status` and `g_hcu_ipc` to Live Expressions. **Truth test:
   `loops` must be *incrementing*.** Sanity-check that `&g_hcu_ipc` reads
   `0x38000000` on **both** cores.

Once the M4 is live in `while(1)`, normal halt / step / resume is fine. Only a
**reset** re-orphans it — so do not reset the M4 mid-session. If you must,
power-cycle and start this sequence over.

For a clean debug session it also helps to comment out the **second** boot
timeout `Error_Handler()` in CM7 `main.c` (around line 159), like the first
(~line 121). Harmless and debug-only.

**Plan B** if step 4 still leaves it at `0xA05F0000` (some gdbserver versions
reset the core on resume too): in a **debug-only** build, comment out the M4 boot
STOP (`HAL_PWREx_EnterSTOPMode(...)` in `CM4/Core/Src/main.c`). The M4 then never
sleeps and is always attachable. **Revert before flashing production** — the
handshake must stay for cold boot.

---

## 3. Debug-time watch list

These globals exist to be watched. None of them is fed to the model.

| Global | Core | Tells you |
|---|---|---|
| `g_sched_ticks` / `g_sched_overruns` | CM7 | loop alive / deadlines missed. **`overruns == 0` is the goal.** |
| `g_air_safety` | CM7 | `supervise_calls` climbing = the ISR fail-safe is live; `stall_latched`, `stall_trips`, `air_closed` |
| `g_can_stats` | CM7 | rx-lost, recoveries, tx-fail, **tx-done** (frames the engine actually sent) |
| `g_log_stats` | CM7 | `writes`, `drops` (logging failing), `occ_max` (ring high-water) |
| `g_sdlog_status` | CM4 | the whole logger state — see §4 |
| `g_hcu_ipc` | both | must sit at `0x38000000` on both cores; `head`/`tail` moving = the ring is flowing |

Trackside without a debugger, `stats` on the console summarises the CM7 half of
that list with OK / WARN / FAIL verdicts.

---

## 4. Triage: `g_sdlog_status` (CM4)

**Read this first when logs do not appear.**

| Reading | Meaning |
|---|---|
| `loops == 0` | CM4 not running or not flashed, or stuck in `Error_Handler` for a **non-SD** fault (no card no longer halts CM4) |
| `loops` climbing, `mounted == 0`, `last_fr == -1` | **No card / not seated** — the normal "running without SD" case. Logging off, car unaffected. |
| `loops > 0`, `mounted == 0`, `last_fr > 0` | Card present but a FAT32 / format / seating problem — read `last_fr` |
| `mounted == 1`, `ring_seen == 0` | Shared-RAM address mismatch — confirm `&g_hcu_ipc == 0x38000000` on both cores |
| `ring_seen == 1`, `records` climbing | Working ✅ |

**Card requirements:** **FAT32** (not exFAT — `_FS_EXFAT = 0`) with **8.3
filenames** (`_USE_LFN = 0`). `LOGxxxx.BIN` fits.

---

## 5. Triage: debug symptoms

| Symptom | Cause | Fix |
|---|---|---|
| CM4 shows `0xA05F0000` right after attach (step 2) | **Expected** — reset by the attach, now parked asleep | Not an error. Continue: resume CM7, then CM4. |
| CM4 **stuck** at `0xA05F0000`; `loops` frozen | Wrong order — CM7 resumed before CM4 attached, so the attach-reset re-slept the M4 with the one-shot wake spent | Power-cycle, redo §2.3 in order |
| CM4 stops at `main` | Normal — the attach reset it there | Continue; optionally turn stop-at-main OFF |
| CM4 breakpoints grey / wrong lines | Stale flash, fresh symbols | Do the §2.2 Option A setup, or reflash CM4 |
| "ST-Link busy" / CM4 won't connect | Two GDB servers fighting for one probe | Both configs need **shared ST-Link** (they have it). Don't run a second server. |
| `loops` is a huge frozen number | M4 asleep — you are reading dead-domain RAM | Get the M4 running; trust only a *changing* `loops` |
| CM7 "running" but logging dead | CM7 trapped in `Error_Handler` (the still-active timeout at `main.c` ~159) | Comment that one out too, debug-only |
| Both cores dead after a CubeMX regen | `.project` corruption | [TRAPS.md](TRAPS.md) §1 |
| HCU receives CAN fine but transmits nothing | see [TRAPS.md](TRAPS.md) §3 | check `stats` tx line and `canreg` |

---

## 6. A sensible first-power-on sequence

1. Card in, USB in, HV **off**. Power up.
2. `ConfigGUI.py` → Connect. Check the banner shows a sane `State_Enum`.
3. `stats` — expect `loop OK overruns 0`, `SD log OK`, `AIR ---- not armed`
   before the first model step and `OK` after.
4. **CAN Bus** tab → Start stream. Confirm the ECU/dash ids appear on bus 1 and
   the BMS (`0x6B1`) on bus 2. ODrive ids (`0x001`/`0x021`, `0x009`/`0x029`,
   `0x017`/`0x037`) will be **absent until HV is live** — that is expected, see
   [CONTROL_STRATEGY.md](CONTROL_STRATEGY.md) §8.
5. **Config** tab → confirm the bench bypasses (`Bench_Engine_Off`,
   `Bench_Speed_Bypass`, `Bench_Velocity_Mode`, `Vel_Scale`) read **0** unless
   you are deliberately bench testing.
6. **Set clock → PC time** so the log file header is timestamped.
7. **Live Telemetry** → Start stream. Watch `Brake_Pressure` with your foot off
   the brake and confirm `Brake_Zero_Offset` is above it
   ([CONTROL_STRATEGY.md](CONTROL_STRATEGY.md) §5).
8. Pull the card, `python hcu_logdecode.py LOG0000.BIN`, confirm a populated CSV.
