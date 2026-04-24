#ifndef NMO_TOOL_SESSION_H
#define NMO_TOOL_SESSION_H

#include "nmo.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool nmo_tool_open_context(nmo_context_t **out_ctx,
                           char *errbuf,
                           size_t errbuf_size);

bool nmo_tool_open_document(const char *path,
                            nmo_context_t **out_ctx,
                            nmo_document_t **out_document,
                            nmo_workspace_t **out_workspace,
                            char *errbuf,
                            size_t errbuf_size);

typedef struct nmo_load_options nmo_load_options_t;

bool nmo_tool_open_document_opts(const char *path,
                                 const nmo_load_options_t *opts,
                                 nmo_context_t **out_ctx,
                                 nmo_document_t **out_document,
                                 nmo_workspace_t **out_workspace,
                                 char *errbuf,
                                 size_t errbuf_size);

void nmo_tool_close_document(nmo_context_t *ctx,
                             nmo_document_t *document,
                             nmo_workspace_t *workspace);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TOOL_SESSION_H */
