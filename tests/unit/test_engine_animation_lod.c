#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_render.h"
#include "object/builtin/nmo_character_schemas.h"

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

static nmo_object_id_t entity(engine_fixture_t *f, const char *name, nmo_object_id_t parent)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DENTITY, name);
    nmo_3dentity_state_t *state = fixture_state(f, id);
    if (state != NULL) state->parent = nmo_ref_from_id(parent);
    return id;
}

static nmo_object_id_t curve(engine_fixture_t *f, const char *name, nmo_object_id_t target, float start, float end)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    float *keys = nmo_arena_alloc(arena, 8*sizeof(float), _Alignof(float));
    nmo_objanim_controller_t *controller = nmo_arena_alloc(arena, sizeof(*controller), _Alignof(nmo_objanim_controller_t));
    if (keys == NULL || controller == NULL) return 0;
    const float values[] = {0,start,0,0, 8,end,0,0};
    memcpy(keys, values, sizeof(values));
    *controller = (nmo_objanim_controller_t){POSITION,2,sizeof(values),keys};
    nmo_object_id_t id = animation(f, name, controller, 1);
    nmo_objectanimation_state_t *state = fixture_state(f, id);
    if (state != NULL) state->entity = nmo_ref_from_id(target);
    return id;
}

static nmo_object_id_t group(engine_fixture_t *f, nmo_object_id_t character, nmo_object_id_t root,
                             nmo_ref_t *members, uint32_t count)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_KEYEDANIMATION, "Global");
    nmo_keyedanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    state->animation_ids = members;
    state->animation_count = count;
    state->base.has_character = state->base.has_root_entity = 1;
    state->base.character = nmo_ref_from_id(character);
    state->base.root_entity = nmo_ref_from_id(root);
    state->base.flags = CKANIMATION_SUBANIMSSORTED; /* Load must invalidate this saved hint. */
    return id;
}

static float x(const nmo_engine_t *engine, nmo_object_id_t target)
{
    float matrix[16];
    return nmo_engine_entity_world_matrix(engine, target, matrix) ? matrix[12] : NAN;
}

static void append_controller(engine_fixture_t *f, nmo_object_id_t id, nmo_objanim_controller_t controller)
{
    nmo_objectanimation_state_t *state = fixture_state(f, id);
    nmo_objanim_controller_t *array = nmo_arena_alloc(nmo_session_get_arena(f->session),
        (state->controller_count+1)*sizeof(*array), _Alignof(nmo_objanim_controller_t));
    ASSERT_NOT_NULL(array);
    memcpy(array, state->controllers, state->controller_count*sizeof(*array));
    array[state->controller_count++] = controller;
    state->controllers = array;
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

TEST(engine_animation_lod, invisible_characters_update_the_root_and_optional_floor_prefix)
{
    for (int floor_present = 0; floor_present < 2; floor_present++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
        nmo_object_id_t root = entity(&f, "Root", character), low = entity(&f, "Low", character), high = entity(&f, "High", character);
        nmo_object_id_t floor = floor_present ? entity(&f, "Floor", character) : 0;
        nmo_character_state_t *actor = fixture_state(&f, character);
        actor->floor_ref = nmo_ref_from_id(floor);
        nmo_object_id_t a = curve(&f, "Low Curve", low, 0,2), b = curve(&f, "High Curve", high, 0,20);
        nmo_object_id_t r = curve(&f, "Root Curve", root, 0,1);
        nmo_object_id_t floor_curve = floor_present ? curve(&f, "Floor Curve", floor, 0,2) : 0;
        nmo_ref_t members[] = {nmo_ref_from_id(a),nmo_ref_from_id(b),nmo_ref_from_id(floor_present ? floor_curve : r),nmo_ref_from_id(r)};
        nmo_object_id_t keyed = group(&f, character, root, members, floor_present ? 4 : 3);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_FLOAT_EQ(1, eng_character_animation_lod(f.engine, character), 0);
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
        ASSERT_FLOAT_EQ(1, x(f.engine, root), 0);
        if (floor_present) ASSERT_FLOAT_EQ(2, x(f.engine, floor), 0);
        ASSERT_FLOAT_EQ(0, x(f.engine, high), 0);
        ASSERT_FLOAT_EQ(0, x(f.engine, low), 0);
        fixture_end(&f);
    }
}

TEST(engine_animation_lod, lod_truncates_the_wide_product_and_full_lod_updates_remaining_channels)
{
    for (int floor_present = 0; floor_present < 2; floor_present++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
        nmo_character_state_t *actor = fixture_state(&f, character);
        actor->base.moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
        nmo_object_id_t root = entity(&f, "Root", character), limbs[25];
        nmo_object_id_t floor = floor_present ? entity(&f, "Floor", character) : 0;
        actor->floor_ref = nmo_ref_from_id(floor);
        nmo_ref_t members[27], saved[27];
        for (int i = 0; i < 25; i++) {
            limbs[i] = entity(&f, "Limb", character);
            members[i] = nmo_ref_from_id(curve(&f, "Limb Curve", limbs[i], 0,(float)(100-i)));
        }
        uint32_t count = 25;
        if (floor_present) members[count++] = nmo_ref_from_id(curve(&f, "Floor Curve", floor, 0,2));
        members[count++] = nmo_ref_from_id(curve(&f, "Root Curve", root, 0,1));
        memcpy(saved, members, count*sizeof(*members));
        nmo_object_id_t keyed = group(&f, character, root, members, count);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0.04f));
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
        ASSERT_FLOAT_EQ(1, x(f.engine, root), 0);
        if (floor_present) ASSERT_FLOAT_EQ(2, x(f.engine, floor), 0);
        for (int i = 0; i < 25; i++) ASSERT_FLOAT_EQ(0, x(f.engine, limbs[i]), 0);
        ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, nextafterf(0.04f,1)));
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
        ASSERT_FLOAT_EQ(100, x(f.engine, limbs[0]), 0);
        for (int i = 1; i < 25; i++) ASSERT_FLOAT_EQ(0, x(f.engine, limbs[i]), 0);
        ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 1));
        ASSERT_FALSE(eng_character_set_animation_lod(f.engine, character, NAN));
        ASSERT_FALSE(eng_character_set_animation_lod(f.engine, root, 0.5f));
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
        for (int i = 0; i < 25; i++) ASSERT_FLOAT_EQ((float)(100-i)*0.5f, x(f.engine, limbs[i]), 0.000001f);
        ASSERT_EQ(0, memcmp(saved, members, count*sizeof(*members)));
        const nmo_keyedanimation_state_t *source = fixture_state(&f, keyed);
        ASSERT_EQ(CKANIMATION_SUBANIMSSORTED, source->base.flags);
        fixture_end(&f);
    }
}

TEST(engine_animation_lod, mixed_channels_sort_stably_ahead_of_unbound_and_empty_slots)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
    nmo_character_state_t *actor = fixture_state(&f, character);
    actor->base.moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
    nmo_object_id_t root = entity(&f, "Root", character), targets[5], channels[5];
    for (int i = 0; i < 5; i++) {
        targets[i] = entity(&f, "Target", character);
        channels[i] = curve(&f, "Channel", targets[i], (float)(i+1)*10, (float)(i+1)*10);
    }
    nmo_objectanimation_state_t *moving = fixture_state(&f, channels[0]);
    ((float *)moving->controllers[0].data)[1] = 0; /* Position score 10. */
    float scale[] = {0,1,1,1, 8,21,1,1}, rotation[] = {0,0,0,0,1, 8,0,1,0,0};
    float axis[] = {0,0,0,0,1, 8,0,0,0.70710678f,0.70710678f};
    uint32_t morph[] = {0,4,0};
    append_controller(&f, channels[1], (nmo_objanim_controller_t){SCALE,2,sizeof(scale),scale});
    append_controller(&f, channels[2], (nmo_objanim_controller_t){ROTATION,2,sizeof(rotation),rotation});
    append_controller(&f, channels[3], (nmo_objanim_controller_t){NMO_OBJANIM_CONTROLLER_MORPH,0,sizeof(morph),morph});
    append_controller(&f, channels[4], (nmo_objanim_controller_t){0x2f200b08u,2,sizeof(axis),axis});
    nmo_object_id_t r = curve(&f, "Root Curve", root, 0,1), unbound = animation(&f, "Unbound", NULL,0);
    nmo_ref_t members[] = {nmo_ref_from_id(0),nmo_ref_from_id(unbound),nmo_ref_from_id(channels[0]),
        nmo_ref_from_id(channels[1]),nmo_ref_from_id(channels[2]),nmo_ref_from_id(channels[3]),nmo_ref_from_id(channels[4]),nmo_ref_from_id(r)};
    nmo_object_id_t keyed = group(&f, character, root, members, COUNT(members));
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0.2f));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(30, x(f.engine, targets[2]), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, targets[3]), 0); /* Equal scores retain rotation-before-morph order. */
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0.4f));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(40, x(f.engine, targets[3]), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, targets[1]), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, targets[0]), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, targets[4]), 0);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 1));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    for (int i = 0; i < 5; i++) ASSERT_FLOAT_EQ((float)(i+1)*10, x(f.engine, targets[i]), 0.00001f);
    fixture_end(&f);
}

TEST(engine_animation_lod, membership_target_and_floor_changes_rebuild_the_private_order)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
    nmo_character_state_t *actor = fixture_state(&f, character);
    actor->base.moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
    nmo_object_id_t root = entity(&f, "Root", character), low = entity(&f, "Low", character);
    nmo_object_id_t high = entity(&f, "High", character), added = entity(&f, "Added", character);
    nmo_object_id_t a = curve(&f, "A", low, 0,2), b = curve(&f, "B", high, 0,10);
    nmo_object_id_t c = curve(&f, "C", added, 0,20), r = curve(&f, "Root Curve", root, 0,1);
    nmo_ref_t members[] = {nmo_ref_from_id(a),nmo_ref_from_id(b),nmo_ref_from_id(r)};
    nmo_object_id_t keyed = group(&f, character, root, members, COUNT(members));
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0.5f));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(10, x(f.engine, high), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, low), 0);
    members[1] = nmo_ref_from_id(c);
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
    ASSERT_FLOAT_EQ(10, x(f.engine, added), 0);
    nmo_objectanimation_state_t *changed = fixture_state(&f, c);
    changed->entity = nmo_ref_from_id(root);
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
    /* Both root channels now tie. C precedes R in source order, so R writes last. */
    ASSERT_FLOAT_EQ(0.5f, x(f.engine, root), 0);
    changed->entity = nmo_ref_from_id(added);
    actor->floor_ref = nmo_ref_from_id(low);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(2, x(f.engine, low), 0);
    ASSERT_FLOAT_EQ(1, x(f.engine, root), 0);
    ASSERT_FLOAT_EQ(10, x(f.engine, added), 0);
    fixture_end(&f);
}

TEST(engine_animation_lod, ignored_selected_entities_keep_their_pose_and_character_lod_copies_independently)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
    nmo_character_state_t *actor = fixture_state(&f, character);
    actor->base.moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
    nmo_object_id_t root = entity(&f, "Root", character), low = entity(&f, "Low", character), high = entity(&f, "Ignored", character);
    nmo_3dentity_state_t *ignored = fixture_state(&f, high);
    ignored->moveable_flags |= CK_3DENTITY_IGNOREANIMATION;
    ignored->world_matrix[12] = 7;
    nmo_object_id_t a = curve(&f, "Low Curve", low, 0,2), b = curve(&f, "High Curve", high, 0,20);
    nmo_object_id_t r = curve(&f, "Root Curve", root, 0,1);
    nmo_ref_t members[] = {nmo_ref_from_id(a),nmo_ref_from_id(b),nmo_ref_from_id(r)};
    nmo_object_id_t keyed = group(&f, character, root, members, COUNT(members));
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0.5f));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(1, x(f.engine, root), 0);
    ASSERT_FLOAT_EQ(7, x(f.engine, high), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, low), 0);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, -1));
    ASSERT_FALSE(eng_character_set_animation_lod(f.engine, character, INFINITY));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
    ASSERT_FLOAT_EQ(0.5f, x(f.engine, root), 0);
    ASSERT_FLOAT_EQ(7, x(f.engine, high), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, low), 0);
    nmo_object_id_t copy = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_copy_objects(f.engine, &character, 1, false, &copy));
    ASSERT_TRUE(copy != 0 && copy != character);
    ASSERT_FLOAT_EQ(-1, eng_character_animation_lod(f.engine, copy), 0);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, copy, 0.75f));
    ASSERT_FLOAT_EQ(-1, eng_character_animation_lod(f.engine, character), 0);
    nmo_engine_t *other = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_create(f.workspace, NULL, &other));
    ASSERT_FLOAT_EQ(1, eng_character_animation_lod(other, character), 0);
    nmo_engine_destroy(other);
    fixture_end(&f);
}

TEST(engine_animation_lod, rendering_feeds_next_frame_lod_and_hierarchy_hide_stops_visibility_feedback)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = entity(&f, "Parent", 0);
    nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
    nmo_character_state_t *actor = fixture_state(&f, character);
    actor->base.parent = nmo_ref_from_id(parent);
    nmo_object_id_t root = entity(&f, "Root", character), limb = entity(&f, "Limb", character);
    nmo_object_id_t r = curve(&f, "Root Curve", root, 0,1), l = curve(&f, "Limb Curve", limb, 0,20);
    nmo_ref_t members[] = {nmo_ref_from_id(l),nmo_ref_from_id(r)};
    nmo_object_id_t keyed = group(&f, character, root, members, COUNT(members));
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(0, x(f.engine, limb), 0);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
    ASSERT_FLOAT_EQ(10, x(f.engine, limb), 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1)); /* No renderer since processing consumed visibility. */
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(10, x(f.engine, limb), 0);
    eng_show(f.engine, character, 2); /* Hierarchy hide. */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(10, x(f.engine, limb), 0);
    eng_show(f.engine, character, 0); /* A plain Hide still traverses its hierarchy. */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.75f));
    ASSERT_FLOAT_EQ(15, x(f.engine, limb), 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 1));
    eng_show(f.engine, parent, 2);
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(15, x(f.engine, limb), 0);
    eng_show(f.engine, parent, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(20, x(f.engine, limb), 0);
    ASSERT_EQ(0u, actor->base.moveable_flags & VX_MOVEABLE_CHARACTERRENDERED);
    fixture_end(&f);
}

TEST(engine_animation_lod, deleted_channels_are_pruned_before_counting_the_lod_prefix)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t character = fixture_create(&f, NMO_CID_CHARACTER, "Character");
    nmo_character_state_t *actor = fixture_state(&f, character);
    actor->base.moveable_flags |= VX_MOVEABLE_CHARACTERRENDERED;
    nmo_object_id_t root = entity(&f, "Root", character), low = entity(&f, "Low", character);
    nmo_object_id_t medium = entity(&f, "Medium", character), high = entity(&f, "High", character);
    nmo_object_id_t r = curve(&f, "Root Curve", root, 0,1), a = curve(&f, "Low Curve", low, 0,2);
    nmo_object_id_t b = curve(&f, "Medium Curve", medium, 0,4), c = curve(&f, "High Curve", high, 0,6);
    nmo_ref_t members[] = {nmo_ref_from_id(a),nmo_ref_from_id(b),nmo_ref_from_id(c),nmo_ref_from_id(r)};
    nmo_object_id_t keyed = group(&f, character, root, members, COUNT(members));
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_character_set_animation_lod(f.engine, character, 0.75f));
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0)); /* Populate the cached order. */
    nmo_engine_destroy_object(f.engine, c);
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 1));
    ASSERT_FLOAT_EQ(1, x(f.engine, root), 0);
    ASSERT_FLOAT_EQ(4, x(f.engine, medium), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, low), 0);
    ASSERT_FLOAT_EQ(0, x(f.engine, high), 0);
    ASSERT_EQ(4u, ((const nmo_keyedanimation_state_t *)fixture_state(&f, keyed))->animation_count);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_animation_lod, contribution_reads_all_vector_layouts_and_ignores_tangents_and_axis_keys);
REGISTER_TEST(engine_animation_lod, contribution_preserves_wide_cancellation_and_small_raw_quaternion_motion);
REGISTER_TEST(engine_animation_lod, merged_contribution_averages_sources_independently_of_weight_and_guards_cycles);
REGISTER_TEST(engine_animation_lod, invisible_characters_update_the_root_and_optional_floor_prefix);
REGISTER_TEST(engine_animation_lod, lod_truncates_the_wide_product_and_full_lod_updates_remaining_channels);
REGISTER_TEST(engine_animation_lod, mixed_channels_sort_stably_ahead_of_unbound_and_empty_slots);
REGISTER_TEST(engine_animation_lod, membership_target_and_floor_changes_rebuild_the_private_order);
REGISTER_TEST(engine_animation_lod, ignored_selected_entities_keep_their_pose_and_character_lod_copies_independently);
REGISTER_TEST(engine_animation_lod, rendering_feeds_next_frame_lod_and_hierarchy_hide_stops_visibility_feedback);
REGISTER_TEST(engine_animation_lod, deleted_channels_are_pruned_before_counting_the_lod_prefix);
TEST_MAIN_END()
