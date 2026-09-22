#include "nmo_repl_session.h"

#include "nmo_tool_common.h"
#include "nmo_tool_session.h"

#include <stdlib.h>
#include <string.h>

static void set_err(char **out_error, const char *msg) {
    if (out_error) {
        *out_error = nmo_tool_strdup(msg);
    }
}

bool nmo_repl_load_file(nmo_repl_context_t *repl, const char *path, char **out_error) {
    if (!repl || !path || !path[0]) {
        set_err(out_error, "Invalid arguments");
        return false;
    }

    /* `path` may alias repl->filename (reload), so copy it before replacing. */
    char *filename = nmo_tool_strdup(path);
    if (!filename) {
        set_err(out_error, "Out of memory");
        return false;
    }

    nmo_context_t *new_ctx = NULL;
    nmo_document_t *new_document = NULL;
    nmo_workspace_t *new_workspace = NULL;
    char *open_error = NULL;
    if (!nmo_tool_open_document(path, &new_ctx, &new_document, &new_workspace,
                                &open_error)) {
        if (out_error) {
            *out_error = open_error ? open_error : nmo_tool_strdup("Failed to open file");
        } else {
            free(open_error);
        }
        free(filename);
        return false;
    }

    nmo_tool_close_document(repl->ctx, repl->document, repl->workspace);
    repl->ctx = new_ctx;
    repl->document = new_document;
    repl->workspace = new_workspace;

    repl->has_selection = false;
    repl->selected_index = 0;

    free(repl->filename_storage);
    repl->filename_storage = filename;
    repl->filename = repl->filename_storage;
    return true;
}

void nmo_repl_session_cleanup(nmo_repl_context_t *repl) {
    if (!repl) {
        return;
    }
    free(repl->filename_storage);
    repl->filename_storage = NULL;
    repl->filename = NULL;
    free(repl->prompt);
    repl->prompt = NULL;
}
