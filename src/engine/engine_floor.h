/**
 * @file engine_floor.h
 * @brief The floor manager (CKFloorManager, Collisions.dll): the objects with
 *        the Floor attribute, and the floors above and below a point.
 */

#ifndef NMO_ENGINE_FLOOR_H
#define NMO_ENGINE_FLOOR_H

#include "engine_base.h"
#include "engine_transform.h"

/* A floor's border edges in the world's XZ plane (FloorManager::GeomEdge) */
typedef struct eng_floor_edge {
    float normal[2]; /* outward */
    float d;
    float a[2], b[2];
    float length, inv_length;
} eng_floor_edge_t;

/* The edges ConstrainToFloor computed for a mesh, kept as long as the engine */
typedef struct eng_floor_edges {
    nmo_object_id_t mesh;
    eng_floor_edge_t *edges;
    int32_t count;
} eng_floor_edges_t;

typedef struct eng_floors {
    float limit_angle, cos_angle; /* faces steeper than the angle are not floors */
    /* Floor Manager Setup's cache settings; the DLL fills its cache but never reads it */
    int32_t cache_size;
    float cache_threshold;
    ENG_VEC(eng_floor_edges_t) edges;
} eng_floors_t;

/* CKFloorPoint: the nearest floors above and below a point */
typedef struct eng_floor_point {
    nmo_object_id_t up_floor, down_floor;
    int32_t up_face, down_face;
    eng_vector_t up_normal, down_normal;
    float up_distance, down_distance; /* up's positive, down's negative or 0 */
} eng_floor_point_t;

/* The Floor attribute's fields (CKPGUID_FLOOR) */
typedef struct eng_floor_settings {
    int32_t geometry; /* CK_FLOORGEOMETRY */
    int32_t moving, type, hierarchy, first;
} eng_floor_settings_t;

void eng_floors_init(nmo_engine_t *engine);
void eng_free_floors(nmo_engine_t *engine);

/* The Floor attribute's type; -1 when the engine has none */
int32_t eng_floor_attribute(const nmo_engine_t *engine);
/* SetLimitAngle: clamped to [0, pi/2] */
void eng_floor_set_limit_angle(nmo_engine_t *engine, float angle);
/* CKFloorPoint::Clear */
void eng_floor_point_clear(eng_floor_point_t *point);
/* ReadAttributeValues: the fields of the object's Floor attribute it has over `settings`
   (left as they are for a field it has no parameter for); false when it has no Floor attribute */
bool eng_floor_read(nmo_engine_t *engine, nmo_object_id_t object, eng_floor_settings_t *settings);
/* AddFloorObject: the Floor attribute with these fields */
void eng_floor_add(nmo_engine_t *engine, nmo_object_id_t object, const eng_floor_settings_t *settings);
/* GetNearestFloors: CK_FLOORNEAREST, the floors in `point` (cleared first); `exclude`, when not 0,
   is not a floor */
int eng_floor_nearest(nmo_engine_t *engine, const eng_vector_t *position, eng_floor_point_t *point,
                      nmo_object_id_t exclude);
/* ConstrainToFloor(old, new): the position moved off the edges of the floor it is over (and
   at each radius step from `old` to `position`), in `out`; false when it touches none */
bool eng_floor_constrain(nmo_engine_t *engine, const eng_vector_t *old, const eng_vector_t *position,
                         float radius, eng_vector_t *out, int32_t exclude_attribute);

#endif /* NMO_ENGINE_FLOOR_H */
