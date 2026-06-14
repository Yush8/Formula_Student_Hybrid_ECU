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
#include "main.h"          /* HAL + FDCAN handle types */

/* CubeMX defines these at file scope in main.c. */
extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;

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
        if      (hfdcan->Instance == FDCAN1) { g_can_stats.bus1_rx_lost++; }
        else if (hfdcan->Instance == FDCAN2) { g_can_stats.bus2_rx_lost++; }
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
        if (hfdcan->Instance == FDCAN1) {
            store(s_bus1, k_routes_bus1, &hdr, buf);
        } else if (hfdcan->Instance == FDCAN2) {
            store(s_bus2, k_routes_bus2, &hdr, buf);
        }
    }
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

    /* Per-bus health (cheap register read; fine outside the critical section). */
    out->bus1_ok = !bus_off(&hfdcan1);
    out->bus2_ok = !bus_off(&hfdcan2);
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
    service_bus(&hfdcan1, &s_rec1, &g_can_stats.bus1_recoveries);
    service_bus(&hfdcan2, &s_rec2, &g_can_stats.bus2_recoveries);
}

void Can_Health(uint8_t bus, can_health_t *out)
{
    if (bus == 1u) {
        out->bus_off          = bus_off(&hfdcan1);
        out->recovery_gave_up = s_rec1.given_up;
    } else if (bus == 2u) {
        out->bus_off          = bus_off(&hfdcan2);
        out->recovery_gave_up = s_rec2.given_up;
    } else {
        out->bus_off          = true;   /* unknown bus: report not usable */
        out->recovery_gave_up = false;
    }
}

void Can_ClearStats(void)
{
    g_can_stats.bus1_rx_lost    = 0u;
    g_can_stats.bus2_rx_lost    = 0u;
    g_can_stats.bus1_recoveries = 0u;
    g_can_stats.bus2_recoveries = 0u;
}

/* ---- Tx ------------------------------------------------------------------ */

bool Can_Send(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    FDCAN_HandleTypeDef *h;
    if      (bus == 1U) { h = &hfdcan1; }
    else if (bus == 2U) { h = &hfdcan2; }
    else                { return false; }

    if (len > 8U) { len = 8U; }

    if (HAL_FDCAN_GetTxFifoFreeLevel(h) == 0U) {
        return false;   /* queue full - backpressure; caller decides policy */
    }

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

    return HAL_FDCAN_AddMessageToTxFifoQ(h, &tx, (uint8_t *)data) == HAL_OK;
}
