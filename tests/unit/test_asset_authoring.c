#include "test_framework.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
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
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_ambient(plan, cube, 0.1f, 0.2f, 0.3f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_specular(plan, cube, 0.4f, 0.5f, 0.6f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_emissive(plan, cube, 0.7f, 0.8f, 0.9f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_specular_power(plan, cube, 12.5f));
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
    ASSERT_TRUE(asset.has_material_diffuse);
    ASSERT_FLOAT_EQ(1.0f, asset.material_diffuse[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, asset.material_diffuse[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, asset.material_diffuse[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, asset.material_diffuse[3], 0.0001f);
    ASSERT_TRUE(asset.has_material_ambient);
    ASSERT_FLOAT_EQ(0.1f, asset.material_ambient[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.2f, asset.material_ambient[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.3f, asset.material_ambient[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, asset.material_ambient[3], 0.0001f);
    ASSERT_TRUE(asset.has_material_specular);
    ASSERT_FLOAT_EQ(0.4f, asset.material_specular[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, asset.material_specular[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.6f, asset.material_specular[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, asset.material_specular[3], 0.0001f);
    ASSERT_TRUE(asset.has_material_emissive);
    ASSERT_FLOAT_EQ(0.7f, asset.material_emissive[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.8f, asset.material_emissive[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.9f, asset.material_emissive[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, asset.material_emissive[3], 0.0001f);
    ASSERT_TRUE(asset.has_material_specular_power);
    ASSERT_FLOAT_EQ(12.5f, asset.material_specular_power, 0.0001f);
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

TEST(asset_authoring, stores_material_texture_slots) {
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
                  },
                  &cube));

    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_texture_slot(
                          plan,
                          cube,
                          1u,
                          "assets/detail.png"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_texture(
                          plan,
                          cube,
                          "assets/base.png"));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_project_plan_set_material_texture_slot(
                  plan,
                  cube,
                  4u,
                  "assets/bad.png"));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(plan, 0u, &asset));
    ASSERT_TRUE(asset.has_material_texture);
    ASSERT_STR_EQ("assets/base.png", asset.material_texture_path);
    ASSERT_TRUE(asset.has_material_texture_slots[0]);
    ASSERT_TRUE(asset.has_material_texture_slots[1]);
    ASSERT_STR_EQ("assets/base.png", asset.material_texture_paths[0]);
    ASSERT_STR_EQ("assets/detail.png", asset.material_texture_paths[1]);
    ASSERT_FALSE(asset.has_material_texture_slots[2]);
    ASSERT_FALSE(asset.has_material_texture_slots[3]);

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
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_ambient(plan, cube, 0.05f, 0.10f, 0.15f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_specular(plan, cube, 0.20f, 0.25f, 0.30f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_emissive(plan, cube, 0.35f, 0.40f, 0.45f, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_specular_power(plan, cube, 6.25f));
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
    ASSERT_TRUE(asset.has_material_ambient);
    ASSERT_FLOAT_EQ(0.05f, asset.material_ambient[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_specular);
    ASSERT_FLOAT_EQ(0.20f, asset.material_specular[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_emissive);
    ASSERT_FLOAT_EQ(0.35f, asset.material_emissive[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_specular_power);
    ASSERT_FLOAT_EQ(6.25f, asset.material_specular_power, 0.0001f);
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

TEST(asset_authoring, stores_named_obj_material_specs) {
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
                  },
                  &cube));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, cube, "assets/cube.obj"));

    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  cube,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Red",
                      .has_color = true,
                      .color = {1.0f, 0.0f, 0.0f, 1.0f},
                      .has_ambient = true,
                      .ambient = {0.1f, 0.0f, 0.0f, 1.0f},
                      .has_specular = true,
                      .specular = {0.4f, 0.4f, 0.4f, 1.0f},
                      .has_emissive = true,
                      .emissive = {0.0f, 0.0f, 0.1f, 1.0f},
                      .has_specular_power = true,
                      .specular_power = 12.5f,
                  }));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  cube,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Blue",
                      .has_texture = true,
                      .texture_path = "assets/blue.png",
                  }));

    ASSERT_EQ(2u, nmo_project_plan_obj_material_count(plan, cube));
    nmo_project_material_spec_t material = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, cube, 0u, &material));
    ASSERT_STR_EQ("Red", material.obj_material_name);
    ASSERT_TRUE(material.has_color);
    ASSERT_FLOAT_EQ(1.0f, material.color[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, material.color[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, material.color[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, material.color[3], 0.0001f);
    ASSERT_TRUE(material.has_diffuse);
    ASSERT_FLOAT_EQ(1.0f, material.diffuse[0], 0.0001f);
    ASSERT_TRUE(material.has_ambient);
    ASSERT_FLOAT_EQ(0.1f, material.ambient[0], 0.0001f);
    ASSERT_TRUE(material.has_specular);
    ASSERT_FLOAT_EQ(0.4f, material.specular[0], 0.0001f);
    ASSERT_TRUE(material.has_emissive);
    ASSERT_FLOAT_EQ(0.1f, material.emissive[2], 0.0001f);
    ASSERT_TRUE(material.has_specular_power);
    ASSERT_FLOAT_EQ(12.5f, material.specular_power, 0.0001f);

    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, cube, 1u, &material));
    ASSERT_STR_EQ("Blue", material.obj_material_name);
    ASSERT_TRUE(material.has_texture);
    ASSERT_STR_EQ("assets/blue.png", material.texture_path);

    nmo_project_plan_destroy(plan);
}

TEST(asset_authoring, stores_named_obj_material_texture_slots) {
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
                  },
                  &cube));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, cube, "assets/cube.obj"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  cube,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Layered",
                      .has_color = true,
                      .color = {1.0f, 1.0f, 1.0f, 1.0f},
                      .has_texture_slots = {true, true, false, false},
                      .texture_paths = {"assets/base.png", "assets/detail.png", NULL, NULL},
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_project_plan_set_obj_material_texture_slot(
                  plan,
                  cube,
                  0u,
                  4u,
                  "assets/bad.png"));

    nmo_project_material_spec_t material = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, cube, 0u, &material));
    ASSERT_TRUE(material.has_texture);
    ASSERT_STR_EQ("assets/base.png", material.texture_path);
    ASSERT_TRUE(material.has_texture_slots[0]);
    ASSERT_TRUE(material.has_texture_slots[1]);
    ASSERT_STR_EQ("assets/base.png", material.texture_paths[0]);
    ASSERT_STR_EQ("assets/detail.png", material.texture_paths[1]);

    nmo_project_plan_destroy(plan);
}

TEST(asset_authoring, clones_named_obj_material_specs) {
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
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, cube, "assets/cube.obj"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  cube,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "CloneMat",
                      .has_color = true,
                      .color = {0.25f, 0.5f, 0.75f, 1.0f},
                      .has_ambient = true,
                      .ambient = {0.05f, 0.10f, 0.15f, 1.0f},
                      .has_specular = true,
                      .specular = {0.20f, 0.25f, 0.30f, 1.0f},
                      .has_emissive = true,
                      .emissive = {0.35f, 0.40f, 0.45f, 1.0f},
                      .has_specular_power = true,
                      .specular_power = 6.25f,
                      .has_texture = true,
                      .texture_path = "assets/clone.png",
                  }));

    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_EQ(1u, nmo_project_plan_obj_material_count(clone, cube));

    nmo_project_material_spec_t material = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(clone, cube, 0u, &material));
    ASSERT_STR_EQ("CloneMat", material.obj_material_name);
    ASSERT_TRUE(material.has_color);
    ASSERT_FLOAT_EQ(0.25f, material.color[0], 0.0001f);
    ASSERT_TRUE(material.has_ambient);
    ASSERT_FLOAT_EQ(0.05f, material.ambient[0], 0.0001f);
    ASSERT_TRUE(material.has_specular);
    ASSERT_FLOAT_EQ(0.20f, material.specular[0], 0.0001f);
    ASSERT_TRUE(material.has_emissive);
    ASSERT_FLOAT_EQ(0.35f, material.emissive[0], 0.0001f);
    ASSERT_TRUE(material.has_specular_power);
    ASSERT_FLOAT_EQ(6.25f, material.specular_power, 0.0001f);
    ASSERT_TRUE(material.has_texture);
    ASSERT_STR_EQ("assets/clone.png", material.texture_path);

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST(asset_authoring, stores_camera_and_light_specs) {
    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t camera = 0u;
    uint32_t light = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_CAMERA,
                      .name = "Camera",
                  },
                  &camera));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_LIGHT,
                      .name = "Light",
                  },
                  &light));

    ASSERT_EQ(NMO_OK, nmo_project_plan_set_camera_settings(plan, camera, 0.75f, 0.25f, 500.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_light_settings(
                          plan,
                          light,
                          0.1f,
                          0.2f,
                          0.3f,
                          1.0f,
                          123.0f,
                          VX_LIGHTDIREC));

    nmo_project_object_desc_t camera_desc = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &camera_desc));
    ASSERT_TRUE(camera_desc.has_camera);
    ASSERT_FLOAT_EQ(0.75f, camera_desc.camera_fov, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, camera_desc.camera_near, 0.0001f);
    ASSERT_FLOAT_EQ(500.0f, camera_desc.camera_far, 0.0001f);

    nmo_project_object_desc_t light_desc = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 1u, &light_desc));
    ASSERT_TRUE(light_desc.has_light);
    ASSERT_FLOAT_EQ(0.1f, light_desc.light_diffuse[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.2f, light_desc.light_diffuse[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.3f, light_desc.light_diffuse[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, light_desc.light_diffuse[3], 0.0001f);
    ASSERT_FLOAT_EQ(123.0f, light_desc.light_range, 0.0001f);
    ASSERT_EQ(VX_LIGHTDIREC, light_desc.light_type);

    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(asset_authoring, stores_primitive_mesh_and_material_specs);
REGISTER_TEST(asset_authoring, stores_material_texture_slots);
REGISTER_TEST(asset_authoring, clones_asset_specs);
REGISTER_TEST(asset_authoring, stores_named_obj_material_specs);
REGISTER_TEST(asset_authoring, stores_named_obj_material_texture_slots);
REGISTER_TEST(asset_authoring, clones_named_obj_material_specs);
REGISTER_TEST(asset_authoring, stores_camera_and_light_specs);
TEST_MAIN_END()
