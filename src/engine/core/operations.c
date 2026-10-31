/**
 * @file operations.c
 * @brief Parameter operations the engine runs itself: on strings and on objects.
 *
 * The operation registry computes values of a fixed size. The operations of
 * the Parameter Operations plugin that make strings, or need the objects the
 * engine runs, are here, by the functions the plugin registers for them
 * (CKStringAddStringString, CKStringSetGeneric, CKGenericSetString,
 * CKStringGetNameObject, CKIntGetTypeObject, CKObjectGetObjectByNameString,
 * CKIntGetScriptCountBeObject, CKScriptGetScriptBeObjectInt and ...String,
 * CKIntGetRowCountDataArray, CKIntGetColumnCountDataArray, CKIntGetCountGroup,
 * CKMeshGetCurrent3dEntity), and the ones on 3D entities, which read the
 * engine's matrices (CKVectorGetPosition3dEntity3dEntity and the others).
 * The registry has no conversions, so the plugin's between numbers and
 * vectors are here too (CKIntSetFloat and the others), and it has no vector
 * components either (CKFloatGetXEuler and the others).
 * The engine looks an operation up in this table and in the registry
 * together, as CK looks one up (see eng_operation_find()).
 */

#include "../engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_type_query.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OP_ADDITION NMO_GUID(0x33cc6b49, 0x3589282b)
#define OP_CONVERT NMO_GUID(0x428009b4, 0x1caa5c78)
#define OP_GET_NAME NMO_GUID(0x5939156e, 0x62ab3472)
#define OP_GET_TYPE NMO_GUID(0x35aa0ddb, 0x7a5e3335)
#define OP_GET_OBJECT_BY_NAME NMO_GUID(0x599203f6, 0x23b06096)
#define OP_GET_SCRIPT_COUNT NMO_GUID(0x551c20ba, 0x20da6d50)
#define OP_GET_SCRIPT NMO_GUID(0x250020d4, 0x10d51b91)
#define OP_GET_LENGTH NMO_GUID(0x556a69af, 0x076e3f09)
#define OP_CONTAIN_STRING NMO_GUID(0x5e866012, 0x5860306d)
#define OP_EQUAL NMO_GUID(0x6c62476b, 0x7a922973)
#define OP_GET_COUNT NMO_GUID(0x44df6f61, 0x5f8f55c8)
#define OP_GET_ROW_COUNT NMO_GUID(0xfb556b23, 0x69b22a75)
#define OP_GET_COLUMN_COUNT NMO_GUID(0x2b554b23, 0x69b22a72)
#define OP_GET_CURRENT NMO_GUID(0x6a5c5061, 0x5efe7428)
#define OP_GET_X NMO_GUID(0x12926657, 0x6228322e)
#define OP_GET_Y NMO_GUID(0x389f72bd, 0x7aef3482)
#define OP_GET_Z NMO_GUID(0x093d5158, 0x53ef47a0)
#define OP_GET_PARENT NMO_GUID(0x5bf9718f, 0x030b698c)
#define OP_GET_CHILDREN NMO_GUID(0x0dd635d5, 0x47e62a28)
#define OP_GET_DISTANCE NMO_GUID(0x265c7d00, 0x5c8f1702)
#define OP_GET_WORLD_MATRIX NMO_GUID(0x472851ba, 0x5ccc2831)
#define OP_GET_LOCAL_MATRIX NMO_GUID(0x14a024c7, 0x050d4228)
#define OP_GET_POSITION NMO_GUID(0x4bc87aea, 0x6b5b643e)
#define OP_GET_EULER_ANGLES NMO_GUID(0x4faa099f, 0x2dcc48a5)
#define OP_GET_DIR NMO_GUID(0x48c20ded, 0xfed12252)
#define OP_GET_UP NMO_GUID(0x48c20ded, 0xfed12457)
#define OP_GET_RIGHT NMO_GUID(0x48c20ded, 0xf15a5781)
#define OP_GET_SCALE NMO_GUID(0x25991e7e, 0x32cc55e7)
#define OP_IS_CHILD_OF NMO_GUID(0x08333942, 0x00973fd1)
#define OP_TRANSFORM NMO_GUID(0x38865f64, 0x5ec70752)
#define OP_TRANSFORM_VECTOR NMO_GUID(0x5fd15589, 0x6652037f)
#define OP_INVERSE_TRANSFORM NMO_GUID(0x09ae53f3, 0x0dc769d6)
#define OP_INVERSE_TRANSFORM_VECTOR NMO_GUID(0x72213b67, 0x52fb2b56)

/* The Script parameter type (a CKBehavior that is a script) */
#define PGUID_SCRIPT NMO_GUID(0x7ea4176d, 0x1b405d30)

static const char *param_string(const eng_param_t *param)
{
    return param != NULL && param->data != NULL && param->size > 0 &&
                   memchr(param->data, '\0', param->size) != NULL
               ? (const char *)param->data
               : NULL;
}

static uint32_t param_u32(const eng_param_t *param)
{
    uint32_t value = 0;
    if (param != NULL && param->size >= sizeof(value)) {
        memcpy(&value, param->data, sizeof(value));
    }
    return value;
}

static nmo_status_t write_u32(nmo_engine_t *engine, eng_param_t *out, uint32_t value)
{
    return eng_param_write(engine, out, &value, sizeof(value));
}

static nmo_status_t write_string(nmo_engine_t *engine, eng_param_t *out, const char *text)
{
    return eng_param_write(engine, out, text, strlen(text) + 1u);
}

/* CKStringAddStringString: the first string, then the second. */
static nmo_status_t op_string_add(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    const char *a = param_string(src1), *b = param_string(src2);
    a = a != NULL ? a : "";
    b = b != NULL ? b : "";
    size_t la = strlen(a), lb = strlen(b);
    char *joined = (char *)malloc(la + lb + 1u);
    if (joined == NULL) {
        return NMO_ERR_NOMEM;
    }
    memcpy(joined, a, la);
    memcpy(joined + la, b, lb + 1u);
    nmo_status_t status = eng_param_write(engine, out, joined, la + lb + 1u);
    free(joined);
    return status;
}

/* CKStringSetGeneric: a value as text (GetStringValue). */
static nmo_status_t op_string_from(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src2;
    char *text = eng_param_text(engine, src1);
    nmo_status_t status = write_string(engine, out, text != NULL ? text : "");
    free(text);
    return status;
}

/* CKGenericSetString: a value read from text (SetStringValue). */
static nmo_status_t op_from_string(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    (void)src2;
    const char *text = param_string(src1);
    return text != NULL ? eng_param_set_text(engine, out, text) : NMO_OK;
}

/* CKIntGetLengthString: strlen, 0 without a string. */
static nmo_status_t op_get_length(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    (void)src2;
    const char *text = param_string(src1);
    return write_u32(engine, out, text != NULL ? (uint32_t)strlen(text) : 0u);
}

/* CKBoolContainStringString: whether the second string is in the first. */
static nmo_status_t op_contain_string(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    const char *a = param_string(src1), *b = param_string(src2);
    return write_u32(engine, out, a != NULL && b != NULL && strstr(a, b) != NULL ? 1u : 0u);
}

/* CKBoolEqualStringString: equal strings, both there. */
static nmo_status_t op_string_equal(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    const char *a = param_string(src1), *b = param_string(src2);
    return write_u32(engine, out, a != NULL && b != NULL && strcmp(a, b) == 0 ? 1u : 0u);
}

/* CKStringGetNameObject: the object's name, "" without one. */
static nmo_status_t op_get_name(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                eng_param_t *out)
{
    (void)src2;
    const char *name = nmo_engine_object_name(engine, (nmo_object_id_t)param_u32(src1));
    return write_string(engine, out, name != NULL ? name : "");
}

/* CKIntGetTypeObject: the object's class, written only for an object there is. */
static nmo_status_t op_get_type(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                eng_param_t *out)
{
    (void)src2;
    int32_t class_id = nmo_engine_object_class(engine, (nmo_object_id_t)param_u32(src1));
    return class_id != 0 ? write_u32(engine, out, (uint32_t)class_id) : NMO_OK;
}

/* CKObjectGetObjectByNameString: the first object of that name of the output's class. */
static nmo_status_t op_get_object_by_name(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const char *name = param_string(src1);
    uint32_t class_id = out->type_desc != NULL ? out->type_desc->class_id : 0u;
    return write_u32(engine, out, name != NULL ? (uint32_t)eng_object_named(engine, name, class_id)
                                               : 0u);
}

/* The engine's scripts of a behavioral object; NULL when it has none. */
static const eng_object_t *script_owner(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_map_get(&engine->object_map, id);
    return index >= 0 ? &engine->objects.data[index] : NULL;
}

static nmo_object_id_t owner_script(nmo_engine_t *engine, const eng_object_t *owner, uint32_t index)
{
    int32_t node = *eng_index(engine, owner->first_script + index);
    return engine->nodes.data[node].id;
}

/* CKIntGetScriptCountBeObject: written only for an object there is. */
static nmo_status_t op_get_script_count(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    if (eng_find_object(engine, id) == NULL) {
        return NMO_OK;
    }
    const eng_object_t *owner = script_owner(engine, id);
    return write_u32(engine, out, owner != NULL ? owner->script_count : 0u);
}

/* CKScriptGetScriptBeObjectInt: the script at an index, 0 for none. */
static nmo_status_t op_get_script_index(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    if (eng_find_object(engine, id) == NULL) {
        return NMO_OK;
    }
    const eng_object_t *owner = script_owner(engine, id);
    int32_t index = (int32_t)param_u32(src2);
    nmo_object_id_t script = owner != NULL && index >= 0 && (uint32_t)index < owner->script_count
                                 ? owner_script(engine, owner, (uint32_t)index)
                                 : 0u;
    return write_u32(engine, out, (uint32_t)script);
}

/* CKScriptGetScriptBeObjectString: the first script whose name contains the text. */
static nmo_status_t op_get_script_named(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    const eng_object_t *owner = script_owner(engine, (nmo_object_id_t)param_u32(src1));
    const char *search = param_string(src2);
    for (uint32_t i = 0; owner != NULL && search != NULL && i < owner->script_count; i++) {
        nmo_object_id_t script = owner_script(engine, owner, i);
        const char *name = nmo_engine_object_name(engine, script);
        if (name != NULL && strstr(name, search) != NULL) {
            return write_u32(engine, out, (uint32_t)script);
        }
    }
    return NMO_OK;
}

/* CKIntGetRowCountDataArray: written only for an array there is. */
static nmo_status_t op_get_row_count(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    return eng_find_array(engine, id) != NULL
               ? write_u32(engine, out, (uint32_t)nmo_engine_array_row_count(engine, id))
               : NMO_OK;
}

/* CKIntGetColumnCountDataArray: written only for an array there is. */
static nmo_status_t op_get_column_count(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    return eng_find_array(engine, id) != NULL
               ? write_u32(engine, out, (uint32_t)nmo_engine_array_column_count(engine, id))
               : NMO_OK;
}

/* CKIntGetCountGroup: written only for a group there is. */
static nmo_status_t op_get_group_count(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    int32_t count = nmo_engine_group_count(engine, (nmo_object_id_t)param_u32(src1));
    return count >= 0 ? write_u32(engine, out, (uint32_t)count) : NMO_OK;
}

/* CKMeshGetCurrent3dEntity: the entity's current mesh, 0 for none. */
static nmo_status_t op_get_current_mesh(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                        eng_param_t *out)
{
    (void)src2;
    nmo_object_id_t id = (nmo_object_id_t)param_u32(src1);
    nmo_object_t *object = eng_find_object(engine, id);
    const nmo_3dentity_state_t *entity =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    nmo_object_id_t mesh = entity != NULL ? nmo_ref_runtime_id(&entity->current_mesh) : 0u;
    return write_u32(engine, out,
                     mesh != 0 ? (uint32_t)eng_module_id(eng_module_of(engine, id, NULL), mesh) : 0u);
}

/* ============================================================================
 * Conversions between numbers and vectors
 * ============================================================================ */

static float param_float(const eng_param_t *param)
{
    float value = 0.0f;
    if (param != NULL && param->size >= sizeof(value)) {
        memcpy(&value, param->data, sizeof(value));
    }
    return value;
}

/* A Boolean as CK reads it there: its first byte */
static bool param_bool_byte(const eng_param_t *param)
{
    return param != NULL && param->size > 0 && param->data[0] != 0;
}

static nmo_status_t write_floats(nmo_engine_t *engine, eng_param_t *out, const float *values,
                                 size_t count)
{
    return eng_param_write(engine, out, values, count * sizeof(float));
}

/* CKIntSetFloat: truncated */
static nmo_status_t op_int_from_float(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src2;
    return write_u32(engine, out, (uint32_t)(int32_t)param_float(src1));
}

/* CKIntSetBool, CKBoolSetInt (the value is a whole int there) */
static nmo_status_t op_int_from_bool(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    return write_u32(engine, out, param_bool_byte(src1) ? 1u : 0u);
}

static nmo_status_t op_bool_from_int(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out)
{
    (void)src2;
    return write_u32(engine, out, param_u32(src1) != 0u ? 1u : 0u);
}

/* CKFloatSetInt, CKFloatSetBool */
static nmo_status_t op_float_from_int(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                      eng_param_t *out)
{
    (void)src2;
    float value = (float)(int32_t)param_u32(src1);
    return write_floats(engine, out, &value, 1u);
}

static nmo_status_t op_float_from_bool(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    float value = param_bool_byte(src1) ? 1.0f : 0.0f;
    return write_floats(engine, out, &value, 1u);
}

/* CKBoolSetFloat */
static nmo_status_t op_bool_from_float(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    return write_u32(engine, out, param_float(src1) != 0.0f ? 1u : 0u);
}

/* CK2dVectorSetFloat, CK2dVectorSetIntInt: x from the first, y from the second */
static nmo_status_t op_2dvector_from_floats(nmo_engine_t *engine, eng_param_t *src1,
                                            eng_param_t *src2, eng_param_t *out)
{
    const float xy[2] = {param_float(src1), param_float(src2)};
    return write_floats(engine, out, xy, 2u);
}

static nmo_status_t op_2dvector_from_ints(nmo_engine_t *engine, eng_param_t *src1,
                                          eng_param_t *src2, eng_param_t *out)
{
    const float xy[2] = {(float)(int32_t)param_u32(src1), (float)(int32_t)param_u32(src2)};
    return write_floats(engine, out, xy, 2u);
}

/* The `index`th float of a vector parameter, 0 past its end */
static float param_component(const eng_param_t *param, size_t index)
{
    float value = 0.0f;
    if (param != NULL && param->size >= (index + 1u) * sizeof(float)) {
        memcpy(&value, param->data + index * sizeof(float), sizeof(value));
    }
    return value;
}

/* CK2dVectorSetVector: x and y of the vector */
static nmo_status_t op_2dvector_from_vector(nmo_engine_t *engine, eng_param_t *src1,
                                            eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    const float xy[2] = {param_component(src1, 0), param_component(src1, 1)};
    return write_floats(engine, out, xy, 2u);
}

/* CKVectorSetVector2DVector: x and y of the 2D vector, z of the vector */
static nmo_status_t op_vector_from_vector_2dvector(nmo_engine_t *engine, eng_param_t *src1,
                                                   eng_param_t *src2, eng_param_t *out)
{
    const float xyz[3] = {param_component(src2, 0), param_component(src2, 1),
                          param_component(src1, 2)};
    return write_floats(engine, out, xyz, 3u);
}

/* ============================================================================
 * 3D entities
 * ============================================================================ */

/* The engine's entity an operand names; -1 for none. */
static int32_t op_entity(nmo_engine_t *engine, const eng_param_t *param)
{
    return eng_entity_index(engine, (nmo_object_id_t)param_u32(param));
}

static nmo_status_t write_vector(nmo_engine_t *engine, eng_param_t *out, eng_vector_t v)
{
    const float xyz[3] = {v.x, v.y, v.z};
    return write_floats(engine, out, xyz, 3u);
}

static eng_vector_t param_vector(const eng_param_t *param)
{
    eng_vector_t v = {param_component(param, 0), param_component(param, 1),
                      param_component(param, 2)};
    return v;
}

/* CKVectorGetPosition3dEntity3dEntity: in the second entity's space given one. */
static nmo_status_t op_get_position(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    int32_t entity = op_entity(engine, src1);
    if (entity < 0) {
        return NMO_OK;
    }
    int32_t ref = src2 != NULL ? op_entity(engine, src2) : -1;
    return write_vector(engine, out, eng_entity_position(engine, entity, ref));
}

/*
 * CK2dVectorGetPosition2dEntity: the top left corner of the entity's
 * rectangle, as the file has it (the engine does not move 2D entities).
 */
static nmo_status_t op_get_position_2d(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                       eng_param_t *out)
{
    (void)src2;
    nmo_object_t *object = eng_find_object(engine, (nmo_object_id_t)param_u32(src1));
    const nmo_2dentity_state_t *entity =
        object != NULL ? (const nmo_2dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_2DENTITY)
                       : NULL;
    if (entity == NULL) {
        return NMO_OK;
    }
    const float xy[2] = {entity->rect.left, entity->rect.top};
    return write_floats(engine, out, xy, 2u);
}

/* CKFloatGetDistance3dEntity: between the world positions. */
static nmo_status_t op_get_distance(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    int32_t a = op_entity(engine, src1), b = op_entity(engine, src2);
    if (a < 0 || b < 0) {
        return NMO_OK;
    }
    eng_vector_t pa = eng_entity_position(engine, a, -1), pb = eng_entity_position(engine, b, -1);
    const float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
    const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    return write_floats(engine, out, &distance, 1u);
}

/* CKVectorGetDistance3dEntity3dEntity: the second's world position minus the first's. */
static nmo_status_t op_get_distance_vector(nmo_engine_t *engine, eng_param_t *src1,
                                           eng_param_t *src2, eng_param_t *out)
{
    int32_t a = op_entity(engine, src1), b = op_entity(engine, src2);
    if (a < 0 || b < 0) {
        return NMO_OK;
    }
    eng_vector_t pa = eng_entity_position(engine, a, -1), pb = eng_entity_position(engine, b, -1);
    eng_vector_t d = {pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
    return write_vector(engine, out, d);
}

/* CKFloatGetX3dEntity and its Y and Z: a world position's component. */
static nmo_status_t op_entity_component(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *out,
                                        int component)
{
    int32_t entity = op_entity(engine, src1);
    if (entity < 0) {
        return NMO_OK;
    }
    eng_vector_t p = eng_entity_position(engine, entity, -1);
    const float v = component == 0 ? p.x : component == 1 ? p.y : p.z;
    return write_floats(engine, out, &v, 1u);
}

static nmo_status_t op_get_x_entity(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    return op_entity_component(engine, src1, out, 0);
}

static nmo_status_t op_get_y_entity(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    return op_entity_component(engine, src1, out, 1);
}

static nmo_status_t op_get_z_entity(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    return op_entity_component(engine, src1, out, 2);
}

/* CKFloatGetXEuler (vectors, angles), CKFloatGetX2dVector, ... : a component. */
static nmo_status_t op_get_x(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                             eng_param_t *out)
{
    (void)src2;
    const float v = param_component(src1, 0);
    return write_floats(engine, out, &v, 1u);
}

static nmo_status_t op_get_y(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                             eng_param_t *out)
{
    (void)src2;
    const float v = param_component(src1, 1);
    return write_floats(engine, out, &v, 1u);
}

static nmo_status_t op_get_z(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                             eng_param_t *out)
{
    (void)src2;
    const float v = param_component(src1, 2);
    return write_floats(engine, out, &v, 1u);
}

/* CKVectorGetScale: the local scale. */
static nmo_status_t op_get_scale(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                 eng_param_t *out)
{
    (void)src2;
    int32_t entity = op_entity(engine, src1);
    return entity >= 0 ? write_vector(engine, out, eng_entity_scale(engine, entity, true)) : NMO_OK;
}

/* CKVectorGetDir3dEntity, ...Up..., ...Right...: a unit world axis. */
static nmo_status_t op_entity_axis(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *out,
                                   int axis)
{
    int32_t entity = op_entity(engine, src1);
    if (entity < 0) {
        return NMO_OK;
    }
    eng_vector_t axes[3];
    eng_entity_orientation(engine, entity, &axes[2], &axes[1], &axes[0], -1);
    return write_vector(engine, out, axes[axis]);
}

static nmo_status_t op_get_dir(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                               eng_param_t *out)
{
    (void)src2;
    return op_entity_axis(engine, src1, out, 2);
}

static nmo_status_t op_get_up(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                              eng_param_t *out)
{
    (void)src2;
    return op_entity_axis(engine, src1, out, 1);
}

static nmo_status_t op_get_right(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                 eng_param_t *out)
{
    (void)src2;
    return op_entity_axis(engine, src1, out, 0);
}

/* CKVectorTransformVector3dEntity and the other three: the vector through the entity. */
static nmo_status_t op_entity_transform(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out, bool direction,
                                        bool inverse)
{
    int32_t entity = op_entity(engine, src2);
    if (entity < 0) {
        return NMO_OK;
    }
    eng_vector_t in = param_vector(src1), result;
    if (direction) {
        eng_entity_transform_vector(engine, entity, &result, &in, -1, inverse);
    } else if (inverse) {
        eng_entity_inverse_transform(engine, entity, &result, &in, -1);
    } else {
        eng_entity_transform(engine, entity, &result, &in, -1);
    }
    return write_vector(engine, out, result);
}

static nmo_status_t op_transform(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                 eng_param_t *out)
{
    return op_entity_transform(engine, src1, src2, out, false, false);
}

static nmo_status_t op_transform_vector(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                        eng_param_t *out)
{
    return op_entity_transform(engine, src1, src2, out, true, false);
}

static nmo_status_t op_inverse_transform(nmo_engine_t *engine, eng_param_t *src1,
                                         eng_param_t *src2, eng_param_t *out)
{
    return op_entity_transform(engine, src1, src2, out, false, true);
}

static nmo_status_t op_inverse_transform_vector(nmo_engine_t *engine, eng_param_t *src1,
                                                eng_param_t *src2, eng_param_t *out)
{
    return op_entity_transform(engine, src1, src2, out, true, true);
}

/* CKEulerGetEuler3dEntity3dEntity: the angles of the world matrix, in the second's space. */
static nmo_status_t op_get_euler_angles(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    int32_t entity = op_entity(engine, src1);
    if (entity < 0) {
        return NMO_OK;
    }
    int32_t ref = src2 != NULL ? op_entity(engine, src2) : -1;
    eng_matrix_t m = engine->entities.data[entity].world;
    if (ref >= 0) {
        eng_matrix_t inverse = engine->entities.data[ref].world;
        eng_matrix_inverse(&inverse, &engine->entities.data[ref].world);
        eng_matrix_multiply(&m, &inverse, &m);
    }
    eng_vector_t angles;
    eng_matrix_to_euler(&m, &angles.x, &angles.y, &angles.z);
    return write_vector(engine, out, angles);
}

/* CKMatrixGetWorldMatrix3dEntity, CKMatrixGetLocalMatrix3dEntity */
static nmo_status_t op_get_world_matrix(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    int32_t entity = op_entity(engine, src1);
    return entity >= 0 ? eng_param_write(engine, out, engine->entities.data[entity].world.m,
                                         sizeof(eng_matrix_t))
                       : NMO_OK;
}

static nmo_status_t op_get_local_matrix(nmo_engine_t *engine, eng_param_t *src1,
                                        eng_param_t *src2, eng_param_t *out)
{
    (void)src2;
    int32_t entity = op_entity(engine, src1);
    return entity >= 0 ? eng_param_write(engine, out, engine->entities.data[entity].local.m,
                                         sizeof(eng_matrix_t))
                       : NMO_OK;
}

/* CKBoolIsChildOf3dEntity3dEntity: whether the second is an ancestor of the first. */
static nmo_status_t op_is_child_of(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                   eng_param_t *out)
{
    int32_t entity = op_entity(engine, src1), ancestor = op_entity(engine, src2);
    if (entity < 0 || ancestor < 0) {
        return NMO_OK;
    }
    int32_t parent = engine->entities.data[entity].parent;
    while (parent >= 0 && parent != ancestor) {
        parent = engine->entities.data[parent].parent;
    }
    return write_u32(engine, out, parent >= 0 ? 1u : 0u);
}

/* CK3dEntityGetParent3dEntity: 0 for none. */
static nmo_status_t op_get_parent(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                  eng_param_t *out)
{
    (void)src2;
    return write_u32(engine, out,
                     (uint32_t)nmo_engine_entity_parent(engine, (nmo_object_id_t)param_u32(src1)));
}

/* CKObjectArrayGetChildren3dEntity: the children's IDs, in order. */
static nmo_status_t op_get_children(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                    eng_param_t *out)
{
    (void)src2;
    int32_t entity = op_entity(engine, src1);
    size_t count = 0;
    for (int32_t c = entity >= 0 ? engine->entities.data[entity].first_child : -1; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        count++;
    }
    uint32_t *ids = count > 0 ? (uint32_t *)malloc(count * sizeof(uint32_t)) : NULL;
    if (count > 0 && ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t n = 0;
    for (int32_t c = entity >= 0 ? engine->entities.data[entity].first_child : -1; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        ids[n++] = (uint32_t)engine->entities.data[c].id;
    }
    nmo_status_t status = eng_param_write(engine, out, ids, n * sizeof(uint32_t));
    free(ids);
    return status;
}

typedef struct engine_op {
    nmo_guid_t operation;
    nmo_guid_t result, input1, input2; /* a null input2 is no second input */
    eng_builtin_op_fn fn;
} engine_op_t;

static const engine_op_t engine_ops[] = {
    {OP_ADDITION, CKPGUID_STRING, CKPGUID_STRING, CKPGUID_STRING, op_string_add},
    {OP_GET_LENGTH, CKPGUID_INT, CKPGUID_STRING, {0, 0}, op_get_length},
    {OP_CONTAIN_STRING, CKPGUID_BOOL, CKPGUID_STRING, CKPGUID_STRING, op_contain_string},
    {OP_EQUAL, CKPGUID_BOOL, CKPGUID_STRING, CKPGUID_STRING, op_string_equal},
    {OP_GET_NAME, CKPGUID_STRING, CKPGUID_OBJECT, {0, 0}, op_get_name},
    {OP_GET_TYPE, CKPGUID_CLASSID, CKPGUID_OBJECT, {0, 0}, op_get_type},
    {OP_GET_OBJECT_BY_NAME, CKPGUID_OBJECT, CKPGUID_STRING, {0, 0}, op_get_object_by_name},
    {OP_GET_SCRIPT_COUNT, CKPGUID_INT, CKPGUID_BEOBJECT, {0, 0}, op_get_script_count},
    {OP_GET_SCRIPT, PGUID_SCRIPT, CKPGUID_BEOBJECT, CKPGUID_INT, op_get_script_index},
    {OP_GET_SCRIPT, PGUID_SCRIPT, CKPGUID_BEOBJECT, CKPGUID_STRING, op_get_script_named},
    {OP_GET_COUNT, CKPGUID_INT, CKPGUID_DATAARRAY, {0, 0}, op_get_row_count},
    {OP_GET_COUNT, CKPGUID_INT, CKPGUID_GROUP, {0, 0}, op_get_group_count},
    {OP_GET_ROW_COUNT, CKPGUID_INT, CKPGUID_DATAARRAY, {0, 0}, op_get_row_count},
    {OP_GET_COLUMN_COUNT, CKPGUID_INT, CKPGUID_DATAARRAY, {0, 0}, op_get_column_count},
    {OP_GET_CURRENT, CKPGUID_MESH, CKPGUID_3DENTITY, {0, 0}, op_get_current_mesh},
    {OP_CONVERT, CKPGUID_INT, CKPGUID_FLOAT, {0, 0}, op_int_from_float},
    {OP_CONVERT, CKPGUID_INT, CKPGUID_BOOL, {0, 0}, op_int_from_bool},
    {OP_CONVERT, CKPGUID_FLOAT, CKPGUID_INT, {0, 0}, op_float_from_int},
    {OP_CONVERT, CKPGUID_FLOAT, CKPGUID_BOOL, {0, 0}, op_float_from_bool},
    {OP_CONVERT, CKPGUID_BOOL, CKPGUID_INT, {0, 0}, op_bool_from_int},
    {OP_CONVERT, CKPGUID_BOOL, CKPGUID_FLOAT, {0, 0}, op_bool_from_float},
    {OP_CONVERT, CKPGUID_2DVECTOR, CKPGUID_FLOAT, CKPGUID_FLOAT, op_2dvector_from_floats},
    {OP_CONVERT, CKPGUID_2DVECTOR, CKPGUID_INT, CKPGUID_INT, op_2dvector_from_ints},
    {OP_CONVERT, CKPGUID_2DVECTOR, CKPGUID_VECTOR, {0, 0}, op_2dvector_from_vector},
    {OP_CONVERT, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_2DVECTOR, op_vector_from_vector_2dvector},
    {OP_GET_POSITION, CKPGUID_VECTOR, CKPGUID_3DENTITY, CKPGUID_3DENTITY, op_get_position},
    {OP_GET_POSITION, CKPGUID_VECTOR, CKPGUID_3DENTITY, {0, 0}, op_get_position},
    {OP_GET_POSITION, CKPGUID_2DVECTOR, CKPGUID_2DENTITY, {0, 0}, op_get_position_2d},
    {OP_GET_DISTANCE, CKPGUID_FLOAT, CKPGUID_3DENTITY, CKPGUID_3DENTITY, op_get_distance},
    {OP_GET_DISTANCE, CKPGUID_VECTOR, CKPGUID_3DENTITY, CKPGUID_3DENTITY, op_get_distance_vector},
    {OP_GET_X, CKPGUID_FLOAT, CKPGUID_3DENTITY, {0, 0}, op_get_x_entity},
    {OP_GET_Y, CKPGUID_FLOAT, CKPGUID_3DENTITY, {0, 0}, op_get_y_entity},
    {OP_GET_Z, CKPGUID_FLOAT, CKPGUID_3DENTITY, {0, 0}, op_get_z_entity},
    {OP_GET_X, CKPGUID_FLOAT, CKPGUID_VECTOR, {0, 0}, op_get_x},
    {OP_GET_Y, CKPGUID_FLOAT, CKPGUID_VECTOR, {0, 0}, op_get_y},
    {OP_GET_Z, CKPGUID_FLOAT, CKPGUID_VECTOR, {0, 0}, op_get_z},
    {OP_GET_X, CKPGUID_FLOAT, CKPGUID_2DVECTOR, {0, 0}, op_get_x},
    {OP_GET_Y, CKPGUID_FLOAT, CKPGUID_2DVECTOR, {0, 0}, op_get_y},
    {OP_GET_X, CKPGUID_FLOAT, CKPGUID_EULERANGLES, {0, 0}, op_get_x},
    {OP_GET_Y, CKPGUID_FLOAT, CKPGUID_EULERANGLES, {0, 0}, op_get_y},
    {OP_GET_Z, CKPGUID_FLOAT, CKPGUID_EULERANGLES, {0, 0}, op_get_z},
    {OP_GET_SCALE, CKPGUID_VECTOR, CKPGUID_3DENTITY, {0, 0}, op_get_scale},
    {OP_GET_DIR, CKPGUID_VECTOR, CKPGUID_3DENTITY, {0, 0}, op_get_dir},
    {OP_GET_UP, CKPGUID_VECTOR, CKPGUID_3DENTITY, {0, 0}, op_get_up},
    {OP_GET_RIGHT, CKPGUID_VECTOR, CKPGUID_3DENTITY, {0, 0}, op_get_right},
    {OP_TRANSFORM, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_3DENTITY, op_transform},
    {OP_TRANSFORM_VECTOR, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_3DENTITY, op_transform_vector},
    {OP_INVERSE_TRANSFORM, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_3DENTITY, op_inverse_transform},
    {OP_INVERSE_TRANSFORM_VECTOR, CKPGUID_VECTOR, CKPGUID_VECTOR, CKPGUID_3DENTITY,
     op_inverse_transform_vector},
    {OP_GET_EULER_ANGLES, CKPGUID_EULERANGLES, CKPGUID_3DENTITY, CKPGUID_3DENTITY,
     op_get_euler_angles},
    {OP_GET_WORLD_MATRIX, CKPGUID_MATRIX, CKPGUID_3DENTITY, {0, 0}, op_get_world_matrix},
    {OP_GET_LOCAL_MATRIX, CKPGUID_MATRIX, CKPGUID_3DENTITY, {0, 0}, op_get_local_matrix},
    {OP_IS_CHILD_OF, CKPGUID_BOOL, CKPGUID_3DENTITY, CKPGUID_3DENTITY, op_is_child_of},
    {OP_GET_PARENT, CKPGUID_3DENTITY, CKPGUID_3DENTITY, {0, 0}, op_get_parent},
    {OP_GET_CHILDREN, CKPGUID_OBJECTARRAY, CKPGUID_3DENTITY, {0, 0}, op_get_children},
};

eng_builtin_op_fn eng_operation_builtin(nmo_guid_t operation, nmo_guid_t result,
                                        nmo_guid_t input1, nmo_guid_t input2)
{
    for (size_t i = 0; i < sizeof(engine_ops) / sizeof(engine_ops[0]); i++) {
        const engine_op_t *op = &engine_ops[i];
        if (nmo_guid_equals(op->operation, operation) && nmo_guid_equals(op->result, result) &&
            nmo_guid_equals(op->input1, input1) && nmo_guid_equals(op->input2, input2)) {
            return op->fn;
        }
    }
    return NULL;
}

eng_builtin_op_fn eng_operation_builtin_text(nmo_guid_t operation, nmo_guid_t result,
                                             nmo_guid_t input1, nmo_guid_t input2)
{
    /* Convert to text from any type that has text, and from text to one */
    if (!nmo_guid_equals(operation, OP_CONVERT) || !nmo_guid_is_null(input2) ||
        nmo_guid_is_null(input1)) {
        return NULL;
    }
    if (nmo_guid_equals(result, CKPGUID_STRING) && !nmo_guid_equals(input1, CKPGUID_STRING)) {
        return op_string_from;
    }
    if (nmo_guid_equals(input1, CKPGUID_STRING) && !nmo_guid_equals(result, CKPGUID_STRING)) {
        return op_from_string;
    }
    return NULL;
}
