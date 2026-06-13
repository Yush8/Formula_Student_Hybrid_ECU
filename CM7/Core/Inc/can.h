/*
 * can.h
 *
 *  Created on: 11 Jun 2026
 *      Author: Yusha
 */

/**
 * can.h - dual classic-CAN (FDCAN1/FDCAN2) demux + tx for the HCU bridge.
 *
 * Role in the architecture: this module is the "hardware reality" layer for CAN.
 * The Rx ISR demuxes incoming frames by (bus, ID) into named raw buffers; the
 * bridge calls Can_Snapshot() in its gather phase to get a coherent copy to feed
 * the model, and Can_Send() in its dispatch phase to transmit a packed frame.
 * No decode / scale / logic here - C is the postman, Simulink is the brain.
 *
 * Buses (classic CAN, 8-byte, 500 kbps, FDCAN kernel clock = HSE 25 MHz):
 *   FDCAN1 = dash / ECU bus
 *   FDCAN2 = tractive system / inverters bus
 *
 * =====================================================================
 *  HOW TO ADD A RECEIVED CAN MESSAGE  (you do NOT need to touch this file)
 *  -------------------------------------------------------------------
 *   1. Add one line to can1_messages.def (or can2_messages.def):
 *            CAN_MSG( MY_NAME , 0x123 )
 *   2. Add one line to model_bridge.c (Model_Step gather section):
 *            CAN_FEED( bus1 , MY_NAME , my_port );
 *   3. In Simulink add two inports for that message:
 *            my_port       (uint8, width 8)   <- the 8 data bytes
 *            my_port_age   (uint32)           <- ms since last seen
 *
 *  That is the whole recipe. The enum, routing table and buffers below
 *  are all generated from the .def lists, so they can never drift apart.
 * =====================================================================
 */
#ifndef CAN_H
#define CAN_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Message slots (generated from the .def lists) ----------------------
 * Each CAN_MSG() line in canN_messages.def becomes one enum entry. The trailing
 * CANx_MSG_COUNT is therefore the number of messages on that bus, and doubles as
 * the size of that bus's buffer array. Do not edit by hand - edit the .def file.
 */
typedef enum {
#define CAN_MSG(name, id) name,
#include "can1_messages.def"
#undef CAN_MSG
    CAN1_MSG_COUNT
} can1_slot_t;

typedef enum {
#define CAN_MSG(name, id) name,
#include "can2_messages.def"
#undef CAN_MSG
    CAN2_MSG_COUNT
} can2_slot_t;

/* Array sizes: a bus with zero messages would give a zero-length array (illegal),
 * so clamp the storage size to a minimum of 1. The real counts above still drive
 * every loop, so the dummy slot is never read. */
#define CAN1_SLOTS ((CAN1_MSG_COUNT) > 0 ? (CAN1_MSG_COUNT) : 1)
#define CAN2_SLOTS ((CAN2_MSG_COUNT) > 0 ? (CAN2_MSG_COUNT) : 1)

/* ---- Snapshot view handed to the bridge ---------------------------------
 * Plain C types only (no HAL, no model types) so the bridge stays the only
 * place that knows about both the hardware and the model.
 */
typedef struct {
    uint8_t  data[8];
    uint8_t  len;       /* valid bytes this frame; trailing bytes are zeroed */
    uint32_t age_ms;    /* ms since last rx; UINT32_MAX if never seen */
} can_view_t;

typedef struct {
    can_view_t bus1[CAN1_SLOTS];
    can_view_t bus2[CAN2_SLOTS];
    bool bus1_ok;       /* false when FDCAN1 is bus-off */
    bool bus2_ok;       /* false when FDCAN2 is bus-off */
} can_snapshot_t;

/* ---- Diagnostics counters ------------------------------------------------
 * Exposed as a plain global (g_can_stats) so you can watch it live in the
 * debugger - it is NOT fed into the Simulink model. At this bus load rx_lost
 * should always read 0; it is here purely for a "did I ever drop a frame?"
 * confidence check. recoveries counts bus-off auto-restart kicks.
 */
typedef struct {
    uint32_t bus1_rx_lost;      /* Rx FIFO0 overflow events (>=1 frame lost each) */
    uint32_t bus2_rx_lost;
    uint32_t bus1_recoveries;   /* bus-off recovery kicks issued */
    uint32_t bus2_recoveries;
} can_stats_t;

extern can_stats_t g_can_stats;

/* ---- API ---------------------------------------------------------------- */

/* Configure global filters (accept-all standard into FIFO0), start both
 * controllers, and enable the Rx FIFO0 interrupts. Call once at boot, AFTER the
 * CubeMX MX_FDCAN1_Init() / MX_FDCAN2_Init() functions have run. */
void Can_Init(void);

/* Coherent copy of every slot + per-bus health into *out. Safe to call from
 * the superloop; briefly masks only the two CAN Rx IRQs. */
void Can_Snapshot(can_snapshot_t *out);

/* Bus-off watchdog + rate-limited auto-restart. Call once per superloop pass.
 * Cheap when the bus is healthy. On bus-off it re-joins the bus, then gives up
 * after CAN_RECOVERY_MAX_ATTEMPTS so a hard wiring fault leaves bus_ok=false and
 * the model fails safe instead of thrashing forever. */
void Can_Service(void);

/* Queue a classic frame for transmission. bus = 1 or 2. Returns false if the
 * tx queue is full (caller decides: drop / retry / count) or on error. */
bool Can_Send(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t len);

#endif /* CAN_H */
