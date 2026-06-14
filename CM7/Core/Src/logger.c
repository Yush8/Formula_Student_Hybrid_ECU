/*
 * logger.c
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * CM7 producer for the dual-core SD log. See logger.h for the contract and
 * Shared/hcu_ipc.h for the shared ring layout. Deterministic and RAM-only: no
 * HAL calls, no SD access, no blocking - safe to call on every control tick.
 */

#include "logger.h"
#include "main.h"     /* CMSIS __DMB() (via the device/HAL headers) */

/* The shared inter-core struct. The .shared_ram NOLOAD section in the linker
 * script places it at HCU_IPC_BASE = 0x38000000 in D3 SRAM4 - the same address
 * CM4 sees. NOLOAD means C startup neither loads nor zeroes it, so Log_Init owns
 * its setup and a later CM4 reset cannot wipe what CM7 put here. */
volatile hcu_ipc_t g_hcu_ipc __attribute__((section(".shared_ram")));

/* Producer-side diagnostics (CM7 .bss, zeroed at boot). See logger.h. Read-only
 * from the `stats` console command; never fed into the model. */
log_stats_t g_log_stats = {0};

void Log_Init(void)
{
    /* Set up the ring once. Order matters: indices and version first, a barrier,
     * then magic LAST - so a consumer that sees the magic is guaranteed to also
     * see a fully initialised struct. */
    g_hcu_ipc.head      = 0u;
    g_hcu_ipc.tail      = 0u;   /* no consumer yet in Phase 1; harmless to seed */
    g_hcu_ipc.now.seq   = 0u;
    g_hcu_ipc.now.valid = 0u;
    g_hcu_ipc.version   = HCU_IPC_VERSION;
    __DMB();
    g_hcu_ipc.magic     = HCU_IPC_MAGIC;
}

void Log_Write(const log_record_t *rec)
{
    uint32_t head = g_hcu_ipc.head;       /* we are the sole writer of head     */
    uint32_t tail = g_hcu_ipc.tail;       /* CM4 writes this; read it once       */
    uint32_t occ  = (uint32_t)(head - tail);

    g_log_stats.writes++;

    if (occ >= HCU_LOG_RING_RECORDS)
    {
        /* Ring full: CM4 is behind (card stalled, full, or no card at all). Drop
         * the newest record rather than block - the control loop owns the timing
         * budget. `drops` is the trackside "logging is failing" signal, surfaced
         * by the `stats` console command (read-only; same policy as g_can_stats). */
        g_log_stats.drops++;
        return;
    }

    g_hcu_ipc.ring[head & HCU_LOG_RING_MASK] = *rec;
    __DMB();                              /* record committed before head moves  */
    g_hcu_ipc.head = head + 1u;           /* publish the slot to CM4             */

    /* High-water mark of the backlog (occupancy AFTER this record was added). */
    uint32_t occ_now = occ + 1u;
    if (occ_now > g_log_stats.occ_max) {
        g_log_stats.occ_max = (uint16_t)occ_now;
    }
}

uint32_t Log_Occupancy(void)
{
    return (uint32_t)(g_hcu_ipc.head - g_hcu_ipc.tail);
}

void Log_ClearStats(void)
{
    g_log_stats.writes  = 0u;
    g_log_stats.drops   = 0u;
    g_log_stats.occ_max = 0u;
}
