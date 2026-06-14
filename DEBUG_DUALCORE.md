# Dual-Core Debug — Bench Instructions (STM32H745, CM7 + CM4)

Goal: step / set breakpoints / watch live variables on **both** cores in STM32CubeIDE
(e.g. watch CM4's `g_sdlog_status` while CM7's control loop runs). Running from flash
already works (HANDOFF §16); this is the *live debug* procedure, which has an order and a
one-time config fix that the old §16.9 note glossed over.

---

## 1. The one thing to understand

CM4 boots **asleep**. Out of reset the M4 immediately enters STOP (D2 domain) and sits
there until CM7 runs far enough to release it over a hardware semaphore (HSEM). You can see
the handshake in the code:

- `CM4/Core/Src/main.c` → `HAL_PWREx_EnterSTOPMode(...)` — M4 parks here.
- `CM7/Core/Src/main.c` → `HAL_HSEM_Release(HSEM_ID_0, 0)` — CM7 wakes it, *after*
  `SystemClock_Config()`.

Two facts drive the whole procedure:
- **The CM4 wake is ONE-SHOT.** `HAL_HSEM_Release` runs exactly once at boot, then CM7 drops
  into `while(1)` and never releases again.
- **Attaching the CM4 debugger RESETS the M4** (it lands at the top of `main`, the
  `__HAL_RCC_HSEM_CLK_ENABLE()` line, then runs down into its boot STOP). The CM4 debugger then
  holds the M4 halted at that STOP.

Put those together and the order is forced: **CM7's single release must happen *after* the CM4
debugger has attached (and reset) the M4 into its STOP — and then you must resume the M4 too,
because its debugger is holding it.** Get the order wrong (e.g. resume CM7 fully, *then* attach
CM4) and the attach-reset re-sleeps the M4 with the one-shot release already spent → it orphans
powered-down at `0xA05F0000` and only a power-cycle recovers it. **The verified order is in §3.**

`0xA05F0000` = "core powered down" (M4 asleep), **not** a crash. Seeing it *mid-sequence* (after
attaching CM4, before resuming) is **expected** — the M4 is correctly parked asleep. Seeing it
*stuck* after you've resumed both cores means it orphaned. Any variable read while PC =
`0xA05F0000` is **garbage from a dead domain** — trust only a *changing* `g_sdlog_status.loops`.

---

## 2. One-time setup (do this once — it's the part that was missing)

The current launch configs have a trap: the **CM4 config has Download = OFF**, and the **CM7
config only downloads its own `.elf`**. As shipped, *nothing flashes CM4's bank 2.* That's
fine the first time only because the M4 was flashed in an earlier session — but the moment you
**rebuild CM4 and forget to reflash it, you debug stale flash with fresh symbols**: breakpoints
land on the wrong lines and you chase ghosts. Fix it once, pick one:

### Option A — recommended (ST-canonical: flash both banks from the CM7 launch)
1. `Run → Debug Configurations… → HCU_V2_CM7 Debug → Startup tab`.
2. Under the load list, **Add…** → Project `HCU_V2_CM4`, file `Debug/HCU_V2_CM4.elf`.
3. For that CM4 entry tick **Download = ON**, **Load symbols = OFF**.
4. Order the list so the **CM7 entry is last** (flash CM4 first, then CM7). **Apply.**

Now launching the CM7 config alone freshly programs **both** banks every time. Leave the CM4
config with Download OFF — it only attaches.

### Option B — manual
Leave the configs alone, but after **every** CM4 rebuild, flash CM4 once before attaching:
temporarily tick Download in the CM4 config for one run, or program
`Debug/HCU_V2_CM4.elf` to bank 2 (`0x08100000`) with STM32CubeProgrammer.

### Also required: make the CM4 attach **passive** (this is the one that bites)
`Debug Configurations → HCU_V2_CM4 Debug → Startup tab → uncheck "Set breakpoint at: main".`
The CM4 config must **attach to the running M4 and leave it alone** — no reset, no
run-to-main. "Stop at main" tells the debugger to *run the application to main*, i.e. restart
the M4 — fatal here, see §3a. The M4 is gated and only gets woken once; if the attach restarts
it, it orphans in STOP forever (`0xA05F0000`). On a correct passive attach you land in the
running `while(1)`/`SdLog_Service`, **never at `main`**. (Reset = none is already set.)

> Already correct in both `.launch` files — don't change these:
> CM7 = AP0, connect-under-reset, halt-all-on-reset ON, stop-at-main (fine for CM7).
> CM4 = AP3, **no-reset**, halt-all OFF, **Download OFF**. (Turn its **stop-at-main OFF** — above.)
> Both: **shared ST-Link ON**, **low-power-debug ON** (keeps the sleeping M4 reachable),
> separate GDB ports (CM7 61234 / CM4 61238). These match ST AN5286.

---

## 3. Routine — every debug session  ✅ VERIFIED ORDER

**Debug M7 → Debug M4 → Resume M7 → Resume M4.** The order is not negotiable (§1).

0. **Build both** projects so both `.elf`s are fresh (build CM4 explicitly — the CM7 launch
   only builds CM7). Power-cycle the board (card in) for a clean start.
1. Launch **CM7** debug (`HCU_V2_CM7 Debug`). It downloads (both banks if you did Option A)
   and **halts at `main`**. **Do NOT resume yet.**
2. Launch **CM4** debug (`HCU_V2_CM4 Debug`). It attaches — this **resets the M4**, which runs
   down into its boot STOP and is held there. **The CM4 session will show `0xA05F0000` here —
   that's correct**, the M4 is parked asleep waiting for CM7's release. Leave it.
3. **Resume CM7 (F8).** Its single `HAL_HSEM_Release` fires — the wake is now *pending* for the
   M4. (The M4 is still held halted by its own debugger, so it won't move yet — expected.)
4. **Resume CM4 (F8).** The M4 continues out of STOP, takes the pending wake, and runs through
   `HAL_Init → SdLog_Init → while(1)`. **`g_sdlog_status.loops` starts flying up.** You're live.
5. Watch what you need: add `g_sdlog_status` and `g_hcu_ipc` to Live Expressions. **Truth test:
   `g_sdlog_status.loops` must be _incrementing_** — a frozen/absurd value means the M4 is
   asleep and you're reading dead RAM. Sanity-check `&g_hcu_ipc == 0x38000000` on **both** cores.

> Once the M4 is live in `while(1)` you can halt / step / resume it normally. Only a **reset**
> re-orphans it (one-shot wake) — so don't reset the M4 mid-session; if you must, restart the
> whole §3 sequence from a power-cycle.

> **Plan B if step 4 still leaves it at `0xA05F0000`** (some gdbserver versions reset the core
> on resume too): debug-only build — comment out the M4 boot STOP
> (`HAL_PWREx_EnterSTOPMode(...)`, `CM4/Core/Src/main.c`). The M4 then never sleeps and is always
> attachable. **Revert before flashing production** — the handshake must stay for cold boot.

---

## 4. When it goes wrong

| Symptom | Cause | Fix |
|---|---|---|
| CM4 shows `0xA05F0000` right after attach (step 2) | **Expected** — M4 reset by attach, now parked asleep in boot STOP | Not an error. Continue: Resume CM7 (step 3), then Resume CM4 (step 4). |
| **CM4 stuck at `0xA05F0000` after resuming both; `loops` frozen** | Wrong order — CM7 was resumed *before* CM4 attached, so the attach-reset re-slept the M4 with the one-shot wake already spent (§1) | **Follow the verified order: Debug M7 → Debug M4 → Resume M7 → Resume M4.** Recover by power-cycling and starting §3 over. |
| CM4 stops at `main` (`__HAL_RCC_HSEM_CLK_ENABLE`) | Normal — that's the M4's first line; the attach reset it there | Continue the §3 order; it'll sleep then wake. (Optional: stop-at-main OFF skips this pause.) |
| CM4 breakpoints grey / land on wrong lines | Stale flash vs fresh symbols — rebuilt CM4 but didn't reflash it | Do the Option A setup, or reflash CM4 (Option B). |
| CM4 won't connect / "ST-Link busy" | Two GDB servers fighting for one probe | Both configs need **shared ST-Link** (they have it). Don't run a second server. |
| `loops` shows a huge frozen number | M4 is asleep — you're reading dead-domain RAM | It's garbage. Get the M4 running (above); trust only a *changing* `loops`. |
| CM7 looks "running" but logging dead | CM7 trapped in `Error_Handler` (still-active timeout at `main.c:159`) | Comment that `Error_Handler()` too (debug-only), like the one at `main.c:121`. |
| Both cores dead after a CubeMX regen | `.project` corruption (HANDOFF §12) | Restore both `.project.known-good`, delete both `Debug/`, Close/Open both projects, rebuild. |

---

## 5. `g_sdlog_status` (CM4) — read this first when logs misbehave

| Reading | Meaning |
|---|---|
| `loops == 0` | CM4 not running / flashed, or stuck in `Error_Handler` for a **non-SD** fault (no card no longer halts CM4) |
| `loops` climbing, `mounted == 0`, `last_fr == -1` | **No card / not seated** — normal "running without SD"; logging off, car unaffected |
| `loops > 0`, `mounted == 0`, `last_fr > 0` | Card present but FAT32 / format / seating problem — read `last_fr` |
| `mounted == 1`, `ring_seen == 0` | Shared-RAM address mismatch — confirm `&g_hcu_ipc == 0x38000000` |
| `ring_seen == 1`, `records` climbing | Working ✅ |

---

*Cross-refs: HANDOFF §16.9 (original note this supersedes), §16.6 (status), §15 (scheduler).
ST app note AN5286 "STM32H7x5/x7 dual-core microcontroller debugging" is the authoritative
reference for the launch-config layout above.*
