/*
 * hcu_ipc.h  --  CM7 <-> CM4 inter-core contract (SHARED: included by BOTH cores)
 *
 *  Created on: 13 Jun 2026
 *      Author: Yusha
 *
 * SINGLE SOURCE OF TRUTH for the dual-core link. CM7 (the control loop) is the
 * sole producer of log records; CM4 (the SD logger) is the sole consumer. They
 * talk through one struct, g_hcu_ipc, placed at a FIXED physical address in D3
 * SRAM4 which BOTH cores see at the same address 0x38000000 (unlike D2 SRAM,
 * which is aliased to a different address on each core - a trap for shared
 * memory). Same anti-desync idea as the CAN .def X-macros (see HANDOFF s13):
 * one definition, compiled identically into both binaries, so the two cores can
 * never disagree about the layout. See HANDOFF.md section 16.
 *
 *  Memory model - lock-free single-producer/single-consumer (SPSC) ring, with
 *  NO per-record hardware semaphore:
 *    - CM7 is the ONLY writer of `head`; CM4 is the ONLY writer of `tail`.
 *    - Each index is 32-bit aligned with a single writer, so reads/writes are
 *      atomic on both cores. The only ordering needed is a __DMB() between a
 *      record's bytes and the index that publishes it (see logger.c).
 *    - head/tail are FREE-RUNNING counters (they wrap at 2^32); the ring slot is
 *      `index & HCU_LOG_RING_MASK`. Occupancy = (uint32_t)(head - tail): empty
 *      when head == tail, full when occupancy == HCU_LOG_RING_RECORDS.
 *
 *  Cache: D-cache is OFF on CM7 today and the Cortex-M4 has none, so there is no
 *  coherency dance. If D-cache is ever enabled on CM7, mark this region
 *  non-cacheable via the MPU (preferably in CubeMX, so it survives regen).
 */
#ifndef HCU_IPC_H
#define HCU_IPC_H

#include <stdint.h>

/* Physical base of the shared struct in D3 SRAM4. The .shared_ram NOLOAD
 * section in BOTH linker scripts places g_hcu_ipc here. */
#define HCU_IPC_BASE          0x38000000UL

/* "HCU1" - tells a set-up region from blank/garbage RAM. The consumer checks
 * this before trusting the ring (same idea as the config-blob magic, s9). */
#define HCU_IPC_MAGIC         0x48435531UL

/* Bump whenever the record/struct layout changes, so an out-of-date consumer or
 * off-target decoder can spot a mismatched layout (the log-file header also
 * carries record_size as a second cross-check). */
#define HCU_IPC_VERSION       2u

/* Ring capacity in records. MUST be a power of two (single-AND indexing).
 * Stall headroom = HCU_LOG_RING_RECORDS / SCHED_RATE_HZ seconds: at 100 Hz, 256
 * records ~= 2.5 s, comfortably over the ~250 ms worst-case card stall. Cheap in
 * D3 SRAM4. Tune here if the record grows large. */
#define HCU_LOG_RING_RECORDS  256u
#define HCU_LOG_RING_MASK     (HCU_LOG_RING_RECORDS - 1u)

/* ---- One log record (GENERATED from Shared/log_signals.def) ---------------
 * DO NOT add fields here. To change what is logged, edit Shared/log_signals.def
 * (one line per signal) - both this layout and the firmware that fills it are
 * generated from that one list, so they cannot drift apart (same X-macro trick
 * as the CAN .def lists, s13).
 *
 * `tick` is always first (the 100 Hz model-step counter = the time axis); the
 * signals from log_signals.def follow. Packed, so the on-disk layout is exactly
 * field-after-field with no padding - which keeps the off-target Python decoder
 * trivial and endian-clean. */
typedef struct __attribute__((packed)) {
    uint32_t tick;                       /* model tick at capture (10 ms units) */
#define LOG_Y(ctype, port)  ctype port;
#define LOG_U(ctype, port)  ctype port;
#include "log_signals.def"               /* resolves next to this header (Shared/) */
#undef LOG_Y
#undef LOG_U
} log_record_t;

/* ---- Wall-clock published by CM7 (RTC owner) for CM4 ---------------------
 * CM7 owns the RTC and republishes the datetime ~1 Hz; CM4 reads it for
 * get_fattime() and the log-file header (CM4 never touches the RTC peripheral).
 * `seq` is a seqlock: CM7 makes it odd before writing the fields and even after,
 * so CM4 can retry until it reads a stable, matching pair. Declared now so the
 * contract is complete; wired in Phase 3. */
typedef struct {
    volatile uint32_t seq;   /* odd = update in progress; even & matching = stable */
    uint16_t year;           /* full year, e.g. 2026 */
    uint8_t  month;          /* 1..12  */
    uint8_t  day;            /* 1..31  */
    uint8_t  hour;           /* 0..23  */
    uint8_t  minute;         /* 0..59  */
    uint8_t  second;         /* 0..59  */
    uint8_t  valid;          /* 0 until CM7 has published at least once */
} hcu_datetime_t;

/* ---- The whole inter-core contract -------------------------------------- */
typedef struct {
    uint32_t magic;            /* HCU_IPC_MAGIC once the producer has set up   */
    uint32_t version;          /* HCU_IPC_VERSION                              */

    volatile uint32_t head;    /* producer (CM7) writes; free-running counter  */
    volatile uint32_t tail;    /* consumer (CM4) writes; free-running counter  */

    hcu_datetime_t    now;     /* CM7 -> CM4 wall-clock                        */

    log_record_t      ring[HCU_LOG_RING_RECORDS];
} hcu_ipc_t;

/* Defined exactly once per core, in the .shared_ram section (see logger.c on
 * CM7 and the future sd_logger.c on CM4). Both definitions land at the same
 * fixed address, so the two binaries share one physical struct. */
extern volatile hcu_ipc_t g_hcu_ipc;

/* Compile-time guards - catch a bad edit when the record/ring is extended. */
_Static_assert((HCU_LOG_RING_RECORDS & HCU_LOG_RING_MASK) == 0u,
               "HCU_LOG_RING_RECORDS must be a power of two");
_Static_assert(sizeof(log_record_t) <= 512u,
               "log_record_t must fit in one 512-byte SD sector buffer "
               "(trim Shared/log_signals.def)");

#endif /* HCU_IPC_H */
