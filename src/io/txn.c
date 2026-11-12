/**
 * @file txn.c
 * @brief Transactional file writes: the bytes go to a new temporary file
 *        next to the target (or in the staging directory), which replaces
 *        the target at once on commit and goes on rollback.
 *
 * The temporary file is named .{basename}.{pid}.{n}.tmp and made only if no
 * file has that name. Durability: NMO_TXN_FSYNC syncs the file, then the
 * directory it was moved into; NMO_TXN_FDATASYNC syncs the file's data;
 * NMO_TXN_NONE leaves it to the system. The operating system's part is the
 * platform layer's.
 */

#include "io/nmo_txn.h"
#include "core/nmo_allocator.h"
#include "../platform/platform.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Names tried for the temporary file before giving up */
#define TXN_NAME_ATTEMPTS 64u

typedef enum nmo_txn_state {
    NMO_TXN_STATE_IDLE = 0,
    NMO_TXN_STATE_ACTIVE,
    NMO_TXN_STATE_COMMITTED,
    NMO_TXN_STATE_ROLLED_BACK,
} nmo_txn_state_t;

struct nmo_txn_handle {
    nmo_platform_file_t *file;       /**< The temporary file; NULL once closed */
    char *final_path;                /**< Where it goes on commit */
    char *temp_path;                 /**< Where it is meanwhile */
    nmo_txn_durability_t durability;
    nmo_txn_state_t state;
    nmo_allocator_t allocator;
};

/* A copy of the first `length` bytes of `text`, ended, from the allocator */
static char *txn_copy(nmo_allocator_t *allocator, const char *text, size_t length)
{
    char *copy = (char *)nmo_alloc(allocator, length + 1, 1);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/* Where the path's file name starts: after its last separator */
static size_t txn_name_start(const char *path)
{
    size_t start = 0;
    for (size_t i = 0; path[i] != '\0'; i++) {
        if (nmo_platform_is_separator(path[i])) {
            start = i + 1;
        }
    }
    return start;
}

/* The directory of a path: what is before its file name ("." for none, the root kept) */
static char *txn_directory(nmo_allocator_t *allocator, const char *path)
{
    size_t start = txn_name_start(path);
    if (start == 0) {
        return txn_copy(allocator, ".", 1);
    }
    return txn_copy(allocator, path, start > 1 ? start - 1 : start);
}

/* dir/.base.pid.n.tmp, from the allocator */
static char *txn_temp_name(nmo_allocator_t *allocator, const char *dir, const char *base,
                           unsigned long pid, unsigned long n)
{
    const char separator[2] = {nmo_platform_separator(), '\0'};
    size_t dir_length = strlen(dir);
    bool ends_with_separator = dir_length > 0 && nmo_platform_is_separator(dir[dir_length - 1]);
    const char *joint = ends_with_separator ? "" : separator;
    int length = snprintf(NULL, 0, "%s%s.%s.%lu.%lu.tmp", dir, joint, base, pid, n);
    if (length < 0) {
        return NULL;
    }
    char *name = (char *)nmo_alloc(allocator, (size_t)length + 1, 1);
    if (name != NULL) {
        snprintf(name, (size_t)length + 1, "%s%s.%s.%lu.%lu.tmp", dir, joint, base, pid, n);
    }
    return name;
}

/* The temporary file: the first name of a few no file has */
static nmo_platform_file_t *txn_create_temp(nmo_txn_handle_t *txn, const char *dir, const char *base)
{
    unsigned long pid = nmo_platform_process_id();
    unsigned long seed = (unsigned long)(nmo_platform_ticks() & 0xFFFFFFFFu);
    for (unsigned long attempt = 0; attempt < TXN_NAME_ATTEMPTS; attempt++) {
        char *name = txn_temp_name(&txn->allocator, dir, base, pid, (seed + attempt * 7919u) & 0xFFFFFFFFu);
        if (name == NULL) {
            return NULL;
        }
        nmo_platform_file_t *file = NULL;
        int made = nmo_platform_file_create_new(name, txn->final_path, &file);
        if (made == 0) {
            txn->temp_path = name;
            return file;
        }
        nmo_free(&txn->allocator, name);
        if (made < 0) {
            return NULL;
        }
    }
    return NULL;
}

nmo_txn_handle_t *nmo_txn_open(const nmo_txn_desc_t *desc) {
    if (desc == NULL || desc->path == NULL || desc->path[0] == '\0') {
        return NULL;
    }
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_txn_handle_t *txn = (nmo_txn_handle_t *)nmo_alloc(&allocator, sizeof(nmo_txn_handle_t),
                                                           _Alignof(nmo_txn_handle_t));
    if (txn == NULL) {
        return NULL;
    }
    memset(txn, 0, sizeof(*txn));
    txn->allocator = allocator;
    txn->state = NMO_TXN_STATE_IDLE;
    txn->durability = desc->durability;
    txn->final_path = txn_copy(&txn->allocator, desc->path, strlen(desc->path));
    char *dir = desc->staging_dir != NULL ? txn_copy(&txn->allocator, desc->staging_dir, strlen(desc->staging_dir))
                                          : txn_directory(&txn->allocator, desc->path);
    if (txn->final_path != NULL && dir != NULL) {
        txn->file = txn_create_temp(txn, dir, desc->path + txn_name_start(desc->path));
    }
    if (dir != NULL) {
        nmo_free(&txn->allocator, dir);
    }
    if (txn->file == NULL) {
        if (txn->final_path != NULL) {
            nmo_free(&txn->allocator, txn->final_path);
        }
        nmo_free(&txn->allocator, txn);
        return NULL;
    }
    txn->state = NMO_TXN_STATE_ACTIVE;
    return txn;
}

nmo_status_t nmo_txn_write(nmo_txn_handle_t *txn, const void *data, size_t size) {
    if (txn == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Transaction handle is NULL");
    }
    if (data == NULL && size > 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Data pointer is NULL");
    }
    if (txn->state != NMO_TXN_STATE_ACTIVE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Transaction is not active");
    }
    if (txn->file == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "The temporary file is closed");
    }
    if (size > 0 && nmo_platform_file_write(txn->file, data, size) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR, "Failed to write to temporary file");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_txn_commit(nmo_txn_handle_t *txn) {
    if (txn == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Transaction handle is NULL");
    }
    if (txn->state != NMO_TXN_STATE_ACTIVE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Transaction is not active");
    }
    if (txn->file == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "The temporary file is closed");
    }
    if (txn->durability != NMO_TXN_NONE &&
        nmo_platform_file_sync(txn->file, txn->durability == NMO_TXN_FDATASYNC) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR, "Failed to sync file to disk");
    }
    int closed = nmo_platform_file_close(txn->file);
    txn->file = NULL;
    if (closed != 0) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR, "Failed to close temporary file");
    }
    if (nmo_platform_replace(txn->temp_path, txn->final_path, txn->durability != NMO_TXN_NONE) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR,
                         "Failed to rename temporary file to final path");
    }
    txn->state = NMO_TXN_STATE_COMMITTED;
    if (txn->durability == NMO_TXN_FSYNC) {
        char *dir = txn_directory(&txn->allocator, txn->final_path);
        int synced = dir != NULL ? nmo_platform_sync_directory(dir) : -1;
        if (dir != NULL) {
            nmo_free(&txn->allocator, dir);
        }
        if (synced != 0) {
            NMO_RETURN_ERROR(NMO_ERR_CANT_WRITE_FILE, NMO_SEVERITY_ERROR,
                             "Failed to sync destination directory");
        }
    }
    NMO_RETURN_OK();
}

/* The temporary file closed and gone */
static void txn_discard(nmo_txn_handle_t *txn)
{
    if (txn->file != NULL) {
        (void)nmo_platform_file_close(txn->file);
        txn->file = NULL;
    }
    if (txn->temp_path != NULL) {
        (void)nmo_platform_remove(txn->temp_path);
    }
}

nmo_status_t nmo_txn_rollback(nmo_txn_handle_t *txn) {
    if (txn == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Transaction handle is NULL");
    }
    if (txn->state != NMO_TXN_STATE_ACTIVE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Transaction is not active");
    }
    txn_discard(txn);
    txn->state = NMO_TXN_STATE_ROLLED_BACK;
    NMO_RETURN_OK();
}

void nmo_txn_close(nmo_txn_handle_t *txn) {
    if (txn == NULL) {
        return;
    }
    if (txn->state == NMO_TXN_STATE_ACTIVE) { /* an implicit rollback */
        txn_discard(txn);
    }
    if (txn->final_path != NULL) {
        nmo_free(&txn->allocator, txn->final_path);
    }
    if (txn->temp_path != NULL) {
        nmo_free(&txn->allocator, txn->temp_path);
    }
    nmo_free(&txn->allocator, txn);
}
