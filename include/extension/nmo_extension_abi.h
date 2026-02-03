/**
 * @file nmo_extension_abi.h
 * @brief Extension ABI contract definitions
 *
 * Defines the ABI contract between the host (libnmo) and extensions.
 * This header is shared by both host and extension code.
 *
 * ABI Policy (development stage):
 * - Host only accepts extensions compiled for the exact same NMO_EXTENSION_ABI_VERSION.
 * - Any incompatible change to any ABI-visible struct requires incrementing the version.
 */

#ifndef NMO_EXTENSION_ABI_H
#define NMO_EXTENSION_ABI_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* Forward declare nmo_chunk to avoid circular includes */
typedef struct nmo_chunk nmo_chunk_t;

/* ============================================================================
 * ABI Version Constants
 * ============================================================================ */

/**
 * @brief Current ABI version (exact match required)
 */
#define NMO_EXTENSION_ABI_VERSION 1u

/**
 * @brief Default symbol name for extension query function
 */
#define NMO_EXTENSION_QUERY_SYMBOL "nmo_extension_query"

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct nmo_extension_plugin nmo_extension_plugin_t;
typedef struct nmo_extension_host nmo_extension_host_t;
typedef struct nmo_extension_manager_desc nmo_extension_manager_desc_t;
typedef struct nmo_extension_type_desc nmo_extension_type_desc_t;

/* ============================================================================
 * Plugin Descriptor
 *
 * Describes an extension plugin exported by a shared library or registered
 * statically. The host deep-copies all strings and metadata.
 * ============================================================================ */

/**
 * @brief Extension plugin descriptor
 *
 * Extensions export an array of these descriptors via the query function.
 * The host deep-copies all strings into host-owned storage.
 */
typedef struct nmo_extension_plugin {
    /** ABI version (must equal NMO_EXTENSION_ABI_VERSION) */
    uint32_t abi_version;

    /** Size of this struct in bytes (for forward compatibility) */
    uint32_t struct_size;

    /** Unique identifier for this plugin */
    nmo_guid_t guid;

    /** Plugin version (semantic: major.minor.patch packed) */
    uint32_t version;

    /** Category for filtering/organization */
    nmo_plugin_category_t category;

    /** Human-readable plugin name (host will deep-copy) */
    const char *name;

    /**
     * @brief Plugin initialization callback
     *
     * Called after the host has deep-copied metadata and created a plugin instance.
     * Extensions register their contributions (types, managers) via the host API.
     *
     * @param host Host API table (valid only during this call)
     * @param host_user Opaque host context (pass to host API calls)
     * @return NMO_OK on success, error code on failure
     */
    nmo_status_t (*init)(const nmo_extension_host_t *host, void *host_user);

    /**
     * @brief Plugin shutdown callback (optional)
     *
     * Called before unregister/unload while the DLL is still loaded.
     * Use for cleanup of extension-owned resources.
     *
     * @param host Host API table
     * @param host_user Opaque host context
     */
    void (*shutdown)(const nmo_extension_host_t *host, void *host_user);
} nmo_extension_plugin_t;

/**
 * @brief Minimum struct_size the host requires to safely read all fields
 */
#define NMO_EXTENSION_PLUGIN_REQUIRED_SIZE \
    (offsetof(nmo_extension_plugin_t, shutdown) + sizeof(((nmo_extension_plugin_t *)0)->shutdown))

/* ============================================================================
 * Host API Table
 *
 * Function table provided by the host to extensions during init().
 * ============================================================================ */

/**
 * @brief Manager contribution descriptor
 *
 * Describes a manager to be registered by an extension.
 */
typedef struct nmo_extension_manager_desc {
    /** Manager ID (must be unique within the context) */
    uint32_t manager_id;

    /** Manager GUID for lookup */
    nmo_guid_t guid;

    /** Human-readable name (host will deep-copy) */
    const char *name;

    /** Category for filtering */
    nmo_plugin_category_t category;

    /* Session lifecycle callbacks (all optional) */
    int (*pre_load)(void *session, void *user_data);
    int (*post_load)(void *session, void *user_data);
    int (*load_data)(void *session, const struct nmo_chunk *chunk, void *user_data);
    struct nmo_chunk *(*save_data)(void *session, void *user_data);
    int (*pre_save)(void *session, void *user_data);
    int (*post_save)(void *session, void *user_data);

    /** User data passed to callbacks */
    void *user_data;
} nmo_extension_manager_desc_t;

/**
 * @brief Type contribution descriptor
 *
 * Describes a type to be registered by an extension.
 * The host stamps creator_plugin_guid and ignores any extension-provided creator field.
 */
typedef struct nmo_extension_type_desc {
    /** Type GUID (required) */
    nmo_guid_t guid;

    /** Human-readable name (host will deep-copy) */
    const char *name;

    /** Parent type GUID for inheritance (NMO_GUID_NULL for no parent) */
    nmo_guid_t parent_guid;

    /** Size in bytes */
    uint32_t size;

    /** Type category flags */
    uint32_t category;

    /** Type flags */
    uint32_t flags;

    /** Class ID for object types (0 if not an object type) */
    nmo_class_id_t class_id;
} nmo_extension_type_desc_t;

/**
 * @brief Host API function table
 *
 * Provided to extensions during init(). Extensions use this to register
 * their contributions. All registration functions deep-copy their inputs.
 */
typedef struct nmo_extension_host {
    /** ABI version (must equal NMO_EXTENSION_ABI_VERSION) */
    uint32_t abi_version;

    /** Size of this struct in bytes */
    uint32_t struct_size;

    /**
     * @brief Register manager contributions
     *
     * @param host_user Opaque host context (from init callback)
     * @param plugin_guid Caller plugin GUID (for rollback tracking)
     * @param descs Array of manager descriptors
     * @param desc_count Number of descriptors
     * @return NMO_OK on success
     */
    nmo_status_t (*register_managers)(
        void *host_user,
        nmo_guid_t plugin_guid,
        const nmo_extension_manager_desc_t *descs,
        size_t desc_count);

    /**
     * @brief Register type contributions
     *
     * @param host_user Opaque host context (from init callback)
     * @param plugin_guid Caller plugin GUID (for rollback tracking)
     * @param descs Array of type descriptors
     * @param desc_count Number of descriptors
     * @return NMO_OK on success
     */
    nmo_status_t (*register_types)(
        void *host_user,
        nmo_guid_t plugin_guid,
        const nmo_extension_type_desc_t *descs,
        size_t desc_count);
} nmo_extension_host_t;

/**
 * @brief Minimum struct_size the extension requires to safely read all fields
 */
#define NMO_EXTENSION_HOST_REQUIRED_SIZE \
    (offsetof(nmo_extension_host_t, register_types) + sizeof(((nmo_extension_host_t *)0)->register_types))

/* ============================================================================
 * Query Function Signature
 * ============================================================================ */

/**
 * @brief Extension query function signature
 *
 * Dynamic extensions export this function (symbol: nmo_extension_query).
 * The returned plugin array may be static storage inside the DLL.
 *
 * @param out_plugins Receives pointer to plugin array
 * @param out_count Receives number of plugins
 * @return NMO_OK on success
 */
typedef nmo_status_t (*nmo_extension_query_fn)(
    const nmo_extension_plugin_t **out_plugins,
    size_t *out_count);

/* ============================================================================
 * ABI Compatibility Predicates
 * ============================================================================ */

/**
 * @brief Check if a plugin descriptor is compatible with this host
 *
 * @param m Plugin descriptor to check
 * @return 1 if compatible, 0 otherwise
 */
static inline int nmo_extension_plugin_is_compatible(const nmo_extension_plugin_t *m) {
    if (m == NULL) return 0;
    if (m->abi_version != NMO_EXTENSION_ABI_VERSION) return 0;
    if (m->struct_size < NMO_EXTENSION_PLUGIN_REQUIRED_SIZE) return 0;
    return 1;
}

/**
 * @brief Check if a host API table is compatible with this extension
 *
 * @param h Host API table to check
 * @return 1 if compatible, 0 otherwise
 */
static inline int nmo_extension_host_is_compatible(const nmo_extension_host_t *h) {
    if (h == NULL) return 0;
    if (h->abi_version != NMO_EXTENSION_ABI_VERSION) return 0;
    if (h->struct_size < NMO_EXTENSION_HOST_REQUIRED_SIZE) return 0;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXTENSION_ABI_H */
