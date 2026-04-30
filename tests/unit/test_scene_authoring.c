#include "test_framework.h"
#include "document/nmo_document_load.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "runtime/nmo_context.h"

#include <stdio.h>

TEST(scene_authoring, adds_scene_to_project_plan)
{
    nmo_project_plan_t *plan = NULL;
    uint32_t scene_handle = 0;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));
    ASSERT_NE(0u, scene_handle);
    ASSERT_EQ(1u, nmo_project_plan_scene_count(plan));
    ASSERT_STR_EQ("Level", nmo_project_plan_scene_name(plan, scene_handle));

    nmo_project_plan_destroy(plan);
}

TEST(scene_authoring, clones_scene_metadata)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;
    uint32_t scene_handle = 0;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));
    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_EQ(1u, nmo_project_plan_scene_count(clone));
    ASSERT_STR_EQ("Level", nmo_project_plan_scene_name(clone, scene_handle));

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST(scene_authoring, clones_generic_object_metadata)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;
    uint32_t scene_handle = 0;
    uint32_t object_handle = 0;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));

    nmo_session_field_edit_t fields[] = {
        {.field_name = "some_reflected_field", .value_str = "42"},
    };
    nmo_project_object_spec_t object_spec = {
        .scene_handle = scene_handle,
        .class_id = NMO_CID_3DENTITY,
        .name = "Cube",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
        .fields = fields,
        .field_count = 1u,
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &object_spec, &object_handle));

    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_EQ(1u, nmo_project_plan_object_count(clone));

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(clone, 0u, &object));
    ASSERT_EQ(object_handle, object.handle);
    ASSERT_EQ(scene_handle, object.scene_handle);
    ASSERT_EQ(NMO_CID_3DENTITY, object.class_id);
    ASSERT_STR_EQ("Cube", object.name);
    ASSERT_EQ(NMO_PROJECT_OBJECT_FLAG_ACTIVE, object.flags);
    ASSERT_EQ(1u, object.field_count);
    ASSERT_STR_EQ("some_reflected_field", object.fields[0].field_name);
    ASSERT_STR_EQ("42", object.fields[0].value_str);

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST(scene_authoring, executor_creates_scene_object)
{
    const char *output_path = "test_scene_authoring_scene.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene_handle = 0;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedScene"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *doc = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &doc));
    ASSERT_NOT_NULL(doc);

    nmo_object_query_t query = {0};
    query.class_id = NMO_CID_SCENE;
    query.name = "Level";
    query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(doc, &query, &count));
    ASSERT_EQ(1u, count);

    nmo_document_destroy(doc);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(scene_authoring, adds_scene_to_project_plan);
REGISTER_TEST(scene_authoring, clones_scene_metadata);
REGISTER_TEST(scene_authoring, clones_generic_object_metadata);
REGISTER_TEST(scene_authoring, executor_creates_scene_object);
TEST_MAIN_END()
