/**
 * @file session.c
 * @brief Session implementation (Phase 8.2)
 */

#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_data.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_ARENA_SIZE (1024 * 1024)  /* 1 MB */

/**
 * Session structure
 */
typedef struct nmo_session {
    /* Borrowed context (not owned) */
    nmo_context_t *context;

    /* Owned resources */
    nmo_arena_t *arena;
    nmo_object_repository_t *repository;

    /* File information */
    nmo_file_info_t file_info;

    /* Manager data for round-trip */
    nmo_manager_data_t *manager_data;
    uint32_t manager_data_count;
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

    return session;
}

/**
 * Destroy session
 */
void nmo_session_destroy(nmo_session_t *session) {
    if (session != NULL) {
        /* Destroy owned resources */
        if (session->repository != NULL) {
            nmo_object_repository_destroy(session->repository);
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
