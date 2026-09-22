/*
 * can.c
 *
 *  Created on: 11 Jun 2026
 *      Author: Yusha
 */


/**
 * can.c - see can.h. Dual classic-CAN demux + tx.
 *
 * You normally never edit this file. To add a received message, edit
 * can1_messages.def / can2_messages.def (see can.h for the recipe).
 */
#include "can.h"
#include "can_sniffer.h"   /* CanSniffer_Capture() - raw all-id bus observer */
#include "console.h"       /* Console_Out() - for the `canreg` register dump */
#include "main.h"          /* HAL + FDCAN handle types */
#include <stdio.h>         /* snprintf for the register dump */

/* CubeMX defines these at file scope in main.c. */
extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;

/* ---- Logical bus  <->  physical peripheral -------------------------------
 * The loom is now wired the intended way: logical bus numbering matches the
 * physical peripheral. Logical bus 1 (can1_messages.def, dash/ECU harness,
 * runs at 1 Mbit/s) lands on FDCAN1; logical bus 2 (can2_messages.def,
 * tractive/inverter, 500 kbit/s) lands on FDCAN2. The ENTIRE mapping lives in
 * these two lines - everything else in the firmware talks in logical bus 1 /
 * bus 2 (CAN_FEED(bus1,...), Can_Send(1,...), the sniffer's bus column, etc.)
 * and this file routes each to the right silicon.
 *
 * If the loom is ever cross-terminated again (buses swapped relative to the
 * logical numbering), swap these two lines: BUS1_HANDLE -> hfdcan2 and
 * BUS2_HANDLE -> hfdcan1. Nothing else in the codebase changes. */
#define BUS1_HANDLE  hfdcan1   /* logical bus 1  (can1_messages.def) */
#define BUS2_HANDLE  hfdcan2   /* logical bus 2  (can2_messages.def) */

/* Diagnostics, watchable in the debugger (declared extern in can.h). */
can_stats_t g_can_stats = {0};

/* ---- Bus-off auto-restart tuning ----------------------------------------
 * Conservative: retry every 100 ms, up to 50 times (~5 s) before giving up.
 * Adjust freely - these are the only knobs.
 */
#define CAN_RECOVERY_INTERVAL_MS   100u
#define CAN_RECOVERY_MAX_ATTEMPTS  50u

/* ---- Raw buffers (written by ISR, read by Can_Snapshot under IRQ mask) ---- */
typedef struct {
    uint8_t  data[8];
    uint8_t  len;
    uint32_t last_seen;     /* HAL tick at last rx */
    uint32_t seq;           /* sole purpose: 0 => never seen (age sentinel) */
} can_slot_t;

static volatile can_slot_t s_bus1[CAN1_SLOTS];
static volatile can_slot_t s_bus2[CAN2_SLOTS];

/* ---- ID -> slot routing tables (generated from the .def lists) ----------
 * Each CAN_MSG() line becomes a { id, slot } row. A linear scan is plenty for
 * a handful of entries in the ISR. The CAN_ROUTE_END terminator means the
 * tables are valid even for a bus with zero messages, and the scan never needs
 * a separate count. Demux is keyed on (bus, ID): bus1 and bus2 have independent
 * tables, so the same ID on both buses can never cross-contaminate.
 */
#define CAN_ROUTE_END 0xFFFFFFFFu   /* not a valid 11-bit id; ends the table */

typedef struct { uint32_t id; uint8_t slot; } can_route_t;

static const can_route_t k_routes_bus1[] = {
#define CAN_MSG(name, id) { (id), (uint8_t)(name) },
#include "can1_messages.def"
#undef CAN_MSG
    { CAN_ROUTE_END, 0 }
};
static const can_route_t k_routes_bus2[] = {
#define CAN_MSG(name, id) { (id), (uint8_t)(name) },
#include "can2_messages.def"
#undef CAN_MSG
    { CAN_ROUTE_END, 0 }
};

/* ---- Helpers ------------------------------------------------------------- */

/* FDCAN_RxHeader DataLength -> classic byte count (0..8). Robust across HAL
 * versions: older HALs encode it as the FDCAN_DLC_BYTES_n macro (n << 16),
 * newer ones give the raw byte count. Normalise both, then clamp to 8. */
static uint8_t classic_len(uint32_t data_length)
{
    uint32_t v = (data_length > 0x0FU) ? (data_length >> 16) : data_length;
    return (v <= 8U) ? (uint8_t)v : 8U;
}

/* Byte count -> the DLC field the HAL expects. Using the macros keeps this
 * correct whichever encoding the installed HAL version uses. */
static uint32_t len_to_dlc(uint8_t len)
{
    switch (len) {
        case 0:  return FDCAN_DLC_BYTES_0;
        case 1:  return FDCAN_DLC_BYTES_1;
        case 2:  return FDCAN_DLC_BYTES_2;
        case 3:  return FDCAN_DLC_BYTES_3;
        case 4:  return FDCAN_DLC_BYTES_4;
        case 5:  return FDCAN_DLC_BYTES_5;
        case 6:  return FDCAN_DLC_BYTES_6;
        case 7:  return FDCAN_DLC_BYTES_7;
        default: return FDCAN_DLC_BYTES_8;
    }
}

static bool bus_off(FDCAN_HandleTypeDef *h)
{
    FDCAN_ProtocolStatusTypeDef ps;
    HAL_FDCAN_GetProtocolStatus(h, &ps);
    return ps.BusOff != 0U;
}

/* Demux one received frame into its slot. Runs in ISR context. */
static void store(volatile can_slot_t *slots, const can_route_t *routes,
                  const FDCAN_RxHeaderTypeDef *hdr, const uint8_t *buf)
{
    for (const can_route_t *r = routes; r->id != CAN_ROUTE_END; r++) {
        if (r->id == hdr->Identifier) {
            volatile can_slot_t *s = &slots[r->slot];
            uint8_t len = classic_len(hdr->DataLength);
            for (uint8_t b = 0; b < 8; b++) {
                s->data[b] = (b < len) ? buf[b] : 0u;   /* zero trailing bytes */
            }
            s->len = len;
            s->last_seen = HAL_GetTick();
            s->seq++;                                    /* freshness marker */
            return;
        }
    }
    /* Unknown ID on this bus: drop. This is the accept-all "default" case. */
}

/* ---- Rx interrupt -------------------------------------------------------- */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    /* FIFO overflowed: at least one frame was lost before we could drain it.
     * Count the event so it can be watched in the debugger. */
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0U) {
        if      (hfdcan->Instance == BUS1_HANDLE.Instance) { g_can_stats.bus1_rx_lost++; }
        else if (hfdcan->Instance == BUS2_HANDLE.Instance) { g_can_stats.bus2_rx_lost++; }
    }

    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U) {
        return;
    }

    FDCAN_RxHeaderTypeDef hdr;
    uint8_t buf[8];

    /* Drain everything queued - the ISR (not the loop) empties the FIFO, so the
     * hardware FIFO only ever has to absorb interrupt latency, not the 100 Hz
     * loop period. */
    while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U) {
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &hdr, buf) != HAL_OK) {
            break;
        }
        if (hfdcan->Instance == BUS1_HANDLE.Instance) {
            store(s_bus1, k_routes_bus1, &hdr, buf);
            CanSniffer_Capture(1u, hdr.Identifier, buf, classic_len(hdr.DataLength));
        } else if (hfdcan->Instance == BUS2_HANDLE.Instance) {
            store(s_bus2, k_routes_bus2, &hdr, buf);
            CanSniffer_Capture(2u, hdr.Identifier, buf, classic_len(hdr.DataLength));
        }
    }
}

/* ---- Tx-complete interrupt ----------------------------------------------- */

/* Fires once the engine has actually put a queued frame on the wire (self-ACK in
 * loopback, real ACK otherwise). This is the ground truth for "did our TX leave
 * the chip": if bus2_tx_done stays 0 while Can_Send keeps returning true, the
 * controller is accepting frames into the FIFO but never transmitting them. */
void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *hfdcan,
                                        uint32_t BufferIndexes)
{
    /* One callback may flag several completed buffers at once - count each. */
    uint32_t n = 0u;
    for (uint32_t m = BufferIndexes; m != 0u; m &= (m - 1u)) { n++; }
    if      (hfdcan->Instance == BUS1_HANDLE.Instance) { g_can_stats.bus1_tx_done += n; }
    else if (hfdcan->Instance == BUS2_HANDLE.Instance) { g_can_stats.bus2_tx_done += n; }
}

/* ---- Public API ---------------------------------------------------------- */

void Can_Init(void)
{
    /* Accept-all: every standard frame -> Rx FIFO0; reject extended (we use
     * 11-bit IDs only) and reject remote frames. This MUST be set explicitly -
     * do not rely on a HAL/reset default for it. */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_ACCEPT_IN_RX_FIFO0,
                                 FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                 FDCAN_REJECT_REMOTE);
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan2, FDCAN_ACCEPT_IN_RX_FIFO0,
                                 FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                 FDCAN_REJECT_REMOTE);

    HAL_FDCAN_Start(&hfdcan1);
    HAL_FDCAN_Start(&hfdcan2);

    /* New-message drives the demux; message-lost feeds the drop counter. */
    HAL_FDCAN_ActivateNotification(&hfdcan1,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST, 0);
    HAL_FDCAN_ActivateNotification(&hfdcan2,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST, 0);

    /* Tx-complete on every Tx FIFO/Queue buffer (0..7): counts frames that the
     * engine actually finished sending, for the `stats` tx line. Diagnostic only. */
    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_TX_COMPLETE,
        FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2 | FDCAN_TX_BUFFER3 |
        FDCAN_TX_BUFFER4 | FDCAN_TX_BUFFER5 | FDCAN_TX_BUFFER6 | FDCAN_TX_BUFFER7);
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_TX_COMPLETE,
        FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2 | FDCAN_TX_BUFFER3 |
        FDCAN_TX_BUFFER4 | FDCAN_TX_BUFFER5 | FDCAN_TX_BUFFER6 | FDCAN_TX_BUFFER7);
}

void Can_Snapshot(can_snapshot_t *out)
{
    uint32_t now = HAL_GetTick();

    /* Mask only the CAN Rx IRQs (not all interrupts via PRIMASK) so the future
     * scheduler timer keeps its timing. Frames arriving during the copy wait
     * safely in the 16-deep hardware FIFO. */
    HAL_NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_DisableIRQ(FDCAN2_IT0_IRQn);

    for (uint32_t i = 0; i < CAN1_MSG_COUNT; i++) {
        volatile can_slot_t *s = &s_bus1[i];
        for (uint8_t b = 0; b < 8; b++) {
            out->bus1[i].data[b] = s->data[b];
        }
        out->bus1[i].len    = s->len;
        out->bus1[i].age_ms = (s->seq == 0U) ? UINT32_MAX : (now - s->last_seen);
    }
    for (uint32_t i = 0; i < CAN2_MSG_COUNT; i++) {
        volatile can_slot_t *s = &s_bus2[i];
        for (uint8_t b = 0; b < 8; b++) {
            out->bus2[i].data[b] = s->data[b];
        }
        out->bus2[i].len    = s->len;
        out->bus2[i].age_ms = (s->seq == 0U) ? UINT32_MAX : (now - s->last_seen);
    }

    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);

    /* Per-bus health (cheap register read; fine outside the critical section).
     * Logical bus -> peripheral via the swap macros so bus1_ok/bus2_ok fed to
     * the model match the same buses everything else routes to. */
    out->bus1_ok = !bus_off(&BUS1_HANDLE);
    out->bus2_ok = !bus_off(&BUS2_HANDLE);
}

/* ---- Bus-off auto-restart ------------------------------------------------ */

typedef struct {
    uint32_t last_attempt_ms;
    uint32_t attempts;      /* consecutive kicks since the bus was last healthy */
    bool     given_up;
} can_recovery_t;

static can_recovery_t s_rec1, s_rec2;

static void service_bus(FDCAN_HandleTypeDef *h, can_recovery_t *rec,
                        uint32_t *recoveries)
{
    if (!bus_off(h)) {
        /* Healthy: re-arm so a future fault gets the full attempt budget. */
        rec->attempts = 0;
        rec->given_up = false;
        return;
    }

    if (rec->given_up) {
        return;             /* hard fault - leave bus down, model fails safe */
    }

    uint32_t now = HAL_GetTick();
    if ((now - rec->last_attempt_ms) < CAN_RECOVERY_INTERVAL_MS) {
        return;             /* rate-limit the kicks */
    }
    rec->last_attempt_ms = now;

    /* Re-join the bus. A bare HAL_FDCAN_Start() fails here: on a hardware
     * bus-off the silicon sets CCCR.INIT but the HAL state stays BUSY, so
     * Start's "must be READY" check rejects it. Stop() (valid while BUSY) drives
     * us back to READY, then Start() clears INIT and we re-join. Config, filters
     * and the message RAM all survive Stop/Start. */
    HAL_FDCAN_Stop(h);
    HAL_FDCAN_Start(h);
    HAL_FDCAN_ActivateNotification(h,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST, 0);

    (*recoveries)++;
    rec->attempts++;
    if (rec->attempts >= CAN_RECOVERY_MAX_ATTEMPTS) {
        rec->given_up = true;
    }
}

void Can_Service(void)
{
    service_bus(&BUS1_HANDLE, &s_rec1, &g_can_stats.bus1_recoveries);
    service_bus(&BUS2_HANDLE, &s_rec2, &g_can_stats.bus2_recoveries);
}

void Can_Health(uint8_t bus, can_health_t *out)
{
    FDCAN_HandleTypeDef *h;
    if      (bus == 1u) { h = &BUS1_HANDLE; out->recovery_gave_up = s_rec1.given_up; }
    else if (bus == 2u) { h = &BUS2_HANDLE; out->recovery_gave_up = s_rec2.given_up; }
    else {
        out->bus_off = true;   /* unknown bus: report not usable */
        out->recovery_gave_up = false;
        out->error_passive = false;
        out->tx_err_cnt = 0u;
        out->last_err_code = 0u;
        out->tx_pending = 0u;
        return;
    }

    /* One coherent read of the protocol status + error counters. Pure register
     * reads, no side effects on the running controller. */
    FDCAN_ProtocolStatusTypeDef ps;
    FDCAN_ErrorCountersTypeDef  ec;
    HAL_FDCAN_GetProtocolStatus(h, &ps);
    HAL_FDCAN_GetErrorCounters(h, &ec);

    out->bus_off       = (ps.BusOff != 0u);
    out->error_passive = (ps.ErrorPassive != 0u);
    out->last_err_code = (uint8_t)ps.LastErrorCode;
    out->tx_err_cnt    = (uint8_t)ec.TxErrorCnt;

    /* Frames currently queued awaiting transmission = popcount(TXBRP). Valid in
     * both FIFO and QUEUE mode, unlike GetTxFifoFreeLevel() which reads 0 in QUEUE
     * mode. 0 = the engine is keeping up; a climbing value = TX backing up. */
    uint32_t brp = h->Instance->TXBRP;
    uint8_t  pending = 0u;
    for (uint32_t m = brp; m != 0u; m &= (m - 1u)) { pending++; }
    out->tx_pending = pending;
}

void Can_ClearStats(void)
{
    g_can_stats.bus1_rx_lost    = 0u;
    g_can_stats.bus2_rx_lost    = 0u;
    g_can_stats.bus1_recoveries = 0u;
    g_can_stats.bus2_recoveries = 0u;
    g_can_stats.bus1_tx_fail    = 0u;
    g_can_stats.bus2_tx_fail    = 0u;
    g_can_stats.bus1_tx_done    = 0u;
    g_can_stats.bus2_tx_done    = 0u;
}

/* ---- Raw FDCAN register dump (`canreg` console command) ------------------
 * Reads the TX-critical M_CAN registers straight off both peripherals so we can
 * SEE why nothing transmits instead of inferring it. Key things to read:
 *   CCCR.INIT=1        -> stuck in init, TX engine off (should be 0)
 *   TXBC.TFQS          -> Tx FIFO/Queue size; 0 = no Tx FIFO allocated
 *   TXBC.TBSA          -> Tx buffer start addr in message RAM (0/garbage = bad layout)
 *   TXFQS.free/putidx  -> is the queue actually being fed/drained
 *   TXBRP              -> Tx requests PENDING; bits stuck set = engine never sends them
 *   PSR.ACT            -> node activity (3 = transmitter); never 3 = never keys TX
 *   TEST/CCCR.MON      -> confirm loopback (MON=1 & TEST.LBCK=1 while looped) */
void Can_DumpTx(void)
{
    FDCAN_GlobalTypeDef *regs[2] = { BUS1_HANDLE.Instance, BUS2_HANDLE.Instance };
    /* static: CDC_Transmit_FS does not copy - the USB endpoint reads this buffer
     * after Console_Out returns, so it must outlive the call (see console.c). */
    static char b[192];
    for (uint8_t i = 0; i < 2u; i++) {
        FDCAN_GlobalTypeDef *R = regs[i];
        uint32_t cccr = R->CCCR, psr = R->PSR, txbc = R->TXBC, txfqs = R->TXFQS;

        snprintf(b, sizeof b,
            "FDCAN%u CCCR %08lX INIT=%lu CCE=%lu MON=%lu DAR=%lu TEST=%lu  TEST %08lX(LBCK=%lu)\r\n",
            (unsigned)(i + 1u), (unsigned long)cccr,
            (unsigned long)(cccr & 1u), (unsigned long)((cccr >> 1) & 1u),
            (unsigned long)((cccr >> 5) & 1u), (unsigned long)((cccr >> 6) & 1u),
            (unsigned long)((cccr >> 7) & 1u),
            (unsigned long)R->TEST, (unsigned long)((R->TEST >> 4) & 1u));
        Console_Out(b);

        snprintf(b, sizeof b,
            "       PSR %08lX LEC=%lu ACT=%lu EP=%lu BO=%lu  TXBC %08lX TBSA=%04lX TFQS=%lu TFQM=%lu\r\n",
            (unsigned long)psr, (unsigned long)(psr & 7u),
            (unsigned long)((psr >> 3) & 3u), (unsigned long)((psr >> 5) & 1u),
            (unsigned long)((psr >> 7) & 1u),
            (unsigned long)txbc, (unsigned long)(txbc & 0xFFFCu),
            (unsigned long)((txbc >> 24) & 0x3Fu), (unsigned long)((txbc >> 30) & 1u));
        Console_Out(b);

        snprintf(b, sizeof b,
            "       TXFQS %08lX free=%lu putidx=%lu full=%lu  TXBRP %08lX TXBTO %08lX  IE %08lX ILE %08lX\r\n",
            (unsigned long)txfqs, (unsigned long)(txfqs & 0x3Fu),
            (unsigned long)((txfqs >> 16) & 0x1Fu), (unsigned long)((txfqs >> 21) & 1u),
            (unsigned long)R->TXBRP, (unsigned long)R->TXBTO,
            (unsigned long)R->IE, (unsigned long)R->ILE);
        Console_Out(b);
    }
}

/* ---- Tx ------------------------------------------------------------------ */

bool Can_Send(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    FDCAN_HandleTypeDef *h;
    if      (bus == 1U) { h = &BUS1_HANDLE; }
    else if (bus == 2U) { h = &BUS2_HANDLE; }
    else                { return false; }

    if (len > 8U) { len = 8U; }

    /* Do NOT gate on HAL_FDCAN_GetTxFifoFreeLevel(): the Tx is configured in QUEUE
     * mode (TXBC.TFQM=1), where TXFQS.TFFL ("free level") ALWAYS reads 0. That guard
     * therefore rejected every frame and nothing was ever queued - the root cause of
     * "the HCU transmits nothing". HAL_FDCAN_AddMessageToTxFifoQ() below checks the
     * queue-FULL flag (TXFQS.TFQF) itself, which IS valid in FIFO and QUEUE mode. */
    FDCAN_TxHeaderTypeDef tx;
    tx.Identifier          = id;
    tx.IdType              = FDCAN_STANDARD_ID;
    tx.TxFrameType         = FDCAN_DATA_FRAME;
    tx.DataLength          = len_to_dlc(len);
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch       = FDCAN_BRS_OFF;        /* classic */
    tx.FDFormat            = FDCAN_CLASSIC_CAN;     /* classic */
    tx.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    tx.MessageMarker       = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(h, &tx, (uint8_t *)data) != HAL_OK) {
        if (bus == 1U) { g_can_stats.bus1_tx_fail++; } else { g_can_stats.bus2_tx_fail++; }
        return false;
    }
    return true;
}
