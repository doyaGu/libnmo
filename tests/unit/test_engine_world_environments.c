/**
 * @file test_engine_world_environments.c
 * @brief WorldEnvironments.dll blocks run in a graph: what the frame's
 *        background and ambient light become.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#define GUID_SET_BACKGROUND_IMAGE NMO_GUID(0x1254d1ce, 0x001d0a58)
#define GUID_SET_BACKGROUND_MATERIAL NMO_GUID(0xddccefec, 0xd010c102)
#define GUID_SET_AMBIENT_LIGHT_COLOR NMO_GUID(0x136b3f22, 0x0dc12786)

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

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_world_environments, background_and_ambient_blocks_set_the_frame);
TEST_MAIN_END()
