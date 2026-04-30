#include "test_framework.h"
#include "core/nmo_array.h"
#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "runtime/nmo_context.h"

#include <stdio.h>

static void assert_named_class_exists(
    nmo_document_t *document,
    const char *name,
    nmo_class_id_t class_id)
{
    nmo_object_query_t query = {0};
    query.name = name;
    query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    query.class_id = class_id;

    size_t count = 0u;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &query, &count));
    ASSERT_EQ(1u, count);
}

TEST(generated_scene_roundtrip, saves_and_reloads_scene_objects)
{
    const char *output_path = "test_generated_scene_roundtrip.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));

    uint32_t scene = 0u;
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene_Main", &scene));

    uint32_t camera = 0u;
    uint32_t light = 0u;
    uint32_t cube = 0u;
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_camera(plan, scene, "Camera_Main", &camera));
    ASSERT_NE(0u, camera);
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_light(plan, scene, "Light_Key", &light));
    ASSERT_NE(0u, light);
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_3d_entity(plan, scene, "Cube_A", &cube));
    ASSERT_NE(0u, cube);

    nmo_project_report_t report;
    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    assert_named_class_exists(document, "Scene_Main", NMO_CID_SCENE);
    assert_named_class_exists(document, "Camera_Main", NMO_CID_CAMERA);
    assert_named_class_exists(document, "Light_Key", NMO_CID_LIGHT);
    assert_named_class_exists(document, "Cube_A", NMO_CID_3DENTITY);

    nmo_object_query_t scene_query = {0};
    scene_query.name = "Scene_Main";
    scene_query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    scene_query.class_id = NMO_CID_SCENE;

    nmo_object_t *scene_object = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                          document,
                          &scene_query,
                          &scene_object,
                          NULL));
    ASSERT_NOT_NULL(scene_object);

    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(3u, nmo_array_size(&scene_state->object_descs));

    const nmo_scene_object_desc_t *descs =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &scene_state->object_descs);
    for (size_t i = 0; i < 3u; ++i) {
        ASSERT_NE(0u, descs[i].object_id);
        ASSERT_TRUE((descs[i].flags & CK_SCENEOBJECT_ACTIVE) != 0u);
        ASSERT_TRUE((descs[i].flags & CK_SCENEOBJECT_START_ACTIVATE) != 0u);
    }

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_scene_roundtrip, saves_and_reloads_scene_objects);
TEST_MAIN_END()
