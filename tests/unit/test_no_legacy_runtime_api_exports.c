#include "test_framework.h"

#include <stdio.h>
#include <string.h>

static const char *k_builtin_headers[] = {
    "include/object/builtin/nmo_object_schemas.h",
    "include/object/builtin/nmo_sceneobject_schemas.h",
    "include/object/builtin/nmo_beobject_schemas.h",
    "include/object/builtin/nmo_renderobject_schemas.h",
    "include/object/builtin/nmo_parameter_schemas.h",
    "include/object/builtin/nmo_parameterin_schemas.h",
    "include/object/builtin/nmo_parameterout_schemas.h",
    "include/object/builtin/nmo_parameterlocal_schemas.h",
    "include/object/builtin/nmo_parameteroperation_schemas.h",
    "include/object/builtin/nmo_behavior_schemas.h",
    "include/object/builtin/nmo_behaviorio_schemas.h",
    "include/object/builtin/nmo_behaviorlink_schemas.h",
    "include/object/builtin/nmo_scene_schemas.h",
    "include/object/builtin/nmo_level_schemas.h",
    "include/object/builtin/nmo_group_schemas.h",
    "include/object/builtin/nmo_dataarray_schemas.h",
    "include/object/builtin/nmo_place_schemas.h",
    "include/object/builtin/nmo_2dentity_schemas.h",
    "include/object/builtin/nmo_sprite_schemas.h",
    "include/object/builtin/nmo_spritetext_schemas.h",
    "include/object/builtin/nmo_3dentity_schemas.h",
    "include/object/builtin/nmo_3dobject_schemas.h",
    "include/object/builtin/nmo_camera_schemas.h",
    "include/object/builtin/nmo_targetcamera_schemas.h",
    "include/object/builtin/nmo_light_schemas.h",
    "include/object/builtin/nmo_targetlight_schemas.h",
    "include/object/builtin/nmo_character_schemas.h",
    "include/object/builtin/nmo_sprite3d_schemas.h",
    "include/object/builtin/nmo_curve_schemas.h",
    "include/object/builtin/nmo_material_schemas.h",
    "include/object/builtin/nmo_texture_schemas.h",
    "include/object/builtin/nmo_mesh_schemas.h",
    "include/object/builtin/nmo_patchmesh_schemas.h",
    "include/object/builtin/nmo_grid_schemas.h",
    "include/object/builtin/nmo_layer_schemas.h",
    "include/object/builtin/nmo_animation_schemas.h",
    "include/object/builtin/nmo_rendercontext_schemas.h",
    "include/object/builtin/nmo_kinematicchain_schemas.h",
    "include/object/builtin/nmo_synchro_schemas.h",
    "include/object/builtin/nmo_sound_schemas.h",
    "include/object/builtin/nmo_interfaceobjectmanager_schemas.h",
    "include/object/builtin/nmo_attributemanager_schemas.h",
    "include/object/builtin/nmo_messagemanager_schemas.h"
};

static const char *k_probe_prefixes[] = {
    "",
    "..\\",
    "..\\..\\",
    "..\\..\\..\\",
    "..\\..\\..\\..\\"
};

static int line_has_legacy_api(const char *line) {
    return strstr(line, "NMO_API") != NULL && strstr(line, "finish_loading") != NULL;
}

static void assert_file_has_no_legacy_api(const char *relative_path) {
    char full_path[512];
    FILE *fp = NULL;
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(full_path, sizeof(full_path), "%s%s", k_probe_prefixes[i], relative_path);
        fp = fopen(full_path, "rb");
        if (fp != NULL) {
            break;
        }
    }

    ASSERT_NOT_NULL(fp);
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        ASSERT_FALSE(line_has_legacy_api(line));
    }
    fclose(fp);
}

TEST(no_legacy_runtime_api_exports, builtin_headers_have_no_legacy_runtime_api_exports) {
    for (size_t i = 0; i < sizeof(k_builtin_headers) / sizeof(k_builtin_headers[0]); i++) {
        assert_file_has_no_legacy_api(k_builtin_headers[i]);
    }

    assert_file_has_no_legacy_api("include/object/nmo_object_type_common.h");
}

TEST_MAIN_BEGIN()
REGISTER_TEST(no_legacy_runtime_api_exports, builtin_headers_have_no_legacy_runtime_api_exports);
TEST_MAIN_END()
