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
#include "telem.h"         /* live model-signal stream for the `telem` command */
#include "can_sniffer.h"   /* raw CAN bus sniffer for the `cansniff` command */
#include "air_safety.h"    /* g_air_safety / AirSafety_Reset() for `safety` + the `stats` AIR line */
#include "events.h"        /* the #E event recorder for the `events` command */
#include "main.h"          /* HAL_GetTick() */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>       /* strtoul() for `telem rate` */

/* Release name for `version`. Bump by hand when you want to name a build; the
 * build date/time printed beside it is stamped by the compiler, so forgetting
 * to bump this loses you a label, never the truth about what is on the board. */
#define HCU_FW_VERSION  "2.4.0"

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

/* Public so telem.c can stream through the same busy-safe TX path. */
void Console_Out(const char *s)
{
    Console_Write((const uint8_t *)s, (uint16_t)strlen(s));
}

/* STATIC buffer on purpose: CDC_Transmit_FS does NOT copy - USBD_CDC_SetTxBuffer
 * just stores the pointer and the USB endpoint reads from it AFTER this function
 * has returned. A local would have its stack frame popped immediately and any
 * interrupt (TIM6 at 100 Hz, FDCAN) would reuse that memory mid-transfer, so the
 * host receives garbage. Same reason telem.c's s_frame and can_sniffer.c's
 * s_stream are static. Safe to share one buffer: every caller is the superloop
 * console path (Console_Poll -> process_line), never an ISR, never reentrant. */
static void Console_Printf(const char *fmt, ...)
{
    static char buf[128];
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

/* FDCAN Last Error Code (PSR.LEC) -> short tag for the `stats` tx line. The one
 * that matters here is "ACK!" (3): the controller transmitted a frame and no
 * other node acknowledged it - i.e. our TX reached the bus but nobody received
 * it (dead TX wire, wrong bus, or lone node). "none"/"nc" with TEC 0 means we
 * are not putting frames on the bus at all. */
static const char *lec_str(uint8_t lec)
{
    switch (lec) {
        case 0u:  return "none";
        case 1u:  return "stuff";
        case 2u:  return "form";
        case 3u:  return "ACK!";
        case 4u:  return "bit1";
        case 5u:  return "bit0";
        case 6u:  return "crc";
        case 7u:  return "nc";     /* no change since last read */
        default:  return "?";
    }
}

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
        uint32_t lost   = (b == 1u) ? g_can_stats.bus1_rx_lost    : g_can_stats.bus2_rx_lost;
        uint32_t rec    = (b == 1u) ? g_can_stats.bus1_recoveries : g_can_stats.bus2_recoveries;
        uint32_t txfail = (b == 1u) ? g_can_stats.bus1_tx_fail    : g_can_stats.bus2_tx_fail;
        uint32_t txdone = (b == 1u) ? g_can_stats.bus1_tx_done    : g_can_stats.bus2_tx_done;

        const char *state;
        if      (hh.bus_off && hh.recovery_gave_up) state = "FAIL  bus-off (gave up)    ";
        else if (hh.bus_off)                        state = "FAIL  bus-off (recovering) ";
        else if (lost > 0u || rec > 0u)             state = "WARN  recovered            ";
        else                                        state = "OK                         ";
        Console_Printf(" CAN%u    %srx_lost %lu  recoveries %lu\r\n",
                       (unsigned)b, state,
                       (unsigned long)lost, (unsigned long)rec);

        /* TX-path health: read straight from the FDCAN protocol/error registers.
         * pend = frames queued awaiting TX (0 = engine keeping up; climbing = backing up)
         * TEC  = transmit error counter (~128 & parked with EP = un-ACKed TX)
         * lec  = last protocol error (ACK! = we transmit but nobody acknowledges)
         * sent = frames the engine actually finished sending (should climb when TXing)
         * send_fail = Can_Send() rejections (queue full / HAL add failed) */
        Console_Printf("         tx  pend %u  TEC %u%s  lec %s  sent %lu  send_fail %lu\r\n",
                       (unsigned)hh.tx_pending,
                       (unsigned)hh.tx_err_cnt,
                       hh.error_passive ? " ERR-PASSIVE" : "",
                       lec_str(hh.last_err_code),
                       (unsigned long)txdone,
                       (unsigned long)txfail);
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

    /* ---- independent controller-freeze fail-safe ----
     *   FAIL -> AIRs are being forced open: the model step froze (stall latch).
     *           The model's command is overridden until `safety reset` / power-cycle.
     *   WARN -> not enforcing now, but a freeze has latched since boot (count>0).
     *   OK   -> model is alive and has AIR authority. (SDC intent lives in the model
     *           + the hardware shutdown cutoff - not policed here; docs/ARCHITECTURE.md section 5.) */
    const char *astate;
    if      (g_air_safety.stall_latched)        astate = "FAIL  stall-latched (loop froze) ";
    else if (!g_air_safety.armed)               astate = "----  not armed (no model step)  ";
    else if (g_air_safety.stall_trips)          astate = "WARN  recovered                  ";
    else                                        astate = "OK                               ";
    Console_Printf(" AIR     %sAIR=%s pre=%s  stall_trips %lu\r\n",
                   astate,
                   g_air_safety.air_closed ? "closed" : "open",
                   g_air_safety.pre_closed ? "closed" : "open",
                   (unsigned long)g_air_safety.stall_trips);

    /* ---- loop headroom ----
     * overruns tells you the loop has ALREADY missed a deadline. These numbers
     * tell you how close you are to missing one, which is the question you
     * actually want answered before a session. Budget is 10000 us per tick:
     *   step  how long Model_Step() takes
     *   late  how long after the hardware tick the step actually started */
    const sched_timing_t *T = &g_sched_timing;
    if (T->samples > 0u) {
        Console_Printf(" step    %lu us now  min %lu  avg %lu  max %lu  (budget %lu)\r\n",
                       (unsigned long)T->step_us_last,
                       (unsigned long)T->step_us_min,
                       (unsigned long)(T->step_us_sum / T->samples),
                       (unsigned long)T->step_us_max,
                       (unsigned long)(1000000u / SCHED_RATE_HZ));
        Console_Printf(" late    %lu us now  min %lu  avg %lu  max %lu  over %lu steps\r\n",
                       (unsigned long)T->lat_us_last,
                       (unsigned long)T->lat_us_min,
                       (unsigned long)(T->lat_us_sum / T->samples),
                       (unsigned long)T->lat_us_max,
                       (unsigned long)T->samples);
    }
}

/* ------------------------------------------------------------------ */
/* `stats json` - the same health data, machine-readable                */
/* ------------------------------------------------------------------ */
/*
 * One JSON object on one line. The GUI's Health tab reads this instead of
 * scraping the human text above, so a cosmetic tweak to the readable version
 * can never break the dashboard - and it is what an AI gets handed when you
 * export a debug bundle.
 *
 * Emitted in several Console_Printf chunks because the shared TX buffer is
 * small; they concatenate into one line on the host because nothing else
 * writes to the console between them (single-threaded superloop).
 */
static void cmd_stats_json(void)
{
    can_health_t h1, h2;
    Can_Health(1u, &h1);
    Can_Health(2u, &h2);
    const sched_timing_t *T = &g_sched_timing;
    uint32_t samples = (T->samples > 0u) ? T->samples : 1u;

    Console_Printf("#J {\"uptime_ms\":%lu,\"ticks\":%lu,\"overruns\":%lu",
                   (unsigned long)HAL_GetTick(),
                   (unsigned long)g_sched_ticks,
                   (unsigned long)g_sched_overruns);

    Console_Printf(",\"step_us\":{\"last\":%lu,\"min\":%lu,\"avg\":%lu,\"max\":%lu}",
                   (unsigned long)T->step_us_last,
                   (unsigned long)T->step_us_min,
                   (unsigned long)(T->step_us_sum / samples),
                   (unsigned long)T->step_us_max);
    Console_Printf(",\"late_us\":{\"last\":%lu,\"min\":%lu,\"avg\":%lu,\"max\":%lu}",
                   (unsigned long)T->lat_us_last,
                   (unsigned long)T->lat_us_min,
                   (unsigned long)(T->lat_us_sum / samples),
                   (unsigned long)T->lat_us_max);
    Console_Printf(",\"step_budget_us\":%lu,\"step_samples\":%lu",
                   (unsigned long)(1000000u / SCHED_RATE_HZ),
                   (unsigned long)T->samples);

    Console_Print(",\"step_hist\":[");
    for (uint32_t b = 0u; b < SCHED_HIST_BUCKETS; b++) {
        Console_Printf("%s%lu", b ? "," : "", (unsigned long)T->hist[b]);
    }
    Console_Print("],\"step_hist_edges_us\":[");
    for (uint32_t b = 0u; b < SCHED_HIST_BUCKETS; b++) {
        /* The last bucket is "and over"; report it as the budget so the GUI can
         * label it without knowing about UINT32_MAX. */
        unsigned long e = (b + 1u == SCHED_HIST_BUCKETS)
                        ? (unsigned long)(1000000u / SCHED_RATE_HZ)
                        : (unsigned long)g_sched_hist_edges[b];
        Console_Printf("%s%lu", b ? "," : "", e);
    }
    Console_Print("]");

    Console_Printf(",\"can1\":{\"bus_off\":%u,\"gave_up\":%u,\"err_passive\":%u,\"tec\":%u,"
                   "\"lec\":%u,\"tx_pending\":%u,\"rx_lost\":%lu,\"recoveries\":%lu,"
                   "\"tx_done\":%lu,\"tx_fail\":%lu}",
                   (unsigned)h1.bus_off, (unsigned)h1.recovery_gave_up,
                   (unsigned)h1.error_passive, (unsigned)h1.tx_err_cnt,
                   (unsigned)h1.last_err_code, (unsigned)h1.tx_pending,
                   (unsigned long)g_can_stats.bus1_rx_lost,
                   (unsigned long)g_can_stats.bus1_recoveries,
                   (unsigned long)g_can_stats.bus1_tx_done,
                   (unsigned long)g_can_stats.bus1_tx_fail);

    Console_Printf(",\"can2\":{\"bus_off\":%u,\"gave_up\":%u,\"err_passive\":%u,\"tec\":%u,"
                   "\"lec\":%u,\"tx_pending\":%u,\"rx_lost\":%lu,\"recoveries\":%lu,"
                   "\"tx_done\":%lu,\"tx_fail\":%lu}",
                   (unsigned)h2.bus_off, (unsigned)h2.recovery_gave_up,
                   (unsigned)h2.error_passive, (unsigned)h2.tx_err_cnt,
                   (unsigned)h2.last_err_code, (unsigned)h2.tx_pending,
                   (unsigned long)g_can_stats.bus2_rx_lost,
                   (unsigned long)g_can_stats.bus2_recoveries,
                   (unsigned long)g_can_stats.bus2_tx_done,
                   (unsigned long)g_can_stats.bus2_tx_fail);

    Console_Printf(",\"log\":{\"writes\":%lu,\"drops\":%lu,\"ring\":%lu,\"ring_max\":%u,"
                   "\"peak\":%u}",
                   (unsigned long)g_log_stats.writes,
                   (unsigned long)g_log_stats.drops,
                   (unsigned long)Log_Occupancy(),
                   (unsigned)HCU_LOG_RING_RECORDS,
                   (unsigned)g_log_stats.occ_max);

    Console_Printf(",\"air\":{\"alive\":%u,\"armed\":%u,\"latched\":%u,\"air_closed\":%u,"
                   "\"pre_closed\":%u,\"stall_age\":%lu,\"trips\":%lu,\"calls\":%lu}",
                   (unsigned)g_air_safety.model_alive,
                   (unsigned)g_air_safety.armed,
                   (unsigned)g_air_safety.stall_latched,
                   (unsigned)g_air_safety.air_closed,
                   (unsigned)g_air_safety.pre_closed,
                   (unsigned long)g_air_safety.stall_age_ticks,
                   (unsigned long)g_air_safety.stall_trips,
                   (unsigned long)g_air_safety.supervise_calls);

    Console_Printf(",\"telem\":{\"on\":%u,\"rate\":%lu,\"signals\":%lu}",
                   (unsigned)Telem_IsStreaming(),
                   (unsigned long)Telem_GetRateHz(),
                   (unsigned long)Telem_Count());

    Console_Printf(",\"events\":{\"on\":%u,\"watched\":%lu,\"raised\":%lu}}\r\n",
                   (unsigned)Events_IsStreaming(),
                   (unsigned long)Events_Watched(),
                   (unsigned long)Events_Count());
}

/* ------------------------------------------------------------------ */
/* `version` - the board's fingerprint                                  */
/* ------------------------------------------------------------------ */
/*
 * What firmware is on this board, and what shape is it? Printed into every
 * exported debug bundle, so a log can always be tied back to the build that
 * produced it. The counts are compile-time facts about the .def files, which is
 * what actually determines whether a saved tune or an old log still matches:
 * params_layout differing from a saved tune's means that tune was captured from
 * a different params.def and must not be applied blind.
 *
 * Build stamp comes from the compiler (__DATE__/__TIME__), so it is always
 * truthful with no build-system step to forget. HCU_FW_VERSION is the one thing
 * you bump by hand, when you want to name a release.
 */
static void cmd_version(void)
{
    Console_Printf("HCU V2 firmware %s\r\n", HCU_FW_VERSION);
    Console_Printf(" built      %s %s\r\n", __DATE__, __TIME__);
    Console_Printf(" core       CM7  model step %lu Hz\r\n",
                   (unsigned long)SCHED_RATE_HZ);
    Console_Printf(" params     %lu  (layout %08lX)\r\n",
                   (unsigned long)Params_Count(),
                   (unsigned long)Params_LayoutId());
    Console_Printf(" telem      %lu signals\r\n", (unsigned long)Telem_Count());
    Console_Printf(" events     %lu watched\r\n", (unsigned long)Events_Watched());
    Console_Printf(" can slots  bus1 %u  bus2 %u\r\n",
                   (unsigned)CAN1_MSG_COUNT, (unsigned)CAN2_MSG_COUNT);
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
                      "  version          firmware build + .def fingerprints\r\n"
                      "  stats            system health (loop, CAN, logging, AIR)\r\n"
                      "  stats json       the same health data, machine-readable\r\n"
                      "  stats clear      zero the stats counters\r\n"
                      "  safety           independent AIR fail-safe state\r\n"
                      "  safety reset     clear a stall latch (stationary; healthy only)\r\n"
                      "  telem            live-stream status\r\n"
                      "  telem on|off     start/stop the live model-signal stream\r\n"
                      "  telem rate <hz>  set stream rate (1-100 Hz)\r\n"
                      "  telem list       list the streamed signals + types\r\n"
                      "  cansniff         CAN sniffer status (all received ids)\r\n"
                      "  cansniff on|off  start/stop the raw CAN bus stream\r\n"
                      "  cansniff rate <hz> set snapshot rate (1-50 Hz)\r\n"
                      "  cansniff clear   forget all captured ids\r\n"
                      "  cansniff list    dump the captured id table once\r\n"
                      "  canreg           dump FDCAN TX registers (tx debug)\r\n"
                      "  events           event-recorder status (#E change stream)\r\n"
                      "  events on|off    start/stop emitting #E lines\r\n"
                      "  events list      re-emit the stored recent events\r\n"
                      "  events clear     forget the stored events\r\n"
                      "  ping             link check\r\n");

    } else if (strcmp(cmd, "stats") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {
            cmd_stats();
        } else if (strcmp(sub, "json") == 0) {
            cmd_stats_json();
        } else if (strcmp(sub, "clear") == 0) {
            Sched_ClearStats();
            Can_ClearStats();
            Log_ClearStats();
            Console_Print("stats cleared\r\n");
        } else {
            Console_Print("usage: stats | stats json | stats clear\r\n");
        }

    } else if (strcmp(cmd, "safety") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {                                  /* "safety" -> detail */
            Console_Printf("controller-freeze fail-safe: %s\r\n",
                           g_air_safety.stall_latched ? "TRIPPED (stall latched)"
                         : !g_air_safety.armed        ? "not armed"
                         :                              "OK (model has authority)");
            Console_Printf(" AIR sink     %s\r\n", g_air_safety.air_closed ? "CLOSED (energised)" : "open");
            Console_Printf(" pre-charge   %s\r\n", g_air_safety.pre_closed ? "CLOSED (energised)" : "open");
            Console_Printf(" model        %s  (age %lu ticks, armed %u)\r\n",
                           g_air_safety.model_alive ? "alive" : "STALLED",
                           (unsigned long)g_air_safety.stall_age_ticks,
                           (unsigned)g_air_safety.armed);
            Console_Printf(" stall trips  %lu\r\n", (unsigned long)g_air_safety.stall_trips);
            Console_Print(" (SDC / shutdown intent is handled in the model + hardware - not here)\r\n");
        } else if (strcmp(sub, "reset") == 0) {
            if (AirSafety_Reset())
                Console_Print("stall latch cleared\r\n");
            else
                Console_Print("refused: loop must be alive again before reset\r\n");
        } else {
            Console_Print("usage: safety | safety reset\r\n");
        }

    } else if (strcmp(cmd, "telem") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {                                  /* "telem" -> status */
            Telem_PrintStatus();
        } else if (strcmp(sub, "on") == 0) {
            Telem_SetStreaming(1);
            Telem_PrintStatus();
        } else if (strcmp(sub, "off") == 0) {
            Telem_SetStreaming(0);
            Telem_PrintStatus();
        } else if (strcmp(sub, "list") == 0) {
            Telem_PrintSchema();
        } else if (strcmp(sub, "rate") == 0) {
            char *hz = strtok(NULL, " ");
            if (!hz) { Console_Print("usage: telem rate <hz>\r\n"); return; }
            Telem_SetRateHz((uint32_t)strtoul(hz, NULL, 10));
            Telem_PrintStatus();
        } else {
            Console_Print("usage: telem | telem on|off | telem rate <hz> | telem list\r\n");
        }

    } else if (strcmp(cmd, "cansniff") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {                                  /* "cansniff" -> status */
            CanSniffer_PrintStatus();
        } else if (strcmp(sub, "on") == 0) {
            CanSniffer_SetStreaming(1);
            CanSniffer_PrintStatus();
        } else if (strcmp(sub, "off") == 0) {
            CanSniffer_SetStreaming(0);
            CanSniffer_PrintStatus();
        } else if (strcmp(sub, "clear") == 0) {
            CanSniffer_Clear();
            Console_Print("cansniff cleared\r\n");
        } else if (strcmp(sub, "list") == 0) {
            CanSniffer_PrintTable();
        } else if (strcmp(sub, "rate") == 0) {
            char *hz = strtok(NULL, " ");
            if (!hz) { Console_Print("usage: cansniff rate <hz>\r\n"); return; }
            CanSniffer_SetRateHz((uint32_t)strtoul(hz, NULL, 10));
            CanSniffer_PrintStatus();
        } else {
            Console_Print("usage: cansniff | cansniff on|off | cansniff rate <hz> | cansniff clear | cansniff list\r\n");
        }

    } else if (strcmp(cmd, "canreg") == 0) {
        Can_DumpTx();

    } else if (strcmp(cmd, "version") == 0) {
        cmd_version();

    } else if (strcmp(cmd, "events") == 0) {
        char *sub = strtok(NULL, " ");
        if (!sub) {
            Events_PrintStatus();
        } else if (strcmp(sub, "on") == 0) {
            Events_SetStreaming(1);
            Events_PrintStatus();
        } else if (strcmp(sub, "off") == 0) {
            Events_SetStreaming(0);
            Events_PrintStatus();
        } else if (strcmp(sub, "list") == 0) {
            Events_PrintHistory();
        } else if (strcmp(sub, "clear") == 0) {
            Events_Clear();
            Console_Print("events cleared\r\n");
        } else {
            Console_Print("usage: events | events on|off | events list | events clear\r\n");
        }

    } else if (strcmp(cmd, "ping") == 0) {
        Console_Print("pong\r\n");

    } else if (strcmp(cmd, "list") == 0) {
        /* Walk the layout (params + their PARAM_SECTION headers) so the GUI can
         * group parameters exactly as params.def reads. Section headers go out as
         * "# section: <name>" - the '#' keeps them clear of the "name = value"
         * lines, so anything that only understands values just ignores them. */
        char name[40], val[24];
        int is_section;
        for (uint32_t i = 0; i < Params_LayoutCount(); i++) {
            if (Params_LayoutDescribe(i, &is_section, name, sizeof(name),
                                      val, sizeof(val)) != 0)
                continue;
            if (is_section)
                Console_Printf("# section: %s\r\n", name);
            else
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
