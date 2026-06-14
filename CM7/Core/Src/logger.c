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

    if ((uint32_t)(head - tail) >= HCU_LOG_RING_RECORDS)
    {
        /* Ring full: CM4 is behind (card stalled, or no card at all). Drop the
         * newest record rather than block - the control loop owns the timing
         * budget. A dropped-record counter could be added here later, for the
         * debugger / a `stat` command only (same policy as g_can_stats). */
        return;
    }

    g_hcu_ipc.ring[head & HCU_LOG_RING_MASK] = *rec;
    __DMB();                              /* record committed before head moves  */
    g_hcu_ipc.head = head + 1u;           /* publish the slot to CM4             */
}
