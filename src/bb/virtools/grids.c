/**
 * @file grids.c
 * @brief Ports of the basic Grids building blocks.
 *
 * Each function does what the block's function in Grids.dll does. Ballance
 * ships no Grids.dll, so the reference is the Ballanced source of the DLL;
 * the grids and layers are the engine's grid manager's (engine_grid.h).
 * The path finding blocks are not here.
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* GetGridArray (GRID_FLAG_SCENE): the grids of the scene, active or not, in their order */
static size_t scene_grids(nmo_engine_t *engine, eng_grid_t ***out)
{
    size_t total = eng_grid_count(engine), count = 0;
    eng_grid_t **grids = total > 0 ? (eng_grid_t **)malloc(total * sizeof(*grids)) : NULL;
    for (size_t i = 0; grids != NULL && i < total; i++) {
        eng_grid_t *grid = eng_grid_at(engine, i);
        if (grid != NULL && eng_in_scene(engine, grid->id)) {
            grids[count++] = grid;
        }
    }
    *out = grids;
    return count;
}

static int32_t clamp_value(int32_t value)
{
    return value > 255 ? 255 : value < 0 ? 0 : value;
}

/* Set Grid Priority: the target grid's priority. Its input stays active, as the DLL leaves it. */
static int bb_set_grid_priority(blk_t *b)
{
    eng_grid_t *grid = eng_grid(b->engine, nmo_engine_block_target(b));
    if (grid == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    grid->priority = get_in_int(b, 0, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The square of `layer_pin`'s layer under Pos3d (in Ref's space) on the preferred grid;
   NULL when no grid has the point or the grid has no such layer */
static eng_layer_t *square_at(blk_t *b, int layer_pin, eng_grid_t **out_grid, int32_t *x, int32_t *y)
{
    eng_vector_t position = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    int32_t ref = input_entity(b, 1);
    int32_t type = get_in_int(b, layer_pin, 1);
    eng_grid_t *grid = eng_grid_preferred(b->engine, &position, ref);
    eng_layer_t *layer = grid != NULL ? eng_grid_layer(b->engine, grid, type) : NULL;
    if (layer != NULL) {
        eng_vector_t local = eng_grid_local(b->engine, grid, &position, ref);
        *x = (int32_t)local.x;
        *y = (int32_t)local.z;
    }
    *out_grid = grid;
    return layer;
}

/* Get Square From 3D Pos: the value of the square under the point, 0 when there is none. */
static int bb_get_square(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_grid_t *grid = NULL;
    int32_t x = 0, y = 0, value = 0;
    eng_layer_t *layer = square_at(b, 2, &grid, &x, &y);
    if (layer != NULL) {
        value = eng_layer_value(grid, layer, x, y);
    }
    set_out(b, 0, &value, sizeof(value));
    return NMO_ENGINE_BR_OK;
}

/* Set Square From 3D Pos: the square under the point takes Value, in [0, 255]. */
static int bb_set_square(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_grid_t *grid = NULL;
    int32_t x = 0, y = 0;
    int32_t value = get_in_int(b, 3, 255);
    eng_layer_t *layer = square_at(b, 2, &grid, &x, &y);
    if (layer != NULL) {
        eng_layer_set_value(grid, layer, x, y, clamp_value(value));
    }
    return NMO_ENGINE_BR_OK;
}

/* Fill Layer: every square of the layer of each active grid of the scene takes Value. */
static int bb_fill_layer(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t type = get_in_int(b, 0, 1);
    int32_t value = clamp_value(get_in_int(b, 1, 255));
    eng_grid_t **grids = NULL;
    size_t count = scene_grids(b->engine, &grids);
    for (size_t i = 0; i < count; i++) {
        eng_layer_t *layer = eng_grid_active(b->engine, grids[i]) ? eng_grid_layer(b->engine, grids[i], type) : NULL;
        int64_t squares = (int64_t)grids[i]->width * grids[i]->length;
        for (int64_t s = 0; layer != NULL && s < squares && s < layer->count; s++) {
            layer->squares[s] = value;
        }
    }
    free(grids);
    return NMO_ENGINE_BR_OK;
}

/*
 * Switch If Square: on the preferred grid under the point, each Layer input
 * (from the last) gives its square's value to its Value output and, when
 * the value is not 0, activates its output; None when none did.
 */
static int bb_switch_if_square(blk_t *b)
{
    IN_OFF(b, 0);
    eng_vector_t position = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    int32_t ref = input_entity(b, 1);
    eng_grid_t *grid = eng_grid_preferred(b->engine, &position, ref);
    bool none = true;
    if (grid != NULL) {
        eng_vector_t local = eng_grid_local(b->engine, grid, &position, ref);
        int32_t x = (int32_t)local.x, y = (int32_t)local.z;
        int count = nmo_engine_block_input_parameter_count(b) - 2;
        while (count > 0) {
            --count;
            eng_layer_t *layer = eng_grid_layer(b->engine, grid, get_in_int(b, count + 2, 1));
            if (layer != NULL) {
                int32_t value = eng_layer_value(grid, layer, x, y);
                set_out(b, count, &value, sizeof(value));
                if (value != 0) {
                    OUT_ON(b, count + 1);
                    none = false;
                }
            }
        }
    }
    if (none) {
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Get Pos From Value's search (its local): the next square, the grid, and the test */
typedef struct pos_from_value {
    int32_t a, b, grid;
    int32_t op; /* CK_COMPOPERATOR: 1 Equal .. 6 Greater or equal */
} pos_from_value_t;

static bool compare_square(int32_t op, int32_t square, int32_t value)
{
    switch (op) {
    case 1: return square == value;
    case 2: return square != value;
    case 3: return square < value;
    case 4: return square <= value;
    case 5: return square > value;
    case 6: return square >= value;
    default: return false; /* the DLL calls through a table it reads out of */
    }
}

/*
 * Get Pos From Value: In starts a search of the active grids of the scene,
 * row by row, for the squares of the layer that pass the test against
 * Value; each found gives its centre (in the grid's space) and the grid,
 * and Loop Out; Loop In goes on from the next. Out when there are no more.
 */
static int bb_get_pos_from_value(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t type = get_in_int(b, 0, 1);
    int32_t value = get_in_int(b, 1, 255);
    pos_from_value_t search = {0, 0, 0, 1};
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        search.op = get_in_int(b, 2, 1);
        nmo_engine_block_set_local_value(b, 0, &search, sizeof(search));
    } else {
        IN_OFF(b, 1);
    }
    const eng_param_t *local = block_local(b, 0);
    if (local != NULL && local->size >= sizeof(search)) {
        memcpy(&search, local->data, sizeof(search));
    }
    eng_grid_t **grids = NULL;
    int32_t count = (int32_t)scene_grids(engine, &grids);
    for (int32_t c = search.grid; c < count; c++) {
        eng_grid_t *grid = grids[c];
        eng_layer_t *layer = eng_grid_active(engine, grid) ? eng_grid_layer(engine, grid, type) : NULL;
        if (layer != NULL) {
            int32_t w = grid->width, l = grid->length;
            int32_t a = search.a;
            for (int32_t row = search.b; row < l; ++row, a = 0) {
                for (; a < w; ++a) {
                    if (compare_square(search.op, eng_layer_value(grid, layer, a, row), value)) {
                        search.a = a + 1;
                        search.b = row;
                        search.grid = c;
                        const float position[3] = {(float)a + 0.5f, 0.5f, (float)row + 0.5f};
                        set_out(b, 0, position, sizeof(position));
                        set_out(b, 1, &grid->id, sizeof(grid->id));
                        nmo_engine_block_set_local_value(b, 0, &search, sizeof(search));
                        free(grids);
                        OUT_ON(b, 1);
                        return NMO_ENGINE_BR_OK;
                    }
                }
            }
        }
        search.a = search.b = 0;
    }
    free(grids);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* A_Calculate_Ellipse_Vector: how far (x, y) is inside the ellipse of radii r1, r2 */
static bool ellipse_vector(float x, float y, float r1, float r2, float *out_x, float *out_y)
{
    float x_over_y = x / y;
    float ratio = r1 / r2;
    float ey = r1 / sqrtf(x_over_y * x_over_y + ratio * ratio);
    if (ey <= y) {
        return false;
    }
    *out_x = ey * x_over_y - x;
    *out_y = ey - y;
    return true;
}

static float row_length(const eng_matrix_t *m, int row)
{
    return sqrtf(m->m[row][0] * m->m[row][0] + m->m[row][1] * m->m[row][1] + m->m[row][2] * m->m[row][2]);
}

/* Layer Slider's locals, in the DLL's order */
#define SLIDER_REACTION 0
#define SLIDER_CONTACTS 1
#define SLIDER_OLD_POSITION 2
#define SLIDER_ACCURACY 3

/*
 * Layer Slider: the target is kept out of the squares that are not 0 on
 * the layers to slide on, a sphere of Influence Radius (scaled by its
 * radius, except for a sprite or a plain entity; from the front plane and
 * field of view for a camera) pushed back to the edges of the squares it
 * enters, or round the corner of a square. Contact when it was pushed, with
 * the sum of the pushes (Reaction) and where it touched (contact outputs);
 * No Contact otherwise. Accuracy steps from the last position in up to that
 * many moves. Each frame until Off.
 */
static int bb_layer_slider(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t old_position = {0.0f, 0.0f, 0.0f};
    const eng_param_t *local = block_local(b, SLIDER_OLD_POSITION);
    if (local != NULL && local->size >= sizeof(old_position)) {
        memcpy(&old_position, local->data, sizeof(old_position));
    }
    eng_vector_t init_position = eng_entity_position(engine, entity, -1);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        old_position = init_position;
        nmo_engine_block_set_local_value(b, SLIDER_OLD_POSITION, &old_position, sizeof(old_position));
    }
    eng_grid_t **grids = NULL;
    size_t grid_count = scene_grids(engine, &grids);
    if (grid_count == 0) {
        free(grids);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t contact_count = get_local_int(b, SLIDER_CONTACTS, 0);
    int32_t index_contact = 0;
    bool reaction = true;
    if (contact_count == 0) {
        reaction = get_local_int(b, SLIDER_REACTION, 0) != 0;
    }
    float influence = get_in_float(b, 0, 0.5f);
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t class_id = nmo_engine_object_class(engine, target);
    if (class_id == NMO_CID_CAMERA || class_id == NMO_CID_TARGETCAMERA) {
        float fov = 0.0f, front = 0.0f, back = 0.0f;
        eng_camera_fov(engine, target, &fov);
        eng_camera_planes(engine, target, &front, &back);
        influence = front * acosf(fov * 0.5f) * 1.2f * influence;
        if (influence < 0.5f) {
            influence = 0.5f;
        }
    } else if (class_id != NMO_CID_SPRITE3D && class_id != NMO_CID_3DENTITY) {
        influence *= eng_entity_radius(engine, entity);
    }
    int32_t precision = get_local_int(b, SLIDER_ACCURACY, 0);
    if (precision < 0) {
        precision = 0;
    }
    int32_t steps = precision;
    eng_vector_t step = {0.0f, 0.0f, 0.0f}, current;
    if (precision != 0) {
        eng_vector_t moved = {init_position.x - old_position.x, init_position.y - old_position.y,
                              init_position.z - old_position.z};
        steps = (int32_t)(sqrtf(moved.x * moved.x + moved.y * moved.y + moved.z * moved.z) / (influence - 0.1f));
        if (steps > precision) {
            steps = precision;
        }
        float parts = (float)steps + 1.0f;
        step = (eng_vector_t){moved.x / parts, moved.y / parts, moved.z / parts};
        current = old_position;
    } else {
        current = init_position;
    }
    int input_total = nmo_engine_block_input_parameter_count(b);
    bool contact = false;
    eng_vector_t total = {0.0f, 0.0f, 0.0f}, t = {0.0f, 0.0f, 0.0f}, is = {0.0f, 0.0f, 0.0f};
    bool has_parent = false;
    do {
        if (precision != 0) {
            current.x += step.x;
            current.y += step.y;
            current.z += step.z;
            eng_entity_set_position(engine, entity, &current, -1, false);
        }
        for (size_t g = 0; g < grid_count; g++) {
            eng_grid_t *grid = grids[g];
            int32_t grid_entity = eng_entity_index(engine, grid->id);
            if (!eng_grid_active(engine, grid) || grid_entity < 0) {
                continue;
            }
            bool have_position = false, have_y = false, have_x = false, have_z = false;
            eng_vector_t pos = {0.0f, 0.0f, 0.0f};
            for (int input = 1; input < input_total; ++input) {
                eng_layer_t *layer = eng_grid_layer(engine, grid, get_in_int(b, input, 0));
                if (layer == NULL) {
                    continue;
                }
                if (!have_position) {
                    pos = eng_grid_local(engine, grid, &current, -1);
                    have_position = true;
                }
                const eng_matrix_t *world = &engine->entities.data[grid_entity].world;
                if (!have_y) {
                    has_parent = engine->entities.data[grid_entity].parent >= 0;
                    if (has_parent) {
                        is.y = row_length(world, 1);
                    } else {
                        is = eng_entity_scale(engine, grid_entity, true);
                    }
                    is.y = influence / is.y;
                    have_y = true;
                }
                if (!(pos.y >= -is.y && pos.y < 1.0f + is.y)) {
                    continue;
                }
                int32_t w = grid->width;
                if (!have_x) {
                    if (has_parent) {
                        is.x = row_length(world, 0);
                    }
                    is.x = influence / is.x;
                    have_x = true;
                }
                float min_x = pos.x - is.x, max_x = pos.x + is.x;
                if (!(min_x < (float)w && max_x >= 0.0f)) {
                    continue;
                }
                int32_t l = grid->length;
                if (!have_z) {
                    if (has_parent) {
                        is.z = row_length(world, 2);
                    }
                    is.z = influence / is.z;
                    have_z = true;
                }
                float min_z = pos.z - is.z, max_z = pos.z + is.z;
                if (!(min_z < (float)l && max_z >= 0.0f)) {
                    continue;
                }
                int32_t x = pos.x >= 0.0f ? (int32_t)pos.x : (int32_t)(pos.x - 1.0f);
                int32_t y = pos.z >= 0.0f ? (int32_t)pos.z : (int32_t)(pos.z - 1.0f);
                /* how far it reaches into the next column or row; <= 0 is into it */
                float p1 = 1.0f, p3 = 1.0f, p5 = 1.0f, p7 = 1.0f;
                if (min_x >= 0.0f) {
                    p7 = min_x - (float)x;
                }
                if (max_x < (float)w) {
                    p3 = (float)(x + 1) - max_x;
                }
                if (min_z >= 0.0f) {
                    p5 = min_z - (float)y;
                }
                if (max_z < (float)l) {
                    p1 = (float)(y + 1) - max_z;
                }
                int flags = 0;
                t.x = 0.0f;
                t.z = 0.0f;
                if (p7 <= 0.0f) {
                    if (y >= 0 && y < l && eng_layer_value(grid, layer, x - 1, y) != 0) {
                        t.x = -p7;
                    } else {
                        flags |= 0x0001;
                    }
                } else if (p3 <= 0.0f) {
                    if (y >= 0 && y < l && eng_layer_value(grid, layer, x + 1, y) != 0) {
                        t.x = p3;
                    } else {
                        flags |= 0x0010;
                    }
                }
                if (p1 <= 0.0f) {
                    if (x >= 0 && x < w && eng_layer_value(grid, layer, x, y + 1) != 0) {
                        t.z = p1;
                    } else {
                        flags |= 0x0100;
                    }
                } else if (p5 <= 0.0f) {
                    if (x >= 0 && x < w && eng_layer_value(grid, layer, x, y - 1) != 0) {
                        t.z = -p5;
                    } else {
                        flags |= 0x1000;
                    }
                }
                float ex = 0.0f, ey = 0.0f;
                switch (flags) {
                case 0x0101: /* left, top */
                    if (eng_layer_value(grid, layer, x - 1, y + 1) != 0 &&
                        ellipse_vector(pos.x - (float)x, (float)(y + 1) - pos.z, is.x, is.z, &ex, &ey)) {
                        t.x = ex;
                        t.z = -ey;
                    }
                    break;
                case 0x1001: /* left, bottom */
                    if (eng_layer_value(grid, layer, x - 1, y - 1) != 0 &&
                        ellipse_vector(pos.x - (float)x, pos.z - (float)y, is.x, is.z, &ex, &ey)) {
                        t.x = ex;
                        t.z = ey;
                    }
                    break;
                case 0x0110: /* right, top */
                    if (eng_layer_value(grid, layer, x + 1, y + 1) != 0 &&
                        ellipse_vector((float)(x + 1) - pos.x, (float)(y + 1) - pos.z, is.x, is.z, &ex, &ey)) {
                        t.x = -ex;
                        t.z = -ey;
                    }
                    break;
                case 0x1010: /* right, bottom */
                    if (eng_layer_value(grid, layer, x + 1, y - 1) != 0 &&
                        ellipse_vector((float)(x + 1) - pos.x, pos.z - (float)y, is.x, is.z, &ex, &ey)) {
                        t.x = -ex;
                        t.z = ey;
                    }
                    break;
                default:
                    break;
                }
                if (t.x != 0.0f || t.z != 0.0f) {
                    eng_entity_translate(engine, entity, &t, grid_entity, false);
                    contact = true;
                    if (reaction) {
                        eng_entity_transform_vector(engine, grid_entity, &t, &t, -1, false);
                        total.x += t.x;
                        total.y += t.y;
                        total.z += t.z;
                    }
                    if (index_contact < contact_count) {
                        index_contact++;
                        eng_vector_normalize(&t);
                        t.x *= influence;
                        t.y *= influence;
                        t.z *= influence;
                        eng_vector_t at = eng_entity_position(engine, entity, -1);
                        at.x -= t.x;
                        at.y -= t.y;
                        at.z -= t.z;
                        set_out(b, index_contact, &at, sizeof(at));
                    }
                    steps = 0;
                }
            }
        }
    } while (steps-- > 0);
    free(grids);
    if (precision != 0) {
        old_position = eng_entity_position(engine, entity, -1);
        nmo_engine_block_set_local_value(b, SLIDER_OLD_POSITION, &old_position, sizeof(old_position));
    }
    if (contact) {
        OUT_ON(b, 0);
        if (reaction) {
            set_out(b, 0, &total, sizeof(total));
        }
    } else {
        OUT_ON(b, 1);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* The grid's world box: its local box, 0 to its size and a unit high, transformed */
static void grid_world_box(nmo_engine_t *engine, const eng_grid_t *grid, eng_vector_t *min, eng_vector_t *max)
{
    int32_t index = eng_entity_index(engine, grid->id);
    for (int corner = 0; corner < 8; corner++) {
        eng_vector_t local = {(corner & 1) ? (float)grid->width : 0.0f, (corner & 2) ? 1.0f : 0.0f,
                              (corner & 4) ? (float)grid->length : 0.0f};
        eng_vector_t world = local;
        if (index >= 0) {
            eng_entity_transform(engine, index, &world, &local, -1);
        }
        if (corner == 0) {
            *min = *max = world;
            continue;
        }
        min->x = fminf(min->x, world.x);
        min->y = fminf(min->y, world.y);
        min->z = fminf(min->z, world.z);
        max->x = fmaxf(max->x, world.x);
        max->y = fmaxf(max->y, world.y);
        max->z = fmaxf(max->z, world.z);
    }
}

static bool boxes_meet(const eng_vector_t *min1, const eng_vector_t *max1, const eng_vector_t *min2,
                       const eng_vector_t *max2)
{
    return min1->x <= max2->x && max1->x >= min2->x && min1->y <= max2->y && max1->y >= min2->y &&
           min1->z <= max2->z && max1->z >= min2->z;
}

/* Get2dCoordsFrom3dPos of the corners of a world box, as squares */
static void box_squares(nmo_engine_t *engine, const eng_grid_t *grid, const eng_vector_t *min,
                        const eng_vector_t *max, int32_t tx[8], int32_t ty[8])
{
    for (int corner = 0; corner < 8; corner++) {
        eng_vector_t point = {(corner & 4) ? max->x : min->x, (corner & 2) ? max->y : min->y,
                              (corner & 1) ? max->z : min->z};
        eng_vector_t local = eng_grid_local(engine, grid, &point, -1);
        tx[corner] = (int32_t)local.x;
        ty[corner] = (int32_t)local.z;
    }
}

static int32_t clamp_square(int32_t v, int32_t size)
{
    return v < 0 ? 0 : v >= size ? size - 1 : v;
}

/* FillBoundingBox: the squares under the box, on each active grid of the scene it meets */
static void fill_bounding_box(nmo_engine_t *engine, eng_grid_t **grids, size_t count, const eng_vector_t *min,
                              const eng_vector_t *max, int32_t type, int32_t value)
{
    for (size_t i = 0; i < count; i++) {
        eng_grid_t *grid = grids[i];
        eng_vector_t grid_min, grid_max;
        grid_world_box(engine, grid, &grid_min, &grid_max);
        if (!eng_grid_active(engine, grid) || !boxes_meet(min, max, &grid_min, &grid_max)) {
            continue;
        }
        int32_t left = grid->width - 1, right = 0, top = grid->length - 1, bottom = 0;
        int32_t tx[8], ty[8];
        box_squares(engine, grid, min, max, tx, ty);
        for (int c = 0; c < 8; c++) {
            left = tx[c] < left ? tx[c] : left;
            right = tx[c] > right ? tx[c] : right;
            top = ty[c] < top ? ty[c] : top;
            bottom = ty[c] > bottom ? ty[c] : bottom;
        }
        left = clamp_square(left, grid->width);
        right = clamp_square(right, grid->width);
        top = clamp_square(top, grid->length);
        bottom = clamp_square(bottom, grid->length);
        eng_layer_t *layer = type != -1 ? eng_grid_layer(engine, grid, type) : NULL;
        for (int32_t x = left; layer != NULL && x <= right; ++x) {
            for (int32_t y = top; y <= bottom; ++y) {
                eng_layer_set_value(grid, layer, x, y, value);
            }
        }
    }
}

/*
 * FillGridWithObjectShape: on each active grid of the scene the entity's box
 * meets, the squares round its box (one more on each side, two more after)
 * whose centre's upward ray in the grid hits the entity within a unit are
 * its surface (the solid layer); the squares beside them its outline (the
 * shape layer). The DLL's table runs a row one wider than the squares.
 */
static void fill_shape(nmo_engine_t *engine, int32_t entity, eng_grid_t **grids, size_t count,
                       const eng_vector_t *min, const eng_vector_t *max, int32_t solid, int32_t shape,
                       int32_t value)
{
    for (size_t i = 0; i < count; i++) {
        eng_grid_t *grid = grids[i];
        eng_vector_t grid_min, grid_max;
        grid_world_box(engine, grid, &grid_min, &grid_max);
        int32_t grid_entity = eng_entity_index(engine, grid->id);
        if (!eng_grid_active(engine, grid) || grid_entity < 0 || !boxes_meet(min, max, &grid_min, &grid_max)) {
            continue;
        }
        float min_x = (float)grid->width, min_y = (float)grid->length, max_x = 0.0f, max_y = 0.0f;
        int32_t tx[8], ty[8];
        box_squares(engine, grid, min, max, tx, ty);
        for (int c = 0; c < 8; c++) {
            min_x = tx[c] < (int32_t)min_x ? (float)tx[c] : min_x;
            max_x = tx[c] > (int32_t)max_x ? (float)tx[c] : max_x;
            min_y = ty[c] < (int32_t)min_y ? (float)ty[c] : min_y;
            max_y = ty[c] > (int32_t)max_y ? (float)ty[c] : max_y;
        }
        float width = (float)grid->width, length = (float)grid->length;
        min_x--;
        min_y--;
        max_x += 2.0f;
        max_y += 2.0f;
        min_x = min_x < 0.0f ? 0.0f : min_x >= width ? width - 1.0f : min_x;
        max_x = max_x < 0.0f ? 0.0f : max_x >= width ? width - 1.0f : max_x;
        min_y = min_y < 0.0f ? 0.0f : min_y >= length ? length - 1.0f : min_y;
        max_y = max_y < 0.0f ? 0.0f : max_y >= length ? length - 1.0f : max_y;
        int32_t columns = (int32_t)(max_x - min_x + 1.0f), rows = (int32_t)(max_y - min_y + 1.0f);
        if (columns <= 0 || rows <= 0) {
            continue;
        }
        size_t cells = (size_t)rows * (size_t)(columns + 1) + 1u;
        unsigned char *table = (unsigned char *)calloc(cells, 1u);
        if (table == NULL) {
            continue;
        }
        size_t at = 0;
        float iz = min_y + 0.5f;
        for (int32_t r = (int32_t)min_y; (float)r <= max_y; r++, iz += 1.0f, at++) {
            float ix = min_x + 0.5f;
            for (int32_t c = (int32_t)min_x; (float)c <= max_x; c++, ix += 1.0f, at++) {
                eng_vector_t from_local = {ix, 0.0f, iz}, to_local = {ix, 1.0f, iz}, from, to;
                eng_entity_transform(engine, grid_entity, &from, &from_local, -1);
                eng_entity_transform(engine, grid_entity, &to, &to_local, -1);
                eng_ray_hit_t hit;
                if (eng_entity_ray_intersection(engine, entity, &from, &to, &hit) > 0) {
                    eng_vector_t world, in_grid;
                    eng_entity_transform(engine, entity, &world, &hit.point, -1);
                    in_grid = eng_grid_local(engine, grid, &world, -1);
                    float dx = in_grid.x - ix, dy = in_grid.y, dz = in_grid.z - iz;
                    if (sqrtf(dx * dx + dy * dy + dz * dz) < 1.0f) {
                        table[at] = 1;
                    }
                }
            }
        }
        eng_layer_t *layer = solid != -1 ? eng_grid_layer(engine, grid, solid) : NULL;
        at = 0;
        for (int32_t r = (int32_t)min_y; layer != NULL && (float)r <= max_y; r++, at++) {
            for (int32_t c = (int32_t)min_x; (float)c <= max_x; c++, at++) {
                if (table[at] == 1) {
                    eng_layer_set_value(grid, layer, c, r, value);
                }
            }
        }
        layer = shape != -1 ? eng_grid_layer(engine, grid, shape) : NULL;
        size_t stride = (size_t)columns + 1u;
        at = 0;
        for (int32_t r = (int32_t)min_y; layer != NULL && (float)r <= max_y; r++, at++) {
            for (int32_t c = (int32_t)min_x; (float)c <= max_x; c++, at++) {
                if (table[at] != 0) {
                    continue;
                }
                if (((float)c < max_x && table[at + 1] == 1) || ((float)c > min_x && table[at - 1] == 1)) {
                    eng_layer_set_value(grid, layer, c, r, value);
                }
                if (((float)r < max_y && at + stride < cells && table[at + stride] == 1) ||
                    ((float)r > min_y && at >= stride && table[at - stride] == 1)) {
                    eng_layer_set_value(grid, layer, c, r, value);
                }
            }
        }
        free(table);
    }
}

/* 3D Entity Fill: the Object's box (Only Bounding Box), or its shape, written into the layers. */
static int bb_entity_fill(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t entity = input_entity(b, 0);
    if (entity < 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t solid = get_in_int(b, 1, -1);
    int32_t shape = get_in_int(b, 2, -1);
    int32_t value = get_in_int(b, 3, 255);
    eng_vector_t min, max;
    if (!eng_entity_world_box(engine, entity, &min, &max) || min.x > max.x || min.y > max.y || min.z > max.z) {
        return NMO_ENGINE_BR_OK;
    }
    eng_grid_t **grids = NULL;
    size_t count = scene_grids(engine, &grids);
    if (get_local_int(b, 0, 0) != 0) {
        fill_bounding_box(engine, grids, count, &min, &max, solid, value);
    } else {
        fill_shape(engine, entity, grids, count, &min, &max, solid, shape, value);
    }
    free(grids);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_grids_blocks[] = {
    {NMO_GUID_INIT(0x74412551, 0x6146504c), "Set Grid Priority", bb_set_grid_priority, NULL, NULL},
    {NMO_GUID_INIT(0x00a376e0, 0x326609e6), "Get Square From 3D Pos", bb_get_square, NULL, NULL},
    {NMO_GUID_INIT(0x73a67252, 0x1d5b39c1), "Set Square From 3D Pos", bb_set_square, NULL, NULL},
    {NMO_GUID_INIT(0x513344bb, 0x6b943d9f), "Fill Layer", bb_fill_layer, NULL, NULL},
    {NMO_GUID_INIT(0x254074c1, 0x22c0397e), "Switch If Square", bb_switch_if_square, NULL, NULL},
    {NMO_GUID_INIT(0x670616ad, 0x2a5c54ac), "Get Pos From Value", bb_get_pos_from_value, NULL, NULL},
    {NMO_GUID_INIT(0x7bac6da2, 0x1cbe76ed), "Layer Slider", bb_layer_slider, NULL, NULL},
    {NMO_GUID_INIT(0x672f0997, 0x563644cc), "3D Entity Fill", bb_entity_fill, NULL, NULL},
};

nmo_status_t eng_register_grids(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_grids_blocks);
}
