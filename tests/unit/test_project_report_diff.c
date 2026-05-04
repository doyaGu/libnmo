#include "test_framework.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"

#include <stdio.h>

TEST(project_report_diff, reports_created_scene_object_and_asset)
{
    const char *sound_path = "test_project_report_sound.wav";
    FILE *sound_file = fopen(sound_path, "wb");
    ASSERT_NOT_NULL(sound_file);
    fputs("RIFF", sound_file);
    fclose(sound_file);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;
    uint32_t sound = 0u;
    uint32_t animation = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Diff"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    nmo_project_object_spec_t cube_spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_3DENTITY,
        .name = "Cube",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &cube_spec, &cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_primitive_mesh(
                  plan,
                  cube,
                  NMO_PRIMITIVE_CUBE));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_material_diffuse(
                  plan,
                  cube,
                  1.0f,
                  0.0f,
                  0.0f,
                  1.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_material_ambient(
                  plan,
                  cube,
                  0.1f,
                  0.2f,
                  0.3f,
                  1.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_material_specular_power(plan, cube, 8.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "Sound",
                  },
                  &sound));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_wavesound_file(
                          plan,
                          sound,
                          sound_path));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_wavesound_attached_object(
                          plan,
                          sound,
                          cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "Animation",
                  },
                  &animation));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_object_animation(
                  plan,
                  animation,
                  cube,
                  CKOBJANIM_FORMAT_CONTROLLERS,
                  false,
                  0.0f,
                  0.0f,
                  0.0f,
                  false,
                  0u,
                  true,
                  1.25f));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_dry_run(plan, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_TRUE(nmo_project_report_diff_has_created_scene(&report, "Level"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_object(&report, "Cube"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_asset(&report, "Cube_Mesh"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_asset(&report, "Cube_Material"));
    ASSERT_EQ(3u, report.evidence.object_count);
    ASSERT_EQ(cube, report.evidence.objects[0].plan_handle);
    ASSERT_EQ(0u, report.evidence.objects[0].object_id);
    ASSERT_EQ(NMO_CID_3DENTITY, report.evidence.objects[0].class_id);
    ASSERT_STR_EQ("Cube", report.evidence.objects[0].name);
    ASSERT_EQ(NMO_CID_WAVESOUND, report.evidence.objects[1].class_id);
    ASSERT_STR_EQ("Sound", report.evidence.objects[1].name);
    ASSERT_EQ(NMO_CID_OBJECTANIMATION, report.evidence.objects[2].class_id);
    ASSERT_STR_EQ("Animation", report.evidence.objects[2].name);
    ASSERT_EQ(2u, report.evidence.asset_binding_count);
    ASSERT_STR_EQ("Cube", report.evidence.asset_bindings[0].owner_name);
    ASSERT_STR_EQ("Cube_Mesh", report.evidence.asset_bindings[0].asset_name);
    ASSERT_STR_EQ("primitive_mesh", report.evidence.asset_bindings[0].kind);
    ASSERT_STR_EQ("Cube", report.evidence.asset_bindings[1].owner_name);
    ASSERT_STR_EQ("Cube_Material", report.evidence.asset_bindings[1].asset_name);
    ASSERT_STR_EQ("material", report.evidence.asset_bindings[1].kind);
    ASSERT_EQ(1u, report.evidence.material_channel_count);
    ASSERT_STR_EQ("Cube_Material", report.evidence.material_channels[0].material_name);
    ASSERT_TRUE(report.evidence.material_channels[0].has_diffuse);
    ASSERT_TRUE(report.evidence.material_channels[0].has_ambient);
    ASSERT_FALSE(report.evidence.material_channels[0].has_specular);
    ASSERT_FALSE(report.evidence.material_channels[0].has_emissive);
    ASSERT_TRUE(report.evidence.material_channels[0].has_specular_power);
    ASSERT_EQ(1u, report.evidence.sound_binding_count);
    ASSERT_STR_EQ("Sound", report.evidence.sound_bindings[0].name);
    ASSERT_STR_EQ(sound_path, report.evidence.sound_bindings[0].file);
    ASSERT_STR_EQ("Cube", report.evidence.sound_bindings[0].attached_object_name);
    ASSERT_EQ(1u, report.evidence.animation_binding_count);
    ASSERT_STR_EQ("Animation", report.evidence.animation_bindings[0].name);
    ASSERT_STR_EQ("Cube", report.evidence.animation_bindings[0].target_name);
    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS,
              report.evidence.animation_bindings[0].format);
    ASSERT_TRUE(report.evidence.animation_bindings[0].has_length);
    ASSERT_FLOAT_EQ(1.25f, report.evidence.animation_bindings[0].length, 0.0001f);

    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(sound_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_report_diff, reports_created_scene_object_and_asset);
TEST_MAIN_END()
