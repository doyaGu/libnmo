#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

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
    [NMO_ERR_TRUNCATED_CHUNK] = "Truncated chunk data",
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
    [NMO_ERR_LIMIT_EXCEEDED] = "Limit exceeded",
};

const char *nmo_error_string(nmo_status_t code) {
    if (code < 0) {
        return "Unknown error";
    }

    const size_t index = (size_t)code;
    const size_t count = sizeof(error_messages) / sizeof(error_messages[0]);
    if (index >= count || error_messages[index] == NULL) {
        return "Unknown error";
    }

    return error_messages[index];
}

/* =============================================================================
 * TLS Last-Error Implementation
 * ============================================================================= */

#define NMO_LAST_ERROR_MESSAGE_SIZE 1024

/* Expected misses (a key not found, an identifier a chunk lacks) set the last
 * error on hot paths, so setting it stays cheap: a message without arguments is
 * copied rather than formatted, and the chain is built only when asked for. */
typedef struct nmo_last_error_state {
    nmo_error_code_t code;
    nmo_severity_t severity;
    const char *file;
    int line;
    char message[NMO_LAST_ERROR_MESSAGE_SIZE];
} nmo_last_error_state_t;

#if defined(_MSC_VER)
static __declspec(thread) nmo_last_error_state_t tls_last_error = {0};
#else
static _Thread_local nmo_last_error_state_t tls_last_error = {0};
#endif

void nmo_last_error_clear(void) {
    /* Every successful return clears it: leave a clear state untouched. */
    if (tls_last_error.code == NMO_OK && tls_last_error.message[0] == '\0' &&
        tls_last_error.file == NULL && tls_last_error.line == 0 &&
        tls_last_error.severity == NMO_SEVERITY_DEBUG) {
        return;
    }
    tls_last_error.code = NMO_OK;
    tls_last_error.severity = NMO_SEVERITY_DEBUG;
    tls_last_error.file = NULL;
    tls_last_error.line = 0;
    tls_last_error.message[0] = '\0';
}

nmo_last_error_saved_t *nmo_last_error_save(void) {
    if (tls_last_error.code == NMO_OK && tls_last_error.message[0] == '\0') {
        return NULL;
    }
    nmo_last_error_saved_t *saved = (nmo_last_error_saved_t *)malloc(sizeof(*saved));
    if (saved != NULL) {
        *saved = tls_last_error;
    }
    return saved;
}

void nmo_last_error_restore(nmo_last_error_saved_t *saved) {
    if (saved == NULL) {
        return;
    }
    tls_last_error = *saved;
    free(saved);
}

nmo_error_code_t nmo_last_error_code(void) {
    return tls_last_error.code;
}

nmo_severity_t nmo_last_error_severity(void) {
    return tls_last_error.severity;
}

const char *nmo_last_error_file(void) {
    return tls_last_error.file;
}

int nmo_last_error_line(void) {
    return tls_last_error.line;
}

const char *nmo_last_error_message(void) {
    return tls_last_error.message;
}

size_t nmo_last_error_message_copy(char *dst, size_t cap) {
    size_t len = strlen(tls_last_error.message);
    if (cap > 0 && dst != NULL) {
        size_t copy_len = (len < cap - 1) ? len : cap - 1;
        memcpy(dst, tls_last_error.message, copy_len);
        dst[copy_len] = '\0';
    }
    return len;
}

size_t nmo_last_error_chain_copy(char *dst, size_t cap) {
    /* The message, the error code's name and where it was set. */
    if (dst == NULL) {
        cap = 0;
    }
    const char *code_str = nmo_error_string(tls_last_error.code);
    int len = tls_last_error.file != NULL
        ? snprintf(dst, cap, "[%s] %s (%s:%d)", code_str, tls_last_error.message,
                   tls_last_error.file, tls_last_error.line)
        : snprintf(dst, cap, "[%s] %s", code_str, tls_last_error.message);
    if (len < 0) {
        if (cap > 0) {
            dst[0] = '\0';
        }
        return 0;
    }
    return (size_t)len;
}

static void nmo_last_error_set_v(nmo_error_code_t code,
                                  nmo_severity_t severity,
                                  const char *file,
                                  int line,
                                  const char *fmt,
                                  va_list args) {
    tls_last_error.code = code;
    tls_last_error.severity = severity;
    tls_last_error.file = file;
    tls_last_error.line = line;

    const char *text = NULL;
    if (fmt == NULL) {
        text = "";
    } else if (strchr(fmt, '%') == NULL) {
        text = fmt;
    } else if (fmt[0] == '%' && fmt[1] == 's' && fmt[2] == '\0') {
        va_list args_copy;
        va_copy(args_copy, args);
        text = va_arg(args_copy, const char *);
        va_end(args_copy);
        if (text == NULL) {
            text = "(null)";
        }
    }
    if (text != NULL) {
        size_t len = strlen(text);
        if (len >= NMO_LAST_ERROR_MESSAGE_SIZE) {
            len = NMO_LAST_ERROR_MESSAGE_SIZE - 1;
        }
        memmove(tls_last_error.message, text, len);
        tls_last_error.message[len] = '\0';
        return;
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int msg_len = vsnprintf(tls_last_error.message, NMO_LAST_ERROR_MESSAGE_SIZE, fmt, args_copy);
    va_end(args_copy);
    if (msg_len < 0) {
        tls_last_error.message[0] = '\0';
    }
}

void nmo_last_error_setf(nmo_error_code_t code,
                          nmo_severity_t severity,
                          const char *file,
                          int line,
                          const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    nmo_last_error_set_v(code, severity, file, line, fmt, args);
    va_end(args);
}
