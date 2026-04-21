#include "session/nmo_session_query.h"

#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_query.h"

nmo_status_t nmo_session_query_count_objects(
    nmo_session_t *session,
    size_t *out_count)
{
    if (session == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repository = nmo_session_get_repository(session);
    *out_count = repository != NULL
        ? nmo_object_repository_get_count(repository)
        : 0;
    return NMO_OK;
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
    return nmo_session_query_first(session, &query, out_object, NULL);
}
