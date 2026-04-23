/**
 * @file extension_registry_internal.h
 * @brief Internal accessor functions for extension registry
 *
 * Used by built-in extension plugins that need direct registry access.
 * Not part of the public API.
 */

#ifndef EXTENSION_REGISTRY_INTERNAL_H
#define EXTENSION_REGISTRY_INTERNAL_H

typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;
typedef struct nmo_bb_registry nmo_behavior_registry_t;

nmo_type_registry_t *nmo_extension_registry_get_type_registry(
    nmo_extension_registry_t *registry);
nmo_operation_registry_t *nmo_extension_registry_get_operation_registry(
    nmo_extension_registry_t *registry);
nmo_behavior_registry_t *nmo_extension_registry_get_bb_registry(
    nmo_extension_registry_t *registry);

#endif /* EXTENSION_REGISTRY_INTERNAL_H */
