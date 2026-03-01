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
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_runtime_event_ctx nmo_runtime_event_ctx_t;

/**
 * @brief Runtime lifecycle events dispatched to managers.
 */
typedef enum nmo_runtime_event_kind {
    NMO_RUNTIME_EVENT_PRE_LOAD = 0,
    NMO_RUNTIME_EVENT_POST_LOAD,
    NMO_RUNTIME_EVENT_PRE_SAVE,
    NMO_RUNTIME_EVENT_POST_SAVE,
    NMO_RUNTIME_EVENT_PRE_COPY,
    NMO_RUNTIME_EVENT_POST_COPY,
    NMO_RUNTIME_EVENT_PRE_DELETE,
    NMO_RUNTIME_EVENT_POST_DELETE
} nmo_runtime_event_kind_t;

/**
 * @brief Manager event callback.
 */
typedef int (*nmo_manager_on_event_fn)(
    void *session,
    const nmo_runtime_event_ctx_t *ctx,
    void *user_data);

/**
 * @brief Event dispatch payload.
 *
 * `manager_chunk_in` is typically populated during load-side dispatch.
 * `manager_chunk_out` is typically populated by managers during save-side dispatch.
 */
typedef struct nmo_runtime_event_ctx {
    nmo_runtime_event_kind_t event;
    uint32_t manager_id;
    nmo_guid_t manager_guid;
    const nmo_chunk_t *manager_chunk_in;
    nmo_chunk_t **manager_chunk_out;
    uint32_t flags;
    const void *request;
    void *report;
} nmo_runtime_event_ctx_t;

/**
 * @brief Manager plugin structure
 *
 * Managers are plugins that handle specific object types during load/save operations.
 * They provide hooks for processing object data at various stages of the pipeline.
 */
typedef struct nmo_manager {
    /* Identity */
    nmo_guid_t guid;              /**< Manager GUID */
    const char *name;             /**< Manager name */
    nmo_plugin_category_t category; /**< Plugin category */

    /* Unified event hook */
    nmo_manager_on_event_fn on_event;

    /* User data */
    void *user_data; /**< User-provided context data */
} nmo_manager_t;

/**
 * @brief Create manager
 * @param guid Manager GUID
 * @param name Manager name
 * @param category Plugin category
 * @return Manager or NULL on error
 */
NMO_API nmo_manager_t *nmo_manager_create(nmo_guid_t guid, const char *name, nmo_plugin_category_t category);

/**
 * @brief Destroy manager
 * @param manager Manager to destroy
 */
NMO_API void nmo_manager_destroy(nmo_manager_t *manager);

/**
 * @brief Set manager user data
 * @param manager Manager
 * @param user_data User data pointer
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_user_data(nmo_manager_t *manager, void *user_data);

/**
 * @brief Set unified event callback.
 * @param manager Manager
 * @param hook Callback function
 * @return NMO_OK on success
 */
NMO_API int nmo_manager_set_on_event_hook(nmo_manager_t *manager, nmo_manager_on_event_fn hook);

/**
 * @brief Dispatch runtime event to manager.
 * @param manager Manager
 * @param session Session context
 * @param ctx Event payload (required)
 * @return NMO_OK on success, or error if hook fails
 */
NMO_API int nmo_manager_invoke_event(
    nmo_manager_t *manager,
    void *session,
    const nmo_runtime_event_ctx_t *ctx);

/**
 * @brief Get manager GUID
 * @param manager Manager
 * @return Manager GUID
 */
NMO_API nmo_guid_t nmo_manager_get_guid(const nmo_manager_t *manager);

/**
 * @brief Get manager name
 * @param manager Manager
 * @return Manager name (may be NULL)
 */
NMO_API const char *nmo_manager_get_name(const nmo_manager_t *manager);

/**
 * @brief Get manager category
 * @param manager Manager
 * @return Plugin category
 */
NMO_API nmo_plugin_category_t nmo_manager_get_category(const nmo_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif /* NMO_MANAGER_H */
