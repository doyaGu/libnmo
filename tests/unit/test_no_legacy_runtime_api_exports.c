#include "test_framework.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *k_builtin_headers[] = {
    "include/object/builtin/nmo_object_schemas.h",
    "include/object/builtin/nmo_sceneobject_schemas.h",
    "include/object/builtin/nmo_beobject_schemas.h",
    "include/object/builtin/nmo_renderobject_schemas.h",
    "include/object/builtin/nmo_parameter_schemas.h",
    "include/object/builtin/nmo_parameterin_schemas.h",
    "include/object/builtin/nmo_parameterout_schemas.h",
    "include/object/builtin/nmo_parameterlocal_schemas.h",
    "include/object/builtin/nmo_parameteroperation_schemas.h",
    "include/object/builtin/nmo_behavior_schemas.h",
    "include/object/builtin/nmo_behaviorio_schemas.h",
    "include/object/builtin/nmo_behaviorlink_schemas.h",
    "include/object/builtin/nmo_scene_schemas.h",
    "include/object/builtin/nmo_level_schemas.h",
    "include/object/builtin/nmo_group_schemas.h",
    "include/object/builtin/nmo_dataarray_schemas.h",
    "include/object/builtin/nmo_place_schemas.h",
    "include/object/builtin/nmo_2dentity_schemas.h",
    "include/object/builtin/nmo_sprite_schemas.h",
    "include/object/builtin/nmo_spritetext_schemas.h",
    "include/object/builtin/nmo_3dentity_schemas.h",
    "include/object/builtin/nmo_3dobject_schemas.h",
    "include/object/builtin/nmo_camera_schemas.h",
    "include/object/builtin/nmo_targetcamera_schemas.h",
    "include/object/builtin/nmo_light_schemas.h",
    "include/object/builtin/nmo_targetlight_schemas.h",
    "include/object/builtin/nmo_character_schemas.h",
    "include/object/builtin/nmo_sprite3d_schemas.h",
    "include/object/builtin/nmo_curve_schemas.h",
    "include/object/builtin/nmo_material_schemas.h",
    "include/object/builtin/nmo_texture_schemas.h",
    "include/object/builtin/nmo_mesh_schemas.h",
    "include/object/builtin/nmo_patchmesh_schemas.h",
    "include/object/builtin/nmo_grid_schemas.h",
    "include/object/builtin/nmo_layer_schemas.h",
    "include/object/builtin/nmo_animation_schemas.h",
    "include/object/builtin/nmo_rendercontext_schemas.h",
    "include/object/builtin/nmo_kinematicchain_schemas.h",
    "include/object/builtin/nmo_synchro_schemas.h",
    "include/object/builtin/nmo_sound_schemas.h",
    "include/object/builtin/nmo_interfaceobjectmanager_schemas.h",
    "include/object/builtin/nmo_attributemanager_schemas.h",
    "include/object/builtin/nmo_messagemanager_schemas.h"
};

static const char *k_probe_prefixes[] = {
    "",
    "..\\",
    "..\\..\\",
    "..\\..\\..\\",
    "..\\..\\..\\..\\"
};

static const char *k_state_only_sources[] = {
    "include/format/nmo_object.h",
    "src/app/save.c",
    "src/format/object.c",
    "src/object/object_system.c",
    "src/session/object_system.c",
    "tools/commands/nmo_cmd_animation.c",
    "tools/commands/nmo_cmd_data.c",
    "tools/commands/nmo_cmd_entity.c",
    "tools/commands/nmo_cmd_material.c",
    "tools/commands/nmo_cmd_mesh.c",
    "tools/commands/nmo_cmd_scene.c",
};

static int line_has_legacy_api(const char *line) {
    return strstr(line, "NMO_API") != NULL && strstr(line, "finish_loading") != NULL;
}

static int line_has_legacy_data_access(const char *line) {
    return strstr(line, "nmo_object_get_data(") != NULL ||
           strstr(line, "nmo_object_set_data(") != NULL ||
           strstr(line, "obj->data") != NULL ||
           strstr(line, "void *data;") != NULL;
}

static void assert_file_has_no_legacy_api(const char *relative_path) {
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        ASSERT_FALSE(line_has_legacy_api(line));
    }
    fclose(fp);
}

static void assert_file_has_no_legacy_data_access(const char *relative_path) {
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        ASSERT_FALSE(line_has_legacy_data_access(line));
    }
    fclose(fp);
}

static void assert_file_has_no_substring(const char *relative_path, const char *needle) {
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        ASSERT_NULL(strstr(line, needle));
    }
    fclose(fp);
}

static bool line_has_public_int_declaration(const char *line) {
    return strstr(line, "NMO_API int") != NULL ||
           strstr(line, "typedef int (*nmo_") != NULL;
}

static bool line_matches_any_substring(
    const char *line,
    const char *const *allowed_substrings,
    size_t allowed_count)
{
    for (size_t i = 0; i < allowed_count; i++) {
        if (strstr(line, allowed_substrings[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static void assert_public_int_declarations_are_allowlisted(
    const char *relative_path,
    const char *const *allowed_substrings,
    size_t allowed_count)
{
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line_has_public_int_declaration(line)) {
            ASSERT_TRUE(line_matches_any_substring(line, allowed_substrings, allowed_count));
        }
    }
    fclose(fp);
}

static void assert_function_has_no_substring(
    const char *relative_path,
    const char *function_name,
    const char *needle)
{
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    bool in_function = false;
    bool saw_function = false;
    int brace_depth = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (!in_function && strstr(line, function_name) != NULL) {
            in_function = true;
            saw_function = true;
        }

        if (!in_function) {
            continue;
        }

        ASSERT_NULL(strstr(line, needle));
        for (const char *p = line; *p != '\0'; p++) {
            if (*p == '{') {
                brace_depth++;
            } else if (*p == '}') {
                brace_depth--;
                if (brace_depth == 0) {
                    fclose(fp);
                    ASSERT_TRUE(saw_function);
                    return;
                }
            }
        }
    }
    fclose(fp);
    ASSERT_TRUE(saw_function);
}

static void assert_function_has_substring(
    const char *relative_path,
    const char *function_name,
    const char *needle)
{
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    bool in_function = false;
    bool saw_function = false;
    bool saw_needle = false;
    int brace_depth = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (!in_function && strstr(line, function_name) != NULL) {
            in_function = true;
            saw_function = true;
        }

        if (!in_function) {
            continue;
        }

        if (strstr(line, needle) != NULL) {
            saw_needle = true;
        }
        for (const char *p = line; *p != '\0'; p++) {
            if (*p == '{') {
                brace_depth++;
            } else if (*p == '}') {
                brace_depth--;
                if (brace_depth == 0) {
                    fclose(fp);
                    ASSERT_TRUE(saw_function);
                    ASSERT_TRUE(saw_needle);
                    return;
                }
            }
        }
    }
    fclose(fp);
    ASSERT_TRUE(saw_function);
    ASSERT_TRUE(saw_needle);
}

static void assert_file_not_found(const char *relative_path) {
    char full_path[512];
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        FILE *fp = fopen(full_path, "rb");
        if (fp != NULL) {
            fclose(fp);
            ASSERT_TRUE(0);
        }
    }
}

TEST(no_legacy_runtime_api_exports, builtin_headers_have_no_legacy_runtime_api_exports) {
    for (size_t i = 0; i < sizeof(k_builtin_headers) / sizeof(k_builtin_headers[0]); i++) {
        assert_file_has_no_legacy_api(k_builtin_headers[i]);
    }

    assert_file_has_no_legacy_api("include/object/nmo_object_type_common.h");
}

TEST(no_legacy_runtime_api_exports, migrated_state_sources_do_not_use_data_pointer_state) {
    for (size_t i = 0; i < sizeof(k_state_only_sources) / sizeof(k_state_only_sources[0]); i++) {
        assert_file_has_no_legacy_data_access(k_state_only_sources[i]);
    }
}

TEST(no_legacy_runtime_api_exports, runtime_graph_compatibility_api_is_removed) {
    assert_file_not_found("include/session/nmo_runtime_graph.h");
    assert_file_not_found("src/session/runtime_graph.c");
    assert_file_not_found("tests/unit/test_runtime_graph.c");
    assert_file_has_no_substring("include/nmo.h", "session/nmo_runtime_graph.h");
    assert_file_has_no_substring("CMakeLists.txt", "src/session/runtime_graph.c");
    assert_file_has_no_substring("tests/unit/CMakeLists.txt", "test_runtime_graph");
}

TEST(no_legacy_runtime_api_exports, ref_graph_legacy_short_names_are_removed) {
    static const char *const legacy_ref_tokens[] = {
        "NMO_REF_UNKNOWN",
        "NMO_REF_HIERARCHY",
        "NMO_REF_MESH",
        "NMO_REF_MATERIAL",
        "NMO_REF_TEXTURE",
        "NMO_REF_OWNER",
        "NMO_REF_BEHAVIOR_LINK",
        "NMO_REF_PARAMETER",
        "NMO_REF_TARGET",
        "NMO_REF_GROUP_MEMBER",
        "NMO_REF_SCENE",
        "NMO_REF_ANIMATION",
        "NMO_REF_PLACE",
        "NMO_REF_SKIN_BONE",
        "NMO_REF_DATA_ARRAY",
        "NMO_REF_SCRIPT",
        "NMO_REF_MAX"
    };

    static const char *const checked_files[] = {
        "include/object/nmo_ref_graph.h",
        "src/object/nmo_ref_graph.c",
        "src/object/nmo_ref_enumerate.c",
        "tools/commands/nmo_cmd_validate.c"
    };

    for (size_t file_i = 0; file_i < sizeof(checked_files) / sizeof(checked_files[0]); file_i++) {
        for (size_t token_i = 0; token_i < sizeof(legacy_ref_tokens) / sizeof(legacy_ref_tokens[0]); token_i++) {
            assert_file_has_no_substring(checked_files[file_i], legacy_ref_tokens[token_i]);
        }
    }
}

TEST(no_legacy_runtime_api_exports, deprecated_public_helper_aliases_are_removed) {
    assert_file_has_no_substring("include/core/nmo_arena_array.h", "nmo_arena_array_dispose(");
    assert_file_has_no_substring("src/core/arena_array.c", "nmo_arena_array_dispose(");
    assert_file_has_no_substring("tests/unit/test_arena_array.c", "nmo_arena_array_dispose(");

    assert_file_has_no_substring("include/format/nmo_chunk_api.h", "nmo_chunk_pack(");
    assert_file_has_no_substring("include/format/nmo_chunk_api.h", "nmo_chunk_unpack(");
    assert_file_has_no_substring("src/format/chunk_compression.c", "nmo_chunk_pack(");
    assert_file_has_no_substring("src/format/chunk_compression.c", "nmo_chunk_unpack(");
    assert_file_has_no_substring("tests/unit/test_chunk_api.c", "nmo_chunk_pack(");
    assert_file_has_no_substring("tests/unit/test_chunk_api.c", "nmo_chunk_unpack(");
}

TEST(no_legacy_runtime_api_exports, type_guid_compat_aliases_are_removed) {
    assert_file_has_no_substring("include/type/nmo_type_guids.h", "nmo_type_guid_compat.h");
    assert_file_not_found("include/type/nmo_type_guid_compat.h");
}

TEST(no_legacy_runtime_api_exports, session_id_remap_compat_layer_is_removed) {
    assert_file_not_found("include/session/nmo_id_remap.h");
    assert_file_not_found("src/session/id_remap.c");
    assert_file_has_no_substring("src/app/save.c", "nmo_id_remap_table_t");
    assert_file_has_no_substring("src/app/save.c", "nmo_id_remap_lookup(");
    assert_file_has_no_substring("src/app/save.c", "nmo_id_remap_plan_");
    assert_file_has_no_substring("src/session/object_system.c", "nmo_id_remap_table_t");
    assert_file_has_no_substring("src/session/object_system.c", "nmo_id_remap_table_get_count");
    assert_file_has_no_substring("src/session/deserializer.c", "session/nmo_id_remap.h");
}

TEST(no_legacy_runtime_api_exports, partial_load_state_setter_is_not_public) {
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_set_partial_load");
}

TEST(no_legacy_runtime_api_exports, object_query_context_is_not_public_api) {
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_get_object_query_context");
}

TEST(no_legacy_runtime_api_exports, object_mutators_use_status_return_type) {
    assert_file_has_no_substring("include/format/nmo_object.h", "NMO_API int nmo_object_set_name");
    assert_file_has_no_substring("include/format/nmo_object.h", "NMO_API int nmo_object_add_child");
    assert_file_has_no_substring("include/format/nmo_object.h", "NMO_API int nmo_object_remove_child");
    assert_file_has_no_substring("include/format/nmo_object.h", "NMO_API int nmo_object_set_chunk");
    assert_file_has_no_substring("include/format/nmo_object.h", "NMO_API int nmo_object_set_file_index");
    assert_file_has_no_substring("include/format/nmo_object.h", "NMO_API int nmo_object_set_type_guid");
}

TEST(no_legacy_runtime_api_exports, object_repository_mutators_use_status_return_type) {
    assert_file_has_no_substring(
        "include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_add_mutation_observer");
    assert_file_has_no_substring("include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_add");
    assert_file_has_no_substring("include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_remove");
    assert_file_has_no_substring("include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_take");
    assert_file_has_no_substring("include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_rename");
    assert_file_has_no_substring(
        "include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_set_type_guid");
    assert_file_has_no_substring("include/object/nmo_object_repository.h", "NMO_API int nmo_object_repository_clear");
}

TEST(no_legacy_runtime_api_exports, manager_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/format/nmo_manager.h", "typedef int (*nmo_manager_on_event_fn)");
    assert_file_has_no_substring("include/format/nmo_manager.h", "NMO_API int nmo_manager_set_user_data");
    assert_file_has_no_substring("include/format/nmo_manager.h", "NMO_API int nmo_manager_set_on_event_hook");
    assert_file_has_no_substring("include/format/nmo_manager.h", "NMO_API int nmo_manager_invoke_event");
}

TEST(no_legacy_runtime_api_exports, app_load_save_apis_use_status_return_type) {
    assert_file_has_no_substring("include/app/nmo_load.h", "NMO_API int nmo_load_file");
    assert_file_has_no_substring("include/app/nmo_save.h", "NMO_API int nmo_save_file");
}

TEST(no_legacy_runtime_api_exports, io_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/io/nmo_io.h", "typedef int (*nmo_io_read_fn)");
    assert_file_has_no_substring("include/io/nmo_io.h", "typedef int (*nmo_io_write_fn)");
    assert_file_has_no_substring("include/io/nmo_io.h", "typedef int (*nmo_io_seek_fn)");
    assert_file_has_no_substring("include/io/nmo_io.h", "typedef int (*nmo_io_flush_fn)");
    assert_file_has_no_substring("include/io/nmo_io.h", "typedef int (*nmo_io_close_fn)");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_read");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_write");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_seek");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_flush");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_close");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_read_exact");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_read_u8");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_read_u16");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_read_u32");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_read_u64");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_write_u8");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_write_u16");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_write_u32");
    assert_file_has_no_substring("include/io/nmo_io.h", "NMO_API int nmo_io_write_u64");
}

TEST(no_legacy_runtime_api_exports, object_index_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/object/nmo_object_index.h", "NMO_API int nmo_object_index_build");
    assert_file_has_no_substring("include/object/nmo_object_index.h", "NMO_API int nmo_object_index_rebuild");
    assert_file_has_no_substring("include/object/nmo_object_index.h", "NMO_API int nmo_object_index_add_object");
    assert_file_has_no_substring("include/object/nmo_object_index.h", "NMO_API int nmo_object_index_remove_object");
    assert_file_has_no_substring("include/object/nmo_object_index.h", "NMO_API int nmo_object_index_clear");
    assert_file_has_no_substring("include/object/nmo_object_index.h", "NMO_API int nmo_object_index_get_stats");
}

TEST(no_legacy_runtime_api_exports, id_mapping_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/session/nmo_id_mapping.h", "NMO_API int nmo_id_mapping_register");
    assert_file_has_no_substring("include/session/nmo_id_mapping.h", "NMO_API int nmo_id_mapping_end");
    assert_file_has_no_substring("include/session/nmo_id_mapping.h", "NMO_API int nmo_id_mapping_get_runtime_id");
    assert_file_has_no_substring("include/session/nmo_id_mapping.h", "NMO_API int nmo_id_mapping_get_all");
    assert_file_has_no_substring("include/session/nmo_id_sanitizer.h", "NMO_API int nmo_id_sanitizer_register");
    assert_file_has_no_substring("include/session/nmo_id_sanitizer.h", "NMO_API int nmo_id_sanitizer_reseed");
}

TEST(no_legacy_runtime_api_exports, runtime_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/session/nmo_runtime_kernel.h", "int (*load_file)");
    assert_file_has_no_substring("include/session/nmo_runtime_kernel.h", "int (*save_file)");
    assert_file_has_no_substring("include/session/nmo_runtime_kernel.h", "int status;");
    assert_file_has_no_substring("include/session/nmo_runtime_kernel.h", "NMO_API int nmo_runtime_kernel_execute");
    assert_file_has_no_substring("include/session/nmo_runtime_kernel.h", "NMO_API int nmo_runtime_kernel_finalize_load");
    assert_file_has_no_substring("include/session/nmo_runtime_ref_remap.h", "NMO_API int nmo_runtime_remap_copy_refs");
    assert_file_has_no_substring("include/session/nmo_runtime_ref_remap.h", "NMO_API int nmo_runtime_remap_all_refs");
    assert_file_has_no_substring("include/session/nmo_runtime_delete.h", "NMO_API int nmo_runtime_execute_delete");
    assert_file_has_no_substring("include/session/nmo_runtime_delete.h", "NMO_API int nmo_runtime_preview_delete");
}

TEST(no_legacy_runtime_api_exports, session_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_ensure_behavior_acceleration");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_execute");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_load_file");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_save_file");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_create_object");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_copy_objects");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_destroy_objects");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_preview_destroy");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_set_file_info");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_set_plugin_dependencies");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_refresh_plugin_diagnostics");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_save(");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_save(");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_get_objects");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_rebuild_indexes");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_get_object_index_stats");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_add_included_file");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_add_included_file_ex");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_add_included_file_borrowed");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_add_included_file_borrowed_ex");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_set_included_file_owners");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_replace_included_file");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_remove_included_file");
    assert_file_has_no_substring("include/session/nmo_session.h", "NMO_API int nmo_session_get_runtime_load_stats");
}

TEST(no_legacy_runtime_api_exports, remaining_public_status_apis_use_status_return_type) {
    assert_file_has_no_substring("include/session/nmo_object_system.h", "typedef int (*nmo_id_register_fn)");
    assert_file_has_no_substring("include/session/nmo_object_system.h", "typedef int (*nmo_id_lookup_fn)");
    assert_file_has_no_substring("include/session/nmo_reference_resolver.h", "NMO_API int nmo_reference_resolver_register_strategy");
    assert_file_has_no_substring("include/session/nmo_reference_resolver.h", "NMO_API int nmo_reference_resolver_resolve_all");
    assert_file_has_no_substring("include/session/nmo_reference_resolver.h", "NMO_API int nmo_reference_resolver_get_stats");
    assert_file_has_no_substring("include/session/nmo_reference_resolver.h", "NMO_API int nmo_reference_resolver_get_unresolved");
    assert_file_has_no_substring("include/object/nmo_shadow_storage.h", "NMO_API int nmo_shadow_capture_included_files");
    assert_file_has_no_substring("include/object/nmo_shadow_storage.h", "NMO_API int nmo_shadow_capture_chunk_tail");

    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_push_context");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_pop_context");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_byte");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_word");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_dword");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_int");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_float");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_guid");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_bytes");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_string");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_buffer");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_buffer_nosize");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_buffer_nosize_lendian16");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_dword_as_words");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_array_dword_as_words");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_object_id");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_start_object_sequence");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_start_manager_sequence");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_manager_int");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_array_lendian");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_array_lendian16");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_buffer_lendian16");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_start_subchunk_sequence");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_subchunk");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_vector2");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_vector");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_vector4");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_matrix");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_quaternion");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_color");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_identifier");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_patch_u32");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_patch_u64");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_write_object_id_audited");
    assert_file_has_no_substring("include/format/nmo_chunk_writer.h", "NMO_API int nmo_chunk_writer_end_intlist");
}

TEST(no_legacy_runtime_api_exports, remaining_public_int_apis_are_non_status_allowlist) {
    static const char *const headers_with_public_ints[] = {
        "include/app/nmo_comparison.h",
        "include/behavior/nmo_bb_registry.h",
        "include/core/nmo_arena_array.h",
        "include/core/nmo_array.h",
        "include/core/nmo_bit_array.h",
        "include/core/nmo_error.h",
        "include/core/nmo_guid.h",
        "include/core/nmo_hash_common.h",
        "include/core/nmo_hash_set.h",
        "include/core/nmo_hash_table.h",
        "include/core/nmo_indexed_map.h",
        "include/core/nmo_string.h",
        "include/extension/nmo_extension_diagnostics.h",
        "include/format/nmo_chunk_api.h",
        "include/format/nmo_chunk_parser.h",
        "include/format/nmo_chunk_pool.h",
        "include/format/nmo_chunk_writer.h",
        "include/format/nmo_image.h",
        "include/format/nmo_manager_registry.h",
        "include/io/nmo_io.h",
        "include/io/nmo_io_file.h",
        "include/io/nmo_io_memory.h",
        "include/io/nmo_io_mmap.h",
        "include/object/nmo_object_index.h",
        "include/object/nmo_object_types.h",
        "include/session/nmo_builder.h",
        "include/session/nmo_context.h",
        "include/session/nmo_id_sanitizer.h",
        "include/session/nmo_reference_resolver.h",
        "include/session/nmo_session.h",
    };
    static const char *const non_status_int_declarations[] = {
        "NMO_API int nmo_extension_check_dependency",
        "NMO_API int64_t nmo_io_tell",
        "NMO_API int nmo_chunk_is_compressed",
        "NMO_API int nmo_chunk_parser_at_end",
        "NMO_API int nmo_builder_is_complete",
        "NMO_API int nmo_chunk_writer_depth",
        "NMO_API int nmo_chunk_writer_intlist_audit_active",
        "NMO_API int64_t nmo_io_file_seek",
        "NMO_API int64_t nmo_io_file_tell",
        "NMO_API int nmo_chunk_pool_validate",
        "NMO_API int nmo_reference_resolver_has_unresolved",
        "typedef int (*nmo_bb_registry_visitor_fn)",
        "NMO_API int nmo_context_get_refcount",
        "NMO_API int nmo_arena_array_is_empty",
        "NMO_API int nmo_arena_array_find",
        "NMO_API int nmo_arena_array_contains",
        "NMO_API int32_t nmo_image_calc_bytes_per_line",
        "NMO_API int nmo_array_is_empty",
        "NMO_API int nmo_array_find",
        "NMO_API int nmo_array_contains",
        "NMO_API int nmo_session_is_partial_load",
        "NMO_API int nmo_session_has_materialized_load_state",
        "NMO_API int nmo_bit_array_test",
        "NMO_API int64_t nmo_io_mmap_seek",
        "NMO_API int64_t nmo_io_mmap_tell",
        "NMO_API int nmo_io_mmap_supported",
        "NMO_API int32_t nmo_id_register_external",
        "NMO_API int32_t nmo_id_original_external",
        "NMO_API int nmo_manager_registry_contains",
        "NMO_API int64_t nmo_io_memory_seek",
        "NMO_API int64_t nmo_io_memory_tell",
        "NMO_API int nmo_last_error_line",
        "NMO_API int nmo_guid_equals",
        "NMO_API int nmo_guid_compare",
        "NMO_API int nmo_guid_is_null",
        "NMO_API int nmo_guid_format",
        "typedef int (*nmo_key_compare_func_t)",
        "typedef int (*nmo_hash_set_iterator_func_t)",
        "NMO_API int nmo_is_object_type",
        "NMO_API int nmo_object_index_has_class_index",
        "NMO_API int nmo_object_index_has_name_index",
        "NMO_API int nmo_object_index_has_guid_index",
        "typedef int (*nmo_hash_table_iterator_func_t)",
        "NMO_API int nmo_session_compare_file_info",
        "NMO_API int nmo_session_compare_objects",
        "typedef int (*nmo_map_compare_func_t)",
        "typedef int (*nmo_indexed_map_iterator_func_t)",
        "NMO_API int nmo_string_empty",
        "NMO_API int nmo_string_contains",
        "NMO_API int nmo_string_starts_with",
        "NMO_API int nmo_string_istarts_with",
        "NMO_API int nmo_string_ends_with",
        "NMO_API int nmo_string_iends_with",
        "NMO_API int nmo_string_compare",
        "NMO_API int nmo_string_compare_view",
        "NMO_API int nmo_string_icompare_view",
        "NMO_API int nmo_string_equals",
        "NMO_API int nmo_string_equals_view",
        "NMO_API int nmo_string_iequals_view",
        "NMO_API int nmo_string_slice_view",
        "NMO_API int nmo_string_to_int",
        "NMO_API int nmo_string_to_uint32",
        "NMO_API int nmo_string_to_float",
        "NMO_API int nmo_string_to_double",
    };

    for (size_t i = 0; i < sizeof(headers_with_public_ints) / sizeof(headers_with_public_ints[0]); i++) {
        assert_public_int_declarations_are_allowlisted(
            headers_with_public_ints[i],
            non_status_int_declarations,
            sizeof(non_status_int_declarations) / sizeof(non_status_int_declarations[0]));
    }
}

TEST(no_legacy_runtime_api_exports, cli_repl_parse_sites_use_shared_parse_helpers) {
    assert_file_has_no_substring("tools/nmo_repl_commands.c", "atoi(");
    assert_file_has_no_substring("tools/nmo_repl_repl.c", "atoi(");
    assert_file_has_no_substring("tools/nmo_opt.c", "strtol(");
    assert_file_has_no_substring("tools/nmo_opt.c", "strtof(");
    assert_file_has_no_substring("tools/nmo_tool_common.c", "strtoul(");
    assert_file_has_no_substring("tools/nmo_tool_common.c", "strtoull(");
    assert_file_has_no_substring("tools/commands/nmo_cmd_convert.c", "strtol(");
    assert_file_has_no_substring("tools/commands/nmo_cmd_entity.c", "strtod(");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object_refs.c", "strtoul(");
    assert_file_has_no_substring("tools/commands/nmo_cmd_animation.c", "strtoul(");
    assert_file_has_no_substring("tools/commands/nmo_cmd_animation.c", "sscanf(");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object_write.c", "sscanf(");
}

TEST(no_legacy_runtime_api_exports, session_edit_parse_sites_use_shared_parse_helpers) {
    assert_file_has_no_substring("src/session/session_edit.c", "strtol(");
    assert_file_has_no_substring("src/session/session_edit.c", "strtod(");
    assert_file_has_no_substring("src/session/session_edit.c", "strtoul(");
}

TEST(no_legacy_runtime_api_exports, type_value_parse_sites_use_shared_parse_helpers) {
    assert_file_has_no_substring("src/type/builtin_type_values.c", "strtof(");
    assert_file_has_no_substring("src/type/builtin_type_values.c", "strtod(");
    assert_file_has_no_substring("src/type/builtin_type_values.c", "strtol(");
    assert_file_has_no_substring("src/type/builtin_type_values.c", "strtoll(");
    assert_file_has_no_substring("src/type/builtin_type_values.c", "strtoul(");
    assert_file_has_no_substring("src/type/builtin_type_values.c", "strtoull(");
}

TEST(no_legacy_runtime_api_exports, core_string_parse_sites_use_shared_parse_helpers) {
    assert_file_has_no_substring("src/core/string.c", "strtoll(");
    assert_file_has_no_substring("src/core/string.c", "strtoull(");
    assert_file_has_no_substring("src/core/string.c", "strtod(");
}

TEST(no_legacy_runtime_api_exports, grammar_parse_sites_use_shared_parse_helpers) {
    assert_file_has_no_substring("src/format/obj_parser.c", "strtof(");
    assert_file_not_found("src/dsl/nmo_dsl_lex.c");
}

TEST(no_legacy_runtime_api_exports, legacy_session_query_api_is_removed) {
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_find_by_name");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_find_by_guid");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_find_object_by_guid");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_get_objects_by_class");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_count_objects_by_class");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_count_objects(");
    assert_file_has_no_substring("include/session/nmo_session.h", "nmo_session_find_object_by_name(");
}

TEST(no_legacy_runtime_api_exports, session_object_count_does_not_scan_via_query_runner) {
    const char *stale_count_comment =
        "Count all session objects "
        "using the query engine";
    assert_file_has_no_substring(
        "src/session/session_query.c",
        "nmo_session_query_objects(");
    assert_file_has_no_substring(
        "include/session/nmo_session_query.h",
        stale_count_comment);
}

TEST(no_legacy_runtime_api_exports, app_layer_does_not_include_session_internal_header) {
    assert_file_has_no_substring("src/app/load.c", "session_internal.h");
    assert_file_has_no_substring("src/app/load.c", "nmo_session_internal_has_materialized_load_state");
    assert_file_has_no_substring("src/app/session.c", "session_internal.h");
}

TEST(no_legacy_runtime_api_exports, cli_uses_shared_library_object_query_bridge) {
    assert_file_has_no_substring("tools/nmo_cmd_core.h", "nmo_core_iter_objects");
    assert_file_has_no_substring("tools/nmo_cmd_core.c", "nmo_core_iter_objects");
    assert_file_has_no_substring("tools/nmo_repl_commands.c", "nmo_core_iter_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_convert.c", "nmo_core_iter_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object.c", "nmo_core_iter_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object_write.c", "nmo_core_iter_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_texture.c", "nmo_core_iter_objects");
}

TEST(no_legacy_runtime_api_exports, cli_object_query_bridge_is_shared) {
    assert_file_has_no_substring("tools/nmo_repl_commands.c", "query_bridge");
    assert_file_has_no_substring("tools/commands/nmo_cmd_convert.c", "query_bridge");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object.c", "query_bridge");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object_write.c", "query_bridge");
    assert_file_has_no_substring("tools/commands/nmo_cmd_texture.c", "query_bridge");
}

TEST(no_legacy_runtime_api_exports, entity_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_entity.c", "nmo_session_get_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_entity.c", "nmo_core_query_matches_object");
}

TEST(no_legacy_runtime_api_exports, entity_set_parent_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_parent",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_parent",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, entity_set_camera_and_light_use_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_camera",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_camera",
        "nmo_save_file(");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_light",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_light",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, entity_set_position_uses_shared_write_runner_and_edit) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_position",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_position",
        "nmo_save_file(");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_entity.c",
        "nmo_cmd_entity_set_position",
        "nmo_3dentity_set_position(");
}

TEST(no_legacy_runtime_api_exports, material_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_material.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, material_set_uses_shared_write_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_material.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, data_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_data.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, data_set_cell_uses_shared_write_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_data.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, scene_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_scene.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, scene_set_uses_shared_write_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_scene.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, mesh_commands_use_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_mesh.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, mesh_import_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, mesh_import_uses_session_edit_transaction) {
    assert_function_has_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_session_edit_begin");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_session_edit_snapshot_bytes");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_session_edit_track_created_object");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_session_edit_snapshot_object_chunk");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_mesh.c",
        "nmo_cmd_mesh_import",
        "nmo_session_edit_commit");
}

TEST(no_legacy_runtime_api_exports, animation_commands_use_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_animation.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, animation_import_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_animation.c",
        "nmo_cmd_animation_import",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_animation.c",
        "nmo_cmd_animation_import",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, animation_import_uses_session_edit_transaction) {
    assert_function_has_substring(
        "tools/commands/nmo_cmd_animation.c",
        "nmo_cmd_animation_import",
        "nmo_session_edit_begin");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_animation.c",
        "nmo_cmd_animation_import",
        "nmo_session_edit_snapshot_bytes");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_animation.c",
        "nmo_cmd_animation_import",
        "nmo_session_edit_track_created_object");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_animation.c",
        "nmo_cmd_animation_import",
        "nmo_session_edit_commit");
}

TEST(no_legacy_runtime_api_exports, texture_replace_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_texture.c",
        "nmo_cmd_texture_replace",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_texture.c",
        "nmo_cmd_texture_replace",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, texture_replace_uses_session_edit_transaction) {
    assert_function_has_substring(
        "tools/commands/nmo_cmd_texture.c",
        "nmo_cmd_texture_replace",
        "nmo_session_edit_begin");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_texture.c",
        "nmo_cmd_texture_replace",
        "nmo_session_edit_snapshot_bytes");
    assert_function_has_substring(
        "tools/commands/nmo_cmd_texture.c",
        "nmo_cmd_texture_replace",
        "nmo_session_edit_commit");
}

TEST(no_legacy_runtime_api_exports, validate_commands_use_shared_save_helper) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_validate.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, convert_commands_use_shared_save_helper) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_convert.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, repl_save_uses_shared_save_helper) {
    assert_file_has_no_substring("tools/nmo_repl_commands.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, resource_commands_use_core_object_lookup) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_resource.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, resource_import_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_resource.c",
        "nmo_cmd_resource_import",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_resource.c",
        "nmo_cmd_resource_import",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, resource_replace_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_resource.c",
        "nmo_cmd_resource_replace",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_resource.c",
        "nmo_cmd_resource_replace",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, resource_remove_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_resource.c",
        "nmo_cmd_resource_remove",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_resource.c",
        "nmo_cmd_resource_remove",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, parameter_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_parameter.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, parameter_set_uses_shared_write_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_parameter.c", "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, behavior_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_behavior.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, object_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_object.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, object_create_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_create",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_create",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_rename_single_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "int nmo_cmd_object_rename(",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "int nmo_cmd_object_rename(",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_rename_batch_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_rename_batch",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_rename_batch",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_import_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_import",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_import",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_import_json_api_uses_session_edit_transaction) {
    assert_function_has_substring(
        "src/app/object_import.c",
        "nmo_object_import_json",
        "nmo_session_edit_begin");
    assert_function_has_substring(
        "src/app/object_import.c",
        "nmo_object_import_json",
        "nmo_session_edit_snapshot_bytes");
    assert_function_has_substring(
        "src/app/object_import.c",
        "nmo_object_import_json",
        "nmo_session_edit_track_created_object");
    assert_function_has_substring(
        "src/app/object_import.c",
        "nmo_object_import_json",
        "nmo_session_edit_commit");
}

TEST(no_legacy_runtime_api_exports, object_delete_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_delete",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_delete",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_copy_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_copy",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_copy",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_delete_batch_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "delete_batch_handler",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "delete_batch_handler",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, object_set_field_uses_shared_write_runner) {
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_set_field",
        "nmo_cmd_ctx_init_with_file");
    assert_function_has_no_substring(
        "tools/commands/nmo_cmd_object_write.c",
        "nmo_cmd_object_set_field",
        "nmo_save_file(");
}

TEST(no_legacy_runtime_api_exports, remaining_cli_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_object_refs.c", "nmo_session_get_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_chunk.c", "nmo_session_get_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_debug.c", "nmo_session_get_objects");
    assert_file_has_no_substring("tools/commands/nmo_cmd_validate.c", "nmo_session_get_objects");
    assert_file_has_no_substring("tools/nmo_repl_commands.c", "nmo_session_get_objects");
    assert_file_has_no_substring("tools/nmo_repl_util.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, cli_commands_do_not_bypass_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_behavior.c", "nmo_object_query_iterate");
    assert_file_has_no_substring("tools/commands/nmo_cmd_parameter.c", "nmo_object_query_iterate");
    assert_file_has_no_substring("tools/commands/nmo_cmd_validate.c", "nmo_object_query_iterate");
}

TEST(no_legacy_runtime_api_exports, tools_do_not_bypass_shared_query_matching) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_entity.c", "nmo_object_query_matches");
    assert_file_has_no_substring("tools/nmo_repl_util.c", "nmo_core_regex_match");
    assert_file_has_no_substring("tools/nmo_repl_input.c", "nmo_object_repository_get_count");
    assert_file_has_no_substring("tools/nmo_repl_input.c", "nmo_object_repository_get_by_index");
}

TEST(no_legacy_runtime_api_exports, behavior_graph_commands_use_core_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_behavior_graph.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/commands/nmo_cmd_behavior_search.c", "nmo_object_repository_get_all");
}

TEST(no_legacy_runtime_api_exports, remaining_tools_do_not_use_repository_get_all_scans) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_convert.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/commands/nmo_cmd_diff.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/commands/nmo_cmd_file.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/commands/nmo_cmd_object_write.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/nmo_repl_input.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/nmo_repl_util.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("tools/nmo_repl_commands.c", "nmo_repl_get_objects");
    assert_file_has_no_substring("tools/nmo_repl_util.c", "nmo_repl_get_objects");
    assert_file_has_no_substring("tools/nmo_repl_util.h", "nmo_repl_get_objects");
}

TEST(no_legacy_runtime_api_exports, session_internals_do_not_use_repository_get_all_snapshots) {
    assert_file_has_no_substring("src/session/id_mapping.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("src/session/runtime_ref_remap.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("src/session/runtime_kernel.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("src/session/deserializer.c", "nmo_object_repository_get_all");
}

TEST(no_legacy_runtime_api_exports, object_index_and_system_do_not_use_repository_get_all_snapshots) {
    assert_file_has_no_substring("src/object/object_index.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("src/object/object_system.c", "nmo_object_repository_get_all");
}

TEST(no_legacy_runtime_api_exports, ref_graph_and_behavior_schemas_do_not_use_repository_get_all_snapshots) {
    assert_file_has_no_substring("src/object/nmo_ref_graph.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("src/object/builtin/ckbehavior_schemas.c", "nmo_object_repository_get_all");
}

TEST(no_legacy_runtime_api_exports, app_stats_and_object_diff_do_not_use_repository_get_all_snapshots) {
    assert_file_has_no_substring("src/app/stats.c", "nmo_object_repository_get_all");
    assert_file_has_no_substring("src/app/object_diff.c", "nmo_object_repository_get_all");
}

TEST(no_legacy_runtime_api_exports, object_summary_uses_reflection_count_metadata) {
    assert_file_has_no_substring("src/app/object_summary.c", "nmo_summary_str_ends_with");
    assert_file_has_no_substring("src/app/object_summary.c", "nmo_summary_is_count_field_name");
}

TEST(no_legacy_runtime_api_exports, umbrella_and_tooling_use_reorganized_public_owners) {
    assert_file_has_no_substring("include/nmo.h", "app/nmo_comparison.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_inspector.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_load.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_object_diff.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_object_import.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_object_summary.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_report_result.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_report_view.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_save.h");
    assert_file_has_no_substring("include/nmo.h", "app/nmo_stats.h");
    assert_file_has_no_substring("include/nmo.h", "session/nmo_session_query.h");
    assert_file_has_no_substring("include/nmo.h", "object/nmo_ref_query.h");

    assert_file_has_no_substring("tools/nmo_cmd_core.c", "nmo_session_query_find_object_by_name");
    assert_file_has_no_substring("tools/nmo_cmd_core.c", "nmo_session_edit_begin");
}

TEST_MAIN_BEGIN()
REGISTER_TEST(no_legacy_runtime_api_exports, builtin_headers_have_no_legacy_runtime_api_exports);
REGISTER_TEST(no_legacy_runtime_api_exports, migrated_state_sources_do_not_use_data_pointer_state);
REGISTER_TEST(no_legacy_runtime_api_exports, runtime_graph_compatibility_api_is_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, ref_graph_legacy_short_names_are_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, deprecated_public_helper_aliases_are_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, type_guid_compat_aliases_are_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, session_id_remap_compat_layer_is_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, partial_load_state_setter_is_not_public);
REGISTER_TEST(no_legacy_runtime_api_exports, object_query_context_is_not_public_api);
REGISTER_TEST(no_legacy_runtime_api_exports, object_mutators_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, object_repository_mutators_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, manager_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, app_load_save_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, io_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, object_index_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, id_mapping_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, runtime_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, session_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, remaining_public_status_apis_use_status_return_type);
REGISTER_TEST(no_legacy_runtime_api_exports, remaining_public_int_apis_are_non_status_allowlist);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_repl_parse_sites_use_shared_parse_helpers);
REGISTER_TEST(no_legacy_runtime_api_exports, session_edit_parse_sites_use_shared_parse_helpers);
REGISTER_TEST(no_legacy_runtime_api_exports, type_value_parse_sites_use_shared_parse_helpers);
REGISTER_TEST(no_legacy_runtime_api_exports, core_string_parse_sites_use_shared_parse_helpers);
REGISTER_TEST(no_legacy_runtime_api_exports, grammar_parse_sites_use_shared_parse_helpers);
REGISTER_TEST(no_legacy_runtime_api_exports, legacy_session_query_api_is_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, session_object_count_does_not_scan_via_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, app_layer_does_not_include_session_internal_header);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_uses_shared_library_object_query_bridge);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_object_query_bridge_is_shared);
REGISTER_TEST(no_legacy_runtime_api_exports, entity_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, entity_set_parent_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, entity_set_camera_and_light_use_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, entity_set_position_uses_shared_write_runner_and_edit);
REGISTER_TEST(no_legacy_runtime_api_exports, material_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, material_set_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, data_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, data_set_cell_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, scene_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, scene_set_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, mesh_commands_use_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, mesh_import_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, mesh_import_uses_session_edit_transaction);
REGISTER_TEST(no_legacy_runtime_api_exports, animation_commands_use_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, animation_import_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, animation_import_uses_session_edit_transaction);
REGISTER_TEST(no_legacy_runtime_api_exports, texture_replace_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, texture_replace_uses_session_edit_transaction);
REGISTER_TEST(no_legacy_runtime_api_exports, validate_commands_use_shared_save_helper);
REGISTER_TEST(no_legacy_runtime_api_exports, convert_commands_use_shared_save_helper);
REGISTER_TEST(no_legacy_runtime_api_exports, repl_save_uses_shared_save_helper);
REGISTER_TEST(no_legacy_runtime_api_exports, resource_commands_use_core_object_lookup);
REGISTER_TEST(no_legacy_runtime_api_exports, resource_import_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, resource_replace_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, resource_remove_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, parameter_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, parameter_set_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, behavior_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, object_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, object_create_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_rename_single_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_rename_batch_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_import_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_import_json_api_uses_session_edit_transaction);
REGISTER_TEST(no_legacy_runtime_api_exports, object_delete_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_copy_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_delete_batch_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, object_set_field_uses_shared_write_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, remaining_cli_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_commands_do_not_bypass_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, tools_do_not_bypass_shared_query_matching);
REGISTER_TEST(no_legacy_runtime_api_exports, behavior_graph_commands_use_core_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, remaining_tools_do_not_use_repository_get_all_scans);
REGISTER_TEST(no_legacy_runtime_api_exports, session_internals_do_not_use_repository_get_all_snapshots);
REGISTER_TEST(no_legacy_runtime_api_exports, object_index_and_system_do_not_use_repository_get_all_snapshots);
REGISTER_TEST(no_legacy_runtime_api_exports, ref_graph_and_behavior_schemas_do_not_use_repository_get_all_snapshots);
REGISTER_TEST(no_legacy_runtime_api_exports, app_stats_and_object_diff_do_not_use_repository_get_all_snapshots);
REGISTER_TEST(no_legacy_runtime_api_exports, object_summary_uses_reflection_count_metadata);
REGISTER_TEST(no_legacy_runtime_api_exports, umbrella_and_tooling_use_reorganized_public_owners);
TEST_MAIN_END()
