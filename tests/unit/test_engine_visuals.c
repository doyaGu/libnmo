/**
 * @file test_engine_visuals.c
 * @brief Visuals.dll blocks that set render state: visibility cycles, Z
 *        buffer use, render order, transparency and the render options.
 */

#include "engine_fixture.h"
#include <math.h>
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_transform.h"
#include "../../src/engine/engine_media.h"
#include "../../src/engine/engine_manager.h"

#include "core/nmo_arena.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_edit.h"
#include "type/nmo_param_guids.h"
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
#define GUID_RENDER_CURVE NMO_GUID(0x215c7794, 0x7ac40f5d)
#define GUID_SHOW_OBJECT_INFORMATION NMO_GUID(0x17cb4c57, 0x00f525fb)
#define GUID_SIMPLE_SHADOW NMO_GUID(0x79b95ed0, 0x3e4a01f5)
#define GUID_SOLID_TRAIL NMO_GUID(0xd00d010a, 0xe00d010a)
#define GUID_MARK_SYSTEM NMO_GUID(0x09a06075, 0x760a4720)
#define GUID_VERTEX_RENDER NMO_GUID(0xcc030377, 0x897aaaaa)
#define GUID_MOTION_BLUR NMO_GUID(0x06d0b04b, 0x1d4ea106)
#define GUID_PLANAR_REFLECTION NMO_GUID(0x44fe1b3c, 0x29ba3445)
#define GUID_PLANAR_SHADOW NMO_GUID(0x731072fa, 0x56cf087f)
#define GUID_SHADOW_CASTER NMO_GUID(0x0718360b, 0x71e5002f)
#define GUID_TEXTURE_RENDER NMO_GUID(0x67fc7084, 0x141154f7)

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

TEST(engine_visuals, the_scene_graph_orders_children_priorities_and_transparent_objects)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    ASSERT_TRUE(material != 0);
    const char *names[6] = {"A", "B", "C", "Near", "Middle", "Far"};
    const float depth[6] = {5, 5, 5, 10, 20, 30};
    nmo_object_id_t objects[6], meshes[6];
    for (int i = 0; i < 6; i++) {
        objects[i] = face_object(&f, names[i], material, &meshes[i]);
        ASSERT_TRUE(objects[i] != 0);
        ((nmo_3dentity_state_t *)fixture_state(&f, objects[i]))->world_matrix[14] = depth[i];
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t index[6];
    for (int i = 0; i < 6; i++) {
        eng_show(f.engine, objects[i], 1);
        eng_show(f.engine, meshes[i], 1);
        index[i] = eng_entity_index(f.engine, objects[i]);
    }
    for (int i = 3; i < 6; i++) {
        f.engine->entities.data[index[i]].moveable_flags |= VX_MOVEABLE_RENDERLAST;
    }
    /* C becomes A's child: the parent, its child, then the next root */
    ASSERT_TRUE(eng_entity_set_parent(f.engine, index[2], index[0], true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int at[6];
    for (int i = 0; i < 6; i++) {
        at[i] = mesh_draw(&frame, objects[i]);
        ASSERT_TRUE(at[i] >= 0);
    }
    ASSERT_TRUE(at[0] < at[2] && at[2] < at[1]);
    /* the transparent ones after every opaque one, back to front: the eye looks down Z */
    ASSERT_TRUE(at[1] < at[5] && at[5] < at[4] && at[4] < at[3]);
    ASSERT_TRUE(frame.draws[at[3]].transparent && !frame.draws[at[1]].transparent);

    /* B's higher Z order puts it before A; the nearest transparent one's puts it first of those */
    f.engine->entities.data[index[1]].render_priority = 5;
    f.engine->entities.data[index[3]].render_priority = 5;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    for (int i = 0; i < 6; i++) {
        at[i] = mesh_draw(&frame, objects[i]);
    }
    ASSERT_TRUE(at[1] < at[0] && at[0] < at[2]);
    ASSERT_TRUE(at[2] < at[3] && at[3] < at[5] && at[5] < at[4]);

    /* Equal Z orders: a root whose child has a higher one goes first, here B before A */
    f.engine->entities.data[index[1]].render_priority = 0;
    f.engine->entities.data[index[2]].render_priority = 7;
    ASSERT_TRUE(eng_entity_set_parent(f.engine, index[2], index[1], true));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    for (int i = 0; i < 3; i++) {
        at[i] = mesh_draw(&frame, objects[i]);
    }
    ASSERT_TRUE(at[1] < at[2] && at[2] < at[0]);
    fixture_end(&f);
}

TEST(engine_visuals, the_camera_culls_what_is_out_of_view_as_the_scene_graph_is_traversed)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    ASSERT_TRUE(material != 0 && lens != NULL);
    lens->entity.has_entityndata_chunk = 1;
    lens->projection_type = 1; /* perspective, at the origin looking down Z */
    lens->fov = 1.0f;
    lens->aspect_width = 4;
    lens->aspect_height = 3;
    lens->near_plane = 1.0f;
    lens->far_plane = 100.0f;
    /* Ahead in view, Behind the eye, a Parent behind with a Child ahead, a Group behind with its Member */
    const char *names[6] = {"Ahead", "Behind", "Parent", "Child", "Group", "Member"};
    const float depth[6] = {10, -10, -10, 10, -10, -12};
    nmo_object_id_t objects[6], meshes[6];
    for (int i = 0; i < 6; i++) {
        objects[i] = face_object(&f, names[i], material, &meshes[i]);
        ASSERT_TRUE(objects[i] != 0);
        ((nmo_3dentity_state_t *)fixture_state(&f, objects[i]))->world_matrix[14] = depth[i];
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t index[6];
    for (int i = 0; i < 6; i++) {
        eng_show(f.engine, objects[i], 1);
        eng_show(f.engine, meshes[i], 1);
        index[i] = eng_entity_index(f.engine, objects[i]);
    }
    ASSERT_TRUE(eng_entity_set_parent(f.engine, index[3], index[2], true));
    ASSERT_TRUE(eng_entity_set_parent(f.engine, index[5], index[4], true));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    /* Without a camera the engine's stand-in eye culls nothing */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    for (int i = 0; i < 6; i++) {
        ASSERT_TRUE(mesh_draw(&frame, objects[i]) >= 0);
    }

    f.engine->render.camera = camera;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(mesh_draw(&frame, objects[0]) >= 0);
    ASSERT_TRUE(mesh_draw(&frame, objects[1]) < 0);
    ASSERT_TRUE(eng_entity_all_outside_frustum(f.engine, index[1]));
    /* the parent's hierarchy reaches into the view: the parent alone is skipped */
    ASSERT_TRUE(mesh_draw(&frame, objects[2]) < 0);
    ASSERT_TRUE(mesh_draw(&frame, objects[3]) >= 0);
    ASSERT_TRUE(eng_entity_all_inside_frustum(f.engine, index[3]));
    /* the whole group is behind: both skipped, both outside */
    ASSERT_TRUE(mesh_draw(&frame, objects[4]) < 0 && mesh_draw(&frame, objects[5]) < 0);
    ASSERT_TRUE(eng_entity_all_outside_frustum(f.engine, index[4]) && eng_entity_all_outside_frustum(f.engine, index[5]));

    /* Render extents (GetRenderExtents): the face ahead, a little right of and above the middle
       of the 640 by 480 view; the one behind tested and left empty; the member never tested */
    const nmo_rect_t *ahead = &f.engine->entities.data[index[0]].render_extents;
    ASSERT_TRUE(ahead->left >= 320.0f && ahead->right > ahead->left && ahead->right < 640.0f);
    ASSERT_TRUE(ahead->bottom <= 240.0f && ahead->top < ahead->bottom && ahead->top > 0.0f);
    const nmo_rect_t *behind = &f.engine->entities.data[index[1]].render_extents;
    ASSERT_FLOAT_EQ(1e8f, behind->left, 0.0f);
    ASSERT_FLOAT_EQ(-1e8f, behind->right, 0.0f);
    const nmo_rect_t *member = &f.engine->entities.data[index[5]].render_extents;
    ASSERT_TRUE(member->left == 0.0f && member->right == 0.0f);
    fixture_end(&f);
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

/* A curve of two linear points, from the origin to (0, 0, 20) */
static nmo_object_id_t straight_curve(engine_fixture_t *f)
{
    nmo_object_id_t curve = fixture_create(f, NMO_CID_CURVE, "Curve");
    nmo_curve_state_t *state = (nmo_curve_state_t *)fixture_state(f, curve);
    if (state == NULL) {
        return 0;
    }
    state->opened = 1;
    state->step_count = 100;
    state->control_point_count = 2;
    state->control_point_ids = (nmo_ref_t *)nmo_arena_alloc(nmo_session_get_arena(f->session),
                                                            2 * sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
    if (state->control_point_ids == NULL) {
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        nmo_object_id_t id = fixture_create(f, NMO_CID_CURVEPOINT, i ? "End" : "Start");
        nmo_curvepoint_state_t *point = (nmo_curvepoint_state_t *)fixture_state(f, id);
        if (point == NULL) {
            return 0;
        }
        point->base.parent = nmo_ref_from_id(curve);
        point->base.has_entityndata_chunk = 1;
        point->curve = nmo_ref_from_id(curve);
        point->linear = 1;
        nmo_curvepoint_set_position(point, 0, 0, i ? 20.0f : 0.0f);
        state = (nmo_curve_state_t *)fixture_state(f, curve);
        state->control_point_ids[i] = nmo_ref_from_id(id);
    }
    return curve;
}

static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

/* The frame's primitive draws of `object`, `out` the first; their count */
static int primitive_draws(const nmo_engine_frame_t *frame, nmo_object_id_t object, const nmo_engine_draw_t **out)
{
    int count = 0;
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_PRIMITIVE && frame->draws[i].object == object) {
            if (count++ == 0) {
                *out = &frame->draws[i];
            }
        }
    }
    return count;
}

static void vertex_at(const nmo_engine_frame_t *frame, const nmo_engine_draw_t *draw, int k, float x, float y,
                      float z, float u, float v)
{
    const nmo_engine_vertex_t *p = &frame->vertices[draw->first_vertex + k];
    ASSERT_FLOAT_EQ(x, p->x, 1e-5f);
    ASSERT_FLOAT_EQ(y, p->y, 1e-5f);
    ASSERT_FLOAT_EQ(z, p->z, 1e-5f);
    ASSERT_FLOAT_EQ(u, p->u, 1e-5f);
    ASSERT_FLOAT_EQ(v, p->v, 1e-5f);
}

TEST(engine_visuals, render_curve_draws_the_curve_each_frame_until_off)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t curve = straight_curve(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Green");
    ASSERT_TRUE(curve != 0 && material != 0);
    ((nmo_material_state_t *)fixture_state(&f, material))->diffuse_color = 0xFF00FF00u;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t render = block_on(&f, GUID_RENDER_CURVE, "Render Curve", curve);
    ASSERT_TRUE(render != 0);
    fixture_link(&f, BLOCK_OUT(&f, render, 0), BLOCK_IN(&f, render, 1), 2); /* Off two frames on */
    fixture_input_int(&f, BLOCK_PIN(&f, render, 0), (int32_t)material);
    fixture_input_int(&f, BLOCK_PIN(&f, render, 5), 1); /* Tube Lighting */
    nmo_object_id_t mode = local_of(&f, render, 0);
    ASSERT_TRUE(mode != 0);
    fixture_commit(&f, NULL);
    const int32_t plane = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, mode, &plane, sizeof(plane)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    /* Plane: a strip across Up of Size 1 each side, two triangles */
    nmo_engine_frame_t frame;
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, primitive_draws(&frame, curve, &draw));
    ASSERT_EQ(material, draw->material);
    ASSERT_EQ(6, draw->vertex_count);
    ASSERT_FALSE(draw->lines || draw->lit || draw->two_sided || draw->post || draw->transparent);
    ASSERT_EQ(0xFF00FF00u, frame.vertices[draw->first_vertex].color);
    vertex_at(&frame, draw, 0, -1, 0, 0, 0, 0);
    vertex_at(&frame, draw, 1, 1, 0, 0, 0, 1);
    vertex_at(&frame, draw, 2, -1, 0, 20, 1, 0);
    vertex_at(&frame, draw, 3, -1, 0, 20, 1, 0); /* the second triangle turned back: 2, 1, 3 */
    vertex_at(&frame, draw, 4, 1, 0, 0, 0, 1);
    vertex_at(&frame, draw, 5, 1, 0, 20, 1, 1);
    /* temporary: the next frame draws it only if the block ran again */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, primitive_draws(&frame, curve, &draw));

    /* Tube, lit: three rings, a quad between each two, both sides */
    const int32_t tube = 2;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, mode, &tube, sizeof(tube)));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, primitive_draws(&frame, curve, &draw));
    ASSERT_EQ(18, draw->vertex_count);
    ASSERT_TRUE(draw->lit && draw->two_sided && !draw->lines);
    const nmo_engine_vertex_t *first = &frame.vertices[draw->first_vertex];
    ASSERT_FLOAT_EQ(1.0f, first->x, 1e-5f); /* the first ring's point at angle 0: Size.x right */
    ASSERT_FLOAT_EQ(1.0f, first->nx, 1e-5f);

    /* Off: no more callback */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, primitive_draws(&frame, curve, &draw));
    fixture_end(&f);
}

TEST(engine_visuals, render_curve_line_mode_draws_the_line_mesh)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t curve = straight_curve(&f);
    ASSERT_TRUE(curve != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t render = block_on(&f, GUID_RENDER_CURVE, "Render Curve", curve);
    ASSERT_TRUE(render != 0);
    nmo_object_id_t mode = local_of(&f, render, 0);
    fixture_commit(&f, NULL);
    const int32_t line = 4;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, mode, &line, sizeof(line)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, primitive_draws(&frame, curve, &draw));
    ASSERT_TRUE(draw->lines && draw->material == 0);
    ASSERT_EQ(2, draw->vertex_count);
    ASSERT_EQ(0xFFFFFFFFu, frame.vertices[draw->first_vertex].color); /* white without a material */
    vertex_at(&frame, draw, 1, 0, 0, 20, 0, 0);
    fixture_end(&f);
}

TEST(engine_visuals, show_object_information_draws_box_and_face_normals_after_the_scene)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && object != 0);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(&f, mesh);
    state->faces[0].normal = (nmo_vector_t){0.0f, 0.0f, 1.0f};
    nmo_mesh_update_bounding_volumes(state);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t info = block_on(&f, GUID_SHOW_OBJECT_INFORMATION, "Show Object Information", object);
    ASSERT_TRUE(info != 0);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    fixture_input_int(&f, BLOCK_PIN(&f, info, 0), 1);
    fixture_input_value(&f, BLOCK_PIN(&f, info, 1), white, sizeof(white));
    fixture_input_int(&f, BLOCK_PIN(&f, info, 7), 1);
    const float half = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, info, 9), &half, sizeof(half));
    fixture_input_int(&f, BLOCK_PIN(&f, info, 10), 1);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int at_mesh = mesh_draw(&frame, object);
    const nmo_engine_draw_t *box = NULL;
    ASSERT_EQ(3, primitive_draws(&frame, object, &box));
    ASSERT_TRUE(at_mesh >= 0 && box > &frame.draws[at_mesh]);
    ASSERT_TRUE(box->lines && box->post && box->material == 0);
    ASSERT_EQ(24, box->vertex_count); /* twelve edges of the world box (0,0,0)-(1,1,0) */
    ASSERT_EQ(0xFFFFFFFFu, frame.vertices[box->first_vertex].color);
    vertex_at(&frame, box, 1, 1, 0, 0, 0, 0);
    vertex_at(&frame, box, 4, 1, 1, 0, 0, 0);
    const nmo_engine_draw_t *normals = box + 1, *sphere = box + 2;
    ASSERT_EQ(2, normals->vertex_count); /* from the face's center along its normal, half long */
    vertex_at(&frame, normals, 0, 0.33333f, 0.33333f, 0, 0, 0);
    vertex_at(&frame, normals, 1, 0.33333f, 0.33333f, 0.5f, 0, 0);
    ASSERT_EQ(3 * 31 * 2, sphere->vertex_count); /* three circles of 32 points */
    ASSERT_EQ(0xFFFF0000u, frame.vertices[sphere->first_vertex].color); /* the default red */

    /* drawn the frame after the block ran, not later */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, primitive_draws(&frame, object, &box));
    fixture_end(&f);
}

/* A 10x10 floor in the XZ plane at the origin, with the Floor attribute. After fixture_begin_edit(). */
static nmo_object_id_t floor_object(engine_fixture_t *f, nmo_object_id_t *out_mesh)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DOBJECT, "Floor");
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, "Floor Mesh");
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, id);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (entity == NULL || state == NULL) {
        return 0;
    }
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, 4u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, 2u * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, 6u * sizeof(uint16_t), _Alignof(uint16_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 4u * sizeof(nmo_vertex_t));
    memset(state->faces, 0, 2u * sizeof(nmo_face_t));
    const float xz[4][2] = {{-5, -5}, {-5, 5}, {5, 5}, {5, -5}};
    for (int i = 0; i < 4; i++) {
        state->vertices[i].position = (nmo_vector_t){xz[i][0], 0.0f, xz[i][1]};
    }
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(state->face_vertex_indices, indices, sizeof(indices));
    state->vertex_count = 4;
    state->face_count = 2;
    nmo_mesh_update_bounding_volumes(state);
    entity->current_mesh = nmo_ref_from_id(mesh);
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    memcpy(entity->world_matrix, world, sizeof(world));
    entity->has_entityndata_chunk = 1;

    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = CKPGUID_FLOAT,
        .category = "Test", .has_compatible_class_id = true, .compatible_class_id = NMO_CID_3DENTITY,
        .has_flags = true, .flags = 0};
    uint32_t type = 0;
    if (nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx), "Floor",
                                                       &options, &type) != NMO_OK) {
        return 0;
    }
    nmo_object_id_t parameter = fixture_create(f, NMO_CID_PARAMETEROUT, "Floor Attribute");
    nmo_parameter_state_t *value = (nmo_parameter_state_t *)fixture_state(f, parameter);
    value->has_state = true;
    value->mode = CKPARAM_MODE_BUFFER;
    value->type_guid = CKPGUID_FLOAT;
    if (nmo_array_alloc(&value->buffer_data, 1, 4, NULL) != NMO_OK) {
        return 0;
    }
    memset(value->buffer_data.data, 0, 4);
    if (nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)entity)->attributes, parameter, type,
                                            NULL) != NMO_OK) {
        return 0;
    }
    *out_mesh = mesh;
    return id;
}

TEST(engine_visuals, simple_shadow_multiplies_the_floors_under_its_owner_until_off)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Shadow");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && texture != 0 && object != 0);
    nmo_mesh_update_bounding_volumes((nmo_mesh_state_t *)fixture_state(&f, mesh));
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[13] = 2.0f;
    f.owner_id = object; /* the shadow is its owner's */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t floor_mesh = 0;
    nmo_object_id_t floor = floor_object(&f, &floor_mesh);
    nmo_object_id_t shadow = block_on(&f, GUID_SIMPLE_SHADOW, "Simple Shadow", 0);
    ASSERT_TRUE(floor != 0 && shadow != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, shadow, 0), (int32_t)texture);
    fixture_link(&f, BLOCK_OUT(&f, shadow, 0), BLOCK_IN(&f, shadow, 1), 2);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);
    const nmo_engine_channel_t *channel = &desc.channels[0];
    nmo_engine_material_t shade;
    ASSERT_TRUE(nmo_engine_material(f.engine, channel->material, &shade));
    ASSERT_EQ(texture, shade.texture);
    ASSERT_EQ(VXBLEND_ZERO, channel->source_blend);
    ASSERT_EQ(VXBLEND_SRCCOLOR, channel->dest_blend);
    ASSERT_NOT_NULL(channel->uvs);
    /* the owner is 1 wide, the scale 2: half a texture a unit, centered under the owner */
    ASSERT_FLOAT_EQ(3.0f, channel->uvs[0], 1e-5f);
    ASSERT_FLOAT_EQ(3.0f, channel->uvs[1], 1e-5f);
    ASSERT_FLOAT_EQ(-2.0f, channel->uvs[4], 1e-5f);

    /* moved 1 along X: the UVs follow */
    eng_vector_t step = {1.0f, 0.0f, 0.0f};
    eng_entity_translate(f.engine, eng_entity_index(f.engine, object), &step, -1, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);
    ASSERT_FLOAT_EQ(3.5f, desc.channels[0].uvs[0], 1e-5f);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f)); /* Off, two frames after On */
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(0, desc.channel_count);
    fixture_end(&f);
}

TEST(engine_visuals, simple_shadow_skips_floors_outside_the_last_render)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Shadow");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    ASSERT_TRUE(material != 0 && texture != 0 && object != 0 && lens != NULL);
    nmo_mesh_update_bounding_volumes((nmo_mesh_state_t *)fixture_state(&f, mesh));
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[13] = 2.0f;
    lens->entity.has_entityndata_chunk = 1;
    lens->projection_type = 1; /* perspective */
    lens->fov = 1.0f;
    lens->aspect_width = 4;
    lens->aspect_height = 3;
    lens->near_plane = 1.0f;
    lens->far_plane = 100.0f;
    f.owner_id = object;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t floor_mesh = 0;
    nmo_object_id_t floor = floor_object(&f, &floor_mesh);
    nmo_object_id_t shadow = block_on(&f, GUID_SIMPLE_SHADOW, "Simple Shadow", 0);
    ASSERT_TRUE(floor != 0 && shadow != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, shadow, 0), (int32_t)texture);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    int32_t floor_index = eng_entity_index(f.engine, floor);

    /* Nothing rendered yet: no floor is outside */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);

    /* Rendered by a camera 20 along Z looking away (+Z): the floor was all outside, the shadow leaves it */
    const float away[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 20, 1};
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, camera, away, false));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(eng_entity_all_outside_frustum(f.engine, floor_index));
    ASSERT_FALSE(eng_entity_all_inside_frustum(f.engine, floor_index));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(0, desc.channel_count);

    /* Turned back (-Z): all of the floor is in view, the shadow returns */
    const float toward[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 1, 20, 1};
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, camera, toward, false));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_FALSE(eng_entity_all_outside_frustum(f.engine, floor_index));
    ASSERT_TRUE(eng_entity_all_inside_frustum(f.engine, floor_index));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);
    fixture_end(&f);
}

static void trail_pair(const nmo_engine_mesh_t *mesh, int pair, float ax, float ay, float az, float bx, float by,
                       float bz)
{
    const nmo_vector_t *a = &mesh->vertices[pair * 2].position, *b = &mesh->vertices[pair * 2 + 1].position;
    ASSERT_FLOAT_EQ(ax, a->x, 1e-5f);
    ASSERT_FLOAT_EQ(ay, a->y, 1e-5f);
    ASSERT_FLOAT_EQ(az, a->z, 1e-5f);
    ASSERT_FLOAT_EQ(bx, b->x, 1e-5f);
    ASSERT_FLOAT_EQ(by, b->y, 1e-5f);
    ASSERT_FLOAT_EQ(bz, b->z, 1e-5f);
}

TEST(engine_visuals, solid_trail_follows_the_owners_farthest_vertices_until_off)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && object != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[12] = 10.0f;
    f.owner_id = object;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t trail = block_on(&f, GUID_SOLID_TRAIL, "Solid Trail", 0);
    ASSERT_TRUE(trail != 0);
    const float width = 0.25f, length = 1.0f, white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, trail, 0), &width, sizeof(width));
    fixture_input_value(&f, BLOCK_PIN(&f, trail, 1), &length, sizeof(length));
    fixture_input_value(&f, BLOCK_PIN(&f, trail, 2), white, sizeof(white));
    fixture_link(&f, BLOCK_OUT(&f, trail, 0), BLOCK_IN(&f, trail, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_object_id_t entity = eng_object_named(f.engine, "STEntity", 0);
    ASSERT_TRUE(entity != 0);

    /* On: the front pair at the two vertices farthest apart, (1,0,0) and (0,1,0), the others a
       quarter closer to each other */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int at = mesh_draw(&frame, entity);
    ASSERT_TRUE(at >= 0);
    ASSERT_TRUE(frame.draws[at].transparent);
    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, frame.draws[at].mesh, &desc));
    ASSERT_EQ(82, desc.vertex_count);
    ASSERT_EQ(80, desc.face_count);
    ASSERT_TRUE(desc.prelit);
    trail_pair(&desc, 0, 11, 0, 0, 10, 1, 0);
    trail_pair(&desc, 1, 10.75f, 0.25f, 0, 10.25f, 0.75f, 0);
    trail_pair(&desc, 40, 10.75f, 0.25f, 0, 10.25f, 0.75f, 0);
    ASSERT_EQ(0xFFFFFFFFu, desc.colors[0]);
    ASSERT_EQ(0xFFF2F2F2u, desc.colors[2]);  /* 38/40 of the color */
    ASSERT_EQ(0xFF060606u, desc.colors[76]); /* 1/40 */
    ASSERT_EQ(0xFF000000u, desc.colors[80]);
    nmo_engine_material_t shade;
    ASSERT_TRUE(nmo_engine_material(f.engine, nmo_engine_mesh_material(f.engine, frame.draws[at].mesh, 0), &shade));
    ASSERT_TRUE(shade.alpha_blend && shade.two_sided && !shade.z_write);
    ASSERT_EQ(VXBLEND_ONE, shade.source_blend);
    ASSERT_EQ(VXBLEND_ONE, shade.dest_blend);

    /* Moved 1 along Z: the front pair follows, the rest step back behind it */
    eng_vector_t step = {0.0f, 0.0f, 1.0f};
    eng_entity_translate(f.engine, eng_entity_index(f.engine, object), &step, -1, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, frame.draws[at].mesh, &desc));
    trail_pair(&desc, 0, 11, 0, 1, 10, 1, 1);
    trail_pair(&desc, 1, 10.75f, 0.25f, 0, 10.25f, 0.75f, 0);
    trail_pair(&desc, 2, 10.625f, 0.375f, 0, 10.375f, 0.625f, 0);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f)); /* Off */
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, entity));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(-1, mesh_draw(&frame, entity));
    fixture_end(&f);
}

/* The block's local parameter `pos` (its settings follow its locals) */
static nmo_object_id_t block_local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

TEST(engine_visuals, mark_system_draws_single_marks_that_fade_and_die)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Decal");
    ASSERT_TRUE(material != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t marks = block_on(&f, GUID_MARK_SYSTEM, "Mark System", 0);
    ASSERT_TRUE(marks != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, marks, 2), 0); /* a mark as it starts */
    const float position[3] = {1.0f, 0.0f, 2.0f}, size = 2.0f, life = 100.0f, fade = 50.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, marks, 0), position, sizeof(position));
    fixture_input_value(&f, BLOCK_PIN(&f, marks, 4), &size, sizeof(size));
    fixture_input_int(&f, BLOCK_PIN(&f, marks, 5), (int32_t)material);
    fixture_input_value(&f, BLOCK_PIN(&f, marks, 6), &life, sizeof(life));
    fixture_input_value(&f, BLOCK_PIN(&f, marks, 7), &fade, sizeof(fade));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));

    /* A square on the XZ plane around the position, its UVs the whole texture */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(1, primitive_draws(&frame, 0, &draw));
    ASSERT_EQ(6, draw->vertex_count);
    ASSERT_EQ(material, draw->material);
    ASSERT_TRUE(draw->two_sided && !draw->lit && !draw->post && !draw->lines);
    ASSERT_FLOAT_EQ(1.001f, draw->projection_z_scale, 1e-7f);
    vertex_at(&frame, draw, 0, 2, 0, 3, 0, 0);
    vertex_at(&frame, draw, 1, 2, 0, 1, 1, 0);
    vertex_at(&frame, draw, 2, 0, 0, 3, 0, 1);
    vertex_at(&frame, draw, 3, 2, 0, 1, 1, 0);
    vertex_at(&frame, draw, 4, 0, 0, 1, 1, 1);
    ASSERT_EQ(0xFFFFFFFFu, frame.vertices[draw->first_vertex].color);

    /* 10 ms a frame: under 50 ms left it fades, a fifth of its alpha a frame, and then dies */
    const uint32_t alpha[] = {255, 255, 255, 255, 204, 153, 102, 51};
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
        ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
        ASSERT_EQ(1, primitive_draws(&frame, 0, &draw));
        ASSERT_EQ(alpha[i], frame.vertices[draw->first_vertex].color >> 24);
    }
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
        ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    }
    ASSERT_EQ(0, primitive_draws(&frame, 0, &draw));
    fixture_end(&f);
}

TEST(engine_visuals, mark_system_joins_continuous_marks_into_a_band)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t marks = block_on(&f, GUID_MARK_SYSTEM, "Mark System", 0);
    ASSERT_TRUE(marks != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, marks, 2), 0);
    fixture_link(&f, BLOCK_OUT(&f, marks, 2), BLOCK_IN(&f, marks, 2), 1); /* a mark every frame */
    ASSERT_EQ(NMO_OK, nmo_script_edit_set_parameter_value(f.tx, block_local(&f, marks, 2), "1"));
    const float size = 2.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, marks, 4), &size, sizeof(size));
    fixture_input_int(&f, BLOCK_PIN(&f, marks, 2), (int32_t)f.owner_id); /* at the owner */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(0, primitive_draws(&frame, 0, &draw)); /* a band's start alone draws nothing */

    /* Moved 1 along Z: the band runs from the start to it, 2 wide across, its V the distance
       times the default texture scale */
    eng_vector_t step = {0.0f, 0.0f, 1.0f};
    eng_entity_translate(f.engine, eng_entity_index(f.engine, f.owner_id), &step, -1, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, primitive_draws(&frame, 0, &draw));
    ASSERT_EQ(6, draw->vertex_count);
    vertex_at(&frame, draw, 0, -1, 0, 1, 0, 0.2f);
    vertex_at(&frame, draw, 1, -1, 0, 0, 0, 0);
    vertex_at(&frame, draw, 2, 1, 0, 1, 1, 0.2f);
    vertex_at(&frame, draw, 3, -1, 0, 0, 0, 0);
    vertex_at(&frame, draw, 4, 1, 0, 0, 1, 0);

    /* Not moved: no new mark */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, primitive_draws(&frame, 0, &draw));
    ASSERT_EQ(6, draw->vertex_count);
    fixture_end(&f);
}

TEST(engine_visuals, vertex_render_draws_a_sprite_at_each_vertex_instead_of_the_mesh)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Light");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && texture != 0 && object != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[12] = 10.0f;
    f.owner_id = object; /* its target is its owner */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t render = block_on(&f, GUID_VERTEX_RENDER, "Vertex Render", 0);
    ASSERT_TRUE(render != 0);
    const float size = 2.0f;
    fixture_input_int(&f, BLOCK_PIN(&f, render, 0), (int32_t)texture);
    fixture_input_value(&f, BLOCK_PIN(&f, render, 1), &size, sizeof(size));
    fixture_input_int(&f, BLOCK_PIN(&f, render, 2), 0); /* no fog */
    fixture_link(&f, BLOCK_OUT(&f, render, 0), BLOCK_IN(&f, render, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(mesh_draw(&frame, object) >= 0);

    /* Three squares facing the eye (no camera: looking down Z), 2 wide, instead of the face */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(-1, mesh_draw(&frame, object));
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(1, primitive_draws(&frame, object, &draw));
    ASSERT_EQ(18, draw->vertex_count);
    ASSERT_TRUE(draw->transparent && draw->no_fog && !draw->lit && !draw->two_sided);
    vertex_at(&frame, draw, 0, 9, 1, 0, 0, 0);
    vertex_at(&frame, draw, 1, 11, 1, 0, 1, 0);
    vertex_at(&frame, draw, 2, 11, -1, 0, 1, 1);
    vertex_at(&frame, draw, 5, 9, -1, 0, 0, 1);
    vertex_at(&frame, draw, 6, 10, 1, 0, 0, 0); /* the second vertex, (1,0,0) */
    ASSERT_EQ(0xFFFFFFFFu, frame.vertices[draw->first_vertex].color);
    nmo_engine_material_t sprite;
    ASSERT_TRUE(nmo_engine_material(f.engine, draw->material, &sprite));
    ASSERT_EQ(texture, sprite.texture);
    ASSERT_EQ(VXTEXTUREBLEND_DECAL, sprite.texture_blend);
    ASSERT_TRUE(sprite.alpha_blend && !sprite.z_write);
    ASSERT_EQ(VXBLEND_ONE, sprite.source_blend);
    ASSERT_EQ(VXBLEND_ONE, sprite.dest_blend);

    /* Off: the mesh again, opaque */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int at = mesh_draw(&frame, object);
    ASSERT_TRUE(at >= 0);
    ASSERT_FALSE(frame.draws[at].transparent);
    ASSERT_EQ(0, primitive_draws(&frame, object, &draw));
    fixture_end(&f);
}

TEST(engine_visuals, mark_system_draws_after_its_supporting_entity_while_it_renders)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t a = face_object(&f, "A", material, &mesh);
    nmo_object_id_t support = face_object(&f, "Support", material, &mesh);
    nmo_object_id_t c = face_object(&f, "C", material, &mesh);
    ASSERT_TRUE(material != 0 && a != 0 && support != 0 && c != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t marks = block_on(&f, GUID_MARK_SYSTEM, "Mark System", 0);
    ASSERT_TRUE(marks != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, marks, 2), 0);
    /* Supporting Entity, a setting: set as the engine starts, like an input's value */
    engine_fixture_value_t *setting = &f.values[f.value_count++];
    setting->param = block_local(&f, marks, 3);
    memcpy(setting->bytes, &support, sizeof(int32_t));
    setting->size = sizeof(int32_t);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(1, primitive_draws(&frame, support, &draw));
    ASSERT_TRUE(draw == &frame.draws[mesh_draw(&frame, support) + 1]);
    ASSERT_TRUE(mesh_draw(&frame, c) > mesh_draw(&frame, support));

    eng_show(f.engine, support, 0); /* not rendered: no marks */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, primitive_draws(&frame, support, &draw));
    fixture_end(&f);
}

TEST(engine_visuals, a_box_set_keeps_a_plain_supporting_entity_drawing_marks_out_of_view)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    /* both supports behind the eye: a plain 3D entity and a 3D object, neither with a mesh */
    nmo_object_id_t plain = fixture_create(&f, NMO_CID_3DENTITY, "Plain");
    nmo_object_id_t object = fixture_create(&f, NMO_CID_3DOBJECT, "Object");
    ASSERT_TRUE(lens != NULL && plain != 0 && object != 0);
    lens->entity.has_entityndata_chunk = 1;
    lens->projection_type = 1;
    lens->fov = 1.0f;
    lens->aspect_width = 4;
    lens->aspect_height = 3;
    lens->near_plane = 1.0f;
    lens->far_plane = 100.0f;
    const nmo_object_id_t supports[2] = {plain, object};
    for (int i = 0; i < 2; i++) {
        nmo_3dentity_state_t *state = (nmo_3dentity_state_t *)fixture_state(&f, supports[i]);
        state->has_entityndata_chunk = 1;
        memset(state->world_matrix, 0, sizeof(state->world_matrix));
        state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = state->world_matrix[15] = 1.0f;
        state->world_matrix[14] = -10.0f;
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    for (int i = 0; i < 2; i++) {
        nmo_object_id_t marks = block_on(&f, GUID_MARK_SYSTEM, i == 0 ? "Plain Marks" : "Object Marks", 0);
        ASSERT_TRUE(marks != 0);
        fixture_link(&f, f.script_in, BLOCK_IN(&f, marks, 2), 0);
        engine_fixture_value_t *setting = &f.values[f.value_count++];
        setting->param = block_local(&f, marks, 3);
        memcpy(setting->bytes, &supports[i], sizeof(int32_t));
        setting->size = sizeof(int32_t);
    }
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    eng_show(f.engine, plain, 1);
    eng_show(f.engine, object, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    /* the plain entity's box is 10000 around it: in view, its marks drawn; the object's origin is
       behind the eye: culled, no marks */
    eng_vector_t low, high;
    ASSERT_TRUE(eng_entity_local_box(f.engine, eng_entity_index(f.engine, plain), &low, &high));
    ASSERT_FLOAT_EQ(-10000.0f, low.x, 0.0f);
    ASSERT_FLOAT_EQ(10000.0f, high.z, 0.0f);
    ASSERT_FALSE(eng_entity_local_box(f.engine, eng_entity_index(f.engine, object), &low, &high));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = NULL;
    ASSERT_EQ(1, primitive_draws(&frame, plain, &draw));
    ASSERT_EQ(0, primitive_draws(&frame, object, &draw));
    fixture_end(&f);
}

TEST(engine_visuals, a_box_set_follows_the_entity_until_another_mesh_takes_it_away)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0, other = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    nmo_object_id_t spare = face_object(&f, "Spare", material, &other);
    ASSERT_TRUE(object != 0 && spare != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[12] = 5.0f;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t index = eng_entity_index(f.engine, object);

    /* set in the world: kept in the entity's space, so it moves with the entity */
    const eng_vector_t low = {4, -1, -1}, high = {6, 1, 1};
    eng_entity_set_user_box(f.engine, index, &low, &high, false);
    eng_vector_t min, max;
    ASSERT_TRUE(eng_entity_local_box(f.engine, index, &min, &max));
    ASSERT_FLOAT_EQ(-1.0f, min.x, 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, max.x, 1e-6f);
    const eng_vector_t step = {10, 0, 0};
    eng_entity_translate(f.engine, index, &step, -1, false);
    ASSERT_TRUE(eng_entity_world_box(f.engine, index, &min, &max));
    ASSERT_FLOAT_EQ(14.0f, min.x, 1e-5f);
    ASSERT_FLOAT_EQ(16.0f, max.x, 1e-5f);

    /* the same mesh keeps it, another takes it away: the mesh's box again (0..1) */
    ASSERT_TRUE(eng_entity_set_current_mesh(f.engine, object, mesh));
    ASSERT_TRUE(eng_entity_local_box(f.engine, index, &min, &max));
    ASSERT_FLOAT_EQ(-1.0f, min.x, 1e-6f);
    ASSERT_TRUE(eng_entity_set_current_mesh(f.engine, object, other));
    ASSERT_TRUE(eng_entity_local_box(f.engine, index, &min, &max));
    ASSERT_FLOAT_EQ(0.0f, min.x, 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, max.x, 1e-6f);
    fixture_end(&f);
}

/* The frame's mesh draws of `object`, up to `max`, in order */
static int mesh_draws(const nmo_engine_frame_t *frame, nmo_object_id_t object, const nmo_engine_draw_t **out, int max)
{
    int count = 0;
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_MESH && frame->draws[i].object == object) {
            if (count < max) {
                out[count] = &frame->draws[i];
            }
            count++;
        }
    }
    return count;
}

TEST(engine_visuals, motion_blur_draws_the_mesh_faded_along_its_last_matrices)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && object != 0);
    f.owner_id = object;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t blur = block_on(&f, GUID_MOTION_BLUR, "Motion Blur", 0);
    ASSERT_TRUE(blur != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, blur, 0), 4);
    fixture_link(&f, BLOCK_OUT(&f, blur, 0), BLOCK_IN(&f, blur, 1), 3); /* Off three frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));

    /* Still: the mesh once, as it is, rendered last */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draws[8];
    ASSERT_EQ(1, mesh_draws(&frame, object, draws, 8));
    ASSERT_TRUE(draws[0]->transparent && !draws[0]->fade);

    /* Moved: four copies, a third back (the oldest three, still at 0) the most opaque, then the
       newest at half, in that order */
    const int32_t index = eng_entity_index(f.engine, object);
    eng_vector_t step = {1.0f, 0.0f, 0.0f};
    for (int moves = 1; moves <= 2; moves++) {
        eng_entity_translate(f.engine, index, &step, -1, false);
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(4, mesh_draws(&frame, object, draws, 8));
    const float x[4] = {1, 0, 0, 2}, alpha[4] = {1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 0.5f};
    for (int k = 0; k < 4; k++) {
        ASSERT_TRUE(draws[k]->fade && draws[k]->transparent);
        ASSERT_FLOAT_EQ(x[k], draws[k]->world[12], 1e-5f);
        ASSERT_FLOAT_EQ(alpha[k], draws[k]->fade_alpha, 1e-6f);
    }

    /* Off: the mesh once, opaque again */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, mesh_draws(&frame, object, draws, 8));
    ASSERT_TRUE(!draws[0]->transparent && !draws[0]->fade);
    fixture_end(&f);
}

/* An attribute `name` with a structure value whose first fields are `count` int parameters
   (`values`, an object's ID for an object field). After fixture_begin_edit(). */
static void struct_attribute(engine_fixture_t *f, nmo_object_id_t object, const char *name, const int32_t *values,
                             int count)
{
    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = CKPGUID_FLOOR,
        .category = "Test", .has_compatible_class_id = true, .compatible_class_id = NMO_CID_3DENTITY,
        .has_flags = true, .flags = 0};
    uint32_t type = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx), name,
                                                                     &options, &type));
    nmo_object_id_t parameter = fixture_create(f, NMO_CID_PARAMETEROUT, name);
    nmo_parameter_state_t *state = (nmo_parameter_state_t *)fixture_state(f, parameter);
    state->has_state = true;
    state->mode = CKPARAM_MODE_BUFFER;
    state->type_guid = CKPGUID_FLOOR;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&state->buffer_data, 1, 20, NULL));
    memset(state->buffer_data.data, 0, 20);
    for (int i = 0; i < count; i++) {
        nmo_object_id_t field = fixture_create(f, NMO_CID_PARAMETERLOCAL, "Field");
        nmo_parameter_state_t *value = (nmo_parameter_state_t *)fixture_state(f, field);
        value->has_state = true;
        value->mode = CKPARAM_MODE_BUFFER;
        value->type_guid = CKPGUID_INT;
        ASSERT_EQ(NMO_OK, nmo_array_alloc(&value->buffer_data, 1, 4, NULL));
        memcpy(value->buffer_data.data, &values[i], 4);
        memcpy((uint8_t *)state->buffer_data.data + 4 * i, &field, 4);
    }
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(f, object))->attributes,
                                                          parameter, type, NULL));
}

/* The frame's index of the mesh draw of `object` of the kind `stencil_only`; -1 for none */
static int mesh_draw_of(const nmo_engine_frame_t *frame, nmo_object_id_t object, bool stencil_only)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_MESH && frame->draws[i].object == object &&
            frame->draws[i].stencil_only == stencil_only) {
            return i;
        }
    }
    return -1;
}

TEST(engine_visuals, planar_reflection_mirrors_reflected_objects_inside_the_plane)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && object != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[13] = 2.0f;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t floor_mesh = 0;
    nmo_object_id_t plane = floor_object(&f, &floor_mesh);
    ASSERT_TRUE(plane != 0);
    const int32_t fields[2] = {1, 0}; /* group 1, no mesh of its own */
    struct_attribute(&f, object, "Reflected Object", fields, 2);
    nmo_object_id_t mirror = block_on(&f, GUID_PLANAR_REFLECTION, "Planar Reflection", plane);
    ASSERT_TRUE(mirror != 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* The plane's pixels marked in stencil bit 0, then the face mirrored to y = -2 drawn there,
       then the plane itself with no stencil test */
    int mask = mesh_draw_of(&frame, plane, true), plane_at = mesh_draw_of(&frame, plane, false);
    ASSERT_TRUE(mask >= 0 && plane_at > mask);
    const nmo_engine_draw_t *marked = &frame.draws[mask];
    ASSERT_TRUE(marked->stencil.enabled);
    ASSERT_EQ(VXCMP_ALWAYS, marked->stencil.func);
    ASSERT_EQ(VXSTENCILOP_REPLACE, marked->stencil.pass);
    ASSERT_EQ(1u, marked->stencil.write_mask);
    const nmo_engine_draw_t *mirrored = &frame.draws[mask + 1];
    ASSERT_EQ(object, mirrored->object);
    ASSERT_TRUE(mirrored->mirrored && !mirrored->stencil_only);
    ASSERT_FLOAT_EQ(-2.0f, mirrored->world[13], 1e-5f);
    ASSERT_FLOAT_EQ(-1.0f, mirrored->world[5], 1e-5f);
    ASSERT_TRUE(mirrored->stencil.enabled);
    ASSERT_EQ(VXCMP_EQUAL, mirrored->stencil.func);
    ASSERT_EQ(1u, mirrored->stencil.mask);
    ASSERT_EQ(VXSTENCILOP_KEEP, mirrored->stencil.pass);
    ASSERT_FALSE(frame.draws[plane_at].stencil.enabled);
    int own = mesh_draw_of(&frame, object, false);
    ASSERT_TRUE(own >= 0 && !frame.draws[own].mirrored && frame.draws[own].world[13] == 2.0f);
    fixture_end(&f);
}

TEST(engine_visuals, planar_shadow_flattens_shadow_objects_on_the_plane_from_the_light)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    nmo_object_id_t light = fixture_create(&f, NMO_CID_LIGHT, "Light");
    ASSERT_TRUE(material != 0 && object != 0 && light != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[13] = 2.0f;
    nmo_light_state_t *point = (nmo_light_state_t *)fixture_state(&f, light);
    point->light_data.type = VX_LIGHTPOINT;
    const float at[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 10, 10, 1};
    memcpy(point->entity.world_matrix, at, sizeof(at));
    point->entity.has_entityndata_chunk = 1;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t floor_mesh = 0;
    nmo_object_id_t plane = floor_object(&f, &floor_mesh);
    ASSERT_TRUE(plane != 0);
    const int32_t fields[2] = {1, 0};
    struct_attribute(&f, object, "Planar Shadow Object", fields, 2);
    nmo_object_id_t shadow = block_on(&f, GUID_PLANAR_SHADOW, "Planar Shadow", plane);
    ASSERT_TRUE(shadow != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, shadow, 1), (int32_t)light);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* The plane writes stencil bit 0 as it draws; right after it, the face's shadow from the
       light at (0,10,10), black, blended, only where the plane is */
    int plane_at = mesh_draw_of(&frame, plane, false);
    ASSERT_TRUE(plane_at >= 0);
    const nmo_engine_draw_t *drawn = &frame.draws[plane_at];
    ASSERT_TRUE(drawn->stencil.enabled);
    ASSERT_EQ(VXCMP_ALWAYS, drawn->stencil.func);
    ASSERT_EQ(VXSTENCILOP_REPLACE, drawn->stencil.pass);
    ASSERT_EQ(1u, drawn->stencil.write_mask);
    const nmo_engine_draw_t *cast = &frame.draws[plane_at + 1];
    ASSERT_EQ(NMO_ENGINE_DRAW_PRIMITIVE, cast->kind);
    ASSERT_EQ(plane, cast->object);
    ASSERT_EQ(3, cast->vertex_count);
    vertex_at(&frame, cast, 0, 0, 0, -2.5f, 0, 0);
    vertex_at(&frame, cast, 1, 1.25f, 0, -2.5f, 0, 0);
    vertex_at(&frame, cast, 2, 0, 0, -10.0f / 7.0f * 3.0f, 0, 0);
    ASSERT_EQ(0xFF000000u, frame.vertices[cast->first_vertex].color);
    ASSERT_TRUE(cast->blend && cast->no_z_write && cast->two_sided && cast->material == 0);
    ASSERT_EQ(VXBLEND_SRCALPHA, cast->source_blend);
    ASSERT_EQ(VXBLEND_INVSRCALPHA, cast->dest_blend);
    ASSERT_FLOAT_EQ(1.005f, cast->projection_z_scale, 1e-7f);
    ASSERT_TRUE(cast->stencil.enabled);
    ASSERT_EQ(VXCMP_EQUAL, cast->stencil.func);
    ASSERT_EQ(1u, cast->stencil.mask);
    ASSERT_EQ(VXSTENCILOP_KEEP, cast->stencil.pass);
    /* the stencil off again after the plane */
    int own = mesh_draw_of(&frame, object, false);
    ASSERT_TRUE(own >= 0);
    ASSERT_FALSE(frame.draws[own].stencil.enabled);
    fixture_end(&f);
}

/* A pass like Texture Render's, asked for straight from a manager: the engine's side of it,
   without the block */
static nmo_object_id_t pass_texture;

static void pass_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage, const eng_render_args_t *args)
{
    (void)state;
    (void)args;
    if (stage == ENG_RENDER_PASSES && pass_texture != 0) {
        eng_scene_view_t view;
        memset(&view, 0, sizeof(view));
        view.eye[0] = view.eye[5] = view.eye[10] = view.eye[15] = 1.0f;
        view.background_color = 0xFF808080u;
        view.render_options = CK_RENDER_USECAMERARATIO | CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ;
        eng_render_scene_pass(engine, pass_texture, 0, &view);
        pass_texture = 0; /* once */
    }
}

static const eng_manager_desc_t pass_manager = {.name = "Test Pass", .render = pass_render};

TEST(engine_visuals, a_scene_pass_draws_the_scene_into_a_texture_before_the_frame)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Target");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && texture != 0 && object != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, eng_add_manager(f.engine, &pass_manager, NULL));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(eng_texture_create(f.engine, eng_texture(f.engine, texture), 64, 32, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    pass_texture = texture;
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* One pass into the 64x32 texture, the 4:3 camera ratio centered in it, its background,
       drawing the scene as the frame does, its draws before the frame's */
    ASSERT_EQ(1, frame.pass_count);
    const nmo_engine_pass_t *pass = &frame.passes[0];
    ASSERT_EQ(texture, pass->texture);
    ASSERT_EQ(0, pass->slot);
    ASSERT_EQ(64, pass->width);
    ASSERT_EQ(32, pass->height);
    ASSERT_EQ(11, pass->viewport[0]);
    ASSERT_EQ(0, pass->viewport[1]);
    ASSERT_EQ(42, pass->viewport[2]);
    ASSERT_EQ(32, pass->viewport[3]);
    ASSERT_EQ(42, pass->frame.width);
    ASSERT_EQ(32, pass->frame.height);
    ASSERT_EQ(0xFF808080u, pass->frame.background_color);
    ASSERT_TRUE(mesh_draw(&pass->frame, object) >= 0);
    ASSERT_TRUE(mesh_draw(&frame, object) >= 0);
    ASSERT_TRUE(pass->frame.draws + pass->frame.draw_count <= frame.draws);
    ASSERT_TRUE(pass->frame.lights == frame.lights && pass->frame.vertices == frame.vertices);

    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, frame.pass_count);
    fixture_end(&f);
}

TEST(engine_visuals, texture_render_asks_for_a_pass_into_its_target_once_a_run)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Target");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && texture != 0 && object != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t render = block_on(&f, GUID_TEXTURE_RENDER, "Texture Render", texture);
    ASSERT_TRUE(render != 0);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, render, 1), red, sizeof(red));
    fixture_commit(&f, NULL);
    /* Render Options (after the three unnamed None slots) as CK reads the prototype's default:
       its "Clear Stencil" names no flag (the type has "Clear Stencil Buffer") and is skipped */
    const int32_t options = CK_RENDER_USECAMERARATIO | CK_RENDER_CLEARZ | CK_RENDER_CLEARBACK |
                            CK_RENDER_CLEARVIEWPORT;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, local_of(&f, render, 3), &options, sizeof(options)));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(eng_texture_create(f.engine, eng_texture(f.engine, texture), 64, 32, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* No Camera (and none attached): the identity view, the default options' 4:3 camera ratio
       centered in the 64x32 target, the Background Color behind the scene */
    ASSERT_EQ(1, frame.pass_count);
    const nmo_engine_pass_t *pass = &frame.passes[0];
    ASSERT_EQ(texture, pass->texture);
    ASSERT_EQ(0, pass->slot);
    ASSERT_EQ(64, pass->width);
    ASSERT_EQ(32, pass->height);
    ASSERT_EQ(11, pass->viewport[0]);
    ASSERT_EQ(0, pass->viewport[1]);
    ASSERT_EQ(42, pass->viewport[2]);
    ASSERT_EQ(32, pass->viewport[3]);
    ASSERT_EQ(0xFFFF0000u, pass->frame.background_color);
    ASSERT_TRUE(mesh_draw(&pass->frame, object) >= 0);
    ASSERT_TRUE(pass->frame.draws + pass->frame.draw_count <= frame.draws);

    /* It ran once (its In went off): no pass the next frame */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, frame.pass_count);
    fixture_end(&f);
}

/* An attribute `name` (with a float value: the blocks only look for it). After
   fixture_begin_edit(). */
static void plain_attribute(engine_fixture_t *f, nmo_object_id_t object, const char *name)
{
    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = CKPGUID_FLOAT,
        .category = "Test", .has_compatible_class_id = true, .compatible_class_id = NMO_CID_3DENTITY,
        .has_flags = true, .flags = 0};
    uint32_t type = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f->tx), name,
                                                                     &options, &type));
    nmo_object_id_t parameter = fixture_create(f, NMO_CID_PARAMETEROUT, name);
    nmo_parameter_state_t *value = (nmo_parameter_state_t *)fixture_state(f, parameter);
    value->has_state = true;
    value->mode = CKPARAM_MODE_BUFFER;
    value->type_guid = CKPGUID_FLOAT;
    ASSERT_EQ(NMO_OK, nmo_array_alloc(&value->buffer_data, 1, 4, NULL));
    memset(value->buffer_data.data, 0, 4);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(f, object))->attributes,
                                                          parameter, type, NULL));
}

TEST(engine_visuals, shadow_caster_draws_its_shadow_texture_and_projects_it_on_receivers)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    nmo_object_id_t light = fixture_create(&f, NMO_CID_LIGHT, "Light");
    ASSERT_TRUE(material != 0 && object != 0 && light != 0);
    nmo_mesh_update_bounding_volumes((nmo_mesh_state_t *)fixture_state(&f, mesh));
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->world_matrix[13] = 2.0f;
    nmo_light_state_t *point = (nmo_light_state_t *)fixture_state(&f, light);
    point->light_data.type = VX_LIGHTPOINT;
    const float at[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 10, 0, 1};
    memcpy(point->entity.world_matrix, at, sizeof(at));
    point->entity.has_entityndata_chunk = 1;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t floor_mesh = 0;
    nmo_object_id_t floor = floor_object(&f, &floor_mesh);
    ASSERT_TRUE(floor != 0);
    nmo_mesh_state_t *floor_state = (nmo_mesh_state_t *)fixture_state(&f, floor_mesh);
    floor_state->faces[0].normal = floor_state->faces[1].normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
    floor_state->faces[0].channel_mask = floor_state->faces[1].channel_mask = 0xFFFF;
    plain_attribute(&f, floor, "Shadow Caster Receiver");
    nmo_object_id_t caster = block_on(&f, GUID_SHADOW_CASTER, "ShadowCaster", object);
    ASSERT_TRUE(caster != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, caster, 0), (int32_t)light);
    fixture_link(&f, BLOCK_OUT(&f, caster, 0), BLOCK_IN(&f, caster, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* The shadow pass: 128 square, white, the face in black, then the light's distance
       ((7.52 - 5) / 45 of white) added over it */
    ASSERT_EQ(1, frame.pass_count);
    const nmo_engine_pass_t *pass = &frame.passes[0];
    ASSERT_EQ(128, pass->width);
    ASSERT_EQ(128, pass->height);
    ASSERT_EQ(0xFFFFFFFFu, pass->frame.background_color);
    ASSERT_FLOAT_EQ(0.2f, pass->frame.fov, 1e-6f);
    int black = mesh_draw(&pass->frame, object);
    ASSERT_TRUE(black >= 0);
    ASSERT_TRUE(pass->frame.draws[black].blackened && pass->frame.draws[black].no_z_test);
    ASSERT_EQ(2, pass->frame.draw_count);
    const nmo_engine_draw_t *lift = &pass->frame.draws[1];
    ASSERT_EQ(NMO_ENGINE_DRAW_QUAD, lift->kind);
    ASSERT_EQ(0x0E0E0E0Eu, lift->color);
    ASSERT_TRUE(lift->blend);
    ASSERT_EQ(VXBLEND_ONE, lift->source_blend);
    ASSERT_EQ(VXBLEND_ONE, lift->dest_blend);
    ASSERT_FLOAT_EQ(128.0f, lift->rect[2], 0.0f);

    /* The floor multiplied by it, unlit, on its faces turned to the light */
    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);
    const nmo_engine_channel_t *channel = &desc.channels[0];
    nmo_engine_material_t shade;
    ASSERT_TRUE(nmo_engine_material(f.engine, channel->material, &shade));
    ASSERT_EQ(pass->texture, shade.texture);
    ASSERT_TRUE(channel->active && !channel->lit);
    ASSERT_EQ(VXBLEND_ZERO, channel->source_blend);
    ASSERT_EQ(VXBLEND_SRCCOLOR, channel->dest_blend);
    ASSERT_EQ(1u, (unsigned)channel->mask);
    ASSERT_NOT_NULL(channel->uvs);
    ASSERT_TRUE(isfinite(channel->uvs[0]) && isfinite(channel->uvs[1]));
    ASSERT_TRUE(desc.faces[0].channel_mask & 1u);

    /* Off: the channel off the floor */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, floor_mesh, &desc));
    ASSERT_EQ(0, desc.channel_count);
    fixture_end(&f);
}

#define GUID_GLOBAL_BLUR NMO_GUID(0x37f1163d, 0x35ad3e6b)

TEST(engine_visuals, global_blur_stops_the_clear_and_lays_a_faint_quad_before_the_scene)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t object = face_object(&f, "Face", material, &mesh);
    ASSERT_TRUE(material != 0 && object != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t blur = block_on(&f, GUID_GLOBAL_BLUR, "Global Blur", 0);
    ASSERT_TRUE(blur != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, blur, 0), (int32_t)material);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, blur, 1), 2); /* Off two frames on */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_material_t *shade = eng_material(f.engine, material);
    ASSERT_NOT_NULL(shade);
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    memcpy(shade->desc.diffuse, green, sizeof(green));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(frame.render_options & CK_RENDER_CLEARBACK);

    /* On: no background clear, and the material's green over the whole view at a faint random
       alpha (0.01 to 0.21) first, before the face */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_FALSE(frame.render_options & CK_RENDER_CLEARBACK);
    ASSERT_TRUE(frame.draw_count >= 2);
    const nmo_engine_draw_t *quad = &frame.draws[0];
    ASSERT_EQ(NMO_ENGINE_DRAW_QUAD, quad->kind);
    ASSERT_TRUE(quad->background && quad->blend);
    ASSERT_EQ(VXBLEND_SRCALPHA, quad->source_blend);
    ASSERT_EQ(VXBLEND_INVSRCALPHA, quad->dest_blend);
    ASSERT_FLOAT_EQ((float)frame.width, quad->rect[2], 0.0f);
    ASSERT_FLOAT_EQ((float)frame.height, quad->rect[3], 0.0f);
    ASSERT_EQ(0x0000FF00u, quad->color & 0x00FFFFFFu);
    uint32_t alpha = quad->color >> 24;
    ASSERT_TRUE(alpha >= 2u && alpha <= 53u);
    ASSERT_TRUE(mesh_draw(&frame, object) > 0);

    /* Off: cleared again, no quad */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_TRUE(frame.render_options & CK_RENDER_CLEARBACK);
    ASSERT_EQ(NMO_ENGINE_DRAW_MESH, frame.draws[0].kind);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_visuals, blink_cycles_the_target_and_fps_reports_the_rate);
    REGISTER_TEST(engine_visuals, z_buffer_and_render_order_shape_the_mesh_draws);
    REGISTER_TEST(engine_visuals, make_transparent_blends_the_materials_and_marks_the_meshes);
    REGISTER_TEST(engine_visuals, render_options_and_use_z_information_set_the_frame);
    REGISTER_TEST(engine_visuals, hide_hierarchy_and_3dsprite_mode);
    REGISTER_TEST(engine_visuals, render_curve_draws_the_curve_each_frame_until_off);
    REGISTER_TEST(engine_visuals, render_curve_line_mode_draws_the_line_mesh);
    REGISTER_TEST(engine_visuals, show_object_information_draws_box_and_face_normals_after_the_scene);
    REGISTER_TEST(engine_visuals, the_scene_graph_orders_children_priorities_and_transparent_objects);
    REGISTER_TEST(engine_visuals, the_camera_culls_what_is_out_of_view_as_the_scene_graph_is_traversed);
    REGISTER_TEST(engine_visuals, simple_shadow_multiplies_the_floors_under_its_owner_until_off);
    REGISTER_TEST(engine_visuals, simple_shadow_skips_floors_outside_the_last_render);
    REGISTER_TEST(engine_visuals, solid_trail_follows_the_owners_farthest_vertices_until_off);
    REGISTER_TEST(engine_visuals, mark_system_draws_single_marks_that_fade_and_die);
    REGISTER_TEST(engine_visuals, mark_system_joins_continuous_marks_into_a_band);
    REGISTER_TEST(engine_visuals, mark_system_draws_after_its_supporting_entity_while_it_renders);
    REGISTER_TEST(engine_visuals, a_box_set_keeps_a_plain_supporting_entity_drawing_marks_out_of_view);
    REGISTER_TEST(engine_visuals, a_box_set_follows_the_entity_until_another_mesh_takes_it_away);
    REGISTER_TEST(engine_visuals, vertex_render_draws_a_sprite_at_each_vertex_instead_of_the_mesh);
    REGISTER_TEST(engine_visuals, motion_blur_draws_the_mesh_faded_along_its_last_matrices);
    REGISTER_TEST(engine_visuals, planar_reflection_mirrors_reflected_objects_inside_the_plane);
    REGISTER_TEST(engine_visuals, planar_shadow_flattens_shadow_objects_on_the_plane_from_the_light);
    REGISTER_TEST(engine_visuals, a_scene_pass_draws_the_scene_into_a_texture_before_the_frame);
    REGISTER_TEST(engine_visuals, texture_render_asks_for_a_pass_into_its_target_once_a_run);
    REGISTER_TEST(engine_visuals, shadow_caster_draws_its_shadow_texture_and_projects_it_on_receivers);
    REGISTER_TEST(engine_visuals, global_blur_stops_the_clear_and_lays_a_faint_quad_before_the_scene);
TEST_MAIN_END()
