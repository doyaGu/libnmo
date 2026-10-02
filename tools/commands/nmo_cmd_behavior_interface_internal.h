/**
 * @file nmo_cmd_behavior_interface_internal.h
 * @brief Types, macros and helpers the behavior interface command files share.
 */

#ifndef NMO_CMD_BEHAVIOR_INTERFACE_INTERNAL_H
#define NMO_CMD_BEHAVIOR_INTERFACE_INTERNAL_H

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"
#include "nmo.h"
#include "edit/nmo_behavior_edit.h"
#include "edit/nmo_script_edit.h"
#include "object/nmo_context.h"
#include "core/nmo_array.h"
#include "core/nmo_parse.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_edit.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Interface edit: verb handlers (now public sub-action handlers)
 * ================================================================ */

enum { IFACE_SELECTOR_ARGV_CAP = 64 };

typedef struct iface_resolving_write_args {
    const nmo_core_object_selector_t *selector;
    uint32_t *target_id;
    void *payload;
    nmo_cli_write_mutate_fn mutate;
    nmo_cli_write_report_fn report;
    const char *usage;
} iface_resolving_write_args_t;

#define IFACE_STRIP_TARGET_SELECTOR_ARGS()                                      \
    char *parse_argv[IFACE_SELECTOR_ARGV_CAP];                                  \
    int parse_argc = 0;                                                         \
    nmo_core_object_selector_t option_selector = {0};                           \
    int selector_rc = iface_strip_target_selector_args(                         \
        argc, argv, &parse_argc, parse_argv, IFACE_SELECTOR_ARGV_CAP,           \
        &option_selector);                                                      \
    if (selector_rc != NMO_CLI_EXIT_SUCCESS) return selector_rc;                \
    argc = parse_argc;                                                          \
    argv = parse_argv

const char *iface_root_kind_name(const nmo_interface_data_t *idata);
bool iface_is_sectioned(const nmo_interface_data_t *idata);
bool iface_color_is_present(const nmo_interface_data_t *idata);
nmo_interface_data_t *iface_edit_get_data(
    nmo_cmd_ctx_t *c, uint32_t target_id,
    nmo_object_t **out_obj);
bool iface_validate_behavior_id(nmo_cmd_ctx_t *c, uint32_t beh_id);
bool iface_parse_f32_arg(const char *text, float *out_value);
int iface_mark_changed(nmo_cmd_ctx_t *c, nmo_object_id_t target_id);
nmo_cli_record_t *iface_report_new(bool dry_run);
int iface_report_emit(
    nmo_cmd_ctx_t *c,
    nmo_cli_record_t *rec,
    bool ok,
    bool dry_run,
    const char *output_path,
    const char *command);
bool iface_option_consumes_next_arg(const char *arg);
int iface_strip_target_selector_args(
    int argc,
    char **argv,
    int *out_argc,
    char **out_argv,
    size_t out_capacity,
    nmo_core_object_selector_t *out_selector);
int iface_prepare_target_selector(
    const nmo_core_object_selector_t *option_selector,
    const nmo_opt_result_t *parse_result,
    int positional_tail_count,
    const char *usage,
    nmo_core_object_selector_t *out_selector,
    int *out_value_offset,
    const char **out_file_path);
int iface_resolve_then_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data);
int iface_resolved_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data);
int iface_run_resolved_write_command(
    const char *file_path,
    const char *output_path,
    bool dry_run,
    const nmo_cli_global_opts_t *global,
    const nmo_cli_write_spec_t *spec,
    const nmo_core_object_selector_t *selector,
    uint32_t *target_id,
    void *payload,
    nmo_cli_write_mutate_fn mutate,
    nmo_cli_write_report_fn report,
    const char *usage);

/* Sub-action handlers of the table in nmo_cmd_behavior_interface.c that
   nmo_cmd_behavior.h does not declare. */
int nmo_cmd_behavior_iface_set_comment_text(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_move_comment(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_comment_style(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_add_point(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_clear_points(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_remove_point(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_move_point(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_link_highlight(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_move_op(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_move_param(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_param_style(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_resize(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_expand(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_viewport(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_graph_io(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_translate(int argc, char **argv, const nmo_cli_global_opts_t *global);

#endif /* NMO_CMD_BEHAVIOR_INTERFACE_INTERNAL_H */
