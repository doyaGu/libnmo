#include "../engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

static const nmo_curve_state_t *curve_state(const nmo_engine_t *engine, nmo_object_id_t curve)
{
    nmo_object_t *object = eng_entity_index(engine,curve) >= 0 ? eng_find_object(engine,curve) : NULL;
    return object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_CURVE) : NULL;
}

bool eng_curve_get_properties(const nmo_engine_t *engine, nmo_object_id_t curve, eng_curve_properties_t *out)
{
    const nmo_curve_state_t *state = curve_state(engine,curve);
    if (state == NULL || out == NULL) return false;
    int32_t index = eng_map_get(&engine->curves.map,curve);
    *out = index >= 0 ? engine->curves.overrides.data[index].properties :
        (eng_curve_properties_t){(int32_t)state->opened,(int32_t)state->step_count,state->fitting_coeff};
    return true;
}

bool eng_curve_set_properties(nmo_engine_t *engine, nmo_object_id_t curve, const eng_curve_properties_t *properties)
{
    if (properties == NULL || curve_state(engine,curve) == NULL) return false;
    eng_curve_properties_t saved = *properties;
    int32_t index = eng_map_get(&engine->curves.map,curve);
    eng_curve_override_t *entry = NULL;
    if (index >= 0) entry = &engine->curves.overrides.data[index];
    else {
        index = (int32_t)engine->curves.overrides.count;
        if (!ENG_VEC_PUSH(engine->curves.overrides,entry)) return false;
        if (!eng_map_put(&engine->curves.map,curve,index)) {
            engine->curves.overrides.count--;
            return false;
        }
        entry->id = curve;
    }
    entry->properties = saved;
    return true;
}

void eng_copy_curve_properties(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target)
{
    eng_curve_properties_t properties;
    if (eng_curve_get_properties(engine,source,&properties)) eng_curve_set_properties(engine,target,&properties);
}

void eng_free_curves(nmo_engine_t *engine)
{
    free(engine->curves.overrides.data);
    free(engine->curves.map.slots);
}
