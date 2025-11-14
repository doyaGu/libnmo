/**
 * @file session.c
 * @brief Session implementation (Phase 8.2)
 */

#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "session/nmo_object_repository.h"
#include "session/nmo_object_index.h"
#include "session/nmo_reference_resolver.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk_pool.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_ARENA_SIZE (1024 * 1024)  /* 1 MB */
#define DEFAULT_CHUNK_POOL_CAPACITY 128

/**
 * Session structure
 */
typedef struct nmo_session {
    /* Borrowed context (not owned) */
    nmo_context_t *context;

    /* Owned resources */
    nmo_arena_t *arena;
    nmo_object_repository_t *repository;

    /* Object index (Phase 5) */
    nmo_object_index_t *object_index;

    /* Reference resolver (initialised on demand) */
    nmo_reference_resolver_t *reference_resolver;

    /* File information */
    nmo_file_info_t file_info;
    
    /* File header (stored opaquely in arena to avoid format layer dependency) */
    void *file_header;
    size_t file_header_size;

    /* Manager data for round-trip */
    nmo_manager_data_t *manager_data;
    uint32_t manager_data_count;

    /* Chunk pool for chunk allocations */
    nmo_chunk_pool_t *chunk_pool;
    size_t chunk_pool_capacity;
} nmo_session_t;

/**
 * Create session
 */
nmo_session_t *nmo_session_create(nmo_context_t *ctx) {
    if (ctx == NULL) {
        return NULL;
    }

    nmo_session_t *session = (nmo_session_t *) malloc(sizeof(nmo_session_t));
    if (session == NULL) {
        return NULL;
    }

    memset(session, 0, sizeof(nmo_session_t));

    /* Borrow context (do not retain) */
    session->context = ctx;

    /* Create arena for session-local allocations */
    nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);
    session->arena = nmo_arena_create(allocator, DEFAULT_ARENA_SIZE);
    if (session->arena == NULL) {
        free(session);
        return NULL;
    }

    /* Create object repository */
    session->repository = nmo_object_repository_create(session->arena);
    if (session->repository == NULL) {
        nmo_arena_destroy(session->arena);
        free(session);
        return NULL;
    }

    /* Initialize file info to zero */
    memset(&session->file_info, 0, sizeof(nmo_file_info_t));

    /* Initialize manager data */
    session->manager_data = NULL;
    session->manager_data_count = 0;
    session->chunk_pool = NULL;
    session->chunk_pool_capacity = 0;

    /* Initialize object index */
    session->object_index = NULL;

    /* Initialize reference resolver */
    session->reference_resolver = NULL;
    
    /* Initialize file header */
    session->file_header = NULL;
    session->file_header_size = 0;

    return session;
}

/**
 * Destroy session
 */
void nmo_session_destroy(nmo_session_t *session) {
    if (session != NULL) {
        if (session->object_index != NULL) {
            nmo_object_index_destroy(session->object_index);
            session->object_index = NULL;
        }

        /* Destroy owned resources */
        if (session->repository != NULL) {
            nmo_object_repository_destroy(session->repository);
        }

        if (session->reference_resolver != NULL) {
            session->reference_resolver = NULL;
        }

        if (session->chunk_pool != NULL) {
            nmo_chunk_pool_destroy(session->chunk_pool);
            session->chunk_pool = NULL;
            session->chunk_pool_capacity = 0;
        }

        if (session->arena != NULL) {
            nmo_arena_destroy(session->arena);
        }

        /* Do not release context - we only borrowed it */

        free(session);
    }
}

/**
 * Get context
 */
nmo_context_t *nmo_session_get_context(const nmo_session_t *session) {
    return session ? session->context : NULL;
}

/**
 * Get arena
 */
nmo_arena_t *nmo_session_get_arena(const nmo_session_t *session) {
    return session ? session->arena : NULL;
}

/**
 * Get object repository
 */
nmo_object_repository_t *nmo_session_get_repository(const nmo_session_t *session) {
    return session ? session->repository : NULL;
}

nmo_chunk_pool_t *nmo_session_get_chunk_pool(const nmo_session_t *session) {
    return session ? session->chunk_pool : NULL;
}

nmo_chunk_pool_t *nmo_session_ensure_chunk_pool(
    nmo_session_t *session,
    size_t initial_capacity_hint
) {
    if (session == NULL || session->arena == NULL) {
        return NULL;
    }

    if (session->chunk_pool != NULL) {
        return session->chunk_pool;
    }

    size_t capacity = initial_capacity_hint > 0 ? initial_capacity_hint : DEFAULT_CHUNK_POOL_CAPACITY;
    session->chunk_pool = nmo_chunk_pool_create(capacity, session->arena);
    session->chunk_pool_capacity = (session->chunk_pool != NULL) ? capacity : 0;
    return session->chunk_pool;
}

/**
 * Get file info
 */
nmo_file_info_t nmo_session_get_file_info(const nmo_session_t *session) {
    if (session) {
        return session->file_info;
    }

    nmo_file_info_t empty;
    memset(&empty, 0, sizeof(nmo_file_info_t));
    return empty;
}

/**
 * Set file info
 */
int nmo_session_set_file_info(nmo_session_t *session, const nmo_file_info_t *info) {
    if (session == NULL || info == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    session->file_info = *info;
    return NMO_OK;
}

/**
 * Set manager data
 */
void nmo_session_set_manager_data(nmo_session_t *session, nmo_manager_data_t *data, uint32_t count) {
    if (session != NULL) {
        session->manager_data = data;
        session->manager_data_count = count;
    }
}

/**
 * Get manager data
 */
nmo_manager_data_t *nmo_session_get_manager_data(const nmo_session_t *session, uint32_t *out_count) {
    if (session == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }

    if (out_count != NULL) {
        *out_count = session->manager_data_count;
    }
    return session->manager_data;
}

/* High-level convenience API */

/**
 * Load NMO file into session
 */
nmo_session_t *nmo_session_load(nmo_context_t *ctx, const char *filename) {
    if (ctx == NULL || filename == NULL) {
        return NULL;
    }

    /* Create session */
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        return NULL;
    }

    /* Load file using high-level API */
    int result = nmo_load_file(session, filename, NMO_LOAD_DEFAULT);
    if (result != NMO_OK) {
        nmo_session_destroy(session);
        return NULL;
    }

    return session;
}

/**
 * Save session to NMO file
 */
int nmo_session_save(nmo_session_t *session, const char *filename) {
    if (session == NULL || filename == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Use high-level save API */
    return nmo_save_file(session, filename, NMO_SAVE_DEFAULT);
}

/**
 * Get all objects from session
 */
int nmo_session_get_objects(
    nmo_session_t *session,
    nmo_object_t ***out_objects,
    size_t *out_count
) {
    if (session == NULL || out_objects == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = session->repository;
    if (repo == NULL) {
        *out_objects = NULL;
        *out_count = 0;
        return NMO_OK;
    }

    *out_objects = nmo_object_repository_get_all(repo, out_count);
    return NMO_OK;
}

/**
 * Set object index
 */
void nmo_session_set_object_index(nmo_session_t *session, nmo_object_index_t *index) {
    if (session != NULL) {
        session->object_index = index;
        nmo_object_repository_set_index(session->repository, index);
    }
}

/**
 * Get object index
 */
nmo_object_index_t *nmo_session_get_object_index(const nmo_session_t *session) {
    return (session != NULL) ? session->object_index : NULL;
}

/**
 * Rebuild object indexes
 */
int nmo_session_rebuild_indexes(nmo_session_t *session, uint32_t flags) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    if (session->object_index == NULL) {
        /* Create index if it doesn't exist */
        session->object_index = nmo_object_index_create(session->repository, session->arena);
        if (session->object_index == NULL) {
            return NMO_ERR_NOMEM;
        }
        nmo_object_repository_set_index(session->repository, session->object_index);
    }

    if (flags == 0) {
        flags = nmo_object_index_get_active_flags(session->object_index);
        if (flags == 0) {
            flags = NMO_INDEX_BUILD_ALL;
        }
    }
    
    /* Rebuild */
    return nmo_object_index_rebuild(session->object_index, flags);
}

int nmo_session_get_object_index_stats(
    const nmo_session_t *session,
    nmo_index_stats_t *stats
) {
    if (session == NULL || stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->object_index == NULL) {
        memset(stats, 0, sizeof(*stats));
        return NMO_ERR_NOT_FOUND;
    }

    return nmo_object_index_get_stats(session->object_index, stats);
}

/**
 * Get file header from session
 */
const nmo_header_t *nmo_session_get_header(const nmo_session_t *session) {
    if (session == NULL) {
        return NULL;
    }

    /* Return stored file header (opaque pointer, caller knows the type) */
    return (const nmo_header_t *)session->file_header;
}

/**
 * Set file header (internal use by parser)
 */
void nmo_session_set_file_header(nmo_session_t *session, const void *header, size_t header_size) {
    if (session == NULL || header == NULL || session->arena == NULL || header_size == 0) {
        return;
    }
    
    /* Allocate header in session arena */
    void *stored_header = nmo_arena_alloc(
        session->arena,
        header_size,
        16
    );
    
    if (stored_header != NULL) {
        /* Copy header data */
        memcpy(stored_header, header, header_size);
        session->file_header = stored_header;
        session->file_header_size = header_size;
    }
}

/* ==================== Object Query API (Phase 5) ==================== */

/**
 * Find object by name
 */
nmo_object_t *nmo_session_find_by_name(
    nmo_session_t *session,
    const char *name,
    nmo_class_id_t class_id
) {
    if (session == NULL || name == NULL) {
        return NULL;
    }
    
    /* Use index if available */
    if (session->object_index != NULL) {
        return nmo_object_index_find_by_name(session->object_index, name, class_id);
    }
    
    /* Fall back to repository linear search */
    return nmo_object_repository_find_by_name(session->repository, name);
}

/**
 * Find object by GUID
 */
nmo_object_t *nmo_session_find_by_guid(
    nmo_session_t *session,
    nmo_guid_t guid
) {
    if (session == NULL) {
        return NULL;
    }
    
    /* Use index if available */
    if (session->object_index != NULL) {
        return nmo_object_index_find_by_guid(session->object_index, guid);
    }
    
    /* Fall back to repository linear search */
    size_t count;
    nmo_object_t **objects = nmo_object_repository_get_all(session->repository, &count);
    
    for (size_t i = 0; i < count; i++) {
        if (nmo_guid_equals(objects[i]->type_guid, guid)) {
            return objects[i];
        }
    }
    
    return NULL;
}

/**
 * Get all objects of a specific class
 */
nmo_object_t **nmo_session_get_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    size_t *out_count
) {
    if (session == NULL || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }
    
    /* Use index if available */
    if (session->object_index != NULL) {
        return nmo_object_index_get_by_class(session->object_index, class_id, out_count);
    }
    
    /* Fall back to repository search */
    return nmo_object_repository_find_by_class(session->repository, class_id, out_count);
}

/**
 * Count objects of a specific class
 */
size_t nmo_session_count_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id
) {
    if (session == NULL) {
        return 0;
    }
    
    size_t count = 0;
    nmo_session_get_objects_by_class(session, class_id, &count);
    return count;
}
nmo_reference_resolver_t *nmo_session_get_reference_resolver(
    const nmo_session_t *session
) {
    return (session != NULL) ? session->reference_resolver : NULL;
}

nmo_reference_resolver_t *nmo_session_ensure_reference_resolver(
    nmo_session_t *session
) {
    if (session == NULL) {
        return NULL;
    }

    if (session->reference_resolver != NULL) {
        return session->reference_resolver;
    }

    if (session->repository == NULL || session->arena == NULL) {
        return NULL;
    }

    nmo_reference_resolver_t *resolver = nmo_reference_resolver_create(
        session->repository,
        session->arena
    );

    if (resolver != NULL) {
        session->reference_resolver = resolver;
    }

    return resolver;
}

void nmo_session_reset_reference_resolver(nmo_session_t *session) {
    if (session != NULL) {
        session->reference_resolver = NULL;
    }
}
