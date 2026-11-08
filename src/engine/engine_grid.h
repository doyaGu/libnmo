/**
 * @file engine_grid.h
 * @brief The grid manager (CKGridManager, Grids.dll): the layer types, the
 *        grids of the scene and the values of their layers.
 */

#ifndef NMO_ENGINE_GRID_H
#define NMO_ENGINE_GRID_H

#include "engine_base.h"
#include "engine_transform.h"

/* CKLayer: a value for each square of its grid, row by row (CKSquare's ival) */
typedef struct eng_layer {
    nmo_object_id_t id;
    nmo_object_id_t grid;
    int32_t type;
    int32_t *squares;
    int32_t count; /* the grid's width x length, or what the file had before it had one */
} eng_layer_t;

/* CKGrid: its size in squares, its priority and its layers in order */
typedef struct eng_grid {
    nmo_object_id_t id;
    int32_t width, length;
    int32_t priority;
    ENG_VEC(nmo_object_id_t) layers;
} eng_grid_t;

typedef struct eng_grids {
    ENG_VEC(char *) type_names; /* the layer types by number: 0 none, 1 "- default -" */
    ENG_VEC(eng_grid_t) grids;  /* in the order they were loaded */
    ENG_VEC(eng_layer_t) layers;
    nmo_object_id_t synced;     /* the next engine ID when the objects were last read */
} eng_grids_t;

#define ENG_DEFAULT_LAYER_NAME "- default -"

void eng_grids_init(nmo_engine_t *engine);
void eng_free_grids(nmo_engine_t *engine);

/* GetTypeFromName: the layer type of that name, 0 for none */
int32_t eng_grid_type_from_name(nmo_engine_t *engine, const char *name);
/* RegisterType: the type of that name, made at the end when there is none; 0 for no name */
int32_t eng_grid_register_type(nmo_engine_t *engine, const char *name);
/* The grids loaded so far, every layer read; the grids in the scene are those eng_in_scene has */
size_t eng_grid_count(nmo_engine_t *engine);
eng_grid_t *eng_grid_at(nmo_engine_t *engine, size_t index);
/* The grid `id`; NULL when it is no CKGrid */
eng_grid_t *eng_grid(nmo_engine_t *engine, nmo_object_id_t id);
/* CKGrid::IsActive: in the scene and active there */
bool eng_grid_active(nmo_engine_t *engine, const eng_grid_t *grid);
/* `position` (in `ref`'s space, -1 for the world) in the grid's space: a square a unit wide */
eng_vector_t eng_grid_local(nmo_engine_t *engine, const eng_grid_t *grid, const eng_vector_t *position,
                            int32_t ref);
/* IsInGrid: an active grid with the point inside it, at most a unit above its base */
bool eng_grid_contains(nmo_engine_t *engine, const eng_grid_t *grid, const eng_vector_t *position,
                       int32_t ref);
/* GetPreferredGrid: of the active grids of the scene that have the point, the first of the
   highest priority; NULL for none */
eng_grid_t *eng_grid_preferred(nmo_engine_t *engine, const eng_vector_t *position, int32_t ref);
/* CKGrid::GetLayer: its first layer of that type; NULL for none */
eng_layer_t *eng_grid_layer(nmo_engine_t *engine, const eng_grid_t *grid, int32_t type);
/* CKLayer::GetValue and SetValue: the square at y x width + x, unchecked in CK; here a square
   outside the layer reads 0 and is not written */
int32_t eng_layer_value(const eng_grid_t *grid, const eng_layer_t *layer, int32_t x, int32_t y);
void eng_layer_set_value(const eng_grid_t *grid, eng_layer_t *layer, int32_t x, int32_t y, int32_t value);

#endif /* NMO_ENGINE_GRID_H */
