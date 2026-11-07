/**
 * @file test_engine_visuals.c
 * @brief Visuals.dll blocks that set render state: visibility cycles, Z
 *        buffer use, render order, transparency and the render options.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_transform.h"

#include "core/nmo_arena.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
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
       newest at half; all sorted where the object is */
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
        ASSERT_TRUE(draws[k]->fade && draws[k]->transparent && draws[k]->has_sort_position);
        ASSERT_FLOAT_EQ(x[k], draws[k]->world[12], 1e-5f);
        ASSERT_FLOAT_EQ(alpha[k], draws[k]->fade_alpha, 1e-6f);
        ASSERT_FLOAT_EQ(2.0f, draws[k]->sort_position[0], 1e-5f);
    }

    /* Off: the mesh once, opaque again */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, mesh_draws(&frame, object, draws, 8));
    ASSERT_TRUE(!draws[0]->transparent && !draws[0]->fade);
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
    REGISTER_TEST(engine_visuals, simple_shadow_multiplies_the_floors_under_its_owner_until_off);
    REGISTER_TEST(engine_visuals, solid_trail_follows_the_owners_farthest_vertices_until_off);
    REGISTER_TEST(engine_visuals, mark_system_draws_single_marks_that_fade_and_die);
    REGISTER_TEST(engine_visuals, mark_system_joins_continuous_marks_into_a_band);
    REGISTER_TEST(engine_visuals, mark_system_draws_after_its_supporting_entity_while_it_renders);
    REGISTER_TEST(engine_visuals, vertex_render_draws_a_sprite_at_each_vertex_instead_of_the_mesh);
    REGISTER_TEST(engine_visuals, motion_blur_draws_the_mesh_faded_along_its_last_matrices);
TEST_MAIN_END()
