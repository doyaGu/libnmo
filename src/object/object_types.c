/**
 * @file object_types.c
 * @brief Implementation of Virtools object type registration
 * 
 * Registers all CKObject-derived classes as types in the type registry,
 * replacing the legacy schema system with unified type system integration.
 */

#include "object/nmo_object_types.h"
#include "type/type_system.h"
#include "object/legacy/nmo_class_ids.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* ============================================================================
 * Object State Constants
 * ============================================================================ */

/* Visibility flag constants (from nmo_ckobject_state_t) */
#define NMO_CKOBJECT_VISIBLE          0x01  /**< Object is visible */
#define NMO_CKOBJECT_HIERARCHICAL     0x02  /**< Object has hierarchical hide */

/* ============================================================================
 * Serialization Constants
 * ============================================================================ */

/* Identifier constants from CKDefines.h */
#define CK_STATESAVE_OBJECTHIDDEN          0x00000001
#define CK_STATESAVE_OBJECTHIERAHIDDEN     0x00000002

/* ============================================================================
 * Forward declarations for vtable functions
 * ============================================================================ */

/* These implement the actual serialization logic */
static nmo_result_t ckobject_serialize(const void *instance, struct nmo_chunk *chunk, 
                                       const nmo_type_descriptor_t *type, void *context);
static nmo_result_t ckobject_deserialize(void *instance, struct nmo_chunk *chunk,
                                         const nmo_type_descriptor_t *type, void *context);

static nmo_result_t ck3dentity_serialize(const void *instance, struct nmo_chunk *chunk,
                                         const nmo_type_descriptor_t *type, void *context);
static nmo_result_t ck3dentity_deserialize(void *instance, struct nmo_chunk *chunk,
                                           const nmo_type_descriptor_t *type, void *context);

/* ============================================================================
 * Vtable Definitions
 * ============================================================================ */

static nmo_type_vtable_t ckobject_vtable = {
    .create = NULL,              /* No special construction needed */
    .destroy = NULL,             /* No special destruction needed */
    .copy = NULL,                /* Use default memcpy */
    .serialize = ckobject_serialize,
    .deserialize = ckobject_deserialize,
    .validate = NULL,            /* No validation yet */
    .equals = NULL,              /* Use default memcmp */
    .hash = NULL,                /* No hashing yet */
    .to_string = NULL,           /* TODO: Phase 6.4 */
    .from_string = NULL          /* TODO: Phase 6.4 */
};

static nmo_type_vtable_t ck3dentity_vtable = {
    .create = NULL,
    .destroy = NULL,
    .copy = NULL,
    .serialize = ck3dentity_serialize,
    .deserialize = ck3dentity_deserialize,
    .validate = NULL,
    .equals = NULL,
    .hash = NULL,
    .to_string = NULL,
    .from_string = NULL
};

/* ============================================================================
 * Helper: Register a single object type
 * ============================================================================ */

/**
 * @brief Helper to register an object type with common patterns
 */
static nmo_result_t register_object_type(
    nmo_type_registry_t *registry,
    nmo_guid_t guid,
    const char *name,
    nmo_class_id_t class_id,
    nmo_guid_t base_guid,
    size_t size,
    size_t alignment,
    nmo_type_vtable_t *vtable)
{
    nmo_type_descriptor_t type_desc = {
        .guid = guid,
        .name = name,
        .size = (uint32_t)size,
        .alignment = (uint32_t)alignment,
        .class_id = class_id,
        .base_type = base_guid,
        .category = NMO_TYPE_CATEGORY_OBJECT_REF,  /* All CKObject types are object refs */
        .flags = NMO_TYPE_FLAG_SERIALIZABLE,
        .id = NMO_TYPE_ID_INVALID,  /* Will be assigned by registry */
        .description = NULL,
        .fields = NULL,              /* TODO: Add field descriptors in future phases */
        .field_count = 0,
        .vtable = vtable,
        .creator_plugin = NULL       /* Builtin types */
    };

    return nmo_type_registry_register(registry, &type_desc);
}

/* ============================================================================
 * Base Object Types Registration
 * ============================================================================ */

nmo_result_t nmo_register_base_object_types(nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type registry");
    }

    nmo_result_t result;

    /* Register CKObject (root of hierarchy) */
    result = register_object_type(
        registry,
        NMO_GUID_CKOBJECT,
        "CKObject",
        NMO_CID_OBJECT,
        (nmo_guid_t){0, 0},  /* No base type */
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKSceneObject : CKObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKSCENEOBJECT,
        "CKSceneObject",
        NMO_CID_SCENEOBJECT,
        NMO_GUID_CKOBJECT,
        sizeof(nmo_ckobject_state_t),  /* No additional state yet */
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKBeObject : CKSceneObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKBEOBJECT,
        "CKBeObject",
        NMO_CID_BEOBJECT,
        NMO_GUID_CKSCENEOBJECT,
        sizeof(nmo_ckobject_state_t),  /* No additional state yet */
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKRenderObject : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKRENDEROBJECT,
        "CKRenderObject",
        NMO_CID_RENDEROBJECT,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckobject_state_t),  /* No additional state yet */
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * 2D Entity Types Registration
 * ============================================================================ */

nmo_result_t nmo_register_2d_entity_types(nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type registry");
    }

    nmo_result_t result;

    /* Register CK2dEntity : CKRenderObject */
    result = register_object_type(
        registry,
        NMO_GUID_CK2DENTITY,
        "CK2dEntity",
        NMO_CID_2DENTITY,
        NMO_GUID_CKRENDEROBJECT,
        sizeof(nmo_ckobject_state_t),  /* TODO: Add 2D-specific state */
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKSprite : CK2dEntity */
    result = register_object_type(
        registry,
        NMO_GUID_CKSPRITE,
        "CKSprite",
        NMO_CID_SPRITE,
        NMO_GUID_CK2DENTITY,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKSpriteText : CKSprite */
    result = register_object_type(
        registry,
        NMO_GUID_CKSPRITETEXT,
        "CKSpriteText",
        NMO_CID_SPRITETEXT,
        NMO_GUID_CKSPRITE,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * 3D Entity Types Registration
 * ============================================================================ */

nmo_result_t nmo_register_3d_entity_types(nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type registry");
    }

    nmo_result_t result;

    /* Register CK3dEntity : CKRenderObject */
    result = register_object_type(
        registry,
        NMO_GUID_CK3DENTITY,
        "CK3dEntity",
        NMO_CID_3DENTITY,
        NMO_GUID_CKRENDEROBJECT,
        sizeof(nmo_ck3dentity_state_t),
        alignof(nmo_ck3dentity_state_t),
        &ck3dentity_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CK3dObject : CK3dEntity */
    result = register_object_type(
        registry,
        NMO_GUID_CK3DOBJECT,
        "CK3dObject",
        NMO_CID_3DOBJECT,
        NMO_GUID_CK3DENTITY,
        sizeof(nmo_ck3dentity_state_t),
        alignof(nmo_ck3dentity_state_t),
        &ck3dentity_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKCamera : CK3dEntity */
    result = register_object_type(
        registry,
        NMO_GUID_CKCAMERA,
        "CKCamera",
        NMO_CID_CAMERA,
        NMO_GUID_CK3DENTITY,
        sizeof(nmo_ck3dentity_state_t),
        alignof(nmo_ck3dentity_state_t),
        &ck3dentity_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKLight : CK3dEntity */
    result = register_object_type(
        registry,
        NMO_GUID_CKLIGHT,
        "CKLight",
        NMO_CID_LIGHT,
        NMO_GUID_CK3DENTITY,
        sizeof(nmo_ck3dentity_state_t),
        alignof(nmo_ck3dentity_state_t),
        &ck3dentity_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKCharacter : CK3dEntity */
    result = register_object_type(
        registry,
        NMO_GUID_CKCHARACTER,
        "CKCharacter",
        NMO_CID_CHARACTER,
        NMO_GUID_CK3DENTITY,
        sizeof(nmo_ck3dentity_state_t),
        alignof(nmo_ck3dentity_state_t),
        &ck3dentity_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Resource Types Registration
 * ============================================================================ */

nmo_result_t nmo_register_resource_types(nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type registry");
    }

    nmo_result_t result;

    /* Register CKMaterial : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKMATERIAL,
        "CKMaterial",
        NMO_CID_MATERIAL,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckmaterial_state_t),
        alignof(nmo_ckmaterial_state_t),
        &ckobject_vtable  /* TODO: Add material-specific vtable */
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKTexture : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKTEXTURE,
        "CKTexture",
        NMO_CID_TEXTURE,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckobject_state_t),  /* TODO: Add texture state */
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKMesh : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKMESH,
        "CKMesh",
        NMO_CID_MESH,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckmesh_state_t),
        alignof(nmo_ckmesh_state_t),
        &ckobject_vtable  /* TODO: Add mesh-specific vtable */
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Behavior Types Registration
 * ============================================================================ */

nmo_result_t nmo_register_behavior_types(nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type registry");
    }

    nmo_result_t result;

    /* Register CKBehavior : CKSceneObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKBEHAVIOR,
        "CKBehavior",
        NMO_CID_BEHAVIOR,
        NMO_GUID_CKSCENEOBJECT,
        sizeof(nmo_ckobject_state_t),  /* TODO: Add behavior state */
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKBehaviorIO : CKObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKBEHAVIORIO,
        "CKBehaviorIO",
        NMO_CID_BEHAVIORIO,
        NMO_GUID_CKOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKBehaviorLink : CKObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKBEHAVIORLINK,
        "CKBehaviorLink",
        NMO_CID_BEHAVIORLINK,
        NMO_GUID_CKOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKParameter : CKObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKPARAMETER,
        "CKParameter",
        NMO_CID_PARAMETER,
        NMO_GUID_CKOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKParameterLocal : CKParameter */
    result = register_object_type(
        registry,
        NMO_GUID_CKPARAMETERLOCAL,
        "CKParameterLocal",
        NMO_CID_PARAMETERLOCAL,
        NMO_GUID_CKPARAMETER,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Scene Management Types
 * ============================================================================ */

static nmo_result_t register_scene_types(nmo_type_registry_t *registry) {
    nmo_result_t result;

    /* Register CKScene : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKSCENE,
        "CKScene",
        NMO_CID_SCENE,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKLevel : CKSceneObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKLEVEL,
        "CKLevel",
        NMO_CID_LEVEL,
        NMO_GUID_CKSCENEOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKGroup : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKGROUP,
        "CKGroup",
        NMO_CID_GROUP,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Data Structure Types
 * ============================================================================ */

static nmo_result_t register_data_types(nmo_type_registry_t *registry) {
    /* Register CKDataArray : CKBeObject */
    return register_object_type(
        registry,
        NMO_GUID_CKDATAARRAY,
        "CKDataArray",
        NMO_CID_DATAARRAY,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
}

/* ============================================================================
 * Animation Types
 * ============================================================================ */

static nmo_result_t register_animation_types(nmo_type_registry_t *registry) {
    nmo_result_t result;

    /* Register CKAnimation : CKBeObject */
    result = register_object_type(
        registry,
        NMO_GUID_CKANIMATION,
        "CKAnimation",
        NMO_CID_ANIMATION,
        NMO_GUID_CKBEOBJECT,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register CKKeyedAnimation : CKAnimation */
    result = register_object_type(
        registry,
        NMO_GUID_CKKEYEDANIMATION,
        "CKKeyedAnimation",
        NMO_CID_KEYEDANIMATION,
        NMO_GUID_CKANIMATION,
        sizeof(nmo_ckobject_state_t),
        alignof(nmo_ckobject_state_t),
        &ckobject_vtable
    );
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Main Registration Function
 * ============================================================================ */

nmo_result_t nmo_register_object_types(nmo_type_registry_t *registry) {
    if (!registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type registry");
    }

    nmo_result_t result;

    /* Register in dependency order (base types first) */
    
    result = nmo_register_base_object_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = nmo_register_2d_entity_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = nmo_register_3d_entity_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = nmo_register_resource_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = nmo_register_behavior_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = register_scene_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = register_data_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    result = register_animation_types(registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

const nmo_type_descriptor_t* nmo_get_object_type_by_class_id(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry) {
        return NULL;
    }

    return nmo_type_registry_find_by_class_id(registry, class_id);
}

int nmo_is_object_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid)
{
    if (!registry) {
        return 0;
    }

    /* Look up both type descriptors first */
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, type_guid);
    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(registry, NMO_GUID_CKOBJECT);
    
    if (!type || !ckobject_type) {
        return 0;
    }

    /* Check if this type is derived from CKObject */
    return nmo_type_is_derived_from(registry, type->id, ckobject_type->id);
}

nmo_class_id_t nmo_object_guid_to_class_id(nmo_guid_t type_guid) {
    /* Check if this is a Virtools object GUID (DWORD1 == 0x564B4F42) */
    if (type_guid.d1 == 0x564B4F42) {
        return (nmo_class_id_t)type_guid.d2;
    }
    return 0;
}

/* ============================================================================
 * Vtable Implementations
 * ============================================================================ */

/**
 * @brief Serialize CKObject state to chunk
 * 
 * Reference: CKObject::Save in Virtools SDK
 */
static nmo_result_t ckobject_serialize(
    const void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    
    if (!instance || !chunk) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL instance or chunk in ckobject_serialize");
    }

    const nmo_ckobject_state_t *state = (const nmo_ckobject_state_t*)instance;

    /* Write appropriate identifier based on visibility state */
    if ((state->visibility_flags & NMO_CKOBJECT_VISIBLE) == 0) {
        if (state->visibility_flags & NMO_CKOBJECT_HIERARCHICAL) {
            /* Hierarchically hidden */
            nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
        } else {
            /* Completely hidden */
            nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
        }
    }
    /* If visible (default), no identifier is written */

    return nmo_result_ok();
}

/**
 * @brief Deserialize CKObject state from chunk
 * 
 * Reference: CKObject::Load in Virtools SDK
 */
static nmo_result_t ckobject_deserialize(
    void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    
    if (!instance || !chunk) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL instance or chunk in ckobject_deserialize");
    }

    nmo_ckobject_state_t *state = (nmo_ckobject_state_t*)instance;

    /* Initialize to default (visible) */
    state->visibility_flags = NMO_CKOBJECT_VISIBLE;

    /* Check for OBJECTHIDDEN identifier (highest priority) */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIDDEN);
    if (result.code == NMO_OK) {
        /* Object is completely hidden (no VISIBLE, no HIERARCHICAL) */
        state->visibility_flags = 0;
        return nmo_result_ok();
    }

    /* Check for OBJECTHIERAHIDDEN identifier */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJECTHIERAHIDDEN);
    if (result.code == NMO_OK) {
        /* Object is hierarchically hidden (no VISIBLE, but has HIERARCHICAL) */
        state->visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
        return nmo_result_ok();
    }

    /* No special identifiers found -> object is visible (default already set) */
    return nmo_result_ok();
}

/**
 * @brief Serialize CK3dEntity state to chunk
 * 
 * Reference: CK3dEntity::Save in Virtools SDK
 */
static nmo_result_t ck3dentity_serialize(
    const void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    
    if (!instance || !chunk) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL instance or chunk in ck3dentity_serialize");
    }

    const nmo_ck3dentity_state_t *state = (const nmo_ck3dentity_state_t*)instance;

    /* First serialize base CKObject state */
    nmo_result_t result = ckobject_serialize(&state->base, chunk, type, context);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Write world transformation matrix (4x4 = 16 floats) */
    for (int i = 0; i < 16; i++) {
        result = nmo_chunk_write_float(chunk, state->world_matrix[i]);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Write entity flags */
    result = nmo_chunk_write_dword(chunk, state->flags);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Write z-order */
    result = nmo_chunk_write_dword(chunk, state->zorder);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/**
 * @brief Deserialize CK3dEntity state from chunk
 * 
 * Reference: CK3dEntity::Load in Virtools SDK
 */
static nmo_result_t ck3dentity_deserialize(
    void *instance,
    struct nmo_chunk *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    
    if (!instance || !chunk) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL instance or chunk in ck3dentity_deserialize");
    }

    nmo_ck3dentity_state_t *state = (nmo_ck3dentity_state_t*)instance;

    /* Initialize to default */
    memset(state, 0, sizeof(*state));

    /* First deserialize base CKObject state */
    nmo_result_t result = ckobject_deserialize(&state->base, chunk, type, context);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Read world transformation matrix (4x4 = 16 floats) */
    for (int i = 0; i < 16; i++) {
        result = nmo_chunk_read_float(chunk, &state->world_matrix[i]);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Read entity flags */
    result = nmo_chunk_read_dword(chunk, &state->flags);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Read z-order */
    result = nmo_chunk_read_dword(chunk, &state->zorder);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}
