/*
 * events.h  --  the event recorder (the "what happened, and when" stream)
 *
 *  Created on: 22 Sep 2026
 *      Author: Yusha
 *
 * Telemetry tells you what a signal IS. This tells you when it CHANGED.
 *
 * Once per model step it compares a short list of discrete signals against
 * their previous values and emits one line per change:
 *
 *     #E <tick> <name> <old> <new>
 *
 * The GUI renders those as the Events timeline, so after a fault you can read
 * the sequence off a list instead of scrubbing a graph:
 *
 *     1812  State_Enum         5 -> 6
 *     1812  Fault_Code         0 -> 20
 *     1812  Inverter_Enable    1 -> 0
 *
 * WHY THE STEP AND NOT THE TELEMETRY RATE
 * Telemetry samples at 1..100 Hz, so two changes inside one sample window are
 * indistinguishable - you cannot tell whether the fault or the shutdown came
 * first. This runs on the 100 Hz model step, on every step, so the ORDER is
 * exact and nothing that lasts a single tick is ever missed.
 *
 * WHAT gets watched is the one list in CM7/Core/Inc/event_signals.def - same
 * one-line-per-signal X-macro idea as telem_signals.def / params.def, so you
 * never edit C to add a model signal. Firmware-internal events (the AIR
 * fail-safe latch, CAN bus-off, SD-log drops) are raised by this module
 * directly, since they are not model signals.
 *
 * Cost: a handful of compares per step and nothing at all when nothing changes.
 * Emission goes out through the same busy-safe USB path as the console, from
 * the superloop - never from an ISR.
 *
 * Console protocol (handled in console.c):
 *   events            status: on/off, watched count, how many raised since boot
 *   events on|off     start/stop emitting #E lines (default ON - they are rare)
 *   events list       re-emit the last EVENT_RING_LEN events (history after a
 *                     reconnect, so the GUI timeline is not empty)
 *   events clear      forget the stored history and zero the counter
 */

#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

/* How many past events are kept for `events list`. Each costs ~40 bytes. */
#define EVENT_RING_LEN   48u

void     Events_Init(void);      /* call once at boot, after Model_Init()     */
void     Events_Poll(void);      /* call right after Model_Step()             */

void     Events_SetStreaming(int on);
int      Events_IsStreaming(void);
uint32_t Events_Count(void);     /* events raised since boot                  */
uint32_t Events_Watched(void);   /* how many signals are being watched        */

void     Events_PrintStatus(void);  /* `events`       */
void     Events_PrintHistory(void); /* `events list`  */
void     Events_Clear(void);        /* `events clear` */

/*
 * Raise an event from firmware code (not a model signal). Used by this module
 * for the AIR fail-safe latch, CAN bus-off and SD-log drops; available if you
 * ever want to timestamp something else. `name` must be a literal / static
 * string - it is not copied.
 */
void     Events_Raise(const char *name, long old_val, long new_val);

#endif /* EVENTS_H */
