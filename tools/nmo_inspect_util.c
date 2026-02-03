#include "nmo_inspect_util.h"

#include "nmo_tool_common.h"

#include "object/nmo_class_hierarchy.h"
#include "object/nmo_ckparameter_schemas.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
extern int fileno(FILE *);
#endif

void nmo_inspect_log(const inspect_options_t *opts, log_level_t level, const char *fmt, ...) {
    log_level_t max_level = LOG_ERROR;
    if (opts && opts->verbosity >= 1) {
        max_level = LOG_INFO;
    }
    if (opts && opts->verbosity >= 2) {
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

void nmo_inspect_warning_list_init(warning_list_t *warnings) {
    if (!warnings) {
        return;
    }
    warnings->items = NULL;
    warnings->count = 0;
    warnings->capacity = 0;
}

void nmo_inspect_warning_list_free(warning_list_t *warnings) {
    if (!warnings) {
        return;
    }
    free(warnings->items);
    warnings->items = NULL;
    warnings->count = 0;
    warnings->capacity = 0;
}

bool nmo_inspect_warning_list_add(warning_list_t *warnings, const char *code, const char *message, nmo_object_id_t object_id) {
    if (!warnings) {
        return false;
    }
    if (warnings->count == warnings->capacity) {
        size_t new_capacity = warnings->capacity ? warnings->capacity * 2 : 16;
        inspect_warning_t *new_items = (inspect_warning_t *)realloc(warnings->items, new_capacity * sizeof(inspect_warning_t));
        if (!new_items) {
            return false;
        }
        warnings->items = new_items;
        warnings->capacity = new_capacity;
    }

    inspect_warning_t *w = &warnings->items[warnings->count++];
    memset(w, 0, sizeof(*w));
    if (code) {
        strncpy(w->code, code, sizeof(w->code) - 1);
    }
    if (message) {
        strncpy(w->message, message, sizeof(w->message) - 1);
    }
    w->object_id = object_id;
    return true;
}

const char *nmo_inspect_safe_object_name(const nmo_object_t *object) {
    const char *name = nmo_object_get_name(object);
    return name ? name : "(unnamed)";
}

bool nmo_inspect_should_use_color(const inspect_options_t *opts, FILE *stream) {
    if (!opts) {
        return false;
    }
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

void nmo_inspect_print_heading(FILE *out, const inspect_options_t *opts, const char *title, bool colorize) {
    if (!out || !opts || !title) {
        return;
    }
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

bool nmo_inspect_match_truncate(const inspect_options_t *opts, const char *value, char *buffer, size_t buffer_size) {
    if (!value) {
        value = "";
    }
    if (!buffer || buffer_size == 0) {
        return false;
    }

    size_t limit = opts ? opts->truncate_length : 0;
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

const char *nmo_inspect_detect_container(const char *path) {
    if (!path) {
        return "unknown";
    }
    const char *ext = strrchr(path, '.');
    if (!ext || ext[1] == '\0') {
        return "unknown";
    }
    ext++;
    if (nmo_tool_stricmp(ext, "nmo") == 0) {
        return "NMO";
    }
    if (nmo_tool_stricmp(ext, "cmo") == 0) {
        return "CMO";
    }
    if (nmo_tool_stricmp(ext, "vmo") == 0) {
        return "VMO";
    }
    return ext;
}

nmo_type_registry_t *nmo_inspect_type_registry_from_state(const inspect_state_t *state) {
    if (!state || !state->ctx) {
        return NULL;
    }
    return nmo_context_get_type_registry(state->ctx);
}

nmo_class_id_t nmo_inspect_class_id_from_name(const inspect_state_t *state, const char *name) {
    if (!name || !*name) {
        return 0;
    }
    nmo_type_registry_t *registry = nmo_inspect_type_registry_from_state(state);
    if (!registry) {
        return 0;
    }
    nmo_type_id_t type_id = nmo_type_registry_name_to_type_id(registry, name);
    if (type_id == NMO_TYPE_ID_INVALID) {
        return 0;
    }
    uint32_t class_id_u32 = 0;
    nmo_status_t rc = nmo_type_registry_type_id_to_class_id(registry, type_id, &class_id_u32);
    if (rc != NMO_OK) {
        return 0;
    }
    return (nmo_class_id_t)class_id_u32;
}

const char *nmo_inspect_class_name_from_id(const inspect_state_t *state, nmo_class_id_t class_id) {
    const char *name = NULL;
    nmo_type_registry_t *registry = nmo_inspect_type_registry_from_state(state);
    if (registry) {
        nmo_type_id_t type_id = nmo_type_registry_class_id_to_type_id(registry, (uint32_t)class_id);
        if (type_id != NMO_TYPE_ID_INVALID) {
            name = nmo_type_registry_type_id_to_name(registry, type_id);
        }
    }

    static char buffer[32];
    if (name) {
        return name;
    }
    snprintf(buffer, sizeof(buffer), "Class#%u", class_id);
    return buffer;
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

/* Virtools CK_OBJECT_FLAGS bits (mirrored for tooling convenience). */
#define NMO_CK_OBJECT_PRIVATE      0x00000002u
#define NMO_CK_OBJECT_NOTTOBESAVED 0x00000020u

static bool object_is_hidden_default(const nmo_object_t *object) {
    if (!object) {
        return false;
    }
    return (object->flags & (NMO_CK_OBJECT_PRIVATE | NMO_CK_OBJECT_NOTTOBESAVED)) != 0u;
}

static bool object_matches_manager_filter(const inspect_state_t *state, const inspect_filters_t *filters, const nmo_object_t *object) {
    if (!filters->has_manager_guid) {
        return true;
    }

    static nmo_class_id_t parameter_id = 0;
    if (!parameter_id) {
        parameter_id = nmo_inspect_class_id_from_name(state, "CKParameter");
    }
    if (!parameter_id) {
        return false;
    }

    if (!nmo_class_is_derived_from(NULL, nmo_object_get_class_id(object), parameter_id)) {
        return false;
    }

    const nmo_ckparameter_state_t *param_state = (const nmo_ckparameter_state_t *)nmo_object_get_data(object);
    if (!param_state) {
        return false;
    }

    if (param_state->mode != NMO_CKPARAM_MODE_MANAGER) {
        return false;
    }

    return nmo_guid_equals(param_state->manager_guid, filters->manager_guid);
}

bool nmo_inspect_object_matches_filters(const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object) {
    (void)state;
    if (!opts || !object) {
        return false;
    }
    const inspect_filters_t *filters = &opts->filters;

    if (!filters->include_hidden && object_is_hidden_default(object)) {
        return false;
    }

    if (!object_matches_manager_filter(state, filters, object)) {
        return false;
    }

    if (filters->object_id_count > 0) {
        if (!contains_object_id(filters->object_ids, filters->object_id_count, nmo_object_get_id(object))) {
            return false;
        }
    }
    if (filters->has_class_filter) {
        if (!nmo_class_is_derived_from(NULL, nmo_object_get_class_id(object), filters->class_id)) {
            return false;
        }
    }
    if (filters->name_pattern) {
        if (!nmo_tool_match_wildcard_ci(filters->name_pattern, nmo_inspect_safe_object_name(object))) {
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

bool nmo_inspect_chunk_matches_filters(const inspect_filters_t *filters, uint32_t chunk_class_id, size_t chunk_index) {
    if (!filters) {
        return true;
    }
    if (filters->chunk_id_count > 0 && !contains_u32(filters->chunk_ids, filters->chunk_id_count, chunk_class_id)) {
        return false;
    }
    if (filters->chunk_index_count > 0 && !contains_size(filters->chunk_indexes, filters->chunk_index_count, chunk_index)) {
        return false;
    }
    return true;
}
