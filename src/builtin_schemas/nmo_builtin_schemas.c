/**
 * @file builtin_schemas.c
 * @brief Built-in schema definitions for Virtools classes
 */

#include "schema/nmo_schema_registry.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk_api.h"
#include <stddef.h>

/* State save identifiers from CKObject.cpp reference */
#define CK_STATESAVE_OBJECTHIDDEN       0x00000001
#define CK_STATESAVE_OBJECTHIERAHIDDEN  0x00000002

/* Object flags from reference */
#define CK_OBJECT_VISIBLE           0x00000001
#define CK_OBJECT_HIERACHICALHIDE   0x00000002

/* ============================================================================
 * CKObject (0x00000001) - Base class for all Virtools objects
 * ============================================================================ */

static nmo_field_descriptor_t ckobject_fields[] = {
    {
        .name = "id",
        .type = NMO_FIELD_OBJECT_ID,
        .offset = offsetof(nmo_object_t, id),
        .size = sizeof(uint32_t),
        .count = 1,
        .class_id = 0,
        .validation_rule = NULL
    },
    {
        .name = "name",
        .type = NMO_FIELD_STRING,
        .offset = offsetof(nmo_object_t, name),
        .size = sizeof(char *),
        .count = 1,
        .class_id = 0,
        .validation_rule = NULL
    },
    {
        .name = "flags",
        .type = NMO_FIELD_UINT32,
        .offset = offsetof(nmo_object_t, flags),
        .size = sizeof(uint32_t),
        .count = 1,
        .class_id = 0,
        .validation_rule = NULL
    },
};

static void ckobject_serialize(nmo_object_t *obj, nmo_chunk_writer_t *writer) {
    (void) obj;
    (void) writer;
    /* Serialization will be implemented when chunk writer has identifier support */
}

static void ckobject_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser) {
    /* Based on CKObject::Load from reference implementation
     * CKObject.cpp lines 91-107
     *
     * Load reads visibility flags stored as identifiers:
     * - CK_STATESAVE_OBJECTHIDDEN: object is hidden
     * - CK_STATESAVE_OBJECTHIERAHIDDEN: object is hierarchically hidden
     * - If neither: object is visible
     */
    
    if (!obj || !parser) {
        return;
    }
    
    nmo_chunk_t *chunk = (nmo_chunk_t *)parser;
    
    /* StartRead() */
    nmo_chunk_start_read(chunk);
    
    /* Check for visibility flags via identifiers */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    if (result.code == NMO_OK) {
        /* Object is hidden */
        obj->flags &= ~(CK_OBJECT_VISIBLE | CK_OBJECT_HIERACHICALHIDE);
    } else {
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
        if (result.code == NMO_OK) {
            /* Object is hierarchically hidden */
            obj->flags &= ~CK_OBJECT_VISIBLE;
            obj->flags |= CK_OBJECT_HIERACHICALHIDE;
        } else {
            /* Object is visible (default) */
            obj->flags &= ~CK_OBJECT_HIERACHICALHIDE;
            obj->flags |= CK_OBJECT_VISIBLE;
        }
    }
}

static const nmo_schema_descriptor_t ckobject_schema = {
    .class_id = 0x00000001,
    .class_name = "CKObject",
    .parent_class_id = 0,
    .fields = ckobject_fields,
    .field_count = sizeof(ckobject_fields) / sizeof(ckobject_fields[0]),
    .chunk_version = 7,
    .serialize_fn = ckobject_serialize,
    .deserialize_fn = ckobject_deserialize,
};

/* ============================================================================
 * CKBeObject (0x00000002) - Behavioral object base class
 * ============================================================================ */

/* Temporarily disabled - ckbeobject.c needs nmo_beobject_data_t
void ckbeobject_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser);

static void ckbeobject_serialize(nmo_object_t *obj, nmo_chunk_writer_t *writer) {
    (void) obj;
    (void) writer;
    // Serialization will be implemented later
}

static const nmo_schema_descriptor_t ckbeobject_schema = {
    .class_id = 0x00000002,
    .class_name = "CKBeObject",
    .parent_class_id = 0x00000001,
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ckbeobject_serialize,
    .deserialize_fn = ckbeobject_deserialize,
};
*/

/* ============================================================================
 * CKSceneObject (0x00000003) - Scene object base class
 * ============================================================================ */

static void cksceneobject_serialize(nmo_object_t *obj, nmo_chunk_writer_t *writer) {
    (void) obj;
    (void) writer;
    /* CKSceneObject does not override Save() - it inherits from CKObject */
}

static void cksceneobject_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser) {
    /* CKSceneObject does not override Load() - it inherits from CKObject
     * Reference: CKSceneObject.cpp has no Save/Load methods
     * The parser will call parent class (CKObject) deserializer
     */
    (void) obj;
    (void) parser;
}

static const nmo_schema_descriptor_t cksceneobject_schema = {
    .class_id = 0x00000003,
    .class_name = "CKSceneObject",
    .parent_class_id = 0x00000002, /* CKBeObject */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = cksceneobject_serialize,
    .deserialize_fn = cksceneobject_deserialize,
};

/* ============================================================================
 * CK3dEntity (0x00000004) - 3D entity base class
 * ============================================================================ */

static void ck3dentity_serialize(nmo_object_t *obj, nmo_chunk_writer_t *writer) {
    (void) obj;
    (void) writer;
    /* CK3dEntity requires RenderObject implementation - keeping as stub */
}

static void ck3dentity_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser) {
    /* CK3dEntity requires CKRenderObject which is not yet implemented
     * Reference: CK3dEntity.h shows it inherits from CKRenderObject
     * Keeping as stub until RenderObject is implemented
     */
    (void) obj;
    (void) parser;
}

static const nmo_schema_descriptor_t ck3dentity_schema = {
    .class_id = 0x00000004,
    .class_name = "CK3dEntity",
    .parent_class_id = 0x00000003, /* CKSceneObject */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ck3dentity_serialize,
    .deserialize_fn = ck3dentity_deserialize,
};

/* ============================================================================
 * CK3dObject (0x00000005) - 3D object base class  
 * ============================================================================ */

static void ck3dobject_serialize(nmo_object_t *obj, nmo_chunk_writer_t *writer) {
    (void) obj;
    (void) writer;
    /* CK3dObject requires CK3dEntity implementation - keeping as stub */
}

static void ck3dobject_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser) {
    /* CK3dObject requires CK3dEntity which is not yet implemented
     * Reference: CK3dObject would inherit from CK3dEntity
     * Keeping as stub until 3dEntity is implemented
     */
    (void) obj;
    (void) parser;
}

static const nmo_schema_descriptor_t ck3dobject_schema = {
    .class_id = 0x00000005,
    .class_name = "CK3dObject",
    .parent_class_id = 0x00000004, /* CK3dEntity */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ck3dobject_serialize,
    .deserialize_fn = ck3dobject_deserialize,
};

/* ============================================================================
 * CKCamera (0x00000006) - Camera class
 * ============================================================================ */

static void ckcamera_serialize(nmo_object_t *obj, nmo_chunk_writer_t *writer) {
    (void) obj;
    (void) writer;
    /* CKCamera requires CK3dEntity implementation - keeping as stub */
}

static void ckcamera_deserialize(nmo_object_t *obj, nmo_chunk_parser_t *parser) {
    /* CKCamera requires CK3dEntity which is not yet implemented
     * Reference: CKCamera.h shows it inherits from CK3dEntity
     * Keeping as stub until 3dEntity is implemented
     */
    (void) obj;
    (void) parser;
}

static const nmo_schema_descriptor_t ckcamera_schema = {
    .class_id = 0x00000006,
    .class_name = "CKCamera",
    .parent_class_id = 0x00000004, /* CK3dEntity */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ckcamera_serialize,
    .deserialize_fn = ckcamera_deserialize,
};

/* ============================================================================
 * Registry function
 * ============================================================================ */

int nmo_builtin_schemas_register(nmo_schema_registry_t *registry) {
    int result;

    result = nmo_schema_registry_add(registry, &ckobject_schema);
    if (result != NMO_OK) return result;

    /* Temporarily disabled - ckbeobject needs implementation
    result = nmo_schema_registry_add(registry, &ckbeobject_schema);
    if (result != NMO_OK) return result;
    */

    result = nmo_schema_registry_add(registry, &cksceneobject_schema);
    if (result != NMO_OK) return result;

    result = nmo_schema_registry_add(registry, &ck3dentity_schema);
    if (result != NMO_OK) return result;

    result = nmo_schema_registry_add(registry, &ck3dobject_schema);
    if (result != NMO_OK) return result;

    result = nmo_schema_registry_add(registry, &ckcamera_schema);
    if (result != NMO_OK) return result;

    return NMO_OK;
}
