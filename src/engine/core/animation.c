/**
 * @file animation.c
 * @brief CKObjectAnimation::SetStep: an entity's local pose from its
 *        animation's keys (CK2_3D's RCKObjectAnimation, VxMath's VxQuaternion).
 *
 * The engine evaluates the linear position, rotation and scale controllers
 * and the TCB rotation one; the TCB position and scale, Bezier, scale-axis
 * and morph controllers change nothing. A keyed animation steps each of its
 * object animations (CKKeyedAnimation::SetStep).
 */

#include "../engine_internal.h"

#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_class_ids.h"

#include <math.h>
#include <string.h>

#define ANIM_LINEAR_POSITION 0x637c4301u
#define ANIM_LINEAR_ROTATION 0x49ed4002u
#define ANIM_LINEAR_SCALE 0x654a3a04u
#define ANIM_TCB_ROTATION 0x45b52a02u

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

static anim_quat_t anim_quat_scale(double s, anim_quat_t q)
{
    return (anim_quat_t){(float)(s * q.x), (float)(s * q.y), (float)(s * q.z), (float)(s * q.w)};
}

static float anim_quat_dot(anim_quat_t a, anim_quat_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/* Vx3DQuaternionMultiply */
static anim_quat_t anim_quat_multiply(anim_quat_t l, anim_quat_t r)
{
    double x = l.x, y = l.y, z = l.z, w = l.w;
    return (anim_quat_t){(float)(x * r.w + w * r.x + y * r.z - z * r.y),
                         (float)(y * r.w + w * r.y + z * r.x - x * r.z),
                         (float)(z * r.w + w * r.z + x * r.y - y * r.x),
                         (float)(w * r.w - x * r.x - y * r.y - z * r.z)};
}

/* LnDif(p, q): Ln of Vx3DQuaternionDivide(q, p), conjugate(p) * q */
static anim_quat_t anim_quat_ln_dif(anim_quat_t p, anim_quat_t q)
{
    double x = q.x, y = q.y, z = q.z, w = q.w;
    anim_quat_t d = {(float)(p.w * x - p.x * w - z * p.y + y * p.z),
                     (float)(y * p.w - w * p.y - x * p.z + z * p.x),
                     (float)(z * p.w - w * p.z - y * p.x + x * p.y),
                     (float)(z * p.z + y * p.y + x * p.x + p.w * w)};
    double magnitude = sqrt((double)d.x * d.x + (double)d.y * d.y + (double)d.z * d.z);
    double scale = magnitude > 0.0 ? atan2(magnitude, (double)d.w) / magnitude : magnitude;
    return (anim_quat_t){(float)(scale * d.x), (float)(scale * d.y), (float)(scale * d.z), 0.0f};
}

/* Exp: of the vector part */
static anim_quat_t anim_quat_exp(anim_quat_t q)
{
    double magnitude = sqrt((double)q.x * q.x + (double)q.y * q.y + (double)q.z * q.z);
    double scale = magnitude == 0.0 ? 1.0 : sin(magnitude) / magnitude;
    return (anim_quat_t){(float)(scale * q.x), (float)(scale * q.y), (float)(scale * q.z),
                         (float)cos(magnitude)};
}

/* Squad: slerp of the slerps, by 2t(1 - t) */
static anim_quat_t anim_squad(float t, anim_quat_t q1, anim_quat_t a, anim_quat_t b, anim_quat_t q2)
{
    anim_quat_t inner = anim_slerp(t, a, b), outer = anim_slerp(t, q1, q2);
    return anim_slerp(2.0 * t * (1.0 - (double)t), outer, inner);
}

/* ApplyEaseParameters: the ease out of one key and into the next */
static float anim_ease(float t, float ease_from, float ease_to)
{
    double from = ease_from, to = ease_to, total = from + to;
    if (t == 0.0f || t == 1.0f || total == 0.0) {
        return t;
    }
    if (total > 1.0) {
        from /= total;
        to /= total;
    }
    double scale = 1.0 / (2.0 - from - to);
    if (t < from) {
        return (float)((scale / from) * t * t);
    }
    if (t < 1.0 - to) {
        return (float)((2.0 * t - from) * scale);
    }
    double remaining = 1.0 - t;
    return (float)(1.0 - (scale / to) * remaining * remaining);
}

/*
 * A TCB rotation key: time, quaternion, tension, continuity, bias, ease to,
 * ease from (CKTCBRotationKey, 40 bytes)
 */
typedef struct anim_tcb_key {
    float time;
    anim_quat_t rot;
    float tension, continuity, bias, ease_to, ease_from;
} anim_tcb_key_t;

/* ComputeTCBQuaternionTangents for key `i`: its incoming and outgoing tangents */
static void anim_tcb_tangents(const anim_tcb_key_t *keys, uint32_t count, uint32_t i,
                              anim_quat_t *in, anim_quat_t *out)
{
    const anim_tcb_key_t *key = &keys[i];
    anim_quat_t previous = {0.0f, 0.0f, 0.0f, 1.0f}, next = {0.0f, 0.0f, 0.0f, 1.0f};
    if (i > 0) {
        anim_quat_t neighbor = keys[i - 1].rot;
        if (anim_quat_dot(neighbor, key->rot) < 0.0f) {
            neighbor = anim_quat_scale(-1.0, neighbor);
        }
        previous = anim_quat_ln_dif(neighbor, key->rot);
    }
    if (i + 1 < count) {
        anim_quat_t neighbor = keys[i + 1].rot;
        if (anim_quat_dot(neighbor, key->rot) < 0.0f) {
            neighbor = anim_quat_scale(-1.0, neighbor);
        }
        next = anim_quat_ln_dif(key->rot, neighbor);
    }
    if (i == 0) {
        previous = next;
    }
    if (i + 1 == count) {
        next = previous;
    }
    /* ComputeTCBTimeWeights, then ComputeTCBCoefficients */
    float weight_previous = 1.0f, weight_next = 1.0f;
    if (i > 0 && i + 1 < count) {
        double duration = (double)keys[i + 1].time - keys[i - 1].time;
        weight_previous = (float)(((double)key->time - keys[i - 1].time) * 2.0 / duration);
        weight_next = (float)(((double)keys[i + 1].time - key->time) * 2.0 / duration);
        float magnitude = fabsf(key->continuity);
        weight_previous = weight_previous + magnitude - magnitude * weight_previous;
        weight_next = weight_next + magnitude - magnitude * weight_next;
    }
    float half_tension = (1.0f - key->tension) * 0.5f;
    float continuity_minus = 1.0f - key->continuity, continuity_plus = 2.0f - continuity_minus;
    float bias_minus = 1.0f - key->bias, bias_plus = 2.0f - bias_minus;
    float minus = half_tension * continuity_minus, plus = half_tension * continuity_plus;
    float in_previous = minus * bias_plus * weight_previous;
    float in_next = plus * bias_minus * weight_previous;
    float out_previous = plus * bias_plus * weight_next;
    float out_next = minus * bias_minus * weight_next;
    anim_quat_t a = anim_quat_scale(1.0f - in_previous, previous), b = anim_quat_scale(in_next, next);
    anim_quat_t incoming = {0.5f * (a.x - b.x), 0.5f * (a.y - b.y), 0.5f * (a.z - b.z),
                            0.5f * (a.w - b.w)};
    a = anim_quat_scale(out_previous, previous);
    b = anim_quat_scale(out_next - 1.0f, next);
    anim_quat_t outgoing = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z),
                            0.5f * (a.w + b.w)};
    *in = anim_quat_multiply(key->rot, anim_quat_exp(incoming));
    *out = anim_quat_multiply(key->rot, anim_quat_exp(outgoing));
}

/* RCKTCBRotationController::Evaluate */
static anim_quat_t anim_tcb_rotation(const nmo_objanim_controller_t *c, float time)
{
    const anim_tcb_key_t *keys = (const anim_tcb_key_t *)c->data;
    uint32_t last = c->key_count - 1;
    if (time <= keys[0].time) {
        return keys[0].rot;
    }
    if (time >= keys[last].time) {
        return keys[last].rot;
    }
    uint32_t low = 0, high = 0;
    anim_segment((const float *)c->data, 10, c->key_count, time, &low, &high);
    const anim_tcb_key_t *a = &keys[low], *b = &keys[high];
    double elapsed = (double)time - a->time, remaining = (double)b->time - time;
    bool reversed = elapsed > remaining;
    float t = (float)((reversed ? remaining : elapsed) / ((double)b->time - a->time));
    t = reversed ? anim_ease(t, b->ease_to, a->ease_from) : anim_ease(t, a->ease_from, b->ease_to);
    anim_quat_t a_in, a_out, b_in, b_out;
    anim_tcb_tangents(keys, c->key_count, low, &a_in, &a_out);
    anim_tcb_tangents(keys, c->key_count, high, &b_in, &b_out);
    if (!reversed) {
        return anim_squad(t, a->rot, a_out, b_in, b->rot);
    }
    anim_quat_t q = anim_squad(t, b->rot, b_in, a_out, a->rot);
    return anim_quat_dot(a->rot, b->rot) < 0.0f ? anim_quat_scale(-1.0, q) : q;
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
    const nmo_objanim_controller_t *tcb_rotation = anim_controller(state, ANIM_TCB_ROTATION, 40);
    const nmo_objanim_controller_t *scaling = anim_controller(state, ANIM_LINEAR_SCALE, 16);
    if (position == NULL && rotation == NULL && tcb_rotation == NULL && scaling == NULL) {
        return true;
    }
    eng_matrix_t local = engine->entities.data[entity].local;
    eng_vector_t pos = {local.m[3][0], local.m[3][1], local.m[3][2]};
    if (position != NULL) {
        anim_linear_vector(position, frame, &pos);
    }
    if (rotation != NULL || tcb_rotation != NULL || scaling != NULL) {
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
        } else if (tcb_rotation != NULL) {
            anim_quat_matrix(anim_tcb_rotation(tcb_rotation, frame), &rot);
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

bool eng_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    nmo_object_t *object = eng_find_object(engine, animation);
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_KEYEDANIMATION) {
        return eng_object_animation_set_step(engine, animation, step);
    }
    const nmo_keyedanimation_state_t *state =
        (const nmo_keyedanimation_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return false;
    }
    step = step > 1.0f ? 1.0f : step < 0.0f ? 0.0f : step;
    for (uint32_t i = 0; i < state->animation_count; i++) {
        eng_object_animation_set_step(engine, eng_object_ref(engine, animation, &state->animation_ids[i]),
                                      step);
    }
    return true;
}
