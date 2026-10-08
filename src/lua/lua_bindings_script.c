/**
 * @file lua_bindings_script.c
 * @brief The nmo.script module: the script model and index as Lua tables
 */

#include "lua_bindings_internal.h"

#include "behavior/nmo_script_index.h"
#include "behavior/nmo_script_model.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_class_ids.h"
#include "lua/nmo_lua_script.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"

#include <stdio.h>
#include <string.h>

#include "lauxlib.h"

/* lua_script_model.lua, the classes of the tables */
static const char script_classes_source[] = {
#include "lua_script_model.lua.inc"
};

static const char script_classes_key[] = "nmo.script.classes";

/* Push the class table, loading lua_script_model.lua the first time. */
static void script_push_classes(lua_State *L)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, script_classes_key) == LUA_TTABLE) {
        return;
    }
    lua_pop(L, 1);
    if (luaL_loadbuffer(L, script_classes_source, sizeof(script_classes_source) - 1,
                        "=nmo.script") != LUA_OK) {
        lua_error(L);
    }
    lua_call(L, 0, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, script_classes_key);
}

/* ============================================================================
 * Building the tables
 * ============================================================================ */

typedef struct script_push {
    lua_State *L;
    const nmo_script_model_t *model;
    const nmo_script_index_t *index;
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *registry;
    int classes; /* absolute stack indexes */
    int by_id;
    int objects;
    int model_table;
} script_push_t;

/* Push a new table of class `class_name`. */
static void script_new(const script_push_t *sp, const char *class_name, int narr, int nrec)
{
    lua_createtable(sp->L, narr, nrec);
    lua_getfield(sp->L, sp->classes, class_name);
    lua_setmetatable(sp->L, -2);
}

static void script_set_string(lua_State *L, const char *field, const char *value)
{
    if (value != NULL) {
        lua_pushstring(L, value);
        lua_setfield(L, -2, field);
    }
}

static void script_set_integer(lua_State *L, const char *field, lua_Integer value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, field);
}

static void script_set_boolean(lua_State *L, const char *field, bool value)
{
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, field);
}

static void script_set_guid(lua_State *L, const char *field, nmo_guid_t guid)
{
    char text[NMO_GUID_STRING_SIZE];
    if (!nmo_guid_is_null(guid) && nmo_guid_format(guid, text, sizeof(text)) > 0) {
        script_set_string(L, field, text);
    }
}

static void script_set_label(const script_push_t *sp, nmo_object_id_t id)
{
    char label[256];
    (void)nmo_script_model_label(sp->model, id, label, sizeof(label));
    script_set_string(sp->L, "label", label);
}

/* Set field `field` of the table on top to a new empty list. */
static void script_set_list(lua_State *L, const char *field)
{
    lua_newtable(L);
    lua_setfield(L, -2, field);
}

/* Append the value on top to list `field` of the table at `table_index`, popping it. */
static void script_append(lua_State *L, int table_index, const char *field)
{
    table_index = lua_absindex(L, table_index);
    lua_getfield(L, table_index, field);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    lua_pop(L, 2);
}

/* Push the item of id `id` built so far, or nil. */
static void script_push_item(const script_push_t *sp, nmo_object_id_t id)
{
    if (id == 0) {
        lua_pushnil(sp->L);
        return;
    }
    lua_rawgeti(sp->L, sp->by_id, (lua_Integer)id);
}

static void script_set_item(const script_push_t *sp, const char *field, nmo_object_id_t id)
{
    script_push_item(sp, id);
    lua_setfield(sp->L, -2, field);
}

/* Push the table of object `id`: the node, parameter, ... table of an object the model
 * holds, an object table ({id, name, class}) for any other, or nil for 0. */
static void script_push_object(const script_push_t *sp, nmo_object_id_t id)
{
    lua_State *L = sp->L;
    if (id == 0) {
        lua_pushnil(L);
        return;
    }
    if (lua_rawgeti(L, sp->by_id, (lua_Integer)id) != LUA_TNIL) {
        return;
    }
    lua_pop(L, 1);
    if (lua_rawgeti(L, sp->objects, (lua_Integer)id) != LUA_TNIL) {
        return;
    }
    lua_pop(L, 1);
    nmo_object_t *object = nmo_object_repository_find_by_id(sp->repo, id);
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    const char *class_name = object != NULL
        ? nmo_type_query_class_name_from_id(sp->registry, nmo_object_get_class_id(object))
        : NULL;
    script_new(sp, "Object", 0, 8);
    lua_pushvalue(L, -1);
    lua_rawseti(L, sp->objects, (lua_Integer)id);
    script_set_integer(L, "id", (lua_Integer)id);
    script_set_string(L, "name", name != NULL ? name : "");
    script_set_string(L, "class", class_name);
    script_set_boolean(L, "missing", object == NULL);
    if (object == NULL) {
        return;
    }
    /* classes: the set of the names of its class and the classes it derives from */
    nmo_class_id_t class_id = nmo_object_get_class_id(object);
    script_set_integer(L, "class_id", (lua_Integer)class_id);
    lua_newtable(L);
    for (int depth = 0; class_id != 0 && depth < 32; depth++) {
        const char *ancestor = nmo_type_query_class_name_from_id(sp->registry, class_id);
        if (ancestor != NULL) {
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, ancestor);
        }
        nmo_class_id_t parent = nmo_type_query_class_get_parent(sp->registry, class_id);
        class_id = parent != class_id ? parent : 0;
    }
    lua_setfield(L, -2, "classes");
    /* a group: its members */
    if (nmo_object_get_class_id(object) == NMO_CID_GROUP) {
        const nmo_group_state_t *group = (const nmo_group_state_t *)nmo_object_get_state(object);
        size_t count = group != NULL ? group->object_ids.count : 0;
        lua_createtable(L, (int)count, 0);
        for (size_t i = 0; i < count; i++) {
            const nmo_ref_t *ref = (const nmo_ref_t *)nmo_array_get(&group->object_ids, i);
            nmo_object_id_t member = ref != NULL ? nmo_ref_runtime_id(ref) : 0;
            if (member != 0) {
                script_push_object(sp, member);
                lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
            }
        }
        lua_setfield(L, -2, "members");
    }
}

static void script_set_object(const script_push_t *sp, const char *field, nmo_object_id_t id)
{
    script_push_object(sp, id);
    lua_setfield(sp->L, -2, field);
}

/* Set `text` and `data` of the parameter table on top to the value it holds. */
static void script_set_value(const script_push_t *sp, nmo_object_id_t param_id)
{
    lua_State *L = sp->L;
    char text[1024];
    if (nmo_script_model_param_value(sp->model, param_id, text, sizeof(text)) == NMO_OK) {
        script_set_string(L, "text", text);
    }
    nmo_script_datum_t datum;
    if (nmo_script_model_param_datum(sp->model, param_id, &datum, text, sizeof(text)) !=
        NMO_OK) {
        return;
    }
    switch (datum.kind) {
    case NMO_SCRIPT_DATUM_BOOLEAN:
        lua_pushboolean(L, datum.boolean ? 1 : 0);
        break;
    case NMO_SCRIPT_DATUM_INTEGER:
        lua_pushinteger(L, (lua_Integer)datum.integer);
        break;
    case NMO_SCRIPT_DATUM_FLOAT:
        lua_pushnumber(L, (lua_Number)datum.number);
        break;
    case NMO_SCRIPT_DATUM_STRING:
    case NMO_SCRIPT_DATUM_MESSAGE:
        lua_pushstring(L, datum.text);
        break;
    case NMO_SCRIPT_DATUM_OBJECT:
        script_push_object(sp, datum.object_id);
        break;
    case NMO_SCRIPT_DATUM_FLOATS:
        lua_createtable(L, (int)datum.float_count, 0);
        for (uint32_t i = 0; i < datum.float_count; i++) {
            lua_pushnumber(L, (lua_Number)datum.floats[i]);
            lua_rawseti(L, -2, (lua_Integer)i + 1);
        }
        break;
    default:
        return;
    }
    lua_setfield(L, -2, "data");
}

static const char *script_value_kind_name(nmo_script_value_kind_t kind)
{
    switch (kind) {
    case NMO_SCRIPT_VALUE_SAVED:    return "saved";
    case NMO_SCRIPT_VALUE_COMPUTED: return "computed";
    case NMO_SCRIPT_VALUE_WRITTEN:  return "written";
    default:                        return "none";
    }
}

static void script_push_param(const script_push_t *sp, nmo_object_id_t id);

/* Set `origin` and `origin_kind` of the parameter table on top. */
static void script_set_origin(const script_push_t *sp, nmo_object_id_t param_id)
{
    nmo_object_id_t holder = 0;
    nmo_script_value_kind_t kind = nmo_script_model_value_source(sp->model, param_id, &holder);
    script_set_string(sp->L, "origin_kind", script_value_kind_name(kind));
    script_push_param(sp, holder);
    lua_setfield(sp->L, -2, "origin");
}

/* Push the parameter table of id `id`; a parameter the model does not hold
 * (an external source) gets one the first time. */
static void script_push_param(const script_push_t *sp, nmo_object_id_t id)
{
    lua_State *L = sp->L;
    script_push_item(sp, id);
    if (id == 0 || !lua_isnil(L, -1)) {
        return;
    }
    lua_pop(L, 1);
    nmo_object_t *object = nmo_object_repository_find_by_id(sp->repo, id);
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    script_new(sp, "Param", 0, 10);
    lua_pushvalue(L, -1);
    lua_rawseti(L, sp->by_id, (lua_Integer)id);
    script_set_integer(L, "id", (lua_Integer)id);
    script_set_string(L, "name", name != NULL ? name : "");
    script_set_string(L, "role", "external");
    script_set_boolean(L, "external", true);
    script_set_list(L, "in_edges");
    script_set_list(L, "out_edges");
    script_set_value(sp, id);
    script_set_origin(sp, id);
}

/* Append the item of id `id` to list `field` of the table on top. */
static void script_append_item(const script_push_t *sp, const char *field, nmo_object_id_t id)
{
    script_push_item(sp, id);
    if (lua_isnil(sp->L, -1)) {
        lua_pop(sp->L, 1);
        return;
    }
    script_append(sp->L, -2, field);
}

/* Store the table on top under its id and in model list `list`, keeping it on top. */
static void script_register(const script_push_t *sp, nmo_object_id_t id, const char *list)
{
    lua_State *L = sp->L;
    if (id != 0) {
        lua_pushvalue(L, -1);
        lua_rawseti(L, sp->by_id, (lua_Integer)id);
    }
    lua_pushvalue(L, -1);
    script_append(L, sp->model_table, list);
}

static void script_build_nodes(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_node_t *nodes = nmo_script_model_nodes(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_node_t *node = &nodes[i];
        script_new(sp, "Node", 0, 32);
        lua_pushvalue(L, sp->model_table);
        lua_setfield(L, -2, "model");
        script_set_integer(L, "id", (lua_Integer)node->id);
        script_set_string(L, "kind", nmo_script_node_kind_name(node->kind));
        script_set_string(L, "name", node->name);
        script_set_label(sp, node->id);
        script_set_integer(L, "depth", (lua_Integer)node->depth);
        script_set_integer(L, "flags", (lua_Integer)node->flags);
        script_set_string(L, "proto", node->prototype_name);
        script_set_guid(L, "proto_guid", node->prototype_guid);
        script_set_string(L, "op", node->operation_name);
        script_set_guid(L, "op_guid", node->operation_guid);
        static const char *const lists[] = {"children", "inputs", "outputs", "pins", "pouts",
                                            "locals", "settings", "operations", "links",
                                            "edges", "uses"};
        for (size_t l = 0; l < sizeof(lists) / sizeof(lists[0]); l++) {
            script_set_list(L, lists[l]);
        }
        script_register(sp, node->id, "nodes");
        lua_pop(L, 1);
    }
}

static void script_build_ios(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_io_t *ios = nmo_script_model_ios(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        script_new(sp, "Io", 0, 8);
        script_set_integer(L, "id", (lua_Integer)ios[i].id);
        script_set_string(L, "name", ios[i].name);
        script_set_integer(L, "index", (lua_Integer)ios[i].index + 1);
        script_set_boolean(L, "is_output", ios[i].is_output);
        script_set_list(L, "links");
        script_set_list(L, "incoming");
        script_register(sp, ios[i].id, "ios");
        lua_pop(L, 1);
    }
}

static void script_build_params(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_param_t *params = nmo_script_model_params(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_param_t *param = &params[i];
        if (nmo_script_model_find_param(sp->model, param->id) != param) {
            continue; /* a second holder of the parameter */
        }
        script_new(sp, "Param", 0, 20);
        script_set_integer(L, "id", (lua_Integer)param->id);
        script_set_string(L, "name", param->name);
        script_set_string(L, "role", nmo_script_param_role_name(param->role));
        script_set_integer(L, "index", (lua_Integer)param->index + 1);
        script_set_string(L, "type", param->type_name);
        script_set_guid(L, "type_guid", param->type_guid);
        script_set_boolean(L, "setting", param->is_setting);
        script_set_boolean(L, "shared", param->is_shared);
        script_set_boolean(L, "external", false);
        script_set_list(L, "in_edges");
        script_set_list(L, "out_edges");
        script_set_value(sp, param->id);
        script_register(sp, param->id, "params");
        lua_pop(L, 1);
    }
}

static void script_build_operations(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_operation_t *ops = nmo_script_model_operations(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        script_new(sp, "Operation", 0, 10);
        script_set_integer(L, "id", (lua_Integer)ops[i].id);
        script_set_string(L, "name", ops[i].operation_name);
        script_set_label(sp, ops[i].id);
        script_set_guid(L, "guid", ops[i].operation_guid);
        script_set_list(L, "params");
        script_register(sp, ops[i].id, "operations");
        lua_pop(L, 1);
    }
}

static void script_build_links(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_link_t *links = nmo_script_model_links(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_link_t *link = &links[i];
        script_new(sp, "Link", 0, 8);
        script_set_integer(L, "id", (lua_Integer)link->id);
        script_set_item(sp, "graph", link->graph_id);
        script_set_item(sp, "source", link->source_io_id);
        script_set_item(sp, "target", link->target_io_id);
        script_set_integer(L, "delay", (lua_Integer)link->activation_delay);
        script_set_integer(L, "initial_delay", (lua_Integer)link->initial_activation_delay);
        script_register(sp, link->id, "links");
        /* the target IO lists it as incoming */
        script_push_item(sp, link->target_io_id);
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, -2);
            script_append(L, -2, "incoming");
        }
        lua_pop(L, 2);
    }
}

static void script_wire_nodes(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_node_t *nodes = nmo_script_model_nodes(sp->model, &count);
    const nmo_object_id_t *children = nmo_script_model_children(sp->model, NULL);
    const nmo_script_io_t *ios = nmo_script_model_ios(sp->model, NULL);
    const nmo_script_param_t *params = nmo_script_model_params(sp->model, NULL);
    const nmo_script_operation_t *ops = nmo_script_model_operations(sp->model, NULL);
    const nmo_script_link_t *links = nmo_script_model_links(sp->model, NULL);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_node_t *node = &nodes[i];
        script_push_item(sp, node->id);
        script_set_item(sp, "parent", node->parent_id);
        const nmo_script_node_t *root = node;
        while (root->parent_id != 0 &&
               nmo_script_model_find_node(sp->model, root->parent_id) != NULL) {
            root = nmo_script_model_find_node(sp->model, root->parent_id);
        }
        script_set_item(sp, "root", root->id);
        script_set_object(sp, "owner", node->owner_object_id);
        for (size_t c = 0; c < node->child_count; c++) {
            script_append_item(sp, "children", children[node->first_child + c]);
        }
        for (size_t k = 0; k < node->input_count + node->output_count; k++) {
            const nmo_script_io_t *io = &ios[node->first_io + k];
            script_append_item(sp, io->is_output ? "outputs" : "inputs", io->id);
        }
        for (size_t p = 0; p < node->param_count; p++) {
            const nmo_script_param_t *param = &params[node->first_param + p];
            switch (param->role) {
            case NMO_SCRIPT_PARAM_TARGET:
                script_set_item(sp, "target", param->id);
                break;
            case NMO_SCRIPT_PARAM_INPUT:
                script_append_item(sp, "pins", param->id);
                break;
            case NMO_SCRIPT_PARAM_OUTPUT:
                script_append_item(sp, "pouts", param->id);
                break;
            case NMO_SCRIPT_PARAM_LOCAL:
                script_append_item(sp, param->is_setting ? "settings" : "locals", param->id);
                break;
            default:
                break;
            }
        }
        for (size_t o = 0; o < node->operation_count; o++) {
            script_append_item(sp, "operations", ops[node->first_operation + o].id);
        }
        for (size_t l = 0; l < node->link_count; l++) {
            script_append_item(sp, "links", links[node->first_link + l].id);
        }
        lua_pop(L, 1);
    }
}

static void script_wire_ios(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_io_t *ios = nmo_script_model_ios(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        script_push_item(sp, ios[i].id);
        script_set_item(sp, "node", ios[i].node_id);
        size_t link_count = 0;
        const nmo_script_link_t *const *links =
            nmo_script_model_links_from_io(sp->model, ios[i].id, &link_count);
        for (size_t l = 0; l < link_count; l++) {
            script_append_item(sp, "links", links[l]->id);
        }
        lua_pop(L, 1);
    }
}

static void script_wire_params(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_param_t *params = nmo_script_model_params(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_param_t *param = &params[i];
        if (nmo_script_model_find_param(sp->model, param->id) != param) {
            continue;
        }
        script_push_item(sp, param->id);
        script_set_item(sp, "owner", param->owner_id);
        script_push_param(sp, param->source_id);
        lua_setfield(L, -2, "source");
        script_set_origin(sp, param->id);
        lua_pop(L, 1);
    }
}

static void script_wire_operations(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_operation_t *ops = nmo_script_model_operations(sp->model, &count);
    const nmo_script_param_t *params = nmo_script_model_params(sp->model, NULL);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_operation_t *op = &ops[i];
        script_push_item(sp, op->id);
        script_set_item(sp, "graph", op->graph_id);
        script_push_param(sp, op->input1_id);
        lua_setfield(L, -2, "in1");
        script_push_param(sp, op->input2_id);
        lua_setfield(L, -2, "in2");
        script_push_param(sp, op->output_id);
        lua_setfield(L, -2, "out");
        for (size_t p = 0; p < op->param_count; p++) {
            script_append_item(sp, "params", params[op->first_param + p].id);
        }
        lua_pop(L, 1);
    }
}

static void script_build_edges(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_data_edge_t *edges = nmo_script_model_data_edges(sp->model, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_data_edge_t *edge = &edges[i];
        script_new(sp, "Edge", 0, 10);
        script_set_item(sp, "graph", edge->graph_id);
        script_set_string(L, "kind", edge->kind == NMO_SCRIPT_DATA_WRITE ? "write" : "read");
        script_push_param(sp, edge->source_id);
        lua_setfield(L, -2, "source");
        script_push_param(sp, edge->target_id);
        lua_setfield(L, -2, "target");
        script_set_item(sp, "source_owner", edge->source_owner_id);
        script_set_item(sp, "target_owner", edge->target_owner_id);
        script_set_string(L, "source_reach", nmo_script_reach_name(edge->source_reach));
        script_set_string(L, "target_reach", nmo_script_reach_name(edge->target_reach));
        script_set_string(L, "type", nmo_field_type_name(sp->registry, edge->type_guid));
        script_set_boolean(L, "shared", edge->is_shared);
        script_register(sp, 0, "edges");
        script_push_item(sp, edge->graph_id);
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, -2);
            script_append(L, -2, "edges");
        }
        lua_pop(L, 1);
        script_push_param(sp, edge->source_id);
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, -2);
            script_append(L, -2, "out_edges");
        }
        lua_pop(L, 1);
        script_push_param(sp, edge->target_id);
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, -2);
            script_append(L, -2, "in_edges");
        }
        lua_pop(L, 2);
    }
}

static void script_build_uses(const script_push_t *sp)
{
    lua_State *L = sp->L;
    size_t count = 0;
    const nmo_script_use_t *uses = nmo_script_index_uses(sp->index, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_script_use_t *use = &uses[i];
        script_new(sp, "Use", 0, 16);
        lua_pushvalue(L, sp->model_table);
        lua_setfield(L, -2, "model");
        script_set_string(L, "kind", nmo_script_use_kind_name(use->kind));
        script_set_item(sp, "node", use->node_id);
        script_set_item(sp, "graph", use->graph_id);
        script_set_item(sp, "root", use->root_id);
        script_set_item(sp, "key_param", use->key_param_id);
        script_set_string(L, "key_value", script_value_kind_name(use->key_value));
        script_set_string(L, "message", use->message);
        script_set_object(sp, "object", use->object_id);
        script_set_string(L, "object_name", use->object_name);
        if (use->column >= 0) {
            script_set_integer(L, "column", (lua_Integer)use->column);
        }
        script_set_string(L, "column_name", use->column_name);
        if (use->route != NMO_SCRIPT_ROUTE_NONE) {
            script_set_string(L, "route", nmo_script_route_name(use->route));
            /* the object it sends to, or receives the messages of */
            script_set_object(sp, use->kind == NMO_SCRIPT_USE_MESSAGE_SEND ? "dest" : "listener",
                              use->route_object_id);
            script_set_string(L, "route_name", use->route_object_name);
        }
        if (use->route_class_id != 0) {
            script_set_string(L, "broadcast_class",
                              nmo_type_query_class_name_from_id(sp->registry,
                                                                use->route_class_id));
        }
        script_register(sp, 0, "uses");
        script_push_item(sp, use->node_id);
        if (!lua_isnil(L, -1)) {
            lua_pushvalue(L, -2);
            script_append(L, -2, "uses");
        }
        lua_pop(L, 2);
    }
}

/* lua_CFunction: build the model table of the script_push_t light userdata at 1. */
static int script_build(lua_State *L)
{
    script_push_t *sp = (script_push_t *)lua_touserdata(L, 1);
    sp->L = L;
    lua_settop(L, 0);
    script_push_classes(L);
    sp->classes = lua_gettop(L);
    lua_newtable(L);
    sp->by_id = lua_gettop(L);
    lua_newtable(L);
    sp->objects = lua_gettop(L);

    script_new(sp, "Model", 0, 16);
    sp->model_table = lua_gettop(L);
    static const char *const lists[] = {"nodes", "roots", "ios", "params", "operations",
                                        "links", "edges", "uses"};
    for (size_t l = 0; l < sizeof(lists) / sizeof(lists[0]); l++) {
        script_set_list(L, lists[l]);
    }
    lua_pushvalue(L, sp->by_id);
    lua_setfield(L, sp->model_table, "by_id");
    lua_pushvalue(L, sp->objects);
    lua_setfield(L, sp->model_table, "objects");

    script_build_nodes(sp);
    script_build_ios(sp);
    script_build_params(sp);
    script_build_operations(sp);
    script_build_links(sp);
    script_wire_nodes(sp);
    script_wire_ios(sp);
    script_wire_params(sp);
    script_wire_operations(sp);
    script_build_edges(sp);
    script_build_uses(sp);

    size_t root_count = 0;
    const nmo_object_id_t *roots = nmo_script_model_roots(sp->model, &root_count);
    for (size_t i = 0; i < root_count; i++) {
        script_push_item(sp, roots[i]);
        script_append(L, sp->model_table, "roots");
    }
    lua_pushvalue(L, sp->model_table);
    return 1;
}

nmo_status_t nmo_lua_push_script_model(lua_State *state, nmo_workspace_t *workspace)
{
    if (state == NULL || workspace == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua state and workspace must be non-null");
    }
    script_push_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.repo = nmo_workspace_internal_repository(workspace);
    sp.registry = nmo_workspace_internal_type_registry(workspace);
    nmo_script_model_t *model = NULL;
    nmo_script_index_t *index = NULL;
    nmo_status_t status = nmo_script_model_build(workspace, &model);
    if (status == NMO_OK) {
        status = nmo_script_index_build(workspace, model, &index);
    }
    if (status != NMO_OK) {
        nmo_script_model_destroy(model);
        NMO_RETURN_ERROR(status, NMO_SEVERITY_ERROR, "Failed to build the script model");
    }
    sp.model = model;
    sp.index = index;

    lua_pushcfunction(state, script_build);
    lua_pushlightuserdata(state, &sp);
    int rc = lua_pcall(state, 1, 1, 0);
    nmo_script_index_destroy(index);
    nmo_script_model_destroy(model);
    if (rc != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        NMO_SET_LAST_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                           "Failed to build the script model tables: %s",
                           message != NULL ? message : "unknown error");
        lua_pop(state, 1);
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

/* ============================================================================
 * Module
 * ============================================================================ */

static int nmo_lua_script_model(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status = nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }
    status = nmo_lua_push_script_model(state, workspace);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build the script model");
    }
    return 1;
}

static int nmo_lua_open_script_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "model", nmo_lua_script_model },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)function_count + 9);
    nmo_lua_set_functions(state, functions, function_count);

    /* the classes, for scripts adding methods */
    script_push_classes(state);
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        lua_pushvalue(state, -2);
        lua_insert(state, -2);
        lua_settable(state, -5);
    }
    lua_pop(state, 1);
    return 1;
}

nmo_status_t nmo_lua_register_script_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.script",
        .open_fn = nmo_lua_open_script_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
