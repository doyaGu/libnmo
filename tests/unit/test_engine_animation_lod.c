#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"

#include <float.h>
#include <math.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define POSITION 0x637c4301u
#define SCALE 0x654a3a04u
#define ROTATION 0x49ed4002u

static nmo_object_id_t animation(engine_fixture_t *f, const char *name,
                                nmo_objanim_controller_t *controllers, uint32_t count)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_OBJECTANIMATION, name);
    nmo_objectanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->has_length = 1;
    state->length = 8;
    state->controllers = controllers;
    state->controller_count = count;
    return id;
}

TEST(engine_animation_lod, contribution_reads_all_vector_layouts_and_ignores_tangents_and_axis_keys)
{
    for (int mode = 0; mode < 3; mode++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        float positions[] = {0,1,2,3, 4,6,2,3, 8,-7,6,3}; /* Extent 5 from key zero. */
        float scales[] = {0,1,1,1, 4,3,2,1, 8,-2,-2,-2}; /* Extent 9. */
        float rotation[] = {0,0,0,0,1, 8,0,1,0,0};
        float axis[] = {0,0,0,0,1, 8,0,0,0.70710678f,0.70710678f};
        float tcb_positions[27] = {0}, tcb_scales[27] = {0}, tcb_rotations[20] = {0};
        uint8_t bezier[2][132] = {{0}};
        uint32_t bezier_size = 0;
        for (int v = 0; v < 2; v++) {
            const float *values = v ? scales : positions;
            uint32_t cursor = 0;
            for (int i = 0; i < 3; i++) {
                memcpy((v ? tcb_scales : tcb_positions)+i*9, values+i*4, 16);
                memcpy(bezier[v]+cursor, values+i*4, 16);
                uint32_t flags = i == 1 ? 0x00200020u : 0;
                memcpy(bezier[v]+cursor+16, &flags, 4);
                if (i == 1) {
                    const float tangents[] = {1e20f,-1e20f,3e20f,-4e20f,5e20f,6e20f};
                    memcpy(bezier[v]+cursor+20, tangents, sizeof(tangents));
                }
                cursor += i == 1 ? 44 : 20;
            }
            bezier_size = cursor;
        }
        for (int i = 0; i < 2; i++) memcpy(tcb_rotations+i*10, rotation+i*5, 20);
        uint32_t morph[] = {0,4,0}; /* An empty controller still contributes 1000. */
        nmo_objanim_controller_t controllers[] = {
            {mode == 0 ? POSITION : mode == 1 ? 0x347e4a01u : 0x921ab801u,3,
             mode == 0 ? sizeof(positions) : mode == 1 ? sizeof(tcb_positions) : bezier_size,
             mode == 0 ? (void *)positions : mode == 1 ? (void *)tcb_positions : bezier[0]},
            {mode == 0 ? SCALE : mode == 1 ? 0x1b545904u : 0x18ab4404u,3,
             mode == 0 ? sizeof(scales) : mode == 1 ? sizeof(tcb_scales) : bezier_size,
             mode == 0 ? (void *)scales : mode == 1 ? (void *)tcb_scales : bezier[1]},
            {mode == 1 ? 0x45b52a02u : ROTATION,2,
             mode == 1 ? sizeof(tcb_rotations) : sizeof(rotation), mode == 1 ? tcb_rotations : rotation},
            {0x2f200b08u,2,sizeof(axis),axis},
            {NMO_OBJANIM_CONTROLLER_MORPH,0,sizeof(morph),morph},
        };
        nmo_object_id_t id = animation(&f, "Channel", controllers, COUNT(controllers));
        nmo_objectanimation_state_t *state = fixture_state(&f, id);
        state->flags = CK_OBJECTANIMATION_IGNOREPOS | CK_OBJECTANIMATION_IGNORESCALE |
            CK_OBJECTANIMATION_IGNOREROT | CK_OBJECTANIMATION_IGNOREMORPH | CK_OBJECTANIMATION_IGNORESCALEROT;
        fixture_begin_edit(&f);
        fixture_commit_bare(&f, NULL);
        ASSERT_FLOAT_EQ(2014, eng_animation_contribution(f.engine, id), 0);
        controllers[0].data_size--;
        ASSERT_FLOAT_EQ(2009, eng_animation_contribution(f.engine, id), 0);
        fixture_end(&f);
    }
}

TEST(engine_animation_lod, contribution_preserves_wide_cancellation_and_small_raw_quaternion_motion)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    float cancelled[] = {0,FLT_MAX,-FLT_MAX,0, 8,-FLT_MAX,FLT_MAX,0};
    float small = ldexpf(1,-13);
    float rotation[] = {0,small,0,0,1, 8,-small,0,0,1};
    nmo_objanim_controller_t controllers[] = {{POSITION,2,sizeof(cancelled),cancelled},{ROTATION,2,sizeof(rotation),rotation}};
    nmo_object_id_t id = animation(&f, "Small rotation", controllers, COUNT(controllers));
    fixture_begin_edit(&f);
    fixture_commit_bare(&f, NULL);
    ASSERT_FLOAT_EQ(ldexpf(125,-23), eng_animation_contribution(f.engine, id), 0);
    rotation[6] = -rotation[1]; rotation[9] = -rotation[4];
    ASSERT_FLOAT_EQ(0, eng_animation_contribution(f.engine, id), 0); /* Opposite quaternion signs. */
    fixture_end(&f);
}

TEST(engine_animation_lod, merged_contribution_averages_sources_independently_of_weight_and_guards_cycles)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    float ka[] = {0,0,0,0, 8,2,0,0}, kb[] = {0,0,0,0, 8,18,0,0};
    nmo_objanim_controller_t ca = {POSITION,2,sizeof(ka),ka}, cb = {POSITION,2,sizeof(kb),kb};
    nmo_object_id_t a = animation(&f, "A", &ca, 1), b = animation(&f, "B", &cb, 1);
    nmo_object_id_t shared = animation(&f, "Shared", NULL, 0), merged = animation(&f, "Merged", NULL, 0);
    nmo_objectanimation_state_t *alias = fixture_state(&f, shared), *blend = fixture_state(&f, merged);
    alias->format = CKOBJANIM_FORMAT_SHARED;
    alias->has_shared_anim = 1;
    alias->shared_anim = nmo_ref_from_id(b);
    blend->has_merge = 1;
    blend->flags = CK_OBJECTANIMATION_MERGED;
    blend->anim1 = nmo_ref_from_id(a);
    blend->anim2 = nmo_ref_from_id(shared);
    fixture_begin_edit(&f);
    fixture_commit_bare(&f, NULL);
    const float factors[] = {-1,0,0.75f,1,2,NAN};
    for (size_t i = 0; i < COUNT(factors); i++) {
        blend->merge_factor = factors[i];
        ASSERT_FLOAT_EQ(10, eng_animation_contribution(f.engine, merged), 0);
    }
    blend->anim1 = nmo_ref_from_id(merged);
    ASSERT_FLOAT_EQ(9, eng_animation_contribution(f.engine, merged), 0);
    blend->anim2 = blend->anim1;
    ASSERT_FLOAT_EQ(0, eng_animation_contribution(f.engine, merged), 0);
    blend->anim1 = blend->anim2 = nmo_ref_from_id(a);
    float extent = ldexpf(1,127);
    ka[5] = extent;
    ASSERT_FLOAT_EQ(extent, eng_animation_contribution(f.engine, merged), 0);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation_lod, contribution_reads_all_vector_layouts_and_ignores_tangents_and_axis_keys);
REGISTER_TEST(engine_animation_lod, contribution_preserves_wide_cancellation_and_small_raw_quaternion_motion);
REGISTER_TEST(engine_animation_lod, merged_contribution_averages_sources_independently_of_weight_and_guards_cycles);
TEST_MAIN_END()
