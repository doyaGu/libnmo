/**
 * @file grid_path.c
 * @brief Ports of the Grids path finding blocks and the grid path manager
 *        they share (GridPathManager of Grids.dll).
 *
 * Ballance ships no Grids.dll, so the reference is the Ballanced source of
 * the DLL. Grid Path Init builds the graph: the linkers (the squares of the
 * layers whose type's parameter is the linker graph enum: teleporters 1-3
 * and doors 4) and, for each layer to search on, the cost between the
 * linkers of a grid. Grid Path Solver registers a problem that the manager
 * solves before the behaviors of each frame: A* over the squares of a grid
 * (the obstacle layer's values over the threshold are walls, the others slow
 * the way by their value), and over the linkers between grids. Character
 * Grid Path Follow then steers a character along a found path with the
 * Joy_Up, Joy_Left and Joy_Right messages, keeping it off the walls and the
 * other followers.
 *
 * What it asks the machine for comes from a fixed machine that is never out
 * of time: a search ends in the frame that starts it (Ms/Frame is not a
 * limit), and the follower's timers count the engine's time. An object
 * destroyed ends the problems it is in at the next frame (the DLL's
 * SequenceToBeDeleted). The curve path is not coloured.
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_type_guids.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CKPGUID_LINKERGRAPH_ENUM NMO_GUID(0x16a90c1bu, 0x740d0f12u)
#define DISTANCE_JUXTAPOSE 1.0f
#define DISTANCE_DIAGONAL 1.414f
#define RAND_MAX_MSVC 32767.0f

typedef enum { TC_NORMAL, TC_TELE1, TC_TELE2, TC_TELE3, TC_DOOR } type_case_t;

typedef enum {
    FS_DONTEXIST,
    FS_ON,
    FS_SLOW,
    FS_TIMEOUT,
    FS_OFF,
    FS_ENTERTELE,
    FS_TRAVELTELE,
    FS_EXITTELE,
    FS_FINISH
} follow_state_t;

typedef struct gp_node gp_node_t;
typedef struct gp_linker gp_linker_t;

/* ContextInfo: a node's A* state in one problem's search */
typedef struct grid_ctx {
    float cost, layer_cost, f;
    bool closed, in_open;
    gp_node_t *parent;
} grid_ctx_t;

typedef struct linker_ctx {
    float cost, layer_cost, f;
    bool closed, in_open;
    gp_linker_t *parent;
} linker_ctx_t;

/* NodeGrid: a square (by index, shared by every grid), and the linker each grid has there */
struct gp_node {
    int32_t case_index;
    int32_t layer_value;
    grid_ctx_t *ctx;
    gp_linker_t **linker_obs;
};

/* IndoorLink: the linkers of the same grid one can walk to on a layer, and what it costs */
typedef struct indoor_link {
    int32_t layer;
    ENG_VEC(gp_linker_t *) same;
    ENG_VEC(float) cost;
    ENG_VEC(int32_t) layer_cost;
    ENG_VEC(int32_t) max_layer_cost;
} indoor_link_t;

/* NodeLinker: a teleporter or door square, or a problem's start or end (linker IDs -1, -2) */
struct gp_linker {
    nmo_object_id_t grid;
    int32_t x, y;
    int32_t linker_id;
    type_case_t type;
    bool has_pos;
    eng_vector_t pos;
    ENG_VEC(gp_linker_t *) connected;
    int32_t layer_count;
    indoor_link_t *indoor;
    int32_t context_count;
    linker_ctx_t *ctx;
    gp_linker_t **dyn;
    float *dyn_cost;
    int32_t attribute;
    bool occupy;
};

typedef struct grid_info {
    nmo_object_id_t grid;
    ENG_VEC(gp_linker_t *) linkers;
    int32_t num;
} grid_info_t;

typedef struct sub_path {
    nmo_object_id_t grid;
    int32_t num_grid;
    gp_linker_t *linker_start, *linker_end;
    ENG_VEC(int32_t) cases;
    float cost;
    bool inversed;
    int32_t path_fid;
} sub_path_t;

typedef ENG_VEC(sub_path_t *) sub_list_t;

typedef struct gp_path {
    sub_list_t subs;
    eng_vector_t last_point;
    float cost;
    bool inversed;
    bool linker_obs;
    int32_t obstacle_layer, obstacle_threshold;
} gp_path_t;

/* An open list, highest F first: the next node is the last */
typedef struct open_list {
    ENG_VEC(void *) items;
    int32_t context;
    bool linkers;
} open_list_t;

typedef struct gp gp_t;
typedef struct path_problem path_problem_t;
typedef int (*state_fn)(gp_t *, path_problem_t *, int32_t);

struct path_problem {
    gp_t *gp;
    int32_t state; /* 1 searching, 2 found, 3 not found */
    state_fn function;
    nmo_object_id_t target;
    eng_vector_t target_pos, goal_pos;
    int32_t obstacle_layer, obstacle_threshold;
    float slowing_factor;
    bool linker, linker_obs;
    float heuristic_coef;
    int32_t heuristic;
    bool diagonal, optimize;
    float slowing_div_threshold;
    open_list_t open_linkers, open_grid;
    nmo_object_id_t grid_start, grid_end, grid_search;
    int32_t x_start_search, y_start_search, x_end_search, y_end_search, end_index;
    gp_linker_t *dyn_start, *dyn_end;
    gp_node_t *node_end; /* (gp_node_t *)1 stands for "none yet" as in the DLL */
    gp_linker_t *linker_end;
    int32_t it_linker, it_linker_end; /* in grid_info linkers */
    grid_info_t *it_info;
    bool start_part;
    gp_linker_t *linker1, *linker2;
    sub_list_t hyp_start;
    sub_list_t hyp_end;
    sub_path_t *sub_start, *sub_end;
    sub_path_t *hyp_start_to_end;
    float hyp_start_to_end_cost;
    gp_path_t path;
    int32_t follow_path_id;
    int32_t successor_index[12];
    const int32_t *square;
    int32_t square_count;
    int32_t grid_w, grid_l;
    int32_t num_grid;
};

typedef struct follow_problem follow_problem_t;
typedef void (*follow_fn)(gp_t *, follow_problem_t *);

struct follow_problem {
    nmo_object_id_t target;
    int32_t path_id;
    float min_distance_factor, limit_angle_cos, fuzzyness, fuzzyness2;
    bool pingpong;
    float max_blocked_time, distance_blocked;
    int32_t collision_filter;
    float radius_collision, radius_avoid;
    float enter_tele_time, travel_tele_time, exit_tele_time;
    int32_t node; /* the behavior's */
    bool manage_teleporter;
    follow_state_t state;
    follow_fn function, align, move;
    bool invisible, linker_obs;
    int32_t test_turn_angle_count;
    nmo_object_id_t charac;
    gp_path_t path;
    int32_t num_sub_path;
    sub_path_t *sub, *next_sub;
    int32_t num_point, total_point;
    nmo_object_id_t grid;
    int32_t layer_type;
    int32_t w, l;
    eng_vector_t old_position, position, real_position, dir, real_dir, dir1, dir2, dir_fp;
    eng_vector_t next_point, next_point_fuzzy, grid_scale, pos_to_move_from;
    float distance_to_point, radius_cgrid;
    int32_t next_x, next_y;
    float min_distance, distance;
    type_case_t old_type_case;
    double time_tele, time_blocked;
};

typedef struct id_index {
    int32_t key;
    int32_t value;
} id_index_t;

typedef struct path_entry {
    int32_t id;
    gp_path_t *path;
} path_entry_t;

/* GridPathManager */
struct gp {
    nmo_engine_t *engine;
    bool reseted, graph_done;
    ENG_VEC(id_index_t) target_contexts; /* target -> problem */
    int32_t num_layer, max_case;
    ENG_VEC(path_problem_t *) problems;
    ENG_VEC(int32_t) free_paths; /* a queue: taken from the front */
    int32_t max_path, num_path;
    ENG_VEC(path_entry_t) paths; /* the paths given to followers, by path ID */
    int32_t follow_path_id;
    ENG_VEC(id_index_t) follow_contexts; /* path ID -> follower */
    ENG_VEC(follow_problem_t *) follows;
    ENG_VEC(int32_t) free_follows;
    int32_t max_follow, num_follow;
    ENG_VEC(gp_linker_t *) linkers;
    ENG_VEC(grid_info_t) infos;
    int32_t *layer_index;
    int32_t layer_index_count;
    gp_node_t *nodes;
    int32_t node_count;
    int32_t successor_id[13];
    float successor_cost[12];
    int32_t successor_layer_cost[12];
};

static const float successor_index_cost[12] = {DISTANCE_DIAGONAL, DISTANCE_JUXTAPOSE, DISTANCE_DIAGONAL,
                                               DISTANCE_JUXTAPOSE, DISTANCE_DIAGONAL, DISTANCE_JUXTAPOSE,
                                               DISTANCE_DIAGONAL, DISTANCE_JUXTAPOSE, DISTANCE_DIAGONAL,
                                               DISTANCE_JUXTAPOSE, DISTANCE_DIAGONAL, DISTANCE_JUXTAPOSE};
static const float successor_index_cost2[6] = {DISTANCE_JUXTAPOSE, DISTANCE_JUXTAPOSE, DISTANCE_JUXTAPOSE,
                                               DISTANCE_JUXTAPOSE, DISTANCE_JUXTAPOSE, DISTANCE_JUXTAPOSE};

/* ============================================================================
 * Engine services
 * ============================================================================ */

static int32_t attribute_type_named(const nmo_engine_t *engine, const char *name)
{
    for (size_t t = 0; name != NULL && t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, name) == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

static bool has_attribute(const nmo_engine_t *engine, nmo_object_id_t object, int32_t type)
{
    return type >= 0 && eng_attribute_find(engine, object, type) != NULL;
}

static int32_t attribute_count(const nmo_engine_t *engine, nmo_object_id_t object)
{
    int32_t count = 0;
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        count += engine->attributes.values.data[i].object == object ? 1 : 0;
    }
    return count;
}

/* A field of a structure attribute (as its bits); false for none */
static bool attribute_field(nmo_engine_t *engine, nmo_object_id_t object, int32_t type, uint32_t field,
                            int32_t *out)
{
    const eng_attribute_t *attribute = eng_attribute_find(engine, object, type);
    if (attribute == NULL) {
        return false;
    }
    if (attribute->fields != NULL) {
        if (field < attribute->field_count) {
            *out = attribute->fields[field];
        }
        return true;
    }
    if (attribute->has_value) {
        eng_struct_field(engine, object, attribute->value.data, attribute->value.size, field, out);
    }
    return true;
}

static float bits_float(int32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int32_t entity_of(nmo_engine_t *engine, nmo_object_id_t id)
{
    return eng_entity_index(engine, id);
}

static eng_vector_t grid_to_world(nmo_engine_t *engine, nmo_object_id_t grid, eng_vector_t local)
{
    eng_vector_t world = local;
    int32_t index = entity_of(engine, grid);
    if (index >= 0) {
        eng_entity_transform(engine, index, &world, &local, -1);
    }
    return world;
}

static eng_vector_t world_to_grid(nmo_engine_t *engine, nmo_object_id_t grid, eng_vector_t world)
{
    eng_vector_t local = world;
    int32_t index = entity_of(engine, grid);
    if (index >= 0) {
        eng_entity_inverse_transform(engine, index, &local, &world, -1);
    }
    return local;
}

/* CKGrid::Get2dCoordsFrom3dPos */
static void grid_coords(nmo_engine_t *engine, nmo_object_id_t grid, const eng_vector_t *pos, int32_t *x, int32_t *y)
{
    eng_vector_t local = world_to_grid(engine, grid, *pos);
    *x = (int32_t)local.x;
    *y = (int32_t)local.z;
}

/* CKGrid::Get3dPosFrom2dCoords: the square's centre at the grid's base */
static eng_vector_t grid_position(nmo_engine_t *engine, nmo_object_id_t grid, int32_t x, int32_t y)
{
    eng_vector_t local = {(float)((double)x + 0.5), 0.0f, (float)((double)y + 0.5)};
    return grid_to_world(engine, grid, local);
}

static int32_t grid_width(nmo_engine_t *engine, nmo_object_id_t id)
{
    const eng_grid_t *grid = eng_grid(engine, id);
    return grid != NULL ? grid->width : 0;
}

static int32_t grid_length(nmo_engine_t *engine, nmo_object_id_t id)
{
    const eng_grid_t *grid = eng_grid(engine, id);
    return grid != NULL ? grid->length : 0;
}

static eng_layer_t *grid_layer(nmo_engine_t *engine, nmo_object_id_t id, int32_t type)
{
    eng_grid_t *grid = eng_grid(engine, id);
    return grid != NULL ? eng_grid_layer(engine, grid, type) : NULL;
}

/* CKLayer::GetValue of the grid's layer of that type; false when the grid has none */
static bool layer_value(nmo_engine_t *engine, nmo_object_id_t id, int32_t type, int32_t x, int32_t y, int32_t *out)
{
    eng_grid_t *grid = eng_grid(engine, id);
    eng_layer_t *layer = grid != NULL ? eng_grid_layer(engine, grid, type) : NULL;
    if (layer == NULL) {
        return false;
    }
    *out = eng_layer_value(grid, layer, x, y);
    return true;
}

static float magnitude(eng_vector_t v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static eng_vector_t sub3(eng_vector_t a, eng_vector_t b)
{
    return (eng_vector_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static eng_vector_t add3(eng_vector_t a, eng_vector_t b)
{
    return (eng_vector_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

static eng_vector_t scale3(eng_vector_t a, float s)
{
    return (eng_vector_t){a.x * s, a.y * s, a.z * s};
}

static float dot3(eng_vector_t a, eng_vector_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* ============================================================================
 * Nodes, linkers, paths and open lists
 * ============================================================================ */

static void node_set(gp_node_t *node, int32_t contexts, int32_t index, int32_t grids)
{
    node->ctx = (grid_ctx_t *)calloc((size_t)contexts, sizeof(grid_ctx_t));
    node->case_index = index;
    node->layer_value = 0;
    node->linker_obs = grids > 0 ? (gp_linker_t **)calloc((size_t)grids, sizeof(gp_linker_t *)) : NULL;
}

static void node_resize(gp_node_t *node, int32_t old_count, int32_t count)
{
    grid_ctx_t *ctx = (grid_ctx_t *)realloc(node->ctx, (size_t)count * sizeof(grid_ctx_t));
    if (ctx == NULL) {
        return;
    }
    for (int32_t i = old_count; i < count; i++) {
        memset(&ctx[i], 0, sizeof(ctx[i]));
    }
    node->ctx = ctx;
}

static gp_linker_t *linker_new(int32_t contexts, int32_t layers)
{
    gp_linker_t *linker = (gp_linker_t *)calloc(1, sizeof(gp_linker_t));
    if (linker == NULL) {
        return NULL;
    }
    linker->type = TC_DOOR;
    linker->context_count = contexts;
    linker->ctx = (linker_ctx_t *)calloc((size_t)contexts, sizeof(linker_ctx_t));
    linker->dyn = (gp_linker_t **)calloc((size_t)contexts, sizeof(gp_linker_t *));
    linker->dyn_cost = (float *)calloc((size_t)contexts, sizeof(float));
    linker->layer_count = layers;
    linker->indoor = layers > 0 ? (indoor_link_t *)calloc((size_t)layers, sizeof(indoor_link_t)) : NULL;
    return linker;
}

static void linker_clear_connected(gp_linker_t *linker)
{
    linker->connected.count = 0;
    for (int32_t i = 0; i < linker->layer_count; i++) {
        linker->indoor[i].same.count = 0;
        linker->indoor[i].cost.count = 0;
        linker->indoor[i].layer_cost.count = 0;
        linker->indoor[i].max_layer_cost.count = 0;
    }
}

static void linker_clear_dyn(gp_linker_t *linker)
{
    for (int32_t i = 0; i < linker->context_count; i++) {
        linker->dyn[i] = NULL;
    }
}

static void linker_resize(gp_linker_t *linker, int32_t count)
{
    int32_t old = linker->context_count;
    linker_ctx_t *ctx = (linker_ctx_t *)realloc(linker->ctx, (size_t)count * sizeof(linker_ctx_t));
    gp_linker_t **dyn = (gp_linker_t **)realloc(linker->dyn, (size_t)count * sizeof(gp_linker_t *));
    float *dyn_cost = (float *)realloc(linker->dyn_cost, (size_t)count * sizeof(float));
    if (ctx != NULL) {
        linker->ctx = ctx;
    }
    if (dyn != NULL) {
        linker->dyn = dyn;
    }
    if (dyn_cost != NULL) {
        linker->dyn_cost = dyn_cost;
    }
    if (ctx == NULL || dyn == NULL || dyn_cost == NULL) {
        return;
    }
    for (int32_t i = old; i < count; i++) {
        memset(&linker->ctx[i], 0, sizeof(linker->ctx[i]));
        linker->dyn[i] = NULL;
        linker->dyn_cost[i] = 0.0f;
    }
    linker->context_count = count;
}

static void linker_free(gp_linker_t *linker)
{
    if (linker == NULL) {
        return;
    }
    free(linker->connected.data);
    for (int32_t i = 0; i < linker->layer_count; i++) {
        free(linker->indoor[i].same.data);
        free(linker->indoor[i].cost.data);
        free(linker->indoor[i].layer_cost.data);
        free(linker->indoor[i].max_layer_cost.data);
    }
    free(linker->indoor);
    free(linker->ctx);
    free(linker->dyn);
    free(linker->dyn_cost);
    free(linker);
}

static void indoor_push(indoor_link_t *link, int32_t layer, gp_linker_t *other, float cost, int32_t layer_cost,
                        int32_t max_layer_cost)
{
    gp_linker_t **same = NULL;
    float *c = NULL;
    int32_t *lc = NULL, *mlc = NULL;
    link->layer = layer;
    if (ENG_VEC_PUSH(link->same, same) && ENG_VEC_PUSH(link->cost, c) && ENG_VEC_PUSH(link->layer_cost, lc) &&
        ENG_VEC_PUSH(link->max_layer_cost, mlc)) {
        *same = other;
        *c = cost;
        *lc = layer_cost;
        *mlc = max_layer_cost;
    }
}

static sub_path_t *sub_path_new(nmo_object_id_t grid, float cost)
{
    sub_path_t *sub = (sub_path_t *)calloc(1, sizeof(sub_path_t));
    if (sub != NULL) {
        sub->grid = grid;
        sub->cost = cost;
    }
    return sub;
}

static void sub_path_free(sub_path_t *sub)
{
    if (sub != NULL) {
        free(sub->cases.data);
        free(sub);
    }
}

static void sub_push_back(sub_path_t *sub, int32_t value)
{
    int32_t *slot = NULL;
    if (ENG_VEC_PUSH(sub->cases, slot)) {
        *slot = value;
    }
}

static void sub_push_front(sub_path_t *sub, int32_t value)
{
    int32_t *slot = NULL;
    if (ENG_VEC_PUSH(sub->cases, slot)) {
        memmove(&sub->cases.data[1], &sub->cases.data[0], (sub->cases.count - 1u) * sizeof(int32_t));
        sub->cases.data[0] = value;
    }
}

static int32_t sub_case(const sub_path_t *sub, int32_t index)
{
    if (sub->inversed) {
        return sub->cases.data[sub->cases.count - 1u - (size_t)index];
    }
    return sub->cases.data[index];
}

static void path_clear(gp_path_t *path)
{
    for (size_t i = 0; i < path->subs.count; i++) {
        sub_path_free(path->subs.data[i]);
    }
    path->subs.count = 0;
    path->inversed = false;
}

static void path_add_back(gp_path_t *path, sub_path_t *sub)
{
    sub_path_t **slot = NULL;
    path->cost += sub->cost + 1.0f;
    if (ENG_VEC_PUSH(path->subs, slot)) {
        *slot = sub;
    }
}

static void path_add_front(gp_path_t *path, sub_path_t *sub)
{
    sub_path_t **slot = NULL;
    path->cost += sub->cost + 1.0f;
    if (ENG_VEC_PUSH(path->subs, slot)) {
        memmove(&path->subs.data[1], &path->subs.data[0], (path->subs.count - 1u) * sizeof(sub_path_t *));
        path->subs.data[0] = sub;
    }
}

static sub_path_t *path_sub(const gp_path_t *path, int32_t index)
{
    if (path->inversed) {
        return path->subs.data[path->subs.count - 1u - (size_t)index];
    }
    return path->subs.data[index];
}

/* Path::CopyAndErase: the sub-paths move over */
static void path_copy_and_erase(gp_path_t *to, gp_path_t *from)
{
    int32_t count = (int32_t)from->subs.count;
    for (int32_t i = 0; i < count; i++) {
        path_add_back(to, path_sub(from, i));
    }
    to->obstacle_layer = from->obstacle_layer;
    to->obstacle_threshold = from->obstacle_threshold;
    to->last_point = from->last_point;
    to->linker_obs = from->linker_obs;
    from->subs.count = 0;
}

static void path_inverse(gp_path_t *path)
{
    path->inversed = !path->inversed;
    for (size_t i = 0; i < path->subs.count; i++) {
        path->subs.data[i]->inversed = !path->subs.data[i]->inversed;
    }
}

static float open_f(const open_list_t *list, const void *item)
{
    return list->linkers ? ((const gp_linker_t *)item)->ctx[list->context].f
                         : ((const gp_node_t *)item)->ctx[list->context].f;
}

/* OpenList::Push: kept from the highest F; ties go before the equal ones */
static void open_push(open_list_t *list, void *item)
{
    void **slot = NULL;
    if (list->items.count == 0) {
        if (ENG_VEC_PUSH(list->items, slot)) {
            *slot = item;
        }
        return;
    }
    float f = open_f(list, item);
    if (open_f(list, list->items.data[list->items.count - 1u]) > f) {
        if (ENG_VEC_PUSH(list->items, slot)) {
            *slot = item;
        }
        return;
    }
    size_t at = 0;
    while (at < list->items.count && f < open_f(list, list->items.data[at])) {
        ++at;
    }
    if (ENG_VEC_PUSH(list->items, slot)) {
        memmove(&list->items.data[at + 1u], &list->items.data[at], (list->items.count - 1u - at) * sizeof(void *));
        list->items.data[at] = item;
    }
}

/* OpenList::Update: the DLL's one pass that moves the first node out of order */
static void open_update(open_list_t *list)
{
    float oldf = FLT_MAX, f;
    size_t it = 0, end = list->items.count;
    for (; it != end; ++it) {
        f = open_f(list, list->items.data[it]);
        if (f > oldf) {
            break;
        }
        oldf = f;
    }
    for (; it != end; ++it) {
        f = open_f(list, list->items.data[it]);
        if (oldf > f) {
            break;
        }
        void *swap = list->items.data[it];
        list->items.data[it] = list->items.data[it - 1u];
        list->items.data[it - 1u] = swap;
    }
}

static void *open_pop(open_list_t *list)
{
    return list->items.data[--list->items.count];
}

/* ============================================================================
 * The manager
 * ============================================================================ */

static grid_info_t *info_of(gp_t *gp, nmo_object_id_t grid)
{
    for (size_t i = 0; i < gp->infos.count; i++) {
        if (gp->infos.data[i].grid == grid) {
            return &gp->infos.data[i];
        }
    }
    return NULL;
}

static int32_t *index_of(id_index_t *items, size_t count, int32_t key)
{
    for (size_t i = 0; i < count; i++) {
        if (items[i].key == key) {
            return &items[i].value;
        }
    }
    return NULL;
}

static void index_remove(id_index_t *items, size_t *count, int32_t key)
{
    for (size_t i = 0; i < *count; i++) {
        if (items[i].key == key) {
            memmove(&items[i], &items[i + 1u], (*count - i - 1u) * sizeof(*items));
            --*count;
            return;
        }
    }
}

static void index_insert(id_index_t **items, size_t *count, size_t *capacity, int32_t key, int32_t value)
{
    if (index_of(*items, *count, key) != NULL) {
        return; /* XHashTable::Insert without override */
    }
    if (eng_vec_reserve((void **)items, capacity, *count + 1u, sizeof(**items))) {
        (*items)[(*count)++] = (id_index_t){key, value};
    }
}

static int32_t queue_pop_front(int32_t *items, size_t *count)
{
    int32_t first = items[0];
    memmove(&items[0], &items[1], (*count - 1u) * sizeof(int32_t));
    --*count;
    return first;
}

static void queue_push_back(int32_t **items, size_t *count, size_t *capacity, int32_t value)
{
    if (eng_vec_reserve((void **)items, capacity, *count + 1u, sizeof(int32_t))) {
        (*items)[(*count)++] = value;
    }
}

static void problem_free(path_problem_t *problem)
{
    if (problem == NULL) {
        return;
    }
    for (size_t i = 0; i < problem->hyp_start.count; i++) {
        sub_path_free(problem->hyp_start.data[i]);
    }
    for (size_t i = 0; i < problem->hyp_end.count; i++) {
        sub_path_free(problem->hyp_end.data[i]);
    }
    free(problem->hyp_start.data);
    free(problem->hyp_end.data);
    free(problem->open_linkers.items.data);
    free(problem->open_grid.items.data);
    linker_free(problem->dyn_start);
    linker_free(problem->dyn_end);
    sub_path_free(problem->hyp_start_to_end);
    path_clear(&problem->path);
    free(problem->path.subs.data);
    free(problem);
}

static void follow_free(follow_problem_t *follow)
{
    if (follow != NULL) {
        path_clear(&follow->path);
        free(follow->path.subs.data);
        free(follow);
    }
}

static void path_free(gp_path_t *path)
{
    if (path != NULL) {
        path_clear(path);
        free(path->subs.data);
        free(path);
    }
}

/* GridPathManager::Reset */
static void gp_reset(gp_t *gp)
{
    if (gp->reseted) {
        return;
    }
    for (size_t i = 0; i < gp->linkers.count; i++) {
        linker_free(gp->linkers.data[i]);
    }
    gp->linkers.count = 0;
    free(gp->layer_index);
    gp->layer_index = NULL;
    gp->layer_index_count = 0;
    for (size_t i = 0; i < gp->problems.count; i++) {
        problem_free(gp->problems.data[i]);
    }
    for (int32_t i = 0; i < gp->node_count; i++) {
        free(gp->nodes[i].ctx);
        free(gp->nodes[i].linker_obs);
    }
    free(gp->nodes);
    gp->nodes = NULL;
    gp->node_count = 0;
    for (size_t i = 0; i < gp->infos.count; i++) {
        free(gp->infos.data[i].linkers.data);
    }
    gp->infos.count = 0;
    gp->free_paths.count = 0;
    for (size_t i = 0; i < gp->paths.count; i++) {
        path_free(gp->paths.data[i].path);
    }
    gp->paths.count = 0;
    for (size_t i = 0; i < gp->follows.count; i++) {
        follow_free(gp->follows.data[i]);
    }
    gp->problems.count = 0;
    gp->follows.count = 0;
    gp->target_contexts.count = 0;
    gp->follow_contexts.count = 0;
    gp->free_follows.count = 0;
    gp->num_path = 0;
    gp->num_follow = 0;
    gp->max_path = 1;
    gp->max_follow = 0;
    gp->graph_done = false;
    gp->reseted = true;
}

static void gp_free(gp_t *gp)
{
    gp->reseted = false;
    gp_reset(gp);
    free(gp->target_contexts.data);
    free(gp->problems.data);
    free(gp->free_paths.data);
    free(gp->paths.data);
    free(gp->follow_contexts.data);
    free(gp->follows.data);
    free(gp->free_follows.data);
    free(gp->linkers.data);
    free(gp->infos.data);
}

/* GridPathManager::ResetArrayContextInfo */
static void reset_grid_contexts(gp_t *gp, int32_t context, int32_t count)
{
    for (int32_t i = 0; i < count && i < gp->node_count; i++) {
        grid_ctx_t *ctx = &gp->nodes[i].ctx[context];
        ctx->cost = 0.0f;
        ctx->layer_cost = 0.0f;
        ctx->closed = false;
        ctx->in_open = false;
        ctx->parent = NULL;
    }
}

static void reset_linker_contexts(gp_t *gp, int32_t context)
{
    for (size_t i = 0; i < gp->linkers.count; i++) {
        linker_ctx_t *ctx = &gp->linkers.data[i]->ctx[context];
        ctx->cost = 0.0f;
        ctx->layer_cost = 0.0f;
        ctx->closed = false;
        ctx->in_open = false;
        ctx->parent = NULL;
    }
}

/* FillSuccessor (grid): the 8 squares round, those under 255 on the layer */
static void fill_successor_grid(gp_t *gp, nmo_engine_t *engine, nmo_object_id_t grid, int32_t index, int32_t layer)
{
    int32_t count = 0, succ = 0;
    int32_t w = grid_width(engine, grid), l = grid_length(engine, grid);
    int32_t case_y = index / w, case_x = index - case_y * w;
    eng_grid_t *g = eng_grid(engine, grid);
    eng_layer_t *lay = g != NULL ? eng_grid_layer(engine, g, layer) : NULL;
    for (int32_t i = case_x - 1; i <= case_x + 1; ++i) {
        for (int32_t j = case_y - 1; j <= case_y + 1; ++j) {
            if (i >= 0 && i < w && j >= 0 && j < l && (case_x != i || case_y != j)) {
                int32_t value = lay != NULL ? eng_layer_value(g, lay, i, j) : 0;
                if (value < 255) {
                    gp->successor_id[succ] = j * w + i;
                    gp->successor_cost[succ] = (count == 0 || count == 2 || count == 6 || count == 8)
                                                   ? DISTANCE_DIAGONAL
                                                   : DISTANCE_JUXTAPOSE;
                    gp->successor_layer_cost[succ++] = value;
                }
            }
            ++count;
        }
    }
    gp->successor_id[succ] = -1;
}

/* CasesDistance: the A* cost between two squares of a grid, and the highest layer value on the way */
static grid_ctx_t *cases_distance(gp_t *gp, nmo_engine_t *engine, nmo_object_id_t grid, int32_t layer, int32_t sx,
                                  int32_t sy, int32_t ex, int32_t ey, int32_t *max_layer_value)
{
    open_list_t list = {{NULL, 0, 0}, 0, false};
    int32_t w = grid_width(engine, grid);
    int32_t goal = ey * w + ex;
    int32_t index = sy * w + sx;
    gp->nodes[index].ctx[0].cost = 0.0f;
    gp->nodes[index].ctx[0].f = sqrtf((float)((sx - ex) * (sx - ex) + (sy - ey) * (sy - ey)));
    open_push(&list, &gp->nodes[index]);
    while (list.items.count > 0) {
        gp_node_t *node = (gp_node_t *)open_pop(&list);
        node->ctx[0].in_open = false;
        if (node->case_index == goal) {
            *max_layer_value = 0;
            for (gp_node_t *n = node; n != NULL; n = n->ctx[0].parent) {
                if (*max_layer_value < n->layer_value) {
                    *max_layer_value = n->layer_value;
                }
            }
            free(list.items.data);
            return &node->ctx[0];
        }
        fill_successor_grid(gp, engine, grid, node->case_index, layer);
        for (int32_t s = 0; gp->successor_id[s] != -1; s++) {
            gp_node_t *succ = &gp->nodes[gp->successor_id[s]];
            grid_ctx_t *cs = &succ->ctx[0];
            float cost = node->ctx[0].cost + gp->successor_cost[s];
            if ((cs->in_open || cs->closed) && cs->cost <= cost) {
                continue;
            }
            cs->cost = cost;
            div_t dt = div(succ->case_index, w);
            int32_t succ_y = dt.quot, succ_x = dt.rem;
            cs->f = cost + sqrtf((float)((succ_x - ex) * (succ_x - ex) + (succ_y - ey) * (succ_y - ey)));
            cs->parent = node;
            succ->layer_value = gp->successor_layer_cost[s];
            cs->closed = false;
            if (!cs->in_open) {
                cs->in_open = true;
                open_push(&list, succ);
            } else {
                open_update(&list);
            }
        }
        node->ctx[0].closed = true;
    }
    free(list.items.data);
    return NULL;
}

/* GridContainOneLayer */
static bool grid_has_one_layer(nmo_engine_t *engine, nmo_object_id_t grid, const int32_t *layers, int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        if (grid_layer(engine, grid, layers[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static void set_layer_index(gp_t *gp, int32_t layer, int32_t index)
{
    if (layer >= 0 && layer < gp->layer_index_count) {
        gp->layer_index[layer] = index;
    }
}

static int32_t layer_index_of(const gp_t *gp, int32_t layer)
{
    return layer >= 0 && layer < gp->layer_index_count ? gp->layer_index[layer] : 0;
}

/* ConstructListNodeLinker */
static bool construct_linkers(gp_t *gp, nmo_engine_t *engine, const int32_t *layers, int32_t layer_count)
{
    int32_t num_grid = 0;
    size_t total = eng_grid_count(engine);
    bool any = false;
    for (size_t i = 0; i < total; i++) {
        eng_grid_t *grid = eng_grid_at(engine, i);
        if (grid == NULL || !eng_in_scene(engine, grid->id)) {
            continue;
        }
        any = true;
        if (!grid_has_one_layer(engine, grid->id, layers, layer_count)) {
            continue;
        }
        grid_info_t *info = NULL;
        if (!ENG_VEC_PUSH(gp->infos, info)) {
            return false;
        }
        info->grid = grid->id;
        info->num = num_grid++;
        int32_t w = grid->width, l = grid->length;
        if (w * l > gp->max_case) {
            gp->max_case = w * l;
        }
        int32_t type_count = eng_grid_type_count(engine);
        for (int32_t j = 0; j < type_count; ++j) {
            if (!nmo_guid_equals(eng_grid_type_param(engine, j), CKPGUID_LINKERGRAPH_ENUM)) {
                continue;
            }
            grid = eng_grid_at(engine, i);
            eng_layer_t *layer = eng_grid_layer(engine, grid, j);
            if (layer == NULL) {
                continue;
            }
            const nmo_object_t *layer_object = eng_find_object(engine, layer->id);
            const char *layer_name = layer_object != NULL ? nmo_object_get_name(layer_object) : NULL;
            for (int32_t x = 0; x < w; ++x) {
                for (int32_t y = 0; y < l; ++y) {
                    int32_t value = eng_layer_value(grid, layer, x, y);
                    if (value == 0) {
                        continue;
                    }
                    gp_linker_t *linker = linker_new(gp->max_path, layer_count);
                    gp_linker_t **slot = NULL;
                    if (linker == NULL) {
                        return false;
                    }
                    linker->attribute = attribute_type_named(engine, layer_name);
                    linker->grid = grid->id;
                    linker->type = value == 1 ? TC_TELE1 : value == 2 ? TC_TELE2 : value == 3 ? TC_TELE3 : TC_DOOR;
                    linker->linker_id = j;
                    linker->x = x;
                    linker->y = y;
                    if (value == 4) {
                        linker->pos = grid_position(engine, grid->id, x, y);
                        linker->has_pos = true;
                    }
                    if (ENG_VEC_PUSH(info->linkers, slot)) {
                        *slot = linker;
                    }
                    if (ENG_VEC_PUSH(gp->linkers, slot)) {
                        *slot = linker;
                    } else {
                        linker_free(linker);
                    }
                }
            }
        }
    }
    if (!any) {
        return false;
    }
    int32_t max_layer = 0;
    for (int32_t i = 0; i < layer_count; i++) {
        if (max_layer < layers[i]) {
            max_layer = layers[i];
        }
    }
    gp->layer_index_count = max_layer + 1;
    gp->layer_index = (int32_t *)calloc((size_t)gp->layer_index_count, sizeof(int32_t));
    gp->nodes = gp->max_case > 0 ? (gp_node_t *)calloc((size_t)gp->max_case, sizeof(gp_node_t)) : NULL;
    if (gp->layer_index == NULL || (gp->max_case > 0 && gp->nodes == NULL)) {
        return false;
    }
    gp->node_count = gp->max_case;
    for (int32_t i = 0; i < gp->max_case; ++i) {
        node_set(&gp->nodes[i], gp->max_path, i, num_grid);
    }
    for (size_t g = 0; g < gp->infos.count; g++) {
        grid_info_t *info = &gp->infos.data[g];
        for (size_t k = 0; k < info->linkers.count; k++) {
            gp_linker_t *node = info->linkers.data[k];
            int32_t index = node->y * grid_width(engine, node->grid) + node->x;
            if (index >= 0 && index < gp->node_count) {
                gp->nodes[index].linker_obs[info->num] = node;
            }
        }
    }
    for (size_t a = 0; a < gp->linkers.count; a++) {
        gp_linker_t *linker1 = gp->linkers.data[a];
        for (size_t b = 0; b < gp->linkers.count; b++) {
            gp_linker_t *linker2 = gp->linkers.data[b];
            if (linker1 == linker2 || linker1->linker_id != linker2->linker_id) {
                continue;
            }
            if ((linker1->type == TC_DOOR && linker2->type == TC_DOOR) ||
                ((linker1->type == TC_TELE1 || linker1->type == TC_TELE3) &&
                 (linker2->type == TC_TELE2 || linker2->type == TC_TELE3))) {
                gp_linker_t **slot = NULL;
                if (ENG_VEC_PUSH(linker1->connected, slot)) {
                    *slot = linker2;
                }
            }
        }
        int32_t num_layer = 0;
        for (int32_t li = 0; li < layer_count; li++) {
            for (size_t b = a; b < gp->linkers.count; b++) {
                gp_linker_t *linker2 = gp->linkers.data[b];
                if (linker1->linker_id == linker2->linker_id || linker1->grid != linker2->grid) {
                    continue;
                }
                int32_t w = grid_width(engine, linker1->grid);
                reset_grid_contexts(gp, 0, w * grid_length(engine, linker1->grid));
                int32_t max_layer_value = 0;
                grid_ctx_t *ctx = cases_distance(gp, engine, linker1->grid, layers[li], linker1->x, linker1->y,
                                                 linker2->x, linker2->y, &max_layer_value);
                if (ctx != NULL) {
                    indoor_push(&linker1->indoor[num_layer], layers[li], linker2, ctx->cost, (int32_t)ctx->layer_cost,
                                max_layer_value);
                    indoor_push(&linker2->indoor[num_layer], layers[li], linker1, ctx->cost, (int32_t)ctx->layer_cost,
                                max_layer_value);
                }
            }
            set_layer_index(gp, layers[li], num_layer);
            ++num_layer;
        }
    }
    if (gp->linkers.count == 0) {
        int32_t num_layer = 0;
        for (int32_t li = 0; li < layer_count; li++) {
            set_layer_index(gp, layers[li], num_layer++);
        }
    }
    return true;
}

static path_problem_t *problem_new(gp_t *gp, int32_t context)
{
    path_problem_t *problem = (path_problem_t *)calloc(1, sizeof(path_problem_t));
    if (problem == NULL) {
        return NULL;
    }
    problem->gp = gp;
    problem->state = 1;
    problem->obstacle_layer = -1;
    problem->hyp_start_to_end_cost = FLT_MAX;
    problem->follow_path_id = -1;
    problem->open_linkers.context = context;
    problem->open_linkers.linkers = true;
    problem->open_grid.context = context;
    problem->dyn_start = linker_new(gp->max_path, gp->num_layer);
    problem->dyn_end = linker_new(gp->max_path, gp->num_layer);
    return problem;
}

static int st_finish(gp_t *gp, path_problem_t *p, int32_t context);

/* ConstructGraph: the layers to search on, or (NULL) every type but the linker ones */
static bool construct_graph(gp_t *gp, nmo_engine_t *engine, const int32_t *layers, int32_t layer_count)
{
    int32_t *all = NULL;
    gp_reset(gp);
    if (layers == NULL) {
        int32_t type_count = eng_grid_type_count(engine);
        all = (int32_t *)malloc((size_t)(type_count > 0 ? type_count : 1) * sizeof(int32_t));
        if (all == NULL) {
            return false;
        }
        layer_count = 0;
        for (int32_t j = 0; j < type_count; ++j) {
            if (!nmo_guid_equals(eng_grid_type_param(engine, j), CKPGUID_LINKERGRAPH_ENUM)) {
                all[layer_count++] = j;
            }
        }
        layers = all;
    }
    bool ok = construct_linkers(gp, engine, layers, layer_count);
    if (ok) {
        gp->num_layer = layer_count;
        path_problem_t **slot = NULL;
        path_problem_t *problem = problem_new(gp, 0);
        if (problem != NULL && ENG_VEC_PUSH(gp->problems, slot)) {
            problem->function = st_finish;
            *slot = problem;
            queue_push_back(&gp->free_paths.data, &gp->free_paths.count, &gp->free_paths.capacity, 0);
        } else {
            problem_free(problem);
        }
        gp->graph_done = true;
        gp->reseted = false;
    }
    free(all);
    return ok;
}

/* ResizePathContextArray */
static void resize_paths(gp_t *gp, int32_t count)
{
    int32_t old = (int32_t)gp->problems.count;
    for (int32_t i = 0; i < old; ++i) {
        linker_resize(gp->problems.data[i]->dyn_start, count);
        linker_resize(gp->problems.data[i]->dyn_end, count);
    }
    for (int32_t i = old; i < count; ++i) {
        path_problem_t *problem = problem_new(gp, i);
        path_problem_t **slot = NULL;
        if (problem == NULL || !ENG_VEC_PUSH(gp->problems, slot)) {
            problem_free(problem);
            return;
        }
        problem->function = st_finish;
        *slot = problem;
    }
    for (size_t i = 0; i < gp->linkers.count; i++) {
        linker_resize(gp->linkers.data[i], count);
    }
    for (int32_t i = 0; i < gp->node_count; ++i) {
        node_resize(&gp->nodes[i], old, count);
    }
}

static int st_get_start_end(gp_t *gp, path_problem_t *p, int32_t context);

static path_problem_t *problem_of(gp_t *gp, nmo_object_id_t target)
{
    int32_t *context = index_of(gp->target_contexts.data, gp->target_contexts.count, (int32_t)target);
    return context != NULL ? gp->problems.data[*context] : NULL;
}

/* GetTargetStatus: 0 none, 1 searching, 2 found, 3 not found */
static int32_t target_status(gp_t *gp, nmo_object_id_t target)
{
    path_problem_t *problem = problem_of(gp, target);
    return problem != NULL ? problem->state : 0;
}

typedef struct problem_settings {
    eng_vector_t target_pos, goal_pos;
    nmo_object_id_t goal_ref;
    int32_t obstacle_layer, obstacle_threshold;
    float slowing_factor;
    bool linker, linker_obs;
    float heuristic_coef;
    int32_t heuristic;
    bool diagonal, optimize;
} problem_settings_t;

/* RegisterPathProblem, with PathProblem::Set */
static bool register_problem(gp_t *gp, nmo_engine_t *engine, nmo_object_id_t target, const problem_settings_t *s)
{
    if (!gp->graph_done && !construct_graph(gp, engine, NULL, 0)) {
        return false;
    }
    int32_t context;
    if (gp->free_paths.count > 0) {
        context = queue_pop_front(gp->free_paths.data, &gp->free_paths.count);
    } else {
        context = gp->max_path++;
        resize_paths(gp, gp->max_path);
        if ((int32_t)gp->problems.count <= context) {
            return false;
        }
    }
    ++gp->num_path;
    path_problem_t *p = gp->problems.data[context];
    p->target = target;
    int32_t target_entity = entity_of(engine, target);
    p->target_pos = s->target_pos;
    if (target_entity >= 0) {
        eng_entity_transform(engine, target_entity, &p->target_pos, &s->target_pos, -1);
    }
    int32_t goal_entity = s->goal_ref != 0 ? entity_of(engine, s->goal_ref) : -1;
    p->goal_pos = s->goal_pos;
    if (goal_entity >= 0) {
        eng_entity_transform(engine, goal_entity, &p->goal_pos, &s->goal_pos, -1);
    }
    p->obstacle_layer = s->obstacle_layer;
    p->obstacle_threshold = s->obstacle_threshold > 255 ? 255 : s->obstacle_threshold < 0 ? 0 : s->obstacle_threshold;
    p->slowing_factor = fabsf(s->slowing_factor);
    p->slowing_div_threshold = p->slowing_factor / (float)(p->obstacle_threshold + 1);
    p->linker = s->linker;
    p->linker_obs = s->linker_obs;
    p->heuristic_coef = s->heuristic_coef < 0.0f ? 0.0f : s->heuristic_coef;
    p->heuristic = s->heuristic;
    p->diagonal = s->diagonal;
    p->optimize = s->optimize;
    p->function = st_get_start_end;
    index_insert(&gp->target_contexts.data, &gp->target_contexts.count, &gp->target_contexts.capacity,
                 (int32_t)target, context);
    return true;
}

/* PathProblem::Reset */
static void problem_reset(path_problem_t *p)
{
    p->state = 1;
    p->grid_start = 0;
    p->grid_end = 0;
    p->open_grid.items.count = 0;
    p->open_linkers.items.count = 0;
    p->start_part = false;
    linker_clear_connected(p->dyn_start);
    linker_clear_dyn(p->dyn_start);
    linker_clear_connected(p->dyn_end);
    linker_clear_dyn(p->dyn_end);
    for (size_t i = 0; i < p->path.subs.count; i++) {
        sub_path_free(p->path.subs.data[i]);
    }
    p->path.subs.count = 0;
    p->path.cost = 0.0f;
    for (size_t i = 0; i < p->hyp_start.count; i++) {
        sub_path_free(p->hyp_start.data[i]);
    }
    p->hyp_start.count = 0;
    for (size_t i = 0; i < p->hyp_end.count; i++) {
        sub_path_free(p->hyp_end.data[i]);
    }
    p->hyp_end.count = 0;
    p->sub_start = NULL;
    p->sub_end = NULL;
    sub_path_free(p->hyp_start_to_end);
    p->hyp_start_to_end = NULL;
    p->hyp_start_to_end_cost = FLT_MAX;
}

/* UnregisterPathProblem: the found path kept for a follower under its path ID when asked */
static bool unregister_problem(gp_t *gp, nmo_object_id_t target, bool give_to_follower)
{
    int32_t *found = index_of(gp->target_contexts.data, gp->target_contexts.count, (int32_t)target);
    if (found == NULL) {
        return false;
    }
    int32_t context = *found;
    index_remove(gp->target_contexts.data, &gp->target_contexts.count, (int32_t)target);
    path_problem_t *p = gp->problems.data[context];
    if (give_to_follower) {
        gp_path_t *path = (gp_path_t *)calloc(1, sizeof(gp_path_t));
        if (path != NULL) {
            path_copy_and_erase(path, &p->path);
            path->obstacle_layer = p->obstacle_layer;
            path->obstacle_threshold = p->obstacle_threshold;
            for (size_t i = 0; i < path->subs.count; ++i) {
                path->subs.data[i]->path_fid = p->follow_path_id;
            }
            bool known = false;
            for (size_t i = 0; i < gp->paths.count && !known; i++) {
                known = gp->paths.data[i].id == p->follow_path_id;
            }
            path_entry_t *entry = NULL;
            if (!known && ENG_VEC_PUSH(gp->paths, entry)) {
                entry->id = p->follow_path_id;
                entry->path = path;
            } else {
                path_free(path);
            }
        }
    }
    problem_reset(p);
    for (size_t i = 0; i < gp->linkers.count; i++) {
        gp->linkers.data[i]->dyn[context] = NULL;
    }
    queue_push_back(&gp->free_paths.data, &gp->free_paths.count, &gp->free_paths.capacity, context);
    --gp->num_path;
    return true;
}

/* ============================================================================
 * The search states
 * ============================================================================ */

static bool case_is_linker_at(gp_t *gp, path_problem_t *p, int32_t num_grid, int32_t index)
{
    if (index < 0 || index >= gp->node_count || num_grid < 0) {
        return false;
    }
    gp_linker_t *linker = gp->nodes[index].linker_obs[num_grid];
    if (linker == NULL) {
        return false;
    }
    if (linker->type == TC_DOOR) {
        return false;
    }
    if (has_attribute(gp->engine, p->target, linker->attribute)) {
        return index != p->end_index;
    }
    return true;
}

/* CaseIsLinker (of a grid) */
static bool case_is_linker(gp_t *gp, path_problem_t *p, nmo_object_id_t grid, int32_t index)
{
    grid_info_t *info = info_of(gp, grid);
    return info != NULL && case_is_linker_at(gp, p, info->num, index);
}

/* PathProblem::InitBeforeCase2Case */
static void init_before_case2case(gp_t *gp, path_problem_t *p, nmo_object_id_t grid)
{
    nmo_engine_t *engine = gp->engine;
    p->open_grid.items.count = 0;
    p->grid_search = grid;
    grid_info_t *info = info_of(gp, grid);
    p->num_grid = info != NULL ? info->num : 0;
    p->grid_w = grid_width(engine, grid);
    p->grid_l = grid_length(engine, grid);
    eng_layer_t *layer = grid_layer(engine, grid, p->obstacle_layer);
    p->square = layer != NULL ? layer->squares : NULL;
    p->square_count = layer != NULL ? layer->count : 0;
    int32_t w = p->grid_w;
    if (p->diagonal) {
        const int32_t index[12] = {w - 1, w, w + 1, 1, -w + 1, -w, -w - 1, -1, w - 1, w, w + 1, 1};
        memcpy(p->successor_index, index, sizeof(index));
    } else {
        const int32_t index[6] = {w, 1, -w, -1, w, 1};
        memcpy(p->successor_index, index, sizeof(index));
    }
}

static int32_t square_at(const path_problem_t *p, int32_t index)
{
    return index >= 0 && index < p->square_count ? p->square[index] : 0;
}

/* FillSuccessor (problem): the squares one may step to that are no wall (or linker) */
static void fill_successor(gp_t *gp, path_problem_t *p, int32_t index)
{
    int32_t w = p->grid_w, l = p->grid_l;
    div_t dt = div(index, w);
    int32_t case_y = dt.quot, case_x = dt.rem;
    int32_t start, end, succ = 0;
    const float *costs;
    if (p->diagonal) {
        costs = successor_index_cost;
        if (case_y == 0) {
            start = case_x == 0 ? 1 : 7;
            end = case_x == 0 ? 3 : case_x == w - 1 ? 9 : 11;
        } else if (case_y == l - 1) {
            start = case_x == 0 ? 3 : case_x == w - 1 ? 5 : 3;
            end = case_x == 0 ? 5 : 7;
        } else if (case_x == 0) {
            start = 1;
            end = 5;
        } else if (case_x == w - 1) {
            start = 5;
            end = 9;
        } else {
            start = 0;
            end = 7;
        }
    } else {
        costs = successor_index_cost2;
        if (case_y == 0) {
            start = case_x == 0 ? 0 : 3;
            end = case_x == 0 ? 1 : case_x == w - 1 ? 4 : 5;
        } else if (case_y == l - 1) {
            start = case_x == 0 ? 1 : case_x == w - 1 ? 2 : 1;
            end = case_x == 0 ? 2 : 3;
        } else if (case_x == 0) {
            start = 0;
            end = 2;
        } else if (case_x == w - 1) {
            start = 2;
            end = 4;
        } else {
            start = 0;
            end = 3;
        }
    }
    for (int32_t i = start; i <= end; ++i) {
        int32_t index_succ = p->successor_index[i] + index;
        if (p->linker_obs && case_is_linker_at(gp, p, p->num_grid, index_succ)) {
            continue;
        }
        int32_t layer_cost = 0;
        if (p->square == NULL || (layer_cost = square_at(p, index_succ)) <= p->obstacle_threshold) {
            gp->successor_id[succ] = index_succ;
            gp->successor_cost[succ] = costs[i];
            gp->successor_layer_cost[succ++] = layer_cost;
        }
    }
    gp->successor_id[succ] = -1;
}

/* GetCase2CasePath: 2 when the end square is reached (node_end), 0 when it cannot be */
static int case2case(gp_t *gp, path_problem_t *p, int32_t context)
{
    int32_t w = p->grid_w;
    for (;;) {
        if (p->open_grid.items.count == 0) {
            return 0;
        }
        gp_node_t *node = (gp_node_t *)open_pop(&p->open_grid);
        node->ctx[context].in_open = false;
        grid_ctx_t *ctx = &node->ctx[context];
        if (node->case_index == p->end_index) {
            p->node_end = node;
            return 2;
        }
        fill_successor(gp, p, node->case_index);
        for (int32_t s = 0; gp->successor_id[s] != -1; s++) {
            if (gp->successor_id[s] < 0 || gp->successor_id[s] >= gp->node_count) {
                continue;
            }
            gp_node_t *succ = &gp->nodes[gp->successor_id[s]];
            grid_ctx_t *cs = &succ->ctx[context];
            float cost = ctx->cost + gp->successor_cost[s];
            float layer_cost = ctx->layer_cost + (float)gp->successor_layer_cost[s] * p->slowing_div_threshold;
            if ((cs->in_open || cs->closed) && cs->cost + cs->layer_cost <= cost + layer_cost) {
                continue;
            }
            cs->cost = cost;
            cs->layer_cost = layer_cost;
            div_t dt = div(succ->case_index, w);
            int32_t dy = dt.quot - p->y_end_search, dx = dt.rem - p->x_end_search;
            switch (p->heuristic) {
            case 1:
                cs->f = cost + layer_cost + sqrtf((float)dx * (float)dx + (float)(dy * dy)) * p->heuristic_coef;
                break;
            case 2:
                cs->f = cost + layer_cost + (float)(abs(dx) + abs(dy)) * p->heuristic_coef;
                break;
            case 3:
                cs->f = cost + layer_cost + (float)(dx * dx + dy * dy) * p->heuristic_coef;
                break;
            case 4: {
                dx = abs(dx);
                dy = abs(dy);
                if (dy > dx) {
                    int32_t t = dx;
                    dx = dy;
                    dy = t;
                }
                cs->f = cost + layer_cost + (0.9604f * (float)dx + 0.3978f * (float)dy) * p->heuristic_coef;
                break;
            }
            default:
                cs->f = cost + layer_cost;
                break;
            }
            cs->parent = node;
            cs->closed = false;
            if (!cs->in_open) {
                cs->in_open = true;
                open_push(&p->open_grid, succ);
            } else {
                open_update(&p->open_grid);
            }
        }
        ctx->closed = true;
    }
}

/* PathProblem::BuildSubPath: the squares from the end node back, a square where the way turns
   (every square when not optimizing) */
static void build_sub_path(const path_problem_t *p, int32_t context, sub_path_t *sub, bool back)
{
    int32_t width = grid_width(p->gp->engine, sub->grid);
    gp_node_t *end = p->node_end;
    int32_t last_case = end->case_index;
    int32_t old_dir = 0, dir = 0;
    sub->linker_start = p->linker1;
    sub->linker_end = p->linker2;
    if (end->ctx[context].parent == NULL) {
        sub_push_back(sub, end->case_index);
        sub_push_front(sub, end->case_index);
        return;
    }
    sub_push_back(sub, end->case_index);
    gp_node_t *node = end->ctx[context].parent;
    int32_t diff = node->case_index - end->case_index;
#define DIR_OF(d, out)                                                                                   \
    do {                                                                                                 \
        if ((d) == 1) (out) = 1;                                                                         \
        else if ((d) == -1) (out) = 2;                                                                   \
        else if ((d) == width) (out) = 3;                                                                \
        else if ((d) == -width) (out) = 4;                                                               \
        else if ((d) == width + 1) (out) = 5;                                                            \
        else if ((d) == width - 1) (out) = 6;                                                            \
        else if ((d) == -width + 1) (out) = 7;                                                           \
        else if ((d) == -width - 1) (out) = 8;                                                           \
    } while (0)
    DIR_OF(diff, old_dir);
    int32_t old_case = node->case_index;
    node = node->ctx[context].parent;
    if (node == NULL) {
        if (back) {
            sub_push_back(sub, old_case);
        } else {
            sub_push_front(sub, old_case);
        }
        return;
    }
    gp_node_t *prev = NULL;
    while (node != NULL) {
        diff = node->case_index - old_case;
        DIR_OF(diff, dir);
        if (dir != old_dir || !p->optimize) {
            if (back) {
                sub_push_back(sub, old_case);
            } else {
                sub_push_front(sub, old_case);
            }
            old_dir = dir;
            last_case = old_case;
        }
        old_case = node->case_index;
        prev = node;
        node = node->ctx[context].parent;
    }
#undef DIR_OF
    if (last_case != prev->case_index) {
        if (back) {
            sub_push_back(sub, prev->case_index);
        } else {
            sub_push_front(sub, prev->case_index);
        }
    }
}

static int st_direct_path(gp_t *gp, path_problem_t *p, int32_t context);
static int st_before_start_linker(gp_t *gp, path_problem_t *p, int32_t context);
static int st_linker_from_start(gp_t *gp, path_problem_t *p, int32_t context);
static int st_linker_from_end(gp_t *gp, path_problem_t *p, int32_t context);
static int st_grid2grid(gp_t *gp, path_problem_t *p, int32_t context);
static int st_all_case2case(gp_t *gp, path_problem_t *p, int32_t context);
static int st_path_found(gp_t *gp, path_problem_t *p, int32_t context);
static int st_path_not_found(gp_t *gp, path_problem_t *p, int32_t context);

#define NO_NODE ((gp_node_t *)1)

static float start_heuristic(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    return sqrtf((float)((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2)));
}

/* Starts a search of a grid from (x1, y1) */
static void push_start(gp_t *gp, path_problem_t *p, int32_t context, int32_t index, float f)
{
    if (index < 0 || index >= gp->node_count) {
        return;
    }
    gp_node_t *node = &gp->nodes[index];
    node->ctx[context].cost = 0.0f;
    node->ctx[context].f = f;
    node->ctx[context].parent = NULL;
    open_push(&p->open_grid, node);
}

/* GetStartEndGridID */
static int st_get_start_end(gp_t *gp, path_problem_t *p, int32_t context)
{
    (void)context;
    nmo_engine_t *engine = gp->engine;
    eng_grid_t *start = eng_grid_preferred(engine, &p->target_pos, -1);
    if (start == NULL) {
        p->function = st_path_not_found;
        return 0;
    }
    nmo_object_id_t grid_start = start->id;
    if (info_of(gp, grid_start) != NULL) {
        eng_grid_t *end = eng_grid_preferred(engine, &p->goal_pos, -1);
        if (end == NULL) {
            p->function = st_path_not_found;
            return 0;
        }
        nmo_object_id_t grid_end = end->id;
        if (info_of(gp, grid_end) != NULL) {
            gp_linker_t *node_start = p->dyn_start, *node_end = p->dyn_end;
            p->grid_start = grid_start;
            p->grid_end = grid_end;
            linker_clear_connected(node_start);
            linker_clear_dyn(node_start);
            node_start->grid = grid_start;
            node_start->linker_id = -1;
            grid_coords(engine, grid_start, &p->target_pos, &node_start->x, &node_start->y);
            linker_clear_connected(node_end);
            linker_clear_dyn(node_end);
            node_end->grid = grid_end;
            node_end->linker_id = -2;
            grid_coords(engine, grid_end, &p->goal_pos, &node_end->x, &node_end->y);
            if (node_start->x == node_end->x && node_start->y == node_end->y) {
                p->function = st_path_not_found;
                return 0;
            }
            if (p->linker_obs &&
                case_is_linker(gp, p, grid_end, node_end->x + node_end->y * grid_width(engine, grid_end))) {
                p->function = st_path_not_found;
                return 0;
            }
            int32_t value = 0;
            if (layer_value(engine, grid_end, p->obstacle_layer, node_end->x, node_end->y, &value) &&
                value > p->obstacle_threshold) {
                p->function = st_path_not_found;
                return 0;
            }
            if (!p->linker || gp->linkers.count == 0 || attribute_count(engine, p->target) == 0) {
                if (grid_start == grid_end) {
                    p->function = st_direct_path;
                    return 1;
                }
                p->function = st_path_not_found;
                return 0;
            }
            p->function = st_before_start_linker;
            return 1;
        }
    }
    p->function = st_path_not_found;
    return 0;
}

/* BeforeStartLinker: on one grid, the direct way first, as the bound for the linkers */
static int st_before_start_linker(gp_t *gp, path_problem_t *p, int32_t context)
{
    nmo_engine_t *engine = gp->engine;
    gp_linker_t *node_start = p->dyn_start, *node_end = p->dyn_end;
    for (;;) {
        if (!p->start_part && p->grid_start == p->grid_end) {
            p->start_part = true;
            init_before_case2case(gp, p, p->grid_start);
            reset_grid_contexts(gp, context, p->grid_w * p->grid_l);
            p->linker1 = p->linker2 = NULL;
            int32_t w = grid_width(engine, node_end->grid);
            push_start(gp, p, context, node_end->y * w + node_end->x,
                       start_heuristic(node_end->x, node_end->y, node_start->x, node_start->y));
            p->x_end_search = node_start->x;
            p->y_end_search = node_start->y;
            p->end_index = node_start->y * grid_width(engine, node_start->grid) + node_start->x;
            p->node_end = NULL;
            return 1;
        }
        if (p->grid_start == p->grid_end) {
            int result = case2case(gp, p, context);
            if (result == 0) {
                p->start_part = false;
                p->function = st_linker_from_start;
                return 1;
            }
            if (p->node_end != NULL) {
                indoor_push(&node_start->indoor[layer_index_of(gp, p->obstacle_layer)], node_start->indoor[layer_index_of(gp, p->obstacle_layer)].layer,
                            node_end, p->node_end->ctx[context].cost, 0, 0);
                p->hyp_start_to_end = sub_path_new(p->grid_search, p->node_end->ctx[context].cost);
                if (p->hyp_start_to_end != NULL) {
                    build_sub_path(p, context, p->hyp_start_to_end, true);
                    p->hyp_start_to_end_cost = p->hyp_start_to_end->cost;
                }
                p->start_part = false;
                p->function = st_linker_from_start;
                return 1;
            }
        } else {
            p->start_part = false;
            p->function = st_linker_from_start;
            return 1;
        }
    }
}

/* Linker2StartIsGood / Linker2EndIsGood: near enough and usable by the target */
static bool linker_is_good(path_problem_t *p, const gp_linker_t *from, const gp_linker_t *linker)
{
    int32_t dx = from->x - linker->x, dy = from->y - linker->y;
    if (sqrtf((float)(dx * dx + dy * dy)) > p->hyp_start_to_end_cost) {
        return false;
    }
    return has_attribute(p->gp->engine, p->target, linker->attribute);
}

/* GetLinkerFromStart / GetLinkerFromEnd: the way from the start (the end) to each linker of its grid */
static int linkers_from(gp_t *gp, path_problem_t *p, int32_t context, bool from_start)
{
    nmo_engine_t *engine = gp->engine;
    nmo_object_id_t grid = from_start ? p->grid_start : p->grid_end;
    gp_linker_t *dyn = from_start ? p->dyn_start : p->dyn_end;
    for (;;) {
        if (!p->start_part) {
            p->it_info = info_of(gp, grid);
            p->it_linker = 0;
            p->it_linker_end = p->it_info != NULL ? (int32_t)p->it_info->linkers.count : 0;
            p->grid_search = grid;
            grid_coords(engine, grid, from_start ? &p->target_pos : &p->goal_pos, &p->x_start_search,
                        &p->y_start_search);
            p->node_end = NO_NODE;
            p->start_part = true;
        }
        if (p->node_end == NULL) {
            if (case2case(gp, p, context) == 0) {
                p->node_end = NO_NODE;
            } else if (p->node_end != NULL) {
                sub_path_t *sub = sub_path_new(p->grid_search, p->node_end->ctx[context].cost);
                sub_path_t **slot = NULL;
                if (sub != NULL) {
                    build_sub_path(p, context, sub, !from_start);
                    sub_list_t *list = from_start ? &p->hyp_start : &p->hyp_end;
                    if (ENG_VEC_PUSH(*list, slot)) {
                        *slot = sub;
                    } else {
                        sub_path_free(sub);
                    }
                }
                gp_linker_t *linker = p->it_info->linkers.data[p->it_linker - 1];
                if (from_start) {
                    indoor_link_t *link = &p->dyn_start->indoor[layer_index_of(gp, p->obstacle_layer)];
                    indoor_push(link, link->layer, linker, p->node_end->ctx[context].cost, 0, 0);
                } else {
                    linker->dyn[context] = p->dyn_end;
                    linker->dyn_cost[context] = p->node_end->ctx[context].cost;
                }
            }
        } else {
            if (p->it_linker != p->it_linker_end || p->node_end == NULL) {
                while (!linker_is_good(p, dyn, p->it_info->linkers.data[p->it_linker])) {
                    p->it_linker++;
                    if (p->it_linker == p->it_linker_end) {
                        break;
                    }
                }
            }
            if (p->it_linker == p->it_linker_end && p->node_end != NULL) {
                size_t found = from_start ? p->hyp_start.count : p->hyp_end.count;
                if (found == 0) {
                    if (p->grid_start == p->grid_end) {
                        p->start_part = false;
                        p->function = st_direct_path;
                        return 1;
                    }
                    p->function = st_path_not_found;
                    return 0;
                }
                p->start_part = false;
                if (from_start) {
                    p->function = st_linker_from_end;
                    return 1;
                }
                p->linker_end = NULL;
                reset_linker_contexts(gp, context);
                linker_ctx_t *ctx = &p->dyn_start->ctx[context];
                ctx->cost = 0.0f;
                ctx->f = 0.0f;
                ctx->parent = NULL;
                open_push(&p->open_linkers, p->dyn_start);
                p->node_end = NO_NODE;
                p->start_part = true;
                p->function = st_grid2grid;
                return 1;
            }
            init_before_case2case(gp, p, grid);
            reset_grid_contexts(gp, context, p->grid_w * p->grid_l);
            gp_linker_t *linker = p->it_info->linkers.data[p->it_linker];
            p->x_end_search = linker->x;
            p->y_end_search = linker->y;
            p->end_index = p->y_end_search * grid_width(engine, grid) + p->x_end_search;
            p->linker1 = linker;
            p->linker2 = NULL;
            push_start(gp, p, context, p->y_start_search * grid_width(engine, p->grid_search) + p->x_start_search,
                       start_heuristic(p->x_start_search, p->y_start_search, p->x_end_search, p->y_end_search));
            p->node_end = NULL;
            p->it_linker++;
        }
    }
}

static int st_linker_from_start(gp_t *gp, path_problem_t *p, int32_t context)
{
    return linkers_from(gp, p, context, true);
}

static int st_linker_from_end(gp_t *gp, path_problem_t *p, int32_t context)
{
    return linkers_from(gp, p, context, false);
}

/* DirectPath: the start and the end on one grid, no linker */
static int st_direct_path(gp_t *gp, path_problem_t *p, int32_t context)
{
    nmo_engine_t *engine = gp->engine;
    for (;;) {
        if (!p->start_part) {
            init_before_case2case(gp, p, p->grid_start);
            reset_grid_contexts(gp, context, p->grid_w * p->grid_l);
            p->linker1 = p->linker2 = NULL;
            gp_linker_t *node_start = p->dyn_start, *node_end = p->dyn_end;
            push_start(gp, p, context, node_end->y * grid_width(engine, node_end->grid) + node_end->x,
                       start_heuristic(node_end->x, node_end->y, node_start->x, node_start->y));
            p->x_end_search = node_start->x;
            p->y_end_search = node_start->y;
            p->end_index = node_start->y * grid_width(engine, node_start->grid) + node_start->x;
            p->start_part = true;
            p->node_end = NULL;
        } else {
            if (case2case(gp, p, context) == 0) {
                p->function = st_path_not_found;
                return 0;
            }
            if (p->node_end != NULL) {
                sub_path_t *sub = sub_path_new(p->grid_search, p->node_end->ctx[context].cost);
                if (sub != NULL) {
                    build_sub_path(p, context, sub, true);
                    path_add_back(&p->path, sub);
                }
                p->function = st_path_found;
                return 1;
            }
        }
    }
}

/* ManageNodeLinkerSuccesor */
static void linker_successor(path_problem_t *p, int32_t context, gp_linker_t *linker, gp_linker_t *succ, float distance)
{
    nmo_engine_t *engine = p->gp->engine;
    if (succ->linker_id >= 0 && !has_attribute(engine, p->target, succ->attribute)) {
        return;
    }
    int32_t value = 0;
    if (layer_value(engine, succ->grid, p->obstacle_layer, succ->x, succ->y, &value) && value > p->obstacle_threshold) {
        return;
    }
    if (linker->type == TC_DOOR && succ->type == TC_DOOR && linker->linker_id == succ->linker_id) {
        distance = magnitude(sub3(linker->pos, succ->pos));
    }
    linker_ctx_t *cs = &succ->ctx[context];
    float cost = linker->ctx[context].cost + distance;
    if ((cs->in_open || cs->closed) && cs->cost <= cost) {
        return;
    }
    cs->cost = cost;
    cs->f = cost;
    cs->parent = linker;
    cs->closed = false;
    if (!cs->in_open) {
        cs->in_open = true;
        open_push(&p->open_linkers, succ);
    } else {
        open_update(&p->open_linkers);
    }
}

/* FindHypPath: the start's way that ends at the first linker, the end's that starts at the last */
static void find_hyp_path(path_problem_t *p, const gp_linker_t *first, const gp_linker_t *last)
{
    int32_t index = first->y * grid_width(p->gp->engine, first->grid) + first->x;
    for (size_t i = 0; i < p->hyp_start.count; ++i) {
        sub_path_t *sub = p->hyp_start.data[i];
        if (sub->cases.count > 0 && sub->cases.data[sub->cases.count - 1u] == index) {
            p->sub_start = sub;
            memmove(&p->hyp_start.data[i], &p->hyp_start.data[i + 1u], (p->hyp_start.count - i - 1u) * sizeof(sub));
            p->hyp_start.count--;
            break;
        }
    }
    for (size_t i = 0; i < p->hyp_start.count; ++i) {
        sub_path_free(p->hyp_start.data[i]);
    }
    p->hyp_start.count = 0;
    index = last->y * grid_width(p->gp->engine, last->grid) + last->x;
    for (size_t i = 0; i < p->hyp_end.count; ++i) {
        sub_path_t *sub = p->hyp_end.data[i];
        if (sub->cases.count > 0 && sub->cases.data[0] == index) {
            p->sub_end = sub;
            memmove(&p->hyp_end.data[i], &p->hyp_end.data[i + 1u], (p->hyp_end.count - i - 1u) * sizeof(sub));
            p->hyp_end.count--;
            break;
        }
    }
    for (size_t i = 0; i < p->hyp_end.count; ++i) {
        sub_path_free(p->hyp_end.data[i]);
    }
    p->hyp_end.count = 0;
}

/* AnalyseGrid2GridPath: the linker path found, the linkers between which a grid is crossed kept */
static void analyse_grid2grid(path_problem_t *p, int32_t context)
{
    gp_linker_t *first = NULL, *second = NULL, *third = NULL, *fourth = NULL, *last;
    int32_t num_node = 0;
    gp_linker_t *linker = p->dyn_end;
    last = linker;
    while (linker != NULL) {
        ++num_node;
        fourth = third;
        third = second;
        second = first;
        first = linker;
        if ((num_node & 1) && num_node >= 3 && second->linker_id != first->linker_id) {
            third->ctx[context].parent = first;
            second = third;
            third = fourth;
            --num_node;
        }
        linker = linker->ctx[context].parent;
    }
    if (num_node == 2) {
        for (size_t i = 0; i < p->hyp_start.count; ++i) {
            sub_path_free(p->hyp_start.data[i]);
        }
        p->hyp_start.count = 0;
        for (size_t i = 0; i < p->hyp_end.count; ++i) {
            sub_path_free(p->hyp_end.data[i]);
        }
        p->hyp_end.count = 0;
        if (p->hyp_start_to_end == NULL) {
            p->function = st_path_not_found;
            return;
        }
        path_add_back(&p->path, p->hyp_start_to_end);
        p->hyp_start_to_end = NULL;
        p->function = st_path_found;
        return;
    }
    find_hyp_path(p, second, last->ctx[context].parent);
    if (p->sub_end == NULL || p->sub_start == NULL || p->sub_start->cases.count == 0 ||
        p->sub_start->cases.data[0] !=
            p->dyn_start->y * grid_width(p->gp->engine, p->dyn_start->grid) + p->dyn_start->x) {
        p->function = st_path_not_found;
        return;
    }
    if (num_node == 4) {
        path_add_back(&p->path, p->sub_end);
        path_add_back(&p->path, p->sub_start);
        p->function = st_path_found;
        return;
    }
    third->ctx[context].parent = NULL;
    p->linker_end = p->linker_end->ctx[context].parent->ctx[context].parent;
    p->start_part = false;
    p->function = st_all_case2case;
}

/* GetGrid2GridPath: Dijkstra over the linkers, from the start to the end */
static int st_grid2grid(gp_t *gp, path_problem_t *p, int32_t context)
{
    nmo_engine_t *engine = gp->engine;
    for (;;) {
        if (p->open_linkers.items.count == 0) {
            p->function = st_path_not_found;
            return 0;
        }
        gp_linker_t *linker = (gp_linker_t *)open_pop(&p->open_linkers);
        linker->ctx[context].in_open = false;
        if (linker == p->dyn_end) {
            p->linker_end = p->dyn_end;
            analyse_grid2grid(p, context);
            return 2;
        }
        float cost = 1.0f;
        if (linker->linker_id != -1) {
            const eng_attribute_t *attribute =
                linker->attribute >= 0 ? eng_attribute_find(engine, p->target, linker->attribute) : NULL;
            if (attribute != NULL && attribute->has_value && attribute->value.size >= sizeof(float)) {
                memcpy(&cost, attribute->value.data, sizeof(float));
                if (cost <= 0.0f) {
                    cost = 0.001f;
                }
            }
        }
        for (size_t i = 0; i < linker->connected.count; i++) {
            linker_successor(p, context, linker, linker->connected.data[i], cost);
        }
        if (linker->indoor != NULL) {
            indoor_link_t *link = &linker->indoor[layer_index_of(gp, p->obstacle_layer)];
            for (size_t i = 0; i < link->same.count; i++) {
                if (link->max_layer_cost.data[i] <= p->obstacle_threshold) {
                    linker_successor(p, context, linker, link->same.data[i],
                                     link->cost.data[i] + (float)link->layer_cost.data[i] * p->slowing_div_threshold);
                }
            }
        }
        if (linker->dyn[context] != NULL) {
            linker_successor(p, context, linker, linker->dyn[context], linker->dyn_cost[context]);
        }
        linker->ctx[context].closed = true;
    }
}

/* GetAllCase2CasePath: the squares between each pair of linkers on the way */
static int st_all_case2case(gp_t *gp, path_problem_t *p, int32_t context)
{
    for (;;) {
        if (!p->start_part) {
            path_add_front(&p->path, p->sub_end);
            p->node_end = NO_NODE;
            p->start_part = true;
        }
        if (p->linker_end == NULL && p->node_end != NULL) {
            path_add_back(&p->path, p->sub_start);
            p->start_part = false;
            p->function = st_path_found;
            return 1;
        }
        if (p->node_end == NULL) {
            if (case2case(gp, p, context) == 0) {
                p->function = st_path_not_found;
                return 0;
            }
            if (p->node_end != NULL) {
                sub_path_t *sub = sub_path_new(p->grid_search, p->node_end->ctx[context].cost);
                if (sub != NULL) {
                    build_sub_path(p, context, sub, true);
                    path_add_back(&p->path, sub);
                }
            }
        } else {
            gp_linker_t *end = p->linker_end;
            gp_linker_t *parent = end->ctx[context].parent;
            init_before_case2case(gp, p, end->grid);
            reset_grid_contexts(gp, context, p->grid_w * p->grid_l);
            p->linker1 = end;
            p->linker2 = parent;
            p->x_start_search = end->x;
            p->y_start_search = end->y;
            p->x_end_search = parent->x;
            p->y_end_search = parent->y;
            p->end_index = p->y_end_search * p->grid_w + p->x_end_search;
            int32_t index = p->y_start_search * p->grid_w + p->x_start_search;
            if (index >= 0 && index < gp->node_count) {
                gp->nodes[index].ctx[context].parent = NULL;
                open_push(&p->open_grid, &gp->nodes[index]);
            }
            p->node_end = NULL;
            p->linker_end = parent->ctx[context].parent;
        }
    }
}

/* ObsBetweenPoint: a wall (or linker) on the squares the line between two squares crosses */
static bool obstacle_between(gp_t *gp, path_problem_t *p, int32_t start, int32_t end, int32_t index_max)
{
    int32_t w = p->grid_w;
    div_t dt = div(start, w);
    int32_t y1 = dt.quot, x1 = dt.rem;
    dt = div(end, w);
    int32_t y2 = dt.quot, x2 = dt.rem;
    uint32_t obs = (uint32_t)p->obstacle_threshold;
    int32_t num_grid = p->num_grid;
    if (!p->linker_obs && p->square == NULL) {
        return false;
    }
#define BLOCKED(index)                                                                                             \
    ((p->square != NULL && (uint32_t)square_at(p, (index)) > obs) ||                                              \
     (p->linker_obs && (index) >= 0 && (index) < gp->node_count && gp->nodes[(index)].linker_obs[num_grid] != NULL))
    if (x1 == x2) {
        if (y1 > y2) {
            int32_t t = y1;
            y1 = y2;
            y2 = t;
        }
        for (int32_t j = y1; j <= y2; j++) {
            int32_t index = j * w + x1;
            if (BLOCKED(index)) {
                return true;
            }
        }
        return false;
    }
    if (x1 > x2) {
        int32_t t = x1;
        x1 = x2;
        x2 = t;
        t = y1;
        y1 = y2;
        y2 = t;
    }
    float x1bis = (float)-x1 + 0.5f;
    float x1bis2 = x1bis + 1.0f;
    float dy = (float)(y2 - y1) / (float)(x2 - x1);
    int32_t ya = y1, yb = y1 + (int32_t)ceilf(((float)x1 + x1bis) * dy);
    if (ya > yb) {
        int32_t t = ya;
        ya = yb;
        yb = t;
    }
    yb = (int32_t)ceilf((float)yb);
    for (int32_t j = ya; j <= yb; ++j) {
        int32_t index = j * w + x1;
        if (index == start || index == end || index >= index_max || index < 0) {
            continue;
        }
        if (BLOCKED(index)) {
            return true;
        }
    }
    for (int32_t i = x1; i < x2 - 1; ++i) {
        ya = (int32_t)((float)y1 + ((float)i + x1bis) * dy);
        yb = (int32_t)ceilf((float)y1 + ((float)i + x1bis2) * dy);
        if (ya > yb) {
            int32_t t = ya;
            ya = yb;
            yb = t;
        }
        for (int32_t j = ya; j <= yb; ++j) {
            int32_t index = j * w + i + 1;
            if (index >= index_max || index < 0) {
                continue;
            }
            if (BLOCKED(index)) {
                return true;
            }
        }
    }
    ya = (int32_t)ceilf((float)y1 + ((float)(x2 - 2) + x1bis2) * dy);
    yb = y2;
    if (ya > yb) {
        int32_t t = ya;
        ya = yb;
        yb = t;
    }
    for (int32_t j = ya; j <= yb; ++j) {
        int32_t index = j * w + x2;
        if (index == start || index == end || index >= index_max || index < 0) {
            continue;
        }
        if (BLOCKED(index)) {
            return true;
        }
    }
#undef BLOCKED
    return false;
}

/* OptimizePath: the squares between two that see each other dropped */
static void optimize_path(gp_t *gp, path_problem_t *p)
{
    int32_t size = (int32_t)p->path.subs.count;
    for (int32_t i = 0; i < size; ++i) {
        sub_path_t *sub = path_sub(&p->path, i);
        eng_layer_t *layer = grid_layer(gp->engine, sub->grid, p->obstacle_layer);
        p->square = layer != NULL ? layer->squares : NULL;
        p->square_count = layer != NULL ? layer->count : 0;
        p->grid_w = grid_width(gp->engine, sub->grid);
        p->num_grid = sub->num_grid;
        int32_t index_max = p->grid_w * grid_length(gp->engine, sub->grid);
        int32_t count = (int32_t)sub->cases.count;
        for (int32_t j = 0; j < count - 2; ++j) {
            int32_t next = j + 1, k = j + 2;
            while (k < count && !obstacle_between(gp, p, sub->cases.data[j], sub->cases.data[k], index_max)) {
                next = k;
                ++k;
            }
            for (int32_t r = j + 1; r < next; ++r) {
                memmove(&sub->cases.data[j + 1], &sub->cases.data[j + 2],
                        (size_t)(count - j - 2) * sizeof(int32_t));
                sub->cases.count--;
                --count;
            }
        }
    }
}

static int st_path_found(gp_t *gp, path_problem_t *p, int32_t context)
{
    (void)context;
    for (size_t i = 0; i < p->path.subs.count; ++i) {
        sub_path_t *sub = path_sub(&p->path, (int32_t)i);
        grid_info_t *info = info_of(gp, sub->grid);
        sub->num_grid = info != NULL ? info->num : 0;
    }
    p->path.last_point = p->goal_pos;
    p->path.linker_obs = p->linker_obs;
    if (p->optimize) {
        optimize_path(gp, p);
    }
    p->state = 2;
    p->function = st_finish;
    return 2;
}

static int st_path_not_found(gp_t *gp, path_problem_t *p, int32_t context)
{
    (void)gp;
    (void)context;
    p->state = 3;
    p->function = st_finish;
    return 0;
}

static int st_finish(gp_t *gp, path_problem_t *p, int32_t context)
{
    (void)gp;
    (void)p;
    (void)context;
    return 0;
}

/* ManagePathFinding: each problem searched to its end */
static void manage_path_finding(gp_t *gp)
{
    int32_t left = gp->num_path;
    for (int32_t i = 0; i < gp->max_path && i < (int32_t)gp->problems.count; ++i) {
        if (left == 0) {
            return;
        }
        path_problem_t *p = gp->problems.data[i];
        if (p->function != st_finish) {
            --left;
            do {
                p->function(gp, p, i);
            } while (p->state == 1 && p->function != st_finish);
        }
    }
}

/* ============================================================================
 * Path recovery
 * ============================================================================ */

/* The path's squares, from the start, in the world */
static size_t path_points(nmo_engine_t *engine, const gp_path_t *path, eng_vector_t **out)
{
    size_t total = 0;
    for (size_t i = 0; i < path->subs.count; i++) {
        total += path->subs.data[i]->cases.count;
    }
    eng_vector_t *points = total > 0 ? (eng_vector_t *)malloc(total * sizeof(eng_vector_t)) : NULL;
    size_t n = 0;
    for (int32_t i = (int32_t)path->subs.count - 1; points != NULL && i >= 0; --i) {
        const sub_path_t *sub = path_sub(path, i);
        int32_t w = grid_width(engine, sub->grid);
        for (int32_t j = 0; j < (int32_t)sub->cases.count; ++j) {
            div_t dt = div(sub_case(sub, j), w);
            points[n++] = grid_position(engine, sub->grid, dt.rem, dt.quot);
        }
    }
    *out = points;
    return points != NULL ? n : 0;
}

/* GetPath (data array): row by row from the start, rows added as needed */
static void path_to_array(nmo_engine_t *engine, const gp_path_t *path, nmo_object_id_t array)
{
    eng_vector_t *points = NULL;
    size_t count = path_points(engine, path, &points);
    for (size_t i = 0; i < count; i++) {
        if (nmo_engine_array_row_count(engine, array) <= (int32_t)i) {
            nmo_engine_array_insert_row(engine, array, -1, NULL);
        }
        nmo_engine_cell_t cell = {.type = CKARRAYTYPE_PARAMETER, .data = &points[i], .size = sizeof(points[i]),
                                  .parameter_type = CKPGUID_VECTOR};
        nmo_engine_array_set_cell(engine, array, (int32_t)i, 0, &cell);
    }
    free(points);
}

/* GetPath (curve): a linear control point at each square, points made or removed as needed */
static void path_to_curve(nmo_engine_t *engine, const gp_path_t *path, nmo_object_id_t curve)
{
    eng_vector_t *points = NULL;
    size_t count = path_points(engine, path, &points);
    const eng_curve_points_t *old = eng_curve_points(engine, curve);
    eng_curve_points_t had = {NULL, 0};
    if (old != NULL) {
        eng_curve_clone_points(&had, old);
    }
    nmo_object_id_t *ids = count > 0 ? (nmo_object_id_t *)malloc(count * sizeof(nmo_object_id_t)) : NULL;
    size_t made = 0;
    for (size_t i = 0; i < count && ids != NULL; i++) {
        nmo_object_id_t point = i < had.count ? had.ids[i] : eng_create_object(engine, NMO_CID_CURVEPOINT, "");
        if (point == 0) {
            break;
        }
        eng_curvepoint_properties_t properties;
        if (!eng_curvepoint_get_properties(engine, point, &properties)) {
            memset(&properties, 0, sizeof(properties));
        }
        properties.linear = 1;
        eng_curvepoint_set_properties(engine, point, &properties);
        int32_t entity = eng_entity_index(engine, point);
        if (entity >= 0) {
            eng_entity_set_position(engine, entity, &points[i], -1, false);
        }
        ids[made++] = point;
    }
    eng_curve_points_t now = {ids, made};
    eng_curve_set_points(engine, curve, &now);
    for (size_t i = made; i < had.count; i++) {
        nmo_engine_destroy_object(engine, had.ids[i]);
    }
    free(had.ids);
    free(ids);
    free(points);
}

/* ============================================================================
 * The follower
 * ============================================================================ */

static nmo_object_id_t charac_of(nmo_engine_t *engine, nmo_object_id_t character)
{
    nmo_object_id_t floor = eng_character_floor_reference(engine, character);
    if (floor == 0) {
        floor = eng_character_root(engine, character);
    }
    return floor != 0 ? floor : character;
}

static eng_vector_t world_position(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = entity_of(engine, id);
    return index >= 0 ? eng_entity_position(engine, index, -1) : (eng_vector_t){0.0f, 0.0f, 0.0f};
}

static void send_joy(nmo_engine_t *engine, follow_problem_t *f, int which)
{
    static const char *const names[4] = {"Joy_Up", "Joy_Down", "Joy_Left", "Joy_Right"};
    eng_message_t message;
    memset(&message, 0, sizeof(message));
    message.type = eng_message_type_add(engine, names[which]);
    message.sending = ENG_MESSAGE_SINGLE;
    message.sender = f->target;
    message.recipient = f->target;
    message.sent_by = -1;
    if (message.type >= 0) {
        eng_send(engine, &message);
    }
}

static void follow_output(nmo_engine_t *engine, follow_problem_t *f, int pos)
{
    nmo_engine_block_t handle = {engine, f->node};
    if (pos < nmo_engine_block_output_count(&handle)) {
        nmo_engine_block_activate_output(&handle, pos, true);
    }
}

static void follow_output_object(nmo_engine_t *engine, follow_problem_t *f, int pos, nmo_object_id_t object)
{
    nmo_engine_block_t handle = {engine, f->node};
    nmo_engine_block_set_output_value(&handle, pos, &object, sizeof(object));
}

static float follow_time(nmo_engine_t *engine, double since)
{
    return (float)(engine->time - since);
}

static void align1(gp_t *gp, follow_problem_t *f);
static void fs_compute_dir(gp_t *gp, follow_problem_t *f);
static void fs_turn_and_up(gp_t *gp, follow_problem_t *f);

/* The grid of the sub-path, and what is measured on it */
static void follow_take_grid(nmo_engine_t *engine, follow_problem_t *f)
{
    f->total_point = (int32_t)f->sub->cases.count;
    f->grid = f->sub->grid;
    f->layer_type = f->path.obstacle_layer;
    int32_t index = entity_of(engine, f->grid);
    f->grid_scale = index >= 0 ? eng_entity_scale(engine, index, true) : (eng_vector_t){1.0f, 1.0f, 1.0f};
    f->grid_scale.y = 0.0f;
    f->radius_cgrid = f->radius_collision / f->grid_scale.x;
    f->min_distance = magnitude(f->grid_scale) * f->min_distance_factor;
    f->w = grid_width(engine, f->grid);
    f->l = grid_length(engine, f->grid);
}

static void fuzz(nmo_engine_t *engine, follow_problem_t *f)
{
    if (f->fuzzyness != 0.0f) {
        f->next_point = world_to_grid(engine, f->grid, f->next_point);
        f->next_point.x += (float)eng_random_u15(engine) * f->fuzzyness - f->fuzzyness2;
        f->next_point.z += (float)eng_random_u15(engine) * f->fuzzyness - f->fuzzyness2;
        f->next_point = grid_to_world(engine, f->grid, f->next_point);
    }
}

static void real_dir_from_points(follow_problem_t *f)
{
    f->next_point.y = 0.0f;
    f->real_dir = sub3(f->next_point, f->next_point_fuzzy);
    f->distance_to_point = magnitude(f->real_dir);
    f->real_dir.y = 0.0f;
    eng_vector_normalize(&f->real_dir);
}

/* PointAndDir2: towards the second square, from where it stands */
static void point_and_dir2(nmo_engine_t *engine, follow_problem_t *f)
{
    div_t dt = div(sub_case(f->sub, 1), f->w);
    f->next_y = dt.quot;
    f->next_x = dt.rem;
    f->next_point = grid_position(engine, f->grid, f->next_x, f->next_y);
    fuzz(engine, f);
    f->next_point.y = 0.0f;
    f->next_point_fuzzy = f->position;
    real_dir_from_points(f);
}

/* PointAndDir: towards the next square (the goal itself for the last) */
static void point_and_dir(nmo_engine_t *engine, follow_problem_t *f)
{
    f->next_point_fuzzy = f->next_point;
    if (f->num_sub_path == 0 && f->num_point == f->total_point - 1) {
        eng_vector_t last = world_to_grid(engine, f->grid, f->path.last_point);
        last.y = 0.0f;
        f->next_point = grid_to_world(engine, f->grid, last);
    } else {
        div_t dt = div(sub_case(f->sub, f->num_point), f->w);
        f->next_y = dt.quot;
        f->next_x = dt.rem;
        f->next_point = grid_position(engine, f->grid, f->next_x, f->next_y);
    }
    fuzz(engine, f);
    real_dir_from_points(f);
}

/* FirstOrient: from the path's first sub-path (after `first`) */
static void first_orient(nmo_engine_t *engine, follow_problem_t *f, int32_t first)
{
    f->num_sub_path = (int32_t)f->path.subs.count - 1 - first;
    f->sub = path_sub(&f->path, f->num_sub_path);
    if (f->num_sub_path != 0) {
        f->next_sub = path_sub(&f->path, f->num_sub_path - 1);
    }
    follow_take_grid(engine, f);
    f->charac = charac_of(engine, f->target);
    f->real_position = world_position(engine, f->charac);
    f->position = f->real_position;
    f->position.y = 0.0f;
    f->old_position = f->position;
    f->pos_to_move_from = f->position;
    f->num_point = 1;
    point_and_dir2(engine, f);
}

static void fs_enter_tele(gp_t *gp, follow_problem_t *f);
static void fs_travel_tele(gp_t *gp, follow_problem_t *f);
static void fs_exit_tele(gp_t *gp, follow_problem_t *f);

static void fs_enter_tele(gp_t *gp, follow_problem_t *f)
{
    (void)gp;
    nmo_engine_t *engine = gp->engine;
    if (f->next_sub->linker_start->occupy) {
        f->time_tele = engine->time;
    }
    if (follow_time(engine, f->time_tele) < f->enter_tele_time) {
        return;
    }
    f->state = FS_TRAVELTELE;
    f->function = fs_travel_tele;
    eng_show(engine, f->target, 2); /* CKHIERARCHICALHIDE */
    f->invisible = true;
    f->time_tele = engine->time;
    if (f->manage_teleporter) {
        follow_output(engine, f, 5);
    }
    f->next_sub->linker_start->occupy = true;
}

static void fs_travel_tele(gp_t *gp, follow_problem_t *f)
{
    (void)gp;
    nmo_engine_t *engine = gp->engine;
    if (follow_time(engine, f->time_tele) < f->travel_tele_time) {
        return;
    }
    f->state = FS_EXITTELE;
    f->function = fs_exit_tele;
    int32_t index = entity_of(engine, f->target);
    if (index >= 0) {
        eng_entity_set_position(engine, index, &f->position, -1, false);
    }
    f->real_position = f->position;
    eng_show(engine, f->target, 1);
    f->invisible = false;
    f->time_tele = engine->time;
    if (f->manage_teleporter) {
        follow_output(engine, f, 6);
    }
}

static void fs_exit_tele(gp_t *gp, follow_problem_t *f)
{
    (void)gp;
    nmo_engine_t *engine = gp->engine;
    if (follow_time(engine, f->time_tele) < f->exit_tele_time) {
        return;
    }
    f->next_sub->linker_start->occupy = false;
    f->state = FS_ON;
    f->function = fs_compute_dir;
    point_and_dir2(engine, f);
}

/* ComputeDir: the next square once near enough to this one, and the way to go */
static void fs_compute_dir(gp_t *gp, follow_problem_t *f)
{
    (void)gp;
    nmo_engine_t *engine = gp->engine;
    f->charac = charac_of(engine, f->target);
    f->real_position = world_position(engine, f->charac);
    f->position = f->real_position;
    f->position.y = 0.0f;
    int32_t grid_entity = entity_of(engine, f->grid);
    if (grid_entity >= 0 && (engine->entities.data[grid_entity].moveable_flags & VX_MOVEABLE_HASMOVED) != 0) {
        point_and_dir(engine, f);
    }
    f->distance = magnitude(sub3(f->next_point, f->position));
    if (f->distance < f->min_distance) {
        f->old_type_case = f->sub->linker_start != NULL ? f->sub->linker_start->type : TC_NORMAL;
        ++f->num_point;
        if (f->num_point == f->total_point) {
            if (--f->num_sub_path < 0 || f->old_type_case == TC_TELE2) {
                if (f->pingpong) {
                    path_inverse(&f->path);
                    first_orient(engine, f, f->old_type_case == TC_TELE2 ? ++f->num_sub_path : 0);
                    return;
                }
                f->state = FS_FINISH;
                return;
            }
            f->sub = path_sub(&f->path, f->num_sub_path);
            if (f->num_sub_path != 0) {
                f->next_sub = path_sub(&f->path, f->num_sub_path - 1);
            }
            follow_take_grid(engine, f);
            if (f->old_type_case != TC_DOOR) {
                f->num_point = 1;
                div_t dt = div(sub_case(f->sub, 0), f->w);
                f->next_y = dt.quot;
                f->next_x = dt.rem;
                f->position = grid_position(engine, f->grid, f->next_x, f->next_y);
                f->time_tele = engine->time;
                f->state = FS_ENTERTELE;
                f->function = fs_enter_tele;
                if (f->manage_teleporter) {
                    follow_output(engine, f, 4);
                }
                fs_enter_tele(gp, f);
                return;
            }
            f->num_point = 0;
        }
        point_and_dir(engine, f);
    }
    eng_vector_t vector = sub3(f->position, f->next_point_fuzzy);
    eng_vector_t point = add3(f->next_point_fuzzy, scale3(f->real_dir, dot3(vector, f->real_dir) + f->grid_scale.x));
    point.y = 0.0f;
    if (magnitude(sub3(f->next_point_fuzzy, point)) < f->distance_to_point) {
        f->dir = sub3(point, f->position);
    } else {
        f->dir = sub3(f->next_point, f->position);
    }
    f->dir.y = 0.0f;
    eng_vector_normalize(&f->dir);
}

static void translate_target(nmo_engine_t *engine, follow_problem_t *f, eng_vector_t t, nmo_object_id_t ref)
{
    int32_t index = entity_of(engine, f->target);
    int32_t ref_index = ref != 0 ? entity_of(engine, ref) : -1;
    if (index >= 0) {
        eng_entity_translate(engine, index, &t, ref_index, false);
    }
}

/* RejectByObstacle: out of the nearest wall (or linker) square it overlaps */
static void reject_by_obstacle(gp_t *gp, follow_problem_t *f)
{
    nmo_engine_t *engine = gp->engine;
    if (f->num_point == 0) {
        return;
    }
    float dist = 999999999.0f;
    int32_t si = -1, sj = 0;
    eng_vector_t pos = world_to_grid(engine, f->grid, f->real_position);
    float min_x = pos.x - f->radius_cgrid - 1.0f, min_z = pos.z - f->radius_cgrid - 1.0f;
    float max_x = pos.x + f->radius_cgrid + 1.0f, max_z = pos.z + f->radius_cgrid + 1.0f;
    if (min_x < 0.0f) {
        min_x = 0.0f;
    }
    if (max_x >= (float)f->w) {
        max_x = (float)f->w - 1.0f;
    }
    if (min_z < 0.0f) {
        min_z = 0.0f;
    }
    if (max_z >= (float)f->l) {
        max_z = (float)f->l - 1.0f;
    }
    for (int32_t i = (int32_t)min_x; i <= (int32_t)max_x; ++i) {
        for (int32_t j = (int32_t)min_z; j <= (int32_t)max_z; ++j) {
            int32_t value = 0;
            layer_value(engine, f->grid, f->layer_type, i, j, &value);
            eng_vector_t centre = {(float)i + 0.5f, 0.0f, (float)j + 0.5f};
            float d2;
            if (value > f->path.obstacle_threshold) {
                if ((d2 = magnitude(sub3(pos, centre))) < dist) {
                    si = i;
                    sj = j;
                    dist = d2;
                }
            } else if (f->linker_obs) {
                int32_t index = j * f->w + i;
                gp_linker_t *linker = index >= 0 && index < gp->node_count && f->sub->num_grid >= 0
                                          ? gp->nodes[index].linker_obs[f->sub->num_grid]
                                          : NULL;
                if (linker != NULL && linker->type != TC_DOOR &&
                    !(index == sub_case(f->sub, 0) || index == sub_case(f->sub, f->total_point - 1)) &&
                    (d2 = magnitude(sub3(pos, centre))) < dist) {
                    si = i;
                    sj = j;
                    dist = d2;
                }
            }
        }
    }
    if (si == -1) {
        return;
    }
    eng_vector_t trans = {0.0f, 0.0f, 0.0f};
    eng_vector_t vector = sub3(pos, (eng_vector_t){(float)si + 0.5f, 0.0f, (float)sj + 0.5f});
    eng_vector_normalize(&vector);
    float y;
    if (vector.x != 0.0f) {
        float slope = vector.z / vector.x;
        float q = (float)sj + 0.5f - slope * ((float)si + 0.5f);
        y = vector.x > 0.0f ? slope * ((float)si + 1.0f) + q : slope * (float)si + q;
    } else {
        y = (float)sj + 2.0f;
    }
    if (y > (float)(sj + 1) || y < (float)sj) {
        if (vector.z > 0.0f) {
            trans.z = pos.z - (float)(sj + 1);
            if (trans.z >= f->radius_cgrid) {
                return;
            }
            trans.z = f->radius_cgrid - trans.z;
        } else {
            trans.z = (float)sj - pos.z;
            if (trans.z >= f->radius_cgrid) {
                return;
            }
            trans.z = -f->radius_cgrid + trans.z;
        }
    } else {
        if (vector.x > 0.0f) {
            trans.x = pos.x - (float)(si + 1);
            if (trans.x >= f->radius_cgrid) {
                return;
            }
            trans.x = f->radius_cgrid - trans.x;
        } else {
            trans.x = (float)si - pos.x;
            if (trans.x >= f->radius_cgrid) {
                return;
            }
            trans.x = -f->radius_cgrid + trans.x;
        }
    }
    translate_target(engine, f, trans, f->grid);
}

/* ScanRadius (followers): the last follower of the same grid in collision, and in avoid range */
static bool scan_followers(gp_t *gp, follow_problem_t *f, follow_problem_t **collision, follow_problem_t **avoid)
{
    *collision = NULL;
    *avoid = NULL;
    if (f->collision_filter == 0) {
        return false;
    }
    for (size_t i = 0; i < gp->follows.count; ++i) {
        follow_problem_t *other = gp->follows.data[i];
        if (other->grid != f->grid || other->target == f->target || (other->collision_filter & f->collision_filter) == 0) {
            continue;
        }
        float distance = magnitude(sub3(other->position, f->position));
        if (distance <= f->radius_collision + other->radius_collision) {
            *collision = other;
        }
        if (distance <= f->radius_avoid + other->radius_collision) {
            *avoid = other;
        }
        if (*collision != NULL && *avoid != NULL) {
            return true;
        }
    }
    return false;
}

/* ScanRadius (entities): the Path Finding Obstacle entities of the scene in range */
static bool scan_entities(nmo_engine_t *engine, follow_problem_t *f, int32_t attribute, nmo_object_id_t *collision,
                          nmo_object_id_t *avoid, float *radius)
{
    *collision = 0;
    *avoid = 0;
    if (f->collision_filter == 0) {
        return false;
    }
    for (size_t i = 0; attribute >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *value = &engine->attributes.values.data[i];
        nmo_object_id_t entity = value->object;
        if (value->type != attribute || entity == f->target || !eng_in_scene(engine, entity)) {
            continue;
        }
        int32_t filter = 0, radius_bits = 0;
        attribute_field(engine, entity, attribute, 0, &filter);
        if ((filter & f->collision_filter) == 0) {
            continue;
        }
        attribute_field(engine, entity, attribute, 1, &radius_bits);
        *radius = bits_float(radius_bits);
        eng_vector_t pos = world_position(engine, entity);
        pos.y = 0.0f;
        float distance = magnitude(sub3(pos, f->position));
        if (distance <= f->radius_collision + *radius) {
            *collision = entity;
        }
        if (distance <= f->radius_avoid + *radius) {
            *avoid = entity;
        }
        if (*collision != 0 && *avoid != 0) {
            return true;
        }
    }
    return false;
}

/* AnalyseTarget: where the other is, which way it goes and whether it is faster */
static void analyse_target(follow_problem_t *f, follow_problem_t *other, int32_t *pos, int32_t *dir, int32_t *speed)
{
    eng_vector_t perp = {f->dir1.z, 0.0f, -f->dir1.x};
    f->dir_fp = sub3(other->position, f->position);
    f->dir_fp.y = 0.0f;
    eng_vector_normalize(&f->dir_fp);
    eng_vector_t perp_fp = {-f->dir_fp.z, 0.0f, f->dir_fp.x};
    float angle_p1 = dot3(f->dir1, f->dir_fp);
    float angle_p2 = dot3(perp_fp, other->dir1);
    float angle_d1 = dot3(f->dir1, other->dir1);
    float angle_d2 = dot3(perp, other->dir1);
    *pos = angle_p2 < 0.0f ? 0 : 1;
    if (angle_p1 < -0.707) {
        *pos += 6;
    } else if (angle_p1 < 0.0f) {
        *pos += 4;
    } else if (angle_p1 < 0.707) {
        *pos += 2;
    }
    *dir = angle_d2 > 0.0f ? 0 : 1;
    if (angle_d1 < -0.707) {
        *dir += 6;
    } else if (angle_d1 < 0.0f) {
        *dir += 4;
    } else if (angle_d1 < 0.707) {
        *dir += 2;
    }
    float speed1 = magnitude(sub3(f->position, f->old_position));
    float speed2 = magnitude(sub3(other->position, other->old_position));
    if (fabsf(speed1 - speed2) < 0.001) {
        *speed = 0;
    } else {
        *speed = speed2 > speed1 ? 1 : -1;
    }
}

static void reject_by(nmo_engine_t *engine, follow_problem_t *f, eng_vector_t other_position, float other_radius)
{
    eng_vector_t correct = sub3(f->position, other_position);
    correct.y = 0.0f;
    float factor = f->radius_collision + other_radius - magnitude(correct);
    eng_vector_normalize(&correct);
    translate_target(engine, f, scale3(correct, factor), 0);
}

/* DetectBlocked: timeout once it stayed within Blocked Distance for Max Blocked Time */
static void detect_blocked(nmo_engine_t *engine, follow_problem_t *f)
{
    if (magnitude(sub3(f->position, f->pos_to_move_from)) > f->distance_blocked) {
        f->pos_to_move_from = f->position;
        f->time_blocked = engine->time;
    } else if (follow_time(engine, f->time_blocked) >= f->max_blocked_time) {
        f->state = FS_TIMEOUT;
        f->pos_to_move_from = f->position;
    }
}

static void fs_turn_and_up2(gp_t *gp, follow_problem_t *f);

/* CorrectDirection: out of the others and the walls, round the ones ahead */
static void correct_direction(gp_t *gp, follow_problem_t *f, int32_t obstacle_attribute)
{
    nmo_engine_t *engine = gp->engine;
    if (f->state != FS_ON && f->state != FS_SLOW) {
        return;
    }
    f->move = fs_turn_and_up;
    f->state = FS_ON;
    follow_problem_t *fp_collision = NULL, *fp_avoid = NULL;
    nmo_object_id_t ent_collision = 0, ent_avoid = 0;
    float ent_radius = 0.0f;
    if (!scan_followers(gp, f, &fp_collision, &fp_avoid)) {
        scan_entities(engine, f, obstacle_attribute, &ent_collision, &ent_avoid, &ent_radius);
    }
    if (fp_collision != NULL) {
        reject_by(engine, f, fp_collision->position, fp_collision->radius_collision);
        follow_output(engine, f, 2);
        follow_output_object(engine, f, 0, fp_collision->target);
    } else if (ent_collision != 0) {
        eng_vector_t pos = world_position(engine, ent_collision);
        pos.y = 0.0f;
        reject_by(engine, f, pos, ent_radius);
        follow_output(engine, f, 2);
        follow_output_object(engine, f, 0, ent_collision);
    }
    if (fp_avoid != NULL) {
        int32_t pos, dir, speed;
        analyse_target(f, fp_avoid, &pos, &dir, &speed);
        if (pos <= 3 && dir > 2) { /* AvoidTarget */
            f->dir = (pos & 1) ? f->dir2 : scale3(f->dir2, -1.0f);
            f->test_turn_angle_count = 10;
            f->move = fs_turn_and_up2;
        }
        follow_output(engine, f, 3);
        follow_output_object(engine, f, 1, fp_avoid->target);
    } else if (ent_avoid != 0) {
        eng_vector_t pos = world_position(engine, ent_avoid);
        pos.y = 0.0f;
        eng_vector_t to = sub3(pos, f->position);
        if (!(dot3(to, f->dir1) < 0.5f)) {
            f->dir = dot3(to, f->dir2) > 0.0f ? scale3(f->dir2, -1.0f) : f->dir2;
            f->test_turn_angle_count = 100;
            f->move = fs_turn_and_up2;
        }
        follow_output(engine, f, 3);
        follow_output_object(engine, f, 1, ent_avoid);
    }
    reject_by_obstacle(gp, f);
    detect_blocked(engine, f);
}

/* AlignOnPath1..6: the character's forward (dir1) and side (dir2) by the Character Direction,
   from its GetOrientation (dir, up, right) */
static void orientation(nmo_engine_t *engine, follow_problem_t *f, int direction)
{
    eng_vector_t d = {0.0f, 0.0f, 0.0f}, u = {0.0f, 0.0f, 0.0f}, r = {0.0f, 0.0f, 0.0f};
    int32_t index = entity_of(engine, f->target);
    if (index >= 0) {
        eng_entity_orientation(engine, index, &d, &u, &r, -1);
    }
    switch (direction) {
    case 1:
        f->dir2 = scale3(d, -1.0f);
        f->dir1 = r;
        break;
    case 2:
        f->dir2 = d;
        f->dir1 = scale3(r, -1.0f);
        break;
    case 3:
        f->dir1 = u;
        f->dir2 = r;
        break;
    case 4:
        f->dir1 = scale3(u, -1.0f);
        f->dir2 = scale3(r, -1.0f);
        break;
    case 5:
        f->dir1 = d;
        f->dir2 = r;
        break;
    default:
        f->dir1 = scale3(d, -1.0f);
        f->dir2 = scale3(r, -1.0f);
        break;
    }
}

#define ALIGN_FN(n)                                                                                 \
    static void align##n(gp_t *gp, follow_problem_t *f)                                             \
    {                                                                                               \
        (void)gp;                                                                                   \
        orientation(gp->engine, f, n);                                          \
    }
ALIGN_FN(1)
ALIGN_FN(2)
ALIGN_FN(3)
ALIGN_FN(4)
ALIGN_FN(5)
ALIGN_FN(6)

static void fs_turn_and_up(gp_t *gp, follow_problem_t *f)
{
    nmo_engine_t *engine = gp->engine;
    f->old_position = f->position;
    if (f->state != FS_ON) {
        return;
    }
    f->align(gp, f);
    float angle1 = dot3(f->dir1, f->dir), angle2 = dot3(f->dir2, f->dir);
    if (angle1 < 0.9f) {
        send_joy(engine, f, angle2 < 0.0f ? 2 : 3);
    }
    if (f->test_turn_angle_count != 0 || angle1 > f->limit_angle_cos) {
        send_joy(engine, f, 0);
    }
    if (f->test_turn_angle_count != 0) {
        --f->test_turn_angle_count;
    }
}

static void fs_turn_and_up2(gp_t *gp, follow_problem_t *f)
{
    nmo_engine_t *engine = gp->engine;
    f->old_position = f->position;
    if (f->state != FS_ON) {
        return;
    }
    f->align(gp, f);
    float angle1 = dot3(f->dir1, f->dir), angle2 = dot3(f->dir2, f->dir);
    if (angle1 < 0.99f) {
        send_joy(engine, f, angle2 < 0.0f ? 2 : 3);
    }
    send_joy(engine, f, 0);
}

typedef struct follow_settings {
    nmo_object_id_t target;
    int32_t node;
    int32_t path_id;
    int32_t direction;
    float min_distance_factor, limit_angle, fuzzyness;
    bool pingpong;
    float max_blocked_time, distance_blocked;
    float enter_tele, travel_tele, exit_tele;
} follow_settings_t;

/* FollowProblem::Set */
static void follow_set(nmo_engine_t *engine, follow_problem_t *f, const follow_settings_t *s, gp_path_t *path,
                       int32_t attribute)
{
    nmo_engine_block_t handle = {engine, s->node};
    f->manage_teleporter = nmo_engine_block_output_count(&handle) == 7;
    f->node = s->node;
    f->target = s->target;
    path_copy_and_erase(&f->path, path);
    f->path_id = s->path_id;
    f->min_distance_factor = s->min_distance_factor > 1.0f   ? 1.0f
                             : s->min_distance_factor < 0.0f ? 0.0001f
                                                             : s->min_distance_factor;
    f->limit_angle_cos = fabsf(cosf(s->limit_angle));
    f->fuzzyness = fabsf(s->fuzzyness);
    if (f->fuzzyness > 1.0f) {
        f->fuzzyness = 1.0f;
    }
    f->fuzzyness2 = 0.5f * f->fuzzyness;
    f->fuzzyness /= RAND_MAX_MSVC;
    f->pingpong = s->pingpong;
    f->max_blocked_time = s->max_blocked_time >= 0.0f ? s->max_blocked_time : 0.0f;
    f->distance_blocked = fabsf(s->distance_blocked);
    int32_t filter = 0, radius = 0, avoid = 0;
    if (attribute >= 0 && eng_attribute_find(engine, f->target, attribute) != NULL) {
        attribute_field(engine, f->target, attribute, 0, &filter);
        attribute_field(engine, f->target, attribute, 1, &radius);
        attribute_field(engine, f->target, attribute, 2, &avoid);
        f->collision_filter = filter;
        f->radius_collision = bits_float(radius);
        f->radius_avoid = bits_float(avoid);
    } else {
        f->collision_filter = 0;
        f->radius_collision = 1.0f;
        f->radius_avoid = 1.0f;
    }
    f->enter_tele_time = s->enter_tele >= 0.0f ? s->enter_tele : 0.0f;
    f->travel_tele_time = s->travel_tele != 0.0f ? s->travel_tele : 0.0f;
    f->exit_tele_time = s->exit_tele != 0.0f ? s->exit_tele : 0.0f;
    f->state = FS_ON;
    f->function = fs_compute_dir;
    f->test_turn_angle_count = 0;
    f->linker_obs = path->linker_obs;
    static const follow_fn aligns[6] = {align1, align2, align3, align4, align5, align6};
    if (s->direction >= 1 && s->direction <= 6) {
        f->align = aligns[s->direction - 1];
    }
    f->time_blocked = engine->time;
}

/* FollowProblem::Reset */
static void follow_reset(nmo_engine_t *engine, follow_problem_t *f)
{
    f->state = FS_FINISH;
    f->function = fs_compute_dir;
    path_clear(&f->path);
    if (f->invisible) {
        f->invisible = false;
        eng_show(engine, f->target, 1);
    }
}

static follow_problem_t *follow_of(gp_t *gp, int32_t path_id)
{
    int32_t *context = index_of(gp->follow_contexts.data, gp->follow_contexts.count, path_id);
    return context != NULL ? gp->follows.data[*context] : NULL;
}

/* RegisterFollowProblem: the path kept under its ID, followed */
static bool register_follow(gp_t *gp, nmo_engine_t *engine, const follow_settings_t *s, int32_t attribute)
{
    size_t at = 0;
    while (at < gp->paths.count && gp->paths.data[at].id != s->path_id) {
        at++;
    }
    if (at == gp->paths.count) {
        return false;
    }
    gp_path_t *path = gp->paths.data[at].path;
    memmove(&gp->paths.data[at], &gp->paths.data[at + 1u], (gp->paths.count - at - 1u) * sizeof(path_entry_t));
    gp->paths.count--;
    if (path->subs.count == 0) {
        path_free(path);
        return false;
    }
    int32_t context;
    if (gp->free_follows.count > 0) {
        context = queue_pop_front(gp->free_follows.data, &gp->free_follows.count);
    } else {
        context = gp->max_follow++;
        follow_problem_t **slot = NULL;
        follow_problem_t *f = (follow_problem_t *)calloc(1, sizeof(follow_problem_t));
        if (f == NULL || !ENG_VEC_PUSH(gp->follows, slot)) {
            free(f);
            path_free(path);
            gp->max_follow--;
            return false;
        }
        f->move = fs_turn_and_up;
        f->align = align1;
        *slot = f;
    }
    ++gp->num_follow;
    follow_problem_t *f = gp->follows.data[context];
    follow_set(engine, f, s, path, attribute);
    path_free(path);
    first_orient(engine, f, 0);
    index_insert(&gp->follow_contexts.data, &gp->follow_contexts.count, &gp->follow_contexts.capacity, s->path_id,
                 context);
    return true;
}

static bool unregister_follow(gp_t *gp, nmo_engine_t *engine, int32_t path_id)
{
    int32_t *found = index_of(gp->follow_contexts.data, gp->follow_contexts.count, path_id);
    if (found == NULL) {
        return false;
    }
    int32_t context = *found;
    index_remove(gp->follow_contexts.data, &gp->follow_contexts.count, path_id);
    follow_reset(engine, gp->follows.data[context]);
    queue_push_back(&gp->free_follows.data, &gp->free_follows.count, &gp->free_follows.capacity, context);
    --gp->num_follow;
    return true;
}

/* ManagePathFollow: every follower's direction, then the corrections, then the moves */
static void manage_path_follow(gp_t *gp, nmo_engine_t *engine)
{
    int32_t attribute = attribute_type_named(engine, "Path Finding Obstacle");
    for (int pass = 0; pass < 3; pass++) {
        int32_t left = gp->num_follow;
        for (int32_t i = 0; i < gp->max_follow && i < (int32_t)gp->follows.count; ++i) {
            if (left == 0) {
                break;
            }
            follow_problem_t *f = gp->follows.data[i];
            if (f->state == FS_FINISH) {
                continue;
            }
            --left;
            if (pass == 0) {
                if (f->state != FS_OFF) {
                    f->function(gp, f);
                }
            } else if (pass == 1) {
                correct_direction(gp, f, attribute);
            } else {
                f->move(gp, f);
            }
        }
    }
}

/* SequenceToBeDeleted: the problems of objects gone end */
static void forget_destroyed(gp_t *gp, nmo_engine_t *engine)
{
    for (size_t i = 0; gp->num_path > 0 && i < gp->problems.count; ++i) {
        path_problem_t *p = gp->problems.data[i];
        if (p->target == 0 || p->function == st_finish) {
            continue;
        }
        if (eng_find_object(engine, p->target) == NULL ||
            (p->grid_start != 0 && eng_find_object(engine, p->grid_start) == NULL) ||
            (p->grid_end != 0 && eng_find_object(engine, p->grid_end) == NULL) ||
            (p->grid_search != 0 && eng_find_object(engine, p->grid_search) == NULL)) {
            unregister_problem(gp, p->target, false);
            p->function = st_finish;
        }
    }
    for (size_t i = 0; i < gp->follows.count; ++i) {
        follow_problem_t *f = gp->follows.data[i];
        if (f->state == FS_FINISH) {
            continue;
        }
        if (eng_find_object(engine, f->target) == NULL || eng_find_object(engine, f->charac) == NULL ||
            eng_find_object(engine, f->grid) == NULL) {
            unregister_follow(gp, engine, f->path_id);
        }
    }
}

/* ============================================================================
 * The manager hooks and the blocks
 * ============================================================================ */

static void gp_pre_process(nmo_engine_t *engine, void *state)
{
    gp_t *gp = (gp_t *)state;
    gp->engine = engine;
    if (!gp->graph_done) {
        return;
    }
    forget_destroyed(gp, engine);
    manage_path_finding(gp);
    manage_path_follow(gp, engine);
}

/* GridManager::OnCKReset: a new path manager */
static void gp_on_reset(nmo_engine_t *engine, void *state)
{
    (void)engine;
    gp_t *gp = (gp_t *)state;
    gp_free(gp);
    memset(gp, 0, sizeof(*gp));
    gp->max_path = 1;
}

static void gp_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    gp_free((gp_t *)state);
}

static const eng_manager_desc_t grid_path_manager = {
    .name = "Grid Path Manager",
    .state_size = sizeof(gp_t),
    .destroy = gp_destroy,
    .pre_process = gp_pre_process,
    .reset = gp_on_reset,
};

static gp_t *gp_of(nmo_engine_t *engine)
{
    gp_t *gp = (gp_t *)eng_manager_state(engine, &grid_path_manager);
    if (gp != NULL) {
        gp->max_path = gp->max_path == 0 ? 1 : gp->max_path;
        gp->engine = engine;
    }
    return gp;
}

/* Grid Path Init: the graph for the Layer inputs (Out), or Error when there is no grid */
static int bb_grid_path_init(blk_t *b)
{
    gp_t *gp = gp_of(b->engine);
    IN_OFF(b, 0);
    int count = nmo_engine_block_input_parameter_count(b);
    int32_t *layers = (int32_t *)malloc((size_t)(count > 0 ? count : 1) * sizeof(int32_t));
    if (gp == NULL || layers == NULL) {
        free(layers);
        return NMO_ENGINE_BR_GENERICERROR;
    }
    for (int i = 0; i < count; i++) {
        layers[i] = get_in_int(b, i, 0);
    }
    OUT_ON(b, construct_graph(gp, b->engine, layers, count) ? 0 : 1);
    free(layers);
    return NMO_ENGINE_BR_OK;
}

#define SOLVER_PATH_TYPE 0
#define SOLVER_OPTIMIZE 1

/*
 * Grid Path Solver: On registers the target's problem (from Position In
 * Owner Ref to the Goal Position in its ref), which the manager solves
 * before the next frame's behaviors; then Found, with the path (an ID for
 * Character Grid Path Follow, a data array of positions, or a curve) and its
 * length, or Not Found. Reset gives the problem up.
 */
static int bb_grid_path_solver(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    gp_t *gp = gp_of(engine);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0 || gp == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        if (target_status(gp, target) == 0) {
            problem_settings_t s;
            memset(&s, 0, sizeof(s));
            s.target_pos = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
            if (nmo_engine_block_version(b) == 0x00020000u) {
                s.goal_ref = nmo_engine_block_get_input_object(b, 2);
                s.goal_pos = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
            } else {
                s.goal_ref = nmo_engine_block_get_input_object(b, 1);
                s.goal_pos = get_in_vector(b, 2, 0.0f, 0.0f, 0.0f);
            }
            s.heuristic = get_in_int(b, 3, 1);
            s.diagonal = get_in_int(b, 5, 0) != 0;
            s.obstacle_layer = get_in_int(b, 6, 0);
            s.obstacle_threshold = get_in_int(b, 7, 0);
            s.slowing_factor = get_in_float(b, 8, 1.0f);
            s.linker = get_in_int(b, 9, 1) != 0;
            s.linker_obs = get_in_int(b, 10, 0) != 0;
            s.heuristic_coef = get_in_float(b, 4, 2.0f);
            s.optimize = get_local_int(b, SOLVER_OPTIMIZE, 1) != 0;
            if (s.obstacle_layer == 0 || !register_problem(gp, engine, target, &s)) {
                OUT_ON(b, 1);
                return NMO_ENGINE_BR_OWNERERROR;
            }
        }
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        unregister_problem(gp, target, false);
        return NMO_ENGINE_BR_OK;
    }
    int32_t state = target_status(gp, target);
    if (state == 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (state == 2) {
        path_problem_t *p = problem_of(gp, target);
        int32_t type = get_local_int(b, SOLVER_PATH_TYPE, 1);
        float cost = 0.0f;
        if (type == 1) {
            p->follow_path_id = gp->follow_path_id;
            int32_t id = gp->follow_path_id++;
            cost = p->path.cost;
            unregister_problem(gp, target, true);
            set_out(b, 0, &id, sizeof(id));
        } else if (type == 2) {
            nmo_object_id_t array = 0;
            nmo_engine_block_get_output_value(b, 0, &array, sizeof(array));
            if (array == 0 || eng_find_array(engine, array) == NULL) {
                array = eng_create_object(engine, NMO_CID_DATAARRAY, "PathArray");
                if (array != 0) {
                    nmo_engine_array_insert_column(engine, array, -1, CKARRAYTYPE_PARAMETER, CKPGUID_VECTOR);
                    nmo_engine_array_set_column_name(engine, array, 0, "path node");
                    eng_add_to_level_scene(engine, array);
                }
            }
            if (array != 0) {
                path_to_array(engine, &p->path, array);
            }
            cost = p->path.cost;
            unregister_problem(gp, target, false);
            set_out(b, 0, &array, sizeof(array));
        } else if (type == 3) {
            nmo_object_id_t curve = 0;
            nmo_engine_block_get_output_value(b, 0, &curve, sizeof(curve));
            if (curve == 0 || eng_find_object(engine, curve) == NULL) {
                curve = eng_create_object(engine, NMO_CID_CURVE, "Result Path");
                if (curve != 0) {
                    eng_add_to_level_scene(engine, curve);
                }
            }
            if (curve != 0) {
                path_to_curve(engine, &p->path, curve);
            }
            cost = p->path.cost;
            unregister_problem(gp, target, false);
            set_out(b, 0, &curve, sizeof(curve));
        }
        cost -= 1.0f;
        set_out(b, 1, &cost, sizeof(cost));
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (state == 3) {
        unregister_problem(gp, target, false);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

#define FOLLOW_MANAGE_TELEPORTERS 0

/*
 * Character Grid Path Follow: On follows the path of Path ID (one the
 * solver gave), or turns a follow that Off stopped back on; Out at its end,
 * Can't Follow Path when it cannot (or stays blocked), Collision and Avoid
 * on meeting others; with Manage Teleporters, the teleporter outputs.
 * Reset gives the follow up.
 */
static int bb_character_grid_path_follow(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t path_id = get_in_int(b, 0, 0);
    gp_t *gp = gp_of(engine);
    if (target == 0 || path_id < 0 || gp == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        follow_problem_t *existing = follow_of(gp, path_id);
        if (existing != NULL && existing->state == FS_OFF) {
            existing->state = FS_ON;
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
        follow_settings_t s;
        memset(&s, 0, sizeof(s));
        s.target = target;
        s.node = b->node;
        s.path_id = path_id;
        if (get_local_int(b, FOLLOW_MANAGE_TELEPORTERS, 0) != 0) {
            s.enter_tele = get_in_float(b, 8, 0.0f);
            s.travel_tele = get_in_float(b, 9, 0.0f);
            s.exit_tele = get_in_float(b, 10, 0.0f);
        }
        s.direction = get_in_int(b, 1, 1);
        s.min_distance_factor = get_in_float(b, 2, 0.2f);
        s.limit_angle = get_in_float(b, 3, 1.2217305f);
        s.fuzzyness = get_in_float(b, 4, 0.0f);
        s.pingpong = get_in_int(b, 5, 0) != 0;
        s.max_blocked_time = get_in_float(b, 6, 10000.0f);
        s.distance_blocked = get_in_float(b, 7, 0.0f);
        if (!register_follow(gp, engine, &s, attribute_type_named(engine, "Path Finding Obstacle"))) {
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        follow_problem_t *f = follow_of(gp, path_id);
        if (f != NULL) {
            f->state = FS_OFF;
        }
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        unregister_follow(gp, engine, path_id);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    follow_problem_t *f = follow_of(gp, path_id);
    follow_state_t state = f != NULL ? f->state : FS_DONTEXIST;
    if (state == FS_FINISH) {
        unregister_follow(gp, engine, path_id);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (state == FS_TIMEOUT) {
        unregister_follow(gp, engine, path_id);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static const nmo_engine_block_desc_t eng_grid_path_blocks[] = {
    {NMO_GUID_INIT(0x74d12456, 0x6f481719), "Grid Path Init", bb_grid_path_init, NULL, NULL},
    {NMO_GUID_INIT(0x17e75566, 0x0c5625a6), "Grid Path Solver", bb_grid_path_solver, NULL, NULL},
    {NMO_GUID_INIT(0x6d0a6dd3, 0x43765df4), "Character Grid Path Follow", bb_character_grid_path_follow, NULL, NULL},
};

nmo_status_t eng_register_grid_path(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &grid_path_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_grid_path_blocks) : status;
}
