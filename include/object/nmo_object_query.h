/**
 * @file nmo_object_query.h
 * @brief Library-level object query helpers.
 */

#ifndef NMO_OBJECT_QUERY_H
#define NMO_OBJECT_QUERY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;

typedef enum nmo_object_query_name_mode {
    NMO_OBJECT_QUERY_NAME_NONE = 0,
    NMO_OBJECT_QUERY_NAME_EXACT,
    NMO_OBJECT_QUERY_NAME_SUBSTRING,
    NMO_OBJECT_QUERY_NAME_WILDCARD,
    NMO_OBJECT_QUERY_NAME_REGEX
} nmo_object_query_name_mode_t;

typedef bool (*nmo_object_query_predicate_fn)(
    const nmo_object_t *object,
    void *user_data);

typedef struct nmo_object_query {
    nmo_object_id_t object_id;
    nmo_class_id_t class_id;
    bool include_derived_classes;
    const char *name;
    nmo_object_query_name_mode_t name_mode;
    bool name_case_insensitive;
    nmo_object_query_predicate_fn predicate;
    void *predicate_user_data;
} nmo_object_query_t;

typedef struct nmo_object_query_result {
    size_t total;
    size_t matched;
    size_t visited;
    bool stopped_early;
} nmo_object_query_result_t;

typedef bool (*nmo_object_query_visitor_fn)(
    size_t match_index,
    nmo_object_t *object,
    void *user_data);

NMO_API nmo_status_t nmo_object_query_matches(
    const nmo_object_t *object,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    bool *out_matches);

NMO_API nmo_status_t nmo_object_query_iterate(
    nmo_object_repository_t *repository,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    nmo_object_query_visitor_fn visitor,
    void *user_data,
    nmo_object_query_result_t *out_result);

NMO_API nmo_status_t nmo_object_query_collect(
    nmo_object_repository_t *repository,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    nmo_arena_t *arena,
    nmo_object_t ***out_objects,
    size_t *out_count,
    nmo_object_query_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_QUERY_H */
