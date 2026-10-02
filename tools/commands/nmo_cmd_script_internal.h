/**
 * @file nmo_cmd_script_internal.h
 * @brief Types and helpers the script command files share.
 */

#ifndef NMO_CMD_SCRIPT_INTERNAL_H
#define NMO_CMD_SCRIPT_INTERNAL_H

#include "nmo_cmd_script.h"
#include "../nmo_cli_json.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_cmd_core.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "../nmo_tool_owner.h"
#include "../nmo_tool_session.h"
#include "behavior/nmo_behavior_view.h"
#include "edit/nmo_behavior_execute.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_query.h"
#include "edit/nmo_edit_plan.h"
#include "edit/nmo_script_edit.h"
#include "behavior/nmo_script_edit_graph.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_view.h"
#include "lua/nmo_lua_fold_map_parser.h"
#include "nmo_lua.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_context.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lauxlib.h"

/* Task 2 freezes the future script write option spellings in
 * tests/fixtures/script_edit_reports.md. Keep CLI long options aligned with
 * Lua option-table fields through the direct kebab-case -> snake_case mapping.
 */

typedef struct script_command_common {
    bool dry_run;
    nmo_edit_report_t edit_report;
} script_command_common_t;

typedef nmo_status_t (*behavior_execute_cli_action_fn)(
    nmo_behavior_execution_t *execution,
    void *user_data);

typedef struct behavior_execute_cli_action_state {
    behavior_execute_cli_action_fn action;
    void *action_user_data;
} behavior_execute_cli_action_state_t;

nmo_workspace_t *script_execution_workspace(
    nmo_behavior_execution_t *execution);
nmo_status_t behavior_execute_cli_action_trampoline(
    nmo_behavior_execution_t *executor,
    void *user_data);
int behavior_execute_cli_run_write_command(
    const char *input_path,
    const char *output_path,
    bool dry_run,
    const nmo_cli_global_opts_t *global,
    const nmo_cli_write_spec_t *spec,
    const char *label,
    uint32_t validation_flags,
    behavior_execute_cli_action_fn action,
    nmo_cli_write_report_fn report,
    void *user_data,
    script_command_common_t *common);
bool script_record_str_or_null(nmo_cli_record_t *rec,
                               const char *key,
                               const char *value);
bool script_edit_report_json(yyjson_mut_doc *doc,
                             yyjson_mut_val *obj,
                             const void *data);
nmo_cli_record_t *script_report_new(nmo_cmd_ctx_t *ctx,
                                    script_command_common_t *common,
                                    bool dry_run,
                                    const char *output_path);
int script_report_emit(nmo_cmd_ctx_t *ctx,
                       nmo_cli_record_t *rec,
                       bool ok,
                       bool dry_run,
                       const char *output_path,
                       const char *cmd_name);
const char *script_manager_entry_policy_name(
    nmo_manager_entry_policy_t policy);
const char *script_manager_entry_schema_name(
    nmo_manager_entry_schema_t schema);
bool script_parse_manager_entry_policy_cli(
    const char *text,
    nmo_manager_entry_policy_t *out_policy);
bool script_parse_manager_entry_schema_cli(
    const char *text,
    nmo_manager_entry_schema_t *out_schema);
bool script_add_manager_entry(
    nmo_cli_record_t *rec,
    const nmo_manager_entry_options_t *manager_entry);
const char *script_interface_mode_string(
    nmo_script_edit_interface_mode_t mode);
bool parse_script_interface_mode(
    const char *text,
    nmo_script_edit_interface_mode_t *out_mode);
bool script_workspace_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id);
nmo_object_id_t script_interface_root_for_object_workspace(
    nmo_workspace_t *workspace,
    nmo_object_id_t object_id);
nmo_status_t script_execute_edit_plan(
    nmo_behavior_execution_t *executor,
    script_command_common_t *common,
    nmo_edit_plan_t *plan);
nmo_object_id_t script_common_result_id(
    const script_command_common_t *common,
    size_t operation_index);

#endif /* NMO_CMD_SCRIPT_INTERNAL_H */
