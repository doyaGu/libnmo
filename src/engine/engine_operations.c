/**
 * @file engine_operations.c
 * @brief Parameter operations the engine runs itself: on strings and on objects.
 *
 * The operation registry computes values of a fixed size. The operations of
 * the Parameter Operations plugin that make strings, or need the objects the
 * engine runs, are here, by the functions the plugin registers for them
 * (CKStringAddStringString, CKStringSetGeneric, CKGenericSetString,
 * CKStringGetNameObject, CKObjectGetObjectByNameString,
 * CKIntGetScriptCountBeObject, CKScriptGetScriptBeObjectInt and ...String).
 * They are tried before the registry, matched as CK matches an operation:
 * by its GUID, and by the input and output types or the types they derive.
 */

#include "engine_internal.h"

#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"

#include <stdlib.h>
#include <string.h>

#define OP_ADDITION NMO_GUID(0x33cc6b49, 0x3589282b)
#define OP_CONVERT NMO_GUID(0x428009b4, 0x1caa5c78)
#define OP_GET_NAME NMO_GUID(0x5939156e, 0x62ab3472)
#define OP_GET_OBJECT_BY_NAME NMO_GUID(0x599203f6, 0x23b06096)
#define OP_GET_SCRIPT_COUNT NMO_GUID(0x551c20ba, 0x20da6d50)
#define OP_GET_SCRIPT NMO_GUID(0x250020d4, 0x10d51b91)
#define OP_GET_LENGTH NMO_GUID(0x556a69af, 0x076e3f09)
#define OP_CONTAIN_STRING NMO_GUID(0x5e866012, 0x5860306d)
#define OP_EQUAL NMO_GUID(0x6c62476b, 0x7a922973)

/* The Script parameter type (a CKBehavior that is a script) */
#define PGUID_SCRIPT NMO_GUID(0x7ea4176d, 0x1b405d30)

/* Whether `type` is `base` or derives from it */
static bool type_is(const nmo_engine_t *engine, nmo_guid_t type, nmo_guid_t base)
{
    if (nmo_guid_equals(type, base)) {
        return true;
    }
    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(engine->types, type);
    const nmo_type_descriptor_t *p = nmo_type_registry_find_by_guid(engine->types, base);
    return t != NULL && p != NULL && nmo_type_get_derivation_depth(engine->types, t->id, p->id) > 0;
}

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

typedef nmo_status_t (*engine_op_fn)(nmo_engine_t *engine, eng_param_t *src1, eng_param_t *src2,
                                     eng_param_t *out);

typedef struct engine_op {
    nmo_guid_t operation;
    nmo_guid_t result, input1, input2; /* a null input2 is no second input */
    engine_op_fn fn;
} engine_op_t;

static const engine_op_t engine_ops[] = {
    {OP_ADDITION, CKPGUID_STRING, CKPGUID_STRING, CKPGUID_STRING, op_string_add},
    {OP_GET_LENGTH, CKPGUID_INT, CKPGUID_STRING, {0, 0}, op_get_length},
    {OP_CONTAIN_STRING, CKPGUID_BOOL, CKPGUID_STRING, CKPGUID_STRING, op_contain_string},
    {OP_EQUAL, CKPGUID_BOOL, CKPGUID_STRING, CKPGUID_STRING, op_string_equal},
    {OP_GET_NAME, CKPGUID_STRING, CKPGUID_OBJECT, {0, 0}, op_get_name},
    {OP_GET_OBJECT_BY_NAME, CKPGUID_OBJECT, CKPGUID_STRING, {0, 0}, op_get_object_by_name},
    {OP_GET_SCRIPT_COUNT, CKPGUID_INT, CKPGUID_BEOBJECT, {0, 0}, op_get_script_count},
    {OP_GET_SCRIPT, PGUID_SCRIPT, CKPGUID_BEOBJECT, CKPGUID_INT, op_get_script_index},
    {OP_GET_SCRIPT, PGUID_SCRIPT, CKPGUID_BEOBJECT, CKPGUID_STRING, op_get_script_named},
};

bool eng_operation_builtin(nmo_engine_t *engine, nmo_guid_t operation, eng_param_t *in1,
                           eng_param_t *src1, eng_param_t *in2, eng_param_t *src2,
                           eng_param_t *out, nmo_status_t *out_status)
{
    nmo_guid_t t1 = in1 != NULL ? in1->type : NMO_GUID(0u, 0u);
    nmo_guid_t t2 = in2 != NULL && src2 != NULL ? in2->type : NMO_GUID(0u, 0u);
    for (size_t i = 0; i < sizeof(engine_ops) / sizeof(engine_ops[0]); i++) {
        const engine_op_t *op = &engine_ops[i];
        if (!nmo_guid_equals(op->operation, operation) || !type_is(engine, out->type, op->result) ||
            !type_is(engine, t1, op->input1)) {
            continue;
        }
        if (nmo_guid_is_null(op->input2) ? !nmo_guid_is_null(t2) : !type_is(engine, t2, op->input2)) {
            continue;
        }
        *out_status = op->fn(engine, src1, src2, out);
        return true;
    }
    /* Convert to text from any type that has text, and from text to one */
    if (nmo_guid_equals(operation, OP_CONVERT) && nmo_guid_is_null(t2) && src1 != NULL) {
        if (nmo_guid_equals(out->type, CKPGUID_STRING) && !nmo_guid_equals(t1, CKPGUID_STRING)) {
            *out_status = op_string_from(engine, src1, NULL, out);
            return true;
        }
        if (nmo_guid_equals(t1, CKPGUID_STRING) && !nmo_guid_equals(out->type, CKPGUID_STRING)) {
            *out_status = op_from_string(engine, src1, NULL, out);
            return true;
        }
    }
    return false;
}
