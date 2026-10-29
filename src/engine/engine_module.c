/**
 * @file engine_module.c
 * @brief The documents the engine runs: the one it was made for, and each
 *        file loaded into it since (CKContext::Load).
 *
 * Every document numbers its objects from 1, so the engine gives each a
 * range of its own: an object's engine ID is its document ID plus the
 * document's offset, and the first document keeps its IDs. Message and
 * attribute types are indexes into a document's own tables; the engine keeps
 * one table of each and maps every document's types into it by name, as
 * the message and attribute managers do when CK loads a file. Objects that
 * blocks create are in a document of their own, made with the first one.
 */

#include "engine_internal.h"

#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "../runtime/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *module_strdup(const char *text)
{
    size_t length = text != NULL ? strlen(text) : 0u;
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text != NULL ? text : "", length);
        copy[length] = '\0';
    }
    return copy;
}

/* The engine message type named `name`, made when the engine has none (AddMessageType). */
int32_t eng_message_type_add(nmo_engine_t *engine, const char *name)
{
    for (size_t i = 0; i < engine->message_names.count; i++) {
        if (strcmp(engine->message_names.data[i], name) == 0) {
            return (int32_t)i;
        }
    }
    char *copy = module_strdup(name);
    char **slot = NULL;
    if (copy == NULL || !ENG_VEC_PUSH(engine->message_names, slot)) {
        free(copy);
        return -1;
    }
    *slot = copy;
    return (int32_t)engine->message_names.count - 1;
}

/*
 * The document's message types. The first document's become the engine's
 * types in their order (duplicates too, so its indexes stay); a later one's
 * map to the engine's types of the same name, new names added.
 */
static nmo_status_t module_map_messages(nmo_engine_t *engine, eng_module_t *module, bool first)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096u);
    if (arena == NULL) {
        return NMO_ERR_NOMEM;
    }
    const char **names = NULL;
    uint32_t count = 0;
    nmo_status_t status = NMO_OK;
    /* A file without a message manager declares no message types */
    if (nmo_workspace_internal_message_names(module->workspace, arena, &names, &count) == NMO_OK &&
        count > 0) {
        module->message_types = (int32_t *)calloc(count, sizeof(int32_t));
        if (module->message_types == NULL) {
            status = NMO_ERR_NOMEM;
        }
        for (uint32_t i = 0; status == NMO_OK && i < count; i++) {
            const char *name = names[i] != NULL ? names[i] : "";
            int32_t type = -1;
            if (first) {
                char *copy = module_strdup(name);
                char **slot = NULL;
                if (copy != NULL && ENG_VEC_PUSH(engine->message_names, slot)) {
                    *slot = copy;
                    type = (int32_t)engine->message_names.count - 1;
                } else {
                    free(copy);
                }
            } else {
                type = eng_message_type_add(engine, name);
            }
            if (type < 0) {
                status = NMO_ERR_NOMEM;
                break;
            }
            module->message_types[i] = type;
        }
        module->message_type_count = status == NMO_OK ? count : 0u;
    }
    nmo_arena_destroy(arena);
    return status;
}

/* The document's attribute types, mapped like its message types. */
static nmo_status_t module_map_attributes(nmo_engine_t *engine, eng_module_t *module, bool first)
{
    nmo_attributemanager_state_t types;
    if (nmo_workspace_internal_attribute_types(module->workspace, &types) != NMO_OK ||
        types.attribute_count == 0) {
        return NMO_OK;
    }
    module->attribute_types = (int32_t *)calloc(types.attribute_count, sizeof(int32_t));
    if (module->attribute_types == NULL) {
        return NMO_ERR_NOMEM;
    }
    module->attribute_type_count = types.attribute_count;
    for (uint32_t i = 0; i < types.attribute_count; i++) {
        const nmo_attribute_descriptor_t *desc = &types.attributes[i];
        int32_t type = -1;
        for (size_t k = 0; !first && desc->present && desc->name != NULL &&
                           k < engine->attribute_types.count;
             k++) {
            const eng_attribute_type_t *known = &engine->attribute_types.data[k];
            if (known->present && known->name != NULL && strcmp(known->name, desc->name) == 0) {
                type = (int32_t)k;
                break;
            }
        }
        if (type < 0 && (first || desc->present)) {
            eng_attribute_type_t *slot = NULL;
            if (!ENG_VEC_PUSH(engine->attribute_types, slot)) {
                return NMO_ERR_NOMEM;
            }
            slot->present = desc->present;
            slot->name = desc->name != NULL ? module_strdup(desc->name) : NULL;
            slot->parameter_type = desc->parameter_type_guid;
            slot->compatible_class_id = desc->compatible_class_id;
            type = (int32_t)engine->attribute_types.count - 1;
        }
        module->attribute_types[i] = type;
    }
    return NMO_OK;
}

nmo_status_t eng_module_open(nmo_engine_t *engine, nmo_workspace_t *workspace,
                             nmo_session_t *session, nmo_document_t *document,
                             int32_t *out_module)
{
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    bool first = engine->modules.count == 0;
    nmo_object_id_t offset =
        first ? 0u : engine->modules.data[engine->modules.count - 1u].limit;
    nmo_object_id_t highest = 0;
    size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        nmo_object_id_t id = object != NULL ? nmo_object_get_id(object) : 0u;
        if (id > highest) {
            highest = id;
        }
    }

    eng_module_t *module = NULL;
    if (!ENG_VEC_PUSH(engine->modules, module)) {
        return NMO_ERR_NOMEM;
    }
    module->workspace = workspace;
    module->repo = repo;
    module->offset = offset;
    module->limit = offset + highest;
    module->session = session;
    module->document = document;
    int32_t index = (int32_t)engine->modules.count - 1;
    nmo_status_t status = nmo_script_model_build(workspace, &module->model);
    if (status == NMO_OK) {
        status = module_map_messages(engine, &engine->modules.data[index], first);
    }
    if (status == NMO_OK) {
        status = module_map_attributes(engine, &engine->modules.data[index], first);
    }
    if (out_module != NULL) {
        *out_module = index;
    }
    return status;
}

void eng_free_modules(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->modules.count; i++) {
        eng_module_t *module = &engine->modules.data[i];
        nmo_script_model_destroy(module->model);
        free(module->message_types);
        free(module->attribute_types);
        free(module->name);
        if (module->session != NULL) {
            if (module->workspace != NULL) {
                nmo_workspace_destroy(module->workspace);
                nmo_document_destroy(module->document);
            }
            nmo_session_destroy(module->session);
        }
    }
    free(engine->modules.data);
}

const eng_module_t *eng_module_of(const nmo_engine_t *engine, nmo_object_id_t id,
                                  nmo_object_id_t *out_local)
{
    for (size_t i = 0; engine != NULL && id != 0 && i < engine->modules.count; i++) {
        const eng_module_t *module = &engine->modules.data[i];
        if (id > module->offset && id <= module->limit) {
            if (out_local != NULL) {
                *out_local = id - module->offset;
            }
            return module;
        }
    }
    return NULL;
}

nmo_object_t *eng_find_object(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_id_t local = 0;
    const eng_module_t *module = eng_module_of(engine, id, &local);
    return module != NULL ? nmo_object_repository_find_by_id(module->repo, local) : NULL;
}

nmo_object_id_t eng_object_named(const nmo_engine_t *engine, const char *name, uint32_t class_id)
{
    for (size_t m = 0; engine != NULL && name != NULL && m < engine->modules.count; m++) {
        const eng_module_t *module = &engine->modules.data[m];
        size_t count = 0;
        nmo_object_t **objects = nmo_object_repository_get_all(module->repo, &count);
        for (size_t i = 0; objects != NULL && i < count; i++) {
            const char *object_name = nmo_object_get_name(objects[i]);
            if (object_name != NULL && strcmp(object_name, name) == 0 &&
                (class_id == 0 ||
                 nmo_type_registry_is_class_derived_from(
                     engine->types, (uint32_t)nmo_object_get_class_id(objects[i]), class_id))) {
                return eng_module_id(module, nmo_object_get_id(objects[i]));
            }
        }
    }
    return 0;
}

/* The IDs the module of created objects keeps, after the files loaded before its first object */
#define ENG_CREATED_CAPACITY 0x100000u

/*
 * CKContext::CreateObject and CKLevel::AddObject: a new object of the class
 * and name in the module of created objects; 0 when it cannot be made. A new
 * data array is empty.
 */
nmo_object_id_t eng_create_object(nmo_engine_t *engine, uint32_t class_id, const char *name)
{
    int32_t index = -1;
    for (size_t i = 0; i < engine->modules.count; i++) {
        if (engine->modules.data[i].created) {
            index = (int32_t)i;
        }
    }
    if (index < 0) {
        nmo_context_t *ctx = nmo_workspace_internal_context(engine->workspace);
        nmo_session_t *session = ctx != NULL ? nmo_session_create(ctx) : NULL;
        eng_module_t *module = NULL;
        if (session == NULL || !ENG_VEC_PUSH(engine->modules, module)) {
            nmo_session_destroy(session);
            return 0;
        }
        index = (int32_t)engine->modules.count - 1;
        module->offset = index > 0 ? engine->modules.data[index - 1].limit : 0u;
        module->limit = module->offset + ENG_CREATED_CAPACITY;
        module->repo = nmo_session_get_repository(session);
        module->session = session;
        module->created = true;
    }
    eng_module_t *module = &engine->modules.data[index];
    nmo_object_id_t local = 0;
    if (nmo_session_create_object(module->session, (nmo_class_id_t)class_id,
                                  name != NULL ? name : "", NMO_GUID(0u, 0u), &local,
                                  NULL) != NMO_OK ||
        local == 0 || local > ENG_CREATED_CAPACITY) {
        return 0;
    }
    nmo_object_id_t id = module->offset + local;
    if (class_id == NMO_CID_DATAARRAY && eng_add_array(engine, id) != NMO_OK) {
        return 0;
    }
    return id;
}

int32_t eng_module_message_type(const eng_module_t *module, int32_t type)
{
    return module != NULL && type >= 0 && (uint32_t)type < module->message_type_count
               ? module->message_types[type]
               : type;
}

int32_t eng_module_attribute_type(const eng_module_t *module, int32_t type)
{
    return module != NULL && type >= 0 && (uint32_t)type < module->attribute_type_count
               ? module->attribute_types[type]
               : type;
}

/* ============================================================================
 * Loading files
 * ============================================================================ */

void nmo_engine_set_file_resolver(nmo_engine_t *engine, nmo_engine_resolve_file_fn fn,
                                  void *user_data)
{
    if (engine != NULL) {
        engine->resolve_file = fn;
        engine->resolve_file_user_data = user_data;
    }
}

/* Read the file at `path` into a new document of the engine. */
static nmo_status_t module_load(nmo_engine_t *engine, const char *path, int32_t *out_module)
{
    nmo_context_t *ctx = nmo_workspace_internal_context(engine->workspace);
    nmo_session_t *session = ctx != NULL ? nmo_session_load(ctx, path) : NULL;
    if (session == NULL) {
        return NMO_ERR_CANT_READ_FILE;
    }
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (nmo_session_borrow_document(session, &document) != NMO_OK ||
        nmo_workspace_create(ctx, document, &workspace) != NMO_OK) {
        nmo_document_destroy(document);
        nmo_session_destroy(session);
        return NMO_ERR_INVALID_STATE;
    }
    /* From here the module holds the session, document and workspace */
    return eng_module_open(engine, workspace, session, document, out_module);
}

nmo_status_t nmo_engine_load(nmo_engine_t *engine, const char *name, bool add_to_level,
                             int32_t *out_file)
{
    if (out_file != NULL) {
        *out_file = -1;
    }
    if (engine == NULL || name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (engine->resolve_file == NULL) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    const char *path = engine->resolve_file(engine->resolve_file_user_data, name);
    if (path == NULL) {
        eng_emit(engine, NMO_ENGINE_EVENT_FILE_LOADED, -1, 0, -1);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    size_t first_node = engine->nodes.count;
    size_t first_object = engine->objects.count;
    int32_t module = -1;
    nmo_status_t status = module_load(engine, path, &module);
    if (status == NMO_OK) {
        engine->modules.data[module].name = module_strdup(name);
        status = eng_build_module(engine, module);
    }
    if (status == NMO_OK) {
        status = eng_load_arrays(engine, module);
    }
    if (status == NMO_OK) {
        status = eng_load_attributes(engine, module);
    }
    if (status != NMO_OK) {
        eng_emit(engine, NMO_ENGINE_EVENT_FILE_LOADED, -1, 0, -1);
        return status;
    }

    /* A loaded object is in no scene until it is added to one */
    for (size_t o = first_object; o < engine->objects.count; o++) {
        engine->objects.data[o].active = false;
    }
    eng_bind_blocks(engine, first_node);
    if (engine->started) {
        /* CKFile sends CKM_BEHAVIORLOAD to the behaviors it loaded */
        for (size_t i = first_node; i < engine->nodes.count; i++) {
            if (engine->nodes.data[i].is_block) {
                eng_block_callback(engine, (int32_t)i, NMO_ENGINE_MSG_LOAD);
            }
        }
    }
    if (add_to_level) {
        eng_add_module_to_level(engine, module);
    }
    eng_emit(engine, NMO_ENGINE_EVENT_FILE_LOADED, -1, 0, module);
    if (out_file != NULL) {
        *out_file = module;
    }
    return NMO_OK;
}

int32_t nmo_engine_loaded_object_count(const nmo_engine_t *engine, int32_t file)
{
    if (engine == NULL || file < 0 || (size_t)file >= engine->modules.count) {
        return 0;
    }
    return (int32_t)nmo_object_repository_get_count(engine->modules.data[file].repo);
}

nmo_object_id_t nmo_engine_loaded_object(const nmo_engine_t *engine, int32_t file, int32_t index)
{
    if (index < 0 || index >= nmo_engine_loaded_object_count(engine, file)) {
        return 0;
    }
    const eng_module_t *module = &engine->modules.data[file];
    nmo_object_t *object = nmo_object_repository_get_by_index(module->repo, (size_t)index);
    return object != NULL ? eng_module_id(module, nmo_object_get_id(object)) : 0;
}

const char *nmo_engine_object_name(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_find_object(engine, id);
    return object != NULL ? nmo_object_get_name(object) : NULL;
}

int32_t nmo_engine_object_class(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_find_object(engine, id);
    return object != NULL ? (int32_t)nmo_object_get_class_id(object) : 0;
}

const nmo_script_node_t *nmo_engine_script_node(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_id_t local = 0;
    const eng_module_t *module = eng_module_of(engine, id, &local);
    return module != NULL ? nmo_script_model_find_node(module->model, local) : NULL;
}

size_t nmo_engine_label(const nmo_engine_t *engine, nmo_object_id_t id, char *buffer,
                        size_t buffer_size)
{
    nmo_object_id_t local = 0;
    const eng_module_t *module = eng_module_of(engine, id, &local);
    if (module == NULL) {
        int length = snprintf(buffer, buffer_size, "(none)#%u", (unsigned)id);
        return length > 0 ? (size_t)length : 0u;
    }
    /* The document's label ends in its ID: give the engine's instead */
    size_t length = nmo_script_model_label(module->model, local, NULL, 0);
    char *label = (char *)malloc(length + 1u);
    if (label == NULL) {
        return 0u;
    }
    nmo_script_model_label(module->model, local, label, length + 1u);
    char *hash = strrchr(label, '#');
    if (hash != NULL) {
        *hash = '\0';
    }
    int written = snprintf(buffer, buffer_size, "%s#%u", label, (unsigned)id);
    free(label);
    return written > 0 ? (size_t)written : 0u;
}

int32_t nmo_engine_script_count(const nmo_engine_t *engine)
{
    int32_t count = 0;
    for (size_t i = 0; engine != NULL && i < engine->nodes.count; i++) {
        count += (engine->nodes.data[i].flags & CKBEHAVIOR_SCRIPT) != 0;
    }
    return count;
}

nmo_object_id_t nmo_engine_script_id(const nmo_engine_t *engine, int32_t index)
{
    for (size_t i = 0; engine != NULL && index >= 0 && i < engine->nodes.count; i++) {
        if ((engine->nodes.data[i].flags & CKBEHAVIOR_SCRIPT) != 0 && index-- == 0) {
            return engine->nodes.data[i].id;
        }
    }
    return 0;
}

const char *nmo_engine_loaded_file_name(const nmo_engine_t *engine, int32_t file)
{
    if (engine == NULL || file <= 0 || (size_t)file >= engine->modules.count) {
        return NULL;
    }
    return engine->modules.data[file].name;
}
