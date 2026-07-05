/**
 * @file type_string.c
 * @brief Type value string facade
 */

#include "type/nmo_type_string.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_reflection.h"
#include "type_value_internal.h"
#include "core/nmo_error.h"
#include "core/nmo_color.h"

enum { NMO_TYPE_STRING_MAX_DEPTH = 6 };

nmo_status_t nmo_type_value_to_string_depth_internal(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth)
{
    if (!value || !type || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for type_value_to_string");
    }

    if (depth > NMO_TYPE_STRING_MAX_DEPTH) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Maximum type string recursion depth exceeded");
    }

    if (!type->vtable || !type->vtable->to_string) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                         "Type has no to_string vtable handler");
    }

    return type->vtable->to_string(
        value, type, registry, buffer, buffer_size, depth);
}

nmo_status_t nmo_type_value_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size)
{
    return nmo_type_value_to_string_depth_internal(
        value, type, registry, buffer, buffer_size, 0);
}

nmo_status_t nmo_type_value_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for type_value_from_string");
    }

    if (!type->vtable || !type->vtable->from_string) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                         "Type has no from_string vtable handler");
    }

    return type->vtable->from_string(value, type, registry, string);
}

static nmo_status_t resolve_field_access(
    void *state,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *field_name,
    const nmo_type_field_t **out_field,
    const nmo_type_descriptor_t **out_field_type,
    void **out_field_ptr)
{
    const nmo_type_field_t *field = nmo_type_get_field_by_name(type, field_name);
    if (!field) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_type_descriptor_t *field_type =
        nmo_type_registry_find_by_guid(registry, field->type_guid);
    if (!field_type) {
        return NMO_ERR_NOT_FOUND;
    }

    void *field_ptr = nmo_field_get_ptr(state, field);
    if (!field_ptr) {
        return NMO_ERR_INVALID_STATE;
    }

    *out_field = field;
    *out_field_type = field_type;
    *out_field_ptr = field_ptr;
    return NMO_OK;
}

nmo_status_t nmo_type_set_field(
    void *state,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *field_name,
    const char *value_str)
{
    if (!state || !type || !registry || !field_name || !value_str) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_type_field_t *field = NULL;
    const nmo_type_descriptor_t *field_type = NULL;
    void *field_ptr = NULL;
    nmo_status_t status = resolve_field_access(
        state, type, registry, field_name, &field, &field_type, &field_ptr);
    if (status != NMO_OK) {
        return status;
    }

    if (nmo_guid_equals(field->type_guid, CKPGUID_COLOR) &&
        field->size == sizeof(uint32_t) &&
        field_type->size == sizeof(nmo_color_t)) {
        nmo_color_t parsed;
        status = nmo_color_from_string(&parsed, value_str);
        if (status != NMO_OK) return status;
        *(uint32_t *)field_ptr = nmo_color_to_argb32(&parsed);
        return NMO_OK;
    }

    return nmo_type_value_from_string(field_ptr, field_type, registry, value_str);
}

nmo_status_t nmo_type_get_field(
    const void *state,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *field_name,
    char *out_buf,
    size_t buf_size)
{
    if (!state || !type || !registry || !field_name || !out_buf || buf_size == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    out_buf[0] = '\0';

    const nmo_type_field_t *field = NULL;
    const nmo_type_descriptor_t *field_type = NULL;
    void *field_ptr = NULL;
    nmo_status_t status = resolve_field_access(
        (void *)state, type, registry, field_name, &field, &field_type, &field_ptr);
    if (status != NMO_OK) {
        return status;
    }

    return nmo_type_value_to_string(field_ptr, field_type, registry, out_buf, buf_size);
}
