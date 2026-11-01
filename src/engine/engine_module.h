/**
 * @file engine_module.h
 * @brief The documents the engine runs and the IDs their objects take, and
 *        building their behaviors.
 */

#ifndef NMO_ENGINE_MODULE_H
#define NMO_ENGINE_MODULE_H

#include "engine_base.h"

#include "behavior/nmo_script_model.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_document.h"
#include "session/nmo_session.h"

/*
 * A document the engine runs, or part of one: the objects of its session
 * whose IDs are in [first_local, last_local]. An engine ID is the document
 * ID plus `offset` (wrapping, so a window far into a session can start right
 * after the engine's last ID); `limit` is the engine ID of last_local. The
 * first module is the document the engine was made for, then each file
 * loaded since; the copies Object Copy makes in a loaded file's session are
 * a module of their own that shares that session. Its message and attribute
 * types map to the engine's.
 */
typedef struct eng_module {
    const struct nmo_engine *engine;
    nmo_workspace_t *workspace;
    nmo_object_repository_t *repo;
    nmo_script_model_t *model;
    nmo_object_id_t offset;
    nmo_object_id_t limit;
    nmo_object_id_t first_local, last_local;
    int32_t *message_types;
    uint32_t message_type_count;
    int32_t *attribute_types;
    uint32_t attribute_type_count;
    char *name;              /* a loaded file: the name it was loaded by */
    nmo_session_t *session;  /* what the engine made (and frees): a loaded file's, the created objects' */
    nmo_document_t *document;
    bool created;            /* the objects blocks create */
    /* Cache of the module's engine IDs in repository order, made for the window and
       repository size it was made with (the engine never removes repository objects) */
    nmo_object_id_t *loaded;
    int32_t loaded_count;
    size_t loaded_repo_count;
    nmo_object_id_t loaded_first, loaded_last;
} eng_module_t;

/* Part of a module to build or load: its objects with IDs in [first, last] */
typedef struct eng_range {
    int32_t module;
    nmo_object_id_t first, last;
} eng_range_t;

/* core/module.c */
/*
 * Add a document: its ID range, script model, and message and attribute
 * types. `session` and `document` are the engine's to free with it (a loaded
 * file), or NULL.
 */
nmo_status_t eng_module_open(nmo_engine_t *engine, nmo_workspace_t *workspace,
                             nmo_session_t *session, nmo_document_t *document,
                             int32_t *out_module);
void eng_free_modules(nmo_engine_t *engine);
/* The document of engine ID `id`, and its ID there; NULL when none has it. */
const eng_module_t *eng_module_of(const nmo_engine_t *engine, nmo_object_id_t id,
                                  nmo_object_id_t *out_local);
/* The objects of class `class_id` (exactly), not deleted, by engine ID (their creation order) */
size_t eng_objects_of_class(nmo_engine_t *engine, uint32_t class_id, nmo_object_id_t **out_ids);
nmo_object_t *eng_find_object(const nmo_engine_t *engine, nmo_object_id_t id);
/* The first object named `name` (of class `class_id` or one derived, unless 0), in document order */
nmo_object_id_t eng_object_named(const nmo_engine_t *engine, const char *name, uint32_t class_id);
nmo_object_id_t eng_create_object(nmo_engine_t *engine, uint32_t class_id, const char *name);
/* The engine ID of an object of a module's session outside its window */
nmo_object_id_t eng_module_id_outside(const eng_module_t *module, nmo_object_id_t local);
/* The engine ID of a document's object ID; 0 stays 0. */
static inline nmo_object_id_t eng_module_id(const eng_module_t *module, nmo_object_id_t local)
{
    if (local == 0 || module == NULL) {
        return local;
    }
    if (local >= module->first_local && local <= module->last_local) {
        return local + module->offset;
    }
    return eng_module_id_outside(module, local);
}
static inline bool eng_module_has(const eng_module_t *module, nmo_object_id_t local)
{
    return local >= module->first_local && local <= module->last_local;
}
nmo_object_id_t eng_next_engine_id(const nmo_engine_t *engine);
/* The whole window of a module */
eng_range_t eng_module_range(const nmo_engine_t *engine, int32_t module);
nmo_status_t eng_load_range(nmo_engine_t *engine, int32_t module, nmo_object_id_t first,
                            nmo_object_id_t last);
/* A document's message or attribute type as the engine's */
int32_t eng_module_message_type(const eng_module_t *module, int32_t type);
int32_t eng_module_attribute_type(const eng_module_t *module, int32_t type);

/* core/build.c */
/* Build the behaviors, parameters and script owners of document `module`. */
nmo_status_t eng_build_module(nmo_engine_t *engine, const eng_range_t *range);
typedef struct nmo_parameter_state nmo_parameter_state_t;
/*
 * The bytes a CKParameter of document `module` holds in memory, from its
 * saved state, into a new heap block; zeroes of `fallback_size` when the
 * state holds no flat value. Object IDs and message and attribute types come
 * out as the engine's.
 */
bool eng_parameter_state_bytes(const eng_module_t *module, const nmo_parameter_state_t *state,
                               size_t fallback_size, uint8_t **out_data, uint32_t *out_size);

#endif /* NMO_ENGINE_MODULE_H */
