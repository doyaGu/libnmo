/**
 * @file test_object_state_layout.c
 * @brief Layout-driven lifecycle, copy, equals and hash of object states
 */

#include "../test_framework.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "object/nmo_ref.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include <string.h>

TEST(object_state_layout, place_copy_equals_hash) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_place_state_t source;
    nmo_place_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(sizeof(nmo_place_portal_entry_t), source.portals.element_size);
    ASSERT_EQ(sizeof(nmo_ref_t), source.references.element_size);

    source.has_camera = 1;
    source.camera = nmo_ref_from_raw(12);
    source.has_portals = 1;
    const nmo_place_portal_entry_t portal = {
        .place = nmo_ref_from_raw(20),
        .portal = nmo_ref_from_raw(21),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.portals, &portal));
    const nmo_ref_t reference = nmo_ref_from_raw(30);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.references, &reference));

    ASSERT_FALSE(nmo_place_vtable.equals(&source, &copied));
    ASSERT_EQ(NMO_OK, nmo_place_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(nmo_place_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_place_vtable.hash(&source), nmo_place_vtable.hash(&copied));
    ASSERT_EQ(1u, copied.portals.count);
    ASSERT_TRUE(copied.portals.data != source.portals.data);
    ASSERT_EQ(12u, copied.camera.raw_id);

    nmo_place_portal_entry_t *entry =
        NMO_ARRAY_DATA(nmo_place_portal_entry_t, &copied.portals);
    entry->portal = nmo_ref_from_raw(22);
    ASSERT_FALSE(nmo_place_vtable.equals(&source, &copied));

    nmo_place_vtable.destroy(&source, NULL, NULL);
    nmo_place_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, copy_into_shallow_alias_detaches_arrays) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_group_state_t source;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&source, NULL, NULL));
    const nmo_ref_t member = nmo_ref_from_raw(40);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.object_ids, &member));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.base.scripts, &member));
    source.has_group_data = 1;

    nmo_group_state_t alias = source;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.copy(&source, &alias, NULL, arena));
    ASSERT_TRUE(alias.object_ids.data != source.object_ids.data);
    ASSERT_TRUE(alias.base.scripts.data != source.base.scripts.data);
    ASSERT_TRUE(nmo_group_vtable.equals(&source, &alias));
    ASSERT_EQ(nmo_group_vtable.hash(&source), nmo_group_vtable.hash(&alias));

    nmo_group_vtable.destroy(&alias, NULL, NULL);
    ASSERT_EQ(1u, source.object_ids.count);
    ASSERT_EQ(40u, NMO_ARRAY_DATA(nmo_ref_t, &source.object_ids)[0].raw_id);
    nmo_group_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, layer_defaults_and_square_data_copy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_layer_state_t source;
    nmo_layer_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(1, source.type);
    ASSERT_EQ(1u, source.flags);
    ASSERT_EQ(1u, source.has_layer_data);

    uint8_t square[4] = {1, 2, 3, 4};
    source.has_square_data = 1;
    source.square_data = square;
    source.square_data_size = sizeof(square);
    source.color_rgba = 0x11223344u;

    ASSERT_EQ(NMO_OK, nmo_layer_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(copied.square_data != source.square_data);
    ASSERT_EQ(sizeof(square), copied.square_data_size);
    ASSERT_EQ(0, memcmp(copied.square_data, square, sizeof(square)));
    ASSERT_EQ(0x11223344u, copied.color_rgba);

    source.square_data = NULL;
    source.square_data_size = 1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_layer_vtable.copy(&source, &copied, NULL, arena));

    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_layer_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_state_layout, place_copy_equals_hash);
    REGISTER_TEST(object_state_layout, copy_into_shallow_alias_detaches_arrays);
    REGISTER_TEST(object_state_layout, layer_defaults_and_square_data_copy);
TEST_MAIN_END()
