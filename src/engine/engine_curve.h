/* CKCurve's editable properties, separate from its loaded control points. */
#ifndef NMO_ENGINE_CURVE_H
#define NMO_ENGINE_CURVE_H

#include "engine_base.h"

enum { ENG_CURVE_OPEN=1, ENG_CURVE_STEPS=2, ENG_CURVE_FITTING=4, ENG_CURVE_PROPERTIES_ALL=7 };
enum { ENG_CURVEPOINT_TCB=1, ENG_CURVEPOINT_LINEAR=2, ENG_CURVEPOINT_PROPERTIES_ALL=3 };

typedef struct eng_curvepoint_properties {
    float bias, tension, continuity;
    int32_t linear;
} eng_curvepoint_properties_t;

typedef struct eng_curvepoint_override {
    nmo_object_id_t id;
    eng_curvepoint_properties_t properties;
} eng_curvepoint_override_t;

typedef struct eng_curve_properties {
    int32_t opened, step_count;
    float fitting_coeff;
} eng_curve_properties_t;

typedef struct eng_curve_override {
    nmo_object_id_t id;
    eng_curve_properties_t properties;
} eng_curve_override_t;

typedef struct eng_curve_points {
    nmo_object_id_t *ids; /* engine IDs, in curve order */
    size_t count;
} eng_curve_points_t;

typedef struct eng_curve_topology {
    nmo_object_id_t id;
    eng_curve_points_t points;
} eng_curve_topology_t;

typedef struct eng_curves {
    ENG_VEC(eng_curve_override_t) overrides;
    eng_map_t map;
    ENG_VEC(eng_curvepoint_override_t) point_overrides;
    eng_map_t point_map;
    ENG_VEC(eng_curve_topology_t) topologies;
    eng_map_t topology_map;
} eng_curves_t;

bool eng_curve_get_properties(const nmo_engine_t *engine, nmo_object_id_t curve, eng_curve_properties_t *out);
bool eng_curve_set_properties(nmo_engine_t *engine, nmo_object_id_t curve, const eng_curve_properties_t *properties);
void eng_copy_curve_properties(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target);
void eng_free_curves(nmo_engine_t *engine);
bool eng_curvepoint_get_properties(const nmo_engine_t *engine, nmo_object_id_t point, eng_curvepoint_properties_t *out);
bool eng_curvepoint_set_properties(nmo_engine_t *engine, nmo_object_id_t point, const eng_curvepoint_properties_t *properties);
void eng_copy_curvepoint_properties(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target);
/* The view lasts until another curve's topology is first read or changed. */
const eng_curve_points_t *eng_curve_points(nmo_engine_t *engine, nmo_object_id_t curve);
bool eng_curve_clone_points(eng_curve_points_t *out, const eng_curve_points_t *points);
bool eng_curve_set_points(nmo_engine_t *engine, nmo_object_id_t curve, const eng_curve_points_t *points);
bool eng_curve_remove_point(nmo_engine_t *engine, nmo_object_id_t curve, nmo_object_id_t point);
/* CKCurve::InsertControlPoint inserts before a present precursor, or appends. */
bool eng_curve_insert_point(nmo_engine_t *engine, nmo_object_id_t curve, nmo_object_id_t precursor, nmo_object_id_t point);
void eng_copy_curve_points(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                            const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);
void eng_curve_forget_object(nmo_engine_t *engine, nmo_object_id_t id);

/* CK2dCurve::GetLength and GetPos on a 2D curve parameter value (curve.c); false for no curve */
bool eng_curve2d_length(const void *curve, size_t size, float *out_length);
bool eng_curve2d_pos(const void *curve, size_t size, float step, float out[2]);

#endif /* NMO_ENGINE_CURVE_H */
