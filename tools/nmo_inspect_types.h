#ifndef NMO_INSPECT_TYPES_H
#define NMO_INSPECT_TYPES_H

#include "nmo.h"
#include "app/nmo_context.h"
#include "app/nmo_stats.h"
#include "format/nmo_data.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INSPECT_FORMAT_TEXT = 0,
    INSPECT_FORMAT_JSON,
    INSPECT_FORMAT_JSON_PRETTY,
    INSPECT_FORMAT_YAML
} inspect_format_t;

typedef enum {
    COLOR_AUTO = 0,
    COLOR_ALWAYS,
    COLOR_NEVER
} inspect_color_mode_t;

typedef struct {
    bool summary;
    bool header;
    bool stats;
    bool finish_stats;
    bool plugins;
    bool chunks;
    bool chunk_tree;
    bool objects;
    bool hierarchy;
    bool managers;
    bool behaviors;
    bool parameters;
    bool resources;
    bool warnings;
    bool modes_requested;
    bool suppress_summary;
    bool suppress_header;
    bool suppress_warnings;
} inspect_modes_t;

typedef struct {
    nmo_object_id_t *object_ids;
    size_t object_id_count;
    size_t object_id_capacity;

    uint32_t *chunk_ids;
    size_t chunk_id_count;
    size_t chunk_id_capacity;

    size_t *chunk_indexes;
    size_t chunk_index_count;
    size_t chunk_index_capacity;

    nmo_object_id_t *behavior_ids;
    size_t behavior_id_count;
    size_t behavior_id_capacity;

    nmo_class_id_t class_id;
    char *class_name;
    bool has_class_filter;

    char *name_pattern;
    bool include_hidden;
    bool root_specified;
    nmo_object_id_t root_object_id;
    char *scene_name;
    size_t chunk_depth_limit; /* 0 = unlimited */

    nmo_guid_t manager_guid;
    bool has_manager_guid;
} inspect_filters_t;

typedef struct {
    const char *input_path;
    const char *output_path;
    inspect_format_t format;
    inspect_color_mode_t color_mode;
    int verbosity;
    bool fail_on_warning;
    bool strict_mode;
    bool compact_output;
    bool show_offsets;
    bool show_size;
    bool show_guids;
    bool no_pager;
    size_t max_rows;
    size_t truncate_length;
    const char *locale;
    const char *encoding;
    bool show_help;
    bool show_version;
    inspect_modes_t modes;
    inspect_filters_t filters;
} inspect_options_t;

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

typedef struct {
    char code[32];
    char message[256];
    nmo_object_id_t object_id;
} inspect_warning_t;

typedef struct {
    inspect_warning_t *items;
    size_t count;
    size_t capacity;
} warning_list_t;

typedef struct {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_object_t **objects;
    size_t object_count;
    nmo_file_info_t file_info;
    nmo_file_stats_t stats;
    bool has_stats;
    nmo_file_header_t file_header;
    bool has_file_header;
    nmo_finish_loading_stats_t finish_stats;
    bool has_finish_stats;
} inspect_state_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_INSPECT_TYPES_H */
