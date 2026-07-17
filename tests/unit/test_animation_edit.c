#include "test_framework.h"

#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_animation_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

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

TEST(animation_edit, sets_object_animation_metadata)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "animation", &edit));

    nmo_object_id_t animation_id = 0;
    nmo_object_id_t entity_id = 0;
    nmo_object_id_t material_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "Animation",
                  },
                  &animation_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Entity",
                  },
                  &entity_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "Material",
                  },
                  &material_id));

    ASSERT_EQ(NMO_OK,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_CONTROLLERS,
                      .entity_id = entity_id,
                      .has_root_position = true,
                      .root_position = {1.0f, 2.0f, 3.0f},
                      .has_flags = true,
                      .flags = 7u,
                      .has_length = true,
                      .length = 12.5f,
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_animation_edit_set_object_animation(edit, animation_id, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_animation_edit_set_object_animation(
                  edit,
                  material_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_CONTROLLERS,
                      .entity_id = entity_id,
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_CONTROLLERS,
                      .entity_id = material_id,
                  }));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_CONTROLLERS,
                      .entity_id = 999999u,
                  }));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *animation_object = find_object(doc, animation_id);
    ASSERT_NOT_NULL(animation_object);
    const nmo_objectanimation_state_t *animation =
        (const nmo_objectanimation_state_t *)nmo_object_get_state(animation_object);
    ASSERT_NOT_NULL(animation);
    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS, animation->format);
    ASSERT_EQ(entity_id, nmo_ref_runtime_id(&animation->entity));
    ASSERT_TRUE(animation->has_root_pos);
    ASSERT_FLOAT_EQ(1.0f, animation->root_pos.x, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, animation->root_pos.y, 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, animation->root_pos.z, 0.0001f);
    ASSERT_EQ(7u, animation->flags);
    ASSERT_TRUE(animation->has_length);
    ASSERT_FLOAT_EQ(12.5f, animation->length, 0.0001f);

    destroy_workspace(ctx, doc, workspace);
}

TEST(animation_edit, sets_object_animation_controllers)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "animation", &edit));

    nmo_object_id_t animation_id = 0;
    nmo_object_id_t entity_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "Animation",
                  },
                  &animation_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Entity",
                  },
                  &entity_id));

    float position_keys[] = {
        0.0f, 1.0f, 2.0f, 3.0f,
        1.0f, 4.0f, 5.0f, 6.0f,
    };
    nmo_objanim_controller_t controllers[] = {
        {
            .type = 0x637c4301u,
            .key_count = 2u,
            .data_size = sizeof(position_keys),
            .data = position_keys,
        },
    };
    nmo_objanim_controller_t malformed[] = {
        {
            .type = 0x637c4301u,
            .key_count = 1u,
            .data_size = sizeof(position_keys) - sizeof(float),
            .data = position_keys,
        },
    };

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_CONTROLLERS,
                      .entity_id = entity_id,
                      .controller_count = 1u,
                      .controllers = malformed,
                  }));
    ASSERT_EQ(NMO_OK,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_CONTROLLERS,
                      .entity_id = entity_id,
                      .controller_count = 1u,
                      .controllers = controllers,
                  }));

    position_keys[1] = 99.0f;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *animation_object = find_object(doc, animation_id);
    ASSERT_NOT_NULL(animation_object);
    const nmo_objectanimation_state_t *animation =
        (const nmo_objectanimation_state_t *)nmo_object_get_state(animation_object);
    ASSERT_NOT_NULL(animation);
    ASSERT_EQ(CKOBJANIM_FORMAT_CONTROLLERS, animation->format);
    ASSERT_EQ(1u, animation->controller_count);
    ASSERT_NOT_NULL(animation->controllers);
    ASSERT_EQ(0x637c4301u, animation->controllers[0].type);
    ASSERT_EQ(2u, animation->controllers[0].key_count);
    ASSERT_EQ(sizeof(position_keys), animation->controllers[0].data_size);
    ASSERT_NOT_NULL(animation->controllers[0].data);
    const float *stored_keys = (const float *)animation->controllers[0].data;
    ASSERT_FLOAT_EQ(1.0f, stored_keys[1], 0.0001f);
    ASSERT_FLOAT_EQ(6.0f, stored_keys[7], 0.0001f);

    destroy_workspace(ctx, doc, workspace);
}

TEST(animation_edit, sets_object_animation_morph_keys)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "animation", &edit));

    nmo_object_id_t animation_id = 0;
    nmo_object_id_t entity_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "MorphAnimation",
                  },
                  &animation_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Entity",
                  },
                  &entity_id));

    float morph_payload[] = {
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
    };
    nmo_objanim_morph_key_t morph_keys[] = {
        {
            .time_step = 0.25f,
            .data_size = sizeof(morph_payload),
            .data = morph_payload,
        },
    };
    nmo_objanim_morph_key_t malformed[] = {
        {
            .time_step = 0.5f,
            .data_size = 0u,
            .data = morph_payload,
        },
    };

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_NEWDATA,
                      .entity_id = entity_id,
                      .morph_key_count = 1u,
                      .morph_keys = malformed,
                  }));
    ASSERT_EQ(NMO_OK,
              nmo_animation_edit_set_object_animation(
                  edit,
                  animation_id,
                  &(nmo_object_animation_settings_t){
                      .format = CKOBJANIM_FORMAT_NEWDATA,
                      .entity_id = entity_id,
                      .morph_key_count = 1u,
                      .morph_keys = morph_keys,
                  }));

    morph_payload[1] = 99.0f;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *animation_object = find_object(doc, animation_id);
    ASSERT_NOT_NULL(animation_object);
    const nmo_objectanimation_state_t *animation =
        (const nmo_objectanimation_state_t *)nmo_object_get_state(animation_object);
    ASSERT_NOT_NULL(animation);
    ASSERT_EQ(CKOBJANIM_FORMAT_NEWDATA, animation->format);
    ASSERT_EQ(1u, animation->morph_key_parsed_count);
    ASSERT_NOT_NULL(animation->morph_keys);
    ASSERT_FLOAT_EQ(0.25f, animation->morph_keys[0].time_step, 0.0001f);
    ASSERT_EQ(sizeof(morph_payload), animation->morph_keys[0].data_size);
    ASSERT_NOT_NULL(animation->morph_keys[0].data);
    const float *stored_payload = (const float *)animation->morph_keys[0].data;
    ASSERT_FLOAT_EQ(2.0f, stored_payload[1], 0.0001f);
    ASSERT_FLOAT_EQ(6.0f, stored_payload[5], 0.0001f);

    destroy_workspace(ctx, doc, workspace);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(animation_edit, sets_object_animation_metadata);
REGISTER_TEST(animation_edit, sets_object_animation_controllers);
REGISTER_TEST(animation_edit, sets_object_animation_morph_keys);
TEST_MAIN_END()
