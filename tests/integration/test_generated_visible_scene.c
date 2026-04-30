#include "test_framework.h"

#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "runtime/nmo_context.h"

#include <stdio.h>

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
    ASSERT_EQ(mesh_id, cube_state->current_mesh_id);
    ASSERT_EQ(1u, cube_state->mesh_count);
    ASSERT_NOT_NULL(cube_state->mesh_ids);
    ASSERT_EQ(mesh_id, cube_state->mesh_ids[0]);

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

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_visible_scene, creates_cube_mesh_and_material);
TEST_MAIN_END()
