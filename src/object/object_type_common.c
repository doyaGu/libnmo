/**
 * @file object_type_common.c
 * @brief Common helpers for CKObject-derived type vtables
 */

#include "object/nmo_object_type_common.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "format/nmo_chunk.h"
#include <string.h>

nmo_result_t nmo_object_default_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)context;
    if (!instance || !type) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL instance/type in create");
    }
    memset(instance, 0, type->size);
    return nmo_result_ok();
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

nmo_result_t nmo_object_default_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)arena;
    if (!src || !dst || !type) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL src/dst/type in copy");
    }
    memcpy(dst, src, type->size);
    return nmo_result_ok();
}

nmo_result_t nmo_object_default_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)context;
    if (!instance || !type) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL instance/type in validate");
    }
    return nmo_result_ok();
}

nmo_result_t nmo_object_default_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context)
{
    const nmo_type_registry_t *registry = (const nmo_type_registry_t *)context;
    return nmo_type_value_to_string(value, type, registry, buffer, buffer_size);
}

nmo_result_t nmo_object_default_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context)
{
    const nmo_type_registry_t *registry = (const nmo_type_registry_t *)context;
    return nmo_type_value_from_string(value, type, registry, string);
}

nmo_result_t nmo_object_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size)
{
    if (!size) {
        *dst = NULL;
        return nmo_result_ok();
    }
    if (!src) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL source buffer for size %zu", size);
    }
    void *mem = nmo_arena_alloc(arena, size, alignof(uint8_t));
    if (!mem) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory copying buffer (%zu bytes)", size);
    }
    memcpy(mem, src, size);
    *dst = mem;
    return nmo_result_ok();
}

nmo_result_t nmo_object_copy_array(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t elem_size,
    uint32_t count)
{
    if (!count) {
        *dst = NULL;
        return nmo_result_ok();
    }
    if (!src) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL source array for count %u", count);
    }
    size_t size = (size_t)count * elem_size;
    void *mem = nmo_arena_alloc(arena, size, alignof(uint8_t));
    if (!mem) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory copying array (%zu bytes)", size);
    }
    memcpy(mem, src, size);
    *dst = mem;
    return nmo_result_ok();
}

nmo_result_t nmo_object_copy_string(
    nmo_arena_t *arena,
    char **dst,
    const char *src)
{
    if (!src) {
        *dst = NULL;
        return nmo_result_ok();
    }
    const char *dup = nmo_arena_strdup(arena, src);
    if (!dup) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory duplicating string");
    }
    *dst = (char *)dup;
    return nmo_result_ok();
}

nmo_result_t nmo_object_copy_string_array(
    nmo_arena_t *arena,
    char ***dst,
    char *const *src,
    uint32_t count)
{
    if (!count) {
        *dst = NULL;
        return nmo_result_ok();
    }
    if (!src) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL source string array for count %u", count);
    }
    char **mem = nmo_arena_alloc(arena, sizeof(char *) * count, alignof(char *));
    if (!mem) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory copying string array");
    }
    for (uint32_t i = 0; i < count; ++i) {
        mem[i] = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_string(arena, &mem[i], src[i]));
    }
    *dst = mem;
    return nmo_result_ok();
}

nmo_result_t nmo_object_copy_chunk(
    nmo_arena_t *arena,
    nmo_chunk_t **dst,
    nmo_chunk_t *src)
{
    if (!src) {
        *dst = NULL;
        return nmo_result_ok();
    }
    nmo_chunk_t *clone = nmo_chunk_clone(src, arena);
    if (!clone) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory cloning chunk");
    }
    *dst = clone;
    return nmo_result_ok();
}

nmo_result_t nmo_object_copy_chunk_array(
    nmo_arena_t *arena,
    nmo_chunk_t ***dst,
    nmo_chunk_t *const *src,
    uint32_t count)
{
    if (!count) {
        *dst = NULL;
        return nmo_result_ok();
    }
    if (!src) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL source chunk array for count %u", count);
    }
    nmo_chunk_t **mem = nmo_arena_alloc(arena, sizeof(nmo_chunk_t *) * count, alignof(nmo_chunk_t *));
    if (!mem) {
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory copying chunk array");
    }
    for (uint32_t i = 0; i < count; ++i) {
        mem[i] = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &mem[i], src[i]));
    }
    *dst = mem;
    return nmo_result_ok();
}
