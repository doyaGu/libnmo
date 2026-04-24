#include "document/nmo_document_file_state.h"

#include "../runtime/runtime_internal.h"

nmo_included_file_t *nmo_document_get_included_files(
    const nmo_document_t *document,
    uint32_t *out_count)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL ? nmo_session_get_included_files(session, out_count) : NULL;
}

nmo_status_t nmo_document_set_file_info(
    nmo_document_t *document,
    const nmo_file_info_t *info)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL ? nmo_session_set_file_info(session, info) : NMO_ERR_INVALID_ARGUMENT;
}

void nmo_document_set_manager_data(
    nmo_document_t *document,
    nmo_manager_data_t *data,
    uint32_t count)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_set_manager_data(session, data, count);
    }
}

nmo_status_t nmo_document_set_plugin_dependencies(
    nmo_document_t *document,
    nmo_plugin_dep_t *deps,
    uint32_t count)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_set_plugin_dependencies(session, deps, count)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_refresh_plugin_diagnostics(
    nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_refresh_plugin_diagnostics(session)
        : NMO_ERR_INVALID_ARGUMENT;
}

void nmo_document_set_runtime_load_stats(
    nmo_document_t *document,
    const nmo_runtime_load_stats_t *stats)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_set_runtime_load_stats(session, stats);
    }
}

void nmo_document_set_plugin_diagnostics(
    nmo_document_t *document,
    const nmo_session_plugin_dependency_status_t *entries,
    size_t entry_count,
    size_t missing_count,
    size_t outdated_count,
    int extension_registry_available)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_set_plugin_diagnostics(
            session,
            entries,
            entry_count,
            missing_count,
            outdated_count,
            extension_registry_available);
    }
}

void nmo_document_set_file_header(
    nmo_document_t *document,
    const void *header,
    size_t header_size)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_set_file_header(session, header, header_size);
    }
}

nmo_status_t nmo_document_add_included_file(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_add_included_file(session, name, data, size)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_add_included_file_ex(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_add_included_file_ex(session, name, data, size, meta)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_add_included_file_borrowed(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_add_included_file_borrowed(session, name, data, size)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_add_included_file_borrowed_ex(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_add_included_file_borrowed_ex(session, name, data, size, meta)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_set_included_file_owners(
    nmo_document_t *document,
    uint32_t index,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_set_included_file_owners(session, index, owner_ids, owner_count)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_replace_included_file(
    nmo_document_t *document,
    uint32_t index,
    const void *new_data,
    uint32_t new_size)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_replace_included_file(session, index, new_data, new_size)
        : NMO_ERR_INVALID_ARGUMENT;
}

nmo_status_t nmo_document_remove_included_file(
    nmo_document_t *document,
    uint32_t index)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_remove_included_file(session, index)
        : NMO_ERR_INVALID_ARGUMENT;
}
