/**
 * @file cklight_schemas.c
 * @brief CKLight schema implementation
 *
 * Implements schema for RCKLight based on reverse engineering analysis.
 * 
 * Serialization format (from CK2_3D.dll analysis):
 * 
 * Modern format (version ??):
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

static void nmo_light_set_defaults(nmo_light_state_t *state) {
    if (state == NULL) {
        return;
    }

    /* Mirrors RCKLight ctor defaults (see CKRenderEngine/src/CKLight.cpp). */
    state->flags = 0x100u;
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
    state->has_light_power_chunk = 0;
}

static void nmo_light_apply_nonspot_defaults(nmo_light_state_t *state) {
    if (state == NULL) {
        return;
    }
    /* In the engine, non-spot lights keep ctor defaults for these fields. */
    state->light_data.inner_spot_cone = 0.69813174f;
    state->light_data.outer_spot_cone = 0.78539819f;
    state->light_data.falloff = 1.0f;
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    light,
    nmo_light_state_t,
    do { \
        nmo_light_set_defaults(state); \
    } while (0),
    ((void)0))

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
    NMO_FIELD(nmo_light_state_t, light_power, CKPGUID_FLOAT)
};

/* =============================================================================
 * CKLight DESERIALIZATION
 * ============================================================================= */

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
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LIGHTDATA);
    if (result == NMO_OK) {
        // Read Type|Flags packed DWORD
        uint32_t packed_type_flags;
        result = nmo_chunk_read_dword(chunk, &packed_type_flags);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read Type|Flags");
        }

        // Unpack: Type in low 8 bits, Flags in high 24 bits
        out_state->light_data.type = (VXLIGHT_TYPE)(packed_type_flags & 0xFFu);
        out_state->flags = packed_type_flags & ~0xFFu;

        // Validate type
        if (out_state->light_data.type < VX_LIGHTPOINT ||
            out_state->light_data.type > VX_LIGHTDIREC) {
            out_state->light_data.type = VX_LIGHTPOINT;  // Default to point light
        }

        // Read Diffuse color (packed ARGB)
        uint32_t diffuse_argb;
        result = nmo_chunk_read_dword(chunk, &diffuse_argb);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read diffuse color");
        }
        nmo_color_from_argb32(diffuse_argb, &out_state->light_data.diffuse);

        // Read attenuation parameters
        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation0);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read attenuation0");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation1);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read attenuation1");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation2);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read attenuation2");
        }

        // Read range
        result = nmo_chunk_read_float(chunk, &out_state->light_data.range);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read range");
        }

        // Conditional: spotlight parameters (only if Type == VX_LIGHTSPOT)
        if (out_state->light_data.type == VX_LIGHTSPOT) {
            result = nmo_chunk_read_float(chunk, &out_state->light_data.outer_spot_cone);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read outer spot cone");
            }

            result = nmo_chunk_read_float(chunk, &out_state->light_data.inner_spot_cone);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read inner spot cone");
            }

            result = nmo_chunk_read_float(chunk, &out_state->light_data.falloff);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read falloff");
            }
        } else {
            // Default spotlight parameters for non-spotlights
            nmo_light_apply_nonspot_defaults(out_state);
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    // Optional: light power (CK_STATESAVE_LIGHTDATA2)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LIGHTDATA2);
    if (result == NMO_OK) {
        out_state->has_light_power_chunk = 1;
        result = nmo_chunk_read_float(chunk, &out_state->light_power);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read light power");
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
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LIGHTDATA);
    if (result == NMO_OK) {
        // Read Type
        uint32_t type;
        result = nmo_chunk_read_dword(chunk, &type);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read type");
        }
        out_state->light_data.type = (VXLIGHT_TYPE)type;

        // Validate type
        if (out_state->light_data.type < VX_LIGHTPOINT ||
            out_state->light_data.type > VX_LIGHTDIREC) {
            out_state->light_data.type = VX_LIGHTPOINT;
        }

        // Read Diffuse.rgb (3 floats)
        result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.r);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read diffuse.r");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.g);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read diffuse.g");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.b);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read diffuse.b");
        }

        // Skip alpha
        float skip_alpha;
        result = nmo_chunk_read_float(chunk, &skip_alpha);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to skip alpha");
        }
        out_state->light_data.diffuse.a = 1.0f;  // Default alpha

        // Read Active state (stored in flags)
        int32_t active;
        result = nmo_chunk_read_int(chunk, &active);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read active state");
        }
        // Store active in flags (bit mapping matches engine: 0x100)
        if (active) {
            out_state->flags |= 0x100u;
        } else {
            out_state->flags &= ~0x100u;
        }

        // Read Specular flag
        int32_t specular;
        result = nmo_chunk_read_int(chunk, &specular);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read specular flag");
        }
        if (specular) {
            out_state->flags |= 0x200u;
        } else {
            out_state->flags &= ~0x200u;
        }

        // Read attenuation parameters
        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation0);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read attenuation0");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation1);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read attenuation1");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation2);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read attenuation2");
        }

        // Read range
        result = nmo_chunk_read_float(chunk, &out_state->light_data.range);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read range");
        }

        // Read spotlight parameters (always present in legacy format)
        result = nmo_chunk_read_float(chunk, &out_state->light_data.outer_spot_cone);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read outer spot cone");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.inner_spot_cone);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read inner spot cone");
        }

        result = nmo_chunk_read_float(chunk, &out_state->light_data.falloff);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read falloff");
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

    out_state->has_light_power_chunk = 0;

    // Check data version to dispatch to modern or legacy deserializer
    uint32_t version = nmo_chunk_get_data_version(chunk);
    
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
 * @brief Serialize CKLight state to chunk (always uses modern format)
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

    // First serialize parent CK3dEntity data
    nmo_status_t result = nmo_3dentity_serialize(&state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_LIGHTONLY) == 0) {
            return NMO_OK;
        }
    }

    // Write identifier (CK_STATESAVE_LIGHTDATA)
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_LIGHTDATA);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write light data identifier");
    }

    // Pack Type|Flags (engine stores flags in upper 24 bits)
    uint32_t packed_type_flags = ((uint32_t)state->light_data.type & 0xFFu) | (state->flags & ~0xFFu);
    result = nmo_chunk_write_dword(chunk, packed_type_flags);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write Type|Flags");
    }

    // Pack and write Diffuse color as ARGB (engine forces alpha to 0xFF)
    uint32_t diffuse_argb = nmo_color_to_argb32_opaque(&state->light_data.diffuse);
    result = nmo_chunk_write_dword(chunk, diffuse_argb);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write diffuse color");
    }

    // Write attenuation parameters
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation0);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write attenuation0");
    }
    
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation1);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write attenuation1");
    }
    
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation2);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write attenuation2");
    }

    // Write range
    result = nmo_chunk_write_float(chunk, state->light_data.range);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write range");
    }

    // Conditional: spotlight parameters (only if Type == VX_LIGHTSPOT)
    if (state->light_data.type == VX_LIGHTSPOT) {
        result = nmo_chunk_write_float(chunk, state->light_data.outer_spot_cone);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write outer spot cone");
        }
        
        result = nmo_chunk_write_float(chunk, state->light_data.inner_spot_cone);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write inner spot cone");
        }
        
        result = nmo_chunk_write_float(chunk, state->light_data.falloff);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write falloff");
        }
    }

    // Optional: light power
    if (state->light_power != 1.0f) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_LIGHTDATA2);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write power identifier");
        }
        
        result = nmo_chunk_write_float(chunk, state->light_power);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to write light power");
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_light_serialize(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || chunk == NULL || chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = chunk->class_id;
    staged->data_version = chunk->data_version;
    staged->chunk_version = chunk->chunk_version;
    staged->chunk_class_id = chunk->chunk_class_id;
    staged->chunk_options = chunk->chunk_options;
    staged->file_context = chunk->file_context;

    nmo_status_t result = nmo_light_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_light_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

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

static nmo_status_t nmo_light_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_light_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_light_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS(light, nmo_light_state_t)

nmo_type_vtable_t nmo_light_vtable = {
    .prepare_dependencies = nmo_light_prepare_dependencies,
    .remap_dependencies = nmo_light_remap_dependencies,
    .pre_delete = nmo_light_pre_delete,
    .post_delete = nmo_light_post_delete,
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
