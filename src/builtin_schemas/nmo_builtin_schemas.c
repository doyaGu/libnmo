/**
 * @file builtin_schemas.c
 * @brief Built-in schema definitions for Virtools classes
 */

#include "schema/nmo_schema_registry.h"
#include "format/nmo_object.h"
#include <stddef.h>

/* ============================================================================
 * CKObject (0x00000001) - Base class for all Virtools objects
 * ============================================================================ */

static nmo_field_descriptor ckobject_fields[] = {
    {
        .name = "id",
        .type = NMO_FIELD_OBJECT_ID,
        .offset = offsetof(nmo_object, id),
        .size = sizeof(uint32_t),
        .count = 1,
        .class_id = 0,
        .validation_rule = NULL
    },
    {
        .name = "name",
        .type = NMO_FIELD_STRING,
        .offset = offsetof(nmo_object, name),
        .size = sizeof(char*),
        .count = 1,
        .class_id = 0,
        .validation_rule = NULL
    },
    {
        .name = "flags",
        .type = NMO_FIELD_UINT32,
        .offset = offsetof(nmo_object, flags),
        .size = sizeof(uint32_t),
        .count = 1,
        .class_id = 0,
        .validation_rule = NULL
    },
};

static void ckobject_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    (void)obj;
    (void)writer;
    /* Serialization will be implemented when chunk writer has identifier support */
}

static void ckobject_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    (void)obj;
    (void)parser;
    /* Deserialization will be implemented when chunk parser has identifier support */
}

static const nmo_schema_descriptor ckobject_schema = {
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

static void ckbeobject_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    (void)obj;
    (void)writer;
}

static void ckbeobject_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    (void)obj;
    (void)parser;
}

static const nmo_schema_descriptor ckbeobject_schema = {
    .class_id = 0x00000002,
    .class_name = "CKBeObject",
    .parent_class_id = 0x00000001,  /* CKObject */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ckbeobject_serialize,
    .deserialize_fn = ckbeobject_deserialize,
};

/* ============================================================================
 * CKSceneObject (0x00000003) - Scene object base class
 * ============================================================================ */

static void cksceneobject_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    (void)obj;
    (void)writer;
}

static void cksceneobject_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    (void)obj;
    (void)parser;
}

static const nmo_schema_descriptor cksceneobject_schema = {
    .class_id = 0x00000003,
    .class_name = "CKSceneObject",
    .parent_class_id = 0x00000002,  /* CKBeObject */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = cksceneobject_serialize,
    .deserialize_fn = cksceneobject_deserialize,
};

/* ============================================================================
 * CK3dEntity (0x00000004) - 3D entity base class
 * ============================================================================ */

static void ck3dentity_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    (void)obj;
    (void)writer;
}

static void ck3dentity_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    (void)obj;
    (void)parser;
}

static const nmo_schema_descriptor ck3dentity_schema = {
    .class_id = 0x00000004,
    .class_name = "CK3dEntity",
    .parent_class_id = 0x00000003,  /* CKSceneObject */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ck3dentity_serialize,
    .deserialize_fn = ck3dentity_deserialize,
};

/* ============================================================================
 * CK3dObject (0x00000005) - 3D object class
 * ============================================================================ */

static void ck3dobject_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    (void)obj;
    (void)writer;
}

static void ck3dobject_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    (void)obj;
    (void)parser;
}

static const nmo_schema_descriptor ck3dobject_schema = {
    .class_id = 0x00000005,
    .class_name = "CK3dObject",
    .parent_class_id = 0x00000004,  /* CK3dEntity */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ck3dobject_serialize,
    .deserialize_fn = ck3dobject_deserialize,
};

/* ============================================================================
 * CKCamera (0x00000006) - Camera class
 * ============================================================================ */

static void ckcamera_serialize(nmo_object* obj, nmo_chunk_writer* writer) {
    (void)obj;
    (void)writer;
}

static void ckcamera_deserialize(nmo_object* obj, nmo_chunk_parser* parser) {
    (void)obj;
    (void)parser;
}

static const nmo_schema_descriptor ckcamera_schema = {
    .class_id = 0x00000006,
    .class_name = "CKCamera",
    .parent_class_id = 0x00000004,  /* CK3dEntity */
    .fields = NULL,
    .field_count = 0,
    .chunk_version = 7,
    .serialize_fn = ckcamera_serialize,
    .deserialize_fn = ckcamera_deserialize,
};

/* ============================================================================
 * Registry function
 * ============================================================================ */

int nmo_builtin_schemas_register(nmo_schema_registry* registry) {
    int result;

    result = nmo_schema_registry_add(registry, &ckobject_schema);
    if (result != NMO_OK) return result;

    result = nmo_schema_registry_add(registry, &ckbeobject_schema);
    if (result != NMO_OK) return result;

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
