# Extending the HCU — the Simulink-only guide

You should never need to write C to add to this firmware. The C is just the
"postman": it carries raw values between the hardware and your Simulink model.
**Simulink is the brain; you do all the logic there.** Everything you add to the
firmware is **one line in a `.def` file** — the same idea every time:

> Add one line → the whole firmware (storage, console, GUI, SD log, decoder)
> updates itself. The pieces are generated from that one line, so they can never
> get out of sync.

If you can add a CAN ID, you can do all of this.

---

## The only files you ever edit

| You want to…                         | Edit this one file                         | Then |
|--------------------------------------|--------------------------------------------|------|
| Add a **tunable parameter**          | `CM7/Core/Inc/params.def`                  | rebuild CM7 |
| Add a **received CAN message**       | `CM7/Core/Inc/can1_messages.def` (or `can2_…`) | + 1 bridge line + Simulink inports |
| Add a **logged signal** (to SD)      | `Shared/log_signals.def`                   | rebuild **both** cores |
| Add an **input/output** (pin/ADC)    | Simulink port + 1 bridge line              | rebuild CM7 |

Each `.def` file starts with a comment block that repeats these exact rules, so
you can work straight from the file. This guide is the overview.

---

## 1. Add a tunable parameter (change values live over USB)

A "parameter" is a number you can read and change at runtime from the Config GUI
(or PuTTY) and store to flash — a gain, a limit, an enable flag.

**Edit `CM7/Core/Inc/params.def` and add ONE line:**

```c
/*           name            default   min       max     */
PARAM_F32(   kp,             1.500f,   0.0f,     100.0f )   /* a decimal value  */
PARAM_I32(   torque_limit,   200,      0,        240    )   /* a whole number   */
```

- `PARAM_F32` = a decimal value (matches a Simulink **single**).
- `PARAM_I32` = a whole number (matches a Simulink **int32**; use `0`/`1` for a
  boolean).
- `default` is what the board powers up with. `min`/`max` is the **safe range** —
  the board clamps to it on every change, so a typo can never apply a silly value.
  Floats need the `f` (e.g. `1.5f`); ints are plain (e.g. `200`).

That one line gives you, automatically:
- storage in the firmware, the power-on default, and the safe clamp;
- the console commands `list` / `get name` / `set name value` / `save`;
- a row in the **Config GUI** (it discovers parameters from the board — nothing
  to edit there);
- flash save/load (and old flash is auto-rejected when you change the list, so
  you fall back to safe defaults — see "Flash & save" below).

**If you also want the *model* to use the parameter** (most of the time you do):
1. In Simulink, add a root **Inport** with the **same name** (`kp`), data type
   `single` for `PARAM_F32` or `int32` for `PARAM_I32`. Regenerate the code.
2. In `CM7/Core/Src/model_bridge.c`, uncomment/add **one line** in the
   "feed tunable parameters to the model" section:
   ```c
   MODEL_PARAM(kp);
   ```
   That copies the live value into the model every step. (This line is left for
   you to add on purpose, so you stay in control of what reaches the model. A
   firmware-only parameter — say a logging rate — just doesn't get a line.)

**You do NOT bump any version number.** The firmware fingerprints the parameter
list; when you change it, the old saved flash is recognised as out-of-date and
the board safely uses your new defaults. After reflashing, just press **Save** once.

---

## 2. Add a received CAN message

**Edit `CM7/Core/Inc/can1_messages.def`** (dash/ECU bus) or `can2_messages.def`
(tractive/inverter bus) — add one line:

```c
CAN_MSG( DASH_STATUS, 0x123 )
```

Then two more small steps (already documented at the top of `can.h`):
1. `model_bridge.c`, gather section: `CAN_FEED( bus1, DASH_STATUS, dash_status );`
2. Simulink: add two inports — `dash_status` (uint8, width 8 = the data bytes)
   and `dash_status_age` (uint32 = milliseconds since last seen; use it to fail
   safe when a message goes stale).

To **send** a frame, in the bridge dispatch section:
`Can_Send(2, 0x0B0, HCU_V2_Simulink_Y.my_tx, 8);` — pack the bytes in Simulink.

---

## 3. Add a logged signal (record it to the SD card)

**Edit `Shared/log_signals.def`** — add one line naming a Simulink port:

```c
LOG_Y( float,   Torque_Cmd )   /* a model OUTPUT (a root Outport / _Y field) */
LOG_U( uint8_t, bus1_ok    )   /* a model INPUT  (a root Inport  / _U field) */
```

Use the C type that matches the Simulink data type (the file's header lists the
mapping; `single`→`float`, `boolean`/`uint8`→`uint8_t`, etc.). `tick` (the time
axis) is added for you.

**Rebuild BOTH cores** (CM7 and CM4) — the record layout is shared. The new column
shows up in the decoded CSV automatically, labelled with the port name.

---

## 4. Add an input or output (a pin or sensor)

1. In Simulink, draw the **Inport** (input) or **Outport** (output), name it, set
   its type; regenerate.
2. In `model_bridge.c`, add **one line**:
   - input (gather):  `HCU_V2_Simulink_U.my_in  = <read the hardware>;`
   - output (dispatch): `<write the hardware> = HCU_V2_Simulink_Y.my_out;`

The hardware reality (which pin, active-low or not) lives in that one bridge line;
all the logic stays in Simulink.

---

## Using the Config GUI (`ConfigGUI.py`)

This is the easy way to talk to the board over USB. The board exposes one USB
serial (COM) port.

```
pip install pyserial      # one-time
python ConfigGUI.py
```

- **Connect**: pick the board's COM port and press *Connect*. (Close PuTTY first —
  only one program can own the port.) It remembers the port and **auto-reconnects**
  if you unplug/replug or power-cycle the board.
- **Parameters** appear automatically — current value on the left, type a new value
  and press *Set* (or Enter). Out-of-range values are clamped by the board and it
  tells you.
- **Set clock → PC time**: one click sets the board's wall-clock to your PC's
  current time. (The board has no battery, so its clock resets on every power
  cycle — set it at the start of a session so the SD-log files are timestamped.
  Logging itself works even if you forget; files are numbered `LOGxxxx.BIN`.)
- **Save to flash**: writes the current values so they survive a power cycle. Do
  this stationary / in the pit. **Load defaults** resets to the built-in values
  (then Save to keep them) — a factory reset.

Everything the GUI does, you can also type in PuTTY (`list`, `get kp`, `set kp 2`,
`save`, `defaults`, `time`, `time set 2026-06-14 12:00:00`, `ping`).

---

## Reading the SD-card logs

Pop the card out, then on your PC:

```
python hcu_logdecode.py LOG0000.BIN          # -> LOG0000.csv
python hcu_logdecode.py *.BIN                # decode them all
```

It reads `Shared/log_signals.def` to know the columns, so the CSV always matches
what the firmware recorded. Keep the `.def` that matches a season's logs (it's
git-tracked) — if you changed the logged signals later, decode old logs with the
old `.def`.

---

## After you edit: build & flash

- Changed `params.def`, a `can*.def`, or the bridge → **rebuild CM7**, reflash.
- Changed `log_signals.def` → **rebuild BOTH CM7 and CM4** (the record layout is
  shared), reflash both.
- After changing any parameter list, press **Save** once on the GUI/console so the
  new layout is written to flash.

> Heads-up for whoever runs CubeMX: re-generating code can corrupt the CubeIDE
> `.project` files (a known dual-core bug). None of the edits in *this* guide need
> CubeMX — they're all in `.def` files, the bridge, or Simulink. If you do run a
> CubeMX *Generate Code*, follow the recovery in `HANDOFF.md` §12 before building.
