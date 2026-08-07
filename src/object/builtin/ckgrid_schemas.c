/**
 * @file ckgrid_schemas.c
 * @brief CKGrid schema implementation
 */

#include "object/builtin/nmo_grid_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "object/nmo_object_repository.h"
#include <string.h>

static void grid_layer_dispose(void *element, void *user_data)
{
    (void)user_data;
    nmo_grid_layer_t *layer = (nmo_grid_layer_t *)element;
    if (layer && layer->chunk) {
        nmo_chunk_destroy(layer->chunk);
        layer->chunk = NULL;
    }
}

static void grid_layers_set_lifecycle(nmo_array_t *layers)
{
    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.dispose = grid_layer_dispose;
    nmo_array_set_lifecycle(layers, &lifecycle);
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    grid,
    nmo_grid_state_t,
    do {
        nmo_status_t result = nmo_array_init(
            &state->layers, sizeof(nmo_grid_layer_t), 0, NULL);
        if (result != NMO_OK) return result;
        grid_layers_set_lifecycle(&state->layers);
        state->width = 0;
        state->length = 0;
        state->priority = 0;
        state->orientation_mode = 0;
        state->has_grid_data = 1;
        state->has_file_flag = 0;
        state->file_flag = 0;
    } while (0),
    ((void)0))

static void nmo_grid_dispose_state_arrays(nmo_grid_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->base.base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
    nmo_array_dispose(&state->layers);
}

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
}

static size_t nmo_grid_identifier_remaining_dwords(
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

static nmo_status_t nmo_grid_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_grid_state_t *out_state = (nmo_grid_state_t *)instance;
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_grid_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    out_state->has_grid_data = 0;
    out_state->has_file_flag = 0;
    out_state->file_flag = 0;

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_GRIDDATA);
    if (result == NMO_ERR_NOT_FOUND) {
        NMO_RETURN_OK();
    }
    if (result != NMO_OK) return result;
    out_state->has_grid_data = 1;

    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->width));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->length));
    {
        int32_t reserved = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &reserved));
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->priority));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->orientation_mode));

    if (nmo_chunk_is_file_mode(chunk)) {
        int32_t file_flag = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &file_flag));
        out_state->has_file_flag = 1;
        out_state->file_flag = file_flag;
    }

    size_t count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(chunk, &count));
    if (count > SIZE_MAX / sizeof(nmo_grid_layer_t)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    const size_t minimum_dwords_per_layer =
        nmo_chunk_is_file_mode(chunk) ? 1u : 2u;
    if (count >
        nmo_grid_identifier_remaining_dwords(chunk) /
            minimum_dwords_per_layer) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t layers;
    NMO_RETURN_IF_ERROR(nmo_array_init(
        &layers, sizeof(nmo_grid_layer_t), count, &out_state->layers.allocator));
    grid_layers_set_lifecycle(&layers);
    nmo_grid_layer_t *items = NULL;
    result = nmo_array_extend(&layers, count, (void **)&items);
    if (result != NMO_OK) {
        nmo_array_dispose(&layers);
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_read(chunk, &items[i].ref);
        if (result != NMO_OK) {
            nmo_array_dispose(&layers);
            return result;
        }
        nmo_ref_check_class(
            &items[i].ref,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_LAYER);
    }
    if (!nmo_chunk_is_file_mode(chunk)) {
        for (size_t i = 0; i < count; ++i) {
            result = nmo_chunk_read_sub_chunk(chunk, &items[i].chunk);
            if (result != NMO_OK) {
                nmo_array_dispose(&layers);
                return result;
            }
        }
    }
    nmo_array_dispose(&out_state->layers);
    out_state->layers = layers;

    NMO_RETURN_OK();
}

nmo_status_t nmo_grid_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_grid_state_t *out_state = (nmo_grid_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_grid_state_t decoded;
    nmo_status_t result = nmo_grid_create(&decoded, type, context);
    if (result != NMO_OK) return result;

    nmo_beobject_state_t *decoded_base = &decoded.base.base.base;
    const nmo_beobject_state_t *old_base = &out_state->base.base.base;
    if (old_base->scripts.allocator.alloc != NULL) {
        decoded_base->scripts.allocator = old_base->scripts.allocator;
    }
    if (old_base->attributes.allocator.alloc != NULL) {
        decoded_base->attributes.allocator = old_base->attributes.allocator;
    }
    if (old_base->legacy_attributes.allocator.alloc != NULL) {
        decoded_base->legacy_attributes.allocator =
            old_base->legacy_attributes.allocator;
    }
    if (out_state->layers.allocator.alloc != NULL) {
        decoded.layers.allocator = out_state->layers.allocator;
    }

    result = nmo_grid_deserialize_internal(&decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_grid_dispose_state_arrays(&decoded);
        return result;
    }

    nmo_grid_dispose_state_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_grid_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_grid_state_t *in_state = (const nmo_grid_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_grid_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const int is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_GRIDONLY) == 0) {
            return NMO_OK;
        }
    }

    if (!in_state->has_grid_data) {
        return NMO_OK;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GRIDDATA);
    if (result != NMO_OK) return result;

    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->width));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->length));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, 0));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->priority));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, in_state->orientation_mode));

    if (is_file) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->has_file_flag ? in_state->file_flag : 1));
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->layers.count);
    if (result != NMO_OK) return result;

    const nmo_grid_layer_t *layers = NMO_ARRAY_DATA(nmo_grid_layer_t, &in_state->layers);
    for (size_t i = 0; i < in_state->layers.count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_ref_write_sequence_item(out_chunk, &layers[i].ref));
    }

    if (!is_file) {
        for (size_t i = 0; i < in_state->layers.count; ++i) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_sub_chunk(out_chunk, layers[i].chunk));
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_grid_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_grid_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static const nmo_type_field_t nmo_grid_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_grid_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_grid_state_t, width, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, length, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, priority, CKPGUID_INT),
    NMO_FIELD(nmo_grid_state_t, orientation_mode, CKPGUID_UINT32),
    NMO_FIELD(nmo_grid_state_t, has_file_flag, CKPGUID_UINT8),
    NMO_FIELD(nmo_grid_state_t, file_flag, CKPGUID_INT),
    NMO_FIELD_ARRAY(nmo_grid_state_t, layers, CKPGUID_NONE)
};

static nmo_status_t nmo_grid_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_grid_state_t *s = src;
    nmo_grid_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    /* The default reflected clone is shallow for the nested chunk pointer.
     * Release only its backing storage before building the deep clone. */
    nmo_container_lifecycle_t no_lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    nmo_array_set_lifecycle(&d->layers, &no_lifecycle);
    nmo_array_dispose(&d->layers);
    NMO_RETURN_IF_ERROR(nmo_array_init(
        &d->layers, sizeof(nmo_grid_layer_t), s->layers.count, &s->layers.allocator));
    grid_layers_set_lifecycle(&d->layers);
    nmo_grid_layer_t *dst_layers = NULL;
    NMO_RETURN_IF_ERROR(nmo_array_extend(&d->layers, s->layers.count, (void **)&dst_layers));
    const nmo_grid_layer_t *src_layers = NMO_ARRAY_DATA(nmo_grid_layer_t, &s->layers);
    for (size_t i = 0; i < s->layers.count; ++i) {
        dst_layers[i].ref = src_layers[i].ref;
        if (src_layers[i].chunk) {
            dst_layers[i].chunk = nmo_chunk_clone(src_layers[i].chunk, arena);
            if (!dst_layers[i].chunk) {
                nmo_array_dispose(&d->layers);
                return NMO_ERR_NOMEM;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_grid_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_grid_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->layers.data, s->layers.count, "layers");
    NMO_RETURN_OK();
}

nmo_status_t nmo_grid_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_grid_validate(instance, type, context);
}

nmo_status_t nmo_grid_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_grid_remap_dependencies");
    }

    nmo_grid_state_t *state = (nmo_grid_state_t *)instance;
    nmo_status_t result = nmo_3dentity_remap_dependencies(&state->base, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (state->layers.count > 0 && state->layers.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Grid layers missing");
    }
    return nmo_grid_validate(state, NULL, NULL);
}

static nmo_status_t nmo_grid_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_grid_pre_delete");
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_grid_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_grid_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    if (state->layers.count > 0 &&
        (state->layers.data == NULL ||
         state->layers.element_size != sizeof(nmo_grid_layer_t))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    const nmo_grid_layer_t *layers = NMO_ARRAY_DATA(
        nmo_grid_layer_t, &state->layers);
    for (size_t i = 0; i < state->layers.count; ++i) {
        if (layers[i].ref.state != NMO_REF_RESOLVED ||
            layers[i].ref.id == NMO_OBJECT_ID_NONE) {
            continue;
        }
        if (!visitor(user_data, layers[i].ref.id, 0, "layers", (uint32_t)i)) {
            break;
        }
    }
    return NMO_OK;
}

static void nmo_grid_post_delete(
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

static nmo_status_t nmo_grid_canonical_bytes(
    const nmo_grid_state_t *state,
    nmo_arena_t **out_arena,
    void **out_data,
    size_t *out_size)
{
    if (state == NULL || out_arena == NULL || out_data == NULL ||
        out_size == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_arena = NULL;
    *out_data = NULL;
    *out_size = 0;

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) return NMO_ERR_NOMEM;
    nmo_chunk_t *file_chunk = nmo_chunk_create(arena);
    nmo_chunk_t *runtime_chunk = nmo_chunk_create(arena);
    if (file_chunk == NULL || runtime_chunk == NULL) {
        nmo_arena_destroy(arena);
        return NMO_ERR_NOMEM;
    }
    file_chunk->class_id = NMO_CID_GRID;
    file_chunk->data_version = 7;
    file_chunk->chunk_options = NMO_CHUNK_OPTION_FILE;
    runtime_chunk->class_id = NMO_CID_GRID;
    runtime_chunk->data_version = 7;

    nmo_serialize_context_t file_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_status_t result = nmo_grid_serialize(
        state, file_chunk, NULL, &file_context);
    if (result == NMO_OK) {
        nmo_chunk_close(file_chunk);
        nmo_serialize_context_t runtime_context =
            nmo_serialize_context_create(
                arena, NULL, 0, CK_STATESAVE_GRIDONLY);
        result = nmo_grid_serialize(
            state, runtime_chunk, NULL, &runtime_context);
    }

    void *file_data = NULL;
    void *runtime_data = NULL;
    size_t file_size = 0;
    size_t runtime_size = 0;
    if (result == NMO_OK) {
        nmo_chunk_close(runtime_chunk);
        result = nmo_chunk_serialize_version1(
            file_chunk, &file_data, &file_size, arena);
    }
    if (result == NMO_OK) {
        result = nmo_chunk_serialize_version1(
            runtime_chunk, &runtime_data, &runtime_size, arena);
    }
    if (result == NMO_OK) {
        const size_t header_size = 2u * sizeof(size_t);
        if (runtime_size > SIZE_MAX - header_size ||
            file_size > SIZE_MAX - header_size - runtime_size) {
            result = NMO_ERR_NOMEM;
        } else {
            *out_size = header_size + file_size + runtime_size;
            uint8_t *combined = (uint8_t *)nmo_arena_alloc(
                arena, *out_size, alignof(size_t));
            if (combined == NULL) {
                result = NMO_ERR_NOMEM;
            } else {
                memcpy(combined, &file_size, sizeof(file_size));
                memcpy(combined + sizeof(file_size),
                       &runtime_size, sizeof(runtime_size));
                memcpy(combined + header_size, file_data, file_size);
                memcpy(combined + header_size + file_size,
                       runtime_data, runtime_size);
                *out_data = combined;
            }
        }
    }
    if (result != NMO_OK) {
        nmo_arena_destroy(arena);
        return result;
    }
    *out_arena = arena;
    return NMO_OK;
}

static bool nmo_grid_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    nmo_arena_t *arena_a = NULL;
    nmo_arena_t *arena_b = NULL;
    void *data_a = NULL;
    void *data_b = NULL;
    size_t size_a = 0;
    size_t size_b = 0;
    const nmo_status_t result_a = nmo_grid_canonical_bytes(
        (const nmo_grid_state_t *)a,
        &arena_a, &data_a, &size_a);
    const nmo_status_t result_b = nmo_grid_canonical_bytes(
        (const nmo_grid_state_t *)b,
        &arena_b, &data_b, &size_b);
    const bool equal = result_a == NMO_OK && result_b == NMO_OK &&
        size_a == size_b &&
        (size_a == 0 || memcmp(data_a, data_b, size_a) == 0);
    nmo_arena_destroy(arena_a);
    nmo_arena_destroy(arena_b);
    return equal;
}

static uint32_t nmo_grid_hash(const void *instance)
{
    if (instance == NULL) return 0;
    nmo_arena_t *arena = NULL;
    void *data = NULL;
    size_t size = 0;
    if (nmo_grid_canonical_bytes(
            (const nmo_grid_state_t *)instance,
            &arena, &data, &size) != NMO_OK) {
        return 0;
    }
    const uint32_t hash = (uint32_t)nmo_hash_fnv1a(data, size);
    nmo_arena_destroy(arena);
    return hash;
}

nmo_type_vtable_t nmo_grid_vtable = {
    .prepare_dependencies = nmo_grid_prepare_dependencies,
    .remap_dependencies = nmo_grid_remap_dependencies,
    .pre_delete = nmo_grid_pre_delete,
    .post_delete = nmo_grid_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_grid_create,
        nmo_grid_destroy,
        nmo_grid_serialize,
        nmo_grid_deserialize,
        nmo_grid_copy,
        nmo_grid_validate,
        nmo_grid_equals,
        nmo_grid_hash,
        nmo_grid_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_grid_type,
    CKPGUID_GRID,
    "CKGrid",
    NMO_CID_GRID,
    CKPGUID_3DENTITY,
    nmo_grid_state_t,
    &nmo_grid_vtable,
    nmo_grid_fields)
