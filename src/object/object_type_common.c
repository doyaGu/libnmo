/**
 * @file object_type_common.c
 * @brief Common helpers for CKObject-derived type vtables
 */

#include "object/nmo_object_type_common.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "format/nmo_chunk.h"
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

nmo_status_t nmo_object_default_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context)
{
    const nmo_type_registry_t *registry = (const nmo_type_registry_t *)context;
    return nmo_type_value_to_string(value, type, registry, buffer, buffer_size);
}

nmo_status_t nmo_object_default_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context)
{
    const nmo_type_registry_t *registry = (const nmo_type_registry_t *)context;
    return nmo_type_value_from_string(value, type, registry, string);
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
    void *mem = nmo_arena_alloc(arena, size, alignof(uint8_t));
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
