/**
 * @file operations_query.c
 * @brief The parameter operations that read the objects' state: the getters
 *        of the Parameter Operations plugin on 3D entities, meshes, cameras,
 *        lights, textures, 2D entities, curves, characters, places, groups,
 *        sounds, the render context and the mouse, by the functions the
 *        plugin registers for them (CKFloatGetRadius3dEntity,
 *        CKBoolIsVisible2dEntity, CKIntGetVertexCountMesh, ... and the
 *        input manager's CK2dVectorGetMousePos).
 *
 * As the plugin's functions, a getter of an object that is not there leaves
 * its output as it was. operations.c looks these up after its own table
 * (eng_operation_builtin_query()).
 */

#include "../engine_internal.h"

#include "engine/nmo_engine_render.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_sprite_schemas.h"
#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_system.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OP_GET_RADIUS NMO_GUID_INIT(0x0ae164ff, 0x3a735fe9)
#define OP_GET_RANGE NMO_GUID_INIT(0x0d2929e5, 0x66f121de)
#define OP_GET_COLOR NMO_GUID_INIT(0x75445df6, 0x3570366b)
#define OP_GET_FOV NMO_GUID_INIT(0x55ba1216, 0x1c255f06)
#define OP_GET_NEAR_CLIP NMO_GUID_INIT(0x12baaf11, 0x2cd5cc09)
#define OP_GET_FAR_CLIP NMO_GUID_INIT(0x15b5fa12, 0xca255f92)
#define OP_GET_ORTHOGRAPHIC_ZOOM NMO_GUID_INIT(0x55ba9574, 0x13469f06)
#define OP_GET_ASPECT_RATIO NMO_GUID_INIT(0x7370134f, 0x004621c6)
#define OP_GET_TARGET NMO_GUID_INIT(0x4e3309de, 0x5de92367)
#define OP_GET_WIDTH NMO_GUID_INIT(0x5a4e13fe, 0x628b7ca2)
#define OP_GET_HEIGHT NMO_GUID_INIT(0x37f9040e, 0x52c05c4e)
#define OP_GET_SLOT_COUNT NMO_GUID_INIT(0x05841a08, 0x3adc7b84)
#define OP_GET_SIZE NMO_GUID_INIT(0x54f52b51, 0x564c4812)
#define OP_GET_GEOMETRIC_CENTER NMO_GUID_INIT(0x6a8c4838, 0x7b026566)
#define OP_GET_HIERARCHICAL_BOX NMO_GUID_INIT(0x31a764af, 0x305e5165)
#define OP_GET_VIEW_RECT NMO_GUID_INIT(0x2a143038, 0x03d126b9)
#define OP_GET_SCREEN_ORIGIN NMO_GUID_INIT(0x2225467a, 0x295e223c)
#define OP_GET_TEXT NMO_GUID_INIT(0x764e2b8b, 0x6b430baf)
#define OP_GET_TEXTURE NMO_GUID_INIT(0x0f9e4f74, 0x0d14175e)
#define OP_GET_VERTEX_COUNT NMO_GUID_INIT(0x7a140f0b, 0x583817f9)
#define OP_GET_FACE_COUNT NMO_GUID_INIT(0x011174f4, 0x42985acb)
#define OP_GET_PM_VERTICES NMO_GUID_INIT(0x011244f4, 0x42985acb)
#define OP_GET_MATERIAL_COUNT NMO_GUID_INIT(0x153768e3, 0x5f576a09)
#define OP_GET_CHANNEL_COUNT NMO_GUID_INIT(0x3db9075c, 0x7718126f)
#define OP_GET_CHANNEL_BY_MATERIAL NMO_GUID_INIT(0x73f70725, 0x39247024)
#define OP_GET_VERTEX_POSITION NMO_GUID_INIT(0x38fa7eba, 0x06cb3ff2)
#define OP_GET_VERTEX_NORMAL NMO_GUID_INIT(0x3abb7401, 0x60923471)
#define OP_GET_VERTEX_UVS NMO_GUID_INIT(0x6ad14794, 0x24c068fd)
#define OP_GET_VERTEX_COLOR NMO_GUID_INIT(0x451764e4, 0x7ded068d)
#define OP_GET_VERTEX_SPECULAR NMO_GUID_INIT(0x06a76830, 0x4f9a7603)
#define OP_GET_VERTEX_WEIGHT NMO_GUID_INIT(0x3e151dbc, 0x4b1036b2)
#define OP_GET_FACE_NORMAL NMO_GUID_INIT(0x584e0516, 0x04ae72ad)
#define OP_GET_FACE_VERTEX NMO_GUID_INIT(0x1e0613be, 0x7c6004b0)
#define OP_GET_FACE_MATERIAL NMO_GUID_INIT(0x38851eb5, 0x1848668b)
#define OP_GET_MESH_LIST NMO_GUID_INIT(0x7ef97c3d, 0x74453060)
#define OP_IS_DERIVED_FROM NMO_GUID_INIT(0x6ab94f5a, 0x1a6376a9)
#define OP_DYNAMIC_CAST NMO_GUID_INIT(0x39697e76, 0x538376ec)
#define OP_IS_COLLISION NMO_GUID_INIT(0x66ae6c61, 0x038658e8)
#define OP_IS_IN NMO_GUID_INIT(0x540e1450, 0x593f1692)
#define OP_IS_VISIBLE NMO_GUID_INIT(0x0a547387, 0x410851af)
#define OP_IS_ACTIVE NMO_GUID_INIT(0x2f6e7455, 0x5fb82ac8)
#define OP_IS_CONTENT_EQUAL NMO_GUID_INIT(0x3dda41e5, 0x036c6833)
#define OP_GET_GROUP_TYPE NMO_GUID_INIT(0x429c3ff2, 0x0e4a4938)
#define OP_GET_CURVE_POSITION NMO_GUID_INIT(0x61323b67, 0x3a282f1c)
#define OP_GET_CURVE_TANGENT NMO_GUID_INIT(0x54ba6299, 0x51bf236d)
#define OP_GET_IN_TANGENT NMO_GUID_INIT(0x19016e82, 0x13ea0f10)
#define OP_GET_OUT_TANGENT NMO_GUID_INIT(0xabc16e82, 0x73ef6952)
#define OP_GET_CURVE_POINT NMO_GUID_INIT(0x4e9426c2, 0x04e50e5f)
#define OP_GET_CHARACTER NMO_GUID_INIT(0x07fd40be, 0x375c1ccf)
#define OP_GET_ROOT NMO_GUID_INIT(0x28ee420c, 0x5ad223e7)
#define OP_GET_BODYPART NMO_GUID_INIT(0x513666b8, 0x24fb34cf)
#define OP_GET_BODYPARTS NMO_GUID_INIT(0x5a605a7f, 0x2dc05974)
#define OP_IS_BODY_PART_OF NMO_GUID_INIT(0x4e084898, 0x52e37010)
#define OP_GET_ANIMATION NMO_GUID_INIT(0x1e705297, 0x352407b6)
#define OP_GET_ANIMATION_COUNT NMO_GUID_INIT(0x1e705297, 0x352407aa)
#define OP_GET_REFERENCE_PLACE NMO_GUID_INIT(0x6b603781, 0x47607025)
#define OP_GET_PORTALS NMO_GUID_INIT(0x38bc419a, 0x7fff0707)
#define OP_PICK_OBJECT NMO_GUID_INIT(0x028e497b, 0x3db86786)
#define OP_GET_SOUND_FILE_NAME NMO_GUID_INIT(0x49e832a5, 0x58e8612c)
#define OP_GET_SAMPLING_RATE NMO_GUID_INIT(0x4b600ce3, 0xf0526471)
#define OP_IS_LOOPING NMO_GUID_INIT(0x1b584b4d, 0x7db27422)
#define OP_IS_STREAMING NMO_GUID_INIT(0x5ac136be, 0x6fd04a75)
#define OP_IS_PLAYING NMO_GUID_INIT(0x026d192c, 0x1dab270e)
#define OP_IS_PAUSED NMO_GUID_INIT(0x11483b48, 0x60642931)
#define OP_GET_GAIN NMO_GUID_INIT(0x144c1161, 0x0ab87cb4)
#define OP_GET_PITCH NMO_GUID_INIT(0x383b2c54, 0x5bff28cb)
#define OP_GET_PAN NMO_GUID_INIT(0x40f22eae, 0x75d53da0)
#define OP_GET_RELATIVE_POSITION NMO_GUID_INIT(0x0d5b2865, 0x4d5f7df7)
#define OP_GET_RELATIVE_DIRECTION NMO_GUID_INIT(0x54cc3c64, 0x55bf239c)
#define OP_GET_CONE NMO_GUID_INIT(0x2da46f8f, 0x33643c47)
#define OP_GET_MIN_MAX NMO_GUID_INIT(0x3de17f0e, 0x5a1c1fe3)
#define OP_GET_VELOCITY NMO_GUID_INIT(0x339f17a4, 0x6c925668)
#define OP_GET_PLAYED_TIME NMO_GUID_INIT(0x33a2705a, 0x3cf97c32)
#define OP_GET_MOUSE_POSITION NMO_GUID_INIT(0x06ea0201, 0x680e3a62)
#define OP_GET_MOUSE_X NMO_GUID_INIT(0x53c51abe, 0x0eba68de)
#define OP_GET_MOUSE_Y NMO_GUID_INIT(0x27af3c9f, 0x0dbc4eb3)

#define OP_GET_PARENT_Q NMO_GUID_INIT(0x5bf9718f, 0x030b698c)
#define OP_GET_DISTANCE_Q NMO_GUID_INIT(0x265c7d00, 0x5c8f1702)
#define OP_GET_LENGTH_Q NMO_GUID_INIT(0x556a69af, 0x076e3f09)
#define OP_GET_COUNT_Q NMO_GUID_INIT(0x44df6f61, 0x5f8f55c8)
#define OP_GET_CURRENT_Q NMO_GUID_INIT(0x6a5c5061, 0x5efe7428)
#define OP_GET_TYPE_Q NMO_GUID_INIT(0x35aa0ddb, 0x7a5e3335)
#define OP_GET_MATERIAL_Q NMO_GUID_INIT(0x6b8c78bd, 0x7b846d3c)
#define OP_GET_PLACE_Q NMO_GUID_INIT(0x1814113a, 0x5dc54c26)
#define OP_GET_BOUNDING_BOX_Q NMO_GUID_INIT(0x31a7649f, 0x305e5164)
#define OP_TRANSFORM_Q NMO_GUID_INIT(0x38865f64, 0x5ec70752)

#define CK_WAVESOUND_FILESTREAMED 0x00002000u

/* ---- Operands and results ---- */

static uint32_t param_u32(const eng_param_t *param)
{
    uint32_t value = 0;
    if (param != NULL && param->size >= sizeof(value)) {
        memcpy(&value, param->data, sizeof(value));
    }
    return value;
}

static float param_float(const eng_param_t *param)
{
    float value = 0.0f;
    if (param != NULL && param->size >= sizeof(value)) {
        memcpy(&value, param->data, sizeof(value));
    }
    return value;
}

static float param_component(const eng_param_t *param, size_t index)
{
    float value = 0.0f;
    if (param != NULL && param->size >= (index + 1u) * sizeof(float)) {
        memcpy(&value, param->data + index * sizeof(float), sizeof(value));
    }
    return value;
}

static const char *param_string(const eng_param_t *param)
{
    return param != NULL && param->data != NULL && param->size > 0 &&
                   memchr(param->data, '\0', param->size) != NULL
               ? (const char *)param->data
               : NULL;
}

static nmo_status_t write_u32(nmo_engine_t *engine, eng_param_t *out, uint32_t value)
{
    return eng_param_write(engine, out, &value, sizeof(value));
}

static nmo_status_t write_float(nmo_engine_t *engine, eng_param_t *out, float value)
{
    return eng_param_write(engine, out, &value, sizeof(value));
}

static nmo_status_t write_floats(nmo_engine_t *engine, eng_param_t *out, const float *values,
                                 size_t count)
{
    return eng_param_write(engine, out, values, count * sizeof(float));
}

static nmo_status_t write_vector(nmo_engine_t *engine, eng_param_t *out, const eng_vector_t *v)
{
    const float values[3] = {v->x, v->y, v->z};
    return write_floats(engine, out, values, 3u);
}

/* A VxBbox: Max, then Min */
static nmo_status_t write_box(nmo_engine_t *engine, eng_param_t *out, const eng_vector_t *min,
                              const eng_vector_t *max)
{
    const float box[6] = {max->x, max->y, max->z, min->x, min->y, min->z};
    return write_floats(engine, out, box, 6u);
}

/* The IDs of a collection (XObjectArray) */
static nmo_status_t write_ids(nmo_engine_t *engine, eng_param_t *out, const uint32_t *ids,
                              size_t count)
{
    return eng_param_write(engine, out, ids, count * sizeof(uint32_t));
}

/* The object the operand names, when it is there */
static nmo_object_t *param_object(nmo_engine_t *engine, const eng_param_t *param)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(param);
    return id != 0 ? eng_find_object(engine, id) : NULL;
}

/* The state of the class `guid` of the object the operand names; NULL when it is not of it */
static const void *param_state(nmo_engine_t *engine, const eng_param_t *param, nmo_guid_t guid)
{
    nmo_object_t *object = param_object(engine, param);
    return object != NULL
               ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, guid)
               : NULL;
}

/* The entity the operand names; -1 for none */
static int32_t param_entity(nmo_engine_t *engine, const eng_param_t *param)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(param);
    return id != 0 ? eng_entity_index(engine, id) : -1;
}

static bool object_is_of_class(nmo_engine_t *engine, nmo_object_id_t id, int32_t class_id)
{
    const int32_t class_of = id != 0 ? nmo_engine_object_class(engine, id) : 0;
    return class_of > 0 && nmo_type_query_class_is_derived_from(engine->types,
                                                                 (nmo_class_id_t)class_of,
                                                                 (nmo_class_id_t)class_id);
}

/* The class the result type's objects are of (CKParameterManager::TypeToClassID) */
static int32_t result_class(const eng_param_t *out)
{
    if (out == NULL || out->type_desc == NULL) {
        return 0;
    }
    return out->type_desc->object_class_id != 0 ? (int32_t)out->type_desc->object_class_id
                                                : (int32_t)out->type_desc->class_id;
}

/* The render context's size: the engine's screen mode */
static const eng_screen_mode_t *screen_mode(const nmo_engine_t *engine)
{
    int32_t mode = engine->render.screen_mode;
    return &eng_screen_modes[mode >= 0 && mode < ENG_SCREEN_MODE_COUNT ? mode : 0];
}

/* ============================================================================
 * 3D entities
 * ============================================================================ */

/* CKFloatGetRadius3dEntity */
static nmo_status_t op_get_radius(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    (void)src2;
    int32_t entity = param_entity(engine, src1);
    return entity >= 0 ? write_float(engine, out, eng_entity_radius(engine, entity)) : NMO_OK;
}

/* CKVectorGetCenter3dEntity: the middle of the world bounding box */
static nmo_status_t op_get_center_entity(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    int32_t entity = param_entity(engine, src1);
    if (entity < 0) {
        return NMO_OK;
    }
    eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
    eng_entity_world_box(engine, entity, &min, &max);
    const eng_vector_t center = {(max.x + min.x) * 0.5f, (max.y + min.y) * 0.5f,
                                 (max.z + min.z) * 0.5f};
    return write_vector(engine, out, &center);
}

/* CKVectorGetCenterBox */
static nmo_status_t op_get_center_box(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src2;
    const eng_vector_t center = {(param_component(src1, 0) + param_component(src1, 3)) * 0.5f,
                                 (param_component(src1, 1) + param_component(src1, 4)) * 0.5f,
                                 (param_component(src1, 2) + param_component(src1, 5)) * 0.5f};
    return write_vector(engine, out, &center);
}

/* CKBoxGetHBox3dEntity */
static nmo_status_t op_get_hierarchical_box(nmo_engine_t *engine, eng_param_t *src1,
                                            eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    int32_t entity = param_entity(engine, src1);
    if (entity < 0) {
        return NMO_OK;
    }
    eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
    eng_entity_hierarchical_box(engine, entity, &min, &max);
    return write_box(engine, out, &min, &max);
}

/* CKBoolIsVisible2dEntity (3D entities too): CKObject::IsVisible */
static nmo_status_t op_is_visible(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    if (id == 0 || eng_find_object(engine, id) == NULL) {
        return NMO_OK;
    }
    return write_u32(engine, out,
                     (eng_object_visibility(engine, id) & CK_OBJECT_VISIBLE) != 0 ? 1u : 0u);
}

/* CKBoolIsActiveBeObject: CKSceneObject::IsActiveInCurrentScene */
static nmo_status_t op_is_active_object(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    if (id == 0 || eng_find_object(engine, id) == NULL) {
        return NMO_OK;
    }
    return write_u32(engine, out, nmo_engine_object_is_active(engine, id) ? 1u : 0u);
}

/* CKBoolIsActiveScript: CKBehavior::IsActive */
static nmo_status_t op_is_active_script(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    if (id == 0 || eng_find_object(engine, id) == NULL) {
        return NMO_OK;
    }
    return write_u32(engine, out, nmo_engine_behavior_is_active(engine, id) ? 1u : 0u);
}

/* CKBoolIsVectorInBboxVector3dEntity: the world point inside the mesh's local box */
static nmo_status_t op_is_in_entity(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    int32_t entity = param_entity(engine, src2);
    eng_vector_t min, max;
    if (entity < 0 || !eng_entity_has_mesh(engine, entity) ||
        !eng_entity_local_box(engine, entity, &min, &max)) {
        return NMO_OK;
    }
    const eng_vector_t world = {param_component(src1, 0), param_component(src1, 1),
                                param_component(src1, 2)};
    eng_vector_t local;
    eng_entity_inverse_transform(engine, entity, &local, &world, -1);
    const bool inside = local.x >= min.x && local.x <= max.x && local.y >= min.y &&
                        local.y <= max.y && local.z >= min.z && local.z <= max.z;
    return write_u32(engine, out, inside ? 1u : 0u);
}

/* CKBoolIsInObjectArrayObject: the object is in the collection */
static nmo_status_t op_is_in_collection(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src2);
    if (id == 0 || eng_find_object(engine, id) == NULL) {
        return write_u32(engine, out, 0u);
    }
    size_t count = src1 != NULL ? src1->size / sizeof(uint32_t) : 0u;
    for (size_t i = 0; i < count; i++) {
        uint32_t element;
        memcpy(&element, src1->data + i * sizeof(uint32_t), sizeof(element));
        if (element == (uint32_t)id) {
            return write_u32(engine, out, 1u);
        }
    }
    return write_u32(engine, out, 0u);
}

/* CKBoolCollisionBoxVector: the point inside the box */
static nmo_status_t op_collision_box_vector(nmo_engine_t *engine, eng_param_t *src1,
                                            eng_param_t *src2, eng_param_t *out)
{
    bool inside = true;
    for (size_t i = 0; i < 3; i++) {
        const float v = param_component(src2, i);
        inside = inside && v >= param_component(src1, i + 3) && v <= param_component(src1, i);
    }
    return write_u32(engine, out, inside ? 1u : 0u);
}

/* CKBoolCollisionBoxBox: the boxes overlap */
static nmo_status_t op_collision_box_box(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    bool overlap = true;
    for (size_t i = 0; i < 3; i++) {
        overlap = overlap && param_component(src2, i + 3) <= param_component(src1, i) &&
                  param_component(src2, i) >= param_component(src1, i + 3);
    }
    return write_u32(engine, out, overlap ? 1u : 0u);
}

/*
 * CKBoolCollisionBox3dEntity: a vertex of the entity's mesh in the box. The
 * plugin compares the first three floats as the minimum and the next three
 * as the maximum, the reverse of VxBbox's layout; so does this.
 */
static nmo_status_t op_collision_box_entity(nmo_engine_t *engine, eng_param_t *src1,
                                            eng_param_t *src2, eng_param_t *out)
{
    nmo_object_t *object = param_object(engine, src2);
    const nmo_3dentity_state_t *entity =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    nmo_object_id_t mesh_id =
        entity != NULL ? eng_object_ref(engine, (nmo_object_id_t)param_u32(src2), &entity->current_mesh) : 0;
    nmo_engine_mesh_t mesh;
    if (mesh_id == 0 || !nmo_engine_mesh(engine, mesh_id, &mesh)) {
        return NMO_OK;
    }
    for (int32_t i = 0; i < mesh.vertex_count; i++) {
        const nmo_vector_t p = mesh.vertices[i].position;
        if (p.x >= param_component(src1, 0) && p.x <= param_component(src1, 3) &&
            p.y >= param_component(src1, 1) && p.y <= param_component(src1, 4) &&
            p.z >= param_component(src1, 2) && p.z <= param_component(src1, 5)) {
            return write_u32(engine, out, 1u);
        }
    }
    return write_u32(engine, out, 0u);
}

/* CKBoolCollision3dEntity3dEntity: the world boxes overlap */
static nmo_status_t op_collision_entities(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    int32_t a = param_entity(engine, src1), b = param_entity(engine, src2);
    if (a < 0 || b < 0) {
        return NMO_OK;
    }
    eng_vector_t min1 = {0, 0, 0}, max1 = {0, 0, 0}, min2 = {0, 0, 0}, max2 = {0, 0, 0};
    eng_entity_world_box(engine, a, &min1, &max1);
    eng_entity_world_box(engine, b, &min2, &max2);
    const bool overlap = min2.x <= max1.x && max2.x >= min1.x && min2.y <= max1.y &&
                         max2.y >= min1.y && min2.z <= max1.z && max2.z >= min1.z;
    return write_u32(engine, out, overlap ? 1u : 0u);
}

/* CKObjectArrayGetMeshList3dEntity: the entity's meshes */
static nmo_status_t op_get_mesh_list(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)param_state(engine, src1, CKPGUID_3DENTITY);
    if (entity == NULL) {
        return NMO_OK;
    }
    nmo_object_id_t owner = (nmo_object_id_t)param_u32(src1);
    uint32_t *ids = entity->mesh_count > 0 ? (uint32_t *)malloc(entity->mesh_count * sizeof(uint32_t)) : NULL;
    if (entity->mesh_count > 0 && ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (uint32_t i = 0; i < entity->mesh_count; i++) {
        ids[i] = (uint32_t)eng_object_ref(engine, owner, &entity->mesh_ids[i]);
    }
    nmo_status_t status = write_ids(engine, out, ids, entity->mesh_count);
    free(ids);
    return status;
}

/* CKPlaceGetRefPlace3DEntity */
static nmo_status_t op_get_reference_place(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)param_state(engine, src1, CKPGUID_3DENTITY);
    nmo_object_id_t place =
        entity != NULL ? eng_object_ref(engine, (nmo_object_id_t)param_u32(src1), &entity->place) : 0;
    return write_u32(engine, out, place != 0 && eng_find_object(engine, place) != NULL ? (uint32_t)place : 0u);
}

/* CKIntGetAnimationCount3dEntity */
static nmo_status_t op_get_animation_count(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)param_state(engine, src1, CKPGUID_3DENTITY);
    return entity != NULL ? write_u32(engine, out, entity->animation_count) : NMO_OK;
}

/* CKObjectAnimationGetAnimation3dEntityInt: the entity's object animation at the index */
static nmo_status_t op_get_object_animation_at(nmo_engine_t *engine, eng_param_t *src1,
                                               eng_param_t *src2, eng_param_t *out)
{
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)param_state(engine, src1, CKPGUID_3DENTITY);
    if (entity == NULL) {
        return write_u32(engine, out, 0u);
    }
    int32_t index = (int32_t)param_u32(src2);
    nmo_object_id_t animation =
        index >= 0 && (uint32_t)index < entity->animation_count
            ? eng_object_ref(engine, (nmo_object_id_t)param_u32(src1), &entity->animation_ids[index])
            : 0;
    return write_u32(engine, out, (uint32_t)animation);
}

/* CKObjectAnimationGetAnimation3dEntityString: the first whose name contains the string */
static nmo_status_t op_get_object_animation_named(nmo_engine_t *engine, eng_param_t *src1,
                                                  eng_param_t *src2, eng_param_t *out)
{
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)param_state(engine, src1, CKPGUID_3DENTITY);
    const char *wanted = param_string(src2);
    if (entity == NULL || wanted == NULL) {
        return NMO_OK;
    }
    for (uint32_t i = 0; i < entity->animation_count; i++) {
        nmo_object_id_t animation =
            eng_object_ref(engine, (nmo_object_id_t)param_u32(src1), &entity->animation_ids[i]);
        const char *name = animation != 0 ? nmo_engine_object_name(engine, animation) : NULL;
        if (name != NULL && strstr(name, wanted) != NULL) {
            return write_u32(engine, out, (uint32_t)animation);
        }
    }
    return NMO_OK;
}

/* ============================================================================
 * Meshes
 * ============================================================================ */

static bool param_mesh(nmo_engine_t *engine, const eng_param_t *param, nmo_engine_mesh_t *mesh)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(param);
    return id != 0 && nmo_engine_mesh(engine, id, mesh);
}

/* CKIntGetVertexCountMesh */
static nmo_status_t op_get_vertex_count(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_engine_mesh_t mesh;
    return param_mesh(engine, src1, &mesh) ? write_u32(engine, out, (uint32_t)mesh.vertex_count)
                                           : NMO_OK;
}

/* CKIntGetFaceCountMesh */
static nmo_status_t op_get_face_count(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src2;
    nmo_engine_mesh_t mesh;
    return param_mesh(engine, src1, &mesh) ? write_u32(engine, out, (uint32_t)mesh.face_count)
                                           : NMO_OK;
}

/* CKIntGetRenderedProgressiveMeshVerticesCount: a progressive mesh renders all its vertices here */
static nmo_status_t op_get_pm_vertices(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    const nmo_mesh_state_t *state = (const nmo_mesh_state_t *)param_state(engine, src1, CKPGUID_MESH);
    return state != NULL && state->has_progressive_mesh ? write_u32(engine, out, state->vertex_count)
                                                        : NMO_OK;
}

/* CKIntGetMaterialCountMesh: the material groups past the mesh's no-material group 0 */
static nmo_status_t op_get_material_count(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_engine_mesh_t mesh;
    if (!param_mesh(engine, src1, &mesh)) {
        return NMO_OK;
    }
    return write_u32(engine, out, mesh.material_count > 0 ? (uint32_t)mesh.material_count - 1u : 0u);
}

/* CKIntGetChannelCountMesh */
static nmo_status_t op_get_channel_count(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_engine_mesh_t mesh;
    return param_mesh(engine, src1, &mesh) ? write_u32(engine, out, (uint32_t)mesh.channel_count)
                                           : NMO_OK;
}

/* CKIntGetChannelByMaterialMeshMaterial: -1 for none */
static nmo_status_t op_get_channel_by_material(nmo_engine_t *engine, eng_param_t *src1,
                                               eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t mesh_id = (nmo_object_id_t)param_u32(src1);
    eng_mesh_copy_t *copy = mesh_id != 0 ? eng_mesh_copy(engine, mesh_id) : NULL;
    if (copy == NULL) {
        return NMO_OK;
    }
    return write_u32(engine, out,
                     (uint32_t)eng_mesh_channel(copy, (nmo_object_id_t)param_u32(src2)));
}

/* The mesh and the vertex the operands name; false past the vertices */
static bool param_mesh_vertex(nmo_engine_t *engine, const eng_param_t *src1,
                              const eng_param_t *src2, nmo_engine_mesh_t *mesh, int32_t *index)
{
    *index = (int32_t)param_u32(src2);
    return param_mesh(engine, src1, mesh) && *index >= 0 && *index < mesh->vertex_count;
}

/* CKVectorGetVertexPositionMeshInt */
static nmo_status_t op_get_vertex_position(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t index;
    if (!param_mesh_vertex(engine, src1, src2, &mesh, &index)) {
        return NMO_OK;
    }
    return write_floats(engine, out, &mesh.vertices[index].position.x, 3u);
}

/* CKVectorGetVertexNormalMeshInt */
static nmo_status_t op_get_vertex_normal(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t index;
    if (!param_mesh_vertex(engine, src1, src2, &mesh, &index)) {
        return NMO_OK;
    }
    return write_floats(engine, out, &mesh.vertices[index].normal.x, 3u);
}

/* CK2dVectorGetVertexUvsMeshInt */
static nmo_status_t op_get_vertex_uvs(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t index;
    if (!param_mesh_vertex(engine, src1, src2, &mesh, &index)) {
        return NMO_OK;
    }
    return write_floats(engine, out, &mesh.vertices[index].uv.x, 2u);
}

/* An ARGB dword as a VxColor */
static nmo_status_t write_argb(nmo_engine_t *engine, eng_param_t *out, uint32_t argb)
{
    const float scale = 1.0f / 255.0f;
    const float color[4] = {(float)((argb >> 16) & 0xFFu) * scale, (float)((argb >> 8) & 0xFFu) * scale,
                            (float)(argb & 0xFFu) * scale, (float)((argb >> 24) & 0xFFu) * scale};
    return write_floats(engine, out, color, 4u);
}

/* CKColorGetVertexColorMeshInt: the prelit color; white without any */
static nmo_status_t op_get_vertex_color(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t index;
    if (!param_mesh_vertex(engine, src1, src2, &mesh, &index)) {
        return NMO_OK;
    }
    return write_argb(engine, out, mesh.colors != NULL ? mesh.colors[index] : 0xFFFFFFFFu);
}

/* CKColorGetVertexSpecularColorMeshInt: black without any */
static nmo_status_t op_get_vertex_specular(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t index;
    if (!param_mesh_vertex(engine, src1, src2, &mesh, &index)) {
        return NMO_OK;
    }
    return write_argb(engine, out, mesh.speculars != NULL ? mesh.speculars[index] : 0u);
}

/* CKFloatGetVertexWeightMeshInt: 0 for a mesh without weights */
static nmo_status_t op_get_vertex_weight(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    const nmo_mesh_state_t *state = (const nmo_mesh_state_t *)param_state(engine, src1, CKPGUID_MESH);
    int32_t index = (int32_t)param_u32(src2);
    if (state == NULL || index < 0 || (uint32_t)index >= state->vertex_count) {
        return NMO_OK;
    }
    const bool weighted = state->vertex_weights != NULL && (uint32_t)index < state->vertex_weight_count;
    return write_float(engine, out, weighted ? state->vertex_weights[index] : 0.0f);
}

/* CKVectorGetFaceNormalMeshInt: the face's normal, built from its vertices as CK builds them */
static nmo_status_t op_get_face_normal(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t face = (int32_t)param_u32(src2);
    if (!param_mesh(engine, src1, &mesh) || face < 0 || face >= mesh.face_count) {
        return NMO_OK;
    }
    const nmo_vector_t *a = &mesh.vertices[mesh.indices[3 * face]].position;
    const nmo_vector_t *b = &mesh.vertices[mesh.indices[3 * face + 1]].position;
    const nmo_vector_t *c = &mesh.vertices[mesh.indices[3 * face + 2]].position;
    const eng_vector_t ab = {b->x - a->x, b->y - a->y, b->z - a->z};
    const eng_vector_t ac = {c->x - a->x, c->y - a->y, c->z - a->z};
    eng_vector_t normal = eng_vector_cross(&ab, &ac);
    eng_vector_normalize(&normal);
    return write_vector(engine, out, &normal);
}

/* CKVectorGetFaceVertexIndexPositionMeshInt: the position of corner index % 3 of face index / 3 */
static nmo_status_t op_get_face_vertex(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t index = (int32_t)param_u32(src2);
    if (!param_mesh(engine, src1, &mesh) || index < 0 || index / 3 >= mesh.face_count) {
        return NMO_OK;
    }
    return write_floats(engine, out, &mesh.vertices[mesh.indices[index]].position.x, 3u);
}

/* CKMaterialGetFaceMaterialMeshInt: 0 for no mesh or no material */
static nmo_status_t op_get_face_material(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    nmo_engine_mesh_t mesh;
    int32_t face = (int32_t)param_u32(src2);
    if (!param_mesh(engine, src1, &mesh)) {
        return write_u32(engine, out, 0u);
    }
    if (face < 0 || face >= mesh.face_count) {
        return NMO_OK;
    }
    return write_u32(engine, out,
                     (uint32_t)nmo_engine_mesh_material(engine, (nmo_object_id_t)param_u32(src1),
                                                        (int32_t)mesh.faces[face].material_group_idx));
}

/* ============================================================================
 * Cameras, lights, materials, textures, 2D entities
 * ============================================================================ */

static nmo_object_id_t param_object_of_class(nmo_engine_t *engine, const eng_param_t *param,
                                             nmo_guid_t guid)
{
    return param_state(engine, param, guid) != NULL ? (nmo_object_id_t)param_u32(param) : 0;
}

/* CKFloatGetFovCamera */
static nmo_status_t op_get_fov(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                               eng_param_t *out)
{
    (void)src2;
    float fov;
    nmo_object_id_t camera = param_object_of_class(engine, src1, CKPGUID_CAMERA);
    return camera != 0 && eng_camera_fov(engine, camera, &fov) ? write_float(engine, out, fov) : NMO_OK;
}

/* CKFloatGetZoomCamera */
static nmo_status_t op_get_orthographic_zoom(nmo_engine_t *engine, eng_param_t *src1,
                                             eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    float zoom;
    nmo_object_id_t camera = param_object_of_class(engine, src1, CKPGUID_CAMERA);
    return camera != 0 && eng_camera_orthographic_zoom(engine, camera, &zoom)
               ? write_float(engine, out, zoom)
               : NMO_OK;
}

/* CKFloatGetLengthCurve, as the plugin names it: the camera's front plane */
static nmo_status_t op_get_near_clip(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    float near_plane, far_plane;
    nmo_object_id_t camera = param_object_of_class(engine, src1, CKPGUID_CAMERA);
    return camera != 0 && eng_camera_planes(engine, camera, &near_plane, &far_plane)
               ? write_float(engine, out, near_plane)
               : NMO_OK;
}

/* CKFloatGetBackPlaneCamera */
static nmo_status_t op_get_far_clip(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    float near_plane, far_plane;
    nmo_object_id_t camera = param_object_of_class(engine, src1, CKPGUID_CAMERA);
    return camera != 0 && eng_camera_planes(engine, camera, &near_plane, &far_plane)
               ? write_float(engine, out, far_plane)
               : NMO_OK;
}

/* CK2dVectorGetAspectRatioCamera: the saved width and height */
static nmo_status_t op_get_aspect_ratio(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_camera_state_t *camera =
        (const nmo_camera_state_t *)param_state(engine, src1, CKPGUID_CAMERA);
    if (camera == NULL) {
        return NMO_OK;
    }
    const float aspect[2] = {(float)camera->aspect_width, (float)camera->aspect_height};
    return write_floats(engine, out, aspect, 2u);
}

/* CK3dEntityGetTargetTargetCamera: the live target, 0 for none */
static nmo_status_t op_get_camera_target(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t camera = param_object_of_class(engine, src1, CKPGUID_TARGETCAMERA);
    return camera != 0 ? write_u32(engine, out, (uint32_t)eng_camera_target(engine, camera)) : NMO_OK;
}

/* CK3dEntityGetTargetTargetLight: the saved target */
static nmo_status_t op_get_light_target(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_targetlight_state_t *light =
        (const nmo_targetlight_state_t *)param_state(engine, src1, CKPGUID_TARGETLIGHT);
    if (light == NULL) {
        return NMO_OK;
    }
    nmo_object_id_t target =
        light->has_target ? eng_object_ref(engine, (nmo_object_id_t)param_u32(src1), &light->target) : 0;
    return write_u32(engine, out, target != 0 && eng_find_object(engine, target) != NULL ? (uint32_t)target : 0u);
}

/* CKFloatGetRangeLight: the saved range */
static nmo_status_t op_get_light_range(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    const nmo_light_state_t *light = (const nmo_light_state_t *)param_state(engine, src1, CKPGUID_LIGHT);
    return light != NULL ? write_float(engine, out, light->light_data.range) : NMO_OK;
}

/* CKColorGetColorLight: the color as it is now */
static nmo_status_t op_get_light_color(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = param_object_of_class(engine, src1, CKPGUID_LIGHT);
    const eng_light_t *light = id != 0 ? eng_light(engine, id) : NULL;
    return light != NULL ? write_floats(engine, out, light->color, 4u) : NMO_OK;
}

/* CKTextureGetTextureMaterial: the material's texture as it is now */
static nmo_status_t op_get_texture(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = param_object_of_class(engine, src1, CKPGUID_MATERIAL);
    const eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
    return material != NULL ? write_u32(engine, out, (uint32_t)material->desc.texture) : NMO_OK;
}

/* CKIntGetWidthTexture, CKIntGetHeightTexture: the image's, decoded the first time */
static nmo_status_t op_texture_size(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *out,
                                    bool height)
{
    nmo_object_id_t id = param_object_of_class(engine, src1, CKPGUID_TEXTURE);
    nmo_engine_image_t image;
    if (id == 0 || !nmo_engine_texture_image(engine, id, &image)) {
        return NMO_OK;
    }
    return write_u32(engine, out, (uint32_t)(height ? image.height : image.width));
}

static nmo_status_t op_get_texture_width(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    return op_texture_size(engine, src1, out, false);
}

static nmo_status_t op_get_texture_height(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    return op_texture_size(engine, src1, out, true);
}

/* CKIntGetSlotCountTexture: the slots as they are now (a loaded movie's frames) */
static nmo_status_t op_get_texture_slot_count(nmo_engine_t *engine, eng_param_t *src1,
                                              eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = param_object_of_class(engine, src1, CKPGUID_TEXTURE);
    const eng_texture_t *texture = id != 0 ? eng_texture(engine, id) : NULL;
    return texture != NULL ? write_u32(engine, out, (uint32_t)texture->slot_count) : NMO_OK;
}

/* CKIntGetSlotCountSprite: the saved slots */
static nmo_status_t op_get_sprite_slot_count(nmo_engine_t *engine, eng_param_t *src1,
                                             eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_sprite_state_t *sprite = (const nmo_sprite_state_t *)param_state(engine, src1, CKPGUID_SPRITE);
    return sprite != NULL ? write_u32(engine, out, sprite->bitmap.slot_count) : NMO_OK;
}

/* CK2dEntity::GetSize of the entity's rectangle, in render context pixels */
static bool param_2d_size(nmo_engine_t *engine, const eng_param_t *param, float *width, float *height)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(param);
    const eng_2d_t *entity = id != 0 ? eng_2d_entity(engine, id) : NULL;
    if (entity == NULL) {
        return false;
    }
    *width = entity->rect.right - entity->rect.left;
    *height = entity->rect.bottom - entity->rect.top;
    return true;
}

/* CKIntGetWidth2dEntity, CKIntGetHeight2dEntity: truncated */
static nmo_status_t op_get_2d_width(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    float width, height;
    return param_2d_size(engine, src1, &width, &height) ? write_u32(engine, out, (uint32_t)(int32_t)width)
                                                        : NMO_OK;
}

static nmo_status_t op_get_2d_height(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    float width, height;
    return param_2d_size(engine, src1, &width, &height) ? write_u32(engine, out, (uint32_t)(int32_t)height)
                                                        : NMO_OK;
}

/* CK2dVectorGetSize2dEntity */
static nmo_status_t op_get_2d_size(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src2;
    float size[2];
    return param_2d_size(engine, src1, &size[0], &size[1]) ? write_floats(engine, out, size, 2u) : NMO_OK;
}

/* CK2dVectorGetSizeRect */
static nmo_status_t op_get_rect_size(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    const float size[2] = {param_component(src1, 2) - param_component(src1, 0),
                           param_component(src1, 3) - param_component(src1, 1)};
    return write_floats(engine, out, size, 2u);
}

/* CK2dVectorGetCenterRect */
static nmo_status_t op_get_rect_center(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    const float left = param_component(src1, 0), top = param_component(src1, 1);
    const float center[2] = {(param_component(src1, 2) - left) * 0.5f + left,
                             (param_component(src1, 3) - top) * 0.5f + top};
    return write_floats(engine, out, center, 2u);
}

/* CKStringGetTextSpriteText: the saved text */
static nmo_status_t op_get_text(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                eng_param_t *out)
{
    (void)src2;
    const nmo_spritetext_state_t *sprite =
        (const nmo_spritetext_state_t *)param_state(engine, src1, CKPGUID_SPRITETEXT);
    if (sprite == NULL || sprite->text_content == NULL) {
        return NMO_OK;
    }
    return eng_param_write(engine, out, sprite->text_content, strlen(sprite->text_content) + 1u);
}

/* IntGetWidthNoneNone, IntGetHeightNoneNone: the render context's */
static nmo_status_t op_get_view_width(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src1;
    (void)src2;
    return write_u32(engine, out, (uint32_t)screen_mode(engine)->width);
}

static nmo_status_t op_get_view_height(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src1;
    (void)src2;
    return write_u32(engine, out, (uint32_t)screen_mode(engine)->height);
}

/* CKRectGetViewRect: the whole render context */
static nmo_status_t op_get_view_rect(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src1;
    (void)src2;
    const eng_screen_mode_t *mode = screen_mode(engine);
    const float rect[4] = {0.0f, 0.0f, (float)mode->width, (float)mode->height};
    return write_floats(engine, out, rect, 4u);
}

/* CK2dVectorGetScreenOrigin: the window is at the screen's origin */
static nmo_status_t op_get_screen_origin(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src1;
    (void)src2;
    const float origin[2] = {0.0f, 0.0f};
    return write_floats(engine, out, origin, 2u);
}

/* ============================================================================
 * Objects, classes, groups and collections
 * ============================================================================ */

/* CKBoolDerivedFromIdId: the first object is of the second's class */
static nmo_status_t op_is_derived_from(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    nmo_object_id_t a = (nmo_object_id_t)param_u32(src1), b = (nmo_object_id_t)param_u32(src2);
    if (a == 0 || b == 0 || eng_find_object(engine, a) == NULL || eng_find_object(engine, b) == NULL) {
        return NMO_OK;
    }
    return write_u32(engine, out, object_is_of_class(engine, a, nmo_engine_object_class(engine, b)) ? 1u : 0u);
}

/* CKBeObjectCastCKBeObject: the object when it is of the result's class, else 0 */
static nmo_status_t op_dynamic_cast(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    const int32_t wanted = result_class(out);
    const bool is = id != 0 && eng_find_object(engine, id) != NULL &&
                    (wanted == 0 || object_is_of_class(engine, id, wanted));
    return write_u32(engine, out, is ? (uint32_t)id : 0u);
}

/* CKIdGetGroupTypeGroup: CKGroup::GetCommonClassID, the closest class all the objects are of */
static nmo_status_t op_get_group_type(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t group = (nmo_object_id_t)param_u32(src1);
    int32_t count = group != 0 ? nmo_engine_group_count(engine, group) : -1;
    if (count < 0) {
        return write_u32(engine, out, 0u);
    }
    nmo_object_id_t first = count > 0 ? nmo_engine_group_object(engine, group, 0) : 0;
    int32_t common = first != 0 ? nmo_engine_object_class(engine, first) : NMO_CID_BEOBJECT;
    for (int32_t i = 1; i < count; i++) {
        nmo_object_id_t object = nmo_engine_group_object(engine, group, i);
        if (object == 0 || object_is_of_class(engine, object, common)) {
            continue;
        }
        /* CKGetCommonParent: up from the common class until the object is of it */
        uint32_t parent = (uint32_t)common;
        while (parent != 0 && !object_is_of_class(engine, object, (int32_t)parent)) {
            parent = nmo_type_registry_get_class_parent(engine->types, parent);
        }
        common = parent != 0 ? (int32_t)parent : NMO_CID_OBJECT;
    }
    return write_u32(engine, out, (uint32_t)common);
}

/* CKBoolEqualGroupGroup: the same objects */
static nmo_status_t op_groups_equal(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    nmo_object_id_t a = (nmo_object_id_t)param_u32(src1), b = (nmo_object_id_t)param_u32(src2);
    int32_t count_a = a != 0 ? nmo_engine_group_count(engine, a) : -1;
    int32_t count_b = b != 0 ? nmo_engine_group_count(engine, b) : -1;
    if (count_a < 0) {
        return write_u32(engine, out, count_b < 0 ? 1u : 0u);
    }
    if (count_b < 0 || count_a != count_b) {
        return write_u32(engine, out, 0u);
    }
    for (int32_t i = 0; i < count_a; i++) {
        nmo_object_id_t object = nmo_engine_group_object(engine, a, i);
        bool found = object == 0;
        for (int32_t j = 0; j < count_b && !found; j++) {
            found = nmo_engine_group_object(engine, b, j) == object;
        }
        if (!found) {
            return write_u32(engine, out, 0u);
        }
    }
    return write_u32(engine, out, 1u);
}

/* CKBoolEqualDataArrayDataArray: the same columns and values */
static nmo_status_t op_arrays_equal(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    nmo_object_id_t a = (nmo_object_id_t)param_u32(src1), b = (nmo_object_id_t)param_u32(src2);
    int32_t rows_a = a != 0 ? nmo_engine_array_row_count(engine, a) : -1;
    int32_t rows_b = b != 0 ? nmo_engine_array_row_count(engine, b) : -1;
    if (rows_a < 0 || rows_b < 0) {
        return write_u32(engine, out, rows_a < 0 && rows_b < 0 ? 1u : 0u);
    }
    int32_t columns = nmo_engine_array_column_count(engine, a);
    if (rows_a != rows_b || columns != nmo_engine_array_column_count(engine, b)) {
        return write_u32(engine, out, 0u);
    }
    for (int32_t c = 0; c < columns; c++) {
        if (nmo_engine_array_column_type(engine, a, c) != nmo_engine_array_column_type(engine, b, c)) {
            return write_u32(engine, out, 0u);
        }
    }
    for (int32_t c = 0; c < columns; c++) {
        for (int32_t r = 0; r < rows_a; r++) {
            nmo_engine_cell_t cell_a, cell_b;
            if (nmo_engine_array_get_cell(engine, a, r, c, &cell_a) != NMO_OK ||
                nmo_engine_array_get_cell(engine, b, r, c, &cell_b) != NMO_OK) {
                return write_u32(engine, out, 0u);
            }
            bool same;
            if (cell_a.text != NULL || cell_b.text != NULL) {
                same = cell_a.text != NULL && cell_b.text != NULL && strcmp(cell_a.text, cell_b.text) == 0;
            } else if (cell_a.data != NULL || cell_b.data != NULL) {
                same = cell_a.data != NULL && cell_b.data != NULL && cell_a.size == cell_b.size &&
                       nmo_guid_equals(cell_a.parameter_type, cell_b.parameter_type) &&
                       memcmp(cell_a.data, cell_b.data, cell_a.size) == 0;
            } else {
                same = cell_a.value == cell_b.value;
            }
            if (!same) {
                return write_u32(engine, out, 0u);
            }
        }
    }
    return write_u32(engine, out, 1u);
}

/* ============================================================================
 * Curves
 * ============================================================================ */

/* CKVectorGetCurvePosFloatCurve */
static nmo_status_t op_get_curve_position(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t curve = (nmo_object_id_t)param_u32(src2);
    eng_vector_t position;
    return curve != 0 && eng_curve_position(engine, curve, param_float(src1), &position)
               ? write_vector(engine, out, &position)
               : NMO_OK;
}

/* CKVectorGetCurveTangentFloatCurve: the way from the step to a thousandth further */
static nmo_status_t op_get_curve_tangent(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t curve = (nmo_object_id_t)param_u32(src2);
    float step = param_float(src1);
    if (step == 1.0f) {
        step = 0.999f;
    }
    eng_vector_t from, to;
    if (curve == 0 || !eng_curve_position(engine, curve, step, &from) ||
        !eng_curve_position(engine, curve, step + 0.001f, &to)) {
        return NMO_OK;
    }
    const eng_vector_t tangent = {to.x - from.x, to.y - from.y, to.z - from.z};
    return write_vector(engine, out, &tangent);
}

/* CKVectorGetInTangentCurvePoint, CKVectorGetOutTangentCurvePoint */
static nmo_status_t op_get_point_tangent(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *out, bool outgoing)
{
    nmo_object_id_t point = param_object_of_class(engine, src1, CKPGUID_CURVEPOINT);
    eng_vector_t in, outward;
    if (point == 0 || !eng_curve_point_tangents(engine, point, &in, &outward)) {
        return NMO_OK;
    }
    return write_vector(engine, out, outgoing ? &outward : &in);
}

static nmo_status_t op_get_in_tangent(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src2;
    return op_get_point_tangent(engine, src1, out, false);
}

static nmo_status_t op_get_out_tangent(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    return op_get_point_tangent(engine, src1, out, true);
}

/* CKCurvePointGetPointCurveInt: the control point at the index, 0 past the end or for no curve */
static nmo_status_t op_get_curve_point(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    nmo_object_id_t curve = param_object_of_class(engine, src1, CKPGUID_CURVE);
    const eng_curve_points_t *points = curve != 0 ? eng_curve_points(engine, curve) : NULL;
    int32_t index = (int32_t)param_u32(src2);
    nmo_object_id_t point = points != NULL && index >= 0 && (size_t)index < points->count ? points->ids[index] : 0;
    return write_u32(engine, out, (uint32_t)point);
}

/* ============================================================================
 * Characters and places
 * ============================================================================ */

/* The character a body part is of, 0 for none */
static nmo_object_id_t character_of_bodypart(nmo_engine_t *engine, nmo_object_id_t bodypart)
{
    for (size_t i = 0; i < engine->characters.entries.count; i++) {
        nmo_object_id_t character = engine->characters.entries.data[i].id;
        size_t count = eng_character_bodypart_count(engine, character);
        for (size_t j = 0; j < count; j++) {
            if (eng_character_bodypart(engine, character, j) == bodypart) {
                return character;
            }
        }
    }
    return 0;
}

/* CKCharacterGetCharacter3dEntity: a body part's character, a character itself, else 0 */
static nmo_status_t op_get_character(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    nmo_object_id_t character = 0;
    if (object_is_of_class(engine, id, NMO_CID_BODYPART)) {
        character = character_of_bodypart(engine, id);
    } else if (object_is_of_class(engine, id, NMO_CID_CHARACTER)) {
        character = id;
    }
    return write_u32(engine, out, (uint32_t)character);
}

/* CK3dEntityGetRootCharacter */
static nmo_status_t op_get_root(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t character = param_object_of_class(engine, src1, CKPGUID_CHARACTER);
    return character != 0 ? write_u32(engine, out, (uint32_t)eng_character_root(engine, character))
                          : NMO_OK;
}

/* CKObjectArrayGetBodyPartCharacter */
static nmo_status_t op_get_bodyparts(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t character = param_object_of_class(engine, src1, CKPGUID_CHARACTER);
    if (character == 0) {
        return NMO_OK;
    }
    size_t count = eng_character_bodypart_count(engine, character);
    uint32_t *ids = count > 0 ? (uint32_t *)malloc(count * sizeof(uint32_t)) : NULL;
    if (count > 0 && ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (size_t i = 0; i < count; i++) {
        ids[i] = (uint32_t)eng_character_bodypart(engine, character, i);
    }
    nmo_status_t status = write_ids(engine, out, ids, count);
    free(ids);
    return status;
}

/* CKBodyPartGetBodyPartByIncludedNameCharacterString: the first whose name contains the string */
static nmo_status_t op_get_bodypart_named(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t character = param_object_of_class(engine, src1, CKPGUID_CHARACTER);
    const char *wanted = param_string(src2);
    if (character == 0 || wanted == NULL) {
        return NMO_OK;
    }
    size_t count = eng_character_bodypart_count(engine, character);
    for (size_t i = 0; i < count; i++) {
        nmo_object_id_t bodypart = eng_character_bodypart(engine, character, i);
        const char *name = bodypart != 0 ? nmo_engine_object_name(engine, bodypart) : NULL;
        if (name != NULL && strstr(name, wanted) != NULL) {
            return write_u32(engine, out, (uint32_t)bodypart);
        }
    }
    return NMO_OK;
}

/* CKBoolIsBodyPartOfBodyPartCharacter */
static nmo_status_t op_is_bodypart_of(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    nmo_object_id_t bodypart = (nmo_object_id_t)param_u32(src1);
    nmo_object_id_t character = param_object_of_class(engine, src2, CKPGUID_CHARACTER);
    if (bodypart == 0 || character == 0) {
        return NMO_OK;
    }
    return write_u32(engine, out, character_of_bodypart(engine, bodypart) == character ? 1u : 0u);
}

/* CKObjectArrayGetAnimationsCharacter */
static nmo_status_t op_get_character_animations(nmo_engine_t *engine, eng_param_t *src1,
                                                eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t character = param_object_of_class(engine, src1, CKPGUID_CHARACTER);
    if (character == 0) {
        return NMO_OK;
    }
    size_t count = eng_character_animation_count(engine, character);
    uint32_t *ids = count > 0 ? (uint32_t *)malloc(count * sizeof(uint32_t)) : NULL;
    if (count > 0 && ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (size_t i = 0; i < count; i++) {
        ids[i] = (uint32_t)eng_character_animation(engine, character, i);
    }
    nmo_status_t status = write_ids(engine, out, ids, count);
    free(ids);
    return status;
}

/* CKAnimationGetAnimationCharacterString: the first whose name contains the string */
static nmo_status_t op_get_character_animation_named(nmo_engine_t *engine, eng_param_t *src1,
                                                     eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t character = param_object_of_class(engine, src1, CKPGUID_CHARACTER);
    const char *wanted = param_string(src2);
    if (character == 0 || wanted == NULL) {
        return NMO_OK;
    }
    size_t count = eng_character_animation_count(engine, character);
    for (size_t i = 0; i < count; i++) {
        nmo_object_id_t animation = eng_character_animation(engine, character, i);
        const char *name = animation != 0 ? nmo_engine_object_name(engine, animation) : NULL;
        if (name != NULL && strstr(name, wanted) != NULL) {
            return write_u32(engine, out, (uint32_t)animation);
        }
    }
    return NMO_OK;
}

/* CKObjectArrayGetPortalsPlace: every portal of the place, 0 for an unrestricted link */
static nmo_status_t op_get_portals(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t place = param_object_of_class(engine, src1, CKPGUID_PLACE);
    const eng_portal_list_t *links = place != 0 ? eng_place_portals(engine, place) : NULL;
    if (links == NULL) {
        return write_ids(engine, out, NULL, 0u);
    }
    uint32_t *ids = links->count > 0 ? (uint32_t *)malloc(links->count * sizeof(uint32_t)) : NULL;
    if (links->count > 0 && ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (size_t i = 0; i < links->count; i++) {
        ids[i] = (uint32_t)links->data[i].portal;
    }
    nmo_status_t status = write_ids(engine, out, ids, links->count);
    free(ids);
    return status;
}

/* CKObjectArrayGetPortalsPlacePlace: the portals of the first place that lead to the second */
static nmo_status_t op_get_portals_between(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t place = param_object_of_class(engine, src1, CKPGUID_PLACE);
    nmo_object_id_t other = (nmo_object_id_t)param_u32(src2);
    const eng_portal_list_t *links = place != 0 ? eng_place_portals(engine, place) : NULL;
    if (links == NULL) {
        return write_ids(engine, out, NULL, 0u);
    }
    uint32_t *ids = links->count > 0 ? (uint32_t *)malloc(links->count * sizeof(uint32_t)) : NULL;
    if (links->count > 0 && ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t count = 0;
    for (size_t i = 0; i < links->count; i++) {
        if (links->data[i].place == other && links->data[i].portal != 0) {
            ids[count++] = (uint32_t)links->data[i].portal;
        }
    }
    nmo_status_t status = write_ids(engine, out, ids, count);
    free(ids);
    return status;
}

/* ============================================================================
 * Picking and the mouse
 * ============================================================================ */

/* CKObjectWindowPickIntInt, CKObjectWindowPick2dVector: what the host picks at the point */
static nmo_status_t op_pick_at(nmo_engine_t *engine, int32_t x, int32_t y, eng_param_t *out)
{
    nmo_engine_pick_t pick;
    memset(&pick, 0, sizeof(pick));
    const bool hit = engine->input.pick != NULL &&
                     engine->input.pick(engine->input.pick_user_data, x, y, &pick);
    nmo_object_id_t picked = hit ? (pick.object != 0 ? pick.object : pick.sprite) : 0;
    const int32_t wanted = result_class(out);
    nmo_object_id_t result = 0;
    if (picked != 0 && (wanted == 0 || object_is_of_class(engine, picked, wanted))) {
        result = picked;
    } else if (picked != 0 && wanted == NMO_CID_MESH) {
        nmo_object_t *object = eng_find_object(engine, picked);
        const nmo_3dentity_state_t *entity =
            object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                 engine->types, object, CKPGUID_3DENTITY)
                           : NULL;
        result = entity != NULL ? eng_object_ref(engine, picked, &entity->current_mesh) : 0;
    }
    return write_u32(engine, out, (uint32_t)result);
}

static nmo_status_t op_pick_ints(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                 eng_param_t *out)
{
    return op_pick_at(engine, (int32_t)param_u32(src1), (int32_t)param_u32(src2), out);
}

static nmo_status_t op_pick_2dvector(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    return op_pick_at(engine, (int32_t)(int64_t)param_component(src1, 0),
                      (int32_t)(int64_t)param_component(src1, 1), out);
}

/* CK2dVectorGetMousePos: the frame's position; "absolute" clamps it to the window */
static nmo_status_t op_get_mouse_position(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    float pos[2] = {engine->input.mouse[0], engine->input.mouse[1]};
    if (src1 != NULL && param_u32(src1) != 0) {
        const eng_screen_mode_t *mode = screen_mode(engine);
        pos[0] = pos[0] < 0.0f ? 0.0f : pos[0] >= (float)mode->width ? (float)(mode->width - 1) : pos[0];
        pos[1] = pos[1] < 0.0f ? 0.0f : pos[1] >= (float)mode->height ? (float)(mode->height - 1) : pos[1];
    }
    return write_floats(engine, out, pos, 2u);
}

/* CKIntGetMouseX, CKIntGetMouseY: truncated */
static nmo_status_t op_get_mouse_x(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src1;
    (void)src2;
    return write_u32(engine, out, (uint32_t)(int32_t)(int64_t)engine->input.mouse[0]);
}

static nmo_status_t op_get_mouse_y(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src1;
    (void)src2;
    return write_u32(engine, out, (uint32_t)(int32_t)(int64_t)engine->input.mouse[1]);
}

/* ============================================================================
 * Wave sounds
 * ============================================================================ */

/* The wave sound the operand names, as the engine plays it; NULL for none */
static eng_sound_t *param_sound(nmo_engine_t *engine, const eng_param_t *param)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(param);
    return id != 0 ? eng_sound(engine, id) : NULL;
}

/* CKFloatGetVolumeWaveSound, CKFloatGetPitchWaveSound, CKFloatGetPanWaveSound */
static nmo_status_t op_get_gain(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_float(engine, out, sound->gain) : NMO_OK;
}

static nmo_status_t op_get_pitch(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                 eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_float(engine, out, sound->pitch) : NMO_OK;
}

static nmo_status_t op_get_pan(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                               eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_float(engine, out, sound->pan) : NMO_OK;
}

/* CKTimeGetPlayedMS */
static nmo_status_t op_get_played_time(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_float(engine, out, sound->playing ? sound->position : 0.0f) : NMO_OK;
}

/* CKBoolIsPlayingWaveSound, CKBoolIsPausedWaveSound, CKBoolGetLoopModeWaveSound */
static nmo_status_t op_is_playing(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_u32(engine, out, sound->playing && !sound->paused ? 1u : 0u) : NMO_OK;
}

static nmo_status_t op_is_paused(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                 eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_u32(engine, out, sound->paused ? 1u : 0u) : NMO_OK;
}

static nmo_status_t op_is_looping(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_u32(engine, out, sound->loop ? 1u : 0u) : NMO_OK;
}

/* CKStringGetSoundFileNameWaveSound: the file it plays */
static nmo_status_t op_get_sound_file_name(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    if (sound == NULL || sound->file == NULL) {
        return NMO_OK;
    }
    return eng_param_write(engine, out, sound->file, strlen(sound->file) + 1u);
}

/* CKIntGetFrequencyWaveSound: the samples a second of the file it plays */
static nmo_status_t op_get_sampling_rate(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_u32(engine, out, eng_wave_sample_rate(engine, sound->file)) : NMO_OK;
}

static const nmo_wavesound_state_t *param_wavesound(nmo_engine_t *engine, const eng_param_t *param)
{
    return (const nmo_wavesound_state_t *)param_state(engine, param, CKPGUID_WAVESOUND);
}

/* CKBoolGetFileStreamingWaveSound: the saved state's flag */
static nmo_status_t op_is_streaming(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    const nmo_wavesound_state_t *sound = param_wavesound(engine, src1);
    return sound != NULL ? write_u32(engine, out, (sound->state_flags & CK_WAVESOUND_FILESTREAMED) != 0 ? 1u : 0u)
                         : NMO_OK;
}

/* CKVectorGetRelPositionWaveSound, CKVectorGetRelDirectionWaveSound: the saved 3D settings */
static nmo_status_t op_get_relative_position(nmo_engine_t *engine, eng_param_t *src1,
                                             eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_wavesound_state_t *sound = param_wavesound(engine, src1);
    return sound != NULL ? write_floats(engine, out, &sound->position.x, 3u) : NMO_OK;
}

static nmo_status_t op_get_relative_direction(nmo_engine_t *engine, eng_param_t *src1,
                                              eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_wavesound_state_t *sound = param_wavesound(engine, src1);
    return sound != NULL ? write_floats(engine, out, &sound->direction.x, 3u) : NMO_OK;
}

/* CKVectorGetConeWaveSound: the angles in radians, then the outside gain */
static nmo_status_t op_get_cone(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                eng_param_t *out)
{
    (void)src2;
    const nmo_wavesound_state_t *sound = param_wavesound(engine, src1);
    if (sound == NULL) {
        return NMO_OK;
    }
    const float cone[3] = {sound->cone_in_angle * 0.017453292f, sound->cone_out_angle * 0.017453292f,
                           sound->cone_out_gain};
    return write_floats(engine, out, cone, 3u);
}

/* CK2dVectorGetMinMaxDistanceWaveSound */
static nmo_status_t op_get_min_max(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src2;
    const nmo_wavesound_state_t *sound = param_wavesound(engine, src1);
    if (sound == NULL) {
        return NMO_OK;
    }
    const float distances[2] = {sound->min_distance, sound->max_distance};
    return write_floats(engine, out, distances, 2u);
}

/* CKVectorGetVelocityWaveSound: nothing moves a sound here */
static nmo_status_t op_get_velocity(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    const eng_vector_t still = {0.0f, 0.0f, 0.0f};
    return param_wavesound(engine, src1) != NULL ? write_vector(engine, out, &still) : NMO_OK;
}

/* ============================================================================
 * Objects the registry has nothing for: 2D entities, curves, animations,
 * sounds, sprites, places, the level, and the render context's transforms
 * ============================================================================ */

static nmo_object_id_t param_id(const eng_param_t *param)
{
    return (nmo_object_id_t)param_u32(param);
}

/* CK2dEntityGetParent2dEntity: 0 for none */
static nmo_status_t op_get_parent_2d(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    const eng_2d_t *entity = param_id(src1) != 0 ? eng_2d_entity(engine, param_id(src1)) : NULL;
    return write_u32(engine, out, entity != NULL ? entity->parent : 0u);
}

/* CKFloatGetDistance2dEntity: between the top left corners, in render context pixels */
static nmo_status_t op_get_distance_2d(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    const eng_2d_t *a = param_id(src1) != 0 ? eng_2d_entity(engine, param_id(src1)) : NULL;
    const eng_2d_t *b = param_id(src2) != 0 ? eng_2d_entity(engine, param_id(src2)) : NULL;
    if (a == NULL || b == NULL) {
        return NMO_OK;
    }
    const float dx = a->rect.left - b->rect.left, dy = a->rect.top - b->rect.top;
    return write_float(engine, out, (float)sqrt(dy * dy + dx * dx));
}

/* CKFloatGetLengthCurve, CKIntGetCountCurve */
static nmo_status_t op_get_curve_length(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    float length;
    nmo_object_id_t curve = param_object_of_class(engine, src1, CKPGUID_CURVE);
    return curve != 0 && eng_curve_length(engine, curve, &length) ? write_float(engine, out, length)
                                                                  : NMO_OK;
}

static nmo_status_t op_get_curve_count(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t curve = param_object_of_class(engine, src1, CKPGUID_CURVE);
    if (curve == 0) {
        return NMO_OK;
    }
    const eng_curve_points_t *points = eng_curve_points(engine, curve);
    return write_u32(engine, out, points != NULL ? (uint32_t)points->count : 0u);
}

/* CKFloatGetLengthCurveCurvePoint: the point's length along its curve, when the curve is there */
static nmo_status_t op_get_curve_point_length(nmo_engine_t *engine, eng_param_t *src1,
                                              eng_param_t *src2, eng_param_t *out)
{
    float length;
    if (param_id(src1) == 0 || eng_find_object(engine, param_id(src1)) == NULL) {
        return NMO_OK;
    }
    nmo_object_id_t point = param_object_of_class(engine, src2, CKPGUID_CURVEPOINT);
    return point != 0 && eng_curve_point_length(engine, point, &length)
               ? write_float(engine, out, length)
               : NMO_OK;
}

/* CKFloatGetLengthAnimation: its length in frames */
static nmo_status_t op_get_animation_length(nmo_engine_t *engine, eng_param_t *src1,
                                            eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const nmo_animation_state_t *state =
        param_id(src1) != 0 ? eng_animation_playback_state(engine, param_id(src1)) : NULL;
    return state != NULL ? write_float(engine, out, eng_animation_length(state)) : NMO_OK;
}

/* CKFloatGetLengthWaveSound: CKWaveSound::GetSoundLength, whole milliseconds */
static nmo_status_t op_get_sound_length(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    return sound != NULL ? write_float(engine, out, (float)(int64_t)sound->length) : NMO_OK;
}

/*
 * CKFloatGetDistanceFromListenerWaveSound: CKWaveSound::GetSound3DInformation,
 * the length of the sound's position taken through the listener's Transform
 * into its attached object's space (CK2.dll's order); nothing without a listener.
 */
static nmo_status_t op_get_sound_distance(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const eng_sound_t *sound = param_sound(engine, src1);
    nmo_object_id_t listener = 0;
    if (sound == NULL || !nmo_engine_sound_listener(engine, &listener, NULL, NULL, NULL)) {
        return NMO_OK;
    }
    int32_t index = listener != 0 ? eng_entity_index(engine, listener) : -1;
    if (index < 0) {
        return NMO_OK;
    }
    int32_t ref = sound->attached != 0 ? eng_entity_index(engine, sound->attached) : -1;
    const eng_vector_t place = {sound->place[0], sound->place[1], sound->place[2]};
    eng_vector_t v;
    eng_entity_transform(engine, index, &v, &place, ref);
    return write_float(engine, out, (float)sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
}

/* CKIntGetCurrentTexture, CKIntGetCurrentSprite: the slot shown now */
static nmo_status_t op_get_current_texture(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = param_object_of_class(engine, src1, CKPGUID_TEXTURE);
    return id != 0 ? write_u32(engine, out, (uint32_t)nmo_engine_texture_current_slot(engine, id))
                   : NMO_OK;
}

static nmo_status_t op_get_current_sprite(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = param_object_of_class(engine, src1, CKPGUID_SPRITE);
    const eng_texture_t *sprite = id != 0 ? eng_texture(engine, id) : NULL;
    return sprite != NULL ? write_u32(engine, out, (uint32_t)sprite->current_slot) : NMO_OK;
}

/* CKIdGetObjectTypeObject: the class, 0 for no object */
static nmo_status_t op_get_type_beobject(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    return write_u32(engine, out, (uint32_t)nmo_engine_object_class(engine, param_id(src1)));
}

/* CKMaterialGetMaterialSprite3D: 0 for none */
static nmo_status_t op_get_material_sprite3d(nmo_engine_t *engine, eng_param_t *src1,
                                             eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const eng_sprite3d_t *sprite = param_id(src1) != 0 ? eng_sprite3d(engine, param_id(src1)) : NULL;
    return write_u32(engine, out, sprite != NULL ? sprite->material : 0u);
}

/* CKPlaceGetPlace3DEntityPlace: the portal of the place leading to the entity's place, 0 for none */
static nmo_status_t op_get_place_portal(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t other = param_id(src1), place = param_id(src2);
    const eng_portal_list_t *links =
        other != 0 && eng_find_object(engine, other) != NULL && place != 0 ? eng_place_portals(engine, place) : NULL;
    for (size_t i = 0; links != NULL && i < links->count; i++) {
        if (links->data[i].place == other) {
            return write_u32(engine, out, links->data[i].portal);
        }
    }
    return write_u32(engine, out, 0u);
}

/* CKSceneGetCurrentSceneNoneNone: 0 while the level scene is the current one (it always is) */
static nmo_status_t op_get_current_scene(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src1;
    (void)src2;
    return write_u32(engine, out, 0u);
}

/* CKLevelGetCurrentLevelNoneNone */
static nmo_status_t op_get_current_level(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    (void)src1;
    (void)src2;
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(engine, NMO_CID_LEVEL, &ids);
    nmo_object_id_t level = count > 0 ? ids[0] : 0;
    free(ids);
    return level != 0 ? write_u32(engine, out, level) : NMO_OK;
}

/* CKRectGetBox3dEntity: CK3dEntity::GetRenderExtents */
static nmo_status_t op_get_render_extents(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    int32_t index = param_entity(engine, src1);
    if (index < 0) {
        return NMO_OK;
    }
    const nmo_rect_t *r = &engine->entities.data[index].render_extents;
    const float rect[4] = {r->left, r->top, r->right, r->bottom};
    return write_floats(engine, out, rect, 4u);
}

/* CK2dVectorTransformVector3dEntity: CKRenderContext::Transform of a point in the entity's space */
static nmo_status_t op_transform_to_screen(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    const eng_vector_t point = {param_component(src1, 0), param_component(src1, 1),
                                param_component(src1, 2)};
    int32_t ref = param_entity(engine, src2);
    eng_vector_t world = point, screen;
    if (ref >= 0) {
        eng_entity_transform(engine, ref, &world, &point, -1);
    }
    eng_render_project(engine, &world, &screen);
    const float xy[2] = {screen.x, screen.y};
    return write_floats(engine, out, xy, 2u);
}

/*
 * CKVectorTransform2dVectorFloat: the screen point at that depth in front of
 * the attached camera, in the world. Both offsets are scaled by the
 * viewport's width; a perspective camera spreads them by tan(fov / 2) times
 * the depth, another by one over its zoom. Nothing without a camera.
 */
static nmo_status_t op_transform_from_screen(nmo_engine_t *engine, eng_param_t *src1,
                                             eng_param_t *src2, eng_param_t *out)
{
    const float x = param_component(src1, 0), y = param_component(src1, 1);
    const float depth = param_float(src2);
    nmo_object_id_t camera = engine->render.camera;
    int32_t index = camera != 0 ? eng_entity_index(engine, camera) : -1;
    if (index < 0) {
        return NMO_OK;
    }
    int32_t viewport[4];
    eng_render_viewport(engine, viewport);
    const float left = (float)viewport[0], top = (float)viewport[1];
    const float right = left + (float)viewport[2], bottom = top + (float)viewport[3];
    const float width = right - left, inverse = 1.0f / width;
    const float sx = (float)((x - left - width * 0.5f) * inverse) * 2.0f;
    const float sy = (float)(((bottom - top) * 0.5f + top - y) * inverse) * 2.0f;
    uint32_t projection = 1;
    float fov = 0.5f, zoom = 1.0f;
    eng_camera_projection_type(engine, camera, &projection);
    eng_vector_t local;
    if (projection == 1u) {
        eng_camera_fov(engine, camera, &fov);
        const float t = (float)tan(fov * 0.5);
        local = (eng_vector_t){t * sx * depth, t * sy * depth, depth};
    } else {
        eng_camera_orthographic_zoom(engine, camera, &zoom);
        const float scale = 1.0f / zoom;
        local = (eng_vector_t){scale * sx, scale * sy, depth};
    }
    eng_vector_t world;
    eng_entity_transform(engine, index, &world, &local, -1);
    return write_floats(engine, out, &world.x, 3u);
}

/* VxRect::Transform(destScreenSize, srcScreenSize) */
static nmo_status_t op_transform_rect_size(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    const eng_screen_mode_t *mode = screen_mode(engine);
    const float dest[2] = {(float)mode->width, (float)mode->height};
    const float source[2] = {param_component(src2, 0), param_component(src2, 1)};
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = param_component(src1, (size_t)i);
    }
    const float inverse_w = 1.0f / source[0], inverse_h = 1.0f / source[1];
    r[0] *= inverse_w * dest[0];
    r[1] *= inverse_h * dest[1];
    r[2] *= inverse_w * dest[0];
    r[3] *= inverse_h * dest[1];
    return write_floats(engine, out, r, 4u);
}

/* VxRect::Transform(destScreen, srcScreen), the destination the render context's (0, 0, w, h) */
static nmo_status_t op_transform_rect_rect(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    const eng_screen_mode_t *mode = screen_mode(engine);
    const float dest[4] = {0.0f, 0.0f, (float)mode->width, (float)mode->height};
    float r[4], s[4];
    for (int i = 0; i < 4; i++) {
        r[i] = param_component(src1, (size_t)i);
        s[i] = param_component(src2, (size_t)i);
    }
    const float inverse_w = 1.0f / (s[2] - s[0]), inverse_h = 1.0f / (s[3] - s[1]);
    const float n[4] = {(r[0] - s[0]) * inverse_w, (r[1] - s[1]) * inverse_h,
                        (r[2] - s[2]) * inverse_w, (r[3] - s[3]) * inverse_h};
    const float dw = dest[2] - dest[0], dh = dest[3] - dest[1];
    const float result[4] = {n[0] * dw + dest[0], n[1] * dh + dest[1], n[2] * dw + dest[2],
                             n[3] * dh + dest[3]};
    return write_floats(engine, out, result, 4u);
}

/* ============================================================================
 * The table
 * ============================================================================ */

typedef struct query_op {
    nmo_guid_t operation;
    nmo_guid_t result, input1, input2; /* a null input is none */
    eng_builtin_op_fn fn;
} query_op_t;

static const query_op_t query_ops[] = {
    {OP_GET_PARENT_Q, CKPGUID_2DENTITY_INIT, CKPGUID_2DENTITY_INIT, {0, 0}, op_get_parent_2d},
    {OP_GET_DISTANCE_Q, CKPGUID_FLOAT_INIT, CKPGUID_2DENTITY_INIT, CKPGUID_2DENTITY_INIT, op_get_distance_2d},
    {OP_GET_LENGTH_Q, CKPGUID_FLOAT_INIT, CKPGUID_CURVE_INIT, {0, 0}, op_get_curve_length},
    {OP_GET_LENGTH_Q, CKPGUID_FLOAT_INIT, CKPGUID_CURVE_INIT, CKPGUID_CURVEPOINT_INIT, op_get_curve_point_length},
    {OP_GET_LENGTH_Q, CKPGUID_FLOAT_INIT, CKPGUID_ANIMATION_INIT, {0, 0}, op_get_animation_length},
    {OP_GET_LENGTH_Q, CKPGUID_FLOAT_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_sound_length},
    {OP_GET_DISTANCE_Q, CKPGUID_FLOAT_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_sound_distance},
    {OP_GET_COUNT_Q, CKPGUID_INT_INIT, CKPGUID_CURVE_INIT, {0, 0}, op_get_curve_count},
    {OP_GET_CURRENT_Q, CKPGUID_INT_INIT, CKPGUID_TEXTURE_INIT, {0, 0}, op_get_current_texture},
    {OP_GET_CURRENT_Q, CKPGUID_INT_INIT, CKPGUID_SPRITE_INIT, {0, 0}, op_get_current_sprite},
    {OP_GET_CURRENT_Q, CKPGUID_SCENE_INIT, {0, 0}, {0, 0}, op_get_current_scene},
    {OP_GET_CURRENT_Q, CKPGUID_LEVEL_INIT, {0, 0}, {0, 0}, op_get_current_level},
    {OP_GET_TYPE_Q, CKPGUID_CLASSID_INIT, CKPGUID_BEOBJECT_INIT, {0, 0}, op_get_type_beobject},
    {OP_GET_MATERIAL_Q, CKPGUID_MATERIAL_INIT, CKPGUID_SPRITE3D_INIT, {0, 0}, op_get_material_sprite3d},
    {OP_GET_PLACE_Q, CKPGUID_PLACE_INIT, CKPGUID_3DENTITY_INIT, CKPGUID_PLACE_INIT, op_get_place_portal},
    {OP_GET_BOUNDING_BOX_Q, CKPGUID_RECT_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_render_extents},
    {OP_TRANSFORM_Q, CKPGUID_2DVECTOR_INIT, CKPGUID_VECTOR_INIT, CKPGUID_3DENTITY_INIT, op_transform_to_screen},
    {OP_TRANSFORM_Q, CKPGUID_VECTOR_INIT, CKPGUID_2DVECTOR_INIT, CKPGUID_FLOAT_INIT, op_transform_from_screen},
    {OP_TRANSFORM_Q, CKPGUID_RECT_INIT, CKPGUID_RECT_INIT, CKPGUID_2DVECTOR_INIT, op_transform_rect_size},
    {OP_TRANSFORM_Q, CKPGUID_RECT_INIT, CKPGUID_RECT_INIT, CKPGUID_RECT_INIT, op_transform_rect_rect},
    {OP_GET_RADIUS, CKPGUID_FLOAT_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_radius},
    {OP_GET_GEOMETRIC_CENTER, CKPGUID_VECTOR_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_center_entity},
    {OP_GET_GEOMETRIC_CENTER, CKPGUID_VECTOR_INIT, CKPGUID_BOX_INIT, {0, 0}, op_get_center_box},
    {OP_GET_GEOMETRIC_CENTER, CKPGUID_2DVECTOR_INIT, CKPGUID_RECT_INIT, {0, 0}, op_get_rect_center},
    {OP_GET_HIERARCHICAL_BOX, CKPGUID_BOX_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_hierarchical_box},
    {OP_IS_VISIBLE, CKPGUID_BOOL_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_is_visible},
    {OP_IS_VISIBLE, CKPGUID_BOOL_INIT, CKPGUID_2DENTITY_INIT, {0, 0}, op_is_visible},
    {OP_IS_ACTIVE, CKPGUID_BOOL_INIT, CKPGUID_BEOBJECT_INIT, {0, 0}, op_is_active_object},
    {OP_IS_ACTIVE, CKPGUID_BOOL_INIT, CKPGUID_SCRIPT_INIT, {0, 0}, op_is_active_script},
    {OP_IS_IN, CKPGUID_BOOL_INIT, CKPGUID_VECTOR_INIT, CKPGUID_3DENTITY_INIT, op_is_in_entity},
    {OP_IS_IN, CKPGUID_BOOL_INIT, CKPGUID_OBJECTARRAY_INIT, CKPGUID_OBJECT_INIT, op_is_in_collection},
    {OP_IS_COLLISION, CKPGUID_BOOL_INIT, CKPGUID_BOX_INIT, CKPGUID_VECTOR_INIT, op_collision_box_vector},
    {OP_IS_COLLISION, CKPGUID_BOOL_INIT, CKPGUID_BOX_INIT, CKPGUID_BOX_INIT, op_collision_box_box},
    {OP_IS_COLLISION, CKPGUID_BOOL_INIT, CKPGUID_BOX_INIT, CKPGUID_3DENTITY_INIT, op_collision_box_entity},
    {OP_IS_COLLISION, CKPGUID_BOOL_INIT, CKPGUID_3DENTITY_INIT, CKPGUID_3DENTITY_INIT, op_collision_entities},
    {OP_GET_MESH_LIST, CKPGUID_OBJECTARRAY_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_mesh_list},
    {OP_GET_REFERENCE_PLACE, CKPGUID_PLACE_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_reference_place},
    {OP_GET_ANIMATION_COUNT, CKPGUID_INT_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_animation_count},
    {OP_GET_ANIMATION, CKPGUID_OBJECTANIMATION_INIT, CKPGUID_3DENTITY_INIT, CKPGUID_INT_INIT, op_get_object_animation_at},
    {OP_GET_ANIMATION, CKPGUID_OBJECTANIMATION_INIT, CKPGUID_3DENTITY_INIT, CKPGUID_STRING_INIT, op_get_object_animation_named},

    {OP_GET_VERTEX_COUNT, CKPGUID_INT_INIT, CKPGUID_MESH_INIT, {0, 0}, op_get_vertex_count},
    {OP_GET_FACE_COUNT, CKPGUID_INT_INIT, CKPGUID_MESH_INIT, {0, 0}, op_get_face_count},
    {OP_GET_PM_VERTICES, CKPGUID_INT_INIT, CKPGUID_MESH_INIT, {0, 0}, op_get_pm_vertices},
    {OP_GET_MATERIAL_COUNT, CKPGUID_INT_INIT, CKPGUID_MESH_INIT, {0, 0}, op_get_material_count},
    {OP_GET_CHANNEL_COUNT, CKPGUID_INT_INIT, CKPGUID_MESH_INIT, {0, 0}, op_get_channel_count},
    {OP_GET_CHANNEL_BY_MATERIAL, CKPGUID_INT_INIT, CKPGUID_MESH_INIT, CKPGUID_MATERIAL_INIT, op_get_channel_by_material},
    {OP_GET_VERTEX_POSITION, CKPGUID_VECTOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_vertex_position},
    {OP_GET_VERTEX_NORMAL, CKPGUID_VECTOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_vertex_normal},
    {OP_GET_VERTEX_UVS, CKPGUID_2DVECTOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_vertex_uvs},
    {OP_GET_VERTEX_COLOR, CKPGUID_COLOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_vertex_color},
    {OP_GET_VERTEX_SPECULAR, CKPGUID_COLOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_vertex_specular},
    {OP_GET_VERTEX_WEIGHT, CKPGUID_FLOAT_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_vertex_weight},
    {OP_GET_FACE_NORMAL, CKPGUID_VECTOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_face_normal},
    {OP_GET_FACE_VERTEX, CKPGUID_VECTOR_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_face_vertex},
    {OP_GET_FACE_MATERIAL, CKPGUID_MATERIAL_INIT, CKPGUID_MESH_INIT, CKPGUID_INT_INIT, op_get_face_material},

    {OP_GET_FOV, CKPGUID_FLOAT_INIT, CKPGUID_CAMERA_INIT, {0, 0}, op_get_fov},
    {OP_GET_NEAR_CLIP, CKPGUID_FLOAT_INIT, CKPGUID_CAMERA_INIT, {0, 0}, op_get_near_clip},
    {OP_GET_FAR_CLIP, CKPGUID_FLOAT_INIT, CKPGUID_CAMERA_INIT, {0, 0}, op_get_far_clip},
    {OP_GET_ORTHOGRAPHIC_ZOOM, CKPGUID_FLOAT_INIT, CKPGUID_CAMERA_INIT, {0, 0}, op_get_orthographic_zoom},
    {OP_GET_ASPECT_RATIO, CKPGUID_2DVECTOR_INIT, CKPGUID_CAMERA_INIT, {0, 0}, op_get_aspect_ratio},
    {OP_GET_TARGET, CKPGUID_3DENTITY_INIT, CKPGUID_TARGETCAMERA_INIT, {0, 0}, op_get_camera_target},
    {OP_GET_TARGET, CKPGUID_3DENTITY_INIT, CKPGUID_TARGETLIGHT_INIT, {0, 0}, op_get_light_target},
    {OP_GET_RANGE, CKPGUID_FLOAT_INIT, CKPGUID_LIGHT_INIT, {0, 0}, op_get_light_range},
    {OP_GET_COLOR, CKPGUID_COLOR_INIT, CKPGUID_LIGHT_INIT, {0, 0}, op_get_light_color},
    {OP_GET_TEXTURE, CKPGUID_TEXTURE_INIT, CKPGUID_MATERIAL_INIT, {0, 0}, op_get_texture},
    {OP_GET_WIDTH, CKPGUID_INT_INIT, CKPGUID_TEXTURE_INIT, {0, 0}, op_get_texture_width},
    {OP_GET_HEIGHT, CKPGUID_INT_INIT, CKPGUID_TEXTURE_INIT, {0, 0}, op_get_texture_height},
    {OP_GET_SLOT_COUNT, CKPGUID_INT_INIT, CKPGUID_TEXTURE_INIT, {0, 0}, op_get_texture_slot_count},
    {OP_GET_SLOT_COUNT, CKPGUID_INT_INIT, CKPGUID_SPRITE_INIT, {0, 0}, op_get_sprite_slot_count},
    {OP_GET_WIDTH, CKPGUID_INT_INIT, CKPGUID_2DENTITY_INIT, {0, 0}, op_get_2d_width},
    {OP_GET_HEIGHT, CKPGUID_INT_INIT, CKPGUID_2DENTITY_INIT, {0, 0}, op_get_2d_height},
    {OP_GET_SIZE, CKPGUID_2DVECTOR_INIT, CKPGUID_2DENTITY_INIT, {0, 0}, op_get_2d_size},
    {OP_GET_SIZE, CKPGUID_2DVECTOR_INIT, CKPGUID_RECT_INIT, {0, 0}, op_get_rect_size},
    {OP_GET_TEXT, CKPGUID_STRING_INIT, CKPGUID_SPRITETEXT_INIT, {0, 0}, op_get_text},
    {OP_GET_WIDTH, CKPGUID_INT_INIT, {0, 0}, {0, 0}, op_get_view_width},
    {OP_GET_HEIGHT, CKPGUID_INT_INIT, {0, 0}, {0, 0}, op_get_view_height},
    {OP_GET_VIEW_RECT, CKPGUID_RECT_INIT, {0, 0}, {0, 0}, op_get_view_rect},
    {OP_GET_SCREEN_ORIGIN, CKPGUID_2DVECTOR_INIT, {0, 0}, {0, 0}, op_get_screen_origin},

    {OP_IS_DERIVED_FROM, CKPGUID_BOOL_INIT, CKPGUID_OBJECT_INIT, CKPGUID_OBJECT_INIT, op_is_derived_from},
    {OP_DYNAMIC_CAST, CKPGUID_OBJECT_INIT, CKPGUID_OBJECT_INIT, {0, 0}, op_dynamic_cast},
    {OP_GET_GROUP_TYPE, CKPGUID_CLASSID_INIT, CKPGUID_GROUP_INIT, {0, 0}, op_get_group_type},
    {OP_IS_CONTENT_EQUAL, CKPGUID_BOOL_INIT, CKPGUID_GROUP_INIT, CKPGUID_GROUP_INIT, op_groups_equal},
    {OP_IS_CONTENT_EQUAL, CKPGUID_BOOL_INIT, CKPGUID_DATAARRAY_INIT, CKPGUID_DATAARRAY_INIT, op_arrays_equal},

    {OP_GET_CURVE_POSITION, CKPGUID_VECTOR_INIT, CKPGUID_FLOAT_INIT, CKPGUID_CURVE_INIT, op_get_curve_position},
    {OP_GET_CURVE_TANGENT, CKPGUID_VECTOR_INIT, CKPGUID_FLOAT_INIT, CKPGUID_CURVE_INIT, op_get_curve_tangent},
    {OP_GET_IN_TANGENT, CKPGUID_VECTOR_INIT, CKPGUID_CURVEPOINT_INIT, {0, 0}, op_get_in_tangent},
    {OP_GET_OUT_TANGENT, CKPGUID_VECTOR_INIT, CKPGUID_CURVEPOINT_INIT, {0, 0}, op_get_out_tangent},
    {OP_GET_CURVE_POINT, CKPGUID_CURVEPOINT_INIT, CKPGUID_CURVE_INIT, CKPGUID_INT_INIT, op_get_curve_point},

    {OP_GET_CHARACTER, CKPGUID_CHARACTER_INIT, CKPGUID_3DENTITY_INIT, {0, 0}, op_get_character},
    {OP_GET_ROOT, CKPGUID_3DENTITY_INIT, CKPGUID_CHARACTER_INIT, {0, 0}, op_get_root},
    {OP_GET_BODYPARTS, CKPGUID_OBJECTARRAY_INIT, CKPGUID_CHARACTER_INIT, {0, 0}, op_get_bodyparts},
    {OP_GET_BODYPART, CKPGUID_BODYPART_INIT, CKPGUID_CHARACTER_INIT, CKPGUID_STRING_INIT, op_get_bodypart_named},
    {OP_IS_BODY_PART_OF, CKPGUID_BOOL_INIT, CKPGUID_BODYPART_INIT, CKPGUID_CHARACTER_INIT, op_is_bodypart_of},
    {OP_GET_ANIMATION, CKPGUID_OBJECTARRAY_INIT, CKPGUID_CHARACTER_INIT, {0, 0}, op_get_character_animations},
    {OP_GET_ANIMATION, CKPGUID_ANIMATION_INIT, CKPGUID_CHARACTER_INIT, CKPGUID_STRING_INIT, op_get_character_animation_named},
    {OP_GET_PORTALS, CKPGUID_OBJECTARRAY_INIT, CKPGUID_PLACE_INIT, {0, 0}, op_get_portals},
    {OP_GET_PORTALS, CKPGUID_OBJECTARRAY_INIT, CKPGUID_PLACE_INIT, CKPGUID_PLACE_INIT, op_get_portals_between},

    {OP_PICK_OBJECT, CKPGUID_RENDEROBJECT_INIT, CKPGUID_INT_INIT, CKPGUID_INT_INIT, op_pick_ints},
    {OP_PICK_OBJECT, CKPGUID_RENDEROBJECT_INIT, CKPGUID_2DVECTOR_INIT, {0, 0}, op_pick_2dvector},
    {OP_GET_MOUSE_POSITION, CKPGUID_2DVECTOR_INIT, {0, 0}, {0, 0}, op_get_mouse_position},
    {OP_GET_MOUSE_POSITION, CKPGUID_2DVECTOR_INIT, CKPGUID_BOOL_INIT, {0, 0}, op_get_mouse_position},
    {OP_GET_MOUSE_X, CKPGUID_INT_INIT, {0, 0}, {0, 0}, op_get_mouse_x},
    {OP_GET_MOUSE_Y, CKPGUID_INT_INIT, {0, 0}, {0, 0}, op_get_mouse_y},

    {OP_GET_GAIN, CKPGUID_FLOAT_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_gain},
    {OP_GET_PITCH, CKPGUID_FLOAT_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_pitch},
    {OP_GET_PAN, CKPGUID_FLOAT_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_pan},
    {OP_GET_PLAYED_TIME, CKPGUID_TIME_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_played_time},
    {OP_IS_PLAYING, CKPGUID_BOOL_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_is_playing},
    {OP_IS_PAUSED, CKPGUID_BOOL_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_is_paused},
    {OP_IS_LOOPING, CKPGUID_BOOL_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_is_looping},
    {OP_IS_STREAMING, CKPGUID_BOOL_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_is_streaming},
    {OP_GET_SOUND_FILE_NAME, CKPGUID_STRING_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_sound_file_name},
    {OP_GET_SAMPLING_RATE, CKPGUID_INT_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_sampling_rate},
    {OP_GET_RELATIVE_POSITION, CKPGUID_VECTOR_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_relative_position},
    {OP_GET_RELATIVE_DIRECTION, CKPGUID_VECTOR_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_relative_direction},
    {OP_GET_CONE, CKPGUID_VECTOR_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_cone},
    {OP_GET_MIN_MAX, CKPGUID_2DVECTOR_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_min_max},
    {OP_GET_VELOCITY, CKPGUID_VECTOR_INIT, CKPGUID_WAVESOUND_INIT, {0, 0}, op_get_velocity},
};

eng_builtin_op_fn eng_operation_builtin_query(nmo_guid_t operation, nmo_guid_t result,
                                              nmo_guid_t input1, nmo_guid_t input2)
{
    for (size_t i = 0; i < sizeof(query_ops) / sizeof(query_ops[0]); i++) {
        const query_op_t *op = &query_ops[i];
        if (nmo_guid_equals(op->operation, operation) && nmo_guid_equals(op->result, result) &&
            nmo_guid_equals(op->input1, input1) && nmo_guid_equals(op->input2, input2)) {
            return op->fn;
        }
    }
    return eng_operation_builtin_values(operation, result, input1, input2);
}
