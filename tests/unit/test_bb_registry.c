/**
 * @file test_bb_registry.c
 * @brief Unit tests for BB prototype registry
 */

#include "../test_framework.h"
#include "behavior/nmo_behavior_registry.h"
#include "session/nmo_context.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#include <string.h>

/* === Pure dynamic: starts empty without data_dir === */

TEST(bb_reg, empty_without_data) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_behavior_registry_t *reg = nmo_context_get_bb_registry(ctx);
    ASSERT_TRUE(reg != NULL);
    ASSERT_EQ(nmo_behavior_registry_count(reg), 0u);
    nmo_context_release(ctx);
}

/* === Loaded from JSON via data_dir === */

TEST(bb_reg, loaded_from_data_dir) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = "data";
    nmo_context_t *ctx = nmo_context_create(&desc);
    nmo_behavior_registry_t *reg = nmo_context_get_bb_registry(ctx);

    ASSERT_TRUE(nmo_behavior_registry_count(reg) > 400);

    /* "Rotate" = (0xFFFFFFEE, 0xEEFFFFFF) */
    const nmo_behavior_proto_t *p = nmo_behavior_registry_find(reg, nmo_guid_create(0xFFFFFFEE, 0xEEFFFFFF));
    ASSERT_TRUE(p != NULL);
    ASSERT_STR_EQ(p->name, "Rotate");
    ASSERT_TRUE(p->input_count >= 1);
    ASSERT_STR_EQ(p->inputs[0], "In");
    ASSERT_TRUE(p->description != NULL);

    nmo_context_release(ctx);
}

TEST(bb_reg, loads_extended_utf8_data_file) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = "data";
    nmo_context_t *ctx = nmo_context_create(&desc);
    nmo_behavior_registry_t *reg = nmo_context_get_bb_registry(ctx);

    const nmo_behavior_proto_t *p = nmo_behavior_registry_find(
        reg,
        nmo_guid_create(0x506B40F7, 0x30852E46));
    ASSERT_TRUE(p != NULL);
    ASSERT_STR_EQ(p->name, "Point Particle System");
    ASSERT_STR_EQ(p->dll, "TT_ParticleSystems_RT.dll");

    p = nmo_behavior_registry_find(
        reg,
        nmo_guid_create(0x50EB3A17, 0x015C5BF9));
    ASSERT_TRUE(p != NULL);
    ASSERT_STR_EQ(p->description, "Get Curve Point");

    nmo_context_release(ctx);
}

/* === Dynamic add/find/remove === */

TEST(bb_reg, add_and_find) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_behavior_registry_t *reg = nmo_behavior_registry_create(arena);

    nmo_guid_t guid = nmo_guid_create(0xAAAA0001, 0xBBBB0001);
    const char *inputs[] = {"In", "Reset"};
    nmo_behavior_param_desc_t in_params[] = {
        {"Position", nmo_guid_create(0x48824eae, 0x2fe47960)},
    };

    nmo_behavior_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Custom BB";
    proto.description = "Test description";
    proto.inputs = inputs;
    proto.input_count = 2;
    proto.input_params = in_params;
    proto.input_param_count = 1;

    ASSERT_EQ(nmo_behavior_registry_add(reg, &proto), NMO_OK);

    const nmo_behavior_proto_t *found = nmo_behavior_registry_find(reg, guid);
    ASSERT_TRUE(found != NULL);
    ASSERT_STR_EQ(found->name, "Custom BB");
    ASSERT_EQ(found->input_count, 2u);
    ASSERT_STR_EQ(found->inputs[0], "In");

    nmo_behavior_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(bb_reg, rejects_nonzero_count_with_null_array) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_behavior_registry_t *reg = nmo_behavior_registry_create(arena);

    nmo_behavior_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = nmo_guid_create(0xAAAA0003, 0xBBBB0003);
    proto.name = "Bad BB";
    proto.input_count = 1;
    proto.inputs = NULL;

    ASSERT_NE(NMO_OK, nmo_behavior_registry_add(reg, &proto));
    ASSERT_TRUE(nmo_behavior_registry_find(reg, proto.guid) == NULL);

    nmo_behavior_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(bb_reg, remove_entry) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_behavior_registry_t *reg = nmo_behavior_registry_create(arena);

    nmo_guid_t guid = nmo_guid_create(0xAAAA0002, 0xBBBB0002);
    nmo_behavior_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Temp BB";
    nmo_behavior_registry_add(reg, &proto);

    ASSERT_TRUE(nmo_behavior_registry_remove(reg, guid));
    ASSERT_TRUE(nmo_behavior_registry_find(reg, guid) == NULL);

    nmo_behavior_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(bb_reg, empty_without_data);
    REGISTER_TEST(bb_reg, loaded_from_data_dir);
    REGISTER_TEST(bb_reg, loads_extended_utf8_data_file);
    REGISTER_TEST(bb_reg, add_and_find);
    REGISTER_TEST(bb_reg, rejects_nonzero_count_with_null_array);
    REGISTER_TEST(bb_reg, remove_entry);
TEST_MAIN_END()
