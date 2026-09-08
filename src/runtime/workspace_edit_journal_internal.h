/**
 * @file workspace_edit_journal_internal.h
 * @brief Private journal Interface for workspace edit transactions.
 */

#ifndef NMO_RUNTIME_WORKSPACE_EDIT_JOURNAL_INTERNAL_H
#define NMO_RUNTIME_WORKSPACE_EDIT_JOURNAL_INTERNAL_H

#include "core/nmo_arena.h"
#include "runtime/nmo_workspace.h"

#include <stdbool.h>
#include <stddef.h>

typedef nmo_status_t (*nmo_workspace_edit_action_fn)(
    nmo_workspace_edit_t *edit,
    void *payload);

typedef enum nmo_workspace_edit_action_phase {
    NMO_WORKSPACE_EDIT_ACTION_ROLLBACK = 0,
    NMO_WORKSPACE_EDIT_ACTION_COMMIT,
    NMO_WORKSPACE_EDIT_ACTION_CLEANUP
} nmo_workspace_edit_action_phase_t;

typedef struct nmo_workspace_edit_action {
    nmo_workspace_edit_action_fn fn;
    void *payload;
} nmo_workspace_edit_action_t;

typedef struct nmo_workspace_edit_action_list {
    nmo_workspace_edit_action_t *items;
    size_t count;
    size_t capacity;
} nmo_workspace_edit_action_list_t;

typedef struct nmo_workspace_edit_journal {
    nmo_arena_t *arena;
    nmo_workspace_edit_action_list_t rollback;
    nmo_workspace_edit_action_list_t commit;
    nmo_workspace_edit_action_list_t cleanup;
} nmo_workspace_edit_journal_t;

typedef struct nmo_workspace_edit_journal_checkpoint {
    size_t rollback_count;
    size_t commit_count;
    size_t cleanup_count;
    nmo_arena_mark_t arena_mark;
    bool arena_mark_active;
} nmo_workspace_edit_journal_checkpoint_t;

nmo_status_t nmo_workspace_edit_journal_init(
    nmo_workspace_edit_journal_t *journal);
void nmo_workspace_edit_journal_dispose(
    nmo_workspace_edit_journal_t *journal);
void *nmo_workspace_edit_journal_alloc(
    nmo_workspace_edit_journal_t *journal,
    size_t size,
    size_t align);

nmo_status_t nmo_workspace_edit_journal_push(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_action_phase_t phase,
    nmo_workspace_edit_action_fn fn,
    void *payload);
nmo_status_t nmo_workspace_edit_journal_push_rollback_cleanup(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn rollback_fn,
    nmo_workspace_edit_action_fn cleanup_fn,
    void *payload);

nmo_workspace_edit_journal_checkpoint_t nmo_workspace_edit_journal_checkpoint(
    const nmo_workspace_edit_journal_t *journal);
nmo_status_t nmo_workspace_edit_journal_mark_arena(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_journal_checkpoint_t *checkpoint);
nmo_status_t nmo_workspace_edit_journal_release_arena(
    nmo_workspace_edit_journal_t *journal,
    const nmo_workspace_edit_journal_checkpoint_t *checkpoint);
nmo_status_t nmo_workspace_edit_journal_rewind_arena(
    nmo_workspace_edit_journal_t *journal,
    const nmo_workspace_edit_journal_checkpoint_t *checkpoint);

void nmo_workspace_edit_journal_abort(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_journal_checkpoint_t checkpoint);
void nmo_workspace_edit_journal_rollback_all(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit);
nmo_status_t nmo_workspace_edit_journal_run_commit(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit);
void nmo_workspace_edit_journal_cleanup_all(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit);

nmo_status_t nmo_workspace_edit_journal_snapshot_bytes(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size);

#endif /* NMO_RUNTIME_WORKSPACE_EDIT_JOURNAL_INTERNAL_H */
