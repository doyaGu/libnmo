#include "test_framework.h"

#include "core/nmo_array.h"
#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_query.h"
#include "object/nmo_scene_edit.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

typedef struct scene_edit_fixture {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} scene_edit_fixture_t;

static void scene_edit_fixture_destroy(scene_edit_fixture_t *fixture)
{
    if (fixture == NULL) {
        return;
    }
    if (fixture->workspace != NULL) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document != NULL) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->ctx != NULL) {
        nmo_context_release(fixture->ctx);
    }
}

static void scene_edit_fixture_create(scene_edit_fixture_t *fixture)
{
    fixture->ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->document = nmo_document_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document, &fixture->workspace));
    ASSERT_NOT_NULL(fixture->workspace);
}

static nmo_object_t *find_one_or_null(nmo_document_t *document, nmo_object_id_t object_id)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            document,
            &(nmo_object_query_t){.object_id = object_id},
            &object,
            NULL) != NMO_OK) {
        return NULL;
    }
    return object;
}

static void create_committed_scene_and_object(
    scene_edit_fixture_t *fixture,
    nmo_object_id_t *out_scene_id,
    nmo_object_id_t *out_object_id)
{
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture->workspace, "create scene objects", &edit));

    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_SCENE,
                      .name = "Scene",
                      .type_guid = NMO_GUID_NULL,
                  },
                  out_scene_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                      .type_guid = NMO_GUID_NULL,
                  },
                  out_object_id));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));
    ASSERT_TRUE(*out_scene_id != 0);
    ASSERT_TRUE(*out_object_id != 0);
}

static void create_committed_scene_camera_and_object(
    scene_edit_fixture_t *fixture,
    nmo_object_id_t *out_scene_id,
    nmo_object_id_t *out_camera_id,
    nmo_object_id_t *out_object_id)
{
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture->workspace, "create scene camera", &edit));

    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_SCENE,
                      .name = "Scene",
                      .type_guid = NMO_GUID_NULL,
                  },
                  out_scene_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_CAMERA,
                      .name = "Camera",
                      .type_guid = NMO_GUID_NULL,
                  },
                  out_camera_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                      .type_guid = NMO_GUID_NULL,
                  },
                  out_object_id));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));
    ASSERT_TRUE(*out_scene_id != 0);
    ASSERT_TRUE(*out_camera_id != 0);
    ASSERT_TRUE(*out_object_id != 0);
}

TEST(scene_edit, adds_object_to_scene_membership) {
    scene_edit_fixture_t fixture = {0};
    scene_edit_fixture_create(&fixture);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t object_id = 0;
    create_committed_scene_and_object(&fixture, &scene_id, &object_id);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "add scene object", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_scene_edit_add_object(
                  edit,
                  scene_id,
                  object_id,
                  NMO_SCENE_MEMBERSHIP_ACTIVE | NMO_SCENE_MEMBERSHIP_START_ACTIVE));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *scene_object = find_one_or_null(fixture.document, scene_id);
    ASSERT_NOT_NULL(scene_object);
    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(1u, nmo_array_size(&scene_state->object_descs));
    nmo_scene_object_desc_t *desc =
        NMO_ARRAY_GET(nmo_scene_object_desc_t, &scene_state->object_descs, 0);
    ASSERT_NOT_NULL(desc);
    ASSERT_EQ(object_id, nmo_ref_runtime_id(&desc->ref));
    ASSERT_TRUE((desc->flags & CK_SCENEOBJECT_ACTIVE) != 0u);
    ASSERT_TRUE((desc->flags & CK_SCENEOBJECT_START_ACTIVATE) != 0u);

    scene_edit_fixture_destroy(&fixture);
}

TEST(scene_edit, rollback_removes_scene_membership) {
    scene_edit_fixture_t fixture = {0};
    scene_edit_fixture_create(&fixture);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t object_id = 0;
    create_committed_scene_and_object(&fixture, &scene_id, &object_id);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "rollback scene object", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_scene_edit_add_object(
                  edit,
                  scene_id,
                  object_id,
                  NMO_SCENE_MEMBERSHIP_ACTIVE));
    nmo_workspace_edit_rollback(edit);

    nmo_object_t *scene_object = find_one_or_null(fixture.document, scene_id);
    ASSERT_NOT_NULL(scene_object);
    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(0u, nmo_array_size(&scene_state->object_descs));
    ASSERT_NOT_NULL(find_one_or_null(fixture.document, object_id));

    scene_edit_fixture_destroy(&fixture);
}

TEST(scene_edit, rejects_invalid_membership) {
    scene_edit_fixture_t fixture = {0};
    scene_edit_fixture_create(&fixture);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t object_id = 0;
    create_committed_scene_and_object(&fixture, &scene_id, &object_id);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "invalid scene object", &edit));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_add_object(NULL, scene_id, object_id, NMO_SCENE_MEMBERSHIP_ACTIVE));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_add_object(edit, object_id, scene_id, NMO_SCENE_MEMBERSHIP_ACTIVE));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_scene_edit_add_object(edit, scene_id, 999999u, NMO_SCENE_MEMBERSHIP_ACTIVE));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_add_object(edit, scene_id, object_id, 1u << 31));
    nmo_workspace_edit_rollback(edit);

    scene_edit_fixture_destroy(&fixture);
}

TEST(scene_edit, sets_environment_preserving_unset_fields) {
    scene_edit_fixture_t fixture = {0};
    scene_edit_fixture_create(&fixture);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t object_id = 0;
    create_committed_scene_and_object(&fixture, &scene_id, &object_id);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "scene environment", &edit));
    nmo_session_field_edit_result_t field_result = {0};
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_set_fields(
                  edit,
                  scene_id,
                  (const nmo_session_field_edit_t[]){
                      {"background_color", "(0,0,0,1)"},
                      {"ambient_light_color", "(0.4,0.5,0.6,1)"},
                      {"fog_mode", "3"},
                      {"fog_color", "(0.7,0.8,0.9,1)"},
                      {"fog_start", "12"},
                      {"fog_end", "34"},
                      {"fog_density", "0.25"},
                  },
                  7u,
                  &field_result));
    ASSERT_EQ(0u, field_result.failed);

    ASSERT_EQ(NMO_OK,
              nmo_scene_edit_set_environment(
                  edit,
                  scene_id,
                  &(nmo_scene_environment_settings_t){
                      .has_background_color = true,
                      .background_color = {0.1f, 0.2f, 0.3f, 1.0f},
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_set_environment(
                  edit,
                  object_id,
                  &(nmo_scene_environment_settings_t){
                      .has_background_color = true,
                      .background_color = {0.1f, 0.2f, 0.3f, 1.0f},
                  }));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_set_environment(edit, scene_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *scene_object = find_one_or_null(fixture.document, scene_id);
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

    scene_edit_fixture_destroy(&fixture);
}

TEST(scene_edit, sets_active_camera)
{
    scene_edit_fixture_t fixture = {0};
    scene_edit_fixture_create(&fixture);

    nmo_object_id_t scene_id = 0;
    nmo_object_id_t camera_id = 0;
    nmo_object_id_t object_id = 0;
    create_committed_scene_camera_and_object(
        &fixture,
        &scene_id,
        &camera_id,
        &object_id);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "active camera", &edit));
    ASSERT_EQ(NMO_OK,
              nmo_scene_edit_set_active_camera(edit, scene_id, camera_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_set_active_camera(edit, object_id, camera_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_scene_edit_set_active_camera(edit, scene_id, object_id));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_scene_edit_set_active_camera(edit, scene_id, 999999u));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *scene_object = find_one_or_null(fixture.document, scene_id);
    ASSERT_NOT_NULL(scene_object);
    const nmo_scene_state_t *scene_state =
        (const nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(camera_id, nmo_ref_runtime_id(&scene_state->starting_camera));

    scene_edit_fixture_destroy(&fixture);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(scene_edit, adds_object_to_scene_membership);
REGISTER_TEST(scene_edit, rollback_removes_scene_membership);
REGISTER_TEST(scene_edit, rejects_invalid_membership);
REGISTER_TEST(scene_edit, sets_environment_preserving_unset_fields);
REGISTER_TEST(scene_edit, sets_active_camera);
TEST_MAIN_END()
