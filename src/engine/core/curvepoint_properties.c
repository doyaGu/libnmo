#include "../engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

static const nmo_curvepoint_state_t *point_state(const nmo_engine_t *engine, nmo_object_id_t point)
{
    nmo_object_t *object = eng_entity_index(engine,point) >= 0 ? eng_find_object(engine,point) : NULL;
    return object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_CURVEPOINT) : NULL;
}

bool eng_curvepoint_get_properties(const nmo_engine_t *engine, nmo_object_id_t point, eng_curvepoint_properties_t *out)
{
    const nmo_curvepoint_state_t *state = point_state(engine,point);
    if (state == NULL || out == NULL) return false;
    int32_t index = eng_map_get(&engine->curves.point_map,point);
    *out = index >= 0 ? engine->curves.point_overrides.data[index].properties :
        (eng_curvepoint_properties_t){state->bias,state->tension,state->continuity,state->linear};
    return true;
}

bool eng_curvepoint_set_properties(nmo_engine_t *engine, nmo_object_id_t point, const eng_curvepoint_properties_t *properties)
{
    if (properties == NULL || point_state(engine,point) == NULL) return false;
    eng_curvepoint_properties_t saved = *properties;
    int32_t index = eng_map_get(&engine->curves.point_map,point);
    eng_curvepoint_override_t *entry = NULL;
    if (index >= 0) entry = &engine->curves.point_overrides.data[index];
    else {
        index = (int32_t)engine->curves.point_overrides.count;
        if (!ENG_VEC_PUSH(engine->curves.point_overrides,entry)) return false;
        if (!eng_map_put(&engine->curves.point_map,point,index)) {
            engine->curves.point_overrides.count--;
            return false;
        }
        entry->id = point;
    }
    entry->properties = saved;
    return true;
}

void eng_copy_curvepoint_properties(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target)
{
    eng_curvepoint_properties_t properties;
    if (eng_curvepoint_get_properties(engine,source,&properties)) eng_curvepoint_set_properties(engine,target,&properties);
}
