/* CKCurve's editable properties, separate from its loaded control points. */
#ifndef NMO_ENGINE_CURVE_H
#define NMO_ENGINE_CURVE_H

#include "engine_base.h"

enum { ENG_CURVE_OPEN=1, ENG_CURVE_STEPS=2, ENG_CURVE_FITTING=4, ENG_CURVE_PROPERTIES_ALL=7 };

typedef struct eng_curve_properties {
    int32_t opened, step_count;
    float fitting_coeff;
} eng_curve_properties_t;

typedef struct eng_curve_override {
    nmo_object_id_t id;
    eng_curve_properties_t properties;
} eng_curve_override_t;

typedef struct eng_curves {
    ENG_VEC(eng_curve_override_t) overrides;
    eng_map_t map;
} eng_curves_t;

bool eng_curve_get_properties(const nmo_engine_t *engine, nmo_object_id_t curve, eng_curve_properties_t *out);
bool eng_curve_set_properties(nmo_engine_t *engine, nmo_object_id_t curve, const eng_curve_properties_t *properties);
void eng_copy_curve_properties(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target);
void eng_free_curves(nmo_engine_t *engine);

#endif /* NMO_ENGINE_CURVE_H */
