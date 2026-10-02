/**
 * @file ckbehaviorlink_schemas.c
 * @brief CKBehaviorLink schema implementation
 *
 * Implements schema-driven deserialization for CKBehaviorLink (behavior graph connections).
 * CKBehaviorLink extends CKObject and stores timing delays plus I/O endpoint references.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorLink.cpp:49-121).
 */

#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_behaviorlink_set_defaults(void *instance)
{
    nmo_behaviorlink_state_t *state = instance;
    state->activation_delay = 1;
    state->initial_activation_delay = 1;
    state->has_format = true;
    state->use_new_format = true;
}

static const nmo_object_state_member_t nmo_behaviorlink_members[] = {
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, activation_delay),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, initial_activation_delay),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, in_io),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, out_io),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, has_format),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, use_new_format),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, has_legacy_curdelay),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, has_legacy_ios),
    NMO_STATE_VALUE(nmo_behaviorlink_state_t, has_legacy_delay)
};

/* CKBehaviorLink::Save writes the new layout: both delays in one dword (the
 * activation delay in the low half), then the two ends. A file of the legacy
 * layout holds the activation delay, the ends and the initial delay in sections
 * of their own, which are read only when the new section is absent. */
static const nmo_object_section_field_t nmo_behaviorlink_new_fields[] = {
    NMO_SECTION_FIELD_INT16_PAIR(nmo_behaviorlink_state_t, activation_delay,
                                 initial_activation_delay),
    NMO_SECTION_FIELD_REF(nmo_behaviorlink_state_t, in_io, NMO_CID_BEHAVIORIO),
    NMO_SECTION_FIELD_REF(nmo_behaviorlink_state_t, out_io, NMO_CID_BEHAVIORIO)
};
static const nmo_object_section_field_t nmo_behaviorlink_curdelay_fields[] = {
    NMO_SECTION_FIELD_INT16_AS_INT(nmo_behaviorlink_state_t, activation_delay)
};
static const nmo_object_section_field_t nmo_behaviorlink_ios_fields[] = {
    NMO_SECTION_FIELD_REF(nmo_behaviorlink_state_t, in_io, NMO_CID_BEHAVIORIO),
    NMO_SECTION_FIELD_REF(nmo_behaviorlink_state_t, out_io, NMO_CID_BEHAVIORIO)
};
static const nmo_object_section_field_t nmo_behaviorlink_delay_fields[] = {
    NMO_SECTION_FIELD_INT16_AS_INT(nmo_behaviorlink_state_t, initial_activation_delay)
};

static const nmo_object_section_t nmo_behaviorlink_section_list[] = {
    NMO_SECTION(CK_STATESAVE_BEHAV_LINK_NEWDATA, nmo_behaviorlink_state_t,
                use_new_format, 0, 0, nmo_behaviorlink_new_fields),
    NMO_SECTION(CK_STATESAVE_BEHAV_LINK_CURDELAY, nmo_behaviorlink_state_t,
                has_legacy_curdelay, 1, 0, nmo_behaviorlink_curdelay_fields),
    NMO_SECTION(CK_STATESAVE_BEHAV_LINK_IOS, nmo_behaviorlink_state_t,
                has_legacy_ios, 1, 0, nmo_behaviorlink_ios_fields),
    NMO_SECTION(CK_STATESAVE_BEHAV_LINK_DELAY, nmo_behaviorlink_state_t,
                has_legacy_delay, 1, 0, nmo_behaviorlink_delay_fields)
};

static const nmo_object_sections_t nmo_behaviorlink_sections = {
    NMO_SECTION_LIST(nmo_behaviorlink_section_list),
    .any_present_offset = offsetof(nmo_behaviorlink_state_t, has_format),
    .non_file_save_flags = CK_STATESAVE_BEHAV_LINKONLY,
};

static const nmo_object_state_layout_t nmo_behaviorlink_layout = {
    .size = sizeof(nmo_behaviorlink_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_behaviorlink_members,
    .member_count =
        sizeof(nmo_behaviorlink_members) / sizeof(nmo_behaviorlink_members[0]),
    .set_defaults = nmo_behaviorlink_set_defaults,
    .sections = &nmo_behaviorlink_sections,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(behaviorlink, nmo_behaviorlink_layout)

static nmo_status_t nmo_behaviorlink_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DEFINE_OBJECT_LAYOUT_SERDE(nmo_behaviorlink, nmo_behaviorlink_layout, nmo_behaviorlink_validate)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_behaviorlink_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_behaviorlink_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_behaviorlink_state_t, activation_delay, CKPGUID_INT16),
    NMO_FIELD(nmo_behaviorlink_state_t, initial_activation_delay, CKPGUID_INT16),
    NMO_FIELD_REF_VALUE(nmo_behaviorlink_state_t, in_io),
    NMO_FIELD_REF_VALUE(nmo_behaviorlink_state_t, out_io),
    NMO_FIELD(nmo_behaviorlink_state_t, has_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, use_new_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, has_legacy_curdelay, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, has_legacy_ios, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, has_legacy_delay, CKPGUID_BOOL)
};

/* =============================================================================
 * CKBehaviorLink DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehaviorLink state from chunk
 * 
 * Implements the symmetric read operation for CKBehaviorLink::Load.
 * Reads activation delays and I/O endpoint references.
 * Supports both new format (NEWDATA) and legacy format (CURDELAY + IOS).
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:73-121
 * 
 * @param chunk Chunk containing CKBehaviorLink data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
/* =============================================================================
 * CKBehaviorLink SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehaviorLink state to chunk
 * 
 * Implements the symmetric write operation for CKBehaviorLink::Save.
 * Writes activation delays and I/O endpoint references in new format.
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:49-71
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_behaviorlink_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behaviorlink_remap_dependencies");
    }

    nmo_behaviorlink_state_t *state = (nmo_behaviorlink_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    /* Preserve link endpoints and authored delays verbatim. */
    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_PREPARE_CHECKED(nmo_behaviorlink)

static nmo_status_t nmo_behaviorlink_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behaviorlink_pre_delete");
    }

    nmo_behaviorlink_state_t *state = (nmo_behaviorlink_state_t *)instance;
    state->in_io = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->out_io = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static nmo_status_t nmo_behaviorlink_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_behaviorlink_state_t *state = instance;
    NMO_RETURN_IF_ERROR(nmo_object_vtable.validate(
        &state->base, NULL, context));

    const bool has_in_io =
        nmo_ref_serialized_id(&state->in_io) != NMO_OBJECT_ID_NONE;
    const bool has_out_io =
        nmo_ref_serialized_id(&state->out_io) != NMO_OBJECT_ID_NONE;
    if (!state->has_format) {
        if (state->use_new_format || state->has_legacy_curdelay ||
            state->has_legacy_ios || state->has_legacy_delay ||
            state->activation_delay != 1 ||
            state->initial_activation_delay != 1 ||
            has_in_io || has_out_io) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "BehaviorLink data is present without a format section");
        }
        NMO_RETURN_OK();
    }
    if (state->use_new_format) {
        if (state->has_legacy_curdelay || state->has_legacy_ios ||
            state->has_legacy_delay) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "BehaviorLink new and legacy layouts are mixed");
        }
        NMO_RETURN_OK();
    }
    if (!state->has_legacy_curdelay && !state->has_legacy_ios &&
        !state->has_legacy_delay) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "BehaviorLink legacy layout has no sections");
    }
    if ((!state->has_legacy_curdelay && state->activation_delay != 1) ||
        (!state->has_legacy_delay &&
         state->initial_activation_delay != 1) ||
        (!state->has_legacy_ios && (has_in_io || has_out_io))) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "BehaviorLink legacy state cannot be serialized losslessly");
    }
    NMO_RETURN_OK();
}

nmo_type_vtable_t nmo_behaviorlink_vtable = {
    .prepare_dependencies = nmo_behaviorlink_prepare_dependencies,
    .remap_dependencies = nmo_behaviorlink_remap_dependencies,
    .pre_delete = nmo_behaviorlink_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_behaviorlink_create,
        nmo_behaviorlink_destroy,
        nmo_behaviorlink_serialize,
        nmo_behaviorlink_deserialize,
        nmo_behaviorlink_copy,
        nmo_behaviorlink_validate,
        nmo_behaviorlink_equals,
        nmo_behaviorlink_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_behaviorlink_type,
    CKPGUID_BEHAVIORLINK,
    "CKBehaviorLink",
    NMO_CID_BEHAVIORLINK,
    CKPGUID_OBJECT,
    nmo_behaviorlink_state_t,
    &nmo_behaviorlink_vtable,
    nmo_behaviorlink_fields)







