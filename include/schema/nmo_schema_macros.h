/**
 * @file nmo_schema_macros.h
 * @brief Declarative schema registration macros for zero-boilerplate type definition
 *
 * This module provides a macro-based DSL for registering schema types with minimal code.
 * Typical registration code reduces from ~40 lines to ~10 lines per type.
 *
 * DESIGN PRINCIPLES:
 * 1. Data-driven field declarations (table-based syntax)
 * 2. Automatic vtable wrapper generation
 * 3. Type-safe with compile-time checks (offsetof, sizeof)
 * 4. Zero runtime overhead (all macros expand to static structures)
 *
 * USAGE EXAMPLE:
 * ```c
 * // 1. Declare field table
 * NMO_DECLARE_SCHEMA(CKObject, nmo_ckobject_state_t) {
 *     NMO_FIELD(visibility_flags, u32),
 *     NMO_FIELD(options, u32),
 *     NMO_FIELD_VERSIONED(new_field, f32, 7, 0)  // Since v7
 * };
 *
 * // 2. Generate vtable (automatic wrapper functions)
 * NMO_GENERATE_VTABLE(ckobject, nmo_ckobject_state_t);
 *
 * // 3. Register in one line
 * nmo_result_t register_ckobject(registry, arena) {
 *     return NMO_REGISTER_SCHEMA(registry, arena, CKObject,
 *                                nmo_ckobject_state_t, &ckobject_vtable);
 * }
 * ```
 */

#ifndef NMO_SCHEMA_MACROS_H
#define NMO_SCHEMA_MACROS_H

#include "schema/nmo_schema.h"
#include "schema/nmo_schema_builder.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * FIELD DESCRIPTOR STRUCTURES (Internal)
 * ============================================================================= */

/**
 * @brief Field descriptor for declarative schema definition
 *
 * This is an intermediate structure used by macros. It's converted to
 * nmo_schema_field_t during registration by looking up type names.
 */
typedef struct nmo_schema_field_descriptor {
    const char *name;                 /**< Field name */
    const char *type_name;            /**< Type name (resolved at registration) */
    size_t offset;                    /**< offsetof(struct, field) */
    uint32_t annotations;             /**< Field annotation flags */
    uint32_t since_version;           /**< Version when field was added (0 = always existed) */
    uint32_t deprecated_version;      /**< Version when field was deprecated (0 = not deprecated) */
    uint32_t removed_version;         /**< Version when field was removed (0 = not removed) */
} nmo_schema_field_descriptor_t;

/**
 * @brief Enum value descriptor for declarative enum definition
 */
typedef struct nmo_schema_enum_descriptor {
    const char *name;                 /**< Enum value name */
    int32_t value;                    /**< Integer value */
} nmo_schema_enum_descriptor_t;

/* =============================================================================
 * FIELD DECLARATION MACROS
 * ============================================================================= */

/**
 * @brief Declare a schema field table
 * @param name Type name (used as prefix for array)
 * @param struct_type C struct type
 *
 * Generates: `static const nmo_schema_field_descriptor_t name##_fields[] = ...`
 *
 * Example:
 * ```c
 * NMO_DECLARE_SCHEMA(CKObject, nmo_ckobject_state_t) {
 *     SCHEMA_FIELD(id, u32, nmo_ckobject_state_t),
 *     SCHEMA_FIELD(flags, u32, nmo_ckobject_state_t)
 * };
 * ```
 */
#define NMO_DECLARE_SCHEMA(name, struct_type) \
    static const nmo_schema_field_descriptor_t name##_fields[] =

/**
 * @brief Basic field with name and type
 * @param fname Field name (must match struct member)
 * @param ftype Type name (string, resolved at registration)
 * @param stype Struct type (for offsetof)
 */
#define SCHEMA_FIELD(fname, ftype, stype) \
    { #fname, #ftype, offsetof(stype, fname), 0, 0, 0, 0 }

/**
 * @brief Field with annotation flags
 * @param fname Field name
 * @param ftype Type name
 * @param stype Struct type
 * @param annot Annotation flags (nmo_field_annotation_t bitfield)
 */
#define SCHEMA_FIELD_EX(fname, ftype, stype, annot) \
    { #fname, #ftype, offsetof(stype, fname), annot, 0, 0, 0 }

/**
 * @brief Field with version information
 * @param fname Field name
 * @param ftype Type name
 * @param stype Struct type
 * @param since Version when field was introduced (0 = always existed)
 * @param depr Version when deprecated (0 = not deprecated)
 */
#define SCHEMA_FIELD_VERSIONED(fname, ftype, stype, since, depr) \
    { #fname, #ftype, offsetof(stype, fname), 0, since, depr, 0 }

/**
 * @brief Field with full metadata (annotations + versions)
 * @param fname Field name
 * @param ftype Type name
 * @param stype Struct type
 * @param annot Annotation flags
 * @param since Since version
 * @param depr Deprecated version
 * @param removed Removed version
 */
#define SCHEMA_FIELD_FULL(fname, ftype, stype, annot, since, depr, removed) \
    { #fname, #ftype, offsetof(stype, fname), annot, since, depr, removed }

/* =============================================================================
 * ENUM DECLARATION MACROS
 * ============================================================================= */

/**
 * @brief Declare an enum value table
 * @param name Enum type name
 */
#define NMO_DECLARE_ENUM(name) \
    static const nmo_schema_enum_descriptor_t name##_values[] =

/**
 * @brief Enum value entry
 * @param vname Value name
 * @param vval Integer value
 */
#define SCHEMA_ENUM_VALUE(vname, vval) \
    { #vname, vval }

/* =============================================================================
 * VTABLE GENERATION MACROS
 * ============================================================================= */

/**
 * @brief Generate vtable wrapper functions for existing serialize/deserialize
 * @param tname Type name (lowercase, e.g., "ckobject")
 * @param stype State struct type (e.g., "nmo_ckobject_state_t")
 *
 * Generates:
 * - `vtable_read_<tname>()`  - Calls `nmo_<tname>_deserialize()`
 * - `vtable_write_<tname>()` - Calls `nmo_<tname>_serialize()`
 * - `<tname>_vtable` - Static vtable struct with read/write pointers
 *
 * REQUIREMENTS:
 * - Must have `nmo_<tname>_deserialize(chunk, arena, out_state)` function
 * - Must have `nmo_<tname>_serialize(in_state, chunk, arena)` function
 *
 * Example:
 * ```c
 * NMO_GENERATE_VTABLE(ckobject, nmo_ckobject_state_t);
 * // Generates: ckobject_vtable with read/write wrappers
 * ```
 */
#define NMO_GENERATE_VTABLE(tname, stype) \
    static nmo_result_t vtable_read_##tname( \
        const nmo_schema_type_t *type, \
        nmo_chunk_t *chunk, \
        nmo_arena_t *arena, \
        void *out_ptr) \
    { \
        (void)type; \
        return nmo_##tname##_deserialize(chunk, arena, (stype *)out_ptr); \
    } \
    \
    static nmo_result_t vtable_write_##tname( \
        const nmo_schema_type_t *type, \
        nmo_chunk_t *chunk, \
        const void *in_ptr, \
        nmo_arena_t *arena) \
    { \
        (void)type; \
        return nmo_##tname##_serialize((const stype *)in_ptr, chunk, arena); \
    } \
    \
    static const nmo_schema_vtable_t tname##_vtable = { \
        .read = vtable_read_##tname, \
        .write = vtable_write_##tname, \
        .validate = NULL \
    }

/* =============================================================================
 * REGISTRATION SUPPORT FUNCTIONS
 * ============================================================================= */

/**
 * @brief Register a schema from field descriptors
 * @param registry Schema registry
 * @param arena Memory arena
 * @param name Type name
 * @param size sizeof(struct)
 * @param align alignof(struct)
 * @param field_descriptors Field descriptor table
 * @param field_count Number of fields
 * @param vtable Vtable pointer (NULL if none)
 * @return Result indicating success or error
 *
 * This function:
 * 1. Creates a builder
 * 2. Iterates field_descriptors, resolves type names to schema types
 * 3. Adds fields to builder
 * 4. Sets vtable if provided
 * 5. Builds and registers
 */
NMO_API nmo_result_t nmo_register_schema_from_descriptor(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena,
    const char *name,
    size_t size,
    size_t align,
    const nmo_schema_field_descriptor_t *field_descriptors,
    size_t field_count,
    const nmo_schema_vtable_t *vtable);

/**
 * @brief Register an enum from enum descriptors
 * @param registry Schema registry
 * @param arena Memory arena
 * @param name Enum type name
 * @param enum_descriptors Enum value table
 * @param enum_count Number of values
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_enum_from_descriptor(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_enum_descriptor_t *enum_descriptors,
    size_t enum_count);

/* =============================================================================
 * REGISTRATION MACROS (High-level API)
 * ============================================================================= */

/**
 * @brief Register a schema with vtable in one macro call
 * @param reg Registry
 * @param arena Arena
 * @param name Type name (matches NMO_DECLARE_SCHEMA name)
 * @param struct_type C struct type
 * @param vtbl Vtable pointer (e.g., &ckobject_vtable)
 *
 * Example:
 * ```c
 * NMO_REGISTER_SCHEMA(registry, arena, CKObject,
 *                     nmo_ckobject_state_t, &ckobject_vtable);
 * ```
 */
#define NMO_REGISTER_SCHEMA(reg, arena, name, struct_type, vtbl) \
    nmo_register_schema_from_descriptor(reg, arena, \
        #name, sizeof(struct_type), _Alignof(struct_type), \
        name##_fields, \
        sizeof(name##_fields) / sizeof(name##_fields[0]), \
        vtbl)

/**
 * @brief Register a simple schema without vtable
 * @param reg Registry
 * @param arena Arena
 * @param name Type name
 * @param struct_type C struct type
 *
 * Use this for pure data structures with no custom serialization logic.
 */
#define NMO_REGISTER_SIMPLE_SCHEMA(reg, arena, name, struct_type) \
    NMO_REGISTER_SCHEMA(reg, arena, name, struct_type, NULL)

/**
 * @brief Register an enum type
 * @param reg Registry
 * @param arena Arena
 * @param name Enum type name
 *
 * Example:
 * ```c
 * NMO_DECLARE_ENUM(CKSPRITE_MODE) {
 *     NMO_ENUM_VALUE(TEXT_MODE, 0),
 *     NMO_ENUM_VALUE(BITMAP_MODE, 1)
 * };
 * NMO_REGISTER_ENUM(registry, arena, CKSPRITE_MODE);
 * ```
 */
#define NMO_REGISTER_ENUM(reg, arena, name) \
    nmo_register_enum_from_descriptor(reg, arena, \
        #name, \
        name##_values, \
        sizeof(name##_values) / sizeof(name##_values[0]))

/* =============================================================================
 * CONVENIENCE MACROS FOR COMMON PATTERNS
 * ============================================================================= */

/**
 * @brief Shorthand for registering a CK* class with standard naming
 * @param reg Registry
 * @param arena Arena
 * @param classname CK class name (without "CK" prefix, e.g., "Object")
 *
 * Assumes:
 * - Field table: `CK<classname>_fields`
 * - State struct: `nmo_ck<lowercase>_state_t`
 * - Vtable: `ck<lowercase>_vtable`
 *
 * Example:
 * ```c
 * NMO_REGISTER_CK_CLASS(registry, arena, Object);
 * // Expands to: NMO_REGISTER_SCHEMA(registry, arena, CKObject,
 * //                                  nmo_ckobject_state_t, &ckobject_vtable)
 * ```
 */
#define NMO_REGISTER_CK_CLASS(reg, arena, classname) \
    nmo_register_schema_from_descriptor(reg, arena, \
        "CK" #classname, \
/* ============================================================================
 * Phase 5 Improvements: Context Macros and Debug Tools
 * ============================================================================ */

/**
 * @brief Helper macro to get array size - moved from internal use
 */
#define NMO_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * @brief Compile-time size verification for schema types
 * 
 * Usage:
 *   NMO_VERIFY_SCHEMA_SIZE(nmo_vector_t, 12);
 */
#define NMO_VERIFY_SCHEMA_SIZE(stype, expected_size) \
    _Static_assert(sizeof(stype) == (expected_size), \
                   "Schema type " #stype " size mismatch: expected " #expected_size " bytes")

/**
 * @brief Compile-time alignment verification
 */
#define NMO_VERIFY_SCHEMA_ALIGN(stype, expected_align) \
    _Static_assert(_Alignof(stype) == (expected_align), \
                   "Schema type " #stype " alignment mismatch: expected " #expected_align " bytes")

/**
 * @brief Simplified field declaration macro for common cases
 * 
 * This is a shorthand for SCHEMA_FIELD that can be used when the struct
 * type name is predictable. Use with caution as it reduces type safety.
 * 
 * Example:
 *   NMO_DECLARE_SCHEMA(Vector3, nmo_vector_t) {
 *       SIMPLE_FIELD(nmo_vector_t, x, f32),
 *       SIMPLE_FIELD(nmo_vector_t, y, f32),
 *       SIMPLE_FIELD(nmo_vector_t, z, f32)
 *   };
 */
#define SIMPLE_FIELD(stype, fname, ftype) \
    SCHEMA_FIELD(fname, ftype, stype)

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_MACROS_H */
