/**
 * @file engine_3dtrans.c
 * @brief Ports of the 3D Transformations building blocks, and of the other
 *        blocks that read 3D entities' positions, on the engine's 3D entities.
 *
 * Each function does what the block's function in 3DTransfo.dll (or
 * TT_Toolbox_RT.dll) does (the reference is the Ballanced source), through
 * the CK3dEntity functions of engine_transform.c. "Hierarchy" means the
 * children move along: CK passes its opposite as KeepChildren.
 */

#include "engine_internal.h"

#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

typedef nmo_engine_block_t blk_t;

#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static float get_in_float(blk_t *b, int pos, float fallback)
{
    float value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static eng_vector_t get_in_vector(blk_t *b, int pos, float x, float y, float z)
{
    eng_vector_t v = {x, y, z};
    nmo_engine_block_get_input_value(b, pos, &v, sizeof(v));
    return v;
}

static int32_t get_local_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static float get_local_float(blk_t *b, int pos, float fallback)
{
    float value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

/* The engine's entity the target is; -1 when it is no 3D entity. */
static int32_t target_entity(blk_t *b)
{
    return eng_entity_index(b->engine, nmo_engine_block_target(b));
}

/* The entity an input names; -1 for none. */
static int32_t input_entity(blk_t *b, int pos)
{
    return eng_entity_index(b->engine, nmo_engine_block_get_input_object(b, pos));
}

/* Hierarchy (TRUE by default) as CK passes it: KeepChildren is its opposite. */
static bool keep_children(blk_t *b, int pos)
{
    return get_in_int(b, pos, 1) == 0;
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

/* TT Scaleable Proximity's outputs (the Use Outputs setting) and axes (Check Axis) */
enum {
    PROXIMITY_IN = 1,
    PROXIMITY_OUT = 2,
    PROXIMITY_ENTER = 4,
    PROXIMITY_EXIT = 8,
    PROXIMITY_WAS_OUT = 0,
    PROXIMITY_WAS_IN = 1,
};

/* The position Scaleable Proximity measures from: the barycenter, or the position. */
static eng_vector_t proximity_point(nmo_engine_t *engine, int32_t entity, bool barycenter)
{
    eng_vector_t p;
    if (barycenter) {
        /* A character would take its bounding box's center: the engine has no box for it */
        eng_entity_bary_center(engine, entity, &p);
    } else {
        p = eng_entity_position(engine, entity, -1);
    }
    return p;
}

/*
 * TT Scaleable Proximity: whether two entities are within Distance (on the
 * checked axes, squared or not), checked again after a number of frames
 * that grows with the distance between the two exactness distances; In,
 * Out, Enter and Exit Range among the outputs the setting keeps.
 */
static int bb_scaleable_proximity(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t outputs = get_local_int(b, 2, 15);
    int32_t axes = get_local_int(b, 3, 0);
    bool squared = get_local_int(b, 4, 1) != 0;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        int32_t was = PROXIMITY_OUT;
        nmo_engine_block_set_local_value(b, 0, &was, sizeof(was));
    }
    int32_t last_check = get_local_int(b, 1, 1);
    if (--last_check > 0) {
        nmo_engine_block_set_local_value(b, 1, &last_check, sizeof(last_check));
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    float distance = get_in_float(b, 0, 0.0f);
    bool barycenter = get_in_int(b, 3, 0) != 0;
    int32_t a = input_entity(b, 1), c = input_entity(b, 2);
    if (a < 0 || c < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t pa = proximity_point(engine, a, barycenter);
    eng_vector_t pb = proximity_point(engine, c, barycenter);
    eng_vector_t a_to_b = {0.0f, 0.0f, 0.0f};
    float current = 0.0f;
    if (axes & 4) {
        a_to_b.z = pb.z - pa.z;
        current += a_to_b.z * a_to_b.z;
    }
    if (axes & 2) {
        a_to_b.y = pb.y - pa.y;
        current += a_to_b.y * a_to_b.y;
    }
    if (axes & 1) {
        a_to_b.x = pb.x - pa.x;
        current += a_to_b.x * a_to_b.x;
    }
    float min_exact = get_in_float(b, 4, 0.0f), max_exact = get_in_float(b, 5, 10.0f);
    int32_t min_delay = get_in_int(b, 6, 1), max_delay = get_in_int(b, 7, 10);
    if (squared) {
        distance *= distance;
        min_exact *= min_exact;
        max_exact *= max_exact;
    } else {
        current = powf(current, 0.5f);
    }
    nmo_engine_block_set_output_value(b, 0, &current, sizeof(current));
    if (nmo_engine_block_output_parameter_count(b) > 1) {
        nmo_engine_block_set_output_value(b, 1, &a_to_b, sizeof(a_to_b));
    }
    if (current < max_exact) {
        last_check = current > min_exact
                         ? min_delay + (int32_t)((current - min_exact) /
                                                 (double)(max_exact - min_exact) *
                                                 (max_delay - min_delay))
                         : min_delay;
    } else {
        last_check = max_delay;
    }
    if (last_check < 1) {
        last_check = 1;
    }
    nmo_engine_block_set_local_value(b, 1, &last_check, sizeof(last_check));
    int32_t was = get_local_int(b, 0, PROXIMITY_WAS_OUT);
    int32_t activation;
    if (current < distance) {
        activation = was == PROXIMITY_WAS_OUT || was == 2 ? PROXIMITY_ENTER : PROXIMITY_IN;
        was = PROXIMITY_WAS_IN;
    } else {
        activation = was == PROXIMITY_WAS_IN || was == 2 ? PROXIMITY_EXIT : PROXIMITY_OUT;
        was = PROXIMITY_WAS_OUT;
    }
    nmo_engine_block_set_local_value(b, 0, &was, sizeof(was));
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        if (outputs & (1 << i)) {
            if (activation & (1 << i)) {
                OUT_ON(b, pos);
            }
            pos++;
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT Set Dynamic Position: each frame the target follows the object like a
 * spring (Force, Damping and Offset per axis, in the coordinate system's
 * space given one), kept within MaxDistance2Target of it.
 */
static int bb_set_dynamic_position(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t frame = input_entity(b, 10);
    bool status = false;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        int32_t off = 0;
        nmo_engine_block_set_local_value(b, 0, &off, sizeof(off));
    } else {
        if (nmo_engine_block_is_input_active(b, 0)) {
            IN_OFF(b, 0);
            OUT_ON(b, 0);
            int32_t on = 1;
            nmo_engine_block_set_local_value(b, 0, &on, sizeof(on));
            eng_vector_t start = eng_entity_position(engine, target, frame);
            nmo_engine_block_set_local_value(b, 1, &start, sizeof(start));
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
        status = get_local_int(b, 0, 0) != 0;
    }
    int32_t object = input_entity(b, 0);
    if (object < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t object_position = eng_entity_position(engine, object, frame);
    eng_vector_t old_target = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 1, &old_target, sizeof(old_target));
    eng_vector_t target_position = eng_entity_position(engine, target, frame);
    nmo_engine_block_set_local_value(b, 1, &target_position, sizeof(target_position));
    eng_vector_t delta = {target_position.x - old_target.x, target_position.y - old_target.y,
                          target_position.z - old_target.z};
    eng_vector_t offset_to = {object_position.x - target_position.x,
                              object_position.y - target_position.y,
                              object_position.z - target_position.z};
    const float factor = nmo_engine_block_delta_time(b) * 0.001f;
    const float force[3] = {get_in_float(b, 1, 1.0f) * factor, get_in_float(b, 2, 1.0f) * factor,
                            get_in_float(b, 3, 1.0f) * factor};
    const float damping[3] = {get_in_float(b, 4, 0.0f), get_in_float(b, 5, 0.0f),
                              get_in_float(b, 6, 0.0f)};
    const float offset[3] = {get_in_float(b, 7, 0.0f), get_in_float(b, 8, 0.0f),
                             get_in_float(b, 9, 0.0f)};
    float max_distance = get_in_float(b, 11, 0.0f);
    target_position.x += (offset_to.x - offset[0]) * force[0] + delta.x * damping[0];
    target_position.y += (offset_to.y - offset[1]) * force[1] + delta.y * damping[1];
    target_position.z += (offset_to.z - offset[2]) * force[2] + delta.z * damping[2];
    object_position.x -= target_position.x;
    object_position.y -= target_position.y;
    object_position.z -= target_position.z;
    bool at_object = object_position.x == 0.0f && object_position.y == 0.0f &&
                     object_position.z == 0.0f;
    float distance = at_object ? 0.0f : eng_vector_magnitude(&object_position);
    eng_vector_t vector = {0.0f, 0.0f, 0.0f};
    if (distance > max_distance && max_distance > 0.0f) {
        vector = object_position;
        vector.x *= max_distance;
        vector.y *= max_distance;
        vector.z *= max_distance;
        target_position.x = delta.x - vector.x;
        target_position.y = delta.y - vector.y;
        target_position.z = delta.z - vector.z;
    }
    eng_entity_set_position(engine, target, &target_position, frame, false);
    nmo_engine_block_set_output_value(b, 0, &vector, sizeof(vector));
    if (status) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT ProximityVolumeControl: while on, the Sound's gain follows how near
 * Object #2 is to Object #1: full within Near-Distance, nothing past
 * Far-Distance, linear in the squared distance between, as CalcGain makes
 * it (0.02 * 50^x). The engine keeps no gain: CKWaveSound::SetGain is an
 * effect on the sound.
 * Off stops it (Off out); On starts it (On out).
 */
static int bb_proximity_volume_control(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t on = 0;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        nmo_engine_block_set_local_value(b, 0, &on, sizeof(on));
    } else if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        on = 1;
        nmo_engine_block_set_local_value(b, 0, &on, sizeof(on));
    } else {
        on = get_local_int(b, 0, 0) & 0xFF;
    }
    nmo_object_id_t object1 = nmo_engine_block_get_input_object(b, 0);
    nmo_object_id_t object2 = nmo_engine_block_get_input_object(b, 1);
    if (object1 == 0 || object2 == 0 || eng_entity_index(engine, object1) < 0 ||
        eng_entity_index(engine, object2) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t sound = nmo_engine_block_get_input_object(b, 4);
    if (sound == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound, 0);
    if (on != 0) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT SpeedOMeter: the target's speed since the last run, in units a second
 * (the frame's time times Loop-Framedelay above 1), and with "Calculate
 * relative speed?" where it is between Minimum and Maximum Speed, clamped to
 * [0, 1]. As TT_Gravity_RT.dll has it, nothing guards a zero time or range.
 */
static int bb_speedometer(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool relative = (get_local_int(b, 1, 0) & 0xFF) != 0; /* CKBOOL, read by its low byte */
    int32_t frames = get_local_int(b, 2, 1);
    float delta = nmo_engine_block_delta_time(b);
    if (frames > 1) {
        delta = (float)((double)frames * delta);
    }
    eng_vector_t last = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 0, &last, sizeof(last));
    eng_vector_t pos = eng_entity_position(b->engine, entity, -1);
    float dx = pos.x - last.x, dy = pos.y - last.y, dz = pos.z - last.z;
    float speed = (float)(sqrt((double)(dz * dz + dy * dy + dx * dx)) * 1000.0 / delta);
    nmo_engine_block_set_output_value(b, 0, &speed, sizeof(speed));
    if (relative) {
        float min = get_in_float(b, 0, 0.0f), max = get_in_float(b, 1, 0.0f);
        double ratio = (double)(speed - min) / (double)(max - min);
        /* x87: a NaN is not <= 1, so it reads as 1 */
        speed = ratio <= 1.0 ? (ratio < 0.0 ? 0.0f : (float)ratio) : 1.0f;
        nmo_engine_block_set_output_value(b, 1, &speed, sizeof(speed));
    }
    nmo_engine_block_set_local_value(b, 0, &pos, sizeof(pos));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Extra: an extra-point pickup, as TT_Gravity_RT.dll has it. The target's
 * children name its parts after their 14th character ("Floor", the shadow;
 * "Ball0", the center) and its small balls after their 18th ("1" to "8").
 * Idle, the small balls circle the target, each about its own axis, until
 * the ball comes within ActivationDistance; then they fly away from where
 * the ball was for FlyAwayTime, then home in on it, each hit hiding one and
 * moving the next object of HitFrameGroup there. Showing and hiding are
 * effect events; the parts' scripts are activated and deactivated in the
 * scene. The engine has one scene, so the DLL's "no scene" errors cannot
 * happen.
 */
#define EXTRA_MAX_BALLS 8

typedef struct extra_ball {
    nmo_object_id_t ball;
    eng_vector_t position; /* where it was last */
    bool flying;           /* not hit yet */
    float force;
} extra_ball_t;

/* The SmallBallList local: the DLL keeps a linked list behind a pointer */
typedef struct extra_state {
    int32_t count; /* 0 for no list */
    extra_ball_t balls[EXTRA_MAX_BALLS];
} extra_state_t;

enum { EXTRA_IDLE = 0, EXTRA_AWAY = 1, EXTRA_HOMING = 2, EXTRA_READY = 3, EXTRA_NONE = -1 };

static void extra_show(blk_t *b, nmo_object_id_t id, bool show)
{
    if (id != 0) {
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, id, show ? 1 : 0);
    }
}

/* CKBeObject::GetScript(0) */
static nmo_object_id_t extra_script(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = id != 0 ? eng_map_get(&engine->object_map, id) : -1;
    if (index < 0 || engine->objects.data[index].script_count == 0) {
        return 0;
    }
    return engine->nodes.data[*eng_index(engine, engine->objects.data[index].first_script)].id;
}

/* CKScene::Activate (with a reset) or DeActivate of an object's first script */
static void extra_script_active(nmo_engine_t *engine, nmo_object_id_t id, bool active)
{
    nmo_object_id_t script = extra_script(engine, id);
    if (script != 0) {
        nmo_engine_activate_script(engine, script, active, active);
    }
}

/* CK3dEntity::GetChild(0); 0 for none */
static nmo_object_id_t extra_first_child(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine, id);
    int32_t child = index >= 0 ? engine->entities.data[index].first_child : -1;
    return child >= 0 ? engine->entities.data[child].id : 0;
}

/* GetChildOfObject: the first child whose name has `name` from character `at` on */
static nmo_object_id_t extra_child(nmo_engine_t *engine, nmo_object_id_t id, size_t at,
                                   const char *name)
{
    int32_t index = eng_entity_index(engine, id);
    for (int32_t c = index >= 0 ? engine->entities.data[index].first_child : -1; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        const char *child = nmo_engine_object_name(engine, engine->entities.data[c].id);
        size_t length = child != NULL ? strlen(child) : 0u;
        if (length >= at && strcmp(child + at, name) == 0) {
            return engine->entities.data[c].id;
        }
    }
    return 0;
}

static eng_vector_t extra_position(nmo_engine_t *engine, nmo_object_id_t id, int32_t ref)
{
    eng_vector_t zero = {0.0f, 0.0f, 0.0f};
    int32_t index = eng_entity_index(engine, id);
    return index >= 0 ? eng_entity_position(engine, index, ref) : zero;
}

static void extra_set_position(nmo_engine_t *engine, nmo_object_id_t id, const eng_vector_t *p,
                               int32_t ref)
{
    int32_t index = eng_entity_index(engine, id);
    if (index >= 0) {
        eng_entity_set_position(engine, index, p, ref, false);
    }
}

static float extra_distance2(const eng_vector_t *a, const eng_vector_t *b)
{
    return (b->z - a->z) * (b->z - a->z) + (b->y - a->y) * (b->y - a->y) +
           (b->x - a->x) * (b->x - a->x);
}

static nmo_object_id_t extra_local_object(blk_t *b, int pos)
{
    uint32_t id = 0;
    nmo_engine_block_get_local_value(b, pos, &id, sizeof(id));
    return id;
}

static void extra_set_local_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

static void extra_set_local_float(blk_t *b, int pos, float value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

static bool extra_flying(blk_t *b)
{
    return (get_in_int(b, 12, 0) & 0xFF) != 0;
}

/* The list's balls' scripts deactivated, the balls hidden, the list dropped (sub_10001F10) */
static void extra_free_list(blk_t *b, extra_state_t *state)
{
    for (int32_t i = 0; i < state->count; i++) {
        extra_script_active(b->engine, extra_first_child(b->engine, state->balls[i].ball), false);
        extra_show(b, state->balls[i].ball, false);
    }
    state->count = 0;
}

/* sub_10001EC0: the list freed, not initialized, no status */
static void extra_uninitialize(blk_t *b, extra_state_t *state)
{
    extra_free_list(b, state);
    extra_set_local_int(b, 5, 0);
    extra_set_local_int(b, 4, EXTRA_NONE);
}

static int extra_initialize(blk_t *b, extra_state_t *state)
{
    nmo_engine_t *engine = b->engine;
    int32_t status = get_local_int(b, 4, EXTRA_NONE);
    if (status == EXTRA_NONE) {
        state->count = 0;
    } else if (status == EXTRA_AWAY) {
        extra_uninitialize(b, state);
    } else if (state->count > 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t count = get_in_int(b, 1, 6);
    if (count <= 0) {
        count = 1;
    }
    extra_set_local_int(b, 7, count);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_entity_index(engine, target) < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool flying = extra_flying(b);
    nmo_object_id_t floor = extra_child(engine, target, 14, "Floor");
    nmo_object_id_t center = extra_child(engine, target, 14, "Ball0");
    if ((!flying && floor == 0) || center == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "There is no Floorglowobject in hirachy!" */
    }
    nmo_engine_block_set_local_value(b, 2, &floor, sizeof(floor));
    nmo_engine_block_set_local_value(b, 3, &center, sizeof(center));
    if (floor != 0) {
        nmo_engine_activate_object(engine, floor, false);
        extra_show(b, floor, false);
    }
    extra_script_active(engine, extra_first_child(engine, center), false);
    extra_show(b, center, false);
    float force = get_in_float(b, 7, 0.06f);
    float width = get_in_float(b, 9, 0.06f);
    width = (float)(width / (double)count);
    force = force - width;
    extra_state_t list;
    memset(&list, 0, sizeof(list));
    for (int32_t i = 0; i < count; i++) {
        static const char *const names[EXTRA_MAX_BALLS] = {"1", "2", "3", "4", "5", "6", "7", "8"};
        nmo_object_id_t ball =
            i < EXTRA_MAX_BALLS ? extra_child(engine, target, 18, names[i]) : 0;
        if (ball == 0) {
            return NMO_ENGINE_BR_PARAMETERERROR; /* "Missing one or more Billboards." */
        }
        nmo_object_id_t frame = extra_first_child(engine, ball);
        extra_script_active(engine, frame, false);
        extra_show(b, ball, false);
        extra_ball_t *small = &list.balls[list.count++];
        small->ball = ball;
        small->position = extra_position(engine, ball, -1);
        small->flying = true;
        force = width + force;
        small->force = force;
        if (frame == 0 || extra_script(engine, frame) == 0) {
            return NMO_ENGINE_BR_PARAMETERERROR; /* "There is no Frame..." / no script */
        }
        nmo_engine_activate_script(engine, extra_script(engine, frame), false, false);
    }
    *state = list;
    extra_set_local_float(b, 6, get_in_float(b, 10, 1000.0f));
    extra_set_local_int(b, 5, 1);
    extra_set_local_int(b, 4, EXTRA_IDLE);
    return NMO_ENGINE_BR_OK;
}

static int extra_activate(blk_t *b, extra_state_t *state)
{
    if (state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool flying = extra_flying(b);
    nmo_object_id_t shadow = extra_local_object(b, 2), center = extra_local_object(b, 3);
    extra_show(b, center, true);
    if (shadow != 0 && !flying) {
        extra_show(b, shadow, true);
    }
    for (int32_t i = 0; i < state->count; i++) {
        nmo_object_id_t frame = extra_first_child(b->engine, state->balls[i].ball);
        extra_script_active(b->engine, frame, true);
        extra_show(b, state->balls[i].ball, true);
        extra_script_active(b->engine, frame, true);
    }
    return NMO_ENGINE_BR_OK;
}

/* Idle: the small balls circle the target until the ball comes near */
static int extra_idle(blk_t *b, extra_state_t *state)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t ball = input_entity(b, 0);
    if (ball < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t ball_pos = eng_entity_position(engine, ball, -1);
    float activation = get_in_float(b, 2, 2.0f);
    activation = activation * activation;
    eng_vector_t target_pos = eng_entity_position(engine, target, -1);
    if (extra_distance2(&target_pos, &ball_pos) > activation) {
        float speed = get_in_float(b, 4, 5.0f) * get_local_float(b, 8, 0.0f);
        if (state->count == 0) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        /* Each ball's axis; from the seventh on, the sixth's */
        static const eng_vector_t axes[6] = {
            {0.0f, 1.0f, 0.0f},      {0.0f, 0.0f, 1.0f},          {1.0f, 0.0f, 0.0f},
            {0.5f, 0.5f, 0.70700002f}, {0.70700002f, -0.70700002f, 0.0f}, {-0.5f, -0.5f, 0.70700002f},
        };
        for (int32_t i = 0; i < state->count; i++) {
            const eng_vector_t *axis = &axes[i < 6 ? i : 5];
            eng_vector_t local = extra_position(engine, state->balls[i].ball, target);
            eng_matrix_t rotation;
            eng_matrix_from_rotation(&rotation, axis, speed);
            eng_vector_t p;
            eng_vector_rotate(&p, &rotation, &local);
            double length = sqrt((double)(p.z * p.z + p.y * p.y + p.x * p.x));
            if (length != 2.0) {
                /* back on the sphere of radius 2 */
                double inverse = 1.0 / length;
                float x = (float)(inverse * p.x), y = (float)(p.y * inverse);
                float z = (float)(inverse * p.z + inverse * p.z);
                p.x = x + x;
                p.y = y + y;
                p.z = z;
            }
            extra_set_position(engine, state->balls[i].ball, &p, target);
        }
        return NMO_ENGINE_BR_OK;
    }
    extra_set_local_int(b, 4, EXTRA_AWAY);
    bool flying = extra_flying(b);
    nmo_object_id_t shadow = extra_local_object(b, 2), center = extra_local_object(b, 3);
    if (!flying && shadow != 0) {
        extra_show(b, shadow, false);
    }
    if (center != 0) {
        nmo_object_id_t frame = extra_first_child(engine, center);
        extra_script_active(engine, frame, false);
        extra_show(b, center, false);
        if (frame != 0) {
            extra_show(b, frame, true);
            extra_script_active(engine, frame, true);
        }
    }
    if (state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    for (int32_t i = 0; i < state->count; i++) {
        state->balls[i].position = extra_position(engine, state->balls[i].ball, -1);
    }
    int32_t activated = 1;
    nmo_engine_block_set_output_value(b, 0, &activated, sizeof(activated));
    OUT_ON(b, 0);
    ball_pos.y = (float)(ball_pos.y - 2.0999999);
    nmo_engine_block_set_local_value(b, 10, &ball_pos, sizeof(ball_pos));
    return NMO_ENGINE_BR_OK;
}

/* Away: for FlyAwayTime the small balls fly from below where the ball was */
static int extra_away(blk_t *b, extra_state_t *state)
{
    eng_vector_t away = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 10, &away, sizeof(away));
    away.y = (float)(away.y - 3.0);
    float left = (float)(get_local_float(b, 6, 0.0f) - nmo_engine_block_delta_time(b));
    if (!(left > 0.0f)) {
        extra_set_local_int(b, 4, EXTRA_HOMING);
        return NMO_ENGINE_BR_OK;
    }
    extra_set_local_float(b, 6, left);
    float force = get_in_float(b, 5, 50.0f);
    float damping = get_in_float(b, 6, 0.5f);
    force = get_local_float(b, 8, 0.0f) * force;
    if (state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    for (int32_t i = 0; i < state->count; i++) {
        extra_ball_t *small = &state->balls[i];
        eng_vector_t old = small->position;
        eng_vector_t pos = extra_position(b->engine, small->ball, -1);
        small->position = pos;
        eng_vector_t next = {(pos.x - old.x) * damping + (pos.x - away.x) * force + pos.x,
                             (pos.y - old.y) * damping + (pos.y - away.y) * force + pos.y,
                             (pos.z - old.z) * damping + (pos.z - away.z) * force + pos.z};
        extra_set_position(b->engine, small->ball, &next, -1);
    }
    return NMO_ENGINE_BR_OK;
}

/* Homing: the small balls fly to the ball, each one near enough a hit */
static int extra_homing(blk_t *b, extra_state_t *state)
{
    nmo_engine_t *engine = b->engine;
    int32_t remaining = get_local_int(b, 7, 0);
    if (remaining <= 0) {
        extra_set_local_int(b, 4, EXTRA_READY);
        OUT_ON(b, 2);
        return NMO_ENGINE_BR_OK;
    }
    int32_t ball = input_entity(b, 0);
    if (ball < 0 || state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t ball_pos = eng_entity_position(engine, ball, -1);
    float damping = get_in_float(b, 8, 0.9f);
    float time = get_local_float(b, 8, 0.0f);
    int32_t hits = 0;
    for (int32_t i = 0; i < state->count; i++) {
        extra_ball_t *small = &state->balls[i];
        if (!small->flying) {
            continue;
        }
        eng_vector_t pos = extra_position(engine, small->ball, -1);
        float collision = get_in_float(b, 3, 2.0f);
        /* The squared distance against the distance itself, as the DLL compares them */
        if (extra_distance2(&pos, &ball_pos) <= collision) {
            hits++;
            small->flying = false;
            extra_set_local_int(b, 7, --remaining);
            nmo_object_id_t frame = extra_first_child(engine, small->ball);
            if (frame != 0) {
                extra_script_active(engine, frame, false);
            }
            extra_show(b, small->ball, false);
            nmo_object_id_t group = nmo_engine_block_get_input_object(b, 11);
            nmo_object_id_t hit = group != 0 ? nmo_engine_group_object(engine, group, i) : 0;
            if (hit != 0) {
                eng_vector_t at = extra_position(engine, small->ball, -1);
                extra_set_position(engine, hit, &at, -1);
                extra_script_active(engine, hit, true);
            }
        } else {
            eng_vector_t old = small->position;
            small->position = pos;
            eng_vector_t next = {
                (ball_pos.x - pos.x) * small->force * time + (pos.x - old.x) * damping + pos.x,
                (ball_pos.y - pos.y) * small->force * time + (pos.y - old.y) * damping + pos.y,
                (ball_pos.z - pos.z) * small->force * time + (pos.z - old.z) * damping + pos.z};
            extra_set_position(engine, small->ball, &next, -1);
        }
    }
    if (hits > 0) {
        nmo_engine_block_set_output_value(b, 0, &hits, sizeof(hits));
        OUT_ON(b, 1);
    }
    return NMO_ENGINE_BR_OK;
}

/* DestroyExtra (Off): the parts hidden and their scripts off; the list stays */
static void extra_destroy(blk_t *b, extra_state_t *state)
{
    if (state->count == 0) {
        return;
    }
    bool flying = extra_flying(b);
    nmo_object_id_t shadow = extra_local_object(b, 2), center = extra_local_object(b, 3);
    if (center != 0) {
        extra_script_active(b->engine, extra_first_child(b->engine, center), false);
        extra_show(b, center, false);
    }
    if (shadow != 0 && !flying) {
        extra_show(b, shadow, false);
    }
    for (int32_t i = 0; i < state->count; i++) {
        extra_script_active(b->engine, extra_first_child(b->engine, state->balls[i].ball), false);
        extra_show(b, state->balls[i].ball, false);
    }
}

static int bb_tt_extra(blk_t *b)
{
    extra_state_t *state = (extra_state_t *)nmo_engine_block_state(b, sizeof(extra_state_t));
    if (state == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    bool active = false;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        extra_set_local_int(b, 0, 0);
        extra_destroy(b, state);
        return NMO_ENGINE_BR_OK;
    }
    bool initialized = (get_local_int(b, 5, 0) & 0xFF) != 0;
    bool exactness = (get_local_int(b, 11, 1) & 0xFF) != 0;
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        if (!initialized) {
            /* "Extra is'nt intialized yet, this could slow all down." */
            if (extra_initialize(b, state) != NMO_ENGINE_BR_OK) {
                return NMO_ENGINE_BR_PARAMETERERROR;
            }
            initialized = true;
        }
        active = true;
        extra_set_local_int(b, 0, 1);
        extra_set_local_float(b, 8, (float)(nmo_engine_block_delta_time(b) * 0.001));
        if (exactness) {
            extra_set_local_int(b, 9, get_in_int(b, 13, 2));
        }
        if (extra_activate(b, state) != NMO_ENGINE_BR_OK) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
    } else if (nmo_engine_block_is_input_active(b, 2)) {
        IN_OFF(b, 2);
        if (!initialized) {
            return extra_initialize(b, state) == NMO_ENGINE_BR_OK
                       ? NMO_ENGINE_BR_OK
                       : NMO_ENGINE_BR_PARAMETERERROR;
        }
    } else {
        active = (get_local_int(b, 0, 0) & 0xFF) != 0;
    }
    if (!active || !initialized) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (exactness) {
        /* Every Exactness Frame frames, the frame's time is the one the speeds use */
        uint32_t counter = (uint32_t)get_local_int(b, 9, 0) - 1u;
        if ((counter & 0x7FFFFFFFu) == 0) { /* read as a float: 0.0 or -0.0 */
            extra_set_local_float(b, 8, (float)(nmo_engine_block_delta_time(b) * 0.001));
            counter = (uint32_t)get_in_int(b, 13, 2);
        }
        extra_set_local_int(b, 9, (int32_t)counter);
    }
    int status = NMO_ENGINE_BR_OK;
    switch (get_local_int(b, 4, EXTRA_NONE)) {
    case EXTRA_IDLE: status = extra_idle(b, state); break;
    case EXTRA_AWAY: status = extra_away(b, state); break;
    case EXTRA_HOMING: status = extra_homing(b, state); break;
    default: break;
    }
    /* ExecuteExtra maps any failure to a parameter error ("Could'nt execute") */
    return status == NMO_ENGINE_BR_OK ? NMO_ENGINE_BR_ACTIVATENEXTFRAME
                                      : NMO_ENGINE_BR_PARAMETERERROR;
}

/*
 * TT_ExtraCallback: a reset, a deactivated script or a removed block takes
 * an initialized Extra down; a load or an attachment starts without a list.
 */
static int cb_tt_extra(blk_t *b, uint32_t message)
{
    extra_state_t *state = (extra_state_t *)nmo_engine_block_state(b, sizeof(extra_state_t));
    if (state == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        if ((get_local_int(b, 5, 0) & 0xFF) != 0) {
            extra_uninitialize(b, state);
        }
    } else if (message == NMO_ENGINE_MSG_LOAD) {
        state->count = 0;
        extra_set_local_int(b, 5, 0);
        extra_set_local_int(b, 0, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Ray Intersection (Logics/Test, Logics.dll): the nearest 3D object within
 * Depth on the ray (in the Referential's space given one), among the
 * Filter group's objects or all 3D objects, characters and 3D sprites in the
 * level scene, the owner skipped unless "Skip Owner" is off. Each object is
 * first tested by its bounding sphere and world box, then its mesh. True
 * with what it hit (with "Output Param Update": its face, the face's vertex
 * nearest the hit, the point and normal in world space, the distance),
 * False otherwise. Characters' body parts and 3D sprites are not hit, and
 * every object counts as visible for "Skip Transparent".
 */
typedef struct ray_best {
    eng_vector_t origin, dir, end;
    float depth;
    nmo_object_id_t object;
    eng_ray_hit_t hit;
    float distance2;
} ray_best_t;

static void ray_test(nmo_engine_t *engine, nmo_object_id_t id, ray_best_t *ray)
{
    uint32_t class_id = (uint32_t)nmo_engine_object_class(engine, id);
    int32_t index = eng_entity_index(engine, id);
    if (index < 0 || !nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_3DENTITY) ||
        !eng_in_scene(engine, id) ||
        nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_CHARACTER) ||
        nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_SPRITE3D)) {
        return;
    }
    float radius = eng_entity_radius(engine, index);
    eng_vector_t center;
    eng_entity_bary_center(engine, index, &center);
    float dx = center.x - ray->origin.x, dy = center.y - ray->origin.y, dz = center.z - ray->origin.z;
    float distance2 = dz * dz + dy * dy + dx * dx;
    float reach = radius + ray->depth;
    if (!(distance2 < reach * reach)) {
        return;
    }
    double along = (double)dz * ray->dir.z + (double)dy * ray->dir.y + (double)dx * ray->dir.x;
    if (!(radius + along > 0.0) || !(distance2 - along * along < (double)radius * radius)) {
        return; /* behind the origin, or beside the ray */
    }
    eng_vector_t min, max;
    if (!eng_entity_world_box(engine, index, &min, &max) || !eng_ray_box(&ray->origin, &ray->dir, &min, &max)) {
        return;
    }
    eng_ray_hit_t hit;
    if (eng_entity_ray_intersection(engine, index, &ray->origin, &ray->end, &hit) == 0) {
        return;
    }
    eng_vector_t world;
    eng_entity_transform(engine, index, &world, &hit.point, -1);
    float wx = world.x - ray->origin.x, wy = world.y - ray->origin.y, wz = world.z - ray->origin.z;
    float d2 = wz * wz + wy * wy + wx * wx;
    if (d2 < ray->distance2) {
        ray->distance2 = d2;
        ray->object = id;
        ray->hit = hit;
    }
}

static int bb_ray_intersection(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_target(b);
    if (owner == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    if (get_local_int(b, 1, 1) == 0) {
        owner = 0; /* "Skip Owner" off */
    }
    ray_best_t ray;
    memset(&ray, 0, sizeof(ray));
    nmo_engine_block_get_input_value(b, 0, &ray.origin, sizeof(ray.origin));
    nmo_engine_block_get_input_value(b, 1, &ray.dir, sizeof(ray.dir));
    int32_t referential = input_entity(b, 2);
    if (referential >= 0) {
        eng_entity_transform(engine, referential, &ray.origin, &ray.origin, -1);
        eng_entity_transform_vector(engine, referential, &ray.dir, &ray.dir, -1, false);
    }
    eng_vector_normalize(&ray.dir);
    ray.depth = get_in_float(b, 3, 100.0f);
    ray.end = (eng_vector_t){ray.dir.x + ray.origin.x, ray.dir.y + ray.origin.y,
                             ray.dir.z + ray.origin.z};
    ray.distance2 = ray.depth * ray.depth;
    nmo_object_id_t filter = nmo_engine_block_get_input_object(b, 4);
    if (filter != 0) {
        int32_t count = nmo_engine_group_count(engine, filter);
        for (int32_t i = 0; i < count; i++) {
            nmo_object_id_t id = nmo_engine_group_object(engine, filter, i);
            if (id != owner) {
                ray_test(engine, id, &ray);
            }
        }
    } else {
        /* The context's 3D objects, then characters, then 3D sprites */
        static const uint32_t classes[3] = {NMO_CID_3DOBJECT, NMO_CID_CHARACTER, NMO_CID_SPRITE3D};
        for (int c = 0; c < 3; c++) {
            for (size_t i = 0; i < engine->entities.count; i++) {
                nmo_object_id_t id = engine->entities.data[i].id;
                if (!engine->entities.data[i].destroyed && id != owner &&
                    (uint32_t)nmo_engine_object_class(engine, id) == classes[c]) {
                    ray_test(engine, id, &ray);
                }
            }
        }
    }
    if (ray.object == 0 || !((double)ray.depth * ray.depth > ray.distance2)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t object = ray.object;
    nmo_engine_block_set_output_value(b, 0, &object, sizeof(object));
    int32_t index = eng_entity_index(engine, ray.object);
    if (get_local_int(b, 0, 1) != 0 && index >= 0) {
        /* The face's vertex nearest the hit (the later one on a tie) */
        float distances[3];
        for (int k = 0; k < 3; k++) {
            float x = ray.hit.corners[k].x - ray.hit.point.x, y = ray.hit.corners[k].y - ray.hit.point.y,
                  z = ray.hit.corners[k].z - ray.hit.point.z;
            distances[k] = (float)sqrt((double)(z * z + y * y + x * x));
        }
        int32_t nearest;
        if (distances[0] >= distances[1]) {
            nearest = distances[1] >= distances[2] ? ray.hit.vertices[2] : ray.hit.vertices[1];
        } else {
            nearest = distances[0] < distances[2] ? ray.hit.vertices[0] : ray.hit.vertices[2];
        }
        eng_vector_t point, normal;
        eng_entity_transform(engine, index, &point, &ray.hit.point, -1);
        eng_entity_transform_vector(engine, index, &normal, &ray.hit.normal, -1, false);
        eng_vector_normalize(&normal);
        float distance = (float)sqrt((double)ray.distance2);
        nmo_engine_block_set_output_value(b, 1, &ray.hit.face, sizeof(ray.hit.face));
        nmo_engine_block_set_output_value(b, 2, &nearest, sizeof(nearest));
        nmo_engine_block_set_output_value(b, 3, &point, sizeof(point));
        nmo_engine_block_set_output_value(b, 4, &normal, sizeof(normal));
        nmo_engine_block_set_output_value(b, 5, &distance, sizeof(distance));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* VxOBB: a local box through a world matrix (unit axes, extents scaled by the axes' lengths) */
typedef struct obb {
    float center[3];
    float axis[3][3];
    float extent[3];
} obb_t;

static void obb_from(nmo_engine_t *engine, int32_t entity, obb_t *out)
{
    eng_vector_t min, max;
    eng_entity_local_box(engine, entity, &min, &max);
    const eng_matrix_t *m = &engine->entities.data[entity].world;
    eng_vector_t center = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f}, c;
    eng_vector_transform(&c, m, &center);
    out->center[0] = c.x;
    out->center[1] = c.y;
    out->center[2] = c.z;
    const float half[3] = {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    for (int i = 0; i < 3; i++) {
        double length = sqrt((double)m->m[i][0] * m->m[i][0] + (double)m->m[i][1] * m->m[i][1] +
                             (double)m->m[i][2] * m->m[i][2]);
        for (int k = 0; k < 3; k++) {
            out->axis[i][k] = length > 0.0 ? (float)(m->m[i][k] / length) : 0.0f;
        }
        out->extent[i] = (float)(length * half[i]);
    }
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* VxIntersect::OBBOBB: no separating axis among the fifteen */
static bool obb_overlap(const obb_t *a, const obb_t *b)
{
    const float t[3] = {b->center[0] - a->center[0], b->center[1] - a->center[1],
                        b->center[2] - a->center[2]};
    float r[3][3], abs_r[3][3], ta[3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] = dot3(a->axis[i], b->axis[j]);
            abs_r[i][j] = fabsf(r[i][j]);
        }
        ta[i] = dot3(t, a->axis[i]);
        if (fabsf(ta[i]) > a->extent[i] + abs_r[i][0] * b->extent[0] + abs_r[i][1] * b->extent[1] +
                               abs_r[i][2] * b->extent[2]) {
            return false;
        }
    }
    for (int j = 0; j < 3; j++) {
        if (fabsf(dot3(t, b->axis[j])) > b->extent[j] + abs_r[0][j] * a->extent[0] +
                                             abs_r[1][j] * a->extent[1] + abs_r[2][j] * a->extent[2]) {
            return false;
        }
    }
    for (int i = 0; i < 3; i++) {
        int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        for (int j = 0; j < 3; j++) {
            int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            float distance = fabsf(ta[i2] * r[i1][j] - ta[i1] * r[i2][j]);
            float reach = a->extent[i1] * abs_r[i2][j] + a->extent[i2] * abs_r[i1][j] +
                          b->extent[j1] * abs_r[i][j2] + b->extent[j2] * abs_r[i][j1];
            if (distance > reach) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Box Box Intersection (Collisions.dll): True when the two entities' boxes,
 * local ones through their world matrices (CKCollisionManager::
 * BoxBoxIntersection with local boxes), overlap; False otherwise. The
 * Hierarchy inputs, and characters' body parts, are not taken into account.
 */
static int bb_box_box_intersection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t first = input_entity(b, 0), second = input_entity(b, 1);
    if (first >= 0 && second >= 0) {
        obb_t a, c;
        obb_from(b->engine, first, &a);
        obb_from(b->engine, second, &c);
        if (obb_overlap(&a, &c)) {
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_OK;
        }
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Simple Shadow: the target's shadow drawn on the floors under it (a pre
 * render callback): On (Exit On) starts it and it stays active, Off (Exit
 * Off) ends it. The engine draws nothing; starting and ending are effects.
 */
static int bb_simple_shadow(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 1);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static const nmo_engine_block_desc_t eng_3dtrans_blocks[] = {
    {NMO_GUID(0xe456e78a, 0x456789aa), "Set Position", bb_set_position, NULL, NULL},
    {NMO_GUID(0xaa4aa6f0, 0xddefdef4), "Set World Matrix", bb_set_world_matrix, NULL, NULL},
    {NMO_GUID(0x41236987, 0xa54a87a6), "Scale", bb_scale, NULL, NULL},
    {NMO_GUID(0x9d9d9d98, 0x7e7a7f75), "Set Parent", bb_set_parent, NULL, NULL},
    {NMO_GUID(0xffffffee, 0xeeffffff), "Rotate", bb_rotate, NULL, NULL},
    {NMO_GUID(0x0c4966d8, 0x6c0c6d14), "Set Euler Orientation", bb_set_euler_orientation, NULL,
     NULL},
    {NMO_GUID(0x26843971, 0x86249317), "Look At", bb_look_at, NULL, NULL},
    {NMO_GUID(0x3d4861f8, 0x2861703d), "TT LookAt", bb_tt_look_at, NULL, NULL},
    {NMO_GUID(0x085207eb, 0x584950d8), "Get Nearest In Group", bb_get_nearest_in_group, NULL,
     NULL},
    {NMO_GUID(0x2a2a63ca, 0x00826175), "TT Scaleable Proximity", bb_scaleable_proximity, NULL,
     NULL},
    {NMO_GUID(0x0fd4755f, 0x7de22dc8), "TT Set Dynamic Position", bb_set_dynamic_position, NULL,
     NULL},
    {NMO_GUID(0x51bd5521, 0x672c67bf), "TT SpeedOMeter", bb_speedometer, NULL, NULL},
    {NMO_GUID(0x38571b61, 0x64cd2174), "TT ProximityVolumeControl", bb_proximity_volume_control,
     NULL, NULL},
    {NMO_GUID(0x671e4a87, 0x383b2912), "Ray Intersection", bb_ray_intersection, NULL, NULL},
    {NMO_GUID(0x64154401, 0x76cf37af), "Box Box Intersection", bb_box_box_intersection, NULL, NULL},
    {NMO_GUID(0x7f0517c2, 0x52cc76bb), "TT Simple Shadow", bb_simple_shadow, NULL, NULL},
    {NMO_GUID(0x36106bd9, 0x51813906), "TT Extra", bb_tt_extra, cb_tt_extra, NULL},
};

nmo_status_t eng_register_3dtrans(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_3dtrans_blocks) / sizeof(eng_3dtrans_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_3dtrans_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
