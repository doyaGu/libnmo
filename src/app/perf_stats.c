/**
 * @file perf_stats.c
 * @brief Phase-level load/save performance statistics helpers
 */

#include "app/nmo_perf_stats.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static const char *const nmo_load_perf_phase_names[NMO_LOAD_PERF_PHASE_COUNT] = {
    "open_detect",
    "header1_read",
    "header1_inflate",
    "header1_parse",
    "data_read",
    "data_inflate",
    "data_parse",
    "object_create",
    "object_deserialize",
    "reference_resolve",
    "behavior_post_load",
    "manager_post_load",
    "index_rebuild"
};

static const char *const nmo_save_perf_phase_names[NMO_SAVE_PERF_PHASE_COUNT] = {
    "pre_hooks",
    "remap_plan",
    "manager_serialize",
    "object_serialize",
    "data_plan",
    "data_write",
    "header1_plan",
    "header1_write",
    "header1_compress",
    "data_compress",
    "crc",
    "txn_write",
    "txn_commit",
    "post_hooks"
};

const char *nmo_load_perf_phase_name(nmo_load_perf_phase_t phase) {
    if (phase < 0 || phase >= NMO_LOAD_PERF_PHASE_COUNT) {
        return "unknown";
    }
    return nmo_load_perf_phase_names[phase];
}

const char *nmo_save_perf_phase_name(nmo_save_perf_phase_t phase) {
    if (phase < 0 || phase >= NMO_SAVE_PERF_PHASE_COUNT) {
        return "unknown";
    }
    return nmo_save_perf_phase_names[phase];
}

void nmo_load_perf_stats_reset(nmo_load_perf_stats_t *stats) {
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

void nmo_save_perf_stats_reset(nmo_save_perf_stats_t *stats) {
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

void nmo_load_perf_stats_record(nmo_load_perf_stats_t *stats,
                                nmo_load_perf_phase_t phase,
                                double milliseconds) {
    if (stats == NULL || phase < 0 || phase >= NMO_LOAD_PERF_PHASE_COUNT) {
        return;
    }
    stats->phases[phase].calls++;
    stats->phases[phase].milliseconds += milliseconds;
}

void nmo_save_perf_stats_record(nmo_save_perf_stats_t *stats,
                                nmo_save_perf_phase_t phase,
                                double milliseconds) {
    if (stats == NULL || phase < 0 || phase >= NMO_SAVE_PERF_PHASE_COUNT) {
        return;
    }
    stats->phases[phase].calls++;
    stats->phases[phase].milliseconds += milliseconds;
}

uint64_t nmo_perf_now_ticks(void) {
#ifdef _WIN32
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart;
#else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
#endif
}

double nmo_perf_elapsed_ms(uint64_t start_ticks, uint64_t end_ticks) {
    if (end_ticks < start_ticks) {
        return 0.0;
    }
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }
    if (frequency.QuadPart <= 0) {
        return 0.0;
    }
    return ((double)(end_ticks - start_ticks) * 1000.0) / (double)frequency.QuadPart;
#else
    return (double)(end_ticks - start_ticks) / 1000000.0;
#endif
}
