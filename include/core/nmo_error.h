#ifndef NMO_ERROR_H
#define NMO_ERROR_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_error.h
 * @brief Error handling with error chains
 *
 * Provides a comprehensive error handling system with:
 * - Error codes for different failure modes
 * - Severity levels
 * - Error chains for causal analysis
 * - File/line tracking for debugging
 */

/**
 * @brief Error codes
 */
typedef enum nmo_error_code {
    NMO_OK = 0,                   /**< Success */
    NMO_ERR_NOMEM,                /**< Out of memory */
    NMO_ERR_BUFFER_OVERRUN,       /**< Buffer overrun */
    NMO_ERR_FILE_NOT_FOUND,       /**< File not found */
    NMO_ERR_CANT_OPEN_FILE,       /**< Cannot open file */
    NMO_ERR_CANT_READ_FILE,       /**< Cannot read file */
    NMO_ERR_CANT_WRITE_FILE,      /**< Cannot write file */
    NMO_ERR_INVALID_SIGNATURE,    /**< Invalid file signature */
    NMO_ERR_UNSUPPORTED_VERSION,  /**< Unsupported file version */
    NMO_ERR_CHECKSUM_MISMATCH,    /**< Checksum mismatch */
    NMO_ERR_DECOMPRESSION_FAILED, /**< Decompression failed */
    NMO_ERR_COMPRESSION_FAILED,   /**< Compression failed */
    NMO_ERR_VALIDATION_FAILED,    /**< Validation failed */
    NMO_ERR_INVALID_FORMAT,       /**< Invalid format */
    NMO_ERR_INVALID_OFFSET,       /**< Invalid offset */
    NMO_ERR_EOF,                  /**< Unexpected end of file */
    NMO_ERR_INVALID_ARGUMENT,     /**< Invalid argument */
    NMO_ERR_INVALID_STATE,        /**< Invalid state */
    NMO_ERR_NOT_IMPLEMENTED,      /**< Not implemented */
    NMO_ERR_NOT_SUPPORTED,        /**< Operation not supported */
    NMO_ERR_UNKNOWN,              /**< Unknown error */
    NMO_ERR_INTERNAL,             /**< Internal error */
    NMO_ERR_OUT_OF_BOUNDS,        /**< Index out of bounds */
    NMO_ERR_NOT_FOUND,            /**< Item not found */
    NMO_ERR_ALREADY_EXISTS,       /**< Item already exists */
    NMO_ERR_CORRUPT,              /**< Corrupted data */
    NMO_ERR_CANCELLED,            /**< Operation cancelled */
} nmo_error_code_t;

/**
 * @brief Error severity levels
 */
typedef enum nmo_severity {
    NMO_SEVERITY_DEBUG,   /**< Debug information */
    NMO_SEVERITY_INFO,    /**< Informational */
    NMO_SEVERITY_WARNING, /**< Warning (recoverable) */
    NMO_SEVERITY_ERROR,   /**< Error (not recoverable) */
    NMO_SEVERITY_FATAL,   /**< Fatal error (abort) */
} nmo_severity_t;

/**
 * @brief Convert an error code to a human-readable string
 * @param code Error code
 * @return Error message string
 */
NMO_API const char *nmo_error_string(nmo_status_t code);

/* =============================================================================
 * TLS Last-Error API (ABI Stable)
 * =============================================================================
 * Thread-local storage for detailed error information.
 * - On failure: set last-error before returning non-OK status
 * - On success: clear last-error before returning NMO_OK
 * ============================================================================= */

/**
 * @brief Clear thread-local last-error state
 * 
 * Called automatically on successful API returns.
 */
NMO_API void nmo_last_error_clear(void);

/**
 * @brief Get the last error code for this thread
 * @return Error code, or NMO_OK if no error is set
 */
NMO_API nmo_error_code_t nmo_last_error_code(void);

/**
 * @brief Get the last error severity for this thread
 * @return Severity level, or NMO_SEVERITY_DEBUG if no error is set
 */
NMO_API nmo_severity_t nmo_last_error_severity(void);

/**
 * @brief Get the source file where the last error occurred
 * @return File name, or NULL if no error is set
 * @note Valid until next libnmo call on this thread
 */
NMO_API const char *nmo_last_error_file(void);

/**
 * @brief Get the source line where the last error occurred
 * @return Line number, or 0 if no error is set
 */
NMO_API int nmo_last_error_line(void);

/**
 * @brief Get the last error message for this thread
 * @return Error message, or empty string if no error is set
 * @note Valid until next libnmo call on this thread
 */
NMO_API const char *nmo_last_error_message(void);

/**
 * @brief Copy the last error message to a buffer
 * @param dst Destination buffer (can be NULL if cap == 0)
 * @param cap Buffer capacity in bytes
 * @return Number of bytes needed (excluding NUL terminator)
 * @note Always NUL-terminates if cap > 0, even if truncated
 */
NMO_API size_t nmo_last_error_message_copy(char *dst, size_t cap);

/**
 * @brief Copy the full error chain to a buffer
 * @param dst Destination buffer (can be NULL if cap == 0)
 * @param cap Buffer capacity in bytes
 * @return Number of bytes needed (excluding NUL terminator)
 * @note Always NUL-terminates if cap > 0, even if truncated
 */
NMO_API size_t nmo_last_error_chain_copy(char *dst, size_t cap);

/**
 * @brief Set last-error with formatted message (internal use)
 * @param code Error code
 * @param severity Severity level
 * @param file Source file
 * @param line Source line
 * @param fmt Format string
 * @param ... Format arguments
 */
NMO_API void nmo_last_error_setf(nmo_error_code_t code,
                                  nmo_severity_t severity,
                                  const char *file,
                                  int line,
                                  const char *fmt, ...);

/* =============================================================================
 * New-style macros for nmo_status_t returns
 * ============================================================================= */

/**
 * @brief Set last-error (does not return)
 */
#define NMO_SET_LAST_ERROR(code, severity, ...) \
    nmo_last_error_setf((code), (severity), __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Set last-error and return the error code
 */
#define NMO_RETURN_ERROR(code, severity, ...) \
    do { \
        nmo_last_error_setf((code), (severity), __FILE__, __LINE__, __VA_ARGS__); \
        return (nmo_status_t)(code); \
    } while (0)

/**
 * @brief Clear last-error and return NMO_OK
 */
#define NMO_RETURN_OK() \
    do { \
        nmo_last_error_clear(); \
        return NMO_OK; \
    } while (0)

/**
 * @brief Check status and propagate error if not OK
 */
#define NMO_RETURN_IF_ERROR(status) \
    do { \
        nmo_status_t _s = (status); \
        if (_s != NMO_OK) { \
            return _s; \
        } \
    } while (0)

/**
 * @brief Check status, run cleanup, and propagate error if not OK
 */
#define NMO_RETURN_IF_ERROR_DO(status, action) \
    do { \
        nmo_status_t _s = (status); \
        if (_s != NMO_OK) { \
            action; \
            return _s; \
        } \
    } while (0)

/**
 * @brief Check status and set contextual error before returning
 */
#define NMO_RETURN_IF_ERROR_CTX(status, ...) \
    do { \
        nmo_status_t _s = (status); \
        if (_s != NMO_OK) { \
            nmo_last_error_setf((nmo_error_code_t)_s, NMO_SEVERITY_ERROR, __FILE__, __LINE__, __VA_ARGS__); \
            return _s; \
        } \
    } while (0)

/**
 * @brief Ensure condition is true, otherwise set last error and return code
 */
#define NMO_ENSURE(condition, code, severity, ...) \
    do { \
        if (!(condition)) { \
            NMO_RETURN_ERROR((code), (severity), __VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief Check status and return NULL if not OK
 */
#define NMO_RETURN_NULL_IF_ERROR(status) \
    do { \
        nmo_status_t _s = (status); \
        if (_s != NMO_OK) { \
            return NULL; \
        } \
    } while (0)

/**
 * @brief Check status, run cleanup, and return NULL if not OK
 */
#define NMO_RETURN_NULL_IF_ERROR_DO(status, action) \
    do { \
        nmo_status_t _s = (status); \
        if (_s != NMO_OK) { \
            action; \
            return NULL; \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif // NMO_ERROR_H
