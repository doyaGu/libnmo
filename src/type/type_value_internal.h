#ifndef NMO_TYPE_VALUE_INTERNAL_H
#define NMO_TYPE_VALUE_INTERNAL_H

#include "type/nmo_type_system.h"
#include <stdint.h>

extern const nmo_type_vtable_t nmo_builtin_vtable_int;
extern const nmo_type_vtable_t nmo_builtin_vtable_float;
extern const nmo_type_vtable_t nmo_builtin_vtable_angle;
extern const nmo_type_vtable_t nmo_builtin_vtable_percentage;
extern const nmo_type_vtable_t nmo_builtin_vtable_bool;
extern const nmo_type_vtable_t nmo_builtin_vtable_double;
extern const nmo_type_vtable_t nmo_builtin_vtable_int8;
extern const nmo_type_vtable_t nmo_builtin_vtable_uint8;
extern const nmo_type_vtable_t nmo_builtin_vtable_int16;
extern const nmo_type_vtable_t nmo_builtin_vtable_uint16;
extern const nmo_type_vtable_t nmo_builtin_vtable_uint32;
extern const nmo_type_vtable_t nmo_builtin_vtable_classid;
extern const nmo_type_vtable_t nmo_builtin_vtable_int64;
extern const nmo_type_vtable_t nmo_builtin_vtable_uint64;
extern const nmo_type_vtable_t nmo_builtin_vtable_string;
extern const nmo_type_vtable_t nmo_builtin_vtable_pointer;
extern const nmo_type_vtable_t nmo_builtin_vtable_chunk;
extern const nmo_type_vtable_t nmo_builtin_vtable_guid;
extern const nmo_type_vtable_t nmo_builtin_vtable_object_id;
extern const nmo_type_vtable_t nmo_builtin_vtable_vector2;
extern const nmo_type_vtable_t nmo_builtin_vtable_vector3;
extern const nmo_type_vtable_t nmo_builtin_vtable_vector4;
extern const nmo_type_vtable_t nmo_builtin_vtable_quaternion;
extern const nmo_type_vtable_t nmo_builtin_vtable_matrix;
extern const nmo_type_vtable_t nmo_builtin_vtable_color;
extern const nmo_type_vtable_t nmo_builtin_vtable_rect;
extern const nmo_type_vtable_t nmo_builtin_vtable_eulerangles;
extern const nmo_type_vtable_t nmo_builtin_vtable_box;
extern const nmo_type_vtable_t nmo_builtin_vtable_time;
extern const nmo_type_vtable_t nmo_builtin_vtable_none;
extern const nmo_type_vtable_t nmo_builtin_vtable_voidbuf;
extern const nmo_type_vtable_t nmo_builtin_vtable_array;

extern const nmo_type_vtable_t nmo_type_vtable_enum;
extern const nmo_type_vtable_t nmo_type_vtable_flags;
extern const nmo_type_vtable_t nmo_type_vtable_reflected_struct;
extern const nmo_type_vtable_t nmo_type_vtable_object_ref;

nmo_status_t nmo_reflected_struct_vt_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth);
nmo_status_t nmo_reflected_struct_vt_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string);
nmo_status_t nmo_object_ref_vt_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth);
nmo_status_t nmo_object_ref_vt_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string);

nmo_status_t nmo_builtin_create_zero(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);
void nmo_builtin_destroy_noop(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);
nmo_status_t nmo_builtin_copy_memcpy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena);
nmo_status_t nmo_builtin_copy_string(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena);

bool nmo_vt_equals_int32(const void *a, const void *b);
uint32_t nmo_vt_hash_int32(const void *instance);
bool nmo_vt_equals_uint32(const void *a, const void *b);
uint32_t nmo_vt_hash_uint32(const void *instance);

#define NMO_DEFINE_ZERO_COPY_TYPE_VTABLE( \
    symbol, copy_fn, equals_fn, hash_fn, to_string_fn, from_string_fn) \
    const nmo_type_vtable_t symbol = { \
        .create = nmo_builtin_create_zero, \
        .destroy = nmo_builtin_destroy_noop, \
        .copy = copy_fn, \
        .equals = equals_fn, \
        .hash = hash_fn, \
        .to_string = to_string_fn, \
        .from_string = from_string_fn, \
    };

#define NMO_DEFINE_ZERO_MEMCPY_TYPE_VTABLE( \
    symbol, equals_fn, hash_fn, to_string_fn, from_string_fn) \
    NMO_DEFINE_ZERO_COPY_TYPE_VTABLE( \
        symbol, nmo_builtin_copy_memcpy, equals_fn, hash_fn, \
        to_string_fn, from_string_fn)

void nmo_type_assign_default_vtable(
    nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry);

void nmo_type_refresh_default_vtable_subtree(
    nmo_type_registry_t *registry,
    nmo_type_id_t root_type_id);

nmo_status_t nmo_type_value_to_string_depth_internal(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth);

#endif
