#include "nmo_inspect_collect.h"

#include "nmo_inspect_util.h"
#include "nmo_tool_common.h"

#include "app/nmo_stats.h"
#include "app/nmo_inspector.h"
#include "object/nmo_class_hierarchy.h"

#include <string.h>

nmo_object_t *nmo_inspect_find_object_by_id(nmo_object_t **objects, size_t object_count, nmo_object_id_t id) {
    for (size_t i = 0; i < object_count; ++i) {
        if (nmo_object_get_id(objects[i]) == id) {
            return objects[i];
        }
    }
    return NULL;
}

void nmo_inspect_resolve_scene_root(inspect_state_t *state, inspect_options_t *opts) {
    if (!state || !opts) {
        return;
    }
    if (!opts->filters.scene_name || opts->filters.root_specified) {
        return;
    }

    nmo_class_id_t scene_class = nmo_inspect_class_id_from_name(state, "CKScene");
    nmo_class_id_t level_class = nmo_inspect_class_id_from_name(state, "CKLevel");

    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        const char *name = nmo_inspect_safe_object_name(object);
        if (!nmo_tool_streq_ci(name, opts->filters.scene_name)) {
            continue;
        }
        nmo_class_id_t class_id = nmo_object_get_class_id(object);
        if ((scene_class && nmo_class_is_derived_from(NULL, class_id, scene_class)) ||
            (level_class && nmo_class_is_derived_from(NULL, class_id, level_class))) {
            opts->filters.root_object_id = nmo_object_get_id(object);
            opts->filters.root_specified = true;
            break;
        }
    }

    if (!opts->filters.root_specified) {
        nmo_inspect_log(opts, LOG_WARN, "Scene '%s' not found", opts->filters.scene_name);
    }
}

bool nmo_inspect_resolve_class_filter(const inspect_state_t *state, inspect_options_t *opts) {
    if (!state || !opts) {
        return true;
    }
    if (!opts->filters.has_class_filter || !opts->filters.class_name) {
        return true;
    }

    uint32_t class_id = (uint32_t)nmo_inspect_class_id_from_name(state, opts->filters.class_name);
    if (!class_id) {
        nmo_inspect_log(opts, LOG_ERROR, "Unknown class name: %s", opts->filters.class_name);
        return false;
    }
    opts->filters.class_id = (nmo_class_id_t)class_id;
    return true;
}

void nmo_inspect_collect_stats(inspect_state_t *state) {
    if (!state || !state->session) {
        return;
    }

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

void nmo_inspect_collect_plugin_warnings(const inspect_state_t *state, const inspect_options_t *opts, warning_list_t *warnings) {
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
        snprintf(message,
                 sizeof(message),
                 "Plugin %s (%s) status flags=0x%X",
                 entry->resolved_name ? entry->resolved_name : guid_buffer,
                 guid_buffer,
                 entry->status_flags);

        if (!nmo_inspect_warning_list_add(warnings, "PluginDependency", message, 0)) {
            nmo_inspect_log(opts, LOG_ERROR, "Failed to record plugin warning");
            return;
        }
    }
}

void nmo_inspect_collect_chunk_warnings(const inspect_state_t *state,
                                       const inspect_options_t *opts,
                                       warning_list_t *warnings,
                                       bool *strict_failure) {
    if (!opts->modes.warnings && !opts->fail_on_warning && !opts->strict_mode) {
        return;
    }

    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(object);
        if (!chunk) {
            char message[128];
            snprintf(message, sizeof(message), "Object %u has no chunk", nmo_object_get_id(object));
            if (!nmo_inspect_warning_list_add(warnings, "MissingChunk", message, nmo_object_get_id(object))) {
                nmo_inspect_log(opts, LOG_ERROR, "Failed to record warning for object %u", nmo_object_get_id(object));
            }
            if (opts->strict_mode && strict_failure) {
                *strict_failure = true;
            }
            continue;
        }

        if (opts->strict_mode) {
            nmo_chunk_validation_t result;
            int rc = nmo_inspector_validate_chunk(chunk, &result);
            if (rc != 0 || !result.is_valid) {
                char message[128];
                if (rc != 0) {
                    snprintf(message, sizeof(message), "Chunk validation failed: rc=%d", rc);
                } else {
                    {
                        const char *err = result.error_message[0] ? result.error_message : "unknown";
                        const size_t prefix = sizeof("Chunk invalid: ") - 1;
                        const size_t avail = sizeof(message) > prefix + 1 ? sizeof(message) - prefix - 1 : 0;
                        snprintf(message, sizeof(message), "Chunk invalid: %.*s", (int)avail, err);
                    }
                }
                if (!nmo_inspect_warning_list_add(warnings, "ChunkInvalid", message, nmo_object_get_id(object))) {
                    nmo_inspect_log(opts, LOG_ERROR, "Failed to record chunk warning");
                }
                if (strict_failure) {
                    *strict_failure = true;
                }
            }
        }
    }
}
