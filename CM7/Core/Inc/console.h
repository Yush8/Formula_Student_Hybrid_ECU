/*
 * console.h
 *
 *  Created on: 10 Jun 2026
 *      Author: Yusha
 */

#ifndef CONSOLE_H
#define CONSOLE_H

/*
 * Simple line-based command console over USB CDC.
 *
 *   Console_Init()  - call once, after the USB device has started
 *   Console_Poll()  - call every superloop iteration (non-blocking-ish)
 *
 * Commands:
 *   help | ?            list commands
 *   list               show all config variables and values
 *   get <name>         read one variable
 *   set <name> <val>   change a variable  (ints accept 0x.. hex too)
 *   ping               link check -> "pong"
 *
 * The variables in console.c are MOCK values for now. To make them real,
 * just point the table entries at your actual model-parameter struct fields.
 */

void Console_Init(void);
void Console_Poll(void);

#endif /* CONSOLE_H */
