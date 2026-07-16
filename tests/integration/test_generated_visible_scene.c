#include "test_framework.h"

#include "document/nmo_document_load.h"
#include "core/nmo_arena.h"
#include "format/nmo_stb_adapter.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "runtime/nmo_context.h"

#include <stdio.h>
#include <string.h>

static nmo_object_t *find_named_object(
    nmo_document_t *document,
    const char *name,
    nmo_class_id_t class_id)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            document,
            &(nmo_object_query_t){
                .class_id = class_id,
                .name = name,
                .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
            },
            &object,
            NULL) != NMO_OK) {
        return NULL;
    }
    return object;
}

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    size_t len = strlen(text);
    int ok = fwrite(text, 1u, len, fp) == len;
    fclose(fp);
    return ok;
}

static int write_png_file(const char *path)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (!arena) {
        return 0;
    }
    const uint8_t pixels[] = {
        255u, 255u, 255u, 255u,
        0u, 0u, 255u, 255u,
    };
    size_t png_size = 0u;
    uint8_t *png = nmo_stbi_write_to_memory(
        arena,
        NMO_BITMAP_FORMAT_PNG,
        2,
        1,
        4,
        pixels,
        90,
        &png_size);
    if (!png || png_size == 0u) {
        nmo_arena_destroy(arena);
        return 0;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        nmo_arena_destroy(arena);
        return 0;
    }
    int ok = fwrite(png, 1u, png_size, fp) == png_size;
    fclose(fp);
    nmo_arena_destroy(arena);
    return ok;
}

TEST(generated_visible_scene, creates_cube_mesh_and_material) {
    const char *output_path = "test_generated_visible_scene.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Visible"));
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

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_t *cube_object = find_named_object(document, "Cube", NMO_CID_3DENTITY);
    nmo_object_t *mesh_object = find_named_object(document, "Cube_Mesh", NMO_CID_MESH);
    nmo_object_t *material_object = find_named_object(document, "Cube_Material", NMO_CID_MATERIAL);
    ASSERT_NOT_NULL(cube_object);
    ASSERT_NOT_NULL(mesh_object);
    ASSERT_NOT_NULL(material_object);

    nmo_object_id_t mesh_id = nmo_object_get_id(mesh_object);
    nmo_object_id_t material_id = nmo_object_get_id(material_object);

    const nmo_3dentity_state_t *cube_state =
        (const nmo_3dentity_state_t *)nmo_object_get_state(cube_object);
    ASSERT_NOT_NULL(cube_state);
    ASSERT_EQ(mesh_id, nmo_ref_runtime_id(&cube_state->current_mesh));
    ASSERT_EQ(1u, cube_state->mesh_count);
    ASSERT_NOT_NULL(cube_state->mesh_ids);
    ASSERT_EQ(mesh_id, nmo_ref_runtime_id(&cube_state->mesh_ids[0]));

    const nmo_mesh_state_t *mesh_state =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh_state);
    ASSERT_EQ(8u, mesh_state->vertex_count);
    ASSERT_EQ(12u, mesh_state->face_count);
    ASSERT_EQ(1u, mesh_state->material_group_count);
    ASSERT_NOT_NULL(mesh_state->material_groups);
    ASSERT_EQ(material_id, mesh_state->material_groups[0].material_id);

    const nmo_material_state_t *material_state =
        (const nmo_material_state_t *)nmo_object_get_state(material_object);
    ASSERT_NOT_NULL(material_state);
    ASSERT_EQ(0xFFFF0000u, material_state->diffuse_color);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_visible_scene, creates_external_obj_texture_and_position) {
    const char *output_path = "test_generated_visible_external.cmo";
    const char *obj_path = "test_generated_visible_external.obj";
    const char *png_path = "test_generated_visible_external.png";
    remove(output_path);
    remove(obj_path);
    remove(png_path);

    ASSERT_TRUE(write_text_file(
        obj_path,
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "f 1/1 2/2 3/3\n"));
    ASSERT_TRUE(write_png_file(png_path));

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t triangle = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "VisibleExternal"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Triangle",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &triangle));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, triangle, obj_path));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_texture(plan, triangle, png_path));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_position(plan, triangle, 4.0f, 5.0f, 6.0f));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_t *triangle_object = find_named_object(document, "Triangle", NMO_CID_3DENTITY);
    nmo_object_t *mesh_object = find_named_object(document, "Triangle_Mesh", NMO_CID_MESH);
    nmo_object_t *material_object = find_named_object(document, "Triangle_Material", NMO_CID_MATERIAL);
    nmo_object_t *texture_object = find_named_object(document, "Triangle_Texture", NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(triangle_object);
    ASSERT_NOT_NULL(mesh_object);
    ASSERT_NOT_NULL(material_object);
    ASSERT_NOT_NULL(texture_object);

    nmo_object_id_t mesh_id = nmo_object_get_id(mesh_object);
    nmo_object_id_t material_id = nmo_object_get_id(material_object);
    nmo_object_id_t texture_id = nmo_object_get_id(texture_object);

    const nmo_3dentity_state_t *entity_state =
        (const nmo_3dentity_state_t *)nmo_object_get_state(triangle_object);
    ASSERT_NOT_NULL(entity_state);
    ASSERT_EQ(mesh_id, nmo_ref_runtime_id(&entity_state->current_mesh));
    ASSERT_FLOAT_EQ(4.0f, entity_state->world_matrix[12], 0.0001f);
    ASSERT_FLOAT_EQ(5.0f, entity_state->world_matrix[13], 0.0001f);
    ASSERT_FLOAT_EQ(6.0f, entity_state->world_matrix[14], 0.0001f);

    const nmo_mesh_state_t *mesh_state =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh_state);
    ASSERT_EQ(3u, mesh_state->vertex_count);
    ASSERT_EQ(1u, mesh_state->face_count);
    ASSERT_EQ(1u, mesh_state->material_group_count);
    ASSERT_NOT_NULL(mesh_state->material_groups);
    ASSERT_EQ(material_id, mesh_state->material_groups[0].material_id);

    const nmo_material_state_t *material_state =
        (const nmo_material_state_t *)nmo_object_get_state(material_object);
    ASSERT_NOT_NULL(material_state);
    ASSERT_EQ(0xFFFFFFFFu, material_state->diffuse_color);
    ASSERT_EQ(texture_id, material_state->texture_ids[0]);

    const nmo_texture_state_t *texture_state =
        (const nmo_texture_state_t *)nmo_object_get_state(texture_object);
    ASSERT_NOT_NULL(texture_state);
    ASSERT_EQ(2, texture_state->reader_width);
    ASSERT_EQ(1, texture_state->reader_height);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
    remove(obj_path);
    remove(png_path);
}

TEST(generated_visible_scene, creates_external_obj_named_materials) {
    const char *obj_path = "test_generated_visible_multi_material.obj";
    const char *base_png_path = "test_generated_visible_multi_material_base.png";
    const char *detail_png_path = "test_generated_visible_multi_material_detail.png";
    const char *output_path = "test_generated_visible_multi_material.cmo";
    remove(obj_path);
    remove(base_png_path);
    remove(detail_png_path);
    remove(output_path);
    remove("test_generated_visible_multi_material.cmo.tmp");
    ASSERT_TRUE(write_text_file(
        obj_path,
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 1 1 0\n"
        "usemtl Red\n"
        "f 1 2 3\n"
        "usemtl Blue\n"
        "f 2 4 3\n"));
    ASSERT_TRUE(write_png_file(base_png_path));
    ASSERT_TRUE(write_png_file(detail_png_path));

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t entity = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "NamedMaterials"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Entity",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &entity));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, entity, obj_path));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  entity,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Red",
                      .has_color = true,
                      .color = {1.0f, 0.0f, 0.0f, 1.0f},
                  }));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  entity,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Blue",
                      .has_color = true,
                      .color = {0.0f, 0.0f, 1.0f, 1.0f},
                      .has_texture_slots = {true, true, false, false},
                      .texture_paths = {base_png_path, detail_png_path, NULL, NULL},
                  }));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_t *mesh_object = find_named_object(document, "Entity_Mesh", NMO_CID_MESH);
    nmo_object_t *red_object = find_named_object(document, "Entity_Red_Material", NMO_CID_MATERIAL);
    nmo_object_t *blue_object = find_named_object(document, "Entity_Blue_Material", NMO_CID_MATERIAL);
    nmo_object_t *blue_base_texture = find_named_object(document, "Entity_Blue_Texture", NMO_CID_TEXTURE);
    nmo_object_t *blue_detail_texture = find_named_object(document, "Entity_Blue_Texture1", NMO_CID_TEXTURE);
    ASSERT_NOT_NULL(mesh_object);
    ASSERT_NOT_NULL(red_object);
    ASSERT_NOT_NULL(blue_object);
    ASSERT_NOT_NULL(blue_base_texture);
    ASSERT_NOT_NULL(blue_detail_texture);

    const nmo_mesh_state_t *mesh_state =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh_state);
    ASSERT_EQ(2u, mesh_state->face_count);
    ASSERT_EQ(2u, mesh_state->material_group_count);
    ASSERT_NOT_NULL(mesh_state->material_groups);
    ASSERT_EQ(nmo_object_get_id(red_object), mesh_state->material_groups[0].material_id);
    ASSERT_EQ(nmo_object_get_id(blue_object), mesh_state->material_groups[1].material_id);
    ASSERT_EQ(0u, mesh_state->faces[0].material_group_idx);
    ASSERT_EQ(1u, mesh_state->faces[1].material_group_idx);

    const nmo_material_state_t *blue_state =
        (const nmo_material_state_t *)nmo_object_get_state(blue_object);
    ASSERT_NOT_NULL(blue_state);
    ASSERT_EQ(nmo_object_get_id(blue_base_texture), blue_state->texture_ids[0]);
    ASSERT_EQ(nmo_object_get_id(blue_detail_texture), blue_state->texture_ids[1]);
    ASSERT_TRUE(blue_state->has_additional_textures);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
    remove(obj_path);
    remove(base_png_path);
    remove(detail_png_path);
}

TEST(generated_visible_scene, creates_transform_hierarchy) {
    const char *output_path = "test_generated_visible_hierarchy.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t parent = 0u;
    uint32_t child = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Hierarchy"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Parent",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &parent));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .parent_handle = parent,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Child",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &child));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_position(plan, child, 4.0f, 5.0f, 6.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_rotation_euler_deg(plan, child, 0.0f, 0.0f, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_scale(plan, child, 2.0f, 3.0f, 4.0f));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_t *parent_object = find_named_object(document, "Parent", NMO_CID_3DENTITY);
    nmo_object_t *child_object = find_named_object(document, "Child", NMO_CID_3DENTITY);
    ASSERT_NOT_NULL(parent_object);
    ASSERT_NOT_NULL(child_object);

    const nmo_3dentity_state_t *child_state =
        (const nmo_3dentity_state_t *)nmo_object_get_state(child_object);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(nmo_object_get_id(parent_object),
              nmo_ref_runtime_id(&child_state->parent));
    ASSERT_FLOAT_EQ(2.0f, child_state->world_matrix[0], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, child_state->world_matrix[5], 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, child_state->world_matrix[10], 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, child_state->world_matrix[12], 0.0001f);
    ASSERT_FLOAT_EQ(5.0f, child_state->world_matrix[13], 0.0001f);
    ASSERT_FLOAT_EQ(6.0f, child_state->world_matrix[14], 0.0001f);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_visible_scene, creates_camera_and_light_settings) {
    const char *output_path = "test_generated_visible_camera_light.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t camera = 0u;
    uint32_t light = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "CameraLight"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_CAMERA,
                      .name = "Camera",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &camera));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_LIGHT,
                      .name = "Light",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
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

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_t *camera_object = find_named_object(document, "Camera", NMO_CID_CAMERA);
    nmo_object_t *light_object = find_named_object(document, "Light", NMO_CID_LIGHT);
    ASSERT_NOT_NULL(camera_object);
    ASSERT_NOT_NULL(light_object);

    const nmo_camera_state_t *camera_state =
        (const nmo_camera_state_t *)nmo_object_get_state(camera_object);
    ASSERT_NOT_NULL(camera_state);
    ASSERT_FLOAT_EQ(0.75f, camera_state->fov, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, camera_state->near_plane, 0.0001f);
    ASSERT_FLOAT_EQ(500.0f, camera_state->far_plane, 0.0001f);

    const nmo_light_state_t *light_state =
        (const nmo_light_state_t *)nmo_object_get_state(light_object);
    ASSERT_NOT_NULL(light_state);
    ASSERT_FLOAT_EQ(26.0f / 255.0f, light_state->light_data.diffuse.r, 0.0001f);
    ASSERT_FLOAT_EQ(51.0f / 255.0f, light_state->light_data.diffuse.g, 0.0001f);
    ASSERT_FLOAT_EQ(77.0f / 255.0f, light_state->light_data.diffuse.b, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, light_state->light_data.diffuse.a, 0.0001f);
    ASSERT_FLOAT_EQ(123.0f, light_state->light_data.range, 0.0001f);
    ASSERT_EQ(VX_LIGHTDIREC, light_state->light_data.type);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_visible_scene, creates_cube_mesh_and_material);
REGISTER_TEST(generated_visible_scene, creates_external_obj_texture_and_position);
REGISTER_TEST(generated_visible_scene, creates_external_obj_named_materials);
REGISTER_TEST(generated_visible_scene, creates_transform_hierarchy);
REGISTER_TEST(generated_visible_scene, creates_camera_and_light_settings);
TEST_MAIN_END()
