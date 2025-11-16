/**
 * @file cklight_schemas.c
 * @brief CKLight schema implementation
 *
 * Implements schema for RCKLight based on reverse engineering analysis.
 * 
 * Serialization format (from CK2_3D.dll analysis):
 * 
 * Modern format (version ≥5):
 * - Identifier 0x400000: Core light data
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
 * - Identifier 0x800000 (optional): Light power
 *   - float: m_LightPower (only if != 1.0)
 * 
 * Legacy format (version <5):
 * - Identifier 0x400000: Full light data
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

#include "schema/nmo_cklight_schemas.h"
#include "schema/nmo_ck3dentity_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Convert ARGB DWORD to VxColor (implementation)
 */
void nmo_vx_color_from_argb(uint32_t argb, nmo_vx_color_t *out_color) {
    if (!out_color) return;
    
    out_color->a = ((argb >> 24) & 0xFF) / 255.0f;
    out_color->r = ((argb >> 16) & 0xFF) / 255.0f;
    out_color->g = ((argb >> 8) & 0xFF) / 255.0f;
    out_color->b = (argb & 0xFF) / 255.0f;
}

/**
 * @brief Convert VxColor to ARGB DWORD (implementation)
 */
uint32_t nmo_vx_color_to_argb(const nmo_vx_color_t *color) {
    if (!color) return 0;
    
    uint8_t a = (uint8_t)(color->a * 255.0f);
    uint8_t r = (uint8_t)(color->r * 255.0f);
    uint8_t g = (uint8_t)(color->g * 255.0f);
    uint8_t b = (uint8_t)(color->b * 255.0f);
    
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* =============================================================================
 * CKLight DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKLight state from chunk (modern format v5+)
 */
static nmo_result_t nmo_cklight_deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklight_state_t *out_state)
{
    nmo_result_t result;
    
    // Seek to light data identifier 0x400000
    result = nmo_chunk_seek_identifier(chunk, 0x400000);
    if (result.code != NMO_OK) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Missing light data identifier 0x400000"));
    }
    
    // Read Type|Flags packed DWORD
    uint32_t packed_type_flags;
    result = nmo_chunk_read_dword(chunk, &packed_type_flags);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read Type|Flags");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Unpack: Type in low 8 bits, Flags in high 24 bits
    out_state->light_data.type = (nmo_vx_light_type_t)(packed_type_flags & 0xFF);
    out_state->flags = packed_type_flags & 0xFFFFFF00;
    
    // Validate type
    if (out_state->light_data.type < NMO_LIGHT_POINT ||
        out_state->light_data.type > NMO_LIGHT_DIRECTIONAL) {
        out_state->light_data.type = NMO_LIGHT_POINT;  // Default to point light
    }
    
    // Read Diffuse color (packed ARGB)
    uint32_t diffuse_argb;
    result = nmo_chunk_read_dword(chunk, &diffuse_argb);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read diffuse color");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    nmo_vx_color_from_argb(diffuse_argb, &out_state->light_data.diffuse);
    
    // Read attenuation parameters
    result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation0);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read attenuation0");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation1);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read attenuation1");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation2);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read attenuation2");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Read range
    result = nmo_chunk_read_float(chunk, &out_state->light_data.range);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read range");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Conditional: spotlight parameters (only if Type == VX_LIGHTSPOT)
    if (out_state->light_data.type == NMO_LIGHT_SPOT) {
        result = nmo_chunk_read_float(chunk, &out_state->light_data.outer_spot_cone);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read outer spot cone");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        result = nmo_chunk_read_float(chunk, &out_state->light_data.inner_spot_cone);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read inner spot cone");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        result = nmo_chunk_read_float(chunk, &out_state->light_data.falloff);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read falloff");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
    } else {
        // Default spotlight parameters for non-spotlights
        out_state->light_data.outer_spot_cone = 0.0f;
        out_state->light_data.inner_spot_cone = 0.0f;
        out_state->light_data.falloff = 0.0f;
    }
    
    // Optional: light power (identifier 0x800000)
    result = nmo_chunk_seek_identifier(chunk, 0x800000);
    if (result.code == NMO_OK) {
        result = nmo_chunk_read_float(chunk, &out_state->light_power);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read light power");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
    } else {
        // Default to 1.0 if not present
        out_state->light_power = 1.0f;
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize CKLight state from chunk (legacy format <v5)
 */
static nmo_result_t nmo_cklight_deserialize_legacy(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklight_state_t *out_state)
{
    nmo_result_t result;
    
    // Seek to light data identifier 0x400000
    result = nmo_chunk_seek_identifier(chunk, 0x400000);
    if (result.code != NMO_OK) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Missing light data identifier 0x400000"));
    }
    
    // Read Type
    uint32_t type;
    result = nmo_chunk_read_dword(chunk, &type);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read type");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    out_state->light_data.type = (nmo_vx_light_type_t)type;
    
    // Validate type
    if (out_state->light_data.type < NMO_LIGHT_POINT ||
        out_state->light_data.type > NMO_LIGHT_DIRECTIONAL) {
        out_state->light_data.type = NMO_LIGHT_POINT;
    }
    
    // Read Diffuse.rgb (3 floats)
    result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.r);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read diffuse.r");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.g);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read diffuse.g");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.diffuse.b);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read diffuse.b");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Skip alpha
    float skip_alpha;
    result = nmo_chunk_read_float(chunk, &skip_alpha);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to skip alpha");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    out_state->light_data.diffuse.a = 1.0f;  // Default alpha
    
    // Read Active state (stored in flags)
    int32_t active;
    result = nmo_chunk_read_int(chunk, &active);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read active state");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    // Store active in flags (implementation-specific bit mapping)
    out_state->flags = active ? 0x100 : 0;
    
    // Read Specular flag
    int32_t specular;
    result = nmo_chunk_read_int(chunk, &specular);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read specular flag");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    if (specular) {
        out_state->flags |= 0x200;
    }
    
    // Read attenuation parameters
    result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation0);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read attenuation0");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation1);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read attenuation1");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.attenuation2);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read attenuation2");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Read range
    result = nmo_chunk_read_float(chunk, &out_state->light_data.range);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read range");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Read spotlight parameters (always present in legacy format)
    result = nmo_chunk_read_float(chunk, &out_state->light_data.outer_spot_cone);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read outer spot cone");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.inner_spot_cone);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read inner spot cone");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_read_float(chunk, &out_state->light_data.falloff);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read falloff");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    // Legacy format always has power = 1.0
    out_state->light_power = 1.0f;
    
    return nmo_result_ok();
}

/**
 * @brief Main deserialize function (dispatches to modern/legacy)
 */
static nmo_result_t nmo_cklight_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklight_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKLight deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    // First deserialize parent CK3dEntity data
    nmo_result_t result = nmo_get_ck3dentity_deserialize()(
        chunk, arena, &out_state->entity);
    if (result.code != NMO_OK) {
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
static nmo_result_t nmo_cklight_serialize(
    const nmo_cklight_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena)
{
    if (!state || !chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKLight serialize"));
    }

    // First serialize parent CK3dEntity data
    nmo_result_t result = nmo_get_ck3dentity_serialize()(
        &state->entity, chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    // Write identifier 0x400000
    result = nmo_chunk_write_identifier(chunk, 0x400000);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write light data identifier");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Pack Type|Flags
    uint32_t packed_type_flags = (uint32_t)state->light_data.type | state->flags;
    result = nmo_chunk_write_dword(chunk, packed_type_flags);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write Type|Flags");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Pack and write Diffuse color as ARGB
    uint32_t diffuse_argb = nmo_vx_color_to_argb(&state->light_data.diffuse);
    result = nmo_chunk_write_dword(chunk, diffuse_argb);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write diffuse color");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Write attenuation parameters
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation0);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write attenuation0");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation1);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write attenuation1");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }
    
    result = nmo_chunk_write_float(chunk, state->light_data.attenuation2);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write attenuation2");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Write range
    result = nmo_chunk_write_float(chunk, state->light_data.range);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to write range");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Conditional: spotlight parameters (only if Type == VX_LIGHTSPOT)
    if (state->light_data.type == NMO_LIGHT_SPOT) {
        result = nmo_chunk_write_float(chunk, state->light_data.outer_spot_cone);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write outer spot cone");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        result = nmo_chunk_write_float(chunk, state->light_data.inner_spot_cone);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write inner spot cone");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        result = nmo_chunk_write_float(chunk, state->light_data.falloff);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write falloff");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
    }

    // Optional: light power (only if != 1.0)
    if (state->light_power != 1.0f) {
        result = nmo_chunk_write_identifier(chunk, 0x800000);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write power identifier");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
        
        result = nmo_chunk_write_float(chunk, state->light_power);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write light power");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKLight FINISH LOADING
 * ============================================================================= */

/**
 * @brief Finish loading CKLight (resolve references, validate data)
 */
static nmo_result_t nmo_cklight_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    if (!state || !arena || !repository) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKLight finish_loading"));
    }

    nmo_cklight_state_t *light_state = (nmo_cklight_state_t *)state;

    // First finish loading parent CK3dEntity
    nmo_result_t result = nmo_get_ck3dentity_finish_loading()(
        &light_state->entity, arena, repository);
    if (result.code != NMO_OK) {
        return result;
    }

    // Validate light type
    if (light_state->light_data.type < NMO_LIGHT_POINT ||
        light_state->light_data.type > NMO_LIGHT_DIRECTIONAL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid light type"));
    }

    // Validate attenuation parameters (should be non-negative)
    if (light_state->light_data.attenuation0 < 0.0f ||
        light_state->light_data.attenuation1 < 0.0f ||
        light_state->light_data.attenuation2 < 0.0f) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Negative attenuation parameters"));
    }

    // Validate range
    if (light_state->light_data.range < 0.0f) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Negative light range"));
    }

    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

nmo_cklight_deserialize_fn nmo_get_cklight_deserialize(void) {
    return nmo_cklight_deserialize;
}

nmo_cklight_serialize_fn nmo_get_cklight_serialize(void) {
    return nmo_cklight_serialize;
}

nmo_cklight_finish_loading_fn nmo_get_cklight_finish_loading(void) {
    return nmo_cklight_finish_loading;
}

/**
 * @brief Register CKLight schema
 * 
 * Registers CKLight state structure schema with type system.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_cklight_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cklight_schemas"));
    }

    /* Get base types */
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "float");
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    
    if (float_type == NULL || uint32_type == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    /* Register CKLight state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKLightState",
                                                      sizeof(nmo_cklight_state_t),
                                                      alignof(nmo_cklight_state_t));
    
    /* Light data fields */
    nmo_builder_add_field_ex(&builder, "type", uint32_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, type),
                            0);
    
    nmo_builder_add_field_ex(&builder, "diffuse_r", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, diffuse) + offsetof(nmo_vx_color_t, r),
                            0);
    
    nmo_builder_add_field_ex(&builder, "specular_r", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, specular) + offsetof(nmo_vx_color_t, r),
                            0);
    
    nmo_builder_add_field_ex(&builder, "ambient_r", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, ambient) + offsetof(nmo_vx_color_t, r),
                            0);
    
    nmo_builder_add_field_ex(&builder, "range", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, range),
                            0);
    
    nmo_builder_add_field_ex(&builder, "attenuation0", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, attenuation0),
                            0);
    
    nmo_builder_add_field_ex(&builder, "attenuation1", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, attenuation1),
                            0);
    
    nmo_builder_add_field_ex(&builder, "attenuation2", float_type,
                            offsetof(nmo_cklight_state_t, light_data) + offsetof(nmo_ck_light_data_t, attenuation2),
                            0);
    
    /* Flags and power */
    nmo_builder_add_field_ex(&builder, "flags", uint32_type,
                            offsetof(nmo_cklight_state_t, flags),
                            0);
    
    nmo_builder_add_field_ex(&builder, "light_power", float_type,
                            offsetof(nmo_cklight_state_t, light_power),
                            0);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}
