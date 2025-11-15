/**
 * @file test_ckobject_hierarchy.c
 * @brief Test CKObject class hierarchy schema registration
 */

#include "../../tests/test_framework.h"
#include "schema/nmo_builtin_types.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <string.h>

/**
 * @brief Test basic CKObject hierarchy registration
 */
TEST(ckobject_hierarchy, basic_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register hierarchy */
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test base class lookups
 */
TEST(ckobject_hierarchy, base_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Look up base classes */
    const nmo_schema_type_t *ckobject = nmo_schema_registry_find_by_name(registry, "CKObject");
    ASSERT_NE(NULL, ckobject);
    ASSERT_STR_EQ("CKObject", ckobject->name);
    ASSERT_EQ(NMO_TYPE_STRUCT, ckobject->kind);
    
    const nmo_schema_type_t *cksceneobject = nmo_schema_registry_find_by_name(registry, "CKSceneObject");
    ASSERT_NE(NULL, cksceneobject);
    ASSERT_STR_EQ("CKSceneObject", cksceneobject->name);
    
    const nmo_schema_type_t *ckbeobject = nmo_schema_registry_find_by_name(registry, "CKBeObject");
    ASSERT_NE(NULL, ckbeobject);
    ASSERT_STR_EQ("CKBeObject", ckbeobject->name);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test render hierarchy classes
 */
TEST(ckobject_hierarchy, render_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Look up render classes */
    const nmo_schema_type_t *ckrenderobject = nmo_schema_registry_find_by_name(registry, "CKRenderObject");
    ASSERT_NE(NULL, ckrenderobject);
    
    const nmo_schema_type_t *ck2dentity = nmo_schema_registry_find_by_name(registry, "CK2dEntity");
    ASSERT_NE(NULL, ck2dentity);
    
    const nmo_schema_type_t *ck3dentity = nmo_schema_registry_find_by_name(registry, "CK3dEntity");
    ASSERT_NE(NULL, ck3dentity);
    
    const nmo_schema_type_t *ck3dobject = nmo_schema_registry_find_by_name(registry, "CK3dObject");
    ASSERT_NE(NULL, ck3dobject);
    
    const nmo_schema_type_t *ckcamera = nmo_schema_registry_find_by_name(registry, "CKCamera");
    ASSERT_NE(NULL, ckcamera);
    
    const nmo_schema_type_t *cklight = nmo_schema_registry_find_by_name(registry, "CKLight");
    ASSERT_NE(NULL, cklight);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test behavior hierarchy classes
 */
TEST(ckobject_hierarchy, behavior_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Look up behavior classes */
    const nmo_schema_type_t *ckbehavior = nmo_schema_registry_find_by_name(registry, "CKBehavior");
    ASSERT_NE(NULL, ckbehavior);
    ASSERT_STR_EQ("CKBehavior", ckbehavior->name);
    
    const nmo_schema_type_t *ckscriptbehavior = nmo_schema_registry_find_by_name(registry, "CKScriptBehavior");
    ASSERT_NE(NULL, ckscriptbehavior);
    ASSERT_STR_EQ("CKScriptBehavior", ckscriptbehavior->name);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test resource classes
 */
TEST(ckobject_hierarchy, resource_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Look up resource classes */
    const nmo_schema_type_t *ckmesh = nmo_schema_registry_find_by_name(registry, "CKMesh");
    ASSERT_NE(NULL, ckmesh);
    
    const nmo_schema_type_t *cktexture = nmo_schema_registry_find_by_name(registry, "CKTexture");
    ASSERT_NE(NULL, cktexture);
    
    const nmo_schema_type_t *cksound = nmo_schema_registry_find_by_name(registry, "CKSound");
    ASSERT_NE(NULL, cksound);
    
    const nmo_schema_type_t *ckwavesound = nmo_schema_registry_find_by_name(registry, "CKWaveSound");
    ASSERT_NE(NULL, ckwavesound);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test utility classes
 */
TEST(ckobject_hierarchy, utility_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Look up utility classes */
    const nmo_schema_type_t *ckgroup = nmo_schema_registry_find_by_name(registry, "CKGroup");
    ASSERT_NE(NULL, ckgroup);
    
    const nmo_schema_type_t *cklevel = nmo_schema_registry_find_by_name(registry, "CKLevel");
    ASSERT_NE(NULL, cklevel);
    
    const nmo_schema_type_t *ckscene = nmo_schema_registry_find_by_name(registry, "CKScene");
    ASSERT_NE(NULL, ckscene);
    
    const nmo_schema_type_t *ckdataarray = nmo_schema_registry_find_by_name(registry, "CKDataArray");
    ASSERT_NE(NULL, ckdataarray);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test parameter classes
 */
TEST(ckobject_hierarchy, parameter_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Look up parameter classes */
    const nmo_schema_type_t *ckparameter = nmo_schema_registry_find_by_name(registry, "CKParameter");
    ASSERT_NE(NULL, ckparameter);
    
    const nmo_schema_type_t *ckparameterlocal = nmo_schema_registry_find_by_name(registry, "CKParameterLocal");
    ASSERT_NE(NULL, ckparameterlocal);
    
    const nmo_schema_type_t *ckparameterin = nmo_schema_registry_find_by_name(registry, "CKParameterIn");
    ASSERT_NE(NULL, ckparameterin);
    
    const nmo_schema_type_t *ckparameterout = nmo_schema_registry_find_by_name(registry, "CKParameterOut");
    ASSERT_NE(NULL, ckparameterout);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test class count
 */
TEST(ckobject_hierarchy, class_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_result_t result = nmo_register_ckobject_hierarchy(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Count registered classes - we expect 45 classes total */
    size_t count = 0;
    const char *class_names[] = {
        "CKObject", "CKSceneObject", "CKBeObject",
        "CKRenderObject", "CK2dEntity", "CKSprite", "CKSpriteText",
        "CK3dEntity", "CK3dObject", "CKBodyPart",
        "CKCamera", "CKTargetCamera", "CKLight", "CKTargetLight",
        "CKCharacter", "CKCurve", "CKCurvePoint", "CKGrid", "CKPlace", "CKSprite3D",
        "CKBehavior", "CKScriptBehavior",
        "CKDataArray", "CKGroup", "CKLevel",
        "CKMesh", "CKPatchMesh", "CKScene",
        "CKSound", "CKMidiSound", "CKWaveSound", "CKTexture",
        "CKBehaviorIO", "CKBehaviorLink",
        "CKInterfaceObjectManager", "CKKinematicChain", "CKLayer",
        "CKParameter", "CKParameterLocal", "CKParameterOut",
        "CKParameterIn", "CKParameterOperation",
        "CKSynchroObject", "CKCriticalSectionObject", "CKStateObject"
    };
    
    for (size_t i = 0; i < sizeof(class_names)/sizeof(class_names[0]); i++) {
        const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, class_names[i]);
        if (type != NULL) {
            count++;
        }
    }
    
    ASSERT_EQ(45, count);  /* Should have registered all 45 classes */
    
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(ckobject_hierarchy, basic_registration);
    REGISTER_TEST(ckobject_hierarchy, base_classes);
    REGISTER_TEST(ckobject_hierarchy, render_classes);
    REGISTER_TEST(ckobject_hierarchy, behavior_classes);
    REGISTER_TEST(ckobject_hierarchy, resource_classes);
    REGISTER_TEST(ckobject_hierarchy, utility_classes);
    REGISTER_TEST(ckobject_hierarchy, parameter_classes);
    REGISTER_TEST(ckobject_hierarchy, class_count);
TEST_MAIN_END()
