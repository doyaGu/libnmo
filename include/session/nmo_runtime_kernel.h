#ifndef NMO_SESSION_RUNTIME_KERNEL_H
#define NMO_SESSION_RUNTIME_KERNEL_H

#include "session/nmo_session.h"
#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_load_options nmo_load_options_t;
typedef struct nmo_save_options nmo_save_options_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_runtime nmo_type_runtime_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_id_remap nmo_id_remap_t;
typedef struct nmo_arena nmo_arena_t;

/*
 * Runtime kernel request/ops shaping is kept public for advanced C callers,
 * but it is not the default binding-facing contract.
 */
#define NMO_RUNTIME_KERNEL_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_RUNTIME_KERNEL_API_TIER NMO_API_TIER_ADVANCED_C

/**
 * @brief Runtime operation callbacks (set by app layer, called by runtime kernel)
 *
 * These callbacks decouple the session-layer runtime kernel from
 * app-layer load/save functions and behavior-layer index building.
 */
typedef struct nmo_runtime_ops {
    nmo_status_t (*load_file)(nmo_session_t *session, const char *path,
                              const nmo_load_options_t *opts);
    nmo_status_t (*save_file)(nmo_session_t *session, const char *path,
                              const nmo_save_options_t *opts);
    void (*post_load)(nmo_session_t *session); /**< Called after remap (e.g., build behavior index) */
} nmo_runtime_ops_t;

typedef enum nmo_runtime_op_kind {
    NMO_RUNTIME_OP_LOAD = 0,
    NMO_RUNTIME_OP_SAVE,
    NMO_RUNTIME_OP_CREATE,
    NMO_RUNTIME_OP_COPY,
    NMO_RUNTIME_OP_DELETE
} nmo_runtime_op_kind_t;

typedef enum nmo_runtime_request_flags {
    NMO_RUNTIME_REQUEST_DEFAULT = 0,
    NMO_RUNTIME_REQUEST_STRICT = 0x0001,
    NMO_RUNTIME_REQUEST_CASCADE = 0x0002,
    NMO_RUNTIME_REQUEST_SAFE_DETACH = 0x0004
} nmo_runtime_request_flags_t;

typedef struct nmo_runtime_request {
    nmo_runtime_op_kind_t kind;
    uint32_t flags;
    union {
        struct {
            const char *path;
            const nmo_load_options_t *options;
        } load;
        struct {
            const char *path;
            const nmo_save_options_t *options;
        } save;
        struct {
            nmo_class_id_t class_id;
            const char *name;
            nmo_guid_t type_guid;
            nmo_object_id_t *out_created_id;
        } create;
        struct {
            const nmo_object_id_t *ids;
            size_t count;
        } copy;
        struct {
            const nmo_object_id_t *ids;
            size_t count;
        } destroy;
    } payload;
} nmo_runtime_request_t;

typedef struct nmo_runtime_report {
    nmo_status_t status;
    size_t affected_objects;
    size_t created_objects;
    size_t copied_objects;
    size_t deleted_objects;
    uint32_t manager_event_errors;
    uint32_t object_hook_errors;
} nmo_runtime_report_t;

/**
 * @brief Execute an advanced runtime-kernel request against the session.
 *
 * Ordinary consumers should prefer owner-led document/workspace APIs.
 * This header keeps advanced runtime-kernel entry points and session-shaped
 * workflow wrappers for low-level callers.
 */
NMO_API nmo_status_t nmo_session_execute(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

/**
 * @brief Install runtime operation callbacks for the session.
 *
 * Advanced setup API used by the app/load-save orchestration layer.
 */
NMO_API void nmo_session_set_runtime_ops(
    nmo_session_t *session,
    const nmo_runtime_ops_t *ops);

/**
 * @brief Get the runtime operation callbacks installed on the session.
 * @ownership borrowed
 */
NMO_API const nmo_runtime_ops_t *nmo_session_get_runtime_ops(
    const nmo_session_t *session);

NMO_API nmo_status_t nmo_runtime_kernel_execute(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

/* Load-specific finalization used by parser once deserialize is complete. */
NMO_API nmo_status_t nmo_runtime_kernel_finalize_load(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

NMO_API nmo_status_t nmo_runtime_execute_delete(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *report);
NMO_API nmo_status_t nmo_runtime_preview_delete(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_object_id_t **out_ids,
    size_t *out_count);
NMO_API nmo_status_t nmo_runtime_remap_copy_refs(
    const nmo_type_runtime_t *type_rt,
    const nmo_type_descriptor_t *type,
    void *instance,
    const nmo_id_remap_t *remap);
NMO_API nmo_status_t nmo_runtime_remap_all_refs(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    uint32_t request_flags);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_RUNTIME_KERNEL_H */
