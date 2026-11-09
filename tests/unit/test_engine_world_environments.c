/**
 * @file test_engine_world_environments.c
 * @brief WorldEnvironments.dll blocks run in a graph: what the frame's
 *        background and ambient light become.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "object/builtin/nmo_camera_schemas.h"

#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"

#include <math.h>

#define GUID_SET_BACKGROUND_IMAGE NMO_GUID(0x1254d1ce, 0x001d0a58)
#define GUID_SET_BACKGROUND_MATERIAL NMO_GUID(0xddccefec, 0xd010c102)
#define GUID_SET_AMBIENT_LIGHT_COLOR NMO_GUID(0x136b3f22, 0x0dc12786)
#define GUID_SKY_AROUND NMO_GUID(0xed453298, 0x01ae789f)
#define GUID_CLOUDS_AROUND NMO_GUID(0x77c04509, 0x00a36a46)
#define GUID_SKY_AROUND_CUBEMAP NMO_GUID(0x01157f66, 0x0af04fd2)

/* A block run when the script starts, after `*previous` */
static nmo_object_id_t chain(engine_fixture_t *f, nmo_guid_t guid, const char *name, nmo_object_id_t *previous)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    if (id != 0) {
        fixture_link(f, *previous != 0 ? BLOCK_OUT(f, *previous, 0) : f->script_in, BLOCK_IN(f, id, 0), 0);
        *previous = id;
    }
    return id;
}

TEST(engine_world_environments, background_and_ambient_blocks_set_the_frame)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t image = fixture_create(&f, NMO_CID_TEXTURE, "Sky");
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Backdrop");
    ASSERT_TRUE(image != 0 && material != 0);
    nmo_object_id_t previous = 0;
    nmo_object_id_t from_material = chain(&f, GUID_SET_BACKGROUND_MATERIAL, "Set Background Material", &previous);
    nmo_object_id_t from_image = chain(&f, GUID_SET_BACKGROUND_IMAGE, "Set Background Image", &previous);
    nmo_object_id_t ambient = chain(&f, GUID_SET_AMBIENT_LIGHT_COLOR, "Set Ambient Light Color", &previous);
    ASSERT_TRUE(from_material != 0 && from_image != 0 && ambient != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, from_material, 0), (int32_t)material);
    fixture_input_int(&f, BLOCK_PIN(&f, from_image, 0), (int32_t)image);
    const float color[4] = {0.5f, 0.25f, 1.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, ambient, 0), color, sizeof(color));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    eng_material(f.engine, material)->desc.diffuse[0] = 1.0f; /* a red backdrop */
    eng_material(f.engine, material)->desc.diffuse[1] = 0.0f;
    eng_material(f.engine, material)->desc.diffuse[2] = 0.0f;
    eng_material(f.engine, material)->desc.diffuse[3] = 1.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0xFFFF0000u, frame.background_color); /* the material's diffuse */
    ASSERT_EQ(image, frame.background_texture);     /* then the image over its texture */
    /* Opaque; red 127.5 and green 63.75 scaled into place and masked keep 127 and 63 */
    ASSERT_EQ(0xFF7F3FFFu, frame.ambient_light);
    fixture_end(&f);
}

/* The active camera at (1, 2, 3), its field of view one radian */
static nmo_object_id_t camera_at_123(engine_fixture_t *f)
{
    nmo_object_id_t camera = fixture_create(f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *state = (nmo_camera_state_t *)fixture_state(f, camera);
    if (state == NULL) {
        return 0;
    }
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1};
    memcpy(state->entity.world_matrix, world, sizeof(world));
    state->entity.has_entityndata_chunk = state->has_cameraonly_chunk = 1;
    state->projection_type = CK_PERSPECTIVEPROJECTION;
    state->fov = 1.0f;
    state->aspect_width = state->aspect_height = 1;
    state->near_plane = 1;
    state->far_plane = 100;
    return camera;
}

/* The frame's draw of a mesh with a projection of its own; NULL for none */
static const nmo_engine_draw_t *sky_draw(const nmo_engine_frame_t *frame, int *index)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_MESH && frame->draws[i].fov > 0.0f) {
            *index = i;
            return &frame->draws[i];
        }
    }
    return NULL;
}

TEST(engine_world_environments, sky_around_draws_a_cube_first_at_the_camera_until_off)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at_123(&f);
    nmo_object_id_t back = fixture_create(&f, NMO_CID_TEXTURE, "Back");
    ASSERT_TRUE(camera != 0 && back != 0);
    nmo_object_id_t sky = fixture_block(&f, GUID_SKY_AROUND, "Sky Around");
    ASSERT_TRUE(sky != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sky, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sky, 0), BLOCK_IN(&f, sky, 1), 3); /* Off three frames on */
    fixture_input_int(&f, BLOCK_PIN(&f, sky, 0), (int32_t)back);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int index = -1;
    const nmo_engine_draw_t *draw = sky_draw(&frame, &index);
    ASSERT_NOT_NULL(draw);
    ASSERT_EQ(0, index); /* before every 3D entity */
    ASSERT_FLOAT_EQ(1.0f + (3.141592654f - 1.0f) * 0.2f, draw->fov, 1e-5f);
    ASSERT_FLOAT_EQ(200.0f, draw->far_plane, 0.0f);
    ASSERT_TRUE(draw->no_z_test && draw->no_z_write);
    ASSERT_FLOAT_EQ(1.0f, draw->world[12], 0.0f);
    ASSERT_FLOAT_EQ(3.0f, draw->world[14], 0.0f);
    nmo_engine_mesh_t mesh;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, draw->mesh, &mesh));
    ASSERT_EQ(24, mesh.vertex_count);
    ASSERT_EQ(12, mesh.face_count);
    ASSERT_EQ(6, mesh.material_count);
    ASSERT_EQ(0x80FFFFFFu, mesh.colors[0]); /* the Filtering Color */
    nmo_engine_material_t first;
    ASSERT_TRUE(nmo_engine_material(f.engine, nmo_engine_mesh_material(f.engine, draw->mesh, 0), &first));
    ASSERT_EQ(back, first.texture);
    ASSERT_EQ(VXTEXTURE_ADDRESSCLAMP, first.address_mode);

    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_NULL(sky_draw(&frame, &index));
    fixture_end(&f);
}

TEST(engine_world_environments, clouds_around_draws_last_blended)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at_123(&f);
    ASSERT_TRUE(camera != 0);
    nmo_object_id_t clouds = fixture_block(&f, GUID_CLOUDS_AROUND, "Clouds Around");
    ASSERT_TRUE(clouds != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, clouds, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, clouds, 10), VXBLEND_ONE); /* Dest Blend */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int index = -1;
    const nmo_engine_draw_t *draw = sky_draw(&frame, &index);
    ASSERT_NOT_NULL(draw);
    ASSERT_TRUE(draw->transparent);
    ASSERT_FLOAT_EQ(1.0f + (3.141592654f - 1.0f) * 0.3f, draw->fov, 1e-5f);
    nmo_engine_material_t material;
    ASSERT_TRUE(nmo_engine_material(f.engine, nmo_engine_mesh_material(f.engine, draw->mesh, 3), &material));
    ASSERT_TRUE(material.alpha_blend);
    ASSERT_EQ(VXBLEND_SRCALPHA, material.source_blend);
    ASSERT_EQ(VXBLEND_ONE, material.dest_blend);
    fixture_end(&f);
}

/* A cube map texture of six 1 x 1 faces, face i red 40 i */
static nmo_object_id_t cube_texture(engine_fixture_t *f)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_TEXTURE, "Cube");
    nmo_texture_state_t *state = id != 0 ? (nmo_texture_state_t *)fixture_state(f, id) : NULL;
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_texture_raw_slot_t *slots = state != NULL ? (nmo_texture_raw_slot_t *)nmo_arena_alloc(
                                                        arena, 6u * sizeof(*slots), _Alignof(nmo_texture_raw_slot_t))
                                                  : NULL;
    uint8_t *planes = slots != NULL ? (uint8_t *)nmo_arena_alloc(arena, 6u * 3u, 1) : NULL;
    if (planes == NULL) {
        return 0;
    }
    memset(slots, 0, 6u * sizeof(*slots));
    for (int i = 0; i < 6; i++) {
        nmo_texture_raw_slot_t *slot = &slots[i];
        planes[i * 3] = (uint8_t)(40 * i);
        planes[i * 3 + 1] = planes[i * 3 + 2] = 0;
        slot->width = slot->height = 1;
        slot->bits_per_pixel = 32;
        slot->red_size = slot->green_size = slot->blue_size = 1;
        slot->red_data = &planes[i * 3];
        slot->green_data = &planes[i * 3 + 1];
        slot->blue_data = &planes[i * 3 + 2];
    }
    state->slot_count = 6;
    state->bitmap_kind = CKTEXTURE_BITMAP_RAW;
    state->raw_slots = slots;
    state->is_cubemap = 1;
    return id;
}

TEST(engine_world_environments, sky_around_cubemap_draws_its_material_as_a_sky_map)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t camera = camera_at_123(&f);
    nmo_object_id_t texture = cube_texture(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Sky");
    ASSERT_TRUE(camera != 0 && texture != 0 && material != 0);
    nmo_material_state_t *desc = (nmo_material_state_t *)fixture_state(&f, material);
    desc->has_material_data = 1;
    desc->textures[0] = nmo_ref_from_id(texture);
    nmo_object_id_t sky = fixture_block(&f, GUID_SKY_AROUND_CUBEMAP, "Sky Around(CubeMap)");
    ASSERT_TRUE(sky != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sky, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, sky, 0), (int32_t)material);
    const float distance = 50.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, sky, 1), &distance, sizeof(distance));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int index = -1;
    const nmo_engine_draw_t *draw = sky_draw(&frame, &index);
    ASSERT_NOT_NULL(draw);
    ASSERT_EQ(32, draw->texgen); /* VXEFFECT_TGCUBEMAP_SKYMAP while it draws */
    ASSERT_FLOAT_EQ(50.0f, draw->world[0], 0.0f);
    ASSERT_FLOAT_EQ(2.0f, draw->world[13], 0.0f);
    nmo_engine_mesh_t mesh;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, draw->mesh, &mesh));
    ASSERT_EQ(8, mesh.vertex_count);
    ASSERT_EQ(material, nmo_engine_mesh_material(f.engine, draw->mesh, 0));
    nmo_engine_material_t own;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &own));
    ASSERT_EQ(0, own.texgen); /* the material's own effect stays */

    /* The texture's six faces */
    ASSERT_TRUE(nmo_engine_texture_cube_map(f.engine, texture));
    ASSERT_FALSE(nmo_engine_texture_cube_map(f.engine, material));
    nmo_engine_image_t face;
    for (int32_t i = 0; i < 6; i++) {
        ASSERT_TRUE(nmo_engine_texture_face_image(f.engine, texture, i, &face));
        ASSERT_EQ(1, face.width);
        ASSERT_EQ(40 * i, face.rgba[0]);
    }
    ASSERT_FALSE(nmo_engine_texture_face_image(f.engine, texture, 6, &face));
    uint32_t version = face.version;
    ASSERT_TRUE(nmo_engine_texture_face_image(f.engine, texture, 5, &face));
    ASSERT_EQ(version, face.version); /* kept until the texture changes */
    eng_texture_changed(f.engine, texture);
    ASSERT_TRUE(nmo_engine_texture_face_image(f.engine, texture, 5, &face));
    ASSERT_TRUE(face.version != version);
    ASSERT_EQ(200, face.rgba[0]);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_world_environments, background_and_ambient_blocks_set_the_frame);
    REGISTER_TEST(engine_world_environments, sky_around_draws_a_cube_first_at_the_camera_until_off);
    REGISTER_TEST(engine_world_environments, clouds_around_draws_last_blended);
    REGISTER_TEST(engine_world_environments, sky_around_cubemap_draws_its_material_as_a_sky_map);
TEST_MAIN_END()
