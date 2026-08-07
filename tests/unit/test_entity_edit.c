#include "test_framework.h"

#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_entity_edit.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_type_query.h"

typedef struct entity_edit_fixture {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} entity_edit_fixture_t;

static void entity_edit_fixture_destroy(entity_edit_fixture_t *fixture)
{
    if (!fixture) {
        return;
    }
    nmo_workspace_destroy(fixture->workspace);
    nmo_document_destroy(fixture->document);
    nmo_context_release(fixture->ctx);
}

static void entity_edit_fixture_create(entity_edit_fixture_t *fixture)
{
    fixture->ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->document = nmo_document_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->document);
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(fixture->ctx,
                                   fixture->document,
                                   &fixture->workspace));
}

static nmo_object_t *find_object(nmo_document_t *document, nmo_object_id_t id)
{
    nmo_object_t *object = NULL;
    if (nmo_object_query_find_first(
            document,
            &(nmo_object_query_t){.object_id = id},
            &object,
            NULL) != NMO_OK) {
        return NULL;
    }
    return object;
}

TEST(entity_edit, sets_parent)
{
    entity_edit_fixture_t fixture = {0};
    entity_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "parent", &edit));

    nmo_object_id_t parent_id = 0;
    nmo_object_id_t child_id = 0;
    nmo_object_id_t material_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Parent",
                  },
                  &parent_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Child",
                  },
                  &child_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "Material",
                  },
                  &material_id));

    ASSERT_EQ(NMO_OK,
              nmo_entity_edit_set_parent(edit, child_id, parent_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_parent(edit, material_id, parent_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_parent(edit, child_id, material_id));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_entity_edit_set_parent(edit, child_id, 999999u));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *child_object = find_object(fixture.document, child_id);
    ASSERT_NOT_NULL(child_object);
    const nmo_3dentity_state_t *child =
        (const nmo_3dentity_state_t *)nmo_object_get_state(child_object);
    ASSERT_NOT_NULL(child);
    ASSERT_EQ(parent_id, nmo_ref_runtime_id(&child->parent));
    ASSERT_FALSE(child->has_parent_chunk);

    entity_edit_fixture_destroy(&fixture);
}

TEST(entity_edit, sets_world_matrix)
{
    entity_edit_fixture_t fixture = {0};
    entity_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(fixture.workspace, "matrix", &edit));

    nmo_object_id_t entity_id = 0;
    nmo_object_id_t material_id = 0;
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

    const float matrix[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        5.0f, 6.0f, 7.0f, 1.0f,
    };
    ASSERT_EQ(NMO_OK,
              nmo_entity_edit_set_world_matrix(edit, entity_id, matrix));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_world_matrix(edit, material_id, matrix));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_entity_edit_set_world_matrix(edit, 999999u, matrix));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_world_matrix(edit, entity_id, NULL));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *entity_object = find_object(fixture.document, entity_id);
    ASSERT_NOT_NULL(entity_object);
    const nmo_3dentity_state_t *entity =
        (const nmo_3dentity_state_t *)nmo_object_get_state(entity_object);
    ASSERT_NOT_NULL(entity);
    for (size_t i = 0; i < 16u; ++i) {
        ASSERT_FLOAT_EQ(matrix[i], entity->world_matrix[i], 0.0001f);
    }
    ASSERT_FALSE(entity->has_matrix_chunk);

    entity_edit_fixture_destroy(&fixture);
}

TEST(entity_edit, edits_explicit_entity_types)
{
    entity_edit_fixture_t fixture = {0};
    entity_edit_fixture_create(&fixture);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_edit_begin(
                  fixture.workspace, "typed entities", &edit));

    nmo_object_id_t parent_id = 0u;
    nmo_object_id_t child_id = 0u;
    nmo_object_id_t camera_id = 0u;
    nmo_object_id_t light_id = 0u;
    nmo_object_id_t conflicting_camera_id = 0u;
    const nmo_object_create_desc_t typed_entity = {
        .class_id = NMO_CID_OBJECT,
        .type_guid = CKPGUID_3DENTITY,
    };
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = typed_entity.class_id,
            .name = "Typed parent",
            .type_guid = typed_entity.type_guid,
        },
        &parent_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = typed_entity.class_id,
            .name = "Typed child",
            .type_guid = typed_entity.type_guid,
        },
        &child_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_OBJECT,
            .name = "Typed target camera",
            .type_guid = CKPGUID_TARGETCAMERA,
        },
        &camera_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_OBJECT,
            .name = "Typed target light",
            .type_guid = CKPGUID_TARGETLIGHT,
        },
        &light_id));
    ASSERT_EQ(NMO_OK, nmo_object_edit_create(
        edit,
        &(nmo_object_create_desc_t){
            .class_id = NMO_CID_TARGETCAMERA,
            .name = "Conflicting camera",
            .type_guid = CKPGUID_MATERIAL,
        },
        &conflicting_camera_id));

    const float matrix[16] = {
        1.0f, 0.0f, 0.0f, 4.0f,
        0.0f, 1.0f, 0.0f, 5.0f,
        0.0f, 0.0f, 1.0f, 6.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    ASSERT_EQ(NMO_OK, nmo_entity_edit_set_parent(
        edit, child_id, parent_id));
    ASSERT_EQ(NMO_OK, nmo_entity_edit_set_world_matrix(
        edit, child_id, matrix));
    ASSERT_EQ(NMO_OK, nmo_entity_edit_set_camera_settings(
        edit,
        camera_id,
        &(nmo_entity_camera_settings_t){
            .fov = 0.75f,
            .near_plane = 0.25f,
            .far_plane = 500.0f,
        }));
    ASSERT_EQ(NMO_OK, nmo_entity_edit_set_camera_target(
        edit, camera_id, child_id));
    ASSERT_EQ(NMO_OK, nmo_entity_edit_set_light_settings(
        edit,
        light_id,
        &(nmo_entity_light_settings_t){
            .diffuse = {0.1f, 0.2f, 0.3f, 1.0f},
            .range = 25.0f,
            .type = VX_LIGHTSPOT,
        }));
    ASSERT_EQ(NMO_OK, nmo_entity_edit_set_light_target(
        edit, light_id, parent_id));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_entity_edit_set_camera_settings(
                  edit,
                  conflicting_camera_id,
                  &(nmo_entity_camera_settings_t){0}));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    const nmo_type_registry_t *registry =
        nmo_context_get_type_registry(fixture.ctx);
    nmo_3dentity_state_t *child = (nmo_3dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            find_object(fixture.document, child_id),
            CKPGUID_3DENTITY);
    nmo_camera_state_t *camera = (nmo_camera_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            find_object(fixture.document, camera_id),
            CKPGUID_CAMERA);
    nmo_targetcamera_state_t *target_camera = (nmo_targetcamera_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            find_object(fixture.document, camera_id),
            CKPGUID_TARGETCAMERA);
    nmo_light_state_t *light = (nmo_light_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            find_object(fixture.document, light_id),
            CKPGUID_LIGHT);
    nmo_targetlight_state_t *target_light = (nmo_targetlight_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry,
            find_object(fixture.document, light_id),
            CKPGUID_TARGETLIGHT);
    ASSERT_NOT_NULL(child);
    ASSERT_NOT_NULL(camera);
    ASSERT_NOT_NULL(target_camera);
    ASSERT_NOT_NULL(light);
    ASSERT_NOT_NULL(target_light);
    ASSERT_EQ(parent_id, nmo_ref_runtime_id(&child->parent));
    ASSERT_FLOAT_EQ(4.0f, child->world_matrix[3], 0.0001f);
    ASSERT_FLOAT_EQ(0.75f, camera->fov, 0.0001f);
    ASSERT_EQ(child_id, nmo_ref_runtime_id(&target_camera->target));
    ASSERT_FLOAT_EQ(25.0f, light->light_data.range, 0.0001f);
    ASSERT_EQ(parent_id, nmo_ref_runtime_id(&target_light->target));

    entity_edit_fixture_destroy(&fixture);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(entity_edit, sets_parent);
REGISTER_TEST(entity_edit, sets_world_matrix);
REGISTER_TEST(entity_edit, edits_explicit_entity_types);
TEST_MAIN_END()
