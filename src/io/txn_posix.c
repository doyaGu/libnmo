/**
 * @file txn_posix.c
 * @brief Transactional IO operations implementation (POSIX)
 *
 * Implements atomic file writes using:
 * - Temporary files in staging directory
 * - Write-through or buffered writes
 * - fsync/fdatasync for durability
 * - Atomic rename for commit
 */

// Enable POSIX extensions for strdup, mkstemp, fdatasync, etc.
#define _POSIX_C_SOURCE 200809L

#include "io/nmo_txn.h"
#include "core/nmo_allocator_t.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>
#include <stdalign.h>

/**
 * @brief Transaction state
 */
typedef enum nmo_txn_state {
    NMO_TXN_STATE_IDLE = 0,
    NMO_TXN_STATE_ACTIVE,
    NMO_TXN_STATE_COMMITTED,
    NMO_TXN_STATE_ROLLED_BACK,
} nmo_txn_state_t;

/**
 * @brief Transaction handle structure
 */
struct nmo_txn_handle {
    int fd;                        /**< File descriptor for temp file */
    char *final_path;              /**< Final file path (allocated) */
    char *temp_path;               /**< Temporary file path (allocated) */
    nmo_txn_durability durability; /**< Durability mode */
    nmo_txn_state_t state;           /**< Current transaction state */
    nmo_allocator_t allocator;       /**< Allocator for memory management */
} nmo_txn_handle_t;

/**
 * @brief Get directory path from file path
 *
 * @param path File path
 * @param dir_buf Buffer for directory path
 * @param dir_size Size of directory buffer
 * @return 0 on success, -1 on error
 */
static int get_dir_path(const char *path, char *dir_buf, size_t dir_size) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }

    char *dir = dirname(path_copy);
    if (strlen(dir) >= dir_size) {
        free(path_copy);
        return -1;
    }

    strcpy(dir_buf, dir);
    free(path_copy);
    return 0;
}

/**
 * @brief Get base filename from path
 *
 * @param path File path
 * @param base_buf Buffer for base filename
 * @param base_size Size of base buffer
 * @return 0 on success, -1 on error
 */
static int get_base_name(const char *path, char *base_buf, size_t base_size) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        return -1;
    }

    char *base = basename(path_copy);
    if (strlen(base) >= base_size) {
        free(path_copy);
        return -1;
    }

    strcpy(base_buf, base);
    free(path_copy);
    return 0;
}

/**
 * @brief Create temporary file with secure naming
 *
 * Creates a temporary file with naming: .{basename}.{pid}.XXXXXX.tmp
 * Uses mkstemp() for secure creation.
 *
 * @param dir Directory for temp file
 * @param basename Base filename
 * @param temp_path_buf Buffer for temp path (must be writable)
 * @param temp_path_size Size of temp path buffer
 * @return File descriptor or -1 on error
 */
static int create_temp_file(const char *dir, const char *basename,
                            char *temp_path_buf, size_t temp_path_size) {
    pid_t pid = getpid();

    // Create template: dir/.basename.pid.XXXXXX.tmp
    int n = snprintf(temp_path_buf, temp_path_size,
                     "%s/.%s.%d.XXXXXX.tmp", dir, basename, (int) pid);

    if (n < 0 || (size_t) n >= temp_path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Find the XXXXXX part and replace .tmp with nothing for mkstemp
    char *tmp_suffix = strrchr(temp_path_buf, '.');
    if (!tmp_suffix || strcmp(tmp_suffix, ".tmp") != 0) {
        errno = EINVAL;
        return -1;
    }

    // Temporarily remove .tmp suffix for mkstemp
    *tmp_suffix = '\0';

    // Create temp file with mkstemp
    int fd = mkstemp(temp_path_buf);

    if (fd >= 0) {
        // Rename to add .tmp suffix back
        char final_temp[PATH_MAX];
        size_t base_len = strlen(temp_path_buf);
        if (base_len + 4 >= PATH_MAX) {
            close(fd);
            unlink(temp_path_buf);
            errno = ENAMETOOLONG;
            return -1;
        }
        snprintf(final_temp, sizeof(final_temp), "%s.tmp", temp_path_buf);

        if (rename(temp_path_buf, final_temp) != 0) {
            int saved_errno = errno;
            close(fd);
            unlink(temp_path_buf);
            errno = saved_errno;
            return -1;
        }

        // Update the buffer with final name
        if (strlen(final_temp) >= temp_path_size) {
            close(fd);
            unlink(final_temp);
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(temp_path_buf, final_temp);
    }

    return fd;
}

/**
 * @brief Open a new transaction for atomic file write
 */
nmo_txn_handle_t *nmo_txn_open(const nmo_txn_desc_t *desc) {
    if (!desc || !desc->path) {
        errno = EINVAL;
        return NULL;
    }

    // Get allocator
    nmo_allocator_t allocator = nmo_allocator_t_default();

    // Allocate transaction handle
    nmo_txn_handle_t *txn = (nmo_txn_handle_t *) nmo_alloc(&allocator,
                                                       sizeof(nmo_txn_handle_t),
                                                       _Alignof(nmo_txn_handle_t));
    if (!txn) {
        return NULL;
    }

    memset(txn, 0, sizeof(*txn));
    txn->allocator = allocator;
    txn->fd = -1;
    txn->state = NMO_TXN_STATE_IDLE;
    txn->durability = desc->durability;

    // Allocate and copy final path
    size_t path_len = strlen(desc->path);
    txn->final_path = (char *) nmo_alloc(&allocator, path_len + 1, 1);
    if (!txn->final_path) {
        nmo_free(&allocator, txn);
        return NULL;
    }
    strcpy(txn->final_path, desc->path);

    // Determine staging directory
    char staging_dir[PATH_MAX];
    if (desc->staging_dir) {
        if (strlen(desc->staging_dir) >= sizeof(staging_dir)) {
            errno = ENAMETOOLONG;
            goto error;
        }
        strcpy(staging_dir, desc->staging_dir);
    } else {
        // Use directory of final path
        if (get_dir_path(desc->path, staging_dir, sizeof(staging_dir)) != 0) {
            goto error;
        }
    }

    // Get base filename
    char base_name[PATH_MAX];
    if (get_base_name(desc->path, base_name, sizeof(base_name)) != 0) {
        goto error;
    }

    // Create temporary file path buffer
    char temp_path[PATH_MAX];
    txn->fd = create_temp_file(staging_dir, base_name, temp_path, sizeof(temp_path));
    if (txn->fd < 0) {
        goto error;
    }

    // Allocate and copy temp path
    size_t temp_len = strlen(temp_path);
    txn->temp_path = (char *) nmo_alloc(&allocator, temp_len + 1, 1);
    if (!txn->temp_path) {
        close(txn->fd);
        unlink(temp_path);
        goto error;
    }
    strcpy(txn->temp_path, temp_path);

    // Set permissions (try to match final file if it exists, else 0644)
    struct stat st;
    mode_t mode = 0644;
    if (stat(desc->path, &st) == 0) {
        mode = st.st_mode & 0777;
    }
    fchmod(txn->fd, mode);

    txn->state = NMO_TXN_STATE_ACTIVE;
    return txn;

error:
    if (txn->final_path) {
        nmo_free(&allocator, txn->final_path);
    }
    if (txn->temp_path) {
        nmo_free(&allocator, txn->temp_path);
    }
    nmo_free(&allocator, txn);
    return NULL;
}

/**
 * @brief Write data to transaction
 */
nmo_result_t nmo_txn_write(nmo_txn_handle_t *txn, const void *data, size_t size) {
    if (!txn) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                   NMO_SEVERITY_ERROR, "Transaction handle is NULL");
        return nmo_result_error(err);
    }

    if (!data && size > 0) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                   NMO_SEVERITY_ERROR, "Data pointer is NULL");
        return nmo_result_error(err);
    }

    if (txn->state != NMO_TXN_STATE_ACTIVE) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                   NMO_SEVERITY_ERROR, "Transaction is not active");
        return nmo_result_error(err);
    }

    if (txn->fd < 0) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                   NMO_SEVERITY_ERROR, "File descriptor is invalid");
        return nmo_result_error(err);
    }

    // Write data
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t n = write(txn->fd,
                          (const char *) data + total_written,
                          size - total_written);

        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrupted, retry
            }

            nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                       NMO_SEVERITY_ERROR, "Failed to write to temporary file");
            return nmo_result_error(err);
        }

        if (n == 0) {
            // Unexpected EOF (should not happen with regular files)
            nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                       NMO_SEVERITY_ERROR, "Unexpected EOF while writing");
            return nmo_result_error(err);
        }

        total_written += (size_t) n;
    }

    return nmo_result_ok();
}

/**
 * @brief Commit transaction atomically
 */
nmo_result_t nmo_txn_commit(nmo_txn_handle_t *txn) {
    if (!txn) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                   NMO_SEVERITY_ERROR, "Transaction handle is NULL");
        return nmo_result_error(err);
    }

    if (txn->state != NMO_TXN_STATE_ACTIVE) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                   NMO_SEVERITY_ERROR, "Transaction is not active");
        return nmo_result_error(err);
    }

    if (txn->fd < 0) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                   NMO_SEVERITY_ERROR, "File descriptor is invalid");
        return nmo_result_error(err);
    }

    // Sync data to disk based on durability setting
    int sync_result = 0;
    switch (txn->durability) {
    case NMO_TXN_FSYNC:
        // Full sync of data and metadata
        sync_result = fsync(txn->fd);
        break;

    case NMO_TXN_FDATASYNC:
        // Sync data only (may skip some metadata)
#ifdef __linux__
        sync_result = fdatasync(txn->fd);
#else
        // fdatasync not available, fall back to fsync
        sync_result = fsync(txn->fd);
#endif
        break;

    case NMO_TXN_NONE:
        // No explicit sync
        sync_result = 0;
        break;

    default:
        sync_result = 0;
        break;
    }

    if (sync_result != 0) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                   NMO_SEVERITY_ERROR, "Failed to sync file to disk");
        return nmo_result_error(err);
    }

    // Close the file descriptor before rename
    if (close(txn->fd) != 0) {
        txn->fd = -1; // Mark as closed even if close failed
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                   NMO_SEVERITY_ERROR, "Failed to close temporary file");
        return nmo_result_error(err);
    }
    txn->fd = -1;

    // Atomic rename
    if (rename(txn->temp_path, txn->final_path) != 0) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                   NMO_SEVERITY_ERROR, "Failed to rename temporary file to final path");
        return nmo_result_error(err);
    }

    txn->state = NMO_TXN_STATE_COMMITTED;
    return nmo_result_ok();
}

/**
 * @brief Rollback transaction and discard changes
 */
nmo_result_t nmo_txn_rollback(nmo_txn_handle_t *txn) {
    if (!txn) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                   NMO_SEVERITY_ERROR, "Transaction handle is NULL");
        return nmo_result_error(err);
    }

    if (txn->state != NMO_TXN_STATE_ACTIVE) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                   NMO_SEVERITY_ERROR, "Transaction is not active");
        return nmo_result_error(err);
    }

    // Close file descriptor if open
    if (txn->fd >= 0) {
        close(txn->fd);
        txn->fd = -1;
    }

    // Delete temporary file
    if (txn->temp_path) {
        unlink(txn->temp_path);
    }

    txn->state = NMO_TXN_STATE_ROLLED_BACK;
    return nmo_result_ok();
}

/**
 * @brief Close transaction and free resources
 */
void nmo_txn_close(nmo_txn_handle_t *txn) {
    if (!txn) {
        return;
    }

    // If still active, perform implicit rollback
    if (txn->state == NMO_TXN_STATE_ACTIVE) {
        if (txn->fd >= 0) {
            close(txn->fd);
            txn->fd = -1;
        }

        if (txn->temp_path) {
            unlink(txn->temp_path);
        }
    }

    // Free allocated strings
    if (txn->final_path) {
        nmo_free(&txn->allocator, txn->final_path);
    }

    if (txn->temp_path) {
        nmo_free(&txn->allocator, txn->temp_path);
    }

    // Free handle
    nmo_free(&txn->allocator, txn);
}
