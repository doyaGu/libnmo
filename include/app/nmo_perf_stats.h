/**
 * @file nmo_perf_stats.h
 * @brief Phase-level load/save performance statistics
 */

#ifndef NMO_PERF_STATS_H
#define NMO_PERF_STATS_H

#include <stddef.h>
#include <stdint.h>
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_load_perf_phase {
    NMO_LOAD_PERF_OPEN_DETECT = 0,
    NMO_LOAD_PERF_HEADER1_READ,
    NMO_LOAD_PERF_HEADER1_INFLATE,
    NMO_LOAD_PERF_HEADER1_PARSE,
    NMO_LOAD_PERF_DATA_READ,
    NMO_LOAD_PERF_DATA_INFLATE,
    NMO_LOAD_PERF_DATA_PARSE,
    NMO_LOAD_PERF_OBJECT_CREATE,
    NMO_LOAD_PERF_OBJECT_DESERIALIZE,
    NMO_LOAD_PERF_REFERENCE_RESOLVE,
    NMO_LOAD_PERF_BEHAVIOR_POST_LOAD,
    NMO_LOAD_PERF_MANAGER_POST_LOAD,
    NMO_LOAD_PERF_INDEX_REBUILD,
    NMO_LOAD_PERF_PHASE_COUNT
} nmo_load_perf_phase_t;

typedef enum nmo_save_perf_phase {
    NMO_SAVE_PERF_PRE_HOOKS = 0,
    NMO_SAVE_PERF_REMAP_PLAN,
    NMO_SAVE_PERF_MANAGER_SERIALIZE,
    NMO_SAVE_PERF_OBJECT_SERIALIZE,
    NMO_SAVE_PERF_DATA_PLAN,
    NMO_SAVE_PERF_DATA_WRITE,
    NMO_SAVE_PERF_HEADER1_PLAN,
    NMO_SAVE_PERF_HEADER1_WRITE,
    NMO_SAVE_PERF_HEADER1_COMPRESS,
    NMO_SAVE_PERF_DATA_COMPRESS,
    NMO_SAVE_PERF_CRC,
    NMO_SAVE_PERF_TXN_WRITE,
    NMO_SAVE_PERF_TXN_COMMIT,
    NMO_SAVE_PERF_POST_HOOKS,
    NMO_SAVE_PERF_PHASE_COUNT
} nmo_save_perf_phase_t;

typedef struct nmo_phase_time {
    uint64_t calls;
    double milliseconds;
} nmo_phase_time_t;

typedef struct nmo_load_perf_stats {
    nmo_phase_time_t phases[NMO_LOAD_PERF_PHASE_COUNT];
    size_t packed_header1_bytes;
    size_t unpacked_header1_bytes;
    size_t packed_data_bytes;
    size_t unpacked_data_bytes;
} nmo_load_perf_stats_t;

typedef struct nmo_save_perf_stats {
    nmo_phase_time_t phases[NMO_SAVE_PERF_PHASE_COUNT];
    size_t planned_chunk_bytes;
    size_t header1_unpacked_bytes;
    size_t data_unpacked_bytes;
    size_t header1_packed_bytes;
    size_t data_packed_bytes;
} nmo_save_perf_stats_t;

NMO_API const char *nmo_load_perf_phase_name(nmo_load_perf_phase_t phase);
NMO_API const char *nmo_save_perf_phase_name(nmo_save_perf_phase_t phase);
NMO_API void nmo_load_perf_stats_reset(nmo_load_perf_stats_t *stats);
NMO_API void nmo_save_perf_stats_reset(nmo_save_perf_stats_t *stats);
NMO_API void nmo_load_perf_stats_record(nmo_load_perf_stats_t *stats,
                                        nmo_load_perf_phase_t phase,
                                        double milliseconds);
NMO_API void nmo_save_perf_stats_record(nmo_save_perf_stats_t *stats,
                                        nmo_save_perf_phase_t phase,
                                        double milliseconds);
NMO_API uint64_t nmo_perf_now_ticks(void);
NMO_API double nmo_perf_elapsed_ms(uint64_t start_ticks, uint64_t end_ticks);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PERF_STATS_H */
