/**
 * @file ck2dentity_schemas.c
 * @brief CK2dEntity schema implementation
 *
 * Implements (de)serialization for CK2dEntity based on reverse-engineered
 * RCK2dEntity::Load/Save behavior documented in docs/CK2_3D_reverse_notes.md.
 * 
 * Key implementation details:
 * - Modern format (v5+): identifier 0x10F000 contains flags followed by
 *   conditional blocks (homogeneous rect, source rect, z-order, parent)
 * - Legacy format (<v5): separate identifiers for each field
 * - Flags are sanitized with mask 0xFFF8F7FF on load
 * - Homogeneous rect flag (0x200) controls coordinate system
 */

#include "schema/nmo_ck2dentity_schemas.h"
#include "schema/nmo_ckrenderobject_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Read VxRect from chunk (4 floats: x, y, width, height)
 */
static nmo_result_t read_rect(nmo_chunk_t *chunk, nmo_vx_rect_t *rect)
{
    nmo_result_t result;
    
    result = nmo_chunk_read_float(chunk, &rect->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &rect->y);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &rect->width);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &rect->height);
    if (result.code != NMO_OK) return result;
    
    return nmo_result_ok();
}

/**
 * @brief Write VxRect to chunk (4 floats: x, y, width, height)
 */
static nmo_result_t write_rect(nmo_chunk_t *chunk, const nmo_vx_rect_t *rect)
{
    nmo_result_t result;
    
    result = nmo_chunk_write_float(chunk, rect->x);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, rect->y);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, rect->width);
    if (result.code != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, rect->height);
    if (result.code != NMO_OK) return result;
    
    return nmo_result_ok();
}

/* =============================================================================
 * CK2dEntity DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK2dEntity state from chunk (modern format v5+)
 * 
 * Modern format uses identifier 0x10F000 with conditional blocks:
 * 1. DWORD flags (sanitized with 0xFFF8F7FF)
 * 2. VxRect (either homogeneous or regular based on flag 0x200)
 * 3. Optional blocks based on flag bits:
 *    - 0x10000: source rect (VxRect)
 *    - 0x20000: z-order (DWORD)
 *    - 0x40000: parent ID (CK_ID)
 *    - 0x200000: material ID (CK_ID, sprites only)
 */
static nmo_result_t deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state)
{
    nmo_result_t result;
    
    /* Read flags */
    uint32_t raw_flags;
    result = nmo_chunk_read_dword(chunk, &raw_flags);
    if (result.code != NMO_OK) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Failed to read CK2dEntity flags"));
    }
    
    /* Sanitize flags (mask applied by RCK2dEntity::Load) */
    out_state->flags = raw_flags & NMO_CK2DENTITY_FLAGS_MASK;
    
    /* Read rectangle (homogeneous or regular based on flag 0x200) */
    if (out_state->flags & NMO_CK2DENTITY_FLAG_HOMOGENEOUS) {
        out_state->has_homogeneous_rect = true;
        result = read_rect(chunk, &out_state->homogeneous_rect);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read homogeneous rect"));
        }
        /* Note: runtime would compute m_Rect from m_HomogeneousRect via
         * GetHomogeneousRelativeRect, but schema layer preserves serialized form */
    } else {
        out_state->has_homogeneous_rect = false;
        result = read_rect(chunk, &out_state->rect);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read rect"));
        }
    }
    
    /* Optional block: source rect (flag 0x10000) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_SOURCE_RECT) {
        out_state->has_source_rect = true;
        result = read_rect(chunk, &out_state->source_rect);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read source rect"));
        }
    }
    
    /* Optional block: z-order (flag 0x20000) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_Z_ORDER) {
        out_state->has_z_order = true;
        result = nmo_chunk_read_dword(chunk, &out_state->z_order);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read z-order"));
        }
    }
    
    /* Optional block: parent ID (flag 0x40000) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_PARENT) {
        out_state->has_parent = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->parent_id);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read parent ID"));
        }
    }
    
    /* Optional block: material ID (flag 0x200000, sprites only) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_MATERIAL) {
        out_state->has_material = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->material_id);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read material ID"));
        }
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize CK2dEntity state from chunk (legacy format <v5)
 * 
 * Legacy format uses separate identifiers:
 * - 0x4000: flags
 * - 0x8000: origin (x, y as ints)
 * - 0x2000: size (width, height as ints)
 * - 0x1000: source rect (x, y, w, h as ints)
 * - 0x100000: z-order
 * 
 * Integers are converted to floats (fixed-point conversion in original code).
 */
static nmo_result_t deserialize_legacy(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state)
{
    nmo_result_t result;
    
    /* Read flags (identifier 0x4000) */
    nmo_result_t seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_FLAGS);
    if (seek_result.code == NMO_OK) {
        uint32_t raw_flags;
        result = nmo_chunk_read_dword(chunk, &raw_flags);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read legacy flags"));
        }
        out_state->flags = raw_flags & NMO_CK2DENTITY_FLAGS_MASK;
    }
    
    /* Read origin (identifier 0x8000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_ORIGIN);
    if (seek_result.code == NMO_OK) {
        int32_t x, y;
        result = nmo_chunk_read_int(chunk, &x);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read origin x"));
        }
        result = nmo_chunk_read_int(chunk, &y);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read origin y"));
        }
        /* Convert int to float (fixed-point conversion, SDK uses helpers) */
        out_state->rect.x = (float)x;
        out_state->rect.y = (float)y;
    }
    
    /* Read size (identifier 0x2000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_SIZE);
    if (seek_result.code == NMO_OK) {
        int32_t w, h;
        result = nmo_chunk_read_int(chunk, &w);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read size width"));
        }
        result = nmo_chunk_read_int(chunk, &h);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read size height"));
        }
        out_state->rect.width = (float)w;
        out_state->rect.height = (float)h;
    }
    
    /* Read source rect (identifier 0x1000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_SOURCE_RECT);
    if (seek_result.code == NMO_OK) {
        int32_t x, y, w, h;
        result = nmo_chunk_read_int(chunk, &x);
        if (result.code != NMO_OK) goto source_rect_error;
        result = nmo_chunk_read_int(chunk, &y);
        if (result.code != NMO_OK) goto source_rect_error;
        result = nmo_chunk_read_int(chunk, &w);
        if (result.code != NMO_OK) goto source_rect_error;
        result = nmo_chunk_read_int(chunk, &h);
        if (result.code != NMO_OK) goto source_rect_error;
        
        out_state->has_source_rect = true;
        out_state->source_rect.x = (float)x;
        out_state->source_rect.y = (float)y;
        out_state->source_rect.width = (float)w;
        out_state->source_rect.height = (float)h;
        goto source_rect_done;
        
source_rect_error:
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Failed to read source rect"));
source_rect_done:;
    }
    
    /* Read z-order (identifier 0x100000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_Z_ORDER);
    if (seek_result.code == NMO_OK) {
        out_state->has_z_order = true;
        result = nmo_chunk_read_dword(chunk, &out_state->z_order);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to read z-order"));
        }
    }
    
    return nmo_result_ok();
}

/**
 * @brief Deserialize CK2dEntity state from chunk
 * 
 * Dispatches to modern or legacy deserializer based on chunk data version.
 */
nmo_result_t nmo_ck2dentity_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ck2dentity_deserialize"));
    }
    
    memset(out_state, 0, sizeof(*out_state));
    
    /* First deserialize parent CKRenderObject data */
    nmo_result_t result = nmo_ckrenderobject_deserialize(
        chunk, arena, &out_state->render_object);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Check chunk version to choose format */
    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    
    if (data_version >= 5) {
        /* Modern format: identifier 0x10F000 */
        nmo_result_t seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_MODERN);
        if (seek_result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Missing modern CK2dEntity chunk (0x10F000)"));
        }
        result = deserialize_modern(chunk, arena, out_state);
    } else {
        /* Legacy format: separate identifiers */
        result = deserialize_legacy(chunk, arena, out_state);
    }
    
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Preserve remaining data as raw tail for round-trip */
    size_t current_pos = nmo_chunk_get_position(chunk);
    size_t chunk_size = nmo_chunk_get_data_size(chunk);
    
    if (current_pos < chunk_size) {
        size_t remaining = chunk_size - current_pos;
        out_state->raw_tail = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
        if (out_state->raw_tail) {
            size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk,
                out_state->raw_tail, remaining);
            if (bytes_read == remaining) {
                out_state->raw_tail_size = remaining;
            } else {
                out_state->raw_tail = NULL;
                out_state->raw_tail_size = 0;
            }
        }
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * CK2dEntity SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK2dEntity state to chunk (modern format v5+)
 * 
 * Writes identifier 0x10F000 with conditional blocks based on presence flags.
 */
static nmo_result_t serialize_modern(
    const nmo_ck2dentity_state_t *state,
    nmo_chunk_t *chunk)
{
    nmo_result_t result;
    
    /* Write modern chunk identifier */
    result = nmo_chunk_write_identifier(chunk, NMO_CK2DENTITY_CHUNK_MODERN);
    if (result.code != NMO_OK) return result;
    
    /* Build flags with optional block indicators */
    uint32_t flags = state->flags;
    if (state->has_source_rect) flags |= NMO_CK2DENTITY_FLAG_SOURCE_RECT;
    if (state->has_z_order) flags |= NMO_CK2DENTITY_FLAG_Z_ORDER;
    if (state->has_parent) flags |= NMO_CK2DENTITY_FLAG_PARENT;
    if (state->has_material) flags |= NMO_CK2DENTITY_FLAG_MATERIAL;
    
    /* Write flags */
    result = nmo_chunk_write_dword(chunk, flags);
    if (result.code != NMO_OK) return result;
    
    /* Write rectangle (homogeneous or regular) */
    if (state->has_homogeneous_rect) {
        result = write_rect(chunk, &state->homogeneous_rect);
    } else {
        result = write_rect(chunk, &state->rect);
    }
    if (result.code != NMO_OK) return result;
    
    /* Optional blocks */
    if (state->has_source_rect) {
        result = write_rect(chunk, &state->source_rect);
        if (result.code != NMO_OK) return result;
    }
    
    if (state->has_z_order) {
        result = nmo_chunk_write_dword(chunk, state->z_order);
        if (result.code != NMO_OK) return result;
    }
    
    if (state->has_parent) {
        result = nmo_chunk_write_object_id(chunk, state->parent_id);
        if (result.code != NMO_OK) return result;
    }
    
    if (state->has_material) {
        result = nmo_chunk_write_object_id(chunk, state->material_id);
        if (result.code != NMO_OK) return result;
    }
    
    return nmo_result_ok();
}

/**
 * @brief Serialize CK2dEntity state to chunk
 * 
 * Always uses modern format (v5+) for simplicity.
 */
nmo_result_t nmo_ck2dentity_serialize(
    const nmo_ck2dentity_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ck2dentity_serialize"));
    }
    
    /* Serialize parent CKRenderObject data */
    nmo_result_t result = nmo_ckrenderobject_serialize(&in_state->render_object, out_chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Serialize CK2dEntity data (always use modern format) */
    result = serialize_modern(in_state, out_chunk);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Write raw tail if present */
    if (in_state->raw_tail && in_state->raw_tail_size > 0) {
        result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->raw_tail, in_state->raw_tail_size);
        if (result.code != NMO_OK) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Failed to write raw tail"));
        }
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

nmo_result_t nmo_register_ck2dentity_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ck2dentity_schemas"));
    }
    
    /* TODO: Register schema types when schema builder is ready */
    /* For now, schemas are registered via deserialize/serialize functions */
    
    return nmo_result_ok();
}
