/**
 * @file nmo_behavior_registry.h
 * @brief Building-block prototype registry for behavior tooling.
 */

#ifndef NMO_BEHAVIOR_REGISTRY_H
#define NMO_BEHAVIOR_REGISTRY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMO_BEHAVIOR_REGISTRY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BEHAVIOR_REGISTRY_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_bb_registry nmo_behavior_registry_t;

typedef struct nmo_behavior_param_desc {
    const char *name;
    nmo_guid_t type_guid;
} nmo_behavior_param_desc_t;

typedef struct nmo_behavior_proto {
    nmo_guid_t guid;
    const char *name;
    const char *description;
    const char *category;
    const char *dll;
    uint32_t version;
    int32_t compatible_class_id;
    uint32_t behavior_flags;
    const char *const *inputs;
    uint32_t input_count;
    const char *const *outputs;
    uint32_t output_count;
    const nmo_behavior_param_desc_t *input_params;
    uint32_t input_param_count;
    const nmo_behavior_param_desc_t *output_params;
    uint32_t output_param_count;
    const nmo_behavior_param_desc_t *local_params;
    uint32_t local_param_count;
    const nmo_behavior_param_desc_t *settings;
    uint32_t setting_count;
} nmo_behavior_proto_t;

typedef int (*nmo_behavior_registry_visitor_fn)(
    const nmo_behavior_proto_t *proto,
    void *user_data);

NMO_API nmo_behavior_registry_t *nmo_behavior_registry_create(nmo_arena_t *arena);
NMO_API void nmo_behavior_registry_destroy(nmo_behavior_registry_t *registry);
NMO_API const nmo_behavior_proto_t *nmo_behavior_registry_find(
    const nmo_behavior_registry_t *registry,
    nmo_guid_t guid);
NMO_API const char *nmo_behavior_registry_get_name(
    const nmo_behavior_registry_t *registry,
    nmo_guid_t guid);
NMO_API nmo_status_t nmo_behavior_registry_add(
    nmo_behavior_registry_t *registry,
    const nmo_behavior_proto_t *proto);
NMO_API bool nmo_behavior_registry_remove(
    nmo_behavior_registry_t *registry,
    nmo_guid_t guid);
NMO_API size_t nmo_behavior_registry_count(
    const nmo_behavior_registry_t *registry);
NMO_API size_t nmo_behavior_registry_builtin_count(
    const nmo_behavior_registry_t *registry);
NMO_API void nmo_behavior_registry_foreach(
    const nmo_behavior_registry_t *registry,
    nmo_behavior_registry_visitor_fn visitor,
    void *user_data);
NMO_API const char *nmo_behavior_builtin_get_name(nmo_guid_t guid);
NMO_API size_t nmo_behavior_builtin_count(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BEHAVIOR_REGISTRY_H */
