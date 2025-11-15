/**
 * @file builtin_types_v2.c
 * @brief Simplified built-in type registrations using builder API
 *
 * This new implementation uses the fluent builder API to dramatically reduce
 * code size while improving readability and maintainability.
 */

#include "schema/nmo_schema_builder.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_math.h"
#include "core/nmo_guid.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * MATH TYPES REGISTRATION
 * ============================================================================= */

nmo_result_t nmo_register_math_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    const nmo_schema_type_t *f32_type = nmo_schema_registry_find_by_name(registry, "f32");
    const nmo_schema_type_t *u32_type = nmo_schema_registry_find_by_name(registry, "u32");
    
    if (!f32_type || !u32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Scalar types must be registered first"));
    }
    
    /* Vector2 */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Vector2", sizeof(nmo_vector2_t), alignof(nmo_vector2_t));
        nmo_builder_add_field(&builder, "x", f32_type, offsetof(nmo_vector2_t, x));
        nmo_builder_add_field(&builder, "y", f32_type, offsetof(nmo_vector2_t, y));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* Vector3 */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Vector3", sizeof(nmo_vector_t), alignof(nmo_vector_t));
        nmo_builder_add_field(&builder, "x", f32_type, offsetof(nmo_vector_t, x));
        nmo_builder_add_field(&builder, "y", f32_type, offsetof(nmo_vector_t, y));
        nmo_builder_add_field(&builder, "z", f32_type, offsetof(nmo_vector_t, z));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* Vector4 */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Vector4", sizeof(nmo_vector4_t), alignof(nmo_vector4_t));
        nmo_builder_add_field(&builder, "x", f32_type, offsetof(nmo_vector4_t, x));
        nmo_builder_add_field(&builder, "y", f32_type, offsetof(nmo_vector4_t, y));
        nmo_builder_add_field(&builder, "z", f32_type, offsetof(nmo_vector4_t, z));
        nmo_builder_add_field(&builder, "w", f32_type, offsetof(nmo_vector4_t, w));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* Quaternion */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Quaternion", sizeof(nmo_quaternion_t), alignof(nmo_quaternion_t));
        nmo_builder_add_field(&builder, "x", f32_type, offsetof(nmo_quaternion_t, x));
        nmo_builder_add_field(&builder, "y", f32_type, offsetof(nmo_quaternion_t, y));
        nmo_builder_add_field(&builder, "z", f32_type, offsetof(nmo_quaternion_t, z));
        nmo_builder_add_field(&builder, "w", f32_type, offsetof(nmo_quaternion_t, w));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* Matrix (4x4) */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Matrix", sizeof(nmo_matrix_t), alignof(nmo_matrix_t));
        /* Matrix is stored as 16 floats in row-major order */
        for (int i = 0; i < 16; i++) {
            char field_name[8];
            snprintf(field_name, sizeof(field_name), "m%d", i);
            /* Note: field names are temporary - need to be arena-allocated */
            char *persistent_name = nmo_arena_alloc(arena, 8, 1);
            if (persistent_name) {
                memcpy(persistent_name, field_name, 8);
                nmo_builder_add_field(&builder, persistent_name, f32_type, offsetof(nmo_matrix_t, m) + i * sizeof(float));
            }
        }
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* Color (RGBA) */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Color", sizeof(nmo_color_t), alignof(nmo_color_t));
        nmo_builder_add_field_ex(&builder, "r", f32_type, offsetof(nmo_color_t, r), NMO_ANNOTATION_COLOR);
        nmo_builder_add_field_ex(&builder, "g", f32_type, offsetof(nmo_color_t, g), NMO_ANNOTATION_COLOR);
        nmo_builder_add_field_ex(&builder, "b", f32_type, offsetof(nmo_color_t, b), NMO_ANNOTATION_COLOR);
        nmo_builder_add_field_ex(&builder, "a", f32_type, offsetof(nmo_color_t, a), NMO_ANNOTATION_COLOR);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* Box (AABB) */
    {
        const nmo_schema_type_t *vec3_type = nmo_schema_registry_find_by_name(registry, "Vector3");
        if (vec3_type) {
            nmo_schema_builder_t builder = nmo_builder_struct(arena, "Box", sizeof(nmo_box_t), alignof(nmo_box_t));
            nmo_builder_add_field(&builder, "min", vec3_type, offsetof(nmo_box_t, min));
            nmo_builder_add_field(&builder, "max", vec3_type, offsetof(nmo_box_t, max));
            result = nmo_builder_build(&builder, registry);
            if (result.code != NMO_OK) return result;
        }
    }
    
    /* Rect (2D bounding box) */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "Rect", sizeof(nmo_rect_t), alignof(nmo_rect_t));
        nmo_builder_add_field(&builder, "left", f32_type, offsetof(nmo_rect_t, left));
        nmo_builder_add_field(&builder, "top", f32_type, offsetof(nmo_rect_t, top));
        nmo_builder_add_field(&builder, "right", f32_type, offsetof(nmo_rect_t, right));
        nmo_builder_add_field(&builder, "bottom", f32_type, offsetof(nmo_rect_t, bottom));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * VIRTOOLS-SPECIFIC TYPES
 * ============================================================================= */

nmo_result_t nmo_register_virtools_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    const nmo_schema_type_t *u32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *u8_type = nmo_schema_registry_find_by_name(registry, "u8");
    
    if (!u32_type || !u8_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Scalar types must be registered first"));
    }
    
    /* GUID (16 bytes) */
    {
        nmo_schema_builder_t builder = nmo_builder_struct(arena, "GUID", sizeof(nmo_guid_t), alignof(nmo_guid_t));
        nmo_builder_add_field(&builder, "d1", u32_type, offsetof(nmo_guid_t, d1));
        nmo_builder_add_field(&builder, "d2", u32_type, offsetof(nmo_guid_t, d2));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* ObjectID (typed alias for u32) */
    {
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "ObjectID", NMO_TYPE_U32, sizeof(nmo_object_id_t));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* ClassID (typed alias for u32) */
    {
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "ClassID", NMO_TYPE_U32, sizeof(nmo_class_id_t));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* ManagerID (typed alias for u32) */
    {
        nmo_schema_builder_t builder = nmo_builder_scalar(arena, "ManagerID", NMO_TYPE_U32, sizeof(uint32_t));
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    /* FileVersion (enum) */
    {
        nmo_schema_builder_t builder = nmo_builder_enum(arena, "FileVersion", NMO_TYPE_U32);
        nmo_builder_add_enum_value(&builder, "VERSION_2", NMO_FILE_VERSION_2);
        nmo_builder_add_enum_value(&builder, "VERSION_3", NMO_FILE_VERSION_3);
        nmo_builder_add_enum_value(&builder, "VERSION_4", NMO_FILE_VERSION_4);
        nmo_builder_add_enum_value(&builder, "VERSION_5", NMO_FILE_VERSION_5);
        nmo_builder_add_enum_value(&builder, "VERSION_6", NMO_FILE_VERSION_6);
        nmo_builder_add_enum_value(&builder, "VERSION_7", NMO_FILE_VERSION_7);
        nmo_builder_add_enum_value(&builder, "VERSION_8", NMO_FILE_VERSION_8);
        nmo_builder_add_enum_value(&builder, "VERSION_9", NMO_FILE_VERSION_9);
        result = nmo_builder_build(&builder, registry);
        if (result.code != NMO_OK) return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * UNIFIED REGISTRATION
 * ============================================================================= */

nmo_result_t nmo_register_builtin_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* Register scalars first (math and virtools types depend on them) */
    result = nmo_register_scalar_types(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Register math types */
    result = nmo_register_math_types(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Register Virtools-specific types */
    result = nmo_register_virtools_types(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}
