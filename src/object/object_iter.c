#include "object/nmo_object_iter.h"

#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

size_t nmo_object_iter_count(const nmo_object_repository_t *repository)
{
    return nmo_object_repository_get_count(repository);
}

nmo_object_t *nmo_object_iter_at(
    const nmo_object_repository_t *repository,
    size_t index)
{
    return nmo_object_repository_get_by_index(repository, index);
}

size_t nmo_object_iter_count_class(
    const nmo_object_repository_t *repository,
    nmo_class_id_t class_id)
{
    if (repository == NULL) {
        return 0;
    }

    size_t total = nmo_object_repository_get_count(repository);
    size_t matched = 0;
    for (size_t i = 0; i < total; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
        if (object != NULL && nmo_object_get_class_id(object) == class_id) {
            matched++;
        }
    }

    return matched;
}

nmo_object_t *nmo_object_iter_at_class(
    const nmo_object_repository_t *repository,
    nmo_class_id_t class_id,
    size_t match_index)
{
    if (repository == NULL) {
        return NULL;
    }

    size_t total = nmo_object_repository_get_count(repository);
    size_t matched = 0;
    for (size_t i = 0; i < total; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
        if (object == NULL || nmo_object_get_class_id(object) != class_id) {
            continue;
        }

        if (matched == match_index) {
            return object;
        }
        matched++;
    }

    return NULL;
}
