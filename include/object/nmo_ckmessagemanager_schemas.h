/**
 * @file nmo_ckmessagemanager_schemas.h
 * @brief CKMessageManager schema definitions
 *
 * CKMessageManager manages message type registrations in Virtools.
 * Messages are used for communication between behaviors and objects.
 * 
 * Based on official Virtools SDK (reference/src/CKMessageManager.cpp:178-250).
 */

#ifndef NMO_CKMESSAGEMANAGER_SCHEMAS_H
#define NMO_CKMESSAGEMANAGER_SCHEMAS_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKMessageManager STATE STRUCTURE
 * ============================================================================= */

/**
 * @brief CKMessageManager state structure
 * 
 * Stores registered message type names.
 * Each message type has a unique ID (index in the array).
 * 
 * Note: Only message types that are actually used in the file are saved.
 * Empty strings indicate unused message type slots.
 * 
 * Reference: reference/src/CKMessageManager.cpp:178-216
 */
typedef struct nmo_ckmessagemanager_state {
    /**
     * @brief Number of registered message types
     * 
     * This is the size of the message_type_names array.
     */
    uint32_t message_type_count;

    /**
     * @brief Message type names
     * 
     * Array of message_type_count strings, allocated from arena.
     * Each string is the name of a registered message type.
     * Empty strings ("") indicate unused slots.
     */
    const char **message_type_names;
} nmo_ckmessagemanager_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_ckmessagemanager_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckmessagemanager_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKMESSAGEMANAGER_SCHEMAS_H */
