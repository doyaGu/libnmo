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
#include <stdint.h>
#include <stdlib.h>
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

static void *layout_member_ptr(
    void *instance,
    size_t offset)
{
    return (uint8_t *)instance + offset;
}

static const void *layout_member_ptr_const(
    const void *instance,
    size_t offset)
{
    return (const uint8_t *)instance + offset;
}

/* A BYTES member is a COUNTED member of single bytes counted by a size_t. */
static size_t layout_element_size(const nmo_object_state_member_t *member)
{
    return member->kind == NMO_OBJECT_STATE_MEMBER_BYTES ? 1u : member->size;
}

static size_t layout_count_size(const nmo_object_state_member_t *member)
{
    return member->kind == NMO_OBJECT_STATE_MEMBER_BYTES
        ? sizeof(size_t) : member->count_size;
}

static size_t layout_counted_count(
    const void *instance,
    const nmo_object_state_member_t *member)
{
    if (member->count_fn != NULL) return member->count_fn(instance);
    const void *count_ptr =
        layout_member_ptr_const(instance, member->size_offset);
    switch (layout_count_size(member)) {
    case sizeof(uint32_t): {
        uint32_t count;
        memcpy(&count, count_ptr, sizeof(count));
        return count;
    }
    default: {
        size_t count;
        memcpy(&count, count_ptr, sizeof(count));
        return count;
    }
    }
}

static bool layout_counted_bytes(
    const void *instance,
    const nmo_object_state_member_t *member,
    size_t *out_bytes)
{
    const size_t count = layout_counted_count(instance, member);
    const size_t element = layout_element_size(member);
    if (element != 0 && count > SIZE_MAX / element) return false;
    *out_bytes = count * element;
    return true;
}

static const void *layout_pointer_member(
    const void *instance,
    const nmo_object_state_member_t *member)
{
    const void *data;
    memcpy(&data, layout_member_ptr_const(instance, member->offset),
           sizeof(data));
    return data;
}

/* An OPTIONAL member is absent when its pointer is NULL or it has no element. */
static bool layout_optional_absent(
    const void *instance,
    const nmo_object_state_member_t *member)
{
    return (member->flags & NMO_OBJECT_STATE_MEMBER_OPTIONAL) != 0u &&
        (layout_pointer_member(instance, member) == NULL ||
         layout_counted_count(instance, member) == 0);
}

static bool layout_bytes_equal(
    const void *lhs,
    const void *rhs,
    size_t size)
{
    if (size == 0) return true;
    if (lhs == NULL || rhs == NULL) return false;
    return memcmp(lhs, rhs, size) == 0;
}

static bool layout_array_bytes(
    const nmo_array_t *array,
    size_t *out_size)
{
    if (array->element_size != 0 &&
        array->count > SIZE_MAX / array->element_size) {
        return false;
    }
    *out_size = array->count * array->element_size;
    return true;
}

static bool layout_string_equal(
    const char *lhs,
    const char *rhs)
{
    if (lhs == rhs) return true;
    return lhs != NULL && rhs != NULL && strcmp(lhs, rhs) == 0;
}

static bool layout_chunk_equal(
    const nmo_chunk_t *lhs,
    const nmo_chunk_t *rhs)
{
    if (lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    size_t lhs_size = 0;
    size_t rhs_size = 0;
    const void *lhs_data = nmo_chunk_get_data(lhs, &lhs_size);
    const void *rhs_data = nmo_chunk_get_data(rhs, &rhs_size);
    return lhs_size == rhs_size &&
        layout_bytes_equal(lhs_data, rhs_data, lhs_size);
}

static uint32_t layout_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool layout_members_equal(
    const nmo_object_state_member_t *members,
    size_t member_count,
    const void *a,
    const void *b);

static uint32_t layout_members_hash(
    uint32_t hash,
    const nmo_object_state_member_t *members,
    size_t member_count,
    const void *instance);

static bool layout_counted_records_equal(
    const nmo_object_state_member_t *member,
    const void *lhs,
    const void *rhs,
    size_t count)
{
    const nmo_object_state_layout_t *record = member->record;
    for (size_t i = 0; i < count; ++i) {
        if (!layout_members_equal(
                record->members, record->member_count,
                (const uint8_t *)lhs + i * record->size,
                (const uint8_t *)rhs + i * record->size)) {
            return false;
        }
    }
    return true;
}

static bool layout_records_equal(
    const nmo_object_state_layout_t *record,
    const nmo_array_t *la,
    const nmo_array_t *ra)
{
    if (la->count != ra->count ||
        la->element_size != ra->element_size ||
        (la->count > 0 && la->element_size != record->size)) {
        return false;
    }
    for (size_t i = 0; i < la->count; ++i) {
        if (!layout_members_equal(
                record->members, record->member_count,
                (const uint8_t *)la->data + i * record->size,
                (const uint8_t *)ra->data + i * record->size)) {
            return false;
        }
    }
    return true;
}

static bool layout_members_equal(
    const nmo_object_state_member_t *members,
    size_t member_count,
    const void *a,
    const void *b)
{
    for (size_t i = 0; i < member_count; ++i) {
        const nmo_object_state_member_t *member = &members[i];
        if ((member->flags & NMO_OBJECT_STATE_MEMBER_UNCOMPARED) != 0u) continue;
        const void *lhs = layout_member_ptr_const(a, member->offset);
        const void *rhs = layout_member_ptr_const(b, member->offset);
        switch (member->kind) {
        case NMO_OBJECT_STATE_MEMBER_VALUE:
            if (memcmp(lhs, rhs, member->size) != 0) return false;
            break;
        case NMO_OBJECT_STATE_MEMBER_ARRAY: {
            const nmo_array_t *la = lhs;
            const nmo_array_t *ra = rhs;
            size_t size = 0;
            if (la->count != ra->count ||
                la->element_size != ra->element_size ||
                !layout_array_bytes(la, &size) ||
                !layout_bytes_equal(la->data, ra->data, size)) {
                return false;
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_CUSTOM:
            if (!member->custom->equals(lhs, rhs)) return false;
            break;
        case NMO_OBJECT_STATE_MEMBER_BYTES:
        case NMO_OBJECT_STATE_MEMBER_COUNTED: {
            const size_t count = layout_counted_count(a, member);
            const void *lhs_data = layout_pointer_member(a, member);
            const void *rhs_data = layout_pointer_member(b, member);
            size_t bytes = 0;
            if ((member->flags & NMO_OBJECT_STATE_MEMBER_OPTIONAL) != 0u) {
                const bool lhs_absent = layout_optional_absent(a, member);
                if (lhs_absent != layout_optional_absent(b, member)) return false;
                if (lhs_absent) break;
            }
            if (count != layout_counted_count(b, member) ||
                !layout_counted_bytes(a, member, &bytes)) {
                return false;
            }
            if (member->record != NULL) {
                if (count > 0 && (lhs_data == NULL || rhs_data == NULL ||
                    !layout_counted_records_equal(
                        member, lhs_data, rhs_data, count))) {
                    return false;
                }
            } else if (!layout_bytes_equal(lhs_data, rhs_data, bytes)) {
                return false;
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_RECORD_PTR: {
            const void *lhs_record = layout_pointer_member(a, member);
            const void *rhs_record = layout_pointer_member(b, member);
            if (lhs_record == rhs_record) break;
            if (lhs_record == NULL || rhs_record == NULL ||
                !layout_members_equal(member->record->members,
                                      member->record->member_count,
                                      lhs_record, rhs_record)) {
                return false;
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_STRING:
            if (!layout_string_equal(layout_pointer_member(a, member),
                                     layout_pointer_member(b, member))) {
                return false;
            }
            break;
        case NMO_OBJECT_STATE_MEMBER_CHUNK:
            if (!layout_chunk_equal(layout_pointer_member(a, member),
                                    layout_pointer_member(b, member))) {
                return false;
            }
            break;
        case NMO_OBJECT_STATE_MEMBER_RECORDS:
            if (!layout_records_equal(member->record, lhs, rhs)) return false;
            break;
        }
    }
    return true;
}

static uint32_t layout_members_hash(
    uint32_t hash,
    const nmo_object_state_member_t *members,
    size_t member_count,
    const void *instance)
{
    for (size_t i = 0; i < member_count; ++i) {
        const nmo_object_state_member_t *member = &members[i];
        if ((member->flags & NMO_OBJECT_STATE_MEMBER_UNCOMPARED) != 0u) continue;
        const void *value = layout_member_ptr_const(instance, member->offset);
        switch (member->kind) {
        case NMO_OBJECT_STATE_MEMBER_VALUE:
            hash = layout_hash_bytes(hash, value, member->size);
            break;
        case NMO_OBJECT_STATE_MEMBER_ARRAY: {
            const nmo_array_t *array = value;
            size_t size = 0;
            hash = layout_hash_bytes(hash, &array->count, sizeof(array->count));
            if (array->data != NULL && layout_array_bytes(array, &size)) {
                hash = layout_hash_bytes(hash, array->data, size);
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_BYTES:
        case NMO_OBJECT_STATE_MEMBER_COUNTED: {
            const size_t count = layout_counted_count(instance, member);
            const void *data = layout_pointer_member(instance, member);
            size_t bytes = 0;
            if ((member->flags & NMO_OBJECT_STATE_MEMBER_OPTIONAL) != 0u) {
                const uint8_t present = !layout_optional_absent(instance, member);
                hash = layout_hash_bytes(hash, &present, sizeof(present));
                if (!present) break;
            }
            hash = layout_hash_bytes(hash, &count, sizeof(count));
            if (data != NULL && member->record != NULL) {
                for (size_t r = 0; r < count; ++r) {
                    hash = layout_members_hash(
                        hash, member->record->members,
                        member->record->member_count,
                        (const uint8_t *)data + r * member->record->size);
                }
            } else if (data != NULL &&
                       layout_counted_bytes(instance, member, &bytes)) {
                hash = layout_hash_bytes(hash, data, bytes);
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_CUSTOM:
            hash = member->custom->hash(hash, value);
            break;
        case NMO_OBJECT_STATE_MEMBER_RECORD_PTR: {
            const void *record = layout_pointer_member(instance, member);
            const uint8_t present = record != NULL;
            hash = layout_hash_bytes(hash, &present, sizeof(present));
            if (record != NULL) {
                hash = layout_members_hash(
                    hash, member->record->members,
                    member->record->member_count, record);
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_STRING: {
            const char *string = layout_pointer_member(instance, member);
            const uint8_t present = string != NULL;
            hash = layout_hash_bytes(hash, &present, sizeof(present));
            if (string != NULL) {
                hash = layout_hash_bytes(hash, string, strlen(string));
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_CHUNK: {
            const nmo_chunk_t *chunk = layout_pointer_member(instance, member);
            const uint8_t present = chunk != NULL;
            hash = layout_hash_bytes(hash, &present, sizeof(present));
            if (chunk != NULL) {
                size_t size = 0;
                const void *data = nmo_chunk_get_data(chunk, &size);
                hash = layout_hash_bytes(hash, &size, sizeof(size));
                if (data != NULL) {
                    hash = layout_hash_bytes(hash, data, size);
                }
            }
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_RECORDS: {
            const nmo_array_t *array = value;
            const nmo_object_state_layout_t *record = member->record;
            hash = layout_hash_bytes(hash, &array->count, sizeof(array->count));
            for (size_t r = 0; r < array->count && array->data != NULL; ++r) {
                hash = layout_members_hash(
                    hash, record->members, record->member_count,
                    (const uint8_t *)array->data + r * record->size);
            }
            break;
        }
        }
    }
    return hash;
}

/* A record array element is plain data plus arena-owned memory, so the array
 * itself needs no per-element teardown. */
static nmo_status_t layout_init_member(
    const nmo_object_state_member_t *member,
    void *instance)
{
    if (member->kind == NMO_OBJECT_STATE_MEMBER_ARRAY) {
        return nmo_array_init(
            layout_member_ptr(instance, member->offset), member->size, 0, NULL);
    }
    if (member->kind == NMO_OBJECT_STATE_MEMBER_RECORDS) {
        return nmo_array_init(
            layout_member_ptr(instance, member->offset),
            member->record->size, 0, NULL);
    }
    return NMO_OK;
}

static bool layout_member_owns_array(const nmo_object_state_member_t *member)
{
    return member->kind == NMO_OBJECT_STATE_MEMBER_ARRAY ||
           member->kind == NMO_OBJECT_STATE_MEMBER_RECORDS;
}

nmo_status_t nmo_object_layout_create(
    const nmo_object_state_layout_t *layout,
    void *instance,
    void *context)
{
    if (layout == NULL || instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_object_layout_create");
    }
    memset(instance, 0, layout->size);
    nmo_status_t result = layout->base_vtable != NULL
        ? layout->base_vtable->create(instance, NULL, context) : NMO_OK;
    if (result != NMO_OK) return result;
    if (layout->set_defaults != NULL) {
        layout->set_defaults(instance);
    }
    for (size_t i = 0; i < layout->member_count; ++i) {
        result = layout_init_member(&layout->members[i], instance);
        if (result != NMO_OK) {
            nmo_object_layout_destroy(layout, instance, context);
            return result;
        }
    }
    NMO_RETURN_OK();
}

void nmo_object_layout_destroy(
    const nmo_object_state_layout_t *layout,
    void *instance,
    void *context)
{
    if (layout == NULL || instance == NULL) return;
    for (size_t i = 0; i < layout->member_count; ++i) {
        const nmo_object_state_member_t *member = &layout->members[i];
        if (layout_member_owns_array(member)) {
            nmo_array_dispose(layout_member_ptr(instance, member->offset));
        }
    }
    if (layout->base_vtable != NULL) {
        layout->base_vtable->destroy(instance, NULL, context);
    }
    memset(instance, 0, layout->size);
}

typedef union layout_owned {
    nmo_array_t array;
    void *pointer;
} layout_owned_t;

static nmo_status_t layout_copy_members_into(
    const nmo_object_state_member_t *members,
    size_t member_count,
    const void *src,
    void *dst,
    nmo_arena_t *arena);

static nmo_status_t layout_copy_owned(
    const nmo_object_state_member_t *member,
    const void *src,
    nmo_arena_t *arena,
    layout_owned_t *out)
{
    switch (member->kind) {
    case NMO_OBJECT_STATE_MEMBER_ARRAY: {
        const nmo_array_t *from = layout_member_ptr_const(src, member->offset);
        return nmo_array_clone(from, &out->array, &from->allocator);
    }
    case NMO_OBJECT_STATE_MEMBER_CUSTOM: {
        out->pointer = calloc(1, member->size);
        if (out->pointer == NULL) return NMO_ERR_NOMEM;
        const nmo_status_t result = member->custom->copy(
            arena, out->pointer, layout_member_ptr_const(src, member->offset));
        if (result != NMO_OK) {
            free(out->pointer);
            out->pointer = NULL;
        }
        return result;
    }
    case NMO_OBJECT_STATE_MEMBER_BYTES:
    case NMO_OBJECT_STATE_MEMBER_COUNTED: {
        size_t bytes = 0;
        out->pointer = NULL;
        if (layout_optional_absent(src, member)) return NMO_OK;
        if (!layout_counted_bytes(src, member, &bytes)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        const void *from = layout_pointer_member(src, member);
        if (bytes == 0) return NMO_OK;
        if (from == NULL || arena == NULL) return NMO_ERR_INVALID_ARGUMENT;
        /* The element type is not known here, so align for any of them. */
        void *copy = nmo_arena_alloc(arena, bytes, NMO_MAX_ALIGN);
        if (copy == NULL) return NMO_ERR_NOMEM;
        memcpy(copy, from, bytes);
        if (member->record != NULL) {
            const nmo_object_state_layout_t *record = member->record;
            const size_t count = layout_counted_count(src, member);
            for (size_t i = 0; i < count; ++i) {
                NMO_RETURN_IF_ERROR(layout_copy_members_into(
                    record->members, record->member_count,
                    (const uint8_t *)from + i * record->size,
                    (uint8_t *)copy + i * record->size, arena));
            }
        }
        out->pointer = copy;
        return NMO_OK;
    }
    case NMO_OBJECT_STATE_MEMBER_RECORD_PTR: {
        const nmo_object_state_layout_t *record = member->record;
        const void *from = layout_pointer_member(src, member);
        out->pointer = NULL;
        if (from == NULL) return NMO_OK;
        if (arena == NULL) return NMO_ERR_INVALID_ARGUMENT;
        void *copy = nmo_arena_alloc(arena, record->size, NMO_MAX_ALIGN);
        if (copy == NULL) return NMO_ERR_NOMEM;
        memcpy(copy, from, record->size);
        NMO_RETURN_IF_ERROR(layout_copy_members_into(
            record->members, record->member_count, from, copy, arena));
        out->pointer = copy;
        return NMO_OK;
    }
    case NMO_OBJECT_STATE_MEMBER_STRING: {
        char *string = NULL;
        const nmo_status_t result = nmo_object_copy_string(
            arena, &string, layout_pointer_member(src, member));
        out->pointer = string;
        return result;
    }
    case NMO_OBJECT_STATE_MEMBER_CHUNK: {
        nmo_chunk_t *chunk = NULL;
        const nmo_status_t result = nmo_object_copy_chunk(
            arena, &chunk, (nmo_chunk_t *)layout_pointer_member(src, member));
        out->pointer = chunk;
        return result;
    }
    case NMO_OBJECT_STATE_MEMBER_RECORDS: {
        const nmo_array_t *from = layout_member_ptr_const(src, member->offset);
        const nmo_object_state_layout_t *record = member->record;
        if (from->count > 0 &&
            (from->data == NULL || from->element_size != record->size)) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_status_t result = nmo_array_init(
            &out->array, record->size, from->count, &from->allocator);
        if (result != NMO_OK) return result;
        nmo_array_set_lifecycle(&out->array, &from->lifecycle);
        void *elements = NULL;
        result = nmo_array_extend(&out->array, from->count, &elements);
        for (size_t i = 0; result == NMO_OK && i < from->count; ++i) {
            const uint8_t *from_record = (const uint8_t *)from->data +
                i * record->size;
            uint8_t *to_record = (uint8_t *)elements + i * record->size;
            memcpy(to_record, from_record, record->size);
            result = layout_copy_members_into(
                record->members, record->member_count,
                from_record, to_record, arena);
        }
        if (result != NMO_OK) nmo_array_dispose(&out->array);
        return result;
    }
    case NMO_OBJECT_STATE_MEMBER_VALUE:
        break;
    }
    return NMO_OK;
}

/* dst already holds the bytes of src; the owned members are replaced by copies. */
static nmo_status_t layout_copy_members_into(
    const nmo_object_state_member_t *members,
    size_t member_count,
    const void *src,
    void *dst,
    nmo_arena_t *arena)
{
    for (size_t i = 0; i < member_count; ++i) {
        const nmo_object_state_member_t *member = &members[i];
        if (member->kind == NMO_OBJECT_STATE_MEMBER_VALUE) continue;
        if (member->kind == NMO_OBJECT_STATE_MEMBER_CUSTOM) {
            NMO_RETURN_IF_ERROR(member->custom->copy(
                arena, layout_member_ptr(dst, member->offset),
                layout_member_ptr_const(src, member->offset)));
            continue;
        }
        layout_owned_t owned;
        memset(&owned, 0, sizeof(owned));
        if (layout_member_owns_array(member)) {
            /* A record holds no array it would have to tear down. */
            return NMO_ERR_INVALID_ARGUMENT;
        }
        NMO_RETURN_IF_ERROR(layout_copy_owned(member, src, arena, &owned));
        if (member->kind == NMO_OBJECT_STATE_MEMBER_RECORDS) {
            memcpy(layout_member_ptr(dst, member->offset), &owned.array,
                   sizeof(owned.array));
        } else {
            memcpy(layout_member_ptr(dst, member->offset), &owned.pointer,
                   sizeof(owned.pointer));
        }
    }
    return NMO_OK;
}

static void layout_dispose_staged(
    const nmo_object_state_layout_t *layout,
    layout_owned_t *staged)
{
    for (size_t i = 0; i < layout->member_count; ++i) {
        if (layout_member_owns_array(&layout->members[i])) {
            nmo_array_dispose(&staged[i].array);
        } else if (layout->members[i].kind == NMO_OBJECT_STATE_MEMBER_CUSTOM) {
            free(staged[i].pointer);
        }
    }
    free(staged);
}

nmo_status_t nmo_object_layout_copy(
    const nmo_object_state_layout_t *layout,
    const void *src,
    void *dst,
    nmo_arena_t *arena)
{
    if (layout == NULL || src == NULL || dst == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (layout->validate != NULL) {
        NMO_RETURN_IF_ERROR(layout->validate(src, NULL, NULL));
    }

    /* Clone owned members first and copy the base in place, which each
     * base vtable does atomically, so a failure leaves dst untouched. */
    layout_owned_t *staged = NULL;
    bool owns_members = false;
    for (size_t i = 0; i < layout->member_count; ++i) {
        owns_members = owns_members ||
            layout->members[i].kind != NMO_OBJECT_STATE_MEMBER_VALUE;
    }
    if (owns_members) {
        staged = calloc(layout->member_count, sizeof(*staged));
        if (staged == NULL) return NMO_ERR_NOMEM;
    }
    nmo_status_t result = NMO_OK;
    for (size_t i = 0; result == NMO_OK && i < layout->member_count; ++i) {
        if (layout->members[i].kind == NMO_OBJECT_STATE_MEMBER_VALUE) continue;
        result = layout_copy_owned(
            &layout->members[i], src, arena, &staged[i]);
    }
    if (result == NMO_OK && layout->base_vtable != NULL) {
        const nmo_type_descriptor_t base_type = {
            .size = (uint32_t)layout->base_size,
        };
        result = layout->base_vtable->copy(
            src, dst, layout->base_size != 0 ? &base_type : NULL, arena);
    }
    if (result != NMO_OK) {
        layout_dispose_staged(layout, staged);
        return result;
    }

    for (size_t i = 0; i < layout->member_count; ++i) {
        const nmo_object_state_member_t *member = &layout->members[i];
        void *to = layout_member_ptr(dst, member->offset);
        switch (member->kind) {
        case NMO_OBJECT_STATE_MEMBER_VALUE:
            memmove(to, layout_member_ptr_const(src, member->offset),
                    member->size);
            break;
        case NMO_OBJECT_STATE_MEMBER_ARRAY:
        case NMO_OBJECT_STATE_MEMBER_RECORDS: {
            nmo_array_t *array = to;
            const nmo_array_t *from =
                layout_member_ptr_const(src, member->offset);
            if (array->data != from->data) {
                nmo_array_dispose(array);
            }
            *array = staged[i].array;
            break;
        }
        case NMO_OBJECT_STATE_MEMBER_CUSTOM:
            memcpy(to, staged[i].pointer, member->size);
            free(staged[i].pointer);
            break;
        default:
            memcpy(to, &staged[i].pointer, sizeof(staged[i].pointer));
            break;
        }
    }
    free(staged);
    return NMO_OK;
}

bool nmo_object_layout_equals(
    const nmo_object_state_layout_t *layout,
    const void *a,
    const void *b)
{
    if (a == b) return true;
    if (layout == NULL || a == NULL || b == NULL) return false;
    if (layout->base_vtable != NULL && !layout->base_vtable->equals(a, b)) {
        return false;
    }
    return layout_members_equal(layout->members, layout->member_count, a, b);
}

uint32_t nmo_object_layout_hash_from(
    const nmo_object_state_layout_t *layout,
    uint32_t hash,
    const void *instance)
{
    if (layout == NULL || instance == NULL) return 0;
    return layout_members_hash(
        hash, layout->members, layout->member_count, instance);
}

uint32_t nmo_object_layout_hash(
    const nmo_object_state_layout_t *layout,
    const void *instance)
{
    if (layout == NULL || instance == NULL) return 0;
    return nmo_object_layout_hash_from(
        layout,
        layout->base_vtable != NULL ? layout->base_vtable->hash(instance)
                                    : 2166136261u,
        instance);
}


nmo_status_t nmo_object_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size)
{
    if (dst == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL destination for byte copy");
    }
    if (!size) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL arena for byte copy");
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
    if (dst == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL destination for array copy");
    }
    if (!count) {
        *dst = NULL;
        NMO_RETURN_OK();
    }
    if (arena == NULL || elem_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arena or element size for array copy");
    }
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL source array for count %u", count);
    }
    if (elem_size > SIZE_MAX / count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Array copy size overflows for count %u", count);
    }
    size_t size = (size_t)count * elem_size;
    /* The element type is not available here, so use the platform's maximum
     * fundamental alignment instead of returning byte-aligned typed arrays. */
    void *mem = nmo_arena_alloc(arena, size, NMO_MAX_ALIGN);
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

nmo_status_t nmo_object_serialize_staged(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context,
    nmo_type_validate_fn validate,
    nmo_type_serialize_fn serialize)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL ||
        serialize == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (validate != NULL) {
        NMO_RETURN_IF_ERROR(validate(instance, type, context));
    }

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = serialize(instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_object_pre_delete_checked(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to pre_delete");
    }
    NMO_RETURN_OK();
}

void nmo_object_post_delete_noop(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

nmo_status_t nmo_object_prepare_dependencies_checked(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_prepare_dependencies_default(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}
