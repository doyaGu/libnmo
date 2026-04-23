#include "lua_bindings_internal.h"

#include "document/nmo_document_compare.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "document/nmo_document_stats.h"
#include "lua/nmo_lua_runtime.h"
#include "session/nmo_session_bridge.h"

#include "lauxlib.h"

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
    status = nmo_document_load_file(context, path, &document);
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
    nmo_session_t *left_session = NULL;
    nmo_session_t *right_session = NULL;
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

    left_session = nmo_session_from_document(left);
    right_session = nmo_session_from_document(right);
    if (left_session == NULL || right_session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Document compare requires backing sessions");
    }

    nmo_comparison_result_init(&result);
    status = nmo_session_compare(left_session, right_session, flags, &result);
    if (status == NMO_OK) {
        status = nmo_comparison_result_collect_stats(&result, &stats);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to compare documents");
    }

    nmo_lua_document_push_compare_stats(state, &stats);
    return 1;
}

static int nmo_lua_open_document_module(lua_State *state)
{
    lua_createtable(state, 0, 5);

    lua_pushcfunction(state, nmo_lua_document_load_file);
    lua_setfield(state, -2, "load_file");

    lua_pushcfunction(state, nmo_lua_document_save_file);
    lua_setfield(state, -2, "save_file");

    lua_pushcfunction(state, nmo_lua_document_stats);
    lua_setfield(state, -2, "stats");

    lua_pushcfunction(state, nmo_lua_document_compare);
    lua_setfield(state, -2, "compare");

    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)NMO_COMPARE_DEFAULT);
    lua_setfield(state, -2, "default");
    lua_pushinteger(state, (lua_Integer)NMO_COMPARE_STRUCTURE);
    lua_setfield(state, -2, "structure");
    lua_pushinteger(state, (lua_Integer)NMO_COMPARE_NAMES);
    lua_setfield(state, -2, "names");
    lua_pushinteger(state, (lua_Integer)NMO_COMPARE_STRICT);
    lua_setfield(state, -2, "strict");
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
