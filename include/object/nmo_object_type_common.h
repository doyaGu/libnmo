/**
 * @file nmo_object_type_common.h
 * @brief Common helpers for CKObject-derived type vtables
 */

#ifndef NMO_OBJECT_TYPE_COMMON_H
#define NMO_OBJECT_TYPE_COMMON_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_hash.h"

#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"

#include "format/nmo_chunk.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include <string.h>

#define NMO_OBJECT_TYPE_COMMON_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_TYPE_COMMON_API_TIER NMO_API_TIER_PUBLIC_PROTOCOL

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Default Lifecycle / Operations
 * ============================================================================ */
NMO_API nmo_status_t nmo_object_default_create(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API void nmo_object_default_destroy(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_object_default_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena);

NMO_API void nmo_object_dispose_array_fields(
    void *instance,
    const nmo_type_descriptor_t *type);

/* Shared deep-copy / validate implementations used by object vtables. */
NMO_API nmo_status_t nmo_object_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena);

NMO_API nmo_status_t nmo_object_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_object_default_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/* ============================================================================
 * Serialized State Comparison
 * ============================================================================ */
typedef struct nmo_object_serialize_pass {
    nmo_class_id_t class_id;
    uint32_t data_version;
    uint32_t chunk_options;
    uint32_t serialize_flags;
    uint32_t save_flags;
    uint8_t use_context;
} nmo_object_serialize_pass_t;

NMO_API bool nmo_object_serialized_state_equals(
    const void *a,
    const void *b,
    nmo_object_serialize_fn serializer,
    const nmo_object_serialize_pass_t *passes,
    size_t pass_count,
    size_t arena_block_size);

NMO_API uint32_t nmo_object_serialized_state_hash(
    const void *instance,
    nmo_object_serialize_fn serializer,
    const nmo_object_serialize_pass_t *passes,
    size_t pass_count,
    size_t arena_block_size);

/* ============================================================================
 * Layout-Driven State Ops
 * ============================================================================ */
typedef enum nmo_object_state_member_kind {
    NMO_OBJECT_STATE_MEMBER_VALUE, /**< Trivially copyable bytes */
    NMO_OBJECT_STATE_MEMBER_ARRAY, /**< Owned nmo_array_t of trivially copyable elements */
    NMO_OBJECT_STATE_MEMBER_BYTES, /**< Arena-owned buffer sized by a size_t member */
    NMO_OBJECT_STATE_MEMBER_STRING, /**< Arena-owned NUL-terminated string, compared by content */
    NMO_OBJECT_STATE_MEMBER_CHUNK, /**< Arena-owned nmo_chunk_t *, compared by chunk data */
    NMO_OBJECT_STATE_MEMBER_COUNTED, /**< Arena-owned array counted by an integer member or a count function; elements are trivially copyable, or records when .record is set */
    NMO_OBJECT_STATE_MEMBER_RECORDS, /**< Owned nmo_array_t of records laid out by a nested member list */
    NMO_OBJECT_STATE_MEMBER_RECORD_PTR, /**< Arena-owned pointer to one record, NULL when absent */
    NMO_OBJECT_STATE_MEMBER_CUSTOM /**< Embedded value copied, compared and hashed by functions */
} nmo_object_state_member_kind_t;

/**
 * @brief Functions of a CUSTOM member. The value they work on holds only
 *        arena-owned memory, so a copy needs no teardown.
 */
typedef struct nmo_object_state_custom_ops {
    /** Copy *src over *dst; leave *dst alone on failure. */
    nmo_status_t (*copy)(nmo_arena_t *arena, void *dst, const void *src);
    bool (*equals)(const void *a, const void *b);
    /** Fold the value into a running FNV-1a hash. */
    uint32_t (*hash)(uint32_t hash, const void *value);
} nmo_object_state_custom_ops_t;

typedef struct nmo_object_state_layout nmo_object_state_layout_t;

/** Member flags */
#define NMO_OBJECT_STATE_MEMBER_UNCOMPARED 0x1u /**< Copied, but left out of equals and hash */
#define NMO_OBJECT_STATE_MEMBER_OPTIONAL 0x2u /**< COUNTED, BYTES: a NULL pointer means absent whatever the count says */

typedef struct nmo_object_state_member {
    nmo_object_state_member_kind_t kind;
    size_t offset;
    size_t size;        /**< VALUE: member size; ARRAY, COUNTED: element size */
    size_t size_offset; /**< BYTES, COUNTED: offset of the integer element count */
    size_t count_size;  /**< COUNTED: byte width of the count (4 or 8) */
    uint32_t flags;     /**< NMO_OBJECT_STATE_MEMBER_* flags */
    /** COUNTED: element count computed from the state that holds the member
     *  (the record, for a member of a record), instead of read from a member. */
    size_t (*count_fn)(const void *owner);
    /** COUNTED, RECORDS, RECORD_PTR: layout of one element */
    const nmo_object_state_layout_t *record;
    /** CUSTOM: how to copy, compare and hash the value */
    const nmo_object_state_custom_ops_t *custom;
} nmo_object_state_member_t;

#define NMO_STATE_VALUE(_state_t, _member) \
    {.kind = NMO_OBJECT_STATE_MEMBER_VALUE, \
     .offset = offsetof(_state_t, _member), \
     .size = sizeof(((_state_t *)0)->_member)}
/* Elements [_first, _first + _count) of a fixed array member, as one value */
#define NMO_STATE_ELEMENTS(_state_t, _member, _first, _count) \
    {.kind = NMO_OBJECT_STATE_MEMBER_VALUE, \
     .offset = offsetof(_state_t, _member[_first]), \
     .size = sizeof(((_state_t *)0)->_member[0]) * (_count)}
/* Elements that are copied but are not part of the state equals and hash
 * compare, because the serialized form derives them from another member. */
#define NMO_STATE_ELEMENTS_UNCOMPARED(_state_t, _member, _first, _count) \
    {.kind = NMO_OBJECT_STATE_MEMBER_VALUE, \
     .offset = offsetof(_state_t, _member[_first]), \
     .size = sizeof(((_state_t *)0)->_member[0]) * (_count), \
     .flags = NMO_OBJECT_STATE_MEMBER_UNCOMPARED}
#define NMO_STATE_ARRAY(_state_t, _member, _element_t) \
    {.kind = NMO_OBJECT_STATE_MEMBER_ARRAY, \
     .offset = offsetof(_state_t, _member), .size = sizeof(_element_t)}
#define NMO_STATE_BYTES(_state_t, _member, _size_member) \
    {.kind = NMO_OBJECT_STATE_MEMBER_BYTES, \
     .offset = offsetof(_state_t, _member), \
     .size_offset = offsetof(_state_t, _size_member)}
/* A pointer to _count_member elements. The count is a member of its own and
 * is listed as a value, like the size of a BYTES member. */
#define NMO_STATE_COUNTED(_state_t, _member, _count_member, _element_t) \
    {.kind = NMO_OBJECT_STATE_MEMBER_COUNTED, \
     .offset = offsetof(_state_t, _member), .size = sizeof(_element_t), \
     .size_offset = offsetof(_state_t, _count_member), \
     .count_size = sizeof(((_state_t *)0)->_count_member)}
/* A pointer to as many elements as _count_fn(state) returns. */
#define NMO_STATE_COUNTED_BY(_state_t, _member, _count_fn, _element_t) \
    {.kind = NMO_OBJECT_STATE_MEMBER_COUNTED, \
     .offset = offsetof(_state_t, _member), .size = sizeof(_element_t), \
     .count_fn = (_count_fn)}
/* A pointer to _count_member records, each laid out by _record. */
#define NMO_STATE_COUNTED_RECORDS(_state_t, _member, _count_member, _record) \
    {.kind = NMO_OBJECT_STATE_MEMBER_COUNTED, \
     .offset = offsetof(_state_t, _member), \
     .size_offset = offsetof(_state_t, _count_member), \
     .count_size = sizeof(((_state_t *)0)->_count_member), \
     .record = &(_record)}
/* As NMO_STATE_COUNTED and NMO_STATE_COUNTED_RECORDS, for a pointer that is
 * NULL while its count is not 0 when the lane it belongs to is unused. */
#define NMO_STATE_COUNTED_OPTIONAL(_state_t, _member, _count_member, _element_t) \
    {.kind = NMO_OBJECT_STATE_MEMBER_COUNTED, \
     .offset = offsetof(_state_t, _member), .size = sizeof(_element_t), \
     .size_offset = offsetof(_state_t, _count_member), \
     .count_size = sizeof(((_state_t *)0)->_count_member), \
     .flags = NMO_OBJECT_STATE_MEMBER_OPTIONAL}
#define NMO_STATE_COUNTED_RECORDS_OPTIONAL(_state_t, _member, _count_member, _record) \
    {.kind = NMO_OBJECT_STATE_MEMBER_COUNTED, \
     .offset = offsetof(_state_t, _member), \
     .size_offset = offsetof(_state_t, _count_member), \
     .count_size = sizeof(((_state_t *)0)->_count_member), \
     .flags = NMO_OBJECT_STATE_MEMBER_OPTIONAL, \
     .record = &(_record)}
/* An embedded value handled by the functions in _ops. */
#define NMO_STATE_CUSTOM(_state_t, _member, _ops) \
    {.kind = NMO_OBJECT_STATE_MEMBER_CUSTOM, \
     .offset = offsetof(_state_t, _member), \
     .size = sizeof(((_state_t *)0)->_member), .custom = &(_ops)}
/* A pointer to one record laid out by _record, NULL when there is none. */
#define NMO_STATE_RECORD_PTR(_state_t, _member, _record) \
    {.kind = NMO_OBJECT_STATE_MEMBER_RECORD_PTR, \
     .offset = offsetof(_state_t, _member), .record = &(_record)}
#define NMO_STATE_STRING(_state_t, _member) \
    {.kind = NMO_OBJECT_STATE_MEMBER_STRING, \
     .offset = offsetof(_state_t, _member)}
#define NMO_STATE_CHUNK(_state_t, _member) \
    {.kind = NMO_OBJECT_STATE_MEMBER_CHUNK, \
     .offset = offsetof(_state_t, _member)}
/* An nmo_array_t of records. _record is the nmo_object_state_layout_t of one
 * record: its size and members, with no base. A record holds values, strings,
 * byte buffers, chunks and counted arrays, but no array of its own. */
#define NMO_STATE_RECORDS(_state_t, _member, _record) \
    {.kind = NMO_OBJECT_STATE_MEMBER_RECORDS, \
     .offset = offsetof(_state_t, _member), .record = &(_record)}

/**
 * @brief State layout of a class whose own members need no custom logic.
 *
 * The base state is embedded at offset 0 and handled by its vtable; the
 * members after it are handled by the generic ops below.
 */
struct nmo_object_state_layout {
    size_t size;
    const nmo_type_vtable_t *base_vtable; /**< NULL for a layout of a plain record */
    size_t base_size;        /**< Type size handed to the base copy; 0 hands no type */
    const nmo_object_state_member_t *members;
    size_t member_count;
    void (*set_defaults)(void *state);
    nmo_status_t (*validate)(const void *instance,
                             const nmo_type_descriptor_t *type,
                             void *context);
};

NMO_API nmo_status_t nmo_object_layout_create(
    const nmo_object_state_layout_t *layout,
    void *instance,
    void *context);

NMO_API void nmo_object_layout_destroy(
    const nmo_object_state_layout_t *layout,
    void *instance,
    void *context);

NMO_API nmo_status_t nmo_object_layout_copy(
    const nmo_object_state_layout_t *layout,
    const void *src,
    void *dst,
    nmo_arena_t *arena);

NMO_API bool nmo_object_layout_equals(
    const nmo_object_state_layout_t *layout,
    const void *a,
    const void *b);

NMO_API uint32_t nmo_object_layout_hash(
    const nmo_object_state_layout_t *layout,
    const void *instance);

/** @brief Fold the members of a layout into a running FNV-1a hash (the base is not hashed). */
NMO_API uint32_t nmo_object_layout_hash_from(
    const nmo_object_state_layout_t *layout,
    uint32_t hash,
    const void *instance);

/* Vtable hooks over a layout: nmo_<prefix>_create/_destroy, _copy, _equals/_hash */
#define NMO_DEFINE_OBJECT_LAYOUT_LIFECYCLE(_prefix, _layout) \
    static nmo_status_t nmo_##_prefix##_create( \
        void *instance, const nmo_type_descriptor_t *type, void *context) \
    { \
        (void)type; \
        return nmo_object_layout_create(&(_layout), instance, context); \
    } \
    static void nmo_##_prefix##_destroy( \
        void *instance, const nmo_type_descriptor_t *type, void *context) \
    { \
        (void)type; \
        nmo_object_layout_destroy(&(_layout), instance, context); \
    }

#define NMO_DEFINE_OBJECT_LAYOUT_COPY(_prefix, _layout) \
    static nmo_status_t nmo_##_prefix##_copy( \
        const void *src, void *dst, const nmo_type_descriptor_t *type, \
        nmo_arena_t *arena) \
    { \
        (void)type; \
        return nmo_object_layout_copy(&(_layout), src, dst, arena); \
    }

#define NMO_DEFINE_OBJECT_LAYOUT_COMPARE(_prefix, _layout) \
    static bool nmo_##_prefix##_equals(const void *a, const void *b) \
    { \
        return nmo_object_layout_equals(&(_layout), a, b); \
    } \
    static uint32_t nmo_##_prefix##_hash(const void *instance) \
    { \
        return nmo_object_layout_hash(&(_layout), instance); \
    }

#define NMO_DEFINE_OBJECT_LAYOUT_OPS(_prefix, _layout) \
    NMO_DEFINE_OBJECT_LAYOUT_LIFECYCLE(_prefix, _layout) \
    NMO_DEFINE_OBJECT_LAYOUT_COPY(_prefix, _layout) \
    NMO_DEFINE_OBJECT_LAYOUT_COMPARE(_prefix, _layout)

/* ============================================================================
 * Shared Hooks and Wrappers
 * ============================================================================ */

/**
 * @brief Serialize through a staging chunk so a failed write leaves out_chunk untouched.
 *
 * Checks the arguments, runs @p validate (may be NULL), lets @p serialize write
 * into a fresh chunk that carries out_chunk's identity, and copies the staging
 * chunk over out_chunk only when everything succeeded.
 */
NMO_API nmo_status_t nmo_object_serialize_staged(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context,
    nmo_type_validate_fn validate,
    nmo_type_serialize_fn serialize);

/** @brief pre_delete hook that only rejects a NULL instance. */
NMO_API nmo_status_t nmo_object_pre_delete_checked(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/** @brief post_delete hook that does nothing. */
NMO_API void nmo_object_post_delete_noop(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/** @brief prepare_dependencies hook that only rejects a NULL instance. */
NMO_API nmo_status_t nmo_object_prepare_dependencies_checked(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/** @brief prepare_dependencies hook that runs nmo_object_default_validate(). */
NMO_API nmo_status_t nmo_object_prepare_dependencies_default(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/*
 * Define the exported <prefix>_serialize / <prefix>_prepare_dependencies
 * entry points of a class. The class provides <prefix>_serialize_internal and,
 * where used, <prefix>_validate.
 */
#define NMO_DEFINE_OBJECT_STAGED_SERIALIZE(_prefix) \
    nmo_status_t _prefix##_serialize( \
        const void *instance, \
        nmo_chunk_t *out_chunk, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        return nmo_object_serialize_staged( \
            instance, out_chunk, type, context, NULL, _prefix##_serialize_internal); \
    }

#define NMO_DEFINE_OBJECT_STAGED_SERIALIZE_VALIDATED(_prefix) \
    nmo_status_t _prefix##_serialize( \
        const void *instance, \
        nmo_chunk_t *out_chunk, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        return nmo_object_serialize_staged( \
            instance, out_chunk, type, context, _prefix##_validate, _prefix##_serialize_internal); \
    }

/*
 * Same entry point for a class whose <prefix>_serialize_internal takes the typed
 * state and no type descriptor:
 *   internal(const _state_t *state, nmo_chunk_t *chunk, void *context)
 * _validate is a type_validate function or NULL.
 */
#define NMO_DEFINE_OBJECT_STAGED_SERIALIZE_STATE(_prefix, _state_t, _validate) \
    static nmo_status_t _prefix##_serialize_staged_body( \
        const void *instance, \
        nmo_chunk_t *out_chunk, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        (void)type; \
        return _prefix##_serialize_internal( \
            (const _state_t *)instance, out_chunk, context); \
    } \
    nmo_status_t _prefix##_serialize( \
        const void *instance, \
        nmo_chunk_t *out_chunk, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        return nmo_object_serialize_staged( \
            instance, out_chunk, type, context, _validate, \
            _prefix##_serialize_staged_body); \
    }

/*
 * Static <prefix>_validate of a class with nothing to check of its own: it only
 * validates the base state embedded as _base_member with the base class vtable.
 */
#define NMO_DEFINE_OBJECT_VALIDATE_BASE(_prefix, _state_t, _base_member, _base_vtable) \
    static nmo_status_t _prefix##_validate( \
        const void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        (void)type; \
        if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT; \
        const _state_t *state = (const _state_t *)instance; \
        return _base_vtable.validate(&state->_base_member, NULL, context); \
    }

#define NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(_prefix) \
    nmo_status_t _prefix##_prepare_dependencies( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        return _prefix##_validate(instance, type, context); \
    }

#define NMO_DEFINE_OBJECT_PREPARE_CHECKED(_prefix) \
    nmo_status_t _prefix##_prepare_dependencies( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        return nmo_object_prepare_dependencies_checked(instance, type, context); \
    }

#define NMO_DEFINE_OBJECT_PREPARE_DEFAULT(_prefix) \
    nmo_status_t _prefix##_prepare_dependencies( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        return nmo_object_prepare_dependencies_default(instance, type, context); \
    }


/* ============================================================================
 * Generic Deep-Copy Helpers
 * ============================================================================ */
NMO_API nmo_status_t nmo_object_copy_bytes(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t size);

NMO_API nmo_status_t nmo_object_copy_array(
    nmo_arena_t *arena,
    void **dst,
    const void *src,
    size_t elem_size,
    uint32_t count);

NMO_API nmo_status_t nmo_object_copy_string(
    nmo_arena_t *arena,
    char **dst,
    const char *src);

NMO_API nmo_status_t nmo_object_copy_string_array(
    nmo_arena_t *arena,
    char ***dst,
    char *const *src,
    uint32_t count);

NMO_API nmo_status_t nmo_object_copy_chunk(
    nmo_arena_t *arena,
    nmo_chunk_t **dst,
    nmo_chunk_t *src);

NMO_API nmo_status_t nmo_object_copy_chunk_array(
    nmo_arena_t *arena,
    nmo_chunk_t ***dst,
    nmo_chunk_t *const *src,
    uint32_t count);

/* ============================================================================
 * Chunk Array Helpers (nmo_array_t)
 * ============================================================================ */
NMO_API void nmo_object_array_set_chunk_lifecycle(nmo_array_t *array);

NMO_API void nmo_object_array_set_string_lifecycle(nmo_array_t *array);

NMO_API nmo_status_t nmo_object_clone_chunk_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src);

NMO_API nmo_status_t nmo_object_clone_string_array(
    nmo_arena_t *arena,
    nmo_array_t *dst,
    const nmo_array_t *src);

/* ============================================================================
 * Validation Helpers
 * ============================================================================ */
#define NMO_VALIDATE_COUNT(ptr, count, label) \
    do { \
        if ((count) > 0 && !(ptr)) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
                                    "Missing %s array for count %zu", (label), \
                                    (size_t)(count)); \
        } \
    } while (0)

#define NMO_VALIDATE_BYTES(ptr, size, label) \
    do { \
        if ((size) > 0 && !(ptr)) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
                                    "Missing %s buffer for size %zu", (label), (size_t)(size)); \
        } \
    } while (0)

/* ============================================================================
 * File Mode
 * ============================================================================ */

/**
 * @brief Whether a chunk is being written in file mode
 *
 * True when the chunk carries the FILE option or the serialize context asks for
 * file mode. A file chunk holds no id table and uses the layouts a file stores;
 * the other mode is the CKStateChunk of a save-flag subset.
 */
static inline bool nmo_object_serialize_is_file(const nmo_chunk_t *chunk, void *context)
{
    const nmo_serialize_context_t *ctx = nmo_serialize_context_try(context);
    return (chunk != NULL && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
           (ctx != NULL && (ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
}

/** @brief Whether a chunk is being read in file mode (see nmo_object_serialize_is_file()) */
static inline bool nmo_object_deserialize_is_file(const nmo_chunk_t *chunk, void *context)
{
    const nmo_deserialize_context_t *ctx = nmo_deserialize_context_get(context);
    return (chunk != NULL && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
           (ctx != NULL && (ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0);
}

/* ============================================================================
 * Per-Type Lifecycle Helpers
 * ============================================================================ */
#define NMO_DEFINE_OBJECT_LIFECYCLE(_prefix, _state_t, _init_block, _destroy_block) \
    static nmo_status_t nmo_##_prefix##_create( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        (void)type; \
        (void)context; \
        if (instance == NULL) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
                             "Invalid arguments to nmo_" #_prefix "_create"); \
        } \
        _state_t *state = (_state_t *)instance; \
        memset(state, 0, sizeof(*state)); \
        _init_block; \
        NMO_RETURN_OK(); \
    } \
    static void nmo_##_prefix##_destroy( \
        void *instance, \
        const nmo_type_descriptor_t *type, \
        void *context) \
    { \
        (void)type; \
        (void)context; \
        if (instance == NULL) { \
            return; \
        } \
        _state_t *state = (_state_t *)instance; \
        _destroy_block; \
        nmo_object_dispose_array_fields(state, type); \
        memset(state, 0, sizeof(*state)); \
    }

#define NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(_prefix, _state_t) \
    NMO_DEFINE_OBJECT_LIFECYCLE(_prefix, _state_t, ((void)0), ((void)0))

/* ============================================================================
 * Per-Type State Ops (equals/hash/copy/validate)
 * ============================================================================ */
#define NMO_DEFINE_OBJECT_STATE_OPS(_name, _state_t) \
static bool nmo_##_name##_equals(const void *a, const void *b) { \
    if (a == b) { \
        return true; \
    } \
    if (!a || !b) { \
        return false; \
    } \
    return memcmp(a, b, sizeof(_state_t)) == 0; \
} \
static uint32_t nmo_##_name##_hash(const void *instance) { \
    if (!instance) { \
        return 0; \
    } \
    return (uint32_t)nmo_hash_fnv1a(instance, sizeof(_state_t)); \
} \
static nmo_status_t nmo_##_name##_copy(const void *src, void *dst, \
                                 const nmo_type_descriptor_t *type, nmo_arena_t *arena) { \
    return nmo_object_default_copy(src, dst, type, arena); \
} \
static nmo_status_t nmo_##_name##_validate(const void *instance, \
                                     const nmo_type_descriptor_t *type, void *context) { \
    return nmo_object_default_validate(instance, type, context); \
}

/* Per-type equals/hash macro without default copy/validate (for custom ops) */
#define NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(_name, _state_t) \
static bool nmo_##_name##_equals(const void *a, const void *b) { \
    if (a == b) { \
        return true; \
    } \
    if (!a || !b) { \
        return false; \
    } \
    return memcmp(a, b, sizeof(_state_t)) == 0; \
} \
static uint32_t nmo_##_name##_hash(const void *instance) { \
    if (!instance) { \
        return 0; \
    } \
    return (uint32_t)nmo_hash_fnv1a(instance, sizeof(_state_t)); \
}

/* ============================================================================
 * Vtable Helpers
 * ============================================================================ */

#define NMO_OBJECT_VTABLE(_create, _destroy, _serialize, _deserialize, _copy, _validate, _equals, _hash) \
    .create = (_create), \
    .destroy = (_destroy), \
    .copy = (_copy), \
    .serialize = (_serialize), \
    .deserialize = (_deserialize), \
    .validate = (_validate), \
    .equals = (_equals), \
    .hash = (_hash), \
    .to_string = NULL, \
    .from_string = NULL, \
    .enumerate_refs = NULL

/**
 * @brief Extended vtable macro with enumerate_refs support
 * 
 * Use this when a schema provides its own reference enumerator.
 */
#define NMO_OBJECT_VTABLE_EX(_create, _destroy, _serialize, _deserialize, _copy, _validate, _equals, _hash, _enumerate_refs) \
    .create = (_create), \
    .destroy = (_destroy), \
    .copy = (_copy), \
    .serialize = (_serialize), \
    .deserialize = (_deserialize), \
    .validate = (_validate), \
    .equals = (_equals), \
    .hash = (_hash), \
    .to_string = NULL, \
    .from_string = NULL, \
    .enumerate_refs = (_enumerate_refs)

/* ============================================================================
 * Registration Helpers
 * ============================================================================ */

/* Registration helper for per-schema files */
#define NMO_DEFINE_OBJECT_REGISTRATION(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable) \
NMO_API nmo_status_t _func(nmo_type_registry_t *registry) { \
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
               "NULL type registry"); \
    nmo_type_descriptor_t type_desc = { \
        .guid = (_guid), \
        .name = (_name), \
        .size = (uint32_t)sizeof(_state_t), \
        .alignment = (uint32_t)alignof(_state_t), \
        .class_id = (_class_id), \
        .base_type = (_base_guid), \
        .category = NMO_TYPE_CATEGORY_OBJECT_REF, \
        .flags = NMO_TYPE_FLAG_SERIALIZABLE, \
        .id = NMO_TYPE_ID_INVALID, \
        .description = NULL, \
        .fields = NULL, \
        .field_count = 0, \
        .vtable = (_vtable) \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Registration helper with reflection fields */
#define NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields) \
NMO_API nmo_status_t _func(nmo_type_registry_t *registry) { \
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
               "NULL type registry"); \
    nmo_type_descriptor_t type_desc = { \
        .guid = (_guid), \
        .name = (_name), \
        .size = (uint32_t)sizeof(_state_t), \
        .alignment = (uint32_t)alignof(_state_t), \
        .class_id = (_class_id), \
        .base_type = (_base_guid), \
        .category = NMO_TYPE_CATEGORY_OBJECT_REF, \
        .flags = NMO_TYPE_FLAG_SERIALIZABLE, \
        .id = NMO_TYPE_ID_INVALID, \
        .description = NULL, \
        .fields = (_fields), \
        .field_count = sizeof(_fields) / sizeof((_fields)[0]), \
        .vtable = (_vtable) \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Registration helper with reflection fields and explicit field count */
#define NMO_DEFINE_OBJECT_REGISTRATION_FIELDS_COUNT(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields, _field_count) \
NMO_API nmo_status_t _func(nmo_type_registry_t *registry) { \
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, \
               "NULL type registry"); \
    nmo_type_descriptor_t type_desc = { \
        .guid = (_guid), \
        .name = (_name), \
        .size = (uint32_t)sizeof(_state_t), \
        .alignment = (uint32_t)alignof(_state_t), \
        .class_id = (_class_id), \
        .base_type = (_base_guid), \
        .category = NMO_TYPE_CATEGORY_OBJECT_REF, \
        .flags = NMO_TYPE_FLAG_SERIALIZABLE, \
        .id = NMO_TYPE_ID_INVALID, \
        .description = NULL, \
        .fields = (_fields), \
        .field_count = (_field_count), \
        .vtable = (_vtable) \
    }; \
    return nmo_type_registry_register(registry, &type_desc); \
}

/* Runtime registration helper aliases used by migrated schema files. */
#define NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable) \
    NMO_DEFINE_OBJECT_REGISTRATION(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable)

#define NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields) \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(_func, _guid, _name, _class_id, _base_guid, _state_t, _vtable, _fields)

/* ============================================================================
 * Schema Declarations
 * ============================================================================ */

/* Declarations for schema headers */
#define NMO_DECLARE_OBJECT_SCHEMA(_vtable, _register_fn) \
    NMO_API extern nmo_type_vtable_t _vtable; \
    NMO_API nmo_status_t _register_fn(nmo_type_registry_t *registry);

/* ============================================================================
 * Schema Definition Macros
 * ============================================================================ */

/* Definition helper for schema source files */
#define NMO_DEFINE_OBJECT_SCHEMA(_prefix, _state_t, _serialize, _deserialize, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

/* Definition helper with reflection fields */
#define NMO_DEFINE_OBJECT_SCHEMA_FIELDS(_prefix, _state_t, _serialize, _deserialize, _fields, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                          _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields)

/* Definition helper with reflection fields and explicit field count */
#define NMO_DEFINE_OBJECT_SCHEMA_FIELDS_COUNT(_prefix, _state_t, _serialize, _deserialize, _fields, _field_count, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS_COUNT(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                                 _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields, _field_count)

/* Schema definition with enumerate_refs for reflection support */
#define NMO_DEFINE_OBJECT_SCHEMA_REFS(_prefix, _state_t, _serialize, _deserialize, _enumerate_refs, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE_EX(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash, _enumerate_refs) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

/* Schema definition with both enumerate_refs and reflection fields */
#define NMO_DEFINE_OBJECT_SCHEMA_REFS_FIELDS(_prefix, _state_t, _serialize, _deserialize, _enumerate_refs, _fields, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE_EX(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash, _enumerate_refs) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                          _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields)

/* ============================================================================
 * Custom Schema Macros (expect _prefix##_copy/_prefix##_validate to be defined)
 * ============================================================================ */

/* Custom schema macros (expect _prefix##_copy/_prefix##_validate to be defined by caller) */
#define NMO_DEFINE_OBJECT_SCHEMA_CUSTOM(_prefix, _state_t, _serialize, _deserialize, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                   _base_guid, _state_t, &nmo_##_prefix##_vtable)

#define NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(_prefix, _state_t, _serialize, _deserialize, _fields, _guid, _name, _class_id, _base_guid) \
    NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(_prefix, _state_t) \
    nmo_type_vtable_t nmo_##_prefix##_vtable = { \
        NMO_OBJECT_VTABLE(nmo_##_prefix##_create, nmo_##_prefix##_destroy, _serialize, _deserialize, \
                          nmo_##_prefix##_copy, nmo_##_prefix##_validate, nmo_##_prefix##_equals, nmo_##_prefix##_hash) \
    }; \
    NMO_DEFINE_OBJECT_REGISTRATION_FIELDS(nmo_register_##_prefix##_type, _guid, _name, _class_id, \
                                          _base_guid, _state_t, &nmo_##_prefix##_vtable, _fields)

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_TYPE_COMMON_H */
