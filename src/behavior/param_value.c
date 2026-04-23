/**
 * @file param_value.c
 * @brief Parameter value decoding 闁?type-aware buffer interpretation
 *
 * Bridges the type system string converters (nmo_type_value_to_string)
 * with parameter buffer data (nmo_parameter_state_t.buffer_data) to
 * produce human-readable parameter value strings.
 */

#include "behavior/nmo_behavior_view.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_guids.h"
#include "core/nmo_guid.h"
#include "core/nmo_hex.h"
#include "core/nmo_error.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "../runtime/runtime_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Storage mode names
 * ============================================================================ */

const char *nmo_behavior_param_mode_to_string(nmo_parameter_mode_t mode)
{
    switch (mode) {
    case CKPARAM_MODE_BUFFER:   return "buffer";
    case CKPARAM_MODE_OBJECT:   return "object";
    case CKPARAM_MODE_SUBCHUNK: return "subchunk";
    case CKPARAM_MODE_MANAGER:  return "manager";
    case CKPARAM_MODE_NONE:     return "none";
    default:                    return "unknown";
    }
}

/* ============================================================================
 * Type name resolution
 * ============================================================================ */

const char *nmo_behavior_param_type_name(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry)
{
    if (!param || !registry) {
        return NULL;
    }
    return nmo_type_registry_guid_to_name(registry, param->type_guid);
}

/* ============================================================================
 * Hex fallback for unknown types
 * ============================================================================ */

static nmo_status_t format_hex_preview(
    const void *data, size_t size,
    char *buffer, size_t buffer_size)
{
    if (buffer_size < 4) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "Buffer too small for hex preview");
    }

    size_t preview_bytes = size;
    bool truncated = false;
    /* Each byte takes 2 hex chars + 1 space; cap at what fits */
    size_t max_bytes = (buffer_size - 4) / 3; /* room for "..." + NUL */
    if (preview_bytes > max_bytes) {
        preview_bytes = max_bytes;
        truncated = true;
    }
    if (preview_bytes > 32) {
        preview_bytes = 32;
        truncated = true;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    size_t pos = 0;
    for (size_t i = 0; i < preview_bytes && pos + 3 < buffer_size; i++) {
        if (i > 0) {
            buffer[pos++] = ' ';
        }
        nmo_hex_write_byte(&buffer[pos], bytes[i], false);
        pos += 2;
    }
    if (truncated && pos + 4 <= buffer_size) {
        buffer[pos++] = '.';
        buffer[pos++] = '.';
        buffer[pos++] = '.';
    }
    buffer[pos] = '\0';
    return NMO_OK;
}

static nmo_status_t format_raw_string_buffer(
    const void *data, size_t size,
    char *buffer, size_t buffer_size)
{
    if (!data || !buffer || buffer_size < 3) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for string buffer preview");
    }

    const unsigned char *bytes = (const unsigned char *)data;
    size_t text_size = size;
    if (text_size > 0 && bytes[text_size - 1] == '\0') {
        text_size--;
    }

    size_t pos = 0;
    buffer[pos++] = '"';

    for (size_t i = 0; i < text_size && pos + 2 < buffer_size; i++) {
        unsigned char c = bytes[i];
        const char *escape = NULL;
        char hex_escape[5];

        switch (c) {
        case '"':  escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\n': escape = "\\n";  break;
        case '\r': escape = "\\r";  break;
        case '\t': escape = "\\t";  break;
        default:
            break;
        }

        if (escape) {
            size_t len = strlen(escape);
            if (pos + len + 2 > buffer_size) {
                break;
            }
            memcpy(buffer + pos, escape, len);
            pos += len;
        } else if (c >= 32 && c <= 126) {
            if (pos + 3 > buffer_size) {
                break;
            }
            buffer[pos++] = (char)c;
        } else {
            if (pos + 6 > buffer_size) {
                break;
            }
            snprintf(hex_escape, sizeof(hex_escape), "\\x%02X", (unsigned)c);
            memcpy(buffer + pos, hex_escape, 4);
            pos += 4;
        }
    }

    buffer[pos++] = '"';
    buffer[pos] = '\0';
    return NMO_OK;
}

/* ============================================================================
 * Object ID formatting with optional name resolution
 * ============================================================================ */

static nmo_status_t format_object_ref(
    nmo_object_id_t id,
    const nmo_workspace_t *workspace,
    char *buffer, size_t buffer_size)
{
    nmo_object_repository_t *repo =
        workspace ? nmo_workspace_internal_repository(workspace) : NULL;

    if (id == 0) {
        snprintf(buffer, buffer_size, "(null)");
        return NMO_OK;
    }

    if (repo) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
        if (obj) {
            const char *name = nmo_object_get_name(obj);
            if (name && name[0] != '\0') {
                snprintf(buffer, buffer_size, "#%u (%s)", (unsigned)id, name);
                return NMO_OK;
            }
        }
    }

    snprintf(buffer, buffer_size, "#%u", (unsigned)id);
    return NMO_OK;
}

/* ============================================================================
 * Core value-to-string conversion
 * ============================================================================ */

nmo_status_t nmo_behavior_param_value_to_string(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace,
    char *buffer,
    size_t buffer_size)
{
    if (!param || !registry || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_behavior_param_value_to_string");
    }

    buffer[0] = '\0';

    if (!param->has_state) {
        snprintf(buffer, buffer_size, "(no state)");
        return NMO_OK;
    }

    switch (param->mode) {
    case CKPARAM_MODE_NONE:
        snprintf(buffer, buffer_size, "(no value)");
        return NMO_OK;

    case CKPARAM_MODE_OBJECT:
        return format_object_ref(param->object_id, workspace,
                                 buffer, buffer_size);

    case CKPARAM_MODE_MANAGER: {
        char guid_buf[24];
        nmo_guid_format(param->manager_guid, guid_buf, sizeof(guid_buf));
        snprintf(buffer, buffer_size, "manager{%s} = %u",
                 guid_buf, (unsigned)param->manager_value);
        return NMO_OK;
    }

    case CKPARAM_MODE_SUBCHUNK:
        if (param->subchunk) {
            snprintf(buffer, buffer_size, "<subchunk>");
        } else {
            snprintf(buffer, buffer_size, "<subchunk, empty>");
        }
        return NMO_OK;

    case CKPARAM_MODE_BUFFER:
        break; /* handled below */

    default:
        snprintf(buffer, buffer_size, "(unknown mode %d)", (int)param->mode);
        return NMO_OK;
    }

    /* --- BUFFER mode: resolve type and decode --- */

    const void *data = param->buffer_data.data;
    size_t data_size = param->buffer_data.count;

    if (!data || data_size == 0) {
        snprintf(buffer, buffer_size, "(empty buffer)");
        return NMO_OK;
    }

    if (nmo_guid_equals(param->type_guid, CKPGUID_STRING)) {
        return format_raw_string_buffer(data, data_size, buffer, buffer_size);
    }

    /* Look up the type descriptor */
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, param->type_guid);

    if (!type) {
        /* Unknown type 闁?hex fallback */
        return format_hex_preview(data, data_size, buffer, buffer_size);
    }

    if (type->size > 0 && data_size < type->size) {
        return format_hex_preview(data, data_size, buffer, buffer_size);
    }

    /* Unified dispatch: vtable -> category fallback -> hex */
    nmo_status_t st = nmo_type_value_to_string(
        data, type, registry, buffer, buffer_size);
    if (st == NMO_OK) {
        return NMO_OK;
    }

    /* Fallback for types where the converter fails */
    return format_hex_preview(data, data_size, buffer, buffer_size);
}

/* ============================================================================
 * Summary formatter
 * ============================================================================ */

nmo_status_t nmo_behavior_param_format_summary(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_workspace_t *workspace,
    char *buffer,
    size_t buffer_size)
{
    if (!param || !registry || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_param_value_format_summary");
    }

    char guid_fallback[24];
    const char *type_name = nmo_behavior_param_type_name(param, registry);
    if (!type_name) {
        nmo_guid_format(param->type_guid, guid_fallback, sizeof(guid_fallback));
        type_name = guid_fallback;
    }

    char value_buf[512];
    nmo_behavior_param_value_to_string(param, registry, workspace,
                              value_buf, sizeof(value_buf));

    snprintf(buffer, buffer_size, "%s = %s (%s)",
             type_name, value_buf, nmo_behavior_param_mode_to_string(param->mode));

    return NMO_OK;
}
