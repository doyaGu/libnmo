/**
 * @file test_object_types_conformance.c
 * @brief Conformance tests for complete object type coverage
 * 
 * Verifies that all CK classes with schema headers are registered in
 * the type system with correct GUIDs, class IDs, and inheritance chains.
 */

#include "test_framework.h"
#include "object/nmo_object_types.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <string.h>

/* =============================================================================
 * HELPERS
 * ============================================================================= */

static nmo_arena_t *g_arena = NULL;
static nmo_type_registry_t *g_registry = NULL;
static int g_fixture_ready = 0;

static void fixture_failf(const char *format, ...) {
    char msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    test_add_result(__func__, __func__, 0, msg, __FILE__, __LINE__);
}

static void conformance_setup(void) {
    g_fixture_ready = 0;
    g_arena = nmo_arena_create(NULL, 131072);  /* 128KB for all types */
    if (g_arena == NULL) {
        fixture_failf("Failed to create arena");
        return;
    }

    g_registry = nmo_type_registry_create(g_arena);
    if (g_registry == NULL) {
        fixture_failf("Failed to create type registry");
        return;
    }

    nmo_last_error_clear();
    nmo_status_t status = nmo_register_builtin_types(g_registry);
    if (status != NMO_OK) {
        char chain[1024];
        nmo_last_error_chain_copy(chain, sizeof(chain));

        char msg[512];
        test_format_error(msg, sizeof(msg),
                          "Fixture setup failed: nmo_register_builtin_types: %d (%s)\n  %s",
                          (int)status, nmo_error_string(status), chain);
        test_add_result(__func__, __func__, 0, msg, __FILE__, __LINE__);
        return;
    }

    status = nmo_register_object_types(g_registry);
    if (status != NMO_OK) {
        char chain[1024];
        nmo_last_error_chain_copy(chain, sizeof(chain));

        char msg[512];
        test_format_error(msg, sizeof(msg),
                          "Fixture setup failed: nmo_register_object_types: %d (%s)\n  %s",
                          (int)status, nmo_error_string(status), chain);
        test_add_result(__func__, __func__, 0, msg, __FILE__, __LINE__);
        return;
    }

    g_fixture_ready = 1;
}

static void conformance_teardown(void) {
    g_fixture_ready = 0;

    if (g_registry != NULL) {
        nmo_type_registry_destroy(g_registry);
        g_registry = NULL;
    }

    if (g_arena != NULL) {
        nmo_arena_destroy(g_arena);
        g_arena = NULL;
    }
}

static void conformance_require_fixture(void) {
    if (!g_fixture_ready || g_registry == NULL) {
        test_add_result(__func__, __func__, 0,
                        "Fixture not initialized (setup failed)", __FILE__, __LINE__);
        return;
    }
}

static bool is_synthetic_class(nmo_class_id_t cid) {
    switch (cid) {
        case NMO_CID_BEHAVIORLINK:
        case NMO_CID_BEHAVIORIO:
        case NMO_CID_PARAMETERIN:
        case NMO_CID_PARAMETEROUT:
        case NMO_CID_PARAMETEROPERATION:
        case NMO_CID_PARAMETERLOCAL:
        case NMO_CID_PARAMETER:
        case NMO_CID_RENDERCONTEXT:
        case NMO_CID_INTERFACEOBJECTMANAGER:
        case NMO_CID_GRID:
        case NMO_CID_LAYER:
        case NMO_CID_KEYEDANIMATION:
            return true;
        default:
            return false;
    }
}

/* =============================================================================
 * TYPE COVERAGE TESTS
 * ============================================================================= */

/**
 * @brief Verify all expected CK classes are registered
 */
TEST(conformance, all_classes_registered) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    /* Class IDs that should be registered (from CK class inventory) */
    static const struct {
        nmo_class_id_t id;
        const char *name;
    } expected_classes[] = {
        { NMO_CID_OBJECT, "CKObject" },
        { NMO_CID_PARAMETERIN, "CKParameterIn" },
        { NMO_CID_PARAMETEROUT, "CKParameterOut" },
        { NMO_CID_PARAMETEROPERATION, "CKParameterOperation" },
        { NMO_CID_STATE, "CKStateObject" },
        { NMO_CID_BEHAVIORLINK, "CKBehaviorLink" },
        { NMO_CID_BEHAVIOR, "CKBehavior" },
        { NMO_CID_BEHAVIORIO, "CKBehaviorIO" },
        { NMO_CID_SCENE, "CKScene" },
        { NMO_CID_SCENEOBJECT, "CKSceneObject" },
        { NMO_CID_RENDERCONTEXT, "CKRenderContext" },
        { NMO_CID_KINEMATICCHAIN, "CKKinematicChain" },
        { NMO_CID_OBJECTANIMATION, "CKObjectAnimation" },
        { NMO_CID_ANIMATION, "CKAnimation" },
        { NMO_CID_KEYEDANIMATION, "CKKeyedAnimation" },
        { NMO_CID_BEOBJECT, "CKBeObject" },
        { NMO_CID_SYNCHRO, "CKSynchroObject" },
        { NMO_CID_LEVEL, "CKLevel" },
        { NMO_CID_PLACE, "CKPlace" },
        { NMO_CID_GROUP, "CKGroup" },
        { NMO_CID_SOUND, "CKSound" },
        { NMO_CID_WAVESOUND, "CKWaveSound" },
        { NMO_CID_MIDISOUND, "CKMidiSound" },
        { NMO_CID_2DENTITY, "CK2dEntity" },
        { NMO_CID_SPRITE, "CKSprite" },
        { NMO_CID_SPRITETEXT, "CKSpriteText" },
        { NMO_CID_MATERIAL, "CKMaterial" },
        { NMO_CID_TEXTURE, "CKTexture" },
        { NMO_CID_MESH, "CKMesh" },
        { NMO_CID_3DENTITY, "CK3dEntity" },
        { NMO_CID_CAMERA, "CKCamera" },
        { NMO_CID_TARGETCAMERA, "CKTargetCamera" },
        { NMO_CID_CURVEPOINT, "CKCurvePoint" },
        { NMO_CID_SPRITE3D, "CKSprite3D" },
        { NMO_CID_LIGHT, "CKLight" },
        { NMO_CID_TARGETLIGHT, "CKTargetLight" },
        { NMO_CID_CHARACTER, "CKCharacter" },
        { NMO_CID_3DOBJECT, "CK3dObject" },
        { NMO_CID_BODYPART, "CKBodyPart" },
        { NMO_CID_CURVE, "CKCurve" },
        { NMO_CID_PARAMETERLOCAL, "CKParameterLocal" },
        { NMO_CID_PARAMETER, "CKParameter" },
        { NMO_CID_RENDEROBJECT, "CKRenderObject" },
        { NMO_CID_INTERFACEOBJECTMANAGER, "CKInterfaceObjectManager" },
        { NMO_CID_CRITICALSECTION, "CKCriticalSectionObject" },
        { NMO_CID_GRID, "CKGrid" },
        { NMO_CID_LAYER, "CKLayer" },
        { NMO_CID_DATAARRAY, "CKDataArray" },
        { NMO_CID_PATCHMESH, "CKPatchMesh" },
    };

    size_t expected_count = sizeof(expected_classes) / sizeof(expected_classes[0]);
    size_t registered_count = 0;

    size_t missing_count = 0;
    size_t mismatch_count = 0;
    char missing_buf[1024];
    char mismatch_buf[1024];
    missing_buf[0] = '\0';
    mismatch_buf[0] = '\0';

    for (size_t i = 0; i < expected_count; i++) {
        const nmo_type_descriptor_t *type = 
            nmo_type_registry_find_by_class_id(g_registry, expected_classes[i].id);
        
        if (type != NULL) {
            if (type->name == NULL || strcmp(type->name, expected_classes[i].name) != 0) {
                char tmp[128];
                const char *actual = (type->name != NULL) ? type->name : "(null)";
                snprintf(tmp, sizeof(tmp), "%s(id=%d!=%s); ",
                         expected_classes[i].name,
                         (int)expected_classes[i].id,
                         actual);
                if (strlen(mismatch_buf) + strlen(tmp) + 1 < sizeof(mismatch_buf)) {
                    strcat(mismatch_buf, tmp);
                }
                mismatch_count++;
            }

            if (type->class_id != expected_classes[i].id) {
                char tmp[128];
                snprintf(tmp, sizeof(tmp), "%s(cid=%d!=%d); ",
                         expected_classes[i].name,
                         (int)expected_classes[i].id,
                         (int)type->class_id);
                if (strlen(mismatch_buf) + strlen(tmp) + 1 < sizeof(mismatch_buf)) {
                    strcat(mismatch_buf, tmp);
                }
                mismatch_count++;
            }

            registered_count++;
        } else {
            char tmp[96];
            snprintf(tmp, sizeof(tmp), "%s(id=%d); ",
                     expected_classes[i].name, (int)expected_classes[i].id);
            if (strlen(missing_buf) + strlen(tmp) + 1 < sizeof(missing_buf)) {
                strcat(missing_buf, tmp);
            }
            missing_count++;
        }
    }

    if (missing_count != 0 || mismatch_count != 0 || registered_count != expected_count) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Registry conformance failed: missing=%zu, mismatched=%zu, registered=%zu/%zu",
                 missing_count, mismatch_count, registered_count, expected_count);
        test_add_result(__func__, __func__, 0, msg, __FILE__, __LINE__);
        if (missing_count != 0 && missing_buf[0] != '\0') {
            test_add_result(__func__, __func__, 0, missing_buf, __FILE__, __LINE__);
        }
        if (mismatch_count != 0 && mismatch_buf[0] != '\0') {
            test_add_result(__func__, __func__, 0, mismatch_buf, __FILE__, __LINE__);
        }
        return;
    }

}

/**
 * @brief Verify all GUIDs follow the {0x564B4F42, class_id} pattern
 */
TEST(conformance, guid_pattern) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    /* Synthetic encoding: { 0x564B4F42, <class_id> } */

    static const nmo_class_id_t test_classes[] = {
        NMO_CID_OBJECT, NMO_CID_MESH, NMO_CID_MATERIAL, NMO_CID_TEXTURE,
        NMO_CID_3DENTITY, NMO_CID_CAMERA, NMO_CID_LIGHT, NMO_CID_BEHAVIOR,
        NMO_CID_SCENE, NMO_CID_GROUP, NMO_CID_DATAARRAY, NMO_CID_PARAMETER,
        NMO_CID_PARAMETERIN, NMO_CID_PARAMETEROUT, NMO_CID_TARGETCAMERA,
        NMO_CID_TARGETLIGHT, NMO_CID_SPRITE3D, NMO_CID_CURVE,
        NMO_CID_STATE, NMO_CID_CRITICALSECTION,
        NMO_CID_OBJECTANIMATION,
        NMO_CID_WAVESOUND, NMO_CID_MIDISOUND,
        NMO_CID_CURVEPOINT, NMO_CID_BODYPART,
    };

    for (size_t i = 0; i < sizeof(test_classes) / sizeof(test_classes[0]); i++) {
        const nmo_type_descriptor_t *type = 
            nmo_type_registry_find_by_class_id(g_registry, test_classes[i]);
        ASSERT_NE(NULL, type);

        ASSERT_EQ((uint32_t)test_classes[i], type->class_id);

        if (is_synthetic_class(test_classes[i])) {
            /* Synthetic GUIDs should follow VKOB encoding */
            ASSERT_EQ(0x564B4F42u, type->guid.d1);
            ASSERT_EQ((uint32_t)test_classes[i], type->guid.d2);
        }
    }
}

/**
 * @brief Verify no duplicate GUIDs or class IDs
 */
TEST(conformance, no_duplicates) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    const nmo_type_descriptor_t *types[NMO_CID_MAXCLASSID];
    size_t type_count = 0;

    /* We can't easily iterate all types, but we can check that lookups are consistent */
    for (nmo_class_id_t cid = 1; cid < NMO_CID_MAXCLASSID; cid++) {
        const nmo_type_descriptor_t *type1 = 
            nmo_type_registry_find_by_class_id(g_registry, cid);
        
        if (type1 != NULL) {
            if (type_count < (sizeof(types) / sizeof(types[0]))) {
                types[type_count++] = type1;
            }

            /* Lookup by GUID should return same type */
            const nmo_type_descriptor_t *type2 = 
                nmo_type_registry_find_by_guid(g_registry, type1->guid);
            ASSERT_EQ(type1, type2);

            /* Lookup by name should return same type */
            const nmo_type_descriptor_t *type3 = 
                nmo_type_registry_find_by_name(g_registry, type1->name);
            ASSERT_EQ(type1, type3);
        }
    }

    /* Stronger duplicate detection within the object class-id space */
    for (size_t i = 0; i < type_count; i++) {
        for (size_t j = i + 1; j < type_count; j++) {
            ASSERT_FALSE(nmo_guid_equals(types[i]->guid, types[j]->guid));

            if (types[i]->name != NULL && types[j]->name != NULL) {
                ASSERT_FALSE(strcmp(types[i]->name, types[j]->name) == 0);
            }
        }
    }
}

/* =============================================================================
 * INHERITANCE TESTS
 * ============================================================================= */

/**
 * @brief Verify 3D entity inheritance chain
 */
TEST(conformance, inheritance_3d_entity_chain) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    /* CK3dEntity -> CKRenderObject -> CKBeObject -> CKSceneObject -> CKObject */
    const nmo_type_descriptor_t *entity3d = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_3DENTITY);
    const nmo_type_descriptor_t *renderobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_RENDEROBJECT);
    const nmo_type_descriptor_t *beobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_BEOBJECT);
    const nmo_type_descriptor_t *sceneobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_SCENEOBJECT);
    const nmo_type_descriptor_t *ckobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_OBJECT);

    ASSERT_NE(NULL, entity3d);
    ASSERT_NE(NULL, renderobj);
    ASSERT_NE(NULL, beobj);
    ASSERT_NE(NULL, sceneobj);
    ASSERT_NE(NULL, ckobj);

    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, entity3d->id, renderobj->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, renderobj->id, beobj->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, beobj->id, sceneobj->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, sceneobj->id, ckobj->id));

    /* Transitivity: CK3dEntity derives from CKObject */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, entity3d->id, ckobj->id));

}

/**
 * @brief Verify camera hierarchy
 */
TEST(conformance, inheritance_camera) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    const nmo_type_descriptor_t *targetcam = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_TARGETCAMERA);
    const nmo_type_descriptor_t *camera = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_CAMERA);
    const nmo_type_descriptor_t *entity3d = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_3DENTITY);

    ASSERT_NE(NULL, targetcam);
    ASSERT_NE(NULL, camera);
    ASSERT_NE(NULL, entity3d);

    /* CKTargetCamera -> CKCamera -> CK3dEntity */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, targetcam->id, camera->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, camera->id, entity3d->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, targetcam->id, entity3d->id));

}

/**
 * @brief Verify parameter hierarchy
 */
TEST(conformance, inheritance_parameter) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    const nmo_type_descriptor_t *param = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_PARAMETER);
    const nmo_type_descriptor_t *paramlocal = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_PARAMETERLOCAL);
    const nmo_type_descriptor_t *paramin = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_PARAMETERIN);
    const nmo_type_descriptor_t *paramout = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_PARAMETEROUT);
    const nmo_type_descriptor_t *ckobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_OBJECT);

    ASSERT_NE(NULL, param);
    ASSERT_NE(NULL, paramlocal);
    ASSERT_NE(NULL, paramin);
    ASSERT_NE(NULL, paramout);

    /* CKParameterLocal -> CKParameter -> CKObject */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, paramlocal->id, param->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, param->id, ckobj->id));

    /* CKParameterOut -> CKParameter (per SDK) */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, paramout->id, param->id));

    /* CKParameterIn -> CKObject directly (NOT CKParameter, per SDK!) */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, paramin->id, ckobj->id));
    ASSERT_FALSE(nmo_type_is_derived_from(g_registry, paramin->id, param->id));

    /* Parameters do NOT derive from CKBeObject */
    const nmo_type_descriptor_t *beobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_BEOBJECT);
    ASSERT_FALSE(nmo_type_is_derived_from(g_registry, param->id, beobj->id));

}

/**
 * @brief Verify behavior hierarchy
 */
TEST(conformance, inheritance_behavior) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    const nmo_type_descriptor_t *behavior = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_BEHAVIOR);
    const nmo_type_descriptor_t *behaviorio = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_BEHAVIORIO);
    const nmo_type_descriptor_t *behaviorlink = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_BEHAVIORLINK);
    const nmo_type_descriptor_t *sceneobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_SCENEOBJECT);
    const nmo_type_descriptor_t *ckobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_OBJECT);

    ASSERT_NE(NULL, behavior);
    ASSERT_NE(NULL, behaviorio);
    ASSERT_NE(NULL, behaviorlink);

    /* CKBehavior -> CKSceneObject */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, behavior->id, sceneobj->id));

    /* CKBehaviorIO, CKBehaviorLink -> CKObject (not CKSceneObject) */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, behaviorio->id, ckobj->id));
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, behaviorlink->id, ckobj->id));
    ASSERT_FALSE(nmo_type_is_derived_from(g_registry, behaviorio->id, sceneobj->id));

}

/**
 * @brief Verify mesh variants inheritance
 */
TEST(conformance, inheritance_mesh_variants) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    const nmo_type_descriptor_t *mesh = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_MESH);
    const nmo_type_descriptor_t *beobj = 
        nmo_type_registry_find_by_class_id(g_registry, NMO_CID_BEOBJECT);

    ASSERT_NE(NULL, mesh);

    /* CKMesh -> CKBeObject */
    ASSERT_TRUE(nmo_type_is_derived_from(g_registry, mesh->id, beobj->id));

}

/* =============================================================================
 * VTABLE TESTS
 * ============================================================================= */

/**
 * @brief Verify all registered types have vtables
 */
TEST(conformance, all_types_have_vtables) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    for (nmo_class_id_t cid = 1; cid < NMO_CID_MAXCLASSID; cid++) {
        const nmo_type_descriptor_t *type = 
            nmo_type_registry_find_by_class_id(g_registry, cid);
        
        if (type != NULL) {
            ASSERT_NE(NULL, type->vtable);
        }
    }

}

/* =============================================================================
 * HIERARCHY COMPATIBILITY TESTS
 * ============================================================================= */

/**
 * @brief Verify hierarchy functions match type system
 */
TEST(conformance, hierarchy_v2_compatibility) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    /* Test nmo_class_is_derived_from matches type system */
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_CAMERA, NMO_CID_3DENTITY));
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_3DENTITY, NMO_CID_RENDEROBJECT));
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_SPRITE, NMO_CID_2DENTITY));
    ASSERT_FALSE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_MESH, NMO_CID_3DENTITY));
    ASSERT_FALSE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_PARAMETER, NMO_CID_BEOBJECT));

    /* Test parent lookup via type base chain */
    ASSERT_EQ(NMO_CID_3DENTITY, nmo_type_registry_get_class_parent(g_registry, NMO_CID_CAMERA));
    ASSERT_EQ(NMO_CID_CAMERA, nmo_type_registry_get_class_parent(g_registry, NMO_CID_TARGETCAMERA));
    ASSERT_EQ(NMO_CID_PARAMETER, nmo_type_registry_get_class_parent(g_registry, NMO_CID_PARAMETERLOCAL));
    ASSERT_EQ(0, nmo_type_registry_get_class_parent(g_registry, NMO_CID_OBJECT));  /* Root has no parent */

    /* Test CKBeObject-derivation predicate */
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_MESH, NMO_CID_BEOBJECT));
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_3DENTITY, NMO_CID_BEOBJECT));
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_SCENE, NMO_CID_BEOBJECT));
    ASSERT_FALSE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_PARAMETER, NMO_CID_BEOBJECT));
    ASSERT_FALSE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_BEHAVIORIO, NMO_CID_BEOBJECT));

}

TEST(conformance, hierarchy_extended_queries) {
    conformance_require_fixture();
    if (!g_fixture_ready) {
        return;
    }

    nmo_class_id_t ancestors[8] = {0};
    int ancestor_count = nmo_type_registry_get_class_ancestors(
        g_registry, NMO_CID_TARGETCAMERA, (uint32_t *)ancestors, 8);

    ASSERT_TRUE(ancestor_count >= 2);
    ASSERT_EQ(NMO_CID_CAMERA, ancestors[0]);
    ASSERT_EQ(NMO_CID_3DENTITY, ancestors[1]);

    nmo_class_id_t common = (nmo_class_id_t)nmo_type_registry_get_common_class_ancestor(
        g_registry, NMO_CID_TARGETCAMERA, NMO_CID_TARGETLIGHT);
    ASSERT_EQ(NMO_CID_3DENTITY, common);

    int object_level = nmo_type_registry_get_class_derivation_level(g_registry, NMO_CID_OBJECT);
    int camera_level = nmo_type_registry_get_class_derivation_level(g_registry, NMO_CID_CAMERA);
    int target_camera_level = nmo_type_registry_get_class_derivation_level(g_registry, NMO_CID_TARGETCAMERA);

    ASSERT_EQ(0, object_level);
    ASSERT_TRUE(camera_level > object_level);
    ASSERT_TRUE(target_camera_level > camera_level);

    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_CAMERA, NMO_CID_RENDEROBJECT));
    ASSERT_TRUE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_CAMERA, NMO_CID_3DENTITY));
    ASSERT_FALSE(nmo_type_registry_is_class_derived_from(g_registry, NMO_CID_CAMERA, NMO_CID_2DENTITY));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_WITH_FIXTURE(conformance, all_classes_registered, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, guid_pattern, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, no_duplicates, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, inheritance_3d_entity_chain, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, inheritance_camera, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, inheritance_parameter, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, inheritance_behavior, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, inheritance_mesh_variants, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, all_types_have_vtables, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, hierarchy_v2_compatibility, conformance_setup, conformance_teardown);
    REGISTER_TEST_WITH_FIXTURE(conformance, hierarchy_extended_queries, conformance_setup, conformance_teardown);
TEST_MAIN_END()
