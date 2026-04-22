#include "lua_bindings_internal.h"

#include "app/nmo_comparison.h"
#include "app/nmo_object_diff.h"
#include "app/nmo_report_result.h"
#include "app/nmo_report_view.h"

#include <string.h>

#include "lauxlib.h"

static void nmo_lua_app_push_guid_string(lua_State *state, nmo_guid_t guid)
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

static void nmo_lua_app_push_object_summary_stats(
    lua_State *state,
    const nmo_object_summary_stats_t *stats)
{
    lua_createtable(state, 0, 10);

    lua_pushinteger(state, (lua_Integer)stats->class_id);
    lua_setfield(state, -2, "class_id");

    if (stats->class_name != NULL) {
        lua_pushstring(state, stats->class_name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "class_name");

    nmo_lua_app_push_guid_string(state, stats->type_guid);
    lua_setfield(state, -2, "type_guid");

    if (stats->type_name != NULL) {
        lua_pushstring(state, stats->type_name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "type_name");

    lua_pushboolean(state, stats->has_reflection ? 1 : 0);
    lua_setfield(state, -2, "has_reflection");

    lua_pushinteger(state, (lua_Integer)stats->total_fields);
    lua_setfield(state, -2, "total_fields");
    lua_pushinteger(state, (lua_Integer)stats->array_fields);
    lua_setfield(state, -2, "array_fields");
    lua_pushinteger(state, (lua_Integer)stats->reference_fields);
    lua_setfield(state, -2, "reference_fields");
    lua_pushinteger(state, (lua_Integer)stats->optional_fields);
    lua_setfield(state, -2, "optional_fields");
    lua_pushinteger(state, (lua_Integer)stats->object_ref_fields);
    lua_setfield(state, -2, "object_ref_fields");
}

static void nmo_lua_app_push_comparison_stats(
    lua_State *state,
    const nmo_comparison_result_stats_t *stats)
{
    lua_createtable(state, 0, 22);

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
    lua_pushinteger(state, (lua_Integer)stats->object_count_diffs);
    lua_setfield(state, -2, "object_count_diffs");
    lua_pushinteger(state, (lua_Integer)stats->manager_count_diffs);
    lua_setfield(state, -2, "manager_count_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_missing_diffs);
    lua_setfield(state, -2, "object_missing_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_order_diffs);
    lua_setfield(state, -2, "object_order_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_id_diffs);
    lua_setfield(state, -2, "object_id_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_name_diffs);
    lua_setfield(state, -2, "object_name_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_class_id_diffs);
    lua_setfield(state, -2, "object_class_id_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_reference_flag_diffs);
    lua_setfield(state, -2, "object_reference_flag_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_chunk_size_diffs);
    lua_setfield(state, -2, "object_chunk_size_diffs");
    lua_pushinteger(state, (lua_Integer)stats->object_chunk_data_diffs);
    lua_setfield(state, -2, "object_chunk_data_diffs");
    lua_pushinteger(state, (lua_Integer)stats->manager_missing_diffs);
    lua_setfield(state, -2, "manager_missing_diffs");
    lua_pushinteger(state, (lua_Integer)stats->manager_guid_diffs);
    lua_setfield(state, -2, "manager_guid_diffs");
    lua_pushinteger(state, (lua_Integer)stats->manager_chunk_size_diffs);
    lua_setfield(state, -2, "manager_chunk_size_diffs");
    lua_pushinteger(state, (lua_Integer)stats->manager_chunk_data_diffs);
    lua_setfield(state, -2, "manager_chunk_data_diffs");
    lua_pushinteger(state, (lua_Integer)stats->file_version_diffs);
    lua_setfield(state, -2, "file_version_diffs");
    lua_pushinteger(state, (lua_Integer)stats->ck_version_diffs);
    lua_setfield(state, -2, "ck_version_diffs");
    lua_pushinteger(state, (lua_Integer)stats->shadow_data_diffs);
    lua_setfield(state, -2, "shadow_data_diffs");
}

static void nmo_lua_app_push_diff_stats(
    lua_State *state,
    const nmo_diff_result_stats_t *stats)
{
    lua_createtable(state, 0, 9);

    lua_pushinteger(state, (lua_Integer)stats->changed_count);
    lua_setfield(state, -2, "changed_count");
    lua_pushinteger(state, (lua_Integer)stats->renamed_count);
    lua_setfield(state, -2, "renamed_count");
    lua_pushinteger(state, (lua_Integer)stats->removed_count);
    lua_setfield(state, -2, "removed_count");
    lua_pushinteger(state, (lua_Integer)stats->added_count);
    lua_setfield(state, -2, "added_count");
    lua_pushinteger(state, (lua_Integer)stats->identical_count);
    lua_setfield(state, -2, "identical_count");
    lua_pushinteger(state, (lua_Integer)stats->total_objects1);
    lua_setfield(state, -2, "total_objects1");
    lua_pushinteger(state, (lua_Integer)stats->total_objects2);
    lua_setfield(state, -2, "total_objects2");
    lua_pushinteger(state, (lua_Integer)stats->reported_field_diffs);
    lua_setfield(state, -2, "reported_field_diffs");
    lua_pushinteger(state, (lua_Integer)stats->total_field_diffs);
    lua_setfield(state, -2, "total_field_diffs");
}

static void nmo_lua_app_push_object_summary_view(
    lua_State *state,
    const nmo_object_summary_view_t *view)
{
    size_t i = 0u;

    nmo_lua_app_push_object_summary_stats(state, &view->stats);
    lua_createtable(state, (int)view->field_count, 0);
    for (i = 0u; i < view->field_count; ++i) {
        size_t item_index = 0u;
        lua_createtable(state, 0, 6);

        if (view->fields[i].name != NULL) {
            lua_pushstring(state, view->fields[i].name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "name");

        if (view->fields[i].kind != NULL) {
            lua_pushstring(state, view->fields[i].kind);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "kind");

        if (view->fields[i].value_str != NULL) {
            lua_pushstring(state, view->fields[i].value_str);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "value_str");

        if (view->fields[i].ref_name != NULL) {
            lua_pushstring(state, view->fields[i].ref_name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "ref_name");

        if (view->fields[i].has_count) {
            lua_pushinteger(state, (lua_Integer)view->fields[i].count);
            lua_setfield(state, -2, "count");
        }

        lua_createtable(state, (int)view->fields[i].item_count, 0);
        for (item_index = 0u; item_index < view->fields[i].item_count; ++item_index) {
            if (view->fields[i].items[item_index] != NULL) {
                lua_pushstring(state, view->fields[i].items[item_index]);
            } else {
                lua_pushnil(state);
            }
            lua_rawseti(state, -2, (lua_Integer)item_index + 1);
        }
        lua_setfield(state, -2, "items");

        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "fields");
}

static void nmo_lua_app_push_comparison_view(
    lua_State *state,
    const nmo_comparison_view_t *view)
{
    size_t i = 0u;

    nmo_lua_app_push_comparison_stats(state, &view->stats);
    lua_createtable(state, (int)view->diff_count, 0);
    for (i = 0u; i < view->diff_count; ++i) {
        lua_createtable(state, 0, 4);
        lua_pushinteger(state, (lua_Integer)view->diffs[i].type_code);
        lua_setfield(state, -2, "type_code");
        if (view->diffs[i].type_name != NULL) {
            lua_pushstring(state, view->diffs[i].type_name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "type");
        lua_pushinteger(state, (lua_Integer)view->diffs[i].object_id);
        lua_setfield(state, -2, "object_id");
        if (view->diffs[i].context != NULL) {
            lua_pushstring(state, view->diffs[i].context);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "context");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
    lua_setfield(state, -2, "diffs");
}

static void nmo_lua_app_push_diff_identity_array(
    lua_State *state,
    const nmo_diff_identity_view_t *entries,
    size_t count)
{
    size_t i = 0u;

    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        lua_createtable(state, 0, 4);
        lua_pushinteger(state, (lua_Integer)entries[i].id);
        lua_setfield(state, -2, "id");
        lua_pushinteger(state, (lua_Integer)entries[i].class_id);
        lua_setfield(state, -2, "class_id");
        if (entries[i].name != NULL) {
            lua_pushstring(state, entries[i].name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "name");
        if (entries[i].path != NULL) {
            lua_pushstring(state, entries[i].path);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "path");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_app_push_diff_object_array(
    lua_State *state,
    const nmo_diff_object_view_t *entries,
    size_t count)
{
    size_t i = 0u;

    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        size_t field_index = 0u;

        lua_createtable(state, 0, 10);
        lua_pushinteger(state, (lua_Integer)entries[i].before_id);
        lua_setfield(state, -2, "before_id");
        lua_pushinteger(state, (lua_Integer)entries[i].after_id);
        lua_setfield(state, -2, "after_id");
        lua_pushinteger(state, (lua_Integer)entries[i].before_class_id);
        lua_setfield(state, -2, "before_class_id");
        lua_pushinteger(state, (lua_Integer)entries[i].after_class_id);
        lua_setfield(state, -2, "after_class_id");
        if (entries[i].before_name != NULL) {
            lua_pushstring(state, entries[i].before_name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "before_name");
        if (entries[i].after_name != NULL) {
            lua_pushstring(state, entries[i].after_name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "after_name");
        if (entries[i].before_path != NULL) {
            lua_pushstring(state, entries[i].before_path);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "before_path");
        if (entries[i].after_path != NULL) {
            lua_pushstring(state, entries[i].after_path);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "after_path");
        lua_pushnumber(state, (lua_Number)entries[i].similarity);
        lua_setfield(state, -2, "similarity");
        lua_pushinteger(state, (lua_Integer)entries[i].field_diff_total);
        lua_setfield(state, -2, "field_diff_total");

        lua_createtable(state, (int)entries[i].field_diff_count, 0);
        for (field_index = 0u; field_index < entries[i].field_diff_count; ++field_index) {
            lua_createtable(state, 0, 3);
            if (entries[i].field_diffs[field_index].field_name != NULL) {
                lua_pushstring(state, entries[i].field_diffs[field_index].field_name);
            } else {
                lua_pushnil(state);
            }
            lua_setfield(state, -2, "field_name");
            if (entries[i].field_diffs[field_index].before != NULL) {
                lua_pushstring(state, entries[i].field_diffs[field_index].before);
            } else {
                lua_pushnil(state);
            }
            lua_setfield(state, -2, "before");
            if (entries[i].field_diffs[field_index].after != NULL) {
                lua_pushstring(state, entries[i].field_diffs[field_index].after);
            } else {
                lua_pushnil(state);
            }
            lua_setfield(state, -2, "after");
            lua_rawseti(state, -2, (lua_Integer)field_index + 1);
        }
        lua_setfield(state, -2, "field_diffs");

        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_app_push_diff_view(
    lua_State *state,
    const nmo_diff_view_t *view)
{
    nmo_lua_app_push_diff_stats(state, &view->stats);
    nmo_lua_app_push_diff_object_array(state, view->changed, view->changed_count);
    lua_setfield(state, -2, "changed");
    nmo_lua_app_push_diff_object_array(state, view->renamed, view->renamed_count);
    lua_setfield(state, -2, "renamed");
    nmo_lua_app_push_diff_identity_array(state, view->removed, view->removed_count);
    lua_setfield(state, -2, "removed");
    nmo_lua_app_push_diff_identity_array(state, view->added, view->added_count);
    lua_setfield(state, -2, "added");
}

static int nmo_lua_app_object_summary_stats(lua_State *state)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    nmo_context_t *context = nmo_session_get_context(handle->session);
    if (context == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Lua object handle session has no context");
    }

    nmo_object_summary_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    status = nmo_object_summary_collect_stats(context, object, &stats);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to collect object summary stats");
    }

    nmo_lua_app_push_object_summary_stats(state, &stats);
    return 1;
}

static int nmo_lua_app_comparison_stats(lua_State *state)
{
    nmo_session_t *session1 = NULL;
    nmo_session_t *session2 = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session1, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid first session handle");
    }

    status = nmo_lua_check_session_handle(state, 2, &session2, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid second session handle");
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_stats_t stats;
    nmo_comparison_result_init(&result);
    memset(&stats, 0, sizeof(stats));

    nmo_compare_flags_t flags =
        (nmo_compare_flags_t)luaL_optinteger(state, 3, NMO_COMPARE_DEFAULT);
    status = nmo_session_compare(session1, session2, flags, &result);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Session comparison failed");
    }

    status = nmo_comparison_result_collect_stats(&result, &stats);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to collect comparison stats");
    }

    nmo_lua_app_push_comparison_stats(state, &stats);
    return 1;
}

static int nmo_lua_app_object_summary(lua_State *state)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_context_t *context = NULL;
    nmo_object_summary_view_t view;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, 1, &handle, &object);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    context = nmo_session_get_context(handle->session);
    if (context == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Lua object handle session has no context");
    }

    memset(&view, 0, sizeof(view));
    status = nmo_object_summary_build_view(context, handle->session, object, &view);
    if (status != NMO_OK) {
        nmo_object_summary_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Failed to build object summary");
    }

    nmo_lua_app_push_object_summary_view(state, &view);
    nmo_object_summary_view_destroy(&view);
    return 1;
}

static int nmo_lua_app_comparison(lua_State *state)
{
    nmo_session_t *session1 = NULL;
    nmo_session_t *session2 = NULL;
    nmo_comparison_view_t view;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session1, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid first session handle");
    }

    status = nmo_lua_check_session_handle(state, 2, &session2, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid second session handle");
    }

    memset(&view, 0, sizeof(view));
    status = nmo_comparison_build_view(
        session1,
        session2,
        (uint32_t)luaL_optinteger(state, 3, NMO_COMPARE_DEFAULT),
        &view);
    if (status != NMO_OK) {
        nmo_comparison_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Session comparison failed");
    }

    nmo_lua_app_push_comparison_view(state, &view);
    nmo_comparison_view_destroy(&view);
    return 1;
}

static int nmo_lua_app_diff_stats(lua_State *state)
{
    nmo_session_t *session1 = NULL;
    nmo_session_t *session2 = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session1, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid first session handle");
    }

    status = nmo_lua_check_session_handle(state, 2, &session2, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid second session handle");
    }

    nmo_context_t *context1 = nmo_session_get_context(session1);
    nmo_context_t *context2 = nmo_session_get_context(session2);
    if (context1 == NULL || context2 == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Lua session handle has no context");
    }

    nmo_diff_result_t result;
    nmo_diff_result_stats_t stats;
    memset(&result, 0, sizeof(result));
    memset(&stats, 0, sizeof(stats));

    status = nmo_diff_objects(context1, session1, context2, session2, NULL, &result);
    if (status != NMO_OK) {
        nmo_diff_result_destroy(&result);
        return nmo_lua_raise_last_error(state, status, "Object diff failed");
    }

    status = nmo_diff_result_collect_stats(&result, &stats);
    nmo_diff_result_destroy(&result);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to collect diff stats");
    }

    nmo_lua_app_push_diff_stats(state, &stats);
    return 1;
}

static int nmo_lua_app_diff(lua_State *state)
{
    nmo_session_t *session1 = NULL;
    nmo_session_t *session2 = NULL;
    nmo_diff_view_t view;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session1, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid first session handle");
    }

    status = nmo_lua_check_session_handle(state, 2, &session2, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid second session handle");
    }

    memset(&view, 0, sizeof(view));
    status = nmo_diff_build_view(session1, session2, &view);
    if (status != NMO_OK) {
        nmo_diff_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Object diff failed");
    }

    nmo_lua_app_push_diff_view(state, &view);
    nmo_diff_view_destroy(&view);
    return 1;
}

static int nmo_lua_open_app_module(lua_State *state)
{
    lua_createtable(state, 0, 6);

    lua_pushcfunction(state, nmo_lua_app_object_summary_stats);
    lua_setfield(state, -2, "object_summary_stats");
    lua_pushcfunction(state, nmo_lua_app_object_summary);
    lua_setfield(state, -2, "object_summary");

    lua_pushcfunction(state, nmo_lua_app_comparison_stats);
    lua_setfield(state, -2, "comparison_stats");
    lua_pushcfunction(state, nmo_lua_app_comparison);
    lua_setfield(state, -2, "comparison");

    lua_pushcfunction(state, nmo_lua_app_diff_stats);
    lua_setfield(state, -2, "diff_stats");
    lua_pushcfunction(state, nmo_lua_app_diff);
    lua_setfield(state, -2, "diff");

    return 1;
}

nmo_status_t nmo_lua_register_app_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.app",
        .open_fn = nmo_lua_open_app_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
