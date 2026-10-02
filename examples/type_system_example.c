/**
 * @file type_system_example.c
 * @brief Example demonstrating runtime type registration
 *
 * This example shows how to:
 * 1. Create a type registry
 * 2. Register builtin types
 * 3. Register enum, flags, and struct types dynamically
 * 4. Query types by name and GUID
 */

#include "nmo.h"
#include "type/nmo_operations.h"
#include "type/nmo_dynamic_types.h"
#include <stdio.h>

static nmo_status_t register_example_types(nmo_type_registry_t *registry) {
    nmo_enum_value_def_t blend_values[] = {
        {"BlendZero", 0, "Zero"},
        {"BlendOne", 1, "One"},
        {"BlendSrcColor", 2, "Source color"},
        {"BlendInvSrcColor", 3, "Inverse source color"}
    };

    nmo_enum_type_def_t blend_enum = {
        .name = "BlendMode",
        .description = "Example blend mode enum",
        .guid = NMO_GUID(0x12345678, 0x00000001),
        .values = blend_values,
        .value_count = sizeof(blend_values) / sizeof(blend_values[0]),
        .default_value = 0
    };

    nmo_status_t result = nmo_type_registry_register_enum(registry, &blend_enum, NULL);
    if (result != NMO_OK) {
        return result;
    }

    nmo_flags_bit_def_t access_bits[] = {
        {"Read", 0x01, "Read permission"},
        {"Write", 0x02, "Write permission"},
        {"Execute", 0x04, "Execute permission"}
    };

    nmo_flags_type_def_t access_flags = {
        .name = "AccessFlags",
        .description = "Example access flags",
        .guid = NMO_GUID(0x12345678, 0x00000002),
        .bits = access_bits,
        .bit_count = sizeof(access_bits) / sizeof(access_bits[0]),
        .default_value = 0x01
    };

    result = nmo_type_registry_register_flags(registry, &access_flags, NULL);
    if (result != NMO_OK) {
        return result;
    }

    nmo_struct_field_def_t vector_fields[] = {
        {.name = "x", .type_name = "float", .description = "X component"},
        {.name = "y", .type_name = "float", .description = "Y component"},
        {.name = "z", .type_name = "float", .description = "Z component"}
    };

    nmo_struct_type_def_t vector_def = {
        .name = "Vector3",
        .description = "Example 3D vector",
        .guid = NMO_GUID(0x12345678, 0x00000003),
        .fields = vector_fields,
        .field_count = sizeof(vector_fields) / sizeof(vector_fields[0]),
        .alignment = 0,
        .packed = false
    };

    return nmo_type_registry_register_struct(registry, &vector_def, NULL);
}

int main(void) {
    printf("=== Type System Example ===\n\n");

    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return 1;
    }

    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    if (!registry) {
        fprintf(stderr, "Error: Failed to create type registry\n");
        nmo_arena_destroy(arena);
        return 1;
    }

    nmo_status_t result = nmo_register_builtin_types(registry);
    if (result != NMO_OK) {
        fprintf(stderr, "Error: Failed to register builtin types (%s)\n",
                nmo_error_string(result));
        nmo_arena_destroy(arena);
        return 1;
    }

    result = register_example_types(registry);
    if (result != NMO_OK) {
        fprintf(stderr, "Error: Failed to register example types (%s)\n",
                nmo_error_string(result));
        nmo_arena_destroy(arena);
        return 1;
    }

    const nmo_type_descriptor_t *vector_type =
        nmo_type_registry_find_by_name(registry, "Vector3");
    if (vector_type) {
        printf("Registered type: %s\n", vector_type->name);
        printf("  Size: %u bytes\n", vector_type->size);
        printf("  Fields: %zu\n", vector_type->field_count);
    }

    nmo_guid_t blend_guid = NMO_GUID(0x12345678, 0x00000001);
    const nmo_type_descriptor_t *blend_type =
        nmo_type_registry_find_by_guid(registry, blend_guid);
    if (blend_type) {
        printf("\nRegistered enum: %s\n", blend_type->name);
    }

    nmo_arena_destroy(arena);
    printf("\nDone.\n");
    return 0;
}
