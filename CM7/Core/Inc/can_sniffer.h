/*
 * can_sniffer.h  --  raw CAN bus sniffer (diagnostic observer of both buses)
 *
 *  Created on: 5 Jul 2026
 *      Author: Yusha
 *
 * A trackside "candump": it captures EVERY standard-ID frame seen on FDCAN1 and
 * FDCAN2 - including IDs that are NOT in can1_messages.def / can2_messages.def -
 * and streams the live bus contents to the PC so the GUI's "CAN Bus" tab shows a
 * table of every ID, its latest 8 data bytes, a frame counter and how long ago it
 * was last seen. It is the CAN equivalent of the telemetry stream (telem.h): a
 * read-only OBSERVER that runs in superloop slack time, never in the model step.
 *
 * How it differs from the normal demux in can.c:
 *   - can.c    routes KNOWN ids into named slots that FEED the Simulink model.
 *   - this     tracks ALL ids purely to LOOK AT the bus; nothing here reaches the
 *              model. The two are independent - adding/using the sniffer changes
 *              neither the demux, the filters, nor the model.
 *
 * Capture is keyed on (bus, id) into a small per-bus table (insertion order, so
 * the GUI rows stay stable). It is fed one call per received frame straight from
 * the FDCAN Rx ISR in can.c, so it always reflects the bus even when the stream
 * is off - turning the stream on shows data immediately, and `cansniff list`
 * dumps the current table at any time.
 *
 * Only STANDARD (11-bit) frames appear: the global CAN filter deliberately
 * rejects extended frames (see Can_Init in can.c) because the whole system uses
 * 11-bit ids. That is intentional and unchanged here.
 *
 * Console protocol (handled in console.c; this module does the work):
 *   cansniff             show status (on/off, rate, id counts, drops)
 *   cansniff on          start streaming the live bus table
 *   cansniff off         stop streaming
 *   cansniff rate <hz>   set snapshot rate, 1..CANSNIFF_MAX_RATE_HZ (default 10)
 *   cansniff clear       forget every captured id (fresh table)
 *   cansniff list        dump the current table once (a one-shot snapshot)
 *
 * Each streamed row is one line (hex id, hex data, decimal counters):
 *   #C <bus> <id> <count> <age_ms> <dlc> <data>
 *   e.g.  #C 2 017 4211 3 8 12AB34CD5678EF90
 */

#ifndef CAN_SNIFFER_H
#define CAN_SNIFFER_H

#include <stdint.h>

/* Distinct standard ids tracked per bus. Once a bus's table is full, a frame
 * carrying a NEW id is counted (see the drops counter) but not tracked; ids
 * already in the table keep updating. A Formula Student bus has far fewer than
 * this many ids - bump it only if `cansniff` reports drops. */
#define CANSNIFF_MAX_IDS      64u

/* Upper bound on the snapshot rate. The table only needs a human-watchable
 * refresh; 10 Hz is the default and plenty. */
#define CANSNIFF_MAX_RATE_HZ  50u

/* Call once at boot, BEFORE Can_Init() enables the Rx interrupts, so the table
 * is ready for the first captured frame. */
void     CanSniffer_Init(void);

/* ISR-context capture: call once for EVERY received frame (known id or not) from
 * the FDCAN Rx callback. bus = 1 or 2. Cheap - a linear scan of the per-bus
 * table plus an 8-byte copy. */
void     CanSniffer_Capture(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t len);

/* Call every superloop pass; emits one snapshot of the whole table when the
 * stream is on and a frame period has elapsed. No-op when the stream is off. */
void     CanSniffer_Service(void);

void     CanSniffer_SetStreaming(int on);
int      CanSniffer_IsStreaming(void);
void     CanSniffer_SetRateHz(uint32_t hz);   /* clamped to 1..CANSNIFF_MAX_RATE_HZ */
uint32_t CanSniffer_GetRateHz(void);

void     CanSniffer_Clear(void);              /* forget all captured ids */

void     CanSniffer_PrintTable(void);         /* `cansniff list` - one-shot dump */
void     CanSniffer_PrintStatus(void);        /* `cansniff`      - one status line */

#endif /* CAN_SNIFFER_H */
