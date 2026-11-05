#include "virtools.h"

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

nmo_status_t eng_register_curve_properties(nmo_engine_t *engine)
{
    static const nmo_engine_block_desc_t blocks[] = {
        {NMO_GUID_INIT(0x1251adfa,0xaebec002), "Get Curve Properties", bb_get_curve_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1257effa,0xf7bec112), "Set Curve Properties", bb_set_curve_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1251effa,0xaebec112), "Get Control Point Properties", bb_get_control_point_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1254effa,0xfebec112), "Set Control Point Properties", bb_set_control_point_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1234effa,0xffbec112), "Remove Control Point", bb_remove_control_point, NULL, NULL},
    };
    return ENG_REGISTER_BLOCKS(engine,blocks);
}
