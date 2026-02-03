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

#include "object/nmo_cklight_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_cklight_set_defaults(nmo_cklight_state_t *state) {
    if (state == NULL) {
        return;
    }

    /* Mirrors RCKLight ctor defaults (see CKRenderEngine/src/CKLight.cpp). */
    state->flags = 0x100u;
    state->light_power = 1.0f;

    state->light_data.type = NMO_LIGHT_POINT;

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
}

static void nmo_cklight_apply_nonspot_defaults(nmo_cklight_state_t *state) {
    if (state == NULL) {
        return;
    }
    /* In the engine, non-spot lights keep ctor defaults for these fields. */
    state->light_data.inner_spot_cone = 0.69813174f;
    state->light_data.outer_spot_cone = 0.78539819f;
    state->light_data.falloff = 1.0f;
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    cklight,
    nmo_cklight_state_t,
    do { \
        nmo_cklight_set_defaults(state); \
    } while (0),
    ((void)0))

/* =============================================================================
 * CKLight DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKLight state from chunk (modern format v5+)
 */
static nmo_status_t nmo_cklight_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklight_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    
    // Seek to light data identifier (CK_STATESAVE_LIGHTDATA)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LIGHTDATA);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Missing light data identifier CK_STATESAVE_LIGHTDATA");
    }
    
    // Read Type|Flags packed DWORD
    uint32_t packed_type_flags;
    result = nmo_chunk_read_dword(chunk, &packed_type_flags);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read Type|Flags");
    }
    
    // Unpack: Type in low 8 bits, Flags in high 24 bits
    out_state->light_data.type = (nmo_vx_light_type_t)(packed_type_flags & 0xFFu);
    out_state->flags = packed_type_flags & ~0xFFu;
    
    // Validate type
    if (out_state->light_data.type < NMO_LIGHT_POINT ||
        out_state->light_data.type > NMO_LIGHT_DIRECTIONAL) {
        out_state->light_data.type = NMO_LIGHT_POINT;  // Default to point light
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
    if (out_state->light_data.type == NMO_LIGHT_SPOT) {
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
        nmo_cklight_apply_nonspot_defaults(out_state);
    }
    
    // Optional: light power (CK_STATESAVE_LIGHTDATA2)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LIGHTDATA2);
    if (result == NMO_OK) {
        result = nmo_chunk_read_float(chunk, &out_state->light_power);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read light power");
        }
    } else {
        // Default to 1.0 if not present
        out_state->light_power = 1.0f;
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CKLight state from chunk (legacy format <v5)
 */
static nmo_status_t nmo_cklight_deserialize_legacy(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklight_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    
    // Seek to light data identifier (CK_STATESAVE_LIGHTDATA)
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LIGHTDATA);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Missing light data identifier CK_STATESAVE_LIGHTDATA");
    }
    
    // Read Type
    uint32_t type;
    result = nmo_chunk_read_dword(chunk, &type);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read type");
    }
    out_state->light_data.type = (nmo_vx_light_type_t)type;
    
    // Validate type
    if (out_state->light_data.type < NMO_LIGHT_POINT ||
        out_state->light_data.type > NMO_LIGHT_DIRECTIONAL) {
        out_state->light_data.type = NMO_LIGHT_POINT;
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
    
    NMO_RETURN_OK();
}

/**
 * @brief Main deserialize function (dispatches to modern/legacy)
 */
nmo_status_t nmo_cklight_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cklight_state_t *out_state = (nmo_cklight_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKLight deserialize");
    }

    // First deserialize parent CK3dEntity data
    nmo_status_t result = nmo_ck3dentity_deserialize(&out_state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    // Check data version to dispatch to modern or legacy deserializer
    uint32_t version = nmo_chunk_get_data_version(chunk);
    
    if (version < 5) {
        return nmo_cklight_deserialize_legacy(chunk, arena, out_state);
    } else {
        return nmo_cklight_deserialize_modern(chunk, arena, out_state);
    }
}

/* =============================================================================
 * CKLight SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKLight state to chunk (always uses modern format)
 */
nmo_status_t nmo_cklight_serialize(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cklight_state_t *state = (const nmo_cklight_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!state || !chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKLight serialize");
    }

    // First serialize parent CK3dEntity data
    nmo_status_t result = nmo_ck3dentity_serialize(&state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
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
    if (state->light_data.type == NMO_LIGHT_SPOT) {
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

    // Optional: light power (only if != 1.0)
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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cklight,
    nmo_cklight_state_t,
    nmo_cklight_serialize,
    nmo_cklight_deserialize,
    NMO_GUID_CKLIGHT,
    "CKLight",
    NMO_CID_LIGHT,
    NMO_GUID_CK3DENTITY
)

/* =============================================================================
 * CKLight FINISH LOADING
 * ============================================================================= */

/**
 * @brief Finish loading CKLight (resolve references, validate data)
 */
nmo_status_t nmo_cklight_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance || !arena || !repository) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKLight finish_loading");
    }

    nmo_cklight_state_t *light_state = (nmo_cklight_state_t *)instance;

    // First finish loading parent CK3dEntity
    nmo_status_t result = nmo_ck3dentity_finish_loading(&light_state->entity, arena, repository);
    if (result != NMO_OK) {
        return result;
    }

    // Validate light type
    if (light_state->light_data.type < NMO_LIGHT_POINT ||
        light_state->light_data.type > NMO_LIGHT_DIRECTIONAL) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid light type");
    }

    // Validate attenuation parameters (should be non-negative)
    if (light_state->light_data.attenuation0 < 0.0f ||
        light_state->light_data.attenuation1 < 0.0f ||
        light_state->light_data.attenuation2 < 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Negative attenuation parameters");
    }

    // Validate range
    if (light_state->light_data.range < 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Negative light range");
    }

    NMO_RETURN_OK();
}


