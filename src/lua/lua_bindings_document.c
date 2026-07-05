#include "lua_bindings_internal.h"

#include "document/nmo_document_compare.h"
#include "document/nmo_document_file_state.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "document/nmo_document_stats.h"
#include "lua/nmo_lua_runtime.h"
#include "lauxlib.h"

static void nmo_lua_document_push_guid_string(lua_State *state, nmo_guid_t guid)
{
    char guid_buffer[32];
    if (nmo_guid_is_null(guid)) {
        lua_pushnil(state);
        return;
    }

    if (nmo_guid_format(guid, guid_buffer, sizeof(guid_buffer)) <= 0) {
        lua_pushnil(state);
        return;
    }

    lua_pushstring(state, guid_buffer);
}

static void nmo_lua_document_push_stats(lua_State *state,
                                        const nmo_file_stats_t *stats)
{
    lua_createtable(state, 0, 5);

    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)stats->objects.total_count);
    lua_setfield(state, -2, "total_count");
    lua_pushinteger(state, (lua_Integer)stats->objects.max_class_id);
    lua_setfield(state, -2, "max_class_id");
    lua_pushinteger(state, (lua_Integer)stats->objects.unique_classes);
    lua_setfield(state, -2, "unique_classes");
    lua_setfield(state, -2, "objects");

    lua_createtable(state, 0, 6);
    lua_pushinteger(state, (lua_Integer)stats->memory.total_size);
    lua_setfield(state, -2, "total_size");
    lua_pushinteger(state, (lua_Integer)stats->memory.header_size);
    lua_setfield(state, -2, "header_size");
    lua_pushinteger(state, (lua_Integer)stats->memory.data_size);
    lua_setfield(state, -2, "data_size");
    lua_pushinteger(state, (lua_Integer)stats->memory.chunk_data_size);
    lua_setfield(state, -2, "chunk_data_size");
    lua_pushinteger(state, (lua_Integer)stats->memory.chunk_overhead);
    lua_setfield(state, -2, "chunk_overhead");
    lua_pushinteger(state, (lua_Integer)stats->memory.compression_ratio);
    lua_setfield(state, -2, "compression_ratio");
    lua_setfield(state, -2, "memory");

    lua_createtable(state, 0, 3);
    lua_pushnumber(state, stats->performance.load_time_ms);
    lua_setfield(state, -2, "load_time_ms");
    lua_pushnumber(state, stats->performance.parse_time_ms);
    lua_setfield(state, -2, "parse_time_ms");
    lua_pushnumber(state, stats->performance.remap_time_ms);
    lua_setfield(state, -2, "remap_time_ms");
    lua_setfield(state, -2, "performance");

    lua_createtable(state, 0, 3);
    lua_pushinteger(state, (lua_Integer)stats->references.total_references);
    lua_setfield(state, -2, "total_references");
    lua_pushinteger(state, (lua_Integer)stats->references.resolved);
    lua_setfield(state, -2, "resolved");
    lua_pushinteger(state, (lua_Integer)stats->references.unresolved);
    lua_setfield(state, -2, "unresolved");
    lua_setfield(state, -2, "references");

    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)stats->chunks.total_chunks);
    lua_setfield(state, -2, "total_chunks");
    lua_pushinteger(state, (lua_Integer)stats->chunks.compressed_chunks);
    lua_setfield(state, -2, "compressed_chunks");
    lua_pushinteger(state, (lua_Integer)stats->chunks.max_chunk_size);
    lua_setfield(state, -2, "max_chunk_size");
    lua_pushinteger(state, (lua_Integer)stats->chunks.avg_chunk_size);
    lua_setfield(state, -2, "avg_chunk_size");
    lua_setfield(state, -2, "chunks");
}

static void nmo_lua_document_push_compare_stats(
    lua_State *state,
    const nmo_comparison_result_stats_t *stats)
{
    lua_createtable(state, 0, 8);
    lua_pushboolean(state, stats->match ? 1 : 0);
    lua_setfield(state, -2, "match");
    lua_pushinteger(state, (lua_Integer)stats->objects_compared);
    lua_setfield(state, -2, "objects_compared");
    lua_pushinteger(state, (lua_Integer)stats->objects_matched);
    lua_setfield(state, -2, "objects_matched");
    lua_pushinteger(state, (lua_Integer)stats->managers_compared);
    lua_setfield(state, -2, "managers_compared");
    lua_pushinteger(state, (lua_Integer)stats->managers_matched);
    lua_setfield(state, -2, "managers_matched");
    lua_pushinteger(state, (lua_Integer)stats->diff_count);
    lua_setfield(state, -2, "diff_count");
    lua_pushboolean(state, stats->diff_overflow ? 1 : 0);
    lua_setfield(state, -2, "diff_overflow");
}

static void nmo_lua_document_push_runtime_load_stats(
    lua_State *state,
    const nmo_runtime_load_stats_t *stats)
{
    size_t i = 0u;
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)stats->total_objects);
    lua_setfield(state, -2, "total_objects");
    lua_pushinteger(state, (lua_Integer)stats->flags);
    lua_setfield(state, -2, "flags");

    lua_createtable(state, 0, 6);
    lua_pushinteger(state, (lua_Integer)stats->references.total);
    lua_setfield(state, -2, "total");
    lua_pushinteger(state, (lua_Integer)stats->references.resolved);
    lua_setfield(state, -2, "resolved");
    lua_pushinteger(state, (lua_Integer)stats->references.unresolved);
    lua_setfield(state, -2, "unresolved");
    lua_pushinteger(state, (lua_Integer)stats->references.ambiguous);
    lua_setfield(state, -2, "ambiguous");
    lua_pushinteger(state, (lua_Integer)stats->references.unresolved_preview_count);
    lua_setfield(state, -2, "unresolved_preview_count");
    lua_createtable(state, (int)stats->references.unresolved_preview_count, 0);
    for (i = 0u; i < stats->references.unresolved_preview_count &&
                 i < (sizeof(stats->references.unresolved_preview) /
                       sizeof(stats->references.unresolved_preview[0]));
         ++i) {
        lua_createtable(state, 0, 2);
        lua_pushinteger(state,
                        (lua_Integer)stats->references.unresolved_preview[i].id);
        lua_setfield(state, -2, "id");
        lua_pushinteger(
            state,
            (lua_Integer)stats->references.unresolved_preview[i].class_id);
        lua_setfield(state, -2, "class_id");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "unresolved_preview");
    lua_setfield(state, -2, "references");

    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)stats->indexes.class_entries);
    lua_setfield(state, -2, "class_entries");
    lua_pushinteger(state, (lua_Integer)stats->indexes.name_entries);
    lua_setfield(state, -2, "name_entries");
    lua_pushinteger(state, (lua_Integer)stats->indexes.guid_entries);
    lua_setfield(state, -2, "guid_entries");
    lua_pushinteger(state, (lua_Integer)stats->indexes.memory_usage);
    lua_setfield(state, -2, "memory_usage");
    lua_setfield(state, -2, "indexes");

    lua_createtable(state, 0, 2);
    lua_pushinteger(state, (lua_Integer)stats->object_postload.invoked);
    lua_setfield(state, -2, "invoked");
    lua_pushinteger(state, (lua_Integer)stats->object_postload.errors);
    lua_setfield(state, -2, "errors");
    lua_setfield(state, -2, "object_postload");

    lua_pushinteger(state, (lua_Integer)stats->manager_errors);
    lua_setfield(state, -2, "manager_errors");
}

static void nmo_lua_document_push_plugin_diagnostics(
    lua_State *state,
    const nmo_session_plugin_diagnostics_t *diag)
{
    size_t i = 0u;
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)diag->entry_count);
    lua_setfield(state, -2, "entry_count");
    lua_pushinteger(state, (lua_Integer)diag->missing_count);
    lua_setfield(state, -2, "missing_count");
    lua_pushinteger(state, (lua_Integer)diag->outdated_count);
    lua_setfield(state, -2, "outdated_count");
    lua_pushboolean(state, diag->extension_registry_available ? 1 : 0);
    lua_setfield(state, -2, "extension_registry_available");

    lua_createtable(state, (int)diag->entry_count, 0);
    for (i = 0u; i < diag->entry_count; ++i) {
        const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
        lua_createtable(state, 0, 7);
        nmo_lua_document_push_guid_string(state, entry->guid);
        lua_setfield(state, -2, "guid");
        lua_pushinteger(state, (lua_Integer)entry->category);
        lua_setfield(state, -2, "category");
        lua_pushinteger(state, (lua_Integer)entry->required_version);
        lua_setfield(state, -2, "required_version");
        lua_pushinteger(state, (lua_Integer)entry->resolved_version);
        lua_setfield(state, -2, "resolved_version");
        if (entry->resolved_name != NULL) {
            lua_pushstring(state, entry->resolved_name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "resolved_name");
        lua_pushinteger(state, (lua_Integer)entry->status_flags);
        lua_setfield(state, -2, "status_flags");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "entries");
}

static void nmo_lua_document_push_included_files(lua_State *state,
                                                 const nmo_included_file_t *files,
                                                 size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        const nmo_included_file_t *entry = &files[i];
        const nmo_object_id_t *owner_ids =
            (const nmo_object_id_t *)entry->owner_ids.data;
        size_t owner_count = entry->owner_ids.count;
        size_t owner_index = 0u;

        lua_createtable(state, 0, 5);
        if (entry->name != NULL) {
            lua_pushstring(state, entry->name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, (lua_Integer)entry->size);
        lua_setfield(state, -2, "size");
        lua_pushinteger(state, (lua_Integer)entry->attributes);
        lua_setfield(state, -2, "attributes");
        if (entry->data != NULL && entry->size > 0u) {
            lua_pushlstring(state, (const char *)entry->data, entry->size);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "data");

        lua_createtable(state, (int)owner_count, 0);
        for (owner_index = 0u; owner_index < owner_count; ++owner_index) {
            lua_pushinteger(state, (lua_Integer)owner_ids[owner_index]);
            lua_rawseti(state, -2, (lua_Integer)owner_index + 1);
        }
        lua_setfield(state, -2, "owner_ids");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static uint32_t nmo_lua_document_check_included_file_index(lua_State *state, int index)
{
    lua_Integer lua_index = luaL_checkinteger(state, index);
    if (lua_index < 1) {
        luaL_error(state, "included file index must be 1-based");
        return 0u;
    }
    if ((uint64_t)lua_index > (uint64_t)UINT32_MAX) {
        luaL_error(state, "included file index is too large");
        return 0u;
    }
    return (uint32_t)(lua_index - 1);
}

static int nmo_lua_document_load_file(lua_State *state)
{
    nmo_context_t *context = NULL;
    const char *path = NULL;
    nmo_document_t *document = NULL;
    nmo_status_t status =
        nmo_lua_check_context_handle(state, 1, &context, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    path = luaL_checkstring(state, 2);
    status = nmo_document_load_file(context, path, NULL, &document);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to load document");
    }

    status = nmo_lua_push_document_handle(state, document);
    if (status != NMO_OK) {
        nmo_document_destroy(document);
        return nmo_lua_raise_last_error(state, status, "Failed to push document handle");
    }
    return 1;
}

static int nmo_lua_document_save_file(lua_State *state)
{
    nmo_document_t *document = NULL;
    const char *path = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    path = luaL_checkstring(state, 2);
    status = nmo_document_save_file(document, path, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to save document");
    }
    return 0;
}

static int nmo_lua_document_stats(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_file_stats_t stats = {0};
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    status = nmo_document_stats_collect(document, &stats);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to collect document stats");
    }

    nmo_lua_document_push_stats(state, &stats);
    return 1;
}

static int nmo_lua_document_compare(lua_State *state)
{
    nmo_document_t *left = NULL;
    nmo_document_t *right = NULL;
    nmo_comparison_result_t result = {0};
    nmo_comparison_result_stats_t stats = {0};
    nmo_compare_flags_t flags = (nmo_compare_flags_t)luaL_optinteger(
        state, 3, NMO_COMPARE_DEFAULT);
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &left, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid left document handle");
    }
    status = nmo_lua_check_document_handle(state, 2, &right, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid right document handle");
    }

    nmo_comparison_result_init(&result);
    status = nmo_document_compare(left, right, flags, &result);
    if (status == NMO_OK) {
        status = nmo_comparison_result_collect_stats(&result, &stats);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to compare documents");
    }

    nmo_lua_document_push_compare_stats(state, &stats);
    return 1;
}

static int nmo_lua_document_file_info(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_file_info_t info = {0};
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    info = nmo_document_get_file_info(document);
    lua_createtable(state, 0, 9);
    lua_pushinteger(state, (lua_Integer)info.file_version);
    lua_setfield(state, -2, "file_version");
    lua_pushinteger(state, (lua_Integer)info.file_version2);
    lua_setfield(state, -2, "file_version2");
    lua_pushinteger(state, (lua_Integer)info.ck_version);
    lua_setfield(state, -2, "ck_version");
    lua_pushinteger(state, (lua_Integer)info.product_version);
    lua_setfield(state, -2, "product_version");
    lua_pushinteger(state, (lua_Integer)info.product_build);
    lua_setfield(state, -2, "product_build");
    lua_pushinteger(state, (lua_Integer)info.file_size);
    lua_setfield(state, -2, "file_size");
    lua_pushinteger(state, (lua_Integer)info.object_count);
    lua_setfield(state, -2, "object_count");
    lua_pushinteger(state, (lua_Integer)info.manager_count);
    lua_setfield(state, -2, "manager_count");
    lua_pushinteger(state, (lua_Integer)info.write_mode);
    lua_setfield(state, -2, "write_mode");
    return 1;
}

static int nmo_lua_document_is_partial_load(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    lua_pushboolean(state, nmo_document_is_partial_load(document) ? 1 : 0);
    return 1;
}

static int nmo_lua_document_has_materialized_load_state(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    lua_pushboolean(state,
                    nmo_document_has_materialized_load_state(document) ? 1 : 0);
    return 1;
}

static int nmo_lua_document_runtime_load_stats(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_runtime_load_stats_t stats = {0};
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    status = nmo_document_get_runtime_load_stats(document, &stats);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to get runtime load stats");
    }

    nmo_lua_document_push_runtime_load_stats(state, &stats);
    return 1;
}

static int nmo_lua_document_plugin_diagnostics(lua_State *state)
{
    nmo_document_t *document = NULL;
    const nmo_session_plugin_diagnostics_t *diag = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    diag = nmo_document_get_plugin_diagnostics(document);
    if (diag == NULL) {
        lua_pushnil(state);
        return 1;
    }

    nmo_lua_document_push_plugin_diagnostics(state, diag);
    return 1;
}

static int nmo_lua_document_included_files(lua_State *state)
{
    nmo_document_t *document = NULL;
    uint32_t count = 0u;
    nmo_included_file_t *files = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    files = nmo_document_get_included_files(document, &count);
    if (files == NULL || count == 0u) {
        lua_createtable(state, 0, 0);
        return 1;
    }

    nmo_lua_document_push_included_files(state, files, count);
    return 1;
}

static int nmo_lua_document_add_included_file(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    const char *name = NULL;
    size_t data_size = 0u;
    const char *data = NULL;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *owner_ids = NULL;
    size_t owner_count = 0u;
    nmo_included_file_metadata_t meta = {0};
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_document_internal_session(document);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Document is detached");
    }

    name = luaL_checkstring(state, 2);
    data = luaL_checklstring(state, 3, &data_size);
    if (data_size > UINT32_MAX) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_ARGUMENT,
                                        "Included file payload is too large");
    }

    if (!lua_isnoneornil(state, 4)) {
        int owner_index = lua_absindex(state, 4);
        if (!lua_istable(state, owner_index)) {
            return luaL_error(state, "included file owners must be a table");
        }

        owner_count = (size_t)lua_rawlen(state, owner_index);
        if (owner_count > 0u) {
            arena = nmo_arena_create(NULL, 0);
            if (arena == NULL) {
                return nmo_lua_raise_last_error(state,
                                                NMO_ERR_NOMEM,
                                                "Failed to allocate included file owner arena");
            }

            status = nmo_lua_collect_object_id_array(state,
                                                     4,
                                                     session,
                                                     arena,
                                                     &owner_ids,
                                                     &owner_count);
            if (status != NMO_OK) {
                nmo_arena_destroy(arena);
                return nmo_lua_raise_last_error(state,
                                                status,
                                                "Failed to collect included file owners");
            }
            meta.owner_ids = owner_ids;
            meta.owner_count = (uint32_t)owner_count;
        }
    }

    status = nmo_document_add_included_file_ex(document,
                                               name,
                                               data,
                                               (uint32_t)data_size,
                                               owner_count > 0u ? &meta : NULL);
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add included file");
    }

    return 0;
}

static int nmo_lua_document_replace_included_file(lua_State *state)
{
    nmo_document_t *document = NULL;
    uint32_t file_index = 0u;
    size_t data_size = 0u;
    const char *data = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    file_index = nmo_lua_document_check_included_file_index(state, 2);
    data = luaL_checklstring(state, 3, &data_size);
    if (data_size > UINT32_MAX) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_ARGUMENT,
                                        "Included file payload is too large");
    }

    status = nmo_document_replace_included_file(document,
                                                file_index,
                                                data,
                                                (uint32_t)data_size);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to replace included file");
    }

    return 0;
}

static int nmo_lua_document_set_included_file_owners(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    uint32_t file_index = 0u;
    nmo_arena_t *arena = NULL;
    nmo_object_id_t *owner_ids = NULL;
    size_t owner_count = 0u;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_document_internal_session(document);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_INVALID_STATE, "Document is detached");
    }

    file_index = nmo_lua_document_check_included_file_index(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        int owner_index = lua_absindex(state, 3);
        if (!lua_istable(state, owner_index)) {
            return luaL_error(state, "included file owners must be a table");
        }
        owner_count = (size_t)lua_rawlen(state, owner_index);
        if (owner_count > 0u) {
            arena = nmo_arena_create(NULL, 0);
            if (arena == NULL) {
                return nmo_lua_raise_last_error(state,
                                                NMO_ERR_NOMEM,
                                                "Failed to allocate included file owner arena");
            }

            status = nmo_lua_collect_object_id_array(state,
                                                     3,
                                                     session,
                                                     arena,
                                                     &owner_ids,
                                                     &owner_count);
            if (status != NMO_OK) {
                nmo_arena_destroy(arena);
                return nmo_lua_raise_last_error(state,
                                                status,
                                                "Failed to collect included file owners");
            }
        }
    }

    status = nmo_document_set_included_file_owners(document,
                                                   file_index,
                                                   owner_ids,
                                                   (uint32_t)owner_count);
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to set included file owners");
    }

    return 0;
}

static int nmo_lua_document_remove_included_file(lua_State *state)
{
    nmo_document_t *document = NULL;
    uint32_t file_index = 0u;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    file_index = nmo_lua_document_check_included_file_index(state, 2);
    status = nmo_document_remove_included_file(document, file_index);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to remove included file");
    }

    return 0;
}

static int nmo_lua_open_document_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "load_file", nmo_lua_document_load_file },
        { "save_file", nmo_lua_document_save_file },
        { "stats", nmo_lua_document_stats },
        { "compare", nmo_lua_document_compare },
        { "file_info", nmo_lua_document_file_info },
        { "is_partial_load", nmo_lua_document_is_partial_load },
        { "has_materialized_load_state", nmo_lua_document_has_materialized_load_state },
        { "runtime_load_stats", nmo_lua_document_runtime_load_stats },
        { "plugin_diagnostics", nmo_lua_document_plugin_diagnostics },
        { "included_files", nmo_lua_document_included_files },
        { "add_included_file", nmo_lua_document_add_included_file },
        { "replace_included_file", nmo_lua_document_replace_included_file },
        { "set_included_file_owners", nmo_lua_document_set_included_file_owners },
        { "remove_included_file", nmo_lua_document_remove_included_file },
    };
    static const nmo_lua_integer_entry_t compare_flags[] = {
        { "default", (lua_Integer)NMO_COMPARE_DEFAULT },
        { "structure", (lua_Integer)NMO_COMPARE_STRUCTURE },
        { "names", (lua_Integer)NMO_COMPARE_NAMES },
        { "strict", (lua_Integer)NMO_COMPARE_STRICT },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)(function_count + 1u));
    nmo_lua_set_functions(state, functions, function_count);

    nmo_lua_push_integer_table(
        state, compare_flags, sizeof(compare_flags) / sizeof(compare_flags[0]));
    lua_setfield(state, -2, "compare_flags");

    return 1;
}

nmo_status_t nmo_lua_register_document_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.document",
        .open_fn = nmo_lua_open_document_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
