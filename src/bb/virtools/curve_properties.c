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
    properties.opened = get_in_int(b, 0, 1) != 0;
    properties.step_count = get_in_int(b, 1, 100);
    properties.fitting_coeff = get_in_float(b, 2, 0);
    eng_curve_set_properties(b->engine,curve,&properties);
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_curve_properties(nmo_engine_t *engine)
{
    static const nmo_engine_block_desc_t blocks[] = {
        {NMO_GUID_INIT(0x1251adfa,0xaebec002), "Get Curve Properties", bb_get_curve_properties, NULL, NULL},
        {NMO_GUID_INIT(0x1257effa,0xf7bec112), "Set Curve Properties", bb_set_curve_properties, NULL, NULL},
    };
    return ENG_REGISTER_BLOCKS(engine,blocks);
}
