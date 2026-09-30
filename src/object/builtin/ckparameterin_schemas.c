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
#include "type/nmo_type_query.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>

static void nmo_parameterin_set_defaults(void *instance)
{
    nmo_parameterin_state_t *state = instance;
    state->has_data = 1;
    state->has_source = 1;
}

static const nmo_object_state_member_t nmo_parameterin_members[] = {
    NMO_STATE_VALUE(nmo_parameterin_state_t, type_guid),
    NMO_STATE_VALUE(nmo_parameterin_state_t, legacy_prefix_ref),
    NMO_STATE_VALUE(nmo_parameterin_state_t, source),
    NMO_STATE_VALUE(nmo_parameterin_state_t, owner),
    NMO_STATE_VALUE(nmo_parameterin_state_t, is_shared),
    NMO_STATE_VALUE(nmo_parameterin_state_t, is_disabled),
    NMO_STATE_VALUE(nmo_parameterin_state_t, has_legacy_layout),
    NMO_STATE_VALUE(nmo_parameterin_state_t, has_data),
    NMO_STATE_VALUE(nmo_parameterin_state_t, has_owner),
    NMO_STATE_VALUE(nmo_parameterin_state_t, has_source)
};

static const nmo_object_state_layout_t nmo_parameterin_layout = {
    .size = sizeof(nmo_parameterin_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_parameterin_members,
    .member_count =
        sizeof(nmo_parameterin_members) / sizeof(nmo_parameterin_members[0]),
    .set_defaults = nmo_parameterin_set_defaults,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(parameterin, nmo_parameterin_layout)

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
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_parameterin_state_t, type_guid, CKPGUID_GUID),
    NMO_FIELD_REF_VALUE(nmo_parameterin_state_t, legacy_prefix_ref),
    NMO_FIELD_REF_VALUE(nmo_parameterin_state_t, source),
    NMO_FIELD_REF_VALUE(nmo_parameterin_state_t, owner),
    NMO_FIELD(nmo_parameterin_state_t, is_shared, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, is_disabled, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, has_legacy_layout, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, has_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, has_owner, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, has_source, CKPGUID_UINT8)
};

static void nmo_parameterin_check_source(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types,
    uint8_t is_shared)
{
    nmo_ref_check_class(
        ref, repository, types,
        is_shared ? NMO_CID_PARAMETERIN : NMO_CID_PARAMETER);
}

static void nmo_parameterin_check_owner(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED ||
        repository == NULL || types == NULL) {
        return;
    }
    const nmo_object_t *target =
        nmo_object_repository_find_by_id(repository, ref->id);
    if (target != NULL &&
        !nmo_type_query_object_is_derived_from_class(
            types, target, NMO_CID_BEHAVIOR) &&
        !nmo_type_query_object_is_derived_from_class(
            types, target, NMO_CID_PARAMETEROPERATION)) {
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
    nmo_ref_t legacy_prefix_ref =
        nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ref_t source = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ref_t owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    uint8_t is_shared = 0;
    uint8_t is_disabled = 0;
    uint8_t has_data = 0;
    uint8_t has_owner = 0;
    uint8_t has_source = 0;
    size_t section_dwords = 0;

    if (data_version >= 1) {
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_PARAMETERIN_DATASHARED,
            &section_dwords);
        if (result == NMO_OK) {
            const size_t expected_dwords = data_version < 5 ? 4u : 3u;
            if (section_dwords < expected_dwords) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            if (section_dwords > expected_dwords) {
                return NMO_ERR_INVALID_FORMAT;
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &type_guid));
            nmo_parameterin_convert_legacy_guid(&type_guid);
            if (data_version < 5) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(
                    chunk, &legacy_prefix_ref));
            }
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
            is_shared = 1;
            has_data = 1;
            has_source = 1;
        } else if (result == NMO_ERR_NOT_FOUND) {
            result = nmo_chunk_seek_identifier_with_size(
                chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE,
                &section_dwords);
            if (result == NMO_OK) {
                const size_t expected_dwords = data_version < 5 ? 4u : 3u;
                if (section_dwords < expected_dwords) {
                    return NMO_ERR_TRUNCATED_CHUNK;
                }
                if (section_dwords > expected_dwords) {
                    return NMO_ERR_INVALID_FORMAT;
                }
                NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &type_guid));
                nmo_parameterin_convert_legacy_guid(&type_guid);
                if (data_version < 5) {
                    NMO_RETURN_IF_ERROR(nmo_ref_read(
                        chunk, &legacy_prefix_ref));
                }
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
                has_data = 1;
                has_source = 1;
            } else if (result == NMO_ERR_NOT_FOUND) {
                result = nmo_chunk_seek_identifier_with_size(
                    chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA,
                    &section_dwords);
                if (result == NMO_OK) {
                    if (section_dwords < 5u) {
                        return NMO_ERR_TRUNCATED_CHUNK;
                    }
                    if (section_dwords > 5u) {
                        return NMO_ERR_INVALID_FORMAT;
                    }
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
                    has_data = 1;
                    has_owner = 1;
                    has_source = 1;

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
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA,
            &section_dwords);
        if (result == NMO_OK) {
            if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
            if (section_dwords > 2u) return NMO_ERR_INVALID_FORMAT;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_guid(chunk, &type_guid));
            nmo_parameterin_convert_legacy_guid(&type_guid);
            has_data = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_PARAMETERIN_OWNER, &section_dwords);
        if (result == NMO_OK) {
            if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
            has_owner = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_PARAMETERIN_INSHARED,
            &section_dwords);
        if (result == NMO_OK) {
            if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
            is_shared = 1;
            has_source = 1;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
        if (!is_shared) {
            result = nmo_chunk_seek_identifier_with_size(
                chunk, CK_STATESAVE_PARAMETERIN_OUTSOURCE,
                &section_dwords);
            if (result == NMO_OK) {
                if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
                if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
                NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &source));
                has_source = 1;
            } else if (result != NMO_ERR_NOT_FOUND) return result;
        }
    }

    /* Check if disabled */
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETERIN_DISABLED, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords != 0u) return NMO_ERR_INVALID_FORMAT;
        is_disabled = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    nmo_parameterin_check_owner(&owner, repository, types);
    nmo_parameterin_check_source(
        &source, repository, types, is_shared);
    out_state->type_guid = type_guid;
    out_state->legacy_prefix_ref = legacy_prefix_ref;
    out_state->source = source;
    out_state->owner = owner;
    out_state->is_shared = is_shared;
    out_state->is_disabled = is_disabled;
    out_state->has_legacy_layout = data_version < 1u;
    out_state->has_data = has_data;
    out_state->has_owner = has_owner;
    out_state->has_source = has_source;

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
    const nmo_serialize_context_t *ser_ctx =
        nmo_serialize_context_try(context);
    const bool is_file =
        ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL &&
         (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    uint32_t data_version = nmo_chunk_get_data_version(out_chunk);
    const bool legacy_layout =
        data_version == 0u && in_state->has_legacy_layout;
    if (data_version == 0u && !legacy_layout) {
        out_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
        data_version = out_chunk->data_version;
    }
    const bool has_owner = in_state->has_owner ||
        nmo_ref_serialized_id(&in_state->owner) != NMO_OBJECT_ID_NONE;
    const bool has_source = in_state->has_source ||
        nmo_ref_serialized_id(&in_state->source) != NMO_OBJECT_ID_NONE;
    const bool has_data = is_file || in_state->has_data || has_owner || has_source;
    const bool writes_owner_layout =
        !is_file && data_version >= 1u && has_owner;
    const bool stores_legacy_prefix =
        data_version >= 1u && data_version < 5u &&
        !writes_owner_layout && has_data;
    if (!stores_legacy_prefix &&
        nmo_ref_serialized_id(&in_state->legacy_prefix_ref) !=
            NMO_OBJECT_ID_NONE) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "ParameterIn layout cannot store the legacy prefix reference");
    }

    result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Write identifier based on shared/direct source */
    if (data_version < 1) {
        if (has_data) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
            if (result != NMO_OK) return result;
        }

        if (has_owner) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_OWNER);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->owner);
            if (result != NMO_OK) return result;
        }

        if (has_source) {
            if (in_state->is_shared) {
                result = nmo_chunk_write_identifier(
                    out_chunk, CK_STATESAVE_PARAMETERIN_INSHARED);
            } else {
                result = nmo_chunk_write_identifier(
                    out_chunk, CK_STATESAVE_PARAMETERIN_OUTSOURCE);
            }
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->source);
            if (result != NMO_OK) return result;
        }

        if (in_state->is_disabled) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
            if (result != NMO_OK) return result;
        }

        NMO_RETURN_OK();
    }

    if (writes_owner_layout) {
        const nmo_ref_t none = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->owner);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(
            out_chunk,
            has_source && !in_state->is_shared ? &in_state->source : &none);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(
            out_chunk,
            has_source && in_state->is_shared ? &in_state->source : &none);
        if (result != NMO_OK) return result;
        if (in_state->is_disabled) {
            result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
            if (result != NMO_OK) return result;
        }
        NMO_RETURN_OK();
    }

    if (!has_data) {
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
        result = nmo_ref_write(out_chunk, &in_state->legacy_prefix_ref);
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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_VALIDATED(nmo_parameterin)

NMO_DEFINE_OBJECT_PREPARE_CHECKED(nmo_parameterin)

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
    state->legacy_prefix_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static nmo_status_t nmo_parameterin_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_parameterin_state_t *state = instance;
    return nmo_object_vtable.validate(&state->base, NULL, context);
}

nmo_type_vtable_t nmo_parameterin_vtable = {
    .prepare_dependencies = nmo_parameterin_prepare_dependencies,
    .remap_dependencies = nmo_parameterin_remap_dependencies,
    .pre_delete = nmo_parameterin_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
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






