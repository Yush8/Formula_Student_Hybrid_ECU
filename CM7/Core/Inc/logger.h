/*
 * logger.h  --  CM7 producer side of the SD-logging IPC ring.
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * The CM7 control loop NEVER touches the SD card: a card stall (tens to ~250 ms
 * on cheap cards) would blow the hard 100 Hz tick budget (see HANDOFF s16.1).
 * Instead the loop drops fixed-size records into a lock-free ring in shared D3
 * SRAM4, and CM4 drains the ring to the card on its own time. This module is
 * the producer half:
 *
 *   Log_Init()      - call once at boot, AFTER Model_Init() and BEFORE the first
 *                     Model_Step (so the ring is set up before any Log_Write).
 *                     Stamps magic/version and clears the indices.
 *   Log_Write(rec)  - ONE line in the bridge dispatch (mirrors the Can_Send
 *                     convention). RAM-only and never blocks: it copies the
 *                     record into the ring and advances head. If CM4 has fallen
 *                     behind and the ring is full, the newest record is dropped
 *                     - the control loop must never wait on the logger.
 *
 * Log_Write is the stable seam: the bridge and the model never know the bytes
 * end up on an SD card. The record layout is the shared contract in
 * Shared/hcu_ipc.h.
 */
#ifndef LOGGER_H
#define LOGGER_H

/* Reach the shared inter-core contract by a SOURCE-RELATIVE path, deliberately.
 * hcu_ipc.h lives in the repo-root Shared/ folder, which is NOT a CubeMX-managed
 * include dir - a hand-added "-I../../Shared" gets silently wiped on every
 * CubeMX regen (it bit us once). A relative include is resolved against THIS
 * file's directory (CM7/Core/Inc) regardless of which .c pulls it in, so it
 * survives any regen with zero project-setting upkeep. Path: Core/Inc -> CM7 ->
 * repo root -> Shared. CM4's consumer header uses the same trick from its tree. */
#include "../../../Shared/hcu_ipc.h"   /* log_record_t, the ring, g_hcu_ipc */

void Log_Init(void);
void Log_Write(const log_record_t *rec);

/* ---- Producer-side diagnostics (CM7 view; for the `stats` console command) --
 * All CM7-local: reading them never touches the card or CM4. The one trackside
 * question is "is the ring backing up?" - the card is the only slow link, so if
 * CM4 can't keep up (card stalled / full / absent, or CM4 down) the ring fills
 * and Log_Write starts dropping the newest records. Same read-only, NOT-fed-to-
 * the-model policy as g_can_stats / g_sched_overruns.
 *   drops == 0  => every record offered since boot made it into the ring.
 *   occ_max     => worst ring backlog seen (HCU_LOG_RING_RECORDS = completely
 *                  full at least once, i.e. right at the edge of dropping). */
typedef struct {
    uint32_t writes;   /* records offered to the ring (Log_Write calls)         */
    uint32_t drops;    /* records dropped because the ring was full             */
    uint16_t occ_max;  /* high-water occupancy seen (0..HCU_LOG_RING_RECORDS)   */
} log_stats_t;
extern log_stats_t g_log_stats;

/* Current ring occupancy = records waiting for CM4 (0..HCU_LOG_RING_RECORDS).
 * head/tail are each single-writer 32-bit, so this read is atomic; a value one
 * tick stale is fine for a status readout. */
uint32_t Log_Occupancy(void);

/* Zero the producer-side diagnostic counters in g_log_stats (for `stats clear`).
 * Touches ONLY the counters - never head/tail/the ring, which are the live
 * inter-core state. */
void Log_ClearStats(void);

#endif /* LOGGER_H */
