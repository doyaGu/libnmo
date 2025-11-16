/**
 * @file ckobject_hierarchy.c
 * @brief CKObject class hierarchy using official Virtools SDK class IDs
 *
 * This version uses the builder API and table-driven registration.
 * 
 * CRITICAL: Class IDs and inheritance are from official SDK (reference/include/CKDefines.h).
 * DO NOT guess or modify class IDs - always use the official reference.
 * 
 * Key inheritance facts from official SDK:
 * - CKBehavior(8) inherits from CKSceneObject(11), NOT CKBeObject!
 * - CKBeObject(19) inherits from CKSceneObject(11)
 * - CKRenderObject(47) inherits from CKBeObject(19)
 * - CK2dEntity(27) and CK3dEntity(33) inherit from CKRenderObject(47)
 */

#include "schema/nmo_schema_builder.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <stddef.h>
#include <string.h>

/* =============================================================================
 * CLASS DEFINITION TABLE
 * ============================================================================= */

/**
 * @brief Class descriptor for table-driven registration
 */
typedef struct {
    const char *name;
    nmo_class_id_t class_id;
    const char *parent_name;  /* NULL for CKObject (root) */
    int is_stub;              /* 1 for stub classes, 0 for full */
} ck_class_def_t;

/**
 * @brief Complete CKObject hierarchy in table form
 * 
 * Class IDs are from official Virtools SDK (reference/include/CKDefines.h lines 307-384).
 * The indentation in CKDefines.h shows the inheritance hierarchy.
 * Classes are listed in dependency order (parent before child).
 * Stub classes are marked for documentation purposes.
 */
static const ck_class_def_t CK_CLASSES[] = {
    /* Base classes */
    {"CKObject",                 1,  NULL,            0},  // CKCID_OBJECT
    {"CKParameterIn",            2,  "CKObject",      0},  // CKCID_PARAMETERIN
    {"CKParameterOut",           3,  "CKParameter",   0},  // CKCID_PARAMETEROUT
    {"CKParameterOperation",     4,  "CKObject",      0},  // CKCID_PARAMETEROPERATION
    {"CKStateObject",            5,  "CKObject",      0},  // CKCID_STATE
    {"CKBehaviorLink",           6,  "CKObject",      0},  // CKCID_BEHAVIORLINK
    {"CKBehavior",               8,  "CKSceneObject", 0},  // CKCID_BEHAVIOR (NOT BeObject!)
    {"CKBehaviorIO",             9,  "CKObject",      0},  // CKCID_BEHAVIORIO
    {"CKScene",                 10,  "CKBeObject",    0},  // CKCID_SCENE
    {"CKSceneObject",           11,  "CKObject",      0},  // CKCID_SCENEOBJECT
    {"CKKinematicChain",        13,  "CKObject",      0},  // CKCID_KINEMATICCHAIN
    {"CKObjectAnimation",       15,  "CKSceneObject", 1},  // CKCID_OBJECTANIMATION (stub)
    {"CKAnimation",             16,  "CKSceneObject", 1},  // CKCID_ANIMATION (stub)
    {"CKKeyedAnimation",        18,  "CKAnimation",   1},  // CKCID_KEYEDANIMATION (stub)
    {"CKBeObject",              19,  "CKSceneObject", 0},  // CKCID_BEOBJECT
    {"CKSynchroObject",         20,  "CKObject",      0},  // CKCID_SYNCHRO
    {"CKLevel",                 21,  "CKBeObject",    0},  // CKCID_LEVEL
    {"CKPlace",                 22,  "CKBeObject",    1},  // CKCID_PLACE (stub)
    {"CKGroup",                 23,  "CKBeObject",    0},  // CKCID_GROUP
    {"CKSound",                 24,  "CKBeObject",    1},  // CKCID_SOUND (stub)
    {"CKWaveSound",             25,  "CKSound",       1},  // CKCID_WAVESOUND (stub)
    {"CKMidiSound",             26,  "CKSound",       1},  // CKCID_MIDISOUND (stub)
    {"CK2dEntity",              27,  "CKRenderObject", 0}, // CKCID_2DENTITY
    {"CKSprite",                28,  "CK2dEntity",    0},  // CKCID_SPRITE
    {"CKSpriteText",            29,  "CKSprite",      0},  // CKCID_SPRITETEXT
    {"CKMaterial",              30,  "CKBeObject",    1},  // CKCID_MATERIAL (stub)
    {"CKTexture",               31,  "CKBeObject",    1},  // CKCID_TEXTURE (stub)
    {"CKMesh",                  32,  "CKBeObject",    1},  // CKCID_MESH (stub)
    {"CK3dEntity",              33,  "CKRenderObject", 1}, // CKCID_3DENTITY (stub)
    {"CKCamera",                34,  "CK3dEntity",    1},  // CKCID_CAMERA (stub)
    {"CKTargetCamera",          35,  "CKCamera",      1},  // CKCID_TARGETCAMERA (stub)
    {"CKCurvePoint",            36,  "CK3dEntity",    1},  // CKCID_CURVEPOINT (stub)
    {"CKSprite3D",              37,  "CK3dEntity",    1},  // CKCID_SPRITE3D (stub)
    {"CKLight",                 38,  "CK3dEntity",    1},  // CKCID_LIGHT (stub)
    {"CKTargetLight",           39,  "CKLight",       1},  // CKCID_TARGETLIGHT (stub)
    {"CKCharacter",             40,  "CK3dEntity",    1},  // CKCID_CHARACTER (stub)
    {"CK3dObject",              41,  "CK3dEntity",    1},  // CKCID_3DOBJECT (stub)
    {"CKBodyPart",              42,  "CK3dObject",    1},  // CKCID_BODYPART (stub)
    {"CKCurve",                 43,  "CK3dEntity",    1},  // CKCID_CURVE (stub)
    {"CKParameterLocal",        45,  "CKParameter",   0},  // CKCID_PARAMETERLOCAL
    {"CKParameter",             46,  "CKObject",      0},  // CKCID_PARAMETER
    {"CKRenderObject",          47,  "CKBeObject",    0},  // CKCID_RENDEROBJECT
    {"CKInterfaceObjectManager", 48, "CKObject",     0},   // CKCID_INTERFACEOBJECTMANAGER
    {"CKCriticalSectionObject", 49,  "CKObject",     0},   // CKCID_CRITICALSECTION
    {"CKGrid",                  50,  "CK3dEntity",    1},  // CKCID_GRID (stub)
    {"CKLayer",                 51,  "CKObject",      0},  // CKCID_LAYER
    {"CKDataArray",             52,  "CKBeObject",    0},  // CKCID_DATAARRAY
    {"CKPatchMesh",             53,  "CKMesh",        1},  // CKCID_PATCHMESH (stub)
    {"CKProgressiveMesh",       54,  "CKMesh",        1},  // CKCID_PROGRESSIVEMESH (stub)
};

#define CK_CLASS_COUNT (sizeof(CK_CLASSES) / sizeof(CK_CLASSES[0]))

/* =============================================================================
 * REGISTRATION
 * ============================================================================= */

/**
 * @brief Register single class using builder API
 */
static nmo_result_t register_class(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena,
    const ck_class_def_t *class_def)
{
    /* All CKObject classes are currently defined as empty structs
     * Fields will be added as their serialization formats are documented */
    nmo_schema_builder_t builder = nmo_builder_struct(
        arena,
        class_def->name,
        0,  /* Variable size */
        4   /* Default alignment */
    );
    
    /* TODO: Add common CKObject fields:
     * - object_id (u32)
     * - name (string)
     * - flags (u32)
     * - class_id (u32)
     */
    
    return nmo_builder_build(&builder, registry);
}

/**
 * @brief Register all CKObject hierarchy classes
 */
nmo_result_t nmo_register_ckobject_hierarchy(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    /* Register all classes in order */
    for (size_t i = 0; i < CK_CLASS_COUNT; i++) {
        nmo_result_t result = register_class(registry, arena, &CK_CLASSES[i]);
        if (result.code != NMO_OK) {
            return result;
        }
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * QUERY UTILITIES
 * ============================================================================= */

/**
 * @brief Check if a class is a stub
 * @param class_name Class name
 * @return 1 if stub, 0 if fully defined, -1 if not found
 */
int nmo_ckclass_is_stub(const char *class_name)
{
    for (size_t i = 0; i < CK_CLASS_COUNT; i++) {
        if (strcmp(CK_CLASSES[i].name, class_name) == 0) {
            return CK_CLASSES[i].is_stub;
        }
    }
    return -1;
}

/**
 * @brief Get parent class name
 * @param class_name Class name
 * @return Parent name, or NULL if root or not found
 */
const char *nmo_ckclass_get_parent(const char *class_name)
{
    for (size_t i = 0; i < CK_CLASS_COUNT; i++) {
        if (strcmp(CK_CLASSES[i].name, class_name) == 0) {
            return CK_CLASSES[i].parent_name;
        }
    }
    return NULL;
}

/**
 * @brief Get class count
 * @return Total number of CKObject classes
 */
size_t nmo_ckclass_get_count(void)
{
    return CK_CLASS_COUNT;
}

/**
 * @brief Get class name by class ID
 */
const char *nmo_ckclass_get_name_by_id(uint32_t class_id)
{
    for (size_t i = 0; i < CK_CLASS_COUNT; i++) {
        if (CK_CLASSES[i].class_id == class_id) {
            return CK_CLASSES[i].name;
        }
    }
    return NULL;
}

/**
 * @brief Get class ID by class name
 */
uint32_t nmo_ckclass_get_id_by_name(const char *class_name)
{
    if (class_name == NULL) {
        return 0;
    }
    for (size_t i = 0; i < CK_CLASS_COUNT; i++) {
        if (strcmp(CK_CLASSES[i].name, class_name) == 0) {
            return CK_CLASSES[i].class_id;
        }
    }
    return 0;
}

/**
 * @brief Check if class uses CKBeObject deserializer
 * 
 * Rules based on official Virtools SDK inheritance (reference/include/CKDefines.h):
 * - CKBeObject (19) and all descendants use CKBeObject deserializer
 * - Descendants include: CKScene, CKLevel, CKPlace, CKGroup, CKSound,
 *   CKMaterial, CKTexture, CKMesh, CKDataArray,
 *   CKRenderObject (and all its children: CK2dEntity, CK3dEntity subtrees)
 * - CKBehavior (8) inherits from CKSceneObject, NOT CKBeObject - uses CKObject deserializer
 * 
 * @param class_id Virtools class ID (CKCID_* from CKDefines.h)
 * @return 1 if uses CKBeObject deserializer, 0 if uses CKObject, -1 if not found
 */
int nmo_ckclass_uses_beobject(uint32_t class_id)
{
    /* Look up class */
    const ck_class_def_t *class_def = NULL;
    for (size_t i = 0; i < CK_CLASS_COUNT; i++) {
        if (CK_CLASSES[i].class_id == class_id) {
            class_def = &CK_CLASSES[i];
            break;
        }
    }
    
    if (class_def == NULL) {
        return -1;  /* Not found */
    }
    
    /* Walk up inheritance chain looking for CKBeObject or CKSceneObject */
    const char *current = class_def->name;
    while (current != NULL) {
        if (strcmp(current, "CKBeObject") == 0) {
            return 1;  /* Inherits from CKBeObject */
        }
        if (strcmp(current, "CKObject") == 0) {
            return 0;  /* Direct CKObject child */
        }
        /* Move to parent */
        current = nmo_ckclass_get_parent(current);
    }
    
    return 0;  /* Default to CKObject */
}
