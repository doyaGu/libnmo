/**
 * @file groups.c
 * @brief Ports of the Logics/Groups building blocks, on the engine's groups.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../virtools.h"

#include "object/nmo_class_ids.h"

#include <math.h>
#include <stdlib.h>

/* ---- Logics/Groups ---- */

static int bb_is_in_group(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    IN_OFF(b, 0);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 1);
    bool member = false;
    if (eng_is_child_class(engine, eng_object_class(engine, object), NMO_CID_BEOBJECT)) {
        int32_t count = nmo_engine_group_count(engine, group);
        for (int32_t i = 0; !member && i < count; i++) {
            member = nmo_engine_group_object(engine, group, i) == object;
        }
    }
    OUT_ON(b, member ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_group_iterator(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    int32_t count = nmo_engine_group_count(engine, group);
    if (count < 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    /* The index is the Index output, or a local in the version without it */
    bool index_output = !nmo_guid_is_null(nmo_engine_block_output_parameter_type(b, 1));
    int32_t index = index_output ? get_out_int(b, 1) : get_local_int(b, 0, 0);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
    } else {
        IN_OFF(b, 1);
        ++index;
    }
    if (index >= count) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t element = (uint32_t)nmo_engine_group_object(engine, group, index);
    nmo_engine_block_set_output_value(b, 0, &element, sizeof(element));
    if (index_output) {
        set_out_int(b, 1, index);
    } else {
        set_local_int(b, 0, index);
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* Add To Group: the target into the group (CKGroup::AddObject). */
static int bb_add_to_group(blk_t *b)
{
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    if (nmo_engine_group_count(nmo_engine_block_engine(b), group) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_group_add(nmo_engine_block_engine(b), group, nmo_engine_block_target(b));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Fill Group By Class: the target group gets each object of the class (and
 * of the class after it, when that one derives from it), or of every class
 * deriving from it with Derived Classes; Only In Current Scene, those in
 * the level scene. In the context's lists' order; private objects are left
 * out (the engine has none).
 */
static int bb_fill_group_by_class(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t group = nmo_engine_block_target(b);
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t class_id = get_in_int(b, 0, 0);
    bool derived = get_in_int(b, 1, 0) != 0, in_scene = get_in_int(b, 2, 1) != 0;
    int32_t first = derived ? 0 : class_id, last = derived ? 55 : class_id + 1;
    for (int32_t c = first; c <= last; c++) {
        if (c < 0 || class_id < 0 ||
            !nmo_type_registry_is_class_derived_from(engine->types, (uint32_t)c, (uint32_t)class_id)) {
            continue;
        }
        nmo_object_id_t *ids = NULL;
        size_t count = eng_objects_of_class(engine, (uint32_t)c, &ids);
        for (size_t i = 0; i < count; i++) {
            if (!in_scene || eng_in_scene(engine, ids[i])) {
                eng_group_add(engine, group, ids[i]);
            }
        }
        free(ids);
    }
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Positions
 * ============================================================================ */

/*
 * Get Nearest In Group: the group's 3D entity nearest a point (of the
 * referential's space), the referential itself left out, and its distance.
 */
static int bb_get_nearest_in_group(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    int32_t count = nmo_engine_group_count(engine, group);
    if (count < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t point = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t ref_id = nmo_engine_block_get_input_object(b, 2);
    int32_t ref = eng_entity_index(engine, ref_id);
    if (ref >= 0) {
        eng_vector_t local = point;
        eng_entity_transform(engine, ref, &point, &local, -1);
    }
    uint32_t none = 0;
    nmo_engine_block_set_output_value(b, 0, &none, sizeof(none));
    float nearest = 3.402823466e+38f; /* FLT_MAX */
    uint32_t nearest_id = 0;
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t id = nmo_engine_group_object(engine, group, i);
        int32_t entity = eng_entity_index(engine, id);
        if (id == ref_id || entity < 0) {
            continue;
        }
        eng_vector_t p = eng_entity_position(engine, entity, -1);
        const float dx = p.x - point.x, dy = p.y - point.y, dz = p.z - point.z;
        const float square = dx * dx + dy * dy + dz * dz;
        if (square < nearest) {
            nearest = square;
            nearest_id = (uint32_t)id;
        }
    }
    nmo_engine_block_set_output_value(b, 0, &nearest_id, sizeof(nearest_id));
    nearest = sqrtf(nearest);
    nmo_engine_block_set_output_value(b, 1, &nearest, sizeof(nearest));
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_groups_blocks[] = {
    {NMO_GUID(0x6050252f, 0x3aa82d40), "Group Iterator", bb_group_iterator, NULL, NULL},
    {NMO_GUID(0x00024125, 0x785420ab), "Add To Group", bb_add_to_group, NULL, NULL},
    {NMO_GUID(0x58a1210b, 0x14e5715c), "Is In Group", bb_is_in_group, NULL, NULL},
    {NMO_GUID(0x4445257b, 0x70016c57), "Fill Group By Class", bb_fill_group_by_class, NULL, NULL},
    {NMO_GUID(0x085207eb, 0x584950d8), "Get Nearest In Group", bb_get_nearest_in_group, NULL,
     NULL},
};

nmo_status_t eng_register_logics_groups(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_groups_blocks);
}
