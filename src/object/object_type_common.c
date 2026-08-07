/**
 * @file object_type_common.c
 * @brief Common helpers for CKObject-derived type vtables
 */

#include "object/nmo_object_type_common.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "type/nmo_reflection.h"
#include <string.h>

void nmo_object_dispose_array_fields(
    void *instance,
    const nmo_type_descriptor_t *type)
{
    if (!instance || !type || !type->fields || type->field_count == 0) {
        return;
    }

    for (size_t i = 0; i < type->field_count; ++i) {
        const nmo_type_field_t *field = &type->fields[i];
        if (!(field->flags & NMO_FIELD_REPEATED)) {
            continue;
        }
        if (field->size != sizeof(nmo_array_t)) {
            continue;
        }
        nmo_array_t *array = (nmo_array_t *)nmo_field_get_ptr(instance, field);
        if (array) {
            nmo_array_dispose(array);
        }
    }
}

nmo_status_t nmo_object_default_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)context;
    if (!instance || !type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL instance/type in create");
    }
    memset(instance, 0, type->size);
    NMO_RETURN_OK();
}

void nmo_object_default_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance && type) {
        memset(instance, 0, type->size);
    }
    (void)context;
}

nmo_status_t nmo_object_default_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)arena;
    if (!src || !dst || !type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL src/dst/type in copy");
    }
    memcpy(dst, src, type->size);

    if (!type->fields || type->field_count == 0) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < type->field_count; ++i) {
        const nmo_type_field_t *field = &type->fields[i];
        if (!(field->flags & NMO_FIELD_REPEATED)) {
            continue;
        }
        if (field->size != sizeof(nmo_array_t)) {
            continue;
        }

        const nmo_array_t *src_array = (const nmo_array_t *)nmo_field_get_ptr_const(src, field);
        nmo_array_t *dst_array = (nmo_array_t *)nmo_field_get_ptr(dst, field);

        if (!src_array || !dst_array) {
            continue;
        }

        if (src_array->element_size == 0) {
            memset(dst_array, 0, sizeof(*dst_array));
            continue;
        }

        nmo_array_t clone = {0};
        nmo_status_t result = nmo_array_clone(src_array, &clone, &src_array->allocator);
        if (result != NMO_OK) {
            return result;
        }
        *dst_array = clone;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_object_default_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)context;
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL instance in validate");
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_object_serialized_state_bytes(
    const void *instance,
    nmo_object_serialize_fn serializer,
    const nmo_object_serialize_pass_t *passes,
    size_t pass_count,
    nmo_arena_t *arena,
    void **out_data,
    size_t *out_size)
{
    if (instance == NULL || serializer == NULL || passes == NULL ||
        pass_count == 0 || arena == NULL || out_data == NULL ||
        out_size == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_size = 0;

    if (pass_count > SIZE_MAX / sizeof(void *) ||
        pass_count > SIZE_MAX / sizeof(size_t)) {
        return NMO_ERR_NOMEM;
    }
    void **pass_data = nmo_arena_alloc(
        arena, pass_count * sizeof(void *), alignof(void *));
    size_t *pass_sizes = nmo_arena_alloc(
        arena, pass_count * sizeof(size_t), alignof(size_t));
    if (pass_data == NULL || pass_sizes == NULL) return NMO_ERR_NOMEM;

    size_t payload_size = 0;
    for (size_t i = 0; i < pass_count; ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        if (chunk == NULL) return NMO_ERR_NOMEM;
        chunk->class_id = passes[i].class_id;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = passes[i].data_version;
        chunk->chunk_options = passes[i].chunk_options;

        void *context = NULL;
        nmo_serialize_context_t serialize_context;
        if (passes[i].use_context) {
            serialize_context = nmo_serialize_context_create(
                arena, NULL, passes[i].serialize_flags,
                passes[i].save_flags);
            context = &serialize_context;
        }
        nmo_status_t result = serializer(
            instance, chunk, NULL, context);
        if (result != NMO_OK) return result;
        nmo_chunk_close(chunk);
        result = nmo_chunk_serialize_version1(
            chunk, &pass_data[i], &pass_sizes[i], arena);
        if (result != NMO_OK) return result;
        if (pass_sizes[i] > SIZE_MAX - payload_size) {
            return NMO_ERR_NOMEM;
        }
        payload_size += pass_sizes[i];
    }

    if (pass_count == 1) {
        *out_data = pass_data[0];
        *out_size = pass_sizes[0];
        return NMO_OK;
    }

    const size_t header_size = pass_count * sizeof(size_t);
    if (payload_size > SIZE_MAX - header_size) return NMO_ERR_NOMEM;
    const size_t combined_size = header_size + payload_size;
    uint8_t *combined = nmo_arena_alloc(
        arena, combined_size, alignof(size_t));
    if (combined == NULL) return NMO_ERR_NOMEM;
    memcpy(combined, pass_sizes, header_size);
    size_t offset = header_size;
    for (size_t i = 0; i < pass_count; ++i) {
        if (pass_sizes[i] > 0) {
            memcpy(combined + offset, pass_data[i], pass_sizes[i]);
            offset += pass_sizes[i];
        }
    }
    *out_data = combined;
    *out_size = combined_size;
    return NMO_OK;
}

bool nmo_object_serialized_state_equals(
    const void *a,
    const void *b,
    nmo_object_serialize_fn serializer,
    const nmo_object_serialize_pass_t *passes,
    size_t pass_count,
    size_t arena_block_size)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    nmo_arena_t *arena = nmo_arena_create(
        NULL, arena_block_size != 0 ? arena_block_size : 4096);
    if (arena == NULL) return false;
    void *data_a = NULL;
    void *data_b = NULL;
    size_t size_a = 0;
    size_t size_b = 0;
    const nmo_status_t result_a = nmo_object_serialized_state_bytes(
        a, serializer, passes, pass_count, arena, &data_a, &size_a);
    const nmo_status_t result_b = nmo_object_serialized_state_bytes(
        b, serializer, passes, pass_count, arena, &data_b, &size_b);
    const bool equal = result_a == NMO_OK && result_b == NMO_OK &&
        size_a == size_b &&
        (size_a == 0 || memcmp(data_a, data_b, size_a) == 0);
    nmo_arena_destroy(arena);
    return equal;
}

uint32_t nmo_object_serialized_state_hash(
    const void *instance,
    nmo_object_serialize_fn serializer,
    const nmo_object_serialize_pass_t *passes,
    size_t pass_count,
    size_t arena_block_size)
{
    if (instance == NULL) return 0;
    nmo_arena_t *arena = nmo_arena_create(
        NULL, arena_block_size != 0 ? arena_block_size : 4096);
    if (arena == NULL) return 0;
    void *data = NULL;
    size_t size = 0;
    const nmo_status_t result = nmo_object_serialized_state_bytes(
        instance, serializer, passes, pass_count, arena, &data, &size);
    const uint32_t hash = result == NMO_OK
        ? (uint32_t)nmo_hash_fnv1a(data, size)
        : 0;
    nmo_arena_destroy(arena);
    return hash;
}


nmo_status_t nmo_object_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size)
{
    if (!size) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL source buffer for size %zu", size);
    }
    void *mem = nmo_arena_alloc(arena, size, alignof(uint8_t));
    if (!mem) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Out of memory copying buffer (%zu bytes)", size);
    }
    memcpy(mem, src, size);
    *dst = mem;
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_copy_array(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t elem_size,
    uint32_t count)
{
    if (!count) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL source array for count %u", count);
    }
    size_t size = (size_t)count * elem_size;
    /* The element type is not available here, so use the platform's maximum
     * fundamental alignment instead of returning byte-aligned typed arrays. */
    void *mem = nmo_arena_alloc(arena, size, alignof(max_align_t));
    if (!mem) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Out of memory copying array (%zu bytes)", size);
    }
    memcpy(mem, src, size);
    *dst = mem;
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_copy_string(
    nmo_arena_t *arena,
    char **dst,
    const char *src)
{
    if (!src) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    const char *dup = nmo_arena_strdup(arena, src);
    if (!dup) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Out of memory duplicating string");
    }
    *dst = (char *)dup;
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_copy_string_array(
    nmo_arena_t *arena,
    char ***dst,
    char *const *src,
    uint32_t count)
{
    if (!count) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL source string array for count %u", count);
    }
    char **mem = nmo_arena_alloc(arena, sizeof(char *) * count, alignof(char *));
    if (!mem) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Out of memory copying string array");
    }
    for (uint32_t i = 0; i < count; ++i) {
        mem[i] = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &mem[i], src[i]));
    }
    *dst = mem;
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_copy_chunk(
    nmo_arena_t *arena,
    nmo_chunk_t **dst,
    nmo_chunk_t *src)
{
    if (!src) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    nmo_chunk_t *clone = nmo_chunk_clone(src, arena);
    if (!clone) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Out of memory cloning chunk");
    }
    *dst = clone;
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_copy_chunk_array(
    nmo_arena_t *arena,
    nmo_chunk_t ***dst,
    nmo_chunk_t *const *src,
    uint32_t count)
{
    if (!count) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL source chunk array for count %u", count);
    }
    nmo_chunk_t **mem = nmo_arena_alloc(arena, sizeof(nmo_chunk_t *) * count, alignof(nmo_chunk_t *));
    if (!mem) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Out of memory copying chunk array");
    }
    for (uint32_t i = 0; i < count; ++i) {
        mem[i] = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &mem[i], src[i]));
    }
    *dst = mem;
    NMO_RETURN_OK();
}

static void nmo_object_dispose_chunk_ptr(void *element, void *user_data)
{
    (void)user_data;
    if (!element) {
        return;
    }
    nmo_chunk_t *chunk = *(nmo_chunk_t **)element;
    if (chunk) {
        nmo_chunk_destroy(chunk);
    }
}

static void nmo_object_reset_chunk_ptr(void *element, void *user_data)
{
    (void)user_data;
    if (!element) {
        return;
    }
    nmo_chunk_t **slot = (nmo_chunk_t **)element;
    if (*slot) {
        nmo_chunk_destroy(*slot);
        *slot = NULL;
    }
}

static void nmo_object_reset_string_ptr(void *element, void *user_data)
{
    (void)user_data;
    if (!element) {
        return;
    }
    *(char **)element = NULL;
}

void nmo_object_array_set_chunk_lifecycle(nmo_array_t *array)
{
    if (!array) {
        return;
    }

    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.reset = nmo_object_reset_chunk_ptr;
    lifecycle.dispose = nmo_object_dispose_chunk_ptr;
    nmo_array_set_lifecycle(array, &lifecycle);
}

void nmo_object_array_set_string_lifecycle(nmo_array_t *array)
{
    if (!array) {
        return;
    }

    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.reset = nmo_object_reset_string_ptr;
    nmo_array_set_lifecycle(array, &lifecycle);
}

nmo_status_t nmo_object_clone_chunk_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src)
{
    if (!arena || !dst || !src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to nmo_object_clone_chunk_array");
    }

    if (src->element_size != sizeof(nmo_chunk_t *)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Chunk array element size mismatch");
    }

    nmo_status_t result = nmo_array_init(dst, src->element_size, src->count, &src->allocator);
    if (result != NMO_OK) {
        return result;
    }
    nmo_object_array_set_chunk_lifecycle(dst);

    if (src->count == 0 || src->data == NULL) {
        NMO_RETURN_OK();
    }

    nmo_chunk_t **out_chunks = NULL;
    result = nmo_array_extend(dst, src->count, (void **)&out_chunks);
    if (result != NMO_OK) {
        nmo_array_dispose(dst);
        return result;
    }

    nmo_chunk_t *const *src_chunks = NMO_ARRAY_DATA(nmo_chunk_t *, src);
    for (size_t i = 0; i < src->count; ++i) {
        if (src_chunks[i]) {
            nmo_chunk_t *clone = nmo_chunk_clone(src_chunks[i], arena);
            if (!clone) {
                nmo_array_dispose(dst);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Out of memory cloning chunk array element");
            }
            out_chunks[i] = clone;
        } else {
            out_chunks[i] = NULL;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_object_clone_string_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src)
{
    if (!arena || !dst || !src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to nmo_object_clone_string_array");
    }

    if (src->element_size != sizeof(char *)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "String array element size mismatch");
    }

    nmo_status_t result = nmo_array_init(dst, src->element_size, src->count, &src->allocator);
    if (result != NMO_OK) {
        return result;
    }

    if (src->count == 0 || src->data == NULL) {
        NMO_RETURN_OK();
    }

    char **out_strings = NULL;
    result = nmo_array_extend(dst, src->count, (void **)&out_strings);
    if (result != NMO_OK) {
        nmo_array_dispose(dst);
        return result;
    }

    char *const *src_strings = NMO_ARRAY_DATA(char *, src);
    for (size_t i = 0; i < src->count; ++i) {
        out_strings[i] = NULL;
        result = nmo_object_copy_string(arena, &out_strings[i], src_strings[i]);
        if (result != NMO_OK) {
            nmo_array_dispose(dst);
            return result;
        }
    }

    NMO_RETURN_OK();
}
