/**
 * @file test_engine_addons.c
 * @brief The BuildingBlocksAddons1 blocks.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_media.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_ref.h"
#include "core/nmo_arena.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define GUID_FILTER_TEXTURE NMO_GUID(0x52df323c, 0x39dc1211)
#define GUID_BLEND_TEXTURES NMO_GUID(0x53ee4292, 0x7d932bb1)
#define GUID_WATER_TEXTURE NMO_GUID(0x3df93ddc, 0x9c89c141)
#define GUID_PIXEL_VALUE NMO_GUID(0x473167b6, 0x48f75a1d)
#define GUID_READ_CONFIG NMO_GUID(0x4a888d94, 0x21ccccfa)
#define GUID_WRITE_CONFIG NMO_GUID(0x4a797d94, 0x21ab5cfa)
#define GUID_OUTPUT_TO_CONSOLE NMO_GUID(0x18655b3f, 0x68291dc3)
#define GUID_GAUGE NMO_GUID(0x6a777fc4, 0x1dc17b23)
#define GUID_REPLACE_RENDERING NMO_GUID(0x208f0030, 0x652f088e)
#define GUID_SYNCHRO_PLAYER NMO_GUID(0x62da29d8, 0x1bbc6234)

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

TEST(engine_addons, gauge_cuts_the_rect_to_the_progression)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t bar = fixture_create(&f, NMO_CID_2DENTITY, "Bar");
    ASSERT_TRUE(bar != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t gauge = fixture_block(&f, GUID_GAUGE, "Gauge");
    ASSERT_TRUE(gauge != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, gauge, 0), 0);
    fixture_input_int(&f, fixture_target(&f, gauge), (int32_t)bar);
    const float half = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, gauge, 0), &half, sizeof(half));
    fixture_input_int(&f, BLOCK_PIN(&f, gauge, 1), 4); /* toward the right */
    fixture_input_int(&f, BLOCK_PIN(&f, gauge, 2), 1);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    const nmo_rect_t full = {10.0f, 10.0f, 110.0f, 30.0f};
    ASSERT_TRUE(eng_2d_set_rect(f.engine, bar, full, false));
    eng_show(f.engine, bar, 1);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    const eng_2d_t *entity = eng_2d_entity(f.engine, bar);
    ASSERT_NOT_NULL(entity);
    ASSERT_FLOAT_EQ(10.0f, entity->rect.left, 0.0f);
    ASSERT_FLOAT_EQ(60.0f, entity->rect.right, 0.0f);
    ASSERT_FLOAT_EQ(30.0f, entity->rect.bottom, 0.0f);
    fixture_end(&f);
}

TEST(engine_addons, synchro_player_pulses_each_row_on_time)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "Beats");
    ASSERT_TRUE(array != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t player = fixture_block(&f, GUID_SYNCHRO_PLAYER, "Player");
    nmo_object_id_t pulse = fixture_block(&f, GUID_OUTPUT_TO_CONSOLE, "Pulse");
    nmo_object_id_t end = fixture_block(&f, GUID_OUTPUT_TO_CONSOLE, "End");
    ASSERT_TRUE(player != 0 && pulse != 0 && end != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, player, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, player, 1), BLOCK_IN(&f, pulse, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, player, 0), BLOCK_IN(&f, end, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, player, 0), (int32_t)array);
    input_text(&f, BLOCK_PIN(&f, pulse, 0), "pulse");
    input_text(&f, BLOCK_PIN(&f, end, 0), "end");
    fixture_commit(&f, NULL);
    /* Time, Amplitude, Length: a beat at 0, one at 50 ms, the end at 100 ms */
    for (int c = 0; c < 3; c++) {
        ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, array, -1, CKARRAYTYPE_FLOAT, NMO_GUID(0u, 0u)));
    }
    const float rows[3][3] = {{0.0f, 0.5f, 10.0f}, {50.0f, 1.0f, 10.0f}, {100.0f, 0.0f, 0.0f}};
    for (int32_t r = 0; r < 3; r++) {
        ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, array, -1, NULL));
        for (int32_t c = 0; c < 3; c++) {
            nmo_engine_cell_t cell = {.type = CKARRAYTYPE_FLOAT};
            memcpy(&cell.value, &rows[r][c], sizeof(float));
            ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, array, r, c, &cell));
        }
    }

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int pulses = 0, ends = 0;
    for (int frame = 0; frame < 8; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        for (int32_t i = 0; i < nmo_engine_console_line_count(f.engine); i++) {
            nmo_engine_console_line_t line = {0};
            ASSERT_TRUE(nmo_engine_console_line(f.engine, i, &line));
            pulses += strcmp(line.text, "pulse") == 0;
            ends += strcmp(line.text, "end") == 0;
        }
        if (frame == 0) {
            float amplitude = 0.0f;
            read_value(&f, BLOCK_POUT(&f, player, 0), &amplitude, sizeof(amplitude));
            ASSERT_FLOAT_EQ(0.5f, amplitude, 0.0f);
        }
    }
    ASSERT_EQ(2, pulses);
    ASSERT_EQ(1, ends);
    fixture_end(&f);
}

TEST(engine_addons, replace_rendering_swaps_a_material_in_the_draws)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t object = fixture_create(&f, NMO_CID_3DOBJECT, "Object");
    nmo_object_id_t mesh = fixture_create(&f, NMO_CID_MESH, "Mesh");
    nmo_object_id_t from = fixture_create(&f, NMO_CID_MATERIAL, "From");
    nmo_object_id_t to = fixture_create(&f, NMO_CID_MATERIAL, "To");
    ASSERT_TRUE(object != 0 && mesh != 0 && from != 0 && to != 0);
    /* one face in the group of From */
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(&f, mesh);
    nmo_arena_t *arena = nmo_session_get_arena(f.session);
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, 3 * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, 3 * sizeof(uint16_t), _Alignof(uint16_t));
    state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(arena, sizeof(nmo_material_group_t),
                                                                      _Alignof(nmo_material_group_t));
    ASSERT_TRUE(state->vertices != NULL && state->faces != NULL && state->face_vertex_indices != NULL &&
                state->material_groups != NULL);
    memset(state->vertices, 0, 3 * sizeof(nmo_vertex_t));
    state->vertices[1].position.x = 1.0f;
    state->vertices[2].position.z = 1.0f;
    memset(state->faces, 0, sizeof(nmo_face_t));
    const uint16_t corners[3] = {0, 1, 2};
    memcpy(state->face_vertex_indices, corners, sizeof(corners));
    state->material_groups[0] = (nmo_material_group_t){nmo_ref_from_id(from), 0};
    state->vertex_count = 3;
    state->face_count = 1;
    state->material_group_count = 1;
    nmo_mesh_update_bounding_volumes(state);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t replace = fixture_block(&f, GUID_REPLACE_RENDERING, "Replace");
    ASSERT_TRUE(replace != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, replace, 0), 0);
    fixture_input_int(&f, fixture_target(&f, replace), (int32_t)object);
    fixture_input_int(&f, BLOCK_PIN(&f, replace, 0), (int32_t)from);
    fixture_input_int(&f, BLOCK_PIN(&f, replace, 1), (int32_t)to);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_entity_set_current_mesh(f.engine, object, mesh));
    eng_show(f.engine, object, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    bool swapped = false;
    for (int32_t d = 0; d < frame.draw_count; d++) {
        const nmo_engine_draw_t *draw = &frame.draws[d];
        if (draw->kind == NMO_ENGINE_DRAW_MESH && draw->object == object) {
            swapped = draw->replaced_material == from && draw->replacing_material == to;
        }
    }
    ASSERT_TRUE(swapped);
    /* the swap is the frame's: the next render without a step has none */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    for (int32_t d = 0; d < frame.draw_count; d++) {
        ASSERT_EQ(0, (int)frame.draws[d].replaced_material);
    }
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_addons, filter_and_blend_textures_use_the_dll_integer_arithmetic);
REGISTER_TEST(engine_addons, water_texture_steps_the_older_slot_and_swaps);
REGISTER_TEST(engine_addons, pixel_value_reads_a_texel);
REGISTER_TEST(engine_addons, config_blocks_and_the_console);
REGISTER_TEST(engine_addons, gauge_cuts_the_rect_to_the_progression);
REGISTER_TEST(engine_addons, synchro_player_pulses_each_row_on_time);
REGISTER_TEST(engine_addons, replace_rendering_swaps_a_material_in_the_draws);
TEST_MAIN_END()
