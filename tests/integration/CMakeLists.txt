if(UNIX)
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

# Integration test macro
macro(add_integration_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Integration tests
# Phase 2: File I/O round-trip test (NEW - verifies complete load/save pipeline)
add_integration_test(test_file_roundtrip)

# Phase 2.1: MMAP load strategy integration test
add_integration_test(test_mmap_load)

# Existing tests
add_integration_test(test_data_roundtrip)
add_integration_test(test_file_io)
add_integration_test(test_chunk_special_cases)

# Phase 3A: Reference file compatibility test (ALL real Virtools files)
add_integration_test(test_all_reference_files)

# Phase 5+: Real file roundtrip with schema-based serialization
add_integration_test(test_real_file_roundtrip)

# Phase 4: CKBehavior historical regression fixtures (Nop1/Nop2)
add_integration_test(test_behavior_roundtrip_regression)

# Phase 3: Declarative registration system integration test (OLD SCHEMA - DISABLED)
# This test uses the old schema system which has been removed
# add_integration_test(test_macro_integration)

# Phase 5.6: Schema v2 plugin and metadata integration tests
add_integration_test(test_cascade_deletion)
add_integration_test(test_specialized_metadata)
add_integration_test(test_type_invalidation)

# Phase 6.7: Complete Type System Integration Test
add_integration_test(test_type_system_complete)

# Phase 3: Object type coverage verification
add_integration_test(test_object_type_coverage)
add_integration_test(test_plugin_dependency_corpus)

# Object layer acceptance test - comprehensive round-trip validation
add_integration_test(test_object_layer_acceptance)
add_integration_test(test_project_skeleton_acceptance)
add_integration_test(test_generated_scene_roundtrip)
add_integration_test(test_generated_visible_scene)
add_integration_test(test_generated_script_lifecycle)
add_integration_test(test_generated_project_rollback)
set_tests_properties(test_generated_script_lifecycle PROPERTIES
    ENVIRONMENT "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
)

# Runtime clean-break integration tests
add_integration_test(test_runtime_load_pipeline)
add_integration_test(test_runtime_copy_behavior_graph)
add_integration_test(test_runtime_delete_scene_group_consistency)
add_integration_test(test_extension_abi_v3)
add_integration_test(test_reference_alignment_matrix)
target_compile_definitions(test_reference_alignment_matrix PRIVATE
    NMO_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_integration_test(test_manager_lane_runtime_events)

# CLI tests
if(TARGET nmo_cli)
    add_executable(test_cli_behavior_graph test_cli_behavior_graph.c write_semantic_probe.c)
    target_link_libraries(test_cli_behavior_graph PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_behavior_graph nmo_cli)
    target_compile_definitions(test_cli_behavior_graph PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_behavior_graph COMMAND test_cli_behavior_graph)

    add_executable(test_cli_behavior_rewrite test_cli_behavior_rewrite.c)
    target_link_libraries(test_cli_behavior_rewrite PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_behavior_rewrite nmo_cli)
    target_compile_definitions(test_cli_behavior_rewrite PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_behavior_rewrite COMMAND test_cli_behavior_rewrite)

    add_executable(test_cli_script_edit test_cli_script_edit.c)
    target_link_libraries(test_cli_script_edit PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_script_edit nmo_cli)
    target_compile_definitions(test_cli_script_edit PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_script_edit COMMAND test_cli_script_edit)

    add_executable(test_cli_script_run test_cli_script_run.c)
    target_link_libraries(test_cli_script_run PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_script_run nmo_cli)
    target_compile_definitions(test_cli_script_run PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_script_run COMMAND test_cli_script_run)

    add_executable(test_lua_public_workflow test_lua_public_workflow.c)
    target_link_libraries(test_lua_public_workflow PRIVATE nmo test_framework yyjson)
    add_dependencies(test_lua_public_workflow nmo_cli)
    target_compile_definitions(test_lua_public_workflow PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_lua_public_workflow COMMAND test_lua_public_workflow)

    add_executable(test_cli_patch_apply test_cli_patch_apply.c)
    target_link_libraries(test_cli_patch_apply PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_patch_apply nmo_cli)
    target_compile_definitions(test_cli_patch_apply PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_patch_apply COMMAND test_cli_patch_apply)

    add_executable(test_generated_project_manifest test_generated_project_manifest.c)
    target_link_libraries(test_generated_project_manifest PRIVATE nmo test_framework yyjson)
    add_dependencies(test_generated_project_manifest nmo_cli)
    target_compile_definitions(test_generated_project_manifest PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_generated_project_manifest COMMAND test_generated_project_manifest)

    add_executable(test_generated_project_acceptance test_generated_project_acceptance.c)
    target_link_libraries(test_generated_project_acceptance PRIVATE nmo test_framework)
    add_dependencies(test_generated_project_acceptance nmo_cli)
    target_compile_definitions(test_generated_project_acceptance PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_generated_project_acceptance COMMAND test_generated_project_acceptance)
    set_tests_properties(test_generated_project_acceptance PROPERTIES
        ENVIRONMENT "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
    )

    add_executable(test_generated_advanced_subsystem_probes test_generated_advanced_subsystem_probes.c)
    target_link_libraries(test_generated_advanced_subsystem_probes PRIVATE nmo test_framework)
    add_dependencies(test_generated_advanced_subsystem_probes nmo_cli)
    target_compile_definitions(test_generated_advanced_subsystem_probes PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_generated_advanced_subsystem_probes COMMAND test_generated_advanced_subsystem_probes)

    add_executable(test_ballance_script_edit_acceptance test_ballance_script_edit_acceptance.c)
    target_link_libraries(test_ballance_script_edit_acceptance PRIVATE nmo test_framework yyjson)
    add_dependencies(test_ballance_script_edit_acceptance nmo_cli)
    target_compile_definitions(test_ballance_script_edit_acceptance PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_ballance_script_edit_acceptance COMMAND test_ballance_script_edit_acceptance)

    add_executable(test_cli_commands test_cli_commands.c)
    target_link_libraries(test_cli_commands PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_commands nmo_cli)
    target_compile_definitions(test_cli_commands PRIVATE
        NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>"
        NMO_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
    add_test(NAME test_cli_commands COMMAND test_cli_commands)
    set_tests_properties(test_cli_commands PROPERTIES
        ENVIRONMENT "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
    )

    add_executable(test_cli_diff_objects test_cli_diff_objects.c)
    target_link_libraries(test_cli_diff_objects PRIVATE nmo test_framework yyjson)
    add_dependencies(test_cli_diff_objects nmo_cli)
    target_compile_definitions(test_cli_diff_objects PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_diff_objects COMMAND test_cli_diff_objects)

    add_executable(test_cli_write_commands test_cli_write_commands.c write_semantic_probe.c)
    target_link_libraries(test_cli_write_commands PRIVATE nmo test_framework)
    add_dependencies(test_cli_write_commands nmo_cli)
    target_compile_definitions(test_cli_write_commands PRIVATE NMO_CLI_PATH="$<TARGET_FILE:nmo_cli>")
    add_test(NAME test_cli_write_commands COMMAND test_cli_write_commands)
    set_tests_properties(test_cli_write_commands PROPERTIES
        ENVIRONMENT "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
    )

    set(REPL_COMMAND_MIRROR_SOURCES
        ${PROJECT_SOURCE_DIR}/tools/nmo_repl_commands.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_repl_session.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_repl_util.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_repl_input.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_command_registry.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cmd_ctx.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cmd_core.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_opt.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cli_common.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cli_json.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_edit_report_json.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cli_output.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cli_sort.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_cli_write.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_tool_owner.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_tool_common.c
        ${PROJECT_SOURCE_DIR}/tools/nmo_tool_session.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_file.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_chunk.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_object.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_object_refs.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_object_write.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_debug.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_type.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_validate.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_resource.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior_show.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior_graph.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior_rewrite.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior_search.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior_interface.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_behavior_link.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_patch.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_parameter.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_script.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_convert.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_diff.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_extension.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_texture.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_data.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_scene.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_entity.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_material.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_mesh.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_animation.c
        ${PROJECT_SOURCE_DIR}/tools/commands/nmo_cmd_completion.c
    )

    add_executable(test_repl_write_commands
        test_repl_write_commands.c
        write_semantic_probe.c
        ${REPL_COMMAND_MIRROR_SOURCES}
    )
    target_include_directories(test_repl_write_commands PRIVATE ${PROJECT_SOURCE_DIR}/tools)
    target_link_libraries(test_repl_write_commands PRIVATE nmo test_framework yyjson)
    add_test(NAME test_repl_write_commands COMMAND test_repl_write_commands)
    set_tests_properties(test_repl_write_commands PROPERTIES
        ENVIRONMENT "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
    )

    add_executable(test_repl_read_commands
        test_repl_read_commands.c
        ${REPL_COMMAND_MIRROR_SOURCES}
    )
    target_include_directories(test_repl_read_commands PRIVATE ${PROJECT_SOURCE_DIR}/tools)
    target_compile_definitions(test_repl_read_commands PRIVATE
        NMO_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
    target_link_libraries(test_repl_read_commands PRIVATE nmo test_framework yyjson)
    add_test(NAME test_repl_read_commands COMMAND test_repl_read_commands)
    set_tests_properties(test_repl_read_commands PROPERTIES
        ENVIRONMENT "NMO_DATA_DIR=${PROJECT_SOURCE_DIR}/data"
    )
endif()

# Temporarily disabled - test_phase6_integration uses deprecated API
# add_integration_test(test_phase6_integration)
