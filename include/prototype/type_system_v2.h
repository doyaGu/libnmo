/**
 * @file type_system_v2_prototype.h
 * @brief Unified Type System v2.0 - Prototype Implementation
 * 
 * This is a working prototype to validate the refactoring proposal.
 * NOT for production use - demonstrates core concepts only.
 */

#ifndef NMO_TYPE_SYSTEM_V2_PROTOTYPE_H
#define NMO_TYPE_SYSTEM_V2_PROTOTYPE_H

#include "core/nmo_guid.h"
#include "core/nmo_result.h"
#include "core/nmo_arena.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Type Category Flags
 * ============================================================================ */

typedef uint32_t nmo_type_category_t;

/* Base categories - can be ORed together */
#define NMO_TYPE_SCALAR       (1u << 0)   /* Scalar type */
#define NMO_TYPE_STRUCT       (1u << 1)   /* Struct */
#define NMO_TYPE_ENUM         (1u << 2)   /* Enum */
#define NMO_TYPE_FLAGS        (1u << 3)   /* Bit flags */
#define NMO_TYPE_ARRAY        (1u << 4)   /* Array */
#define NMO_TYPE_POINTER      (1u << 5)   /* Pointer/Reference */
#define NMO_TYPE_OBJECT_REF   (1u << 6)   /* Virtools object ref */

/* Plugin extensibility */
#define NMO_TYPE_PLUGIN_BASE  (1u << 16)  /* Plugin types start here */

/* Type characteristics */
#define NMO_TYPE_SERIALIZABLE (1u << 24)  /* Serializable */
#define NMO_TYPE_ANIMATABLE   (1u << 25)  /* Animatable */
#define NMO_TYPE_DERIVED      (1u << 26)  /* Derived type */
#define NMO_TYPE_BUILTIN      (1u << 27)  /* Built-in type */
#define NMO_TYPE_PLUGIN       (1u << 28)  /* Plugin type */

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_type_field nmo_type_field_t;
typedef struct nmo_type_enum_value nmo_type_enum_value_t;
typedef struct nmo_type_vtable nmo_type_vtable_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/* ============================================================================
 * Type Descriptor - Unified Metadata
 * ============================================================================ */

/**
 * @brief Field descriptor with GUID-based type reference
 */
struct nmo_type_field {
    const char *name;
    nmo_guid_t type_guid;        /* GUID reference instead of string! */
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    uint32_t added_version;
    uint32_t removed_version;
};

/**
 * @brief Enum value descriptor
 */
struct nmo_type_enum_value {
    const char *name;
    int64_t value;
    const char *description;
};

/**
 * @brief Type operations vtable (zero-cost extension point)
 */
struct nmo_type_vtable {
    /* Serialization */
    nmo_result_t (*serialize)(const void *instance, void *chunk, 
                              const nmo_type_descriptor_t *type, void *context);
    nmo_result_t (*deserialize)(void *instance, void *chunk,
                                 const nmo_type_descriptor_t *type, void *context);
    
    /* Validation */
    nmo_result_t (*validate)(const void *instance, 
                             const nmo_type_descriptor_t *type, void *context);
    
    /* Comparison */
    bool (*equals)(const void *a, const void *b);
    uint32_t (*hash)(const void *instance);
    
    /* Lifecycle */
    void (*construct)(void *instance);
    void (*destruct)(void *instance);
    
    /* Clone */
    nmo_result_t (*clone)(const void *src, void *dst, nmo_arena_t *arena);
};

/**
 * @brief Unified type descriptor
 * 
 * Merges nmo_schema_type_t and nmo_param_meta_t into single structure
 */
struct nmo_type_descriptor {
    /* === Primary identifiers === */
    nmo_guid_t guid;             /* Global unique identifier (PRIMARY KEY) */
    const char *name;            /* Human-readable name (alias) */
    uint32_t type_id;            /* Runtime type ID (assigned on registration) */
    
    /* === Type attributes === */
    nmo_type_category_t category;/* Type category and characteristics (bit flags) */
    uint32_t size;               /* Type size in bytes */
    uint32_t alignment;          /* Memory alignment */
    uint32_t version;            /* Type version */
    
    /* === Type relationships === */
    nmo_guid_t base_type;        /* Base type GUID (for derived types) */
    nmo_guid_t element_type;     /* Element type GUID (for arrays/pointers) */
    uint32_t element_count;      /* Array length (0 = dynamic) */
    
    /* === Virtools specific === */
    uint32_t class_id;           /* CK_CLASSID (for object references) */
    nmo_guid_t creator_plugin;   /* Creator plugin GUID */
    
    /* === Structured type info === */
    const nmo_type_field_t *fields;
    size_t field_count;
    
    const nmo_type_enum_value_t *enum_values;
    size_t enum_value_count;
    
    /* === Runtime operations === */
    const nmo_type_vtable_t *vtable;
    
    /* === Metadata === */
    const char *description;
    const char *ui_name;
    void *user_data;
};

/* ============================================================================
 * Type Registry - Unified Lookup
 * ============================================================================ */

/**
 * @brief Create unified type registry
 */
nmo_type_registry_t *nmo_type_registry_create(nmo_arena_t *arena);

/**
 * @brief Register type
 */
nmo_result_t nmo_type_registry_register(
    nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *descriptor);

/**
 * @brief Find by GUID - O(1) primary lookup
 */
const nmo_type_descriptor_t *nmo_type_registry_find_by_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Find by name - O(1) auxiliary lookup (for debugging)
 */
const nmo_type_descriptor_t *nmo_type_registry_find_by_name(
    const nmo_type_registry_t *registry,
    const char *name);

/**
 * @brief Find by type ID - O(1) runtime fast access
 */
const nmo_type_descriptor_t *nmo_type_registry_find_by_id(
    const nmo_type_registry_t *registry,
    uint32_t type_id);

/**
 * @brief Type compatibility check (supports derivation)
 */
bool nmo_type_is_compatible(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid,
    nmo_guid_t expected_guid);

/**
 * @brief Get inheritance depth
 */
int nmo_type_get_depth(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid);

/* ============================================================================
 * Declarative Registration Macros v2.0
 * ============================================================================ */

/**
 * @brief Declare type field with GUID reference
 */
#define TYPE_FIELD(fname, ftype_guid, stype) \
    { \
        .name = #fname, \
        .type_guid = (ftype_guid), \
        .offset = offsetof(stype, fname), \
        .size = sizeof(((stype *)0)->fname), \
        .flags = 0, \
        .added_version = 0, \
        .removed_version = 0 \
    }

/**
 * @brief Declare type field with flags
 */
#define TYPE_FIELD_EX(fname, ftype_guid, stype, ...) \
    { \
        .name = #fname, \
        .type_guid = (ftype_guid), \
        .offset = offsetof(stype, fname), \
        .size = sizeof(((stype *)0)->fname), \
        .flags = (__VA_ARGS__), \
        .added_version = 0, \
        .removed_version = 0 \
    }

/**
 * @brief Declare struct type
 * 
 * Usage:
 *   NMO_DECLARE_TYPE(Vector3, CKPGUID_VECTOR, nmo_vector_t) {
 *       TYPE_FIELD(x, CKPGUID_FLOAT, nmo_vector_t),
 *       TYPE_FIELD(y, CKPGUID_FLOAT, nmo_vector_t),
 *       TYPE_FIELD(z, CKPGUID_FLOAT, nmo_vector_t)
 *   };
 */
#define NMO_DECLARE_TYPE(tname, tguid, stype) \
    static const nmo_type_field_t tname##_fields[] =

/**
 * @brief Create type descriptor
 */
#define NMO_TYPE_DESCRIPTOR(tname, tguid, stype, category_flags) \
    ((nmo_type_descriptor_t){ \
        .guid = (tguid), \
        .name = #tname, \
        .type_id = 0, \
        .category = (category_flags), \
        .size = sizeof(stype), \
        .alignment = _Alignof(stype), \
        .version = 1, \
        .base_type = NMO_GUID_NULL, \
        .element_type = NMO_GUID_NULL, \
        .element_count = 0, \
        .class_id = 0, \
        .creator_plugin = NMO_GUID_NULL, \
        .fields = tname##_fields, \
        .field_count = sizeof(tname##_fields) / sizeof(tname##_fields[0]), \
        .enum_values = NULL, \
        .enum_value_count = 0, \
        .vtable = NULL, \
        .description = NULL, \
        .ui_name = #tname, \
        .user_data = NULL \
    })

/**
 * @brief Register type (one-liner)
 */
#define NMO_REGISTER_TYPE(registry, tname, tguid, stype, category_flags) \
    do { \
        nmo_type_descriptor_t desc = NMO_TYPE_DESCRIPTOR(tname, tguid, stype, category_flags); \
        nmo_result_t result = nmo_type_registry_register(registry, &desc); \
        if (result.code != NMO_OK) return result; \
    } while (0)

/**
 * @brief Declare enum type
 */
#define NMO_DECLARE_ENUM(ename) \
    static const nmo_type_enum_value_t ename##_values[] =

#define ENUM_VALUE(vname, vvalue) \
    { .name = #vname, .value = (vvalue), .description = NULL }

/**
 * @brief Register enum
 */
#define NMO_REGISTER_ENUM(registry, ename, eguid, etype) \
    do { \
        nmo_type_descriptor_t desc = { \
            .guid = (eguid), \
            .name = #ename, \
            .type_id = 0, \
            .category = NMO_TYPE_ENUM | NMO_TYPE_SERIALIZABLE, \
            .size = sizeof(etype), \
            .alignment = _Alignof(etype), \
            .version = 1, \
            .enum_values = ename##_values, \
            .enum_value_count = sizeof(ename##_values) / sizeof(ename##_values[0]) \
        }; \
        nmo_result_t result = nmo_type_registry_register(registry, &desc); \
        if (result.code != NMO_OK) return result; \
    } while (0)

/* ============================================================================
 * Compile-time Verification
 * ============================================================================ */

/**
 * @brief Verify type size at compile time
 */
#define NMO_VERIFY_TYPE_SIZE(stype, expected_size) \
    _Static_assert(sizeof(stype) == (expected_size), \
                   "Type " #stype " size mismatch: expected " #expected_size " bytes")

/**
 * @brief Verify type alignment at compile time
 */
#define NMO_VERIFY_TYPE_ALIGN(stype, expected_align) \
    _Static_assert(_Alignof(stype) == (expected_align), \
                   "Type " #stype " alignment mismatch: expected " #expected_align " bytes")

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_SYSTEM_V2_PROTOTYPE_H */
