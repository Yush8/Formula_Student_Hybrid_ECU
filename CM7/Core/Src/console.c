/*
 * console.c
 *
 *  Created on: 10 Jun 2026
 *      Author: Yusha
 */


#include "console.h"
#include "usbd_cdc_if.h"   /* CDC_Read(), CDC_Transmit_FS(), USBD_BUSY */
#include "params.h"        /* the parameter store this console drives */
#include "clock.h"         /* Clock_Set()/Clock_Format() for the `time` command */
#include "scheduler.h"     /* g_sched_overruns / g_sched_ticks for `stats` */
#include "can.h"           /* g_can_stats / Can_Health() for `stats` */
#include "logger.h"        /* g_log_stats / Log_Occupancy() for `stats` */
#include "main.h"          /* HAL_GetTick() */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Line buffer                                                        */
/* ------------------------------------------------------------------ */
static char     line[96];
static uint16_t line_len = 0;

/* ------------------------------------------------------------------ */
/* Output helpers (busy-safe TX)                                      */
/* ------------------------------------------------------------------ */
static void Console_Write(const uint8_t *buf, uint16_t len)
{
    uint32_t t0 = HAL_GetTick();
    while (CDC_Transmit_FS((uint8_t *)buf, len) == USBD_BUSY) {
        if ((HAL_GetTick() - t0) > 10) return;   /* no host reading -> drop */
    }
}

static void Console_Print(const char *s)
{
    Console_Write((const uint8_t *)s, (uint16_t)strlen(s));
}

static void Console_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        uint16_t out = (len < (int)sizeof(buf)) ? (uint16_t)len
                                                : (uint16_t)(sizeof(buf) - 1);
        Console_Write((const uint8_t *)buf, out);
    }
}

/* ------------------------------------------------------------------ */
/* `stats` - trackside system-health readout                           */
/* ------------------------------------------------------------------ */
/*
 * One-glance health: each subsystem gets an OK / WARN / FAIL tag followed by the
 * raw counters behind the verdict. All reads are CM7-local and cheap (no card,
 * no CM4, no IRQ masking): the counters are 32-bit single-writer, so a read is
 * atomic and being a tick stale is fine for a status line. These counters are
 * for monitoring ONLY - none of them feeds the model (same policy as the live
 * g_can_stats / g_sched_overruns they wrap).
 *
 * Verdicts (what to do trackside):
 *   loop   WARN  -> the 100 Hz model step missed deadlines (overruns>0): the
 *                   loop is overloaded; control is no longer hard real-time.
 *   CANx   FAIL  -> bus is OFF right now (wiring/transceiver/no other node, or
 *                   the bus-off auto-restart gave up). No comms on that bus.
 *          WARN  -> bus is up now but has seen overflow (rx_lost>0, frames lost
 *                   to FIFO overrun) or has had to auto-restart (recoveries>0).
 *   SD log FAIL  -> records are being DROPPED (drops>0): the ring filled because
 *                   the card/CM4 can't keep up (stalled/full/absent/CM4 down).
 *          WARN  -> ring is backing up (>= half full) but not yet dropping.
 */
static void cmd_stats(void)
{
    /* ---- uptime since boot ---- */
    uint32_t s  = HAL_GetTick() / 1000u;
    uint32_t d  = s / 86400u; s %= 86400u;
    uint32_t h  = s / 3600u;  s %= 3600u;
    uint32_t mi = s / 60u;    s %= 60u;
    Console_Print("HCU stats\r\n");
    Console_Printf(" uptime  %lud %02lu:%02lu:%02lu\r\n",
                   (unsigned long)d, (unsigned long)h,
                   (unsigned long)mi, (unsigned long)s);

    /* ---- control loop (100 Hz model step) ---- */
    Console_Printf(" loop    %s  overruns %lu  ticks %lu\r\n",
                   (g_sched_overruns == 0u) ? "OK  " : "WARN",
                   (unsigned long)g_sched_overruns,
                   (unsigned long)g_sched_ticks);

    /* ---- CAN buses ---- */
    for (uint8_t b = 1u; b <= 2u; b++) {
        can_health_t hh;
        Can_Health(b, &hh);
        uint32_t lost = (b == 1u) ? g_can_stats.bus1_rx_lost    : g_can_stats.bus2_rx_lost;
        uint32_t rec  = (b == 1u) ? g_can_stats.bus1_recoveries : g_can_stats.bus2_recoveries;

        const char *state;
        if      (hh.bus_off && hh.recovery_gave_up) state = "FAIL  bus-off (gave up)    ";
        else if (hh.bus_off)                        state = "FAIL  bus-off (recovering) ";
        else if (lost > 0u || rec > 0u)             state = "WARN  recovered            ";
        else                                        state = "OK                         ";
        Console_Printf(" CAN%u    %srx_lost %lu  recoveries %lu\r\n",
                       (unsigned)b, state,
                       (unsigned long)lost, (unsigned long)rec);
    }

    /* ---- SD logging (CM7 producer view of the inter-core ring) ---- */
    uint32_t occ = Log_Occupancy();
    const char *lstate;
    if      (g_log_stats.drops > 0u)                 lstate = "FAIL";  /* losing records */
    else if (occ >= (HCU_LOG_RING_RECORDS / 2u))     lstate = "WARN";  /* backing up     */
    else                                             lstate = "OK  ";
    Console_Printf(" SD log  %s  drops %lu  ring %lu/%u (peak %u)\r\n",
                   lstate,
                   (unsigned long)g_log_stats.drops,
                   (unsigned long)occ, (unsigned)HCU_LOG_RING_RECORDS,
                   (unsigned)g_log_stats.occ_max);
}

/* ------------------------------------------------------------------ */
/* Command dispatch  (all parameter work delegates to params.c)        */
/* ------------------------------------------------------------------ */
static void process_line(char *s)
{
    char *cmd = strtok(s, " ");
    if (!cmd) return;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        Console_Print("commands:\r\n"
                      "  list             show all parameters\r\n"
                      "  get <name>       read one\r\n"
                      "  set <name> <val> change one (RAM, live)\r\n"
                      "  save             write current values to flash\r\n"
                      "  defaults         reset to built-in defaults (RAM)\r\n"
                      "  time             show RTC wall-clock\r\n"
                      "  time set <d> <t> set clock: YYYY-MM-DD HH:MM:SS\r\n"
                      "  stats            system health (loop, CAN, logging)\r\n"
                      "  stats clear      zero the stats counters\r\n"
                      "  ping             link check\r\n");

    } else if (strcmp(cmd, "stats") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {
            cmd_stats();
        } else if (strcmp(sub, "clear") == 0) {
            Sched_ClearStats();
            Can_ClearStats();
            Log_ClearStats();
            Console_Print("stats cleared\r\n");
        } else {
            Console_Print("usage: stats | stats clear\r\n");
        }

    } else if (strcmp(cmd, "ping") == 0) {
        Console_Print("pong\r\n");

    } else if (strcmp(cmd, "list") == 0) {
        char name[24], val[24];
        for (uint32_t i = 0; i < Params_Count(); i++) {
            Params_Describe(i, name, sizeof(name), val, sizeof(val));
            Console_Printf("%s = %s\r\n", name, val);
        }

    } else if (strcmp(cmd, "get") == 0) {
        char *name = strtok(NULL, " ");
        char val[24];
        if (!name) { Console_Print("usage: get <name>\r\n"); return; }
        if (Params_GetFormatted(name, val, sizeof(val)) == 0)
            Console_Printf("%s = %s\r\n", name, val);
        else
            Console_Printf("unknown parameter: %s\r\n", name);

    } else if (strcmp(cmd, "set") == 0) {
        char *name = strtok(NULL, " ");
        char *val  = strtok(NULL, " ");
        char applied[24];
        if (!name || !val) { Console_Print("usage: set <name> <value>\r\n"); return; }
        switch (Params_SetFromString(name, val, applied, sizeof(applied))) {
            case PARAM_OK:       Console_Printf("ok: %s = %s\r\n", name, applied);          break;
            case PARAM_CLAMPED:  Console_Printf("ok (clamped): %s = %s\r\n", name, applied); break;
            case PARAM_UNKNOWN:  Console_Printf("unknown parameter: %s\r\n", name);          break;
            case PARAM_BADVALUE: Console_Print("bad value\r\n");                             break;
        }

    } else if (strcmp(cmd, "save") == 0) {
        if (Params_Save() == 0) Console_Print("saved to flash\r\n");
        else                    Console_Print("save FAILED\r\n");

    } else if (strcmp(cmd, "defaults") == 0) {
        Params_LoadDefaults();
        Console_Print("defaults loaded (use 'save' to keep them)\r\n");

    } else if (strcmp(cmd, "time") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {                                  /* "time" -> show */
            char ts[24];
            Clock_Format(ts, sizeof(ts));
            Console_Printf("%s\r\n", ts);
        } else if (strcmp(sub, "set") == 0) {        /* "time set YYYY-MM-DD HH:MM:SS" */
            char *date = strtok(NULL, " ");
            char *tod  = strtok(NULL, " ");
            int y, mo, da, h, mi, s;
            if (!date || !tod) {
                Console_Print("usage: time set YYYY-MM-DD HH:MM:SS\r\n");
            } else if (sscanf(date, "%d-%d-%d", &y, &mo, &da) == 3 &&
                       sscanf(tod,  "%d:%d:%d", &h, &mi, &s) == 3 &&
                       Clock_Set(y, mo, da, h, mi, s)) {
                char ts[24];
                Clock_Format(ts, sizeof(ts));
                Console_Printf("time set: %s\r\n", ts);
            } else {
                Console_Print("bad time (use YYYY-MM-DD HH:MM:SS, year 2000-2099)\r\n");
            }
        } else {
            Console_Print("usage: time | time set YYYY-MM-DD HH:MM:SS\r\n");
        }

    } else {
        Console_Printf("unknown command: %s  (try 'help')\r\n", cmd);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void Console_Init(void)
{
    line_len = 0;
    Console_Print("\r\nHCU V2 console ready. Type 'help'.\r\n> ");
}

void Console_Poll(void)
{
    static char last_eol = 0;
    uint8_t  rx[64];
    uint16_t n = CDC_Read(rx, sizeof(rx));

    for (uint16_t i = 0; i < n; i++) {
        uint8_t c = rx[i];

        if (c == '\r' || c == '\n') {
            /* swallow the partner of a CR+LF / LF+CR pair */
            if ((c == '\n' && last_eol == '\r') ||
                (c == '\r' && last_eol == '\n')) {
                last_eol = 0;
                continue;
            }
            last_eol = c;

            Console_Print("\r\n");
            line[line_len] = '\0';
            if (line_len > 0) process_line(line);
            line_len = 0;
            Console_Print("> ");
        } else {
            last_eol = 0;

            if (c == 0x08 || c == 0x7F) {            /* backspace / DEL */
                if (line_len > 0) {
                    line_len--;
                    Console_Print("\b \b");
                }
            } else if (c >= 0x20 && c < 0x7F) {      /* printable: echo + store */
                if (line_len < sizeof(line) - 1) {
                    line[line_len++] = (char)c;
                    Console_Write(&c, 1);
                }
            }
        }
    }
}
