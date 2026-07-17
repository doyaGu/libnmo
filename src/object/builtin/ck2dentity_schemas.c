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

#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(2dentity, nmo_2dentity_state_t)

static void nmo_2dentity_dispose_base_arrays(nmo_2dentity_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_2dentity_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_2dentity_state_t, base),
                    sizeof(nmo_renderobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_2dentity_state_t, rect, CKPGUID_RECT),
    NMO_FIELD(nmo_2dentity_state_t, has_homogeneous_rect, CKPGUID_BOOL),
    NMO_FIELD(nmo_2dentity_state_t, homogeneous_rect, CKPGUID_RECT),
    NMO_FIELD(nmo_2dentity_state_t, has_source_rect, CKPGUID_BOOL),
    NMO_FIELD(nmo_2dentity_state_t, source_rect, CKPGUID_RECT),
    NMO_FIELD(nmo_2dentity_state_t, has_z_order, CKPGUID_BOOL),
    NMO_FIELD(nmo_2dentity_state_t, z_order, CKPGUID_INT),
    NMO_FIELD(nmo_2dentity_state_t, has_parent, CKPGUID_BOOL),
    NMO_FIELD_REF(nmo_2dentity_state_t, parent),
    NMO_FIELD(nmo_2dentity_state_t, has_material, CKPGUID_BOOL),
    NMO_FIELD_REF(nmo_2dentity_state_t, material),
    NMO_FIELD(nmo_2dentity_state_t, flags, NMO_GUID_ENUM_CK_2DENTITY_FLAGS)
};

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

static void nmo_2dentity_set_default_source_rect(
    nmo_2dentity_state_t *state,
    nmo_class_id_t class_id)
{
    if (class_id == NMO_CID_2DENTITY) {
        state->source_rect.left = 0.0f;
        state->source_rect.top = 0.0f;
        state->source_rect.right = 1.0f;
        state->source_rect.bottom = 1.0f;
    } else {
        state->source_rect.left = 0.0f;
        state->source_rect.top = 0.0f;
        state->source_rect.right = 0.0f;
        state->source_rect.bottom = 0.0f;
    }
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
    nmo_2dentity_state_t *out_state,
    void *context)
{
    (void)arena;
    nmo_status_t result;
    const nmo_class_id_t class_id = nmo_chunk_get_class_id(chunk);
    
    /* Read flags */
    uint32_t raw_flags;
    result = nmo_chunk_read_dword(chunk, &raw_flags);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read CK2dEntity flags");
    }
    
    /* Sanitize flags (mask applied by RCK2dEntity::Load) */
    out_state->flags = raw_flags & NMO_CK2DENTITY_FLAGS_MASK;
    out_state->has_source_rect = false;
    out_state->has_z_order = false;
    out_state->has_parent = false;
    out_state->parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->z_order = 0;
    nmo_2dentity_set_default_source_rect(out_state, class_id);
    
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
        nmo_ref_t parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &parent);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read parent ID");
        }
        nmo_ref_check_class(
            &parent,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_2DENTITY);
        out_state->parent = parent;
        out_state->has_parent = true;
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
    nmo_2dentity_state_t *out_state)
{
    (void)arena;
    nmo_status_t result;
    const nmo_class_id_t class_id = nmo_chunk_get_class_id(chunk);
    bool has_flags = false;

    out_state->has_source_rect = false;
    out_state->has_z_order = false;
    out_state->has_parent = false;
    out_state->parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->z_order = 0;
    nmo_2dentity_set_default_source_rect(out_state, class_id);
    
    /* Read flags (identifier 0x4000) */
    nmo_status_t seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_2DENTITYFLAGS);
    if (seek_result == NMO_OK) {
        uint32_t raw_flags;
        result = nmo_chunk_read_dword(chunk, &raw_flags);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read legacy flags");
        }
        out_state->flags = raw_flags;
        has_flags = true;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    if (has_flags) {
        if ((out_state->flags & CK_2DENTITY_RESERVED3) == 0) {
            out_state->flags |= CK_2DENTITY_RESERVED3;
            out_state->base.base.base.base.visibility_flags = 0;
        }
        out_state->flags &= ~CK_2DENTITY_UPDATEHOMOGENEOUSCOORD;
        out_state->flags |= CK_2DENTITY_STICKTOP | CK_2DENTITY_STICKLEFT;
    }
    
    /* Read origin (identifier 0x8000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_2DENTITYPOS);
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
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read size (identifier 0x2000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_2DENTITYSIZE);
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
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read source rect (identifier 0x1000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_2DENTITYSRCSIZE);
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
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    /* Read z-order (identifier 0x100000) */
    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_2DENTITYZORDER);
    if (seek_result == NMO_OK) {
        out_state->has_z_order = true;
        result = nmo_chunk_read_int(chunk, (int32_t *)&out_state->z_order);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read z-order");
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    
    NMO_RETURN_OK();
}

/**
 * @brief Deserialize CK2dEntity state from chunk
 * 
 * Dispatches to modern or legacy deserializer based on chunk data version.
 */
static nmo_status_t nmo_2dentity_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_2dentity_state_t *out_state = (nmo_2dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_2dentity_deserialize");
    }
    
    /* First deserialize parent CKRenderObject data */
    nmo_status_t result = nmo_renderobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_material = false;
    out_state->material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    
    /* Check chunk version to choose format */
    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    
    if (data_version >= 5) {
        /* Modern format: identifier 0x10F000 */
        nmo_status_t seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_2DENTITYONLY);
        if (seek_result != NMO_OK) {
            if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Missing modern CK2dEntity chunk (0x10F000)");
        }
        result = deserialize_modern(chunk, arena, out_state, context);
    } else {
        /* Legacy format: separate identifiers */
        result = deserialize_legacy(chunk, arena, out_state);
    }
    
    if (result != NMO_OK) {
        return result;
    }
    
    /* Optional material (identifier 0x200000, CKCID_2DENTITY only) */
    if (nmo_chunk_get_class_id(chunk) == NMO_CID_2DENTITY) {
        nmo_status_t seek_result = nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_2DENTITYMATERIAL);
        if (seek_result == NMO_OK) {
            nmo_ref_t material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            result = nmo_ref_read(chunk, &material);
            if (result != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Failed to read material ID");
            }
            nmo_ref_check_class(
                &material,
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_MATERIAL);
            out_state->material = material;
            out_state->has_material = true;
        } else if (seek_result != NMO_ERR_NOT_FOUND) {
            return seek_result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_2dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_2dentity_state_t *out_state = (nmo_2dentity_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_2dentity_state_t decoded = {0};
    nmo_status_t result = nmo_2dentity_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_2dentity_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_2dentity_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
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
    const nmo_2dentity_state_t *state,
    nmo_chunk_t *chunk)
{
    nmo_status_t result;
    
    /* Write modern chunk identifier */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_2DENTITYONLY);
    if (result != NMO_OK) return result;
    
    /* Build flags with optional block indicators */
    uint32_t flags = state->flags;
    const bool write_source = state->has_source_rect ||
        state->source_rect.left != 0.0f || state->source_rect.top != 0.0f ||
        state->source_rect.right != 0.0f || state->source_rect.bottom != 0.0f;
    if (write_source) {
        flags |= NMO_CK2DENTITY_FLAG_SOURCE_RECT;
    }
    const bool write_z_order = state->has_z_order || state->z_order != 0;
    if (write_z_order) {
        flags |= NMO_CK2DENTITY_FLAG_Z_ORDER;
    }
    const bool write_parent = state->has_parent ||
        state->parent.state != NMO_REF_NONE;
    if (write_parent) {
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
    if (write_source) {
        result = write_rect(chunk, &state->source_rect);
        if (result != NMO_OK) return result;
    }
    
    if (write_z_order) {
        result = nmo_chunk_write_int(chunk, state->z_order);
        if (result != NMO_OK) return result;
    }
    
    if (write_parent) {
        result = nmo_ref_write(chunk, &state->parent);
        if (result != NMO_OK) return result;
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Serialize CK2dEntity state to chunk
 * 
 * Always uses modern format (v5+) for simplicity.
 */
static nmo_status_t nmo_2dentity_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_2dentity_state_t *in_state = (const nmo_2dentity_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_2dentity_serialize");
    }
    
    /* Serialize parent CKRenderObject data */
    nmo_status_t result = nmo_renderobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Serialize CK2dEntity data (always use modern format) */
    result = serialize_modern(in_state, out_chunk);
    if (result != NMO_OK) {
        return result;
    }
    
    /* Write material identifier if present */
    if (in_state->has_material && in_state->material.state != NMO_REF_NONE) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_2DENTITYMATERIAL);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->material);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_2dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_2dentity_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_status_t nmo_2dentity_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_2dentity_remap_dependencies");
    }

    nmo_2dentity_state_t *state = (nmo_2dentity_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_renderobject_remap_dependencies(&state->base, NULL, context));

    /* Keep raw flags and references unchanged during remap. */
    return nmo_object_default_validate(state, NULL, NULL);
}

nmo_status_t nmo_2dentity_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

static nmo_status_t nmo_2dentity_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_2dentity_pre_delete");
    }
    nmo_2dentity_state_t *state = (nmo_2dentity_state_t *)instance;
    state->has_parent = false;
    state->parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->has_material = false;
    state->material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

static void nmo_2dentity_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

NMO_DEFINE_OBJECT_STATE_OPS(2dentity, nmo_2dentity_state_t)

nmo_type_vtable_t nmo_2dentity_vtable = {
    .prepare_dependencies = nmo_2dentity_prepare_dependencies,
    .remap_dependencies = nmo_2dentity_remap_dependencies,
    .pre_delete = nmo_2dentity_pre_delete,
    .post_delete = nmo_2dentity_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_2dentity_create,
        nmo_2dentity_destroy,
        nmo_2dentity_serialize,
        nmo_2dentity_deserialize,
        nmo_2dentity_copy,
        nmo_2dentity_validate,
        nmo_2dentity_equals,
        nmo_2dentity_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_2dentity_type,
    CKPGUID_2DENTITY,
    "CK2dEntity",
    NMO_CID_2DENTITY,
    CKPGUID_RENDEROBJECT,
    nmo_2dentity_state_t,
    &nmo_2dentity_vtable,
    nmo_2dentity_fields)






