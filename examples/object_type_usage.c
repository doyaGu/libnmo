/**
 * @file object_type_usage_example.c
 * @brief Complete example of using the rebuilt object type system
 * 
 * This example demonstrates:
 * - Registering object types
 * - Looking up types by GUID and class ID
 * - Checking inheritance
 * - Serializing and deserializing objects
 * - Using the type system for file I/O
 */

#include "object/nmo_object_types.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operations.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Example 1: Basic Type Registration and Lookup
 * ============================================================================ */

void example_basic_usage(void) {
    printf("=== Example 1: Basic Type Registration ===\n\n");

    /* Create arena for memory management */
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);  /* 1MB */
    
    /* Create type registry */
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    
    /* Register builtin primitive types (INT, FLOAT, BOOL) */
    nmo_result_t result = nmo_register_builtin_types(registry);
    if (result.code != NMO_OK) {
        printf("Failed to register builtin types\n");
        nmo_arena_destroy(arena);
        return;
    }
    
    /* Register all Virtools object types */
    result = nmo_register_object_types(registry);
    if (result.code != NMO_OK) {
        printf("Failed to register object types\n");
        nmo_arena_destroy(arena);
        return;
    }
    
    printf("✓ Registered all object types successfully\n\n");
    
    /* Lookup by GUID */
    const nmo_type_descriptor_t *mesh_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKMESH);
    if (mesh_type) {
        printf("Found CKMesh by GUID:\n");
        printf("  Name: %s\n", mesh_type->name);
        printf("  Class ID: %u\n", mesh_type->class_id);
        printf("  Size: %u bytes\n", mesh_type->size);
        printf("  Category: 0x%04X\n", mesh_type->category);
        printf("\n");
    }
    
    /* Lookup by class ID */
    const nmo_type_descriptor_t *camera_type = nmo_get_object_type_by_class_id(
        registry, 34);  /* NMO_CID_CAMERA */
    if (camera_type) {
        printf("Found CKCamera by class ID 34:\n");
        printf("  Name: %s\n", camera_type->name);
        char guid_str[64];
        nmo_guid_format(camera_type->guid, guid_str, sizeof(guid_str));
        printf("  GUID: %s\n", guid_str);
        printf("\n");
    }
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Example 2: Inheritance Checking
 * ============================================================================ */

void example_inheritance(void) {
    printf("=== Example 2: Inheritance Checking ===\n\n");

    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);
    
    /* Get types */
    const nmo_type_descriptor_t *sprite = nmo_type_registry_find_by_class_id(
        registry, 28);  /* CKSprite */
    const nmo_type_descriptor_t *entity2d = nmo_type_registry_find_by_class_id(
        registry, 27);  /* CK2dEntity */
    const nmo_type_descriptor_t *renderobj = nmo_type_registry_find_by_class_id(
        registry, 47);  /* CKRenderObject */
    const nmo_type_descriptor_t *ckobject = nmo_type_registry_find_by_class_id(
        registry, 1);   /* CKObject */
    
    if (!sprite || !entity2d || !renderobj || !ckobject) {
        printf("Failed to find required types\n");
        nmo_arena_destroy(arena);
        return;
    }
    
    /* Check inheritance chain: CKSprite → CK2dEntity → CKRenderObject → CKBeObject → CKObject */
    printf("Inheritance checks for CKSprite:\n");
    printf("  CKSprite derives from CK2dEntity: %s\n",
           nmo_type_is_derived_from(registry, sprite->id, entity2d->id) ? "YES" : "NO");
    printf("  CKSprite derives from CKRenderObject: %s\n",
           nmo_type_is_derived_from(registry, sprite->id, renderobj->id) ? "YES" : "NO");
    printf("  CKSprite derives from CKObject: %s\n",
           nmo_type_is_derived_from(registry, sprite->id, ckobject->id) ? "YES" : "NO");
    printf("\n");
    
    /* Check using GUID helper */
    printf("Is CKMesh a Virtools object? %s\n",
           nmo_is_object_type(registry, NMO_GUID_CKMESH) ? "YES" : "NO");
    printf("Is INT a Virtools object? %s\n",
           nmo_is_object_type(registry, NMO_TYPE_GUID_INT) ? "YES" : "NO");
    printf("\n");
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Example 3: Object Serialization
 * ============================================================================ */

void example_serialization(void) {
    printf("=== Example 3: Object Serialization ===\n\n");

    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);
    
    /* Get CKObject type */
    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CKOBJECT);
    
    if (!ckobject_type || !ckobject_type->vtable) {
        printf("CKObject type not found or no vtable\n");
        nmo_arena_destroy(arena);
        return;
    }
    
    /* Create object state */
    nmo_object_state_t obj_state = {
        .visibility_flags = NMO_CKOBJECT_VISIBLE
    };
    
    /* Create chunk for serialization */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    
    /* Serialize */
    printf("Serializing CKObject (visible)...\n");
    nmo_result_t result = ckobject_type->vtable->serialize(
        &obj_state, chunk, ckobject_type, arena);
    
    if (result.code == NMO_OK) {
        printf("✓ Serialization successful\n");
        printf("  Chunk size: %zu bytes\n", nmo_chunk_get_data_size(chunk));
    } else {
        printf("✗ Serialization failed\n");
    }
    
    /* Deserialize back */
    nmo_chunk_start_read(chunk);
    nmo_object_state_t obj_state_in = {0};
    
    printf("\nDeserializing...\n");
    result = ckobject_type->vtable->deserialize(
        &obj_state_in, chunk, ckobject_type, arena);
    
    if (result.code == NMO_OK) {
        printf("✓ Deserialization successful\n");
        printf("  Visibility flags: 0x%02X\n", obj_state_in.visibility_flags);
        printf("  Is visible: %s\n",
               (obj_state_in.visibility_flags & NMO_CKOBJECT_VISIBLE) ? "YES" : "NO");
    } else {
        printf("✗ Deserialization failed\n");
    }
    printf("\n");
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Example 4: 3D Entity Serialization
 * ============================================================================ */

void example_3d_entity(void) {
    printf("=== Example 4: 3D Entity Serialization ===\n\n");

    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_object_types(registry);
    
    /* Get CK3dEntity type */
    const nmo_type_descriptor_t *entity3d_type = nmo_type_registry_find_by_guid(
        registry, NMO_GUID_CK3DENTITY);
    
    if (!entity3d_type || !entity3d_type->vtable) {
        printf("CK3dEntity type not found or no vtable\n");
        nmo_arena_destroy(arena);
        return;
    }
    
    /* Create 3D entity with identity matrix */
    nmo_3dentity_state_t entity_out = {
        .base = { .visibility_flags = NMO_CKOBJECT_VISIBLE },
        .flags = 0x00000001,
        .world_matrix = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            100.0f, 200.0f, 300.0f, 1.0f  /* Translation */
        },
        .zorder = 10
    };
    
    printf("Created CK3dEntity:\n");
    printf("  Position: (%.1f, %.1f, %.1f)\n",
           entity_out.world_matrix[12],
           entity_out.world_matrix[13],
           entity_out.world_matrix[14]);
    printf("  Z-order: %u\n", entity_out.zorder);
    printf("\n");
    
    /* Serialize */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    nmo_chunk_start_write(chunk);
    
    nmo_result_t result = entity3d_type->vtable->serialize(
        &entity_out, chunk, entity3d_type, arena);
    
    if (result.code == NMO_OK) {
        printf("✓ Serialized CK3dEntity\n");
         size_t chunk_bytes = nmo_chunk_get_data_size(chunk);
         printf("  Chunk size: %zu bytes (%zu DWORDs)\n", 
             chunk_bytes,
             chunk_bytes / 4);
    } else {
        printf("✗ Serialization failed with code %d\n", result.code);
        nmo_arena_destroy(arena);
        return;
    }
    
    /* Deserialize */
    nmo_chunk_start_read(chunk);
    nmo_3dentity_state_t entity_in = {0};
    
    result = entity3d_type->vtable->deserialize(
        &entity_in, chunk, entity3d_type, arena);
    
    if (result.code == NMO_OK) {
        printf("✓ Deserialized CK3dEntity\n");
        printf("  Position: (%.1f, %.1f, %.1f)\n",
               entity_in.world_matrix[12],
               entity_in.world_matrix[13],
               entity_in.world_matrix[14]);
        printf("  Z-order: %u\n", entity_in.zorder);
        printf("  Flags match: %s\n",
               (entity_in.flags == entity_out.flags) ? "YES" : "NO");
    } else {
        printf("✗ Deserialization failed with code %d\n", result.code);
    }
    printf("\n");
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Example 5: Type Category Queries
 * ============================================================================ */

void example_categories(void) {
    printf("=== Example 5: Type Categories ===\n\n");

    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    nmo_register_builtin_types(registry);
    nmo_register_object_types(registry);
    
    /* Query different type categories */
    struct {
        const char *name;
        nmo_guid_t guid;
    } types_to_check[] = {
        {"INT", NMO_TYPE_GUID_INT},
        {"FLOAT", NMO_TYPE_GUID_FLOAT},
        {"CKObject", NMO_GUID_CKOBJECT},
        {"CKMesh", NMO_GUID_CKMESH},
        {"CK3dEntity", NMO_GUID_CK3DENTITY}
    };
    
    printf("Type categories:\n");
    for (size_t i = 0; i < sizeof(types_to_check) / sizeof(types_to_check[0]); i++) {
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(
            registry, types_to_check[i].guid);
        
        if (type) {
            printf("  %-12s: ", types_to_check[i].name);
            if (type->category & NMO_TYPE_CATEGORY_SCALAR) printf("SCALAR ");
            if (type->category & NMO_TYPE_CATEGORY_STRUCT) printf("STRUCT ");
            if (type->category & NMO_TYPE_CATEGORY_OBJECT_REF) printf("OBJECT_REF ");
            if (type->flags & NMO_TYPE_FLAG_SERIALIZABLE) printf("SERIALIZABLE ");
            printf("\n");
        }
    }
    printf("\n");
    
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║  Virtools Object Type System - Usage Examples           ║\n");
    printf("║  Phase 6.1: Unified Type Registry Integration           ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    example_basic_usage();
    example_inheritance();
    example_serialization();
    example_3d_entity();
    example_categories();
    
    printf("All examples completed successfully!\n\n");
    return 0;
}
