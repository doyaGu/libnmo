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
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, cube, "assets/cube.obj"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_color(plan, cube, 1.0f, 0.0f, 0.0f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_texture(plan, cube, "assets/cube.png"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_position(plan, cube, 1.0f, 2.0f, 3.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_rotation_euler_deg(plan, cube, 10.0f, 20.0f, 30.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_scale(plan, cube, 2.0f, 3.0f, 4.0f));

    ASSERT_EQ(1u, nmo_project_plan_asset_count(plan));
    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(plan, 0u, &asset));
    ASSERT_EQ(cube, asset.object_handle);
    ASSERT_TRUE(asset.has_primitive_mesh);
    ASSERT_EQ(NMO_PRIMITIVE_CUBE, asset.primitive_mesh);
    ASSERT_TRUE(asset.has_external_mesh);
    ASSERT_STR_EQ("assets/cube.obj", asset.external_mesh_path);
    ASSERT_TRUE(asset.has_material_color);
    ASSERT_FLOAT_EQ(1.0f, asset.material_color[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, asset.material_color[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, asset.material_color[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, asset.material_color[3], 0.0001f);
    ASSERT_TRUE(asset.has_material_texture);
    ASSERT_STR_EQ("assets/cube.png", asset.material_texture_path);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &object));
    ASSERT_TRUE(object.has_position);
    ASSERT_FLOAT_EQ(1.0f, object.position[0], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, object.position[1], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, object.position[2], 0.0001f);
    ASSERT_TRUE(object.has_rotation_euler_deg);
    ASSERT_FLOAT_EQ(10.0f, object.rotation_euler_deg[0], 0.0001f);
    ASSERT_FLOAT_EQ(20.0f, object.rotation_euler_deg[1], 0.0001f);
    ASSERT_FLOAT_EQ(30.0f, object.rotation_euler_deg[2], 0.0001f);
    ASSERT_TRUE(object.has_scale);
    ASSERT_FLOAT_EQ(2.0f, object.scale[0], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, object.scale[1], 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, object.scale[2], 0.0001f);

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
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, cube, "assets/clone.obj"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_color(plan, cube, 0.25f, 0.5f, 0.75f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_texture(plan, cube, "assets/clone.png"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_position(plan, cube, -1.0f, 4.0f, 8.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_rotation_euler_deg(plan, cube, 45.0f, 0.0f, 90.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_scale(plan, cube, 0.5f, 1.5f, 2.5f));

    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_EQ(1u, nmo_project_plan_asset_count(clone));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(clone, 0u, &asset));
    ASSERT_EQ(cube, asset.object_handle);
    ASSERT_EQ(NMO_PRIMITIVE_CUBE, asset.primitive_mesh);
    ASSERT_TRUE(asset.has_external_mesh);
    ASSERT_STR_EQ("assets/clone.obj", asset.external_mesh_path);
    ASSERT_FLOAT_EQ(0.25f, asset.material_color[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, asset.material_color[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.75f, asset.material_color[2], 0.0001f);
    ASSERT_TRUE(asset.has_material_texture);
    ASSERT_STR_EQ("assets/clone.png", asset.material_texture_path);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(clone, 0u, &object));
    ASSERT_TRUE(object.has_position);
    ASSERT_FLOAT_EQ(-1.0f, object.position[0], 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, object.position[1], 0.0001f);
    ASSERT_FLOAT_EQ(8.0f, object.position[2], 0.0001f);
    ASSERT_TRUE(object.has_rotation_euler_deg);
    ASSERT_FLOAT_EQ(45.0f, object.rotation_euler_deg[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, object.rotation_euler_deg[1], 0.0001f);
    ASSERT_FLOAT_EQ(90.0f, object.rotation_euler_deg[2], 0.0001f);
    ASSERT_TRUE(object.has_scale);
    ASSERT_FLOAT_EQ(0.5f, object.scale[0], 0.0001f);
    ASSERT_FLOAT_EQ(1.5f, object.scale[1], 0.0001f);
    ASSERT_FLOAT_EQ(2.5f, object.scale[2], 0.0001f);

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(asset_authoring, stores_primitive_mesh_and_material_specs);
REGISTER_TEST(asset_authoring, clones_asset_specs);
TEST_MAIN_END()
