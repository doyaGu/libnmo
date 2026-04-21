#ifndef NMO_OBJECT_ITER_H
#define NMO_OBJECT_ITER_H

#include "nmo_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;

/*
 * Narrow stable repository iteration helpers that avoid repository-owned
 * scratch arrays and expose only count/index-based traversal.
 */
#define NMO_OBJECT_ITER_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_ITER_READ_API_TIER NMO_API_TIER_STABLE_CONSUMER

/**
 * @brief Get object count through the stable iteration facade.
 */
NMO_API size_t nmo_object_iter_count(
    const nmo_object_repository_t *repository);

/**
 * @brief Get object by index through the stable iteration facade.
 * @ownership borrowed (owned by repository)
 */
NMO_API nmo_object_t *nmo_object_iter_at(
    const nmo_object_repository_t *repository,
    size_t index);

/**
 * @brief Count objects whose class ID exactly matches @p class_id.
 *
 * This stable facade avoids the repository scratch-array contract exposed by
 * nmo_object_repository_find_by_class().
 */
NMO_API size_t nmo_object_iter_count_class(
    const nmo_object_repository_t *repository,
    nmo_class_id_t class_id);

/**
 * @brief Return the Nth object whose class ID exactly matches @p class_id.
 * @ownership borrowed (owned by repository)
 */
NMO_API nmo_object_t *nmo_object_iter_at_class(
    const nmo_object_repository_t *repository,
    nmo_class_id_t class_id,
    size_t match_index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_ITER_H */
