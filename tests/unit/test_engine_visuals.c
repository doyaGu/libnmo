/**
 * @file test_engine_visuals.c
 * @brief Visuals.dll blocks that set render state: visibility cycles, Z
 *        buffer use, render order, transparency and the render options.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "core/nmo_arena.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_ref.h"

#define GUID_BLINK NMO_GUID(0xf00d010a, 0xfa0d010a)
#define GUID_FPS NMO_GUID(0xa58a313a, 0xe7f8d32a)
#define GUID_HIDE_HIERARCHY NMO_GUID(0x27447f6b, 0x22aa0c59)
#define GUID_SET_Z_BUFFER NMO_GUID(0x1d2d378d, 0x4a4f36b6)
#define GUID_SET_RENDER_ORDER NMO_GUID(0x6f8b6782, 0x21554283)
#define GUID_SET_3DSPRITE_MODE NMO_GUID(0x0c5d0457, 0xc64c7000)
#define GUID_MAKE_TRANSPARENT NMO_GUID(0x16cc600c, 0x702836d1)
#define GUID_SET_RENDER_OPTIONS NMO_GUID(0x4d586c55, 0x5250236a)
#define GUID_USE_Z_INFORMATION NMO_GUID(0x01f60adc, 0x39b02cb8)

/* A block acting on `object` (0: no target), run when the script starts */
static nmo_object_id_t block_on(engine_fixture_t *f, nmo_guid_t guid, const char *name, nmo_object_id_t object)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    if (id == 0) {
        return 0;
    }
    if (object != 0) {
        nmo_object_id_t target = fixture_target(f, id);
        if (target == 0) {
            return 0;
        }
        fixture_input_int(f, target, (int32_t)object);
    }
    fixture_link(f, f->script_in, BLOCK_IN(f, id, 0), 0);
    return id;
}

/* A 3D object at the origin with a one-face mesh of `material`, the mesh its only one */
static nmo_object_id_t face_object(engine_fixture_t *f, const char *name, nmo_object_id_t material,
                                   nmo_object_id_t *out_mesh)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_object_id_t object = fixture_create(f, NMO_CID_3DOBJECT, name);
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, object);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (entity == NULL || state == NULL) {
        return 0;
    }
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, 3u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, 3u * sizeof(uint16_t), _Alignof(uint16_t));
    state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(arena, sizeof(nmo_material_group_t),
                                                                      _Alignof(nmo_material_group_t));
    entity->mesh_ids = (nmo_ref_t *)nmo_arena_alloc(arena, sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL ||
        state->material_groups == NULL || entity->mesh_ids == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 3u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 3; i++) {
        state->vertices[i].position = (nmo_vector_t){(float)(i & 1), (float)(i >> 1), 0.0f};
        state->face_vertex_indices[i] = (uint16_t)i;
    }
    memset(state->faces, 0, sizeof(nmo_face_t));
    memset(state->material_groups, 0, sizeof(nmo_material_group_t));
    state->material_groups[0].material = nmo_ref_from_id(material);
    state->vertex_count = 3;
    state->face_count = 1;
    state->material_group_count = 1;
    state->has_material_groups = 1;
    entity->current_mesh = nmo_ref_from_id(mesh);
    entity->mesh_ids[0] = nmo_ref_from_id(mesh);
    entity->mesh_count = 1;
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(entity->world_matrix, world, sizeof(world));
    entity->has_entityndata_chunk = 1;
    *out_mesh = mesh;
    return object;
}

/* The frame's index of the mesh draw of `object`; -1 for none */
static int mesh_draw(const nmo_engine_frame_t *frame, nmo_object_id_t object)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_MESH && frame->draws[i].object == object) {
            return i;
        }
    }
    return -1;
}

TEST(engine_visuals, blink_cycles_the_target_and_fps_reports_the_rate)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t blink = block_on(&f, GUID_BLINK, "Blink", f.owner_id);
    nmo_object_id_t fps = block_on(&f, GUID_FPS, "FPS", 0);
    ASSERT_TRUE(blink != 0 && fps != 0);
    fixture_link(&f, BLOCK_OUT(&f, blink, 0), BLOCK_IN(&f, blink, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, blink, 0), 1); /* hidden one frame */
    fixture_input_int(&f, BLOCK_PIN(&f, blink, 1), 2); /* then shown two */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const bool expected[6] = {false, true, true, false, true, true};
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
        ASSERT_EQ(expected[i], nmo_engine_object_is_visible(f.engine, f.owner_id));
    }
    float rate = 0.0f;
    read_value(&f, BLOCK_POUT(&f, fps, 0), &rate, sizeof(rate));
    ASSERT_FLOAT_EQ(100.0f, rate, 1e-4f);
    fixture_end(&f);
}

TEST(engine_visuals, z_buffer_and_render_order_shape_the_mesh_draws)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh_a = 0, mesh_b = 0, mesh_c = 0;
    nmo_object_id_t a = face_object(&f, "A", material, &mesh_a);
    nmo_object_id_t b = face_object(&f, "B", material, &mesh_b);
    nmo_object_id_t c = face_object(&f, "C", material, &mesh_c);
    ASSERT_TRUE(material != 0 && a != 0 && b != 0 && c != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t z = block_on(&f, GUID_SET_Z_BUFFER, "Set Z Buffer", a);
    nmo_object_id_t last = block_on(&f, GUID_SET_RENDER_ORDER, "Set Render Order", a);
    nmo_object_id_t first = block_on(&f, GUID_SET_RENDER_ORDER, "Set Render Order", c);
    nmo_object_id_t priority = block_on(&f, GUID_SET_RENDER_ORDER, "Set Render Order", b);
    ASSERT_TRUE(z != 0 && last != 0 && first != 0 && priority != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, z, 0), 0); /* no Z test */
    fixture_input_int(&f, BLOCK_PIN(&f, z, 1), 0); /* no Z write */
    fixture_input_int(&f, BLOCK_PIN(&f, last, 1), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, first, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, priority, 2), 5);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const nmo_object_id_t shown[6] = {a, b, c, mesh_a, mesh_b, mesh_c};
    for (int i = 0; i < 6; i++) {
        eng_show(f.engine, shown[i], 1);
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int at_a = mesh_draw(&frame, a), at_b = mesh_draw(&frame, b), at_c = mesh_draw(&frame, c);
    ASSERT_TRUE(at_a >= 0 && at_b >= 0 && at_c >= 0);
    /* rendered first (priority 10000), then priority 5, then the rest */
    ASSERT_TRUE(at_c < at_b && at_b < at_a);
    ASSERT_TRUE(frame.draws[at_a].no_z_test && frame.draws[at_a].no_z_write);
    ASSERT_TRUE(frame.draws[at_a].transparent); /* rendered last */
    ASSERT_FALSE(frame.draws[at_b].transparent || frame.draws[at_b].no_z_test);
    fixture_end(&f);
}

TEST(engine_visuals, make_transparent_blends_the_materials_and_marks_the_meshes)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Object", material, &mesh);
    ASSERT_TRUE(material != 0 && object != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t fade = block_on(&f, GUID_MAKE_TRANSPARENT, "Make Transparent", object);
    ASSERT_TRUE(fade != 0);
    const float alpha = 0.25f;
    fixture_input_value(&f, BLOCK_PIN(&f, fade, 2), &alpha, sizeof(alpha));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, fade));

    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_TRUE(desc.alpha_blend);
    ASSERT_FALSE(desc.z_write);
    ASSERT_EQ(VXBLEND_SRCALPHA, desc.source_blend);
    ASSERT_EQ(VXBLEND_INVSRCALPHA, desc.dest_blend);
    ASSERT_FLOAT_EQ(0.25f, desc.diffuse[3], 0.0f);
    eng_mesh_copy_t *copy = eng_mesh_copy_find(f.engine, mesh);
    ASSERT_NOT_NULL(copy);
    ASSERT_TRUE((copy->flags & VXMESH_FORCETRANSPARENCY) != 0);
    fixture_end(&f);
}

TEST(engine_visuals, render_options_and_use_z_information_set_the_frame)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t panel = fixture_create(&f, NMO_CID_2DENTITY, "Panel");
    nmo_2dentity_state_t *state = (nmo_2dentity_state_t *)fixture_state(&f, panel);
    ASSERT_TRUE(material != 0 && state != NULL);
    state->rect = (nmo_rect_t){0.0f, 0.0f, 10.0f, 10.0f};
    state->material = nmo_ref_from_id(material);
    state->has_material = 1;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t options = block_on(&f, GUID_SET_RENDER_OPTIONS, "Set Render Options", 0);
    nmo_object_id_t zinfo = fixture_block(&f, GUID_USE_Z_INFORMATION, "Use Z information");
    ASSERT_TRUE(options != 0 && zinfo != 0);
    /* background sprites and clear back only: no foreground 2D entities */
    fixture_input_int(&f, BLOCK_PIN(&f, options, 0), CK_RENDER_BACKGROUNDSPRITES | CK_RENDER_CLEARBACK);
    fixture_input_int(&f, BLOCK_PIN(&f, options, 1), 0); /* wireframe */
    fixture_input_int(&f, BLOCK_PIN(&f, options, 2), 0); /* no textures */
    fixture_input_int(&f, BLOCK_PIN(&f, zinfo, 0), 0); /* Clear Background: no */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_show(f.engine, panel, 1);

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* the defaults, but Use Z information's load callback stopped clearing the background */
    ASSERT_EQ((uint32_t)CK_RENDER_DEFAULTSETTINGS & ~(uint32_t)CK_RENDER_CLEARBACK, frame.render_options);
    ASSERT_EQ(2, frame.shading);
    ASSERT_TRUE(frame.use_textures);
    bool drawn = false;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        drawn = drawn || frame.draws[i].object == panel;
    }
    ASSERT_TRUE(drawn);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, options));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ((uint32_t)(CK_RENDER_BACKGROUNDSPRITES | CK_RENDER_CLEARBACK), frame.render_options);
    ASSERT_EQ(0, frame.shading);
    ASSERT_FALSE(frame.use_textures);
    for (int32_t i = 0; i < frame.draw_count; i++) {
        ASSERT_TRUE(frame.draws[i].object != panel);
    }
    fixture_end(&f);
}

TEST(engine_visuals, hide_hierarchy_and_3dsprite_mode)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sprite = fixture_create(&f, NMO_CID_SPRITE3D, "Sprite");
    ASSERT_TRUE(sprite != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t hide = block_on(&f, GUID_HIDE_HIERARCHY, "Hide Hierarchy", sprite);
    nmo_object_id_t mode = block_on(&f, GUID_SET_3DSPRITE_MODE, "Set 3DSprite Mode", sprite);
    ASSERT_TRUE(hide != 0 && mode != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, mode, 0), VXSPRITE3D_YROTATE);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, mode));
    ASSERT_EQ((uint32_t)VXSPRITE3D_YROTATE, eng_sprite3d(f.engine, sprite)->mode);
    ASSERT_EQ(0x200, eng_object_visibility(f.engine, sprite) & 0x200); /* hidden with its children */
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_visuals, blink_cycles_the_target_and_fps_reports_the_rate);
    REGISTER_TEST(engine_visuals, z_buffer_and_render_order_shape_the_mesh_draws);
    REGISTER_TEST(engine_visuals, make_transparent_blends_the_materials_and_marks_the_meshes);
    REGISTER_TEST(engine_visuals, render_options_and_use_z_information_set_the_frame);
    REGISTER_TEST(engine_visuals, hide_hierarchy_and_3dsprite_mode);
TEST_MAIN_END()
