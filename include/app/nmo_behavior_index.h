/**
 * @file nmo_behavior_index.h
 * @brief Behavior ownership index for O(1) reverse lookups
 *
 * Given an IO port ID or parameter ID, resolves which behavior owns it
 * and at what position in the owner's array. Built by walking all behaviors
 * in a session once.
 *
 * Follows the same ownership/lifetime patterns as nmo_type_registry_t.
 */

#ifndef NMO_BEHAVIOR_INDEX_H
#define NMO_BEHAVIOR_INDEX_H

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
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;

/* ============================================================================
 * Port ownership info
 * ============================================================================ */

/** What kind of port/parameter slot this is. */
typedef enum nmo_port_kind {
    NMO_PORT_IO_IN,         /**< inputs[] slot (CKBehaviorIO input) */
    NMO_PORT_IO_OUT,        /**< outputs[] slot (CKBehaviorIO output) */
    NMO_PORT_PARAM_IN,      /**< in_parameters[] slot */
    NMO_PORT_PARAM_OUT,     /**< out_parameters[] slot */
    NMO_PORT_PARAM_LOCAL,   /**< local_parameters[] slot */
    NMO_PORT_PARAM_TARGET,  /**< target_parameter_id */
    NMO_PORT_OPERATION,     /**< operations[] slot */
    NMO_PORT_SUB_BEHAVIOR,  /**< sub_behaviors[] slot */
    NMO_PORT_SUB_LINK,      /**< sub_behavior_links[] slot */
} nmo_port_kind_t;

/** Ownership info for a port/parameter/sub-behavior. */
typedef struct nmo_port_owner {
    nmo_object_id_t owner_id;   /**< Owning CKBehavior ID */
    int32_t index;              /**< Index in owner's array (-1 for target) */
    nmo_port_kind_t kind;       /**< Which array */
} nmo_port_owner_t;

/* ============================================================================
 * Behavior Index
 * ============================================================================ */

typedef struct nmo_behavior_index nmo_behavior_index_t;

/**
 * @brief Create an empty behavior index.
 * @param arena  Arena for allocations (must not be NULL)
 */
NMO_API nmo_behavior_index_t *nmo_behavior_index_create(nmo_arena_t *arena);

/**
 * @brief Destroy a behavior index.
 */
NMO_API void nmo_behavior_index_destroy(nmo_behavior_index_t *index);

/**
 * @brief Build the index by scanning all behaviors in the session.
 *
 * Walks the entire behavior tree (all scripts, recursively) and indexes
 * every IO port, parameter, operation, sub-behavior, and link.
 *
 * @param index    Index to populate
 * @param ctx      Context
 * @param session  Session with loaded file
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_behavior_index_build(
    nmo_behavior_index_t *index,
    nmo_context_t *ctx,
    nmo_session_t *session);

/**
 * @brief Find the owner of an object by ID.
 *
 * Works for IO ports, parameters, operations, sub-behaviors, and links.
 *
 * @param index  Index
 * @param id     Object ID to look up
 * @return Ownership info, or NULL if not indexed
 */
NMO_API const nmo_port_owner_t *nmo_behavior_index_find(
    const nmo_behavior_index_t *index,
    nmo_object_id_t id);

/**
 * @brief Get total number of indexed entries.
 */
NMO_API size_t nmo_behavior_index_count(const nmo_behavior_index_t *index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_INDEX_H */
