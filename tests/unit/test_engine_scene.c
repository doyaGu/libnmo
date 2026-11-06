#include "engine_fixture.h"

#include "engine/nmo_engine_render.h"
#include "document/nmo_document_load.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"

#define GUID_RESTORE_IC NMO_GUID(0x766e4e44, 0x4fac6d52)

static void place(engine_fixture_t *f, nmo_object_id_t id, float x, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = fixture_state(f, id);
    ASSERT_NOT_NULL(state);
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = state->world_matrix[15] = 1;
    state->world_matrix[12] = x;
    state->parent = nmo_ref_from_id(parent);
}

static void save_ic(engine_fixture_t *f, nmo_class_id_t class_id, const void *state,
                     const nmo_type_vtable_t *vtable, nmo_chunk_t **out)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = class_id;
    nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena, f->repo, CK_STATESAVE_ALL);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, vtable->serialize(state, chunk, NULL, &context));
    nmo_chunk_close(chunk);
    *out = chunk;
}

static void save_entity_ic(engine_fixture_t *f, nmo_object_id_t id, float x,
                            nmo_object_id_t parent, bool visible, nmo_chunk_t **out)
{
    nmo_3dentity_state_t saved = *(nmo_3dentity_state_t *)fixture_state(f, id);
    saved.world_matrix[12] = x;
    saved.parent = nmo_ref_from_id(parent);
    saved.base.base.base.base.visibility_flags = visible ? NMO_CKOBJECT_VISIBLE : 0;
    save_ic(f, NMO_CID_3DENTITY, &saved, &nmo_3dentity_vtable, out);
}

static void install_scene(engine_fixture_t *f, const nmo_object_id_t *ids,
                           nmo_chunk_t *const *ics, size_t count)
{
    nmo_object_id_t level_id = fixture_create(f, NMO_CID_LEVEL, "Level");
    nmo_level_state_t *level = fixture_state(f, level_id);
    ASSERT_NOT_NULL(level);
    nmo_scene_state_t scene;
    memset(&scene, 0, sizeof(scene));
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&scene, NULL, NULL));
    scene.level = nmo_ref_from_id(level_id);
    nmo_scene_object_desc_t *descs = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&scene.object_descs, count, (void **)&descs));
    for (size_t i = 0; i < count; i++) {
        descs[i].ref = nmo_ref_from_id(ids[i]);
        descs[i].flags = CK_SCENEOBJECT_START_ACTIVATE;
        descs[i].initial_value = ics[i];
        nmo_object_t *object = nmo_object_repository_find_by_id(f->repo, ids[i]);
        if (object != NULL && object->class_id == NMO_CID_CAMERA) scene.starting_camera = descs[i].ref;
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_SCENE;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_serialize_context_t context = nmo_serialize_context_create(arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(&scene, chunk, NULL, &context));
    nmo_chunk_close(chunk);
    level->level_scene_chunk = chunk;
    nmo_scene_vtable.destroy(&scene, NULL, NULL);
}

TEST(engine_scene, launch_and_restore_use_the_saved_initial_value)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    place(&f, f.owner_id, 50, 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    nmo_chunk_t *ic = NULL;
    save_entity_ic(&f, f.owner_id, 3, 0, false, &ic);
    nmo_object_id_t ids[] = {f.owner_id, f.script_id};
    nmo_chunk_t *ics[] = {ic, NULL};
    install_scene(&f, ids, ics, 2);
    fixture_begin_edit(&f);
    nmo_object_id_t restore = fixture_block(&f, GUID_RESTORE_IC, "Restore");
    ASSERT_TRUE(restore != 0);
    fixture_input_int(&f, fixture_target(&f, restore), (int32_t)f.owner_id);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, restore, 0), 0);
    fixture_commit(&f, NULL);

    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
    ASSERT_FLOAT_EQ(50, world[12], 0.0001f);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_SCENE_DEFAULT));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
    ASSERT_FLOAT_EQ(3, world[12], 0.0001f);
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, f.owner_id));
    world[12] = 100;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, f.owner_id, world, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
    ASSERT_FLOAT_EQ(3, world[12], 0.0001f);
    const nmo_3dentity_state_t *loaded = fixture_state(&f, f.owner_id);
    ASSERT_FLOAT_EQ(50, loaded->world_matrix[12], 0.0001f);
    fixture_end(&f);
}

TEST(engine_scene, partial_initial_value_keeps_unsaved_transform_members)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    place(&f, f.owner_id, 50, 0);
    place(&f, child, 51, f.owner_id);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    nmo_chunk_t *ic = nmo_chunk_create(nmo_session_get_arena(f.session));
    ASSERT_NOT_NULL(ic);
    ic->class_id = NMO_CID_3DENTITY;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(ic));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(ic, CK_STATESAVE_OBJECTHIDDEN));
    nmo_chunk_close(ic);
    nmo_object_id_t ids[] = {f.owner_id, child, f.script_id};
    nmo_chunk_t *ics[] = {NULL, ic, NULL};
    install_scene(&f, ids, ics, 3);
    fixture_begin_edit(&f);
    nmo_object_id_t restore = fixture_block(&f, GUID_RESTORE_IC, "Restore");
    fixture_input_int(&f, fixture_target(&f, restore), (int32_t)child);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, restore, 0), 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, child));
    ASSERT_EQ(f.owner_id, nmo_engine_entity_parent(f.engine, child));
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, child, world));
    ASSERT_FLOAT_EQ(51, world[12], 0.0001f);
    world[12] = 90;
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, child, world, true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, child, world));
    ASSERT_FLOAT_EQ(90, world[12], 0.0001f);
    ASSERT_EQ(f.owner_id, nmo_engine_entity_parent(f.engine, child));
    fixture_end(&f);
}

TEST(engine_scene, malformed_initial_value_fails_start_without_changing_the_object)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    place(&f, f.owner_id, 50, 0);
    nmo_chunk_t *ic = nmo_chunk_create(nmo_session_get_arena(f.session));
    ASSERT_NOT_NULL(ic);
    ic->class_id = NMO_CID_3DENTITY;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(ic));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(ic, CK_STATESAVE_3DENTITYNDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(ic, 0));
    nmo_chunk_close(ic);
    nmo_object_id_t ids[] = {f.owner_id};
    nmo_chunk_t *ics[] = {ic};
    install_scene(&f, ids, ics, 1);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_NE(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_SCENE_DEFAULT));
    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_engine_step(f.engine, 10));
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, f.owner_id, world));
    ASSERT_FLOAT_EQ(50, world[12], 0.0001f);
    fixture_end(&f);
}

TEST(engine_scene, saved_file_initial_values_restore_references_arrays_and_render_members)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t child = fixture_create(&f, NMO_CID_3DENTITY, "Child");
    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "Table");
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t light = fixture_create(&f, NMO_CID_LIGHT, "Light");
    place(&f, f.owner_id, 50, 0);
    place(&f, child, 51, f.owner_id);
    place(&f, camera, 0, 0);
    place(&f, light, 0, 0);
    nmo_object_id_t ids[] = {f.owner_id, child, array, camera, material, light};
    nmo_chunk_t *ics[6] = {0};
    save_entity_ic(&f, f.owner_id, 3, 0, true, &ics[0]);
    save_entity_ic(&f, child, 4, f.owner_id, true, &ics[1]);
    nmo_dataarray_state_t table = *(nmo_dataarray_state_t *)fixture_state(&f, array);
    nmo_dataarray_column_format_t columns[] = {
        {.name = "Value", .type = CKARRAYTYPE_INT}, {.name = "Object", .type = CKARRAYTYPE_OBJECT},
    };
    nmo_dataarray_cell_t cells[2] = {0};
    cells[0].int_value = 7;
    cells[1].object_ref = nmo_ref_from_id(child);
    nmo_dataarray_row_t row = {.column_count = 2, .cells = cells};
    table.column_count = 2;
    table.column_formats = columns;
    table.row_count = 1;
    table.rows = &row;
    table.key_column = -1;
    save_ic(&f, NMO_CID_DATAARRAY, &table, &nmo_dataarray_vtable, &ics[2]);
    nmo_camera_state_t saved_camera = *(nmo_camera_state_t *)fixture_state(&f, camera);
    saved_camera.projection_type = CK_ORTHOGRAPHICPROJECTION;
    saved_camera.orthographic_zoom = 2.5f;
    saved_camera.near_plane = 2;
    saved_camera.far_plane = 333;
    save_ic(&f, NMO_CID_CAMERA, &saved_camera, &nmo_camera_vtable, &ics[3]);
    nmo_material_state_t *loaded_material = fixture_state(&f, material);
    loaded_material->diffuse_color = 0xff0000ffu;
    nmo_material_state_t saved_material = *loaded_material;
    saved_material.diffuse_color = 0xffff0000u;
    save_ic(&f, NMO_CID_MATERIAL, &saved_material, &nmo_material_vtable, &ics[4]);
    nmo_light_state_t *loaded_light = fixture_state(&f, light);
    loaded_light->flags &= ~NMO_LIGHT_FLAG_ACTIVE;
    nmo_light_state_t saved_light = *loaded_light;
    saved_light.flags |= NMO_LIGHT_FLAG_ACTIVE;
    saved_light.light_data.diffuse = (nmo_color_t){.r = 0, .g = 1, .b = 0, .a = 1};
    save_ic(&f, NMO_CID_LIGHT, &saved_light, &nmo_light_vtable, &ics[5]);
    install_scene(&f, ids, ics, 6);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    const char *path = NMO_TEST_SCRATCH_FILE("engine_scene_initial_values.cmo");
    ASSERT_EQ(NMO_OK, nmo_session_save_file(f.session, path, NULL, NULL));
    nmo_engine_destroy(f.engine);
    f.engine = NULL;
    nmo_workspace_destroy(f.workspace);
    f.workspace = NULL;
    nmo_document_destroy(f.document);
    f.document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(f.ctx, path, NULL, &f.document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(f.ctx, f.document, &f.workspace));
    ASSERT_EQ(NMO_OK, nmo_engine_create(f.workspace, NULL, &f.engine));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_SCENE_DEFAULT));
    nmo_object_id_t owner_id = nmo_engine_object_named(f.engine, "Owner", NMO_CID_3DENTITY);
    nmo_object_id_t child_id = nmo_engine_object_named(f.engine, "Child", NMO_CID_3DENTITY);
    nmo_object_id_t table_id = nmo_engine_object_named(f.engine, "Table", NMO_CID_DATAARRAY);
    nmo_object_id_t material_id = nmo_engine_object_named(f.engine, "Material", NMO_CID_MATERIAL);
    ASSERT_TRUE(owner_id != 0 && child_id != 0 && table_id != 0 && material_id != 0);
    ASSERT_EQ(owner_id, nmo_engine_entity_parent(f.engine, child_id));
    float world[16];
    ASSERT_TRUE(nmo_engine_entity_world_matrix(f.engine, child_id, world));
    ASSERT_FLOAT_EQ(4, world[12], 0.0001f);
    ASSERT_EQ(1, nmo_engine_array_row_count(f.engine, table_id));
    nmo_engine_cell_t cell;
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, table_id, 0, 0, &cell));
    ASSERT_EQ(7u, cell.value);
    ASSERT_EQ(NMO_OK, nmo_engine_array_get_cell(f.engine, table_id, 0, 1, &cell));
    ASSERT_EQ(child_id, cell.value);
    nmo_engine_material_t out_material;
    ASSERT_TRUE(nmo_engine_material(f.engine, material_id, &out_material));
    ASSERT_FLOAT_EQ(1, out_material.diffuse[0], 0.0001f);
    ASSERT_FLOAT_EQ(0, out_material.diffuse[2], 0.0001f);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_FLOAT_EQ(2, frame.near_plane, 0.0001f);
    ASSERT_FLOAT_EQ(333, frame.far_plane, 0.0001f);
    ASSERT_FLOAT_EQ(2.5f, frame.zoom, 0);
    ASSERT_TRUE(frame.orthographic);
    ASSERT_EQ(1, frame.light_count);
    ASSERT_FLOAT_EQ(0, frame.lights[0].color[0], 0.0001f);
    ASSERT_FLOAT_EQ(1, frame.lights[0].color[1], 0.0001f);
    nmo_object_t *source = nmo_object_repository_find_by_id(nmo_document_get_repository(f.document), table_id);
    ASSERT_NOT_NULL(source);
    ASSERT_EQ(0u, ((const nmo_dataarray_state_t *)nmo_object_get_state(source))->row_count);
    source = nmo_object_repository_find_by_id(nmo_document_get_repository(f.document), material_id);
    ASSERT_NOT_NULL(source);
    ASSERT_EQ(0xff0000ffu, ((const nmo_material_state_t *)nmo_object_get_state(source))->diffuse_color);
    source = nmo_object_repository_find_by_id(nmo_document_get_repository(f.document), frame.camera);
    ASSERT_NOT_NULL(source);
    ASSERT_FLOAT_EQ(1, ((const nmo_camera_state_t *)nmo_object_get_state(source))->orthographic_zoom, 0);
    ASSERT_EQ(CK_PERSPECTIVEPROJECTION, ((const nmo_camera_state_t *)nmo_object_get_state(source))->projection_type);
    fixture_end(&f);
    remove(path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_scene, launch_and_restore_use_the_saved_initial_value);
REGISTER_TEST(engine_scene, partial_initial_value_keeps_unsaved_transform_members);
REGISTER_TEST(engine_scene, malformed_initial_value_fails_start_without_changing_the_object);
REGISTER_TEST(engine_scene, saved_file_initial_values_restore_references_arrays_and_render_members);
TEST_MAIN_END()
