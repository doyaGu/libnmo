#ifndef NMO_LOGGER_H
#define NMO_LOGGER_H

#include "nmo_types.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_logger.h
 * @brief Logging interface
 *
 * Provides a flexible logging system with:
 * - Multiple log levels
 * - Custom log handlers
 * - Built-in stderr and null loggers
 */

/**
 * @brief Log levels
 */
typedef enum {
    NMO_LOG_DEBUG,   /**< Debug messages */
    NMO_LOG_INFO,    /**< Informational messages */
    NMO_LOG_WARN,    /**< Warnings */
    NMO_LOG_ERROR,   /**< Errors */
} nmo_log_level;

/**
 * @brief Log function callback type
 *
 * @param user_data User-defined context
 * @param level Log level
 * @param message Log message
 */
typedef void (*nmo_log_fn)(void* user_data, nmo_log_level level, const char* message);

/**
 * @brief Logger interface
 */
typedef struct nmo_logger {
    nmo_log_fn    log;       /**< Log callback */
    void*         user_data; /**< User-defined context */
    nmo_log_level level;     /**< Minimum log level */
} nmo_logger;

/**
 * @brief Create stderr logger
 *
 * @return Logger that outputs to stderr
 */
NMO_API nmo_logger nmo_logger_stderr(void);

/**
 * @brief Create null logger (discards all messages)
 *
 * @return Null logger
 */
NMO_API nmo_logger nmo_logger_null(void);

/**
 * @brief Create custom logger
 *
 * @param log Log callback
 * @param user_data User-defined context
 * @param level Minimum log level
 * @return Custom logger
 */
NMO_API nmo_logger nmo_logger_custom(nmo_log_fn log, void* user_data, nmo_log_level level);

/**
 * @brief Log message with printf-style formatting
 *
 * @param logger Logger instance
 * @param level Log level
 * @param format Printf-style format string
 * @param ... Format arguments
 */
NMO_API void nmo_log(nmo_logger* logger, nmo_log_level level, const char* format, ...);

/**
 * @brief Log message with va_list
 *
 * @param logger Logger instance
 * @param level Log level
 * @param format Printf-style format string
 * @param args Format arguments
 */
NMO_API void nmo_vlog(nmo_logger* logger, nmo_log_level level, const char* format, va_list args);

// Convenience macros
#define nmo_log_debug(logger, ...) nmo_log(logger, NMO_LOG_DEBUG, __VA_ARGS__)
#define nmo_log_info(logger, ...)  nmo_log(logger, NMO_LOG_INFO, __VA_ARGS__)
#define nmo_log_warn(logger, ...)  nmo_log(logger, NMO_LOG_WARN, __VA_ARGS__)
#define nmo_log_error(logger, ...) nmo_log(logger, NMO_LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // NMO_LOGGER_H
