#include "test_framework.h"

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

TEST(no_legacy_runtime_api_exports, runtime_graph_uses_current_ref_graph_names) {
    assert_file_has_no_substring("src/session/runtime_graph.c", "legacy");
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

TEST(no_legacy_runtime_api_exports, material_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_material.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, data_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_data.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, scene_list_uses_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_scene.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, mesh_commands_use_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_mesh.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, animation_commands_use_shared_object_query_runner) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_animation.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, resource_commands_use_core_object_lookup) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_resource.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, parameter_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_parameter.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, behavior_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_behavior.c", "nmo_session_get_objects");
}

TEST(no_legacy_runtime_api_exports, object_commands_use_object_query_api) {
    assert_file_has_no_substring("tools/commands/nmo_cmd_object.c", "nmo_session_get_objects");
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

TEST_MAIN_BEGIN()
REGISTER_TEST(no_legacy_runtime_api_exports, builtin_headers_have_no_legacy_runtime_api_exports);
REGISTER_TEST(no_legacy_runtime_api_exports, migrated_state_sources_do_not_use_data_pointer_state);
REGISTER_TEST(no_legacy_runtime_api_exports, runtime_graph_uses_current_ref_graph_names);
REGISTER_TEST(no_legacy_runtime_api_exports, type_guid_compat_aliases_are_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, session_id_remap_compat_layer_is_removed);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_uses_shared_library_object_query_bridge);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_object_query_bridge_is_shared);
REGISTER_TEST(no_legacy_runtime_api_exports, entity_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, material_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, data_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, scene_list_uses_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, mesh_commands_use_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, animation_commands_use_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, resource_commands_use_core_object_lookup);
REGISTER_TEST(no_legacy_runtime_api_exports, parameter_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, behavior_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, object_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, remaining_cli_commands_use_object_query_api);
REGISTER_TEST(no_legacy_runtime_api_exports, cli_commands_do_not_bypass_shared_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, behavior_graph_commands_use_core_object_query_runner);
REGISTER_TEST(no_legacy_runtime_api_exports, remaining_tools_do_not_use_repository_get_all_scans);
REGISTER_TEST(no_legacy_runtime_api_exports, session_internals_do_not_use_repository_get_all_snapshots);
TEST_MAIN_END()
