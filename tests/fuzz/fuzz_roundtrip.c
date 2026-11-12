/**
 * @file fuzz_roundtrip.c
 * @brief Fuzz load, save and load again.
 *
 * Whatever loads must save, and what was saved must load: a file libnmo accepts
 * and then cannot write, or writes and then cannot read, is a bug even when
 * nothing crashes.
 */

#include "fuzz_common.h"

#include "object/nmo_object_repository.h"
#include "runtime/nmo_document.h"
#include "session/nmo_serializer.h"
#include "../../src/runtime/runtime_internal.h"
#include "../../src/session/session_internal.h"

#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static char *scratch = NULL;
    if (scratch == NULL) {
        scratch = fuzz_scratch_path("nmo_fuzz_roundtrip");
        if (scratch == NULL) {
            fuzz_fail("cannot build a scratch path", NMO_ERR_NOMEM);
        }
    }

    nmo_context_t *ctx = fuzz_context();
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        fuzz_fail("cannot create a session", NMO_ERR_NOMEM);
    }

    /* A save refuses a session without objects, on purpose, so a file whose
     * entries all come to nothing has no round trip. */
    if (fuzz_load_memory(session, data, size, NULL) == NMO_OK &&
        nmo_object_repository_get_count(nmo_session_get_repository(session)) > 0) {
        remove(scratch);
        /* The default save, without the flush to disk that makes it durable. */
        nmo_save_options_t options = nmo_save_options_default();
        options.durability = NMO_SAVE_DURABILITY_FAST;
        nmo_status_t status = nmo_session_save_file(session, scratch, &options, NULL);
        if (status != NMO_OK) {
            fuzz_fail("a file that loads does not save", status);
        }

        size_t saved_size = 0;
        uint8_t *saved = fuzz_read_file(scratch, &saved_size);
        if (saved == NULL) {
            fuzz_fail("cannot read the saved file back", NMO_ERR_FILE_NOT_FOUND);
        }
        nmo_session_t *reload = nmo_session_create(ctx);
        if (reload == NULL) {
            fuzz_fail("cannot create a session", NMO_ERR_NOMEM);
        }
        status = fuzz_load_memory(reload, saved, saved_size, NULL);
        if (status != NMO_OK) {
            fuzz_fail("a saved file does not load", status);
        }
        nmo_session_destroy(reload);
        free(saved);
        remove(scratch);
    }

    nmo_session_destroy(session);
    return 0;
}
