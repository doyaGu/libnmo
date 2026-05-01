#include "test_framework.h"

#include "core/nmo_arena.h"
#include "document/nmo_document.h"
#include "format/nmo_object.h"
#include "format/nmo_stb_adapter.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_asset_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

#include <stdint.h>
#include <stdio.h>

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

TEST(asset_edit_texture, replaces_rgba_texture_and_binds_material_slot_zero)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "texture asset", &edit));
    nmo_object_id_t material_id = 0;
    nmo_object_id_t texture_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MATERIAL, .name = "Mat"},
                  &material_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_TEXTURE, .name = "Tex"},
                  &texture_id));

    const uint8_t pixels[] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
    };
    ASSERT_EQ(NMO_OK,
              nmo_asset_edit_set_texture_rgba(edit, texture_id, pixels, 2u, 1u));
    ASSERT_EQ(NMO_OK,
              nmo_asset_edit_bind_material_texture(edit, material_id, texture_id, 0u));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *texture_object = find_object(doc, texture_id);
    ASSERT_NOT_NULL(texture_object);
    const nmo_texture_state_t *texture =
        (const nmo_texture_state_t *)nmo_object_get_state(texture_object);
    ASSERT_NOT_NULL(texture);
    ASSERT_EQ(2, texture->reader_width);
    ASSERT_EQ(1, texture->reader_height);
    ASSERT_NOT_NULL(texture->reader_slots);
    ASSERT_TRUE(texture->reader_slots[0].data_size > 0u);

    nmo_object_t *material_object = find_object(doc, material_id);
    ASSERT_NOT_NULL(material_object);
    const nmo_material_state_t *material =
        (const nmo_material_state_t *)nmo_object_get_state(material_object);
    ASSERT_NOT_NULL(material);
    ASSERT_EQ(texture_id, material->texture_ids[0]);

    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_texture, binds_material_texture_slot_one)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "texture slot one", &edit));
    nmo_object_id_t material_id = 0;
    nmo_object_id_t texture_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MATERIAL, .name = "Mat"},
                  &material_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_TEXTURE, .name = "Tex"},
                  &texture_id));

    ASSERT_EQ(NMO_OK,
              nmo_asset_edit_bind_material_texture(edit, material_id, texture_id, 1u));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *material_object = find_object(doc, material_id);
    ASSERT_NOT_NULL(material_object);
    const nmo_material_state_t *material =
        (const nmo_material_state_t *)nmo_object_get_state(material_object);
    ASSERT_NOT_NULL(material);
    ASSERT_EQ(texture_id, material->texture_ids[1]);
    ASSERT_TRUE(material->has_additional_textures);

    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_texture, replaces_texture_from_file)
{
    const char *path = "test_asset_edit_texture_tmp.png";
    remove(path);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    const uint8_t pixels[] = {
        0u, 0u, 255u, 255u,
    };
    size_t png_size = 0u;
    uint8_t *png = nmo_stbi_write_to_memory(
        arena,
        NMO_BITMAP_FORMAT_PNG,
        1,
        1,
        4,
        pixels,
        90,
        &png_size);
    ASSERT_NOT_NULL(png);
    ASSERT_TRUE(png_size > 0u);
    FILE *fp = fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    ASSERT_EQ(png_size, fwrite(png, 1u, png_size, fp));
    fclose(fp);

    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "texture file", &edit));
    nmo_object_id_t texture_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_TEXTURE, .name = "Tex"},
                  &texture_id));
    ASSERT_EQ(NMO_OK, nmo_asset_edit_set_texture_from_file(edit, texture_id, path));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *texture_object = find_object(doc, texture_id);
    ASSERT_NOT_NULL(texture_object);
    const nmo_texture_state_t *texture =
        (const nmo_texture_state_t *)nmo_object_get_state(texture_object);
    ASSERT_NOT_NULL(texture);
    ASSERT_EQ(1, texture->reader_width);
    ASSERT_EQ(1, texture->reader_height);

    destroy_workspace(ctx, doc, workspace);
    nmo_arena_destroy(arena);
    remove(path);
}

TEST(asset_edit_texture, rejects_invalid_material_texture_binding)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "bad texture bind", &edit));
    nmo_object_id_t material_id = 0;
    nmo_object_id_t texture_id = 0;
    nmo_object_id_t camera_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MATERIAL, .name = "Mat"},
                  &material_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_TEXTURE, .name = "Tex"},
                  &texture_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_CAMERA, .name = "Camera"},
                  &camera_id));

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_asset_edit_bind_material_texture(edit, material_id, texture_id, 4u));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_asset_edit_bind_material_texture(edit, camera_id, texture_id, 0u));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_asset_edit_bind_material_texture(edit, material_id, camera_id, 0u));
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_asset_edit_bind_material_texture(edit, material_id, 99999u, 0u));

    nmo_workspace_edit_rollback(edit);
    destroy_workspace(ctx, doc, workspace);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(asset_edit_texture, replaces_rgba_texture_and_binds_material_slot_zero);
REGISTER_TEST(asset_edit_texture, binds_material_texture_slot_one);
REGISTER_TEST(asset_edit_texture, replaces_texture_from_file);
REGISTER_TEST(asset_edit_texture, rejects_invalid_material_texture_binding);
TEST_MAIN_END()
