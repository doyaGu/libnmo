#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <string.h>

// Error message table
static const char* error_messages[] = {
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
    [NMO_ERR_INVALID_OFFSET] = "Invalid offset",
    [NMO_ERR_EOF] = "Unexpected end of file",
    [NMO_ERR_INVALID_ARGUMENT] = "Invalid argument",
    [NMO_ERR_INVALID_STATE] = "Invalid state",
    [NMO_ERR_NOT_IMPLEMENTED] = "Not implemented",
    [NMO_ERR_UNKNOWN] = "Unknown error",
};

nmo_error* nmo_error_create(nmo_arena* arena,
                            nmo_error_code code,
                            nmo_severity severity,
                            const char* message,
                            const char* file,
                            int line) {
    nmo_error* error;

    // Allocate from arena if provided, otherwise use malloc
    if (arena != NULL) {
        error = (nmo_error*)nmo_arena_alloc(arena, sizeof(nmo_error), sizeof(void*));
    } else {
        nmo_allocator alloc = nmo_allocator_default();
        error = (nmo_error*)nmo_alloc(&alloc, sizeof(nmo_error), sizeof(void*));
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

void nmo_error_add_cause(nmo_error* error, nmo_error* cause) {
    if (error == NULL) {
        return;
    }

    // Find end of chain
    while (error->cause != NULL) {
        error = error->cause;
    }

    error->cause = cause;
}

const char* nmo_error_string(nmo_error_code code) {
    if (code < 0 || code >= (nmo_error_code)(sizeof(error_messages) / sizeof(error_messages[0]))) {
        return "Invalid error code";
    }

    return error_messages[code];
}

nmo_result nmo_result_ok(void) {
    nmo_result result = {
        .code = NMO_OK,
        .error = NULL
    };
    return result;
}

nmo_result nmo_result_error(nmo_error* error) {
    nmo_result result = {
        .code = error ? error->code : NMO_ERR_UNKNOWN,
        .error = error
    };
    return result;
}
