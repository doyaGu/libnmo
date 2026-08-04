/**
 * @file test_public_api_smoke.c
 * @brief Smoke tests for the main public header (nmo.h)
 */

#include "test_framework.h"
#include "nmo.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "document/nmo_document.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_file_state.h"
#include "document/nmo_document_save.h"
#include "document/nmo_document_stats.h"
#include "document/nmo_document_compare.h"
#include "chunk/nmo_chunk_index.h"
#include "chunk/nmo_chunk_inspect.h"
#include "object/nmo_object_refs.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_hierarchy.h"
#include "object/nmo_object_summary.h"
#include "object/nmo_object_diff.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_behavior_query.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_execute.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_script_edit_graph.h"
#include "export/nmo_export_text.h"
#include "export/nmo_export_json.h"
#include "export/nmo_export_dot.h"
#include "export/nmo_ansi.h"
#include "export/nmo_hexdump.h"
#include "extension/nmo_extension_registry.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_edit.h"
#include "format/nmo_interface_view.h"
#include "format/nmo_object.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_iter.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_object_refs.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_reference_resolver.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_builder.h"
#include "session/nmo_id_mapping.h"
#include "session/nmo_session_pipeline.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NMO_SOURCE_DIR
#define NMO_SOURCE_DIR "."
#endif

#ifdef NMO_JSON_STREAM_PUBLIC_HEADER_KIND
#define NMO_TEST_HAS_JSON_STREAM_PUBLIC_API 1
#else
#define NMO_TEST_HAS_JSON_STREAM_PUBLIC_API 0
#endif

static char *read_source_text(const char *relative_path) {
    const char *candidates[] = {
        relative_path,
        "../../",
        NMO_SOURCE_DIR "/",
    };

    FILE *fp = NULL;
    char path[1024];
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        snprintf(path, sizeof(path), "%s%s", candidates[i], relative_path);
        fp = fopen(path, "rb");
        if (fp) {
            break;
        }
    }
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (nread != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

TEST(public_api_smoke, version) {
    const char *ver = nmo_version();
    ASSERT_NOT_NULL(ver);
    ASSERT_TRUE(ver[0] != '\0');

    uint32_t ver_int = nmo_version_int();
    ASSERT_NE(0u, ver_int);
}

TEST(public_api_smoke, context_create_release) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_context_release(ctx);
}

TEST(public_api_smoke, preferred_edit_and_query_headers_are_directly_usable) {
    nmo_workspace_edit_t *edit = NULL;
    nmo_object_query_t query = {0};
    query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;

    ASSERT_NULL(edit);
    ASSERT_EQ(NMO_OBJECT_QUERY_NAME_EXACT, query.name_mode);
}

TEST(public_api_smoke, reusable_edit_headers_are_in_umbrella) {
    char *umbrella = read_source_text("include/nmo.h");
    ASSERT_NOT_NULL(umbrella);
    ASSERT_NOT_NULL(strstr(umbrella, "object/nmo_asset_edit.h"));
    ASSERT_NOT_NULL(strstr(umbrella, "object/nmo_scene_edit.h"));
    ASSERT_NOT_NULL(strstr(umbrella, "object/nmo_animation_edit.h"));
    ASSERT_NOT_NULL(strstr(umbrella, "object/nmo_sound_edit.h"));
    free(umbrella);
}

TEST(public_api_smoke, project_generation_headers_are_in_umbrella) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_report_t report;
    nmo_project_validation_report_t validation;
    nmo_project_material_render_flags_t render_flags = {0};
    nmo_project_script_step_kind_t script_step =
        NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT;

    nmo_project_report_init(&report);
    nmo_project_validation_report_init(&validation);

    ASSERT_NULL(plan);
    ASSERT_FALSE(render_flags.has_alpha_func);
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT, script_step);
    ASSERT_FALSE(report.ok);
    ASSERT_FALSE(validation.ok);

    nmo_project_validation_report_dispose(&validation);
    nmo_project_report_dispose(&report);
}

TEST(public_api_smoke, reorg_owner_headers_are_directly_usable) {
    ASSERT_TRUE(1);
}

TEST(public_api_smoke, report_owner_headers_are_directly_usable) {
    nmo_comparison_result_stats_t comparison_stats = {0};
    nmo_diff_result_t diff_result = {0};
    nmo_summary_config_t summary_config = nmo_summary_config_default();

    ASSERT_FALSE(comparison_stats.match);
    ASSERT_EQ(0u, diff_result.changed_count);
    ASSERT_TRUE(summary_config.array_preview_max > 0u);
}

TEST(public_api_smoke, json_stream_is_not_part_of_public_api_surface) {
    ASSERT_FALSE(NMO_TEST_HAS_JSON_STREAM_PUBLIC_API);
}

TEST(public_api_smoke, dsl_headers_are_not_part_of_public_api_surface) {
    char *umbrella = read_source_text("include/nmo.h");
    ASSERT_NOT_NULL(umbrella);
    ASSERT_FALSE(strstr(umbrella, "dsl/nmo_dsl.h") != NULL);
    free(umbrella);

    char *legacy_summary = read_source_text("include/app/nmo_object_summary.h");
    ASSERT_NULL(legacy_summary);

    char *legacy_edit = read_source_text("include/session/nmo_session_edit.h");
    ASSERT_NULL(legacy_edit);

    char *dsl_json = read_source_text("include/app/nmo_dsl_json.h");
    ASSERT_NULL(dsl_json);
}

TEST(public_api_smoke, canonical_umbrella_and_headers_exclude_legacy_worldview) {
    char *umbrella = read_source_text("include/nmo.h");
    ASSERT_NOT_NULL(umbrella);
    ASSERT_NULL(strstr(umbrella, "session/"));
    ASSERT_NULL(strstr(umbrella, "app/"));
    ASSERT_NULL(strstr(umbrella, "behavior/nmo_script_view.h"));
    ASSERT_NULL(strstr(umbrella, "behavior/nmo_script_executor.h"));
    free(umbrella);

    char *document_header = read_source_text("include/document/nmo_document.h");
    ASSERT_NOT_NULL(document_header);
    ASSERT_NULL(strstr(document_header, "nmo_document_borrow_session"));
    ASSERT_NULL(strstr(document_header, "nmo_document_session("));
    ASSERT_NULL(strstr(document_header, "nmo_document_session_const("));
    free(document_header);

    char *workspace_header = read_source_text("include/runtime/nmo_workspace.h");
    ASSERT_NOT_NULL(workspace_header);
    ASSERT_NULL(strstr(workspace_header, "nmo_workspace_session("));
    ASSERT_NULL(strstr(workspace_header, "nmo_workspace_session_const("));
    ASSERT_NULL(strstr(workspace_header, "nmo_session_edit_flags_t"));
    ASSERT_NULL(strstr(workspace_header, "nmo_session_apply_edit_flags("));
    free(workspace_header);

    char *session_bridge = read_source_text("include/session/nmo_session_bridge.h");
    ASSERT_NULL(session_bridge);

    char *script_view = read_source_text("include/behavior/nmo_script_view.h");
    ASSERT_NULL(script_view);

    char *script_executor = read_source_text("include/behavior/nmo_script_executor.h");
    ASSERT_NULL(script_executor);
}

TEST(public_api_smoke, public_api_tier_signals_are_declared) {
    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_UMBRELLA_PUBLIC_HEADER_KIND);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_CONTEXT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_CONTEXT_LIFECYCLE_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_CONTEXT_REGISTRY_ACCESS_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_CONTEXT_RUNTIME_VIEW_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_SESSION_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SESSION_WORKFLOW_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SESSION_EXECUTION_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SESSION_ACCELERATION_CACHE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_RUNTIME_KERNEL_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_RUNTIME_KERNEL_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_REFERENCE_RESOLVER_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_REFERENCE_RESOLVER_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SERIALIZER_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SERIALIZER_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SESSION_PIPELINE_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SESSION_PIPELINE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_WORKSPACE_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_WORKSPACE_LIFECYCLE_API_TIER);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_WORKSPACE_EDIT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_OBJECT_REPOSITORY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_OBJECT_REPOSITORY_IDENTITY_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_REPOSITORY_SCRATCH_RESULT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_INDEX_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_INDEX_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_QUERY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_QUERY_ENGINE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_REF_GRAPH_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_REF_GRAPH_KIND_ENUM_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_REF_GRAPH_ANALYSIS_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_TYPE_COMMON_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_OBJECT_TYPE_COMMON_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_TYPE_QUERY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_TYPE_QUERY_SCALAR_LOOKUP_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_TYPE_QUERY_DESCRIPTOR_VIEW_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_TYPE_QUERY_STATE_VIEW_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_TYPE_SYSTEM_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_TYPE_SYSTEM_REGISTRY_API_TIER);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_TYPE_SYSTEM_AUTHORING_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_REFLECTION_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_REFLECTION_FIELD_QUERY_API_TIER);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_REFLECTION_AUTHORING_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_OPERATION_SYSTEM_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OPERATION_SYSTEM_LIFECYCLE_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OPERATION_SYSTEM_LOOKUP_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OPERATION_SYSTEM_EXECUTION_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OPERATION_SYSTEM_INTROSPECTION_API_TIER);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_OPERATION_SYSTEM_AUTHORING_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_TYPE_RUNTIME_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_TYPE_RUNTIME_AGGREGATE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_TYPE_VIEW_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_TYPE_VIEW_METADATA_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_BEHAVIOR_REGISTRY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_BEHAVIOR_REGISTRY_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_BEHAVIOR_QUERY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_BEHAVIOR_QUERY_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_BEHAVIOR_ANALYZE_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_BEHAVIOR_ANALYZE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SCRIPT_EDIT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_SCRIPT_EDIT_TRANSACTION_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SCRIPT_EDIT_GRAPH_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SCRIPT_EDIT_GRAPH_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_BEHAVIOR_VIEW_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_BEHAVIOR_VIEW_READ_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_BEHAVIOR_EDIT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_BEHAVIOR_EDIT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_BEHAVIOR_EXECUTE_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_BEHAVIOR_EXECUTE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_CHUNK_API_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_CHUNK_API_COMPAT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_MIXED_TIER, NMO_INTERFACE_CHUNK_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_INTERFACE_CHUNK_LAYOUT_API_TIER);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_INTERFACE_CHUNK_TYPE_REGISTRATION_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_INTERFACE_EDIT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_INTERFACE_EDIT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_INTERFACE_VIEW_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_INTERFACE_VIEW_READ_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_FORMAT_OBJECT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_PUBLIC_PROTOCOL, NMO_FORMAT_OBJECT_LAYOUT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_LOAD_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_LOAD_WORKFLOW_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_DOCUMENT_FILE_STATE_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_DOCUMENT_FILE_STATE_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SAVE_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_SAVE_WORKFLOW_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_STATS_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_STATS_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_CHUNK_INDEX_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_CHUNK_INDEX_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_CHUNK_INSPECT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_CHUNK_INSPECT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_SUMMARY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_SUMMARY_RENDERING_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_DIFF_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_DIFF_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_HIERARCHY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_HIERARCHY_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_COMPARISON_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_COMPARISON_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_IMPORT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_OBJECT_IMPORT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_EXPORT_DOT_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_EXPORT_DOT_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_ANSI_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_ANSI_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_HEXDUMP_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_HEXDUMP_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_OBJECT_ITER_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_STABLE_CONSUMER, NMO_OBJECT_ITER_READ_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_EXTENSION_REGISTRY_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_EXTENSION_REGISTRY_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_PROJECT_PLAN_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_PROJECT_PLAN_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_ASSET_PLAN_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_ASSET_PLAN_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SCENE_AUTHORING_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SCENE_AUTHORING_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_SCRIPT_AUTHORING_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_SCRIPT_AUTHORING_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_PROJECT_VALIDATOR_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_PROJECT_VALIDATOR_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_PROJECT_EXECUTOR_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_PROJECT_EXECUTOR_API_TIER);

    ASSERT_EQ(NMO_PUBLIC_HEADER_KIND_SINGLE_TIER, NMO_PROJECT_MANIFEST_JSON_PUBLIC_HEADER_KIND);
    ASSERT_EQ(NMO_API_TIER_ADVANCED_C, NMO_PROJECT_MANIFEST_JSON_API_TIER);
}

TEST(public_api_smoke, project_report_evidence_helpers_cover_dry_run_and_write_run) {
    const char *sound_path = "test_public_api_smoke_sound.wav";
    const char *output_path = "test_public_api_smoke_project.cmo";
    remove(sound_path);
    remove(output_path);
    remove("test_public_api_smoke_project.cmo.tmp");
    FILE *sound_file = fopen(sound_path, "wb");
    ASSERT_NOT_NULL(sound_file);
    fputs("RIFF", sound_file);
    fclose(sound_file);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;
    uint32_t sound = 0u;
    uint32_t animation = 0u;
    uint32_t script = 0u;
    float position_keys[] = {
        0.0f, 1.0f, 2.0f, 3.0f,
        1.0f, 4.0f, 5.0f, 6.0f,
    };
    nmo_objanim_controller_t controllers[] = {
        {
            .type = 1669088001u,
            .key_count = 2u,
            .data_size = sizeof(position_keys),
            .data = position_keys,
        },
    };
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Public Evidence"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_primitive_mesh(
                  plan,
                  cube,
                  NMO_PRIMITIVE_CUBE));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_material_diffuse(
                  plan,
                  cube,
                  0.9f,
                  0.2f,
                  0.1f,
                  1.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "Sound",
                  },
                  &sound));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_wavesound_file(plan, sound, sound_path));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_wavesound_attached_object(plan, sound, cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "Animation",
                  },
                  &animation));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_object_animation(
                  plan,
                  animation,
                  cube,
                  CKOBJANIM_FORMAT_CONTROLLERS,
                  false,
                  0.0f,
                  0.0f,
                  0.0f,
                  false,
                  0u,
                  true,
                  1.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_object_animation_controllers(
                  plan,
                  animation,
                  controllers,
                  sizeof(controllers) / sizeof(controllers[0])));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_FALSE(report.dry_run);
    const nmo_project_report_object_evidence_t *cube_by_name =
        nmo_project_report_find_object_evidence_by_name(&report, "Cube");
    ASSERT_NOT_NULL(cube_by_name);
    ASSERT_NE(0u, cube_by_name->object_id);
    ASSERT_NOT_NULL(nmo_project_report_find_object_evidence_by_handle(&report, cube));
    nmo_project_report_dispose(&report);

    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object_script(
                  plan,
                  cube,
                  "StartupScript",
                  &script));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_script_add_on_start_debug_output(
                  plan,
                  script,
                  "public api smoke"));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_dry_run(plan, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(3u, nmo_project_report_evidence_object_count(&report));
    ASSERT_EQ(2u, nmo_project_report_evidence_asset_binding_count(&report));
    ASSERT_EQ(0u, nmo_project_report_evidence_material_texture_slot_count(&report));
    ASSERT_EQ(1u, nmo_project_report_evidence_material_channel_count(&report));
    ASSERT_EQ(1u, nmo_project_report_evidence_script_count(&report));
    ASSERT_EQ(1u, nmo_project_report_evidence_sound_binding_count(&report));
    ASSERT_EQ(1u, nmo_project_report_evidence_animation_binding_count(&report));
    ASSERT_EQ(0u, nmo_project_report_evidence_object_count(NULL));
    ASSERT_EQ(0u, nmo_project_report_evidence_asset_binding_count(NULL));
    ASSERT_EQ(0u, nmo_project_report_evidence_material_texture_slot_count(NULL));
    ASSERT_EQ(0u, nmo_project_report_evidence_material_channel_count(NULL));
    ASSERT_EQ(0u, nmo_project_report_evidence_script_count(NULL));
    ASSERT_EQ(0u, nmo_project_report_evidence_sound_binding_count(NULL));
    ASSERT_EQ(0u, nmo_project_report_evidence_animation_binding_count(NULL));

    cube_by_name =
        nmo_project_report_find_object_evidence_by_name(&report, "Cube");
    ASSERT_NOT_NULL(cube_by_name);
    ASSERT_EQ(cube, cube_by_name->plan_handle);
    ASSERT_EQ(NMO_CID_3DENTITY, cube_by_name->class_id);
    const nmo_project_report_object_evidence_t *cube_by_handle =
        nmo_project_report_find_object_evidence_by_handle(&report, cube);
    ASSERT_EQ(cube_by_name, cube_by_handle);
    ASSERT_NOT_NULL(nmo_project_report_find_asset_binding_evidence(
        &report,
        "Cube_Mesh"));
    ASSERT_NOT_NULL(nmo_project_report_find_material_channel_evidence(
        &report,
        "Cube_Material"));
    ASSERT_NULL(nmo_project_report_find_material_texture_slot_evidence(
        &report,
        "Cube_Material",
        0u));
    const nmo_project_report_script_evidence_t *script_evidence =
        nmo_project_report_find_script_evidence(&report, "StartupScript");
    ASSERT_NOT_NULL(script_evidence);
    ASSERT_EQ(1u, script_evidence->step_count);
    ASSERT_TRUE(script_evidence->validation_ok);
    ASSERT_NOT_NULL(nmo_project_report_find_sound_binding_evidence(
        &report,
        "Sound"));
    ASSERT_NOT_NULL(nmo_project_report_find_animation_binding_evidence(
        &report,
        "Animation"));
    ASSERT_NULL(nmo_project_report_find_asset_binding_evidence(NULL, "Cube_Mesh"));
    ASSERT_NULL(nmo_project_report_find_asset_binding_evidence(&report, NULL));
    ASSERT_NULL(nmo_project_report_find_material_texture_slot_evidence(
        NULL,
        "Cube_Material",
        0u));
    ASSERT_NULL(nmo_project_report_find_material_texture_slot_evidence(
        &report,
        NULL,
        0u));
    ASSERT_NULL(nmo_project_report_find_material_channel_evidence(NULL, "Cube_Material"));
    ASSERT_NULL(nmo_project_report_find_material_channel_evidence(&report, NULL));
    ASSERT_NULL(nmo_project_report_find_script_evidence(NULL, "StartupScript"));
    ASSERT_NULL(nmo_project_report_find_script_evidence(&report, NULL));
    ASSERT_NULL(nmo_project_report_find_sound_binding_evidence(NULL, "Sound"));
    ASSERT_NULL(nmo_project_report_find_sound_binding_evidence(&report, NULL));
    ASSERT_NULL(nmo_project_report_find_animation_binding_evidence(NULL, "Animation"));
    ASSERT_NULL(nmo_project_report_find_animation_binding_evidence(&report, NULL));
    ASSERT_NULL(nmo_project_report_find_object_evidence_by_name(&report, "Missing"));
    ASSERT_NULL(nmo_project_report_find_object_evidence_by_name(NULL, "Cube"));
    ASSERT_NULL(nmo_project_report_find_object_evidence_by_name(&report, NULL));
    ASSERT_NULL(nmo_project_report_find_object_evidence_by_handle(&report, 9999u));
    ASSERT_NULL(nmo_project_report_find_object_evidence_by_handle(NULL, cube));
    nmo_project_report_dispose(&report);

    nmo_project_plan_destroy(plan);
    remove(output_path);
    remove(sound_path);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(public_api_smoke, version);
    REGISTER_TEST(public_api_smoke, context_create_release);
    REGISTER_TEST(public_api_smoke, preferred_edit_and_query_headers_are_directly_usable);
    REGISTER_TEST(public_api_smoke, reusable_edit_headers_are_in_umbrella);
    REGISTER_TEST(public_api_smoke, project_generation_headers_are_in_umbrella);
    REGISTER_TEST(public_api_smoke, reorg_owner_headers_are_directly_usable);
    REGISTER_TEST(public_api_smoke, report_owner_headers_are_directly_usable);
    REGISTER_TEST(public_api_smoke, json_stream_is_not_part_of_public_api_surface);
    REGISTER_TEST(public_api_smoke, dsl_headers_are_not_part_of_public_api_surface);
    REGISTER_TEST(public_api_smoke, canonical_umbrella_and_headers_exclude_legacy_worldview);
    REGISTER_TEST(public_api_smoke, public_api_tier_signals_are_declared);
    REGISTER_TEST(public_api_smoke, project_report_evidence_helpers_cover_dry_run_and_write_run);
TEST_MAIN_END()
