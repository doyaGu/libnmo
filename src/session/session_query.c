#include "session/nmo_session_query.h"

#include "document/nmo_document.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_query.h"

nmo_status_t nmo_session_query_count_objects(
    nmo_session_t *session,
    size_t *out_count)
{
    return nmo_object_query_count((nmo_document_t *)session, NULL, out_count);
}

nmo_status_t nmo_session_query_find_object_by_name(
    nmo_session_t *session,
    const char *name,
    nmo_object_t **out_object)
{
    if (session == NULL || name == NULL || out_object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_query_t query = {
        .name = name,
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
        .name_case_insensitive = false
    };
    return nmo_object_query_find_first(
        (nmo_document_t *)session,
        &query,
        out_object,
        NULL);
}
