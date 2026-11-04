/**
 * @file 3dtransfo.c
 * @brief Ports of the 3D Transformations building blocks, on the engine's 3D
 *        entities.
 *
 * Each function does what the block's function in 3DTransfo.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). TT LookAt (TT_Toolbox_RT.dll) is here with Look At. "Hierarchy"
 * means the children move along: CK passes its opposite as KeepChildren. Play
 * Animation 3D Entity and Play Global Animation keep their progression, the
 * step the animation shows.
 */

#include "virtools.h"

#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

/* ============================================================================
 * Animations
 * ============================================================================ */

/*
 * Play Animation 3D Entity (PlayAnimEntity): the animation's progression
 * from 0 to 1 over Duration, One Loop Played each time it passes 1; without
 * Loop it stays at 1 and stops. The animation takes that step through the
 * progression curve (A_CurveInterpolation).
 */
/* A_CurveInterpolation: the step out, then the animation at the curve's Y of it */
static void play_animation_step(blk_t *b, nmo_object_id_t animation, float step)
{
    nmo_engine_block_set_local_value(b, 0, &step, sizeof(step));
    nmo_engine_block_set_output_value(b, 0, &step, sizeof(step));
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 2, &curve_size);
    float y = curve != NULL ? nmo_engine_curve_y(curve, curve_size, step) : step;
    eng_animation_set_step(b->engine, animation, y);
}

static int bb_play_animation(blk_t *b)
{
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b, 0);
    if (animation == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    float duration = 5000.0f;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 1), CKPGUID_INT)) {
        duration = (float)get_in_int(b, 1, 5000); /* back compatibility */
    } else {
        duration = get_in_float(b, 1, 5000.0f);
    }
    if (duration < 0.001f) {
        return NMO_ENGINE_BR_OK;
    }
    bool loop = get_in_int(b, 3, 1) != 0;
    float step = 0.0f;
    nmo_engine_block_get_local_value(b, 0, &step, sizeof(step));
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    step += nmo_engine_block_delta_time(b) / duration;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        step = 0.0f;
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_owner(b), 0);
    }
    int result = NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    if (step > 1.0f) {
        OUT_ON(b, 0);
        if (loop) {
            step -= (float)(int)step;
        } else {
            step = 1.0f;
            result = NMO_ENGINE_BR_OK;
        }
    }
    play_animation_step(b, animation, step);
    return result;
}

/* An object a reference of `owner`'s state names, as an engine ID */
static nmo_object_id_t referenced(const nmo_engine_t *engine, nmo_object_id_t owner,
                                  const nmo_ref_t *ref)
{
    const eng_module_t *module = eng_module_of(engine, owner, NULL);
    nmo_object_id_t local = nmo_ref_runtime_id(ref);
    return module != NULL && local != 0 ? eng_module_id(module, local) : 0;
}

/*
 * What Play Global Animation's On does to a keyed animation's characters:
 * the character of each body part it animates (each once in a row) stops
 * its automatic processing and goes back to
 * the identity world matrix, its children with it.
 */
static void reset_animated_characters(nmo_engine_t *engine, nmo_object_id_t animation)
{
    nmo_object_t *object = animation != 0 ? eng_find_object(engine, animation) : NULL;
    const nmo_keyedanimation_state_t *keyed =
        object != NULL ? (const nmo_keyedanimation_state_t *)
                             nmo_type_query_object_get_ancestor_state_by_guid(
                                 engine->types, object, CKPGUID_KEYEDANIMATION)
                       : NULL;
    nmo_object_id_t last = 0;
    for (uint32_t i = 0; keyed != NULL && i < keyed->animation_count; i++) {
        nmo_object_id_t part_animation = referenced(engine, animation, &keyed->animation_ids[i]);
        nmo_object_t *part_object = part_animation != 0 ? eng_find_object(engine, part_animation)
                                                        : NULL;
        const nmo_objectanimation_state_t *state =
            part_object != NULL ? (const nmo_objectanimation_state_t *)
                                      nmo_type_query_object_get_ancestor_state_by_guid(
                                          engine->types, part_object, CKPGUID_OBJECTANIMATION)
                                : NULL;
        nmo_object_id_t entity = state != NULL ? eng_object_animation_entity(engine,part_animation) : 0;
        nmo_object_t *entity_object = entity != 0 ? eng_find_object(engine, entity) : NULL;
        const nmo_bodypart_state_t *part =
            entity_object != NULL ? (const nmo_bodypart_state_t *)
                                        nmo_type_query_object_get_ancestor_state_by_guid(
                                            engine->types, entity_object, CKPGUID_BODYPART)
                                  : NULL;
        nmo_object_id_t character =
            part != NULL && part->has_character ? referenced(engine, entity, &part->character) : 0;
        int32_t index = character != 0 ? eng_entity_index(engine, character) : -1;
        if (index < 0 || character == last) {
            continue;
        }
        last = character;
        eng_character_stop_automatic(engine,character);
        eng_matrix_t identity;
        eng_matrix_identity(&identity);
        eng_entity_set_world_matrix(engine, index, &identity, false);
    }
}

/*
 * Play Global Animation (PlayGlobalAnim): Play Animation 3D Entity for an
 * animation that moves several entities. On also puts the characters of
 * the body parts a keyed animation moves back at the origin. Without an
 * animation the progression is not kept, as in the original.
 */
static int bb_play_global_animation(blk_t *b)
{
    nmo_object_id_t animation = nmo_engine_block_get_input_object(b, 0);
    float duration = 5000.0f;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 1), CKPGUID_INT)) {
        duration = (float)get_in_int(b, 1, 5000); /* back compatibility */
    } else {
        duration = get_in_float(b, 1, 5000.0f);
    }
    if (duration < 0.001f) {
        return NMO_ENGINE_BR_OK;
    }
    bool loop = get_in_int(b, 3, 1) != 0;
    float step = 0.0f;
    nmo_engine_block_get_local_value(b, 0, &step, sizeof(step));
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    step += nmo_engine_block_delta_time(b) / duration;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        step = 0.0f;
        reset_animated_characters(b->engine, animation);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, animation, 0);
    }
    int result = NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    if (step > 1.0f) {
        OUT_ON(b, 0);
        if (loop) {
            step -= (float)(int)step;
        } else {
            step = 1.0f;
            result = NMO_ENGINE_BR_OK;
        }
    }
    if (animation != 0) {
        play_animation_step(b, animation, step);
    }
    return result;
}

/* Set Position: the target's position, in the referential's space given one. */
static int bb_set_position(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t position = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    eng_entity_set_position(b->engine, entity, &position, input_entity(b, 1), keep_children(b, 2));
    return NMO_ENGINE_BR_OK;
}

/* Set World Matrix */
static int bb_set_world_matrix(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_matrix_t m;
    eng_matrix_identity(&m);
    nmo_engine_block_get_input_value(b, 0, m.m, sizeof(m.m));
    eng_entity_set_world_matrix(b->engine, entity, &m, keep_children(b, 1));
    return NMO_ENGINE_BR_OK;
}

/*
 * Scale (AddScale): the local axes scaled by the vector, or set to it with
 * Absolute; a 3D sprite changes its size instead (an effect).
 */
static int bb_scale(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t entity = eng_entity_index(b->engine, target);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t scale = get_in_vector(b, 0, 1.0f, 1.0f, 1.0f);
    bool keep = keep_children(b, 1);
    bool absolute = get_in_int(b, 2, 0) != 0;
    uint32_t class_id = (uint32_t)nmo_engine_object_class(b->engine, target);
    if (nmo_type_registry_is_class_derived_from(b->engine->types, class_id, NMO_CID_SPRITE3D)) {
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    } else if (absolute) {
        eng_entity_set_scale(b->engine, entity, &scale, keep, true);
    } else {
        eng_entity_add_scale(b->engine, entity, &scale, keep, true);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Parent: the target keeps its place in the world under its new parent. */
static int bb_set_parent(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_entity_set_parent(b->engine, entity, input_entity(b, 0), true);
    return NMO_ENGINE_BR_OK;
}

/* Rotate: about an axis of the referential's space (the world's without one). */
static int bb_rotate(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_vector_t axis = get_in_vector(b, 0, 0.0f, 1.0f, 0.0f);
    float angle = get_in_float(b, 1, 0.01f);
    eng_entity_rotate(b->engine, entity, &axis, angle, input_entity(b, 2), keep_children(b, 3));
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Euler Orientation: the axes of the angles' rotation. An old block
 * (Merge Angles Params off, with four inputs or more) has the three angles
 * as three inputs.
 */
static int bb_set_euler_orientation(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    bool merged = get_local_int(b, 0, 0) != 0 || nmo_engine_block_input_parameter_count(b) == 3;
    int offset = 0;
    eng_vector_t angles = {0.0f, 0.0f, 0.0f};
    if (merged) {
        angles = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    } else {
        offset = 2;
        angles.x = get_in_float(b, 0, 0.0f);
        angles.y = get_in_float(b, 1, 0.0f);
        angles.z = get_in_float(b, 2, 0.0f);
    }
    bool keep = keep_children(b, 1 + offset);
    eng_matrix_t m;
    eng_matrix_from_euler(&m, angles.x, angles.y, angles.z);
    eng_vector_t dir = {m.m[2][0], m.m[2][1], m.m[2][2]};
    eng_vector_t up = {m.m[1][0], m.m[1][1], m.m[1][2]};
    eng_vector_t right = {m.m[0][0], m.m[0][1], m.m[0][2]};
    eng_entity_set_orientation(b->engine, entity, &dir, &up, &right, input_entity(b, 2 + offset),
                               keep);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Look At
 * ============================================================================ */

static eng_vector_t vector_toward(const eng_vector_t *from, const eng_vector_t *to, float speed)
{
    eng_vector_t v = {from->x + speed * (to->x - from->x), from->y + speed * (to->y - from->y),
                      from->z + speed * (to->z - from->z)};
    return v;
}

/*
 * BB_Orientate (3DTrans.cpp): the axis `dir_mode` names (1 X, 3 Y, 5 Z; the
 * even modes the opposite) turned `speed` of the way toward `dir`, about the
 * up vector, then rolled about it; the local scale kept.
 */
static void orientate(nmo_engine_t *engine, int32_t entity, eng_vector_t dir, int32_t dir_mode,
                      float roll, eng_vector_t up, float speed)
{
    eng_vector_t scale = eng_entity_scale(engine, entity, true);
    if ((dir_mode & 1) == 0) {
        dir.x = -dir.x, dir.y = -dir.y, dir.z = -dir.z;
    }
    up.x *= 0.5f;
    up.z *= 0.5f;
    eng_vector_t old_dir, right;
    switch (dir_mode) {
    case 5: /* Z */
    case 6: /* -Z */
        if (up.x > 1.5f) {
            eng_entity_orientation(engine, entity, NULL, &up, NULL, -1);
        }
        eng_entity_orientation(engine, entity, &old_dir, NULL, NULL, -1);
        dir = vector_toward(&old_dir, &dir, speed);
        right = eng_vector_cross(&up, &dir);
        up = eng_vector_cross(&dir, &right);
        eng_vector_normalize(&dir);
        eng_vector_normalize(&right);
        eng_vector_normalize(&up);
        eng_entity_set_orientation(engine, entity, &dir, &up, &right, -1, false);
        if (roll != 0.0f) {
            const eng_vector_t axis = {0.0f, 0.0f, 1.0f};
            eng_entity_rotate(engine, entity, &axis, roll, entity, false);
        }
        break;
    case 1: /* X */
    case 2: /* -X */
        if (up.x > 1.5f) {
            eng_entity_orientation(engine, entity, NULL, &up, NULL, -1);
        }
        eng_entity_orientation(engine, entity, NULL, NULL, &old_dir, -1);
        dir = vector_toward(&old_dir, &dir, speed);
        right = eng_vector_cross(&dir, &up);
        up = eng_vector_cross(&right, &dir);
        eng_vector_normalize(&dir);
        eng_vector_normalize(&right);
        eng_vector_normalize(&up);
        eng_entity_set_orientation(engine, entity, &right, &up, &dir, -1, false);
        if (roll != 0.0f) {
            const eng_vector_t axis = {1.0f, 0.0f, 0.0f};
            eng_entity_rotate(engine, entity, &axis, roll, entity, false);
        }
        break;
    case 3: /* Y */
    case 4: /* -Y */
        if (up.x > 1.5f) {
            eng_entity_orientation(engine, entity, &up, NULL, NULL, -1);
        }
        eng_entity_orientation(engine, entity, NULL, &old_dir, NULL, -1);
        dir = vector_toward(&old_dir, &dir, speed);
        right = eng_vector_cross(&dir, &up);
        up = eng_vector_cross(&dir, &right);
        eng_vector_normalize(&dir);
        eng_vector_normalize(&right);
        eng_vector_normalize(&up);
        eng_entity_set_orientation(engine, entity, &right, &dir, &up, -1, false);
        if (roll != 0.0f) {
            const eng_vector_t axis = {0.0f, 1.0f, 0.0f};
            eng_entity_rotate(engine, entity, &axis, roll, entity, false);
        }
        break;
    default:
        break;
    }
    eng_entity_set_scale(engine, entity, &scale, false, true);
}

/* The Following Speed of a Look At: per 1/0.07 ms when Time Based, at most all the way. */
static float look_speed(blk_t *b, float speed)
{
    if (get_local_int(b, 0, 0) != 0) {
        speed *= nmo_engine_block_delta_time(b) * 0.07f;
    }
    return speed > 1.0f ? 1.0f : speed;
}

/*
 * Look At: the target's direction axis turned toward a point (of the
 * referential's space), or toward an entity (the old block, with three inputs).
 */
static int bb_look_at(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = b->engine;
    eng_vector_t from = eng_entity_position(engine, entity, -1), to;
    float speed = 1.0f;
    if (nmo_engine_block_input_parameter_count(b) == 4) {
        eng_vector_t point = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
        int32_t ref = input_entity(b, 1);
        speed = get_in_float(b, 2, 1.0f);
        if (ref >= 0) {
            eng_entity_transform(engine, ref, &to, &point, -1);
        } else {
            to = point;
        }
    } else {
        int32_t toward = input_entity(b, 0);
        speed = get_in_float(b, 1, 1.0f);
        if (toward >= 0) {
            to = eng_entity_position(engine, toward, -1);
        } else {
            to.x = to.y = to.z = 0.0f;
        }
    }
    speed = look_speed(b, speed);
    eng_vector_t dir = {to.x - from.x, to.y - from.y, to.z - from.z};
    if (dir.x != 0.0f || dir.y != 0.0f || dir.z != 0.0f) {
        eng_vector_normalize(&dir);
        const eng_vector_t up = {0.0f, 1.0f, 0.0f};
        orientate(engine, entity, dir, get_local_int(b, 1, 5), get_local_float(b, 2, 0.0f), up,
                  speed);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT LookAt: Look At with the direction's components cleared as asked
 * (the three inputs all read "look X-Axis", as in the original).
 */
static int bb_tt_look_at(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_t *engine = b->engine;
    eng_vector_t point = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f), to;
    int32_t ref = input_entity(b, 1);
    float speed = get_in_float(b, 2, 1.0f);
    bool clear = get_in_int(b, 4, 1) != 0;
    eng_vector_t from = eng_entity_position(engine, entity, -1);
    if (ref >= 0) {
        eng_entity_transform(engine, ref, &to, &point, -1);
    } else {
        to = point;
    }
    speed = look_speed(b, speed);
    eng_vector_t dir = {to.x - from.x, to.y - from.y, to.z - from.z};
    if (dir.x != 0.0f || dir.y != 0.0f || dir.z != 0.0f) {
        if (clear) {
            dir.x = dir.y = dir.z = 0.0f;
        }
        if (eng_vector_magnitude(&dir) != 0.0f) {
            eng_vector_normalize(&dir);
        }
        const eng_vector_t up = {0.0f, 1.0f, 0.0f};
        orientate(engine, entity, dir, get_local_int(b, 1, 5), get_local_float(b, 2, 0.0f), up,
                  speed);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Position On Curve (SetCurveStep): the target at the curve's position for
 * Progression. Follow and Bank, which also turn it along the curve, are not
 * ported (Ballance uses neither).
 */
static int bb_position_on_curve(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t position;
    if (!eng_curve_position(b->engine, nmo_engine_block_get_input_object(b, 0),
                            get_in_float(b, 1, 0.5f), &position)) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_entity_set_position(b->engine, entity, &position, -1, keep_children(b, 6));
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_3dtransfo_blocks[] = {
    {NMO_GUID_INIT(0x676776d0, 0x20d457bd), "Position On Curve", bb_position_on_curve, NULL, NULL},
    {NMO_GUID_INIT(0x64221225, 0x769a143f), "Play Animation 3D Entity", bb_play_animation, NULL, NULL},
    {NMO_GUID_INIT(0x1c9236e1, 0x42f40996), "Play Global Animation", bb_play_global_animation, NULL,
     NULL},
    {NMO_GUID_INIT(0xe456e78a, 0x456789aa), "Set Position", bb_set_position, NULL, NULL},
    {NMO_GUID_INIT(0xaa4aa6f0, 0xddefdef4), "Set World Matrix", bb_set_world_matrix, NULL, NULL},
    {NMO_GUID_INIT(0x41236987, 0xa54a87a6), "Scale", bb_scale, NULL, NULL},
    {NMO_GUID_INIT(0x9d9d9d98, 0x7e7a7f75), "Set Parent", bb_set_parent, NULL, NULL},
    {NMO_GUID_INIT(0xffffffee, 0xeeffffff), "Rotate", bb_rotate, NULL, NULL},
    {NMO_GUID_INIT(0x0c4966d8, 0x6c0c6d14), "Set Euler Orientation", bb_set_euler_orientation, NULL,
     NULL},
    {NMO_GUID_INIT(0x26843971, 0x86249317), "Look At", bb_look_at, NULL, NULL},
    {NMO_GUID_INIT(0x3d4861f8, 0x2861703d), "TT LookAt", bb_tt_look_at, NULL, NULL},
};

nmo_status_t eng_register_3dtransfo(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_3dtransfo_blocks);
}
