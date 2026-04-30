#include "test_framework.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"

TEST(asset_authoring, stores_primitive_mesh_and_material_specs) {
    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &cube));

    ASSERT_EQ(NMO_OK, nmo_project_plan_set_primitive_mesh(plan, cube, NMO_PRIMITIVE_CUBE));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_color(plan, cube, 1.0f, 0.0f, 0.0f, 1.0f));

    ASSERT_EQ(1u, nmo_project_plan_asset_count(plan));
    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(plan, 0u, &asset));
    ASSERT_EQ(cube, asset.object_handle);
    ASSERT_TRUE(asset.has_primitive_mesh);
    ASSERT_EQ(NMO_PRIMITIVE_CUBE, asset.primitive_mesh);
    ASSERT_TRUE(asset.has_material_color);
    ASSERT_FLOAT_EQ(1.0f, asset.material_color[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, asset.material_color[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, asset.material_color[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, asset.material_color[3], 0.0001f);

    nmo_project_plan_destroy(plan);
}

TEST(asset_authoring, clones_asset_specs) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                  },
                  &cube));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_primitive_mesh(plan, cube, NMO_PRIMITIVE_CUBE));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_color(plan, cube, 0.25f, 0.5f, 0.75f, 1.0f));

    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_EQ(1u, nmo_project_plan_asset_count(clone));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(clone, 0u, &asset));
    ASSERT_EQ(cube, asset.object_handle);
    ASSERT_EQ(NMO_PRIMITIVE_CUBE, asset.primitive_mesh);
    ASSERT_FLOAT_EQ(0.25f, asset.material_color[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, asset.material_color[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.75f, asset.material_color[2], 0.0001f);

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(asset_authoring, stores_primitive_mesh_and_material_specs);
REGISTER_TEST(asset_authoring, clones_asset_specs);
TEST_MAIN_END()
