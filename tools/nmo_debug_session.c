#include "nmo_debug_session.h"

#include "nmo_tool_session.h"

#include <string.h>

static void set_err(char *errbuf, size_t errbuf_size, const char *msg) {
    if (!errbuf || errbuf_size == 0) {
        return;
    }
    if (!msg) {
        errbuf[0] = '\0';
        return;
    }
#if defined(_MSC_VER)
    strncpy_s(errbuf, errbuf_size, msg, _TRUNCATE);
#else
    strncpy(errbuf, msg, errbuf_size - 1);
    errbuf[errbuf_size - 1] = '\0';
#endif
}

bool nmo_debug_load_file(nmo_debug_context_t *dbg, const char *path, char *errbuf, size_t errbuf_size) {
    if (!dbg || !path || !path[0]) {
        set_err(errbuf, errbuf_size, "Invalid arguments");
        return false;
    }

    nmo_context_t *new_ctx = NULL;
    nmo_session_t *new_session = NULL;
    char local_err[128];
    if (!nmo_tool_open_session(path, &new_ctx, &new_session, local_err, sizeof(local_err))) {
        set_err(errbuf, errbuf_size, local_err[0] ? local_err : "Failed to open file");
        return false;
    }

    nmo_tool_close_session(dbg->ctx, dbg->session);
    dbg->ctx = new_ctx;
    dbg->session = new_session;

    dbg->has_selection = false;
    dbg->selected_index = 0;

    strncpy(dbg->filename_storage, path, sizeof(dbg->filename_storage) - 1);
    dbg->filename_storage[sizeof(dbg->filename_storage) - 1] = '\0';
    dbg->filename = dbg->filename_storage;

    set_err(errbuf, errbuf_size, NULL);
    return true;
}
