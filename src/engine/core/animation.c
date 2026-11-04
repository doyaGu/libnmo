/**
 * @file animation.c
 * @brief CKObjectAnimation::SetStep: an entity's local pose from its
 *        animation's keys (CK2_3D's RCKObjectAnimation, VxMath's VxQuaternion).
 *
 * The engine evaluates the linear and TCB position, rotation and scale
 * controllers, Bezier position and scale, and linear/TCB scale axes.
 * Morph keys update the current mesh's vertices and normals.
 * A keyed animation steps each of its object animations (CKKeyedAnimation::SetStep).
 */

#include "../engine_internal.h"
#include "../engine_animation.h"

#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_class_ids.h"
#include "core/nmo_arena.h"

#include <math.h>
#include <float.h>
#include <string.h>

#define ANIM_LINEAR_POSITION 0x637c4301u
#define ANIM_LINEAR_ROTATION 0x49ed4002u
#define ANIM_LINEAR_SCALE 0x654a3a04u
#define ANIM_TCB_POSITION 0x347e4a01u
#define ANIM_TCB_ROTATION 0x45b52a02u
#define ANIM_TCB_SCALE 0x1b545904u
#define ANIM_BEZIER_POSITION 0x921ab801u
#define ANIM_BEZIER_SCALE 0x18ab4404u
#define ANIM_LINEAR_SCALE_AXIS 0x2f200b08u
#define ANIM_TCB_SCALE_AXIS 0x32595908u

typedef struct anim_quat {
    float x, y, z, w;
} anim_quat_t;

static const nmo_objectanimation_state_t *anim_state(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_find_object(engine, id);
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_OBJECTANIMATION
        ? (const nmo_objectanimation_state_t *)nmo_object_get_state(object) : NULL;
}

static nmo_object_id_t anim_shared_next(const nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_objectanimation_state_t *state = anim_state(engine, id);
    return state != NULL && state->format == CKOBJANIM_FORMAT_SHARED
        ? eng_object_ref(engine, id, &state->shared_anim) : 0;
}

bool eng_animation_source(const nmo_engine_t *engine, nmo_object_id_t animation,
                            eng_animation_source_t *out)
{
    const nmo_objectanimation_state_t *state = anim_state(engine, animation);
    if (state == NULL) return false;
    *out = (eng_animation_source_t){animation, state, state, 0};
    nmo_object_id_t current = animation, slow = animation, fast = animation;
    while (out->keys != NULL && out->keys->format == CKOBJANIM_FORMAT_SHARED) {
        current = anim_shared_next(engine, current);
        out->keys = anim_state(engine, current);
        slow = anim_shared_next(engine, slow);
        fast = anim_shared_next(engine, anim_shared_next(engine, fast));
        if (slow != 0 && slow == fast) { out->keys = NULL; break; }
    }
    /* ShareDataFrom(NULL) allocates empty native keyframe data of length 100. */
    out->length = out->keys != NULL ? (out->keys->has_length ? out->keys->length : 0) : 100;
    return true;
}

bool eng_animation_visit(const eng_animation_path_t *path, nmo_object_id_t id, uint32_t *work)
{
    if (*work == 0 || (path != NULL && path->depth >= 128)) return false;
    --*work;
    for (const eng_animation_path_t *p = path; p != NULL; p = p->parent) {
        if (p->id == id) return false;
    }
    return true;
}

bool eng_animation_map_time(const eng_animation_source_t *parent,
                              const eng_animation_source_t *child, float frame, float *out)
{
    if (!isfinite(frame) || !isfinite(parent->length) || !isfinite(child->length)) return false;
    double time = parent->length == 0 || child->length == 0 ? 0 : (double)frame * child->length / parent->length;
    if (time < -FLT_MAX || time > FLT_MAX) return false;
    *out = (float)time;
    return true;
}

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
static anim_quat_t anim_linear_rotation(const nmo_objanim_controller_t *c, float time, uint32_t stride)
{
    const float *keys = (const float *)c->data;
    uint32_t offset = stride - 4; /* Legacy scale-axis keys have an unused float after time. */
    uint32_t last = c->key_count - 1;
    if (time <= keys[0]) {
        return (anim_quat_t){keys[offset], keys[offset+1], keys[offset+2], keys[offset+3]};
    }
    if (time >= keys[last * stride]) {
        const float *k = &keys[last * stride + offset];
        return (anim_quat_t){k[0], k[1], k[2], k[3]};
    }
    uint32_t low = 0, high = 0;
    anim_segment(keys, stride, c->key_count, time, &low, &high);
    const float *a = &keys[low * stride], *b = &keys[high * stride];
    anim_quat_t q1 = {a[offset], a[offset+1], a[offset+2], a[offset+3]};
    anim_quat_t q2 = {b[offset], b[offset+1], b[offset+2], b[offset+3]};
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
static double anim_ease(double t, float ease_from, float ease_to)
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
        return (scale / from) * t * t;
    }
    if (t < 1.0 - to) {
        return (2.0 * t - from) * scale;
    }
    double remaining = 1.0 - t;
    return 1.0 - (scale / to) * remaining * remaining;
}

/* ComputeTCBTimeWeights: unequal key intervals, adjusted by continuity. */
static void anim_tcb_time_weights(float previous, float time, float next, float continuity,
                                   float *weight_previous, float *weight_next)
{
    double duration = (double)next - previous;
    *weight_previous = (float)(((double)time - previous) * 2.0 / duration);
    *weight_next = (float)(((double)next - time) * 2.0 / duration);
    float magnitude = fabsf(continuity);
    *weight_previous = *weight_previous + magnitude - magnitude * *weight_previous;
    *weight_next = *weight_next + magnitude - magnitude * *weight_next;
}

typedef struct anim_tcb_factors {
    float in_previous, in_next, out_previous, out_next;
} anim_tcb_factors_t;

static anim_tcb_factors_t anim_tcb_factors(float tension, float continuity, float bias,
                                          float weight_previous, float weight_next)
{
    float half_tension = (1.0f - tension) * 0.5f;
    float continuity_minus = 1.0f - continuity, continuity_plus = 2.0f - continuity_minus;
    float bias_minus = 1.0f - bias, bias_plus = 2.0f - bias_minus;
    float minus = half_tension * continuity_minus, plus = half_tension * continuity_plus;
    return (anim_tcb_factors_t){minus * bias_plus * weight_previous,
                                plus * bias_minus * weight_previous,
                                plus * bias_plus * weight_next,
                                minus * bias_minus * weight_next};
}

/* CKTCBPositionKey and CKTCBScaleKey share the same 36-byte layout. */
typedef struct anim_tcb_vector_key {
    float time, value[3];
    float tension, continuity, bias, ease_to, ease_from;
} anim_tcb_vector_key_t;

_Static_assert(sizeof(anim_tcb_vector_key_t) == 36, "TCB vector key layout");

/* Compute only the tangent needed by the current segment, without a cache.
 * The native last endpoint uses its neighbor's incoming tangent. */
static void anim_tcb_vector_tangent(const anim_tcb_vector_key_t *keys, uint32_t count,
                                    uint32_t index, bool outgoing, float tangent[3])
{
    if (count == 2) {
        for (size_t c = 0; c < 3; c++) {
            tangent[c] = (float)((1.0f - keys[index].tension) *
                                 ((double)keys[1].value[c] - keys[0].value[c]));
        }
        return;
    }
    if (index == 0 || index + 1 == count) {
        bool first = index == 0;
        uint32_t neighbor = first ? 1 : index - 1;
        anim_tcb_vector_tangent(keys, count, neighbor, false, tangent);
        float factor = (1.0f - keys[index].tension) * (first ? 0.5f : -0.5f);
        for (size_t c = 0; c < 3; c++) {
            double delta = (double)keys[neighbor].value[c] - keys[index].value[c];
            tangent[c] = (float)((3.0 * delta - tangent[c]) * factor);
        }
        return;
    }
    const anim_tcb_vector_key_t *key = &keys[index];
    float previous, next;
    anim_tcb_time_weights(keys[index - 1].time, key->time, keys[index + 1].time,
                          key->continuity, &previous, &next);
    anim_tcb_factors_t factors = anim_tcb_factors(key->tension, key->continuity, key->bias,
                                                previous, next);
    for (size_t c = 0; c < 3; c++) {
        double left = (double)key->value[c] - keys[index - 1].value[c];
        double right = (double)keys[index + 1].value[c] - key->value[c];
        tangent[c] = (float)(outgoing ? factors.out_previous * left + factors.out_next * right
                                      : factors.in_previous * left + factors.in_next * right);
    }
}

/* RCKTCBPositionController::Evaluate, also used by the scale controller. */
static void anim_tcb_vector(const nmo_objanim_controller_t *controller, float time, eng_vector_t *out)
{
    const anim_tcb_vector_key_t *keys = controller->data;
    uint32_t last = controller->key_count - 1;
    if (time <= keys[0].time || time >= keys[last].time) {
        const float *value = time <= keys[0].time ? keys[0].value : keys[last].value;
        *out = (eng_vector_t){value[0], value[1], value[2]};
        return;
    }
    uint32_t low, high;
    anim_segment(controller->data, 9, controller->key_count, time, &low, &high);
    const anim_tcb_vector_key_t *a = &keys[low], *b = &keys[high];
    double elapsed = (double)time - a->time, remaining = (double)b->time - time;
    bool reversed = elapsed > remaining;
    double t = (reversed ? remaining : elapsed) / ((double)b->time - a->time);
    t = reversed ? anim_ease(t, b->ease_to, a->ease_from) : anim_ease(t, a->ease_from, b->ease_to);
    double squared = t * t;
    double near_position = (2.0 * t - 3.0) * squared + 1.0;
    double far_position = (3.0 - 2.0 * t) * squared;
    double near_tangent = ((t - 2.0) * t + 1.0) * t;
    double far_tangent = (t - 1.0) * squared;
    double h1 = reversed ? far_position : near_position;
    double h2 = reversed ? near_position : far_position;
    double h3 = reversed ? -far_tangent : near_tangent;
    double h4 = reversed ? -near_tangent : far_tangent;
    float outgoing[3], incoming[3], value[3];
    anim_tcb_vector_tangent(keys, controller->key_count, low, true, outgoing);
    anim_tcb_vector_tangent(keys, controller->key_count, high, false, incoming);
    for (size_t c = 0; c < 3; c++) {
        value[c] = (float)(h1 * a->value[c] + h2 * b->value[c] +
                           h3 * outgoing[c] + h4 * incoming[c]);
    }
    *out = (eng_vector_t){value[0], value[1], value[2]};
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
        anim_tcb_time_weights(keys[i - 1].time, key->time, keys[i + 1].time, key->continuity,
                              &weight_previous, &weight_next);
    }
    anim_tcb_factors_t factors = anim_tcb_factors(key->tension, key->continuity, key->bias,
                                                weight_previous, weight_next);
    anim_quat_t a = anim_quat_scale(1.0f - factors.in_previous, previous);
    anim_quat_t b = anim_quat_scale(factors.in_next, next);
    anim_quat_t incoming = {0.5f * (a.x - b.x), 0.5f * (a.y - b.y), 0.5f * (a.z - b.z),
                            0.5f * (a.w - b.w)};
    a = anim_quat_scale(factors.out_previous, previous);
    b = anim_quat_scale(factors.out_next - 1.0f, next);
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
            c->data_size == (uint64_t)c->key_count * key_size) {
            return c;
        }
    }
    return NULL;
}

static const nmo_objanim_controller_t *anim_score_controller(const nmo_objectanimation_state_t *state,
                                                              const uint32_t *types, size_t count)
{
    for (size_t type = 0; type < count; type++) {
        for (uint32_t i = 0; state->controllers != NULL && i < state->controller_count; i++) {
            const nmo_objanim_controller_t *controller = &state->controllers[i];
            size_t bytes;
            if (controller->type == types[type] && controller->key_count > 0 && controller->data != NULL &&
                nmo_objanim_controller_keys_size(controller->type, state->format, controller->data,
                    controller->data_size, controller->key_count, &bytes) && bytes == controller->data_size) return controller;
        }
    }
    return NULL;
}

static float anim_vector_contribution(const nmo_objectanimation_state_t *state, bool scale)
{
    const uint32_t types[] = {scale ? ANIM_LINEAR_SCALE : ANIM_LINEAR_POSITION,
        scale ? ANIM_TCB_SCALE : ANIM_TCB_POSITION, scale ? ANIM_BEZIER_SCALE : ANIM_BEZIER_POSITION};
    const nmo_objanim_controller_t *controller = anim_score_controller(state, types, 3);
    if (controller == NULL) return 0;
    const uint8_t *key = controller->data;
    size_t remaining = controller->data_size;
    bool bezier = controller->type == ANIM_BEZIER_POSITION || controller->type == ANIM_BEZIER_SCALE;
    uint32_t stride = nmo_objanim_controller_format_key_size(controller->type, state->format);
    float first[3], extent = 0;
    memcpy(first, key + sizeof(float), sizeof(first));
    for (uint32_t i = 0; i < controller->key_count; i++) {
        float value[3];
        memcpy(value, key + sizeof(float), sizeof(value));
        double sum = ((double)first[0] - value[0]) + ((double)first[1] - value[1]) + ((double)first[2] - value[2]);
        float difference = (float)fabs(sum);
        if (difference > extent) extent = difference;
        size_t bytes = bezier ? nmo_objanim_bezier_key_decode(key, remaining, NULL) : stride;
        key += bytes;
        remaining -= bytes;
    }
    return extent;
}

static float anim_rotation_contribution(const nmo_objectanimation_state_t *state)
{
    const uint32_t types[] = {ANIM_LINEAR_ROTATION,ANIM_TCB_ROTATION};
    const nmo_objanim_controller_t *controller = anim_score_controller(state, types, 2);
    if (controller == NULL) return 0;
    uint32_t stride = nmo_objanim_controller_format_key_size(controller->type, state->format);
    float extent = 0;
    for (uint32_t i = 0; i < controller->key_count; i++) {
        float a[4];
        memcpy(a, (const uint8_t *)controller->data + (size_t)i*stride + sizeof(float), sizeof(a));
        for (uint32_t j = i + 1; j < controller->key_count; j++) {
            float b[4];
            memcpy(b, (const uint8_t *)controller->data + (size_t)j*stride + sizeof(float), sizeof(b));
            double dot = (double)a[0]*b[0] + (double)a[1]*b[1] + (double)a[2]*b[2] + (double)a[3]*b[3];
            float difference = (float)((1.0 - fabs(dot))*1000.0);
            if (difference > extent) extent = difference;
        }
    }
    return extent;
}

/* CKKeyedAnimation::ComputeAnimationContribution uses raw keys, independently
 * of ignore flags, duration and merge factor. Shared data keeps its own layout. */
static float anim_contribution(nmo_engine_t *engine, nmo_object_id_t animation,
                                const eng_animation_path_t *path, uint32_t *work)
{
    eng_animation_source_t source;
    if (!eng_animation_visit(path, animation, work) || !eng_animation_source(engine, animation, &source)) return 0;
    if (source.state->flags & CK_OBJECTANIMATION_MERGED) {
        eng_animation_path_t current = {animation,path != NULL ? path->depth + 1 : 1,path};
        float a = anim_contribution(engine, eng_object_ref(engine, animation, &source.state->anim1), &current, work);
        float b = anim_contribution(engine, eng_object_ref(engine, animation, &source.state->anim2), &current, work);
        return (float)(((double)a + b)*0.5);
    }
    if (source.keys == NULL) return 0;
    float score = anim_vector_contribution(source.keys, false) + anim_vector_contribution(source.keys, true);
    score += anim_rotation_contribution(source.keys);
    bool morph = source.keys->morph_key_parsed_count > 0 && source.keys->morph_keys != NULL;
    for (uint32_t i = 0; source.keys->controllers != NULL && i < source.keys->controller_count; i++) {
        nmo_objanim_morph_info_t info;
        if (nmo_objanim_morph_controller_info(&source.keys->controllers[i], &info)) morph = true;
    }
    return score + (morph ? 1000 : 0);
}

float eng_animation_contribution(nmo_engine_t *engine, nmo_object_id_t animation)
{
    uint32_t work = ENG_ANIMATION_WORK_LIMIT;
    return anim_contribution(engine, animation, NULL, &work);
}

enum {
    ANIM_BEZIER_SMOOTH = 1, ANIM_BEZIER_LINEAR = 2, ANIM_BEZIER_STEP = 4,
    ANIM_BEZIER_FAST = 8, ANIM_BEZIER_SLOW = 16, ANIM_BEZIER_EXPLICIT = 32,
};

static uint32_t anim_bezier_mode(const nmo_objanim_bezier_key_t *key, size_t side)
{
    return (key->flags >> (side * 16)) & 0xffffu;
}

/* CKBezierPositionController::ComputeKeyDistance, including the wrap interval. */
static double anim_bezier_span(const nmo_objanim_bezier_key_t *keys, float length,
                                uint32_t first, uint32_t second)
{
    return second <= first ? (double)keys[second].time + length - keys[first].time
                            : (double)keys[second].time - keys[first].time;
}

/* ComputeBezierForward: an endpoint derivative or a linear side whose neighbor
 * has another mode. Only the first key's incoming side is also cleared. */
static void anim_bezier_forward(nmo_objanim_bezier_key_t *keys, uint32_t count,
                                 float length, uint32_t index)
{
    nmo_objanim_bezier_key_t *key = &keys[index];
    if (count == 1) {
        memset(key->tangent, 0, sizeof(key->tangent));
        return;
    }
    double span = anim_bezier_span(keys, length, index, index + 1);
    if (span == 0.0) span = 1.0;
    double next_span = 1.0, ratio = 0.0;
    if (index != count - 2) {
        next_span = anim_bezier_span(keys, length, index + 1, index + 2);
        if (next_span == 0.0) next_span = 1.0;
        ratio = span / (span + next_span);
    }
    for (size_t c = 0; c < 3; c++) {
        double delta = (double)keys[index + 1].position[c] - key->position[c];
        double derivative = delta / span;
        if (index != count - 2) {
            double next = (double)keys[index + 2].position[c] - keys[index + 1].position[c];
            derivative = ((ratio + 1.0) / span) * delta - (ratio / next_span) * next;
        }
        key->tangent[1][c] = (float)derivative;
    }
    if (index == 0) memset(key->tangent[0], 0, sizeof(key->tangent[0]));
}

static void anim_bezier_backward(nmo_objanim_bezier_key_t *keys, uint32_t count,
                                  float length, uint32_t index)
{
    nmo_objanim_bezier_key_t *key = &keys[index];
    if (count == 1) {
        memset(key->tangent, 0, sizeof(key->tangent));
        return;
    }
    double span = anim_bezier_span(keys, length, index - 1, index);
    if (span == 0.0) span = 1.0;
    double previous_span = 1.0, ratio = 0.0;
    if (index != 1) {
        previous_span = anim_bezier_span(keys, length, index - 2, index - 1);
        if (previous_span == 0.0) previous_span = 1.0;
        ratio = previous_span / (previous_span + span);
    }
    for (size_t c = 0; c < 3; c++) {
        double delta = (double)key->position[c] - keys[index - 1].position[c];
        double derivative = -delta / span;
        if (index != 1) {
            double previous = (double)keys[index - 1].position[c] - keys[index - 2].position[c];
            derivative = -(((ratio - 1.0) / previous_span) * previous +
                            ((2.0 - ratio) / span) * delta);
        }
        key->tangent[0][c] = (float)derivative;
    }
    if (index == count - 1) memset(key->tangent[1], 0, sizeof(key->tangent[1]));
}

static void anim_bezier_smooth(nmo_objanim_bezier_key_t *keys, uint32_t count,
                                float length, uint32_t index)
{
    nmo_objanim_bezier_key_t *key = &keys[index];
    uint32_t in_mode = anim_bezier_mode(key, 0), out_mode = anim_bezier_mode(key, 1);
    bool smooth_in = in_mode == ANIM_BEZIER_SMOOTH, smooth_out = out_mode == ANIM_BEZIER_SMOOTH;
    uint32_t previous = index == 0 ? count - 1 : index - 1;
    uint32_t next = index == count - 1 ? 0 : index + 1;
    double previous_span = anim_bezier_span(keys, length, previous, index);
    double next_span = anim_bezier_span(keys, length, index, next);
    if (previous_span == 0.0 || next_span == 0.0 || index == 0 || index == count - 1) {
        if (smooth_out) {
            if (index == 0) anim_bezier_forward(keys, count, length, index);
            else memset(key->tangent[1], 0, sizeof(key->tangent[1]));
        }
        if (smooth_in) {
            if (index == count - 1) anim_bezier_backward(keys, count, length, index);
            else memset(key->tangent[0], 0, sizeof(key->tangent[0]));
        }
    } else if (smooth_in && smooth_out) {
        double ratio = previous_span / (previous_span + next_span);
        for (size_t c = 0; c < 3; c++) {
            double before = (double)key->position[c] - keys[previous].position[c];
            double after = (double)keys[next].position[c] - key->position[c];
            double tangent = ((1.0 - ratio) / previous_span) * before + (ratio / next_span) * after;
            key->tangent[0][c] = (float)-tangent;
            key->tangent[1][c] = (float)tangent;
        }
    } else {
        uint32_t mode = smooth_in ? out_mode : in_mode;
        size_t side = smooth_in ? 0 : 1;
        for (size_t c = 0; c < 3; c++) {
            if (mode == ANIM_BEZIER_LINEAR || mode == ANIM_BEZIER_FAST ||
                mode == ANIM_BEZIER_SLOW || mode == ANIM_BEZIER_EXPLICIT) {
                key->tangent[side][c] = -key->tangent[1 - side][c];
            } else if (mode == ANIM_BEZIER_STEP) {
                key->tangent[side][c] = 0;
            }
        }
    }
}

/* ComputeBezierPts dispatches both modes, then smooths that key. Endpoint
 * helpers can change the opposite side, so the order matters for mixed modes. */
static void anim_bezier_tangents(nmo_objanim_bezier_key_t *keys, uint32_t count, float length)
{
    for (uint32_t i = 0; i < count; i++) {
        nmo_objanim_bezier_key_t *key = &keys[i];
        uint32_t previous = i == 0 ? count - 1 : i - 1;
        uint32_t next = i == count - 1 ? 0 : i + 1;
        uint32_t in_mode = anim_bezier_mode(key, 0), out_mode = anim_bezier_mode(key, 1);
        for (size_t side = 0; side < 2; side++) {
            uint32_t mode = side == 0 ? in_mode : out_mode;
            uint32_t neighbor = side == 0 ? previous : next;
            if (mode == ANIM_BEZIER_LINEAR || mode == ANIM_BEZIER_FAST) {
                bool endpoint = side == 0 ? i == 0 : i == count - 1;
                if (mode == ANIM_BEZIER_FAST || endpoint ||
                    anim_bezier_mode(&keys[neighbor], 1 - side) == ANIM_BEZIER_LINEAR) {
                    double span = side == 0 ? anim_bezier_span(keys, length, previous, i)
                                            : anim_bezier_span(keys, length, i, next);
                    if (span == 0.0) span = 1.0;
                    double scale = (mode == ANIM_BEZIER_FAST ? 2.0 : 1.0) / span;
                    for (size_t c = 0; c < 3; c++) {
                        key->tangent[side][c] = (float)(scale *
                            ((double)keys[neighbor].position[c] - key->position[c]));
                    }
                } else if (side == 0) {
                    anim_bezier_backward(keys, count, length, i);
                } else {
                    anim_bezier_forward(keys, count, length, i);
                }
            } else if (mode == ANIM_BEZIER_STEP || mode == ANIM_BEZIER_SLOW) {
                memset(key->tangent[side], 0, sizeof(key->tangent[side]));
            }
        }
        if (in_mode == ANIM_BEZIER_SMOOTH || out_mode == ANIM_BEZIER_SMOOTH) {
            anim_bezier_smooth(keys, count, length, i);
        }
    }
}

/* Decode into temporary keys: tangent computation must not alter the document. */
static bool anim_bezier_vector(const nmo_objanim_controller_t *controller, float length,
                                float time, eng_vector_t *out)
{
    uint32_t count = controller->key_count;
    if (count == 0 || controller->data == NULL || !isfinite(time) ||
        count > controller->data_size / 20u ||
        (uint64_t)count * sizeof(nmo_objanim_bezier_key_t) > SIZE_MAX) {
        return false;
    }
    nmo_objanim_bezier_key_t *keys = malloc((size_t)count * sizeof(*keys));
    if (keys == NULL) return false;
    size_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        size_t size = nmo_objanim_bezier_key_decode((const uint8_t *)controller->data + offset,
                                                    controller->data_size - offset, &keys[i]);
        if (size == 0 || !isfinite(keys[i].time) || (i > 0 && keys[i].time < keys[i - 1].time)) {
            free(keys);
            return false;
        }
        offset += size;
    }
    if (offset != controller->data_size) {
        free(keys);
        return false;
    }
    float value[3];
    if (time <= keys[0].time || time >= keys[count - 1].time) {
        memcpy(value, time <= keys[0].time ? keys[0].position : keys[count - 1].position,
               sizeof(value));
    } else {
        uint32_t low = 0, high = count - 1;
        while (low + 1 < high) {
            uint32_t mid = low + (high - low) / 2;
            if (keys[mid].time < time) low = mid;
            else high = mid;
        }
        const nmo_objanim_bezier_key_t *a = &keys[low], *b = &keys[high];
        if (anim_bezier_mode(a, 1) == ANIM_BEZIER_STEP || anim_bezier_mode(b, 0) == ANIM_BEZIER_STEP) {
            memcpy(value, time == b->time ? b->position : a->position, sizeof(value));
        } else {
            anim_bezier_tangents(keys, count, length);
            double elapsed = (double)time - a->time, remaining = (double)b->time - time;
            bool reversed = elapsed > remaining;
            double duration = (double)b->time - a->time;
            double t = (reversed ? remaining : elapsed) / duration, squared = t * t, omt = 1.0 - t;
            double near_weight = (2.0 * t - 3.0) * squared + 1.0;
            double far_weight = (3.0 - 2.0 * t) * squared;
            double outgoing = 3.0 * (reversed ? omt * squared : omt * omt * t);
            double incoming = 3.0 * (reversed ? omt * omt * t : omt * squared);
            double handle = duration * (double)0.33333331f;
            for (size_t c = 0; c < 3; c++) {
                double derivative = outgoing * a->tangent[1][c] + incoming * b->tangent[0][c];
                value[c] = (float)(near_weight * (reversed ? b : a)->position[c] +
                                   far_weight * (reversed ? a : b)->position[c] + handle * derivative);
            }
        }
    }
    free(keys);
    *out = (eng_vector_t){value[0], value[1], value[2]};
    return true;
}

/* Evaluate one vector channel; a missing or malformed channel preserves its value. */
static bool anim_vector(const nmo_objectanimation_state_t *state, bool scaling,
                         float frame, eng_vector_t *out)
{
    const nmo_objanim_controller_t *controller = anim_controller(
        state, scaling ? ANIM_LINEAR_SCALE : ANIM_LINEAR_POSITION, 16);
    if (controller != NULL) {
        anim_linear_vector(controller, frame, out);
        return true;
    }
    controller = anim_controller(state, scaling ? ANIM_TCB_SCALE : ANIM_TCB_POSITION, 36);
    if (controller != NULL) {
        anim_tcb_vector(controller, frame, out);
        return true;
    }
    uint32_t type = scaling ? ANIM_BEZIER_SCALE : ANIM_BEZIER_POSITION;
    for (uint32_t i = 0; i < state->controller_count; i++) {
        controller = &state->controllers[i];
        if (controller->type == type && anim_bezier_vector(controller,
                state->has_length ? state->length : 0, frame, out)) {
            return true;
        }
    }
    return false;
}

typedef enum anim_channel { ANIM_POSITION, ANIM_ROTATION, ANIM_SCALE, ANIM_AXIS } anim_channel_t;
typedef union anim_value { eng_vector_t vector; anim_quat_t quaternion; } anim_value_t;

static bool anim_evaluate(nmo_engine_t *engine, nmo_object_id_t animation, anim_channel_t channel,
                           float frame, const eng_animation_path_t *path, uint32_t *work, anim_value_t *out);

static bool anim_child(nmo_engine_t *engine, const eng_animation_source_t *parent,
                        const nmo_ref_t *reference, anim_channel_t channel, float frame,
                        const eng_animation_path_t *path, uint32_t *work, anim_value_t *out)
{
    nmo_object_id_t id = eng_object_ref(engine, parent->id, reference);
    eng_animation_source_t child;
    float time;
    return eng_animation_source(engine, id, &child) && eng_animation_map_time(parent, &child, frame, &time) &&
        anim_evaluate(engine, id, channel, time, path, work, out);
}

/* Sample channels without touching either source's entity or playback cursor. */
static bool anim_evaluate(nmo_engine_t *engine, nmo_object_id_t animation, anim_channel_t channel,
                           float frame, const eng_animation_path_t *path, uint32_t *work, anim_value_t *out)
{
    eng_animation_source_t source;
    if (!isfinite(frame) || !eng_animation_visit(path, animation, work) ||
        !eng_animation_source(engine, animation, &source)) return false;
    static const uint32_t ignored[] = {CK_OBJECTANIMATION_IGNOREPOS, CK_OBJECTANIMATION_IGNOREROT,
        CK_OBJECTANIMATION_IGNORESCALE, CK_OBJECTANIMATION_IGNORESCALEROT};
    if (source.state->flags & ignored[channel]) return false;
    if (source.state->flags & CK_OBJECTANIMATION_MERGED) {
        float factor = eng_animation_merge_factor(engine,animation);
        if (!isfinite(source.length) || !isfinite(factor)) return false;
        eng_animation_path_t current = {animation, path != NULL ? path->depth + 1 : 1, path};
        bool endpoint = channel == ANIM_POSITION || channel == ANIM_ROTATION;
        if (endpoint && (factor == 0 || factor == 1)) {
            return anim_child(engine, &source, factor == 0 ? &source.state->anim1 : &source.state->anim2,
                                channel, frame, &current, work, out);
        }
        anim_value_t a, b;
        bool first = anim_child(engine, &source, &source.state->anim1, channel, frame, &current, work, &a);
        bool second = anim_child(engine, &source, &source.state->anim2, channel, frame, &current, work, &b);
        if (!first && !second) return false;
        if (!first || !second) { *out = first ? a : b; return true; }
        if (channel == ANIM_POSITION || channel == ANIM_SCALE) {
            double left = 1.0 - factor;
            out->vector = (eng_vector_t){(float)(a.vector.x * left + b.vector.x * factor),
                (float)(a.vector.y * left + b.vector.y * factor), (float)(a.vector.z * left + b.vector.z * factor)};
        } else {
            out->quaternion = anim_slerp(factor, a.quaternion, b.quaternion);
        }
        return true;
    }
    if (source.keys == NULL) return false;
    if (channel == ANIM_POSITION || channel == ANIM_SCALE) {
        return anim_vector(source.keys, channel == ANIM_SCALE, frame, &out->vector);
    }
    bool axis = channel == ANIM_AXIS;
    uint32_t stride = axis ? nmo_objanim_controller_format_key_size(ANIM_LINEAR_SCALE_AXIS, source.keys->format) : 20;
    const nmo_objanim_controller_t *controller = anim_controller(source.keys,
        axis ? ANIM_LINEAR_SCALE_AXIS : ANIM_LINEAR_ROTATION, stride);
    if (controller != NULL) {
        out->quaternion = anim_linear_rotation(controller, frame, stride / sizeof(float));
        return true;
    }
    controller = anim_controller(source.keys, axis ? ANIM_TCB_SCALE_AXIS : ANIM_TCB_ROTATION, 40);
    if (controller == NULL) return false;
    out->quaternion = anim_tcb_rotation(controller, frame);
    return true;
}

static bool anim_sample(nmo_engine_t *engine, nmo_object_id_t animation, anim_channel_t channel,
                         float frame, anim_value_t *out)
{
    uint32_t work = ENG_ANIMATION_WORK_LIMIT;
    return anim_evaluate(engine, animation, channel, frame, NULL, &work, out);
}

bool eng_animation_position(nmo_engine_t *engine, nmo_object_id_t animation, float frame, nmo_vector_t *out)
{
    anim_value_t value;
    if (!anim_sample(engine, animation, ANIM_POSITION, frame, &value)) return false;
    *out = (nmo_vector_t){value.vector.x,value.vector.y,value.vector.z};
    return true;
}

eng_animation_pose_t eng_animation_sample_pose(nmo_engine_t *engine, nmo_object_id_t animation, float frame)
{
    eng_animation_pose_t pose = {.scale={1,1,1},.rotation={0,0,0,1},.axis={0,0,0,1}};
    if (!isfinite(frame)) return pose;
    anim_value_t value;
    if (anim_sample(engine,animation,ANIM_POSITION,frame,&value)) {
        pose.present |= ENG_ANIM_POSITION;
        pose.position = (nmo_vector_t){value.vector.x,value.vector.y,value.vector.z};
    }
    if (anim_sample(engine,animation,ANIM_SCALE,frame,&value)) {
        pose.present |= ENG_ANIM_SCALE;
        pose.scale = (nmo_vector_t){value.vector.x,value.vector.y,value.vector.z};
    }
    if (anim_sample(engine,animation,ANIM_ROTATION,frame,&value)) {
        pose.present |= ENG_ANIM_ROTATION;
        pose.rotation = (nmo_vector4_t){value.quaternion.x,value.quaternion.y,value.quaternion.z,value.quaternion.w};
    }
    if (anim_sample(engine,animation,ANIM_AXIS,frame,&value)) {
        pose.present |= ENG_ANIM_AXIS;
        pose.axis = (nmo_vector4_t){value.quaternion.x,value.quaternion.y,value.quaternion.z,value.quaternion.w};
    }
    return pose;
}

static nmo_vector_t anim_velocity(nmo_engine_t *engine, nmo_object_id_t animation, float step, float length)
{
    float frame = step*length, other = frame+1 < length ? frame+1 : frame-1;
    float next_step = length != 0 ? other/length : 0;
    nmo_vector_t a = {0}, b = {0};
    eng_animation_position(engine,animation,fminf(fmaxf(step,0),1)*length,&a);
    eng_animation_position(engine,animation,fminf(fmaxf(next_step,0),1)*length,&b);
    float sign = step < next_step ? 0.4f : -0.4f;
    return (nmo_vector_t){sign*(b.x-a.x),sign*(b.y-a.y),sign*(b.z-a.z)};
}

nmo_status_t eng_object_animation_capture_transition(nmo_engine_t *engine, nmo_object_id_t result,
    nmo_object_id_t from, float from_step, nmo_object_id_t to, float to_step, float length,
    bool velocity, bool dont_turn, const eng_animation_pose_t *starting, nmo_objectanimation_state_t *out)
{
    eng_animation_source_t a, b;
    if (anim_state(engine,result) == NULL || !eng_animation_source(engine,from,&a) || !eng_animation_source(engine,to,&b) ||
        !isfinite(length) || length <= 0 || !isfinite(from_step) || !isfinite(to_step) ||
        !isfinite(a.length*from_step) || !isfinite(b.length*to_step)) return NMO_ERR_INVALID_ARGUMENT;
    if (velocity && length >= ENG_TRANSITION_MAX_SAMPLES) return NMO_ERR_NOT_SUPPORTED;
    eng_animation_pose_t first = eng_animation_sample_pose(engine,from,a.length*from_step);
    eng_animation_pose_t last = eng_animation_sample_pose(engine,to,b.length*to_step);
    uint32_t present = first.present & last.present;
    if (starting != NULL) first = *starting;
    if (dont_turn && (present & ENG_ANIM_ROTATION)) {
        anim_value_t initial = {.quaternion={0,0,0,1}}, current = initial;
        anim_sample(engine,from,ANIM_ROTATION,0,&initial);
        anim_sample(engine,from,ANIM_ROTATION,a.length*from_step,&current);
        initial.quaternion.w = -initial.quaternion.w;
        anim_quat_t heading = anim_quat_multiply(current.quaternion,initial.quaternion);
        heading.x = heading.z = 0;
        double magnitude = sqrt((double)heading.y*heading.y+(double)heading.w*heading.w);
        first.rotation = magnitude > 0 ? (nmo_vector4_t){0,(float)(heading.y/magnitude),0,(float)(-heading.w/magnitude)} : (nmo_vector4_t){0,0,0,1};
    }
    nmo_object_id_t entity = eng_object_animation_entity(engine,to);
    nmo_vector_t va = {0}, vb = {0};
    if (velocity && (present & ENG_ANIM_POSITION)) {
        va = anim_velocity(engine,from,from_step,a.length);
        vb = anim_velocity(engine,to,to_step,b.length);
    }
    nmo_arena_t *arena = eng_animation_edit_arena(engine,result);
    if (arena == NULL) return NMO_ERR_NOMEM;
    nmo_objectanimation_state_t *target = nmo_object_get_state(eng_find_object(engine,result));
    nmo_objectanimation_state_t generated = {.base=target->base,.format=CKOBJANIM_FORMAT_CONTROLLERS,
        .flags=target->flags & ~CK_OBJECTANIMATION_MERGED,.has_length=1,.length=length,.merge_factor=0.5f};
    /* A retargeted channel can belong to another module. Its live engine
     * binding is installed only after the complete transition is captured. */
    if (!eng_animation_local_ref(engine,result,entity,&generated.entity)) generated.entity = nmo_ref_from_id(0);
    generated.controllers = nmo_arena_alloc(arena,5*sizeof(*generated.controllers),_Alignof(nmo_objanim_controller_t));
    if (generated.controllers == NULL) return NMO_ERR_NOMEM;
    const uint32_t masks[] = {ENG_ANIM_POSITION,ENG_ANIM_ROTATION,ENG_ANIM_SCALE,ENG_ANIM_AXIS};
    const uint32_t types[] = {ANIM_LINEAR_POSITION,ANIM_LINEAR_ROTATION,ANIM_LINEAR_SCALE,ANIM_LINEAR_SCALE_AXIS};
    for (size_t i = 0; i < 4; i++) {
        if (!(present & masks[i])) continue;
        uint32_t stride = i == 0 || i == 2 ? 4 : 5;
        uint32_t intervals = velocity && i == 0 ? (length < 1 ? 1 : (uint32_t)length) : 1;
        uint32_t count = intervals+1, bytes = count*stride*sizeof(float);
        float *keys = nmo_arena_alloc(arena,bytes,_Alignof(float));
        if (keys == NULL) return NMO_ERR_NOMEM;
        nmo_vector_t displacement = {0};
        for (uint32_t k = 0; k < count; k++) {
            float phase = (float)k/intervals;
            float *key = keys+k*stride;
            key[0] = phase*length;
            if (i == 0 && velocity) {
                key[1] = displacement.x; key[2] = displacement.y; key[3] = displacement.z;
                float dt = fminf(length,1);
                displacement.x += dt*(va.x+phase*(vb.x-va.x));
                displacement.y += dt*(va.y+phase*(vb.y-va.y));
                displacement.z += dt*(va.z+phase*(vb.z-va.z));
            } else if (stride == 4) {
                nmo_vector_t value = i == 0 ? (k == 0 ? first.position : last.position) : (k == 0 ? first.scale : last.scale);
                key[1] = value.x; key[2] = value.y; key[3] = value.z;
            } else {
                nmo_vector4_t value = i == 1 ? (k == 0 ? first.rotation : last.rotation) : (k == 0 ? first.axis : last.axis);
                key[1] = value.x; key[2] = value.y; key[3] = value.z; key[4] = value.w;
            }
        }
        generated.controllers[generated.controller_count++] = (nmo_objanim_controller_t){types[i],count,bytes,keys};
    }
    nmo_status_t status = eng_animation_transition_morph(engine,from,a.length*from_step,to,b.length*to_step,arena,target,&generated);
    if (status != NMO_OK) return status;
    *out = generated;
    return NMO_OK;
}

nmo_status_t eng_object_animation_transition(nmo_engine_t *engine, nmo_object_id_t result,
    nmo_object_id_t from, float from_step, nmo_object_id_t to, float to_step, float length,
    bool velocity, bool dont_turn, const eng_animation_pose_t *starting)
{
    nmo_objectanimation_state_t generated;
    nmo_status_t status = eng_object_animation_capture_transition(engine,result,from,from_step,to,to_step,length,velocity,dont_turn,starting,&generated);
    if (status == NMO_OK && !eng_object_animation_set_entity(engine,result,eng_object_animation_entity(engine,to))) return NMO_ERR_NOMEM;
    if (status == NMO_OK) *(nmo_objectanimation_state_t *)nmo_object_get_state(eng_find_object(engine,result)) = generated;
    return status;
}

void eng_animation_yaw(nmo_engine_t *engine, nmo_object_id_t animation, float frame, eng_matrix_t *out)
{
    anim_value_t first = {.quaternion = {0,0,0,1}}, current = first;
    anim_sample(engine, animation, ANIM_ROTATION, 0, &first);
    anim_sample(engine, animation, ANIM_ROTATION, frame, &current);
    first.quaternion.w = -first.quaternion.w;
    anim_quat_t yaw = anim_quat_multiply(current.quaternion, first.quaternion);
    yaw.x = yaw.z = 0;
    if ((double)yaw.y*yaw.y + (double)yaw.w*yaw.w > 0) anim_quat_matrix(yaw, out);
    else eng_matrix_identity(out);
}

bool eng_object_animation_apply_step(nmo_engine_t *engine, nmo_object_id_t animation, float step, nmo_object_id_t keyed)
{
    eng_animation_source_t source;
    if (!eng_animation_source(engine, animation, &source)) return false;
    int32_t entity = eng_entity_index(engine, eng_object_animation_entity(engine,animation));
    if (entity < 0) {
        return false; /* CKERR_NOTINITIALIZED */
    }
    if (keyed == NMO_OBJECT_ID_INVALID) keyed = 0; /* FORCESETSTEP bypasses IgnoreAnimations only. */
    else if (engine->entities.data[entity].moveable_flags & CK_3DENTITY_IGNOREANIMATION) return true;
    nmo_object_id_t exclusive = engine->entities.data[entity].exclusive_animation;
    if (exclusive != 0 && exclusive != keyed && eng_animation_state(engine, exclusive) != NULL) return true;
    float frame = step * source.length;
    anim_value_t axis, rotation, position, scaling;
    bool has_axis = anim_sample(engine, animation, ANIM_AXIS, frame, &axis);
    bool has_rotation = anim_sample(engine, animation, ANIM_ROTATION, frame, &rotation);
    anim_quat_t scale_axis = has_axis ? axis.quaternion : (anim_quat_t){0, 0, 0, 1};
    bool use_axis = has_axis && (scale_axis.x != 0 || scale_axis.y != 0 || scale_axis.z != 0);
    eng_matrix_t local = engine->entities.data[entity].local;
    eng_vector_t pos = {local.m[3][0], local.m[3][1], local.m[3][2]};
    eng_vector_t scale = {1.0f, 1.0f, 1.0f};
    bool has_position = anim_sample(engine, animation, ANIM_POSITION, frame, &position);
    bool has_scaling = anim_sample(engine, animation, ANIM_SCALE, frame, &scaling);
    if (has_position) pos = position.vector;
    nmo_vector_t offset;
    if (has_position && eng_animation_root_offset(engine, animation, &offset)) {
        pos.x += offset.x;
        pos.y += offset.y;
        pos.z += offset.z;
    }
    if (has_scaling) scale = scaling.vector;
    if (!has_position && !has_scaling && !has_rotation && !has_axis) {
        eng_animation_morph(engine, animation, engine->entities.data[entity].id, frame);
        return true;
    }
    if (has_rotation || has_scaling || has_axis) {
        /* The channels the animation lacks, from the local matrix as it is */
        eng_matrix_t rot;
        eng_matrix_identity(&rot);
        if (has_axis) {
            bool need_rotation = !has_rotation;
            if ((need_rotation || !has_scaling) &&
                !eng_matrix_decompose(&local, use_axis, need_rotation ? &rot : NULL,
                                       has_scaling ? NULL : &scale)) return false;
        } else {
            float *axes[3] = {&scale.x, &scale.y, &scale.z};
            for (int r = 0; r < 3; r++) {
                float length = sqrtf(local.m[r][0] * local.m[r][0] + local.m[r][1] * local.m[r][1] +
                                     local.m[r][2] * local.m[r][2]);
                if (!has_scaling) *axes[r] = length;
                for (int c = 0; c < 3; c++) {
                    rot.m[r][c] = length > 0.0f ? local.m[r][c] / length : (r == c ? 1.0f : 0.0f);
                }
            }
        }
        if (has_rotation) anim_quat_matrix(rotation.quaternion, &rot);
        /* The stretch before the rotation */
        eng_matrix_t stretch;
        eng_matrix_identity(&stretch);
        if (use_axis) {
            eng_matrix_t basis;
            anim_quat_matrix(scale_axis, &basis);
            const float values[3] = {scale.x, scale.y, scale.z};
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
                double sum = 0;
                for (int k = 0; k < 3; k++) sum += (double)basis.m[r][k] * basis.m[c][k] * values[k];
                stretch.m[r][c] = (float)sum;
            }
        } else {
            stretch.m[0][0] = scale.x;
            stretch.m[1][1] = scale.y;
            stretch.m[2][2] = scale.z;
        }
        eng_matrix_multiply(&local, &rot, &stretch);
    }
    local.m[3][0] = pos.x;
    local.m[3][1] = pos.y;
    local.m[3][2] = pos.z;
    eng_entity_set_local_matrix(engine, entity, &local, false);
    eng_animation_morph(engine, animation, engine->entities.data[entity].id, frame);
    return true;
}

bool eng_object_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    return eng_object_animation_apply_step(engine, animation, step, 0);
}

bool eng_object_animation_set_frame(nmo_engine_t *engine, nmo_object_id_t animation, float frame, bool force)
{
    eng_animation_source_t source;
    if (!isfinite(frame) || !eng_animation_source(engine,animation,&source) ||
        !isfinite(source.length) || source.length == 0) return false;
    float step = frame/source.length;
    return isfinite(step) && eng_object_animation_apply_step(engine,animation,step,force ? NMO_OBJECT_ID_INVALID : 0);
}

bool eng_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    nmo_object_t *object = eng_find_object(engine, animation);
    if (object != NULL && nmo_object_get_class_id(object) == NMO_CID_ANIMATION) {
        return eng_animation_set_current_step(engine, animation, step);
    }
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_KEYEDANIMATION) {
        return eng_object_animation_set_step(engine, animation, step);
    }
    return eng_keyed_animation_set_step(engine, animation, step);
}
