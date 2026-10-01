/**
 * @file cklight_schemas.c
 * @brief CKLight schema implementation
 *
 * Implements schema for RCKLight based on reverse engineering analysis.
 * 
 * Serialization format (from CK2_3D.dll analysis):
 * 
 * Modern format (data version 5 and later):
 * - Identifier CK_STATESAVE_LIGHTDATA (0x00400000): Core light data
 *   - DWORD: Type (low 8 bits) | Flags (high 24 bits)
 *   - DWORD: Diffuse color (packed ARGB)
 *   - float: Attenuation0
 *   - float: Attenuation1
 *   - float: Attenuation2
 *   - float: Range
 *   - IF Type == VX_LIGHTSPOT:
 *     - float: OuterSpotCone
 *     - float: InnerSpotCone
 *     - float: Falloff
 * 
 * - Identifier CK_STATESAVE_LIGHTDATA2 (0x00800000) (optional): Light power
 *   - float: m_LightPower (only if != 1.0)
 * 
 * Legacy format (version <5):
 * - Identifier CK_STATESAVE_LIGHTDATA (0x00400000): Full light data
 *   - DWORD: Type
 *   - float: Diffuse.r, Diffuse.g, Diffuse.b
 *   - float: (skip alpha)
 *   - int: Active state
 *   - int: Specular flag
 *   - float: Attenuation0, Attenuation1, Attenuation2
 *   - float: Range
 *   - float: OuterSpotCone, InnerSpotCone, Falloff
 *   - m_LightPower defaults to 1.0
 */

#include "object/builtin/nmo_light_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_light_set_defaults(void *instance)
{
    nmo_light_state_t *state = instance;
    if (state == NULL) {
        return;
    }

    /* Mirrors RCKLight ctor defaults (see CKRenderEngine/src/CKLight.cpp). */
    state->flags = NMO_LIGHT_FLAG_ACTIVE;
    state->light_power = 1.0f;

    state->light_data.type = VX_LIGHTPOINT;

    state->light_data.diffuse.r = 1.0f;
    state->light_data.diffuse.g = 1.0f;
    state->light_data.diffuse.b = 1.0f;
    state->light_data.diffuse.a = 1.0f;

    state->light_data.specular.r = 0.0f;
    state->light_data.specular.g = 0.0f;
    state->light_data.specular.b = 0.0f;
    state->light_data.specular.a = 0.0f;

    state->light_data.ambient.r = 0.0f;
    state->light_data.ambient.g = 0.0f;
    state->light_data.ambient.b = 0.0f;
    state->light_data.ambient.a = 0.0f;

    state->light_data.range = 5000.0f;
    state->light_data.falloff = 1.0f;
    state->light_data.attenuation0 = 1.0f;
    state->light_data.attenuation1 = 0.0f;
    state->light_data.attenuation2 = 0.0f;

    state->light_data.inner_spot_cone = 0.69813174f;
    state->light_data.outer_spot_cone = 0.78539819f;
    state->has_light_data_chunk = 1;
    state->has_light_power_chunk = 0;
    state->light_data_is_legacy = 0;
    state->legacy_diffuse_alpha = 1.0f;
}

void nmo_light_apply_nonspot_defaults(nmo_light_state_t *state) {
    if (state == NULL) {
        return;
    }
    /* In the engine, non-spot lights keep ctor defaults for these fields. */
    state->light_data.inner_spot_cone = 0.69813174f;
    state->light_data.outer_spot_cone = 0.78539819f;
    state->light_data.falloff = 1.0f;
}

static const nmo_object_state_member_t nmo_light_members[] = {
    NMO_STATE_VALUE(nmo_light_state_t, light_data),
    NMO_STATE_VALUE(nmo_light_state_t, flags),
    NMO_STATE_VALUE(nmo_light_state_t, light_power),
    NMO_STATE_VALUE(nmo_light_state_t, has_light_data_chunk),
    NMO_STATE_VALUE(nmo_light_state_t, has_light_power_chunk),
    NMO_STATE_VALUE(nmo_light_state_t, light_data_is_legacy),
    NMO_STATE_VALUE(nmo_light_state_t, legacy_diffuse_alpha),
    NMO_STATE_VALUE(nmo_light_state_t, has_raw_light_data),
    NMO_STATE_VALUE(nmo_light_state_t, raw_type_dword),
    NMO_STATE_VALUE(nmo_light_state_t, raw_diffuse_argb)
};

static const nmo_object_state_layout_t nmo_light_layout = {
    .size = sizeof(nmo_light_state_t),
    .base_vtable = &nmo_3dentity_vtable,
    .base_size = sizeof(nmo_3dentity_state_t),
    .members = nmo_light_members,
    .member_count =
        sizeof(nmo_light_members) / sizeof(nmo_light_members[0]),
    .set_defaults = nmo_light_set_defaults,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(light, nmo_light_layout)

static nmo_status_t nmo_light_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static void nmo_light_dispose_base_arrays(nmo_light_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->entity.base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_light_fields[] = {
    NMO_FIELD_NAMED("entity", offsetof(nmo_light_state_t, entity),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("light_data", offsetof(nmo_light_state_t, light_data),
                    sizeof(nmo_light_data_t), NMO_GUID_STRUCT_CKLIGHTDATA,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_light_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_light_state_t, light_power, CKPGUID_FLOAT),
    NMO_FIELD(nmo_light_state_t, has_light_data_chunk, CKPGUID_BOOL),
    NMO_FIELD(nmo_light_state_t, has_light_power_chunk, CKPGUID_BOOL),
    NMO_FIELD(nmo_light_state_t, light_data_is_legacy, CKPGUID_BOOL),
    NMO_FIELD(nmo_light_state_t, legacy_diffuse_alpha, CKPGUID_FLOAT)
};

/* =============================================================================
 * CKLight DESERIALIZATION
 * ============================================================================= */

/**
 * RCKLight::Load keeps a type of 1 to 3 and turns anything else into a point
 * light. The value only decides the spot cone reads, which happen for type 2
 * before this normalisation, so the layout is the same either way.
 */
static VXLIGHT_TYPE nmo_light_type_from_file(uint32_t type)
{
    return type >= VX_LIGHTPOINT && type <= VX_LIGHTDIREC
        ? (VXLIGHT_TYPE)type
        : VX_LIGHTPOINT;
}

/**
 * @brief Deserialize CKLight state from chunk (modern format v5+)
 */
static nmo_status_t nmo_light_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_light_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    
    // Seek to light data identifier (CK_STATESAVE_LIGHTDATA)
    size_t payload_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_LIGHTDATA, &payload_dwords);
    if (result == NMO_OK) {
        if (payload_dwords < 6u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_light_data_chunk = 1;
        // Read Type|Flags packed DWORD
        uint32_t packed_type_flags;
        result = nmo_chunk_read_dword(chunk, &packed_type_flags);
        if (result != NMO_OK) {
            return result;
        }

        // Unpack: Type in low 8 bits, Flags in high 24 bits
        out_state->light_data.type = nmo_light_type_from_file(packed_type_flags & 0xFFu);
        out_state->flags = packed_type_flags & ~0xFFu;
        const size_t expected_dwords =
            out_state->light_data.type == VX_LIGHTSPOT ? 9u : 6u;
        if (payload_dwords < expected_dwords) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }

        // Read Diffuse color (packed ARGB)
        uint32_t diffuse_argb;
        result = nmo_chunk_read_dword(chunk, &diffuse_argb);
        if (result != NMO_OK) {
            return result;
        }
        nmo_color_from_argb32(diffuse_argb, &out_state->light_data.diffuse);
        out_state->has_raw_light_data = 1;
        out_state->raw_type_dword = packed_type_flags;
        out_state->raw_diffuse_argb = diffuse_argb;

        // Read attenuation parameters
        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation0);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation1);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation2);
        if (result != NMO_OK) {
            return result;
        }

        // Read range
        result = nmo_chunk_read_float(chunk, &out_state->light_data.range);
        if (result != NMO_OK) {
            return result;
        }

        // Conditional: spotlight parameters (only if Type == VX_LIGHTSPOT)
        if (out_state->light_data.type == VX_LIGHTSPOT) {
            result = nmo_chunk_read_float(chunk, &out_state->light_data.outer_spot_cone);
            if (result != NMO_OK) {
                return result;
            }

            result = nmo_chunk_read_float(chunk, &out_state->light_data.inner_spot_cone);
            if (result != NMO_OK) {
                return result;
            }

            result = nmo_chunk_read_float(chunk, &out_state->light_data.falloff);
            if (result != NMO_OK) {
                return result;
            }
        } else {
            // Default spotlight parameters for non-spotlights
            nmo_light_apply_nonspot_defaults(out_state);
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    // Optional: light power (CK_STATESAVE_LIGHTDATA2)
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_LIGHTDATA2, &payload_dwords);
    if (result == NMO_OK) {
        if (payload_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_light_power_chunk = 1;
        result = nmo_chunk_read_float(chunk, &out_state->light_power);
        if (result != NMO_OK) {
            return result;
        }
    } else if (result == NMO_ERR_NOT_FOUND) {
        // Default to 1.0 if not present
        out_state->light_power = 1.0f;
        out_state->has_light_power_chunk = 0;
    } else return result;
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CKLight state from chunk (legacy format <v5)
 */
static nmo_status_t nmo_light_deserialize_legacy(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_light_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    
    // Seek to light data identifier (CK_STATESAVE_LIGHTDATA)
    size_t payload_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_LIGHTDATA, &payload_dwords);
    if (result == NMO_OK) {
        if (payload_dwords < 14u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_light_data_chunk = 1;
        // Read Type
        uint32_t type;
        result = nmo_chunk_read_dword(chunk, &type);
        if (result != NMO_OK) {
            return result;
        }
        out_state->light_data.type = nmo_light_type_from_file(type);

        // Read Diffuse.rgb (3 floats)
        result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.r);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.g);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.b);
        if (result != NMO_OK) {
            return result;
        }

        // Preserve the legacy alpha field even though it has no runtime meaning.
        result = nmo_chunk_read_float(
            chunk, &out_state->legacy_diffuse_alpha);
        if (result != NMO_OK) {
            return result;
        }
        out_state->light_data.diffuse.a = 1.0f;  // Default alpha

        // Read Active state (stored in flags)
        int32_t active;
        result = nmo_chunk_read_int(chunk, &active);
        if (result != NMO_OK) {
            return result;
        }
        // Store active in flags (bit mapping matches engine: 0x100)
        if (active) {
            out_state->flags |= NMO_LIGHT_FLAG_ACTIVE;
        } else {
            out_state->flags &= ~NMO_LIGHT_FLAG_ACTIVE;
        }

        // Read Specular flag
        int32_t specular;
        result = nmo_chunk_read_int(chunk, &specular);
        if (result != NMO_OK) {
            return result;
        }
        if (specular) {
            out_state->flags |= NMO_LIGHT_FLAG_SPECULAR;
        } else {
            out_state->flags &= ~NMO_LIGHT_FLAG_SPECULAR;
        }

        // Read attenuation parameters
        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation0);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation1);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation2);
        if (result != NMO_OK) {
            return result;
        }

        // Read range
        result = nmo_chunk_read_float(chunk, &out_state->light_data.range);
        if (result != NMO_OK) {
            return result;
        }

        // Read spotlight parameters (always present in legacy format)
        result = nmo_chunk_read_float(chunk, &out_state->light_data.outer_spot_cone);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.inner_spot_cone);
        if (result != NMO_OK) {
            return result;
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.falloff);
        if (result != NMO_OK) {
            return result;
        }

        // Legacy format always has power = 1.0
        out_state->light_power = 1.0f;
        out_state->has_light_power_chunk = 0;
    } else if (result != NMO_ERR_NOT_FOUND) return result;
    
    NMO_RETURN_OK();
}

/**
 * @brief Main deserialize function (dispatches to modern/legacy)
 */
static nmo_status_t nmo_light_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_light_state_t *out_state = (nmo_light_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKLight deserialize");
    }

    // First deserialize parent CK3dEntity data
    nmo_status_t result = nmo_3dentity_deserialize(&out_state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_light_data_chunk = 0;
    out_state->has_light_power_chunk = 0;

    // Check data version to dispatch to modern or legacy deserializer
    uint32_t version = nmo_chunk_get_data_version(chunk);
    out_state->light_data_is_legacy = version < 5u;
    
    if (version < 5) {
        return nmo_light_deserialize_legacy(chunk, arena, out_state);
    } else {
        return nmo_light_deserialize_modern(chunk, arena, out_state);
    }
}

nmo_status_t nmo_light_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_light_state_t *out_state = (nmo_light_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_light_state_t decoded;
    nmo_status_t result = nmo_light_create(&decoded, type, context);
    if (result != NMO_OK) return result;

    result = nmo_light_deserialize_internal(&decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_light_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_light_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKLight SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKLight state to chunk
 */
static nmo_status_t nmo_light_serialize_internal(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_light_state_t *state = (const nmo_light_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!state || !chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKLight serialize");
    }

    NMO_RETURN_IF_ERROR(nmo_light_validate(state, type, context));

    const bool is_file = nmo_object_serialize_is_file(chunk, context);
    const bool write_light = is_file ||
        (nmo_serialize_context_get_save_flags(context) &
         CK_STATESAVE_LIGHTONLY) != 0;
    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    const bool write_legacy = is_file && data_version < 5u &&
        (data_version != 0u || state->light_data_is_legacy);
    if (data_version == 0u && !write_legacy) {
        chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
    }
    const bool has_default_data =
        state->light_data.type == VX_LIGHTPOINT &&
        state->flags == NMO_LIGHT_FLAG_ACTIVE &&
        state->light_data.diffuse.r == 1.0f &&
        state->light_data.diffuse.g == 1.0f &&
        state->light_data.diffuse.b == 1.0f &&
        state->light_data.attenuation0 == 1.0f &&
        state->light_data.attenuation1 == 0.0f &&
        state->light_data.attenuation2 == 0.0f &&
        state->light_data.range == 5000.0f &&
        state->light_data.outer_spot_cone == 0.78539819f &&
        state->light_data.inner_spot_cone == 0.69813174f &&
        state->light_data.falloff == 1.0f;

    if (write_legacy &&
        (state->has_light_power_chunk || state->light_power != 1.0f)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Legacy light layout cannot store a light power section");
    }
    /* The diffuse alpha is not checked: CK reads it but always writes 0xFF. */
    if (write_light && (state->flags & 0xFFu) != 0u) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Light data cannot be serialized losslessly");
    }
    if (write_light && !write_legacy &&
        state->legacy_diffuse_alpha != 1.0f) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Modern light layout cannot store legacy diffuse alpha");
    }
    if (write_light && (!is_file || !write_legacy) &&
        state->light_data.type != VX_LIGHTSPOT &&
        (state->light_data.outer_spot_cone != 0.78539819f ||
         state->light_data.inner_spot_cone != 0.69813174f ||
         state->light_data.falloff != 1.0f)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Modern non-spot light layout cannot store spot parameters");
    }
    if (write_legacy && (state->flags & ~(NMO_LIGHT_FLAG_ACTIVE | NMO_LIGHT_FLAG_SPECULAR)) != 0u) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Legacy light layout cannot store the requested flags");
    }

    // First serialize parent CK3dEntity data
    nmo_status_t result = nmo_3dentity_serialize(&state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (!write_light) return NMO_OK;

    const bool write_data = !is_file || state->has_light_data_chunk ||
        !has_default_data;
    if (write_data && write_legacy) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            chunk, CK_STATESAVE_LIGHTDATA));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
            chunk, (uint32_t)state->light_data.type));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.diffuse.r));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.diffuse.g));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.diffuse.b));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->legacy_diffuse_alpha));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
            chunk, (state->flags & NMO_LIGHT_FLAG_ACTIVE) != 0u));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(
            chunk, (state->flags & NMO_LIGHT_FLAG_SPECULAR) != 0u));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.attenuation0));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.attenuation1));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.attenuation2));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.range));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.outer_spot_cone));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.inner_spot_cone));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            chunk, state->light_data.falloff));
        NMO_RETURN_OK();
    }

    if (!write_data) goto write_power;

    // Write identifier (CK_STATESAVE_LIGHTDATA)
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_LIGHTDATA);
    if (result != NMO_OK) {
        return result;
    }

    // Pack Type|Flags (engine stores flags in upper 24 bits)
    uint32_t type_byte = (uint32_t)state->light_data.type & 0xFFu;
    if (state->has_raw_light_data &&
        nmo_light_type_from_file(state->raw_type_dword & 0xFFu) == state->light_data.type) {
        type_byte = state->raw_type_dword & 0xFFu;
    }
    uint32_t packed_type_flags = type_byte | (state->flags & ~0xFFu);
    result = nmo_chunk_write_dword(chunk, packed_type_flags);
    if (result != NMO_OK) {
        return result;
    }

    // Pack and write Diffuse color as ARGB (engine forces alpha to 0xFF)
    uint32_t diffuse_argb = nmo_color_to_argb32_opaque(&state->light_data.diffuse);
    if (state->has_raw_light_data) {
        nmo_color_t raw_color;
        nmo_color_from_argb32(state->raw_diffuse_argb, &raw_color);
        if (memcmp(&raw_color, &state->light_data.diffuse, sizeof(raw_color)) == 0) {
            diffuse_argb = state->raw_diffuse_argb;
        }
    }
    result = nmo_chunk_write_dword(chunk, diffuse_argb);
    if (result != NMO_OK) {
        return result;
    }

    // Write attenuation parameters
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation0);
    if (result != NMO_OK) {
        return result;
    }
    
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation1);
    if (result != NMO_OK) {
        return result;
    }
    
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation2);
    if (result != NMO_OK) {
        return result;
    }

    // Write range
    result = nmo_chunk_write_float(chunk, state->light_data.range);
    if (result != NMO_OK) {
        return result;
    }

    // Conditional: spotlight parameters (only if Type == VX_LIGHTSPOT)
    if (state->light_data.type == VX_LIGHTSPOT) {
        result = nmo_chunk_write_float(chunk, state->light_data.outer_spot_cone);
        if (result != NMO_OK) {
            return result;
        }
        
        result = nmo_chunk_write_float(chunk, state->light_data.inner_spot_cone);
        if (result != NMO_OK) {
            return result;
        }
        
        result = nmo_chunk_write_float(chunk, state->light_data.falloff);
        if (result != NMO_OK) {
            return result;
        }
    }

write_power:
    // Optional: light power
    if ((!write_legacy && state->light_power != 1.0f) ||
        (is_file && state->has_light_power_chunk)) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_LIGHTDATA2);
        if (result != NMO_OK) {
            return result;
        }
        
        result = nmo_chunk_write_float(chunk, state->light_power);
        if (result != NMO_OK) {
            return result;
        }
    }

    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_light)

NMO_DEFINE_OBJECT_PREPARE_DEFAULT(nmo_light)

nmo_status_t nmo_light_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_light_remap_dependencies");
    }

    nmo_light_state_t *light_state = (nmo_light_state_t *)instance;

    nmo_status_t result = nmo_3dentity_remap_dependencies(&light_state->entity, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    return nmo_object_default_validate(light_state, NULL, NULL);
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static nmo_status_t nmo_light_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_light_state_t *state = instance;
    NMO_RETURN_IF_ERROR(nmo_3dentity_vtable.validate(
        &state->entity, NULL, context));
    if (state->light_data.type < VX_LIGHTPOINT ||
        state->light_data.type > VX_LIGHTDIREC) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Light data cannot be serialized losslessly");
    }
    NMO_RETURN_OK();
}

nmo_type_vtable_t nmo_light_vtable = {
    .prepare_dependencies = nmo_light_prepare_dependencies,
    .remap_dependencies = nmo_light_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_light_create,
        nmo_light_destroy,
        nmo_light_serialize,
        nmo_light_deserialize,
        nmo_light_copy,
        nmo_light_validate,
        nmo_light_equals,
        nmo_light_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_light_type,
    CKPGUID_LIGHT,
    "CKLight",
    NMO_CID_LIGHT,
    CKPGUID_3DENTITY,
    nmo_light_state_t,
    &nmo_light_vtable,
    nmo_light_fields)
