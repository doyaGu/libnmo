#include "nmo_repl_session.h"

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

bool nmo_repl_load_file(nmo_repl_context_t *repl, const char *path, char *errbuf, size_t errbuf_size) {
    if (!repl || !path || !path[0]) {
        set_err(errbuf, errbuf_size, "Invalid arguments");
        return false;
    }

    nmo_context_t *new_ctx = NULL;
    nmo_document_t *new_document = NULL;
    nmo_workspace_t *new_workspace = NULL;
    char local_err[128];
    if (!nmo_tool_open_document(path, &new_ctx, &new_document, &new_workspace,
                                local_err, sizeof(local_err))) {
        set_err(errbuf, errbuf_size, local_err[0] ? local_err : "Failed to open file");
        return false;
    }

    nmo_tool_close_document(repl->ctx, repl->document, repl->workspace);
    repl->ctx = new_ctx;
    repl->document = new_document;
    repl->workspace = new_workspace;

    repl->has_selection = false;
    repl->selected_index = 0;

    strncpy(repl->filename_storage, path, sizeof(repl->filename_storage) - 1);
    repl->filename_storage[sizeof(repl->filename_storage) - 1] = '\0';
    repl->filename = repl->filename_storage;

    set_err(errbuf, errbuf_size, NULL);
    return true;
}
