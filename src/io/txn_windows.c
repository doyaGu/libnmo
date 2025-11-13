/**
 * @file txn_windows.c
 * @brief Transactional IO operations implementation (Windows)
 *
 * Implements atomic file writes using:
 * - Temporary files with FILE_ATTRIBUTE_TEMPORARY
 * - Write-through or buffered writes
 * - FlushFileBuffers for durability
 * - MoveFileEx with MOVEFILE_REPLACE_EXISTING for atomic commit
 */

#include "io/nmo_txn.h"
#include "core/nmo_allocator.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * @brief Convert UTF-8 string to UTF-16 (wide char)
 *
 * @param utf8 UTF-8 string
 * @param allocator Allocator for memory
 * @return Allocated UTF-16 string or NULL on error
 */
static wchar_t* utf8_to_utf16(const char* utf8, nmo_allocator_t* allocator) {
    if (!utf8) {
        return NULL;
    }

    // Get required buffer size
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (size <= 0) {
        return NULL;
    }

    // Allocate buffer
    wchar_t* wstr = (wchar_t*)nmo_alloc(allocator, size * sizeof(wchar_t), _Alignof(wchar_t));
    if (!wstr) {
        return NULL;
    }

    // Perform conversion
    int result = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wstr, size);
    if (result <= 0) {
        nmo_free(allocator, wstr);
        return NULL;
    }

    return wstr;
}

/**
 * @brief Convert UTF-16 (wide char) to UTF-8 string
 *
 * @param utf16 UTF-16 string
 * @param allocator Allocator for memory
 * @return Allocated UTF-8 string or NULL on error
 */
static char* utf16_to_utf8(const wchar_t* utf16, nmo_allocator_t* allocator) {
    if (!utf16) {
        return NULL;
    }

    // Get required buffer size
    int size = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return NULL;
    }

    // Allocate buffer
    char* str = (char*)nmo_alloc(allocator, size, 1);
    if (!str) {
        return NULL;
    }

    // Perform conversion
    int result = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, str, size, NULL, NULL);
    if (result <= 0) {
        nmo_free(allocator, str);
        return NULL;
    }

    return str;
}

/**
 * @brief Transaction handle structure
 */
struct nmo_txn_handle {
    HANDLE file_handle;            /**< Windows file handle */
    char *final_path;              /**< Final file path (allocated) */
    char *temp_path;               /**< Temporary file path (allocated) */
    nmo_txn_durability_t durability; /**< Durability mode */
    nmo_txn_state_t state;           /**< Current transaction state */
    nmo_allocator_t allocator;     /**< Allocator for memory management */
};

/**
 * @brief Get directory path from file path (UTF-8)
 *
 * @param path File path in UTF-8
 * @param dir_buf Buffer for directory path (UTF-8)
 * @param dir_size Size of directory buffer
 * @return 0 on success, -1 on error
 */
static int get_dir_path(const char *path, char *dir_buf, size_t dir_size) {
    char *path_copy = _strdup(path);
    if (!path_copy) {
        return -1;
    }

    // Find last backslash or forward slash (safe for UTF-8 multi-byte chars)
    char *last_sep = NULL;
    char *p = path_copy;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            last_sep = p;
        }
        // Skip UTF-8 continuation bytes
        if ((*p & 0x80) == 0) {
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            p += 4;
        } else {
            p++;
        }
    }

    if (last_sep) {
        *last_sep = '\0';
        if (strlen(path_copy) >= dir_size) {
            free(path_copy);
            return -1;
        }
        strcpy(dir_buf, path_copy);
    } else {
        // No directory, use current directory
        if (dir_size < 2) {
            free(path_copy);
            return -1;
        }
        strcpy(dir_buf, ".");
    }

    free(path_copy);
    return 0;
}

/**
 * @brief Get base filename from path (UTF-8)
 *
 * @param path File path in UTF-8
 * @param base_buf Buffer for base filename (UTF-8)
 * @param base_size Size of base buffer
 * @return 0 on success, -1 on error
 */
static int get_base_name(const char *path, char *base_buf, size_t base_size) {
    // Find last backslash or forward slash (safe for UTF-8 multi-byte chars)
    const char *last_sep = NULL;
    const char *p = path;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            last_sep = p;
        }
        // Skip UTF-8 continuation bytes
        if ((*p & 0x80) == 0) {
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            p += 4;
        } else {
            p++;
        }
    }

    const char *base = last_sep ? last_sep + 1 : path;

    if (strlen(base) >= base_size) {
        return -1;
    }

    strcpy(base_buf, base);
    return 0;
}

/**
 * @brief Create temporary file with secure naming (UTF-8)
 *
 * Creates a temporary file with naming: .{basename}.{pid}.{rand}.tmp
 *
 * @param dir Directory for temp file (UTF-8)
 * @param basename Base filename (UTF-8)
 * @param temp_path_buf Buffer for temp path (UTF-8)
 * @param temp_path_size Size of temp path buffer
 * @param allocator Allocator for temporary buffers
 * @return File handle or INVALID_HANDLE_VALUE on error
 */
static HANDLE create_temp_file(const char *dir, const char *basename,
                                char *temp_path_buf, size_t temp_path_size,
                                nmo_allocator_t *allocator) {
    DWORD pid = GetCurrentProcessId();
    unsigned int rand_val = (unsigned int)GetTickCount();

    // Create temporary file name: dir\.basename.pid.rand.tmp
    int n = snprintf(temp_path_buf, temp_path_size,
                     "%s\\.%s.%lu.%u.tmp", dir, basename, pid, rand_val);

    if (n < 0 || (size_t)n >= temp_path_size) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return INVALID_HANDLE_VALUE;
    }

    // Convert UTF-8 path to UTF-16
    wchar_t *wpath = utf8_to_utf16(temp_path_buf, allocator);
    if (!wpath) {
        return INVALID_HANDLE_VALUE;
    }

    // Create the file with appropriate flags using Unicode API
    HANDLE hFile = CreateFileW(
        wpath,
        GENERIC_READ | GENERIC_WRITE,
        0,  // No sharing
        NULL,  // Default security
        CREATE_NEW,  // Must not exist
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        // If file exists, try with a different random value
        if (GetLastError() == ERROR_FILE_EXISTS) {
            rand_val = (unsigned int)(GetTickCount() + rand());
            n = snprintf(temp_path_buf, temp_path_size,
                        "%s\\.%s.%lu.%u.tmp", dir, basename, pid, rand_val);
            if (n >= 0 && (size_t)n < temp_path_size) {
                nmo_free(allocator, wpath);
                wpath = utf8_to_utf16(temp_path_buf, allocator);
                if (wpath) {
                    hFile = CreateFileW(
                        wpath,
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_NEW,
                        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
                        NULL
                    );
                }
            }
        }
    }

    nmo_free(allocator, wpath);
    return hFile;
}

/**
 * @brief Open a new transaction for atomic file write
 */
nmo_txn_handle_t *nmo_txn_open(const nmo_txn_desc_t *desc) {
    if (!desc || !desc->path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    // Get allocator
    nmo_allocator_t allocator = nmo_allocator_default();

    // Allocate transaction handle
    nmo_txn_handle_t *txn = (nmo_txn_handle_t *)nmo_alloc(&allocator,
                                                           sizeof(nmo_txn_handle_t),
                                                           _Alignof(nmo_txn_handle_t));
    if (!txn) {
        return NULL;
    }

    memset(txn, 0, sizeof(*txn));
    txn->allocator = allocator;
    txn->file_handle = INVALID_HANDLE_VALUE;
    txn->state = NMO_TXN_STATE_IDLE;
    txn->durability = desc->durability;

    // Allocate and copy final path
    size_t path_len = strlen(desc->path);
    txn->final_path = (char *)nmo_alloc(&allocator, path_len + 1, 1);
    if (!txn->final_path) {
        nmo_free(&allocator, txn);
        return NULL;
    }
    strcpy(txn->final_path, desc->path);

    // Determine staging directory
    char staging_dir[MAX_PATH];
    if (desc->staging_dir) {
        if (strlen(desc->staging_dir) >= sizeof(staging_dir)) {
            SetLastError(ERROR_BUFFER_OVERFLOW);
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
    char base_name[MAX_PATH];
    if (get_base_name(desc->path, base_name, sizeof(base_name)) != 0) {
        goto error;
    }

    // Create temporary file
    char temp_path[MAX_PATH];
    txn->file_handle = create_temp_file(staging_dir, base_name, temp_path, sizeof(temp_path), &allocator);
    if (txn->file_handle == INVALID_HANDLE_VALUE) {
        goto error;
    }

    // Allocate and copy temp path
    size_t temp_len = strlen(temp_path);
    txn->temp_path = (char *)nmo_alloc(&allocator, temp_len + 1, 1);
    if (!txn->temp_path) {
        CloseHandle(txn->file_handle);
        // Convert to UTF-16 for deletion
        wchar_t *wtemp = utf8_to_utf16(temp_path, &allocator);
        if (wtemp) {
            DeleteFileW(wtemp);
            nmo_free(&allocator, wtemp);
        }
        goto error;
    }
    strcpy(txn->temp_path, temp_path);

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

    if (txn->file_handle == INVALID_HANDLE_VALUE) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                     NMO_SEVERITY_ERROR, "File handle is invalid");
        return nmo_result_error(err);
    }

    // Write data
    size_t total_written = 0;
    while (total_written < size) {
        DWORD to_write = (DWORD)((size - total_written) > 0xFFFFFFFF ? 0xFFFFFFFF : (size - total_written));
        DWORD written = 0;

        BOOL result = WriteFile(txn->file_handle,
                               (const char *)data + total_written,
                               to_write,
                               &written,
                               NULL);

        if (!result) {
            nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                         NMO_SEVERITY_ERROR, "Failed to write to temporary file");
            return nmo_result_error(err);
        }

        if (written == 0) {
            // Unexpected EOF (should not happen with regular files)
            nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                         NMO_SEVERITY_ERROR, "Unexpected EOF while writing");
            return nmo_result_error(err);
        }

        total_written += written;
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

    if (txn->file_handle == INVALID_HANDLE_VALUE) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                     NMO_SEVERITY_ERROR, "File handle is invalid");
        return nmo_result_error(err);
    }

    // Sync data to disk based on durability setting
    BOOL sync_result = TRUE;
    switch (txn->durability) {
    case NMO_TXN_FSYNC:
    case NMO_TXN_FDATASYNC:
        // Windows FlushFileBuffers is equivalent to fsync
        sync_result = FlushFileBuffers(txn->file_handle);
        break;

    case NMO_TXN_NONE:
        // No explicit sync
        sync_result = TRUE;
        break;

    default:
        sync_result = TRUE;
        break;
    }

    if (!sync_result) {
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                     NMO_SEVERITY_ERROR, "Failed to sync file to disk");
        return nmo_result_error(err);
    }

    // Close the file handle before rename
    if (!CloseHandle(txn->file_handle)) {
        txn->file_handle = INVALID_HANDLE_VALUE; // Mark as closed even if close failed
        nmo_error_t *err = NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                     NMO_SEVERITY_ERROR, "Failed to close temporary file");
        return nmo_result_error(err);
    }
    txn->file_handle = INVALID_HANDLE_VALUE;

    // Atomic rename with MoveFileExW (UTF-8 paths converted to UTF-16)
    wchar_t *wtemp = utf8_to_utf16(txn->temp_path, &txn->allocator);
    wchar_t *wfinal = utf8_to_utf16(txn->final_path, &txn->allocator);

    BOOL rename_result = FALSE;
    if (wtemp && wfinal) {
        rename_result = MoveFileExW(wtemp, wfinal,
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    if (wtemp) nmo_free(&txn->allocator, wtemp);
    if (wfinal) nmo_free(&txn->allocator, wfinal);

    if (!rename_result) {
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

    // Close file handle if open
    if (txn->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(txn->file_handle);
        txn->file_handle = INVALID_HANDLE_VALUE;
    }

    // Delete temporary file (convert UTF-8 to UTF-16)
    if (txn->temp_path) {
        wchar_t *wtemp = utf8_to_utf16(txn->temp_path, &txn->allocator);
        if (wtemp) {
            DeleteFileW(wtemp);
            nmo_free(&txn->allocator, wtemp);
        }
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
        if (txn->file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(txn->file_handle);
            txn->file_handle = INVALID_HANDLE_VALUE;
        }

        if (txn->temp_path) {
            wchar_t *wtemp = utf8_to_utf16(txn->temp_path, &txn->allocator);
            if (wtemp) {
                DeleteFileW(wtemp);
                nmo_free(&txn->allocator, wtemp);
            }
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
