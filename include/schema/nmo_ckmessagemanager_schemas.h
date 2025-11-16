/**
 * @file nmo_ckmessagemanager_schemas.h
 * @brief CKMessageManager schema definitions
 *
 * CKMessageManager manages message type registrations in Virtools.
 * Messages are used for communication between behaviors and objects.
 * 
 * This is a simplified schema that only handles message type names.
 * The actual message routing and delivery is runtime functionality.
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
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_result nmo_result_t;

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
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief Function pointer for CKMessageManager deserialization
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckmessagemanager_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckmessagemanager_state_t *out_state);

/**
 * @brief Function pointer for CKMessageManager serialization
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckmessagemanager_serialize_fn)(
    nmo_chunk_t *chunk,
    const nmo_ckmessagemanager_state_t *state);

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

/**
 * @brief Register CKMessageManager schema types
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckmessagemanager_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get the deserialize function for CKMessageManager
 * 
 * @return Deserialize function pointer
 */
nmo_ckmessagemanager_deserialize_fn nmo_get_ckmessagemanager_deserialize(void);

/**
 * @brief Get the serialize function for CKMessageManager
 * 
 * @return Serialize function pointer
 */
nmo_ckmessagemanager_serialize_fn nmo_get_ckmessagemanager_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKMESSAGEMANAGER_SCHEMAS_H */
