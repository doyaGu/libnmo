/**
 * @file engine_fixture.h
 * @brief Behavior graphs built with script edits, for the engine tests.
 *
 * A document with one 3D entity owning scripts, each with a single input,
 * and an open script edit to add building blocks to them. Committing builds
 * the engine. The helpers that return a value return 0 on failure; the
 * caller's next check fails.
 */

#ifndef NMO_TEST_ENGINE_FIXTURE_H
#define NMO_TEST_ENGINE_FIXTURE_H

#include "test_framework.h"

#include "nmo_engine.h"
#include "edit/nmo_script_edit.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_statesave_ids.h"
#include "runtime/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"

#include <string.h>

#define ENGINE_FIXTURE_MAX_VALUES 32

typedef struct engine_fixture_value {
    nmo_object_id_t param;
    uint8_t bytes[16];
    size_t size;
} engine_fixture_value_t;

typedef struct engine_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_object_repository_t *repo;
    nmo_object_id_t owner_id;
    nmo_object_id_t script_id; /* the first script */
    nmo_object_id_t script_in;
    nmo_script_edit_tx_t *tx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_engine_t *engine;
    /* Values the engine gets once it exists (any parameter mode) */
    engine_fixture_value_t values[ENGINE_FIXTURE_MAX_VALUES];
    size_t value_count;
} engine_fixture_t;

static inline nmo_object_id_t fixture_create(engine_fixture_t *f, nmo_class_id_t class_id,
                                      const char *name)
{
    nmo_object_id_t id = 0;
    if (nmo_session_create_object(f->session, class_id, name, (nmo_guid_t){0, 0}, &id, NULL) !=
        NMO_OK) {
        return 0;
    }
    return id;
}

static inline void *fixture_state(engine_fixture_t *f, nmo_object_id_t id)
{
    nmo_object_t *object = nmo_object_repository_find_by_id(f->repo, id);
    return object != NULL ? nmo_object_get_state(object) : NULL;
}

/* A script of the owner with one input; 0 on failure. Before fixture_begin_edit(). */
static inline nmo_object_id_t fixture_add_script(engine_fixture_t *f, const char *name,
                                          nmo_object_id_t *out_input)
{
    nmo_object_id_t script_id = fixture_create(f, NMO_CID_BEHAVIOR, name);
    nmo_object_id_t input_id = fixture_create(f, NMO_CID_BEHAVIORIO, "Start");
    nmo_beobject_state_t *owner = (nmo_beobject_state_t *)fixture_state(f, f->owner_id);
    nmo_behavior_state_t *script = (nmo_behavior_state_t *)fixture_state(f, script_id);
    nmo_behaviorio_state_t *io = (nmo_behaviorio_state_t *)fixture_state(f, input_id);
    if (owner == NULL || script == NULL || io == NULL ||
        nmo_beobject_script_array_append(&owner->scripts, script_id) != NMO_OK ||
        nmo_behavior_ref_array_append(&script->inputs, input_id, NULL) != NMO_OK) {
        return 0;
    }
    script->flags |= CKBEHAVIOR_SCRIPT;
    nmo_behavior_set_owner_id(script, f->owner_id);
    script->has_save_flags = true;
    script->save_flags |= CK_STATESAVE_BEHAVIORSUBBEHAV | CK_STATESAVE_BEHAVIORINPUTS;
    io->old_flags = NMO_BEHAVIORIO_OLD_IN;
    io->has_flags = true;
    if (out_input != NULL) {
        *out_input = input_id;
    }
    return script_id;
}

/*
 * Message manager data declaring `names` as the file's message types, so
 * message parameters (their default is "OnClick") resolve. Before
 * fixture_begin_edit().
 */
static inline void fixture_install_messages(engine_fixture_t *f, const char *const *names,
                                            int32_t count)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, count));
    for (int32_t i = 0; i < count; i++) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_string(chunk, names[i]));
    }
    nmo_chunk_close(chunk);
    nmo_manager_data_t *manager = (nmo_manager_data_t *)nmo_arena_alloc(
        arena, sizeof(*manager), _Alignof(nmo_manager_data_t));
    ASSERT_NOT_NULL(manager);
    *manager = (nmo_manager_data_t){
        .guid = NMO_MANAGER_GUID_MESSAGE,
        .data_size = (uint32_t)nmo_chunk_get_size(chunk),
        .chunk = chunk,
        .flags = 0u,
    };
    nmo_session_set_manager_data(f->session, manager, 1u);
}

/* Context, session and owner; add scripts, then fixture_begin_edit(). */
static inline void fixture_begin_document(engine_fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->ctx = nmo_context_create(&(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(f->ctx);
    f->session = nmo_session_create(f->ctx);
    ASSERT_NOT_NULL(f->session);
    f->repo = nmo_session_get_repository(f->session);
    f->owner_id = fixture_create(f, NMO_CID_3DENTITY, "Owner");
    ASSERT_TRUE(f->owner_id != 0);
}

static inline void fixture_begin_edit(engine_fixture_t *f)
{
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(f->session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f->ctx, document, &workspace));
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(workspace, "engine fixture", &f->tx));
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
}

/* A document with one script, ready for blocks. */
static inline void fixture_begin(engine_fixture_t *f)
{
    fixture_begin_document(f);
    f->script_id = fixture_add_script(f, "Script", &f->script_in);
    ASSERT_TRUE(f->script_id != 0);
    fixture_begin_edit(f);
}

static inline nmo_object_id_t fixture_block_in(engine_fixture_t *f, nmo_object_id_t graph,
                                        nmo_guid_t guid, const char *name)
{
    nmo_object_id_t id = 0;
    return nmo_script_edit_add_node(f->tx, graph, guid, name, &id) == NMO_OK ? id : 0;
}

static inline nmo_object_id_t fixture_block(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    return fixture_block_in(f, f->script_id, guid, name);
}

/* which: 0 inputs, 1 outputs, 2 input parameters, 3 output parameters */
static inline nmo_object_id_t fixture_ref(engine_fixture_t *f, nmo_object_id_t behavior_id,
                                   size_t which, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, behavior_id);
    if (state == NULL) {
        return 0;
    }
    const nmo_array_t *arrays[] = {&state->inputs, &state->outputs, &state->in_parameters,
                                   &state->out_parameters};
    return pos < arrays[which]->count ? nmo_behavior_ref_array_get_id(arrays[which], pos) : 0;
}

#define BLOCK_IN(f, id, pos) fixture_ref((f), (id), 0, (pos))
#define BLOCK_OUT(f, id, pos) fixture_ref((f), (id), 1, (pos))
#define BLOCK_PIN(f, id, pos) fixture_ref((f), (id), 2, (pos))
#define BLOCK_POUT(f, id, pos) fixture_ref((f), (id), 3, (pos))

static inline nmo_object_id_t fixture_target(engine_fixture_t *f, nmo_object_id_t behavior_id)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, behavior_id);
    return state != NULL ? nmo_behavior_target_parameter_id(state) : 0;
}

static inline void fixture_link_in(engine_fixture_t *f, nmo_object_id_t graph, nmo_object_id_t from,
                            nmo_object_id_t to, uint32_t delay)
{
    nmo_object_id_t link = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_behavior_link(f->tx, graph, from, to, delay, &link));
}

static inline void fixture_link(engine_fixture_t *f, nmo_object_id_t from, nmo_object_id_t to,
                         uint32_t delay)
{
    fixture_link_in(f, f->script_id, from, to, delay);
}

/*
 * Give input parameter `pin` a source the engine sets to `size` bytes once
 * it is created. The engine takes the value whatever mode the source's type
 * saves in (a message or an object, not only a buffer).
 */
static inline void fixture_input_value(engine_fixture_t *f, nmo_object_id_t pin, const void *data,
                                size_t size)
{
    nmo_object_id_t source = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_ensure_input_parameter_source(f->tx, pin, &source));
    ASSERT_TRUE(f->value_count < ENGINE_FIXTURE_MAX_VALUES);
    ASSERT_TRUE(size <= sizeof(f->values[0].bytes));
    engine_fixture_value_t *value = &f->values[f->value_count++];
    value->param = source;
    memcpy(value->bytes, data, size);
    value->size = size;
}

static inline void fixture_input_int(engine_fixture_t *f, nmo_object_id_t pin, int32_t value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

static inline void fixture_commit(engine_fixture_t *f, const nmo_engine_options_t *options)
{
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(f->tx));
    f->tx = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(f->session, &f->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f->ctx, f->document, &f->workspace));
    ASSERT_EQ(NMO_OK, nmo_engine_create(f->workspace, options, &f->engine));
    for (size_t i = 0; i < f->value_count; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f->engine, f->values[i].param,
                                                     f->values[i].bytes, f->values[i].size));
    }
}

static inline void fixture_end(engine_fixture_t *f)
{
    if (f->tx != NULL) {
        nmo_script_edit_rollback(f->tx);
    }
    nmo_engine_destroy(f->engine);
    if (f->workspace != NULL) {
        nmo_workspace_destroy(f->workspace);
    }
    if (f->document != NULL) {
        nmo_document_destroy(f->document);
    }
    nmo_session_destroy(f->session);
    nmo_context_release(f->ctx);
}

static inline uint64_t runs(engine_fixture_t *f, nmo_object_id_t id)
{
    return nmo_engine_behavior_execution_count(f->engine, id);
}

/* Copy `size` bytes of the value parameter `param` holds in the engine into `out`. */
static inline void read_value(engine_fixture_t *f, nmo_object_id_t param, void *out, size_t size)
{
    const void *data = NULL;
    size_t data_size = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f->engine, param, &data, &data_size, NULL));
    ASSERT_TRUE(data_size >= size);
    memcpy(out, data, size);
}

#endif /* NMO_TEST_ENGINE_FIXTURE_H */
