/*
 * console.h
 *
 *  Created on: 10 Jun 2026
 *      Author: Yusha
 */

#ifndef CONSOLE_H
#define CONSOLE_H

/*
 * Simple line-based command console over USB CDC. A thin text UI over the
 * parameter store (params.c) and the wall-clock (clock.c) - no logic here.
 *
 *   Console_Init()  - call once, after the USB device has started
 *   Console_Poll()  - call every superloop iteration (non-blocking-ish)
 *
 * Commands:
 *   help | ?                       list commands
 *   list                           show all tunable parameters and values
 *   get <name>                     read one parameter
 *   set <name> <val>               change one  (ints accept 0x.. hex too; clamped)
 *   save                           write current values to flash
 *   defaults                       reset to built-in defaults (RAM only)
 *   time                           show RTC wall-clock
 *   time set YYYY-MM-DD HH:MM:SS    set the wall-clock
 *   ping                           link check -> "pong"
 *
 * The parameter list is GENERATED from params.def, so `list`/`get`/`set` pick
 * up new parameters automatically - this console needs no edit to gain one.
 * The Config GUI (ConfigGUI.py) drives this same text protocol and discovers
 * the parameters by parsing `list`, so it never needs editing either.
 */

void Console_Init(void);
void Console_Poll(void);

#endif /* CONSOLE_H */
