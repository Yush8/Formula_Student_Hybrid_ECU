/*
 * clock.h
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * CM7 wall-clock owner. CM7 owns the RTC (LSE, NO VBAT backup -> the calendar
 * resets every power cycle). At boot the RTC is seeded with the firmware BUILD
 * TIME; the console `time set` command sets the real wall-clock at session
 * start. Once a second the current datetime is published into the shared IPC
 * struct (g_hcu_ipc.now) through a seqlock, so CM4 can stamp log files and
 * answer get_fattime() WITHOUT ever touching the RTC peripheral itself.
 *
 * Filenames on the card use a sequence counter (LOGxxxx.BIN), so logging is
 * robust even before the clock is set; the datetime is for the file header /
 * FAT timestamps only. See HANDOFF s16.2 and Shared/hcu_ipc.h.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stddef.h>

/* Seed the RTC from the firmware build time and publish once. Call ONCE at
 * boot, AFTER MX_RTC_Init() (needs hrtc) and AFTER Log_Init() (it writes into
 * the shared struct that Log_Init sets up). */
void Clock_Init(void);

/* Call every superloop pass. Republishes the datetime to the IPC at ~1 Hz
 * (rate-limited internally off HAL_GetTick). Cheap when not due. */
void Clock_Service(void);

/* Validate and set the RTC wall-clock, then publish immediately. Returns false
 * if any field is out of range (year 2000-2099). Used by the console. */
bool Clock_Set(int year, int month, int day, int hour, int minute, int second);

/* Write the current RTC datetime as "YYYY-MM-DD HH:MM:SS" into buf. */
void Clock_Format(char *buf, size_t n);

#endif /* CLOCK_H */
