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

// Forward declarations
typedef struct nmo_error nmo_error_t;
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Error structure with causal chain
 */
typedef struct nmo_error {
    nmo_error_code_t code;   /**< Error code */
    nmo_severity_t severity; /**< Severity level */
    const char *message;   /**< Error message */
    const char *file;      /**< Source file (for debugging) */
    int line;              /**< Source line (for debugging) */
    nmo_error_t *cause;    /**< Causal error (chain) */
    uint8_t owns_message; /**< Non-zero if message storage should be freed */
    uint8_t from_arena;   /**< Non-zero if error was allocated from an arena */
    uint16_t _reserved;   /**< Reserved for future use/padding */
} nmo_error_t;

/**
 * @brief Result type combining error code and error details
 */
typedef struct nmo_result {
    nmo_error_code_t code; /**< Error code */
    nmo_error_t *error;  /**< Detailed error info (NULL if OK) */
} nmo_result_t;

/**
 * @brief Create error with message
 *
 * @param arena Arena for allocation (NULL for malloc)
 * @param code Error code
 * @param severity Severity level
 * @param message Error message
 * @param file Source file (__FILE__)
 * @param line Source line (__LINE__)
 * @return Error structure or NULL on allocation failure
 */
NMO_API nmo_error_t *nmo_error_create(nmo_arena_t *arena,
                                      nmo_error_code_t code,
                                      nmo_severity_t severity,
                                      const char *message,
                                      const char *file,
                                      int line);

/**
 * @brief Create formatted error with message and explicit file/line
 *
 * Allocates storage for the formatted message using the provided arena or
 * the default allocator when arena is NULL.
 *
 * @param arena Arena for allocation (NULL for malloc)
 * @param code Error code
 * @param severity Severity level
 * @param file Source file (__FILE__)
 * @param line Source line (__LINE__)
 * @param fmt printf-style format string
 * @param ... Arguments for format string
 * @return Error structure or NULL on allocation failure
 */
NMO_API nmo_error_t *nmo_error_createf_at(nmo_arena_t *arena,
                                         nmo_error_code_t code,
                                         nmo_severity_t severity,
                                         const char *file,
                                         int line,
                                         const char *fmt, ...);

/**
 * @brief Add causal error to error chain
 *
 * @param error Parent error
 * @param cause Causal error
 */
NMO_API void nmo_error_add_cause(nmo_error_t *error, nmo_error_t *cause);

/**
 * @brief Free a non-arena error chain created by the error helpers.
 *
 * Errors allocated from an arena are not freed (arena owns the memory).
 */
NMO_API void nmo_error_free(nmo_error_t *error);

/**
 * @brief Get error message for error code
 *
 * @param code Error code
 * @return Error message string
 */
NMO_API const char *nmo_error_string(nmo_error_code_t code);

/**
 * @brief Create success result
 *
 * @return Success result
 */
NMO_API nmo_result_t nmo_result_ok(void);

/**
 * @brief Create error result
 *
 * @param error Error details
 * @return Error result
 */
NMO_API nmo_result_t nmo_result_error(nmo_error_t *error);

/**
 * @brief Create formatted error result with printf-style formatting
 *
 * Allocates storage for the error message using the provided arena or the
 * default allocator when arena is NULL.
 *
 * @param arena Arena for allocations (optional)
 * @param code Error code
 * @param severity Severity level
 * @param fmt printf-style format string
 * @param ... Arguments for format string
 * @return Result describing the error
 */
NMO_API nmo_result_t nmo_result_errorf(nmo_arena_t *arena,
                                       nmo_error_code_t code,
                                       nmo_severity_t severity,
                                       const char *fmt, ...);

/**
 * @brief Create formatted error result with explicit file/line
 *
 * Allocates storage for the error message using the provided arena or the
 * default allocator when arena is NULL.
 *
 * @param arena Arena for allocations (optional)
 * @param code Error code
 * @param severity Severity level
 * @param file Source file (__FILE__)
 * @param line Source line (__LINE__)
 * @param fmt printf-style format string
 * @param ... Arguments for format string
 * @return Result describing the error
 */
NMO_API nmo_result_t nmo_result_errorf_at(nmo_arena_t *arena,
                                         nmo_error_code_t code,
                                         nmo_severity_t severity,
                                         const char *file,
                                         int line,
                                         const char *fmt, ...);

/**
 * @brief Add context to an error result by wrapping it in a new error node.
 *
 * If @p result is OK, it is returned unchanged.
 * If @p result is an error, a new error is allocated (using @p arena or the
 * default allocator) with the same error code, and the original error is
 * attached as a cause. This makes error chains more actionable without
 * duplicating error handling boilerplate at call sites.
 *
 * @param arena Arena for allocations (optional)
 * @param result Existing result (error or ok)
 * @param file Source file (__FILE__)
 * @param line Source line (__LINE__)
 * @param fmt printf-style context format string
 * @param ... Arguments for format string
 * @return Result with additional context on error
 */
NMO_API nmo_result_t nmo_result_add_contextf_at(nmo_arena_t *arena,
                                               nmo_result_t result,
                                               const char *file,
                                               int line,
                                               const char *fmt, ...);

/**
 * @brief Check whether a result indicates success
 */
static inline int nmo_result_is_ok(nmo_result_t result) {
    return result.code == NMO_OK;
}

/**
 * @brief Check whether a result indicates failure
 */
static inline int nmo_result_is_error(nmo_result_t result) {
    return result.code != NMO_OK;
}

/**
 * @brief Check whether a result indicates a not-found condition
 */
static inline int nmo_result_is_not_found(nmo_result_t result) {
    return result.code == NMO_ERR_NOT_FOUND;
}

// Convenience macros
#define NMO_ERROR(arena, code, severity, message) \
    nmo_error_create(arena, code, severity, message, __FILE__, __LINE__)

#define NMO_ERRORF(arena, code, severity, ...) \
    nmo_error_createf_at((arena), (code), (severity), __FILE__, __LINE__, __VA_ARGS__)

#define nmo_result_errorf(arena, code, severity, ...) \
    nmo_result_errorf_at((arena), (code), (severity), __FILE__, __LINE__, __VA_ARGS__)

#define NMO_RETURN_IF_ERROR(result) \
    do { \
        nmo_result_t _r = (result); \
        if (_r.code != NMO_OK) { \
            return _r; \
        } \
    } while (0)

#define NMO_RETURN_IF_ERROR_DO(result, on_error) \
    do { \
        nmo_result_t _r = (result); \
        if (_r.code != NMO_OK) { \
            on_error; \
            return _r; \
        } \
    } while (0)

/**
 * @brief Evaluate expression returning nmo_result_t; on error, wrap with context and return.
 */
#define NMO_RETURN_IF_ERROR_CTX(arena, expr, ...) \
    do { \
        nmo_result_t _r = (expr); \
        if (_r.code != NMO_OK) { \
            return nmo_result_add_contextf_at((arena), _r, __FILE__, __LINE__, __VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief Evaluate expression returning nmo_result_t; on error, wrap with context and goto label.
 */
#define NMO_GOTO_IF_ERROR_CTX(arena, expr, label, ...) \
    do { \
        nmo_result_t _r = (expr); \
        if (_r.code != NMO_OK) { \
            _r = nmo_result_add_contextf_at((arena), _r, __FILE__, __LINE__, __VA_ARGS__); \
            goto label; \
        } \
    } while (0)

/**
 * @brief Ensure condition holds; otherwise return a formatted error.
 */
#define NMO_ENSURE(arena, cond, code, severity, ...) \
    do { \
        if (!(cond)) { \
            return nmo_result_errorf_at((arena), (code), (severity), __FILE__, __LINE__, __VA_ARGS__); \
        } \
    } while (0)

#define NMO_RETURN_NULL_IF_ERROR(result) \
    do { \
        nmo_result_t _r = (result); \
        if (_r.code != NMO_OK) { \
            return NULL; \
        } \
    } while (0)

#define NMO_RETURN_NULL_IF_ERROR_DO(result, on_error) \
    do { \
        nmo_result_t _r = (result); \
        if (_r.code != NMO_OK) { \
            on_error; \
            return NULL; \
        } \
    } while (0)

#define NMO_RETURN_ERROR(error) \
    return nmo_result_error(error)

#define NMO_RETURN_OK() \
    return nmo_result_ok()

#ifdef __cplusplus
}
#endif

#endif // NMO_ERROR_H
