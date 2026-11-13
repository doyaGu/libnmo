/**
 * @file test_engine_operations.c
 * @brief The parameter operations the engine runs itself because they read
 *        its state: Per Second (the frame's delta) and Random (its random
 *        sequence), and the math operations of the registry reached through
 *        a derived type (Angle finds the Float function, as CK does).
 */

#include "engine_fixture.h"

#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <math.h>

#define GUID_IDENTITY NMO_GUID(0x15151652, 0xaeefffd5)
#define OP_GET_WIDTH NMO_GUID(0x5a4e13fe, 0x628b7ca2)
#define OP_GET_VIEW_RECT NMO_GUID(0x2a143038, 0x03d126b9)
#define OP_GET_MOUSE_POSITION NMO_GUID(0x06ea0201, 0x680e3a62)
#define OP_GET_MOUSE_X NMO_GUID(0x53c51abe, 0x0eba68de)
#define OP_IS_DERIVED_FROM NMO_GUID(0x6ab94f5a, 0x1a6376a9)
#define OP_DYNAMIC_CAST NMO_GUID(0x39697e76, 0x538376ec)
#define OP_GET_GROUP_TYPE NMO_GUID(0x429c3ff2, 0x0e4a4938)
#define OP_IS_CONTENT_EQUAL NMO_GUID(0x3dda41e5, 0x036c6833)
#define OP_IS_IN NMO_GUID(0x540e1450, 0x593f1692)
#define OP_IS_COLLISION NMO_GUID(0x66ae6c61, 0x038658e8)
#define OP_GET_GEOMETRIC_CENTER NMO_GUID(0x6a8c4838, 0x7b026566)
#define OP_GET_SIZE NMO_GUID(0x54f52b51, 0x564c4812)
#define OP_GET_CURVE_POINT NMO_GUID(0x4e9426c2, 0x04e50e5f)
#define OP_GET_CURVE_POSITION NMO_GUID(0x61323b67, 0x3a282f1c)
#define OP_GET_CURVE_TANGENT NMO_GUID(0x54ba6299, 0x51bf236d)
#define OP_GET_OUT_TANGENT NMO_GUID(0xabc16e82, 0x73ef6952)

/* A local parameter of the script of `type` holding `size` bytes of `data`; a string holds
   its text, its buffer grown to it (it starts as large as a pointer) */
static nmo_object_id_t local_value(engine_fixture_t *f, nmo_guid_t type, const char *name,
                                   const void *data, size_t size)
{
    nmo_object_id_t id = 0;
    if (nmo_script_edit_add_parameter(f->tx, f->script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, type,
                                      name, &id) != NMO_OK) {
        return 0;
    }
    nmo_status_t status =
        nmo_guid_equals(type, CKPGUID_STRING)
            ? nmo_script_edit_set_parameter_value(f->tx, id, (const char *)data)
            : nmo_script_edit_set_parameter_bytes(f->tx, id, (const uint8_t *)data, size);
    return status == NMO_OK ? id : 0;
}

static nmo_object_id_t local_float(engine_fixture_t *f, nmo_guid_t type, const char *name,
                                   float value)
{
    return local_value(f, type, name, &value, sizeof(value));
}

static nmo_object_id_t local_int(engine_fixture_t *f, const char *name, int32_t value)
{
    return local_value(f, CKPGUID_INT, name, &value, sizeof(value));
}

/*
 * An operation on `in1` and `in2` (either may be 0: that input reads nothing
 * and is of type None), its output of `type`; the output's ID.
 */
static nmo_object_id_t operation(engine_fixture_t *f, nmo_guid_t guid, nmo_object_id_t in1,
                                 nmo_object_id_t in2, nmo_guid_t type)
{
    nmo_object_id_t op = 0;
    if (nmo_script_edit_add_operation(f->tx, f->script_id, guid, in1, in2, 0, &op) != NMO_OK) {
        return 0;
    }
    const nmo_parameteroperation_state_t *state =
        (const nmo_parameteroperation_state_t *)fixture_state(f, op);
    nmo_object_id_t out = state != NULL ? nmo_parameteroperation_out_id(state) : 0;
    nmo_parameter_state_t *out_state = (nmo_parameter_state_t *)fixture_state(f, out);
    if (out_state == NULL) {
        return 0;
    }
    const nmo_object_id_t inputs[2] = {in1, in2};
    const nmo_object_id_t pins[2] = {nmo_parameteroperation_in1_id(state),
                                     nmo_parameteroperation_in2_id(state)};
    for (int i = 0; i < 2; i++) {
        nmo_parameterin_state_t *pin = (nmo_parameterin_state_t *)fixture_state(f, pins[i]);
        const nmo_parameter_state_t *source =
            inputs[i] != 0 ? (const nmo_parameter_state_t *)fixture_state(f, inputs[i]) : NULL;
        if (pin == NULL) {
            return 0;
        }
        /* The operation takes its operands' types, as the operation that matches them does */
        pin->type_guid = source != NULL ? source->type_guid : CKPGUID_NONE;
    }
    out_state->type_guid = type;
    return out;
}

/* An Identity reading `source` as `type`, activated after `after` */
static nmo_object_id_t identity_of(engine_fixture_t *f, nmo_object_id_t source, nmo_guid_t type,
                                   nmo_object_id_t after)
{
    nmo_object_id_t identity = fixture_block(f, GUID_IDENTITY, "Identity");
    nmo_parameterin_state_t *pin =
        (nmo_parameterin_state_t *)fixture_state(f, BLOCK_PIN(f, identity, 0));
    nmo_parameter_state_t *pout = (nmo_parameter_state_t *)fixture_state(f, BLOCK_POUT(f, identity, 0));
    if (pin == NULL || pout == NULL) {
        return 0;
    }
    pin->type_guid = type;
    pout->type_guid = type;
    if (nmo_script_edit_connect_parameter(f->tx, source, BLOCK_PIN(f, identity, 0)) != NMO_OK) {
        return 0;
    }
    fixture_link(f, after, BLOCK_IN(f, identity, 0), 0);
    return identity;
}

static void run_frame(engine_fixture_t *f, float delta_ms)
{
    ASSERT_EQ(NMO_OK, nmo_engine_start(f->engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, delta_ms));
}

TEST(engine_operations, per_second_scales_by_the_frame_delta)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t speed = local_float(&f, CKPGUID_FLOAT, "Speed", 100.0f);
    const float velocity[3] = {10.0f, 20.0f, 30.0f};
    nmo_object_id_t motion = local_value(&f, CKPGUID_VECTOR, "Motion", velocity, sizeof(velocity));
    ASSERT_TRUE(speed != 0 && motion != 0);
    nmo_object_id_t per_frame = operation(&f, NMO_OP_GUID_PER_SECOND, speed, 0, CKPGUID_FLOAT);
    nmo_object_id_t step = operation(&f, NMO_OP_GUID_PER_SECOND, motion, 0, CKPGUID_VECTOR);
    ASSERT_TRUE(per_frame != 0 && step != 0);
    nmo_object_id_t read_speed = identity_of(&f, per_frame, CKPGUID_FLOAT, f.script_in);
    ASSERT_TRUE(read_speed != 0);
    nmo_object_id_t read_motion =
        identity_of(&f, step, CKPGUID_VECTOR, BLOCK_OUT(&f, read_speed, 0));
    ASSERT_TRUE(read_motion != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 20.0f);
    /* 100 per second over 20 ms; the plugin's vector version scales by a tenth of that */
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, read_speed, 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(2.0f, value, 1e-6f);
    float scaled[3] = {0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, read_motion, 0), scaled, sizeof(scaled));
    ASSERT_FLOAT_EQ(0.02f, scaled[0], 1e-6f);
    ASSERT_FLOAT_EQ(0.04f, scaled[1], 1e-6f);
    ASSERT_FLOAT_EQ(0.06f, scaled[2], 1e-6f);
    fixture_end(&f);
}

typedef struct random_run {
    int32_t whole;
    float real;
    uint32_t flag;
    float color[4];
} random_run_t;

/* One engine's Random values of a frame */
static void random_frame(random_run_t *out)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t low = local_int(&f, "Low", 5), high = local_int(&f, "High", 7);
    nmo_object_id_t from = local_float(&f, CKPGUID_FLOAT, "From", 1.0f);
    nmo_object_id_t to = local_float(&f, CKPGUID_FLOAT, "To", 2.0f);
    ASSERT_TRUE(low != 0 && high != 0 && from != 0 && to != 0);
    nmo_object_id_t whole = operation(&f, NMO_OP_GUID_RANDOM, low, high, CKPGUID_INT);
    nmo_object_id_t real = operation(&f, NMO_OP_GUID_RANDOM, from, to, CKPGUID_FLOAT);
    nmo_object_id_t flag = operation(&f, NMO_OP_GUID_RANDOM, 0, 0, CKPGUID_BOOL);
    nmo_object_id_t color = operation(&f, NMO_OP_GUID_RANDOM, 0, 0, CKPGUID_COLOR);
    ASSERT_TRUE(whole != 0 && real != 0 && flag != 0 && color != 0);
    nmo_object_id_t read_whole = identity_of(&f, whole, CKPGUID_INT, f.script_in);
    nmo_object_id_t read_real = identity_of(&f, real, CKPGUID_FLOAT, BLOCK_OUT(&f, read_whole, 0));
    nmo_object_id_t read_flag = identity_of(&f, flag, CKPGUID_BOOL, BLOCK_OUT(&f, read_real, 0));
    nmo_object_id_t read_color = identity_of(&f, color, CKPGUID_COLOR, BLOCK_OUT(&f, read_flag, 0));
    ASSERT_TRUE(read_whole != 0 && read_real != 0 && read_flag != 0 && read_color != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 16.0f);
    read_value(&f, BLOCK_POUT(&f, read_whole, 0), &out->whole, sizeof(out->whole));
    read_value(&f, BLOCK_POUT(&f, read_real, 0), &out->real, sizeof(out->real));
    read_value(&f, BLOCK_POUT(&f, read_flag, 0), &out->flag, sizeof(out->flag));
    read_value(&f, BLOCK_POUT(&f, read_color, 0), out->color, sizeof(out->color));
    fixture_end(&f);
}

TEST(engine_operations, random_stays_in_its_bounds_and_repeats_with_the_seed)
{
    random_run_t first, second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0xff, sizeof(second));
    random_frame(&first);
    random_frame(&second);
    /* Between the bounds, the integer ones inclusive; the color is opaque */
    ASSERT_TRUE(first.whole >= 5 && first.whole <= 7);
    ASSERT_TRUE(first.real >= 1.0f && first.real < 2.0f);
    ASSERT_TRUE(first.flag == 0u || first.flag == 1u);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(first.color[i] >= 0.0f && first.color[i] < 1.0f);
    }
    ASSERT_FLOAT_EQ(1.0f, first.color[3], 0.0f);
    /* The engine's random sequence, seeded the same, gives the same values */
    ASSERT_EQ(0, memcmp(&first, &second, sizeof(first)));
}

TEST(engine_operations, derived_types_find_the_float_functions)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* Square Root is registered for Float; an Angle operand finds it through its base type */
    nmo_object_id_t square = local_float(&f, CKPGUID_ANGLE, "Square", 9.0f);
    ASSERT_TRUE(square != 0);
    nmo_object_id_t root = operation(&f, NMO_OP_GUID_SQRT, square, 0, CKPGUID_ANGLE);
    ASSERT_TRUE(root != 0);
    nmo_object_id_t read_root = identity_of(&f, root, CKPGUID_ANGLE, f.script_in);
    ASSERT_TRUE(read_root != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 16.0f);
    float value = 0.0f;
    read_value(&f, BLOCK_POUT(&f, read_root, 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(3.0f, value, 0.0f);
    fixture_end(&f);
}

/* The engine ID an Identity block's output holds */
static uint32_t read_id(engine_fixture_t *f, nmo_object_id_t identity)
{
    uint32_t id = 0xFFFFFFFFu;
    read_value(f, BLOCK_POUT(f, identity, 0), &id, sizeof(id));
    return id;
}

TEST(engine_operations, render_context_and_mouse_getters)
{
    engine_fixture_t f;
    fixture_begin(&f);
    const int32_t yes = 1;
    nmo_object_id_t absolute = local_value(&f, CKPGUID_BOOL, "Absolute", &yes, sizeof(yes));
    ASSERT_TRUE(absolute != 0);
    nmo_object_id_t width = operation(&f, OP_GET_WIDTH, 0, 0, CKPGUID_INT);
    nmo_object_id_t view = operation(&f, OP_GET_VIEW_RECT, 0, 0, CKPGUID_RECT);
    nmo_object_id_t mouse_x = operation(&f, OP_GET_MOUSE_X, 0, 0, CKPGUID_INT);
    nmo_object_id_t clamped = operation(&f, OP_GET_MOUSE_POSITION, absolute, 0, CKPGUID_2DVECTOR);
    ASSERT_TRUE(width != 0 && view != 0 && mouse_x != 0 && clamped != 0);
    nmo_object_id_t read_width = identity_of(&f, width, CKPGUID_INT, f.script_in);
    nmo_object_id_t read_view = identity_of(&f, view, CKPGUID_RECT, BLOCK_OUT(&f, read_width, 0));
    nmo_object_id_t read_x = identity_of(&f, mouse_x, CKPGUID_INT, BLOCK_OUT(&f, read_view, 0));
    nmo_object_id_t read_clamped = identity_of(&f, clamped, CKPGUID_2DVECTOR, BLOCK_OUT(&f, read_x, 0));
    ASSERT_TRUE(read_width != 0 && read_view != 0 && read_x != 0 && read_clamped != 0);
    fixture_commit(&f, NULL);
    /* Off the window to the left and below it */
    nmo_engine_set_mouse_position(f.engine, -5.5f, 700.0f);

    run_frame(&f, 16.0f);
    /* The default screen mode is 640 by 480 */
    ASSERT_EQ(640u, read_id(&f, read_width));
    float rect[4] = {1, 1, 1, 1};
    read_value(&f, BLOCK_POUT(&f, read_view, 0), rect, sizeof(rect));
    ASSERT_FLOAT_EQ(0.0f, rect[0], 0.0f);
    ASSERT_FLOAT_EQ(0.0f, rect[1], 0.0f);
    ASSERT_FLOAT_EQ(640.0f, rect[2], 0.0f);
    ASSERT_FLOAT_EQ(480.0f, rect[3], 0.0f);
    ASSERT_EQ((uint32_t)-5, read_id(&f, read_x));
    float position[2] = {1, 1};
    read_value(&f, BLOCK_POUT(&f, read_clamped, 0), position, sizeof(position));
    ASSERT_FLOAT_EQ(0.0f, position[0], 0.0f);
    ASSERT_FLOAT_EQ(479.0f, position[1], 0.0f);
    fixture_end(&f);
}

/* A local parameter of the script of object type `type`; its object is set in the engine */
static nmo_object_id_t local_object(engine_fixture_t *f, const char *name, nmo_guid_t type)
{
    nmo_object_id_t param = 0;
    return nmo_script_edit_add_parameter(f->tx, f->script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, type,
                                         name, &param) == NMO_OK
               ? param
               : 0;
}

TEST(engine_operations, object_class_group_and_collection_getters)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t other = fixture_create(&f, NMO_CID_3DENTITY, "Other");
    nmo_object_id_t group = fixture_create(&f, NMO_CID_GROUP, "Group");
    nmo_group_state_t *state = (nmo_group_state_t *)fixture_state(&f, group);
    ASSERT_TRUE(other != 0 && state != NULL);
    nmo_ref_t *members = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&state->object_ids, 2u, (void **)&members));
    members[0] = nmo_ref_from_id(f.owner_id);
    members[1] = nmo_ref_from_id(other);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t object = local_object(&f, "Object", CKPGUID_3DENTITY);
    nmo_object_id_t owner = local_object(&f, "Owner", CKPGUID_3DENTITY);
    nmo_object_id_t in_group = local_object(&f, "Group", CKPGUID_GROUP);
    nmo_object_id_t collection = local_object(&f, "Collection", CKPGUID_OBJECTARRAY);
    ASSERT_TRUE(object != 0 && owner != 0 && in_group != 0 && collection != 0);
    nmo_object_id_t derived = operation(&f, OP_IS_DERIVED_FROM, object, owner, CKPGUID_BOOL);
    nmo_object_id_t as_entity = operation(&f, OP_DYNAMIC_CAST, object, 0, CKPGUID_3DENTITY);
    nmo_object_id_t as_camera = operation(&f, OP_DYNAMIC_CAST, object, 0, CKPGUID_CAMERA);
    nmo_object_id_t group_type = operation(&f, OP_GET_GROUP_TYPE, in_group, 0, CKPGUID_CLASSID);
    nmo_object_id_t same = operation(&f, OP_IS_CONTENT_EQUAL, in_group, in_group, CKPGUID_BOOL);
    nmo_object_id_t in = operation(&f, OP_IS_IN, collection, owner, CKPGUID_BOOL);
    ASSERT_TRUE(derived != 0 && as_entity != 0 && as_camera != 0 && group_type != 0 && same != 0 && in != 0);
    nmo_object_id_t read_derived = identity_of(&f, derived, CKPGUID_BOOL, f.script_in);
    nmo_object_id_t read_entity = identity_of(&f, as_entity, CKPGUID_3DENTITY, BLOCK_OUT(&f, read_derived, 0));
    nmo_object_id_t read_camera = identity_of(&f, as_camera, CKPGUID_CAMERA, BLOCK_OUT(&f, read_entity, 0));
    nmo_object_id_t read_type = identity_of(&f, group_type, CKPGUID_CLASSID, BLOCK_OUT(&f, read_camera, 0));
    nmo_object_id_t read_same = identity_of(&f, same, CKPGUID_BOOL, BLOCK_OUT(&f, read_type, 0));
    nmo_object_id_t read_in = identity_of(&f, in, CKPGUID_BOOL, BLOCK_OUT(&f, read_same, 0));
    ASSERT_TRUE(read_derived != 0 && read_entity != 0 && read_camera != 0 && read_type != 0 &&
                read_same != 0 && read_in != 0);
    fixture_commit(&f, NULL);
    const uint32_t ids[3] = {(uint32_t)other, (uint32_t)f.owner_id, (uint32_t)group};
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, object, &ids[0], sizeof(ids[0])));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, owner, &ids[1], sizeof(ids[1])));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, in_group, &ids[2], sizeof(ids[2])));
    /* The collection holds the other entity and the owner */
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, collection, ids, 2u * sizeof(ids[0])));

    run_frame(&f, 16.0f);
    ASSERT_EQ(1u, read_id(&f, read_derived));
    ASSERT_EQ((uint32_t)other, read_id(&f, read_entity));
    ASSERT_EQ(0u, read_id(&f, read_camera));
    ASSERT_EQ((uint32_t)NMO_CID_3DENTITY, read_id(&f, read_type));
    ASSERT_EQ(1u, read_id(&f, read_same));
    ASSERT_EQ(1u, read_id(&f, read_in));
    fixture_end(&f);
}

TEST(engine_operations, box_and_rectangle_getters)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* A VxBbox is its maximum then its minimum */
    const float box[6] = {4.0f, 6.0f, 8.0f, 0.0f, 2.0f, 4.0f};
    const float inside[3] = {1.0f, 3.0f, 5.0f}, outside[3] = {5.0f, 3.0f, 5.0f};
    const float rect[4] = {10.0f, 20.0f, 30.0f, 60.0f};
    nmo_object_id_t b = local_value(&f, CKPGUID_BOX, "Box", box, sizeof(box));
    nmo_object_id_t in = local_value(&f, CKPGUID_VECTOR, "Inside", inside, sizeof(inside));
    nmo_object_id_t out = local_value(&f, CKPGUID_VECTOR, "Outside", outside, sizeof(outside));
    nmo_object_id_t r = local_value(&f, CKPGUID_RECT, "Rect", rect, sizeof(rect));
    ASSERT_TRUE(b != 0 && in != 0 && out != 0 && r != 0);
    nmo_object_id_t hit = operation(&f, OP_IS_COLLISION, b, in, CKPGUID_BOOL);
    nmo_object_id_t miss = operation(&f, OP_IS_COLLISION, b, out, CKPGUID_BOOL);
    nmo_object_id_t center = operation(&f, OP_GET_GEOMETRIC_CENTER, b, 0, CKPGUID_VECTOR);
    nmo_object_id_t size = operation(&f, OP_GET_SIZE, r, 0, CKPGUID_2DVECTOR);
    nmo_object_id_t middle = operation(&f, OP_GET_GEOMETRIC_CENTER, r, 0, CKPGUID_2DVECTOR);
    ASSERT_TRUE(hit != 0 && miss != 0 && center != 0 && size != 0 && middle != 0);
    nmo_object_id_t read_hit = identity_of(&f, hit, CKPGUID_BOOL, f.script_in);
    nmo_object_id_t read_miss = identity_of(&f, miss, CKPGUID_BOOL, BLOCK_OUT(&f, read_hit, 0));
    nmo_object_id_t read_center = identity_of(&f, center, CKPGUID_VECTOR, BLOCK_OUT(&f, read_miss, 0));
    nmo_object_id_t read_size = identity_of(&f, size, CKPGUID_2DVECTOR, BLOCK_OUT(&f, read_center, 0));
    nmo_object_id_t read_middle = identity_of(&f, middle, CKPGUID_2DVECTOR, BLOCK_OUT(&f, read_size, 0));
    ASSERT_TRUE(read_hit != 0 && read_miss != 0 && read_center != 0 && read_size != 0 && read_middle != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 16.0f);
    ASSERT_EQ(1u, read_id(&f, read_hit));
    ASSERT_EQ(0u, read_id(&f, read_miss));
    float v[3] = {0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, read_center, 0), v, sizeof(v));
    ASSERT_FLOAT_EQ(2.0f, v[0], 0.0f);
    ASSERT_FLOAT_EQ(4.0f, v[1], 0.0f);
    ASSERT_FLOAT_EQ(6.0f, v[2], 0.0f);
    read_value(&f, BLOCK_POUT(&f, read_size, 0), v, 2u * sizeof(v[0]));
    ASSERT_FLOAT_EQ(20.0f, v[0], 0.0f);
    ASSERT_FLOAT_EQ(40.0f, v[1], 0.0f);
    read_value(&f, BLOCK_POUT(&f, read_middle, 0), v, 2u * sizeof(v[0]));
    ASSERT_FLOAT_EQ(20.0f, v[0], 0.0f);
    ASSERT_FLOAT_EQ(40.0f, v[1], 0.0f);
    fixture_end(&f);
}

/* An open curve of three TCB points along x, two apart */
TEST(engine_operations, curve_getters)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t path = fixture_create(&f, NMO_CID_CURVE, "Path");
    nmo_curve_state_t *curve = (nmo_curve_state_t *)fixture_state(&f, path);
    ASSERT_TRUE(curve != NULL);
    curve->opened = 1;
    curve->control_point_count = 3u;
    curve->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(f.session), 3u * sizeof(nmo_ref_t),
                                               _Alignof(nmo_ref_t));
    ASSERT_TRUE(curve->control_point_ids != NULL);
    nmo_object_id_t points[3];
    for (size_t i = 0; i < 3; i++) {
        points[i] = fixture_create(&f, NMO_CID_CURVEPOINT, "Point");
        nmo_curvepoint_state_t *p = (nmo_curvepoint_state_t *)fixture_state(&f, points[i]);
        ASSERT_TRUE(p != NULL);
        p->base.parent = nmo_ref_from_id(path);
        p->base.has_entityndata_chunk = 1;
        p->curve = nmo_ref_from_id(path);
        nmo_curvepoint_set_position(p, 2.0f * (float)i, 0.0f, 0.0f);
        curve->control_point_ids[i] = nmo_ref_from_id(points[i]);
    }
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t on_curve = local_object(&f, "Curve", CKPGUID_CURVE);
    nmo_object_id_t half = local_float(&f, CKPGUID_FLOAT, "Half", 0.5f);
    const int32_t one = 1;
    nmo_object_id_t index = local_value(&f, CKPGUID_INT, "Index", &one, sizeof(one));
    ASSERT_TRUE(on_curve != 0 && half != 0 && index != 0);
    nmo_object_id_t point = operation(&f, OP_GET_CURVE_POINT, on_curve, index, CKPGUID_CURVEPOINT);
    nmo_object_id_t position = operation(&f, OP_GET_CURVE_POSITION, half, on_curve, CKPGUID_VECTOR);
    nmo_object_id_t tangent = operation(&f, OP_GET_CURVE_TANGENT, half, on_curve, CKPGUID_VECTOR);
    nmo_object_id_t outgoing = operation(&f, OP_GET_OUT_TANGENT, point, 0, CKPGUID_VECTOR);
    ASSERT_TRUE(point != 0 && position != 0 && tangent != 0 && outgoing != 0);
    nmo_object_id_t read_point = identity_of(&f, point, CKPGUID_CURVEPOINT, f.script_in);
    nmo_object_id_t read_position = identity_of(&f, position, CKPGUID_VECTOR, BLOCK_OUT(&f, read_point, 0));
    nmo_object_id_t read_tangent = identity_of(&f, tangent, CKPGUID_VECTOR, BLOCK_OUT(&f, read_position, 0));
    nmo_object_id_t read_outgoing = identity_of(&f, outgoing, CKPGUID_VECTOR, BLOCK_OUT(&f, read_tangent, 0));
    ASSERT_TRUE(read_point != 0 && read_position != 0 && read_tangent != 0 && read_outgoing != 0);
    fixture_commit(&f, NULL);
    const uint32_t id = (uint32_t)path;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, on_curve, &id, sizeof(id)));

    run_frame(&f, 16.0f);
    ASSERT_EQ((uint32_t)points[1], read_id(&f, read_point));
    float v[3] = {0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, read_position, 0), v, sizeof(v));
    ASSERT_FLOAT_EQ(2.0f, v[0], 1e-4f);
    ASSERT_FLOAT_EQ(0.0f, v[1], 1e-6f);
    /* A thousandth of the curve's length of 4, along x */
    read_value(&f, BLOCK_POUT(&f, read_tangent, 0), v, sizeof(v));
    ASSERT_FLOAT_EQ(0.004f, v[0], 1e-4f);
    ASSERT_FLOAT_EQ(0.0f, v[1], 1e-6f);
    /* The middle point's TCB out tangent: half the way from the previous to the next point */
    read_value(&f, BLOCK_POUT(&f, read_outgoing, 0), v, sizeof(v));
    ASSERT_FLOAT_EQ(2.0f, v[0], 1e-5f);
    ASSERT_FLOAT_EQ(0.0f, v[1], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, v[2], 1e-6f);
    fixture_end(&f);
}

/* An Identity per output, each activated after the one before; the last one's ID */
static nmo_object_id_t read_chain(engine_fixture_t *f, const nmo_object_id_t *outputs,
                                  const nmo_guid_t *types, nmo_object_id_t *identities, size_t count)
{
    nmo_object_id_t after = f->script_in;
    for (size_t i = 0; i < count; i++) {
        identities[i] = identity_of(f, outputs[i], types[i], after);
        if (identities[i] == 0) {
            return 0;
        }
        after = BLOCK_OUT(f, identities[i], 0);
    }
    return identities[count - 1];
}

TEST(engine_operations, plugin_value_operations_on_mixed_and_compound_types)
{
    engine_fixture_t f;
    fixture_begin(&f);
    const nmo_guid_t add = NMO_GUID(0x33cc6b49, 0x3589282b), div = NMO_GUID(0x748c3502, 0x0fea0cf1);
    const nmo_guid_t and_op = NMO_GUID(0x4a450980, 0x6b230426), opposite = NMO_GUID(0x599c2cb6, 0x3e7d4de8);
    const nmo_guid_t inferior = NMO_GUID(0x74a36ef4, 0x61262b24), distance = NMO_GUID(0x265c7d00, 0x5c8f1702);
    const nmo_guid_t length = NMO_GUID(0x556a69af, 0x076e3f09);
    nmo_object_id_t one_half = local_float(&f, CKPGUID_FLOAT, "OneHalf", 1.5f);
    nmo_object_id_t two = local_int(&f, "Two", 2), zero = local_int(&f, "Zero", 0);
    nmo_object_id_t six = local_int(&f, "Six", 6), three = local_int(&f, "Three", 3);
    nmo_object_id_t almost_two = local_float(&f, CKPGUID_FLOAT, "AlmostTwo", 1.9f);
    const int32_t no = 0;
    nmo_object_id_t off = local_value(&f, CKPGUID_BOOL, "Off", &no, sizeof(no));
    const float origin[3] = {0, 0, 0}, distant[3] = {3, 4, 0};
    nmo_object_id_t a = local_value(&f, CKPGUID_VECTOR, "A", origin, sizeof(origin));
    nmo_object_id_t b = local_value(&f, CKPGUID_VECTOR, "B", distant, sizeof(distant));
    const float bright[4] = {0.6f, 0.2f, 0.9f, 1.0f};
    nmo_object_id_t color = local_value(&f, CKPGUID_COLOR, "Color", bright, sizeof(bright));
    nmo_object_id_t curve = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_add_parameter(f.tx, f.script_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                                                    CKPGUID_2DCURVE, "Curve", &curve));
    ASSERT_TRUE(one_half && two && zero && six && three && almost_two && off && a && b && color);
    const nmo_object_id_t outputs[] = {
        operation(&f, add, one_half, two, CKPGUID_FLOAT),      /* CKFloatAddFloatInt */
        operation(&f, add, two, almost_two, CKPGUID_INT),      /* CKIntAddIntFloat: 2 + (int)1.9 */
        operation(&f, and_op, six, three, CKPGUID_INT),        /* CKIntAndIntInt */
        operation(&f, opposite, off, 0, CKPGUID_BOOL),         /* CKBoolNotBool */
        operation(&f, inferior, one_half, two, CKPGUID_BOOL),  /* CKBoolInfFloatInt */
        operation(&f, div, one_half, zero, CKPGUID_FLOAT),     /* divides by one instead */
        operation(&f, distance, a, b, CKPGUID_FLOAT),          /* CKFloatGetDistanceVector */
        operation(&f, add, color, color, CKPGUID_COLOR),       /* CKColorAddColorColor: capped */
        operation(&f, length, curve, 0, CKPGUID_FLOAT),        /* CKFloatGetLength2dCurve */
    };
    const nmo_guid_t types[] = {CKPGUID_FLOAT, CKPGUID_INT, CKPGUID_INT, CKPGUID_BOOL, CKPGUID_BOOL,
                                CKPGUID_FLOAT, CKPGUID_FLOAT, CKPGUID_COLOR, CKPGUID_FLOAT};
    const size_t count = sizeof(types) / sizeof(types[0]);
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE(outputs[i] != 0);
    }
    nmo_object_id_t reads[sizeof(types) / sizeof(types[0])];
    ASSERT_TRUE(read_chain(&f, outputs, types, reads, count) != 0);
    fixture_commit(&f, NULL);

    run_frame(&f, 16.0f);
    float value = 0.0f;
    int32_t integer = 0;
    read_value(&f, BLOCK_POUT(&f, reads[0], 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(3.5f, value, 0.0f);
    read_value(&f, BLOCK_POUT(&f, reads[1], 0), &integer, sizeof(integer));
    ASSERT_EQ(3, integer);
    read_value(&f, BLOCK_POUT(&f, reads[2], 0), &integer, sizeof(integer));
    ASSERT_EQ(2, integer);
    read_value(&f, BLOCK_POUT(&f, reads[3], 0), &integer, sizeof(integer));
    ASSERT_EQ(1, integer);
    read_value(&f, BLOCK_POUT(&f, reads[4], 0), &integer, sizeof(integer));
    ASSERT_EQ(1, integer);
    read_value(&f, BLOCK_POUT(&f, reads[5], 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(1.5f, value, 0.0f);
    read_value(&f, BLOCK_POUT(&f, reads[6], 0), &value, sizeof(value));
    ASSERT_FLOAT_EQ(5.0f, value, 1e-6f);
    float sum[4] = {0, 0, 0, 0};
    read_value(&f, BLOCK_POUT(&f, reads[7], 0), sum, sizeof(sum));
    ASSERT_FLOAT_EQ(1.0f, sum[0], 0.0f);
    ASSERT_FLOAT_EQ(0.4f, sum[1], 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, sum[2], 0.0f);
    ASSERT_FLOAT_EQ(1.0f, sum[3], 0.0f);
    /* The default curve from (0, 0) to (1, 1): at least the straight line's length */
    read_value(&f, BLOCK_POUT(&f, reads[8], 0), &value, sizeof(value));
    ASSERT_TRUE(value >= 1.4142f && value < 1.6f);
    fixture_end(&f);
}

TEST(engine_operations, grids_layer_by_name)
{
    engine_fixture_t f;
    fixture_begin(&f);
    const nmo_guid_t by_name = NMO_GUID(0x470c4e6c, 0x144748b6);
    nmo_object_id_t known = local_value(&f, CKPGUID_STRING, "Known", "Nothing", 8);
    nmo_object_id_t unknown = local_value(&f, CKPGUID_STRING, "Unknown", "Nothing", 8);
    ASSERT_TRUE(known != 0 && unknown != 0);
    const nmo_object_id_t outputs[] = {operation(&f, by_name, known, 0, CKPGUID_LAYERTYPE),
                                       operation(&f, by_name, unknown, 0, CKPGUID_LAYERTYPE)};
    const nmo_guid_t types[] = {CKPGUID_LAYERTYPE, CKPGUID_LAYERTYPE};
    nmo_object_id_t reads[2];
    ASSERT_TRUE(outputs[0] != 0 && outputs[1] != 0);
    ASSERT_TRUE(read_chain(&f, outputs, types, reads, 2) != 0);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, known, "- default -", 12));

    run_frame(&f, 16.0f);
    int32_t type = -1;
    read_value(&f, BLOCK_POUT(&f, reads[0], 0), &type, sizeof(type));
    ASSERT_EQ(1, type);
    read_value(&f, BLOCK_POUT(&f, reads[1], 0), &type, sizeof(type));
    ASSERT_EQ(0, type);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_operations, per_second_scales_by_the_frame_delta);
    REGISTER_TEST(engine_operations, random_stays_in_its_bounds_and_repeats_with_the_seed);
    REGISTER_TEST(engine_operations, derived_types_find_the_float_functions);
    REGISTER_TEST(engine_operations, render_context_and_mouse_getters);
    REGISTER_TEST(engine_operations, object_class_group_and_collection_getters);
    REGISTER_TEST(engine_operations, box_and_rectangle_getters);
    REGISTER_TEST(engine_operations, curve_getters);
    REGISTER_TEST(engine_operations, plugin_value_operations_on_mixed_and_compound_types);
    REGISTER_TEST(engine_operations, grids_layer_by_name);
TEST_MAIN_END()
