/**
 * @file workspace_edit_journal.c
 * @brief Workspace edit journal Implementation.
 */

#include "workspace_edit_journal_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct field_bytes_snapshot {
    void *field_ptr;
    size_t size;
    uint8_t bytes[];
} field_bytes_snapshot_t;

static nmo_workspace_edit_action_list_t *journal_action_list(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_action_phase_t phase)
{
    if (journal == NULL) {
        return NULL;
    }
    switch (phase) {
        case NMO_WORKSPACE_EDIT_ACTION_ROLLBACK:
            return &journal->rollback;
        case NMO_WORKSPACE_EDIT_ACTION_COMMIT:
            return &journal->commit;
        case NMO_WORKSPACE_EDIT_ACTION_CLEANUP:
            return &journal->cleanup;
        default:
            return NULL;
    }
}

static nmo_status_t journal_action_list_push(
    nmo_workspace_edit_action_list_t *list,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    if (list == NULL || fn == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0u ? 8u : list->capacity * 2u;
        nmo_workspace_edit_action_t *new_items =
            (nmo_workspace_edit_action_t *)realloc(
                list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return NMO_ERR_NOMEM;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count].fn = fn;
    list->items[list->count].payload = payload;
    list->count++;
    return NMO_OK;
}

static void journal_action_list_run_reverse(
    nmo_workspace_edit_action_list_t *list,
    nmo_workspace_edit_t *edit,
    size_t checkpoint)
{
    if (list == NULL) {
        return;
    }
    while (list->count > checkpoint) {
        list->count--;
        nmo_workspace_edit_action_t action = list->items[list->count];
        (void)action.fn(edit, action.payload);
    }
}

nmo_status_t nmo_workspace_edit_journal_init(
    nmo_workspace_edit_journal_t *journal)
{
    if (journal == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(journal, 0, sizeof(*journal));
    journal->arena = nmo_arena_create(NULL, 16u * 1024u);
    return journal->arena != NULL ? NMO_OK : NMO_ERR_NOMEM;
}

void nmo_workspace_edit_journal_dispose(
    nmo_workspace_edit_journal_t *journal)
{
    if (journal == NULL) {
        return;
    }
    free(journal->rollback.items);
    free(journal->commit.items);
    free(journal->cleanup.items);
    if (journal->arena != NULL) {
        nmo_arena_destroy(journal->arena);
    }
    memset(journal, 0, sizeof(*journal));
}

void *nmo_workspace_edit_journal_alloc(
    nmo_workspace_edit_journal_t *journal,
    size_t size,
    size_t align)
{
    if (journal == NULL || journal->arena == NULL || size == 0u) {
        return NULL;
    }
    return nmo_arena_alloc(journal->arena, size, align);
}

nmo_status_t nmo_workspace_edit_journal_push(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_action_phase_t phase,
    nmo_workspace_edit_action_fn fn,
    void *payload)
{
    return journal_action_list_push(
        journal_action_list(journal, phase), fn, payload);
}

nmo_status_t nmo_workspace_edit_journal_push_rollback_cleanup(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_action_fn rollback_fn,
    nmo_workspace_edit_action_fn cleanup_fn,
    void *payload)
{
    nmo_status_t status = nmo_workspace_edit_journal_push(
        journal, NMO_WORKSPACE_EDIT_ACTION_CLEANUP, cleanup_fn, payload);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_workspace_edit_journal_push(
        journal, NMO_WORKSPACE_EDIT_ACTION_ROLLBACK, rollback_fn, payload);
    if (status != NMO_OK) {
        journal->cleanup.count--;
        (void)cleanup_fn(edit, payload);
    }
    return status;
}

nmo_workspace_edit_journal_checkpoint_t nmo_workspace_edit_journal_checkpoint(
    const nmo_workspace_edit_journal_t *journal)
{
    return (nmo_workspace_edit_journal_checkpoint_t){
        .rollback_count = journal != NULL ? journal->rollback.count : 0u,
        .commit_count = journal != NULL ? journal->commit.count : 0u,
        .cleanup_count = journal != NULL ? journal->cleanup.count : 0u,
    };
}

nmo_status_t nmo_workspace_edit_journal_mark_arena(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_journal_checkpoint_t *checkpoint)
{
    if (journal == NULL || journal->arena == NULL || checkpoint == NULL ||
        checkpoint->arena_mark_active) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t status = nmo_arena_mark(
        journal->arena, &checkpoint->arena_mark);
    if (status == NMO_OK) {
        checkpoint->arena_mark_active = true;
    }
    return status;
}

nmo_status_t nmo_workspace_edit_journal_release_arena(
    nmo_workspace_edit_journal_t *journal,
    const nmo_workspace_edit_journal_checkpoint_t *checkpoint)
{
    if (journal == NULL || journal->arena == NULL || checkpoint == NULL ||
        !checkpoint->arena_mark_active) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_arena_release_mark(journal->arena, &checkpoint->arena_mark);
}

nmo_status_t nmo_workspace_edit_journal_rewind_arena(
    nmo_workspace_edit_journal_t *journal,
    const nmo_workspace_edit_journal_checkpoint_t *checkpoint)
{
    if (journal == NULL || journal->arena == NULL || checkpoint == NULL ||
        !checkpoint->arena_mark_active) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_arena_rewind(journal->arena, &checkpoint->arena_mark);
}

void nmo_workspace_edit_journal_abort(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit,
    nmo_workspace_edit_journal_checkpoint_t checkpoint)
{
    if (journal == NULL) {
        return;
    }
    journal_action_list_run_reverse(
        &journal->rollback, edit, checkpoint.rollback_count);
    if (journal->commit.count > checkpoint.commit_count) {
        journal->commit.count = checkpoint.commit_count;
    }
    journal_action_list_run_reverse(
        &journal->cleanup, edit, checkpoint.cleanup_count);
}

void nmo_workspace_edit_journal_rollback_all(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit)
{
    if (journal != NULL) {
        journal_action_list_run_reverse(&journal->rollback, edit, 0u);
    }
}

nmo_status_t nmo_workspace_edit_journal_run_commit(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit)
{
    if (journal == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < journal->commit.count; i++) {
        nmo_workspace_edit_action_t action = journal->commit.items[i];
        nmo_status_t status = action.fn(edit, action.payload);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}

void nmo_workspace_edit_journal_cleanup_all(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit)
{
    if (journal != NULL) {
        journal_action_list_run_reverse(&journal->cleanup, edit, 0u);
    }
}

static nmo_status_t rollback_field_bytes(
    nmo_workspace_edit_t *edit,
    void *payload)
{
    (void)edit;
    field_bytes_snapshot_t *snapshot = (field_bytes_snapshot_t *)payload;
    if (snapshot == NULL || snapshot->field_ptr == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memcpy(snapshot->field_ptr, snapshot->bytes, snapshot->size);
    return NMO_OK;
}

nmo_status_t nmo_workspace_edit_journal_snapshot_bytes(
    nmo_workspace_edit_journal_t *journal,
    nmo_workspace_edit_t *edit,
    void *target,
    size_t size)
{
    if (journal == NULL || edit == NULL || target == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    field_bytes_snapshot_t *snapshot =
        (field_bytes_snapshot_t *)nmo_workspace_edit_journal_alloc(
            journal,
            sizeof(*snapshot) + size,
            _Alignof(field_bytes_snapshot_t));
    if (snapshot == NULL) {
        return NMO_ERR_NOMEM;
    }
    snapshot->field_ptr = target;
    snapshot->size = size;
    if (size > 0u) {
        memcpy(snapshot->bytes, target, size);
    }

    return nmo_workspace_edit_journal_push(
        journal,
        NMO_WORKSPACE_EDIT_ACTION_ROLLBACK,
        rollback_field_bytes,
        snapshot);
}
