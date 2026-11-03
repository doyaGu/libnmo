#include "engine_fixture.h"
#include "../../src/engine/engine_animation.h"
#include "../../src/engine/engine_transform.h"
#include "object/builtin/nmo_3dentity_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define POSITION 0x637c4301u
#define ROTATION 0x49ed4002u

static nmo_object_id_t part(engine_fixture_t *f, const char *name, nmo_object_id_t entity,
                            float *keys, uint32_t bytes)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_OBJECTANIMATION, name);
    nmo_objectanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    state->format = CKOBJANIM_FORMAT_CONTROLLERS;
    state->has_length = 1;
    state->length = 8;
    state->entity = nmo_ref_from_id(entity);
    if (bytes != 0) {
        state->controllers = nmo_arena_alloc(nmo_session_get_arena(f->session), sizeof(*state->controllers), _Alignof(nmo_objanim_controller_t));
        if (state->controllers == NULL) return 0;
        state->controller_count = 1;
        *state->controllers = (nmo_objanim_controller_t){POSITION,bytes/16,bytes,keys};
    }
    return id;
}

static nmo_object_id_t group(engine_fixture_t *f, const char *name, nmo_ref_t *parts,
                             uint32_t count, nmo_object_id_t root)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_KEYEDANIMATION, name);
    nmo_keyedanimation_state_t *state = fixture_state(f, id);
    if (state == NULL) return 0;
    state->animation_count = count;
    state->animation_ids = parts;
    state->base.has_root_entity = root != 0;
    state->base.root_entity = nmo_ref_from_id(root);
    return id;
}

static void offset(engine_fixture_t *f, nmo_object_id_t animation, float x, float y, float z)
{
    nmo_objectanimation_state_t *state = fixture_state(f, animation);
    ASSERT_NOT_NULL(state);
    state->has_root_pos = 1;
    state->root_pos = (nmo_vector_t){x,y,z};
}

static void position(const nmo_engine_t *engine, nmo_object_id_t entity, float x, float y, float z)
{
    float matrix[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(engine, entity, matrix));
    ASSERT_FLOAT_EQ(x, matrix[12], 0.00001f);
    ASSERT_FLOAT_EQ(y, matrix[13], 0.00001f);
    ASSERT_FLOAT_EQ(z, matrix[14], 0.00001f);
}

TEST(engine_keyed, saved_vectors_transfer_once_in_list_order_and_only_offset_the_root)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_3dentity_state_t *other_state = fixture_state(&f, other);
    other_state->parent = nmo_ref_from_id(f.owner_id);
    float root_keys[] = {0,0,1,2, 8,8,9,10}, other_keys[] = {0,10,20,30};
    float saved[COUNT(root_keys)];
    memcpy(saved, root_keys, sizeof(saved));
    nmo_object_id_t root = part(&f, "Root", f.owner_id, root_keys, sizeof(root_keys));
    nmo_object_id_t child = part(&f, "Child", other, other_keys, sizeof(other_keys));
    offset(&f, root, -1,2,-3);
    offset(&f, child, -2,3,-4);
    nmo_ref_t parts[] = {nmo_ref_from_id(root),nmo_ref_from_id(child),nmo_ref_from_id(root),nmo_ref_from_id(0)};
    nmo_object_id_t keyed = group(&f, "Global", parts, COUNT(parts), f.owner_id);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    /* Loading attaches the channels; root selection remains lazy. */
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, root, 0.5f));
    position(f.engine, f.owner_id, 4,5,6);
    for (int repeat = 0; repeat < 2; repeat++) {
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
        position(f.engine, f.owner_id, 2,8,2);
        position(f.engine, other, 12,28,32);
    }
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, root, 0.25f));
    position(f.engine, f.owner_id, 0,6,0);
    ASSERT_EQ(0, memcmp(saved, root_keys, sizeof(saved)));
    const nmo_objectanimation_state_t *source = fixture_state(&f, child);
    ASSERT_FLOAT_EQ(-2, source->root_pos.x, 0);
    /* A second engine transfers its own copy of the saved pending vectors. */
    nmo_engine_t *second = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_create(f.workspace, NULL, &second));
    ASSERT_TRUE(eng_animation_set_step(second, keyed, 0.5f));
    position(second, f.owner_id, 2,8,2);
    nmo_engine_destroy(second);
    fixture_end(&f);
}

TEST(engine_keyed, channels_shared_by_two_groups_transfer_saved_offsets_only_once)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    float keys[] = {0,0,0,0, 8,8,0,0};
    nmo_object_id_t root = part(&f, "Root", f.owner_id, keys, sizeof(keys));
    offset(&f, root, -2,0,0);
    nmo_ref_t reference = nmo_ref_from_id(root);
    nmo_object_id_t a = group(&f, "A", &reference, 1, f.owner_id);
    nmo_object_id_t b = group(&f, "B", &reference, 1, f.owner_id);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_animation_set_step(f.engine, a, 0.5f));
    position(f.engine, f.owner_id, 2,0,0);
    ASSERT_TRUE(eng_animation_set_step(f.engine, b, 0.5f));
    position(f.engine, f.owner_id, 4,0,0);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, root, 0.5f));
    position(f.engine, f.owner_id, 4,0,0);
    ASSERT_TRUE(eng_animation_set_step(f.engine, a, 0.5f));
    position(f.engine, f.owner_id, 2,0,0);
    fixture_end(&f);
}

TEST(engine_keyed, lazy_roots_walk_past_empty_slots_to_standalone_or_character_ancestors)
{
    for (int character = 0; character < 2; character++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t actor = character ? fixture_create(&f, NMO_CID_CHARACTER, "Character") : 0;
        if (character) {
            nmo_3dentity_state_t *root_state = fixture_state(&f, f.owner_id);
            root_state->parent = nmo_ref_from_id(actor);
        }
        nmo_object_id_t leaf = fixture_create(&f, NMO_CID_3DENTITY, "Leaf");
        nmo_3dentity_state_t *leaf_state = fixture_state(&f, leaf);
        leaf_state->parent = nmo_ref_from_id(f.owner_id);
        float root_keys[] = {0,0,0,0, 8,8,0,0}, leaf_keys[] = {0,2,0,0};
        nmo_object_id_t r = part(&f, "Root", f.owner_id, root_keys, sizeof(root_keys));
        nmo_object_id_t l = part(&f, "Leaf Animation", leaf, leaf_keys, sizeof(leaf_keys));
        nmo_object_id_t unbound = part(&f, "Unbound", 0, NULL, 0);
        offset(&f, r, -1,0,0);
        nmo_ref_t parts[] = {nmo_ref_from_id(0),nmo_ref_from_id(unbound),nmo_ref_from_id(l),nmo_ref_from_id(r)};
        nmo_object_id_t keyed = group(&f, "Global", parts, COUNT(parts), 0);
        nmo_keyedanimation_state_t *state = fixture_state(&f, keyed);
        state->base.has_character = character;
        state->base.character = nmo_ref_from_id(actor);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        if (character) {
            const float matrix[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 100,0,0,1};
            ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, actor, matrix, false));
        }
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
        position(f.engine, f.owner_id, character ? 103 : 3,0,0);
        position(f.engine, leaf, character ? 105 : 5,0,0);
        fixture_end(&f);
    }
}

TEST(engine_keyed, an_offset_does_not_create_a_missing_or_ignored_position_channel)
{
    for (int ignored = 0; ignored < 2; ignored++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        float keys[] = {0,20,30,40}, rotation[] = {0,0,0,0,1};
        nmo_object_id_t root = part(&f, "Root", f.owner_id, keys, sizeof(keys));
        nmo_objectanimation_state_t *state = fixture_state(&f, root);
        nmo_objanim_controller_t controllers[] = {*state->controllers,{ROTATION,1,sizeof(rotation),rotation}};
        state->controllers = ignored ? controllers : controllers+1;
        state->controller_count = ignored ? 2 : 1;
        state->flags = ignored ? CK_OBJECTANIMATION_IGNOREPOS : 0;
        offset(&f, root, -2,3,-4);
        nmo_ref_t reference = nmo_ref_from_id(root);
        nmo_object_id_t keyed = group(&f, "Global", &reference, 1, f.owner_id);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        const float matrix[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1};
        ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, matrix, false));
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
        position(f.engine, f.owner_id, 7,8,9);
        fixture_end(&f);
    }
}

TEST(engine_keyed, centering_samples_absolute_frames_without_moving_entities_or_changing_keys)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    nmo_3dentity_state_t *child_state = fixture_state(&f, child);
    child_state->parent = nmo_ref_from_id(f.owner_id);
    float keys[] = {0,0,0,0, 8,8,16,24}, leaf_keys[] = {0,2,0,0};
    float saved[COUNT(keys)];
    memcpy(saved, keys, sizeof(saved));
    nmo_object_id_t root = part(&f, "Root", f.owner_id, keys, sizeof(keys));
    nmo_object_id_t leaf = part(&f, "Leaf", child, leaf_keys, sizeof(leaf_keys));
    offset(&f, root, -20,30,-40);
    nmo_ref_t parts[] = {nmo_ref_from_id(leaf),nmo_ref_from_id(root)};
    nmo_object_id_t keyed = group(&f, "Global", parts, COUNT(parts), 0);
    nmo_keyedanimation_state_t *state = fixture_state(&f, keyed);
    state->base.has_length = 1;
    state->base.length = 200; /* Center's argument is the root channel's frame. */
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2));
    position(f.engine, f.owner_id, 0,0,0);
    ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
    position(f.engine, f.owner_id, 2,4,6);
    position(f.engine, child, 4,4,6);
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 4));
    position(f.engine, f.owner_id, 2,4,6);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, root, 1));
    position(f.engine, f.owner_id, 4,8,12);
    ASSERT_FALSE(eng_keyed_animation_center(f.engine, keyed, NAN));
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, root, 1));
    position(f.engine, f.owner_id, 4,8,12);
    ASSERT_EQ(0, memcmp(saved, keys, sizeof(saved)));
    const nmo_objectanimation_state_t *source = fixture_state(&f, root);
    ASSERT_FLOAT_EQ(-20, source->root_pos.x, 0);
    /* Centering belongs to one engine; another starts from the saved vector. */
    nmo_engine_t *other = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_create(f.workspace, NULL, &other));
    ASSERT_TRUE(eng_animation_set_step(other, keyed, 0.5f));
    position(other, f.owner_id, -16,38,-28);
    nmo_engine_destroy(other);
    fixture_end(&f);
}

TEST(engine_keyed, centering_revalidates_membership_and_rebound_channels)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    float ka[] = {0,0,0,0, 8,8,0,0}, kb[] = {0,0,0,0, 8,24,0,0};
    nmo_object_id_t a = part(&f, "A", f.owner_id, ka, sizeof(ka));
    nmo_object_id_t b = part(&f, "B", f.owner_id, kb, sizeof(kb));
    nmo_ref_t parts[] = {nmo_ref_from_id(a),nmo_ref_from_id(b)};
    nmo_object_id_t keyed = group(&f, "Global", parts, COUNT(parts), f.owner_id);
    nmo_keyedanimation_state_t *state = fixture_state(&f, keyed);
    nmo_objectanimation_state_t *first = fixture_state(&f, a);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2));
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, a, 0.5f));
    position(f.engine, f.owner_id, 2,0,0);
    first->entity = nmo_ref_from_id(other);
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2));
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, b, 0.5f));
    position(f.engine, f.owner_id, 6,0,0);
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, a, 0.5f));
    position(f.engine, other, 4,0,0);
    state->animation_count = 1; /* Remove B from the group. */
    ASSERT_FALSE(eng_keyed_animation_center(f.engine, keyed, 2));
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, b, 0.5f));
    position(f.engine, f.owner_id, 12,0,0);
    first->entity = nmo_ref_from_id(f.owner_id);
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2));
    first->flags = CK_OBJECTANIMATION_IGNOREPOS;
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2)); /* Clears the previous offset. */
    first->flags = 0;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, a, 0.5f));
    position(f.engine, f.owner_id, 4,0,0);
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2));
    first->controller_count = 0;
    ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 2));
    first->controller_count = 1;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, a, 0.5f));
    position(f.engine, f.owner_id, 4,0,0);
    fixture_end(&f);
}

TEST(engine_keyed, centering_samples_shared_and_merged_root_positions_before_applying_offsets)
{
    for (int shared = 0; shared < 2; shared++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        float ka[] = {0,0,0,0, 8,8,0,0}, kb[] = {0,100,0,0, 20,120,0,0};
        nmo_object_id_t a = part(&f, "A", 0, ka, sizeof(ka));
        nmo_object_id_t b = part(&f, "B", 0, kb, sizeof(kb));
        nmo_objectanimation_state_t *second = fixture_state(&f, b);
        second->length = 20;
        nmo_object_id_t root = part(&f, "Root", f.owner_id, NULL, 0);
        nmo_objectanimation_state_t *blend = fixture_state(&f, root);
        blend->has_merge = 1;
        blend->flags = CK_OBJECTANIMATION_MERGED;
        blend->merge_factor = 0.25f;
        blend->length = 40;
        blend->anim1 = nmo_ref_from_id(a);
        blend->anim2 = nmo_ref_from_id(b);
        if (shared) {
            nmo_object_id_t owner = part(&f, "Shared Length", 0, NULL, 0);
            nmo_objectanimation_state_t *keys = fixture_state(&f, owner);
            keys->length = 40;
            keys->flags = CK_OBJECTANIMATION_IGNOREPOS;
            blend->format = CKOBJANIM_FORMAT_SHARED;
            blend->has_shared_anim = 1;
            blend->shared_anim = nmo_ref_from_id(owner);
            blend->length = 999;
        }
        nmo_ref_t reference = nmo_ref_from_id(root);
        nmo_object_id_t keyed = group(&f, "Global", &reference, 1, f.owner_id);
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_TRUE(eng_keyed_animation_center(f.engine, keyed, 10));
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.25f));
        position(f.engine, f.owner_id, 0,0,0);
        ASSERT_TRUE(eng_animation_set_step(f.engine, keyed, 0.5f));
        position(f.engine, f.owner_id, 2.75f,0,0);
        fixture_end(&f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_keyed, saved_vectors_transfer_once_in_list_order_and_only_offset_the_root);
REGISTER_TEST(engine_keyed, channels_shared_by_two_groups_transfer_saved_offsets_only_once);
REGISTER_TEST(engine_keyed, lazy_roots_walk_past_empty_slots_to_standalone_or_character_ancestors);
REGISTER_TEST(engine_keyed, an_offset_does_not_create_a_missing_or_ignored_position_channel);
REGISTER_TEST(engine_keyed, centering_samples_absolute_frames_without_moving_entities_or_changing_keys);
REGISTER_TEST(engine_keyed, centering_revalidates_membership_and_rebound_channels);
REGISTER_TEST(engine_keyed, centering_samples_shared_and_merged_root_positions_before_applying_offsets);
TEST_MAIN_END()
