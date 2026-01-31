#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef nmo_result_errorf
#undef nmo_result_errorf
#endif

// Error message table
static const char *error_messages[] = {
    [NMO_OK] = "Success",
    [NMO_ERR_NOMEM] = "Out of memory",
    [NMO_ERR_BUFFER_OVERRUN] = "Buffer overrun",
    [NMO_ERR_FILE_NOT_FOUND] = "File not found",
    [NMO_ERR_CANT_OPEN_FILE] = "Cannot open file",
    [NMO_ERR_CANT_READ_FILE] = "Cannot read file",
    [NMO_ERR_CANT_WRITE_FILE] = "Cannot write file",
    [NMO_ERR_INVALID_SIGNATURE] = "Invalid file signature",
    [NMO_ERR_UNSUPPORTED_VERSION] = "Unsupported file version",
    [NMO_ERR_CHECKSUM_MISMATCH] = "Checksum mismatch",
    [NMO_ERR_DECOMPRESSION_FAILED] = "Decompression failed",
    [NMO_ERR_COMPRESSION_FAILED] = "Compression failed",
    [NMO_ERR_VALIDATION_FAILED] = "Validation failed",
    [NMO_ERR_INVALID_FORMAT] = "Invalid format",
    [NMO_ERR_INVALID_OFFSET] = "Invalid offset",
    [NMO_ERR_EOF] = "Unexpected end of file",
    [NMO_ERR_INVALID_ARGUMENT] = "Invalid argument",
    [NMO_ERR_INVALID_STATE] = "Invalid state",
    [NMO_ERR_NOT_IMPLEMENTED] = "Not implemented",
    [NMO_ERR_NOT_SUPPORTED] = "Operation not supported",
    [NMO_ERR_UNKNOWN] = "Unknown error",
    [NMO_ERR_INTERNAL] = "Internal error",
    [NMO_ERR_OUT_OF_BOUNDS] = "Index out of bounds",
    [NMO_ERR_NOT_FOUND] = "Item not found",
    [NMO_ERR_ALREADY_EXISTS] = "Item already exists",
    [NMO_ERR_CORRUPT] = "Corrupted data",
    [NMO_ERR_CANCELLED] = "Operation cancelled",
};

nmo_error_t *nmo_error_create(nmo_arena_t *arena,
                              nmo_error_code_t code,
                              nmo_severity_t severity,
                              const char *message,
                              const char *file,
                              int line) {
    nmo_error_t *error;

    // Allocate from arena if provided, otherwise use malloc
    if (arena != NULL) {
        error = (nmo_error_t *) nmo_arena_alloc(arena, sizeof(nmo_error_t), sizeof(void *));
    } else {
        nmo_allocator_t alloc = nmo_allocator_default();
        error = (nmo_error_t *) nmo_alloc(&alloc, sizeof(nmo_error_t), sizeof(void *));
    }

    if (error == NULL) {
        return NULL;
    }

    error->code = code;
    error->severity = severity;
    error->message = message;
    error->file = file;
    error->line = line;
    error->cause = NULL;

    return error;
}

void nmo_error_add_cause(nmo_error_t *error, nmo_error_t *cause) {
    if (error == NULL) {
        return;
    }

    // Find end of chain
    while (error->cause != NULL) {
        error = error->cause;
    }

    error->cause = cause;
}

const char *nmo_error_string(nmo_error_code_t code) {
    if (code < 0 || code >= (nmo_error_code_t) (sizeof(error_messages) / sizeof(error_messages[0]))) {
        return "Invalid error code";
    }

    return error_messages[code];
}

nmo_result_t nmo_result_ok(void) {
    nmo_result_t result = {
        .code = NMO_OK,
        .error = NULL
    };
    return result;
}

nmo_result_t nmo_result_error(nmo_error_t *error) {
    nmo_result_t result = {
        .code = error ? error->code : NMO_ERR_UNKNOWN,
        .error = error
    };
    return result;
}

static char *nmo_error_alloc_message(nmo_arena_t *arena, size_t length) {
    size_t bytes = length + 1;
    if (arena != NULL) {
        return (char *) nmo_arena_alloc(arena, bytes, 1);
    }
    nmo_allocator_t alloc = nmo_allocator_default();
    return (char *) nmo_alloc(&alloc, bytes, 1);
}

static char *nmo_error_format_message(nmo_arena_t *arena,
                                      const char *fmt,
                                      va_list args,
                                      int *message_allocated) {
    if (message_allocated != NULL) {
        *message_allocated = 0;
    }

    if (fmt == NULL) {
        return NULL;
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        return NULL;
    }

    char *message = nmo_error_alloc_message(arena, (size_t)needed);
    if (message == NULL) {
        return NULL;
    }

    (void)vsnprintf(message, (size_t)needed + 1, fmt, args);
    if (message_allocated != NULL) {
        *message_allocated = 1;
    }
    return message;
}

static nmo_error_t *nmo_error_createf_at_v(nmo_arena_t *arena,
                                           nmo_error_code_t code,
                                           nmo_severity_t severity,
                                           const char *file,
                                           int line,
                                           const char *fmt,
                                           va_list args) {
    if (fmt == NULL) {
        return nmo_error_create(arena, code, severity,
                                "Invalid error format string",
                                file, line);
    }

    int message_allocated = 0;
    char *message = nmo_error_format_message(arena, fmt, args, &message_allocated);
    if (message == NULL) {
        message = "Failed to format error message";
    }

    nmo_error_t *error = nmo_error_create(arena, code, severity, message, file, line);
    if (error == NULL) {
        if (message_allocated && arena == NULL) {
            nmo_allocator_t alloc = nmo_allocator_default();
            nmo_free(&alloc, message);
        }
        return NULL;
    }

    return error;
}

nmo_error_t *nmo_error_createf_at(nmo_arena_t *arena,
                                 nmo_error_code_t code,
                                 nmo_severity_t severity,
                                 const char *file,
                                 int line,
                                 const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nmo_error_t *error = nmo_error_createf_at_v(arena, code, severity, file, line, fmt, args);
    va_end(args);
    return error;
}

nmo_result_t nmo_result_errorf_at(nmo_arena_t *arena,
                                 nmo_error_code_t code,
                                 nmo_severity_t severity,
                                 const char *file,
                                 int line,
                                 const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nmo_error_t *error = nmo_error_createf_at_v(arena, code, severity, file, line, fmt, args);
    va_end(args);

    if (error == NULL) {
        nmo_result_t result = { code, NULL };
        return result;
    }

    return nmo_result_error(error);
}

nmo_result_t nmo_result_errorf(nmo_arena_t *arena,
                               nmo_error_code_t code,
                               nmo_severity_t severity,
                               const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nmo_error_t *error = nmo_error_createf_at_v(arena, code, severity, NULL, 0, fmt, args);
    va_end(args);

    if (error == NULL) {
        nmo_result_t result = { code, NULL };
        return result;
    }

    return nmo_result_error(error);
}
