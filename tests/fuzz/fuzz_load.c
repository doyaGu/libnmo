/**
 * @file fuzz_load.c
 * @brief Fuzz the whole load: file header, Header1, the data section and every
 *        object and manager chunk through its schema, then the finalize pass.
 *
 * Any status is fine; a crash, a sanitizer report or a leak is not. Seeds are
 * uncompressed files, so the mutations reach the chunks instead of breaking the
 * zlib stream.
 */

#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    nmo_session_t *session = nmo_session_create(fuzz_context());
    if (session == NULL) {
        fuzz_fail("cannot create a session", NMO_ERR_NOMEM);
    }
    (void)fuzz_load_memory(session, data, size, NULL);
    nmo_session_destroy(session);
    return 0;
}
