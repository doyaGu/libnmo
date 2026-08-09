/**
 * @file ckgroup_schemas.c
 * @brief CKGroup schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKGroup (object groups).
 * CKGroup extends CKBeObject and contains an array of object references.
 * 
 * Based on official Virtools SDK (reference/src/CKGroup.cpp:185-220):
 * - CKGroup::Save writes identifier + object array
 * - CKGroup::Load reads object array using XObjectPointerArray::Load
 * - PostLoad ensures bidirectional group membership consistency
 */

#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

static void nmo_group_dispose_state_arrays(nmo_group_state_t *state);
static nmo_status_t nmo_group_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DEFINE_OBJECT_LIFECYCLE(
    group,
    nmo_group_state_t,
    do {
        nmo_status_t result = nmo_beobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        result = nmo_array_init(
            &state->object_ids, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_group_dispose_state_arrays(state);
            return result;
        }
    } while (0),
    nmo_group_dispose_state_arrays(state))

static void nmo_group_dispose_state_arrays(nmo_group_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->object_ids);
    nmo_beobject_vtable.destroy(&state->base, NULL, NULL);
}

static size_t nmo_group_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_group_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_group_state_t, base),
                       sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_group_state_t, object_ids)
};

static int nmo_group_is_file_mode_ser(const nmo_chunk_t *chunk, void *context)
{
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    if (chunk && ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0)) {
        return 1;
    }
    if (ser_ctx && ((ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0)) {
        return 1;
    }
    return 0;
}

/* =============================================================================
 * CKGroup DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKGroup state from chunk
 * 
 * Implements the symmetric read operation for CKGroup::Load.
 * Reads the object ID array.
 * 
 * Reference: reference/src/CKGroup.cpp:197-220
 * 
 * @param chunk Chunk containing CKGroup data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_status_t nmo_group_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_group_state_t *out_state = (nmo_group_state_t *)instance;
    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_group_deserialize");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Seek group data identifier */
    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_GROUPALL, &section_dwords);
    if (result != NMO_OK) {
        /* No group data - empty group is valid */
        if (result == NMO_ERR_NOT_FOUND) NMO_RETURN_OK();
        return result;
    }
    if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
    const size_t section_end =
        nmo_chunk_get_position(chunk) + section_dwords;

    /* Read object array using XObjectPointerArray::Load format
     * Reference: XObjectArray.cpp - array is stored as [count, id1, id2, ...] */
    int32_t count;
    result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) {
        return result;
    }

    if (count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Group object count is negative");
    }

    if ((size_t)count > nmo_group_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t decoded = {0};
    result = nmo_array_init(&decoded, sizeof(nmo_ref_t), (size_t)count, NULL);
    if (result != NMO_OK) return result;
    nmo_ref_t *refs = NULL;
    result = nmo_array_extend(&decoded, (size_t)count, (void **)&refs);
    for (int32_t i = 0; result == NMO_OK && i < count; i++) {
        result = nmo_ref_read(chunk, &refs[i]);
        if (result == NMO_OK) {
            nmo_ref_check_class(
                &refs[i],
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_BEOBJECT);
        }
    }
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded);
        return result;
    }
    if (nmo_chunk_get_position(chunk) != section_end) {
        nmo_array_dispose(&decoded);
        return NMO_ERR_INVALID_FORMAT;
    }
    result = nmo_array_swap(&out_state->object_ids, &decoded);
    nmo_array_dispose(&decoded);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_group_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_group_state_t *out_state = (nmo_group_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_group_state_t decoded = {0};
    if (out_state->base.scripts.allocator.alloc != NULL) {
        decoded.base.scripts.allocator = out_state->base.scripts.allocator;
    }
    if (out_state->base.attributes.allocator.alloc != NULL) {
        decoded.base.attributes.allocator = out_state->base.attributes.allocator;
    }
    if (out_state->base.legacy_attributes.allocator.alloc != NULL) {
        decoded.base.legacy_attributes.allocator =
            out_state->base.legacy_attributes.allocator;
    }
    const nmo_allocator_t *allocator =
        out_state->object_ids.allocator.alloc != NULL
            ? &out_state->object_ids.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.object_ids, sizeof(nmo_ref_t), 0, allocator);
    if (result != NMO_OK) return result;
    result = nmo_group_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_group_dispose_state_arrays(&decoded);
        return result;
    }
    nmo_group_dispose_state_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKGroup SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKGroup state to chunk
 * 
 * Implements the symmetric write operation for CKGroup::Save.
 * Writes the object ID array.
 * 
 * Reference: reference/src/CKGroup.cpp:185-195
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_status_t nmo_group_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_group_state_t *in_state = (const nmo_group_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_group_serialize");
    }

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_group_is_file_mode_ser(out_chunk, context);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    if (!is_file && save_flags == 0) {
        NMO_RETURN_OK();
    }

    const bool want_group = is_file || ((save_flags & CK_STATESAVE_GROUPALL) != 0);
    if (!want_group) {
        NMO_RETURN_OK();
    }

    /* In file mode, emit explicit empty group payload to avoid schema fallbacks. */
    if (!is_file && in_state->object_ids.count == 0) {
        NMO_RETURN_OK();
    }
    if ((in_state->object_ids.count > 0 && !in_state->object_ids.data) ||
        in_state->object_ids.element_size != sizeof(nmo_ref_t) ||
        in_state->object_ids.count > INT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Group object IDs missing");
    }

    /* Write identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GROUPALL);
    if (result != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->object_ids.count);
    if (result != NMO_OK) return result;

    /* Write object IDs */
    if (in_state->object_ids.count > 0) {
        const nmo_ref_t *refs = NMO_ARRAY_DATA(nmo_ref_t, &in_state->object_ids);
        for (uint32_t i = 0; i < in_state->object_ids.count; i++) {
            result = nmo_ref_write(out_chunk, &refs[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_group_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_group_validate(instance, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_group_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_group_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_group_state_t *s = src;
    nmo_group_state_t *d = dst;
    (void)type;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_group_validate(s, NULL, NULL));

    nmo_group_state_t copied;
    nmo_status_t result = nmo_group_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_beobject_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;
    nmo_array_dispose(&copied.object_ids);
    result = nmo_array_clone(
        &s->object_ids, &copied.object_ids, &s->object_ids.allocator);
    if (result != NMO_OK) goto fail;

    if (d->base.scripts.data == s->base.scripts.data) {
        memset(&d->base.scripts, 0, sizeof(d->base.scripts));
    }
    if (d->base.attributes.data == s->base.attributes.data) {
        memset(&d->base.attributes, 0, sizeof(d->base.attributes));
    }
    if (d->base.legacy_attributes.data == s->base.legacy_attributes.data) {
        memset(&d->base.legacy_attributes, 0,
               sizeof(d->base.legacy_attributes));
    }
    if (d->object_ids.data == s->object_ids.data) {
        memset(&d->object_ids, 0, sizeof(d->object_ids));
    }
    nmo_group_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_group_destroy(&copied, NULL, NULL);
    return result;
}

static bool nmo_group_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_group_state_t *lhs = (const nmo_group_state_t *)a;
    const nmo_group_state_t *rhs = (const nmo_group_state_t *)b;
    if (!nmo_beobject_vtable.equals(&lhs->base, &rhs->base) ||
        lhs->object_ids.count != rhs->object_ids.count ||
        lhs->object_ids.element_size != rhs->object_ids.element_size) {
        return false;
    }
    if (lhs->object_ids.count == 0) return true;
    if (lhs->object_ids.data == NULL || rhs->object_ids.data == NULL ||
        lhs->object_ids.element_size != sizeof(nmo_ref_t)) {
        return false;
    }
    return memcmp(lhs->object_ids.data, rhs->object_ids.data,
                  lhs->object_ids.count * sizeof(nmo_ref_t)) == 0;
}

static uint32_t nmo_group_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_group_state_t *state = (const nmo_group_state_t *)instance;
    uint32_t hash = nmo_beobject_vtable.hash(&state->base);
    hash ^= (uint32_t)nmo_hash_fnv1a(
        &state->object_ids.count, sizeof(state->object_ids.count));
    if (state->object_ids.data != NULL &&
        state->object_ids.element_size == sizeof(nmo_ref_t) &&
        state->object_ids.count > 0) {
        hash ^= (uint32_t)nmo_hash_fnv1a(
            state->object_ids.data,
            state->object_ids.count * sizeof(nmo_ref_t));
    }
    return hash;
}

static nmo_status_t nmo_group_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_group_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(s->object_ids.data, s->object_ids.count, "object_ids");
    if (s->object_ids.element_size != sizeof(nmo_ref_t) ||
        s->object_ids.count > (size_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

/* =============================================================================
 * CKGroup FINISH LOADING (Phase 15 - PostLoad equivalent)
 * ============================================================================= */

/**
 * @brief Finish loading CKGroup - establish bidirectional group membership
 * 
 * Called during Phase 15 after deserialization. Resolves object ID references
 * and establishes bidirectional relationships (groups know members, members know groups).
 * 
 * This is equivalent to CKGroup::PostLoad() in Virtools SDK.
 * 
 * @param state CKGroup state (must be nmo_group_state_t*)
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution (opaque void*)
 * @return Result indicating success or error
 */
nmo_status_t nmo_group_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_group_remap_dependencies");
    }

    nmo_group_state_t *group_state = (nmo_group_state_t *)instance;

    (void)context;
    return nmo_group_validate(group_state, NULL, NULL);
}

nmo_status_t nmo_group_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_group_validate(instance, type, context);
}

static nmo_status_t nmo_group_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_group_pre_delete");
    }

    nmo_group_state_t *state = (nmo_group_state_t *)instance;
    state->object_ids.count = 0;
    NMO_RETURN_OK();
}

static void nmo_group_post_delete(
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

nmo_type_vtable_t nmo_group_vtable = {
    .prepare_dependencies = nmo_group_prepare_dependencies,
    .remap_dependencies = nmo_group_remap_dependencies,
    .pre_delete = nmo_group_pre_delete,
    .post_delete = nmo_group_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_group_create,
        nmo_group_destroy,
        nmo_group_serialize,
        nmo_group_deserialize,
        nmo_group_copy,
        nmo_group_validate,
        nmo_group_equals,
        nmo_group_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_group_type,
    CKPGUID_GROUP,
    "CKGroup",
    NMO_CID_GROUP,
    CKPGUID_BEOBJECT,
    nmo_group_state_t,
    &nmo_group_vtable,
    nmo_group_fields)







