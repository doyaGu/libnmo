#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define POSITION 0x637c4301u
#define ROTATION 0x49ed4002u
#define SCALE 0x654a3a04u
#define AXIS 0x2f200b08u

static nmo_object_id_t shared_animation(engine_fixture_t *f, const char *name, nmo_object_id_t owner)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_OBJECTANIMATION, name);
    nmo_objectanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    state->format = CKOBJANIM_FORMAT_SHARED;
    state->has_shared_anim = 1;
    state->shared_anim = nmo_ref_from_id(owner);
    state->entity = nmo_ref_from_id(f->owner_id);
    return id;
}

TEST(engine_composition, shared_keys_keep_the_alias_target_flags_and_owner_layout)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t owner = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Keys");
    nmo_object_id_t alias = shared_animation(&f, "Alias", owner);
    nmo_object_id_t chain = shared_animation(&f, "Chain", alias);
    float position[] = {0, 0,1,2, 8, 8,9,10}, scale[] = {0, 2,4,6};
    float rotation[] = {0, 0.70710678f,0,0,0.70710678f};
    float axis[] = {0, 123, 0,0,0.70710678f,0.70710678f};
    nmo_objanim_controller_t controllers[] = {
        {POSITION, 2, sizeof(position), position}, {SCALE, 1, sizeof(scale), scale},
        {ROTATION, 1, sizeof(rotation), rotation}, {AXIS, 1, sizeof(axis), axis},
    };
    nmo_objectanimation_state_t *keys = fixture_state(&f, owner), *copy = fixture_state(&f, chain);
    keys->format = CKOBJANIM_FORMAT_NEWDATA;
    keys->has_length = 1;
    keys->length = 8;
    keys->flags = CK_OBJECTANIMATION_IGNOREPOS | CK_OBJECTANIMATION_IGNOREROT |
        CK_OBJECTANIMATION_IGNORESCALE | CK_OBJECTANIMATION_IGNORESCALEROT | CK_OBJECTANIMATION_MERGED;
    keys->controllers = controllers;
    keys->controller_count = COUNT(controllers);
    copy->has_length = 1;
    copy->length = 999; /* The alias's serialized layout has no length field. */
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, chain, 0.5f));
    float matrix[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
    const float expected[] = {4,0,0,0, 0,0,-2,0, 0,6,0,0, 4,5,6,1};
    for (size_t i = 0; i < COUNT(expected); i++) ASSERT_FLOAT_EQ(expected[i], matrix[i], 0.00001f);
    copy->flags = CK_OBJECTANIMATION_IGNOREPOS;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, chain, 1));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
    ASSERT_FLOAT_EQ(4, matrix[12], 0.00001f);
    ASSERT_FLOAT_EQ(5, matrix[13], 0.00001f);
    ASSERT_FLOAT_EQ(6, matrix[14], 0.00001f);
    ASSERT_EQ(0, memcmp(keys->controllers[0].data, position, sizeof(position)));
    fixture_end(&f);
}

TEST(engine_composition, absent_and_cyclic_shared_references_preserve_the_pose)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t first = shared_animation(&f, "First", 0);
    nmo_object_id_t second = shared_animation(&f, "Second", first);
    nmo_objectanimation_state_t *state = fixture_state(&f, first);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const nmo_object_id_t targets[] = {0, first, second, f.owner_id, UINT32_MAX};
    float initial[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, initial));
    initial[12] = 7;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, initial, false));
    for (size_t i = 0; i < COUNT(targets); i++) {
        state->shared_anim = nmo_ref_from_id(targets[i]);
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, first, 0.5f));
        float matrix[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
        ASSERT_EQ(0, memcmp(initial, matrix, sizeof(matrix)));
    }
    fixture_end(&f);
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name;
    return data;
}

TEST(engine_composition, loaded_shared_references_resolve_inside_their_module)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_t *entity = nmo_object_repository_find_by_id(f.repo, f.owner_id);
    ASSERT_EQ(NMO_OK, nmo_object_set_name(entity, "Shared Target"));
    nmo_object_id_t owner = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Keys");
    nmo_object_id_t alias = shared_animation(&f, "Alias", owner);
    float position[] = {0, 0,0,0, 8, 8,16,24};
    nmo_objanim_controller_t controller = {POSITION, 2, sizeof(position), position};
    nmo_objectanimation_state_t *state = fixture_state(&f, owner);
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->has_length = 1;
    state->length = 8;
    state->flags = CK_OBJECTANIMATION_IGNOREPOS;
    state->controller_count = 1;
    state->controllers = &controller;
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_shared.cmo");
    ASSERT_EQ(NMO_OK, nmo_session_save_file(f.session, path, NULL, NULL));
    engine_fixture_t host;
    fixture_begin_document(&host);
    fixture_begin_edit(&host);
    fixture_commit(&host, NULL);
    nmo_engine_set_file_resolver(host.engine, resolve_file, (void *)path);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(host.engine, "shared.cmo", false, &file));
    ASSERT_TRUE(file > 0);
    nmo_object_id_t loaded = nmo_engine_object_named(host.engine, "Alias", NMO_CID_OBJECTANIMATION);
    nmo_object_id_t target = nmo_engine_object_named(host.engine, "Shared Target", NMO_CID_3DENTITY);
    ASSERT_TRUE(loaded != 0 && loaded != alias && target != 0 && target != host.owner_id);
    ASSERT_TRUE(eng_object_animation_set_step(host.engine, loaded, 0.5f));
    float matrix[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(host.engine, target, matrix));
    ASSERT_FLOAT_EQ(4, matrix[12], 0.000001f);
    ASSERT_FLOAT_EQ(8, matrix[13], 0.000001f);
    ASSERT_FLOAT_EQ(12, matrix[14], 0.000001f);
    ASSERT_TRUE(nmo_engine_entity_world_matrix(host.engine, host.owner_id, matrix));
    ASSERT_FLOAT_EQ(0, matrix[12], 0.000001f);
    fixture_end(&host);
    fixture_end(&f);
    remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_composition, shared_keys_keep_the_alias_target_flags_and_owner_layout);
REGISTER_TEST(engine_composition, absent_and_cyclic_shared_references_preserve_the_pose);
REGISTER_TEST(engine_composition, loaded_shared_references_resolve_inside_their_module);
TEST_MAIN_END()
