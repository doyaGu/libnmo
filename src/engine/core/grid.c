/**
 * @file grid.c
 * @brief The grid manager (GridManager of Grids.dll) and the grids and layers
 *        it reads (CKGrid, CKLayer).
 *
 * The layer types are numbered by name: 1 is "- default -", and a layer
 * that names a type the manager does not have registers it at the end
 * (CKLayer::Load), in the order the layers were loaded. The grids are the
 * CKGrid objects, each of which has the Grid attribute from its creation;
 * those of the scene are the ones the level scene has. Their layers' values
 * are kept here once read, since blocks change them.
 *
 * Not modelled: the type's random color RegisterType draws (it would take
 * numbers from rand()), and the remapping GridManager::LoadData makes of the
 * layer type parameters of a second file whose layers number the types
 * otherwise.
 */

#include "../engine_internal.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_type_guids.h"

#include <stdlib.h>
#include <string.h>

static char *grid_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

void eng_grids_init(nmo_engine_t *engine)
{
    /* InitData, then OnCKInit's SetTypeName(1, DEFAULT_LAYER_NAME) */
    char **name = NULL;
    nmo_guid_t *param = NULL;
    if (ENG_VEC_PUSH(engine->grids.type_names, name) && ENG_VEC_PUSH(engine->grids.type_params, param)) {
        *name = grid_text("invalid layer type");
    }
    if (ENG_VEC_PUSH(engine->grids.type_names, name) && ENG_VEC_PUSH(engine->grids.type_params, param)) {
        *name = grid_text(ENG_DEFAULT_LAYER_NAME);
        *param = CKPGUID_INT;
    }
}

void eng_free_grids(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->grids.type_names.count; i++) {
        free(engine->grids.type_names.data[i]);
    }
    free(engine->grids.type_names.data);
    free(engine->grids.type_params.data);
    for (size_t i = 0; i < engine->grids.grids.count; i++) {
        free(engine->grids.grids.data[i].layers.data);
    }
    free(engine->grids.grids.data);
    for (size_t i = 0; i < engine->grids.layers.count; i++) {
        free(engine->grids.layers.data[i].squares);
    }
    free(engine->grids.layers.data);
    memset(&engine->grids, 0, sizeof(engine->grids));
}

static void grids_sync(nmo_engine_t *engine);

static int32_t type_from_name(nmo_engine_t *engine, const char *name)
{
    if (name == NULL) {
        return 0;
    }
    int32_t type = (int32_t)engine->grids.type_names.count;
    while (type > 0) {
        const char *known = engine->grids.type_names.data[--type];
        if (known != NULL && strcmp(known, name) == 0) {
            break;
        }
    }
    return type;
}

int32_t eng_grid_type_from_name(nmo_engine_t *engine, const char *name)
{
    grids_sync(engine);
    return type_from_name(engine, name);
}

int32_t eng_grid_register_type(nmo_engine_t *engine, const char *name)
{
    if (name == NULL) {
        return 0;
    }
    int32_t type = type_from_name(engine, name);
    if (type != 0) {
        return type;
    }
    char *copy = grid_text(name);
    char **slot = NULL;
    nmo_guid_t *param = NULL;
    if (copy == NULL || !eng_vec_reserve((void **)&engine->grids.type_params.data, &engine->grids.type_params.capacity,
                                         engine->grids.type_params.count + 1u, sizeof(nmo_guid_t)) ||
        !ENG_VEC_PUSH(engine->grids.type_names, slot)) {
        free(copy);
        return 0;
    }
    *slot = copy;
    ENG_VEC_PUSH(engine->grids.type_params, param);
    *param = CKPGUID_INT;
    return (int32_t)engine->grids.type_names.count - 1;
}

int32_t eng_grid_type_count(nmo_engine_t *engine)
{
    grids_sync(engine);
    return (int32_t)engine->grids.type_names.count;
}

const char *eng_grid_type_name(nmo_engine_t *engine, int32_t type)
{
    grids_sync(engine);
    return type >= 0 && (size_t)type < engine->grids.type_names.count ? engine->grids.type_names.data[type] : NULL;
}

nmo_guid_t eng_grid_type_param(nmo_engine_t *engine, int32_t type)
{
    grids_sync(engine);
    return type >= 0 && (size_t)type < engine->grids.type_params.count ? engine->grids.type_params.data[type]
                                                                        : NMO_GUID(0u, 0u);
}

static eng_layer_t *layer_of(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->grids.layers.count; i++) {
        if (engine->grids.layers.data[i].id == id) {
            return &engine->grids.layers.data[i];
        }
    }
    return NULL;
}

static eng_grid_t *grid_of(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->grids.grids.count; i++) {
        if (engine->grids.grids.data[i].id == id) {
            return &engine->grids.grids.data[i];
        }
    }
    return NULL;
}

/* CKLayer::Load: its type from its name, its grid and the squares it saved */
static void add_layer(nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_object_t *object = eng_find_object(engine, id);
    const nmo_layer_state_t *state =
        object != NULL ? (const nmo_layer_state_t *)nmo_object_get_state(object) : NULL;
    eng_layer_t *layer = NULL;
    if (state == NULL || !ENG_VEC_PUSH(engine->grids.layers, layer)) {
        return;
    }
    memset(layer, 0, sizeof(*layer));
    layer->id = id;
    const char *name = nmo_object_get_name(object);
    layer->type = type_from_name(engine, name);
    if (layer->type == 0) {
        layer->type = eng_grid_register_type(engine, name);
    }
    /* the file's associated parameter: its own from version 3, INT before, none before 2 */
    if (layer->type > 0 && (size_t)layer->type < engine->grids.type_params.count && state->has_version &&
        state->version >= 2) {
        engine->grids.type_params.data[layer->type] = state->version >= 3 ? state->param_guid : CKPGUID_INT;
    }
    layer->grid = eng_object_ref(engine, id, &state->grid);
    int32_t count = state->square_data != NULL ? (int32_t)(state->square_data_size / 4u) : 0;
    if (count > 0) {
        layer->squares = (int32_t *)malloc((size_t)count * sizeof(int32_t));
        if (layer->squares != NULL) {
            memcpy(layer->squares, state->square_data, (size_t)count * sizeof(int32_t));
            layer->count = count;
        }
    }
}

static void add_grid(nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_object_t *object = eng_find_object(engine, id);
    const nmo_grid_state_t *state =
        object != NULL ? (const nmo_grid_state_t *)nmo_object_get_state(object) : NULL;
    eng_grid_t *grid = NULL;
    if (state == NULL || !ENG_VEC_PUSH(engine->grids.grids, grid)) {
        return;
    }
    memset(grid, 0, sizeof(*grid));
    grid->id = id;
    grid->width = state->width;
    grid->length = state->length;
    grid->priority = state->priority;
    for (size_t i = 0; i < state->layers.count; i++) {
        const nmo_grid_layer_t *entry = (const nmo_grid_layer_t *)nmo_array_get(&state->layers, i);
        nmo_object_id_t layer = entry != NULL ? eng_object_ref(engine, id, &entry->ref) : 0;
        nmo_object_id_t *slot = NULL;
        if (layer != 0 && ENG_VEC_PUSH(grid->layers, slot)) {
            *slot = layer;
        }
    }
}

/* FitSquareArrayForLoad: a layer of a grid with a size has a square for each, the saved ones
   first and the rest 0 */
static void fit_layer(eng_layer_t *layer, const eng_grid_t *grid)
{
    int64_t count = (int64_t)grid->width * grid->length;
    if (grid->width <= 0 || grid->length <= 0 || count > INT32_MAX || count == layer->count) {
        return;
    }
    int32_t *squares = (int32_t *)calloc((size_t)count, sizeof(int32_t));
    if (squares == NULL) {
        return;
    }
    int32_t kept = layer->count < (int32_t)count ? layer->count : (int32_t)count;
    if (kept > 0) {
        memcpy(squares, layer->squares, (size_t)kept * sizeof(int32_t));
    }
    free(layer->squares);
    layer->squares = squares;
    layer->count = (int32_t)count;
}

/* The layers, then the grids, loaded since the last time */
static void grids_sync(nmo_engine_t *engine)
{
    nmo_object_id_t next = eng_next_engine_id(engine);
    if (next == engine->grids.synced) {
        return;
    }
    engine->grids.synced = next;
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(engine, NMO_CID_LAYER, &ids);
    for (size_t i = 0; i < count; i++) {
        if (layer_of(engine, ids[i]) == NULL) {
            add_layer(engine, ids[i]);
        }
    }
    free(ids);
    ids = NULL;
    count = eng_objects_of_class(engine, NMO_CID_GRID, &ids);
    for (size_t i = 0; i < count; i++) {
        if (grid_of(engine, ids[i]) == NULL) {
            add_grid(engine, ids[i]);
        }
    }
    free(ids);
    for (size_t i = 0; i < engine->grids.layers.count; i++) {
        eng_layer_t *layer = &engine->grids.layers.data[i];
        const eng_grid_t *grid = layer->grid != 0 ? grid_of(engine, layer->grid) : NULL;
        if (grid != NULL) {
            fit_layer(layer, grid);
        }
    }
}

size_t eng_grid_count(nmo_engine_t *engine)
{
    grids_sync(engine);
    return engine->grids.grids.count;
}

eng_grid_t *eng_grid_at(nmo_engine_t *engine, size_t index)
{
    grids_sync(engine);
    return index < engine->grids.grids.count ? &engine->grids.grids.data[index] : NULL;
}

eng_grid_t *eng_grid(nmo_engine_t *engine, nmo_object_id_t id)
{
    grids_sync(engine);
    return id != 0 ? grid_of(engine, id) : NULL;
}

bool eng_grid_active(nmo_engine_t *engine, const eng_grid_t *grid)
{
    return grid != NULL && eng_find_object(engine, grid->id) != NULL && eng_in_scene(engine, grid->id) &&
           nmo_engine_object_is_active(engine, grid->id);
}

eng_vector_t eng_grid_local(nmo_engine_t *engine, const eng_grid_t *grid, const eng_vector_t *position,
                            int32_t ref)
{
    eng_vector_t local = *position;
    int32_t index = eng_entity_index(engine, grid->id);
    if (index >= 0) {
        eng_entity_inverse_transform(engine, index, &local, position, ref);
    }
    return local;
}

bool eng_grid_contains(nmo_engine_t *engine, const eng_grid_t *grid, const eng_vector_t *position,
                       int32_t ref)
{
    if (!eng_grid_active(engine, grid)) {
        return false;
    }
    eng_vector_t local = eng_grid_local(engine, grid, position, ref);
    return local.x >= 0.0f && local.x < (float)grid->width && local.z >= 0.0f &&
           local.z < (float)grid->length && local.y >= 0.0f && local.y < 1.0f;
}

eng_grid_t *eng_grid_preferred(nmo_engine_t *engine, const eng_vector_t *position, int32_t ref)
{
    grids_sync(engine);
    eng_grid_t *preferred = NULL;
    int32_t best = -100000000;
    for (size_t i = 0; i < engine->grids.grids.count; i++) {
        eng_grid_t *grid = &engine->grids.grids.data[i];
        if (eng_grid_contains(engine, grid, position, ref) && grid->priority > best) {
            best = grid->priority;
            preferred = grid;
        }
    }
    return preferred;
}

eng_layer_t *eng_grid_layer(nmo_engine_t *engine, const eng_grid_t *grid, int32_t type)
{
    grids_sync(engine);
    for (size_t i = 0; grid != NULL && i < grid->layers.count; i++) {
        eng_layer_t *layer = layer_of(engine, grid->layers.data[i]);
        if (layer != NULL && layer->type == type) {
            return layer;
        }
    }
    return NULL;
}

int32_t eng_layer_value(const eng_grid_t *grid, const eng_layer_t *layer, int32_t x, int32_t y)
{
    int64_t at = (int64_t)y * grid->width + x;
    return at >= 0 && at < layer->count ? layer->squares[at] : 0;
}

void eng_layer_set_value(const eng_grid_t *grid, eng_layer_t *layer, int32_t x, int32_t y, int32_t value)
{
    int64_t at = (int64_t)y * grid->width + x;
    if (at >= 0 && at < layer->count) {
        layer->squares[at] = value;
    }
}
