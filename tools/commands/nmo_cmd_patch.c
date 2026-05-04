/**
 * @file nmo_cmd_patch.c
 * @brief Strict patch apply/diff commands.
 */

#include "nmo_cmd_patch.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_json.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"

#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_edit_plan_json.h"
#include "core/nmo_error.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_manifest_json.h"
#include "runtime/nmo_context.h"
#include "yyjson.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct patch_plan {
    nmo_edit_plan_manifest_t manifest;
    const char *input;
    const char *output;
    nmo_edit_plan_t *edit_plan;
} patch_plan_t;

typedef struct patch_apply_args {
    bool dry_run;
    const char *patch_path;
    const char *project_path;
    const char *output_path;
} patch_apply_args_t;

static void patch_add_normalized_manifest_json(yyjson_mut_doc *doc,
                                               yyjson_mut_val *data,
                                               const patch_plan_t *plan) {
    if (!doc || !data || !plan || !plan->edit_plan) {
        return;
    }

    char *json = NULL;
    if (nmo_edit_plan_manifest_json_write(
            plan->edit_plan, plan->input, plan->output, &json) != NMO_OK ||
        json == NULL) {
        return;
    }

    yyjson_doc *manifest_doc = yyjson_read(json, strlen(json), 0);
    nmo_edit_plan_manifest_json_free(json);
    if (manifest_doc == NULL) {
        return;
    }

    yyjson_val *manifest_root = yyjson_doc_get_root(manifest_doc);
    yyjson_mut_val *manifest = yyjson_val_mut_copy(doc, manifest_root);
    yyjson_doc_free(manifest_doc);
    if (manifest != NULL) {
        yyjson_mut_obj_add_val(doc, data, "manifest", manifest);
    }
}

static void patch_add_edit_report_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const patch_plan_t *plan,
    nmo_edit_report_t *report,
    bool dry_run) {
    if (plan != NULL && plan->output != NULL && report != NULL &&
        report->output_path == NULL) {
        (void)nmo_edit_report_set_output_path(report, plan->output);
    }
    nmo_cli_edit_report_add_schema_v2_json(doc, data, report, dry_run);
    if (plan != NULL) {
        nmo_cli_json_add_str_safe(doc, data, "input", plan->input);
        nmo_cli_json_add_str_safe(doc, data, "output", plan->output);
        patch_add_normalized_manifest_json(doc, data, plan);
    }
}

static void patch_plan_free(patch_plan_t *plan) {
    if (!plan) {
        return;
    }
    nmo_edit_plan_manifest_dispose(&plan->manifest);
    memset(plan, 0, sizeof(*plan));
}

static int patch_parse_plan(const char *path, patch_plan_t *out_plan) {
    if (!path || !out_plan) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    memset(out_plan, 0, sizeof(*out_plan));

    nmo_status_t read_status =
        nmo_edit_plan_manifest_json_read_file(path, &out_plan->manifest);
    if (read_status != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n",
                message != NULL && message[0] != '\0'
                    ? message
                    : nmo_error_string(read_status));
        patch_plan_free(out_plan);
        return read_status == NMO_ERR_NOMEM
            ? NMO_CLI_EXIT_INTERNAL_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }

    out_plan->input = out_plan->manifest.input_path;
    out_plan->output = out_plan->manifest.output_path;
    out_plan->edit_plan = out_plan->manifest.plan;
    if (out_plan->input == NULL || out_plan->output == NULL ||
        out_plan->edit_plan == NULL) {
        fprintf(stderr, "Error: Edit plan manifest requires input and output\n");
        patch_plan_free(out_plan);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int patch_apply_plan(patch_plan_t *plan,
                            bool dry_run,
                            const nmo_cli_global_opts_t *global,
                            bool emit_diff) {
    nmo_cmd_ctx_t ctx;
    int rc = nmo_cli_write_init_ctx(&ctx, plan->input, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    if (emit_diff) {
        nmo_edit_report_t edit_report;
        nmo_status_t report_rc = nmo_edit_report_init(&edit_report);
        if (report_rc != NMO_OK) {
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = true;
        nmo_status_t st = nmo_edit_executor_execute(
            ctx.workspace, plan->edit_plan, &options, &edit_report);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: patch diff failed: %s\n",
                    nmo_error_string(st));
            nmo_edit_report_dispose(&edit_report);
            int exit_code = (st == NMO_ERR_INVALID_ARGUMENT ||
                             st == NMO_ERR_NOT_FOUND)
                ? NMO_CLI_EXIT_ARG_ERROR
                : NMO_CLI_EXIT_INTERNAL_ERROR;
            return nmo_cmd_ctx_done(&ctx, exit_code);
        }

        if (ctx.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
            if (!doc) {
                nmo_edit_report_dispose(&edit_report);
                return nmo_cmd_ctx_done(&ctx,
                                        NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            patch_add_edit_report_json(doc, data, plan, &edit_report, true);
            int json_rc = nmo_cmd_ctx_json_end(&ctx, doc, data,
                                               "patch.diff");
            nmo_edit_report_dispose(&edit_report);
            return json_rc;
        }

        for (size_t i = 0; i < edit_report.operation_count; ++i) {
            const nmo_edit_operation_result_t *op = &edit_report.operations[i];
            fprintf(ctx.out, "%s #%u: result #%u, status %s\n",
                    nmo_cli_edit_report_op_kind_string(op->kind),
                    op->primary_id,
                    op->result_id,
                    nmo_error_string(op->status));
        }
        nmo_edit_report_dispose(&edit_report);
        return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_SUCCESS);
    }

    nmo_edit_report_t edit_report;
    nmo_status_t report_rc = nmo_edit_report_init(&edit_report);
    if (report_rc != NMO_OK) {
        return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = dry_run;
    nmo_status_t st = nmo_edit_executor_execute(
        ctx.workspace, plan->edit_plan, &options, &edit_report);
    if (st != NMO_OK) {
        int exit_code = (st == NMO_ERR_INVALID_ARGUMENT ||
                         st == NMO_ERR_NOT_FOUND)
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
        if (ctx.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
            if (!doc) {
                nmo_edit_report_dispose(&edit_report);
                return nmo_cmd_ctx_done(&ctx,
                                        NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            patch_add_edit_report_json(doc, data, plan, &edit_report,
                                       dry_run);
            int json_rc = nmo_cmd_ctx_json_end(&ctx, doc, data,
                                               "patch.apply");
            nmo_edit_report_dispose(&edit_report);
            (void)json_rc;
            return nmo_cmd_ctx_done(&ctx, exit_code);
        }
        size_t failed_index = 0;
        for (size_t i = 0; i < edit_report.operation_count; ++i) {
            if (edit_report.operations[i].status != NMO_OK) {
                failed_index = i;
                break;
            }
        }
        const nmo_edit_op_t *edit_op =
            nmo_edit_plan_get(plan->edit_plan, failed_index);
        const nmo_edit_operation_result_t *failed_op =
            failed_index < edit_report.operation_count
                ? &edit_report.operations[failed_index]
                : NULL;
        if (edit_op) {
            fprintf(stderr, "Error: %s #%u failed: %s",
                    nmo_cli_edit_report_op_kind_string(edit_op->kind),
                    edit_op->primary_id,
                    nmo_error_string(st));
            if (failed_op && failed_op->diagnostic_code) {
                fprintf(stderr, " (%s)", failed_op->diagnostic_code);
            }
            if (failed_op && failed_op->diagnostic_message) {
                fprintf(stderr, ": %s", failed_op->diagnostic_message);
            }
        } else {
            fprintf(stderr, "Error: patch operation failed: %s",
                    nmo_error_string(st));
        }
        fputc('\n', stderr);
        nmo_edit_report_dispose(&edit_report);
        return nmo_cmd_ctx_done(&ctx, exit_code);
    }

    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
        int save_rc = nmo_cli_save_document(ctx.document, plan->output,
                                           &save_opts);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_edit_report_dispose(&edit_report);
            return nmo_cmd_ctx_done(&ctx, save_rc);
        }
    }

    if (ctx.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&ctx);
        if (!doc) {
            nmo_edit_report_dispose(&edit_report);
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        patch_add_edit_report_json(doc, data, plan, &edit_report, dry_run);
        int json_rc = nmo_cmd_ctx_json_end(&ctx, doc, data, "patch.apply");
        nmo_edit_report_dispose(&edit_report);
        return json_rc;
    }

    if (dry_run) {
        fprintf(ctx.out, "[dry-run] Applied %zu operation(s)\n",
                nmo_edit_plan_count(plan->edit_plan));
    } else {
        fprintf(ctx.out, "Applied %zu operation(s)\n",
                nmo_edit_plan_count(plan->edit_plan));
        fprintf(ctx.out, "Saved to: %s\n", plan->output);
    }
    nmo_edit_report_dispose(&edit_report);
    return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_SUCCESS);
}

static int patch_read_file(const char *path, char **out_text, size_t *out_size)
{
    FILE *fp = NULL;
    long size = 0;
    char *text = NULL;
    size_t bytes_read = 0u;

    if (!path || !out_text || !out_size) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    *out_text = NULL;
    *out_size = 0u;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open project manifest: %s\n", path);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fprintf(stderr, "Error: Failed to seek project manifest: %s\n", path);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        fprintf(stderr, "Error: Failed to size project manifest: %s\n", path);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    rewind(fp);

    text = (char *)malloc((size_t)size + 1u);
    if (!text) {
        fclose(fp);
        fprintf(stderr, "Error: Failed to allocate project manifest buffer\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    bytes_read = fread(text, 1u, (size_t)size, fp);
    fclose(fp);
    if (bytes_read != (size_t)size) {
        free(text);
        fprintf(stderr, "Error: Failed to read project manifest: %s\n", path);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    text[bytes_read] = '\0';
    *out_text = text;
    *out_size = bytes_read;
    return NMO_CLI_EXIT_SUCCESS;
}

static bool patch_path_is_absolute(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}

static char *patch_manifest_base_dir(const char *manifest_path)
{
    if (!manifest_path) {
        return NULL;
    }
    const char *slash = strrchr(manifest_path, '/');
    const char *bslash = strrchr(manifest_path, '\\');
    const char *sep = slash;
    if (!sep || (bslash && bslash > sep)) {
        sep = bslash;
    }
    if (!sep) {
        return NULL;
    }

    size_t len = (size_t)(sep - manifest_path);
    char *dir = (char *)malloc(len + 1u);
    if (!dir) {
        return NULL;
    }
    memcpy(dir, manifest_path, len);
    dir[len] = '\0';
    return dir;
}

static char *patch_join_manifest_path(const char *base_dir, const char *path)
{
    if (!path) {
        return NULL;
    }
    if (patch_path_is_absolute(path) || !base_dir || !base_dir[0]) {
        size_t len = strlen(path);
        char *copy = (char *)malloc(len + 1u);
        if (copy) {
            memcpy(copy, path, len + 1u);
        }
        return copy;
    }

    size_t base_len = strlen(base_dir);
    size_t path_len = strlen(path);
    bool needs_sep = base_dir[base_len - 1u] != '/' && base_dir[base_len - 1u] != '\\';
    char *joined = (char *)malloc(base_len + (needs_sep ? 1u : 0u) + path_len + 1u);
    if (!joined) {
        return NULL;
    }
    memcpy(joined, base_dir, base_len);
    size_t offset = base_len;
    if (needs_sep) {
        joined[offset++] = '/';
    }
    memcpy(joined + offset, path, path_len + 1u);
    return joined;
}

static nmo_status_t patch_resolve_project_manifest_paths(
    nmo_project_manifest_t *manifest,
    const char *base_dir)
{
    if (!manifest || !manifest->plan || !base_dir || !base_dir[0]) {
        return NMO_OK;
    }

    size_t asset_count = nmo_project_plan_asset_count(manifest->plan);
    for (size_t i = 0u; i < asset_count; ++i) {
        nmo_project_asset_desc_t asset = {0};
        NMO_RETURN_IF_ERROR(nmo_project_plan_get_asset(manifest->plan, i, &asset));
        if (asset.has_external_mesh &&
            !patch_path_is_absolute(asset.external_mesh_path)) {
            char *resolved =
                patch_join_manifest_path(base_dir, asset.external_mesh_path);
            if (!resolved) {
                return NMO_ERR_NOMEM;
            }
            nmo_status_t st = nmo_project_plan_set_external_mesh(
                manifest->plan,
                asset.object_handle,
                resolved);
            free(resolved);
            if (st != NMO_OK) {
                return st;
            }
        }
        if (asset.has_material_texture &&
            !patch_path_is_absolute(asset.material_texture_path)) {
            char *resolved =
                patch_join_manifest_path(base_dir, asset.material_texture_path);
            if (!resolved) {
                return NMO_ERR_NOMEM;
            }
            nmo_status_t st = nmo_project_plan_set_material_texture(
                manifest->plan,
                asset.object_handle,
                resolved);
            free(resolved);
            if (st != NMO_OK) {
                return st;
            }
        }
        size_t obj_material_count =
            nmo_project_plan_obj_material_count(manifest->plan, asset.object_handle);
        for (size_t material_index = 0u;
             material_index < obj_material_count;
             ++material_index) {
            nmo_project_material_spec_t material = {0};
            NMO_RETURN_IF_ERROR(nmo_project_plan_get_obj_material(
                manifest->plan,
                asset.object_handle,
                material_index,
                &material));
            if (material.has_texture &&
                !patch_path_is_absolute(material.texture_path)) {
                char *resolved =
                    patch_join_manifest_path(base_dir, material.texture_path);
                if (!resolved) {
                    return NMO_ERR_NOMEM;
                }
                nmo_status_t st = nmo_project_plan_set_obj_material_texture(
                    manifest->plan,
                    asset.object_handle,
                    material_index,
                    resolved);
                free(resolved);
                if (st != NMO_OK) {
                    return st;
                }
            }
        }
    }
    return NMO_OK;
}

static yyjson_mut_val *patch_project_name_list_json(
    yyjson_mut_doc *doc,
    const nmo_project_report_name_list_t *list)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (!arr) {
        return NULL;
    }
    if (!list) {
        return arr;
    }
    for (size_t i = 0u; i < list->count; ++i) {
        if (!nmo_cli_json_add_str_safe_to_arr(doc, arr, list->names[i])) {
            return NULL;
        }
    }
    return arr;
}

static void patch_project_report_add_created_list(
    yyjson_mut_doc *doc,
    yyjson_mut_val *created,
    const char *key,
    const nmo_project_report_diff_t *diff)
{
    yyjson_mut_val *arr = patch_project_name_list_json(
        doc,
        diff ? &diff->created : NULL);
    if (arr) {
        nmo_cli_json_add_val_safe(doc, created, key, arr);
    }
}

static void patch_add_project_validation_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const nmo_project_validation_report_t *validation)
{
    yyjson_mut_val *validation_obj = yyjson_mut_obj(doc);
    yyjson_mut_val *issues = yyjson_mut_arr(doc);
    if (!validation_obj || !issues) {
        return;
    }

    bool ok = validation ? validation->ok : false;
    nmo_cli_json_add_bool_safe(doc, validation_obj, "ok", ok);
    if (validation) {
        for (size_t i = 0u; i < validation->issue_count; ++i) {
            const nmo_project_validation_issue_t *issue =
                &validation->issues[i];
            yyjson_mut_val *issue_obj = yyjson_mut_obj(doc);
            if (!issue_obj) {
                continue;
            }
            nmo_cli_json_add_str_safe(doc, issue_obj, "code", issue->code);
            nmo_cli_json_add_str_safe(doc, issue_obj, "message", issue->message);
            nmo_cli_json_add_str_safe(doc,
                                      issue_obj,
                                      "subject_kind",
                                      issue->subject_kind);
            nmo_cli_json_add_str_safe(doc,
                                      issue_obj,
                                      "subject_name",
                                      issue->subject_name);
            nmo_cli_json_add_str_safe(doc,
                                      issue_obj,
                                      "source_path",
                                      issue->source_path);
            yyjson_mut_arr_append(issues, issue_obj);
        }
    }
    nmo_cli_json_add_val_safe(doc, validation_obj, "issues", issues);
    nmo_cli_json_add_val_safe(doc, data, "validation", validation_obj);
}

static void patch_add_project_evidence_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const nmo_project_report_evidence_t *evidence)
{
    yyjson_mut_val *evidence_obj = yyjson_mut_obj(doc);
    if (!evidence_obj) {
        return;
    }

    yyjson_mut_val *objects = yyjson_mut_arr(doc);
    if (objects) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->object_count; ++i) {
                const nmo_project_report_object_evidence_t *object =
                    &evidence->objects[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_uint_safe(
                    doc,
                    item,
                    "plan_handle",
                    (uint64_t)object->plan_handle);
                nmo_cli_json_add_uint_safe(
                    doc,
                    item,
                    "id",
                    (uint64_t)object->object_id);
                nmo_cli_json_add_uint_safe(
                    doc,
                    item,
                    "class_id",
                    (uint64_t)object->class_id);
                nmo_cli_json_add_str_safe(doc, item, "name", object->name);
                yyjson_mut_arr_append(objects, item);
            }
        }
        nmo_cli_json_add_val_safe(doc, evidence_obj, "generated_objects", objects);
    }

    yyjson_mut_val *asset_bindings = yyjson_mut_arr(doc);
    if (asset_bindings) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->asset_binding_count; ++i) {
                const nmo_project_report_asset_binding_evidence_t *binding =
                    &evidence->asset_bindings[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "owner",
                    binding->owner_name);
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "asset",
                    binding->asset_name);
                nmo_cli_json_add_str_safe(doc, item, "kind", binding->kind);
                yyjson_mut_arr_append(asset_bindings, item);
            }
        }
        nmo_cli_json_add_val_safe(
            doc,
            evidence_obj,
            "asset_bindings",
            asset_bindings);
    }

    yyjson_mut_val *texture_slots = yyjson_mut_arr(doc);
    if (texture_slots) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->material_texture_slot_count; ++i) {
                const nmo_project_report_material_texture_slot_evidence_t *slot =
                    &evidence->material_texture_slots[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "material",
                    slot->material_name);
                nmo_cli_json_add_uint_safe(
                    doc,
                    item,
                    "slot",
                    (uint64_t)slot->slot);
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "texture",
                    slot->texture_name);
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "source_path",
                    slot->source_path);
                yyjson_mut_arr_append(texture_slots, item);
            }
        }
        nmo_cli_json_add_val_safe(
            doc,
            evidence_obj,
            "material_texture_slots",
            texture_slots);
    }

    yyjson_mut_val *material_channels = yyjson_mut_arr(doc);
    if (material_channels) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->material_channel_count; ++i) {
                const nmo_project_report_material_channel_evidence_t *channel =
                    &evidence->material_channels[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "material",
                    channel->material_name);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "diffuse",
                    channel->has_diffuse);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "ambient",
                    channel->has_ambient);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "specular",
                    channel->has_specular);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "emissive",
                    channel->has_emissive);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "specular_power",
                    channel->has_specular_power);
                yyjson_mut_arr_append(material_channels, item);
            }
        }
        nmo_cli_json_add_val_safe(
            doc,
            evidence_obj,
            "material_channels",
            material_channels);
    }

    yyjson_mut_val *scripts = yyjson_mut_arr(doc);
    if (scripts) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->script_count; ++i) {
                const nmo_project_report_script_evidence_t *script =
                    &evidence->scripts[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_str_safe(doc, item, "name", script->name);
                nmo_cli_json_add_uint_safe(
                    doc,
                    item,
                    "step_count",
                    (uint64_t)script->step_count);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "validation_ok",
                    script->validation_ok);
                yyjson_mut_arr_append(scripts, item);
            }
        }
        nmo_cli_json_add_val_safe(doc, evidence_obj, "scripts", scripts);
    }

    yyjson_mut_val *sound_bindings = yyjson_mut_arr(doc);
    if (sound_bindings) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->sound_binding_count; ++i) {
                const nmo_project_report_sound_binding_evidence_t *sound =
                    &evidence->sound_bindings[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_str_safe(doc, item, "name", sound->name);
                nmo_cli_json_add_str_safe(doc, item, "file", sound->file);
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "attached_object",
                    sound->attached_object_name);
                yyjson_mut_arr_append(sound_bindings, item);
            }
        }
        nmo_cli_json_add_val_safe(
            doc,
            evidence_obj,
            "sound_bindings",
            sound_bindings);
    }

    yyjson_mut_val *animation_bindings = yyjson_mut_arr(doc);
    if (animation_bindings) {
        if (evidence) {
            for (size_t i = 0u; i < evidence->animation_binding_count; ++i) {
                const nmo_project_report_animation_binding_evidence_t *animation =
                    &evidence->animation_bindings[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item) {
                    continue;
                }
                nmo_cli_json_add_str_safe(doc, item, "name", animation->name);
                nmo_cli_json_add_str_safe(
                    doc,
                    item,
                    "target",
                    animation->target_name);
                nmo_cli_json_add_uint_safe(
                    doc,
                    item,
                    "format",
                    (uint64_t)animation->format);
                nmo_cli_json_add_bool_safe(
                    doc,
                    item,
                    "has_length",
                    animation->has_length);
                if (animation->has_length) {
                    nmo_cli_json_add_real_safe(
                        doc,
                        item,
                        "length",
                        animation->length);
                }
                yyjson_mut_arr_append(animation_bindings, item);
            }
        }
        nmo_cli_json_add_val_safe(
            doc,
            evidence_obj,
            "animation_bindings",
            animation_bindings);
    }

    nmo_cli_json_add_bool_safe(
        doc,
        evidence_obj,
        "post_save_validate_checked",
        evidence ? evidence->post_save_validate_checked : false);
    nmo_cli_json_add_bool_safe(
        doc,
        evidence_obj,
        "post_save_validate_ok",
        evidence ? evidence->post_save_validate_ok : false);
    nmo_cli_json_add_bool_safe(
        doc,
        evidence_obj,
        "post_load_checked",
        evidence ? evidence->post_load_checked : false);
    nmo_cli_json_add_bool_safe(
        doc,
        evidence_obj,
        "post_load_ok",
        evidence ? evidence->post_load_ok : false);
    nmo_cli_json_add_val_safe(doc, data, "evidence", evidence_obj);
}

static void patch_add_project_report_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const nmo_project_report_t *report,
    bool requested_dry_run,
    const char *manifest_path,
    const char *output_path)
{
    bool ok = report ? report->ok : false;
    bool dry_run = report ? report->dry_run : requested_dry_run;
    nmo_cli_json_add_bool_safe(doc, data, "ok", ok);
    nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
    nmo_cli_json_add_str_safe(doc, data, "manifest", manifest_path);
    nmo_cli_json_add_str_safe(doc, data, "output", output_path);

    yyjson_mut_val *diffs = yyjson_mut_obj(doc);
    yyjson_mut_val *created = yyjson_mut_obj(doc);
    if (diffs && created) {
        patch_project_report_add_created_list(
            doc,
            created,
            "documents",
            report ? &report->document_diff : NULL);
        patch_project_report_add_created_list(
            doc,
            created,
            "scenes",
            report ? &report->scene_diff : NULL);
        patch_project_report_add_created_list(
            doc,
            created,
            "objects",
            report ? &report->object_diff : NULL);
        patch_project_report_add_created_list(
            doc,
            created,
            "assets",
            report ? &report->asset_diff : NULL);
        patch_project_report_add_created_list(
            doc,
            created,
            "scripts",
            report ? &report->script_diff : NULL);
        patch_project_report_add_created_list(
            doc,
            created,
            "managers",
            report ? &report->manager_diff : NULL);
        nmo_cli_json_add_val_safe(doc, diffs, "created", created);
        nmo_cli_json_add_val_safe(doc, data, "diffs", diffs);
    }

    patch_add_project_validation_json(
        doc,
        data,
        report ? &report->validation : NULL);
    patch_add_project_evidence_json(
        doc,
        data,
        report ? &report->evidence : NULL);
}

static int patch_emit_project_report_json(
    nmo_cmd_ctx_t *ctx,
    const nmo_project_report_t *report,
    bool requested_dry_run,
    const char *manifest_path,
    const char *output_path)
{
    yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
    if (!doc) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    patch_add_project_report_json(doc,
                                  data,
                                  report,
                                  requested_dry_run,
                                  manifest_path,
                                  output_path);
    return nmo_cmd_ctx_json_end(ctx, doc, data, "patch.apply");
}

static void patch_print_project_validation_issues(
    FILE *out,
    const nmo_project_validation_report_t *validation)
{
    if (!out || !validation) {
        return;
    }
    for (size_t i = 0u; i < validation->issue_count; ++i) {
        const nmo_project_validation_issue_t *issue = &validation->issues[i];
        fprintf(out,
                "Validation: %s: %s",
                issue->code ? issue->code : "project_validation",
                issue->message ? issue->message : "Validation failed");
        if (issue->subject_kind || issue->subject_name ||
            issue->source_path) {
            fputs(" (", out);
            bool needs_comma = false;
            if (issue->subject_kind || issue->subject_name) {
                fprintf(out,
                        "%s%s%s",
                        issue->subject_kind ? issue->subject_kind : "subject",
                        issue->subject_name ? " " : "",
                        issue->subject_name ? issue->subject_name : "");
                needs_comma = true;
            }
            if (issue->source_path) {
                fprintf(out,
                        "%s%s",
                        needs_comma ? ", " : "",
                        issue->source_path);
            }
            fputs(")", out);
        }
        fputc('\n', out);
    }
}

static int patch_apply_project_manifest(
    const patch_apply_args_t *args,
    const nmo_cli_global_opts_t *global)
{
    nmo_cmd_ctx_t ctx;
    int rc = nmo_cmd_ctx_init_no_file(&ctx, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    char *json = NULL;
    size_t json_size = 0u;
    rc = patch_read_file(args->project_path, &json, &json_size);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&ctx, rc);
    }

    nmo_project_manifest_t manifest;
    nmo_project_manifest_init(&manifest);
    nmo_status_t st = nmo_project_manifest_json_read_manifest(
        json,
        json_size,
        &manifest);
    free(json);
    if (st != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n",
                message && message[0] ? message : nmo_error_string(st));
        nmo_project_manifest_dispose(&manifest);
        return nmo_cmd_ctx_done(&ctx,
                                st == NMO_ERR_NOMEM
                                    ? NMO_CLI_EXIT_INTERNAL_ERROR
                                    : NMO_CLI_EXIT_ARG_ERROR);
    }

    char *manifest_base_dir = patch_manifest_base_dir(args->project_path);
    st = patch_resolve_project_manifest_paths(&manifest, manifest_base_dir);
    if (st != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n",
                message && message[0] ? message : nmo_error_string(st));
        free(manifest_base_dir);
        nmo_project_manifest_dispose(&manifest);
        return nmo_cmd_ctx_done(&ctx,
                                st == NMO_ERR_NOMEM
                                    ? NMO_CLI_EXIT_INTERNAL_ERROR
                                    : NMO_CLI_EXIT_ARG_ERROR);
    }

    char *resolved_manifest_output = NULL;
    const char *output_path = args->output_path
        ? args->output_path
        : manifest.output_path;
    if (!args->output_path && output_path && manifest_base_dir &&
        !patch_path_is_absolute(output_path)) {
        resolved_manifest_output =
            patch_join_manifest_path(manifest_base_dir, output_path);
        if (!resolved_manifest_output) {
            free(manifest_base_dir);
            nmo_project_manifest_dispose(&manifest);
            return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        output_path = resolved_manifest_output;
    }
    if (!output_path || output_path[0] == '\0') {
        fprintf(stderr,
                "Error: Project manifest output requires -o/--output or output\n");
        free(resolved_manifest_output);
        free(manifest_base_dir);
        nmo_project_manifest_dispose(&manifest);
        return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_project_report_t report;
    nmo_project_report_init(&report);
    st = args->dry_run
        ? nmo_project_executor_execute_dry_run(manifest.plan, &report)
        : nmo_project_executor_execute_to_file(
              manifest.plan,
              output_path,
              &report);
    if (st != NMO_OK) {
        int exit_code = st == NMO_ERR_INVALID_ARGUMENT ||
                                st == NMO_ERR_VALIDATION_FAILED ||
                                st == NMO_ERR_NOT_FOUND
                            ? NMO_CLI_EXIT_ARG_ERROR
                            : NMO_CLI_EXIT_INTERNAL_ERROR;
        if (ctx.is_json) {
            int json_rc = patch_emit_project_report_json(&ctx,
                                                         &report,
                                                         args->dry_run,
                                                         args->project_path,
                                                         output_path);
            (void)json_rc;
        } else {
            const char *message = nmo_last_error_message();
            fprintf(stderr, "Error: %s\n",
                    message && message[0] ? message : nmo_error_string(st));
            patch_print_project_validation_issues(stderr, &report.validation);
        }
        nmo_project_report_dispose(&report);
        free(resolved_manifest_output);
        free(manifest_base_dir);
        nmo_project_manifest_dispose(&manifest);
        return nmo_cmd_ctx_done(&ctx, exit_code);
    }

    if (ctx.is_json) {
        rc = patch_emit_project_report_json(&ctx,
                                            &report,
                                            args->dry_run,
                                            args->project_path,
                                            output_path);
    } else {
        fprintf(ctx.out, "Generated project from: %s\n", args->project_path);
        if (args->dry_run) {
            fprintf(ctx.out, "[dry-run] Output not written: %s\n",
                    output_path);
        } else {
            fprintf(ctx.out, "Saved to: %s\n", output_path);
        }
        rc = NMO_CLI_EXIT_SUCCESS;
    }

    nmo_project_report_dispose(&report);
    free(resolved_manifest_output);
    free(manifest_base_dir);
    nmo_project_manifest_dispose(&manifest);
    return nmo_cmd_ctx_done(&ctx, rc);
}

static int patch_parse_apply_args(int argc,
                                  char **argv,
                                  patch_apply_args_t *out_args) {
    static const nmo_opt_def_t opts[] = {
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"},
        {"--project", NULL, NMO_OPT_STRING, "Project manifest to generate"},
        {"--output", "-o", NMO_OPT_STRING, "Output CMO path"},
    };
    enum { OPT_DRY_RUN, OPT_PROJECT, OPT_OUTPUT, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[4];
    nmo_opt_result_t r = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 4,
    };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0 ||
        ((vals[OPT_PROJECT].present && r.pos_count != 0) ||
         (!vals[OPT_PROJECT].present && r.pos_count != 1)) ||
        (vals[OPT_OUTPUT].present && !vals[OPT_PROJECT].present)) {
        fprintf(stderr,
                "Usage: nmo patch apply <patch.json> [--dry-run]\n"
                "       nmo patch apply --project <manifest.json> -o <output.cmo>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    memset(out_args, 0, sizeof(*out_args));
    out_args->dry_run = vals[OPT_DRY_RUN].present &&
                        vals[OPT_DRY_RUN].val.flag;
    out_args->patch_path = r.pos_count == 1 ? r.pos_args[0] : NULL;
    out_args->project_path = vals[OPT_PROJECT].present
        ? vals[OPT_PROJECT].val.str
        : NULL;
    out_args->output_path = vals[OPT_OUTPUT].present
        ? vals[OPT_OUTPUT].val.str
        : NULL;
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_patch_apply(int argc,
                        char **argv,
                        const nmo_cli_global_opts_t *global) {
    patch_apply_args_t args;
    int rc = patch_parse_apply_args(argc, argv, &args);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    if (args.project_path) {
        return patch_apply_project_manifest(&args, global);
    }

    patch_plan_t plan;
    rc = patch_parse_plan(args.patch_path, &plan);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_apply_plan(&plan, args.dry_run, global, false);
    patch_plan_free(&plan);
    return rc;
}

int nmo_cmd_patch_diff(int argc,
                       char **argv,
                       const nmo_cli_global_opts_t *global) {
    if (argc != 2 || !argv || !argv[1]) {
        fprintf(stderr, "Usage: nmo patch diff <patch.json>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    patch_plan_t plan;
    int rc = patch_parse_plan(argv[1], &plan);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    rc = patch_apply_plan(&plan, true, global, true);
    patch_plan_free(&plan);
    return rc;
}

