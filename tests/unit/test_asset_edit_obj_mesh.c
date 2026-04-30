#include "test_framework.h"

#include "core/nmo_arena.h"
#include "document/nmo_document.h"
#include "format/nmo_obj_parser.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_asset_edit.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

static void parse_obj_or_fail(
    nmo_arena_t *arena,
    const char *text,
    nmo_obj_data_t *out_data)
{
    ASSERT_EQ(NMO_OK, nmo_obj_parse(arena, text, strlen(text), out_data));
}

TEST(asset_edit_obj_mesh, imports_triangle_from_parsed_obj)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj mesh", &edit));
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MESH,
                      .name = "TriangleMesh",
                  },
                  &mesh_id));

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(parse_arena);
    nmo_obj_data_t obj = {0};
    parse_obj_or_fail(
        parse_arena,
        "v 0 0 0 1 0 0\n"
        "v 1 0 0 0 1 0\n"
        "v 0 1 0 0 0 1\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\n",
        &obj);

    ASSERT_EQ(NMO_OK, nmo_asset_edit_set_obj_mesh(edit, mesh_id, &obj, NULL));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *mesh_object = find_object(doc, mesh_id);
    ASSERT_NOT_NULL(mesh_object);
    const nmo_mesh_state_t *mesh =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ(3u, mesh->vertex_count);
    ASSERT_EQ(1u, mesh->face_count);
    ASSERT_EQ(0u, mesh->line_count);
    ASSERT_EQ(1u, mesh->material_group_count);
    ASSERT_NOT_NULL(mesh->vertices);
    ASSERT_NOT_NULL(mesh->face_vertex_indices);
    ASSERT_NOT_NULL(mesh->vertex_colors);
    ASSERT_EQ(0xFFFF0000u, mesh->vertex_colors[0]);
    ASSERT_EQ(0xFF00FF00u, mesh->vertex_colors[1]);
    ASSERT_EQ(0xFF0000FFu, mesh->vertex_colors[2]);
    ASSERT_EQ(0u, mesh->face_vertex_indices[0]);
    ASSERT_EQ(1u, mesh->face_vertex_indices[1]);
    ASSERT_EQ(2u, mesh->face_vertex_indices[2]);
    ASSERT_FLOAT_EQ(0.5f, mesh->bary_center.x, 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, mesh->bary_center.y, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, mesh->bary_center.z, 0.0001f);

    nmo_arena_destroy(parse_arena);
    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_obj_mesh, binds_named_obj_materials)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj material", &edit));
    nmo_object_id_t material_id = 0;
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MATERIAL,
                      .name = "Red",
                  },
                  &material_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){
                      .class_id = NMO_CID_MESH,
                      .name = "MaterialMesh",
                  },
                  &mesh_id));

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(parse_arena);
    nmo_obj_data_t obj = {0};
    parse_obj_or_fail(
        parse_arena,
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "usemtl Red\n"
        "f 1 2 3\n",
        &obj);

    nmo_asset_mesh_material_binding_t bindings[] = {
        {.name = "Red", .material_id = material_id},
    };
    ASSERT_EQ(NMO_OK,
              nmo_asset_edit_set_obj_mesh(
                  edit,
                  mesh_id,
                  &obj,
                  &(nmo_asset_mesh_import_options_t){
                      .materials = bindings,
                      .material_count = 1u,
                  }));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *mesh_object = find_object(doc, mesh_id);
    ASSERT_NOT_NULL(mesh_object);
    const nmo_mesh_state_t *mesh =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ(1u, mesh->material_group_count);
    ASSERT_NOT_NULL(mesh->material_groups);
    ASSERT_EQ(material_id, mesh->material_groups[0].material_id);

    nmo_arena_destroy(parse_arena);
    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_obj_mesh, rejects_wrong_class_material_binding)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj bad material", &edit));
    nmo_object_id_t wrong_id = 0;
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_CAMERA, .name = "Camera"},
                  &wrong_id));
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MESH, .name = "Mesh"},
                  &mesh_id));

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(parse_arena);
    nmo_obj_data_t obj = {0};
    parse_obj_or_fail(parse_arena, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", &obj);

    ASSERT_NE(NMO_OK,
              nmo_asset_edit_set_obj_mesh(
                  edit,
                  mesh_id,
                  &obj,
                  &(nmo_asset_mesh_import_options_t){.default_material_id = wrong_id}));

    nmo_workspace_edit_rollback(edit);
    nmo_arena_destroy(parse_arena);
    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_obj_mesh, rejects_missing_face_array)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj missing faces", &edit));
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MESH, .name = "Mesh"},
                  &mesh_id));

    nmo_obj_data_t obj = {
        .face_count = 1u,
        .faces = NULL,
    };
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_asset_edit_set_obj_mesh(edit, mesh_id, &obj, NULL));

    nmo_workspace_edit_rollback(edit);
    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_obj_mesh, rejects_overflowing_line_count)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj overflow", &edit));
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MESH, .name = "Mesh"},
                  &mesh_id));

    nmo_obj_line_t line = {0};
    nmo_obj_data_t obj = {
        .line_count = SIZE_MAX / 2u + 2u,
        .lines = &line,
    };
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_asset_edit_set_obj_mesh(edit, mesh_id, &obj, NULL));

    nmo_workspace_edit_rollback(edit);
    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_obj_mesh, rejects_overflowing_face_count)
{
    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj face overflow", &edit));
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MESH, .name = "Mesh"},
                  &mesh_id));

    nmo_obj_face_t face = {0};
    nmo_obj_data_t obj = {
        .face_count = SIZE_MAX / 3u + 1u,
        .faces = &face,
    };
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_asset_edit_set_obj_mesh(edit, mesh_id, &obj, NULL));

    nmo_workspace_edit_rollback(edit);
    destroy_workspace(ctx, doc, workspace);
}

TEST(asset_edit_obj_mesh, imports_obj_from_file)
{
    const char *path = "test_asset_edit_obj_mesh_tmp.obj";
    remove(path);
    FILE *fp = fopen(path, "wb");
    ASSERT_NOT_NULL(fp);
    fputs("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", fp);
    fclose(fp);

    nmo_context_t *ctx = NULL;
    nmo_document_t *doc = NULL;
    nmo_workspace_t *workspace = NULL;
    create_workspace(&ctx, &doc, &workspace);

    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_begin(workspace, "obj file", &edit));
    nmo_object_id_t mesh_id = 0;
    ASSERT_EQ(NMO_OK,
              nmo_object_edit_create(
                  edit,
                  &(nmo_object_create_desc_t){.class_id = NMO_CID_MESH, .name = "FileMesh"},
                  &mesh_id));

    ASSERT_EQ(NMO_OK, nmo_asset_edit_set_obj_mesh_from_file(edit, mesh_id, path, NULL));
    ASSERT_EQ(NMO_OK, nmo_workspace_edit_commit(edit));

    nmo_object_t *mesh_object = find_object(doc, mesh_id);
    ASSERT_NOT_NULL(mesh_object);
    const nmo_mesh_state_t *mesh =
        (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
    ASSERT_NOT_NULL(mesh);
    ASSERT_EQ(3u, mesh->vertex_count);
    ASSERT_EQ(1u, mesh->face_count);

    destroy_workspace(ctx, doc, workspace);
    remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(asset_edit_obj_mesh, imports_triangle_from_parsed_obj);
REGISTER_TEST(asset_edit_obj_mesh, binds_named_obj_materials);
REGISTER_TEST(asset_edit_obj_mesh, rejects_wrong_class_material_binding);
REGISTER_TEST(asset_edit_obj_mesh, rejects_missing_face_array);
REGISTER_TEST(asset_edit_obj_mesh, rejects_overflowing_line_count);
REGISTER_TEST(asset_edit_obj_mesh, rejects_overflowing_face_count);
REGISTER_TEST(asset_edit_obj_mesh, imports_obj_from_file);
TEST_MAIN_END()
