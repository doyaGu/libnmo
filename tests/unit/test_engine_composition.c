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

static nmo_object_id_t merged_animation(engine_fixture_t *f, const char *name,
                                        nmo_object_id_t first, nmo_object_id_t second, float length)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_OBJECTANIMATION, name);
    nmo_objectanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->entity = nmo_ref_from_id(f->owner_id);
    state->has_length = state->has_merge = 1;
    state->length = length;
    state->flags = CK_OBJECTANIMATION_MERGED;
    state->merge_factor = 0.5f;
    state->anim1 = nmo_ref_from_id(first);
    state->anim2 = nmo_ref_from_id(second);
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

TEST(engine_composition, merged_channels_map_source_times_and_blend_before_composing)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = fixture_create(&f, NMO_CID_OBJECTANIMATION, "A");
    nmo_object_id_t b = fixture_create(&f, NMO_CID_OBJECTANIMATION, "B");
    nmo_object_id_t merged = merged_animation(&f, "Blend", a, b, 40);
    float pa[] = {0, 0,0,0, 8, 8,16,24}, pb[] = {0, 100,200,300, 20, 120,240,360};
    float sa[] = {0, 2,4,6}, sb[] = {0, 6,8,10};
    float ra[] = {0, 0,0,0,1}, rb[] = {0, 0,0,-0.70710678f,-0.70710678f};
    nmo_objanim_controller_t ca[] = {{POSITION,2,sizeof(pa),pa}, {SCALE,1,sizeof(sa),sa}, {ROTATION,1,sizeof(ra),ra}};
    nmo_objanim_controller_t cb[] = {{POSITION,2,sizeof(pb),pb}, {SCALE,1,sizeof(sb),sb}, {ROTATION,1,sizeof(rb),rb}};
    nmo_objectanimation_state_t *first = fixture_state(&f, a), *second = fixture_state(&f, b), *blend = fixture_state(&f, merged);
    first->has_length = second->has_length = 1;
    first->length = 8;
    second->length = 20;
    first->controllers = ca; first->controller_count = COUNT(ca);
    second->controllers = cb; second->controller_count = COUNT(cb);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const float factors[] = {-0.5f, 0, 0.25f, 0.5f, 1, 1.5f};
    for (size_t i = 0; i < COUNT(factors); i++) {
        float t = blend->merge_factor = factors[i];
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, merged, 0.25f));
        float matrix[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
        float x = 2 + 4*t, y = 4 + 4*t, z = 6 + 4*t;
        float c = (float)cos(t * 1.5707963267948966), s = (float)sin(t * 1.5707963267948966);
        const float expected[] = {x*c,-x*s,0,0, y*s,y*c,0,0, 0,0,z,0,
                                  2*(1-t)+105*t, 4*(1-t)+210*t, 6*(1-t)+315*t, 1};
        for (size_t k = 0; k < COUNT(expected); k++) ASSERT_FLOAT_EQ(expected[k], matrix[k], 0.00003f);
    }
    /* Source entities are absent: evaluation must only sample their channels. */
    ASSERT_EQ(0u, nmo_ref_runtime_id(&first->entity));
    ASSERT_EQ(0u, nmo_ref_runtime_id(&second->entity));
    fixture_end(&f);
}

TEST(engine_composition, merged_scale_axes_use_slerp_before_noncommuting_object_rotation)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = fixture_create(&f, NMO_CID_OBJECTANIMATION, "A");
    nmo_object_id_t b = fixture_create(&f, NMO_CID_OBJECTANIMATION, "B");
    nmo_object_id_t merged = merged_animation(&f, "Blend", a, b, 8);
    float scale[] = {0, 2,4,6}, rotation[] = {0, 0.70710678f,0,0,0.70710678f};
    float aa[] = {0, 0,0,0,1}, ab[] = {0, 0,0,0.70710678f,0.70710678f};
    nmo_objanim_controller_t ca[] = {{SCALE,1,sizeof(scale),scale}, {ROTATION,1,sizeof(rotation),rotation}, {AXIS,1,sizeof(aa),aa}};
    nmo_objanim_controller_t cb[] = {{SCALE,1,sizeof(scale),scale}, {ROTATION,1,sizeof(rotation),rotation}, {AXIS,1,sizeof(ab),ab}};
    nmo_objectanimation_state_t *first = fixture_state(&f, a), *second = fixture_state(&f, b), *blend = fixture_state(&f, merged);
    first->controllers = ca; first->controller_count = COUNT(ca);
    second->controllers = cb; second->controller_count = COUNT(cb);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int i = 0; i <= 8; i++) {
        float t = blend->merge_factor = (float)i / 8;
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, merged, 0.5f));
        double c = cos(t * 1.5707963267948966), s = sin(t * 1.5707963267948966);
        float xx = (float)(2*c*c + 4*s*s), yy = (float)(2*s*s + 4*c*c), xy = (float)(-2*c*s);
        const float expected[] = {xx,0,-xy,0, xy,0,-yy,0, 0,6,0,0, 0,0,0,1};
        float matrix[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
        for (size_t k = 0; k < COUNT(expected); k++) ASSERT_FLOAT_EQ(expected[k], matrix[k], 0.00001f);
    }
    fixture_end(&f);
}

TEST(engine_composition, missing_channels_and_cycles_fall_back_per_channel)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t a = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Empty");
    nmo_object_id_t b = fixture_create(&f, NMO_CID_OBJECTANIMATION, "B");
    nmo_object_id_t merged = merged_animation(&f, "Blend", a, b, 8);
    float position[] = {0, 100,200,300}, scale[] = {0, 4,4,4}, rotation[] = {0, 0,0,0.70710678f,0.70710678f};
    nmo_objanim_controller_t controllers[] = {{POSITION,1,sizeof(position),position},
        {SCALE,1,sizeof(scale),scale}, {ROTATION,1,sizeof(rotation),rotation}};
    nmo_objectanimation_state_t *source = fixture_state(&f, b), *blend = fixture_state(&f, merged);
    source->controllers = controllers;
    source->controller_count = COUNT(controllers);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int cycle = 0; cycle < 2; cycle++) {
        if (cycle) blend->anim1 = nmo_ref_from_id(merged);
        for (int i = 0; i < 3; i++) {
            blend->merge_factor = (float)i / 2;
            const float initial[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1};
            ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, initial, false));
            ASSERT_TRUE(eng_object_animation_set_step(f.engine, merged, 0.5f));
            float matrix[16];
            ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
            ASSERT_FLOAT_EQ(i == 0 ? 7 : 100, matrix[12], 0.00001f);
            ASSERT_FLOAT_EQ(i == 0 ? 4 : 0, matrix[0], 0.00001f);
            ASSERT_FLOAT_EQ(i == 0 ? 0 : -4, matrix[1], 0.00001f);
            ASSERT_FLOAT_EQ(4, matrix[10], 0.00001f); /* Scale falls back even at factor zero. */
        }
    }
    /* Reusing a descendant on both sides is a DAG, not a recursion cycle. */
    blend->anim1 = blend->anim2;
    blend->merge_factor = 0.5f;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, merged, 0.5f));
    float matrix[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
    ASSERT_FLOAT_EQ(100, matrix[12], 0.000001f);
    blend->merge_factor = NAN;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, merged, 0.5f));
    float unchanged[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, unchanged));
    ASSERT_EQ(0, memcmp(matrix, unchanged, sizeof(matrix)));
    fixture_end(&f);
}

TEST(engine_composition, merged_time_mapping_handles_zero_and_extreme_finite_lengths)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t child = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Child");
    nmo_object_id_t merged = merged_animation(&f, "Blend", child, child, 8);
    float position[] = {0, 0,0,0, 8, 100,0,0};
    nmo_objanim_controller_t controller = {POSITION,2,sizeof(position),position};
    nmo_objectanimation_state_t *source = fixture_state(&f, child), *blend = fixture_state(&f, merged);
    source->has_length = 1;
    source->controller_count = 1;
    source->controllers = &controller;
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const float cases[][3] = {{0,8,0}, {1e30f,1e-30f,25}, {1e-30f,1e30f,25},
                              {8,0,0}, {8,INFINITY,7}, {INFINITY,8,7}};
    for (size_t i = 0; i < COUNT(cases); i++) {
        blend->length = cases[i][0];
        source->length = cases[i][1];
        position[4] = isfinite(source->length) && source->length > 0 ? source->length : 8;
        const float initial[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1};
        ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, initial, false));
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, merged, 0.25f));
        float matrix[16];
        ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, matrix));
        ASSERT_FLOAT_EQ(cases[i][2], matrix[12], 0.00001f);
    }
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_composition, shared_keys_keep_the_alias_target_flags_and_owner_layout);
REGISTER_TEST(engine_composition, absent_and_cyclic_shared_references_preserve_the_pose);
REGISTER_TEST(engine_composition, loaded_shared_references_resolve_inside_their_module);
REGISTER_TEST(engine_composition, merged_channels_map_source_times_and_blend_before_composing);
REGISTER_TEST(engine_composition, merged_scale_axes_use_slerp_before_noncommuting_object_rotation);
REGISTER_TEST(engine_composition, missing_channels_and_cycles_fall_back_per_channel);
REGISTER_TEST(engine_composition, merged_time_mapping_handles_zero_and_extreme_finite_lengths);
TEST_MAIN_END()
