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

| You want to… | Edit this one file | Then |
|---|---|---|
| Add a **tunable parameter** | `CM7/Core/Inc/params.def` | rebuild CM7 |
| Add a **received CAN message** | `CM7/Core/Inc/can1_messages.def` (or `can2_…`) | + 1 bridge line + Simulink inports |
| Add a **logged signal** (to SD) | `Shared/log_signals.def` | rebuild **both** cores |
| **Watch a signal live** on the PC | `CM7/Core/Inc/telem_signals.def` | rebuild CM7 |
| Add an **input/output** (pin/ADC) | Simulink port + 1 bridge line | rebuild CM7 |

> **Logging and live telemetry are two separate choices.** `log_signals.def` =
> what gets *written to the SD card*. `telem_signals.def` = what you can *watch
> live in the GUI*. A signal can be in either, both, or neither — edit them
> independently.

Each `.def` file starts with a comment block repeating these exact rules, so you
can work straight from the file. This guide is the overview.

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
  the board clamps to it on every change, so a typo can never apply a silly
  value. Floats need the `f` (e.g. `1.5f`); ints are plain (e.g. `200`).

That one line gives you, automatically:

- storage in the firmware, the power-on default, and the safe clamp;
- the console commands `list` / `get name` / `set name value` / `save`;
- a row in the **Config GUI** (it discovers parameters from the board — nothing
  to edit there);
- flash save/load, with old flash auto-rejected when you change the list.

**Grouping (optional):** the Config GUI files parameters under labelled
headings. The heading comes from the board, so you set it in `params.def` with a
one-line marker — everything below it (until the next marker) goes under that
heading:

```c
PARAM_SECTION( "Torque Vectoring" )
PARAM_F32( TV_Gain, 0.0f, 0.0f, 2.0f )   /* appears under "Torque Vectoring" */
```

A parameter above every marker lands in a generic "Other" group. Sections are
presentation only — they do not touch storage, flash, or the layout
fingerprint, so re-grouping never invalidates a saved config.

**If you also want the *model* to use the parameter** (most of the time you do):

1. In Simulink, add a root **Inport** with the **same name** (`kp`), data type
   `single` for `PARAM_F32` or `int32` for `PARAM_I32`. Regenerate the code.
2. In `CM7/Core/Src/model_bridge.c`, add **one line** in the
   "feed tunable parameters to the model" section:

   ```c
   MODEL_PARAM(kp);
   ```

   That copies the live value into the model every step. This line is left for
   you on purpose, so you stay in control of what reaches the model. A
   firmware-only parameter — say a logging rate — just does not get a line.

**You do NOT bump any version number.** The firmware fingerprints the parameter
list; when you change it the old saved flash is recognised as out-of-date and
the board safely uses your new defaults. After reflashing, press **Save** once.

> ⚠ **Changing a *default* does not invalidate flash** — the fingerprint covers
> names and types, not values. A previous `save` will still load the old value.
> After changing a default you must `set <name> <newvalue>; save`, or
> `defaults; save`.

> ⚠ **Safety-critical limits do not belong here.** AIR/precharge thresholds,
> fault trips, the torque slew rate — keep those `const` in firmware so no
> console `set` and no edited flash image can move them.

---

## 2. Add a received CAN message

**Edit `CM7/Core/Inc/can1_messages.def`** (dash/ECU bus, 1 Mbit/s) or
`can2_messages.def` (tractive/inverter bus, 500 kbit/s) — add one line:

```c
CAN_MSG( DASH_STATUS, 0x123 )
```

Then two more small steps (also documented at the top of `can.h`):

1. `model_bridge.c`, gather section: `CAN_FEED( bus1, DASH_STATUS, dash_status );`
2. Simulink: add two inports — `dash_status` (uint8, width 8 = the data bytes)
   and `dash_status_age` (uint32 = milliseconds since last seen; use it to fail
   safe when a message goes stale).

To **send** a frame, in the bridge dispatch section:
`Can_Send(2, 0x0B0, HCU_V2_Simulink_Y.my_tx, 8);` — pack the bytes in Simulink.

> The C side never decodes a frame. It hands you eight raw bytes and an age; the
> unpack and scaling are visible Simulink blocks. That is why there is no DBC —
> see [ARCHITECTURE.md](ARCHITECTURE.md) §8.

> ⚠ **Leave the `CAN_FEED` line commented until the model actually uses the
> bytes.** Embedded Coder prunes an unconnected root inport, so referencing it
> from C fails the CM7 build at link.

---

## 3. Add a logged signal (record it to the SD card)

**Edit `Shared/log_signals.def`** — add one line naming a Simulink port:

```c
LOG_Y( float,   Torque_Cmd )   /* a model OUTPUT (a root Outport / _Y field) */
LOG_U( uint8_t, bus1_ok    )   /* a model INPUT  (a root Inport  / _U field) */
```

Use the C type matching the Simulink data type (the file's header lists the
mapping; `single`→`float`, `boolean`/`uint8`→`uint8_t`, etc.). `tick` (the time
axis) and `overruns` (loop health) are added for you.

**Rebuild BOTH cores** (CM7 and CM4) — the record layout is shared. The new
column appears in the decoded CSV automatically, labelled with the port name.

Scalars only. Arrays (an 8-byte CAN payload as one field) need a `LOG_ARRAY`
macro that does not exist yet — ask, it is a five-minute add.

---

## 4. Watch a signal live on the PC (telemetry)

This is the "see every variable on my laptop, live" feed. It is **separate from
logging**: it never touches the SD card, it just streams to the GUI's **Live
Telemetry** tab (and feeds the **Plot** tab's picker).

**Edit `CM7/Core/Inc/telem_signals.def`** — add one line naming a Simulink port.
Unlike logging you do **not** state the type; the firmware reads it straight from
the model, so it cannot be wrong:

```c
TELEM_Y( Torque_Scale_Factor )      /* a model OUTPUT (a root Outport / _Y field) */
TELEM_U( bus1_ok )                  /* a model INPUT  (a root Inport  / _U field) */
TELEM_ARRAY_Y( Torque_Left, 8 )     /* an array output (e.g. an 8-byte CAN frame) */
TELEM_ARRAY_U( APPS, 8 )            /* an array input  (raw CAN payload)          */
```

The file covers most current ports but is **no longer exhaustive** — several
newer ports are deliberately not streamed. [STATUS.md](STATUS.md) §7 lists
exactly which. To stop watching a signal, delete its line (or comment it out).

**Rebuild CM7 only** — telemetry is CM7-only, no CM4 rebuild. The signal appears
or disappears in the GUI automatically.

By hand in PuTTY: `telem on` / `telem off`, `telem rate 50` (1–100 Hz),
`telem list` (the schema). Each live frame is one line:
`#T tick=1234 User_LED_1=1 Torque_Scale_Factor=0.8571 APPS=12,0,255,...`.

---

## 5. Watch the raw CAN bus (sniffer)

The **CAN Bus** tab is a live sniffer: **every** id the board receives on either
bus — including ids that are *not* in `can1_messages.def` / `can2_messages.def` —
with the latest 8 data bytes, a frame counter, a measured message rate and how
long ago each id was last seen. It self-discovers, so there is **nothing to
configure**: plug in, press **▶ Start stream**, and the table fills in.

- **Rate** sets how often the whole table is re-sent (1–50 Hz; the data itself is
  captured on every frame in the Rx interrupt, so the counts are exact
  regardless).
- **Clear** forgets every captured id (board and grid) for a fresh start.
- **Snapshot** dumps the table once without streaming.
- **Filter** narrows to an id or bus (type e.g. `017` or `2:`).

Read-only — it never transmits, and it is completely independent of the model
demux and the telemetry stream. Only standard (11-bit) frames appear, because
the CAN filter deliberately rejects extended frames. Bump `CANSNIFF_MAX_IDS` in
`can_sniffer.h` only if `cansniff` reports drops.

By hand: `cansniff on` / `off` / `rate 20` / `clear` / `list`. Each streamed row
is one line: `#C <bus> <id> <count> <age_ms> <dlc> <data>`, e.g.
`#C 2 017 4211 3 8 12AB34CD5678EF90`.

---

## 6. Add an input or output (a pin or sensor)

1. In Simulink, draw the **Inport** (input) or **Outport** (output), name it, set
   its type; regenerate.
2. In `model_bridge.c`, add **one line**:
   - input (gather): `HCU_V2_Simulink_U.my_in = <read the hardware>;`
   - output (dispatch): `<write the hardware> = HCU_V2_Simulink_Y.my_out;`

The hardware reality (which pin, active-low or not) lives in that one bridge
line; all the logic stays in Simulink.

---

## 7. Using the Config GUI (`ConfigGUI.py`)

The easy way to talk to the board over USB. The board exposes one USB serial
(COM) port.

```bash
pip install pyserial
```

```bash
python ConfigGUI.py
```

`matplotlib` is optional and only enables the Plot tab. tkinter ships with
Python (on Debian/Ubuntu: `sudo apt install python3-tk`).

- **Connect:** pick the board's COM port and press *Connect*. Close PuTTY first —
  only one program can own the port. It remembers the port and **auto-reconnects**
  if you unplug/replug or power-cycle the board.
- **Parameters** appear automatically — current value on the left, type a new
  value and press *Set* (or Enter). Out-of-range values are clamped by the board
  and it tells you.
- **Set clock → PC time:** one click sets the board's wall-clock. The board has
  no battery, so its clock resets on every power cycle — set it at the start of a
  session so SD-log files are timestamped. Logging works even if you forget;
  files are numbered `LOGxxxx.BIN`.
- **Save to flash:** writes the current values so they survive a power cycle. Do
  this stationary, in the pit. **Load defaults** resets to the built-in values
  (then Save to keep them) — a factory reset.
- **START** pulses the GUI start request; **CLEAR FAULT** acknowledges a latched
  supervisor error *and* clears the AIR fail-safe stall latch in one press.

Five tabs:

| Tab | What it shows |
|---|---|
| **Live Telemetry** | every signal from `telem_signals.def`, updating in place. Start/Stop, rate 1–100 Hz, filter box. Rows auto-discover. |
| **Plot** | pick signals → live strip charts (needs matplotlib) |
| **CAN Bus** | the raw sniffer, section 5 |
| **Config** | the tunable parameters, section 1 |
| **Console** | the raw text log and a box to type any command |

Everything the GUI does you can type in PuTTY: `list`, `get kp`, `set kp 2`,
`save`, `defaults`, `time`, `time set 2026-06-14 12:00:00`, `stats`,
`safety`, `telem on|off|rate 50|list`,
`cansniff on|off|rate 20|clear|list`, `canreg`, `ping`.

---

## 8. Reading the SD-card logs

Pop the card out, then on your PC:

```bash
python hcu_logdecode.py LOG0000.BIN
```

```bash
python hcu_logdecode.py *.BIN
```

It reads `Shared/log_signals.def` to know the columns, so the CSV always matches
what the firmware recorded. Keep the `.def` that matches a season's logs (it is
git-tracked) — if you changed the logged signals later, decode old logs with the
old `.def`.

---

## 9. After you edit: build and flash

| Changed | Rebuild |
|---|---|
| `params.def`, `telem_signals.def`, a `can*.def`, the bridge | **CM7** |
| `log_signals.def` | **BOTH cores** (the record layout is shared) |

After changing any parameter list, press **Save** once on the GUI or console so
the new layout is written to flash.

> **Heads-up for whoever runs CubeMX:** re-generating code can corrupt the
> CubeIDE `.project` files on **both** cores — a known dual-core bug. None of the
> edits in *this* guide need CubeMX; they are all `.def` files, the bridge, or
> Simulink. If you do run a *Generate Code*, follow the recovery in
> [TRAPS.md](TRAPS.md) §1 **before** building.
