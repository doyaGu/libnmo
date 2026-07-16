#include "test_framework.h"

#include "core/nmo_array.h"
#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
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

static bool behavior_ref_array_contains(
    const nmo_array_t *array,
    nmo_object_id_t id)
{
    return nmo_behavior_ref_array_find(array, id, NULL);
}

static void assert_script_has_sub_behavior(
    nmo_document_t *document,
    const nmo_behavior_state_t *script_state,
    const char *name)
{
    nmo_object_t *node = find_named_object(document, name, NMO_CID_BEHAVIOR);
    ASSERT_NOT_NULL(node);
    ASSERT_TRUE(behavior_ref_array_contains(
        &script_state->sub_behaviors,
        nmo_object_get_id(node)));
}

TEST(generated_script_lifecycle, binds_generated_script_to_object)
{
    const char *output_path = "test_generated_script_lifecycle.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;
    uint32_t script = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Scripted"));
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
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object_script(
                  plan,
                  cube,
                  "CubeScript",
                  &script));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_script_add_on_start_debug_output(
                  plan,
                  script,
                  "generated script start"));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_t *cube_object = find_named_object(document, "Cube", NMO_CID_3DENTITY);
    nmo_object_t *script_object =
        find_named_object(document, "CubeScript", NMO_CID_BEHAVIOR);
    nmo_object_t *debug_object =
        find_named_object(document, "CubeScript_DebugOutput", NMO_CID_BEHAVIOR);
    ASSERT_NOT_NULL(cube_object);
    ASSERT_NOT_NULL(script_object);
    ASSERT_NOT_NULL(debug_object);

    nmo_object_id_t script_id = nmo_object_get_id(script_object);
    nmo_object_id_t debug_id = nmo_object_get_id(debug_object);

    const nmo_beobject_state_t *cube_state =
        (const nmo_beobject_state_t *)nmo_object_get_state(cube_object);
    const nmo_behavior_state_t *script_state =
        (const nmo_behavior_state_t *)nmo_object_get_state(script_object);
    ASSERT_NOT_NULL(cube_state);
    ASSERT_NOT_NULL(script_state);
    ASSERT_TRUE(nmo_beobject_script_array_find(
        &cube_state->scripts, script_id, NULL));
    ASSERT_TRUE((script_state->flags & CKBEHAVIOR_SCRIPT) != 0u);
    ASSERT_EQ(NMO_CID_3DENTITY, script_state->compatible_class_id);
    ASSERT_TRUE(behavior_ref_array_contains(&script_state->sub_behaviors, debug_id));

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_script_lifecycle, generates_v2_debug_templates)
{
    const char *output_path = "test_generated_script_templates_v2.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t owner = 0u;
    uint32_t timer_script = 0u;
    uint32_t input_script = 0u;
    uint32_t trigger_script = 0u;
    uint32_t scene_timer_script = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "ScriptTemplates"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Owner",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &owner));

    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object_script(
                          plan,
                          owner,
                          "TimerScript",
                          &timer_script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_script_add_timer_debug_output(
                          plan,
                          timer_script,
                          "timer"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object_script(
                          plan,
                          owner,
                          "InputScript",
                          &input_script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_script_add_input_key_debug_output(
                          plan,
                          input_script,
                          "input"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object_script(
                          plan,
                          owner,
                          "TriggerScript",
                          &trigger_script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_script_add_object_trigger_debug_output(
                          plan,
                          trigger_script,
                          "trigger"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object_script(
                          plan,
                          owner,
                          "SceneTimerScript",
                          &scene_timer_script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_script_add_scene_start_then_timer_debug_output(
                          plan,
                          scene_timer_script,
                          "scene timer"));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    const char *script_names[] = {
        "TimerScript",
        "InputScript",
        "TriggerScript",
        "SceneTimerScript",
    };
    const char *debug_names[] = {
        "TimerScript_DebugOutput",
        "InputScript_DebugOutput",
        "TriggerScript_DebugOutput",
        "SceneTimerScript_DebugOutput",
    };
    const char *trigger_names[] = {
        "TimerScript_Timer",
        "InputScript_KeyWaiter",
        "TriggerScript_TriggerEvent",
        "SceneTimerScript_Timer",
    };
    nmo_object_t *owner_object = find_named_object(document, "Owner", NMO_CID_3DENTITY);
    ASSERT_NOT_NULL(owner_object);
    const nmo_beobject_state_t *owner_state =
        (const nmo_beobject_state_t *)nmo_object_get_state(owner_object);
    ASSERT_NOT_NULL(owner_state);
    for (size_t i = 0u; i < 4u; ++i) {
        nmo_object_t *script_object =
            find_named_object(document, script_names[i], NMO_CID_BEHAVIOR);
        nmo_object_t *debug_object =
            find_named_object(document, debug_names[i], NMO_CID_BEHAVIOR);
        ASSERT_NOT_NULL(script_object);
        ASSERT_NOT_NULL(debug_object);
        const nmo_behavior_state_t *script_state =
            (const nmo_behavior_state_t *)nmo_object_get_state(script_object);
        ASSERT_NOT_NULL(script_state);
        ASSERT_TRUE(nmo_beobject_script_array_find(
            &owner_state->scripts, nmo_object_get_id(script_object), NULL));
        ASSERT_TRUE(behavior_ref_array_contains(
            &script_state->sub_behaviors,
            nmo_object_get_id(debug_object)));
        assert_script_has_sub_behavior(document, script_state, trigger_names[i]);
        ASSERT_TRUE(script_state->sub_behavior_links.count >= 1u);
    }

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_script_lifecycle, binds_generated_script_to_object);
REGISTER_TEST(generated_script_lifecycle, generates_v2_debug_templates);
TEST_MAIN_END()
