/**
 * @file param_types.c
 * @brief Parameter metadata registration for core Virtools parameter types
 *
 * This file registers Parameter metadata for scalar, math, and object reference types.
 * Each type gets a GUID, size, kind, and optional derivation information matching
 * the original Virtools Parameter system (CKParameterTypeDesc).
 *
 * Registration pattern:
 *   1. Create nmo_param_meta_t structure with GUID, kind, default_size
 *   2. Build type with nmo_builder_*()
 *   3. Attach param_meta with nmo_builder_set_param_meta()
 *   4. Register with nmo_builder_build()
 */

#include "schema/nmo_schema_builder.h"
#include "schema/nmo_param_meta.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <string.h>

/* =============================================================================
 * SCALAR TYPE REGISTRATION
 * ============================================================================= */

/**
 * @brief Register scalar parameter types (int, float, bool, string, key)
 *
 * These correspond to CKPGUID_INT, CKPGUID_FLOAT, CKPGUID_BOOL, etc.
 * All scalars have NMO_PARAM_SCALAR kind and fixed sizes (except string).
 */
static nmo_result_t register_scalar_param_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* INT (4 bytes, CKPGUID_INT) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_SCALAR,
            .guid = CKPGUID_INT,
            .derived_from = NMO_GUID_NULL,
            .default_size = 4,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "Integer",
            .description = "32-bit signed integer"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "int", NMO_TYPE_I32, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* FLOAT (4 bytes, CKPGUID_FLOAT) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_SCALAR,
            .guid = CKPGUID_FLOAT,
            .derived_from = NMO_GUID_NULL,
            .default_size = 4,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "Float",
            .description = "32-bit floating point"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "float", NMO_TYPE_F32, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* BOOL (4 bytes, CKPGUID_BOOL) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_SCALAR,
            .guid = CKPGUID_BOOL,
            .derived_from = NMO_GUID_NULL,
            .default_size = 4,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE,
            .creator_plugin = NULL,
            .ui_name = "Boolean",
            .description = "Boolean value (0 or 1)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "bool", NMO_TYPE_BOOL, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* STRING (variable size, CKPGUID_STRING) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_SCALAR,
            .guid = CKPGUID_STRING,
            .derived_from = NMO_GUID_NULL,
            .default_size = 0, /* Variable */
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE,
            .creator_plugin = NULL,
            .ui_name = "String",
            .description = "Variable-length string"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "string", NMO_TYPE_STRING, 0);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* KEY (4 bytes, CKPGUID_KEY) - for CK_ID/DWORD keys */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_SCALAR,
            .guid = CKPGUID_KEY,
            .derived_from = NMO_GUID_NULL,
            .default_size = 4,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE,
            .creator_plugin = NULL,
            .ui_name = "Key",
            .description = "Unique identifier (DWORD)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "key", NMO_TYPE_U32, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * MATH TYPE REGISTRATION
 * ============================================================================= */

/**
 * @brief Register math parameter types (Vector, 2DVector, Quaternion, Matrix, Color, etc.)
 *
 * These are struct types with known layouts. In full implementation, would use
 * proper struct registration with fields. For now, we register them as opaque
 * structs with correct sizes.
 */
static nmo_result_t register_math_param_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* VECTOR (12 bytes: 3 floats, CKPGUID_VECTOR) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_VECTOR,
            .derived_from = NMO_GUID_NULL,
            .default_size = 12,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "Vector",
            .description = "3D vector (x, y, z)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Vector", 12, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* 2DVECTOR (8 bytes: 2 floats, CKPGUID_2DVECTOR) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_2DVECTOR,
            .derived_from = NMO_GUID_NULL,
            .default_size = 8,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "2D Vector",
            .description = "2D vector (x, y)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "2DVector", 8, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* QUATERNION (16 bytes: 4 floats, CKPGUID_QUATERNION) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_QUATERNION,
            .derived_from = NMO_GUID_NULL,
            .default_size = 16,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "Quaternion",
            .description = "Rotation quaternion (x, y, z, w)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Quaternion", 16, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* MATRIX (64 bytes: 4x4 floats, CKPGUID_MATRIX) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_MATRIX,
            .derived_from = NMO_GUID_NULL,
            .default_size = 64,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "Matrix",
            .description = "4x4 transformation matrix"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Matrix", 64, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* COLOR (16 bytes: RGBA as 4 floats, CKPGUID_COLOR) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_COLOR,
            .derived_from = NMO_GUID_NULL,
            .default_size = 16,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_ANIMATABLE,
            .creator_plugin = NULL,
            .ui_name = "Color",
            .description = "RGBA color (4 floats)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Color", 16, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* BOX (24 bytes: 2 Vectors, CKPGUID_BOX) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_BOX,
            .derived_from = NMO_GUID_NULL,
            .default_size = 24,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE,
            .creator_plugin = NULL,
            .ui_name = "Box",
            .description = "3D bounding box (min, max)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Box", 24, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* RECT (16 bytes: 4 floats, CKPGUID_RECT) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_STRUCT,
            .guid = CKPGUID_RECT,
            .derived_from = NMO_GUID_NULL,
            .default_size = 16,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE,
            .creator_plugin = NULL,
            .ui_name = "Rectangle",
            .description = "2D rectangle (left, top, right, bottom)"
        };
        
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Rect", 16, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * OBJECT REFERENCE TYPE REGISTRATION
 * ============================================================================= */

/**
 * @brief Register object reference parameter types (Object, ID)
 *
 * These have NMO_PARAM_OBJECT_REF kind and store object IDs (4 bytes).
 * In full implementation, would support class_id filtering (e.g., CK_3DENTITY).
 */
static nmo_result_t register_object_ref_param_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* OBJECT (4 bytes: CK_ID, CKPGUID_OBJECT) */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_OBJECT_REF,
            .guid = CKPGUID_OBJECT,
            .derived_from = NMO_GUID_NULL,
            .default_size = 4,
            .class_id = 0, /* Any object class (would be CK_OBJECT in full impl) */
            .flags = NMO_PARAM_FLAG_SERIALIZABLE,
            .creator_plugin = NULL,
            .ui_name = "Object",
            .description = "Reference to Virtools object"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "Object", NMO_TYPE_U32, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* ID (4 bytes: CK_ID, CKPGUID_ID) - alias for Object */
    {
        nmo_param_meta_t meta = {
            .kind = NMO_PARAM_OBJECT_REF,
            .guid = CKPGUID_ID,
            .derived_from = CKPGUID_OBJECT, /* Derived from Object */
            .default_size = 4,
            .class_id = 0,
            .flags = NMO_PARAM_FLAG_SERIALIZABLE | NMO_PARAM_FLAG_DERIVED,
            .creator_plugin = NULL,
            .ui_name = "ID",
            .description = "Object identifier"
        };
        
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "ID", NMO_TYPE_U32, 4);
        nmo_builder_set_param_meta(&builder, &meta);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API: BATCH REGISTRATION
 * ============================================================================= */

/**
 * @brief Register all parameter types with metadata
 *
 * This function registers 12 core parameter types:
 *   - Scalars: int, float, bool, string, key (5 types)
 *   - Math: Vector, 2DVector, Quaternion, Matrix, Color, Box, Rect (7 types)
 *   - References: Object, ID (2 types - ID derived from Object)
 *
 * All types include complete Parameter metadata (GUID, kind, size, flags).
 *
 * @param registry Schema registry to register into
 * @param arena Arena for allocations
 * @return NMO_OK on success, error otherwise
 */
nmo_result_t nmo_register_param_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    result = register_scalar_param_types(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    result = register_math_param_types(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    result = register_object_ref_param_types(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}
