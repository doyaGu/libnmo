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

#include "object/nmo_ck2dentity_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
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
 * @brief Read rect from chunk (4 floats: left, top, right, bottom)
 */
static nmo_status_t read_rect(nmo_chunk_t *chunk, nmo_rect_t *rect)
{
    nmo_status_t result;
    
    result = nmo_chunk_read_float(chunk, &rect->left);
    if (result != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &rect->top);
    if (result != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &rect->right);
    if (result != NMO_OK) return result;
    
    result = nmo_chunk_read_float(chunk, &rect->bottom);
    if (result != NMO_OK) return result;
    
    NMO_RETURN_OK();
}

/**
 * @brief Write rect to chunk (4 floats: left, top, right, bottom)
 */
static nmo_status_t write_rect(nmo_chunk_t *chunk, const nmo_rect_t *rect)
{
    nmo_status_t result;
    
    result = nmo_chunk_write_float(chunk, rect->left);
    if (result != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, rect->top);
    if (result != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, rect->right);
    if (result != NMO_OK) return result;
    
    result = nmo_chunk_write_float(chunk, rect->bottom);
    if (result != NMO_OK) return result;
    
    NMO_RETURN_OK();
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
static nmo_status_t deserialize_modern(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    
    /* Read flags */
    uint32_t raw_flags;
    result = nmo_chunk_read_dword(chunk, &raw_flags);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read CK2dEntity flags");
    }
    
    /* Sanitize flags (mask applied by RCK2dEntity::Load) */
    out_state->flags = raw_flags & NMO_CK2DENTITY_FLAGS_MASK;
    
    /* Read rectangle (homogeneous or regular based on flag 0x200) */
    if (out_state->flags & NMO_CK2DENTITY_FLAG_HOMOGENEOUS) {
        out_state->has_homogeneous_rect = true;
        result = read_rect(chunk, &out_state->homogeneous_rect);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read homogeneous rect");
        }
        /* Note: runtime would compute m_Rect from m_HomogeneousRect via
         * GetHomogeneousRelativeRect, but schema layer preserves serialized form */
    } else {
        out_state->has_homogeneous_rect = false;
        result = read_rect(chunk, &out_state->rect);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read rect");
        }
    }
    
    /* Optional block: source rect (flag reserved0) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_SOURCE_RECT) {
        out_state->has_source_rect = true;
        result = read_rect(chunk, &out_state->source_rect);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read source rect");
        }
    }
    
    /* Optional block: z-order (flag reserved1) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_Z_ORDER) {
        out_state->has_z_order = true;
        result = nmo_chunk_read_int(chunk, (int32_t *)&out_state->z_order);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read z-order");
        }
    }
    
    /* Optional block: parent ID (flag reserved2) */
    if (raw_flags & NMO_CK2DENTITY_FLAG_PARENT) {
        out_state->has_parent = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->parent_id);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read parent ID");
        }
    }
    
    NMO_RETURN_OK();
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
static nmo_status_t deserialize_legacy(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck2dentity_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    
    /* Read flags (identifier 0x4000) */
    nmo_status_t seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_FLAGS);
    if (seek_result == NMO_OK) {
        uint32_t raw_flags;
        result = nmo_chunk_read_dword(chunk, &raw_flags);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read legacy flags");
        }
        out_state->flags = raw_flags & NMO_CK2DENTITY_FLAGS_MASK;
    }
    
    /* Read origin (identifier 0x8000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_ORIGIN);
    if (seek_result == NMO_OK) {
        if (out_state->flags & NMO_CK2DENTITY_FLAG_HOMOGENEOUS) {
            out_state->has_homogeneous_rect = true;
            result = nmo_chunk_read_float(chunk, &out_state->homogeneous_rect.left);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read homogeneous origin x");
            }
            result = nmo_chunk_read_float(chunk, &out_state->homogeneous_rect.top);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read homogeneous origin y");
            }
        } else {
            int32_t x, y;
            result = nmo_chunk_read_int(chunk, &x);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read origin x");
            }
            result = nmo_chunk_read_int(chunk, &y);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read origin y");
            }
            /* Convert int to float (fixed-point conversion, SDK uses helpers) */
            out_state->rect.left = (float)x;
            out_state->rect.top = (float)y;
        }
    }
    
    /* Read size (identifier 0x2000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_SIZE);
    if (seek_result == NMO_OK) {
        if (out_state->flags & NMO_CK2DENTITY_FLAG_HOMOGENEOUS) {
            float w, h;
            out_state->has_homogeneous_rect = true;
            result = nmo_chunk_read_float(chunk, &w);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read homogeneous width");
            }
            result = nmo_chunk_read_float(chunk, &h);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read homogeneous height");
            }
            out_state->homogeneous_rect.right = out_state->homogeneous_rect.left + w;
            out_state->homogeneous_rect.bottom = out_state->homogeneous_rect.top + h;
        } else {
            int32_t w, h;
            result = nmo_chunk_read_int(chunk, &w);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read size width");
            }
            result = nmo_chunk_read_int(chunk, &h);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read size height");
            }
            out_state->rect.right = out_state->rect.left + (float)w;
            out_state->rect.bottom = out_state->rect.top + (float)h;
        }
    }
    
    /* Read source rect (identifier 0x1000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_SOURCE_RECT);
    if (seek_result == NMO_OK) {
        int32_t x, y, w, h;
        result = nmo_chunk_read_int(chunk, &x);
        if (result != NMO_OK) goto source_rect_error;
        result = nmo_chunk_read_int(chunk, &y);
        if (result != NMO_OK) goto source_rect_error;
        result = nmo_chunk_read_int(chunk, &w);
        if (result != NMO_OK) goto source_rect_error;
        result = nmo_chunk_read_int(chunk, &h);
        if (result != NMO_OK) goto source_rect_error;
        
        out_state->has_source_rect = true;
        out_state->source_rect.right = (float)x;
        out_state->source_rect.left = (float)y;
        out_state->source_rect.top = (float)w;
        out_state->source_rect.bottom = (float)h;
        goto source_rect_done;
        
source_rect_error:
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read source rect");
source_rect_done:;
    }
    
    /* Read z-order (identifier 0x100000) */
    seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_Z_ORDER);
    if (seek_result == NMO_OK) {
        out_state->has_z_order = true;
        result = nmo_chunk_read_int(chunk, (int32_t *)&out_state->z_order);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read z-order");
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CK2dEntity state from chunk
 * 
 * Dispatches to modern or legacy deserializer based on chunk data version.
 */
nmo_status_t nmo_ck2dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ck2dentity_state_t *out_state = (nmo_ck2dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ck2dentity_deserialize");
    }
    
    memset(out_state, 0, sizeof(*out_state));
    
    /* First deserialize parent CKRenderObject data */
    nmo_status_t result = nmo_ckrenderobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Check chunk version to choose format */
    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    
    if (data_version >= 5) {
        /* Modern format: identifier 0x10F000 */
        nmo_status_t seek_result = nmo_chunk_seek_identifier(chunk, NMO_CK2DENTITY_CHUNK_MODERN);
        if (seek_result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Missing modern CK2dEntity chunk (0x10F000)");
        }
        result = deserialize_modern(chunk, arena, out_state);
    } else {
        /* Legacy format: separate identifiers */
        result = deserialize_legacy(chunk, arena, out_state);
    }
    
    if (result != NMO_OK) {
        return result;
    }
    
    /* Optional material (identifier 0x200000, CKCID_2DENTITY only) */
    if (nmo_chunk_get_class_id(chunk) == NMO_CID_2DENTITY &&
        nmo_chunk_seek_identifier(chunk, 0x200000) == NMO_OK) {
        out_state->has_material = true;
        result = nmo_chunk_read_object_id(chunk, &out_state->material_id);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read material ID");
        }
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CK2dEntity SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK2dEntity state to chunk (modern format v5+)
 * 
 * Writes identifier 0x10F000 with conditional blocks based on presence flags.
 */
static nmo_status_t serialize_modern(
    const nmo_ck2dentity_state_t *state,
    nmo_chunk_t *chunk)
{
    nmo_status_t result;
    
    /* Write modern chunk identifier */
    result = nmo_chunk_write_identifier(chunk, NMO_CK2DENTITY_CHUNK_MODERN);
    if (result != NMO_OK) return result;
    
    /* Build flags with optional block indicators */
    uint32_t flags = state->flags;
    if (state->has_source_rect ||
        state->source_rect.left != 0.0f || state->source_rect.top != 0.0f ||
        state->source_rect.right != 0.0f || state->source_rect.bottom != 0.0f) {
        flags |= NMO_CK2DENTITY_FLAG_SOURCE_RECT;
    }
    if (state->has_z_order || state->z_order != 0) {
        flags |= NMO_CK2DENTITY_FLAG_Z_ORDER;
    }
    if (state->has_parent || state->parent_id != 0) {
        flags |= NMO_CK2DENTITY_FLAG_PARENT;
    }
    
    /* Write flags */
    result = nmo_chunk_write_dword(chunk, flags);
    if (result != NMO_OK) return result;
    
    /* Write rectangle (homogeneous or regular) */
    if ((state->flags & NMO_CK2DENTITY_FLAG_HOMOGENEOUS) && state->has_homogeneous_rect) {
        result = write_rect(chunk, &state->homogeneous_rect);
    } else {
        result = write_rect(chunk, &state->rect);
    }
    if (result != NMO_OK) return result;
    
    /* Optional blocks */
    if (state->has_source_rect) {
        result = write_rect(chunk, &state->source_rect);
        if (result != NMO_OK) return result;
    }
    
    if (state->has_z_order || state->z_order != 0) {
        result = nmo_chunk_write_int(chunk, state->z_order);
        if (result != NMO_OK) return result;
    }
    
    if (state->has_parent || state->parent_id != 0) {
        result = nmo_chunk_write_object_id(chunk, state->parent_id);
        if (result != NMO_OK) return result;
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Serialize CK2dEntity state to chunk
 * 
 * Always uses modern format (v5+) for simplicity.
 */
nmo_status_t nmo_ck2dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ck2dentity_state_t *in_state = (const nmo_ck2dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ck2dentity_serialize");
    }
    
    /* Serialize parent CKRenderObject data */
    nmo_status_t result = nmo_ckrenderobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Serialize CK2dEntity data (always use modern format) */
    result = serialize_modern(in_state, out_chunk);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Write material identifier if present */
    if (in_state->has_material && in_state->material_id) {
        result = nmo_chunk_write_identifier(out_chunk, 0x200000);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->material_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ck2dentity,
    nmo_ck2dentity_state_t,
    nmo_ck2dentity_serialize,
    nmo_ck2dentity_deserialize,
    NMO_GUID_CK2DENTITY,
    "CK2dEntity",
    NMO_CID_2DENTITY,
    NMO_GUID_CKRENDEROBJECT
)

