/**
 * @file fuzz_common.c
 * @brief Helpers shared by the fuzz harnesses.
 */

#include "fuzz_common.h"

#include "core/nmo_logger.h"
#include "io/nmo_io.h"
#include "io/nmo_io_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define fuzz_getpid() ((long)_getpid())
#else
#include <unistd.h>
#define fuzz_getpid() ((long)getpid())
#endif

nmo_context_t *fuzz_context_create(void) {
    /* NMO_FUZZ_LOG=1 logs to stderr, for looking into an input by hand. */
    static nmo_logger_t logger;
    const char *log = getenv("NMO_FUZZ_LOG");
    logger = (log != NULL && log[0] != '\0' && log[0] != '0') ? nmo_logger_stderr()
                                                              : nmo_logger_null();
    nmo_context_desc_t desc = {0};
    desc.logger = &logger;
    desc.data_dir = NMO_FUZZ_DATA_DIR;
    return nmo_context_create(&desc);
}

nmo_context_t *fuzz_context(void) {
    static nmo_context_t *ctx = NULL;
    if (ctx == NULL) {
        ctx = fuzz_context_create();
        if (ctx == NULL) {
            fuzz_fail("cannot create a context", NMO_ERR_NOMEM);
        }
    }
    return ctx;
}

nmo_status_t fuzz_load_memory(nmo_session_t *session, const uint8_t *data, size_t size,
                              const nmo_load_options_t *options) {
    if (session == NULL || data == NULL || size == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_io_interface_t *io = nmo_memory_io_open_read(data, size);
    if (io == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, options);
    if (ds == NULL) {
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }
    nmo_status_t status = nmo_deserializer_parse_header(ds);
    if (status == NMO_OK) {
        status = nmo_deserializer_parse_objects(ds);
    }
    if (status == NMO_OK) {
        status = nmo_deserializer_finalize(ds);
    }
    nmo_deserializer_destroy(ds);
    return status;
}

uint8_t *fuzz_read_file(const char *path, size_t *out_size) {
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    uint8_t *data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    for (;;) {
        if (size == capacity) {
            size_t grown = capacity == 0 ? 65536u : capacity * 2u;
            uint8_t *bigger = (uint8_t *)realloc(data, grown);
            if (bigger == NULL) {
                free(data);
                fclose(file);
                return NULL;
            }
            data = bigger;
            capacity = grown;
        }
        size_t got = fread(data + size, 1, capacity - size, file);
        size += got;
        if (got == 0) {
            break;
        }
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) {
        free(data);
        return NULL;
    }
    *out_size = size;
    return data;
}

char *fuzz_scratch_path(const char *stem) {
    const char *dir = getenv("TMPDIR");
    if (dir == NULL || dir[0] == '\0') dir = getenv("TEMP");
    if (dir == NULL || dir[0] == '\0') dir = getenv("TMP");
    if (dir == NULL || dir[0] == '\0') dir = ".";
    size_t dir_len = strlen(dir);
    const char *sep = (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\'))
        ? "" : "/";
    int needed = snprintf(NULL, 0, "%s%s%s-%ld.tmp", dir, sep, stem, fuzz_getpid());
    if (needed < 0) {
        return NULL;
    }
    char *path = (char *)malloc((size_t)needed + 1u);
    if (path == NULL) {
        return NULL;
    }
    snprintf(path, (size_t)needed + 1u, "%s%s%s-%ld.tmp", dir, sep, stem, fuzz_getpid());
    return path;
}

void fuzz_fail(const char *what, nmo_status_t status) {
    fprintf(stderr, "fuzz check failed: %s (status %d: %s)\n",
            what, (int)status, nmo_error_string(status));
    fflush(stderr);
    abort();
}
