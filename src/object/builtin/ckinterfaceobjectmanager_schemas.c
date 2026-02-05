/**
 * @file ckinterfaceobjectmanager_schemas.c
 * @brief CKInterfaceObjectManager schema implementation
 */

#include "object/nmo_ckinterfaceobjectmanager_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckinterfaceobjectmanager, nmo_ckinterfaceobjectmanager_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckinterfaceobjectmanager_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckinterfaceobjectmanager_state_t, base),
                    sizeof(nmo_ckobject_state_t), NMO_GUID_FIELD_VOID,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckinterfaceobjectmanager_state_t, chunk_count, NMO_GUID_FIELD_INT32),
    NMO_FIELD_ARRAY(nmo_ckinterfaceobjectmanager_state_t, chunks, NMO_GUID_FIELD_CHUNK),
    NMO_FIELD(nmo_ckinterfaceobjectmanager_state_t, guid, NMO_GUID_FIELD_GUID)
};

/* Identifiers from CKInterfaceObjectManager.cpp */
#define CK_STATESAVE_IOM_CHUNKS 0x01234567u
#define CK_STATESAVE_IOM_GUID   0x87654321u

static nmo_status_t nmo_ckinterfaceobjectmanager_deserialize_internal(
    nmo_ckinterfaceobjectmanager_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckinterfaceobjectmanager_deserialize");
    }

    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_IOM_CHUNKS) == NMO_OK) {
        int32_t count = 0;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result == NMO_OK && count > 0) {
            out_state->chunk_count = count;
            out_state->chunks = (nmo_chunk_t **)nmo_arena_alloc(
                arena, sizeof(nmo_chunk_t *) * (size_t)count, _Alignof(nmo_chunk_t *));
            if (out_state->chunks) {
                for (int32_t i = 0; i < count; ++i) {
                    (void)nmo_chunk_read_sub_chunk(chunk, &out_state->chunks[i]);
                }
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_IOM_GUID) == NMO_OK) {
        (void)nmo_chunk_read_guid(chunk, &out_state->guid);
    }

    NMO_RETURN_OK();
}

static nmo_status_t ckinterfaceobjectmanager_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckinterfaceobjectmanager_state_t *s = src;
    nmo_ckinterfaceobjectmanager_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    return nmo_object_copy_chunk_array(arena, &d->chunks, s->chunks, (uint32_t)s->chunk_count);
}

static nmo_status_t ckinterfaceobjectmanager_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckinterfaceobjectmanager_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->chunks, (uint32_t)s->chunk_count, "chunks");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckinterfaceobjectmanager,
    nmo_ckinterfaceobjectmanager_state_t,
    nmo_ckinterfaceobjectmanager_serialize,
    nmo_ckinterfaceobjectmanager_deserialize,
    nmo_ckinterfaceobjectmanager_fields,
    NMO_GUID_CKINTERFACEOBJECTMANAGER,
    "CKInterfaceObjectManager",
    NMO_CID_INTERFACEOBJECTMANAGER,
    NMO_GUID_CKOBJECT
)

static nmo_status_t nmo_ckinterfaceobjectmanager_serialize_internal(
    const nmo_ckinterfaceobjectmanager_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckinterfaceobjectmanager_serialize");
    }

    nmo_status_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_IOM_CHUNKS);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->chunk_count);
    if (result != NMO_OK) return result;

    for (int32_t i = 0; i < in_state->chunk_count; ++i) {
        nmo_chunk_t *sub = NULL;
        if (in_state->chunks && i >= 0) {
            sub = in_state->chunks[i];
        }
        if (!sub) {
            sub = nmo_chunk_create(arena);
        }
        result = nmo_chunk_write_sub_chunk(out_chunk, sub);
        if (result != NMO_OK) return result;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_IOM_GUID);
    if (result != NMO_OK) return result;

    return nmo_chunk_write_guid(out_chunk, in_state->guid);
}

nmo_status_t nmo_ckinterfaceobjectmanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckinterfaceobjectmanager_state_t *out_state = (nmo_ckinterfaceobjectmanager_state_t *)instance;
    return nmo_ckinterfaceobjectmanager_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_ckinterfaceobjectmanager_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckinterfaceobjectmanager_state_t *in_state =
        (const nmo_ckinterfaceobjectmanager_state_t *)instance;
    return nmo_ckinterfaceobjectmanager_serialize_internal(in_state, out_chunk, context);
}
