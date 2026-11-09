/**
 * @file test_engine_addons.c
 * @brief The BuildingBlocksAddons1 blocks.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_media.h"
#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

#define GUID_FILTER_TEXTURE NMO_GUID(0x52df323c, 0x39dc1211)
#define GUID_BLEND_TEXTURES NMO_GUID(0x53ee4292, 0x7d932bb1)
#define GUID_WATER_TEXTURE NMO_GUID(0x3df93ddc, 0x9c89c141)
#define GUID_PIXEL_VALUE NMO_GUID(0x473167b6, 0x48f75a1d)
#define GUID_READ_CONFIG NMO_GUID(0x4a888d94, 0x21ccccfa)
#define GUID_WRITE_CONFIG NMO_GUID(0x4a797d94, 0x21ab5cfa)
#define GUID_OUTPUT_TO_CONSOLE NMO_GUID(0x18655b3f, 0x68291dc3)

static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

static void input_text(engine_fixture_t *f, nmo_object_id_t pin, const char *text)
{
    fixture_input_value(f, pin, text, strlen(text) + 1u);
}

/* Slot `slot` of the texture made `width` x `height`, every channel of every pixel `value` */
static eng_surface_t *filled(engine_fixture_t *f, nmo_object_id_t texture, int32_t slot, int32_t width,
                             int32_t height, uint8_t value)
{
    eng_texture_t *t = eng_texture(f->engine, texture);
    if (t == NULL || !eng_texture_create(f->engine, t, width, height, slot)) {
        return NULL;
    }
    eng_surface_t *surface = eng_texture_surface(f->engine, t, slot);
    if (surface != NULL && surface->rgba != NULL) {
        memset(surface->rgba, value, (size_t)width * (size_t)height * 4u);
    }
    return surface;
}

static uint8_t channel(eng_surface_t *surface, int32_t x, int32_t y, int c)
{
    return surface->rgba[4u * ((size_t)y * (size_t)surface->width + (size_t)x) + (size_t)c];
}

TEST(engine_addons, filter_and_blend_textures_use_the_dll_integer_arithmetic)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t filtered = fixture_create(&f, NMO_CID_TEXTURE, "Filtered");
    nmo_object_id_t blended = fixture_create(&f, NMO_CID_TEXTURE, "Blended");
    nmo_object_id_t one = fixture_create(&f, NMO_CID_TEXTURE, "One");
    nmo_object_id_t two = fixture_create(&f, NMO_CID_TEXTURE, "Two");
    ASSERT_TRUE(filtered != 0 && blended != 0 && one != 0 && two != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t filter = fixture_block(&f, GUID_FILTER_TEXTURE, "Filter");
    nmo_object_id_t blend = fixture_block(&f, GUID_BLEND_TEXTURES, "Blend");
    ASSERT_TRUE(filter != 0 && blend != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, filter, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, blend, 0), 0);
    fixture_input_int(&f, fixture_target(&f, filter), (int32_t)filtered);
    const float rows[3][3] = {{1, 2, 1}, {2, 8, 2}, {1, 2, 1}};
    for (int r = 0; r < 3; r++) {
        fixture_input_value(&f, BLOCK_PIN(&f, filter, r), rows[r], sizeof(rows[r]));
    }
    fixture_input_int(&f, fixture_target(&f, blend), (int32_t)blended);
    fixture_input_int(&f, BLOCK_PIN(&f, blend, 0), (int32_t)one);
    fixture_input_int(&f, BLOCK_PIN(&f, blend, 1), (int32_t)two);
    const float quarter = 0.25f;
    fixture_input_value(&f, BLOCK_PIN(&f, blend, 2), &quarter, sizeof(quarter));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_surface_t *image = filled(&f, filtered, 0, 3, 3, 100);
    ASSERT_NOT_NULL(image);
    ASSERT_NOT_NULL(filled(&f, blended, 0, 2, 2, 0));
    ASSERT_NOT_NULL(filled(&f, one, 0, 2, 2, 200));
    ASSERT_NOT_NULL(filled(&f, two, 0, 2, 2, 100));

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* The weights over 20 truncated to 12, 25 and 102: a flat 100 comes out 97 */
    image = eng_texture_surface(f.engine, eng_texture(f.engine, filtered), 0);
    for (int c = 0; c < 4; c++) {
        ASSERT_EQ(97, channel(image, 1, 1, c));
        ASSERT_EQ(97, channel(image, 0, 2, c));
    }
    /* (200 x 192 + 100 x 64) >> 8 */
    eng_surface_t *mix = eng_texture_surface(f.engine, eng_texture(f.engine, blended), 0);
    ASSERT_EQ(175, channel(mix, 1, 0, 0));
    ASSERT_EQ(175, channel(mix, 0, 1, 3));
    fixture_end(&f);
}

TEST(engine_addons, water_texture_steps_the_older_slot_and_swaps)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t water = fixture_create(&f, NMO_CID_TEXTURE, "Water");
    ASSERT_TRUE(water != 0);
    f.owner_id = water; /* the block is not targetable: it steps its owner */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t step = fixture_block(&f, GUID_WATER_TEXTURE, "Step");
    ASSERT_TRUE(step != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, step, 0), 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_surface_t *now = filled(&f, water, 0, 4, 4, 0);
    ASSERT_NOT_NULL(now);
    memset(&now->rgba[4u * (1u * 4u + 1u)], 200, 4u); /* (1, 1) */
    ASSERT_NOT_NULL(filled(&f, water, 1, 4, 4, 0));
    eng_texture(f.engine, water)->current_slot = 0;

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    eng_texture_t *texture = eng_texture(f.engine, water);
    ASSERT_EQ(1, texture->current_slot);
    eng_surface_t *older = eng_texture_surface(f.engine, texture, 1);
    /* inside: the rounded means of (0, 0) and (200, 0) averaged, doubled: 100; the top row
       sums its four neighbours to a quarter: 50, doubled */
    ASSERT_EQ(100, channel(older, 2, 1, 0));
    ASSERT_EQ(100, channel(older, 1, 0, 1));
    ASSERT_EQ(0, channel(older, 1, 1, 0));
    ASSERT_EQ(0, channel(older, 3, 3, 0));
    fixture_end(&f);
}

TEST(engine_addons, pixel_value_reads_a_texel)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t picture = fixture_create(&f, NMO_CID_TEXTURE, "Picture");
    ASSERT_TRUE(picture != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t inside = fixture_block(&f, GUID_PIXEL_VALUE, "Inside");
    nmo_object_id_t outside = fixture_block(&f, GUID_PIXEL_VALUE, "Outside");
    nmo_object_id_t in_range = fixture_block(&f, GUID_OUTPUT_TO_CONSOLE, "In Range");
    nmo_object_id_t out_range = fixture_block(&f, GUID_OUTPUT_TO_CONSOLE, "Out Range");
    ASSERT_TRUE(inside != 0 && outside != 0 && in_range != 0 && out_range != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, inside, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, inside, 0), BLOCK_IN(&f, outside, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, inside, 1), BLOCK_IN(&f, out_range, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, outside, 1), BLOCK_IN(&f, out_range, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, outside, 0), BLOCK_IN(&f, in_range, 0), 0);
    input_text(&f, BLOCK_PIN(&f, in_range, 0), "in");
    input_text(&f, BLOCK_PIN(&f, out_range, 0), "out");
    const float at[2] = {0.6f, 0.6f}, off[2] = {1.6f, 0.6f};
    nmo_object_id_t blocks[2] = {inside, outside};
    for (int i = 0; i < 2; i++) {
        fixture_input_int(&f, fixture_target(&f, blocks[i]), (int32_t)picture);
        fixture_input_value(&f, BLOCK_PIN(&f, blocks[i], 1), i == 0 ? at : off, sizeof(at));
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 2), 1); /* wrap */
    }
    nmo_object_id_t homogeneous[2] = {local_of(&f, inside, 1), local_of(&f, outside, 1)};
    ASSERT_TRUE(homogeneous[0] != 0 && homogeneous[1] != 0);
    fixture_commit(&f, NULL);
    const int32_t yes = 1;
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, homogeneous[i], &yes, sizeof(yes)));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_surface_t *image = filled(&f, picture, 0, 3, 3, 0);
    ASSERT_NOT_NULL(image);
    const uint8_t red[4] = {255, 0, 0, 255};
    memcpy(&image->rgba[4u * (1u * 3u + 1u)], red, 4u);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* (0.6 x 2) truncated: texel (1, 1); 1.6 wraps to 0.6 the same, but Out Range too */
    for (int i = 0; i < 2; i++) {
        float color[4] = {-1, -1, -1, -1};
        read_value(&f, BLOCK_POUT(&f, blocks[i], 0), color, sizeof(color));
        ASSERT_FLOAT_EQ(1.0f, color[0], 1e-6f);
        ASSERT_FLOAT_EQ(0.0f, color[1], 0.0f);
        ASSERT_FLOAT_EQ(1.0f, color[3], 1e-6f);
    }
    /* Inside goes on to Outside by its In Range; Outside opens both */
    ASSERT_EQ(1u, runs(&f, out_range));
    ASSERT_EQ(1u, runs(&f, in_range));
    fixture_end(&f);
}

TEST(engine_addons, config_blocks_and_the_console)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t write = fixture_block(&f, GUID_WRITE_CONFIG, "Write");
    nmo_object_id_t read = fixture_block(&f, GUID_READ_CONFIG, "Read");
    nmo_object_id_t missing = fixture_block(&f, GUID_READ_CONFIG, "Missing");
    nmo_object_id_t console = fixture_block(&f, GUID_OUTPUT_TO_CONSOLE, "Console");
    ASSERT_TRUE(write != 0 && read != 0 && missing != 0 && console != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, write, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, write, 0), BLOCK_IN(&f, read, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, read, 0), BLOCK_IN(&f, missing, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, missing, 1), BLOCK_IN(&f, console, 0), 0);
    nmo_object_id_t blocks[3] = {write, read, missing};
    for (int i = 0; i < 3; i++) {
        input_text(&f, BLOCK_PIN(&f, blocks[i], 0), "MyGame\\Scores");
        input_text(&f, BLOCK_PIN(&f, blocks[i], 1), i == 2 ? "Nobody" : "John");
    }
    fixture_input_int(&f, BLOCK_PIN(&f, write, 2), 42);
    input_text(&f, BLOCK_PIN(&f, console, 0), "hello");
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_STR_EQ("42", nmo_engine_registry_value(f.engine, "Software\\Virtools\\UserConfig\\MyGame\\Scores", "John"));
    int32_t data = 0;
    read_value(&f, BLOCK_POUT(&f, read, 0), &data, sizeof(data));
    ASSERT_EQ(42, data);
    ASSERT_EQ(1u, runs(&f, console)); /* the missing entry */
    ASSERT_EQ(1, nmo_engine_console_line_count(f.engine));
    nmo_engine_console_line_t line = {0};
    ASSERT_TRUE(nmo_engine_console_line(f.engine, 0, &line));
    ASSERT_STR_EQ("hello", line.text);
    ASSERT_FALSE(line.beep);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_addons, filter_and_blend_textures_use_the_dll_integer_arithmetic);
REGISTER_TEST(engine_addons, water_texture_steps_the_older_slot_and_swaps);
REGISTER_TEST(engine_addons, pixel_value_reads_a_texel);
REGISTER_TEST(engine_addons, config_blocks_and_the_console);
TEST_MAIN_END()
