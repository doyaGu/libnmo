#include "test_framework.h"

#include "project/nmo_asset_plan.h"
#include "project/nmo_project_manifest_json.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"
#include "object/nmo_object_enum_defs.h"

#include <string.h>

TEST(project_manifest_json, parses_minimal_visible_scene)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"output\":\"out.cmo\","
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Camera\",\"class\":\"CKCamera\","
                    "\"camera\":{\"fov\":0.75,\"near\":0.25,\"far\":500}},"
                "{\"name\":\"Light\",\"class\":\"CKLight\","
                    "\"light\":{\"diffuse\":[0.1,0.2,0.3,1],"
                        "\"range\":123,\"type\":\"directional\"}},"
                "{\"name\":\"Cube\",\"class\":\"CK3dEntity\","
                    "\"mesh\":{\"obj\":\"assets/cube.obj\"},"
                    "\"material\":{\"color\":[1,0,0,1],\"texture\":\"assets/cube.png\"},"
                    "\"transform\":{\"position\":[1,2,3],"
                        "\"rotation_euler_deg\":[10,20,30],"
                        "\"scale\":[2,3,4]}}"
            "]"
        "}]"
        "}";

    nmo_project_manifest_t manifest;
    nmo_project_manifest_init(&manifest);
    ASSERT_EQ(
        NMO_OK,
        nmo_project_manifest_json_read_manifest(
            json,
            strlen(json),
            &manifest));
    ASSERT_STR_EQ("out.cmo", manifest.output_path);
    ASSERT_NOT_NULL(manifest.plan);
    ASSERT_STR_EQ("Generated", nmo_project_plan_document_name(manifest.plan));
    ASSERT_EQ(1u, nmo_project_plan_scene_count(manifest.plan));
    ASSERT_EQ(3u, nmo_project_plan_object_count(manifest.plan));
    ASSERT_EQ(1u, nmo_project_plan_asset_count(manifest.plan));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(manifest.plan, 0u, &asset));
    ASSERT_TRUE(asset.has_external_mesh);
    ASSERT_STR_EQ("assets/cube.obj", asset.external_mesh_path);
    ASSERT_TRUE(asset.has_material_color);
    ASSERT_TRUE(asset.has_material_texture);
    ASSERT_STR_EQ("assets/cube.png", asset.material_texture_path);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(manifest.plan, 2u, &object));
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

    nmo_project_object_desc_t camera = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(manifest.plan, 0u, &camera));
    ASSERT_TRUE(camera.has_camera);
    ASSERT_FLOAT_EQ(0.75f, camera.camera_fov, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, camera.camera_near, 0.0001f);
    ASSERT_FLOAT_EQ(500.0f, camera.camera_far, 0.0001f);

    nmo_project_object_desc_t light = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(manifest.plan, 1u, &light));
    ASSERT_TRUE(light.has_light);
    ASSERT_FLOAT_EQ(0.1f, light.light_diffuse[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.2f, light.light_diffuse[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.3f, light.light_diffuse[2], 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, light.light_diffuse[3], 0.0001f);
    ASSERT_FLOAT_EQ(123.0f, light.light_range, 0.0001f);
    ASSERT_EQ(VX_LIGHTDIREC, light.light_type);

    nmo_project_manifest_dispose(&manifest);
}

TEST(project_manifest_json, parses_named_obj_materials)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"assets/cube.obj\"},"
        "\"materials\":["
        "{\"name\":\"Red\",\"color\":[1,0,0,1]},"
        "{\"name\":\"Blue\",\"texture\":\"assets/blue.png\"}"
        "]"
        "}]"
        "}]"
        "}";
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(1u, nmo_project_plan_object_count(plan));

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &object));
    ASSERT_EQ(2u, nmo_project_plan_obj_material_count(plan, object.handle));

    nmo_project_material_spec_t material = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, object.handle, 0u, &material));
    ASSERT_STR_EQ("Red", material.obj_material_name);
    ASSERT_TRUE(material.has_color);
    ASSERT_FLOAT_EQ(1.0f, material.color[0], 0.0001f);

    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, object.handle, 1u, &material));
    ASSERT_STR_EQ("Blue", material.obj_material_name);
    ASSERT_TRUE(material.has_texture);
    ASSERT_STR_EQ("assets/blue.png", material.texture_path);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_material_texture_slots)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"assets/cube.obj\"},"
        "\"material\":{\"texture\":\"assets/base.png\","
            "\"textures\":[{\"slot\":1,\"path\":\"assets/detail.png\"}]},"
        "\"materials\":[{"
            "\"name\":\"Layered\","
            "\"texture\":\"assets/layered-base.png\","
            "\"textures\":[{\"slot\":1,\"path\":\"assets/layered-detail.png\"}]"
        "}]"
        "}]"
        "}]"
        "}";
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &object));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(plan, 0u, &asset));
    ASSERT_TRUE(asset.has_material_texture_slots[0]);
    ASSERT_TRUE(asset.has_material_texture_slots[1]);
    ASSERT_STR_EQ("assets/base.png", asset.material_texture_paths[0]);
    ASSERT_STR_EQ("assets/detail.png", asset.material_texture_paths[1]);

    nmo_project_material_spec_t material = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, object.handle, 0u, &material));
    ASSERT_TRUE(material.has_texture_slots[0]);
    ASSERT_TRUE(material.has_texture_slots[1]);
    ASSERT_STR_EQ("assets/layered-base.png", material.texture_paths[0]);
    ASSERT_STR_EQ("assets/layered-detail.png", material.texture_paths[1]);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_material_color_channels)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"assets/cube.obj\"},"
        "\"material\":{"
            "\"diffuse\":[1,0,0,1],"
            "\"ambient\":[0.1,0.2,0.3,1],"
            "\"specular\":[0.4,0.5,0.6,1],"
            "\"emissive\":[0.7,0.8,0.9,1],"
            "\"specular_power\":12.5"
        "},"
        "\"materials\":[{"
            "\"name\":\"Layered\","
            "\"diffuse\":[0.9,0.8,0.7,1],"
            "\"ambient\":[0.6,0.5,0.4,1],"
            "\"specular\":[0.3,0.2,0.1,1],"
            "\"emissive\":[0.05,0.06,0.07,1],"
            "\"specular_power\":6.25"
        "}]"
        "}]"
        "}]"
        "}";
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &object));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(plan, 0u, &asset));
    ASSERT_TRUE(asset.has_material_diffuse);
    ASSERT_FLOAT_EQ(1.0f, asset.material_diffuse[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_ambient);
    ASSERT_FLOAT_EQ(0.1f, asset.material_ambient[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_specular);
    ASSERT_FLOAT_EQ(0.4f, asset.material_specular[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_emissive);
    ASSERT_FLOAT_EQ(0.7f, asset.material_emissive[0], 0.0001f);
    ASSERT_TRUE(asset.has_material_specular_power);
    ASSERT_FLOAT_EQ(12.5f, asset.material_specular_power, 0.0001f);

    nmo_project_material_spec_t material = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_obj_material(plan, object.handle, 0u, &material));
    ASSERT_STR_EQ("Layered", material.obj_material_name);
    ASSERT_TRUE(material.has_diffuse);
    ASSERT_FLOAT_EQ(0.9f, material.diffuse[0], 0.0001f);
    ASSERT_TRUE(material.has_ambient);
    ASSERT_FLOAT_EQ(0.6f, material.ambient[0], 0.0001f);
    ASSERT_TRUE(material.has_specular);
    ASSERT_FLOAT_EQ(0.3f, material.specular[0], 0.0001f);
    ASSERT_TRUE(material.has_emissive);
    ASSERT_FLOAT_EQ(0.05f, material.emissive[0], 0.0001f);
    ASSERT_TRUE(material.has_specular_power);
    ASSERT_FLOAT_EQ(6.25f, material.specular_power, 0.0001f);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, rejects_material_color_alias_conflict)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"assets/cube.obj\"},"
        "\"material\":{\"color\":[1,0,0,1],\"diffuse\":[1,0,0,1]}"
        "}]"
        "}]"
        "}";
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
              nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, rejects_unproven_material_flag_fields)
{
    static const char *const field_names[] = {
        "alpha",
        "blend",
        "filter",
        "wrap",
    };
    for (size_t i = 0u; i < sizeof(field_names) / sizeof(field_names[0]); ++i) {
        char json[512];
        snprintf(json,
                 sizeof(json),
                 "{"
                 "\"version\":1,"
                 "\"document\":{\"name\":\"Generated\"},"
                 "\"scenes\":[{"
                 "\"name\":\"Level\","
                 "\"objects\":[{"
                 "\"name\":\"Cube\","
                 "\"class\":\"CK3dEntity\","
                 "\"mesh\":{\"primitive\":\"cube\"},"
                 "\"material\":{\"%s\":true}"
                 "}]"
                 "}]"
                 "}",
                 field_names[i]);
        nmo_project_plan_t *plan = NULL;
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
                  nmo_project_manifest_json_read(json, strlen(json), &plan));
        ASSERT_NULL(plan);
    }
}

TEST(project_manifest_json, parses_wavesound_authoring)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":["
        "{\"id\":\"anchor\",\"name\":\"Anchor\",\"class\":\"CK3dEntity\"},"
        "{\"name\":\"Sound\",\"class\":\"CKWaveSound\","
        "\"sound\":{\"file\":\"assets/tone.wav\","
        "\"gain\":0.75,\"pan\":-0.5,\"pitch\":1.25,"
        "\"attached_object\":\"anchor\","
        "\"position\":[1,2,3],\"direction\":[0,0,-1]}}"
        "]"
        "}]"
        "}";
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(2u, nmo_project_plan_object_count(plan));

    nmo_project_object_desc_t sound = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 1u, &sound));
    ASSERT_TRUE(sound.has_sound);
    ASSERT_STR_EQ("assets/tone.wav", sound.sound_file_path);
    ASSERT_TRUE(sound.has_sound_gain);
    ASSERT_FLOAT_EQ(0.75f, sound.sound_gain, 0.0001f);
    ASSERT_TRUE(sound.has_sound_pan);
    ASSERT_FLOAT_EQ(-0.5f, sound.sound_pan, 0.0001f);
    ASSERT_TRUE(sound.has_sound_pitch);
    ASSERT_FLOAT_EQ(1.25f, sound.sound_pitch, 0.0001f);
    ASSERT_TRUE(sound.has_sound_attached_object);
    ASSERT_EQ(1u, sound.sound_attached_object_handle);
    ASSERT_TRUE(sound.has_sound_position);
    ASSERT_FLOAT_EQ(1.0f, sound.sound_position[0], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, sound.sound_position[1], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, sound.sound_position[2], 0.0001f);
    ASSERT_TRUE(sound.has_sound_direction);
    ASSERT_FLOAT_EQ(0.0f, sound.sound_direction[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, sound.sound_direction[1], 0.0001f);
    ASSERT_FLOAT_EQ(-1.0f, sound.sound_direction[2], 0.0001f);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_objectanimation_authoring)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":["
        "{\"id\":\"target\",\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
        "{\"name\":\"Anim\",\"class\":\"CKObjectAnimation\","
        "\"animation\":{\"target\":\"target\","
        "\"format\":\"controllers\","
        "\"root_position\":[1,2,3],"
        "\"flags\":1,"
        "\"length\":12.5}}"
        "]"
        "}]"
        "}";
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_object_desc_t animation = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 1u, &animation));
    ASSERT_TRUE(animation.has_animation);
    ASSERT_EQ(1u, animation.animation_target_handle);
    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS, animation.animation_format);
    ASSERT_TRUE(animation.has_animation_root_position);
    ASSERT_FLOAT_EQ(1.0f, animation.animation_root_position[0], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, animation.animation_root_position[1], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, animation.animation_root_position[2], 0.0001f);
    ASSERT_TRUE(animation.has_animation_flags);
    ASSERT_EQ(1u, animation.animation_flags);
    ASSERT_TRUE(animation.has_animation_length);
    ASSERT_FLOAT_EQ(12.5f, animation.animation_length, 0.0001f);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, rejects_unproven_animation_payload_fields)
{
    static const char *const field_names[] = {
        "file",
        "controllers",
        "keys",
    };
    for (size_t i = 0u; i < sizeof(field_names) / sizeof(field_names[0]); ++i) {
        char json[512];
        snprintf(json,
                 sizeof(json),
                 "{"
                 "\"version\":1,"
                 "\"document\":{\"name\":\"Generated\"},"
                 "\"scenes\":[{\"name\":\"Level\",\"objects\":["
                 "{\"id\":\"target\",\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
                 "{\"name\":\"Anim\",\"class\":\"CKObjectAnimation\","
                 "\"animation\":{\"target\":\"target\",\"format\":\"controllers\","
                 "\"%s\":[]}}"
                 "]}]"
                 "}",
                 field_names[i]);
        nmo_project_plan_t *plan = NULL;
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
                  nmo_project_manifest_json_read(json, strlen(json), &plan));
        ASSERT_NULL(plan);
    }
}

TEST(project_manifest_json, rejects_duplicate_material_texture_slots)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"material\":{\"textures\":["
            "{\"slot\":1,\"path\":\"a.png\"},"
            "{\"slot\":1,\"path\":\"b.png\"}"
        "]}"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, rejects_out_of_range_material_texture_slot)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"materials\":[{"
            "\"name\":\"Bad\","
            "\"textures\":[{\"slot\":4,\"path\":\"bad.png\"}]"
        "}]"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, maps_fields_and_scripts_to_project_plan)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"Cube\","
                "\"class\":\"CK3dEntity\","
                "\"fields\":{\"entity_flags\":\"4\"},"
                "\"scripts\":[{"
                    "\"name\":\"CubeScript\","
                    "\"debug_output\":[\"generated script start\"]"
                "}]"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(1u, nmo_project_plan_object_count(plan));
    ASSERT_EQ(1u, nmo_project_plan_script_count(plan));

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &object));
    ASSERT_EQ(1u, object.field_count);
    ASSERT_STR_EQ("entity_flags", object.fields[0].field_name);
    ASSERT_STR_EQ("4", object.fields[0].value_str);

    nmo_project_script_desc_t script = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 0u, &script));
    ASSERT_EQ(object.handle, script.object_handle);
    ASSERT_STR_EQ("CubeScript", script.name);
    ASSERT_EQ(1u, script.step_count);

    nmo_project_script_step_desc_t step = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(
                          plan,
                          script.handle,
                          0u,
                          &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT, step.kind);
    ASSERT_STR_EQ("generated script start", step.message);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_script_template)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"Cube\","
                "\"class\":\"CK3dEntity\","
                "\"scripts\":[{"
                    "\"name\":\"CubeScript\","
                    "\"template\":\"on_start_debug_output\","
                    "\"message\":\"template script start\""
                "}]"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(1u, nmo_project_plan_script_count(plan));

    nmo_project_script_desc_t script = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 0u, &script));
    ASSERT_EQ(1u, script.step_count);

    nmo_project_script_step_desc_t step = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(
                          plan,
                          script.handle,
                          0u,
                          &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_ON_START_DEBUG_OUTPUT, step.kind);
    ASSERT_STR_EQ("template script start", step.message);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_scene_script_template)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"SceneOwner\","
                "\"class\":\"CK3dEntity\","
                "\"scripts\":[{"
                    "\"name\":\"SceneScript\","
                    "\"template\":\"scene_on_start_debug_output\","
                    "\"message\":\"scene template start\""
                "}]"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_script_desc_t script = {0};
    nmo_project_script_step_desc_t step = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 0u, &script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(
                          plan,
                          script.handle,
                          0u,
                          &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_SCENE_ON_START_DEBUG_OUTPUT, step.kind);
    ASSERT_STR_EQ("scene template start", step.message);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_script_template_v2_names)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"Owner\","
                "\"class\":\"CK3dEntity\","
                "\"scripts\":["
                    "{\"name\":\"TimerScript\",\"template\":\"timer_debug_output\","
                        "\"message\":\"timer\"},"
                    "{\"name\":\"InputScript\",\"template\":\"input_key_debug_output\","
                        "\"message\":\"input\"},"
                    "{\"name\":\"TriggerScript\",\"template\":\"object_trigger_debug_output\","
                        "\"message\":\"trigger\"},"
                    "{\"name\":\"SceneTimerScript\","
                        "\"template\":\"scene_start_then_timer_debug_output\","
                        "\"message\":\"scene timer\"}"
                "]"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(4u, nmo_project_plan_script_count(plan));

    nmo_project_script_desc_t script = {0};
    nmo_project_script_step_desc_t step = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 0u, &script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(plan, script.handle, 0u, &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_TIMER_DEBUG_OUTPUT, step.kind);
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 1u, &script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(plan, script.handle, 0u, &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_INPUT_KEY_DEBUG_OUTPUT, step.kind);
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 2u, &script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(plan, script.handle, 0u, &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_OBJECT_TRIGGER_DEBUG_OUTPUT, step.kind);
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 3u, &script));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(plan, script.handle, 0u, &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_SCENE_START_THEN_TIMER_DEBUG_OUTPUT, step.kind);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, rejects_unknown_fields)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"unexpected\":true,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, rejects_unknown_transform_fields)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":[{"
                "\"name\":\"Cube\","
                "\"class\":\"CK3dEntity\","
                "\"transform\":{\"rotation\":[0,0,0]}"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, parses_parent_by_prior_object_name)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Parent\",\"class\":\"CK3dEntity\"},"
                "{\"name\":\"Child\",\"class\":\"CK3dEntity\","
                    "\"parent\":\"Parent\","
                    "\"transform\":{\"position\":[4,5,6]}}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(2u, nmo_project_plan_object_count(plan));

    nmo_project_object_desc_t parent = {0};
    nmo_project_object_desc_t child = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &parent));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 1u, &child));
    ASSERT_EQ(parent.handle, child.parent_handle);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_parent_by_forward_object_id)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"id\":\"child-id\",\"name\":\"Child\",\"class\":\"CK3dEntity\","
                    "\"parent\":\"parent-id\"},"
                "{\"id\":\"parent-id\",\"name\":\"Parent\",\"class\":\"CK3dEntity\"}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(2u, nmo_project_plan_object_count(plan));

    nmo_project_object_desc_t child = {0};
    nmo_project_object_desc_t parent = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &child));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 1u, &parent));
    ASSERT_EQ(parent.handle, child.parent_handle);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_scene_active_camera)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"active_camera\":\"main-camera\","
            "\"startup_active\":true,"
            "\"objects\":["
                "{\"id\":\"main-camera\",\"name\":\"Camera\",\"class\":\"CKCamera\"}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_scene_desc_t scene = {0};
    nmo_project_object_desc_t camera = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_scene(plan, 0u, &scene));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &camera));
    ASSERT_TRUE(scene.startup_active);
    ASSERT_EQ(camera.handle, scene.active_camera_handle);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, parses_scene_environment)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"environment\":{"
                "\"background_color\":[0.1,0.2,0.3,1],"
                "\"ambient_light\":[0.4,0.5,0.6,1],"
                "\"fog\":{"
                    "\"mode\":\"linear\","
                    "\"color\":[0.7,0.8,0.9,1],"
                    "\"start\":12,"
                    "\"end\":34,"
                    "\"density\":0.25"
                "}"
            "}"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

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

TEST(project_manifest_json, rejects_unknown_scene_environment_fields)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"environment\":{\"skybox\":\"unsupported\"}"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, parses_camera_and_light_targets)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"id\":\"target\",\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
                "{\"name\":\"Camera\",\"class\":\"CKTargetCamera\","
                    "\"camera\":{\"fov\":0.5,\"near\":0.1,\"far\":100,\"target\":\"target\"}},"
                "{\"name\":\"Light\",\"class\":\"CKTargetLight\","
                    "\"light\":{\"diffuse\":[1,1,1,1],\"range\":50,"
                        "\"type\":\"point\",\"target\":\"target\"}}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_object_desc_t target = {0};
    nmo_project_object_desc_t camera = {0};
    nmo_project_object_desc_t light = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &target));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 1u, &camera));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 2u, &light));
    ASSERT_TRUE(camera.has_camera_target);
    ASSERT_EQ(target.handle, camera.camera_target_handle);
    ASSERT_TRUE(light.has_light_target);
    ASSERT_EQ(target.handle, light.light_target_handle);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, resolves_parent_id_before_ambiguous_name)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Child\",\"class\":\"CK3dEntity\",\"parent\":\"Target\"},"
                "{\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
                "{\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
                "{\"id\":\"Target\",\"name\":\"ActualParent\",\"class\":\"CK3dEntity\"}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);

    nmo_project_object_desc_t child = {0};
    nmo_project_object_desc_t parent = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &child));
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 3u, &parent));
    ASSERT_EQ(parent.handle, child.parent_handle);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, rejects_duplicate_object_id)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"id\":\"same\",\"name\":\"One\",\"class\":\"CK3dEntity\"},"
                "{\"id\":\"same\",\"name\":\"Two\",\"class\":\"CK3dEntity\"}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, rejects_ambiguous_parent_name)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Child\",\"class\":\"CK3dEntity\",\"parent\":\"Target\"},"
                "{\"name\":\"Target\",\"class\":\"CK3dEntity\"},"
                "{\"name\":\"Target\",\"class\":\"CK3dEntity\"}"
            "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_manifest_json, parses_minimal_visible_scene);
REGISTER_TEST(project_manifest_json, parses_named_obj_materials);
REGISTER_TEST(project_manifest_json, parses_material_texture_slots);
REGISTER_TEST(project_manifest_json, parses_material_color_channels);
REGISTER_TEST(project_manifest_json, rejects_material_color_alias_conflict);
REGISTER_TEST(project_manifest_json, rejects_unproven_material_flag_fields);
REGISTER_TEST(project_manifest_json, parses_wavesound_authoring);
REGISTER_TEST(project_manifest_json, parses_objectanimation_authoring);
REGISTER_TEST(project_manifest_json, rejects_unproven_animation_payload_fields);
REGISTER_TEST(project_manifest_json, rejects_duplicate_material_texture_slots);
REGISTER_TEST(project_manifest_json, rejects_out_of_range_material_texture_slot);
REGISTER_TEST(project_manifest_json, maps_fields_and_scripts_to_project_plan);
REGISTER_TEST(project_manifest_json, parses_script_template);
REGISTER_TEST(project_manifest_json, parses_scene_script_template);
REGISTER_TEST(project_manifest_json, parses_script_template_v2_names);
REGISTER_TEST(project_manifest_json, rejects_unknown_fields);
REGISTER_TEST(project_manifest_json, rejects_unknown_transform_fields);
REGISTER_TEST(project_manifest_json, parses_parent_by_prior_object_name);
REGISTER_TEST(project_manifest_json, parses_parent_by_forward_object_id);
REGISTER_TEST(project_manifest_json, parses_scene_active_camera);
REGISTER_TEST(project_manifest_json, parses_scene_environment);
REGISTER_TEST(project_manifest_json, rejects_unknown_scene_environment_fields);
REGISTER_TEST(project_manifest_json, parses_camera_and_light_targets);
REGISTER_TEST(project_manifest_json, resolves_parent_id_before_ambiguous_name);
REGISTER_TEST(project_manifest_json, rejects_duplicate_object_id);
REGISTER_TEST(project_manifest_json, rejects_ambiguous_parent_name);
TEST_MAIN_END()
