#include "engine_fixture.h"

#include "document/nmo_document_save.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_ref.h"
#include "object/nmo_serialize_context.h"

#include <stdio.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_OBJECT_LOAD NMO_GUID(0x7bd977d7, 0x26396c0c)

/* The files the engine loads, from the scratch directory */
static const char *resolve_scratch(void *user_data, const char *name)
{
    static char path[512];
    (void)user_data;
    int length = snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, name);
    return length > 0 && (size_t)length < sizeof(path) ? path : NULL;
}

/*
 * Save "loaded.nmo": an entity "Loaded" saved to start active, with a
 * script "Loaded Script" saved to start active and reset, and one block.
 */
static void save_loaded_file(void)
{
    engine_fixture_t g;
    fixture_begin_document(&g);
    nmo_object_t *owner = nmo_object_repository_find_by_id(g.repo, g.owner_id);
    ASSERT_NOT_NULL(owner);
    ASSERT_EQ(NMO_OK, nmo_object_set_name(owner, "Loaded"));
    g.script_id = fixture_add_script(&g, "Loaded Script", &g.script_in);
    ASSERT_TRUE(g.script_id != 0);
    nmo_beobject_state_t *be = (nmo_beobject_state_t *)fixture_state(&g, g.owner_id);
    nmo_behavior_state_t *script = (nmo_behavior_state_t *)fixture_state(&g, g.script_id);
    ASSERT_TRUE(be != NULL && script != NULL);
    be->has_single_activity = 1;
    be->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    script->has_single_activity = true;
    script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    fixture_begin_edit(&g);
    nmo_object_id_t nop = fixture_block(&g, GUID_NOP, "Nop");
    ASSERT_TRUE(nop != 0);
    fixture_link(&g, g.script_in, BLOCK_IN(&g, nop, 0), 0);
    fixture_commit(&g, NULL);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NMO_TEST_SCRATCH_DIR, "loaded.nmo");
    ASSERT_EQ(NMO_OK, nmo_document_save_file(g.document, path, NULL));
    fixture_end(&g);
}

/* The engine ID of the script named `name`; 0 when there is none. */
static nmo_object_id_t script_named(engine_fixture_t *f, const char *name)
{
    for (int32_t i = 0; i < nmo_engine_script_count(f->engine); i++) {
        nmo_object_id_t id = nmo_engine_script_id(f->engine, i);
        const char *script_name = nmo_engine_object_name(f->engine, id);
        if (script_name != NULL && strcmp(script_name, name) == 0) {
            return id;
        }
    }
    return 0;
}

TEST(engine_loading, object_load_adds_the_file_to_the_level)
{
    save_loaded_file();
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t load = fixture_block(&f, GUID_OBJECT_LOAD, "Load");
    nmo_object_id_t missing = fixture_block(&f, GUID_OBJECT_LOAD, "Missing");
    nmo_object_id_t loaded = fixture_block(&f, GUID_NOP, "Loaded");
    nmo_object_id_t failed = fixture_block(&f, GUID_NOP, "Failed");
    ASSERT_TRUE(load != 0 && missing != 0 && loaded != 0 && failed != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, load, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, load, 0), BLOCK_IN(&f, loaded, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, loaded, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 2), BLOCK_IN(&f, failed, 0), 0);
    fixture_input_value(&f, BLOCK_PIN(&f, load, 0), "loaded.nmo", sizeof("loaded.nmo"));
    fixture_input_int(&f, BLOCK_PIN(&f, load, 2), NMO_CID_3DENTITY);
    fixture_input_value(&f, BLOCK_PIN(&f, missing, 0), "none.nmo", sizeof("none.nmo"));
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    ASSERT_EQ(0, script_named(&f, "Loaded Script"));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, loaded));
    ASSERT_EQ(1u, runs(&f, failed));
    ASSERT_STR_EQ("loaded.nmo", nmo_engine_loaded_file_name(f.engine, 1));
    /* The master object: the root entity of the filter class, with an engine ID of its own */
    uint32_t master = 0;
    read_value(&f, BLOCK_POUT(&f, load, 1), &master, sizeof(master));
    ASSERT_TRUE(master != 0 && master != f.owner_id);
    ASSERT_STR_EQ("Loaded", nmo_engine_object_name(f.engine, master));

    /* Added to the level as saved: active now, its script runs from the next frame */
    nmo_object_id_t script = script_named(&f, "Loaded Script");
    ASSERT_TRUE(script != 0);
    ASSERT_EQ(0u, runs(&f, script));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_TRUE(nmo_engine_object_is_active(f.engine, master));
    ASSERT_EQ(1u, runs(&f, script));
    fixture_end(&f);
}

/* The level scene of the document: `count` objects starting with `flags` */
static void add_level_scene(engine_fixture_t *f, const nmo_object_id_t *ids,
                            const uint32_t *flags, size_t count)
{
    nmo_object_id_t level_id = fixture_create(f, NMO_CID_LEVEL, "Level");
    nmo_level_state_t *level = (nmo_level_state_t *)fixture_state(f, level_id);
    ASSERT_NOT_NULL(level);
    nmo_scene_state_t scene;
    memset(&scene, 0, sizeof(scene));
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&scene, NULL, NULL));
    scene.level = nmo_ref_from_id(level_id);
    nmo_scene_object_desc_t *descs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&scene.object_descs, count, (void **)&descs));
    for (size_t i = 0; i < count; i++) {
        descs[i].ref = nmo_ref_from_id(ids[i]);
        descs[i].flags = flags[i];
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_SCENE;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_serialize_context_t context =
        nmo_serialize_context_create(arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(&scene, chunk, NULL, &context));
    nmo_chunk_close(chunk);
    nmo_scene_vtable.destroy(&scene, NULL, NULL);
    level->level_scene_chunk = chunk;
}

TEST(engine_loading, level_scene_says_what_starts_active)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t starts_in = 0, waits_in = 0, elsewhere_in = 0;
    nmo_object_id_t starts = fixture_add_script(&f, "Starts", &starts_in);
    nmo_object_id_t waits = fixture_add_script(&f, "Waits", &waits_in);
    nmo_object_id_t owner = f.owner_id;
    f.owner_id = other;
    nmo_object_id_t elsewhere = fixture_add_script(&f, "Elsewhere", &elsewhere_in);
    f.owner_id = owner;
    ASSERT_TRUE(starts != 0 && waits != 0 && elsewhere != 0);
    /* Other and its script are not in the scene */
    const nmo_object_id_t ids[3] = {owner, starts, waits};
    const uint32_t flags[3] = {
        CK_SCENEOBJECT_START_ACTIVATE,
        CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET,
        CK_SCENEOBJECT_START_DEACTIVATE | CK_SCENEOBJECT_START_RESET,
    };
    add_level_scene(&f, ids, flags, 3);
    fixture_begin_edit(&f);
    nmo_object_id_t scripts[3] = {starts, waits, elsewhere};
    nmo_object_id_t inputs[3] = {starts_in, waits_in, elsewhere_in};
    nmo_object_id_t nops[3];
    for (int i = 0; i < 3; i++) {
        nops[i] = fixture_block_in(&f, scripts[i], GUID_NOP, "Nop");
        ASSERT_TRUE(nops[i] != 0);
        fixture_link_in(&f, scripts[i], inputs[i], BLOCK_IN(&f, nops[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_SCENE_DEFAULT));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, nops[0]));
    ASSERT_EQ(0u, runs(&f, nops[1]));
    ASSERT_EQ(0u, runs(&f, nops[2]));
    ASSERT_TRUE(nmo_engine_object_is_active(f.engine, owner));
    ASSERT_FALSE(nmo_engine_object_is_active(f.engine, other));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_loading, object_load_adds_the_file_to_the_level);
REGISTER_TEST(engine_loading, level_scene_says_what_starts_active);
TEST_MAIN_END()
