#include "../engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

static const nmo_curve_state_t *curve_state(const nmo_engine_t *engine, nmo_object_id_t curve)
{
    nmo_object_t *object = eng_entity_index(engine,curve) >= 0 ? eng_find_object(engine,curve) : NULL;
    return object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_CURVE) : NULL;
}

static bool live_point(const nmo_engine_t *engine, nmo_object_id_t id)
{
    eng_curvepoint_properties_t properties;
    return eng_curvepoint_get_properties(engine,id,&properties);
}

bool eng_curve_clone_points(eng_curve_points_t *out, const eng_curve_points_t *points)
{
    if (points->count > INT32_MAX || points->count > SIZE_MAX/sizeof(*points->ids) || (points->count != 0 && points->ids == NULL)) return false;
    nmo_object_id_t *ids = points->count != 0 ? malloc(points->count*sizeof(*ids)) : NULL;
    if (points->count != 0 && ids == NULL) return false;
    if (ids != NULL) memcpy(ids,points->ids,points->count*sizeof(*ids));
    *out = (eng_curve_points_t){ids,points->count};
    return true;
}

/* Takes the array only on success. Allocate before touching an aliased view. */
static eng_curve_points_t *store_points(nmo_engine_t *engine, nmo_object_id_t curve, eng_curve_points_t points)
{
    int32_t index = eng_map_get(&engine->curves.topology_map,curve);
    eng_curve_topology_t *entry = NULL;
    if (index >= 0) entry = &engine->curves.topologies.data[index];
    else {
        index = (int32_t)engine->curves.topologies.count;
        if (!ENG_VEC_PUSH(engine->curves.topologies,entry)) return NULL;
        if (!eng_map_put(&engine->curves.topology_map,curve,index)) {
            engine->curves.topologies.count--;
            return NULL;
        }
        entry->id = curve;
    }
    free(entry->points.ids);
    entry->points = points;
    return &entry->points;
}

const eng_curve_points_t *eng_curve_points(nmo_engine_t *engine, nmo_object_id_t curve)
{
    const nmo_curve_state_t *state = curve_state(engine,curve);
    if (state == NULL) return NULL;
    int32_t index = eng_map_get(&engine->curves.topology_map,curve);
    if (index >= 0) return &engine->curves.topologies.data[index].points;
    eng_curve_points_t points = {0};
    if (state->control_point_count > INT32_MAX || (state->control_point_count != 0 && state->control_point_ids == NULL)) return NULL;
    if (state->control_point_count != 0) {
        points.ids = malloc(state->control_point_count*sizeof(*points.ids));
        if (points.ids == NULL) return NULL;
    }
    const eng_module_t *module = eng_module_of(engine,curve,NULL);
    for (uint32_t i = 0; i < state->control_point_count; i++) {
        nmo_object_id_t id = eng_module_id(module,nmo_ref_runtime_id(&state->control_point_ids[i]));
        if (live_point(engine,id)) points.ids[points.count++] = id;
    }
    eng_curve_points_t *stored = store_points(engine,curve,points);
    if (stored == NULL) free(points.ids);
    return stored;
}

bool eng_curve_set_points(nmo_engine_t *engine, nmo_object_id_t curve, const eng_curve_points_t *points)
{
    eng_curve_points_t saved;
    if (curve_state(engine,curve) == NULL || points == NULL || !eng_curve_clone_points(&saved,points)) return false;
    size_t kept = 0;
    for (size_t i = 0; i < saved.count; i++) if (live_point(engine,saved.ids[i])) saved.ids[kept++] = saved.ids[i];
    saved.count = kept;
    if (store_points(engine,curve,saved) != NULL) return true;
    free(saved.ids);
    return false;
}

bool eng_curve_remove_point(nmo_engine_t *engine, nmo_object_id_t curve, nmo_object_id_t point)
{
    if (!live_point(engine,point) || eng_curve_points(engine,curve) == NULL) return false;
    eng_curve_points_t *points = &engine->curves.topologies.data[eng_map_get(&engine->curves.topology_map,curve)].points;
    for (size_t i = 0; i < points->count; i++) if (points->ids[i] == point) {
        memmove(&points->ids[i],&points->ids[i+1],(points->count-i-1)*sizeof(*points->ids));
        points->count--;
        break;
    }
    /* CKCurvePoint::SetCurve(NULL) leaves its 3D parent and world matrix alone. */
    return true;
}

void eng_copy_curve_points(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                            const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count)
{
    const eng_curve_points_t *points = eng_curve_points(engine,source);
    eng_curve_points_t saved;
    if (points == NULL || !eng_curve_clone_points(&saved,points)) return;
    for (size_t i = 0; i < saved.count; i++) for (size_t j = 0; j < count; j++) if (saved.ids[i] == originals[j]) {
        saved.ids[i] = copies[j];
        break;
    }
    eng_curve_set_points(engine,target,&saved);
    free(saved.ids);
}

void eng_curve_forget_object(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->curves.topologies.count; i++) {
        eng_curve_topology_t *entry = &engine->curves.topologies.data[i];
        if (entry->id == id) {
            free(entry->points.ids);
            entry->points = (eng_curve_points_t){0};
        } else {
            size_t kept = 0;
            for (size_t p = 0; p < entry->points.count; p++) if (entry->points.ids[p] != id) entry->points.ids[kept++] = entry->points.ids[p];
            entry->points.count = kept;
        }
    }
}
