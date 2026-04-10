/**
 * @file test_bb_registry.c
 * @brief Unit tests for BB prototype registry
 */

#include "../test_framework.h"
#include "app/nmo_bb_registry.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#include <string.h>

/* === Builtin lookups === */

TEST(bb_reg, builtin_count)
{
    ASSERT_TRUE(nmo_bb_registry_builtin_count(NULL) > 500);
}

TEST(bb_reg, find_rotate)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);
    ASSERT_TRUE(reg != NULL);

    /* Rotate = (0xFFFFFFEE, 0xEEFFFFFF) */
    nmo_guid_t guid = nmo_guid_create(0xFFFFFFEE, 0xEEFFFFFF);
    const nmo_bb_proto_t *proto = nmo_bb_registry_find(reg, guid);
    ASSERT_TRUE(proto != NULL);
    ASSERT_STR_EQ(proto->name, "Rotate");
    ASSERT_TRUE(proto->dll != NULL);

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(bb_reg, get_name_shortcut)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    /* "Set Position" from TT_Toolbox_RT */
    nmo_guid_t guid = nmo_guid_create(0xE456E78A, 0x456789AA);
    const char *name = nmo_bb_registry_get_name(reg, guid);
    ASSERT_TRUE(name != NULL);
    ASSERT_STR_EQ(name, "Set Position");

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(bb_reg, not_found)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    nmo_guid_t guid = nmo_guid_create(0xDEADDEAD, 0xBEEFBEEF);
    ASSERT_TRUE(nmo_bb_registry_find(reg, guid) == NULL);
    ASSERT_TRUE(nmo_bb_registry_get_name(reg, guid) == NULL);

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

/* === Dynamic add === */

TEST(bb_reg, add_and_find)
{
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
    proto.category = "Test/Custom";
    proto.dll = "test.dll";
    proto.inputs = inputs;
    proto.input_count = 2;
    proto.input_params = in_params;
    proto.input_param_count = 1;

    ASSERT_EQ(nmo_bb_registry_add(reg, &proto), NMO_OK);

    const nmo_bb_proto_t *found = nmo_bb_registry_find(reg, guid);
    ASSERT_TRUE(found != NULL);
    ASSERT_STR_EQ(found->name, "Custom BB");
    ASSERT_STR_EQ(found->description, "Test description");
    ASSERT_EQ(found->input_count, 2u);
    ASSERT_STR_EQ(found->inputs[0], "In");
    ASSERT_STR_EQ(found->inputs[1], "Reset");
    ASSERT_EQ(found->input_param_count, 1u);
    ASSERT_STR_EQ(found->input_params[0].name, "Position");

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST(bb_reg, add_overrides_builtin)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    /* Override "Rotate" */
    nmo_guid_t guid = nmo_guid_create(0xFFFFFFEE, 0xEEFFFFFF);
    nmo_bb_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Custom Rotate";

    ASSERT_EQ(nmo_bb_registry_add(reg, &proto), NMO_OK);

    const char *name = nmo_bb_registry_get_name(reg, guid);
    ASSERT_STR_EQ(name, "Custom Rotate");

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

/* === Remove === */

TEST(bb_reg, remove_dynamic)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    nmo_guid_t guid = nmo_guid_create(0xAAAA0002, 0xBBBB0002);
    nmo_bb_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Temp BB";
    nmo_bb_registry_add(reg, &proto);

    ASSERT_TRUE(nmo_bb_registry_find(reg, guid) != NULL);
    ASSERT_TRUE(nmo_bb_registry_remove(reg, guid));
    ASSERT_TRUE(nmo_bb_registry_find(reg, guid) == NULL);
    ASSERT_FALSE(nmo_bb_registry_remove(reg, guid)); /* double remove */

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

/* === Count === */

TEST(bb_reg, count_tracks_dynamic)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_bb_registry_t *reg = nmo_bb_registry_create(arena);

    size_t base = nmo_bb_registry_count(reg);

    nmo_guid_t guid = nmo_guid_create(0xAAAA0003, 0xBBBB0003);
    nmo_bb_proto_t proto;
    memset(&proto, 0, sizeof(proto));
    proto.guid = guid;
    proto.name = "Extra";
    nmo_bb_registry_add(reg, &proto);

    ASSERT_EQ(nmo_bb_registry_count(reg), base + 1);

    nmo_bb_registry_remove(reg, guid);
    ASSERT_EQ(nmo_bb_registry_count(reg), base);

    nmo_bb_registry_destroy(reg);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(bb_reg, builtin_count);
    REGISTER_TEST(bb_reg, find_rotate);
    REGISTER_TEST(bb_reg, get_name_shortcut);
    REGISTER_TEST(bb_reg, not_found);
    REGISTER_TEST(bb_reg, add_and_find);
    REGISTER_TEST(bb_reg, add_overrides_builtin);
    REGISTER_TEST(bb_reg, remove_dynamic);
    REGISTER_TEST(bb_reg, count_tracks_dynamic);
TEST_MAIN_END()
