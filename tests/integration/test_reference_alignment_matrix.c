#include "test_framework.h"
#include "session/nmo_context.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <string.h>

typedef struct reference_sample {
    const char *batch;
    const char *type_name;
    nmo_class_id_t class_id;
    const char *reference_path;
    const char *source_path;
} reference_sample_t;

static const reference_sample_t k_samples[] = {
    {"B0", "CKObject", 1, "reference/CK2/src/CKObject.cpp", "src/object/builtin/ckobject_schemas.c"},
    {"B0", "CKSceneObject", 11, "reference/CK2/src/CKSceneObject.cpp", "src/object/builtin/cksceneobject_schemas.c"},

    {"B1", "CKBehavior", 8, "reference/CK2/src/CKBehavior.cpp", "src/object/builtin/ckbehavior_schemas.c"},
    {"B1", "CKBehaviorIO", 9, "reference/CK2/src/CKBehaviorIO.cpp", "src/object/builtin/ckbehaviorio_schemas.c"},

    {"B2", "CKParameterIn", 2, "reference/CK2/src/CKParameterIn.cpp", "src/object/builtin/ckparameterin_schemas.c"},
    {"B2", "CKParameterOperation", 4, "reference/CK2/src/CKParameterOperation.cpp", "src/object/builtin/ckparameteroperation_schemas.c"},

    {"B3", "CKScene", 10, "reference/CK2/src/CKScene.cpp", "src/object/builtin/ckscene_schemas.c"},
    {"B3", "CKGroup", 23, "reference/CK2/src/CKGroup.cpp", "src/object/builtin/ckgroup_schemas.c"},

    {"B4", "CK2dEntity", 27, "reference/CKRenderEngine/src/CK2dEntity.cpp", "src/object/builtin/ck2dentity_schemas.c"},
    {"B4", "CKSprite", 28, "reference/CKRenderEngine/src/CKSprite.cpp", "src/object/builtin/cksprite_schemas.c"},

    {"B5", "CK3dEntity", 33, "reference/CKRenderEngine/src/CK3dEntity.cpp", "src/object/builtin/ck3dentity_schemas.c"},
    {"B5", "CKCamera", 34, "reference/CKRenderEngine/src/CKCamera.cpp", "src/object/builtin/ckcamera_schemas.c"},

    {"B6", "CKMaterial", 30, "reference/CKRenderEngine/src/CKMaterial.cpp", "src/object/builtin/ckmaterial_schemas.c"},
    {"B6", "CKMesh", 32, "reference/CKRenderEngine/src/CKMesh.cpp", "src/object/builtin/ckmesh_schemas.c"},

    {"B7", "CKAnimation", 16, "reference/CKRenderEngine/src/CKAnimation.cpp", "src/object/builtin/ckanimation_schemas.c"},
    {"B7", "CKObjectAnimation", 15, "reference/CKRenderEngine/src/CKObjectAnimation.cpp", "src/object/builtin/ckanimation_schemas.c"},

    {"B8", "CKRenderContext", 12, "reference/CKRenderEngine/src/CKRenderContext.cpp", "src/object/builtin/ckrendercontext_schemas.c"},
    {"B8", "CKSound", 24, "reference/CK2/src/CKSound.cpp", "src/object/builtin/cksound_schemas.c"},

    {"B9", "CKAttributeManager", 0, "reference/CK2/src/CKAttributeManager.cpp", "src/object/builtin/ckattributemanager_schemas.c"},
    {"B9", "CKMessageManager", 0, "reference/CK2/src/CKMessageManager.cpp", "src/object/builtin/ckmessagemanager_schemas.c"},
};

static const char *k_probe_prefixes[] = {
    "",
    "..\\",
    "..\\..\\",
    "..\\..\\..\\",
    "..\\..\\..\\..\\"
};

static FILE *open_with_probe_prefixes(const char *relative_path, char *resolved, size_t resolved_size) {
    for (size_t i = 0; i < sizeof(k_probe_prefixes) / sizeof(k_probe_prefixes[0]); i++) {
        snprintf(resolved, resolved_size, "%s%s", k_probe_prefixes[i], relative_path);
        FILE *fp = fopen(resolved, "rb");
        if (fp != NULL) {
            return fp;
        }
    }
    return NULL;
}

static int file_contains_text(const char *relative_path, const char *needle) {
    char path[512];
    FILE *fp = open_with_probe_prefixes(relative_path, path, sizeof(path));
    if (fp == NULL) {
        return 0;
    }

    char line[2048];
    int found = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, needle) != NULL) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

TEST(reference_alignment_matrix, batch_samples_have_reference_and_runtime_remap_hooks) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    ASSERT_NOT_NULL(registry);

    size_t batch_counts[10] = {0};
    for (size_t i = 0; i < sizeof(k_samples) / sizeof(k_samples[0]); i++) {
        const reference_sample_t *sample = &k_samples[i];
        ASSERT_TRUE(file_contains_text(sample->reference_path, "CK"));
        ASSERT_TRUE(file_contains_text(sample->source_path, ".c"));

        const int batch_index = sample->batch[1] - '0';
        ASSERT_TRUE(batch_index >= 0 && batch_index <= 9);
        batch_counts[(size_t)batch_index]++;

        if (sample->class_id > 0) {
            const nmo_type_descriptor_t *type =
                nmo_type_registry_find_by_class_id_inherited(registry, sample->class_id);
            ASSERT_NOT_NULL(type);
            ASSERT_NOT_NULL(type->vtable);
            ASSERT_NOT_NULL(type->vtable->remap_dependencies);
        } else {
            ASSERT_TRUE(file_contains_text(sample->source_path, "_remap_dependencies"));
            ASSERT_TRUE(file_contains_text(sample->source_path, "_vtable"));
        }
    }

    for (size_t i = 0; i < 10; i++) {
        ASSERT_TRUE(batch_counts[i] >= 2);
    }

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(reference_alignment_matrix, batch_samples_have_reference_and_runtime_remap_hooks);
TEST_MAIN_END()
