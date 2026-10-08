/**
 * @file script_index.c
 * @brief What a document's building blocks do with messages, data arrays, and scripts
 */

#include "behavior/nmo_script_index.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "../runtime/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Building block semantics
 * ============================================================================ */

/* Where a block's key comes from. */
typedef enum bb_key_source {
    BB_KEY_INPUT = 0,      /* the input named `key_input` */
    BB_KEY_MESSAGE_INPUTS, /* every input of type Message */
    BB_KEY_TARGET,         /* the target parameter (array blocks) */
} bb_key_source_t;

typedef struct bb_semantics {
    nmo_guid_t guid;
    nmo_script_use_kind_t kind;
    bb_key_source_t key_source;
    const char *key_input;    /* BB_KEY_INPUT */
    const char *column_input; /* array blocks on a cell or column */
    const char *dest_input;   /* message senders */
} bb_semantics_t;

#define BB(d1, d2) NMO_GUID_INIT(0x##d1##u, 0x##d2##u)

static const bb_semantics_t bb_semantics[] = {
    /* Logics/Message */
    {BB(A20E8D5B, DF002150), NMO_SCRIPT_USE_MESSAGE_SEND, BB_KEY_INPUT, "Message", NULL, "Dest"},
    {BB(3D6C4AE1, 72AE2CD6), NMO_SCRIPT_USE_MESSAGE_SEND, BB_KEY_INPUT, "Message", NULL, NULL},
    {BB(5F906952, 6DF11649), NMO_SCRIPT_USE_MESSAGE_SEND, BB_KEY_INPUT, "Message", NULL, "Group"},
    {BB(4587FFEE, 4587FFDD), NMO_SCRIPT_USE_MESSAGE_WAIT, BB_KEY_INPUT, "Message", NULL, NULL},
    {BB(1BB23F1D, 17FF14B9), NMO_SCRIPT_USE_MESSAGE_WAIT, BB_KEY_MESSAGE_INPUTS, NULL, NULL, NULL},
    /* Logics/Array: the target parameter is the array */
    {BB(33B99F51, 07D95C45), NMO_SCRIPT_USE_ARRAY_READ, BB_KEY_TARGET, NULL, "Column Index", NULL},
    {BB(33B77F41, 07B95C45), NMO_SCRIPT_USE_ARRAY_READ, BB_KEY_TARGET, NULL, NULL, NULL},
    {BB(49064205, 10E72F7A), NMO_SCRIPT_USE_ARRAY_READ, BB_KEY_TARGET, NULL, NULL, NULL},
    {BB(198F0AF9, 0268249F), NMO_SCRIPT_USE_ARRAY_READ, BB_KEY_TARGET, NULL, NULL, NULL},
    {BB(6BEC4BE6, 12D64C7C), NMO_SCRIPT_USE_ARRAY_READ, BB_KEY_TARGET, NULL, "Column", NULL},
    {BB(30ED1C6D, 4A3B7067), NMO_SCRIPT_USE_ARRAY_WRITE, BB_KEY_TARGET, NULL, "Column Index", NULL},
    {BB(62E87901, 2DF007DD), NMO_SCRIPT_USE_ARRAY_WRITE, BB_KEY_TARGET, NULL, NULL, NULL},
    {BB(1C7E5DC6, 3F6423C2), NMO_SCRIPT_USE_ARRAY_WRITE, BB_KEY_TARGET, NULL, NULL, NULL},
    {BB(1FA57136, 14310857), NMO_SCRIPT_USE_ARRAY_WRITE, BB_KEY_TARGET, NULL, NULL, NULL},
    {BB(35C9352F, 7B1A193B), NMO_SCRIPT_USE_ARRAY_WRITE, BB_KEY_TARGET, NULL, NULL, NULL},
    /* Narratives/Script Management */
    {BB(4C7E7BC3, 0B693155), NMO_SCRIPT_USE_SCRIPT_ACTIVATE, BB_KEY_INPUT, "Script", NULL, NULL},
    {BB(706C5A40, 5BB31A0B), NMO_SCRIPT_USE_SCRIPT_ACTIVATE, BB_KEY_INPUT, "Script", NULL, NULL},
    {BB(14367C05, 635B24F9), NMO_SCRIPT_USE_SCRIPT_DEACTIVATE, BB_KEY_INPUT, "Script", NULL, NULL},
};

#undef BB

/* Operations that find an object by its name: p1 is the name */
static const nmo_guid_t lookup_operations[] = {
    NMO_GUID_INIT(0x599203F6u, 0x23B06096u), /* Get Object By Name */
    NMO_GUID_INIT(0x428009B4u, 0x1CAA5C78u), /* Convert */
};

static const bb_semantics_t *bb_semantics_find(nmo_guid_t guid)
{
    for (size_t i = 0; i < sizeof(bb_semantics) / sizeof(bb_semantics[0]); i++) {
        if (nmo_guid_equals(bb_semantics[i].guid, guid)) {
            return &bb_semantics[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Index
 * ============================================================================ */

struct nmo_script_index {
    nmo_script_use_t *uses;
    size_t count;
    size_t capacity;
    char **strings; /* the message names the uses point at */
    size_t string_count;
    size_t string_capacity;
};

typedef struct index_builder {
    nmo_script_index_t *index;
    nmo_workspace_t *workspace;
    nmo_object_repository_t *repo;
    const nmo_script_model_t *model;
    const nmo_script_param_t *params;
} index_builder_t;

static const char *index_intern(nmo_script_index_t *index, const char *text)
{
    for (size_t i = 0; i < index->string_count; i++) {
        if (strcmp(index->strings[i], text) == 0) {
            return index->strings[i];
        }
    }
    if (index->string_count == index->string_capacity) {
        size_t capacity = index->string_capacity ? index->string_capacity * 2u : 64u;
        char **strings = (char **)realloc(index->strings, capacity * sizeof(*strings));
        if (strings == NULL) {
            return NULL;
        }
        index->strings = strings;
        index->string_capacity = capacity;
    }
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1u);
    index->strings[index->string_count++] = copy;
    return copy;
}

static nmo_script_use_t *index_push(nmo_script_index_t *index)
{
    if (index->count == index->capacity) {
        size_t capacity = index->capacity ? index->capacity * 2u : 128u;
        nmo_script_use_t *uses = (nmo_script_use_t *)realloc(index->uses,
                                                             capacity * sizeof(*uses));
        if (uses == NULL) {
            return NULL;
        }
        index->uses = uses;
        index->capacity = capacity;
    }
    nmo_script_use_t *use = &index->uses[index->count++];
    memset(use, 0, sizeof(*use));
    use->column = -1;
    return use;
}

/* The saved state of the parameter `param_id` takes its value from, or NULL. */
static const nmo_parameter_state_t *index_saved_value(const index_builder_t *b,
                                                      nmo_object_id_t param_id,
                                                      nmo_script_value_kind_t *out_kind)
{
    nmo_object_id_t holder = 0;
    nmo_script_value_kind_t kind = nmo_script_model_value_source(b->model, param_id, &holder);
    if (out_kind != NULL) {
        *out_kind = kind;
    }
    if (kind != NMO_SCRIPT_VALUE_SAVED) {
        return NULL;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(b->repo, holder);
    const nmo_parameter_state_t *state =
        object != NULL ? (const nmo_parameter_state_t *)nmo_object_get_state(object) : NULL;
    return (state != NULL && state->has_state) ? state : NULL;
}

static nmo_object_id_t index_saved_object(const index_builder_t *b, nmo_object_id_t param_id)
{
    const nmo_parameter_state_t *state = index_saved_value(b, param_id, NULL);
    return (state != NULL && state->mode == CKPARAM_MODE_OBJECT)
        ? nmo_parameter_object_id(state) : 0;
}

static bool index_saved_int(const index_builder_t *b, nmo_object_id_t param_id, int32_t *out)
{
    const nmo_parameter_state_t *state = index_saved_value(b, param_id, NULL);
    if (state == NULL || state->mode != CKPARAM_MODE_BUFFER ||
        state->buffer_data.data == NULL || state->buffer_data.count < sizeof(int32_t)) {
        return false;
    }
    memcpy(out, state->buffer_data.data, sizeof(*out));
    return true;
}

/* The message name a Message input's saved value names, interned; NULL if none. */
static const char *index_saved_message(const index_builder_t *b,
                                       nmo_object_id_t param_id,
                                       nmo_script_value_kind_t *out_kind,
                                       bool *out_oom)
{
    const nmo_parameter_state_t *state = index_saved_value(b, param_id, out_kind);
    char name[256];
    if (state == NULL || state->mode != CKPARAM_MODE_MANAGER ||
        !nmo_guid_equals(state->manager_guid, NMO_MANAGER_GUID_MESSAGE) ||
        nmo_workspace_internal_message_name(b->workspace, state->manager_value, name,
                                            sizeof(name)) != NMO_OK) {
        return NULL;
    }
    const char *interned = index_intern(b->index, name);
    *out_oom = interned == NULL;
    return interned;
}

/* The saved string a parameter takes its value from, interned; NULL if none. */
static const char *index_saved_string(const index_builder_t *b, nmo_object_id_t param_id,
                                      bool *out_oom)
{
    const nmo_parameter_state_t *state = index_saved_value(b, param_id, NULL);
    if (state == NULL || state->mode != CKPARAM_MODE_BUFFER ||
        !nmo_guid_equals(state->type_guid, CKPGUID_STRING) ||
        state->buffer_data.data == NULL || state->buffer_data.count == 0) {
        return NULL;
    }
    char text[256];
    size_t length = 0;
    const char *data = (const char *)state->buffer_data.data;
    while (length < state->buffer_data.count && length + 1u < sizeof(text) &&
           data[length] != '\0') {
        text[length] = data[length];
        length++;
    }
    text[length] = '\0';
    if (length == 0) {
        return NULL;
    }
    const char *interned = index_intern(b->index, text);
    *out_oom = interned == NULL;
    return interned;
}

/* The name output `output_id` looks an object up by, when a lookup operation makes it. */
static const char *index_lookup_output_name(const index_builder_t *b, nmo_object_id_t output_id,
                                            bool *out_oom)
{
    const nmo_script_param_t *output = nmo_script_model_find_param(b->model, output_id);
    if (output == NULL) {
        return NULL;
    }
    nmo_guid_t op_guid = NMO_GUID(0u, 0u);
    nmo_object_id_t name_input = 0;
    const nmo_script_operation_t *op = nmo_script_model_find_operation(b->model, output->owner_id);
    const nmo_script_node_t *node = nmo_script_model_find_node(b->model, output->owner_id);
    if (op != NULL) {
        op_guid = op->operation_guid;
        name_input = op->input1_id;
    } else if (node != NULL && node->operation_name != NULL) {
        op_guid = node->operation_guid;
        for (size_t i = 0; i < node->param_count && name_input == 0; i++) {
            const nmo_script_param_t *p = &b->params[node->first_param + i];
            if (p->role == NMO_SCRIPT_PARAM_INPUT && p->index == 0) {
                name_input = p->id;
            }
        }
    }
    for (size_t i = 0; name_input != 0 && i < sizeof(lookup_operations) / sizeof(lookup_operations[0]);
         i++) {
        if (nmo_guid_equals(op_guid, lookup_operations[i])) {
            return index_saved_string(b, name_input, out_oom);
        }
    }
    return NULL;
}

/*
 * The name the key input `param_id` looks its object up by: a lookup output
 * it reads, or one that writes the local it reads.
 */
static const char *index_lookup_name(const index_builder_t *b, nmo_object_id_t param_id,
                                     bool *out_oom)
{
    nmo_object_id_t holder = 0;
    nmo_script_value_kind_t kind = nmo_script_model_value_source(b->model, param_id, &holder);
    if (kind == NMO_SCRIPT_VALUE_COMPUTED) {
        return index_lookup_output_name(b, holder, out_oom);
    }
    if (kind != NMO_SCRIPT_VALUE_WRITTEN) {
        return NULL;
    }
    size_t writer_count = 0;
    const nmo_script_data_edge_t *const *writers =
        nmo_script_model_param_writers(b->model, holder, &writer_count);
    for (size_t i = 0; i < writer_count && !*out_oom; i++) {
        const char *name = index_lookup_output_name(b, writers[i]->source_id, out_oom);
        if (name != NULL) {
            return name;
        }
    }
    return NULL;
}

/* The object of class `class_id` named `name` in the document, or 0. */
static nmo_object_id_t index_object_named(const index_builder_t *b, const char *name,
                                          nmo_class_id_t class_id)
{
    size_t count = nmo_object_repository_get_count(b->repo);
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(b->repo, i);
        const char *object_name = object != NULL ? nmo_object_get_name(object) : NULL;
        if (object_name != NULL && nmo_object_get_class_id(object) == class_id &&
            strcmp(object_name, name) == 0) {
            return nmo_object_get_id(object);
        }
    }
    return 0;
}

static const char *index_column_name(const index_builder_t *b,
                                     nmo_object_id_t array_id,
                                     int32_t column)
{
    nmo_object_t *object = array_id != 0 ? nmo_object_repository_find_by_id(b->repo, array_id)
                                         : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_DATAARRAY || column < 0) {
        return NULL;
    }
    const nmo_dataarray_state_t *array =
        (const nmo_dataarray_state_t *)nmo_object_get_state(object);
    if (array == NULL || array->column_formats == NULL ||
        (uint32_t)column >= array->column_count) {
        return NULL;
    }
    return array->column_formats[column].name;
}

/* The input of `node` named `name`, or NULL. */
static const nmo_script_param_t *index_input(const index_builder_t *b,
                                             const nmo_script_node_t *node,
                                             const char *name,
                                             nmo_script_param_role_t role)
{
    for (size_t i = 0; name != NULL && i < node->param_count; i++) {
        const nmo_script_param_t *param = &b->params[node->first_param + i];
        if (param->role == role && strcmp(param->name, name) == 0) {
            return param;
        }
    }
    return NULL;
}

static const nmo_script_param_t *index_target(const index_builder_t *b,
                                              const nmo_script_node_t *node)
{
    for (size_t i = 0; i < node->param_count; i++) {
        const nmo_script_param_t *param = &b->params[node->first_param + i];
        if (param->role == NMO_SCRIPT_PARAM_TARGET) {
            return param;
        }
    }
    return NULL;
}

static nmo_object_id_t index_root(const nmo_script_model_t *model, const nmo_script_node_t *node)
{
    const nmo_script_node_t *cur = node;
    for (int hops = 0; cur != NULL && cur->parent_id != 0 && hops < 512; hops++) {
        cur = nmo_script_model_find_node(model, cur->parent_id);
    }
    return cur != NULL ? cur->id : node->id;
}

/* One use of `node` with key input `key` (NULL when it has none). */
static bool index_add_use(const index_builder_t *b,
                          const nmo_script_node_t *node,
                          nmo_script_use_kind_t kind,
                          const nmo_script_param_t *key,
                          const bb_semantics_t *sem)
{
    nmo_script_use_t *use = index_push(b->index);
    if (use == NULL) {
        return false;
    }
    use->kind = kind;
    use->node_id = node->id;
    use->graph_id = node->parent_id;
    use->root_id = index_root(b->model, node);
    use->key_param_id = key != NULL ? key->id : 0;

    bool oom = false;
    if (nmo_script_use_kind_is_message(kind)) {
        if (key != NULL) {
            use->message = index_saved_message(b, key->id, &use->key_value, &oom);
        }
        const nmo_script_param_t *dest =
            sem != NULL ? index_input(b, node, sem->dest_input, NMO_SCRIPT_PARAM_INPUT) : NULL;
        use->dest_object_id = dest != NULL ? index_saved_object(b, dest->id) : 0;
    } else {
        if (key != NULL) {
            index_saved_value(b, key->id, &use->key_value);
            use->object_id = index_saved_object(b, key->id);
            if (use->object_id == 0) {
                use->object_name = index_lookup_name(b, key->id, &oom);
                if (use->object_name != NULL) {
                    use->object_id = index_object_named(
                        b, use->object_name,
                        nmo_script_use_kind_is_array(kind) ? NMO_CID_DATAARRAY : NMO_CID_BEHAVIOR);
                }
            }
        }
        const nmo_script_param_t *column =
            sem != NULL ? index_input(b, node, sem->column_input, NMO_SCRIPT_PARAM_INPUT) : NULL;
        /* An array block without a target acts on the object its script belongs to */
        const nmo_script_node_t *root = nmo_script_model_find_node(b->model, use->root_id);
        nmo_object_t *owner = (use->object_id == 0 && root != NULL && root->owner_object_id != 0)
            ? nmo_object_repository_find_by_id(b->repo, root->owner_object_id) : NULL;
        if (nmo_script_use_kind_is_array(kind) && use->key_value == NMO_SCRIPT_VALUE_NONE &&
            owner != NULL && nmo_object_get_class_id(owner) == NMO_CID_DATAARRAY) {
            use->object_id = root->owner_object_id;
        }
        int32_t value = 0;
        if (column != NULL && index_saved_int(b, column->id, &value)) {
            use->column = value;
            use->column_name = index_column_name(b, use->object_id, value);
        }
    }
    return !oom;
}

static bool index_add_node(const index_builder_t *b, const nmo_script_node_t *node)
{
    if (node->kind != NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        return true;
    }
    const bb_semantics_t *sem = bb_semantics_find(node->prototype_guid);
    if (sem != NULL && sem->key_source == BB_KEY_INPUT) {
        return index_add_use(b, node, sem->kind,
                             index_input(b, node, sem->key_input, NMO_SCRIPT_PARAM_INPUT), sem);
    }
    if (sem != NULL && sem->key_source == BB_KEY_TARGET) {
        return index_add_use(b, node, sem->kind, index_target(b, node), sem);
    }
    /* Every Message input: of a message waiter, or of any other block */
    nmo_script_use_kind_t kind = sem != NULL ? sem->kind : NMO_SCRIPT_USE_MESSAGE_OTHER;
    for (size_t i = 0; i < node->param_count; i++) {
        const nmo_script_param_t *param = &b->params[node->first_param + i];
        if (param->role == NMO_SCRIPT_PARAM_INPUT &&
            nmo_guid_equals(param->type_guid, CKPGUID_MESSAGE) &&
            !index_add_use(b, node, kind, param, sem)) {
            return false;
        }
    }
    return true;
}

nmo_status_t nmo_script_index_build(nmo_workspace_t *workspace,
                                    const nmo_script_model_t *model,
                                    nmo_script_index_t **out_index)
{
    if (workspace == NULL || model == NULL || out_index == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_index = NULL;
    nmo_script_index_t *index = (nmo_script_index_t *)calloc(1, sizeof(*index));
    if (index == NULL) {
        return NMO_ERR_NOMEM;
    }
    index_builder_t b = {
        .index = index,
        .workspace = workspace,
        .repo = nmo_workspace_internal_repository(workspace),
        .model = model,
        .params = nmo_script_model_params(model, NULL),
    };
    size_t node_count = 0;
    const nmo_script_node_t *nodes = nmo_script_model_nodes(model, &node_count);
    for (size_t i = 0; i < node_count; i++) {
        if (!index_add_node(&b, &nodes[i])) {
            nmo_script_index_destroy(index);
            return NMO_ERR_NOMEM;
        }
    }
    *out_index = index;
    return NMO_OK;
}

void nmo_script_index_destroy(nmo_script_index_t *index)
{
    if (index == NULL) {
        return;
    }
    for (size_t i = 0; i < index->string_count; i++) {
        free(index->strings[i]);
    }
    free(index->strings);
    free(index->uses);
    free(index);
}

const nmo_script_use_t *nmo_script_index_uses(const nmo_script_index_t *index,
                                              size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = index != NULL ? index->count : 0u;
    }
    return index != NULL ? index->uses : NULL;
}

const char *nmo_script_use_kind_name(nmo_script_use_kind_t kind)
{
    switch (kind) {
    case NMO_SCRIPT_USE_MESSAGE_SEND:      return "send";
    case NMO_SCRIPT_USE_MESSAGE_WAIT:      return "wait";
    case NMO_SCRIPT_USE_MESSAGE_OTHER:     return "message";
    case NMO_SCRIPT_USE_ARRAY_READ:        return "read";
    case NMO_SCRIPT_USE_ARRAY_WRITE:       return "write";
    case NMO_SCRIPT_USE_SCRIPT_ACTIVATE:   return "activate";
    case NMO_SCRIPT_USE_SCRIPT_DEACTIVATE: return "deactivate";
    default:                               return "?";
    }
}

bool nmo_script_use_kind_is_message(nmo_script_use_kind_t kind)
{
    return kind == NMO_SCRIPT_USE_MESSAGE_SEND || kind == NMO_SCRIPT_USE_MESSAGE_WAIT ||
           kind == NMO_SCRIPT_USE_MESSAGE_OTHER;
}

bool nmo_script_use_kind_is_array(nmo_script_use_kind_t kind)
{
    return kind == NMO_SCRIPT_USE_ARRAY_READ || kind == NMO_SCRIPT_USE_ARRAY_WRITE;
}

bool nmo_script_use_kind_is_script(nmo_script_use_kind_t kind)
{
    return kind == NMO_SCRIPT_USE_SCRIPT_ACTIVATE || kind == NMO_SCRIPT_USE_SCRIPT_DEACTIVATE;
}
