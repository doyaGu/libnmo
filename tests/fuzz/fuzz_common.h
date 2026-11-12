/**
 * @file fuzz_common.h
 * @brief What the fuzz harnesses share: the libFuzzer entry point, a quiet
 *        context, loading from memory and failing a check.
 *
 * Every harness defines LLVMFuzzerTestOneInput. A libFuzzer build links it with
 * libFuzzer's main; every other build links it with fuzz_replay.c, which runs it
 * over the files named on the command line.
 */

#ifndef NMO_FUZZ_COMMON_H
#define NMO_FUZZ_COMMON_H

#include "core/nmo_error.h"
#include "object/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_session.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/** A context that reads the type tables from data/ and logs nothing, or to
 *  stderr when the environment sets NMO_FUZZ_LOG=1. */
nmo_context_t *fuzz_context_create(void);

/**
 * The context of this process, created on first use and kept to the end.
 * Creating one reads the type tables, which takes far longer than loading a
 * small file; a load reads the context and changes only its own session.
 */
nmo_context_t *fuzz_context(void);

/** Load a whole file from memory into a session, as nmo_load_file loads one from disk. */
nmo_status_t fuzz_load_memory(nmo_session_t *session, const uint8_t *data, size_t size,
                              const nmo_load_options_t *options);

/** Read a whole file. Returns NULL when it cannot be read; free() the result. */
uint8_t *fuzz_read_file(const char *path, size_t *out_size);

/** A path for a scratch file of this process ("<temp dir>/<stem>-<pid>.tmp"); free() it. */
char *fuzz_scratch_path(const char *stem);

/** Report a broken invariant and abort, which libFuzzer and CTest both count as a crash. */
void fuzz_fail(const char *what, nmo_status_t status);

#endif /* NMO_FUZZ_COMMON_H */
