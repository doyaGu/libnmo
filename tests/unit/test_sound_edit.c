#include "test_framework.h"

#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_query.h"
#include "object/nmo_sound_edit.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_type_query.h"

static void create_workspace(
    nmo_context_t **out_ctx,
    nmo_document_t **out_doc,
    nmo_workspace_t **out_workspace)
{
    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *doc = nmo_document_create(ctx);
    ASSERT_NOT_NULL(doc);
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, doc, &workspace));
    *out_ctx = ctx;
    *out_doc = doc;
    *out_workspace = workspace;
}

static void destroy_workspace(
    nmo_context_t *ctx,
    nmo_document_t *doc,
    nmo_workspace_t *workspace)
{
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(doc);
    nmo_context_release(ctx);
}

static nmo_object_t *find_object(nmo_document_t *doc, nmo_object_id_t id)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            doc,
            &(nmo_object_query_t){.object_id = id},
            &object,
            NULL) != NMO_OK) {
        return NULL;
    }
    return object;
}

TEST(sound_edit, sets_sound_file)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "sound", &edit));
    nmo_object_id_t sound_id = 0;
    nmo_object_id_t material_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_SOUND,
                      .name = "Sound",
                  },
                  &sound_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "Material",
                  },
                  &material_id));

    ASSERT_EQ(NMO_OK,
              nmo_sound_edit_set_sound(
                  edit,
                  sound_id,
                  &(nmo_sound_edit_settings_t){
                      .file_path = "assets/tone.wav",
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_sound_edit_set_sound(edit, sound_id, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_sound_edit_set_sound(
                  edit,
                  material_id,
                  &(nmo_sound_edit_settings_t){
                      .file_path = "assets/tone.wav",
                  }));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *sound_object = find_object(doc, sound_id);
    ASSERT_NOT_NULL(sound_object);
    const nmo_sound_state_t *sound =
        (const nmo_sound_state_t *)nmo_object_get_state(sound_object);
    ASSERT_NOT_NULL(sound);
    ASSERT_EQ(CKSOUND_INCLUDEORIGINALFILE, sound->save_options);
    ASSERT_STR_EQ("assets/tone.wav", sound->file_name);

    destroy_workspace(ctx, doc, workspace);
}

TEST(sound_edit, sets_wavesound_authoring)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "wavesound", &edit));
    nmo_object_id_t sound_id = 0;
    nmo_object_id_t anchor_id = 0;
    nmo_object_id_t material_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "Wave",
                  },
                  &sound_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Anchor",
                  },
                  &anchor_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "Material",
                  },
                  &material_id));

    ASSERT_EQ(NMO_OK,
              nmo_sound_edit_set_sound(
                  edit,
                  sound_id,
                  &(nmo_sound_edit_settings_t){
                      .file_path = "tone.wav",
                      .has_gain = true,
                      .gain = 0.75f,
                      .has_pan = true,
                      .pan = -0.5f,
                      .has_pitch = true,
                      .pitch = 1.25f,
                      .has_attached_object = true,
                      .attached_object_id = anchor_id,
                      .has_position = true,
                      .position = {1.0f, 2.0f, 3.0f},
                      .has_direction = true,
                      .direction = {0.0f, 0.0f, -1.0f},
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_sound_edit_set_sound(
                  edit,
                  sound_id,
                  &(nmo_sound_edit_settings_t){
                      .has_attached_object = true,
                      .attached_object_id = material_id,
                  }));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_sound_edit_set_sound(
                  edit,
                  sound_id,
                  &(nmo_sound_edit_settings_t){
                      .has_attached_object = true,
                      .attached_object_id = 999999u,
                  }));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *sound_object = find_object(doc, sound_id);
    ASSERT_NOT_NULL(sound_object);
    const nmo_wavesound_state_t *sound =
        (const nmo_wavesound_state_t *)nmo_object_get_state(sound_object);
    ASSERT_NOT_NULL(sound);
    ASSERT_TRUE(sound->has_wave_file_name);
    ASSERT_STR_EQ("tone.wav", sound->wave_file_name);
    ASSERT_TRUE(sound->has_data2);
    ASSERT_FLOAT_EQ(0.75f, sound->gain, 0.0001f);
    ASSERT_FLOAT_EQ(-0.5f, sound->pan, 0.0001f);
    ASSERT_FLOAT_EQ(1.25f, sound->pitch, 0.0001f);
    ASSERT_EQ(anchor_id, nmo_ref_runtime_id(&sound->attached_object));
    ASSERT_FLOAT_EQ(1.0f, sound->position.x, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, sound->position.y, 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, sound->position.z, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, sound->direction.x, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, sound->direction.y, 0.0001f);
    ASSERT_FLOAT_EQ(-1.0f, sound->direction.z, 0.0001f);

    destroy_workspace(ctx, doc, workspace);
}

TEST(sound_edit, edits_explicit_sound_types)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "typed sound", &edit));
    nmo_object_id_t sound_id = 0u;
    nmo_object_id_t wave_id = 0u;
    nmo_object_id_t anchor_id = 0u;
    nmo_object_id_t conflicting_id = 0u;
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_OBJECT,
            .name = "Typed sound",
            .type_guid = CKPGUID_SOUND,
        },
        &sound_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_OBJECT,
            .name = "Typed wave",
            .type_guid = CKPGUID_WAVESOUND,
        },
        &wave_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_OBJECT,
            .name = "Typed anchor",
            .type_guid = CKPGUID_3DENTITY,
        },
        &anchor_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_WAVESOUND,
            .name = "Conflicting wave",
            .type_guid = CKPGUID_MATERIAL,
        },
        &conflicting_id));

    ASSERT_EQ(NMO_OK, nmo_sound_edit_set_sound(
        edit,
        sound_id,
        &(nmo_sound_edit_settings_t){.file_path = "typed-sound.wav"}));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_sound_edit_set_sound(
        edit,
        sound_id,
        &(nmo_sound_edit_settings_t){.has_gain = true, .gain = 0.5f}));
    ASSERT_EQ(NMO_OK, nmo_sound_edit_set_sound(
        edit,
        wave_id,
        &(nmo_sound_edit_settings_t){
            .file_path = "typed-wave.wav",
            .has_gain = true,
            .gain = 0.5f,
            .has_attached_object = true,
            .attached_object_id = anchor_id,
        }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_sound_edit_set_sound(
        edit,
        conflicting_id,
        &(nmo_sound_edit_settings_t){.file_path = "wrong.wav"}));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_sound_state_t *sound = (nmo_sound_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, find_object(doc, sound_id), CKPGUID_SOUND);
    nmo_wavesound_state_t *wave = (nmo_wavesound_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, find_object(doc, wave_id), CKPGUID_WAVESOUND);
    ASSERT_NOT_NULL(sound);
    ASSERT_NOT_NULL(wave);
    ASSERT_STR_EQ("typed-sound.wav", sound->file_name);
    ASSERT_STR_EQ("typed-wave.wav", wave->wave_file_name);
    ASSERT_FLOAT_EQ(0.5f, wave->gain, 0.0001f);
    ASSERT_EQ(anchor_id, nmo_ref_runtime_id(&wave->attached_object));

    destroy_workspace(ctx, doc, workspace);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(sound_edit, sets_sound_file);
REGISTER_TEST(sound_edit, sets_wavesound_authoring);
REGISTER_TEST(sound_edit, edits_explicit_sound_types);
TEST_MAIN_END()
