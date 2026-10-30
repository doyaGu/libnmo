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
