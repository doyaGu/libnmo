#include "engine_fixture.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "../../src/engine/engine_internal.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_CREATE_SYSTEM_FONT NMO_GUID(0x936334fc, 0xf243684f)
#define GUID_DELETE_FONT NMO_GUID(0x0153342c, 0xe543674f)
#define GUID_SET_FONT_PROPERTIES NMO_GUID(0xdacfbd61, 0x7a6e65e7)
#define GUID_TEXT_2D NMO_GUID(0x055b29fe, 0x662d5ca0)
#define GUID_PUSH_BUTTON NMO_GUID(0x761b7e64, 0x2b4c6911)
#define GUID_TT_PUSH_BUTTON2 NMO_GUID(0x14d325d1, 0x6748654e)
#define GUID_SHOW_MOUSE_CURSOR NMO_GUID(0x16f6368f, 0x506b60fc)
#define GUID_TEXT_DISPLAY NMO_GUID(0xf22d010a, 0xf2cd010a)
#define GUID_IDENTITY NMO_GUID(0x15151652, 0xaeefffd5)
#define OP_GET_TEXT NMO_GUID(0x764e2b8b, 0x6b430baf)
#define GUID_ALIGN_2D NMO_GUID(0x03385bc3, 0x1034102f)
#define GUID_ALIGN_3D NMO_GUID(0x0f384bc3, 0x1b34102f)
#define GUID_FLOW_LAYOUT_2D NMO_GUID(0x295f342c, 0x95e467e6)
#define GUID_GRID_LAYOUT_2D NMO_GUID(0x265f342c, 0x95e466e6)
#define GUID_GET_PROPORTIONAL_SCREEN_POS NMO_GUID(0x671e53a2, 0x5ba16d62)
#define GUID_GET_SCREEN_PROPORTIONAL_POS NMO_GUID(0x7bdf0404, 0x0f765834)
#define GUID_SET_PICKABLE NMO_GUID(0xf1499052, 0xebe9bbf1)
#define GUID_MOUSE_CURSOR_SYSTEM NMO_GUID(0x2483576d, 0x57c66324)
#define GUID_SET_CARET_POSITION NMO_GUID(0x0f760d6e, 0x7ce7589a)
#define GUID_DRAW_RECTANGLE NMO_GUID(0x4095766f, 0x291161c1)
#define GUID_MOUSE_CURSOR NMO_GUID(0x140c3eac, 0x6e502a81)
#define GUID_DRAG_AND_DROP NMO_GUID(0x39c45869, 0x3dcf3fc1)
#define GUID_BITMAP_TEXT_DISPLAY NMO_GUID(0x049d487b, 0x57565345)
#define GUID_RESIZE_VIEW NMO_GUID(0x040e1fe8, 0x79ed7c9b)
#define GUID_ADDITIONAL_VIEW NMO_GUID(0x00cd159b, 0x164d010b)

typedef struct effect_log {
    size_t effects;
    nmo_object_id_t target;
} effect_log_t;

static void on_event(const nmo_engine_event_t *event, void *user_data)
{
    effect_log_t *log = (effect_log_t *)user_data;
    if (event->kind == NMO_ENGINE_EVENT_EFFECT) {
        log->effects++;
        log->target = event->object_id;
    }
}

/* A Create System Font making font `name`, run after `previous` (an output) */
static nmo_object_id_t system_font(engine_fixture_t *f, const char *name, nmo_object_id_t previous)
{
    nmo_object_id_t block = fixture_block(f, GUID_CREATE_SYSTEM_FONT, name);
    if (block == 0) {
        return 0;
    }
    fixture_link(f, previous, BLOCK_IN(f, block, 0), 0);
    fixture_input_value(f, BLOCK_PIN(f, block, 0), name, strlen(name) + 1);
    return block;
}

static int32_t output_int(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    int32_t value = 0;
    read_value(f, BLOCK_POUT(f, block, pos), &value, sizeof(value));
    return value;
}

/* The `pos`th local parameter (or setting) of a block */
static nmo_object_id_t block_local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

TEST(engine_interface, fonts_are_numbered_by_name)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = system_font(&f, "A", f.script_in);
    ASSERT_TRUE(a != 0);
    nmo_object_id_t b = system_font(&f, "B", BLOCK_OUT(&f, a, 0));
    nmo_object_id_t again = system_font(&f, "A", BLOCK_OUT(&f, b, 0));
    nmo_object_id_t properties = fixture_block(&f, GUID_SET_FONT_PROPERTIES, "Properties");
    nmo_object_id_t remove = fixture_block(&f, GUID_DELETE_FONT, "Delete");
    ASSERT_TRUE(b != 0 && again != 0 && properties != 0 && remove != 0);
    fixture_link(&f, BLOCK_OUT(&f, again, 0), BLOCK_IN(&f, properties, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, properties, 0), BLOCK_IN(&f, remove, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, properties, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 0), 1);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* A font is numbered from 1; making a font of the same name replaces it there */
    ASSERT_EQ(1, output_int(&f, a, 0));
    ASSERT_EQ(2, output_int(&f, b, 0));
    ASSERT_EQ(1, output_int(&f, again, 0));
    ASSERT_EQ(1u, runs(&f, properties));
    /* Deleting font 1 moves B down */
    ASSERT_EQ(1, nmo_engine_font_count(f.engine));
    ASSERT_STR_EQ("B", nmo_engine_font_name(f.engine, 1));
    ASSERT_NULL(nmo_engine_font_name(f.engine, 2));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    fixture_end(&f);
}

TEST(engine_interface, text_2d_draws_until_off)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t frame = fixture_create(&f, NMO_CID_2DENTITY, "Frame");
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t text = fixture_block(&f, GUID_TEXT_2D, "Text");
    nmo_object_id_t once = fixture_block(&f, GUID_TEXT_2D, "Once");
    nmo_object_id_t exit_on = fixture_block(&f, GUID_NOP, "Exit On");
    nmo_object_id_t exit_off = fixture_block(&f, GUID_NOP, "Exit Off");
    ASSERT_TRUE(text != 0 && once != 0 && fixture_target(&f, text) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, text, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, text, 1), 3);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, once, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, text, 0), BLOCK_IN(&f, exit_on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, text, 1), BLOCK_IN(&f, exit_off, 0), 0);
    fixture_input_int(&f, fixture_target(&f, text), (int32_t)frame);
    nmo_object_id_t once_properties = block_local(&f, once, 0);
    ASSERT_TRUE(once_properties != 0);
    fixture_commit(&f, NULL);
    /* "Multiple": draw once each time On is activated */
    int32_t multiple = 256;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, once_properties, &multiple,
                                                 sizeof(multiple)));
    effect_log_t log = {0};
    nmo_engine_set_trace(f.engine, on_event, &log);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, exit_on));
    ASSERT_EQ(2u, log.effects);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, once));
    for (int frame_index = 1; frame_index < 3; frame_index++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, text));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, exit_off));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, text));
    ASSERT_EQ(2u, log.effects);
    fixture_end(&f);
}

/* The frame's sprite text draw, or NULL */
static const nmo_engine_draw_t *sprite_text_draw(const nmo_engine_frame_t *frame)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_SPRITE_TEXT) {
            return &frame->draws[i];
        }
    }
    return NULL;
}

TEST(engine_interface, text_display_shows_its_text_until_off)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t display = fixture_block(&f, GUID_TEXT_DISPLAY, "Text Display");
    nmo_object_id_t exit_off = fixture_block(&f, GUID_NOP, "Exit Off");
    ASSERT_TRUE(display != 0 && exit_off != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, display, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, display, 1), 2);
    fixture_link(&f, BLOCK_OUT(&f, display, 1), BLOCK_IN(&f, exit_off, 0), 0);
    const float offset[2] = {10.0f, 20.0f};
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, display, 0), offset, sizeof(offset));
    fixture_input_value(&f, BLOCK_PIN(&f, display, 1), red, sizeof(red));
    /* Left as CK reads the "Left" default (the edit layer's Sprite Text Alignment names differ) */
    fixture_input_int(&f, BLOCK_PIN(&f, display, 2), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, display, 3), 16);
    fixture_input_value(&f, BLOCK_PIN(&f, display, 4), "Hello", 6);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* At Offset, the Sprite Size setting's 320 x 25, the text with a space after it, red
       reaching GDI as blue, Arial (no font info) at Size points, Left */
    const nmo_engine_draw_t *draw = sprite_text_draw(&frame);
    ASSERT_NOT_NULL(draw);
    ASSERT_FLOAT_EQ(10.0f, draw->rect[0], 0.0f);
    ASSERT_FLOAT_EQ(20.0f, draw->rect[1], 0.0f);
    ASSERT_FLOAT_EQ(330.0f, draw->rect[2], 0.0f);
    ASSERT_FLOAT_EQ(45.0f, draw->rect[3], 0.0f);
    ASSERT_STR_EQ("Hello ", draw->text);
    ASSERT_EQ(0xFF0000FFu, draw->color);
    ASSERT_STR_EQ("Arial", draw->font_name);
    ASSERT_EQ(16, draw->font_size);
    ASSERT_EQ(2u, draw->text_align);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, display));

    /* Off two frames on: hidden, Exit Off, the block done */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, exit_off));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, display));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_NULL(sprite_text_draw(&frame));
    fixture_end(&f);
}

TEST(engine_interface, text_display_keeps_its_text_in_a_sprite_text)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t display = fixture_block(&f, GUID_TEXT_DISPLAY, "Text Display");
    nmo_object_id_t identity = fixture_block(&f, GUID_IDENTITY, "Identity");
    ASSERT_TRUE(display != 0 && identity != 0);
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, display);
    ASSERT_TRUE(state != NULL && state->local_parameters.count > 0);
    const nmo_object_id_t sprite_local =
        nmo_ref_runtime_id(&((const nmo_behavior_ref_t *)state->local_parameters.data)[0].ref);
    /* Get Text reads the CKSpriteText the block keeps in its first local */
    nmo_object_id_t op = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_operation(f.tx, f.script_id, OP_GET_TEXT, sprite_local, 0, 0, &op));
    const nmo_parameteroperation_state_t *op_state =
        (const nmo_parameteroperation_state_t *)fixture_state(&f, op);
    nmo_object_id_t text = op_state != NULL ? nmo_parameteroperation_out_id(op_state) : 0;
    nmo_parameter_state_t *text_state = (nmo_parameter_state_t *)fixture_state(&f, text);
    nmo_parameterin_state_t *op_in =
        (nmo_parameterin_state_t *)fixture_state(&f, nmo_parameteroperation_in1_id(op_state));
    nmo_parameterin_state_t *pin = (nmo_parameterin_state_t *)fixture_state(&f, BLOCK_PIN(&f, identity, 0));
    nmo_parameter_state_t *pout = (nmo_parameter_state_t *)fixture_state(&f, BLOCK_POUT(&f, identity, 0));
    ASSERT_TRUE(text_state != NULL && op_in != NULL && pin != NULL && pout != NULL);
    text_state->type_guid = CKPGUID_STRING;
    op_in->type_guid = CKPGUID_SPRITETEXT;
    pin->type_guid = CKPGUID_STRING;
    pout->type_guid = CKPGUID_STRING;
    ASSERT_EQ(NMO_OK, nmo_script_edit_connect_parameter(f.tx, text, BLOCK_PIN(&f, identity, 0)));
    fixture_link(&f, f.script_in, BLOCK_IN(&f, display, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, display, 0), BLOCK_IN(&f, identity, 0), 0);
    fixture_input_value(&f, BLOCK_PIN(&f, display, 4), "Hello", 6);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));

    /* Made at load: a sprite text of its own */
    uint32_t sprite = 0;
    read_value(&f, sprite_local, &sprite, sizeof(sprite));
    ASSERT_TRUE(sprite != 0);
    ASSERT_EQ(NMO_CID_SPRITETEXT, nmo_engine_object_class(f.engine, sprite));
    ASSERT_STR_EQ("TextDisplay Sprite", nmo_engine_object_name(f.engine, sprite));
    char got[16] = {0};
    read_value(&f, BLOCK_POUT(&f, identity, 0), got, 7);
    ASSERT_STR_EQ("Hello ", got);
    fixture_end(&f);
}

TEST(engine_interface, push_button_follows_the_mouse)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* PushButton has no target parameter: its frame is the script's owner */
    nmo_object_id_t frame = f.owner_id;
    nmo_object_id_t button = fixture_block(&f, GUID_PUSH_BUTTON, "Button");
    ASSERT_TRUE(button != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, button, 0), 0);
    /* Pressed, Released, Active, Enter Button, Exit Button, In Button */
    nmo_object_id_t outputs[6];
    for (int i = 0; i < 6; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, button, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_set_entity_under_mouse(f.engine, frame);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* enter */
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* press */
    nmo_engine_set_mouse_button(f.engine, 0, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* release over the button */
    nmo_engine_set_entity_under_mouse(f.engine, 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* leave */

    const uint64_t expected[6] = {1u, 1u, 1u, 1u, 1u, 3u};
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(expected[i], runs(&f, outputs[i]));
    }
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, button));
    fixture_end(&f);
}

TEST(engine_interface, tt_push_button2_needs_the_cursor_shown)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t frame = fixture_create(&f, NMO_CID_2DENTITY, "Frame");
    ASSERT_TRUE(frame != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t button = fixture_block(&f, GUID_TT_PUSH_BUTTON2, "Button");
    nmo_object_id_t hide = fixture_block(&f, GUID_SHOW_MOUSE_CURSOR, "Hide");
    ASSERT_TRUE(button != 0 && hide != 0 && fixture_target(&f, button) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, button, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, hide, 0), 4);
    fixture_input_int(&f, fixture_target(&f, button), (int32_t)frame);
    fixture_input_int(&f, BLOCK_PIN(&f, hide, 0), 0); /* Show: no */
    /* Released, Roll Over, Mouse Down */
    nmo_object_id_t outputs[3];
    for (int i = 0; i < 3; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, button, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(nmo_engine_cursor_is_visible(f.engine));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_set_entity_under_mouse(f.engine, frame);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* roll over */
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* mouse down */
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    nmo_engine_set_mouse_button(f.engine, 0, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(0u, runs(&f, outputs[0]));
    /* The cursor hidden in frame 4: the mouse is over nothing the button sees */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FALSE(nmo_engine_cursor_is_visible(f.engine));
    ASSERT_EQ(1u, runs(&f, outputs[0]));
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    fixture_end(&f);
}

static nmo_object_id_t started(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block != 0) {
        fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    }
    return block;
}

static void set_rect_of(engine_fixture_t *f, nmo_object_id_t entity, float l, float t, float r, float b)
{
    eng_2d_set_rect(f->engine, entity, (nmo_rect_t){l, t, r, b}, true);
}

static nmo_rect_t rect_of(engine_fixture_t *f, nmo_object_id_t entity)
{
    const eng_2d_t *e = eng_2d_entity(f->engine, entity);
    return e != NULL ? e->rect : (nmo_rect_t){-1.0f, -1.0f, -1.0f, -1.0f};
}

static nmo_object_id_t placed(engine_fixture_t *f, const char *name, float x, float y, float z, float scale)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_3DENTITY, name);
    nmo_3dentity_state_t *state = id != 0 ? (nmo_3dentity_state_t *)fixture_state(f, id) : NULL;
    if (state == NULL) {
        return 0;
    }
    memset(state->world_matrix, 0, sizeof(state->world_matrix));
    state->world_matrix[0] = state->world_matrix[5] = state->world_matrix[10] = scale;
    state->world_matrix[15] = 1.0f;
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    return id;
}

TEST(engine_interface, layouts_place_2d_and_3d_entities)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t frame = fixture_create(&f, NMO_CID_2DENTITY, "Frame");
    nmo_object_id_t panel = fixture_create(&f, NMO_CID_2DENTITY, "Panel");
    nmo_object_id_t cells[4], items[2];
    for (int i = 0; i < 4; i++) {
        cells[i] = fixture_create(&f, NMO_CID_2DENTITY, "Cell");
    }
    for (int i = 0; i < 2; i++) {
        items[i] = fixture_create(&f, NMO_CID_2DENTITY, "Item");
    }
    nmo_object_id_t grid = fixture_create(&f, NMO_CID_DATAARRAY, "Cells");
    nmo_object_id_t row = fixture_create(&f, NMO_CID_GROUP, "Row");
    nmo_object_id_t box = placed(&f, "Box", 0.0f, 0.0f, 0.0f, 2.0f);
    nmo_object_id_t lid = placed(&f, "Lid", 5.0f, 5.0f, 5.0f, 1.0f);
    ASSERT_TRUE(frame && panel && grid && row && box && lid);
    nmo_group_state_t *group = (nmo_group_state_t *)fixture_state(&f, row);
    nmo_ref_t *members = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&group->object_ids, 2, (void **)&members));
    for (int i = 0; i < 2; i++) {
        members[i] = nmo_ref_from_id(items[i]);
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t align = started(&f, GUID_ALIGN_2D, "Align");
    nmo_object_id_t layout = started(&f, GUID_GRID_LAYOUT_2D, "Grid");
    nmo_object_id_t flow = started(&f, GUID_FLOW_LAYOUT_2D, "Flow");
    nmo_object_id_t align3d = started(&f, GUID_ALIGN_3D, "Align 3D");
    ASSERT_TRUE(align && layout && flow && align3d);
    /* Align: right and bottom of the 640 x 480 render context, 5 pixels in */
    fixture_input_int(&f, fixture_target(&f, align), (int32_t)panel);
    fixture_input_int(&f, BLOCK_PIN(&f, align, 1), 2 | 8);
    fixture_input_int(&f, BLOCK_PIN(&f, align, 2), 0);
    const nmo_rect_t five = {5.0f, 5.0f, 5.0f, 5.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, align, 3), &five, sizeof(five));
    /* Grid: 2 x 2 cells resized in the frame, 10 pixel margins and gaps */
    fixture_input_int(&f, fixture_target(&f, layout), (int32_t)frame);
    fixture_input_int(&f, BLOCK_PIN(&f, layout, 0), (int32_t)grid);
    const nmo_rect_t ten = {10.0f, 10.0f, 10.0f, 10.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, layout, 1), &ten, sizeof(ten));
    fixture_input_int(&f, BLOCK_PIN(&f, layout, 2), 10);
    fixture_input_int(&f, BLOCK_PIN(&f, layout, 3), 10);
    fixture_input_int(&f, BLOCK_PIN(&f, layout, 4), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, layout, 5), 0);
    /* Flow: a row from the left of the frame */
    fixture_input_int(&f, fixture_target(&f, flow), (int32_t)frame);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 0), (int32_t)row);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 1), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 2), 10);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 3), 10);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 4), 5);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 5), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, flow, 6), 0);
    /* Align 3D: against the box's left, top and front, as its half sizes say */
    fixture_input_int(&f, fixture_target(&f, align3d), (int32_t)lid);
    fixture_input_int(&f, BLOCK_PIN(&f, align3d, 0), (int32_t)box);
    fixture_input_int(&f, BLOCK_PIN(&f, align3d, 1), 1 | 4);
    fixture_input_int(&f, BLOCK_PIN(&f, align3d, 2), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, align3d, 3), 0);
    const nmo_rect_t none = {0.0f, 0.0f, 0.0f, 0.0f};
    const float depth[2] = {0.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, align3d, 4), &none, sizeof(none));
    fixture_input_value(&f, BLOCK_PIN(&f, align3d, 5), depth, sizeof(depth));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    set_rect_of(&f, panel, 10.0f, 10.0f, 60.0f, 30.0f);
    set_rect_of(&f, frame, 0.0f, 0.0f, 230.0f, 120.0f);
    for (int i = 0; i < 2; i++) {
        set_rect_of(&f, items[i], 0.0f, 0.0f, 50.0f, 20.0f);
    }
    for (int c = 0; c < 2; c++) {
        ASSERT_EQ(NMO_OK, nmo_engine_array_insert_column(f.engine, grid, -1, CKARRAYTYPE_OBJECT, NMO_GUID(0u, 0u)));
    }
    for (int r = 0; r < 2; r++) {
        ASSERT_EQ(NMO_OK, nmo_engine_array_insert_row(f.engine, grid, -1, NULL));
        for (int c = 0; c < 2; c++) {
            nmo_engine_cell_t cell = {.type = CKARRAYTYPE_OBJECT, .value = (uint32_t)cells[r * 2 + c]};
            ASSERT_EQ(NMO_OK, nmo_engine_array_set_cell(f.engine, grid, r, c, &cell));
        }
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_rect_t r = rect_of(&f, panel);
    ASSERT_FLOAT_EQ(585.0f, r.left, 1e-4f); /* 640 - 5 - 50 */
    ASSERT_FLOAT_EQ(455.0f, r.top, 1e-4f);  /* 480 - 5 - 20 */
    ASSERT_FLOAT_EQ(635.0f, r.right, 1e-4f);
    /* Cells of (230 - 30) / 2 by (120 - 30) / 2 */
    r = rect_of(&f, cells[3]);
    ASSERT_FLOAT_EQ(120.0f, r.left, 1e-4f);
    ASSERT_FLOAT_EQ(65.0f, r.top, 1e-4f);
    ASSERT_FLOAT_EQ(220.0f, r.right, 1e-4f);
    ASSERT_FLOAT_EQ(110.0f, r.bottom, 1e-4f);
    ASSERT_FLOAT_EQ(10.0f, rect_of(&f, cells[0]).left, 1e-4f);
    /* The row: 10 in, then 5 after the first's 50 */
    ASSERT_FLOAT_EQ(10.0f, rect_of(&f, items[0]).left, 1e-4f);
    ASSERT_FLOAT_EQ(65.0f, rect_of(&f, items[1]).left, 1e-4f);
    ASSERT_FLOAT_EQ(10.0f, rect_of(&f, items[1]).top, 1e-4f);
    /* The lid's centre one of its half sizes inside the box's left, top and front faces */
    eng_vector_t at = eng_entity_position(f.engine, eng_entity_index(f.engine, lid), -1);
    ASSERT_FLOAT_EQ(-1.0f, at.x, 1e-5f);
    ASSERT_FLOAT_EQ(1.0f, at.y, 1e-5f);
    ASSERT_FLOAT_EQ(-1.0f, at.z, 1e-5f);
    fixture_end(&f);
}

/* The frame's quads in a color */
static int32_t quads_of_color(const nmo_engine_frame_t *frame, uint32_t color)
{
    int32_t count = 0;
    for (int32_t i = 0; i < frame->draw_count; i++) {
        count += frame->draws[i].kind == NMO_ENGINE_DRAW_QUAD && frame->draws[i].color == color ? 1 : 0;
    }
    return count;
}

TEST(engine_interface, screen_blocks_rectangles_and_cursor)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t thing = placed(&f, "Thing", 0.0f, 0.0f, 0.0f, 1.0f);
    ASSERT_TRUE(thing != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t to_ratio = started(&f, GUID_GET_PROPORTIONAL_SCREEN_POS, "Ratio");
    nmo_object_id_t to_pixel = started(&f, GUID_GET_SCREEN_PROPORTIONAL_POS, "Pixel");
    nmo_object_id_t pickable = started(&f, GUID_SET_PICKABLE, "Pickable");
    nmo_object_id_t system = started(&f, GUID_MOUSE_CURSOR_SYSTEM, "System");
    nmo_object_id_t caret = started(&f, GUID_SET_CARET_POSITION, "Caret");
    nmo_object_id_t rectangle = started(&f, GUID_DRAW_RECTANGLE, "Rectangle");
    nmo_object_id_t cursor = started(&f, GUID_MOUSE_CURSOR, "Cursor");
    ASSERT_TRUE(to_ratio && to_pixel && pickable && system && caret && rectangle && cursor);
    fixture_input_int(&f, BLOCK_PIN(&f, to_ratio, 0), 320);
    fixture_input_int(&f, BLOCK_PIN(&f, to_ratio, 1), 120);
    const float quarter = 0.25f, half = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, to_pixel, 0), &quarter, sizeof(quarter));
    fixture_input_value(&f, BLOCK_PIN(&f, to_pixel, 1), &half, sizeof(half));
    fixture_input_int(&f, fixture_target(&f, pickable), (int32_t)thing);
    fixture_input_int(&f, BLOCK_PIN(&f, pickable, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, system, 0), 2); /* busy */
    fixture_input_value(&f, BLOCK_PIN(&f, caret, 0), "ab\bcd", 6);
    fixture_input_int(&f, BLOCK_PIN(&f, caret, 1), 1);
    /* A red rectangle with a 2 pixel green border */
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f}, green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const nmo_rect_t screen = {10.0f, 10.0f, 50.0f, 30.0f}, uv = {0.0f, 0.0f, 1.0f, 1.0f};
    const float border = 2.0f;
    fixture_input_int(&f, BLOCK_PIN(&f, rectangle, 0), 1);
    fixture_input_value(&f, BLOCK_PIN(&f, rectangle, 1), red, sizeof(red));
    fixture_input_value(&f, BLOCK_PIN(&f, rectangle, 3), &screen, sizeof(screen));
    fixture_input_value(&f, BLOCK_PIN(&f, rectangle, 4), &uv, sizeof(uv));
    fixture_input_int(&f, BLOCK_PIN(&f, rectangle, 5), 1);
    fixture_input_value(&f, BLOCK_PIN(&f, rectangle, 6), green, sizeof(green));
    fixture_input_value(&f, BLOCK_PIN(&f, rectangle, 7), &border, sizeof(border));
    const float size[2] = {16.0f, 16.0f}, white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, cursor, 1), size, sizeof(size));
    fixture_input_value(&f, BLOCK_PIN(&f, cursor, 2), white, sizeof(white));
    fixture_input_int(&f, BLOCK_PIN(&f, cursor, 3), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_set_mouse_position(f.engine, 105.0f, 104.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    float ratio[2] = {0.0f, 0.0f};
    read_value(&f, BLOCK_POUT(&f, to_ratio, 0), &ratio[0], sizeof(float));
    read_value(&f, BLOCK_POUT(&f, to_ratio, 1), &ratio[1], sizeof(float));
    ASSERT_FLOAT_EQ(0.5f, ratio[0], 1e-6f);
    ASSERT_FLOAT_EQ(0.25f, ratio[1], 1e-6f);
    ASSERT_EQ(160, output_int(&f, to_pixel, 0));
    ASSERT_EQ(240, output_int(&f, to_pixel, 1));
    int32_t index = eng_entity_index(f.engine, thing);
    ASSERT_EQ(0u, f.engine->entities.data[index].moveable_flags & VX_MOVEABLE_PICKABLE);
    ASSERT_EQ(2, nmo_engine_system_cursor(f.engine));
    const void *text = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, caret, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("a\bbcd", (const char *)text); /* the old caret gone, the new one after 'a' */
    ASSERT_FALSE(nmo_engine_cursor_is_visible(f.engine));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(1, quads_of_color(&frame, 0xFFFF0000u)); /* the interior, inside the border */
    ASSERT_EQ(4, quads_of_color(&frame, 0xFF00FF00u));
    int32_t cursors = 0;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        const nmo_engine_draw_t *draw = &frame.draws[i];
        if (draw->kind == NMO_ENGINE_DRAW_QUAD && draw->color == 0xFFFF0000u) {
            ASSERT_FLOAT_EQ(12.0f, draw->rect[0], 0.0f);
            ASSERT_FLOAT_EQ(48.0f, draw->rect[2], 0.0f);
        }
        if (draw->kind == NMO_ENGINE_DRAW_QUAD && draw->color == 0xFFFFFFFFu && draw->rect[0] == 105.0f &&
            draw->rect[3] == 120.0f) {
            cursors++;
        }
    }
    ASSERT_EQ(1, cursors); /* at the mouse, 16 pixels big */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, quads_of_color(&frame, 0xFFFF0000u)); /* drawn once */
    fixture_end(&f);
}

TEST(engine_interface, drag_and_drop_moves_its_owner)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t knob = fixture_create(&f, NMO_CID_2DENTITY, "Knob");
    ASSERT_TRUE(knob != 0);
    f.owner_id = knob; /* not targetable: the block moves its owner */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t drag = started(&f, GUID_DRAG_AND_DROP, "Drag");
    nmo_object_id_t dragged = fixture_block(&f, GUID_NOP, "Dragged"), dropped = fixture_block(&f, GUID_NOP, "Dropped");
    ASSERT_TRUE(drag && dragged && dropped);
    const nmo_rect_t limit = {0.0f, 0.0f, 300.0f, 200.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, drag, 0), &limit, sizeof(limit));
    fixture_link(&f, BLOCK_OUT(&f, drag, 0), BLOCK_IN(&f, dragged, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, drag, 1), BLOCK_IN(&f, dropped, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    set_rect_of(&f, knob, 100.0f, 100.0f, 120.0f, 110.0f);
    nmo_engine_set_mouse_position(f.engine, 105.0f, 104.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Grab it 5, 4 in, move, release */
    nmo_engine_set_entity_under_mouse(f.engine, knob);
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, dragged));
    nmo_engine_set_mouse_position(f.engine, 205.0f, 154.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(200.0f, rect_of(&f, knob).left, 1e-4f);
    ASSERT_FLOAT_EQ(150.0f, rect_of(&f, knob).top, 1e-4f);
    nmo_engine_set_mouse_position(f.engine, 905.0f, 154.0f); /* kept in the limit: right - 1 */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FLOAT_EQ(299.0f, rect_of(&f, knob).left, 1e-4f);
    nmo_engine_set_mouse_button(f.engine, 0, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, dropped));
    ASSERT_EQ(1u, runs(&f, dragged));
    fixture_end(&f);
}

TEST(engine_interface, bitmap_text_display_draws_the_sprite_a_character_at_a_time)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sprite = fixture_create(&f, NMO_CID_SPRITE, "Font Sprite");
    ASSERT_TRUE(sprite != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t display = started(&f, GUID_BITMAP_TEXT_DISPLAY, "Display");
    ASSERT_TRUE(display != 0);
    const float position[2] = {10.0f, 20.0f}, scale[2] = {1.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, display, 0), position, sizeof(position));
    fixture_input_int(&f, BLOCK_PIN(&f, display, 1), 1);
    fixture_input_value(&f, BLOCK_PIN(&f, display, 2), scale, sizeof(scale));
    fixture_input_int(&f, BLOCK_PIN(&f, display, 3), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, display, 4), 20);
    fixture_input_value(&f, BLOCK_PIN(&f, display, 5), "AB|B", 5);
    fixture_commit(&f, NULL);
    /* The font as the file keeps it: 'A' from x 1, 3 wide; 'B' from 5, 2 wide; 4 high */
    int32_t font[516];
    memset(font, 0, sizeof(font));
    for (int c = 0; c < 256; c++) {
        font[1 + 2 * c] = -1;
    }
    font[1 + 2 * 'A'] = 1;
    font[2 + 2 * 'A'] = 3;
    font[1 + 2 * 'B'] = 5;
    font[2 + 2 * 'B'] = 2;
    font[513] = 4;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, block_local(&f, display, 0), &sprite, sizeof(sprite)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, block_local(&f, display, 2), font, sizeof(font)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    eng_texture_t *bitmap = eng_texture(f.engine, sprite);
    ASSERT_TRUE(bitmap != NULL && eng_texture_create(f.engine, bitmap, 8, 4, 0));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, sprite)); /* hidden, drawn by the block */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* "AB" then, '|' starting a line 20 lower, "B" */
    const float expected[3][4] = {{10, 20, 13, 24}, {14, 20, 16, 24}, {10, 40, 12, 44}};
    const float uv_left[3] = {1.0f / 8.0f, 5.0f / 8.0f, 5.0f / 8.0f};
    int32_t found = 0;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        const nmo_engine_draw_t *draw = &frame.draws[i];
        if (draw->kind != NMO_ENGINE_DRAW_QUAD || draw->object != sprite) {
            continue;
        }
        ASSERT_TRUE(found < 3);
        for (int k = 0; k < 4; k++) {
            ASSERT_FLOAT_EQ(expected[found][k], draw->rect[k], 1e-4f);
        }
        ASSERT_FLOAT_EQ(uv_left[found], draw->uv[0], 1e-6f);
        found++;
    }
    ASSERT_EQ(3, found);
    fixture_end(&f);
}

TEST(engine_interface, resize_view_and_additional_view)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Side Camera");
    ASSERT_TRUE(camera != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t resize = started(&f, GUID_RESIZE_VIEW, "Resize");
    nmo_object_id_t extra = started(&f, GUID_ADDITIONAL_VIEW, "Extra");
    nmo_object_id_t stop = fixture_block(&f, GUID_NOP, "Later");
    ASSERT_TRUE(resize && extra && stop);
    const nmo_rect_t region = {10.0f, 20.0f, 330.0f, 260.0f}, corner = {0.0f, 0.0f, 100.0f, 60.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, resize, 0), &region, sizeof(region));
    fixture_input_int(&f, fixture_target(&f, extra), (int32_t)camera);
    fixture_input_value(&f, BLOCK_PIN(&f, extra, 0), &corner, sizeof(corner));
    fixture_input_int(&f, BLOCK_PIN(&f, extra, 1), 0x31); /* background sprites, clear Z and back */
    fixture_input_int(&f, BLOCK_PIN(&f, extra, 2), 1);
    /* Off for both two frames later */
    fixture_link(&f, f.script_in, BLOCK_IN(&f, stop, 0), 2);
    fixture_link(&f, BLOCK_OUT(&f, stop, 0), BLOCK_IN(&f, resize, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, stop, 0), BLOCK_IN(&f, extra, 1), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, frame.viewport[0]); /* the whole 640 x 480 render context at first */
    ASSERT_EQ(640, frame.viewport[2]);
    ASSERT_EQ(0, frame.view_count);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* The scene in the region, the camera's ratio no longer kept */
    ASSERT_EQ(10, frame.viewport[0]);
    ASSERT_EQ(20, frame.viewport[1]);
    ASSERT_EQ(320, frame.viewport[2]);
    ASSERT_EQ(240, frame.viewport[3]);
    ASSERT_EQ(0u, frame.render_options & 0x08u);
    eng_matrix_t eye;
    eng_render_viewpoint(f.engine, &eye);
    eng_vector_t ahead = {eye.m[3][0] + eye.m[2][0] * 10.0f, eye.m[3][1] + eye.m[2][1] * 10.0f,
                          eye.m[3][2] + eye.m[2][2] * 10.0f}, screen;
    eng_render_project(f.engine, &ahead, &screen);
    ASSERT_FLOAT_EQ(170.0f, screen.x, 1e-3f); /* the middle of the region */
    ASSERT_FLOAT_EQ(140.0f, screen.y, 1e-3f);
    /* The side camera's view in the corner, over the 2D */
    ASSERT_EQ(1, frame.view_count);
    const nmo_engine_frame_t *side = &frame.views[0].frame;
    ASSERT_TRUE(frame.views[0].over_2d);
    ASSERT_EQ(camera, side->camera);
    ASSERT_EQ(100, side->viewport[2]);
    ASSERT_EQ(60, side->viewport[3]);
    ASSERT_EQ(0x931u, side->render_options); /* with CLEARVIEWPORT and DONOTUPDATEEXTENTS */
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, frame.view_count); /* once a frame of the behaviors */
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* Off: the whole render context, the camera's ratio again */
    ASSERT_EQ(0, frame.viewport[0]);
    ASSERT_EQ(640, frame.viewport[2]);
    ASSERT_EQ(480, frame.viewport[3]);
    ASSERT_EQ(0x08u, frame.render_options & 0x08u);
    /* the request of the frame Off came in, before it, waited for this render */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_EQ(0, frame.view_count);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_interface, fonts_are_numbered_by_name);
REGISTER_TEST(engine_interface, text_2d_draws_until_off);
REGISTER_TEST(engine_interface, text_display_shows_its_text_until_off);
REGISTER_TEST(engine_interface, text_display_keeps_its_text_in_a_sprite_text);
REGISTER_TEST(engine_interface, push_button_follows_the_mouse);
REGISTER_TEST(engine_interface, tt_push_button2_needs_the_cursor_shown);
REGISTER_TEST(engine_interface, layouts_place_2d_and_3d_entities);
REGISTER_TEST(engine_interface, screen_blocks_rectangles_and_cursor);
REGISTER_TEST(engine_interface, drag_and_drop_moves_its_owner);
REGISTER_TEST(engine_interface, bitmap_text_display_draws_the_sprite_a_character_at_a_time);
REGISTER_TEST(engine_interface, resize_view_and_additional_view);
TEST_MAIN_END()
