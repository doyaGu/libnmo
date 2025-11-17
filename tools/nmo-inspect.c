/**
 * nmo-inspect - Structured inspector for Virtools NMO/CMO/VMO files
 *
 * Implements the documented CLI in nmo-inspect.md with flexible modes,
 * filtering, and machine readable output.
 */

#include "nmo.h"
#include "app/nmo_stats.h"
#include "app/nmo_inspector.h"
#include "schema/nmo_ckobject_hierarchy.h"
#include "schema/nmo_class_hierarchy.h"
#include "format/nmo_data.h"
#include "core/nmo_guid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
extern int fileno(FILE *);
#endif

#include "yyjson.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* ------------------------------------------------------------------------- */
/* CLI option and state definitions                                          */
/* ------------------------------------------------------------------------- */

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
    nmo_schema_registry_t *registry;
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

/* ------------------------------------------------------------------------- */
/* Utility helpers                                                           */
/* ------------------------------------------------------------------------- */

static void log_message(const inspect_options_t *opts, log_level_t level, const char *fmt, ...) {
    log_level_t max_level = LOG_ERROR;
    if (opts->verbosity >= 1) {
        max_level = LOG_INFO; /* allows WARN and INFO */
    }
    if (opts->verbosity >= 2) {
        max_level = LOG_DEBUG;
    }
    if (level > max_level) {
        return;
    }

    const char *prefix = NULL;
    switch (level) {
        case LOG_ERROR: prefix = "error"; break;
        case LOG_WARN: prefix = "warn"; break;
        case LOG_INFO: prefix = "info"; break;
        case LOG_DEBUG: prefix = "debug"; break;
        default: prefix = "log"; break;
    }

    fprintf(stderr, "[%s] ", prefix);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static void warning_list_init(warning_list_t *warnings) {
    warnings->items = NULL;
    warnings->count = 0;
    warnings->capacity = 0;
}

static void warning_list_free(warning_list_t *warnings) {
    free(warnings->items);
    warnings->items = NULL;
    warnings->count = 0;
    warnings->capacity = 0;
}

static bool warning_list_add(warning_list_t *warnings, const char *code, const char *message, nmo_object_id_t object_id) {
    if (warnings->count == warnings->capacity) {
        size_t new_capacity = warnings->capacity == 0 ? 8 : warnings->capacity * 2;
        inspect_warning_t *new_items = (inspect_warning_t *)realloc(warnings->items, new_capacity * sizeof(*new_items));
        if (!new_items) {
            return false;
        }
        warnings->items = new_items;
        warnings->capacity = new_capacity;
    }
    inspect_warning_t *entry = &warnings->items[warnings->count++];
    strncpy(entry->code, code ? code : "Unknown", sizeof(entry->code) - 1);
    entry->code[sizeof(entry->code) - 1] = '\0';
    strncpy(entry->message, message ? message : "", sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    entry->object_id = object_id;
    return true;
}

static void filters_init(inspect_filters_t *filters) {
    memset(filters, 0, sizeof(*filters));
}

static void filters_free(inspect_filters_t *filters) {
    free(filters->object_ids);
    free(filters->chunk_ids);
    free(filters->chunk_indexes);
    free(filters->behavior_ids);
    free(filters->class_name);
    free(filters->name_pattern);
    free(filters->scene_name);
    filters_init(filters);
}

static void options_init(inspect_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->format = INSPECT_FORMAT_TEXT;
    opts->color_mode = COLOR_AUTO;
    opts->truncate_length = 80;
    opts->max_rows = 0; /* unlimited */
    filters_init(&opts->filters);
}

static void options_free(inspect_options_t *opts) {
    filters_free(&opts->filters);
}

static char *duplicate_string(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

static const char *trim_start(const char *str) {
    while (*str && isspace((unsigned char)*str)) {
        ++str;
    }
    return str;
}

static size_t trim_end(const char *start, const char *end) {
    size_t len = (size_t)(end - start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        --len;
    }
    return len;
}

static int parse_u32_token(const char *text, uint32_t *out_value) {
    if (!text || !*text) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long value = strtoul(text, &endptr, 0);
    if (errno != 0 || endptr == text || *endptr != '\0' || value > UINT32_MAX) {
        return -1;
    }
    *out_value = (uint32_t)value;
    return 0;
}

static int parse_size_token(const char *text, size_t *out_value) {
    if (!text || !*text) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long long value = strtoull(text, &endptr, 0);
    if (errno != 0 || endptr == text || *endptr != '\0') {
        return -1;
    }
    *out_value = (size_t)value;
    return 0;
}

static bool append_u32(uint32_t **list, size_t *count, size_t *capacity, uint32_t value) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        uint32_t *new_list = (uint32_t *)realloc(*list, new_capacity * sizeof(uint32_t));
        if (!new_list) {
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }
    (*list)[(*count)++] = value;
    return true;
}

static bool append_size(size_t **list, size_t *count, size_t *capacity, size_t value) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        size_t *new_list = (size_t *)realloc(*list, new_capacity * sizeof(size_t));
        if (!new_list) {
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }
    (*list)[(*count)++] = value;
    return true;
}

static bool append_object_id(nmo_object_id_t **list, size_t *count, size_t *capacity, nmo_object_id_t value) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        nmo_object_id_t *new_list = (nmo_object_id_t *)realloc(*list, new_capacity * sizeof(nmo_object_id_t));
        if (!new_list) {
            return false;
        }
        *list = new_list;
        *capacity = new_capacity;
    }
    (*list)[(*count)++] = value;
    return true;
}

static int parse_object_id_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            uint32_t parsed = 0;
            if (parse_u32_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_object_id(&filters->object_ids, &filters->object_id_count, &filters->object_id_capacity, (nmo_object_id_t)parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static int parse_behavior_id_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            uint32_t parsed = 0;
            if (parse_u32_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_object_id(&filters->behavior_ids, &filters->behavior_id_count, &filters->behavior_id_capacity, (nmo_object_id_t)parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static int parse_chunk_id_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            uint32_t parsed = 0;
            if (parse_u32_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_u32(&filters->chunk_ids, &filters->chunk_id_count, &filters->chunk_id_capacity, parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static int parse_chunk_index_list(inspect_filters_t *filters, const char *value) {
    const char *ptr = value;
    while (ptr && *ptr) {
        ptr = trim_start(ptr);
        if (*ptr == '\0') {
            break;
        }
        const char *end = strchr(ptr, ',');
        if (!end) {
            end = ptr + strlen(ptr);
        }
        size_t len = trim_end(ptr, end);
        if (len > 0) {
            char buffer[64];
            if (len >= sizeof(buffer)) {
                len = sizeof(buffer) - 1;
            }
            memcpy(buffer, ptr, len);
            buffer[len] = '\0';
            size_t parsed = 0;
            if (parse_size_token(buffer, &parsed) != 0) {
                return -1;
            }
            if (!append_size(&filters->chunk_indexes, &filters->chunk_index_count, &filters->chunk_index_capacity, parsed)) {
                return -2;
            }
        }
        ptr = (*end == ',') ? end + 1 : end;
    }
    return 0;
}

static bool contains_object_id(const nmo_object_id_t *values, size_t count, nmo_object_id_t value) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) {
            return true;
        }
    }
    return false;
}

static bool contains_u32(const uint32_t *values, size_t count, uint32_t value) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) {
            return true;
        }
    }
    return false;
}

static bool contains_size(const size_t *values, size_t count, size_t value) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) {
            return true;
        }
    }
    return false;
}

static const char *safe_object_name(const nmo_object_t *object) {
    const char *name = nmo_object_get_name(object);
    return name ? name : "(unnamed)";
}

static const char *class_name_from_id(nmo_class_id_t class_id) {
    const char *name = nmo_ckclass_get_name_by_id(class_id);
    static char buffer[32];
    if (name) {
        return name;
    }
    snprintf(buffer, sizeof(buffer), "Class#%u", class_id);
    return buffer;
}

static int str_casecmp_local(const char *a, const char *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && *b) {
        int da = tolower((unsigned char)*a);
        int db = tolower((unsigned char)*b);
        if (da != db) {
            return da - db;
        }
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static const char *detect_container(const char *path) {
    if (!path) {
        return "unknown";
    }
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return "unknown";
    }
    ++ext;
    if (str_casecmp_local(ext, "nmo") == 0) {
        return "NMO";
    }
    if (str_casecmp_local(ext, "cmo") == 0) {
        return "CMO";
    }
    if (str_casecmp_local(ext, "vmo") == 0) {
        return "VMO";
    }
    return ext;
}

static bool strings_equal_ci(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool match_pattern_ci(const char *pattern, const char *value) {
    if (!pattern || !*pattern) {
        return true;
    }
    if (!value) {
        value = "";
    }
    char pc = *pattern;
    if (pc == '*') {
        pattern++;
        if (!*pattern) {
            return true;
        }
        while (*value) {
            if (match_pattern_ci(pattern, value)) {
                return true;
            }
            value++;
        }
        return match_pattern_ci(pattern, value);
    }
    if (pc == '?') {
        if (!*value) {
            return false;
        }
        return match_pattern_ci(pattern + 1, value + 1);
    }
    if (tolower((unsigned char)pc) != tolower((unsigned char)*value)) {
        return false;
    }
    return match_pattern_ci(pattern + 1, value + 1);
}

static bool match_truncate(const inspect_options_t *opts, const char *value, char *buffer, size_t buffer_size) {
    if (!value) {
        value = "";
    }
    size_t limit = opts->truncate_length;
    if (limit == 0 || strlen(value) <= limit) {
        strncpy(buffer, value, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return false;
    }
    if (buffer_size < 4) {
        return false;
    }
    size_t copy_len = limit > 3 ? limit - 3 : limit;
    if (copy_len > buffer_size - 4) {
        copy_len = buffer_size - 4;
    }
    memcpy(buffer, value, copy_len);
    buffer[copy_len] = '\0';
    strcat(buffer, "...");
    return true;
}

static bool should_use_color(const inspect_options_t *opts, FILE *stream) {
    if (opts->color_mode == COLOR_ALWAYS) {
        return true;
    }
    if (opts->color_mode == COLOR_NEVER) {
        return false;
    }
    if (!stream) {
        return false;
    }
    return isatty(fileno(stream)) != 0;
}

static void print_heading(FILE *out, const inspect_options_t *opts, const char *title, bool colorize) {
    if (opts->compact_output) {
        fprintf(out, "[%s] ", title);
        return;
    }
    if (colorize) {
        fprintf(out, "\033[1m== %s ==\033[0m\n", title);
    } else {
        fprintf(out, "== %s ==\n", title);
    }
}

static bool object_is_under_root(const nmo_object_t *object, nmo_object_id_t root_id) {
    if (!object) {
        return false;
    }
    nmo_object_id_t id = nmo_object_get_id(object);
    if (id == root_id) {
        return true;
    }
    const nmo_object_t *parent = object;
    while (parent) {
        if (nmo_object_get_id(parent) == root_id) {
            return true;
        }
        parent = parent->parent;
    }
    return false;
}

static bool object_matches_filters(const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object) {
    const inspect_filters_t *filters = &opts->filters;
    if (filters->object_id_count > 0) {
        if (!contains_object_id(filters->object_ids, filters->object_id_count, nmo_object_get_id(object))) {
            return false;
        }
    }
    if (filters->has_class_filter) {
        if (!state->registry) {
            return false;
        }
        if (!nmo_class_is_derived_from(state->registry, nmo_object_get_class_id(object), filters->class_id)) {
            return false;
        }
    }
    if (filters->name_pattern) {
        if (!match_pattern_ci(filters->name_pattern, safe_object_name(object))) {
            return false;
        }
    }
    if (filters->root_specified) {
        if (!object_is_under_root(object, filters->root_object_id)) {
            return false;
        }
    }
    return true;
}

static bool chunk_matches_filters(const inspect_filters_t *filters, uint32_t chunk_class_id, size_t chunk_index) {
    if (filters->chunk_id_count > 0 && !contains_u32(filters->chunk_ids, filters->chunk_id_count, chunk_class_id)) {
        return false;
    }
    if (filters->chunk_index_count > 0 && !contains_size(filters->chunk_indexes, filters->chunk_index_count, chunk_index)) {
        return false;
    }
    return true;
}

static void print_usage(void) {
    printf("Usage: nmo-inspect [options] <file>\n\n");
    printf("General options:\n");
    printf("  -h, --help                 Show this help\n");
    printf("  -V, --version              Show version information\n");
    printf("  -v, --verbose              Increase logging (repeatable)\n");
    printf("                             (default shows errors only; -v adds warnings/info, -vv adds debug)\n");
    printf("      --color[=mode]         Color output auto|always|never\n");
    printf("  -o, --output <path>        Write report to file\n");
    printf("  -F, --format <fmt>         text|json|json-pretty|yaml\n");
    printf("      --fail-on-warning      Exit with error if warnings exist\n");
    printf("      --strict               Treat validation issues as fatal\n");
    printf("      --no-pager             Disable pager even when interactive\n");
    printf("      --locale <name>        Override locale for formatting\n");
    printf("      --encoding <name>      Override text encoding (default UTF-8)\n");
    printf("\nModes (select what to display):\n");
    printf("      --summary              Show summary (default)\n");
    printf("      --header               Show file header information\n");
    printf("      --chunks               Show chunk table\n");
    printf("      --chunk-tree           Show chunk hierarchy tree\n");
    printf("      --objects              Show object list (default)\n");
    printf("      --hierarchy            Show object hierarchy\n");
    printf("      --managers             Show manager chunk summary\n");
    printf("      --behaviors            Show behavior/script objects\n");
    printf("      --parameters           Show parameter objects\n");
    printf("      --resources            Show included resources\n");
    printf("      --warnings             Show collected warnings (default)\n");
    printf("      --all                  summary+header+objects+hierarchy+warnings\n");
    printf("      --no-summary           Disable summary section\n");
    printf("      --no-header            Disable header section\n");
    printf("      --no-warnings          Disable warnings section\n");
    printf("\nFiltering:\n");
    printf("      --object-id <list>     Only include specified object IDs\n");
    printf("      --class <name>         Only include class and descendants\n");
    printf("      --name <pattern>       Filter objects by name (supports * ?)\n");
    printf("      --manager <guid>       Filter objects by manager GUID\n");
    printf("      --root <object-id>     Start hierarchy at object ID\n");
    printf("      --scene <name>         Restrict to CKScene/CKLevel by name\n");
    printf("      --object-behaviors     Alias for --behaviors\n");
    printf("      --behavior-id <list>   Filter behaviors by object ID\n");
    printf("      --chunk-id <list>      Filter chunks by chunk class ID\n");
    printf("      --chunk-index <list>   Filter chunks by index\n");
    printf("      --chunk-depth <n>      Limit chunk tree depth\n");
    printf("      --include-hidden       Include hidden/editor-only objects\n");
    printf("\nFormatting controls:\n");
    printf("      --compact              Compact single-line text output\n");
    printf("      --max-rows <n>         Limit rows per section (0=all)\n");
    printf("      --truncate <n>         Truncate long strings (default 80)\n");
    printf("      --show-offsets         Attempt to show chunk offsets\n");
    printf("      --show-size            Include size columns\n");
    printf("      --show-guids           Show GUIDs in listings\n");
    printf("\nExamples:\n");
    printf("  nmo-inspect scene.cmo\n");
    printf("  nmo-inspect --chunks --chunk-tree --show-size scene.cmo\n");
    printf("  nmo-inspect --objects --class CK3dEntity scene.cmo\n");
    printf("  nmo-inspect --all --format json scene.cmo > report.json\n");
}

/* ------------------------------------------------------------------------- */
/* CLI parsing                                                               */
/* ------------------------------------------------------------------------- */

static int parse_long_option(int argc, char **argv, int *index, inspect_options_t *opts, const char *option_text) {
    const char *value = NULL;
    char name_buffer[128];
    const char *name = option_text;
    const char *eq = strchr(option_text, '=');
    if (eq) {
        size_t len = (size_t)(eq - option_text);
        if (len >= sizeof(name_buffer)) {
            log_message(opts, LOG_ERROR, "Option name too long: --%s", option_text);
            return -1;
        }
        memcpy(name_buffer, option_text, len);
        name_buffer[len] = '\0';
        name = name_buffer;
        if (*(eq + 1) != '\0') {
            value = eq + 1;
        }
    }

    #define REQUIRE_VALUE() \
        do { \
            if (!value || !*value) { \
                if (*index + 1 >= argc) { \
                    log_message(opts, LOG_ERROR, "Option --%s requires a value", name); \
                    return -1; \
                } \
                value = argv[++(*index)]; \
            } \
        } while (0)

    if (strcmp(name, "help") == 0) {
        opts->show_help = true;
    } else if (strcmp(name, "version") == 0) {
        opts->show_version = true;
    } else if (strcmp(name, "verbose") == 0) {
        opts->verbosity++;
    } else if (strncmp(name, "color", 5) == 0) {
        if (!value) {
            value = "always";
        }
        if (strcmp(value, "auto") == 0) {
            opts->color_mode = COLOR_AUTO;
        } else if (strcmp(value, "always") == 0) {
            opts->color_mode = COLOR_ALWAYS;
        } else if (strcmp(value, "never") == 0) {
            opts->color_mode = COLOR_NEVER;
        } else {
            log_message(opts, LOG_ERROR, "Invalid color mode: %s", value);
            return -1;
        }
    } else if (strcmp(name, "output") == 0) {
        REQUIRE_VALUE();
        opts->output_path = value;
    } else if (strcmp(name, "format") == 0) {
        REQUIRE_VALUE();
        if (strcmp(value, "text") == 0) {
            opts->format = INSPECT_FORMAT_TEXT;
        } else if (strcmp(value, "json") == 0) {
            opts->format = INSPECT_FORMAT_JSON;
        } else if (strcmp(value, "json-pretty") == 0) {
            opts->format = INSPECT_FORMAT_JSON_PRETTY;
        } else if (strcmp(value, "yaml") == 0) {
            opts->format = INSPECT_FORMAT_YAML;
        } else {
            log_message(opts, LOG_ERROR, "Unknown format: %s", value);
            return -1;
        }
    } else if (strcmp(name, "fail-on-warning") == 0) {
        opts->fail_on_warning = true;
    } else if (strcmp(name, "strict") == 0) {
        opts->strict_mode = true;
    } else if (strcmp(name, "no-pager") == 0) {
        opts->no_pager = true;
    } else if (strcmp(name, "locale") == 0) {
        REQUIRE_VALUE();
        opts->locale = value;
    } else if (strcmp(name, "encoding") == 0) {
        REQUIRE_VALUE();
        opts->encoding = value;
    } else if (strcmp(name, "summary") == 0) {
        opts->modes.summary = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "header") == 0) {
        opts->modes.header = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "chunks") == 0) {
        opts->modes.chunks = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "chunk-tree") == 0) {
        opts->modes.chunk_tree = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "objects") == 0) {
        opts->modes.objects = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "hierarchy") == 0) {
        opts->modes.hierarchy = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "managers") == 0) {
        opts->modes.managers = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "behaviors") == 0 || strcmp(name, "object-behaviors") == 0) {
        opts->modes.behaviors = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "parameters") == 0) {
        opts->modes.parameters = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "resources") == 0) {
        opts->modes.resources = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "warnings") == 0) {
        opts->modes.warnings = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "all") == 0) {
        opts->modes.summary = true;
        opts->modes.header = true;
        opts->modes.objects = true;
        opts->modes.hierarchy = true;
        opts->modes.warnings = true;
        opts->modes.modes_requested = true;
    } else if (strcmp(name, "no-summary") == 0) {
        opts->modes.suppress_summary = true;
    } else if (strcmp(name, "no-header") == 0) {
        opts->modes.suppress_header = true;
    } else if (strcmp(name, "no-warnings") == 0) {
        opts->modes.suppress_warnings = true;
    } else if (strcmp(name, "object-id") == 0) {
        REQUIRE_VALUE();
        if (parse_object_id_list(&opts->filters, value) != 0) {
            log_message(opts, LOG_ERROR, "Invalid object ID list");
            return -1;
        }
    } else if (strcmp(name, "behavior-id") == 0) {
        REQUIRE_VALUE();
        if (parse_behavior_id_list(&opts->filters, value) != 0) {
            log_message(opts, LOG_ERROR, "Invalid behavior ID list");
            return -1;
        }
    } else if (strcmp(name, "chunk-id") == 0) {
        REQUIRE_VALUE();
        if (parse_chunk_id_list(&opts->filters, value) != 0) {
            log_message(opts, LOG_ERROR, "Invalid chunk ID list");
            return -1;
        }
    } else if (strcmp(name, "chunk-index") == 0) {
        REQUIRE_VALUE();
        if (parse_chunk_index_list(&opts->filters, value) != 0) {
            log_message(opts, LOG_ERROR, "Invalid chunk index list");
            return -1;
        }
    } else if (strcmp(name, "chunk-depth") == 0) {
        REQUIRE_VALUE();
        size_t depth = 0;
        if (parse_size_token(value, &depth) != 0) {
            log_message(opts, LOG_ERROR, "Invalid chunk depth value");
            return -1;
        }
        opts->filters.chunk_depth_limit = depth;
    } else if (strcmp(name, "class") == 0) {
        REQUIRE_VALUE();
        free(opts->filters.class_name);
        opts->filters.class_name = duplicate_string(value);
        if (!opts->filters.class_name) {
            log_message(opts, LOG_ERROR, "Out of memory while parsing class filter");
            return -1;
        }
        opts->filters.has_class_filter = true;
    } else if (strcmp(name, "name") == 0) {
        REQUIRE_VALUE();
        free(opts->filters.name_pattern);
        opts->filters.name_pattern = duplicate_string(value);
        if (!opts->filters.name_pattern) {
            log_message(opts, LOG_ERROR, "Out of memory while parsing name filter");
            return -1;
        }
    } else if (strcmp(name, "manager") == 0) {
        REQUIRE_VALUE();
        nmo_guid_t parsed = nmo_guid_parse(value);
        if (nmo_guid_is_null(parsed) && !strings_equal_ci(value, "{00000000-00000000}")) {
            log_message(opts, LOG_ERROR, "Invalid manager GUID: %s", value);
            return -1;
        }
        opts->filters.manager_guid = parsed;
        opts->filters.has_manager_guid = true;
    } else if (strcmp(name, "root") == 0) {
        REQUIRE_VALUE();
        uint32_t id = 0;
        if (parse_u32_token(value, &id) != 0) {
            log_message(opts, LOG_ERROR, "Invalid root object ID");
            return -1;
        }
        opts->filters.root_object_id = (nmo_object_id_t)id;
        opts->filters.root_specified = true;
    } else if (strcmp(name, "scene") == 0) {
        REQUIRE_VALUE();
        free(opts->filters.scene_name);
        opts->filters.scene_name = duplicate_string(value);
        if (!opts->filters.scene_name) {
            log_message(opts, LOG_ERROR, "Out of memory while parsing scene name");
            return -1;
        }
    } else if (strcmp(name, "include-hidden") == 0) {
        opts->filters.include_hidden = true;
    } else if (strcmp(name, "compact") == 0) {
        opts->compact_output = true;
    } else if (strcmp(name, "max-rows") == 0) {
        REQUIRE_VALUE();
        size_t rows = 0;
        if (parse_size_token(value, &rows) != 0) {
            log_message(opts, LOG_ERROR, "Invalid max rows value");
            return -1;
        }
        opts->max_rows = rows;
    } else if (strcmp(name, "truncate") == 0) {
        REQUIRE_VALUE();
        size_t length = 0;
        if (parse_size_token(value, &length) != 0) {
            log_message(opts, LOG_ERROR, "Invalid truncate length");
            return -1;
        }
        opts->truncate_length = length;
    } else if (strcmp(name, "show-offsets") == 0) {
        opts->show_offsets = true;
    } else if (strcmp(name, "show-size") == 0) {
        opts->show_size = true;
    } else if (strcmp(name, "show-guids") == 0) {
        opts->show_guids = true;
    } else {
        log_message(opts, LOG_ERROR, "Unknown option --%s", name);
        return -1;
    }

    return 0;

    #undef REQUIRE_VALUE
}

static int parse_short_options(int argc, char **argv, int *index, inspect_options_t *opts) {
    const char *arg = argv[*index];
    for (size_t i = 1; arg[i]; ++i) {
        char opt = arg[i];
        switch (opt) {
            case 'h':
                opts->show_help = true;
                break;
            case 'V':
                opts->show_version = true;
                break;
            case 'v':
                opts->verbosity++;
                break;
            case 'o':
            case 'F': {
                const char *value = NULL;
                if (arg[i + 1]) {
                    value = &arg[i + 1];
                    i = strlen(arg) - 1;
                } else {
                    if (*index + 1 >= argc) {
                        log_message(opts, LOG_ERROR, "Option -%c requires a value", opt);
                        return -1;
                    }
                    value = argv[++(*index)];
                }
                if (opt == 'o') {
                    opts->output_path = value;
                } else {
                    if (strcmp(value, "text") == 0) {
                        opts->format = INSPECT_FORMAT_TEXT;
                    } else if (strcmp(value, "json") == 0) {
                        opts->format = INSPECT_FORMAT_JSON;
                    } else if (strcmp(value, "json-pretty") == 0) {
                        opts->format = INSPECT_FORMAT_JSON_PRETTY;
                    } else if (strcmp(value, "yaml") == 0) {
                        opts->format = INSPECT_FORMAT_YAML;
                    } else {
                        log_message(opts, LOG_ERROR, "Unknown format: %s", value);
                        return -1;
                    }
                }
                i = strlen(arg);
                break;
            }
            default:
                log_message(opts, LOG_ERROR, "Unknown option -%c", opt);
                return -1;
        }
    }
    return 0;
}

static int parse_args(int argc, char **argv, inspect_options_t *opts) {
    if (argc < 2) {
        print_usage();
        return -1;
    }

    bool end_of_options = false;
    for (int i = 1; i < argc; ++i) {
        if (!end_of_options && argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                end_of_options = true;
                continue;
            }
            if (argv[i][1] == '-') {
                if (parse_long_option(argc, argv, &i, opts, argv[i] + 2) != 0) {
                    return -1;
                }
            } else {
                if (parse_short_options(argc, argv, &i, opts) != 0) {
                    return -1;
                }
            }
        } else {
            if (opts->input_path) {
                log_message(opts, LOG_ERROR, "Multiple input files are not supported");
                return -1;
            }
            opts->input_path = argv[i];
        }
    }

    if (!opts->input_path && !opts->show_help && !opts->show_version) {
        log_message(opts, LOG_ERROR, "No input file specified");
        return -1;
    }

    if (!opts->modes.modes_requested) {
        opts->modes.summary = true;
        opts->modes.objects = true;
        opts->modes.warnings = true;
    }
    if (opts->modes.suppress_summary) {
        opts->modes.summary = false;
    }
    if (opts->modes.suppress_header) {
        opts->modes.header = false;
    }
    if (opts->modes.suppress_warnings) {
        opts->modes.warnings = false;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Data gathering                                                            */
/* ------------------------------------------------------------------------- */

static nmo_object_t *find_object_by_id(nmo_object_t **objects, size_t object_count, nmo_object_id_t id) {
    for (size_t i = 0; i < object_count; ++i) {
        if (nmo_object_get_id(objects[i]) == id) {
            return objects[i];
        }
    }
    return NULL;
}

static void resolve_scene_root(inspect_state_t *state, inspect_options_t *opts) {
    if (!opts->filters.scene_name || opts->filters.root_specified) {
        return;
    }
    if (!state->registry) {
        log_message(opts, LOG_WARN, "Scene filtering requested but schema registry unavailable");
        return;
    }
    nmo_class_id_t scene_class = nmo_ckclass_get_id_by_name("CKScene");
    nmo_class_id_t level_class = nmo_ckclass_get_id_by_name("CKLevel");
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        const char *name = safe_object_name(object);
        if (!strings_equal_ci(name, opts->filters.scene_name)) {
            continue;
        }
        nmo_class_id_t class_id = nmo_object_get_class_id(object);
        if ((scene_class && nmo_class_is_derived_from(state->registry, class_id, scene_class)) ||
            (level_class && nmo_class_is_derived_from(state->registry, class_id, level_class))) {
            opts->filters.root_object_id = nmo_object_get_id(object);
            opts->filters.root_specified = true;
            break;
        }
    }
    if (!opts->filters.root_specified) {
        log_message(opts, LOG_WARN, "Scene '%s' not found", opts->filters.scene_name);
    }
}

static bool resolve_class_filter(inspect_options_t *opts) {
    if (!opts->filters.has_class_filter || !opts->filters.class_name) {
        return true;
    }
    uint32_t class_id = nmo_ckclass_get_id_by_name(opts->filters.class_name);
    if (!class_id) {
        log_message(opts, LOG_ERROR, "Unknown class name: %s", opts->filters.class_name);
        return false;
    }
    opts->filters.class_id = (nmo_class_id_t)class_id;
    return true;
}

static void collect_stats(inspect_state_t *state) {
    if (nmo_stats_collect(state->session, &state->stats) == 0) {
        state->has_stats = true;
    }
    if (nmo_session_get_finish_loading_stats(state->session, &state->finish_stats) == NMO_OK) {
        state->has_finish_stats = true;
    }
    const nmo_file_header_t *file_header = (const nmo_file_header_t *)nmo_session_get_header(state->session);
    if (file_header != NULL) {
        memcpy(&state->file_header, file_header, sizeof(nmo_file_header_t));
        state->has_file_header = true;
    }
}

static void collect_plugin_warnings(const inspect_state_t *state, const inspect_options_t *opts, warning_list_t *warnings) {
    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(state->session);
    if (!diag || !diag->entries) {
        return;
    }
    for (size_t i = 0; i < diag->entry_count; ++i) {
        const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
        if (!entry->status_flags) {
            continue;
        }
        char guid_buffer[64];
        nmo_guid_format(entry->guid, guid_buffer, sizeof(guid_buffer));
        char message[256];
        snprintf(message, sizeof(message), "Plugin %s (%s) status flags=0x%X", entry->resolved_name ? entry->resolved_name : guid_buffer, guid_buffer, entry->status_flags);
        if (!warning_list_add(warnings, "PluginDependency", message, 0)) {
            log_message(opts, LOG_ERROR, "Failed to record plugin warning");
            return;
        }
    }
}

static void collect_chunk_warnings(const inspect_state_t *state, const inspect_options_t *opts, warning_list_t *warnings, bool *strict_failure) {
    if (!opts->modes.warnings && !opts->fail_on_warning && !opts->strict_mode) {
        return;
    }
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(object);
        if (!chunk) {
            char message[128];
            snprintf(message, sizeof(message), "Object %u has no chunk", nmo_object_get_id(object));
            if (!warning_list_add(warnings, "MissingChunk", message, nmo_object_get_id(object))) {
                log_message(opts, LOG_ERROR, "Failed to record warning for object %u", nmo_object_get_id(object));
            }
            if (opts->strict_mode) {
                *strict_failure = true;
            }
            continue;
        }
        if (opts->strict_mode) {
            nmo_chunk_validation_t result;
            int rc = nmo_inspector_validate_chunk(chunk, &result);
            if (rc != 0 || !result.is_valid) {
                char message[128];
                snprintf(message, sizeof(message), "Chunk validation failed for object %u", nmo_object_get_id(object));
                if (!warning_list_add(warnings, "ChunkInvalid", message, nmo_object_get_id(object))) {
                    log_message(opts, LOG_ERROR, "Failed to record validation warning");
                }
                *strict_failure = true;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Text output                                                               */
/* ------------------------------------------------------------------------- */

static void print_summary_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, const warning_list_t *warnings, bool colorize) {
    if (!opts->modes.summary) {
        return;
    }
    if (opts->compact_output) {
        fprintf(out, "SUMMARY file=%s objects=%u warnings=%zu\n",
                opts->input_path ? opts->input_path : "<stdin>",
                state->file_info.object_count,
                warnings->count);
        return;
    }
    print_heading(out, opts, "Summary", colorize);
    fprintf(out, "File: %s\n", opts->input_path ? opts->input_path : "<stdin>");
    fprintf(out, "Container: %s\n", detect_container(opts->input_path));
    fprintf(out, "Objects: %u\n", state->file_info.object_count);
    fprintf(out, "Managers: %u\n", state->file_info.manager_count);
    fprintf(out, "CK Version: %u\n", state->file_info.ck_version);
    if (state->has_stats) {
        fprintf(out, "Unique classes: %zu\n", state->stats.objects.unique_classes);
    }
    fprintf(out, "Warnings: %zu\n", warnings->count);
    fprintf(out, "\n");
}

static void print_header_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.header) {
        return;
    }
    print_heading(out, opts, "Header", colorize);
    if (!state->has_file_header) {
        fprintf(out, "Header information unavailable\n\n");
        return;
    }

    const nmo_file_header_t *header = &state->file_header;
    char signature[9];
    memcpy(signature, header->signature, 8);
    signature[8] = '\0';

    uint32_t header_bytes = (header->file_version >= 5) ? 64u : 32u;
    uint64_t approx_file_size = (uint64_t)header_bytes + header->hdr1_pack_size + header->data_pack_size;

    fprintf(out, "Signature: %s\n", signature);
    fprintf(out, "File version: %u (secondary %u)\n", header->file_version, header->file_version2);
    fprintf(out, "CK version: 0x%08X\n", header->ck_version);
    fprintf(out, "CRC: 0x%08X\n", header->crc);
    fprintf(out, "Write mode: 0x%X\n", header->file_write_mode);
    fprintf(out, "Header1 packed size: %u bytes\n", header->hdr1_pack_size);
    if (header->file_version >= 5) {
        fprintf(out, "Data packed size: %u bytes (unpacked %u bytes)\n",
                header->data_pack_size, header->data_unpack_size);
        fprintf(out, "Objects (header): %u  Managers: %u\n",
                header->object_count, header->manager_count);
        fprintf(out, "Max ID saved: %u\n", header->max_id_saved);
        fprintf(out, "Product version/build: %u / %u\n",
                header->product_version, header->product_build);
        fprintf(out, "Header1 unpacked size: %u bytes\n", header->hdr1_unpack_size);
    }
    fprintf(out, "Estimated file size: %llu bytes\n",
            (unsigned long long)approx_file_size);
    fprintf(out, "Header bytes: %u\n\n", header_bytes);
}

static void print_objects_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.objects) {
        return;
    }
    print_heading(out, opts, "Objects", colorize);
    size_t rows_written = 0;
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        if (!object_matches_filters(state, opts, object)) {
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated, more objects not shown ...\n");
            break;
        }
        const char *name = safe_object_name(object);
        char buffer[256];
        match_truncate(opts, name, buffer, sizeof(buffer));
        nmo_class_id_t class_id = nmo_object_get_class_id(object);
        fprintf(out, "[%u] %-16s %-8s", nmo_object_get_id(object), class_name_from_id(class_id), buffer);
        if (opts->show_guids) {
            nmo_guid_t type_guid = nmo_object_get_type_guid(object);
            char guid_buf[64];
            if (!nmo_guid_is_null(type_guid)) {
                nmo_guid_format(type_guid, guid_buf, sizeof(guid_buf));
                fprintf(out, " guid=%s", guid_buf);
            }
        }
        fprintf(out, "\n");
        rows_written++;
    }
    fprintf(out, "\n");
}

static void print_hierarchy_node(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object, size_t depth, size_t *rows_written, bool colorize) {
    bool visible = object_matches_filters(state, opts, object);
    if (visible) {
        if (opts->max_rows && *rows_written >= opts->max_rows) {
            return;
        }
        for (size_t i = 0; i < depth; ++i) {
            fprintf(out, "  ");
        }
        fprintf(out, "- [%u] %s\n", nmo_object_get_id(object), safe_object_name(object));
        (*rows_written)++;
    }
    size_t child_count = nmo_object_get_child_count(object);
    for (size_t i = 0; i < child_count; ++i) {
        nmo_object_t *child = nmo_object_get_child(object, i);
        print_hierarchy_node(out, state, opts, child, depth + 1, rows_written, colorize);
        if (opts->max_rows && *rows_written >= opts->max_rows) {
            return;
        }
    }
}

static void print_hierarchy_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.hierarchy) {
        return;
    }
    print_heading(out, opts, "Hierarchy", colorize);
    size_t rows_written = 0;
    if (opts->filters.root_specified) {
        nmo_object_t *root = find_object_by_id(state->objects, state->object_count, opts->filters.root_object_id);
        if (root) {
            print_hierarchy_node(out, state, opts, root, 0, &rows_written, colorize);
        }
    } else {
        for (size_t i = 0; i < state->object_count; ++i) {
            nmo_object_t *object = state->objects[i];
            if (object->parent != NULL) {
                continue;
            }
            print_hierarchy_node(out, state, opts, object, 0, &rows_written, colorize);
            if (opts->max_rows && rows_written >= opts->max_rows) {
                break;
            }
        }
    }
    fprintf(out, "\n");
}

static void print_chunk_info(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.chunks) {
        return;
    }
    print_heading(out, opts, "Chunks", colorize);
    size_t rows_written = 0;
    size_t chunk_index = 0;
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(object);
        if (!chunk) {
            continue;
        }
        if (!object_matches_filters(state, opts, object)) {
            chunk_index++;
            continue;
        }
        if (!chunk_matches_filters(&opts->filters, chunk->class_id, chunk_index)) {
            chunk_index++;
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }
        size_t size_bytes = chunk->data_size * sizeof(uint32_t);
        fprintf(out, "#%zu Object=%u ChunkClass=%u", chunk_index, nmo_object_get_id(object), chunk->class_id);
        if (opts->show_size) {
            fprintf(out, " Size=%zu bytes", size_bytes);
        }
        if (opts->show_offsets) {
            fprintf(out, " Offset=n/a");
        }
        fprintf(out, " SubChunks=%zu\n", chunk->chunk_count);
        rows_written++;
        chunk_index++;
    }
    fprintf(out, "\n");
}

static void print_chunk_tree_node(FILE *out, const inspect_options_t *opts, const nmo_chunk_t *chunk, size_t depth, size_t *rows_written) {
    if (opts->filters.chunk_depth_limit && depth > opts->filters.chunk_depth_limit) {
        return;
    }
    for (size_t i = 0; i < depth; ++i) {
        fprintf(out, "  ");
    }
    fprintf(out, "- ChunkClass=%u SubChunks=%zu", chunk->class_id, chunk->chunk_count);
    if (opts->show_size) {
        size_t size_bytes = chunk->data_size * sizeof(uint32_t);
        fprintf(out, " Size=%zu", size_bytes);
    }
    fprintf(out, "\n");
    (*rows_written)++;
    for (size_t i = 0; i < chunk->chunk_count; ++i) {
        const nmo_chunk_t *child = chunk->chunks[i];
        if (!child) {
            continue;
        }
        print_chunk_tree_node(out, opts, child, depth + 1, rows_written);
        if (opts->max_rows && *rows_written >= opts->max_rows) {
            return;
        }
    }
}

static void print_chunk_tree(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.chunk_tree) {
        return;
    }
    print_heading(out, opts, "Chunk Tree", colorize);
    size_t rows_written = 0;
    size_t chunk_index = 0;
    for (size_t i = 0; i < state->object_count; ++i) {
        const nmo_object_t *object = state->objects[i];
        const nmo_chunk_t *chunk = nmo_object_get_chunk(object);
        if (!chunk) {
            continue;
        }
        if (!object_matches_filters(state, opts, object)) {
            chunk_index++;
            continue;
        }
        if (!chunk_matches_filters(&opts->filters, chunk->class_id, chunk_index)) {
            chunk_index++;
            continue;
        }
        fprintf(out, "Object %u (%s)\n", nmo_object_get_id(object), safe_object_name(object));
        print_chunk_tree_node(out, opts, chunk, 1, &rows_written);
        chunk_index++;
        if (opts->max_rows && rows_written >= opts->max_rows) {
            break;
        }
    }
    fprintf(out, "\n");
}

static void print_manager_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.managers) {
        return;
    }
    print_heading(out, opts, "Managers", colorize);
    uint32_t count = 0;
    nmo_manager_data_t *managers = nmo_session_get_manager_data(state->session, &count);
    if (!managers || count == 0) {
        fprintf(out, "No manager data\n\n");
        return;
    }
    size_t rows_written = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }
        char guid_buf[64];
        nmo_guid_format(managers[i].guid, guid_buf, sizeof(guid_buf));
        fprintf(out, "[%u] GUID=%s Size=%u bytes\n", i, guid_buf, managers[i].data_size);
        rows_written++;
    }
    fprintf(out, "\n");
}

static bool behavior_matches(const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object) {
    if (!state->registry) {
        return false;
    }
    nmo_class_id_t class_id = nmo_object_get_class_id(object);
    static nmo_class_id_t behavior_id = 0;
    static nmo_class_id_t script_behavior_id = 0;
    if (!behavior_id) {
        behavior_id = nmo_ckclass_get_id_by_name("CKBehavior");
    }
    if (!script_behavior_id) {
        script_behavior_id = nmo_ckclass_get_id_by_name("CKScriptBehavior");
    }
    bool is_behavior = false;
    if (behavior_id && nmo_class_is_derived_from(state->registry, class_id, behavior_id)) {
        is_behavior = true;
    }
    if (!is_behavior && script_behavior_id && nmo_class_is_derived_from(state->registry, class_id, script_behavior_id)) {
        is_behavior = true;
    }
    if (!is_behavior) {
        return false;
    }
    if (opts->filters.behavior_id_count > 0) {
        if (!contains_object_id(opts->filters.behavior_ids, opts->filters.behavior_id_count, nmo_object_get_id(object))) {
            return false;
        }
    }
    return object_matches_filters(state, opts, object);
}

static void print_behavior_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.behaviors) {
        return;
    }
    print_heading(out, opts, "Behaviors", colorize);
    size_t rows_written = 0;
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        if (!behavior_matches(state, opts, object)) {
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }
        fprintf(out, "[%u] %s\n", nmo_object_get_id(object), safe_object_name(object));
        rows_written++;
    }
    fprintf(out, "\n");
}

static bool parameter_matches(const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object) {
    if (!state->registry) {
        return false;
    }
    nmo_class_id_t class_id = nmo_object_get_class_id(object);
    static nmo_class_id_t parameter_id = 0;
    if (!parameter_id) {
        parameter_id = nmo_ckclass_get_id_by_name("CKParameter");
    }
    if (!parameter_id) {
        return false;
    }
    if (!nmo_class_is_derived_from(state->registry, class_id, parameter_id)) {
        return false;
    }
    return object_matches_filters(state, opts, object);
}

static void print_parameter_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.parameters) {
        return;
    }
    print_heading(out, opts, "Parameters", colorize);
    size_t rows_written = 0;
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        if (!parameter_matches(state, opts, object)) {
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }
        fprintf(out, "[%u] %s\n", nmo_object_get_id(object), safe_object_name(object));
        rows_written++;
    }
    fprintf(out, "\n");
}

static void print_resource_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.resources) {
        return;
    }
    print_heading(out, opts, "Resources", colorize);
    uint32_t count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(state->session, &count);
    if (!files || count == 0) {
        fprintf(out, "No included resources\n\n");
        return;
    }
    size_t rows_written = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }
        fprintf(out, "[%u] %s (%u bytes) owners=%u\n", i, files[i].name ? files[i].name : "(unnamed)", files[i].size, files[i].owner_count);
        rows_written++;
    }
    fprintf(out, "\n");
}

static void print_warnings_section(FILE *out, const inspect_options_t *opts, const warning_list_t *warnings, bool colorize) {
    if (!opts->modes.warnings) {
        return;
    }
    print_heading(out, opts, "Warnings", colorize);
    if (warnings->count == 0) {
        fprintf(out, "(none)\n\n");
        return;
    }
    size_t rows_written = 0;
    for (size_t i = 0; i < warnings->count; ++i) {
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }
        if (warnings->items[i].object_id) {
            fprintf(out, "%s: %s (object %u)\n", warnings->items[i].code, warnings->items[i].message, warnings->items[i].object_id);
        } else {
            fprintf(out, "%s: %s\n", warnings->items[i].code, warnings->items[i].message);
        }
        rows_written++;
    }
    fprintf(out, "\n");
}

static void render_text_report(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, const warning_list_t *warnings) {
    bool colorize = should_use_color(opts, out);
    print_summary_section(out, state, opts, warnings, colorize);
    print_header_section(out, state, opts, colorize);
    print_objects_section(out, state, opts, colorize);
    print_hierarchy_section(out, state, opts, colorize);
    print_chunk_info(out, state, opts, colorize);
    print_chunk_tree(out, state, opts, colorize);
    print_manager_section(out, state, opts, colorize);
    print_behavior_section(out, state, opts, colorize);
    print_parameter_section(out, state, opts, colorize);
    print_resource_section(out, state, opts, colorize);
    print_warnings_section(out, opts, warnings, colorize);
}

/* ------------------------------------------------------------------------- */
/* JSON helpers                                                              */
/* ------------------------------------------------------------------------- */

static const char *json_prepare_string(const inspect_options_t *opts, const char *value, char *buffer, size_t buffer_size) {
    if (!value) {
        return "";
    }
    if (opts->truncate_length == 0) {
        return value;
    }
    if (strlen(value) <= opts->truncate_length || buffer_size == 0) {
        return value;
    }
    match_truncate(opts, value, buffer, buffer_size);
    return buffer;
}

static void json_add_file_section(yyjson_mut_doc *doc, yyjson_mut_val *root, const inspect_state_t *state, const inspect_options_t *opts) {
    yyjson_mut_val *file = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, file, "path", opts->input_path ? opts->input_path : "<stdin>");
    yyjson_mut_obj_add_str(doc, file, "container", detect_container(opts->input_path));
    yyjson_mut_obj_add_uint(doc, file, "object_count", state->file_info.object_count);
    yyjson_mut_obj_add_uint(doc, file, "manager_count", state->file_info.manager_count);
    yyjson_mut_obj_add_uint(doc, file, "ck_version", state->file_info.ck_version);
    yyjson_mut_obj_add_uint(doc, file, "file_version", state->file_info.file_version);
    yyjson_mut_obj_add_val(doc, root, "file", file);
}

static void json_add_header_section(yyjson_mut_doc *doc, yyjson_mut_val *root, const inspect_state_t *state) {
    yyjson_mut_val *header = yyjson_mut_obj(doc);
    if (!state->has_file_header) {
        yyjson_mut_obj_add_bool(doc, header, "available", false);
        yyjson_mut_obj_add_val(doc, root, "header", header);
        return;
    }

    const nmo_file_header_t *fh = &state->file_header;
    char signature[9];
    memcpy(signature, fh->signature, 8);
    signature[8] = '\0';

    uint32_t header_bytes = (fh->file_version >= 5) ? 64u : 32u;
    uint64_t approx_file_size = (uint64_t)header_bytes + fh->hdr1_pack_size + fh->data_pack_size;

    yyjson_mut_obj_add_str(doc, header, "signature", signature);
    yyjson_mut_obj_add_uint(doc, header, "crc", fh->crc);
    yyjson_mut_obj_add_uint(doc, header, "ck_version", fh->ck_version);
    yyjson_mut_obj_add_uint(doc, header, "file_version", fh->file_version);
    yyjson_mut_obj_add_uint(doc, header, "file_version2", fh->file_version2);
    yyjson_mut_obj_add_uint(doc, header, "write_mode", fh->file_write_mode);
    yyjson_mut_obj_add_uint(doc, header, "header1_pack_size", fh->hdr1_pack_size);
    yyjson_mut_obj_add_uint(doc, header, "header_bytes", header_bytes);
    yyjson_mut_obj_add_uint(doc, header, "estimated_file_size", (uint64_t)approx_file_size);

    if (fh->file_version >= 5) {
        yyjson_mut_obj_add_uint(doc, header, "data_pack_size", fh->data_pack_size);
        yyjson_mut_obj_add_uint(doc, header, "data_unpack_size", fh->data_unpack_size);
        yyjson_mut_obj_add_uint(doc, header, "manager_count", fh->manager_count);
        yyjson_mut_obj_add_uint(doc, header, "object_count", fh->object_count);
        yyjson_mut_obj_add_uint(doc, header, "max_id_saved", fh->max_id_saved);
        yyjson_mut_obj_add_uint(doc, header, "product_version", fh->product_version);
        yyjson_mut_obj_add_uint(doc, header, "product_build", fh->product_build);
        yyjson_mut_obj_add_uint(doc, header, "header1_unpack_size", fh->hdr1_unpack_size);
    }

    yyjson_mut_obj_add_val(doc, root, "header", header);
}

static void json_add_objects_section(yyjson_mut_doc *doc, yyjson_mut_val *root, const inspect_state_t *state, const inspect_options_t *opts) {
    yyjson_mut_val *objects = yyjson_mut_arr(doc);
    size_t rows_written = 0;
    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        if (!object_matches_filters(state, opts, object)) {
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            break;
        }
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, entry, "id", nmo_object_get_id(object));
        yyjson_mut_obj_add_uint(doc, entry, "class_id", nmo_object_get_class_id(object));
        yyjson_mut_obj_add_str(doc, entry, "class", class_name_from_id(nmo_object_get_class_id(object)));
        char truncated[512];
        const char *name_value = json_prepare_string(opts, safe_object_name(object), truncated, sizeof(truncated));
        yyjson_mut_obj_add_str(doc, entry, "name", name_value);
        yyjson_mut_arr_append(objects, entry);
        rows_written++;
    }
    yyjson_mut_obj_add_val(doc, root, "objects", objects);
}

static void json_add_warnings_section(yyjson_mut_doc *doc, yyjson_mut_val *root, const inspect_options_t *opts, const warning_list_t *warnings) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    size_t limit = warnings->count;
    if (opts->max_rows && limit > opts->max_rows) {
        limit = opts->max_rows;
    }
    for (size_t i = 0; i < limit; ++i) {
        const inspect_warning_t *warn = &warnings->items[i];
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, entry, "code", warn->code);
        yyjson_mut_obj_add_str(doc, entry, "message", warn->message);
        if (warn->object_id) {
            yyjson_mut_obj_add_uint(doc, entry, "object_id", warn->object_id);
        }
        yyjson_mut_arr_append(arr, entry);
    }
    yyjson_mut_obj_add_val(doc, root, "warnings", arr);
}

static void render_json_report(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, const warning_list_t *warnings) {
    if (opts->format == INSPECT_FORMAT_YAML) {
        log_message(opts, LOG_WARN, "YAML output not yet implemented; falling back to JSON");
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        log_message(opts, LOG_ERROR, "Failed to allocate JSON document");
        return;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    json_add_file_section(doc, root, state, opts);
    if (opts->modes.header) {
        json_add_header_section(doc, root, state);
    }
    if (opts->modes.objects) {
        json_add_objects_section(doc, root, state, opts);
    }
    if (opts->modes.warnings) {
        json_add_warnings_section(doc, root, opts, warnings);
    }

    yyjson_write_flag flags = YYJSON_WRITE_ESCAPE_UNICODE;
    if (opts->format == INSPECT_FORMAT_JSON_PRETTY) {
        flags |= YYJSON_WRITE_PRETTY;
    }

    yyjson_write_err err;
    size_t json_length = 0;
    char *json_text = yyjson_mut_write_opts(doc, flags, NULL, &json_length, &err);
    if (!json_text) {
        log_message(opts, LOG_ERROR, "Failed to serialize JSON: %s", err.msg ? err.msg : "unknown error");
        yyjson_mut_doc_free(doc);
        return;
    }

    fputs(json_text, out);
    fputc('\n', out);
    free(json_text);
    yyjson_mut_doc_free(doc);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    inspect_options_t opts;
    options_init(&opts);

    if (parse_args(argc, argv, &opts) != 0) {
        options_free(&opts);
        return 1;
    }

    if (opts.show_help) {
        print_usage();
        options_free(&opts);
        return 0;
    }
    if (opts.show_version) {
        printf("nmo-inspect %d.%d.%d\n", NMO_VERSION_MAJOR, NMO_VERSION_MINOR, NMO_VERSION_PATCH);
        options_free(&opts);
        return 0;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        log_message(&opts, LOG_ERROR, "Failed to create libnmo context");
        options_free(&opts);
        return 5;
    }

    nmo_session_t *session = nmo_session_load(ctx, opts.input_path);
    if (!session) {
        log_message(&opts, LOG_ERROR, "Failed to load %s", opts.input_path);
        nmo_context_destroy(ctx);
        options_free(&opts);
        return 2;
    }

    inspect_state_t state;
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    state.session = session;
    state.registry = nmo_context_get_schema_registry(ctx);
    state.file_info = nmo_session_get_file_info(session);
    if (nmo_session_get_objects(session, &state.objects, &state.object_count) != 0) {
        log_message(&opts, LOG_ERROR, "Failed to query objects from session");
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        options_free(&opts);
        return 5;
    }

    if (!resolve_class_filter(&opts)) {
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        options_free(&opts);
        return 1;
    }
    if (opts.filters.has_manager_guid) {
        log_message(&opts, LOG_WARN, "Manager GUID filtering is not implemented yet and will be ignored");
    }
    resolve_scene_root(&state, &opts);
    collect_stats(&state);

    warning_list_t warnings;
    warning_list_init(&warnings);
    bool strict_failure = false;
    collect_plugin_warnings(&state, &opts, &warnings);
    collect_chunk_warnings(&state, &opts, &warnings, &strict_failure);

    FILE *output = stdout;
    if (opts.output_path) {
        output = fopen(opts.output_path, "w");
        if (!output) {
            log_message(&opts, LOG_ERROR, "Failed to open %s", opts.output_path);
            warning_list_free(&warnings);
            nmo_session_destroy(session);
            nmo_context_destroy(ctx);
            options_free(&opts);
            return 2;
        }
    }

    if (opts.format == INSPECT_FORMAT_TEXT) {
        render_text_report(output, &state, &opts, &warnings);
    } else {
        render_json_report(output, &state, &opts, &warnings);
    }

    if (output != stdout) {
        fclose(output);
    }

    int exit_code = 0;
    if (strict_failure && opts.strict_mode) {
        exit_code = 3;
    }
    if (opts.fail_on_warning && warnings.count > 0) {
        exit_code = 4;
    }

    warning_list_free(&warnings);
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
    options_free(&opts);
    return exit_code;
}
