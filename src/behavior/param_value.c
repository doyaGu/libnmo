/**
 * @file param_value.c
 * @brief Parameter value decoding - type-aware buffer interpretation
 *
 * Bridges the type system string converters (nmo_type_value_to_string)
 * with parameter buffer data (nmo_parameter_state_t.buffer_data) to
 * produce human-readable parameter value strings.
 */

#include "behavior/nmo_behavior_view.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_param_guids.h"
#include "object/nmo_manager_guids.h"
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
 * Manager values and keyboard keys
 * ============================================================================ */

/* A Message value is an index into the Message Manager's name table. */
static bool format_message_name(uint32_t index,
                                const nmo_workspace_t *workspace,
                                char *buffer, size_t buffer_size)
{
    char name[256];
    if (workspace == NULL ||
        nmo_workspace_internal_message_name((nmo_workspace_t *)workspace, index,
                                            name, sizeof(name)) != NMO_OK) {
        return false;
    }
    snprintf(buffer, buffer_size, "\"%s\"", name);
    return true;
}

/* DirectInput scan code names, as the Windows key name text spells them. */
static const char *keyboard_key_name(uint32_t key)
{
    static const char *const low[0x59] = {
        [0x01] = "Esc", [0x02] = "1", [0x03] = "2", [0x04] = "3", [0x05] = "4",
        [0x06] = "5", [0x07] = "6", [0x08] = "7", [0x09] = "8", [0x0A] = "9",
        [0x0B] = "0", [0x0C] = "-", [0x0D] = "=", [0x0E] = "Backspace",
        [0x0F] = "Tab", [0x10] = "Q", [0x11] = "W", [0x12] = "E", [0x13] = "R",
        [0x14] = "T", [0x15] = "Y", [0x16] = "U", [0x17] = "I", [0x18] = "O",
        [0x19] = "P", [0x1A] = "[", [0x1B] = "]", [0x1C] = "Enter",
        [0x1D] = "Ctrl", [0x1E] = "A", [0x1F] = "S", [0x20] = "D", [0x21] = "F",
        [0x22] = "G", [0x23] = "H", [0x24] = "J", [0x25] = "K", [0x26] = "L",
        [0x27] = ";", [0x28] = "'", [0x29] = "`", [0x2A] = "Shift",
        [0x2B] = "\\", [0x2C] = "Z", [0x2D] = "X", [0x2E] = "C", [0x2F] = "V",
        [0x30] = "B", [0x31] = "N", [0x32] = "M", [0x33] = ",", [0x34] = ".",
        [0x35] = "/", [0x36] = "Right Shift", [0x37] = "Num *", [0x38] = "Alt",
        [0x39] = "Space", [0x3A] = "Caps Lock", [0x3B] = "F1", [0x3C] = "F2",
        [0x3D] = "F3", [0x3E] = "F4", [0x3F] = "F5", [0x40] = "F6",
        [0x41] = "F7", [0x42] = "F8", [0x43] = "F9", [0x44] = "F10",
        [0x45] = "Pause", [0x46] = "Scroll Lock", [0x47] = "Num 7",
        [0x48] = "Num 8", [0x49] = "Num 9", [0x4A] = "Num -", [0x4B] = "Num 4",
        [0x4C] = "Num 5", [0x4D] = "Num 6", [0x4E] = "Num +", [0x4F] = "Num 1",
        [0x50] = "Num 2", [0x51] = "Num 3", [0x52] = "Num 0", [0x53] = "Num Del",
        [0x56] = "<>", [0x57] = "F11", [0x58] = "F12",
    };
    if (key < sizeof(low) / sizeof(low[0])) {
        return low[key];
    }
    switch (key) {
    case 0x9C: return "Num Enter";
    case 0x9D: return "Right Ctrl";
    case 0xB5: return "Num /";
    case 0xB7: return "Prnt Scrn";
    case 0xB8: return "Right Alt";
    case 0xC5: return "Num Lock";
    case 0xC7: return "Home";
    case 0xC8: return "Up";
    case 0xC9: return "Page Up";
    case 0xCB: return "Left";
    case 0xCD: return "Right";
    case 0xCF: return "End";
    case 0xD0: return "Down";
    case 0xD1: return "Page Down";
    case 0xD2: return "Insert";
    case 0xD3: return "Delete";
    case 0xDB: return "Left Windows";
    case 0xDC: return "Right Windows";
    case 0xDD: return "Application";
    default:   return NULL;
    }
}

static nmo_status_t format_keyboard_key(uint32_t key, char *buffer, size_t buffer_size)
{
    const char *name = keyboard_key_name(key);
    if (key == 0) {
        snprintf(buffer, buffer_size, "(none)");
    } else if (name != NULL) {
        snprintf(buffer, buffer_size, "\"%s\" (%u)", name, (unsigned)key);
    } else {
        snprintf(buffer, buffer_size, "%u", (unsigned)key);
    }
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
        return format_object_ref(nmo_parameter_object_id(param), workspace,
                                 buffer, buffer_size);

    case CKPARAM_MODE_MANAGER: {
        if (nmo_guid_equals(param->manager_guid, NMO_MANAGER_GUID_MESSAGE) &&
            format_message_name(param->manager_value, workspace,
                                buffer, buffer_size)) {
            return NMO_OK;
        }
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

    if (nmo_guid_equals(param->type_guid, CKPGUID_KEY) && data_size >= sizeof(uint32_t)) {
        uint32_t key = 0;
        memcpy(&key, data, sizeof(key));
        return format_keyboard_key(key, buffer, buffer_size);
    }

    /* Look up the type descriptor */
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, param->type_guid);

    if (!type) {
        /* Unknown type - hex fallback */
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
