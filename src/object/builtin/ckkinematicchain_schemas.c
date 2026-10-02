/**
 * @file ckkinematicchain_schemas.c
 * @brief CKKinematicChain schema implementation
 */

#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_deserialize_context.h"
#include "type/nmo_reflection.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include <string.h>

static const nmo_object_state_member_t nmo_kinematicchain_members[] = {
    NMO_STATE_VALUE(nmo_kinematicchain_state_t, has_chain_data),
    NMO_STATE_VALUE(nmo_kinematicchain_state_t, reserved_ref),
    NMO_STATE_VALUE(nmo_kinematicchain_state_t, start_effector),
    NMO_STATE_VALUE(nmo_kinematicchain_state_t, end_effector)
};

/* CKKinematicChain::Save writes the chain in one section: a reserved reference,
 * then the start and the end effector (body parts). */
static const nmo_object_section_field_t nmo_kinematicchain_chain_fields[] = {
    NMO_SECTION_FIELD_REF(nmo_kinematicchain_state_t, reserved_ref, 0),
    NMO_SECTION_FIELD_REF(nmo_kinematicchain_state_t, start_effector, NMO_CID_BODYPART),
    NMO_SECTION_FIELD_REF(nmo_kinematicchain_state_t, end_effector, NMO_CID_BODYPART)
};

/* A longer section is read as far as its fields go. */
static const nmo_object_section_t nmo_kinematicchain_section_list[] = {
    NMO_SECTION(CK_STATESAVE_KINEMATICCHAINALL, nmo_kinematicchain_state_t,
                has_chain_data, 0, NMO_OBJECT_SECTION_ALLOW_LONGER,
                nmo_kinematicchain_chain_fields)
};

static const nmo_object_sections_t nmo_kinematicchain_sections = {
    NMO_SECTION_LIST(nmo_kinematicchain_section_list),
    .non_file_save_flags = CK_STATESAVE_KINEMATICCHAINALL,
};

static const nmo_object_state_layout_t nmo_kinematicchain_layout = {
    .size = sizeof(nmo_kinematicchain_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_kinematicchain_members,
    .member_count =
        sizeof(nmo_kinematicchain_members) / sizeof(nmo_kinematicchain_members[0]),
    .sections = &nmo_kinematicchain_sections,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(kinematicchain, nmo_kinematicchain_layout)

#include <stddef.h>
#include <stdalign.h>

static const nmo_type_field_t nmo_kinematicchain_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_kinematicchain_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_kinematicchain_state_t, has_chain_data, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_kinematicchain_state_t, reserved_ref),
    NMO_FIELD_REF_VALUE(nmo_kinematicchain_state_t, start_effector),
    NMO_FIELD_REF_VALUE(nmo_kinematicchain_state_t, end_effector)
};

NMO_DEFINE_OBJECT_PREPARE_DEFAULT(nmo_kinematicchain)

nmo_status_t nmo_kinematicchain_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_kinematicchain_remap_dependencies");
    }

    nmo_kinematicchain_state_t *state = (nmo_kinematicchain_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    /* Preserve chain section presence and unresolved serialized values. */
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_kinematicchain_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_kinematicchain_pre_delete");
    }
    nmo_kinematicchain_state_t *state = instance;
    state->start_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->end_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_kinematicchain, nmo_kinematicchain_state_t, base, nmo_object_vtable)

NMO_DEFINE_OBJECT_LAYOUT_SERDE(nmo_kinematicchain, nmo_kinematicchain_layout, nmo_kinematicchain_validate)

nmo_type_vtable_t nmo_kinematicchain_vtable = {
    .prepare_dependencies = nmo_kinematicchain_prepare_dependencies,
    .remap_dependencies = nmo_kinematicchain_remap_dependencies,
    .pre_delete = nmo_kinematicchain_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_kinematicchain_create,
        nmo_kinematicchain_destroy,
        nmo_kinematicchain_serialize,
        nmo_kinematicchain_deserialize,
        nmo_kinematicchain_copy,
        nmo_kinematicchain_validate,
        nmo_kinematicchain_equals,
        nmo_kinematicchain_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_kinematicchain_type,
    CKPGUID_KINEMATICCHAIN,
    "CKKinematicChain",
    NMO_CID_KINEMATICCHAIN,
    CKPGUID_OBJECT,
    nmo_kinematicchain_state_t,
    &nmo_kinematicchain_vtable,
    nmo_kinematicchain_fields)

