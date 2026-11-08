#include "engine_fixture.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_ref.h"
#include "../../src/engine/engine_internal.h"

#include <string.h>

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_SET_GRID_PRIORITY NMO_GUID(0x74412551, 0x6146504c)
#define GUID_GET_SQUARE NMO_GUID(0x00a376e0, 0x326609e6)
#define GUID_SET_SQUARE NMO_GUID(0x73a67252, 0x1d5b39c1)
#define GUID_FILL_LAYER NMO_GUID(0x513344bb, 0x6b943d9f)
#define GUID_SWITCH_IF_SQUARE NMO_GUID(0x254074c1, 0x22c0397e)
#define GUID_GET_POS_FROM_VALUE NMO_GUID(0x670616ad, 0x2a5c54ac)
#define GUID_LAYER_SLIDER NMO_GUID(0x7bac6da2, 0x1cbe76ed)
#define GUID_ENTITY_FILL NMO_GUID(0x672f0997, 0x563644cc)
#define GUID_TT_SWITCH_ON_LAYER NMO_GUID(0x1a151160, 0x1be26257)

/* A grid of width x length squares at (x, 0, z), scaled (1, 10, 1) as CKGrid's constructor scales it */
static nmo_object_id_t grid_at(engine_fixture_t *f, const char *name, int32_t width, int32_t length,
                               int32_t priority, float x, float z)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_GRID, name);
    nmo_grid_state_t *state = id != 0 ? (nmo_grid_state_t *)fixture_state(f, id) : NULL;
    if (state == NULL) {
        return 0;
    }
    state->width = width;
    state->length = length;
    state->priority = priority;
    state->has_grid_data = 1;
    state->base.world_matrix[0] = 1.0f;
    state->base.world_matrix[5] = 10.0f;
    state->base.world_matrix[10] = 1.0f;
    state->base.world_matrix[15] = 1.0f;
    state->base.world_matrix[12] = x;
    state->base.world_matrix[14] = z;
    return id;
}

/* A layer of the grid named after its type, with these squares (row by row) */
static nmo_object_id_t layer_of(engine_fixture_t *f, nmo_object_id_t grid, const char *type,
                                const int32_t *squares, size_t count)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_LAYER, type);
    nmo_layer_state_t *state = id != 0 ? (nmo_layer_state_t *)fixture_state(f, id) : NULL;
    nmo_grid_state_t *owner = (nmo_grid_state_t *)fixture_state(f, grid);
    if (state == NULL || owner == NULL) {
        return 0;
    }
    state->has_layer_data = 1;
    state->grid = nmo_ref_from_id(grid);
    state->format = 0;
    state->square_data = nmo_arena_alloc(nmo_session_get_arena(f->session), count * sizeof(int32_t),
                                         _Alignof(int32_t));
    if (state->square_data == NULL) {
        return 0;
    }
    memcpy(state->square_data, squares, count * sizeof(int32_t));
    state->square_data_size = count * sizeof(int32_t);
    state->has_square_data = 1;
    const nmo_grid_layer_t entry = {nmo_ref_from_id(id), NULL};
    return nmo_array_append(&owner->layers, &entry) == NMO_OK ? id : 0;
}

static nmo_object_id_t entity_at(engine_fixture_t *f, uint32_t cid, const char *name, float x, float y, float z)
{
    nmo_object_id_t id = fixture_create(f, cid, name);
    nmo_3dentity_state_t *state = id != 0 ? (nmo_3dentity_state_t *)fixture_state(f, id) : NULL;
    if (state == NULL) {
        return 0;
    }
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    return id;
}

static nmo_object_id_t started(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block != 0) {
        fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    }
    return block;
}

static void input_vector(engine_fixture_t *f, nmo_object_id_t pin, float x, float y, float z)
{
    const float v[3] = {x, y, z};
    fixture_input_value(f, pin, v, sizeof(v));
}

static int32_t output_int(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    int32_t value = -1;
    read_value(f, BLOCK_POUT(f, block, pos), &value, sizeof(value));
    return value;
}

static int32_t square(engine_fixture_t *f, nmo_object_id_t grid_id, const char *type, int32_t x, int32_t y)
{
    eng_grid_t *grid = eng_grid(f->engine, grid_id);
    eng_layer_t *layer = eng_grid_layer(f->engine, grid, eng_grid_type_from_name(f->engine, type));
    return grid != NULL && layer != NULL ? eng_layer_value(grid, layer, x, y) : -1;
}

TEST(engine_grids, squares_come_from_the_preferred_grid)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    /* Low: 4 x 4 at the origin; High: 2 x 2 from x = 2, of a higher priority */
    nmo_object_id_t low = grid_at(&f, "Low", 4, 4, 0, 0.0f, 0.0f);
    nmo_object_id_t high = grid_at(&f, "High", 2, 2, 5, 2.0f, 0.0f);
    int32_t low_water[16] = {0}, low_fire[16] = {0};
    const int32_t high_water[4] = {3, 3, 3, 3};
    low_water[2 * 4 + 1] = 7;
    ASSERT_TRUE(low && high);
    ASSERT_TRUE(layer_of(&f, low, "Water", low_water, 16) != 0);
    ASSERT_TRUE(layer_of(&f, low, "Fire", low_fire, 16) != 0);
    ASSERT_TRUE(layer_of(&f, high, "Water", high_water, 4) != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t both = started(&f, GUID_GET_SQUARE, "Both");
    nmo_object_id_t only_low = started(&f, GUID_GET_SQUARE, "Only Low");
    nmo_object_id_t set = started(&f, GUID_SET_SQUARE, "Set");
    nmo_object_id_t fill = started(&f, GUID_FILL_LAYER, "Fill");
    nmo_object_id_t found = started(&f, GUID_SWITCH_IF_SQUARE, "Found");
    nmo_object_id_t empty = started(&f, GUID_SWITCH_IF_SQUARE, "Empty");
    ASSERT_TRUE(both && only_low && set && fill && found && empty);
    /* The layer types: 1 "- default -", then the layers' names in their order */
    const int32_t water = 2, fire = 3;
    input_vector(&f, BLOCK_PIN(&f, both, 0), 2.5f, 1.0f, 0.5f);
    fixture_input_int(&f, BLOCK_PIN(&f, both, 2), water);
    input_vector(&f, BLOCK_PIN(&f, only_low, 0), 1.5f, 1.0f, 2.5f);
    fixture_input_int(&f, BLOCK_PIN(&f, only_low, 2), water);
    input_vector(&f, BLOCK_PIN(&f, set, 0), 3.5f, 1.0f, 1.5f);
    fixture_input_int(&f, BLOCK_PIN(&f, set, 2), water);
    fixture_input_int(&f, BLOCK_PIN(&f, set, 3), 300);
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 0), fire);
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 1), 9);
    input_vector(&f, BLOCK_PIN(&f, found, 0), 1.5f, 1.0f, 2.5f);
    fixture_input_int(&f, BLOCK_PIN(&f, found, 2), water);
    input_vector(&f, BLOCK_PIN(&f, empty, 0), 0.5f, 1.0f, 0.5f);
    fixture_input_int(&f, BLOCK_PIN(&f, empty, 2), water);
    nmo_object_id_t found_outs[2], empty_outs[2];
    for (size_t i = 0; i < 2; i++) {
        found_outs[i] = fixture_block(&f, GUID_NOP, "Found Out");
        empty_outs[i] = fixture_block(&f, GUID_NOP, "Empty Out");
        fixture_link(&f, BLOCK_OUT(&f, found, i), BLOCK_IN(&f, found_outs[i], 0), 0);
        fixture_link(&f, BLOCK_OUT(&f, empty, i), BLOCK_IN(&f, empty_outs[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(water, eng_grid_type_from_name(f.engine, "Water"));
    ASSERT_EQ(1, eng_grid_type_from_name(f.engine, "- default -"));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(3, output_int(&f, both, 0));     /* in both grids: High's priority wins */
    ASSERT_EQ(7, output_int(&f, only_low, 0)); /* square (1, 2) of Low */
    ASSERT_EQ(255, square(&f, high, "Water", 1, 1)); /* 300 clamped */
    ASSERT_EQ(3, square(&f, high, "Water", 0, 1));
    for (int32_t i = 0; i < 16; i++) {
        ASSERT_EQ(9, square(&f, low, "Fire", i % 4, i / 4));
    }
    ASSERT_EQ(1u, runs(&f, found_outs[1]));
    ASSERT_EQ(0u, runs(&f, found_outs[0]));
    ASSERT_EQ(7, output_int(&f, found, 0));
    ASSERT_EQ(1u, runs(&f, empty_outs[0])); /* None */
    ASSERT_EQ(0u, runs(&f, empty_outs[1]));
    fixture_end(&f);
}

TEST(engine_grids, priority_and_positions_of_a_value)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t grid = grid_at(&f, "Grid", 3, 2, 0, 0.0f, 0.0f);
    nmo_object_id_t other = grid_at(&f, "Other", 3, 2, 1, 0.0f, 0.0f);
    const int32_t values[6] = {0, 5, 0, 5, 0, 5}, others[6] = {1, 1, 1, 1, 1, 1};
    ASSERT_TRUE(grid && other && layer_of(&f, grid, "Mark", values, 6) && layer_of(&f, other, "Mark", others, 6));
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t priority = started(&f, GUID_SET_GRID_PRIORITY, "Priority");
    nmo_object_id_t search = fixture_block(&f, GUID_GET_POS_FROM_VALUE, "Search");
    nmo_object_id_t each = fixture_block(&f, GUID_NOP, "Each");
    nmo_object_id_t done = fixture_block(&f, GUID_NOP, "Done");
    nmo_object_id_t get = fixture_block(&f, GUID_GET_SQUARE, "Get");
    ASSERT_TRUE(priority && search && each && done && get);
    fixture_input_int(&f, fixture_target(&f, priority), (int32_t)grid);
    fixture_input_int(&f, BLOCK_PIN(&f, priority, 0), 2);
    fixture_link(&f, BLOCK_OUT(&f, priority, 0), BLOCK_IN(&f, search, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, priority, 0), BLOCK_IN(&f, get, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, search, 0), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, search, 1), 5);
    fixture_input_int(&f, BLOCK_PIN(&f, search, 2), 1); /* Equal */
    fixture_link(&f, BLOCK_OUT(&f, search, 1), BLOCK_IN(&f, each, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, each, 0), BLOCK_IN(&f, search, 1), 0);
    fixture_link(&f, BLOCK_OUT(&f, search, 0), BLOCK_IN(&f, done, 0), 0);
    input_vector(&f, BLOCK_PIN(&f, get, 0), 1.5f, 1.0f, 0.5f);
    fixture_input_int(&f, BLOCK_PIN(&f, get, 2), 2);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(2, eng_grid(f.engine, grid)->priority);
    ASSERT_EQ(5, output_int(&f, get, 0)); /* Grid now above Other */
    /* The 5s of Grid, then none in Other's layer */
    ASSERT_EQ(3u, runs(&f, each));
    ASSERT_EQ(1u, runs(&f, done));
    float position[3] = {0};
    read_value(&f, BLOCK_POUT(&f, search, 0), position, sizeof(position));
    ASSERT_FLOAT_EQ(2.5f, position[0], 0.0f); /* square (2, 1), in the grid's space */
    ASSERT_FLOAT_EQ(0.5f, position[1], 0.0f);
    ASSERT_FLOAT_EQ(1.5f, position[2], 0.0f);
    ASSERT_EQ((int32_t)grid, output_int(&f, search, 1));
    fixture_end(&f);
}

TEST(engine_grids, layer_slider_keeps_the_entity_off_walls)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t grid = grid_at(&f, "Grid", 3, 3, 0, 0.0f, 0.0f);
    const int32_t walls[9] = {0, 0, 1, 0, 0, 1, 0, 0, 1}; /* column 2 */
    nmo_object_id_t mover = entity_at(&f, NMO_CID_3DENTITY, "Mover", 1.8f, 1.0f, 1.4f);
    ASSERT_TRUE(grid && mover && layer_of(&f, grid, "Wall", walls, 9));
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t slider = started(&f, GUID_LAYER_SLIDER, "Slider");
    nmo_object_id_t contact = fixture_block(&f, GUID_NOP, "Contact");
    nmo_object_id_t free_move = fixture_block(&f, GUID_NOP, "No Contact");
    ASSERT_TRUE(slider && contact && free_move);
    fixture_input_int(&f, fixture_target(&f, slider), (int32_t)mover);
    const float influence = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, slider, 0), &influence, sizeof(influence));
    fixture_input_int(&f, BLOCK_PIN(&f, slider, 1), 2);
    fixture_link(&f, BLOCK_OUT(&f, slider, 0), BLOCK_IN(&f, contact, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, slider, 1), BLOCK_IN(&f, free_move, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* The sphere reached 0.3 into column 2: pushed back to touch it */
    int32_t index = eng_entity_index(f.engine, mover);
    eng_vector_t at = eng_entity_position(f.engine, index, -1);
    ASSERT_FLOAT_EQ(1.5f, at.x, 1e-5f);
    ASSERT_FLOAT_EQ(1.4f, at.z, 1e-6f);
    ASSERT_EQ(1u, runs(&f, contact));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, free_move)); /* touching is no contact */
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, slider));
    fixture_end(&f);
}

TEST(engine_grids, switch_on_layer_and_entity_fill)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t grid = grid_at(&f, "Grid", 4, 4, 0, 0.0f, 0.0f);
    int32_t zone[16] = {0}, solid[16] = {0};
    zone[1 * 4 + 1] = 255;
    nmo_object_id_t mover = entity_at(&f, NMO_CID_3DENTITY, "Mover", 0.5f, 1.0f, 0.5f);
    nmo_object_id_t block = entity_at(&f, NMO_CID_3DOBJECT, "Block", 2.5f, 0.5f, 2.5f);
    nmo_object_id_t mesh = fixture_create(&f, NMO_CID_MESH, "Block Mesh");
    nmo_mesh_state_t *shape = mesh != 0 ? (nmo_mesh_state_t *)fixture_state(&f, mesh) : NULL;
    ASSERT_TRUE(grid && mover && block && shape);
    ASSERT_TRUE(layer_of(&f, grid, "Zone", zone, 16) && layer_of(&f, grid, "Solid", solid, 16));
    shape->vertices = (nmo_vertex_t *)nmo_arena_alloc(nmo_session_get_arena(f.session), 8u * sizeof(nmo_vertex_t),
                                                      _Alignof(nmo_vertex_t));
    ASSERT_NOT_NULL(shape->vertices);
    memset(shape->vertices, 0, 8u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 8; i++) {
        shape->vertices[i].position =
            (nmo_vector_t){(i & 1) ? 0.4f : -0.4f, (i & 2) ? 0.4f : -0.4f, (i & 4) ? 0.4f : -0.4f};
    }
    shape->vertex_count = 8;
    nmo_mesh_update_bounding_volumes(shape);
    ((nmo_3dentity_state_t *)fixture_state(&f, block))->current_mesh = nmo_ref_from_id(mesh);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t layer = started(&f, GUID_TT_SWITCH_ON_LAYER, "Layer");
    nmo_object_id_t fill = started(&f, GUID_ENTITY_FILL, "Fill");
    ASSERT_TRUE(layer && fill);
    fixture_input_int(&f, fixture_target(&f, layer), (int32_t)mover);
    fixture_input_int(&f, BLOCK_PIN(&f, layer, 1), 2); /* Zone */
    fixture_input_int(&f, BLOCK_PIN(&f, layer, 2), 255);
    /* In, Out, Enter, Exit */
    nmo_object_id_t outs[4];
    for (size_t i = 0; i < 4; i++) {
        outs[i] = fixture_block(&f, GUID_NOP, "Output");
        fixture_link(&f, BLOCK_OUT(&f, layer, i), BLOCK_IN(&f, outs[i], 0), 0);
    }
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 0), (int32_t)block);
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 1), 3); /* Solid */
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 2), -1);
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 3), 8);
    fixture_commit(&f, NULL);
    const int32_t box_only = 1;
    const nmo_behavior_state_t *fill_state = (const nmo_behavior_state_t *)fixture_state(&f, fill);
    ASSERT_TRUE(fill_state != NULL && fill_state->local_parameters.count > 0);
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine,
                                                 nmo_behavior_ref_array_get_id(&fill_state->local_parameters, 0),
                                                 &box_only, sizeof(box_only)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    int32_t index = eng_entity_index(f.engine, mover);
    /* In clears Was Inside only in part: outside on the first frame is an Exit, of no move */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outs[3]));
    float exit_vector[3] = {-1.0f, -1.0f, -1.0f};
    read_value(&f, BLOCK_POUT(&f, layer, 0), exit_vector, sizeof(exit_vector));
    ASSERT_FLOAT_EQ(0.0f, exit_vector[0], 0.0f);
    eng_entity_set_position(f.engine, index, &(eng_vector_t){1.5f, 1.0f, 1.5f}, -1, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outs[2])); /* Enter */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outs[0])); /* In */
    eng_entity_set_position(f.engine, index, &(eng_vector_t){2.5f, 1.0f, 1.5f}, -1, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(2u, runs(&f, outs[3]));
    read_value(&f, BLOCK_POUT(&f, layer, 0), exit_vector, sizeof(exit_vector));
    ASSERT_FLOAT_EQ(1.0f, exit_vector[0], 1e-6f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, outs[1])); /* Out */
    /* The block's box, 2.1 to 2.9, covers square (2, 2) only */
    ASSERT_EQ(8, square(&f, grid, "Solid", 2, 2));
    ASSERT_EQ(0, square(&f, grid, "Solid", 3, 2));
    ASSERT_EQ(0, square(&f, grid, "Solid", 1, 2));
    ASSERT_EQ(0, square(&f, grid, "Zone", 2, 2));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_grids, squares_come_from_the_preferred_grid);
REGISTER_TEST(engine_grids, priority_and_positions_of_a_value);
REGISTER_TEST(engine_grids, layer_slider_keeps_the_entity_off_walls);
REGISTER_TEST(engine_grids, switch_on_layer_and_entity_fill);
TEST_MAIN_END()
