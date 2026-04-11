/**
 * @file nmo_extension_registry.h
 * @brief Extension registry for managing loaded plugins
 *
 * The extension registry owns all loaded plugin instances and provides
 * unified registration, lookup, and unload semantics.
 *
 * Ownership model:
 * - Registry owns all plugin instance records and their deep-copied metadata
 * - Each plugin instance has a dedicated arena for its strings and contribution lists
 * - On unload, contributions are unregistered, then the plugin arena is destroyed
 *
 * Thread safety: NOT thread-safe. External synchronization required.
 */

#ifndef NMO_EXTENSION_REGISTRY_H
#define NMO_EXTENSION_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_allocator.h"
#include "extension/nmo_extension_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;
typedef struct nmo_manager_registry nmo_manager_registry_t;

/* ============================================================================
 * Plugin Instance Info
 *
 * Read-only view of a loaded plugin returned by list/find operations.
 * ============================================================================ */

/**
 * @brief Plugin info flags
 */
typedef enum nmo_extension_plugin_flags {
    NMO_EXTENSION_FLAG_NONE = 0,
    /** Plugin was loaded from a shared library */
    NMO_EXTENSION_FLAG_DYNAMIC = 0x0001,
    /** Plugin init() has been called */
    NMO_EXTENSION_FLAG_INITIALIZED = 0x0002,
} nmo_extension_plugin_flags_t;

/**
 * @brief Plugin instance info (read-only view)
 *
 * Returned by list/find operations. Pointers are valid until the plugin
 * is unloaded or the registry is destroyed.
 */
typedef struct nmo_extension_plugin_info {
    /** Plugin GUID */
    nmo_guid_t guid;

    /** Plugin version */
    uint32_t version;

    /** Plugin category */
    nmo_plugin_category_t category;

    /** Plugin flags */
    uint32_t flags;

    /** Plugin name (owned by registry) */
    const char *name;

    /** Library path if dynamic (owned by registry, NULL for static) */
    const char *library_path;

    /** Number of manager contributions */
    size_t manager_count;

    /** Number of type contributions */
    size_t type_count;

    /** Number of operation contributions */
    size_t operation_count;
} nmo_extension_plugin_info_t;

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================ */

/**
 * @brief Create an extension registry
 *
 * @param allocator Allocator for registry structures (NULL for default)
 * @param type_registry Type registry for type contributions (required)
 * @param operation_registry Operation registry for operation contributions (may be NULL)
 * @param manager_registry Manager registry for manager contributions (required)
 * @return New registry or NULL on failure
 * @ownership owned
 */
NMO_API nmo_extension_registry_t *nmo_extension_registry_create(
    nmo_allocator_t *allocator,
    nmo_type_registry_t *type_registry,
    nmo_operation_registry_t *operation_registry,
    nmo_manager_registry_t *manager_registry);

/**
 * @brief Destroy an extension registry
 *
 * Unloads all plugins in reverse order, then frees the registry.
 *
 * @param registry Registry to destroy
 */
NMO_API void nmo_extension_registry_destroy(nmo_extension_registry_t *registry);

/* ============================================================================
 * Static Registration
 * ============================================================================ */

/**
 * @brief Register static plugin descriptors
 *
 * Registers plugins from a static array (not from a shared library).
 * The registry deep-copies all metadata and calls init() for each plugin.
 *
 * On partial failure, successfully registered plugins are rolled back.
 *
 * @param registry Extension registry
 * @param plugins Array of plugin descriptors
 * @param plugin_count Number of plugins
 * @return NMO_OK on success, error code on failure
 *
 * Errors:
 * - NMO_ERR_INVALID_ARGUMENT: registry or plugins is NULL
 * - NMO_ERR_UNSUPPORTED_VERSION: ABI version mismatch
 * - NMO_ERR_ALREADY_EXISTS: Plugin GUID already registered
 */
NMO_API nmo_status_t nmo_extension_registry_register_static(
    nmo_extension_registry_t *registry,
    const nmo_extension_plugin_t *plugins,
    size_t plugin_count);

/* ============================================================================
 * Dynamic Loading
 * ============================================================================ */

/**
 * @brief Load plugins from a shared library
 *
 * Opens the library, resolves the query symbol, and registers all exported plugins.
 * The registry takes ownership of the library handle.
 *
 * On partial failure, successfully registered plugins are rolled back and
 * the library is closed.
 *
 * @param registry Extension registry
 * @param path Path to the shared library
 * @param symbol_name Query function symbol (NULL for default "nmo_extension_query")
 * @return NMO_OK on success, error code on failure
 *
 * Errors:
 * - NMO_ERR_INVALID_ARGUMENT: registry or path is NULL
 * - NMO_ERR_CANT_OPEN_FILE: Cannot open library
 * - NMO_ERR_NOT_FOUND: Symbol not found
 * - NMO_ERR_UNSUPPORTED_VERSION: ABI version mismatch
 * - NMO_ERR_ALREADY_EXISTS: Plugin GUID already registered
 */
NMO_API nmo_status_t nmo_extension_registry_load_library(
    nmo_extension_registry_t *registry,
    const char *path,
    const char *symbol_name);

/* ============================================================================
 * Unloading
 * ============================================================================ */

/**
 * @brief Unload a plugin by GUID
 *
 * Performs teardown in strict order:
 * 1. Call shutdown() callback (if present)
 * 2. Unregister all contributions (types + managers)
 * 3. Remove plugin instance from registry
 * 4. Close shared library (if dynamic)
 *
 * @param registry Extension registry
 * @param guid Plugin GUID to unload
 * @return NMO_OK on success
 *
 * Errors:
 * - NMO_ERR_INVALID_ARGUMENT: registry is NULL
 * - NMO_ERR_NOT_FOUND: Plugin not found
 */
NMO_API nmo_status_t nmo_extension_registry_unload_by_guid(
    nmo_extension_registry_t *registry,
    nmo_guid_t guid);

/* ============================================================================
 * Queries
 * ============================================================================ */

/**
 * @brief List all loaded plugins
 *
 * Returns an array of plugin info records. The array and its contents
 * are valid until the next registry modification.
 *
 * @param registry Extension registry
 * @param out_count Receives number of plugins
 * @return Array of plugin info records, or NULL if empty/error
 * @ownership borrowed
 */
NMO_API const nmo_extension_plugin_info_t *nmo_extension_registry_list(
    const nmo_extension_registry_t *registry,
    size_t *out_count);

/**
 * @brief Find a plugin by GUID
 *
 * @param registry Extension registry
 * @param guid Plugin GUID to find
 * @return Plugin info or NULL if not found
 * @ownership borrowed
 */
NMO_API const nmo_extension_plugin_info_t *nmo_extension_registry_find(
    const nmo_extension_registry_t *registry,
    nmo_guid_t guid);

/**
 * @brief Get the number of loaded plugins
 *
 * @param registry Extension registry
 * @return Number of loaded plugins
 */
NMO_API size_t nmo_extension_registry_get_count(const nmo_extension_registry_t *registry);

/* ============================================================================
 * User Data
 * ============================================================================ */

/**
 * @brief Set opaque user data on the registry
 *
 * Plugins can retrieve this during init() via
 * nmo_extension_registry_get_user_data().  Typical use: store a data
 * directory path so built-in extensions can find their resources.
 *
 * @param registry Extension registry
 * @param user_data Opaque pointer (ownership stays with caller)
 */
NMO_API void nmo_extension_registry_set_user_data(
    nmo_extension_registry_t *registry,
    void *user_data);

/**
 * @brief Get the opaque user data stored on the registry
 *
 * @param registry Extension registry
 * @return Previously stored pointer, or NULL
 */
NMO_API void *nmo_extension_registry_get_user_data(
    const nmo_extension_registry_t *registry);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXTENSION_REGISTRY_H */
