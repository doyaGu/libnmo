#include "test_framework.h"
#include "document/nmo_document_load.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "project/nmo_project_validator.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "object/nmo_context.h"

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

TEST(scene_authoring, stores_scene_environment)
{
    nmo_project_plan_t *plan = NULL;
    uint32_t scene_handle = 0;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_background_color(
                          plan,
                          scene_handle,
                          0.1f,
                          0.2f,
                          0.3f,
                          1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_ambient_light(
                          plan,
                          scene_handle,
                          0.4f,
                          0.5f,
                          0.6f,
                          1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_fog(
                          plan,
                          scene_handle,
                          VXFOG_LINEAR,
                          0.7f,
                          0.8f,
                          0.9f,
                          1.0f,
                          12.0f,
                          34.0f,
                          0.25f));

    nmo_project_scene_desc_t scene = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_scene(plan, 0u, &scene));
    ASSERT_TRUE(scene.has_background_color);
    ASSERT_FLOAT_EQ(0.1f, scene.background_color[0], 0.0001f);
    ASSERT_TRUE(scene.has_ambient_light);
    ASSERT_FLOAT_EQ(0.5f, scene.ambient_light[1], 0.0001f);
    ASSERT_TRUE(scene.has_fog);
    ASSERT_EQ(VXFOG_LINEAR, scene.fog_mode);
    ASSERT_FLOAT_EQ(0.9f, scene.fog_color[2], 0.0001f);
    ASSERT_FLOAT_EQ(12.0f, scene.fog_start, 0.0001f);
    ASSERT_FLOAT_EQ(34.0f, scene.fog_end, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, scene.fog_density, 0.0001f);

    nmo_project_plan_destroy(plan);
}

TEST(scene_authoring, clones_scene_environment)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;
    uint32_t scene_handle = 0;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_background_color(
                          plan,
                          scene_handle,
                          0.2f,
                          0.3f,
                          0.4f,
                          1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_ambient_light(
                          plan,
                          scene_handle,
                          0.5f,
                          0.6f,
                          0.7f,
                          1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_fog(
                          plan,
                          scene_handle,
                          VXFOG_EXP2,
                          0.1f,
                          0.2f,
                          0.3f,
                          1.0f,
                          1.0f,
                          2.0f,
                          0.5f));

    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    nmo_project_scene_desc_t scene = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_scene(clone, 0u, &scene));
    ASSERT_TRUE(scene.has_background_color);
    ASSERT_FLOAT_EQ(0.2f, scene.background_color[0], 0.0001f);
    ASSERT_TRUE(scene.has_ambient_light);
    ASSERT_FLOAT_EQ(0.6f, scene.ambient_light[1], 0.0001f);
    ASSERT_TRUE(scene.has_fog);
    ASSERT_EQ(VXFOG_EXP2, scene.fog_mode);
    ASSERT_FLOAT_EQ(0.5f, scene.fog_density, 0.0001f);

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

TEST(scene_authoring, executor_authors_scene_environment)
{
    const char *output_path = "test_scene_authoring_environment.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene_handle = 0;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedScene"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene_handle));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_background_color(
                          plan,
                          scene_handle,
                          0.1f,
                          0.2f,
                          0.3f,
                          1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_ambient_light(
                          plan,
                          scene_handle,
                          0.4f,
                          0.5f,
                          0.6f,
                          1.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_fog(
                          plan,
                          scene_handle,
                          VXFOG_LINEAR,
                          0.7f,
                          0.8f,
                          0.9f,
                          1.0f,
                          12.0f,
                          34.0f,
                          0.25f));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *doc = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &doc));
    ASSERT_NOT_NULL(doc);

    nmo_object_t *scene_object = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_object_query_find_first(
                  doc,
                  &(nmo_object_query_t){
                      .class_id = NMO_CID_SCENE,
                      .name = "Level",
                      .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
                  },
                  &scene_object,
                  NULL));
    ASSERT_NOT_NULL(scene_object);
    const nmo_scene_state_t *scene_state =
        (const nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(0xFF1A334Du, scene_state->background_color);
    ASSERT_EQ(0xFF668099u, scene_state->ambient_light_color);
    ASSERT_EQ(VXFOG_LINEAR, scene_state->fog_mode);
    ASSERT_EQ(0xFFB3CCE6u, scene_state->fog_color);
    ASSERT_FLOAT_EQ(12.0f, scene_state->fog_start, 0.0001f);
    ASSERT_FLOAT_EQ(34.0f, scene_state->fog_end, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, scene_state->fog_density, 0.0001f);

    nmo_document_destroy(doc);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(scene_authoring, stores_and_clones_groups)
{
    nmo_project_plan_t *plan = NULL;
    uint32_t floor = 0u;
    uint32_t rail = 0u;
    uint32_t floors = 0u;
    uint32_t checkpoints = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_3DOBJECT,
                              .name = "A01_Floor_01",
                          },
                          &floor));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_3DOBJECT,
                              .name = "A01_Rail_01",
                          },
                          &rail));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group(plan, "Phys_Floors", &floors));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group(plan, "PC_Checkpoints", &checkpoints));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group_member(plan, floors, rail));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group_member(plan, floors, floor));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group_member(plan, floors, rail));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_group_source_path(plan, floors, "groups[0]"));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_project_plan_add_group_member(plan, 99u, floor));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_project_plan_add_group(plan, "", NULL));

    nmo_project_plan_t *clone = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    nmo_project_plan_destroy(plan);

    ASSERT_EQ(2u, nmo_project_plan_group_count(clone));
    nmo_project_group_desc_t group = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_group(clone, 0u, &group));
    ASSERT_EQ(floors, group.handle);
    ASSERT_STR_EQ("Phys_Floors", group.name);
    ASSERT_STR_EQ("groups[0]", group.source_path);
    ASSERT_EQ(2u, group.member_count);
    ASSERT_EQ(rail, group.member_handles[0]);
    ASSERT_EQ(floor, group.member_handles[1]);
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_group(clone, 1u, &group));
    ASSERT_STR_EQ("PC_Checkpoints", group.name);
    ASSERT_EQ(0u, group.member_count);
    ASSERT_EQ(NMO_ERR_OUT_OF_BOUNDS, nmo_project_plan_get_group(clone, 2u, &group));

    uint32_t next = 0u;
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group(clone, "Shadow", &next));
    ASSERT_TRUE(next != floors && next != checkpoints);

    nmo_project_plan_destroy(clone);
}

static const nmo_group_state_t *find_group_state(nmo_document_t *doc, const char *name)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            doc,
            &(nmo_object_query_t){
                .class_id = NMO_CID_GROUP,
                .name = name,
                .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
            },
            &object,
            NULL) != NMO_OK ||
        object == NULL) {
        return NULL;
    }
    return (const nmo_group_state_t *)nmo_object_get_state(object);
}

TEST(scene_authoring, executor_authors_groups)
{
    const char *output_path = "test_scene_authoring_groups.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t floor = 0u;
    uint32_t start = 0u;
    uint32_t group = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));
    /* No scene: a Ballance level file holds objects and groups only. */
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_3DOBJECT,
                              .name = "A01_Floor_01",
                          },
                          &floor));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_3DOBJECT,
                              .name = "PS_FourFlames_01",
                          },
                          &start));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group(plan, "Phys_Floors", &group));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group_member(plan, group, floor));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group(plan, "PS_Levelstart", &group));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group_member(plan, group, start));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_group(plan, "PC_Checkpoints", &group));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(3u, report.group_diff.created.count);
    ASSERT_STR_EQ("Phys_Floors", report.group_diff.created.names[0]);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *doc = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &doc));
    ASSERT_NOT_NULL(doc);

    nmo_object_t *floor_object = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_object_query_find_first(
                  doc,
                  &(nmo_object_query_t){
                      .class_id = NMO_CID_3DOBJECT,
                      .name = "A01_Floor_01",
                      .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
                  },
                  &floor_object,
                  NULL));
    ASSERT_NOT_NULL(floor_object);

    const nmo_group_state_t *floors = find_group_state(doc, "Phys_Floors");
    ASSERT_NOT_NULL(floors);
    ASSERT_EQ(1u, floors->object_ids.count);
    ASSERT_EQ(nmo_object_get_id(floor_object),
              nmo_ref_runtime_id((const nmo_ref_t *)nmo_array_get(&floors->object_ids, 0)));
    const nmo_group_state_t *levelstart = find_group_state(doc, "PS_Levelstart");
    ASSERT_NOT_NULL(levelstart);
    ASSERT_EQ(1u, levelstart->object_ids.count);
    const nmo_group_state_t *checkpoints = find_group_state(doc, "PC_Checkpoints");
    ASSERT_NOT_NULL(checkpoints);
    ASSERT_EQ(0u, checkpoints->object_ids.count);
    ASSERT_EQ(1u, checkpoints->has_group_data);

    nmo_document_destroy(doc);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(scene_authoring, executor_saves_hidden_objects)
{
    const char *output_path = "test_scene_authoring_hidden.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t hidden = 0u;
    uint32_t shown = 0u;
    uint32_t group = 0u;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_3DOBJECT,
                              .name = "Quader01",
                              .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE | NMO_PROJECT_OBJECT_FLAG_HIDDEN,
                          },
                          &hidden));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_3DOBJECT,
                              .name = "A01_Floor_01",
                              .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                          },
                          &shown));

    nmo_project_report_t report;
    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *doc = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &doc));
    const char *names[2] = {"Quader01", "A01_Floor_01"};
    for (size_t i = 0u; i < 2u; ++i) {
        nmo_object_t *object = NULL;
        ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                              doc,
                              &(nmo_object_query_t){
                                  .class_id = NMO_CID_3DOBJECT,
                                  .name = names[i],
                                  .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
                              },
                              &object,
                              NULL));
        ASSERT_NOT_NULL(object);
        const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
        const nmo_3dentity_state_t *entity = (const nmo_3dentity_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(registry, object, CKPGUID_3DENTITY);
        const nmo_object_state_t *base = (const nmo_object_state_t *)
            nmo_type_query_object_get_ancestor_state_by_guid(registry, object, CKPGUID_OBJECT);
        ASSERT_NOT_NULL(entity);
        ASSERT_NOT_NULL(base);
        bool visible = i == 1u;
        ASSERT_EQ(visible, (entity->moveable_flags & (uint32_t)VX_MOVEABLE_VISIBLE) != 0u);
        ASSERT_EQ(visible, (base->visibility_flags & NMO_CKOBJECT_VISIBLE) != 0u);
    }
    nmo_document_destroy(doc);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);

    /* Only 3D entities can be hidden. */
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(
                          plan,
                          &(nmo_project_object_spec_t){
                              .class_id = NMO_CID_GROUP,
                              .name = "Shadow",
                              .flags = NMO_PROJECT_OBJECT_FLAG_HIDDEN,
                          },
                          &group));
    nmo_project_validation_report_t validation;
    nmo_project_validation_report_init(&validation);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &validation));
    ASSERT_TRUE(nmo_project_validation_contains(&validation, "invalid_hidden_target"));
    nmo_project_validation_report_dispose(&validation);

    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(scene_authoring, adds_scene_to_project_plan);
REGISTER_TEST(scene_authoring, clones_scene_metadata);
REGISTER_TEST(scene_authoring, stores_scene_environment);
REGISTER_TEST(scene_authoring, clones_scene_environment);
REGISTER_TEST(scene_authoring, clones_generic_object_metadata);
REGISTER_TEST(scene_authoring, executor_creates_scene_object);
REGISTER_TEST(scene_authoring, executor_authors_scene_environment);
REGISTER_TEST(scene_authoring, stores_and_clones_groups);
REGISTER_TEST(scene_authoring, executor_authors_groups);
REGISTER_TEST(scene_authoring, executor_saves_hidden_objects);
TEST_MAIN_END()
