/**
 * @file animation.c
 * @brief CKObjectAnimation::SetStep: an entity's local pose from its
 *        animation's keys (CK2_3D's RCKObjectAnimation, VxMath's VxQuaternion).
 *
 * The engine evaluates the linear position, rotation and scale controllers;
 * the TCB, Bezier, scale-axis and morph controllers change nothing.
 */

#include "../engine_internal.h"

#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

#define ANIM_LINEAR_POSITION 0x637c4301u
#define ANIM_LINEAR_ROTATION 0x49ed4002u
#define ANIM_LINEAR_SCALE 0x654a3a04u

typedef struct anim_quat {
    float x, y, z, w;
} anim_quat_t;

/* The segment of keys around `time`: the one ending at an exact interior key (0x1004B8DF) */
static void anim_segment(const float *keys, uint32_t stride, uint32_t count, float time,
                         uint32_t *low, uint32_t *high)
{
    uint32_t lo = 0, hi = count - 1;
    while (lo + 1 < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (keys[mid * stride] < time) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    *low = lo;
    *high = hi;
}

/* RCKLinearPositionController::Evaluate (and the scale one, whose keys are alike): time, xyz */
static void anim_linear_vector(const nmo_objanim_controller_t *c, float time, eng_vector_t *out)
{
    const float *keys = (const float *)c->data;
    uint32_t last = c->key_count - 1;
    const float *key = NULL;
    if (time <= keys[0]) {
        key = keys;
    } else if (time >= keys[last * 4]) {
        key = &keys[last * 4];
    }
    if (key != NULL) {
        *out = (eng_vector_t){key[1], key[2], key[3]};
        return;
    }
    uint32_t low = 0, high = 0;
    anim_segment(keys, 4, c->key_count, time, &low, &high);
    const float *a = &keys[low * 4], *b = &keys[high * 4];
    double duration = (double)b[0] - a[0];
    double left = ((double)b[0] - time) / duration, right = ((double)time - a[0]) / duration;
    out->x = (float)(a[1] * left + b[1] * right);
    out->y = (float)(a[2] * left + b[2] * right);
    out->z = (float)(a[3] * left + b[3] * right);
}

/* VxQuaternionInterpolate: slerp on the shorter arc, linear when the two are within acos 0.99 */
static anim_quat_t anim_slerp(double t, anim_quat_t a, anim_quat_t b)
{
    double dot = (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z + (double)a.w * b.w;
    double sign = dot < 0.0 ? -1.0 : 1.0, cosine = dot < 0.0 ? -dot : dot;
    double wa = 1.0 - t, wb = sign * t;
    if (cosine < 0.99) {
        double angle = acos(cosine), inverse_sine = 1.0 / sin(angle);
        wa = sin((1.0 - t) * angle) * inverse_sine;
        wb = sign * sin(t * angle) * inverse_sine;
    }
    return (anim_quat_t){(float)(a.x * wa + b.x * wb), (float)(a.y * wa + b.y * wb),
                         (float)(a.z * wa + b.z * wb), (float)(a.w * wa + b.w * wb)};
}

/* RCKLinearRotationController::Evaluate: time, quaternion */
static anim_quat_t anim_linear_rotation(const nmo_objanim_controller_t *c, float time)
{
    const float *keys = (const float *)c->data;
    uint32_t last = c->key_count - 1;
    if (time <= keys[0]) {
        return (anim_quat_t){keys[1], keys[2], keys[3], keys[4]};
    }
    if (time >= keys[last * 5]) {
        const float *k = &keys[last * 5];
        return (anim_quat_t){k[1], k[2], k[3], k[4]};
    }
    uint32_t low = 0, high = 0;
    anim_segment(keys, 5, c->key_count, time, &low, &high);
    const float *a = &keys[low * 5], *b = &keys[high * 5];
    anim_quat_t q1 = {a[1], a[2], a[3], a[4]}, q2 = {b[1], b[2], b[3], b[4]};
    /* The phase from the nearer end; from the right one, the left one's hemisphere kept */
    double elapsed = (double)time - a[0], remaining = (double)b[0] - time;
    bool reversed = elapsed > remaining;
    float t = (float)((reversed ? remaining : elapsed) / ((double)b[0] - a[0]));
    if (!reversed) {
        return anim_slerp(t, q1, q2);
    }
    anim_quat_t q = anim_slerp(t, q2, q1);
    if (q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w < 0.0f) {
        q = (anim_quat_t){-q.x, -q.y, -q.z, -q.w};
    }
    return q;
}

/* VxQuaternion::ToMatrix */
static void anim_quat_matrix(anim_quat_t q, eng_matrix_t *m)
{
    double x = q.x, y = q.y, z = q.z, w = q.w;
    double s = 2.0 / (x * x + y * y + z * z + w * w);
    double xs = x * s, ys = y * s, zs = z * s;
    double wx = w * xs, wy = w * ys, wz = w * zs;
    double xx = x * xs, xy = x * ys, xz = x * zs;
    double yy = y * ys, yz = y * zs, zz = z * zs;
    eng_matrix_identity(m);
    m->m[0][0] = (float)(1.0 - (yy + zz));
    m->m[0][1] = (float)(xy - wz);
    m->m[0][2] = (float)(xz + wy);
    m->m[1][0] = (float)(xy + wz);
    m->m[1][1] = (float)(1.0 - (zz + xx));
    m->m[1][2] = (float)(yz - wx);
    m->m[2][0] = (float)(xz - wy);
    m->m[2][1] = (float)(yz + wx);
    m->m[2][2] = (float)(1.0 - (yy + xx));
}

/* A controller with keys of the size its type has; NULL for none */
static const nmo_objanim_controller_t *anim_controller(const nmo_objectanimation_state_t *state,
                                                        uint32_t type, uint32_t key_size)
{
    for (uint32_t i = 0; i < state->controller_count; i++) {
        const nmo_objanim_controller_t *c = &state->controllers[i];
        if (c->type == type && c->key_count > 0 && c->data != NULL &&
            c->data_size == c->key_count * key_size) {
            return c;
        }
    }
    return NULL;
}

bool eng_object_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    nmo_object_t *object = eng_find_object(engine, animation);
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_OBJECTANIMATION) {
        return false;
    }
    const nmo_objectanimation_state_t *state = (const nmo_objectanimation_state_t *)nmo_object_get_state(object);
    int32_t entity = state != NULL ? eng_entity_index(engine, eng_object_ref(engine, animation,
                                                                                &state->entity))
                                   : -1;
    if (entity < 0) {
        return false; /* CKERR_NOTINITIALIZED */
    }
    float frame = step * (state->has_length ? state->length : 0.0f);
    const nmo_objanim_controller_t *position = anim_controller(state, ANIM_LINEAR_POSITION, 16);
    const nmo_objanim_controller_t *rotation = anim_controller(state, ANIM_LINEAR_ROTATION, 20);
    const nmo_objanim_controller_t *scaling = anim_controller(state, ANIM_LINEAR_SCALE, 16);
    if (position == NULL && rotation == NULL && scaling == NULL) {
        return true;
    }
    eng_matrix_t local = engine->entities.data[entity].local;
    eng_vector_t pos = {local.m[3][0], local.m[3][1], local.m[3][2]};
    if (position != NULL) {
        anim_linear_vector(position, frame, &pos);
    }
    if (rotation != NULL || scaling != NULL) {
        /* The channels the animation lacks, from the local matrix as it is */
        eng_vector_t scale = {1.0f, 1.0f, 1.0f};
        eng_matrix_t rot;
        eng_matrix_identity(&rot);
        float *axes[3] = {&scale.x, &scale.y, &scale.z};
        for (int r = 0; r < 3; r++) {
            float length = sqrtf(local.m[r][0] * local.m[r][0] + local.m[r][1] * local.m[r][1] +
                                 local.m[r][2] * local.m[r][2]);
            *axes[r] = length;
            for (int c = 0; c < 3; c++) {
                rot.m[r][c] = length > 0.0f ? local.m[r][c] / length : (r == c ? 1.0f : 0.0f);
            }
        }
        if (rotation != NULL) {
            anim_quat_matrix(anim_linear_rotation(rotation, frame), &rot);
        }
        if (scaling != NULL) {
            anim_linear_vector(scaling, frame, &scale);
        }
        /* The stretch before the rotation */
        eng_matrix_t stretch;
        eng_matrix_identity(&stretch);
        stretch.m[0][0] = scale.x;
        stretch.m[1][1] = scale.y;
        stretch.m[2][2] = scale.z;
        eng_matrix_multiply(&local, &rot, &stretch);
    }
    local.m[3][0] = pos.x;
    local.m[3][1] = pos.y;
    local.m[3][2] = pos.z;
    eng_entity_set_local_matrix(engine, entity, &local, false);
    return true;
}
