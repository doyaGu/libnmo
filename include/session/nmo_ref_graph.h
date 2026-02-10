/**
 * @file nmo_ref_graph.h
 * @brief Reference graph enumeration for Virtools files
 *
 * Phase 4: Provides unified reference edge enumeration across all object types.
 * Used by `nmo object refs` and `nmo validate --rules references`.
 * 
 * ARCHITECTURE NOTE:
 * Reference semantics (ref_kind values) are defined HERE in the Session layer,
 * NOT in the Type layer. The Type layer provides generic enumeration mechanism
 * with opaque uint32_t ref_kind. This layer defines the concrete semantics.
 */

#ifndef NMO_REF_GRAPH_H
#define NMO_REF_GRAPH_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_ref_graph nmo_ref_graph_t;

/**
 * @brief Reference kind enumeration (Session layer semantic definition)
 * 
 * Defines the semantic categories of object references in Virtools files.
 * These values are passed as opaque uint32_t through the Type layer's
 * generic enumeration API.
 */
typedef enum nmo_ref_kind {
    NMO_REF_KIND_UNKNOWN = 0,        /**< Unknown or unclassified */
    NMO_REF_KIND_HIERARCHY,          /**< Parent-child hierarchy */
    NMO_REF_KIND_MESH,               /**< Entity to mesh */
    NMO_REF_KIND_MATERIAL,           /**< Mesh to material */
    NMO_REF_KIND_TEXTURE,            /**< Material to texture */
    NMO_REF_KIND_OWNER,              /**< Object ownership */
    NMO_REF_KIND_BEHAVIOR_LINK,      /**< Behavior graph edge */
    NMO_REF_KIND_PARAMETER,          /**< Parameter reference */
    NMO_REF_KIND_TARGET,             /**< Target entity */
    NMO_REF_KIND_GROUP_MEMBER,       /**< Group membership */
    NMO_REF_KIND_SCENE,              /**< Scene membership */
    NMO_REF_KIND_ANIMATION,          /**< Animation reference */
    NMO_REF_KIND_PLACE,              /**< Place reference */
    NMO_REF_KIND_SKIN_BONE,          /**< Skinning bone */
    NMO_REF_KIND_DATA_ARRAY,         /**< Data array element */
    NMO_REF_KIND_SCRIPT,             /**< Script reference */
    NMO_REF_KIND_MAX
} nmo_ref_kind_t;

/* Legacy short names for compatibility */
#define NMO_REF_UNKNOWN        NMO_REF_KIND_UNKNOWN
#define NMO_REF_HIERARCHY      NMO_REF_KIND_HIERARCHY
#define NMO_REF_MESH           NMO_REF_KIND_MESH
#define NMO_REF_MATERIAL       NMO_REF_KIND_MATERIAL
#define NMO_REF_TEXTURE        NMO_REF_KIND_TEXTURE
#define NMO_REF_OWNER          NMO_REF_KIND_OWNER
#define NMO_REF_BEHAVIOR_LINK  NMO_REF_KIND_BEHAVIOR_LINK
#define NMO_REF_PARAMETER      NMO_REF_KIND_PARAMETER
#define NMO_REF_TARGET         NMO_REF_KIND_TARGET
#define NMO_REF_GROUP_MEMBER   NMO_REF_KIND_GROUP_MEMBER
#define NMO_REF_SCENE          NMO_REF_KIND_SCENE
#define NMO_REF_ANIMATION      NMO_REF_KIND_ANIMATION
#define NMO_REF_PLACE          NMO_REF_KIND_PLACE
#define NMO_REF_SKIN_BONE      NMO_REF_KIND_SKIN_BONE
#define NMO_REF_DATA_ARRAY     NMO_REF_KIND_DATA_ARRAY
#define NMO_REF_SCRIPT         NMO_REF_KIND_SCRIPT
#define NMO_REF_MAX            NMO_REF_KIND_MAX

/**
 * @brief Reference direction
 */
typedef enum nmo_ref_direction {
    NMO_REF_DIR_OUTGOING,        /**< Reference from this object to another */
    NMO_REF_DIR_INCOMING         /**< Reference to this object from another */
} nmo_ref_direction_t;

/**
 * @brief Reference edge descriptor
 *
 * Represents a single reference between two objects.
 */
typedef struct nmo_ref_edge {
    nmo_object_id_t from;        /**< Source object ID */
    nmo_object_id_t to;          /**< Target object ID */
    nmo_ref_kind_t kind;         /**< Reference kind */
    const char *field_path;      /**< Field path (e.g., "parent", "meshes[0]") */
    uint32_t index;              /**< Index for array references (0 otherwise) */
} nmo_ref_edge_t;

/**
 * @brief Reference graph statistics
 */
typedef struct nmo_ref_graph_stats {
    size_t total_edges;          /**< Total reference edges */
    size_t edge_counts[NMO_REF_MAX]; /**< Count by reference kind */
    size_t broken_refs;          /**< References to non-existent objects */
    size_t self_refs;            /**< Self-references */
} nmo_ref_graph_stats_t;

/**
 * @brief Create reference graph from session
 *
 * Enumerates all reference edges across all objects in the session.
 *
 * @param session Session with loaded objects (required)
 * @param arena Arena for edge allocations (required)
 * @return Reference graph or NULL on failure
 * @note Returned graph is caller-owned; arena owns edge storage.
 */
NMO_API nmo_ref_graph_t *nmo_ref_graph_create(
    nmo_session_t *session,
    nmo_arena_t *arena
);

/**
 * @brief Destroy reference graph
 *
 * @param graph Graph to destroy
 */
NMO_API void nmo_ref_graph_destroy(nmo_ref_graph_t *graph);

/**
 * @brief Get all reference edges
 *
 * @param graph Graph instance (required)
 * @param edges Output pointer to edge array
 * @param count Output edge count
 * @return NMO_OK on success
 * @note Returned array is graph-owned; valid until graph destruction.
 */
NMO_API nmo_status_t nmo_ref_graph_get_edges(
    nmo_ref_graph_t *graph,
    nmo_ref_edge_t **edges,
    size_t *count
);

/**
 * @brief Get edges for specific object
 *
 * @param graph Graph instance (required)
 * @param object_id Object ID to query
 * @param direction Incoming or outgoing references
 * @param edges Output pointer to edge array
 * @param count Output edge count
 * @return NMO_OK on success
 * @note Returned array is graph-owned; valid until graph destruction.
 */
NMO_API nmo_status_t nmo_ref_graph_get_object_edges(
    nmo_ref_graph_t *graph,
    nmo_object_id_t object_id,
    nmo_ref_direction_t direction,
    nmo_ref_edge_t **edges,
    size_t *count
);

/**
 * @brief Get graph statistics
 *
 * @param graph Graph instance (required)
 * @param stats Output statistics
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_ref_graph_get_stats(
    nmo_ref_graph_t *graph,
    nmo_ref_graph_stats_t *stats
);

/**
 * @brief Validate reference integrity
 *
 * Checks that all referenced objects exist in the session.
 *
 * @param graph Graph instance (required)
 * @param broken_edges Output array of broken edges (can be NULL)
 * @param broken_count Output count of broken edges (can be NULL)
 * @return NMO_OK if all references valid, NMO_ERR_VALIDATION_FAILED otherwise
 * @note broken_edges (if requested) is graph-owned; valid until graph destruction.
 */
NMO_API nmo_status_t nmo_ref_graph_validate(
    nmo_ref_graph_t *graph,
    nmo_ref_edge_t **broken_edges,
    size_t *broken_count
);

/**
 * @brief Get string name for reference kind
 *
 * @param kind Reference kind
 * @return Human-readable name
 */
NMO_API const char *nmo_ref_kind_name(nmo_ref_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REF_GRAPH_H */
