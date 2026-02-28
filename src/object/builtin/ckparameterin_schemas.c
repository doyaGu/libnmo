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
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "session/nmo_object_repository.h"
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

static bool nmo_parameterin_is_valid_target(
    void *context,
    nmo_object_id_t object_id,
    nmo_class_id_t expected_base)
{
    if (object_id == 0) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_deserialize_context_get_type_registry(context);
    nmo_object_repository_t *repo = (nmo_object_repository_t *)
        nmo_deserialize_context_get_repository(context);

    if (repo == NULL || registry == NULL) {
        return true;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (obj == NULL) {
        return false;
    }

    return nmo_type_registry_is_class_derived_from(
        registry,
        (uint32_t)nmo_object_get_class_id(obj),
        (uint32_t)expected_base);
}

static bool nmo_parameterin_is_valid_owner(
    void *context,
    nmo_object_id_t object_id)
{
    if (object_id == 0) {
        return false;
    }

    const nmo_type_registry_t *registry = nmo_deserialize_context_get_type_registry(context);
    nmo_object_repository_t *repo = (nmo_object_repository_t *)
        nmo_deserialize_context_get_repository(context);

    if (repo == NULL || registry == NULL) {
        return true;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (obj == NULL) {
        return false;
    }

    const uint32_t class_id = (uint32_t)nmo_object_get_class_id(obj);
    return nmo_type_registry_is_class_derived_from(
        registry, class_id, (uint32_t)NMO_CID_BEHAVIOR) ||
        nmo_type_registry_is_class_derived_from(
            registry, class_id, (uint32_t)NMO_CID_PARAMETEROPERATION);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterin_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterin_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_parameterin_state_t, type_guid, CKPGUID_GUID),
    NMO_FIELD_REF(nmo_parameterin_state_t, source_id),
    NMO_FIELD_REF(nmo_parameterin_state_t, owner_id),
    NMO_FIELD(nmo_parameterin_state_t, is_shared, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterin_state_t, is_disabled, CKPGUID_UINT8)
};

/* =============================================================================
 * CKParameterIn DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterIn state from chunk
 *
 * Reference: reference/src/CKParameterIn.cpp:177-250
 */
nmo_status_t nmo_parameterin_deserialize(
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

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (data_version >= 1) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASHARED) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            nmo_parameterin_convert_legacy_guid(&out_state->type_guid);
            if (data_version < 5) {
                nmo_object_id_t legacy_id = 0;
                (void)nmo_chunk_read_object_id(chunk, &legacy_id);
            }
            nmo_object_id_t shared_id = 0;
            nmo_chunk_read_object_id(chunk, &shared_id);
            if (nmo_parameterin_is_valid_target(context, shared_id, NMO_CID_PARAMETERIN)) {
                out_state->source_id = shared_id;
                out_state->is_shared = 1;
            } else {
                out_state->source_id = 0;
                out_state->is_shared = 0;
            }
        } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            nmo_parameterin_convert_legacy_guid(&out_state->type_guid);
            if (data_version < 5) {
                nmo_object_id_t legacy_id = 0;
                (void)nmo_chunk_read_object_id(chunk, &legacy_id);
            }
            nmo_object_id_t source_id = 0;
            nmo_chunk_read_object_id(chunk, &source_id);
            if (nmo_parameterin_is_valid_target(context, source_id, NMO_CID_PARAMETER)) {
                out_state->source_id = source_id;
                out_state->is_shared = 0;
            } else {
                out_state->source_id = 0;
                out_state->is_shared = 0;
            }
        } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            nmo_parameterin_convert_legacy_guid(&out_state->type_guid);

            nmo_object_id_t owner_id = 0;
            nmo_object_id_t out_source_id = 0;
            nmo_object_id_t param_id = 0;
            (void)nmo_chunk_read_object_id(chunk, &owner_id);
            (void)nmo_chunk_read_object_id(chunk, &out_source_id);
            (void)nmo_chunk_read_object_id(chunk, &param_id);

            if (nmo_parameterin_is_valid_owner(context, owner_id)) {
                out_state->owner_id = owner_id;
            } else {
                out_state->owner_id = 0;
            }

            if (nmo_parameterin_is_valid_target(context, out_source_id, NMO_CID_PARAMETER)) {
                out_state->source_id = out_source_id;
                out_state->is_shared = 0;
            } else if (nmo_parameterin_is_valid_target(context, param_id, NMO_CID_PARAMETERIN)) {
                out_state->source_id = param_id;
                out_state->is_shared = 1;
            } else {
                out_state->source_id = 0;
                out_state->is_shared = 0;
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DISABLED) == NMO_OK) {
            out_state->is_disabled = 1;
        }
    } else {
        /* Legacy path: CK2 uses DEFAULTDATA/OWNER/INSHARED/OUTSOURCE */
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DEFAULTDATA) == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->type_guid);
            nmo_parameterin_convert_legacy_guid(&out_state->type_guid);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_OWNER) == NMO_OK) {
            nmo_object_id_t owner_id = 0;
            (void)nmo_chunk_read_object_id(chunk, &owner_id);
            if (nmo_parameterin_is_valid_owner(context, owner_id)) {
                out_state->owner_id = owner_id;
            } else {
                out_state->owner_id = 0;
            }
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_INSHARED) == NMO_OK) {
            nmo_object_id_t shared_id = 0;
            nmo_chunk_read_object_id(chunk, &shared_id);
            if (nmo_parameterin_is_valid_target(context, shared_id, NMO_CID_PARAMETERIN)) {
                out_state->source_id = shared_id;
                out_state->is_shared = 1;
            }
        }
        if (!out_state->is_shared &&
            nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_OUTSOURCE) == NMO_OK) {
            nmo_object_id_t source_id = 0;
            nmo_chunk_read_object_id(chunk, &source_id);
            if (nmo_parameterin_is_valid_target(context, source_id, NMO_CID_PARAMETER)) {
                out_state->source_id = source_id;
            }
        }
    }

    /* Check if disabled */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DISABLED) == NMO_OK) {
        out_state->is_disabled = 1;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize CKParameterIn state to chunk
 *
 * Reference: reference/src/CKParameterIn.cpp:142-162
 */
nmo_status_t nmo_parameterin_serialize(
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

        if (in_state->owner_id != 0) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_OWNER);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->owner_id);
            if (result != NMO_OK) return result;
        }

        if (in_state->is_shared) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_INSHARED);
            if (result != NMO_OK) return result;
        } else {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_OUTSOURCE);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_id(out_chunk, in_state->source_id);
        if (result != NMO_OK) return result;

        if (in_state->is_disabled) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
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
        result = nmo_chunk_write_object_id(out_chunk, in_state->source_id);
        if (result != NMO_OK) return result;
    }

    result = nmo_chunk_write_object_id(out_chunk, in_state->source_id);
    if (result != NMO_OK) return result;

    /* Write disabled flag if needed */
    if (in_state->is_disabled) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    parameterin,
    nmo_parameterin_state_t,
    nmo_parameterin_serialize,
    nmo_parameterin_deserialize,
    nmo_parameterin_fields,
    CKPGUID_PARAMETERIN,
    "CKParameterIn",
    NMO_CID_PARAMETERIN,
    CKPGUID_OBJECT
)


