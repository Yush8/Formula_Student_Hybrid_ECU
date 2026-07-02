/*
 * telem.h  --  live USB telemetry stream
 *
 *  Created on: 30 Jun 2026
 *      Author: Yusha
 *
 * Streams selected Simulink model signals out of the USB-CDC console so you can
 * watch every variable live on the PC (the GUI's "Live telemetry" panel). It is
 * a read-only OBSERVER of the model - it never changes inputs or outputs and is
 * driven from the superloop in slack time, NOT from the control step, so a slow
 * host can never cost a model tick.
 *
 * WHAT gets streamed is the single list in CM7/Core/Inc/telem_signals.def - the
 * same one-line-per-signal X-macro idea as params.def / log_signals.def, so the
 * firmware, the `telem list` schema and the GUI grid can never drift apart.
 *
 * This is SEPARATE from SD logging (Shared/log_signals.def): telemetry = watch
 * live, logging = write to card. A signal can be in either, both, or neither.
 *
 * Console protocol (all handled in console.c, this module does the work):
 *   telem            show streaming status (on/off, rate, signal count)
 *   telem on         start streaming live frames
 *   telem off        stop streaming
 *   telem rate <hz>  set frame rate, 1..100 Hz (default 20)
 *   telem list       dump the signal schema: one "<name> <type> <len>" per line
 *
 * Each streamed frame is one line:
 *   #T tick=<n> <name>=<value> <name>=<value> ...      (arrays: v0,v1,v2,...)
 */

#ifndef TELEM_H
#define TELEM_H

#include <stdint.h>

void     Telem_Init(void);          /* call once at boot (after Console_Init)   */
void     Telem_Service(void);       /* call every superloop; emits when due     */

void     Telem_SetStreaming(int on);
int      Telem_IsStreaming(void);
void     Telem_SetRateHz(uint32_t hz);   /* clamped to 1..TELEM_MAX_RATE_HZ     */
uint32_t Telem_GetRateHz(void);
uint32_t Telem_Count(void);         /* number of signals (excluding tick)       */

void     Telem_PrintSchema(void);   /* `telem list` - emits the schema lines    */
void     Telem_PrintStatus(void);   /* `telem`      - emits one status line      */

#define TELEM_MAX_RATE_HZ   100u    /* can't exceed the model step rate         */

#endif /* TELEM_H */
