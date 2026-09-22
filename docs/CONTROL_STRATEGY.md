# HCU V2 — Control Strategy

**Why the car behaves the way it does.** Every decision here was made
deliberately; several were made *instead of* an obvious alternative, and the
reasoning matters more than the code. All of this logic lives in
`HCU_V2_Simulink.slx` — this document records the design, the numbers and the
rationale so they survive outside the model file.

> Until now this material existed only in AI session memory. It is the single
> highest-value thing in this documentation set: the firmware can be re-read, but
> the *reasoning* could not be.

For how the C side plumbs these signals see
[ARCHITECTURE.md](ARCHITECTURE.md) §3 and §8.

---

## 1. The vehicle

**Hybrid, not an EV.** The **rear axle is combustion**; the **front axle has two
independent electric motors** — two ODrives (nodes 0 and 1), one per front wheel,
through a Neu 2530 motor and a 5:1 planetary gearbox each.

Consequences that shape everything below:

- **All torque vectoring is front-axle.**
- **There is no undriven wheel**, so there is no clean ground-speed source. The
  only speed signals in software are the two front motor encoder estimates
  (`Velocity_0`, `Velocity_1`, turns/s), which are wheelspin-corrupted under
  power. Road speed comes from the ECU over CAN instead (see §6).
- **One throttle commands both powertrains.** The front e-drive is an
  independent drive path with no clutch — which creates the launch hazard in §6.
- **Low-voltage system**, so **no EV rules apply**: no mandated regen cutoff
  speed, no EV-specific torque rules. FSAE brake plausibility (T.4.3) still
  applies and is implemented.
- Rear (combustion) wheel-speed encoders were **dropped** for this year's car;
  TIM2/TIM5 were freed in the `.ioc`.

The ASM330LHHX IMU is fitted but **not yet wired into the model**. When it is, it
gives yaw *rate* for feedback — not ground speed. A trustworthy ground-speed
source (GPS, or IMU integration) remains an open hardware question.

---

## 2. The torque chain, end to end

Read top to bottom; each stage is a separate, visible part of the model.

```
APPS (CAN 0x604)  ──► APPS plausibility ──► APPS_Clean [%]
                                               │
                             1-D lookup: Drive_Torque_Map
                                               │  tau_drive  (>= 0)
Brake pressure (CAN 0x627) ─► − Brake_Zero_Offset ─► saturate[0,60]
                                               │
                             1-D lookup: Regen_Shape          (§5)
                                               │  tau_regen  (>= 0)
                                               ▼
                           tau_axle = tau_drive − tau_regen      (signed blend)
                                               │
                        Torque vectoring split (§4)  ──►  tau_L , tau_R
                                               │
                    ┌──────────────────────────▼──────────────────────────┐
                    │  Torque_Power_Limiter  (bms_power_limit, §3)        │
                    │  one shared scale factor s, preserves the L/R ratio │
                    └──────────────────────────┬──────────────────────────┘
                                               │
                      launch speed gate (§6)  ×  (DRIVE branch only)
                                               │
                     Rate Limiter Dynamic (§7) — symmetric slew
                                               │
          × Inverter_Enable   × NOT(brake-plausibility latch)    ← INSTANT cuts
                                               │
                        × Left_Direction / Right_Direction
                                               │
                    Byte Pack ──► Torque_Left / Torque_Right (8 bytes)
                                               │
                 C: Can_Send(2, 0x00E / 0x02E) every 10 ms tick     (§8)
```

**Ordering is load-bearing.** The slew limiter sits *before* the
`× Inverter_Enable` gate and the power limiter's `s → 0`, both of which are
downstream multiplies. So a fault still zeroes torque **instantly**, regardless
of the limiter's internal ramp state.

---

## 3. BMS power limiter — the pack-current clamp

**File:** a single MATLAB Function block `bms_power_limit` (subsystem
`Torque_Power_Limiter`). It is a **pure limiter**: it takes an already-signed
per-wheel torque and makes it safe. No decoding, no brake, no APPS inside it.

### Why feedforward and not feedback

Decided deliberately: a feedforward power-budget clamp is **predictive** (no
reaction delay, no overshoot, no windup) and is the simplest thing to set up.
Yusha wanted **one** method, not a hybrid of two.

### The maths

The binding pack constraint is on **total** power, not per-wheel — which is why
the motor count drops out (we sum the real per-motor powers instead of
pre-allocating DCL/N):

```
tau_i1    = clamp( tau_i_req , −Motor_Regen_Max , +Motor_Torque_Max )
P_demand  = Σ  tau_i1 · 2π·|vel_i|          (signed: drive +, regen −)

              ⎧ Drive_Efficiency · BMS_Margin · DCL · V_pack       if P_demand ≥ 0
P_budget  =   ⎨
              ⎩ BMS_Margin · ACL · V_pack / Regen_Efficiency       if P_demand < 0

s         = clamp01( P_budget / max(|P_demand|, ε) )
tau_out_i = s · tau_i1 · Direction_i
```

**One shared `s` for both wheels.** Uniform scaling preserves the L/R torque
ratio, so the torque-vectoring intent survives the power limit in proportion.
Mixed-sign vectoring (one wheel regenerating) is still correct because
`P(s) = s · P_demand` is linear, so `s` lands *net* pack power on budget.

**Efficiency divides on the charge side, and the conservative direction flips.**
Charge-safe means *over*estimating efficiency, which is why `Regen_Efficiency`
is a separate parameter tuned **high** (~0.95) and must never be set to
`Drive_Efficiency`'s 0.85.

### Fail-safe gate

`s = 0` if any of: BMS frame age ≥ 150 ms, either encoder age ≥ 150 ms, or
`V_pack ≤ 0`. The 150 ms window is a `const` inside the function, not a
parameter — it is a safety trip.

`DCL`, `ACL` and `V_pack` are deliberately **not** in that global gate. They
self-disable through `P_budget ≤ 0 ⇒ s = 0`, so an invalid limit only cuts *its
own* side. (An earlier version had `ACL ≤ 0` in the global gate, which meant a
full pack — legitimately ACL = 0 — wrongly killed drive torque.)

### Low-speed blind spot — and who covers it

The power model is blind at stall: `P = τ·ω → 0` as ω → 0, but DC current stays
high (`I ≈ τ/Kt`, all I²R heat). This clamp lets full `Motor_Torque_Max` through
at standstill. That is **deliberate delegation**, not an oversight:

| Layer | Covers |
|---|---|
| ODrive `current_lim` / `dc_max_positive_current` | fast inverter guard, kHz rate |
| this clamp + `Motor_Torque_Max` | speed-dependent pack/DCL guard |
| BMS hard trip | last resort |

Set each ODrive's `dc_max_positive_current` so that 2 × per-ODrive stall DC ≤ DCL.
The ODrive bus currents *are* already on CAN (`ODrive_x_VI`), so a secondary
current clamp is available if a dyno run shows stall current biting — not added,
because of the one-method rule.

### Tuning traps that cause over-limiting

1. **Do not double-dip efficiency.** At the limit,
   `I_pack = (Drive_Efficiency / η_real) · BMS_Margin · DCL` — voltage *and*
   efficiency cancel when η is set truthfully. Setting `Drive_Efficiency` below
   the real η stacks a second cushion on top of `BMS_Margin`. Set
   `Drive_Efficiency` to the **measured** η and let `BMS_Margin` be the only
   cushion. The 0.85 default is probably a lowball.
2. `Regen_Efficiency = 0.95` is a deliberate regen *under*-use (safe). Dial it
   toward the true η for more regen.
3. `Regen_Cutoff_Speed` set too high kills regen across usable speeds. Keep it low.
4. `DCL`/`ACL`/`V_pack` are unfiltered — one glitch frame = a torque dropout. If
   the BMS turns out noisy, slew-limit the *budget*, never the limit.
5. **Verify the BMS scaling against the datasheet.** Assumed: DCL = bytes 0:1 LE
   (1 A/bit), ACL = bytes 2:3 LE, `V_pack` = bytes 4:5 LE × 0.1 V. Wrong scale =
   gross mis-limit.

### Known coverage gaps (accepted)

- The limiter assumes **all** DCL is traction's. If the LV pack feeds
  non-trivial auxiliaries, reserve headroom by lowering `BMS_Margin` — we measure
  ODrive current, not total pack current.
- **No thermal derating.** Relies on the ODrives' own thermal limits.

### Post-processing the logs

The logged signals are chosen to make efficiency recoverable offline:

```
P_elec = Bus_Voltage_0·Bus_Current_0 + Bus_Voltage_1·Bus_Current_1
P_mech = 2π·(Torque_Request_Left·Velocity_0 + Torque_Request_Right·Velocity_1)
drive efficiency = P_mech / P_elec      (use rows where Torque_Scale_Factor == 1)
```

---

## 4. Torque vectoring — Option A

**Chosen: direct steering→torque-split, empirically tuned.**
**Rejected: bicycle-model yaw-rate target.**

The bicycle model needs a trustworthy vehicle speed V. With both axles driven
there is no undriven wheel, and the front encoder speeds are wheelspin-corrupted
exactly when vectoring matters most. Rather than build a controller on a speed
signal we do not trust, we took the direct map and tune it empirically.

The split is applied **before** the power limiter, so the single shared `s`
scales both wheels and the ratio survives the clamp. **Drive torque only — no
vectoring under regen braking.**

| Parameter | Default | Meaning |
|---|---|---|
| `TV_Gain` | **0.0** | Nm of split per unit of steering. **0 = TV inert**, so the car drives exactly as before until deliberately tuned up. Confirm the sign on a stand first, then raise slowly on a skidpad. |
| `Steering_Centre` | 0.0 | straight-ahead offset. **Leave at 0 while steering is a constant-0 placeholder** — a non-zero centre with no real steering signal acts as a fake steering input and commands a constant split. |
| `Steering_Deadzone` | 2.0 | no vectoring within ± this of centre; rejects on-centre noise |
| `Max_Torque_Split` | 2.0 | hard cap on \|ΔT\| — a steering fault can never command a wider split. Keep ≤ `Motor_Torque_Max`. |

Steering angle arrives from the ECU on CAN (`ECU_Misc`, 0x627). A steering
plausibility check (out-of-range or stale ⇒ force the split to 0) is **good
practice, not a rule**, and is still deferred — acceptable while `TV_Gain = 0`.

---

## 5. Regen

Regen is a **blend**, not a mode: `tau_net = tau_drive(APPS) − tau_regen(brake)`,
clamped to `[−Motor_Regen_Max, +Motor_Torque_Max]`. An algebraic blend gives
better overlap and trail-braking feel than an either/or switch, and it is
continuous through zero (at the crossover `P_demand ≈ 0`, so `s ≈ 1` on both
branches and the DCL→ACL flip is invisible).

**Source:** brake pressure from the ECU on CAN (`ECU_Misc` / 0x627, bytes 4:5
× 0.01). It inherits the same 150 ms staleness gate as everything else —
staleness is handled **upstream** in the decision block, not inside the limiter.

**The curve is a visible Simulink lookup table (`Regen_Shape`), not parameters.**
Deadzone and full-scale are baked into the table so the team tunes the *shape* in
the model. Only the safety backstops are parameters:

| Parameter | Default | Role |
|---|---|---|
| `Regen_Efficiency` | 0.95 | charge-side efficiency; **higher = more conservative** (opposite of `Drive_Efficiency`) |
| `Motor_Regen_Max` | 4.4 Nm | per-wheel regen magnitude cap |
| `Regen_Cutoff_Speed` | 2.0 turns/s | low-speed taper |

### Why there is a low-speed taper with no rule demanding one

Regen torque is a **holding** torque: `P = τω → 0` as ω → 0, but τ does not. So
without a taper, `s → 1` commands full `Motor_Regen_Max` at standstill and the
motor reverse-drives a stopped car. The taper fades regen out in the last bit of
speed and hands off to the hydraulics. This is physics, not regulation — which is
why it is a tunable rather than a `const`.

**Fail to zero, always.** Brake stale, ACL stale, ACL ≤ 0 or `V_pack` ≤ 0 ⇒ regen
0. This is safe precisely because the hydraulic brakes are independent and
mandatory: losing regen is never dangerous.

**No software brake-plausibility cut for regen** — a separate *hardware*
plausibility device acts on brake pressure and APPS directly, so duplicating it
in software would be redundant. (The FSAE T.4.3 APPS/brake cut in §6 is a
different check and *is* implemented.)

### `Brake_Zero_Offset` — the bug this parameter exists for

Symptom: full throttle produced **negative** `Base_Torque_Demand` and the car
made no torque.

Cause: the brake sensor rests at about **3.0** counts. `Regen_Shape` starts at
2.0, so the resting reading was read as light braking → the model took the regen
branch → the accelerator branch never ran (they are mutually exclusive). It
looked like a plausibility trip but was not — that latch needs brake ≥ 25.

Fix: `saturate[0,60]( Brake_Pressure − Brake_Zero_Offset )` feeds `Regen_Shape`.
Default **4.0**, just above the observed rest, so the hijack is dead out of the
box.

**Calibration:** foot *off* the brake, read live `Brake_Pressure`, set this to
that value + ~1. It is a **sensor calibration, not a bench bypass — safe to
`save`.** Plausibility (≥ 25) and the brake flag (> 10) still read **raw**
`Brake_Pressure`, so a few counts of offset never weakens those safeties. It is
the exact analogue of `Steering_Centre`, for the brake.

---

## 6. Launch speed gate and brake plausibility

### The hazard

Clutch in, in gear, driver revs the engine to warm it. One throttle commands
both powertrains, and the front e-drive has no clutch — so the front motors
launch the stationary car.

### Decision: Option 1 — inhibit drive torque until the car is rolling

No launch assist from standstill (there is no launch button). Chosen over a
clutch/launch-intent permit because **road speed alone cannot distinguish "rev
in place" from "e-launch"** — so the only robust discriminator is whether the car
is actually moving.

### Speed source — no new CAN message

`vehicleSpeed` already rides in the **APPS frame (0x604, MoTeC Frame 5:
ppsA, ppsB, vehicleSpeed, SPARE)** at `APPS[4..5]`, big-endian, scale **0.036 →
km/h**, range 0–300, treated unsigned. So it is decoded in the existing APPS
unpack block, gated by the existing `APPS_age < 50 ms` switch, with **no new
`CAN_MSG`, no new `CAN_FEED`, and no extra inport.** Cross-checked against
front-encoder speed with `max()` for redundancy.

### Gates

| Gate | Rule | Placement | Why there |
|---|---|---|---|
| **Launch permit** | SR latch, **5 km/h ON / 3 km/h OFF**, OR'd with `Bench_Speed_Bypass` | multiplies the DRIVE branch **before** the slew limiter | so release ramps up smoothly instead of stepping |
| **Brake plausibility (FSAE T.4.3)** | set-dominant SR latch: `latch = (brake ≥ 25 AND APPS_Clean ≥ 20) OR (APPS_Clean > 5 AND latch_prev)` | the **instant** `× Inverter_Enable` multiply | the rules require an immediate cut |

The plausibility latch holds until APPS ≤ 5 % **regardless of the brake**, which
is what T.4.3 requires. The SET threshold is 20 % APPS where the rule ceiling is
25 % — earlier, therefore compliant and more conservative.

All trip thresholds here are **Simulink constants, not parameters** — safety
trips must not be runtime-movable.

### `Bench_Speed_Bypass`

With the car on a jack the wheels never roll, so the gate would hold torque at 0
forever. This boolean ORs *only* the speed gate open. Engine sync, HV, precharge,
APPS plausibility, brake plausibility, the AIR fail-safe and the stall latch all
remain live. **For a full engine-off jack test, set both `Bench_Engine_Off` and
`Bench_Speed_Bypass` to 1.** Never `save` either as 1.

---

## 7. Gearbox-protection torque slew

Protects the Neu 2530 / 5:1 planetary from shock loading. A pedal stab, or a
throttle↔brake (drive↔regen) reversal, is otherwise a torque **step** that slams
the gear teeth across the backlash.

**Symmetric slew on the signed base torque demand — both directions.** A
drive-only "limit the rise, let the fall be instant" trick does not work on a
regen car: with regen the fall is a real driver command, and **the reversal
through zero is the worst gear event**, so both directions must be ramped.

**A ramp *time*, not a raw Nm/s.** The physically safe, transferable quantity is
the 0→full-torque ramp time. Fix that and let Nm/s follow `Motor_Torque_Max`, so
the ramp stays gentle whatever the cap is set to and can never accidentally
become aggressive.

```
TORQUE_FULL_RAMP_S = 0.20 s          /* 0 -> full torque */
rate = Motor_Torque_Max / 0.20       /* Nm/s, fed as Torque_Rate_Up / _Down */
```

At the current 4.4 Nm cap that is **22 Nm/s = 0.22 Nm per 10 ms tick**, ×5 at the
gearbox output. A full drive↔regen reversal spans about twice the range, so it
takes ~0.40 s.

**Where it lives:** `const` in `model_bridge.c`, fed to the inports
`Torque_Rate_Up` / `Torque_Rate_Down` each step. Deliberately **not** in
`params.def` — a gear-protection limit is safety-critical and must not be
movable by a console `set` or an edited flash image.

**Known residual:** a fault *clearing* while the pedal is held can step torque
back up (bounded, rare). The dangerous fast *cut* is fully handled. The full fix
is a resettable limiter, to be added only if the rig shows re-application shock.

---

## 8. ODrive interface

Two ODrive Pro/S1, CAN nodes 0 and 1, on bus 2.

| Direction | Id | Frame | Pattern |
|---|---|---|---|
| RX | `0x001` / `0x021` | Heartbeat | `CAN_FEED` |
| RX | `0x009` / `0x029` | Encoder estimates (velocity = bytes 4:7 float, turns/s) | `CAN_FEED` |
| RX | `0x017` / `0x037` | Bus voltage & current | `CAN_FEED` |
| TX | `0x00E` / `0x02E` | `Set_Input_Torque` | every tick, when in torque mode |
| TX | `0x00D` / `0x02D` | `Set_Input_Vel` | every tick, when in velocity mode |
| TX | `0x00B` / `0x02B` | `Set_Controller_Mode` | gated, IDLE only |
| TX | `0x007` / `0x027` | `Set_Axis_State` | gated, until heartbeat confirms |

Setpoints must repeat every 10 ms: the ODrive rx-watchdog disarms the axis if the
stream stops. That is also a safety feature — a frozen model stops the stream and
the axes disarm within ~100 ms.

### The both-frames bug — never stream both setpoints

`Set_Input_Vel` (0x0D) bytes 4..7 are a **torque feed-forward** that writes the
**same** `controller.input_torque` register as `Set_Input_Torque` (0x0E). Stream
both every tick and they fight: in `TORQUE_CONTROL`, 0x0D's feed-forward (0)
stamps the real torque request back to 0 the instant after 0x0E set it. Earlier
code did exactly this and claimed it was safe. It was not.

So the bridge streams **exactly one** frame, selected by the model's
`Velocity_Mode_Active` — the single source of truth.

### Bench velocity mode, switched over CAN

`Bench_Velocity_Mode` (0 = torque/race, 1 = velocity/bench) feeds the model,
which builds the `Set_Controller_Mode` payload
(`Control_Mode` uint32 LE bytes 0–3: 1 = TORQUE, 2 = VELOCITY; `Input_Mode` bytes
4–7: 1 = PASSTHROUGH, 2 = VEL_RAMP) and raises `_req` **only while that axis's
heartbeat reports IDLE**. So the mode is applied in the pre-arm window, **never
rewritten on a live axis**, and it self-heals after an ODrive reboot. No USB, no
`odrivetool`, no per-inverter fiddling.

`Vel_Scale` (default **0**) sets the velocity setpoint magnitude, tapped *after*
the limiter — so the wheels only spin with HV and the BMS live, and a
power-cycle (if unsaved) returns it to 0. The ODrive's own `vel_limit` is the
real backstop.

> **Set each ODrive's SAVED `control_mode` to `TORQUE_CONTROL`** so a power-cycle
> always reverts to the race-safe mode.
>
> Velocity control chases a speed setpoint regardless of load. Select it only
> with the car jacked and the wheels off the ground. Never `save` it as 1.

### ODrives are dark until HV is live — and that is handled

ODrive Pro/S1 only boot (including their isolated CAN transceiver) when the DC
bus is present. With HV off, both inverters **send nothing and ACK nothing** on
bus 2. This was investigated and confirmed **not** to be a problem:

- Never-seen slots report `age_ms = UINT32_MAX` with zeroed data, so nothing
  looks fresh.
- The HCU streams setpoints from boot regardless of HV, so bus 2 needs an
  always-on ACK source — **the BMS (0x6B1) provides it**, so bus 2 never goes
  bus-off during the dark window.
- `Torque_Scale_Factor` is forced to 0 on stale encoder/BMS or `V_pack ≤ 0`, so a
  dark ODrive means zero torque anyway.
- `Inverter_Enable` cannot be set until DRIVE, which is reachable only through
  PRE_CHARGE and RELAY_SWAP — so HV is live by then.
- **Precharge completion keys on the ODrives waking up:** it waits for
  ODrive-reported bus voltage ≥ 90 % of BMS-reported `Pack_Voltage`, with VI
  zeroed when stale so it waits for the boot. 10 s timeout → Error. This requires
  ODrive boot voltage < 90 % of pack, always true for an LV (≤ 58 V) system.

Minor and non-safety: ODrive heartbeat *age* is not checked, so while dark,
`Heartbeat[4] = 0` and the model perpetually requests IDLE, transmitting
`0x007`/`0x027` every tick. Harmless chatter; self-clears when the ODrive boots.
Torque enable never depends on the heartbeat.

---

## 9. Safety supervisor states and fault codes

The model streams `State_Enum` and, on a fault, `Fault_Code`. The GUI decodes
both into the banner. **These numbers must match what the Stateflow chart
writes** — the GUI tables in `configgui/protocol.py` are the mirror.

| `State_Enum` | State | Note |
|---|---|---|
| 0 | INIT / UNKNOWN | chart has not run |
| 1 | HV OFF | shutdown / no HV |
| 2 | STANDBY | HV up, idle, ready |
| 3 | PRE-CHARGE | bus charging |
| 4 | RELAY SWAP | AIR closing; also the engine-sync gate |
| 5 | DRIVE | armed, motors live |
| 6 | ERROR / FAULT | **latched** |

| `Fault_Code` | Meaning | Status |
|---|---|---|
| 0 | no fault | |
| 20 | BMS zero-limit (DCL and CCL both 0) | live |
| 40 | pre-charge timeout | live |
| 10, 21, 30, 31, 32, 41, 50, 51, 60 | SDC open, BMS stale, APPS implausible, APPS+brake, APPS stale, relay-swap/sync timeout, ODrive fault, ODrive stale, CAN bus-off | **reserved** — numbers allocated, not yet routed into Error_State |

### Error is a latch, and how it clears

Once a confirmed fault puts the chart in Error_State it stays there, so you do
not have to power-cycle the car to clear a fault. The model exposes one boolean
inport `Reset_Req`, gated in the chart by **`Reset_Req && !BMS_Fault`**:

- it **acknowledges** a fault that has already cleared; it can never suppress a
  live one;
- it drops the chart to **HV_OFF, never straight to DRIVE** — the full
  brake + start re-sequence still runs.

Two request sources are OR'd into that one inport in the bridge: the **physical
reset button** (User Button 2, PD13, active-low) and the **GUI Clear-Fault
button** (`Error_Reset_GUI`, a momentary pulse).

> `Reset_Req` clears only the **model's** Error latch. A controller **freeze** is
> latched separately by `air_safety.c` and is cleared by `safety reset`. The GUI
> Clear-Fault button issues **both**, so one press covers either.

### Two start sources, one sequence

The physical `Start_Button` (User Button 1, PD12, active-low: pulled up to +3V3
through 4.7 kΩ, shorted to GND when pressed) and `Start_Button_GUI` are simply
**OR'd in Simulink**. Both run the identical ready-to-drive sequence and every
interlock. The GUI button can *request* a start; it can never bypass a gate.

---

## 10. Parameter inventory

Every tunable, what it is for, and whether it is safe to `save`. Full text —
including the warnings — is in `CM7/Core/Inc/params.def`.

| Section | Parameter | Default | Range | Safe to `save`? |
|---|---|---|---|---|
| Controller / PID | `kp`, `ki`, `torque_limit`, `Parameter_1` | 1.5, 0.2, 200, 0 | | legacy / unused placeholders |
| BMS Power Limiter | `Drive_Efficiency` | 0.85 | 0–1 | yes — set to **measured** η |
| | `BMS_Margin` | 0.90 | 0–1 | yes — the only intended cushion |
| | `Motor_Torque_Max` | 4.4 Nm | 0–50 | yes |
| | `Left_Direction` / `Right_Direction` | −1 / +1 | −1…1 | yes — confirm mounting |
| Regen / Charge | `Regen_Efficiency` | 0.95 | 0.5–1 | yes |
| | `Motor_Regen_Max` | 4.4 Nm | 0–50 | yes |
| | `Regen_Cutoff_Speed` | 2.0 turns/s | 0.01–100 | yes |
| | `Brake_Zero_Offset` | 4.0 | 0–50 | **yes** — sensor calibration |
| Torque Vectoring | `TV_Gain` | **0.0** | 0–2 | yes (0 = TV off) |
| | `Steering_Centre` | 0.0 | ±30 | keep 0 until steering is real |
| | `Steering_Deadzone` | 2.0 | 0–20 | yes |
| | `Max_Torque_Split` | 2.0 Nm | 0–50 | yes, keep ≤ `Motor_Torque_Max` |
| Bench / Test | `Vel_Scale` | **0.0** | 0–50 | leave 0 |
| | `Bench_Velocity_Mode` | **0** | 0/1 | ❌ **never save as 1** |
| | `Bench_Engine_Off` | **0** | 0/1 | ❌ **never save as 1** |
| | `Bench_Speed_Bypass` | **0** | 0/1 | ❌ **never save as 1** |
| Drive / Start | `Start_Button_GUI` | 0 | 0/1 | momentary pulse; rests at 0 |
| | `Error_Reset_GUI` | 0 | 0/1 | momentary pulse; rests at 0 |

**Three of these are drive-enable interlock bypasses.** A saved 1 makes the car
boot ready to arm the e-drive with no engine, or with no speed interlock. Flip
for the test, then power-cycle (or set 0) before running for real.

> **Changing a default does not invalidate flash.** `layout_id` fingerprints
> names and types, not values — so a prior `save` still loads the old value.
> After changing a default you must `set <name> <newvalue>; save`, or
> `defaults; save`.

---

## 11. `*_FEED_READY` flags — current state

Compile-time guards in `model_bridge.c`. Each is 1 only when the matching
Simulink port exists. See [ARCHITECTURE.md](ARCHITECTURE.md) §3 for why.

| Flag | Value | Gates |
|---|---|---|
| `TORQUE_SLEW_FEED_READY` | **1** | `Torque_Rate_Up` / `_Down` → the symmetric slew (§7) |
| `START_GUI_FEED_READY` | **1** | `Start_Button_GUI` → OR'd with the physical start |
| `SPEED_GATE_FEED_READY` | **1** | `Bench_Speed_Bypass` → ORs the launch speed gate open (§6) |
| `VEL_MODE_FEED_READY` | **1** | `Bench_Velocity_Mode`, `Set_Controller_Mode_*`, `Velocity_Mode_Active` (§8) |
| `RESET_FEED_READY` | **1** | `Reset_Req` ← button OR `Error_Reset_GUI` (§9) |
| `USER_LED_FEED_READY` | **0** | `User_LED_3` / `User_LED_4` — outports **not yet added** to the model |

`User_LED_3` (PD10) and `User_LED_4` (PD11) are free status LEDs whose *meaning*
is yours to define. Add two boolean root Outports with exactly those names,
regenerate, flip the flag to 1, and the model drives them (active-low handled
in C).
