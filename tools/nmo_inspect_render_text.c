#include "nmo_inspect_render.h"

#include "nmo_inspect_collect.h"
#include "nmo_inspect_util.h"

#include "app/nmo_stats.h"
#include "object/nmo_class_hierarchy.h"

#include <string.h>

static void print_summary_section(FILE *out,
                                 const inspect_state_t *state,
                                 const inspect_options_t *opts,
                                 const warning_list_t *warnings,
                                 bool colorize) {
    if (!opts->modes.summary) {
        return;
    }

    if (opts->compact_output) {
        fprintf(out,
                "SUMMARY file=%s objects=%u warnings=%zu\n",
                opts->input_path ? opts->input_path : "<stdin>",
                state->file_info.object_count,
                warnings->count);
        return;
    }

    nmo_inspect_print_heading(out, opts, "Summary", colorize);
    fprintf(out, "File: %s\n", opts->input_path ? opts->input_path : "<stdin>");
    fprintf(out, "Container: %s\n", nmo_inspect_detect_container(opts->input_path));
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

    nmo_inspect_print_heading(out, opts, "Header", colorize);
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
        fprintf(out,
                "Data packed size: %u bytes (unpacked %u bytes)\n",
                header->data_pack_size,
                header->data_unpack_size);
        fprintf(out, "Objects (header): %u  Managers: %u\n", header->object_count, header->manager_count);
        fprintf(out, "Max ID saved: %u\n", header->max_id_saved);
        fprintf(out, "Product version/build: %u / %u\n", header->product_version, header->product_build);
        fprintf(out, "Header1 unpacked size: %u bytes\n", header->hdr1_unpack_size);
    }
    fprintf(out, "Estimated file size: %llu bytes\n", (unsigned long long)approx_file_size);
    fprintf(out, "Header bytes: %u\n\n", header_bytes);
}

static void print_stats_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.stats) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Stats", colorize);
    if (!state->has_stats) {
        fprintf(out, "Statistics unavailable\n\n");
        return;
    }

    nmo_stats_print(&state->stats, out);
    fprintf(out, "\n");
}

static void print_finish_stats_section(FILE *out,
                                      const inspect_state_t *state,
                                      const inspect_options_t *opts,
                                      bool colorize) {
    if (!opts->modes.finish_stats) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Finish Loading", colorize);
    if (!state->has_finish_stats) {
        fprintf(out, "Finish-loading statistics unavailable\n\n");
        return;
    }

    const nmo_finish_loading_stats_t *st = &state->finish_stats;
    fprintf(out, "Objects: %zu\n", st->total_objects);
    fprintf(out,
            "References: total=%u resolved=%u unresolved=%u ambiguous=%u\n",
            st->references.total,
            st->references.resolved,
            st->references.unresolved,
            st->references.ambiguous);
    fprintf(out,
            "Indexes: classes=%zu names=%zu guids=%zu memory=%zu bytes\n",
            st->indexes.class_entries,
            st->indexes.name_entries,
            st->indexes.guid_entries,
            st->indexes.memory_usage);
    fprintf(out, "Manager errors: %u\n\n", st->manager_errors);
}

static void print_plugins_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.plugins) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Plugins", colorize);

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(state->session);
    if (!diag) {
        fprintf(out, "Plugin diagnostics unavailable\n\n");
        return;
    }

    fprintf(out, "Plugin manager available: %s\n", diag->plugin_manager_available ? "yes" : "no");
    fprintf(out,
            "Missing: %zu  Outdated: %zu  Total entries: %zu\n",
            diag->missing_count,
            diag->outdated_count,
            diag->entry_count);

    if (!diag->entries || diag->entry_count == 0) {
        fprintf(out, "(no entries)\n\n");
        return;
    }

    size_t rows_written = 0;
    for (size_t i = 0; i < diag->entry_count; ++i) {
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }

        const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
        char guid_buf[64];
        nmo_guid_format(entry->guid, guid_buf, sizeof(guid_buf));
        fprintf(out,
                "- %s (%s) required=%u resolved=%u flags=0x%X\n",
                entry->resolved_name ? entry->resolved_name : "(unknown)",
                guid_buf,
                entry->required_version,
                entry->resolved_version,
                entry->status_flags);
        rows_written++;
    }

    fprintf(out, "\n");
}

static void print_objects_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.objects) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Objects", colorize);
    size_t rows_written = 0;

    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        if (!nmo_inspect_object_matches_filters(state, opts, object)) {
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated, more objects not shown ...\n");
            break;
        }

        const char *name = nmo_inspect_safe_object_name(object);
        char buffer[256];
        nmo_inspect_match_truncate(opts, name, buffer, sizeof(buffer));

        nmo_class_id_t class_id = nmo_object_get_class_id(object);
        fprintf(out,
                "[%u] %-16s %-8s",
                nmo_object_get_id(object),
                nmo_inspect_class_name_from_id(state, class_id),
                buffer);

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

static void print_hierarchy_node(FILE *out,
                                const inspect_state_t *state,
                                const inspect_options_t *opts,
                                const nmo_object_t *object,
                                size_t depth,
                                size_t *rows_written) {
    bool visible = nmo_inspect_object_matches_filters(state, opts, object);
    if (visible) {
        if (opts->max_rows && *rows_written >= opts->max_rows) {
            return;
        }

        for (size_t i = 0; i < depth; ++i) {
            fprintf(out, "  ");
        }

        fprintf(out, "- [%u] %s\n", nmo_object_get_id(object), nmo_inspect_safe_object_name(object));
        (*rows_written)++;
    }

    size_t child_count = nmo_object_get_child_count(object);
    for (size_t i = 0; i < child_count; ++i) {
        nmo_object_t *child = nmo_object_get_child(object, i);
        print_hierarchy_node(out, state, opts, child, depth + 1, rows_written);
        if (opts->max_rows && *rows_written >= opts->max_rows) {
            return;
        }
    }
}

static void print_hierarchy_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.hierarchy) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Hierarchy", colorize);
    size_t rows_written = 0;

    if (opts->filters.root_specified) {
        nmo_object_t *root = nmo_inspect_find_object_by_id(state->objects, state->object_count, opts->filters.root_object_id);
        if (root) {
            print_hierarchy_node(out, state, opts, root, 0, &rows_written);
        }
    } else {
        for (size_t i = 0; i < state->object_count; ++i) {
            nmo_object_t *object = state->objects[i];
            if (object->parent != NULL) {
                continue;
            }
            print_hierarchy_node(out, state, opts, object, 0, &rows_written);
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

    nmo_inspect_print_heading(out, opts, "Chunks", colorize);
    size_t rows_written = 0;
    size_t chunk_index = 0;

    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(object);
        if (!chunk) {
            continue;
        }

        if (!nmo_inspect_object_matches_filters(state, opts, object)) {
            chunk_index++;
            continue;
        }
        if (!nmo_inspect_chunk_matches_filters(&opts->filters, chunk->class_id, chunk_index)) {
            chunk_index++;
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            fprintf(out, "... truncated ...\n");
            break;
        }

        size_t size_bytes = chunk->data.count * sizeof(uint32_t);
        fprintf(out, "#%zu Object=%u ChunkClass=%u", chunk_index, nmo_object_get_id(object), chunk->class_id);
        if (opts->show_size) {
            fprintf(out, " Size=%zu bytes", size_bytes);
        }
        if (opts->show_offsets) {
            fprintf(out, " Offset=n/a");
        }
        fprintf(out, " SubChunks=%zu\n", chunk->chunks.count);
        rows_written++;
        chunk_index++;
    }

    fprintf(out, "\n");
}

static bool chunk_tree_limit_hit(const inspect_options_t *opts, size_t depth) {
    return opts->filters.chunk_depth_limit && depth > opts->filters.chunk_depth_limit;
}

static void print_chunk_tree_node(FILE *out,
                                 const inspect_options_t *opts,
                                 const nmo_chunk_t *chunk,
                                 size_t depth,
                                 size_t *rows_written) {
    if (chunk_tree_limit_hit(opts, depth)) {
        return;
    }

    for (size_t i = 0; i < depth; ++i) {
        fprintf(out, "  ");
    }

    fprintf(out, "- ChunkClass=%u SubChunks=%zu", chunk->class_id, chunk->chunks.count);
    if (opts->show_size) {
        size_t size_bytes = chunk->data.count * sizeof(uint32_t);
        fprintf(out, " Size=%zu", size_bytes);
    }
    fprintf(out, "\n");

    (*rows_written)++;

    const nmo_chunk_t *const *children = (const nmo_chunk_t *const *)chunk->chunks.data;
    for (size_t i = 0; i < chunk->chunks.count; ++i) {
        const nmo_chunk_t *child = children ? children[i] : NULL;
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

    nmo_inspect_print_heading(out, opts, "Chunk Tree", colorize);
    size_t rows_written = 0;
    size_t chunk_index = 0;

    for (size_t i = 0; i < state->object_count; ++i) {
        const nmo_object_t *object = state->objects[i];
        const nmo_chunk_t *chunk = nmo_object_get_chunk(object);
        if (!chunk) {
            continue;
        }

        if (!nmo_inspect_object_matches_filters(state, opts, object)) {
            chunk_index++;
            continue;
        }
        if (!nmo_inspect_chunk_matches_filters(&opts->filters, chunk->class_id, chunk_index)) {
            chunk_index++;
            continue;
        }

        fprintf(out, "Object %u (%s)\n", nmo_object_get_id(object), nmo_inspect_safe_object_name(object));
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

    nmo_inspect_print_heading(out, opts, "Managers", colorize);
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
    nmo_class_id_t class_id = nmo_object_get_class_id(object);

    static nmo_class_id_t behavior_id = 0;
    static nmo_class_id_t script_behavior_id = 0;
    if (!behavior_id) {
        behavior_id = nmo_inspect_class_id_from_name(state, "CKBehavior");
    }
    if (!script_behavior_id) {
        script_behavior_id = nmo_inspect_class_id_from_name(state, "CKScriptBehavior");
    }

    bool is_behavior = false;
    if (behavior_id && nmo_class_is_derived_from(NULL, class_id, behavior_id)) {
        is_behavior = true;
    }
    if (!is_behavior && script_behavior_id && nmo_class_is_derived_from(NULL, class_id, script_behavior_id)) {
        is_behavior = true;
    }
    if (!is_behavior) {
        return false;
    }

    if (opts->filters.behavior_id_count > 0) {
        nmo_object_id_t object_id = nmo_object_get_id(object);
        bool found = false;
        for (size_t i = 0; i < opts->filters.behavior_id_count; ++i) {
            if (opts->filters.behavior_ids[i] == object_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return nmo_inspect_object_matches_filters(state, opts, object);
}

static void print_behavior_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.behaviors) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Behaviors", colorize);
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
        fprintf(out, "[%u] %s\n", nmo_object_get_id(object), nmo_inspect_safe_object_name(object));
        rows_written++;
    }

    fprintf(out, "\n");
}

static bool parameter_matches(const inspect_state_t *state, const inspect_options_t *opts, const nmo_object_t *object) {
    nmo_class_id_t class_id = nmo_object_get_class_id(object);

    static nmo_class_id_t parameter_id = 0;
    if (!parameter_id) {
        parameter_id = nmo_inspect_class_id_from_name(state, "CKParameter");
    }
    if (!parameter_id) {
        return false;
    }
    if (!nmo_class_is_derived_from(NULL, class_id, parameter_id)) {
        return false;
    }

    return nmo_inspect_object_matches_filters(state, opts, object);
}

static void print_parameter_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.parameters) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Parameters", colorize);
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
        fprintf(out, "[%u] %s\n", nmo_object_get_id(object), nmo_inspect_safe_object_name(object));
        rows_written++;
    }

    fprintf(out, "\n");
}

static void print_resource_section(FILE *out, const inspect_state_t *state, const inspect_options_t *opts, bool colorize) {
    if (!opts->modes.resources) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Resources", colorize);
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
        fprintf(out,
            "[%u] %s (%u bytes) owners=%zu\n",
                i,
                files[i].name ? files[i].name : "(unnamed)",
                files[i].size,
            files[i].owner_ids.count);
        rows_written++;
    }

    fprintf(out, "\n");
}

static void print_warnings_section(FILE *out,
                                  const inspect_options_t *opts,
                                  const warning_list_t *warnings,
                                  bool colorize) {
    if (!opts->modes.warnings) {
        return;
    }

    nmo_inspect_print_heading(out, opts, "Warnings", colorize);
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

        const inspect_warning_t *w = &warnings->items[i];
        if (w->object_id) {
            fprintf(out, "%s: %s (object %u)\n", w->code, w->message, w->object_id);
        } else {
            fprintf(out, "%s: %s\n", w->code, w->message);
        }
        rows_written++;
    }

    fprintf(out, "\n");
}

void nmo_inspect_render_text(FILE *out,
                            const inspect_state_t *state,
                            const inspect_options_t *opts,
                            const warning_list_t *warnings) {
    bool colorize = nmo_inspect_should_use_color(opts, out);

    print_summary_section(out, state, opts, warnings, colorize);
    print_header_section(out, state, opts, colorize);
    print_stats_section(out, state, opts, colorize);
    print_finish_stats_section(out, state, opts, colorize);
    print_plugins_section(out, state, opts, colorize);
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
