#include "virtools.h"
#include "object/nmo_class_ids.h"

static int bb_get_curve_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t curve = nmo_engine_block_target(b);
    eng_curve_properties_t properties;
    if (!eng_curve_get_properties(b->engine,curve,&properties)) return NMO_ENGINE_BR_OWNERERROR;
    set_out_int(b, 0, properties.opened);
    set_out_int(b, 1, properties.step_count);
    set_out_float(b, 2, properties.fitting_coeff);
    float length = 0;
    eng_curve_length(b->engine,curve,&length);
    set_out_float(b, 3, length);
    return NMO_ENGINE_BR_OK;
}

static int bb_set_curve_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t curve = nmo_engine_block_target(b);
    eng_curve_properties_t properties;
    if (!eng_curve_get_properties(b->engine,curve,&properties)) return NMO_ENGINE_BR_OWNERERROR;
    if (get_in_int(b, 0, 1) == 0) properties.opened = 0;
    else if (properties.opened == 0) properties.opened = 1;
    properties.step_count = get_in_int(b, 1, 100);
    properties.fitting_coeff = get_in_float(b, 2, 0);
    eng_curve_set_properties(b->engine,curve,&properties);
    return NMO_ENGINE_BR_OK;
}

static int bb_get_control_point_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_curvepoint_properties_t properties;
    if (!eng_curvepoint_get_properties(b->engine,nmo_engine_block_target(b),&properties)) return NMO_ENGINE_BR_OWNERERROR;
    set_out_float(b, 0, properties.bias);
    set_out_float(b, 1, properties.tension);
    set_out_float(b, 2, properties.continuity);
    set_out_int(b, 3, properties.linear);
    return NMO_ENGINE_BR_OK;
}

static int bb_set_control_point_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t point = nmo_engine_block_target(b);
    eng_curvepoint_properties_t properties;
    if (!eng_curvepoint_get_properties(b->engine,point,&properties)) return NMO_ENGINE_BR_OWNERERROR;
    properties.bias = get_in_float(b, 0, 0);
    properties.tension = get_in_float(b, 1, 0);
    properties.continuity = get_in_float(b, 2, 0);
    properties.linear = get_in_int(b, 3, 0);
    eng_curvepoint_set_properties(b->engine,point,&properties);
    return NMO_ENGINE_BR_OK;
}

static int bb_remove_control_point(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t curve = nmo_engine_block_target(b);
    eng_curve_properties_t properties;
    if (!eng_curve_get_properties(b->engine,curve,&properties)) return NMO_ENGINE_BR_OWNERERROR;
    nmo_object_id_t point = nmo_engine_block_get_input_object(b,0);
    eng_curvepoint_properties_t point_properties;
    if (!eng_curvepoint_get_properties(b->engine,point,&point_properties)) return -1; /* CKERR_INVALIDPARAMETER */
    return eng_curve_remove_point(b->engine,curve,point) ? NMO_ENGINE_BR_OK : -6; /* CKERR_OUTOFMEMORY */
}

static int bb_add_control_point(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t curve = nmo_engine_block_target(b);
    eng_curve_properties_t properties;
    if (!eng_curve_get_properties(b->engine,curve,&properties)) return NMO_ENGINE_BR_OWNERERROR;
    eng_vector_t position = get_in_vector(b,0,0,0,0);
    int32_t reference = input_entity(b,1), prior = get_in_int(b,2,0);
    const eng_curve_points_t *points = eng_curve_points(b->engine,curve);
    if (points == NULL) return -6; /* CKERR_OUTOFMEMORY */
    if (prior > (int32_t)points->count) return NMO_ENGINE_BR_PARAMETERERROR;
    nmo_object_id_t precursor = prior >= 0 && (size_t)prior < points->count ? points->ids[prior] : 0;
    bool dynamic = get_local_int(b,0,1) != 0;
    nmo_object_id_t point = eng_create_related_object_with_options(b->engine,curve,NMO_CID_CURVEPOINT,"X",dynamic);
    if (point == 0) return -6;
    int32_t entity = eng_entity_index(b->engine,point);
    eng_entity_set_position(b->engine,entity,&position,reference,false);
    eng_entity_set_scale(b->engine,entity,&(eng_vector_t){.4f,.4f,.4f},false,true);
    if (!eng_curve_insert_point(b->engine,curve,precursor,point)) {
        nmo_engine_destroy_object(b->engine,point);
        return -6;
    }
    set_out_int(b,0,(int32_t)point);
    if (get_local_int(b,1,1) != 0) {
        float length = 0;
        eng_curve_length(b->engine,curve,&length);
        /* 3DTransfo.dll 0x250056AE reads the new point's initial length (0)
         * before GetLength updates the curve. Keep that order, including 0/0. */
        set_out_float(b,1,0.0f/length);
    }
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_curve_properties(nmo_engine_t *engine)
{
    static const nmo_engine_block_desc_t blocks[] = {
        {NMO_GUID_INIT(0x1251adfa,0xaebec002), "Get Curve Properties", bb_get_curve_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1257effa,0xf7bec112), "Set Curve Properties", bb_set_curve_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1251effa,0xaebec112), "Get Control Point Properties", bb_get_control_point_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1254effa,0xfebec112), "Set Control Point Properties", bb_set_control_point_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1234effa,0xffbec112), "Remove Control Point", bb_remove_control_point, NULL, NULL},
        {NMO_GUID_INIT(0x1234eefa,0xffaec112), "Add Control Point", bb_add_control_point, NULL, NULL},
    };
    return ENG_REGISTER_BLOCKS(engine,blocks);
}
