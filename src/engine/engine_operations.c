/**
 * @file engine_operations.c
 * @brief Parameter operations the engine runs itself: on strings and on objects.
 *
 * The operation registry computes values of a fixed size. The operations of
 * the Parameter Operations plugin that make strings, or need the objects the
 * engine runs, are here, by the functions the plugin registers for them
 * (CKStringAddStringString, CKStringSetGeneric, CKGenericSetString,
 * CKStringGetNameObject, CKIntGetTypeObject, CKObjectGetObjectByNameString,
 * CKIntGetScriptCountBeObject, CKScriptGetScriptBeObjectInt and ...String,
 * CKIntGetRowCountDataArray, CKIntGetColumnCountDataArray, CKIntGetCountGroup,
 * CKMeshGetCurrent3dEntity). The registry has no conversions, so the plugin's
 * between numbers and vectors are here too (CKIntSetFloat and the others).
 * The engine looks an operation up in this table and in the registry
 * together, as CK looks one up (see eng_operation_find()).
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_type_query.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"

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
