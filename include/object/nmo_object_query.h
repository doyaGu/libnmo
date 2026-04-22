/**
 * @file nmo_object_query.h
 * @brief Library-level object query helpers.
 */

#ifndef NMO_OBJECT_QUERY_H
#define NMO_OBJECT_QUERY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_allocator nmo_allocator_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object_query_index nmo_object_query_index_t;
typedef struct nmo_session nmo_document_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/*
 * The raw query engine remains public for advanced C callers, but bindings are
 * expected to depend on narrower result/iterator facades over time.
 */
#define NMO_OBJECT_QUERY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_QUERY_ENGINE_API_TIER NMO_API_TIER_ADVANCED_C

typedef enum nmo_object_query_index_flags {
    NMO_OBJECT_QUERY_INDEX_MEMBERSHIP = 1u << 0,
    NMO_OBJECT_QUERY_INDEX_NAMES      = 1u << 1,
    NMO_OBJECT_QUERY_INDEX_TEXT       = 1u << 2,
    NMO_OBJECT_QUERY_INDEX_TYPE_GUID  = 1u << 3,
    NMO_OBJECT_QUERY_INDEX_ALL        = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3)
} nmo_object_query_index_flags_t;

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
    bool has_type_guid;
    nmo_guid_t type_guid;
    const char *name;
    nmo_object_query_name_mode_t name_mode;
    bool name_case_insensitive;
    nmo_object_query_predicate_fn predicate;
    void *predicate_user_data;
} nmo_object_query_t;

typedef struct nmo_object_query_context {
    nmo_object_repository_t *repository;
    nmo_object_query_index_t *index;
    const nmo_type_registry_t *registry;
} nmo_object_query_context_t;

typedef struct nmo_object_query_result {
    size_t total;
    size_t matched;
    size_t visited;
    bool stopped_early;
} nmo_object_query_result_t;

typedef struct nmo_object_selector {
    bool has_id;
    nmo_object_id_t id;
    const char *name;
    nmo_class_id_t required_base_class;
    const nmo_class_id_t *allowed_class_ids;
    size_t allowed_class_count;
} nmo_object_selector_t;

typedef bool (*nmo_object_query_visitor_fn)(
    size_t object_index,
    nmo_object_t *object,
    void *user_data);

NMO_API nmo_status_t nmo_object_query_matches(
    const nmo_object_t *object,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    bool *out_matches);

NMO_API nmo_object_query_index_t *nmo_object_query_index_create(
    nmo_object_repository_t *repository,
    const nmo_type_registry_t *registry,
    const nmo_allocator_t *allocator);

NMO_API void nmo_object_query_index_destroy(nmo_object_query_index_t *index);

NMO_API nmo_status_t nmo_object_query_index_rebuild(
    nmo_object_query_index_t *index);

NMO_API void nmo_object_query_index_invalidate(
    nmo_object_query_index_t *index,
    uint32_t flags);

NMO_API void nmo_object_query_index_trim(
    nmo_object_query_index_t *index,
    uint32_t flags);

NMO_API nmo_status_t nmo_object_query_index_attach_repository_observer(
    nmo_object_query_index_t *index);

NMO_API void nmo_object_query_index_detach_repository_observer(
    nmo_object_query_index_t *index);

NMO_API nmo_status_t nmo_object_query_iterate(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    nmo_object_query_visitor_fn visitor,
    void *user_data,
    nmo_object_query_result_t *out_result);

NMO_API nmo_status_t nmo_object_query_count(
    nmo_document_t *document,
    const nmo_object_query_t *query,
    size_t *out_count);

NMO_API nmo_status_t nmo_object_query_find_first(
    nmo_document_t *document,
    const nmo_object_query_t *query,
    nmo_object_t **out_object,
    size_t *out_index);

NMO_API nmo_status_t nmo_object_query_resolve_one(
    nmo_document_t *document,
    const nmo_object_selector_t *selector,
    nmo_object_t **out_object,
    nmo_object_id_t *out_id);

/**
 * @ownership borrowed (arena-owned by caller; valid until arena reset/destroy)
 */
NMO_API nmo_status_t nmo_object_query_collect(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    nmo_arena_t *arena,
    nmo_object_t ***out_objects,
    size_t *out_count,
    nmo_object_query_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_QUERY_H */
