/**
 * @file nmo_manager.h
 * @brief Manager plugin interface for object type handling
 */

#ifndef NMO_MANAGER_H
#define NMO_MANAGER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk;

/**
 * @brief Manager plugin structure
 *
 * Managers are plugins that handle specific object types during load/save operations.
 * They provide hooks for processing object data at various stages of the pipeline.
 */
typedef struct nmo_manager {
    /* Identity */
    nmo_guid            guid;           /**< Manager GUID */
    const char*         name;           /**< Manager name */
    nmo_plugin_category category;       /**< Plugin category */

    /* Load hooks */
    /**
     * @brief Called before loading objects
     * @param session Session context
     * @param user_data User-provided data
     * @return NMO_OK on success
     */
    int (*pre_load)(void* session, void* user_data);

    /**
     * @brief Called after loading objects
     * @param session Session context
     * @param user_data User-provided data
     * @return NMO_OK on success
     */
    int (*post_load)(void* session, void* user_data);

    /**
     * @brief Load object data from chunk
     * @param session Session context
     * @param chunk Chunk containing object data
     * @param user_data User-provided data
     * @return NMO_OK on success
     */
    int (*load_data)(void* session, const nmo_chunk* chunk, void* user_data);

    /* Save hooks */
    /**
     * @brief Save object data to chunk
     * @param session Session context
     * @param user_data User-provided data
     * @return Chunk containing object data or NULL on error
     */
    nmo_chunk* (*save_data)(void* session, void* user_data);

    /**
     * @brief Called before saving objects
     * @param session Session context
     * @param user_data User-provided data
     * @return NMO_OK on success
     */
    int (*pre_save)(void* session, void* user_data);

    /**
     * @brief Called after saving objects
     * @param session Session context
     * @param user_data User-provided data
     * @return NMO_OK on success
     */
    int (*post_save)(void* session, void* user_data);

    /* User data */
    void* user_data;                    /**< User-provided context data */
} nmo_manager;

/**
 * @brief Create manager
 * @param guid Manager GUID
 * @param name Manager name
 * @param category Plugin category
 * @return Manager or NULL on error
 */
NMO_API nmo_manager* nmo_manager_create(nmo_guid guid, const char* name, nmo_plugin_category category);

/**
 * @brief Destroy manager
 * @param manager Manager to destroy
 */
NMO_API void nmo_manager_destroy(nmo_manager* manager);

/**
 * @brief Set manager user data
 * @param manager Manager
 * @param user_data User data pointer
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_user_data(nmo_manager* manager, void* user_data);

/**
 * @brief Set pre-load hook
 * @param manager Manager
 * @param hook Hook function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_pre_load_hook(nmo_manager* manager, int (*hook)(void*, void*));

/**
 * @brief Set post-load hook
 * @param manager Manager
 * @param hook Hook function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_post_load_hook(nmo_manager* manager, int (*hook)(void*, void*));

/**
 * @brief Set load-data hook
 * @param manager Manager
 * @param hook Hook function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_load_data_hook(nmo_manager* manager, int (*hook)(void*, const nmo_chunk*, void*));

/**
 * @brief Set save-data hook
 * @param manager Manager
 * @param hook Hook function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_save_data_hook(nmo_manager* manager, nmo_chunk* (*hook)(void*, void*));

/**
 * @brief Set pre-save hook
 * @param manager Manager
 * @param hook Hook function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_pre_save_hook(nmo_manager* manager, int (*hook)(void*, void*));

/**
 * @brief Set post-save hook
 * @param manager Manager
 * @param hook Hook function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_post_save_hook(nmo_manager* manager, int (*hook)(void*, void*));

/**
 * @brief Invoke pre-load hook
 * @param manager Manager
 * @param session Session context
 * @return NMO_OK on success, or error if hook fails
 */
NMO_API int nmo_manager_invoke_pre_load(nmo_manager* manager, void* session);

/**
 * @brief Invoke post-load hook
 * @param manager Manager
 * @param session Session context
 * @return NMO_OK on success, or error if hook fails
 */
NMO_API int nmo_manager_invoke_post_load(nmo_manager* manager, void* session);

/**
 * @brief Invoke load-data hook
 * @param manager Manager
 * @param session Session context
 * @param chunk Chunk to load from
 * @return NMO_OK on success, or error if hook fails
 */
NMO_API int nmo_manager_invoke_load_data(nmo_manager* manager, void* session, const nmo_chunk* chunk);

/**
 * @brief Invoke save-data hook
 * @param manager Manager
 * @param session Session context
 * @return Chunk or NULL if hook fails
 */
NMO_API nmo_chunk* nmo_manager_invoke_save_data(nmo_manager* manager, void* session);

/**
 * @brief Invoke pre-save hook
 * @param manager Manager
 * @param session Session context
 * @return NMO_OK on success, or error if hook fails
 */
NMO_API int nmo_manager_invoke_pre_save(nmo_manager* manager, void* session);

/**
 * @brief Invoke post-save hook
 * @param manager Manager
 * @param session Session context
 * @return NMO_OK on success, or error if hook fails
 */
NMO_API int nmo_manager_invoke_post_save(nmo_manager* manager, void* session);

/**
 * @brief Get manager GUID
 * @param manager Manager
 * @return Manager GUID
 */
NMO_API nmo_guid nmo_manager_get_guid(const nmo_manager* manager);

/**
 * @brief Get manager name
 * @param manager Manager
 * @return Manager name (may be NULL)
 */
NMO_API const char* nmo_manager_get_name(const nmo_manager* manager);

/**
 * @brief Get manager category
 * @param manager Manager
 * @return Plugin category
 */
NMO_API nmo_plugin_category nmo_manager_get_category(const nmo_manager* manager);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MANAGER_H */
