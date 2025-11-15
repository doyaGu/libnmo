/**
 * @file nmo_schema_builder.h
 * @brief Fluent API for building schema types with minimal boilerplate
 *
 * This builder API dramatically reduces code required for type registration:
 * 
 * Before:
 *   nmo_schema_type_t *type = nmo_arena_alloc(...);
 *   type->name = "Vec3";
 *   type->kind = NMO_TYPE_STRUCT;
 *   type->size = sizeof(nmo_vec3_t);
 *   ... (15+ lines for one struct)
 *
 * After:
 *   NMO_STRUCT_TYPE(Vec3, nmo_vec3_t)
 *       .field("x", NMO_TYPE_F32, offsetof(nmo_vec3_t, x))
 *       .field("y", NMO_TYPE_F32, offsetof(nmo_vec3_t, y))
 *       .field("z", NMO_TYPE_F32, offsetof(nmo_vec3_t, z))
 *       .build(registry, arena);
 */

#ifndef NMO_SCHEMA_BUILDER_H
#define NMO_SCHEMA_BUILDER_H

#include "nmo_schema.h"
#include "nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * TYPE BUILDER
 * ============================================================================= */

/**
 * @brief Schema type builder for fluent API construction
 */
typedef struct nmo_schema_builder {
    nmo_schema_type_t *type;
    nmo_arena_t *arena;
    
    /* Dynamic arrays for fields/enum values during construction */
    nmo_schema_field_t *fields_buffer;
    size_t fields_capacity;
    size_t fields_count;
    
    nmo_enum_value_t *enum_buffer;
    size_t enum_capacity;
    size_t enum_count;
} nmo_schema_builder_t;

/* =============================================================================
 * BUILDER INITIALIZATION
 * ============================================================================= */

/**
 * @brief Create scalar type builder
 * @param arena Arena for allocations
 * @param name Type name
 * @param kind Scalar kind (NMO_TYPE_U8, NMO_TYPE_F32, etc.)
 * @param size Type size in bytes
 * @return Builder instance
 */
NMO_API nmo_schema_builder_t nmo_builder_scalar(
    nmo_arena_t *arena,
    const char *name,
    nmo_type_kind_t kind,
    size_t size);

/**
 * @brief Create struct type builder
 * @param arena Arena for allocations
 * @param name Type name
 * @param size Struct size in bytes
 * @param align Struct alignment
 * @return Builder instance
 */
NMO_API nmo_schema_builder_t nmo_builder_struct(
    nmo_arena_t *arena,
    const char *name,
    size_t size,
    size_t align);

/**
 * @brief Create array type builder
 * @param arena Arena for allocations
 * @param name Type name
 * @param element_type Element type
 * @return Builder instance
 */
NMO_API nmo_schema_builder_t nmo_builder_array(
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_type_t *element_type);

/**
 * @brief Create fixed array type builder
 * @param arena Arena for allocations
 * @param name Type name
 * @param element_type Element type
 * @param length Array length
 * @return Builder instance
 */
NMO_API nmo_schema_builder_t nmo_builder_fixed_array(
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_type_t *element_type,
    size_t length);

/**
 * @brief Create enum type builder
 * @param arena Arena for allocations
 * @param name Type name
 * @param base_type Base integer type (usually NMO_TYPE_U32 or NMO_TYPE_I32)
 * @return Builder instance
 */
NMO_API nmo_schema_builder_t nmo_builder_enum(
    nmo_arena_t *arena,
    const char *name,
    nmo_type_kind_t base_type);

/* =============================================================================
 * FIELD CONSTRUCTION
 * ============================================================================= */

/**
 * @brief Add field to struct type
 * @param builder Builder instance (must be struct type)
 * @param field_name Field name
 * @param field_type Field type
 * @param field_offset Field offset in struct
 * @return Builder instance for chaining
 */
NMO_API nmo_schema_builder_t *nmo_builder_add_field(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset);

/**
 * @brief Add field with annotations
 * @param builder Builder instance
 * @param field_name Field name
 * @param field_type Field type
 * @param field_offset Field offset
 * @param annotations Annotation flags (bitset)
 * @return Builder instance for chaining
 */
NMO_API nmo_schema_builder_t *nmo_builder_add_field_ex(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset,
    uint32_t annotations);

/**
 * @brief Add versioned field
 * @param builder Builder instance
 * @param field_name Field name
 * @param field_type Field type
 * @param field_offset Field offset
 * @param since_version Version when field was added
 * @param deprecated_version Version when deprecated (0 = never)
 * @return Builder instance for chaining
 */
NMO_API nmo_schema_builder_t *nmo_builder_add_field_versioned(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset,
    uint32_t since_version,
    uint32_t deprecated_version);

/* =============================================================================
 * ENUM CONSTRUCTION
 * ============================================================================= */

/**
 * @brief Add enum value
 * @param builder Builder instance (must be enum type)
 * @param value_name Symbolic name
 * @param value Integer value
 * @return Builder instance for chaining
 */
NMO_API nmo_schema_builder_t *nmo_builder_add_enum_value(
    nmo_schema_builder_t *builder,
    const char *value_name,
    int32_t value);

/* =============================================================================
 * VTABLE CONFIGURATION
 * ============================================================================= */

/**
 * @brief Set vtable for custom read/write/validate
 * @param builder Builder instance
 * @param vtable Vtable pointer (must remain valid)
 * @return Builder instance for chaining
 */
NMO_API nmo_schema_builder_t *nmo_builder_set_vtable(
    nmo_schema_builder_t *builder,
    const nmo_schema_vtable_t *vtable);

/* =============================================================================
 * FINALIZATION
 * ============================================================================= */

/**
 * @brief Build and register type
 * @param builder Builder instance
 * @param registry Registry to add type to
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_builder_build(
    nmo_schema_builder_t *builder,
    nmo_schema_registry_t *registry);

/**
 * @brief Build type without registering
 * @param builder Builder instance
 * @return Type pointer (valid until arena is destroyed)
 */
NMO_API const nmo_schema_type_t *nmo_builder_build_type(
    nmo_schema_builder_t *builder);

/* =============================================================================
 * CONVENIENCE MACROS
 * ============================================================================= */

/**
 * @brief Declare and start building struct type
 * Usage: NMO_STRUCT_TYPE(Vec3, nmo_vec3_t, arena)
 */
#define NMO_STRUCT_TYPE(name, c_type, arena_ptr) \
    nmo_builder_struct(arena_ptr, #name, sizeof(c_type), alignof(c_type))

/**
 * @brief Add struct field with automatic offset
 * Usage: NMO_FIELD(x, f32, Vec3)
 */
#define NMO_FIELD(builder_ptr, field_name, field_type, struct_type) \
    nmo_builder_add_field(builder_ptr, #field_name, field_type, offsetof(struct_type, field_name))

/**
 * @brief Add annotated field
 * Usage: NMO_FIELD_ANNOTATED(x, f32, Vec3, NMO_ANNOTATION_POSITION)
 */
#define NMO_FIELD_ANNOTATED(builder_ptr, field_name, field_type, struct_type, annotations) \
    nmo_builder_add_field_ex(builder_ptr, #field_name, field_type, offsetof(struct_type, field_name), annotations)

/**
 * @brief Declare enum type
 * Usage: NMO_ENUM_TYPE(MyEnum, NMO_TYPE_U32, arena)
 */
#define NMO_ENUM_TYPE(name, base_type, arena_ptr) \
    nmo_builder_enum(arena_ptr, #name, base_type)

/**
 * @brief Add enum value
 * Usage: NMO_ENUM_VALUE(builder_ptr, VALUE_NAME, 42)
 */
#define NMO_ENUM_VALUE(builder_ptr, value_name, int_value) \
    nmo_builder_add_enum_value(builder_ptr, #value_name, int_value)

/* =============================================================================
 * BATCH REGISTRATION HELPERS
 * ============================================================================= */

/**
 * @brief Register all scalar types at once
 * @param registry Registry
 * @param arena Arena
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_scalar_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Register common math types (Vec2/3/4, Quaternion, Matrix, Color)
 * @param registry Registry
 * @param arena Arena
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_math_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Register Virtools-specific types (GUID, ObjectID, ClassID, etc.)
 * @param registry Registry
 * @param arena Arena
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_virtools_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Register all built-in types (scalars + math + Virtools)
 * Convenience function that calls all registration functions.
 * @param registry Registry
 * @param arena Arena
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_builtin_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_BUILDER_H */
