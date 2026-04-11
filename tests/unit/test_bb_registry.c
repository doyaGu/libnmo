/**
 * @file test_bb_registry.c
 * @brief Unit tests for BB prototype registry
 */

#include "../test_framework.h"
#include "behavior/nmo_bb_registry.h"
#include "session/nmo_context.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#include <string.h>

/* === Pure dynamic: starts empty without data_dir === */

TEST(bb_reg, empty_without_data) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_bb_registry_t *reg = nmo_context_get_bb_registry(ctx);
    ASSERT_TRUE(reg != NULL);
    ASSERT_EQ(nmo_bb_registry_count(reg), 0u);
    nmo_context_release(ctx);
}

/* === Loaded from JSON via data_dir === */

TEST(bb_reg, loaded_from_data_dir) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = "data";
    nmo_context_t *ctx = nmo_context_create(&desc);
    nmo_bb_registry_t *reg = nmo_context_get_bb_registry(ctx);

    ASSERT_TRUE(nmo_bb_registry_count(reg) > 400);

    /* "Rotate" = (0xFFFFFFEE, 0xEEFFFFFF) */
    const nmo_bb_proto_t *p = nmo_bb_registry_find(reg, nmo_guid_create(0xFFFFFFEE, 0xEEFFFFFF));
    ASSERT_TRUE(p != NULL);
    ASSERT_STR_EQ(p->name, "Rotate");
    ASSERT_TRUE(p->input_count >= 1);
    ASSERT_STR_EQ(p->inputs[0], "In");
    ASSERT_TRUE(p->description != NULL);

    nmo_context_release(ctx);
}

/* === Dynamic add/find/remove === */

TEST(bb_reg, add_and_find) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    nmo_guid_t guid = nmo_guid_create(0xAAAA0001, 0xBBBB0001);
    const char *inputs[] = {"In", "Reset"};
    nmo_bb_param_desc_t in_params[] = {
        {"Position", nmo_guid_create(0x48824eae, 0x2fe47960)},
    };

    nmo_bb_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Custom BB";
    proto.description = "Test description";
    proto.inputs = inputs;
    proto.input_count = 2;
    proto.input_params = in_params;
    proto.input_param_count = 1;

    ASSERT_EQ(nmo_bb_registry_add(reg, &proto), NMO_OK);

    const nmo_bb_proto_t *found = nmo_bb_registry_find(reg, guid);
    ASSERT_TRUE(found != NULL);
    ASSERT_STR_EQ(found->name, "Custom BB");
    ASSERT_EQ(found->input_count, 2u);
    ASSERT_STR_EQ(found->inputs[0], "In");

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(bb_reg, remove_entry) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    nmo_guid_t guid = nmo_guid_create(0xAAAA0002, 0xBBBB0002);
    nmo_bb_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Temp BB";
    nmo_bb_registry_add(reg, &proto);

    ASSERT_TRUE(nmo_bb_registry_remove(reg, guid));
    ASSERT_TRUE(nmo_bb_registry_find(reg, guid) == NULL);

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(bb_reg, empty_without_data);
    REGISTER_TEST(bb_reg, loaded_from_data_dir);
    REGISTER_TEST(bb_reg, add_and_find);
    REGISTER_TEST(bb_reg, remove_entry);
TEST_MAIN_END()
