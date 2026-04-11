/**
 * @file nmo_bb_registry.h
 * @brief Building block prototype registry
 *
 * Maps BB prototype GUIDs to their full metadata: name, description,
 * category, I/O ports, parameter signatures, flags, etc.
 *
 * Populated from:
 * - Compiled-in builtin table (from gen_bb_registry.py / virtools_data.json)
 * - Runtime registration via add() API
 *
 * Follows the same ownership/lifetime patterns as nmo_type_registry_t
 * and nmo_operation_registry_t.
 */

#ifndef NMO_BB_REGISTRY_H
#define NMO_BB_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;

/* ============================================================================
 * BB Parameter Descriptor
 * ============================================================================ */

/** Describes one parameter slot in a BB prototype. */
typedef struct nmo_bb_param_desc {
    const char *name;           /**< Parameter name (e.g. "Position") */
    nmo_guid_t type_guid;       /**< Parameter type GUID (e.g. CKPGUID_VECTOR) */
} nmo_bb_param_desc_t;

/* ============================================================================
 * BB Prototype Info
 * ============================================================================ */

/**
 * @brief Complete building block prototype metadata.
 *
 * All string and array pointers are registry-owned (borrowed by callers).
 */
typedef struct nmo_bb_proto {
    nmo_guid_t guid;            /**< Prototype GUID (primary key) */
    const char *name;           /**< Prototype name (e.g. "Set Position") */
    const char *description;    /**< Human-readable description */
    const char *category;       /**< Category path (e.g. "3D Transformations/Basic") */
    const char *dll;            /**< Source DLL name (e.g. "3DTransfo.dll") */
    uint32_t version;           /**< Prototype version */
    int32_t compatible_class_id; /**< Target CK_CLASSID (e.g. CKCID_3DENTITY) */
    uint32_t behavior_flags;    /**< CK_BEHAVIOR_FLAGS bitmask */

    /* I/O ports */
    const char *const *inputs;  /**< Input IO names array */
    uint32_t input_count;
    const char *const *outputs; /**< Output IO names array */
    uint32_t output_count;

    /* Parameters */
    const nmo_bb_param_desc_t *input_params;
    uint32_t input_param_count;
    const nmo_bb_param_desc_t *output_params;
    uint32_t output_param_count;
    const nmo_bb_param_desc_t *local_params;
    uint32_t local_param_count;
    const nmo_bb_param_desc_t *settings;
    uint32_t setting_count;
} nmo_bb_proto_t;

/* ============================================================================
 * BB Registry
 * ============================================================================ */

typedef struct nmo_bb_registry nmo_bb_registry_t;

/* OWNERSHIP:
 * - registry returned by create(): caller-owned, destroy with nmo_bb_registry_destroy()
 * - nmo_bb_proto_t returned by find(): registry-owned, do not free
 * - inputs to add(): deep-copied into registry arena
 * - thread: caller-synchronized
 */

/**
 * @brief Create a BB registry.
 *
 * Initializes with compiled-in builtin entries. Additional entries can
 * be added via nmo_bb_registry_add().
 *
 * @param arena  Arena for allocations (must not be NULL)
 * @return Registry, or NULL on failure
 */
NMO_API nmo_bb_registry_t *nmo_bb_registry_create(nmo_arena_t *arena);

/**
 * @brief Destroy a BB registry.
 */
NMO_API void nmo_bb_registry_destroy(nmo_bb_registry_t *registry);

/**
 * @brief Look up a BB prototype by GUID.
 *
 * Searches dynamic entries first, then builtin table.
 *
 * @param registry  Registry
 * @param guid      BB prototype GUID
 * @return Prototype info (registry-owned), or NULL if not found
 */
NMO_API const nmo_bb_proto_t *nmo_bb_registry_find(
    const nmo_bb_registry_t *registry, nmo_guid_t guid);

/**
 * @brief Convenience: get BB name by GUID, or NULL.
 */
NMO_API const char *nmo_bb_registry_get_name(
    const nmo_bb_registry_t *registry, nmo_guid_t guid);

/**
 * @brief Register a BB prototype. Deep-copies all data.
 *
 * If a prototype with the same GUID already exists, it is replaced.
 *
 * @param registry  Registry
 * @param proto     Prototype info to copy
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_bb_registry_add(
    nmo_bb_registry_t *registry, const nmo_bb_proto_t *proto);

/**
 * @brief Remove a BB prototype by GUID.
 * @return true if found and removed
 */
NMO_API bool nmo_bb_registry_remove(
    nmo_bb_registry_t *registry, nmo_guid_t guid);

/**
 * @brief Get total prototype count (builtin + dynamic).
 */
NMO_API size_t nmo_bb_registry_count(const nmo_bb_registry_t *registry);

/**
 * @brief Get builtin (compiled-in) prototype count.
 */
NMO_API size_t nmo_bb_registry_builtin_count(const nmo_bb_registry_t *registry);

/* ============================================================================
 * Static (no-instance) builtin lookups
 *
 * Search only the compiled-in table. No registry instance needed.
 * Useful in low-level code or when context is unavailable.
 * ============================================================================ */

/**
 * @brief Look up a BB name from the compiled-in table only.
 * @return Name (static string), or NULL if not in builtin table
 */
NMO_API const char *nmo_bb_builtin_get_name(nmo_guid_t guid);

/**
 * @brief Get number of compiled-in BB entries.
 */
NMO_API size_t nmo_bb_builtin_count(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BB_REGISTRY_H */
