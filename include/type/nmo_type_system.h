#ifndef NMO_TYPE_TYPE_SYSTEM_H
#define NMO_TYPE_TYPE_SYSTEM_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Type System v2.0 - Unified Type Metadata
 * 
 * Based on Virtools SDK reference implementation validation.
 * See: REFERENCE_VALIDATION_REPORT.md, SCHEMA_REFACTOR_ADDENDUM.md
 * ============================================================================ */

/* Forward declarations */
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_type_field nmo_type_field_t;
typedef struct nmo_type_vtable nmo_type_vtable_t;
typedef struct nmo_hash_table nmo_hash_table_t;
struct nmo_chunk;  /* Forward declare for function pointers */

/* Type ID - runtime fast access index */
typedef int32_t nmo_type_id_t;
#define NMO_TYPE_ID_INVALID ((nmo_type_id_t)-1)

/* Manager index for custom serialization */
typedef int32_t nmo_manager_index_t;
#define NMO_MANAGER_INDEX_INVALID ((nmo_manager_index_t)-1)

/* ============================================================================
 * Type Categories (Bit Flags)
 * 
 * Matches Virtools CKPARAMETERTYPE_* pattern
 * Reference: CKParameterManager.cpp, lines 460-475
 * ============================================================================ */

typedef enum nmo_type_category {
    /* Basic categories */
    NMO_TYPE_CATEGORY_SCALAR       = 0x0001,  /* Primitive types (int, float, etc) */
    NMO_TYPE_CATEGORY_STRUCT       = 0x0002,  /* Composite types with fields */
    NMO_TYPE_CATEGORY_UNION        = 0x0004,  /* Union types (overlapping fields) */
    NMO_TYPE_CATEGORY_ENUM         = 0x0008,  /* Enumeration with named values */
    NMO_TYPE_CATEGORY_FLAGS        = 0x0010,  /* Bit flags */
    
    /* Special categories */
    NMO_TYPE_CATEGORY_OBJECT_REF   = 0x0020,  /* Reference to CKObject */
    NMO_TYPE_CATEGORY_ARRAY        = 0x0040,  /* Array type */
    NMO_TYPE_CATEGORY_POINTER      = 0x0080,  /* Pointer type */
    
    /* Plugin extensibility */
    NMO_TYPE_CATEGORY_PLUGIN_BASE  = 0x1000,  /* Plugin-defined types start here */
    
    /* Visibility */
    NMO_TYPE_CATEGORY_HIDDEN       = 0x8000   /* Hidden from UI */
} nmo_type_category_t;

/* Type flags */
typedef enum nmo_type_flags {
    NMO_TYPE_FLAG_NONE             = 0x0000,
    NMO_TYPE_FLAG_DEPRECATED       = 0x0001,  /* Type is deprecated */
    NMO_TYPE_FLAG_SERIALIZABLE     = 0x0002,  /* Type can be serialized */
    NMO_TYPE_FLAG_COPYABLE         = 0x0004,  /* Type can be copied */
    NMO_TYPE_FLAG_POD              = 0x0008   /* Plain old data (memcpy safe) */
} nmo_type_flags_t;

/* ============================================================================
 * Specialized Metadata Descriptors
 * 
 * For enum, struct, and flags types, store detailed field information.
 * Reference: CKParameterManager.cpp, lines 298-304
 * ============================================================================ */

/**
 * @brief Enum value descriptor
 */
typedef struct nmo_enum_descriptor {
    const char *name;                   /* Enum constant name */
    int64_t value;                      /* Enum constant value */
    const char *description;            /* Optional description */
    uint32_t flags;                     /* Visibility/deprecated flags */
} nmo_enum_descriptor_t;

/**
 * @brief Struct field descriptor
 */
typedef struct nmo_struct_descriptor {
    const char *name;                   /* Field name */
    nmo_guid_t type_guid;              /* Field type GUID */
    uint32_t offset;                    /* Offset in bytes from struct start */
    uint32_t size;                      /* Field size in bytes */
    uint32_t array_count;               /* 0 = scalar, >0 = array */
    uint32_t flags;                     /* Field flags */
    const char *description;            /* Optional description */
    nmo_guid_t pointee_guid;           /* Base type GUID for pointer fields (NULL_GUID if not pointer) */
    uint32_t pointer_depth;             /* Pointer indirection depth (0 if not pointer) */
} nmo_struct_descriptor_t;

/**
 * @brief Flags (bitmask) bit descriptor
 */
typedef struct nmo_flags_descriptor {
    const char *name;                   /* Flag bit name */
    uint64_t mask;                      /* Bit mask value */
    const char *description;            /* Optional description */
    uint32_t flags;                     /* Visibility/deprecated flags */
} nmo_flags_descriptor_t;

/**
 * @brief Specialized metadata container
 * 
 * Stores enum values, struct fields, or flags bits for a specific type.
 */
typedef struct nmo_specialized_metadata {
    nmo_type_id_t type_id;              /* Owner type ID */
    uint16_t metadata_type;             /* ENUM, STRUCT, or FLAGS */
    uint16_t reserved;
    
    union {
        struct {
            const nmo_enum_descriptor_t *values;
            size_t value_count;
        } enum_meta;
        
        struct {
            const nmo_struct_descriptor_t *fields;
            size_t field_count;
        } struct_meta;

        struct {
            const nmo_struct_descriptor_t *fields;
            size_t field_count;
        } union_meta;
        
        struct {
            const nmo_flags_descriptor_t *bits;
            size_t bit_count;
        } flags_meta;
    };
} nmo_specialized_metadata_t;

/* Metadata type constants */
#define NMO_METADATA_TYPE_ENUM   1
#define NMO_METADATA_TYPE_STRUCT 2
#define NMO_METADATA_TYPE_FLAGS  3
#define NMO_METADATA_TYPE_UNION  4

/* Specialized metadata index constants */
#define NMO_SPECIALIZED_INDEX_INVALID ((uint32_t)UINT32_MAX)

/* ============================================================================
 * Field Semantic Annotations
 * 
 * Semantic hints for tools and editors (not used in serialization).
 * ============================================================================ */

typedef enum nmo_field_semantic {
    NMO_SEMANTIC_NONE = 0,
    
    /* Spatial */
    NMO_SEMANTIC_POSITION,
    NMO_SEMANTIC_ROTATION,
    NMO_SEMANTIC_SCALE,
    NMO_SEMANTIC_DIRECTION,
    NMO_SEMANTIC_NORMAL,
    
    /* Visual */
    NMO_SEMANTIC_COLOR,
    NMO_SEMANTIC_ALPHA,
    NMO_SEMANTIC_UV,
    
    /* Reference */
    NMO_SEMANTIC_ID,
    NMO_SEMANTIC_OBJECT_REF,
    NMO_SEMANTIC_MANAGER_REF,
    
    /* Temporal */
    NMO_SEMANTIC_TIME,
    NMO_SEMANTIC_DURATION,
    
    /* Other */
    NMO_SEMANTIC_NAME,
    NMO_SEMANTIC_PATH,
    NMO_SEMANTIC_USER_DATA,
} nmo_field_semantic_t;

typedef enum nmo_field_units {
    NMO_UNITS_NONE = 0,
    
    /* Angular */
    NMO_UNITS_DEGREES,
    NMO_UNITS_RADIANS,
    
    /* Spatial */
    NMO_UNITS_METERS,
    NMO_UNITS_CENTIMETERS,
    NMO_UNITS_UNITS,  /* Generic units */
    
    /* Temporal */
    NMO_UNITS_SECONDS,
    NMO_UNITS_MILLISECONDS,
    NMO_UNITS_FRAMES,
} nmo_field_units_t;

/* Field flags */
#define NMO_FIELD_REQUIRED        0x0001  /* Field must be present */
#define NMO_FIELD_OPTIONAL        0x0002  /* Field may be absent */
#define NMO_FIELD_REPEATED        0x0004  /* Array field */
#define NMO_FIELD_DEPRECATED      0x0008  /* No longer recommended */
#define NMO_FIELD_EDITOR_ONLY     0x0010  /* Only in editor, strip for runtime */
#define NMO_FIELD_RUNTIME_ONLY    0x0020  /* Runtime-generated, don't serialize */
#define NMO_FIELD_ID              0x0040  /* Object/Manager ID field */
#define NMO_FIELD_REFERENCE       0x0080  /* Reference to another object */

/* ============================================================================
 * Type Field Descriptor
 * 
 * Extended with annotation support per design.md Section 5.4
 * ============================================================================ */

typedef struct nmo_type_field {
    const char *name;                   /* Field name */
    const char *description;            /* Documentation (optional) */
    nmo_guid_t type_guid;              /* Field type GUID */
    uint32_t offset;                    /* Offset in bytes */
    uint32_t size;                      /* Size in bytes */
    uint32_t flags;                     /* Field flags (NMO_FIELD_*) */
    uint32_t added_version;             /* Version added (for migration) */
    uint32_t removed_version;           /* Version removed (0 = still present) */
    
    /* === Annotations (optional) === */
    nmo_field_semantic_t semantic;      /* Semantic hint */
    nmo_field_units_t units;            /* Unit of measurement */
    const void *default_value;          /* Default value pointer (optional) */
} nmo_type_field_t;

/* ============================================================================
 * Compatibility Mask (Bit Vector)
 * 
 * Tracks inheritance hierarchy for O(1) compatibility checks.
 * Reference: CKParameterManager.cpp, lines 1265-1276
 * ============================================================================ */

#include "core/nmo_bit_array.h"

/**
 * @brief Compatibility mask growth granularity (in bits)
 *
 * Historically this was a hard cap (256 types). It is now used only as a
 * coarse growth hint.
 */
#define NMO_TYPE_COMPAT_MASK_SIZE 256

typedef struct nmo_compatibility_mask {
    nmo_bit_array_t bits;
} nmo_compatibility_mask_t;

static inline void nmo_compat_mask_set(nmo_compatibility_mask_t *mask, nmo_type_id_t id) {
    if (!mask || id < 0) {
        return;
    }

    (void)nmo_bit_array_set(&mask->bits, (size_t)id);
}

static inline bool nmo_compat_mask_is_set(const nmo_compatibility_mask_t *mask, nmo_type_id_t id) {
    if (!mask || id < 0) {
        return false;
    }

    return nmo_bit_array_test(&mask->bits, (size_t)id) != 0;
}

static inline void nmo_compat_mask_clear(nmo_compatibility_mask_t *mask) {
    if (!mask) {
        return;
    }

    nmo_bit_array_clear_all(&mask->bits);
}

/* ============================================================================
 * Type Virtual Table (Zero-cost Extension Points)
 * ============================================================================ */

typedef nmo_status_t (*nmo_type_create_fn)(void *instance, const nmo_type_descriptor_t *type, void *context);
typedef void (*nmo_type_destroy_fn)(void *instance, const nmo_type_descriptor_t *type, void *context);
typedef nmo_status_t (*nmo_type_copy_fn)(const void *src, void *dst, const nmo_type_descriptor_t *type, nmo_arena_t *arena);
typedef nmo_status_t (*nmo_type_serialize_fn)(const void *instance, struct nmo_chunk *chunk, const nmo_type_descriptor_t *type, void *context);
typedef nmo_status_t (*nmo_type_deserialize_fn)(void *instance, struct nmo_chunk *chunk, const nmo_type_descriptor_t *type, void *context);
typedef nmo_status_t (*nmo_type_validate_fn)(const void *instance, const nmo_type_descriptor_t *type, void *context);
typedef bool (*nmo_type_equals_fn)(const void *a, const void *b);
typedef uint32_t (*nmo_type_hash_fn)(const void *instance);

/* Finish-loading hook (CKObject::PostLoad equivalent for object types).
 * Kept in type layer so app/session can dispatch without depending on object layer. */
typedef nmo_status_t (*nmo_type_finish_loading_fn)(void *instance, nmo_arena_t *arena, void *repository);

/* Phase 6.4: String conversion function pointers */
typedef nmo_status_t (*nmo_type_to_string_fn)(const void *value, const nmo_type_descriptor_t *type, char *buffer, size_t buffer_size, void *context);
typedef nmo_status_t (*nmo_type_from_string_fn)(void *value, const nmo_type_descriptor_t *type, const char *string, void *context);

/* ============================================================================
 * Reference Enumeration (Reflection)
 * 
 * Visitor pattern for enumerating object references. Used by reference graph,
 * validation, and garbage collection. Types can provide custom implementations
 * or use the default field-based enumerator.
 * 
 * DESIGN NOTE: The type layer provides the enumeration MECHANISM only.
 * Semantic interpretation of ref_kind values is defined by higher layers
 * (Object/Session). The type layer treats ref_kind as an opaque uint32_t.
 * ============================================================================ */

/**
 * @brief Reference visitor callback (generic)
 *
 * Called for each reference found by an enumerator.
 * The ref_kind is an opaque value - concrete semantics are defined by
 * higher layers (Session/Object layer defines nmo_ref_kind_t enum).
 *
 * @param user_data User-provided context
 * @param target_id Referenced object ID (non-zero)
 * @param ref_kind Opaque reference kind (semantics defined by higher layers)
 * @param field_name Field name containing the reference (may be NULL)
 * @param index Array index (0 for non-array fields)
 * @return true to continue enumeration, false to stop
 */
typedef bool (*nmo_type_ref_visitor_fn)(
    void *user_data,
    uint32_t target_id,
    uint32_t ref_kind,
    const char *field_name,
    uint32_t index
);

/**
 * @brief Reference enumerator function pointer
 *
 * Enumerates all references from a type instance.
 * The visitor receives opaque ref_kind values - semantic interpretation
 * is the responsibility of the caller (Session/Object layer).
 *
 * @param instance Object state pointer
 * @param type Type descriptor
 * @param visitor Callback for each reference
 * @param user_data User context
 * @return NMO_OK on success
 */
typedef nmo_status_t (*nmo_type_enumerate_refs_fn)(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data
);

typedef struct nmo_type_vtable {
    /* Lifecycle hooks */
    nmo_type_create_fn create;          /* Create default instance */
    nmo_type_destroy_fn destroy;        /* Destroy instance */
    
    /* Operations */
    nmo_type_copy_fn copy;              /* Deep copy */
    nmo_type_serialize_fn serialize;    /* Custom serialization */
    nmo_type_deserialize_fn deserialize;/* Custom deserialization */
    nmo_type_validate_fn validate;      /* Validation */
    
    /* Comparison */
    nmo_type_equals_fn equals;          /* Equality check */
    nmo_type_hash_fn hash;              /* Hash function */
    
    /* String conversion (Phase 6.4) */
    nmo_type_to_string_fn to_string;    /* Convert value to string */
    nmo_type_from_string_fn from_string;/* Parse value from string */
    
    /* Reference enumeration (Reflection) */
    nmo_type_enumerate_refs_fn enumerate_refs; /* Enumerate object references */
} nmo_type_vtable_t;

/* ============================================================================
 * Phase 6.6: Custom Manager Support
 * 
 * Manager-based serialization for special types (Message, Attribute).
 * Reference: CKParameterManager.cpp custom serialization hooks
 * ============================================================================ */

/* Forward declare chunk type */
struct nmo_chunk;

/**
 * @brief Manager serialize function
 * 
 * Custom serialization callback for types requiring specialized handling.
 * Used by Message and Attribute types in Virtools.
 * 
 * @param instance Value to serialize
 * @param chunk Target chunk
 * @param manager_context Manager-specific context
 * @return nmo_ok() on success
 */
typedef nmo_status_t (*nmo_manager_serialize_fn)(
    const void *instance,
    struct nmo_chunk *chunk,
    void *manager_context);

/**
 * @brief Manager deserialize function
 * 
 * Custom deserialization callback for types requiring specialized handling.
 * 
 * @param instance Target value
 * @param chunk Source chunk
 * @param manager_context Manager-specific context
 * @return nmo_ok() on success
 */
typedef nmo_status_t (*nmo_manager_deserialize_fn)(
    void *instance,
    struct nmo_chunk *chunk,
    void *manager_context);

/**
 * @brief Saver manager descriptor
 * 
 * Represents a custom serialization manager for types that need
 * specialized save/load logic (e.g., Message, Attribute).
 */
typedef struct nmo_saver_manager {
    nmo_guid_t guid;                        /* Manager GUID */
    const char *name;                       /* Manager name */
    nmo_manager_serialize_fn serialize;     /* Serialize callback */
    nmo_manager_deserialize_fn deserialize; /* Deserialize callback */
    void *context;                          /* Manager-specific context */
} nmo_saver_manager_t;

/* ============================================================================
 * Unified Type Descriptor
 * 
 * Merges legacy schema metadata and param metadata into a single structure.
 * Reference: CKParameterTypeDesc validation
 * Extended with annotation support per design.md Section 5.4
 * ============================================================================ */

typedef struct nmo_type_descriptor_ext {
    nmo_compatibility_mask_t compat_mask;      /* Cached derivation mask */
    const struct nmo_type_descriptor **hierarchy; /* Array of ancestor types (root first, self last) */
    uint32_t *state_offsets;                   /* Byte offset of each ancestor's state */
    uint16_t hierarchy_depth;                  /* Number of types in hierarchy (including self) */
    uint16_t _padding;                         /* Alignment padding */
    uint32_t total_state_size;                 /* Sum of all ancestor state sizes */
} nmo_type_descriptor_ext_t;

typedef struct nmo_type_descriptor {
    /* === Core Identity (40 bytes) === */
    nmo_guid_t guid;                    /* Type GUID (primary key) */
    nmo_type_id_t id;                   /* Runtime type ID (array index) */
    uint32_t class_id;                  /* Virtools CK_CLASSID (for objects) */
    uint16_t category;                  /* Type category bit flags */
    uint16_t flags;                     /* Type flags */

    /* === Naming & Documentation (24 bytes) === */
    const char *name;                   /* Type name */
    const char *description;            /* Type documentation (optional) */
    nmo_guid_t base_type;               /* Parent type GUID (for inheritance) */
    nmo_type_id_t base_type_id;         /* Cached parent type ID */

    /* === Size & Alignment (8 bytes) === */
    uint32_t size;                      /* Size in bytes */
    uint32_t alignment;                 /* Alignment requirement */

    /* === Fields (16 bytes) === */
    const nmo_type_field_t *fields;     /* Field descriptors (for structs) */
    size_t field_count;                 /* Number of fields */

    /* === Extension Points (8 bytes) === */
    const nmo_type_vtable_t *vtable;    /* Virtual function table */
    nmo_type_finish_loading_fn finish_loading; /* Optional post-deserialize hook */

    /* === Plugin Tracking (16 bytes) === */
    nmo_guid_t creator_plugin_guid;     /* Extension plugin GUID that registered this type */
    nmo_manager_index_t saver_manager;  /* Manager for custom serialization */
    uint32_t specialized_index;         /* 0-based index into metadata array (NMO_SPECIALIZED_INDEX_INVALID if none) */
    bool valid;                          /* FALSE after unregistration (soft invalidation) */
    uint8_t _padding0[3];               /* Alignment padding */

    /* === Versioning (8 bytes) === */
    uint32_t version;                   /* Type version (0 = unspecified) */
    uint32_t min_compatible_version;    /* Minimum compatible version */

    /* === Cold/Derived Data === */
    nmo_type_descriptor_ext_t *ext;     /* Optional cold data storage */
} nmo_type_descriptor_t;

/**
 * @brief Per-type alias list tracking (registry-owned).
 */
typedef struct nmo_type_alias_list {
    const char **aliases;  /* Pointer array of alias strings */
    size_t count;          /* Number of aliases */
    size_t capacity;       /* Allocated alias slots */
} nmo_type_alias_list_t;

/**
 * @brief Per-type derived list tracking (registry-owned).
 */
typedef struct nmo_type_child_list {
    nmo_type_id_t *children; /* Pointer array of child type IDs */
    size_t count;            /* Number of children */
    size_t capacity;         /* Allocated child slots */
} nmo_type_child_list_t;

/* ============================================================================
 * Type Registry
 * 
 * Unified registry replacing schema_registry + param_type_table.
 * Reference: CKParameterManager structure
 * ============================================================================ */

typedef struct nmo_type_registry {
    /* === Type Storage === */
    nmo_arena_array_t types;     /* Type array (index = type_id) */
    
    /* === Fast Lookup Tables === */
    nmo_hash_table_t *guid_map;  /* GUID -> type_id (O(1) primary lookup) */
    nmo_hash_table_t *name_map;  /* name -> type_id (O(1) auxiliary lookup) */
    nmo_hash_table_t *class_id_map;  /* class_id -> type_id (O(1) for Virtools objects) */
    nmo_arena_array_t alias_lists; /* Array of nmo_type_alias_list_t (index = type_id) */
    nmo_arena_array_t child_lists; /* Array of nmo_type_child_list_t (index = type_id) */
    nmo_arena_array_t free_slots;  /* Stack of freed type slots */
    nmo_hash_table_t *class_id_inherited_map; /* class_id -> inherited type_id cache */
    uint32_t class_id_inherited_version;      /* Cache version (matches registry_version) */
    
    /* === Derivation Cache === */
    bool derivation_masks_valid;        /* FALSE when types change (lazy update) */
    
    /* === Plugin Tracking === */
    nmo_hash_table_t *type_to_plugin;/* type_id -> plugin_guid */
    
    /* === Specialized Metadata Storage === */
    nmo_arena_array_t metadata;  /* Array of metadata containers */
    nmo_hash_table_t *type_to_metadata;  /* type_id -> metadata_index (O(1) lookup) */
    
    /* === Custom Manager Support (Phase 6.6) === */
    nmo_arena_array_t saver_managers;   /* Array of custom managers */
    nmo_hash_table_t *manager_guid_map;     /* manager_guid -> manager_index (O(1) lookup) */
    nmo_hash_table_t *type_to_manager;      /* type_id -> manager_index (O(1) lookup) */
    
    /* === Memory Management === */
    nmo_arena_t *arena;                 /* Arena allocator */
    nmo_allocator_t type_allocator;     /* Allocator for type-owned data */
    
    /* === Statistics === */
    size_t builtin_count;               /* Built-in types */
    size_t plugin_count;                /* Plugin-defined types */
    uint32_t registry_version;          /* Version for cache invalidation */
} nmo_type_registry_t;

/* ============================================================================
 * Type Registry API
 * ============================================================================ */

/**
 * @brief Create type registry
 * @param arena Arena allocator for registry memory
 * @return New registry instance, or NULL on failure
 */
nmo_type_registry_t* nmo_type_registry_create(nmo_arena_t *arena);

/**
 * @brief Create type registry with custom allocator for type-owned data
 * @param arena Arena allocator for registry memory
 * @param type_allocator Allocator for type-owned data (descriptors, strings, fields)
 * @return New registry instance, or NULL on failure
 */
nmo_type_registry_t* nmo_type_registry_create_ex(nmo_arena_t *arena, nmo_allocator_t type_allocator);

/**
 * @brief Destroy type registry
 * @param registry Registry to destroy
 */
void nmo_type_registry_destroy(nmo_type_registry_t *registry);

/**
 * @brief Register new type (with slot recycling)
 * 
 * Automatically reuses slots from unregistered types.
 * Reference: CKParameterManager::RegisterParameterType, lines 11-37
 * 
 * @param registry Registry
 * @param descriptor Type descriptor to register
 * @return nmo_ok() on success, error on failure
 */
nmo_status_t nmo_type_registry_register(
    nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *descriptor);

/**
 * @brief Unregister type (soft invalidation)
 * 
 * Sets valid=false, keeps slot for recycling.
 * Reference: CKParameterManager::UnRegisterParameterType, lines 66-145
 * 
 * @param registry Registry
 * @param guid Type GUID to unregister
 * @return nmo_ok() on success, error if not found
 */
nmo_status_t nmo_type_registry_unregister(
    nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Unregister all types from a plugin (cascade deletion)
 * 
 * Recursively unregisters derived types first.
 * Reference: CKParameterManager cascade deletion pattern
 * 
 * @param registry Registry
 * @param plugin_guid Plugin GUID
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_unregister_plugin_types(
    nmo_type_registry_t *registry,
    nmo_guid_t plugin_guid);

/**
 * @brief Find type by GUID (O(1) primary lookup)
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @return Type descriptor, or NULL if not found
 */
const nmo_type_descriptor_t* nmo_type_registry_find_by_guid(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Find type by name (O(1) auxiliary lookup)
 * 
 * Used for debugging and user input, not primary path.
 * 
 * @param registry Registry
 * @param name Type name
 * @return Type descriptor, or NULL if not found
 */
const nmo_type_descriptor_t* nmo_type_registry_find_by_name(
    const nmo_type_registry_t *registry,
    const char *name);

/**
 * @brief Add an additional name alias for an existing type.
 *
 * This is primarily used for backward-compatible spellings (e.g. legacy
 * uppercase names) while keeping canonical type names normalized.
 *
 * @param registry Registry
 * @param type_id Type ID to alias
 * @param alias Alias string (will be copied into the registry arena)
 * @return nmo_ok() on success, error on failure
 */
nmo_status_t nmo_type_registry_add_name_alias(
    nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    const char *alias);

/**
 * @brief Find type by Virtools class ID (O(1) lookup)
 * 
 * Used for loading Virtools files - maps CK_CLASSID to type descriptor.
 * 
 * @param registry Registry
 * @param class_id Virtools CK_CLASSID
 * @return Type descriptor, or NULL if not found
 */
const nmo_type_descriptor_t* nmo_type_registry_find_by_class_id(
    const nmo_type_registry_t *registry,
    uint32_t class_id);

/**
 * @brief Find type by class ID with inheritance fallback
 * 
 * Searches up the class hierarchy using nmo_ckclass API for parent lookups.
 * This is crucial for Virtools file loading where derived classes may not have schemas.
 * 
 * @param registry Registry
 * @param class_id Virtools CK_CLASSID
 * @return Type descriptor (possibly parent class), or NULL if not found
 */
const nmo_type_descriptor_t* nmo_type_registry_find_by_class_id_inherited(
    nmo_type_registry_t *registry,
    uint32_t class_id);

/**
 * @brief Get type by ID (O(1) runtime fast access)
 * 
 * @param registry Registry
 * @param id Type ID
 * @return Type descriptor, or NULL if invalid ID
 */
const nmo_type_descriptor_t* nmo_type_registry_get_by_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t id);

/* ============================================================================
 * Phase 6.3: Type Compatibility & Conversion API
 * ============================================================================ */

/**
 * @brief Check if type is derived from parent (O(1) with cached masks)
 * 
 * Uses compatibility mask for fast lookup. Automatically triggers lazy
 * derivation mask update if registry changed.
 * Reference: CKParameterManager::IsDerivedFrom (pattern from IsTypeCompatible)
 * 
 * @param registry Registry
 * @param child_id Child type ID
 * @param parent_id Parent type ID
 * @return true if child_id derives from parent_id (or is same type)
 */
bool nmo_type_is_derived_from(
    nmo_type_registry_t *registry,
    nmo_type_id_t child_id,
    nmo_type_id_t parent_id);

/**
 * @brief Get full inheritance chain for a type
 * 
 * Returns array of type IDs from most-derived (self) to least-derived (root).
 * Example: [Vector3D, Vector2D, Scalar] for Vector3D
 * 
 * @param registry Registry
 * @param type_id Type ID to query
 * @param out_chain Output: array of type IDs (arena-allocated)
 * @param out_count Output: length of chain
 * @param arena Arena allocator for output array
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_get_inheritance_chain(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_type_id_t **out_chain,
    size_t *out_count,
    nmo_arena_t *arena);

/**
 * @brief Check type compatibility (O(1) with cached masks)
 * 
 * Two types are compatible if:
 * - They are the same type, OR
 * - One derives from the other (symmetric inheritance check)
 * 
 * Automatically triggers lazy derivation mask update if needed.
 * Reference: CKParameterManager::IsTypeCompatible, lines 257-270
 * 
 * @param registry Registry
 * @param type1 First type ID
 * @param type2 Second type ID
 * @return true if compatible (type1 can be used as type2 or vice versa)
 */
bool nmo_type_is_compatible(
    nmo_type_registry_t *registry,
    nmo_type_id_t type1,
    nmo_type_id_t type2);

/**
 * @brief Get inheritance depth (0 = no relation, 1 = direct parent, etc)
 * 
 * @param registry Registry
 * @param child_id Child type ID
 * @param parent_id Parent type ID
 * @return Depth (1=direct parent, 2=grandparent, etc), or -1 if not derived
 */
int32_t nmo_type_get_derivation_depth(
    nmo_type_registry_t *registry,
    nmo_type_id_t child_id,
    nmo_type_id_t parent_id);

/**
 * @brief Finalize registry caches (derivation masks, derived caches)
 *
 * Must be called after registration/unregistration before concurrent read usage.
 *
 * @param registry Registry
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_finalize(nmo_type_registry_t *registry);

/* --- Type Conversion API (Phase 6.3.3) ---
 *
 * These functions provide bidirectional conversion between different type identifiers.
 * While there are multiple conversion paths, each serves a specific purpose:
 *
 * Direct Conversions (O(1)):
 * - GUID <-> Type ID: Primary conversion (GUID is canonical, Type ID is fast)
 * - Type ID <-> Name: For debugging and UI display
 * - GUID <-> ClassID: For Virtools file format compatibility
 *
 * The API is designed for clarity rather than minimalism - each function name
 * explicitly states the conversion direction, making the code self-documenting.
 *
 * TODO: Consider adding a generic conversion function for plugin extensibility:
 *   nmo_status_t nmo_type_convert(const nmo_type_registry_t *registry,
 *                                 nmo_type_identifier_type_t from_type,
 *                                 const void *from_value,
 *                                 nmo_type_identifier_type_t to_type,
 *                                 void *to_value);
 */

/**
 * @brief Convert GUID to Type ID (O(1) hash lookup)
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @return Type ID, or NMO_TYPE_ID_INVALID if not found
 */
nmo_type_id_t nmo_type_registry_guid_to_type_id(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Convert Type ID to GUID (O(1) array access)
 * 
 * @param registry Registry
 * @param type_id Type ID
 * @param out_guid Output: Type GUID
 * @return nmo_ok() on success, error if invalid ID
 */
nmo_status_t nmo_type_registry_type_id_to_guid(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_guid_t *out_guid);

/**
 * @brief Convert GUID to Type Name (O(1) hash lookup + direct access)
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @return Type name, or NULL if not found
 */
const char* nmo_type_registry_guid_to_name(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Convert Name to GUID (O(1) hash lookup)
 * 
 * @param registry Registry
 * @param name Type name
 * @param out_guid Output: Type GUID
 * @return nmo_ok() on success, error if not found
 */
nmo_status_t nmo_type_registry_name_to_guid(
    const nmo_type_registry_t *registry,
    const char *name,
    nmo_guid_t *out_guid);

/**
 * @brief Convert Type ID to Name (O(1) array access)
 * 
 * @param registry Registry
 * @param type_id Type ID
 * @return Type name, or NULL if invalid ID
 */
const char* nmo_type_registry_type_id_to_name(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id);

/**
 * @brief Convert Name to Type ID (O(1) hash lookup)
 * 
 * @param registry Registry
 * @param name Type name
 * @return Type ID, or NMO_TYPE_ID_INVALID if not found
 */
nmo_type_id_t nmo_type_registry_name_to_type_id(
    const nmo_type_registry_t *registry,
    const char *name);

/**
 * @brief Convert ClassID to GUID (O(1) hash lookup)
 * 
 * @param registry Registry
 * @param class_id Virtools CK_CLASSID
 * @param out_guid Output: Type GUID
 * @return nmo_ok() on success, error if not found
 */
nmo_status_t nmo_type_registry_class_id_to_guid(
    const nmo_type_registry_t *registry,
    uint32_t class_id,
    nmo_guid_t *out_guid);

/**
 * @brief Convert GUID to ClassID (O(1) hash lookup + direct access)
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @param out_class_id Output: Virtools CK_CLASSID
 * @return nmo_ok() on success, error if not found or type has no ClassID
 */
nmo_status_t nmo_type_registry_guid_to_class_id(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid,
    uint32_t *out_class_id);

/**
 * @brief Convert Type ID to ClassID (O(1) array access)
 * 
 * @param registry Registry
 * @param type_id Type ID
 * @param out_class_id Output: Virtools CK_CLASSID
 * @return nmo_ok() on success, error if invalid ID or type has no ClassID
 */
nmo_status_t nmo_type_registry_type_id_to_class_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    uint32_t *out_class_id);

/**
 * @brief Convert ClassID to Type ID (O(1) hash lookup)
 * 
 * @param registry Registry
 * @param class_id Virtools CK_CLASSID
 * @return Type ID, or NMO_TYPE_ID_INVALID if not found
 */
nmo_type_id_t nmo_type_registry_class_id_to_type_id(
    const nmo_type_registry_t *registry,
    uint32_t class_id);

/**
 * @brief Manually update derivation masks (usually automatic)
 * 
 * Deferred O(n²) computation of compatibility masks.
 * Reference: CKParameterManager::UpdateDerivationTables, lines 1265-1276
 * 
 * @param registry Registry
 */
void nmo_type_registry_update_derivation_masks(nmo_type_registry_t *registry);

/**
 * @brief Compute state layouts for all types
 * 
 * Computes inheritance hierarchy and state offsets for each type.
 * This enables ECS-style combined state allocation where a single buffer
 * holds all inherited state structures at computed offsets.
 * 
 * Should be called after all types are registered.
 * 
 * @param registry Registry
 */
void nmo_type_registry_compute_state_layouts(nmo_type_registry_t *registry);

/**
 * @brief Get state offset for an ancestor type within a derived type's combined state
 * 
 * Returns the byte offset where ancestor_type's state is located within
 * derived_type's combined state buffer.
 * 
 * @param registry Registry
 * @param derived_type The derived type whose state layout we're querying
 * @param ancestor_type The ancestor type whose offset we want
 * @return Byte offset, or (uint32_t)-1 if not found
 */
uint32_t nmo_type_get_state_offset(
    const nmo_type_registry_t *registry,
    const nmo_type_descriptor_t *derived_type,
    const nmo_type_descriptor_t *ancestor_type);

/**
 * @brief Get registry statistics
 * 
 * @param registry Registry
 * @param total_types Output: total types
 * @param builtin_types Output: built-in types
 * @param plugin_types Output: plugin types
 */
void nmo_type_registry_get_stats(
    const nmo_type_registry_t *registry,
    size_t *total_types,
    size_t *builtin_types,
    size_t *plugin_types);

/* ============================================================================
 * Phase 6.5: Type Statistics & Visibility Control API
 * ============================================================================ */

/**
 * @brief Get total number of registered types
 * 
 * @param registry Registry
 * @return Total number of valid types
 */
size_t nmo_type_registry_get_type_count(const nmo_type_registry_t *registry);

/**
 * @brief Get number of builtin types
 * 
 * @param registry Registry
 * @return Number of builtin types
 */
size_t nmo_type_registry_get_builtin_count(const nmo_type_registry_t *registry);

/**
 * @brief Get number of plugin-defined types
 * 
 * @param registry Registry
 * @return Number of plugin-registered types
 */
size_t nmo_type_registry_get_plugin_type_count(const nmo_type_registry_t *registry);

/**
 * @brief Get number of Flags types
 * 
 * @param registry Registry
 * @return Number of Flags types
 */
size_t nmo_type_registry_get_flags_count(const nmo_type_registry_t *registry);

/**
 * @brief Get number of Enum types
 * 
 * @param registry Registry
 * @return Number of Enum types
 */
size_t nmo_type_registry_get_enum_count(const nmo_type_registry_t *registry);

/**
 * @brief Get number of Struct types
 * 
 * @param registry Registry
 * @return Number of Struct types
 */
size_t nmo_type_registry_get_struct_count(const nmo_type_registry_t *registry);

/**
 * @brief Get estimated memory usage of registry in bytes
 * 
 * Includes: type descriptors, hash tables, metadata, arena overhead
 * 
 * @param registry Registry
 * @return Estimated memory usage in bytes
 */
size_t nmo_type_registry_get_memory_usage(const nmo_type_registry_t *registry);

/**
 * @brief Check if type is visible in UI
 * 
 * Types with NMO_TYPE_CATEGORY_HIDDEN flag are hidden from UI.
 * Typically used for internal types (POINTER, BUFFER).
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @return true if type should be visible in UI, false otherwise
 */
bool nmo_type_registry_is_ui_visible(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Check if type is visible in UI (by Type ID)
 * 
 * @param registry Registry
 * @param type_id Type ID
 * @return true if type should be visible in UI, false otherwise
 */
bool nmo_type_registry_is_ui_visible_by_id(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id);

/**
 * @brief Set UI visibility for a type
 * 
 * Dynamically modify visibility by toggling NMO_TYPE_CATEGORY_HIDDEN flag.
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @param visible true to make visible, false to hide
 * @return nmo_ok() on success, error if type not found
 */
nmo_status_t nmo_type_registry_set_ui_visibility(
    nmo_type_registry_t *registry,
    nmo_guid_t guid,
    bool visible);

/* ============================================================================
 * Specialized Metadata API
 * ============================================================================ */

/**
 * @brief Register specialized metadata for a type
 * 
 * @param registry Registry
 * @param metadata Metadata container (enum/struct/flags). Contents are
 *        deep-copied into the registry arena, so caller buffers may be temporary.
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_register_metadata(
    nmo_type_registry_t *registry,
    const nmo_specialized_metadata_t *metadata);

/**
 * @brief Get specialized metadata for a type
 * 
 * @param registry Registry
 * @param type_id Type ID
 * @return Metadata container, or NULL if not found
 */
const nmo_specialized_metadata_t* nmo_type_registry_get_metadata(
    const nmo_type_registry_t *registry,
    nmo_type_id_t type_id);

/**
 * @brief Unregister specialized metadata for a type
 * 
 * @param registry Registry
 * @param type_id Type ID
 */
void nmo_type_registry_unregister_metadata(
    nmo_type_registry_t *registry,
    nmo_type_id_t type_id);

/* ============================================================================
 * Plugin Tracking API (Cascade Deletion)
 * ============================================================================ */

/**
 * @brief Set creator plugin GUID for a type
 * 
 * Associates a type with the extension plugin that registered it.
 * Used for cascade deletion when unloading plugins.
 * 
 * @param registry Registry
 * @param type_id Type ID
 * @param plugin_guid Plugin GUID
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_set_creator_plugin(
    nmo_type_registry_t *registry,
    nmo_type_id_t type_id,
    nmo_guid_t plugin_guid);

/**
 * @brief Unregister all derived types recursively
 * 
 * Used internally by cascade deletion.
 * Reference: CKParameterManager.cpp, lines 140-145
 * 
 * @param registry Registry
 * @param base_guid Base type GUID
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_unregister_derived(
    nmo_type_registry_t *registry,
    nmo_guid_t base_guid);

/**
 * @brief Invalidate type (soft delete)
 * 
 * Marks type as invalid without removing slot.
 * Reference: CKParameterManager.cpp, lines 84-129
 * 
 * @param registry Registry
 * @param guid Type GUID
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_invalidate(
    nmo_type_registry_t *registry,
    nmo_guid_t guid);

/* ============================================================================
 * Phase 6.6: Custom Manager Registration API
 * 
 * Manage custom serialization handlers for special types (Message, Attribute).
 * These types require manager-specific serialize/deserialize logic.
 * ============================================================================ */

/**
 * @brief Register custom saver manager
 * 
 * Registers a custom serialization manager for types that require
 * specialized save/load logic (e.g., Message, Attribute types).
 * 
 * @param registry Type registry
 * @param manager_guid Unique manager GUID
 * @param name Manager name (for debugging)
 * @param serialize Serialize callback function
 * @param deserialize Deserialize callback function
 * @param manager_context Manager-specific context (optional)
 * @return nmo_ok() on success, error if GUID already registered
 */
nmo_status_t nmo_type_registry_register_saver_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t manager_guid,
    const char *name,
    nmo_manager_serialize_fn serialize,
    nmo_manager_deserialize_fn deserialize,
    void *manager_context);

/**
 * @brief Unregister custom saver manager
 * 
 * Removes a saver manager and unlinks all types using it.
 * Types will fall back to their vtable or default serialization.
 * 
 * @param registry Type registry
 * @param manager_guid Manager GUID
 * @return nmo_ok() on success, error if not found
 */
nmo_status_t nmo_type_registry_unregister_saver_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t manager_guid);

/**
 * @brief Get saver manager by GUID
 * 
 * @param registry Type registry
 * @param manager_guid Manager GUID
 * @return Manager descriptor, or NULL if not found
 */
const nmo_saver_manager_t* nmo_type_registry_get_saver_manager(
    const nmo_type_registry_t *registry,
    nmo_guid_t manager_guid);

/**
 * @brief Associate type with custom manager
 * 
 * Links a type to a custom saver manager for specialized serialization.
 * The manager must be registered before calling this function.
 * 
 * @param registry Type registry
 * @param type_guid Type GUID
 * @param manager_guid Manager GUID
 * @return nmo_ok() on success, error if type or manager not found
 */
nmo_status_t nmo_type_registry_set_type_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid,
    nmo_guid_t manager_guid);

/**
 * @brief Get manager associated with type
 * 
 * @param registry Type registry
 * @param type_guid Type GUID
 * @return Manager descriptor, or NULL if no manager assigned
 */
const nmo_saver_manager_t* nmo_type_registry_get_type_manager(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid);

/**
 * @brief Clear manager association for type
 * 
 * Type will fall back to vtable or default serialization.
 * 
 * @param registry Type registry
 * @param type_guid Type GUID
 * @return nmo_ok() on success
 */
nmo_status_t nmo_type_registry_clear_type_manager(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid);

/**
 * @brief Get count of registered managers
 * 
 * @param registry Type registry
 * @return Number of registered saver managers
 */
size_t nmo_type_registry_get_manager_count(const nmo_type_registry_t *registry);

/* ============================================================================
 * Field Annotation Query API
 * 
 * Query field semantic information, units, and documentation.
 * Used by tools and editors (not part of core serialization).
 * ============================================================================ */

/**
 * @brief Get semantic annotation for field
 * 
 * @param field Field descriptor
 * @return Semantic hint (NMO_SEMANTIC_NONE if not annotated)
 */
static inline nmo_field_semantic_t nmo_field_get_semantic(const nmo_type_field_t *field) {
    return field ? field->semantic : NMO_SEMANTIC_NONE;
}

/**
 * @brief Get unit annotation for field
 * 
 * @param field Field descriptor
 * @return Unit hint (NMO_UNITS_NONE if not annotated)
 */
static inline nmo_field_units_t nmo_field_get_units(const nmo_type_field_t *field) {
    return field ? field->units : NMO_UNITS_NONE;
}

/**
 * @brief Check if field is editor-only
 * 
 * @param field Field descriptor
 * @return true if field should be stripped for runtime
 */
static inline bool nmo_field_is_editor_only(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_EDITOR_ONLY);
}

/**
 * @brief Check if field is deprecated
 * 
 * @param field Field descriptor
 * @return true if field is marked deprecated
 */
static inline bool nmo_field_is_deprecated(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_DEPRECATED);
}

/**
 * @brief Check if field is a reference
 * 
 * @param field Field descriptor
 * @return true if field references another object
 */
static inline bool nmo_field_is_reference(const nmo_type_field_t *field) {
    return field && (field->flags & NMO_FIELD_REFERENCE);
}

/**
 * @brief Get semantic annotation name (for display)
 * 
 * @param semantic Semantic enum value
 * @return String representation (e.g., "position", "color")
 */
const char* nmo_field_semantic_name(nmo_field_semantic_t semantic);

/**
 * @brief Get unit annotation name (for display)
 * 
 * @param units Unit enum value
 * @return String representation (e.g., "degrees", "meters")
 */
const char* nmo_field_units_name(nmo_field_units_t units);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_TYPE_SYSTEM_H */
