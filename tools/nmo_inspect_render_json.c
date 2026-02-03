#include "nmo_inspect_render.h"

#include "nmo_inspect_util.h"

#include "yyjson.h"

#include <string.h>

static const char *json_prepare_string(const inspect_options_t *opts, const char *value, char *buffer, size_t buffer_size) {
    if (!value) {
        return "";
    }
    if (!opts || opts->truncate_length == 0) {
        return value;
    }
    if (strlen(value) <= opts->truncate_length || buffer_size == 0) {
        return value;
    }
    nmo_inspect_match_truncate(opts, value, buffer, buffer_size);
    return buffer;
}

static void json_add_file_section(yyjson_mut_doc *doc,
                                 yyjson_mut_val *root,
                                 const inspect_state_t *state,
                                 const inspect_options_t *opts) {
    yyjson_mut_val *file = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, file, "path", opts->input_path ? opts->input_path : "<stdin>");
    yyjson_mut_obj_add_str(doc, file, "container", nmo_inspect_detect_container(opts->input_path));
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

static void json_add_objects_section(yyjson_mut_doc *doc,
                                   yyjson_mut_val *root,
                                   const inspect_state_t *state,
                                   const inspect_options_t *opts) {
    yyjson_mut_val *objects = yyjson_mut_arr(doc);
    size_t rows_written = 0;

    for (size_t i = 0; i < state->object_count; ++i) {
        nmo_object_t *object = state->objects[i];
        if (!nmo_inspect_object_matches_filters(state, opts, object)) {
            continue;
        }
        if (opts->max_rows && rows_written >= opts->max_rows) {
            break;
        }

        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, entry, "id", nmo_object_get_id(object));
        yyjson_mut_obj_add_uint(doc, entry, "class_id", nmo_object_get_class_id(object));
        yyjson_mut_obj_add_str(doc,
                               entry,
                               "class",
                               nmo_inspect_class_name_from_id(state, nmo_object_get_class_id(object)));

        char truncated[512];
        const char *name_value = json_prepare_string(opts,
                                                     nmo_inspect_safe_object_name(object),
                                                     truncated,
                                                     sizeof(truncated));
        yyjson_mut_obj_add_str(doc, entry, "name", name_value);
        yyjson_mut_arr_append(objects, entry);
        rows_written++;
    }

    yyjson_mut_obj_add_val(doc, root, "objects", objects);
}

static void json_add_warnings_section(yyjson_mut_doc *doc,
                                     yyjson_mut_val *root,
                                     const inspect_options_t *opts,
                                     const warning_list_t *warnings) {
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

static void json_add_stats_section(yyjson_mut_doc *doc, yyjson_mut_val *root, const inspect_state_t *state) {
    yyjson_mut_val *stats = yyjson_mut_obj(doc);
    if (!state->has_stats) {
        yyjson_mut_obj_add_bool(doc, stats, "available", false);
        yyjson_mut_obj_add_val(doc, root, "stats", stats);
        return;
    }

    yyjson_mut_obj_add_bool(doc, stats, "available", true);

    yyjson_mut_val *objects = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, objects, "total_count", (uint64_t)state->stats.objects.total_count);
    yyjson_mut_obj_add_uint(doc, objects, "unique_classes", (uint64_t)state->stats.objects.unique_classes);
    yyjson_mut_obj_add_uint(doc, objects, "max_class_id", (uint64_t)state->stats.objects.max_class_id);
    yyjson_mut_obj_add_val(doc, stats, "objects", objects);

    yyjson_mut_val *memory = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, memory, "total_size", (uint64_t)state->stats.memory.total_size);
    yyjson_mut_obj_add_uint(doc, memory, "header_size", (uint64_t)state->stats.memory.header_size);
    yyjson_mut_obj_add_uint(doc, memory, "data_size", (uint64_t)state->stats.memory.data_size);
    yyjson_mut_obj_add_uint(doc, memory, "chunk_data_size", (uint64_t)state->stats.memory.chunk_data_size);
    yyjson_mut_obj_add_uint(doc, memory, "chunk_overhead", (uint64_t)state->stats.memory.chunk_overhead);
    yyjson_mut_obj_add_uint(doc, memory, "compression_ratio", (uint64_t)state->stats.memory.compression_ratio);
    yyjson_mut_obj_add_val(doc, stats, "memory", memory);

    yyjson_mut_val *performance = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, performance, "load_time_ms", state->stats.performance.load_time_ms);
    yyjson_mut_obj_add_real(doc, performance, "parse_time_ms", state->stats.performance.parse_time_ms);
    yyjson_mut_obj_add_real(doc, performance, "remap_time_ms", state->stats.performance.remap_time_ms);
    yyjson_mut_obj_add_val(doc, stats, "performance", performance);

    yyjson_mut_val *refs = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, refs, "total", (uint64_t)state->stats.references.total_references);
    yyjson_mut_obj_add_uint(doc, refs, "resolved", (uint64_t)state->stats.references.resolved);
    yyjson_mut_obj_add_uint(doc, refs, "unresolved", (uint64_t)state->stats.references.unresolved);
    yyjson_mut_obj_add_val(doc, stats, "references", refs);

    yyjson_mut_val *chunks = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, chunks, "total_chunks", (uint64_t)state->stats.chunks.total_chunks);
    yyjson_mut_obj_add_uint(doc, chunks, "compressed_chunks", (uint64_t)state->stats.chunks.compressed_chunks);
    yyjson_mut_obj_add_uint(doc, chunks, "max_chunk_size", (uint64_t)state->stats.chunks.max_chunk_size);
    yyjson_mut_obj_add_uint(doc, chunks, "avg_chunk_size", (uint64_t)state->stats.chunks.avg_chunk_size);
    yyjson_mut_obj_add_val(doc, stats, "chunks", chunks);

    yyjson_mut_obj_add_val(doc, root, "stats", stats);
}

static void json_add_finish_stats_section(yyjson_mut_doc *doc, yyjson_mut_val *root, const inspect_state_t *state) {
    yyjson_mut_val *finish = yyjson_mut_obj(doc);
    if (!state->has_finish_stats) {
        yyjson_mut_obj_add_bool(doc, finish, "available", false);
        yyjson_mut_obj_add_val(doc, root, "finish_loading", finish);
        return;
    }

    const nmo_finish_loading_stats_t *st = &state->finish_stats;
    yyjson_mut_obj_add_bool(doc, finish, "available", true);
    yyjson_mut_obj_add_uint(doc, finish, "total_objects", (uint64_t)st->total_objects);
    yyjson_mut_obj_add_uint(doc, finish, "flags", (uint64_t)st->flags);
    yyjson_mut_obj_add_uint(doc, finish, "manager_errors", (uint64_t)st->manager_errors);

    yyjson_mut_val *refs = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, refs, "total", (uint64_t)st->references.total);
    yyjson_mut_obj_add_uint(doc, refs, "resolved", (uint64_t)st->references.resolved);
    yyjson_mut_obj_add_uint(doc, refs, "unresolved", (uint64_t)st->references.unresolved);
    yyjson_mut_obj_add_uint(doc, refs, "ambiguous", (uint64_t)st->references.ambiguous);
    yyjson_mut_obj_add_val(doc, finish, "references", refs);

    yyjson_mut_val *indexes = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, indexes, "class_entries", (uint64_t)st->indexes.class_entries);
    yyjson_mut_obj_add_uint(doc, indexes, "name_entries", (uint64_t)st->indexes.name_entries);
    yyjson_mut_obj_add_uint(doc, indexes, "guid_entries", (uint64_t)st->indexes.guid_entries);
    yyjson_mut_obj_add_uint(doc, indexes, "memory_usage", (uint64_t)st->indexes.memory_usage);
    yyjson_mut_obj_add_val(doc, finish, "indexes", indexes);

    yyjson_mut_obj_add_val(doc, root, "finish_loading", finish);
}

static void json_add_plugins_section(yyjson_mut_doc *doc,
                                   yyjson_mut_val *root,
                                   const inspect_state_t *state,
                                   const inspect_options_t *opts) {
    yyjson_mut_val *plugins = yyjson_mut_obj(doc);
    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(state->session);
    if (!diag) {
        yyjson_mut_obj_add_bool(doc, plugins, "available", false);
        yyjson_mut_obj_add_val(doc, root, "plugins", plugins);
        return;
    }

    yyjson_mut_obj_add_bool(doc, plugins, "available", true);
    yyjson_mut_obj_add_bool(doc, plugins, "extension_registry_available", diag->extension_registry_available != 0);
    yyjson_mut_obj_add_uint(doc, plugins, "missing_count", (uint64_t)diag->missing_count);
    yyjson_mut_obj_add_uint(doc, plugins, "outdated_count", (uint64_t)diag->outdated_count);
    yyjson_mut_obj_add_uint(doc, plugins, "entry_count", (uint64_t)diag->entry_count);

    yyjson_mut_val *entries = yyjson_mut_arr(doc);
    size_t limit = diag->entry_count;
    if (opts->max_rows && limit > opts->max_rows) {
        limit = opts->max_rows;
    }

    for (size_t i = 0; i < limit; ++i) {
        const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        char guid_buf[64];
        nmo_guid_format(entry->guid, guid_buf, sizeof(guid_buf));
        yyjson_mut_obj_add_str(doc, obj, "guid", guid_buf);
        yyjson_mut_obj_add_uint(doc, obj, "category", (uint64_t)entry->category);
        yyjson_mut_obj_add_uint(doc, obj, "required_version", (uint64_t)entry->required_version);
        yyjson_mut_obj_add_uint(doc, obj, "resolved_version", (uint64_t)entry->resolved_version);
        yyjson_mut_obj_add_uint(doc, obj, "status_flags", (uint64_t)entry->status_flags);
        if (entry->resolved_name) {
            yyjson_mut_obj_add_str(doc, obj, "resolved_name", entry->resolved_name);
        }
        yyjson_mut_arr_append(entries, obj);
    }

    yyjson_mut_obj_add_val(doc, plugins, "entries", entries);
    yyjson_mut_obj_add_val(doc, root, "plugins", plugins);
}

static void yaml_write_indent(FILE *out, size_t indent) {
    for (size_t i = 0; i < indent; ++i) {
        fputc(' ', out);
    }
}

static void yaml_write_json_string(FILE *out, const char *str) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)str; p && *p; ++p) {
        switch (*p) {
            case '\\': fputs("\\\\", out); break;
            case '"':  fputs("\\\"", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (*p < 0x20u) {
                    fprintf(out, "\\u%04x", (unsigned int)*p);
                } else {
                    fputc((int)*p, out);
                }
                break;
        }
    }
    fputc('"', out);
}

static bool yaml_is_scalar(const yyjson_val *val) {
    yyjson_val *mutable_val = (yyjson_val *)val;
    return yyjson_is_null(mutable_val) || yyjson_is_bool(mutable_val) || yyjson_is_num(mutable_val) ||
           yyjson_is_str(mutable_val);
}

static void yaml_write_val(FILE *out, yyjson_val *val, size_t indent);

static void yaml_write_array(FILE *out, yyjson_val *val, size_t indent) {
    size_t idx, max;
    yyjson_val *item;
    yyjson_arr_foreach(val, idx, max, item) {
        yaml_write_indent(out, indent);
        fputs("- ", out);
        if (yaml_is_scalar(item)) {
            yaml_write_val(out, item, 0);
            fputc('\n', out);
        } else {
            fputc('\n', out);
            yaml_write_val(out, item, indent + 2);
        }
    }

    if (yyjson_arr_size(val) == 0) {
        yaml_write_indent(out, indent);
        fputs("[]\n", out);
    }
}

static void yaml_write_object(FILE *out, yyjson_val *val, size_t indent) {
    size_t idx, max;
    yyjson_val *key, *v;
    yyjson_obj_foreach(val, idx, max, key, v) {
        yaml_write_indent(out, indent);
        const char *k = yyjson_get_str(key);
        if (k && *k) {
            fputs(k, out);
        } else {
            fputs("\"\"", out);
        }
        fputs(":", out);
        if (yaml_is_scalar(v)) {
            fputc(' ', out);
            yaml_write_val(out, v, 0);
            fputc('\n', out);
        } else {
            fputc('\n', out);
            yaml_write_val(out, v, indent + 2);
        }
    }

    if (yyjson_obj_size(val) == 0) {
        yaml_write_indent(out, indent);
        fputs("{}\n", out);
    }
}

static void yaml_write_val(FILE *out, yyjson_val *val, size_t indent) {
    if (yyjson_is_null(val)) {
        fputs("null", out);
        return;
    }
    if (yyjson_is_bool(val)) {
        fputs(yyjson_get_bool(val) ? "true" : "false", out);
        return;
    }
    if (yyjson_is_int(val)) {
        fprintf(out, "%lld", (long long)yyjson_get_sint(val));
        return;
    }
    if (yyjson_is_uint(val)) {
        fprintf(out, "%llu", (unsigned long long)yyjson_get_uint(val));
        return;
    }
    if (yyjson_is_real(val)) {
        fprintf(out, "%.17g", yyjson_get_real(val));
        return;
    }
    if (yyjson_is_str(val)) {
        const char *s = yyjson_get_str(val);
        yaml_write_json_string(out, s ? s : "");
        return;
    }
    if (yyjson_is_arr(val)) {
        yaml_write_array(out, val, indent);
        return;
    }
    if (yyjson_is_obj(val)) {
        yaml_write_object(out, val, indent);
        return;
    }

    yaml_write_indent(out, indent);
    fputs("null", out);
}

void nmo_inspect_render_machine(FILE *out,
                               const inspect_state_t *state,
                               const inspect_options_t *opts,
                               const warning_list_t *warnings) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        nmo_inspect_log(opts, LOG_ERROR, "Failed to allocate JSON document");
        return;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    json_add_file_section(doc, root, state, opts);

    if (opts->modes.header) {
        json_add_header_section(doc, root, state);
    }
    if (opts->modes.stats) {
        json_add_stats_section(doc, root, state);
    }
    if (opts->modes.finish_stats) {
        json_add_finish_stats_section(doc, root, state);
    }
    if (opts->modes.plugins) {
        json_add_plugins_section(doc, root, state, opts);
    }
    if (opts->modes.objects) {
        json_add_objects_section(doc, root, state, opts);
    }
    if (opts->modes.warnings) {
        json_add_warnings_section(doc, root, opts, warnings);
    }

    if (opts->format == INSPECT_FORMAT_YAML) {
        yyjson_doc *imut = yyjson_mut_doc_imut_copy(doc, NULL);
        if (!imut) {
            nmo_inspect_log(opts, LOG_ERROR, "Failed to convert JSON document for YAML output");
            yyjson_mut_doc_free(doc);
            return;
        }

        yyjson_val *r = yyjson_doc_get_root(imut);
        yaml_write_val(out, r, 0);
        fputc('\n', out);
        yyjson_doc_free(imut);
        yyjson_mut_doc_free(doc);
        return;
    }

    yyjson_write_flag flags = YYJSON_WRITE_ESCAPE_UNICODE;
    if (opts->format == INSPECT_FORMAT_JSON_PRETTY) {
        flags |= YYJSON_WRITE_PRETTY;
    }

    yyjson_write_err err;
    size_t json_length = 0;
    char *json_text = yyjson_mut_write_opts(doc, flags, NULL, &json_length, &err);
    if (!json_text) {
        nmo_inspect_log(opts, LOG_ERROR, "Failed to serialize JSON: %s", err.msg ? err.msg : "unknown error");
        yyjson_mut_doc_free(doc);
        return;
    }

    fputs(json_text, out);
    fputc('\n', out);

    free(json_text);
    yyjson_mut_doc_free(doc);
}
