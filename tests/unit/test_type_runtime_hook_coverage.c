#include "test_framework.h"
#include "session/nmo_context.h"
#include "type/nmo_type_system.h"
#include "object/nmo_class_ids.h"

static const nmo_class_id_t k_registered_object_cids[] = {
    NMO_CID_OBJECT,
    NMO_CID_SCENEOBJECT,
    NMO_CID_BEOBJECT,
    NMO_CID_RENDEROBJECT,
    NMO_CID_PARAMETER,
    NMO_CID_BEHAVIOR,
    NMO_CID_BEHAVIORIO,
    NMO_CID_BEHAVIORLINK,
    NMO_CID_PARAMETERIN,
    NMO_CID_PARAMETEROUT,
    NMO_CID_PARAMETERLOCAL,
    NMO_CID_PARAMETEROPERATION,
    NMO_CID_SCENE,
    NMO_CID_LEVEL,
    NMO_CID_GROUP,
    NMO_CID_DATAARRAY,
    NMO_CID_PLACE,
    NMO_CID_2DENTITY,
    NMO_CID_SPRITE,
    NMO_CID_SPRITETEXT,
    NMO_CID_3DENTITY,
    NMO_CID_3DOBJECT,
    NMO_CID_CAMERA,
    NMO_CID_TARGETCAMERA,
    NMO_CID_LIGHT,
    NMO_CID_TARGETLIGHT,
    NMO_CID_CHARACTER,
    NMO_CID_BODYPART,
    NMO_CID_SPRITE3D,
    NMO_CID_CURVE,
    NMO_CID_CURVEPOINT,
    NMO_CID_MATERIAL,
    NMO_CID_TEXTURE,
    NMO_CID_MESH,
    NMO_CID_PATCHMESH,
    NMO_CID_GRID,
    NMO_CID_LAYER,
    NMO_CID_ANIMATION,
    NMO_CID_KEYEDANIMATION,
    NMO_CID_OBJECTANIMATION,
    NMO_CID_RENDERCONTEXT,
    NMO_CID_KINEMATICCHAIN,
    NMO_CID_SYNCHRO,
    NMO_CID_STATE,
    NMO_CID_CRITICALSECTION,
    NMO_CID_SOUND,
    NMO_CID_WAVESOUND,
    NMO_CID_MIDISOUND,
    NMO_CID_INTERFACEOBJECTMANAGER
};

TEST(type_runtime_hook_coverage, registered_types_have_explicit_remap_hook) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    for (size_t i = 0; i < sizeof(k_registered_object_cids) / sizeof(k_registered_object_cids[0]); i++) {
        const nmo_class_id_t cid = k_registered_object_cids[i];
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id_inherited(registry, cid);
        ASSERT_NOT_NULL(type);
        ASSERT_NOT_NULL(type->vtable);
        ASSERT_NOT_NULL(type->vtable->prepare_dependencies);
        ASSERT_NOT_NULL(type->vtable->remap_dependencies);
    }

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(type_runtime_hook_coverage, registered_types_have_explicit_remap_hook);
TEST_MAIN_END()
