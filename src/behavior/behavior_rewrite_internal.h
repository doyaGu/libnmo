/**
 * @file behavior_rewrite_internal.h
 * @brief Internal declarations shared by the behavior rewrite translation units.
 */

#ifndef NMO_BEHAVIOR_REWRITE_INTERNAL_H
#define NMO_BEHAVIOR_REWRITE_INTERNAL_H

#include "behavior/nmo_behavior_edit.h"

#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_statesave_ids.h"
#include "../runtime/runtime_internal.h"

bool rewrite_is_behavior_object(
    nmo_context_t *ctx,
    const nmo_object_t *object);

nmo_behavior_state_t *rewrite_behavior_state(
    nmo_context_t *ctx,
    nmo_object_t *object);

nmo_status_t rewrite_fold_transform_anchor_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report,
    bool clear_graph_state);

uint32_t rewrite_fold_mapped_new_index(
    const nmo_behavior_fold_map_t *maps,
    size_t map_count,
    uint32_t old_index);

nmo_status_t rewrite_fold_anchor_io_at(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    nmo_object_id_t anchor_id,
    bool input,
    uint32_t index,
    nmo_object_id_t *out_io_id);

nmo_status_t rewrite_fold_anchor_parameter_at(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    nmo_object_id_t anchor_id,
    bool input,
    uint32_t index,
    nmo_object_id_t *out_parameter_id);

nmo_status_t rewrite_fold_rewire_control_boundary_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    nmo_behavior_fold_report_t *report);

nmo_status_t rewrite_fold_rewire_parameter_boundary_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    nmo_behavior_fold_report_t *report);

void rewrite_fold_report_reject(nmo_behavior_fold_report_t *report,
                                       const char *code,
                                       const char *message);

#endif /* NMO_BEHAVIOR_REWRITE_INTERNAL_H */
