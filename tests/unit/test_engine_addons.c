/**
 * @file test_engine_addons.c
 * @brief The BuildingBlocksAddons1 blocks.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_media.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_enum_defs.h"
#include "../../src/engine/engine_animation.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_type_guids.h"
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
#define GUID_VOLUMETRIC_FOG NMO_GUID(0xb0f1ca98, 0xe9dabe5f)
#define GUID_LENS_FLARE NMO_GUID(0x76926718, 0x7ee807d5)
#define GUID_SHADOW_STENCIL NMO_GUID(0x60a873c5, 0x51245a67)
#define GUID_CREATE_DECAL NMO_GUID(0x27b71ada, 0x22191538)
#define GUID_ANIMATION_RECORDER NMO_GUID(0x00532d07, 0x1db631cc)
#define GUID_LIGHTMAP NMO_GUID(0x4a797d21, 0x214d37f9)
#define GUID_STATIC_LIGHTMAP NMO_GUID(0x4a797d21, 0x214d37fa)

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


/* A mesh of `side` x `side` vertices one apart in XZ at height 0, two faces a square, normals up */
static nmo_object_id_t grid_mesh(engine_fixture_t *f, const char *name, int side)
{
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_mesh_state_t *state = mesh != 0 ? (nmo_mesh_state_t *)fixture_state(f, mesh) : NULL;
    if (state == NULL) {
        return 0;
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    uint32_t count = (uint32_t)(side * side), faces = (uint32_t)(2 * (side - 1) * (side - 1));
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, count * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, faces * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, faces * 3 * sizeof(uint16_t), _Alignof(uint16_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL) {
        return 0;
    }
    memset(state->vertices, 0, count * sizeof(nmo_vertex_t));
    memset(state->faces, 0, faces * sizeof(nmo_face_t));
    for (int r = 0; r < side; r++) {
        for (int c = 0; c < side; c++) {
            nmo_vertex_t *v = &state->vertices[r * side + c];
            v->position = (nmo_vector_t){(float)c, 0.0f, (float)r};
            v->normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
        }
    }
    uint32_t k = 0;
    for (int r = 0; r + 1 < side; r++) {
        for (int c = 0; c + 1 < side; c++) {
            const uint16_t a = (uint16_t)(r * side + c), b = (uint16_t)(a + 1), d = (uint16_t)(a + side);
            const uint16_t e = (uint16_t)(d + 1);
            const uint16_t quad[6] = {a, d, b, b, d, e};
            memcpy(&state->face_vertex_indices[k * 3], quad, sizeof(quad));
            state->faces[k].normal = state->faces[k + 1].normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
            k += 2;
        }
    }
    state->vertex_count = count;
    state->face_count = faces;
    nmo_mesh_update_bounding_volumes(state);
    return mesh;
}

/* A 3D entity of class `cid` with the world matrix's rows `x`, `y`, `z` and position `p` */
static nmo_object_id_t placed_entity(engine_fixture_t *f, int32_t cid, const char *name, const float x[3],
                                     const float y[3], const float z[3], const float p[3], nmo_object_id_t mesh)
{
    nmo_object_id_t id = fixture_create(f, cid, name);
    nmo_3dentity_state_t *state = id != 0 ? (nmo_3dentity_state_t *)fixture_state(f, id) : NULL;
    if (state == NULL) {
        return 0;
    }
    const float *rows[4] = {x, y, z, p};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            state->world_matrix[4 * r + c] = rows[r][c];
        }
        state->world_matrix[4 * r + 3] = r == 3 ? 1.0f : 0.0f;
    }
    if (mesh != 0) {
        state->current_mesh = nmo_ref_from_id(mesh);
    }
    return id;
}

static const float X_AXIS[3] = {1, 0, 0}, Y_AXIS[3] = {0, 1, 0}, Z_AXIS[3] = {0, 0, 1};

static nmo_object_id_t a_camera(engine_fixture_t *f, const float x[3], const float y[3], const float z[3],
                                const float p[3], float fov)
{
    nmo_object_id_t camera = placed_entity(f, NMO_CID_CAMERA, "Camera", x, y, z, p, 0);
    nmo_camera_state_t *state = camera != 0 ? (nmo_camera_state_t *)fixture_state(f, camera) : NULL;
    if (state != NULL) {
        state->fov = fov;
        state->near_plane = 1.0f;
        state->far_plane = 100.0f;
        state->aspect_width = 4;
        state->aspect_height = 3;
    }
    return camera;
}

static nmo_object_id_t a_group(engine_fixture_t *f, const char *name, const nmo_object_id_t *ids, size_t count)
{
    nmo_object_id_t group = fixture_create(f, NMO_CID_GROUP, name);
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(f, group);
    nmo_ref_t *members = NULL;
    if (state == NULL || nmo_array_extend(&state->object_ids, count, (void **)&members) != NMO_OK) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        members[i] = nmo_ref_from_id(ids[i]);
    }
    return group;
}

TEST(engine_addons, volumetric_fog_writes_the_fog_channel_v)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Floor", 2);
    const float origin[3] = {0, 0, 0}, eye[3] = {0, -1, -10};
    nmo_object_id_t floor = placed_entity(&f, NMO_CID_3DOBJECT, "Floor", X_AXIS, Y_AXIS, Z_AXIS, origin, mesh);
    nmo_object_id_t fog = placed_entity(&f, NMO_CID_3DENTITY, "Fog", X_AXIS, Y_AXIS, Z_AXIS, origin, 0);
    nmo_object_id_t camera = a_camera(&f, X_AXIS, Y_AXIS, Z_AXIS, eye, 1.0f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Fog Material");
    nmo_object_id_t group = a_group(&f, "Fogged", &floor, 1);
    ASSERT_TRUE(mesh && floor && fog && camera && material && group);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t block = fixture_block(&f, GUID_VOLUMETRIC_FOG, "Fog");
    ASSERT_TRUE(block != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, block, 0), 0);
    fixture_input_int(&f, fixture_target(&f, block), (int32_t)fog);
    fixture_input_int(&f, BLOCK_PIN(&f, block, 0), (int32_t)material);
    const float density = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, block, 1), &density, sizeof(density));
    fixture_input_int(&f, BLOCK_PIN(&f, block, 2), (int32_t)group);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    eng_show(f.engine, floor, 1);

    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* The eye 1 under the fog's top looks along +Z: (0, 0, 1) is 11 away, times the density */
    eng_mesh_copy_t *copy = eng_mesh_copy(f.engine, mesh);
    int32_t channel = eng_mesh_channel(copy, material);
    ASSERT_TRUE(channel >= 0);
    size_t stride = 0;
    int32_t count = 0;
    float *uvs = eng_mesh_uvs(f.engine, copy, channel, &stride, &count);
    ASSERT_NOT_NULL(uvs);
    ASSERT_FLOAT_EQ(5.5f, ((float *)((uint8_t *)uvs + 2 * stride))[1], 1e-5f);
    ASSERT_FLOAT_EQ(5.0f, ((float *)((uint8_t *)uvs + 0 * stride))[1], 1e-5f);
    ASSERT_EQ(VXBLEND_INVSRCCOLOR, copy->channels.data[channel].dest_blend);
    fixture_end(&f);
}

TEST(engine_addons, lens_flare_fades_its_flares_in)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    const float at[3] = {0, 0, 50}, origin[3] = {0, 0, 0};
    nmo_object_id_t light = placed_entity(&f, NMO_CID_LIGHT, "Sun", X_AXIS, Y_AXIS, Z_AXIS, at, 0);
    nmo_object_id_t camera = a_camera(&f, X_AXIS, Y_AXIS, Z_AXIS, origin, 1.0f);
    nmo_object_id_t array = fixture_create(&f, NMO_CID_DATAARRAY, "Flares");
    ASSERT_TRUE(light && camera && array);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t flare = fixture_block(&f, GUID_LENS_FLARE, "Flare");
    ASSERT_TRUE(flare != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, flare, 0), 0);
    fixture_input_int(&f, fixture_target(&f, flare), (int32_t)light);
    fixture_input_int(&f, BLOCK_PIN(&f, flare, 0), (int32_t)array);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, array, -1, CKARRAYTYPE_INT, NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, array, -1, CKARRAYTYPE_FLOAT, NMO_GUID(0u, 0u)));
    ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, array, -1, NULL));
    nmo_engine_cell_t at_light = {.type = CKARRAYTYPE_FLOAT};
    const float one = 1.0f;
    memcpy(&at_light.value, &one, sizeof(one));
    ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, array, 0, 1, &at_light));
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;

    /* Seen ahead (no picker): it fades in over its 100 ms, 20 ms a frame */
    nmo_engine_frame_t frame;
    uint32_t alpha = 0;
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
        alpha = 0;
        for (int32_t d = 0; d < frame.draw_count; d++) {
            const nmo_engine_draw_t *draw = &frame.draws[d];
            if (draw->kind == NMO_ENGINE_DRAW_TRIANGLES && draw->object == light && draw->vertex_count == 6) {
                alpha = frame.vertices[draw->first_vertex].color >> 24;
                ASSERT_EQ(VXBLEND_SRCALPHA, draw->source_blend);
                ASSERT_EQ(VXBLEND_ONE, draw->dest_blend);
            }
        }
        ASSERT_EQ((uint32_t)(i == 0 ? 51 : 102), alpha); /* turned on and 20 of its 100 ms in, then 40 */
    }
    fixture_end(&f);
}

TEST(engine_addons, shadow_stencil_draws_volumes_and_the_shadow)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Plate", 2);
    const float above[3] = {0, 5, 0}, origin[3] = {0, 0, 0}, eye[3] = {0, 2, -10};
    nmo_object_id_t caster = placed_entity(&f, NMO_CID_3DOBJECT, "Plate", X_AXIS, Y_AXIS, Z_AXIS, origin, mesh);
    nmo_object_id_t light = placed_entity(&f, NMO_CID_LIGHT, "Lamp", X_AXIS, Y_AXIS, Z_AXIS, above, 0);
    nmo_object_id_t camera = a_camera(&f, X_AXIS, Y_AXIS, Z_AXIS, eye, 1.0f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Shadow");
    ASSERT_TRUE(mesh && caster && light && camera && material);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_manager_entry_create_options_t options = {.enabled = true, .attribute_type_guid = CKPGUID_NONE,
        .category = "Visuals FX", .has_compatible_class_id = true, .compatible_class_id = NMO_CID_3DOBJECT,
        .has_flags = true, .flags = 0};
    uint32_t attribute = 0;
    ASSERT_EQ(NMO_OK, nmo_object_edit_ensure_attribute_manager_entry(nmo_script_edit_workspace_edit(f.tx),
                                                                     "Shadow Stencil Caster", &options, &attribute));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(&((nmo_beobject_state_t *)fixture_state(&f, caster))->attributes,
                                                          0, attribute, NULL));
    nmo_object_id_t shadow = fixture_block(&f, GUID_SHADOW_STENCIL, "Shadow");
    ASSERT_TRUE(shadow != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, shadow, 0), 0);
    fixture_input_int(&f, fixture_target(&f, shadow), (int32_t)light);
    fixture_input_int(&f, BLOCK_PIN(&f, shadow, 1), (int32_t)material);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    eng_show(f.engine, caster, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* the stencil cleared, the volume's four open edges (24 vertices) twice, then the shadow */
    int clears = 0, incr = 0, decr = 0, cover = 0;
    for (int32_t d = 0; d < frame.draw_count; d++) {
        const nmo_engine_draw_t *draw = &frame.draws[d];
        if (draw->kind != NMO_ENGINE_DRAW_PRIMITIVE || !draw->post) {
            continue;
        }
        clears += draw->clears_stencil;
        if (draw->stencil.enabled && draw->stencil.pass == VXSTENCILOP_INCR) {
            incr++;
            ASSERT_EQ(24, draw->vertex_count);
            ASSERT_FALSE(draw->mirrored);
        }
        if (draw->stencil.enabled && draw->stencil.pass == VXSTENCILOP_DECR) {
            decr++;
            ASSERT_TRUE(draw->mirrored);
        }
        if (draw->stencil.enabled && draw->stencil.func == VXCMP_NOTEQUAL) {
            cover++;
            ASSERT_EQ((int64_t)material, (int64_t)draw->material);
        }
    }
    ASSERT_EQ(1, clears);
    ASSERT_EQ(1, incr);
    ASSERT_EQ(1, decr);
    ASSERT_EQ(1, cover);
    fixture_end(&f);
}

TEST(engine_addons, create_decal_cuts_the_faces_in_the_frustum)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Ground", 5);
    const float origin[3] = {0, 0, 0}, high[3] = {2, 5, 2};
    const float down[3] = {0, -1, 0}, ahead[3] = {0, 0, 1};
    nmo_object_id_t ground = placed_entity(&f, NMO_CID_3DOBJECT, "Ground", X_AXIS, Y_AXIS, Z_AXIS, origin, mesh);
    nmo_object_id_t camera = a_camera(&f, X_AXIS, ahead, down, high, 0.4f);
    ASSERT_TRUE(mesh && ground && camera);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t decal = fixture_block(&f, GUID_CREATE_DECAL, "Decal");
    ASSERT_TRUE(decal != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, decal, 0), 0);
    fixture_input_int(&f, fixture_target(&f, decal), (int32_t)ground);
    fixture_input_int(&f, BLOCK_PIN(&f, decal, 0), (int32_t)camera);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_object_id_t made = 0;
    read_value(&f, BLOCK_POUT(&f, decal, 0), &made, sizeof(made));
    ASSERT_TRUE(made != 0);
    ASSERT_STR_EQ("Ground_Decal", nmo_engine_object_name(f.engine, made));
    nmo_object_id_t decal_mesh = nmo_engine_object_named(f.engine, "Ground_DecalMesh", NMO_CID_MESH);
    nmo_engine_mesh_t out;
    ASSERT_TRUE(decal_mesh != 0 && nmo_engine_mesh(f.engine, decal_mesh, &out));
    /* 5 x tan 0.2 = 1.01 around (2, 2): a cut square, welded, 0.01 above the ground */
    ASSERT_TRUE(out.vertex_count >= 4 && out.face_count >= 2);
    for (int32_t v = 0; v < out.vertex_count; v++) {
        ASSERT_FLOAT_EQ(0.01f, out.vertices[v].position.y, 1e-5f);
        ASSERT_TRUE(fabsf(out.vertices[v].position.x - 2.0f) <= 1.02f);
        ASSERT_TRUE(fabsf(out.vertices[v].position.z - 2.0f) <= 1.02f);
    }
    fixture_end(&f);
}

TEST(engine_addons, animation_recorder_keys_the_object_each_frame)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    const float origin[3] = {0, 0, 0};
    nmo_object_id_t box = placed_entity(&f, NMO_CID_3DENTITY, "Box", X_AXIS, Y_AXIS, Z_AXIS, origin, 0);
    ASSERT_TRUE(box != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t recorder = fixture_block(&f, GUID_ANIMATION_RECORDER, "Recorder");
    ASSERT_TRUE(recorder != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, recorder, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, recorder, 1), 3);
    fixture_input_int(&f, BLOCK_PIN(&f, recorder, 0), (int32_t)box);
    input_text(&f, BLOCK_PIN(&f, recorder, 1), "Take");
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    for (int i = 0; i < 4; i++) {
        float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)i, 0, 0, 1};
        ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, box, world, false));
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    /* keys at frames 0, 1 and 2 (x 0, 1, 2), then Off: a length of 3 on the box */
    nmo_object_id_t take = 0;
    read_value(&f, BLOCK_POUT(&f, recorder, 0), &take, sizeof(take));
    ASSERT_TRUE(take != 0);
    ASSERT_EQ((int64_t)take, (int64_t)nmo_engine_object_named(f.engine, "Take", NMO_CID_OBJECTANIMATION));
    const nmo_objectanimation_state_t *state = (const nmo_objectanimation_state_t *)nmo_object_get_state(eng_find_object(f.engine, take));
    ASSERT_EQ(3u, state->controller_count);
    ASSERT_EQ(3u, state->controllers[0].key_count);
    const float *keys = (const float *)state->controllers[0].data;
    ASSERT_FLOAT_EQ(2.0f, keys[0 + 4 * 2], 0.0f);
    ASSERT_FLOAT_EQ(2.0f, keys[1 + 4 * 2], 0.0f);
    ASSERT_FLOAT_EQ(3.0f, state->length, 0.0f);
    ASSERT_EQ((int64_t)box, (int64_t)eng_object_animation_entity(f.engine, take));
    fixture_end(&f);
}

/* A 4 x 4 ground under a point light at (2, 3, 2), a 1 x 1 plate at height 1.5 over its corner:
   the block's inputs set on `block` (the receiving, occluding and light groups) */
typedef struct lightmap_scene {
    nmo_object_id_t ground, plate, light, camera;
    nmo_object_id_t receivers, occluders, lights;
} lightmap_scene_t;

static void lightmap_scene(engine_fixture_t *f, lightmap_scene_t *s)
{
    nmo_object_id_t ground_mesh = grid_mesh(f, "Ground", 5), plate_mesh = grid_mesh(f, "Plate", 2);
    const float origin[3] = {0, 0, 0}, plate_at[3] = {0.4f, 1.5f, 0.4f}, lamp[3] = {2, 3, 2}, eye[3] = {2, 3, -6};
    s->ground = placed_entity(f, NMO_CID_3DOBJECT, "Ground", X_AXIS, Y_AXIS, Z_AXIS, origin, ground_mesh);
    s->plate = placed_entity(f, NMO_CID_3DOBJECT, "Plate", X_AXIS, Y_AXIS, Z_AXIS, plate_at, plate_mesh);
    s->light = placed_entity(f, NMO_CID_LIGHT, "Lamp", X_AXIS, Y_AXIS, Z_AXIS, lamp, 0);
    s->camera = a_camera(f, X_AXIS, Y_AXIS, Z_AXIS, eye, 1.0f);
    nmo_light_state_t *light = s->light != 0 ? (nmo_light_state_t *)fixture_state(f, s->light) : NULL;
    ASSERT_TRUE(s->ground && s->plate && light && s->camera);
    light->light_data.type = VX_LIGHTPOINT;
    light->light_data.diffuse = (nmo_color_t){1.0f, 1.0f, 1.0f, 1.0f};
    light->light_data.range = 10.0f;
    light->light_data.attenuation0 = 1.0f;
    light->light_power = 1.0f;
    s->receivers = a_group(f, "Receivers", &s->ground, 1);
    s->occluders = a_group(f, "Occluders", &s->plate, 1);
    s->lights = a_group(f, "Lights", &s->light, 1);
}

static void lightmap_inputs(engine_fixture_t *f, const lightmap_scene_t *s, nmo_object_id_t block)
{
    const float resolution = 1.0f, threshold = 0.17453292f;
    fixture_input_int(f, BLOCK_PIN(f, block, 0), (int32_t)s->receivers);
    fixture_input_int(f, BLOCK_PIN(f, block, 1), (int32_t)s->occluders);
    fixture_input_int(f, BLOCK_PIN(f, block, 2), (int32_t)s->lights);
    fixture_input_value(f, BLOCK_PIN(f, block, 3), &resolution, sizeof(resolution));
    fixture_input_int(f, BLOCK_PIN(f, block, 4), 1);
    fixture_input_int(f, BLOCK_PIN(f, block, 5), 0);
    fixture_input_value(f, BLOCK_PIN(f, block, 6), &threshold, sizeof(threshold));
}

/* The ground's one polygon (its 32 coplanar faces) lit in an 8 x 8 texture: 8 = 2 x 4 units x a
   texel a unit. Texel (1, 1) samples (1/3, 1/3) on the ground, whose way to the lamp the plate
   cuts; texel (2, 1) samples (1, 1/3), lit by cos 0.838 (2.99 up of 3.566). */
static void assert_ground_lightmap(engine_fixture_t *f)
{
    nmo_object_id_t texture = nmo_engine_object_named(f->engine, "Ground - face 0", NMO_CID_TEXTURE);
    ASSERT_TRUE(texture != 0);
    eng_texture_t *t = eng_texture(f->engine, texture);
    eng_surface_t *surface = t != NULL ? eng_texture_surface(f->engine, t, -1) : NULL;
    ASSERT_TRUE(surface != NULL && surface->rgba != NULL);
    ASSERT_EQ(8, surface->width);
    ASSERT_EQ(8, surface->height);
    ASSERT_EQ(0, channel(surface, 1, 1, 0));
    ASSERT_EQ(255, channel(surface, 1, 1, 3));
    ASSERT_EQ(213, channel(surface, 2, 1, 0));
    ASSERT_EQ(213, channel(surface, 2, 1, 2));
}

TEST(engine_addons, lightmap_lights_the_ground_and_draws_over_it)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    lightmap_scene_t s;
    lightmap_scene(&f, &s);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t lightmap = fixture_block(&f, GUID_LIGHTMAP, "Lightmap");
    ASSERT_TRUE(lightmap != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, lightmap, 2), 0);
    lightmap_inputs(&f, &s, lightmap);
    const float opacity[4] = {1.0f, 1.0f, 1.0f, 0.5f};
    fixture_input_value(&f, BLOCK_PIN(&f, lightmap, 7), opacity, sizeof(opacity));
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = s.camera;
    eng_show(f.engine, s.ground, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    assert_ground_lightmap(&f);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* the ground's post render callback: its faces multiplied by the texture times the opacity */
    nmo_object_id_t texture = nmo_engine_object_named(f.engine, "Ground - face 0", NMO_CID_TEXTURE);
    int found = 0;
    for (int32_t d = 0; d < frame.draw_count; d++) {
        const nmo_engine_draw_t *draw = &frame.draws[d];
        if (draw->kind != NMO_ENGINE_DRAW_PRIMITIVE || draw->object != s.ground) {
            continue;
        }
        found++;
        ASSERT_EQ((int64_t)texture, (int64_t)draw->texture);
        ASSERT_EQ(96, draw->vertex_count);
        ASSERT_TRUE(draw->blend && draw->no_z_write);
        ASSERT_EQ(VXBLEND_ZERO, draw->source_blend);
        ASSERT_EQ(VXBLEND_SRCCOLOR, draw->dest_blend);
        ASSERT_FLOAT_EQ(1.0001f, draw->projection_z_scale, 0.0f);
        ASSERT_EQ(0x7FFFFFFFu, frame.vertices[draw->first_vertex].color);
    }
    ASSERT_EQ(1, found);
    fixture_end(&f);
}

TEST(engine_addons, static_lightmap_makes_a_lightmapped_copy)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    lightmap_scene_t s;
    lightmap_scene(&f, &s);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t lightmap = fixture_block(&f, GUID_STATIC_LIGHTMAP, "Static");
    ASSERT_TRUE(lightmap != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, lightmap, 0), 0);
    lightmap_inputs(&f, &s, lightmap);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    assert_ground_lightmap(&f);
    /* "LM Ground": the polygon's 25 vertices and 32 faces, the lightmap a channel */
    nmo_object_id_t copy = nmo_engine_object_named(f.engine, "LM Ground", NMO_CID_3DOBJECT);
    nmo_object_id_t mesh = nmo_engine_object_named(f.engine, "LM Ground Mesh", NMO_CID_MESH);
    nmo_object_id_t material = nmo_engine_object_named(f.engine, "LM Ground Material", NMO_CID_MATERIAL);
    ASSERT_TRUE(copy != 0 && mesh != 0 && material != 0);
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    ASSERT_EQ(25, out.vertex_count);
    ASSERT_EQ(32, out.face_count);
    float sum_x = 0.0f, sum_z = 0.0f;
    for (int32_t v = 0; v < out.vertex_count; v++) {
        sum_x += out.vertices[v].position.x;
        sum_z += out.vertices[v].position.z;
    }
    ASSERT_FLOAT_EQ(50.0f, sum_x, 0.0f); /* the ground's 5 x 5 vertices */
    ASSERT_FLOAT_EQ(50.0f, sum_z, 0.0f);
    ASSERT_EQ(1, out.channel_count);
    ASSERT_EQ((int64_t)material, (int64_t)out.channels[0].material);
    ASSERT_EQ(VXBLEND_ZERO, out.channels[0].source_blend);
    ASSERT_EQ(VXBLEND_SRCCOLOR, out.channels[0].dest_blend);
    ASSERT_FALSE(out.channels[0].lit);
    ASSERT_TRUE(out.channels[0].uvs != NULL);
    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_EQ((int64_t)nmo_engine_object_named(f.engine, "Ground - face 0", NMO_CID_TEXTURE), (int64_t)desc.texture);
    fixture_end(&f);
}

/* The lightmaps a file saved in the block's state chunk local (LightmapGenerator::Save): the
   ground's one triangle in a texture of its own, drawn while On */
TEST(engine_addons, lightmap_load_draws_the_saved_lightmaps)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Ground", 2);
    const float origin[3] = {0, 0, 0}, eye[3] = {0.5f, 3, -6};
    nmo_object_id_t ground = placed_entity(&f, NMO_CID_3DOBJECT, "Ground", X_AXIS, Y_AXIS, Z_AXIS, origin, mesh);
    nmo_object_id_t camera = a_camera(&f, X_AXIS, Y_AXIS, Z_AXIS, eye, 1.0f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Saved Map");
    ASSERT_TRUE(ground && camera && texture);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t lightmap = fixture_block(&f, GUID_LIGHTMAP, "Lightmap");
    ASSERT_TRUE(lightmap != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, lightmap, 0), 0);
    nmo_parameter_state_t *local = (nmo_parameter_state_t *)fixture_state(&f, local_of(&f, lightmap, 1));
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(f.session));
    ASSERT_TRUE(local != NULL && chunk != NULL);
    const uint16_t indices[3] = {0, 1, 2};
    const struct { float x, y, z; uint32_t diffuse, specular; float u, v; uint32_t index; } vertices[3] = {
        {0, 0, 0, 0xFFFFFFFFu, 0, 0.25f, 0.75f, 0}, {0, 0, 1, 0xFFFFFFFFu, 0, 0.25f, 0.25f, 2},
        {1, 0, 0, 0xFFFFFFFFu, 0, 0.75f, 0.75f, 1}};
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 4510204u);
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_object_id(chunk, ground);
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_object_id(chunk, texture);
    nmo_chunk_write_int(chunk, 3);
    nmo_chunk_write_buffer_no_size(chunk, indices, sizeof(indices));
    nmo_chunk_write_int(chunk, 3);
    nmo_chunk_write_buffer_no_size(chunk, vertices, sizeof(vertices));
    nmo_chunk_close(chunk);
    nmo_array_dispose(&local->buffer_data);
    local->mode = CKPARAM_MODE_SUBCHUNK;
    local->subchunk = chunk;
    local->has_state = true;
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    eng_show(f.engine, ground, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int found = 0;
    for (int32_t d = 0; d < frame.draw_count; d++) {
        const nmo_engine_draw_t *draw = &frame.draws[d];
        if (draw->kind != NMO_ENGINE_DRAW_PRIMITIVE || draw->object != ground) {
            continue;
        }
        found++;
        ASSERT_EQ((int64_t)texture, (int64_t)draw->texture);
        ASSERT_EQ(3, draw->vertex_count);
        const nmo_engine_vertex_t *v = &frame.vertices[draw->first_vertex];
        ASSERT_FLOAT_EQ(1.0f, v[1].z, 0.0f);
        ASSERT_FLOAT_EQ(0.25f, v[1].u, 0.0f);
        ASSERT_FLOAT_EQ(0.25f, v[1].v, 0.0f);
        ASSERT_EQ(0xFFFFFFFFu, v[0].color);
    }
    ASSERT_EQ(1, found);
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
REGISTER_TEST(engine_addons, volumetric_fog_writes_the_fog_channel_v);
REGISTER_TEST(engine_addons, lens_flare_fades_its_flares_in);
REGISTER_TEST(engine_addons, shadow_stencil_draws_volumes_and_the_shadow);
REGISTER_TEST(engine_addons, create_decal_cuts_the_faces_in_the_frustum);
REGISTER_TEST(engine_addons, animation_recorder_keys_the_object_each_frame);
REGISTER_TEST(engine_addons, lightmap_lights_the_ground_and_draws_over_it);
REGISTER_TEST(engine_addons, static_lightmap_makes_a_lightmapped_copy);
REGISTER_TEST(engine_addons, lightmap_load_draws_the_saved_lightmaps);
TEST_MAIN_END()
