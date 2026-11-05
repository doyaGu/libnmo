#include "virtools.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"

static nmo_object_id_t place_input(blk_t *b, int pos)
{
    nmo_object_id_t id = nmo_engine_block_get_input_object(b,pos);
    return eng_entity_index(b->engine,id) >= 0 && eng_is_child_class(b->engine,eng_object_class(b->engine,id),NMO_CID_PLACE) ? id : 0;
}

static int bb_set_portal(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t portal = nmo_engine_block_target(b); int32_t entity = eng_entity_index(b->engine,portal);
    if (entity < 0) return NMO_ENGINE_BR_OWNERERROR;
    b->engine->entities.data[entity].entity_flags |= CK_3DENTITY_PORTAL;
    nmo_object_id_t first = place_input(b,0), second = place_input(b,1);
    if (first == second) return NMO_ENGINE_BR_PARAMETERERROR;
    if (first != 0) eng_place_add_portal(b->engine,first,second,portal);
    if (get_in_int(b,2,0) != 0) {
        if (first == 0 || second == 0) return NMO_ENGINE_BR_PARAMETERERROR;
        eng_matrix_t box;
        if (eng_place_best_fit(b->engine,first,second,&box)) eng_entity_set_world_matrix(b->engine,entity,&box,false);
    }
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_portals(nmo_engine_t *engine)
{
    static const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x465c4732,0x30996468),"Set Portal",bb_set_portal,NULL,NULL};
    return nmo_engine_register_block(engine,&block);
}
