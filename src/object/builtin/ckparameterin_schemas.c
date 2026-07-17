/**
 * @file ckparameterin_schemas.c
 * @brief CKParameterIn schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterIn.
 *
 * Based on official Virtools SDK (reference/src/CKParameterIn.cpp:140-250).
 */

#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/builtin/nmo_object_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(parameterin, nmo_parameterin_state_t)

static void nmo_parameterin_convert_legacy_guid(nmo_guid_t *guid) {
    if (guid == NULL) {
        return;
    }

    if (nmo_guid_equals(*guid, CKPGUID_OLDMESSAGE)) {
        *guid = CKPGUID_MESSAGE;
    } else if (nmo_guid_equals(*guid, CKPGUID_OLDATTRIBUTE)) {
        *guid = CKPGUID_ATTRIBUTE;
    } else if (nmo_guid_equals(*guid, CKPGUID_OLDTIME)) {
        *guid = CKPGUID_TIME;
    }
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterin_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterin_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_parameterin_state_t, type_guid, CKPGUID_GUID),
    NMO_FIELD_REF(nmo_parameterin_state_t, source),
    NMO_FIELD_REF(nmo_parameterin_state_t, owner),
    NMO_FIELD(nmo_parameterin_state_t, is_shared, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, is_disabled, CKPGUID_UINT8)
};

static int nmo_parameterin_is_parameter_class(
    nmo_class_id_t class_id,
    const nmo_type_registry_t *types)
{
    const nmo_class_id_t bases[] = {
        NMO_CID_PARAMETER,
        NMO_CID_PARAMETERIN,
        NMO_CID_PARAMETEROUT,
        NMO_CID_PARAMETERLOCAL,
        NMO_CID_PARAMETEROPERATION,
    };
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        if (class_id == bases[i] ||
            (types != NULL && nmo_type_registry_is_class_derived_from(
                types, (uint32_t)class_id, (uint32_t)bases[i]))) {
            return 1;
        }
    }
    return 0;
}

static void nmo_parameterin_check_source(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED || repository == NULL) {
        return;
    }
    const nmo_object_t *target =
        nmo_object_repository_find_by_id(repository, ref->id);
    if (target != NULL && !nmo_parameterin_is_parameter_class(
            nmo_object_get_class_id(target), types)) {
        ref->state = NMO_REF_CLASS_MISMATCH;
    }
}

/* =============================================================================
 * CKParameterIn DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterIn state from chunk
 *
 * Reference: reference/src/CKParameterIn.cpp:177-250
 */
static nmo_status_t nmo_parameterin_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_parameterin_state_t *out_state = (nmo_parameterin_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    nmo_guid_t type_guid = NMO_GUID_NULL;
    nmo_ref_t source = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ref_t owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    uint8_t is_shared = 0;
    uint8_t is_disabled = 0;

    if (data_version >= 1) {
        result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PARAMETERIN_DATASHARED);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &type_guid));
            nmo_parameterin_convert_legacy_guid(&type_guid);
            if (data_version < 5) {
                nmo_ref_t legacy = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &legacy));
            }
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
            is_shared = 1;
        } else if (result == NMO_ERR_NOT_FOUND) {
            result = nmo_chunk_seek_identifier(
                chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE);
            if (result == NMO_OK) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &type_guid));
                nmo_parameterin_convert_legacy_guid(&type_guid);
                if (data_version < 5) {
                    nmo_ref_t legacy = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &legacy));
                }
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
            } else if (result == NMO_ERR_NOT_FOUND) {
                result = nmo_chunk_seek_identifier(
                    chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
                if (result == NMO_OK) {
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(
                        chunk, &type_guid));
                    nmo_parameterin_convert_legacy_guid(&type_guid);

                    nmo_ref_t out_source =
                        nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                    nmo_ref_t parameter =
                        nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
                    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_source));
                    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &parameter));

                    if (nmo_ref_serialized_id(&out_source) !=
                        NMO_OBJECT_ID_NONE) {
                        source = out_source;
                    } else if (nmo_ref_serialized_id(&parameter) !=
                               NMO_OBJECT_ID_NONE) {
                        source = parameter;
                        is_shared = 1;
                    }
                } else if (result != NMO_ERR_NOT_FOUND) return result;
            } else return result;
        } else return result;
    } else {
        /* Legacy path: CK2 uses DEFAULTDATA/OWNER/INSHARED/OUTSOURCE */
        result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &type_guid));
            nmo_parameterin_convert_legacy_guid(&type_guid);
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PARAMETERIN_OWNER);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PARAMETERIN_INSHARED);
        if (result == NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
            is_shared = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        if (!is_shared) {
            result = nmo_chunk_seek_identifier(
                chunk, CK_STATESAVE_PARAMETERIN_OUTSOURCE);
            if (result == NMO_OK) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
            } else if (result != NMO_ERR_NOT_FOUND) return result;
        }
    }

    /* Check if disabled */
    result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
    if (result == NMO_OK) {
        is_disabled = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    nmo_ref_check_class(&owner, repository, types, NMO_CID_BEHAVIOR);
    nmo_parameterin_check_source(&source, repository, types);
    out_state->type_guid = type_guid;
    out_state->source = source;
    out_state->owner = owner;
    out_state->is_shared = is_shared;
    out_state->is_disabled = is_disabled;

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterin_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_parameterin_state_t *out_state =
        (nmo_parameterin_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_parameterin_state_t decoded = *out_state;
    nmo_status_t result = nmo_parameterin_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

/**
 * @brief Serialize CKParameterIn state to chunk
 *
 * Reference: reference/src/CKParameterIn.cpp:142-162
 */
static nmo_status_t nmo_parameterin_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameterin_state_t *in_state = (const nmo_parameterin_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Write identifier based on shared/direct source */
    const uint32_t data_version = nmo_chunk_get_data_version(out_chunk);

    if (data_version < 1) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
        if (result != NMO_OK) return result;

        if (nmo_ref_serialized_id(&in_state->owner) != NMO_OBJECT_ID_NONE) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_OWNER);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->owner);
            if (result != NMO_OK) return result;
        }

        if (in_state->is_shared) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_INSHARED);
            if (result != NMO_OK) return result;
        } else {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_OUTSOURCE);
            if (result != NMO_OK) return result;
        }

        result = nmo_ref_write(out_chunk, &in_state->source);
        if (result != NMO_OK) return result;

        if (in_state->is_disabled) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
            if (result != NMO_OK) return result;
        }

        NMO_RETURN_OK();
    }

    if (nmo_ref_serialized_id(&in_state->owner) != NMO_OBJECT_ID_NONE) {
        const nmo_ref_t none = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->owner);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(
            out_chunk, in_state->is_shared ? &none : &in_state->source);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(
            out_chunk, in_state->is_shared ? &in_state->source : &none);
        if (result != NMO_OK) return result;
        if (in_state->is_disabled) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
            if (result != NMO_OK) return result;
        }
        NMO_RETURN_OK();
    }

    /* Write identifier based on shared/direct source */
    uint32_t identifier = in_state->is_shared
        ? CK_STATESAVE_PARAMETERIN_DATASHARED
        : CK_STATESAVE_PARAMETERIN_DATASOURCE;

    result = nmo_chunk_write_identifier(out_chunk, identifier);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
    if (result != NMO_OK) return result;

    if (data_version < 5) {
        result = nmo_ref_write(out_chunk, &in_state->source);
        if (result != NMO_OK) return result;
    }

    result = nmo_ref_write(out_chunk, &in_state->source);
    if (result != NMO_OK) return result;

    /* Write disabled flag if needed */
    if (in_state->is_disabled) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_parameterin_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_parameterin_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_parameterin_validate(instance, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_parameterin_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_parameterin_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterin_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterin_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterin_remap_dependencies");
    }

    nmo_parameterin_state_t *state = (nmo_parameterin_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    nmo_parameterin_convert_legacy_guid(&state->type_guid);

    /* Preserve unresolved source/owner references and authored flags. */
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_parameterin_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterin_pre_delete");
    }
    nmo_parameterin_state_t *state = (nmo_parameterin_state_t *)instance;
    state->source = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

static void nmo_parameterin_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS(parameterin, nmo_parameterin_state_t)

nmo_type_vtable_t nmo_parameterin_vtable = {
    .prepare_dependencies = nmo_parameterin_prepare_dependencies,
    .remap_dependencies = nmo_parameterin_remap_dependencies,
    .pre_delete = nmo_parameterin_pre_delete,
    .post_delete = nmo_parameterin_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_parameterin_create,
        nmo_parameterin_destroy,
        nmo_parameterin_serialize,
        nmo_parameterin_deserialize,
        nmo_parameterin_copy,
        nmo_parameterin_validate,
        nmo_parameterin_equals,
        nmo_parameterin_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_parameterin_type,
    CKPGUID_PARAMETERIN,
    "CKParameterIn",
    NMO_CID_PARAMETERIN,
    CKPGUID_OBJECT,
    nmo_parameterin_state_t,
    &nmo_parameterin_vtable,
    nmo_parameterin_fields)






