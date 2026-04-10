/**
 * @file builtin_types.c
 * @brief Builtin type registration
 *
 * Registers builtin CK2-like parameter value types in the GUID-based type system.
 */

#include "type/nmo_type_system.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include "core/nmo_array.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include "type/nmo_type_guids.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Builtin Type VTables
 * ============================================================================ */

/* Helpers implemented in type_string_converters.c */
nmo_status_t nmo_builtin_create_zero(void *instance, const nmo_type_descriptor_t *type, void *context);
void nmo_builtin_destroy_noop(void *instance, const nmo_type_descriptor_t *type, void *context);
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

bool nmo_equals_float_bits(const void *a, const void *b);
uint32_t nmo_hash_float_bits(const void *instance);
bool nmo_equals_double_bits(const void *a, const void *b);
uint32_t nmo_hash_double_bits(const void *instance);

bool nmo_equals_string_value(const void *a, const void *b);
uint32_t nmo_hash_string_value(const void *instance);

bool nmo_vt_equals_int32(const void *a, const void *b);
uint32_t nmo_vt_hash_int32(const void *instance);
bool nmo_vt_equals_uint32(const void *a, const void *b);
uint32_t nmo_vt_hash_uint32(const void *instance);
bool nmo_vt_equals_int8(const void *a, const void *b);
uint32_t nmo_vt_hash_int8(const void *instance);
bool nmo_vt_equals_uint8(const void *a, const void *b);
uint32_t nmo_vt_hash_uint8(const void *instance);
bool nmo_vt_equals_int16(const void *a, const void *b);
uint32_t nmo_vt_hash_int16(const void *instance);
bool nmo_vt_equals_uint16(const void *a, const void *b);
uint32_t nmo_vt_hash_uint16(const void *instance);
bool nmo_vt_equals_int64(const void *a, const void *b);
uint32_t nmo_vt_hash_int64(const void *instance);
bool nmo_vt_equals_uint64(const void *a, const void *b);
uint32_t nmo_vt_hash_uint64(const void *instance);

bool nmo_equals_bool(const void *a, const void *b);
uint32_t nmo_hash_bool(const void *instance);
bool nmo_equals_pointer(const void *a, const void *b);
uint32_t nmo_hash_pointer(const void *instance);
bool nmo_equals_guid(const void *a, const void *b);
uint32_t nmo_hash_guid(const void *instance);
bool nmo_equals_object_id(const void *a, const void *b);
uint32_t nmo_hash_object_id(const void *instance);

bool nmo_equals_bytes_vector2(const void *a, const void *b);
uint32_t nmo_hash_bytes_vector2(const void *instance);
bool nmo_equals_bytes_vector3(const void *a, const void *b);
uint32_t nmo_hash_bytes_vector3(const void *instance);
bool nmo_equals_bytes_vector4(const void *a, const void *b);
uint32_t nmo_hash_bytes_vector4(const void *instance);
bool nmo_equals_bytes_quaternion(const void *a, const void *b);
uint32_t nmo_hash_bytes_quaternion(const void *instance);
bool nmo_equals_bytes_matrix(const void *a, const void *b);
uint32_t nmo_hash_bytes_matrix(const void *instance);
bool nmo_equals_bytes_color(const void *a, const void *b);
uint32_t nmo_hash_bytes_color(const void *instance);
bool nmo_equals_bytes_rect(const void *a, const void *b);
uint32_t nmo_hash_bytes_rect(const void *instance);
bool nmo_equals_bytes_eulerangles(const void *a, const void *b);
uint32_t nmo_hash_bytes_eulerangles(const void *instance);
bool nmo_equals_bytes_box(const void *a, const void *b);
uint32_t nmo_hash_bytes_box(const void *instance);

nmo_status_t nmo_vt_to_string_int32(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_int32(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_uint32(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_uint32(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_int8(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_int8(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_uint8(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_uint8(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_int16(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_int16(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_uint16(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_uint16(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_int64(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_int64(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_uint64(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_uint64(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_float(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_float(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_double(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_double(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_bool(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_bool(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_pointer(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_pointer(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_guid(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_guid(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_object_id(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_object_id(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_vector2(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_vector2(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_vector3(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_vector3(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_vector4(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_vector4(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_quaternion(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_quaternion(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_matrix(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_matrix(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_color(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_color(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_rect(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_rect(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_eulerangles(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_eulerangles(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

nmo_status_t nmo_vt_to_string_box(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context);
nmo_status_t nmo_vt_from_string_box(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context);

const nmo_type_vtable_t nmo_builtin_vtable_int = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_int32,
    .hash = nmo_vt_hash_int32,
    .to_string = nmo_vt_to_string_int32,
    .from_string = nmo_vt_from_string_int32,
};

const nmo_type_vtable_t nmo_builtin_vtable_float = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_float_bits,
    .hash = nmo_hash_float_bits,
    .to_string = nmo_vt_to_string_float,
    .from_string = nmo_vt_from_string_float,
};

const nmo_type_vtable_t nmo_builtin_vtable_angle = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_float_bits,
    .hash = nmo_hash_float_bits,
    .to_string = nmo_vt_to_string_float,
    .from_string = nmo_vt_from_string_float,
};

const nmo_type_vtable_t nmo_builtin_vtable_percentage = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_float_bits,
    .hash = nmo_hash_float_bits,
    .to_string = nmo_vt_to_string_float,
    .from_string = nmo_vt_from_string_float,
};

const nmo_type_vtable_t nmo_builtin_vtable_bool = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bool,
    .hash = nmo_hash_bool,
    .to_string = nmo_vt_to_string_bool,
    .from_string = nmo_vt_from_string_bool,
};

const nmo_type_vtable_t nmo_builtin_vtable_double = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_double_bits,
    .hash = nmo_hash_double_bits,
    .to_string = nmo_vt_to_string_double,
    .from_string = nmo_vt_from_string_double,
};

const nmo_type_vtable_t nmo_builtin_vtable_int8 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_int8,
    .hash = nmo_vt_hash_int8,
    .to_string = nmo_vt_to_string_int8,
    .from_string = nmo_vt_from_string_int8,
};

const nmo_type_vtable_t nmo_builtin_vtable_uint8 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_uint8,
    .hash = nmo_vt_hash_uint8,
    .to_string = nmo_vt_to_string_uint8,
    .from_string = nmo_vt_from_string_uint8,
};

const nmo_type_vtable_t nmo_builtin_vtable_int16 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_int16,
    .hash = nmo_vt_hash_int16,
    .to_string = nmo_vt_to_string_int16,
    .from_string = nmo_vt_from_string_int16,
};

const nmo_type_vtable_t nmo_builtin_vtable_uint16 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_uint16,
    .hash = nmo_vt_hash_uint16,
    .to_string = nmo_vt_to_string_uint16,
    .from_string = nmo_vt_from_string_uint16,
};

const nmo_type_vtable_t nmo_builtin_vtable_uint32 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_uint32,
    .hash = nmo_vt_hash_uint32,
    .to_string = nmo_vt_to_string_uint32,
    .from_string = nmo_vt_from_string_uint32,
};

const nmo_type_vtable_t nmo_builtin_vtable_classid = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_uint32,
    .hash = nmo_vt_hash_uint32,
    .to_string = nmo_vt_to_string_uint32,
    .from_string = nmo_vt_from_string_uint32,
};

const nmo_type_vtable_t nmo_builtin_vtable_int64 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_int64,
    .hash = nmo_vt_hash_int64,
    .to_string = nmo_vt_to_string_int64,
    .from_string = nmo_vt_from_string_int64,
};

const nmo_type_vtable_t nmo_builtin_vtable_uint64 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_vt_equals_uint64,
    .hash = nmo_vt_hash_uint64,
    .to_string = nmo_vt_to_string_uint64,
    .from_string = nmo_vt_from_string_uint64,
};

const nmo_type_vtable_t nmo_builtin_vtable_string = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_string,
    .equals = nmo_equals_string_value,
    .hash = nmo_hash_string_value,
    .to_string = nmo_vt_to_string_string,
    .from_string = nmo_vt_from_string_string,
};

const nmo_type_vtable_t nmo_builtin_vtable_pointer = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_pointer,
    .hash = nmo_hash_pointer,
    .to_string = nmo_vt_to_string_pointer,
    .from_string = nmo_vt_from_string_pointer,
};

const nmo_type_vtable_t nmo_builtin_vtable_chunk = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_pointer,
    .hash = nmo_hash_pointer,
    .to_string = nmo_vt_to_string_pointer,
    .from_string = nmo_vt_from_string_pointer,
};

const nmo_type_vtable_t nmo_builtin_vtable_guid = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_guid,
    .hash = nmo_hash_guid,
    .to_string = nmo_vt_to_string_guid,
    .from_string = nmo_vt_from_string_guid,
};

const nmo_type_vtable_t nmo_builtin_vtable_object_id = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_object_id,
    .hash = nmo_hash_object_id,
    .to_string = nmo_vt_to_string_object_id,
    .from_string = nmo_vt_from_string_object_id,
};

const nmo_type_vtable_t nmo_builtin_vtable_vector2 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_vector2,
    .hash = nmo_hash_bytes_vector2,
    .to_string = nmo_vt_to_string_vector2,
    .from_string = nmo_vt_from_string_vector2,
};

const nmo_type_vtable_t nmo_builtin_vtable_vector3 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_vector3,
    .hash = nmo_hash_bytes_vector3,
    .to_string = nmo_vt_to_string_vector3,
    .from_string = nmo_vt_from_string_vector3,
};

const nmo_type_vtable_t nmo_builtin_vtable_vector4 = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_vector4,
    .hash = nmo_hash_bytes_vector4,
    .to_string = nmo_vt_to_string_vector4,
    .from_string = nmo_vt_from_string_vector4,
};

const nmo_type_vtable_t nmo_builtin_vtable_quaternion = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_quaternion,
    .hash = nmo_hash_bytes_quaternion,
    .to_string = nmo_vt_to_string_quaternion,
    .from_string = nmo_vt_from_string_quaternion,
};

const nmo_type_vtable_t nmo_builtin_vtable_matrix = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_matrix,
    .hash = nmo_hash_bytes_matrix,
    .to_string = nmo_vt_to_string_matrix,
    .from_string = nmo_vt_from_string_matrix,
};

const nmo_type_vtable_t nmo_builtin_vtable_color = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_color,
    .hash = nmo_hash_bytes_color,
    .to_string = nmo_vt_to_string_color,
    .from_string = nmo_vt_from_string_color,
};

const nmo_type_vtable_t nmo_builtin_vtable_rect = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_rect,
    .hash = nmo_hash_bytes_rect,
    .to_string = nmo_vt_to_string_rect,
    .from_string = nmo_vt_from_string_rect,
};

const nmo_type_vtable_t nmo_builtin_vtable_eulerangles = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_eulerangles,
    .hash = nmo_hash_bytes_eulerangles,
    .to_string = nmo_vt_to_string_eulerangles,
    .from_string = nmo_vt_from_string_eulerangles,
};

const nmo_type_vtable_t nmo_builtin_vtable_box = {
    .create = nmo_builtin_create_zero,
    .destroy = nmo_builtin_destroy_noop,
    .copy = nmo_builtin_copy_memcpy,
    .equals = nmo_equals_bytes_box,
    .hash = nmo_hash_bytes_box,
    .to_string = nmo_vt_to_string_box,
    .from_string = nmo_vt_from_string_box,
};

static nmo_status_t nmo_builtin_create_array(void *instance, const nmo_type_descriptor_t *type, void *context) {
    (void)type;
    (void)context;

    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "array create: NULL instance");
    }

    nmo_array_t *arr = (nmo_array_t *)instance;
    return nmo_array_init(arr, 1, 0, NULL);
}

static void nmo_builtin_destroy_array(void *instance, const nmo_type_descriptor_t *type, void *context) {
    (void)type;
    (void)context;

    if (instance == NULL) {
        return;
    }

    nmo_array_dispose((nmo_array_t *)instance);
}

static nmo_status_t nmo_builtin_copy_array(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena
) {
    (void)type;
    (void)arena;

    if (src == NULL || dst == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "array copy: NULL argument");
    }

    const nmo_array_t *src_arr = (const nmo_array_t *)src;
    nmo_array_t *dst_arr = (nmo_array_t *)dst;

    /* Ensure destination starts empty to avoid leaking pre-existing backing store. */
    memset(dst_arr, 0, sizeof(*dst_arr));

    if (src_arr->element_size == 0) {
        /* Treat as empty byte array for resilience. */
        return nmo_array_init(dst_arr, 1, 0, NULL);
    }

    return nmo_array_clone(src_arr, dst_arr, &src_arr->allocator);
}

static bool nmo_builtin_equals_array(const void *a, const void *b) {
    if (a == b) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }

    const nmo_array_t *aa = (const nmo_array_t *)a;
    const nmo_array_t *bb = (const nmo_array_t *)b;

    if (aa->element_size != bb->element_size || aa->count != bb->count) {
        return false;
    }
    if (aa->count == 0) {
        return true;
    }
    if (aa->data == NULL || bb->data == NULL) {
        return false;
    }

    size_t byte_len = aa->count * aa->element_size;
    return memcmp(aa->data, bb->data, byte_len) == 0;
}

static uint32_t nmo_builtin_hash_array(const void *instance) {
    if (instance == NULL) {
        return 0;
    }

    const nmo_array_t *arr = (const nmo_array_t *)instance;
    const uint32_t seed = nmo_hash_int32((uint32_t)arr->element_size) ^ nmo_hash_int32((uint32_t)arr->count);

    if (arr->count == 0 || arr->data == NULL || arr->element_size == 0) {
        return seed;
    }

    size_t byte_len = arr->count * arr->element_size;
    return nmo_murmur3_32(arr->data, byte_len, seed);
}

static nmo_status_t nmo_builtin_to_string_array(
    const void *value,
    const nmo_type_descriptor_t *type,
    char *buffer,
    size_t buffer_size,
    void *context
) {
    (void)type;
    (void)context;

    if (buffer == NULL || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "array to_string: invalid buffer");
    }

    if (value == NULL) {
        int written = snprintf(buffer, buffer_size, "null");
        if (written < 0 || (size_t)written >= buffer_size) {
            NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "array to_string: buffer too small");
        }
        NMO_RETURN_OK();
    }

    const nmo_array_t *arr = (const nmo_array_t *)value;
    int written = snprintf(buffer, buffer_size, "array(count=%zu, element_size=%zu)", arr->count, arr->element_size);
    if (written < 0 || (size_t)written >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "array to_string: buffer too small");
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_builtin_from_string_array(
    void *value,
    const nmo_type_descriptor_t *type,
    const char *string,
    void *context
) {
    (void)type;
    (void)context;

    if (value == NULL || string == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "array from_string: NULL argument");
    }

    /* Minimal parser: accept "[]" as an empty byte array. */
    if (strcmp(string, "[]") != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "array from_string: expected []");
    }

    nmo_array_t *arr = (nmo_array_t *)value;
    nmo_array_dispose(arr);
    return nmo_array_init(arr, 1, 0, NULL);
}

const nmo_type_vtable_t nmo_builtin_vtable_array = {
    .create = nmo_builtin_create_array,
    .destroy = nmo_builtin_destroy_array,
    .copy = nmo_builtin_copy_array,
    .equals = nmo_builtin_equals_array,
    .hash = nmo_builtin_hash_array,
    .to_string = nmo_builtin_to_string_array,
    .from_string = nmo_builtin_from_string_array,
};

/* ============================================================================
 * Type Registration
 * ============================================================================ */
nmo_status_t nmo_register_builtin_types(nmo_type_registry_t *type_registry);

static nmo_status_t nmo_add_builtin_alias(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *alias)
{
    if (!type_registry || !alias) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL type_registry or alias");
    }

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "builtin alias target type not found");
    }

    return nmo_type_registry_add_name_alias(type_registry, type->id, alias);
}

nmo_status_t nmo_register_builtin_types(nmo_type_registry_t *type_registry) {
    if (!type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry");
    }

    /* Register basic scalar types */

    /* CKPGUID_NONE: placeholder/no-type (hidden) */
    nmo_type_descriptor_t none_type = {
        .guid = CKPGUID_NONE,
        .name = "none",
        .size = 0,
        .alignment = 1,
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR | NMO_TYPE_CATEGORY_HIDDEN,
        .flags = 0,
        .id = NMO_TYPE_ID_INVALID,
        .description = "No type / placeholder",
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    nmo_status_t result = nmo_type_registry_register(type_registry, &none_type);
    if (result != NMO_OK) {
        return result;
    }

    /* CKPGUID_VOIDBUF: variable-sized raw buffer; fixed-size metadata only */
    nmo_type_descriptor_t voidbuf_type = {
        .guid = CKPGUID_VOIDBUF,
        .name = "voidbuf",
        .size = 0,
        .alignment = 1,
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Raw variable-sized buffer",
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &voidbuf_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int_type = {
        .guid = CKPGUID_INT,
        .name = "int",
        .size = sizeof(int32_t),
        .alignment = alignof(int32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int,
    };

    result = nmo_type_registry_register(type_registry, &int_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register FLOAT type */
    nmo_type_descriptor_t float_type = {
        .guid = CKPGUID_FLOAT,
        .name = "float",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_float,
    };

    result = nmo_type_registry_register(type_registry, &float_type);
    if (result != NMO_OK) {
        return result;
    }

    /* CK2 derived scalar types */
    nmo_type_descriptor_t angle_type = {
        .guid = CKPGUID_ANGLE,
        .name = "angle",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = CKPGUID_FLOAT,
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Angle (float)",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_angle,
    };

    result = nmo_type_registry_register(type_registry, &angle_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t percentage_type = {
        .guid = CKPGUID_PERCENTAGE,
        .name = "percentage",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = CKPGUID_FLOAT,
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Percentage (float)",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_percentage,
    };

    result = nmo_type_registry_register(type_registry, &percentage_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register BOOL type */
    nmo_type_descriptor_t bool_type = {
        .guid = CKPGUID_BOOL,
        .name = "bool",
        .size = sizeof(int32_t),          /* Virtools BOOL is 4 bytes (int-sized) */
        .alignment = sizeof(int32_t),
        .class_id = 0,
        .base_type = CKPGUID_INT, /* Virtools: Boolean derived from Integer */
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_bool,
    };

    result = nmo_type_registry_register(type_registry, &bool_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t double_type = {
        .guid = CKPGUID_DOUBLE,
        .name = "double",
        .size = sizeof(double),
        .alignment = alignof(double),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_double,
    };

    result = nmo_type_registry_register(type_registry, &double_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int8_type = {
        .guid = CKPGUID_INT8,
        .name = "int8",
        .size = sizeof(int8_t),
        .alignment = alignof(int8_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int8,
    };

    result = nmo_type_registry_register(type_registry, &int8_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint8_type = {
        .guid = CKPGUID_UINT8,
        .name = "uint8",
        .size = sizeof(uint8_t),
        .alignment = alignof(uint8_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint8,
    };

    result = nmo_type_registry_register(type_registry, &uint8_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int16_type = {
        .guid = CKPGUID_INT16,
        .name = "int16",
        .size = sizeof(int16_t),
        .alignment = alignof(int16_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int16,
    };

    result = nmo_type_registry_register(type_registry, &int16_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint16_type = {
        .guid = CKPGUID_UINT16,
        .name = "uint16",
        .size = sizeof(uint16_t),
        .alignment = alignof(uint16_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint16,
    };

    result = nmo_type_registry_register(type_registry, &uint16_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint32_type = {
        .guid = CKPGUID_UINT32,
        .name = "uint32",
        .size = sizeof(uint32_t),
        .alignment = alignof(uint32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint32,
    };

    result = nmo_type_registry_register(type_registry, &uint32_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t classid_type = {
        .guid = CKPGUID_CLASSID,
        .name = "classid",
        .size = sizeof(uint32_t),
        .alignment = alignof(uint32_t),
        .class_id = 0,
        .base_type = CKPGUID_UINT32,
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Virtools class id (uint32)",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_classid,
    };

    result = nmo_type_registry_register(type_registry, &classid_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int64_type = {
        .guid = CKPGUID_INT64,
        .name = "int64",
        .size = sizeof(int64_t),
        .alignment = alignof(int64_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int64,
    };

    result = nmo_type_registry_register(type_registry, &int64_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint64_type = {
        .guid = CKPGUID_UINT64,
        .name = "uint64",
        .size = sizeof(uint64_t),
        .alignment = alignof(uint64_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint64,
    };

    result = nmo_type_registry_register(type_registry, &uint64_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t string_type = {
        .guid = CKPGUID_STRING,
        .name = "string",
        .size = sizeof(char *),
        .alignment = alignof(char *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_string,
    };

    result = nmo_type_registry_register(type_registry, &string_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t pointer_type = {
        .guid = CKPGUID_POINTER,
        .name = "pointer",
        .size = sizeof(void *),
        .alignment = alignof(void *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_POINTER,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_pointer,
    };

    result = nmo_type_registry_register(type_registry, &pointer_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t chunk_type = {
        .guid = CKPGUID_STATECHUNK,
        .name = "chunk",
        .size = sizeof(void *),
        .alignment = alignof(void *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_POINTER,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_chunk,
    };

    result = nmo_type_registry_register(type_registry, &chunk_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t guid_type = {
        .guid = CKPGUID_GUID,
        .name = "guid",
        .size = sizeof(nmo_guid_t),
        .alignment = alignof(nmo_guid_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_guid,
    };

    result = nmo_type_registry_register(type_registry, &guid_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t object_id_type = {
        .guid = CKPGUID_ID,
        .name = "object_id",
        .size = sizeof(nmo_object_id_t),
        .alignment = alignof(nmo_object_id_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_object_id,
    };

    result = nmo_type_registry_register(type_registry, &object_id_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register Virtools math types */
    nmo_type_descriptor_t vec2_type = {
        .guid = CKPGUID_2DVECTOR,
        .name = "vector2",
        .size = sizeof(nmo_vector2_t),
        .alignment = alignof(nmo_vector2_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_vector2,
    };

    result = nmo_type_registry_register(type_registry, &vec2_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t vec3_type = {
        .guid = CKPGUID_VECTOR,
        .name = "vector3",
        .size = sizeof(nmo_vector_t),
        .alignment = alignof(nmo_vector_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_vector3,
    };

    result = nmo_type_registry_register(type_registry, &vec3_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t vec4_type = {
        .guid = CKPGUID_VECTOR4,
        .name = "vector4",
        .size = sizeof(nmo_vector4_t),
        .alignment = alignof(nmo_vector4_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_vector4,
    };

    result = nmo_type_registry_register(type_registry, &vec4_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t quat_type = {
        .guid = CKPGUID_QUATERNION,
        .name = "quaternion",
        .size = sizeof(nmo_quaternion_t),
        .alignment = alignof(nmo_quaternion_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_quaternion,
    };

    result = nmo_type_registry_register(type_registry, &quat_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t mat_type = {
        .guid = CKPGUID_MATRIX,
        .name = "matrix",
        .size = sizeof(nmo_matrix_t),
        .alignment = alignof(nmo_matrix_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_matrix,
    };

    result = nmo_type_registry_register(type_registry, &mat_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t color_type = {
        .guid = CKPGUID_COLOR,
        .name = "color",
        .size = sizeof(nmo_color_t),
        .alignment = alignof(nmo_color_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_color,
    };

    result = nmo_type_registry_register(type_registry, &color_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t rect_type = {
        .guid = CKPGUID_RECT,
        .name = "rect",
        .size = sizeof(nmo_rect_t),
        .alignment = alignof(nmo_rect_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = (const nmo_type_field_t[]){
            { .name = "left",   .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, left),   .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "top",    .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, top),    .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "right",  .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, right),  .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "bottom", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, bottom), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
        },
        .field_count = 4,
        .vtable = &nmo_builtin_vtable_rect,
    };

    result = nmo_type_registry_register(type_registry, &rect_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Euler angles (3 floats) */
    nmo_type_descriptor_t euler_type = {
        .guid = CKPGUID_EULERANGLES,
        .name = "euler_angles",
        .size = sizeof(nmo_eulerangles_t),
        .alignment = alignof(nmo_eulerangles_t),
        .class_id = 0,
        .base_type = CKPGUID_VECTOR, /* Virtools: Euler Angles derived from Vector */
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = (const nmo_type_field_t[]){
            { .name = "x", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_eulerangles_t, x), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "y", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_eulerangles_t, y), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "z", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_eulerangles_t, z), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
        },
        .field_count = 3,
        .vtable = &nmo_builtin_vtable_eulerangles,
    };

    result = nmo_type_registry_register(type_registry, &euler_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Bounding box: {min, max} */
    nmo_type_descriptor_t box_type = {
        .guid = CKPGUID_BOX,
        .name = "box",
        .size = sizeof(nmo_box_t),
        .alignment = alignof(nmo_box_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = (const nmo_type_field_t[]){
            { .name = "min", .description = NULL, .type_guid = CKPGUID_VECTOR, .offset = offsetof(nmo_box_t, min), .size = sizeof(nmo_vector_t), .flags = NMO_FIELD_REQUIRED },
            { .name = "max", .description = NULL, .type_guid = CKPGUID_VECTOR, .offset = offsetof(nmo_box_t, max), .size = sizeof(nmo_vector_t), .flags = NMO_FIELD_REQUIRED },
        },
        .field_count = 2,
        .vtable = &nmo_builtin_vtable_box,
    };

    result = nmo_type_registry_register(type_registry, &box_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Container/meta types present in CK2 */
    nmo_type_descriptor_t array_type = {
        .guid = CKPGUID_ARRAY,
        .name = "array",
        .size = sizeof(nmo_array_t),
        .alignment = alignof(nmo_array_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_ARRAY | NMO_TYPE_CATEGORY_HIDDEN,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Array meta-type",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_array,
    };

    result = nmo_type_registry_register(type_registry, &array_type);
    if (result != NMO_OK) {
        return result;
    }

    /*
     * Name aliases
     *
     * The type parser and DSL are intentionally case-sensitive, and tests
     * expect legacy/uppercase spellings (e.g. INT, UINT32, STRING) to resolve.
     */
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_INT, "INT"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_FLOAT, "FLOAT"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_BOOL, "BOOL"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_STRING, "STRING"));

    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "UINT32"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT64, "UINT64"));

    /* C-like convenience spellings used in parser tests */
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "uint"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "UINT"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "uint32_t"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT64, "uint64_t"));

    /* Platform-sized unsigned type */
    if (sizeof(size_t) == 8) {
        NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT64, "size_t"));
    } else {
        NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "size_t"));
    }

    /* Virtools-style names */
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_VECTOR, "VxVector3"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_MATRIX, "VxMatrix"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_COLOR, "VxColor"));

    NMO_RETURN_OK();
}
