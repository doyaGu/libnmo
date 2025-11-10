#include "core/nmo_logger.h"
#include <stdio.h>
#include <string.h>

#define NMO_LOG_BUFFER_SIZE 4096

static const char* log_level_names[] = {
    [NMO_LOG_DEBUG] = "DEBUG",
    [NMO_LOG_INFO]  = "INFO",
    [NMO_LOG_WARN]  = "WARN",
    [NMO_LOG_ERROR] = "ERROR",
};

static void stderr_log(void* user_data, nmo_log_level level, const char* message) {
    (void)user_data;
    fprintf(stderr, "[%s] %s\n", log_level_names[level], message);
}

static void null_log(void* user_data, nmo_log_level level, const char* message) {
    (void)user_data;
    (void)level;
    (void)message;
    // Discard all messages
}

nmo_logger nmo_logger_stderr(void) {
    nmo_logger logger = {
        .log = stderr_log,
        .user_data = NULL,
        .level = NMO_LOG_INFO
    };
    return logger;
}

nmo_logger nmo_logger_null(void) {
    nmo_logger logger = {
        .log = null_log,
        .user_data = NULL,
        .level = NMO_LOG_DEBUG
    };
    return logger;
}

nmo_logger nmo_logger_custom(nmo_log_fn log, void* user_data, nmo_log_level level) {
    nmo_logger logger = {
        .log = log,
        .user_data = user_data,
        .level = level
    };
    return logger;
}

void nmo_vlog(nmo_logger* logger, nmo_log_level level, const char* format, va_list args) {
    if (logger == NULL || logger->log == NULL) {
        return;
    }

    // Filter by log level
    if (level < logger->level) {
        return;
    }

    // Format message
    char buffer[NMO_LOG_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);

    // Call log callback
    logger->log(logger->user_data, level, buffer);
}

void nmo_log(nmo_logger* logger, nmo_log_level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    nmo_vlog(logger, level, format, args);
    va_end(args);
}
