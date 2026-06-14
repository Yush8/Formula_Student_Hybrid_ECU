/*
 * clock.c
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * CM7 RTC wall-clock + publisher. See clock.h for the contract. The RTC itself
 * (LSE source, 24h, 127/255 predividers -> 1 Hz) is configured by CubeMX in
 * MX_RTC_Init(); this module only seeds it, reads it, and publishes the datetime
 * into the shared IPC struct for CM4. No control logic here.
 */

#include "clock.h"
#include "main.h"      /* hrtc handle, HAL_RTC_*, HAL_GetTick(), __DMB() */
#include "logger.h"    /* -> hcu_ipc.h: g_hcu_ipc (the shared struct) */
#include <stdio.h>     /* snprintf */
#include <stdlib.h>    /* atoi    */

extern RTC_HandleTypeDef hrtc;   /* defined by CubeMX in main.c (MX_RTC_Init) */

#define CLOCK_PUBLISH_MS  1000u

static uint32_t s_last_publish_ms;

/* ---- publish current RTC datetime into the shared IPC (seqlock WRITER) -----
 * CM7 is the sole writer of g_hcu_ipc.now. seq is made odd before the fields are
 * touched and even again after, so CM4's reader can retry until it sees a stable
 * snapshot. */
static void publish(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    /* On STM32 the calendar shadow registers lock on a time read and only
     * unlock after the date is read - so always read date right after time,
     * even though we use both here anyway. */
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    uint32_t seq = g_hcu_ipc.now.seq;
    g_hcu_ipc.now.seq = seq + 1u;        /* odd: update in progress */
    __DMB();
    g_hcu_ipc.now.year   = (uint16_t)(2000 + d.Year);
    g_hcu_ipc.now.month  = d.Month;
    g_hcu_ipc.now.day    = d.Date;
    g_hcu_ipc.now.hour   = t.Hours;
    g_hcu_ipc.now.minute = t.Minutes;
    g_hcu_ipc.now.second = t.Seconds;
    g_hcu_ipc.now.valid  = 1u;
    __DMB();
    g_hcu_ipc.now.seq = seq + 2u;        /* even: snapshot stable */
}

bool Clock_Set(int year, int month, int day, int hour, int minute, int second)
{
    if (year  < 2000 || year  > 2099) return false;
    if (month < 1    || month > 12)   return false;
    if (day   < 1    || day   > 31)   return false;
    if (hour  < 0    || hour  > 23)   return false;
    if (minute < 0   || minute > 59)  return false;
    if (second < 0   || second > 59)  return false;

    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    t.Hours          = (uint8_t)hour;
    t.Minutes        = (uint8_t)minute;
    t.Seconds        = (uint8_t)second;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    d.Year    = (uint8_t)(year - 2000);
    d.Month   = (uint8_t)month;           /* RTC month is 1..12 in BIN format */
    d.Date    = (uint8_t)day;
    d.WeekDay = RTC_WEEKDAY_MONDAY;        /* weekday not tracked; value unused  */

    if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return false;
    publish();
    return true;
}

/* Seed the RTC from the compiler's __DATE__ / __TIME__ (the firmware build
 * time). __DATE__ is "Mmm dd yyyy" (year always at offset 7), __TIME__ is
 * "hh:mm:ss". This just gives a sane starting point before `time set`. */
static void seed_from_build_time(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *bd = __DATE__;
    const char *bt = __TIME__;

    int month = 1;
    for (int i = 0; i < 12; i++) {
        if (bd[0] == months[i*3] && bd[1] == months[i*3+1] && bd[2] == months[i*3+2]) {
            month = i + 1;
            break;
        }
    }
    int day  = atoi(bd + 4);   /* atoi skips the leading space for single digits */
    int year = atoi(bd + 7);
    int hh   = atoi(bt);
    int mm   = atoi(bt + 3);
    int ss   = atoi(bt + 6);

    Clock_Set(year, month, day, hh, mm, ss);
}

void Clock_Init(void)
{
    seed_from_build_time();              /* overrides MX_RTC_Init's 2000-01-01 */
    s_last_publish_ms = HAL_GetTick();
    publish();
}

void Clock_Service(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_last_publish_ms) >= CLOCK_PUBLISH_MS) {
        s_last_publish_ms = now;
        publish();
    }
}

void Clock_Format(char *buf, size_t n)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
             2000 + d.Year, d.Month, d.Date, t.Hours, t.Minutes, t.Seconds);
}
