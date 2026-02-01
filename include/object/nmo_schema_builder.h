/**
 * @file nmo_schema_builder.h
 * @brief Schema builder API used by object schema registration
 */

#ifndef NMO_SCHEMA_BUILDER_H
#define NMO_SCHEMA_BUILDER_H

#include "object/nmo_schema.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_param_meta nmo_param_meta_t;

typedef struct nmo_schema_builder {
    nmo_arena_t *arena;
    nmo_schema_type_t *type;
    nmo_schema_field_t *fields_buffer;
    size_t fields_count;
    size_t fields_capacity;
    nmo_enum_value_t *enum_buffer;
    size_t enum_count;
    size_t enum_capacity;
    uint32_t since_version;
    uint32_t deprecated_version;
    uint32_t removed_version;
    const nmo_param_meta_t *param_meta;
    nmo_error_t *first_error;
} nmo_schema_builder_t;

nmo_schema_builder_t nmo_builder_scalar(
    nmo_arena_t *arena,
    const char *name,
    nmo_type_kind_t kind,
    size_t size);

nmo_schema_builder_t nmo_builder_struct(
    nmo_arena_t *arena,
    const char *name,
    size_t size,
    size_t align);

nmo_schema_builder_t nmo_builder_array(
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_type_t *element_type);

nmo_schema_builder_t nmo_builder_fixed_array(
    nmo_arena_t *arena,
    const char *name,
    const nmo_schema_type_t *element_type,
    size_t length);

nmo_schema_builder_t nmo_builder_enum(
    nmo_arena_t *arena,
    const char *name,
    nmo_type_kind_t base_type);

nmo_schema_builder_t *nmo_builder_add_field(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset);

nmo_schema_builder_t *nmo_builder_add_field_ex(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset,
    uint32_t annotations);

nmo_schema_builder_t *nmo_builder_add_field_versioned(
    nmo_schema_builder_t *builder,
    const char *field_name,
    const nmo_schema_type_t *field_type,
    size_t field_offset,
    uint32_t since_version,
    uint32_t deprecated_version);

nmo_result_t nmo_builder_add_field_manual(
    nmo_schema_builder_t *builder,
    const nmo_schema_field_t *field);

nmo_schema_builder_t *nmo_builder_add_enum_value(
    nmo_schema_builder_t *builder,
    const char *value_name,
    int32_t value);

nmo_schema_builder_t *nmo_builder_set_vtable(
    nmo_schema_builder_t *builder,
    const nmo_schema_vtable_t *vtable);

const nmo_schema_type_t *nmo_builder_build_type(nmo_schema_builder_t *builder);

nmo_result_t nmo_builder_build(
    nmo_schema_builder_t *builder,
    nmo_schema_registry_t *registry);

nmo_result_t nmo_register_scalar_types(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

nmo_schema_builder_t *nmo_builder_set_since_version(
    nmo_schema_builder_t *builder,
    uint32_t version);

nmo_schema_builder_t *nmo_builder_set_deprecated_version(
    nmo_schema_builder_t *builder,
    uint32_t version);

nmo_schema_builder_t *nmo_builder_set_removed_version(
    nmo_schema_builder_t *builder,
    uint32_t version);

nmo_schema_builder_t *nmo_builder_set_version_range(
    nmo_schema_builder_t *builder,
    uint32_t since_version,
    uint32_t deprecated_version,
    uint32_t removed_version);

nmo_schema_builder_t *nmo_builder_set_param_meta(
    nmo_schema_builder_t *builder,
    const nmo_param_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_BUILDER_H */
