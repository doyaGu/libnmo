/* CKObjectAnimation's morph channel, applied to an engine-owned mesh copy. */
#include "../engine_internal.h"
#include "../engine_animation.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "core/nmo_arena.h"

#include <math.h>
#include <limits.h>
#include <string.h>

typedef struct morph_source {
    const nmo_objectanimation_state_t *state;
    const nmo_objanim_controller_t *controller;
    nmo_objanim_morph_info_t info;
} morph_source_t;

typedef struct morph_key {
    float time;
    const void *positions;
    const uint8_t *normals;
    uint32_t normal_bytes;
    bool old_normals;
} morph_key_t;

static const void *morph_state(const nmo_engine_t *engine, nmo_object_id_t id, nmo_guid_t type)
{
    nmo_object_t *object = eng_find_object(engine, id);
    return object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, type) : NULL;
}

static bool morph_payload(const void *data, uint32_t bytes, uint32_t count, uint32_t stride)
{
    return bytes == 0 || (data != NULL && bytes % stride == 0 && (uint64_t)count * stride <= bytes);
}

static bool morph_source(const nmo_objectanimation_state_t *state, morph_source_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = state;
    for (uint32_t i = 0; state->controllers != NULL && i < state->controller_count; i++) {
        const nmo_objanim_controller_t *controller = &state->controllers[i];
        if (controller->type == NMO_OBJANIM_CONTROLLER_MORPH) {
            out->controller = controller;
            return nmo_objanim_morph_controller_info(controller, &out->info) && out->info.key_count > 0;
        }
    }
    if (state->morph_vertex_count < 0 || state->morph_key_count <= 0 || state->morph_keys == NULL ||
        state->morph_key_parsed_count != (uint32_t)state->morph_key_count) return false;
    out->info.key_count = state->morph_key_parsed_count;
    out->info.vertex_count = (uint32_t)state->morph_vertex_count;
    if ((state->morph_comp_count != 0 && (state->morph_comp_count != out->info.key_count ||
        state->morph_comp_data == NULL || state->morph_comp_sizes == NULL)) ||
        (state->morph_normals_count != 0 && (state->morph_normals_count != out->info.key_count ||
        state->morph_normals_data == NULL || state->morph_normals_sizes == NULL))) return false;
    for (uint32_t i = 0; i < out->info.key_count; i++) {
        if (!morph_payload(state->morph_keys[i].data, state->morph_keys[i].data_size,
                            out->info.vertex_count, 12)) return false;
        if (state->morph_comp_count && !morph_payload(state->morph_comp_data[i], state->morph_comp_sizes[i],
                                                     out->info.vertex_count, 4)) return false;
        if (state->morph_normals_count && !morph_payload(state->morph_normals_data[i], state->morph_normals_sizes[i],
                                                        out->info.vertex_count, 4)) return false;
    }
    return true;
}

static morph_key_t morph_key(const morph_source_t *source, uint32_t index)
{
    morph_key_t key = {0};
    if (source->controller != NULL) {
        const float *positions = NULL;
        nmo_objanim_morph_controller_key(source->controller, &source->info, index,
                                         &key.time, &positions, &key.normals);
        key.positions = positions;
        key.normal_bytes = source->info.has_normals ? source->info.vertex_count * 4u : 0;
    } else {
        const nmo_objectanimation_state_t *state = source->state;
        key.time = state->morph_keys[index].time_step;
        key.positions = state->morph_keys[index].data_size ? state->morph_keys[index].data : NULL;
        if (state->morph_normals_count) {
            key.normals = state->morph_normals_sizes[index] ? state->morph_normals_data[index] : NULL;
            key.normal_bytes = state->morph_normals_sizes[index];
            key.old_normals = true;
        } else if (state->morph_comp_count) {
            key.normals = state->morph_comp_sizes[index] ? state->morph_comp_data[index] : NULL;
            key.normal_bytes = state->morph_comp_sizes[index];
        }
    }
    return key;
}

static int morph_angle(const morph_key_t *key, uint32_t vertex, int component)
{
    size_t offset = ((size_t)vertex * 2u + (uint32_t)component) * (key->old_normals ? 4u : 2u);
    uint16_t bits = 0;
    /* Legacy conversion reads into zero-initialized two-DWORD entries. */
    if (offset + sizeof(bits) <= key->normal_bytes) memcpy(&bits, key->normals + offset, sizeof(bits));
    return bits < 32768 ? (int)bits : (int)bits - 65536;
}

/* Arithmetic fixed-point shift, without implementation-defined signed shifts. */
static int morph_fixed(int delta, int coefficient)
{
    int64_t product = (int64_t)delta * coefficient;
    return (int)(product >= 0 ? product / 65536 : -((-product + 65535) / 65536));
}

static uint32_t morph_pack(int pitch, int yaw)
{
    return (uint32_t)(uint16_t)pitch | ((uint32_t)(uint16_t)yaw << 16);
}

static uint32_t morph_normal_word(const morph_key_t *key, uint32_t vertex)
{
    return morph_pack(morph_angle(key, vertex, 0), morph_angle(key, vertex, 1));
}

static bool morph_compare_source(const nmo_objectanimation_state_t *state, morph_source_t *out, bool *present)
{
    *out = (morph_source_t){.state = state}; *present = state->has_morph_counts != 0;
    for (uint32_t i = 0; state->controllers != NULL && i < state->controller_count; i++) if (state->controllers[i].type == NMO_OBJANIM_CONTROLLER_MORPH) out->controller = &state->controllers[i];
    if (out->controller != NULL) { *present = true; return nmo_objanim_morph_controller_info(out->controller,&out->info); }
    if (!*present) return true;
    if (state->morph_vertex_count < 0 || state->morph_key_count < 0) return false;
    if (state->morph_key_count == 0) { out->info.vertex_count = (uint32_t)state->morph_vertex_count; return true; }
    return morph_source(state,out);
}

bool eng_animation_morph_compare(const nmo_objectanimation_state_t *target, const nmo_objectanimation_state_t *source)
{
    morph_source_t a, b; bool present, other;
    if (!morph_compare_source(target,&a,&present)) return false;
    if (!present) return true;
    if (!morph_compare_source(source,&b,&other) || !other || a.info.key_count != b.info.key_count || a.info.vertex_count != b.info.vertex_count) return false;
    for (uint32_t i = 0; i < a.info.key_count; i++) {
        morph_key_t x = morph_key(&a,i), y = morph_key(&b,i);
        if (fabs((double)x.time-y.time) > 0 || (x.positions != NULL && y.positions == NULL) || (x.normals != NULL && y.normals == NULL)) return false;
        for (uint32_t v = 0; v < a.info.vertex_count; v++) {
            if (x.positions != NULL) {
                nmo_vector_t p, q; memcpy(&p,(const uint8_t *)x.positions+(size_t)v*sizeof(p),sizeof(p)); memcpy(&q,(const uint8_t *)y.positions+(size_t)v*sizeof(q),sizeof(q));
                if (p.x != q.x || p.y != q.y || p.z != q.z) return false;
            }
            if (x.normals != NULL && morph_normal_word(&x,v) != morph_normal_word(&y,v)) return false;
        }
    }
    return true;
}

static int morph_signed_angle(uint32_t value)
{
    value &= 65535;
    return value < 32768 ? (int)value : (int)value - 65536;
}

static uint32_t morph_slerp(uint32_t a, uint32_t b, int coefficient)
{
    int pitch = morph_signed_angle(a), yaw = (a >> 16) & 16383;
    pitch += morph_fixed(morph_signed_angle(b) - pitch, coefficient);
    int delta = (int)((b >> 16) & 16383) - yaw;
    /* Correct the native positive-wrap defect as in Ballanced's VxMath. */
    if (delta > 8192) delta -= 16384;
    else if (delta < -8192) delta += 16384;
    yaw += morph_fixed(delta, coefficient);
    return morph_pack(pitch, yaw);
}

static nmo_vector_t morph_normal(uint32_t word)
{
    double x = (word & 16383) * (6.28318530717958647692 / 16384);
    double y = ((word >> 16) & 16383) * (6.28318530717958647692 / 16384);
    return (nmo_vector_t){(float)(sin(y) * cos(x)), (float)-sin(x), (float)(cos(y) * cos(x))};
}

typedef struct morph_vertex { nmo_vector_t position; uint32_t normal; } morph_vertex_t;
typedef struct morph_result { bool success, positions, normals; } morph_result_t;

static bool morph_child(const nmo_engine_t *engine, const eng_animation_source_t *parent,
                          bool second, eng_animation_source_t *out)
{
    return eng_animation_source(engine, eng_animation_merge_source(engine,parent->id,second), out);
}

/* GetMorphVertexCount inspects data even on ignored sources. */
static uint32_t morph_count(const nmo_engine_t *engine, const eng_animation_source_t *animation,
                             const eng_animation_path_t *path, uint32_t *work)
{
    if (!eng_animation_visit(path, animation->id, work)) return 0;
    if (animation->state->flags & CK_OBJECTANIMATION_MERGED) {
        eng_animation_path_t current = {animation->id, path != NULL ? path->depth + 1 : 1, path};
        eng_animation_source_t child;
        uint32_t a = 0, b = 0;
        if (morph_child(engine, animation, false, &child)) a = morph_count(engine, &child, &current, work);
        if (morph_child(engine, animation, true, &child)) b = morph_count(engine, &child, &current, work);
        return a > b ? a : b;
    }
    morph_source_t source;
    if (animation->keys == NULL || !morph_source(animation->keys, &source)) return 0;
    return source.info.vertex_count;
}

static morph_result_t morph_evaluate(const nmo_engine_t *engine, const eng_animation_source_t *animation,
                                      float frame, uint32_t count, morph_vertex_t *out,
                                      const eng_animation_path_t *path, uint32_t *work);

static morph_result_t morph_merge(const nmo_engine_t *engine, const eng_animation_source_t *animation,
                                   float frame, uint32_t count, morph_vertex_t *out,
                                   const eng_animation_path_t *path, uint32_t *work)
{
    const morph_result_t failed = {0};
    float factor = eng_animation_merge_factor(engine,animation->id);
    double coefficient = (double)(65536.0f * factor);
    if (!isfinite(animation->length) || !isfinite(coefficient) || coefficient < INT_MIN || coefficient > INT_MAX) return failed;
    eng_animation_source_t first, second;
    bool has_first = morph_child(engine, animation, false, &first);
    bool has_second = morph_child(engine, animation, true, &second);
    uint32_t a = 0, b = 0;
    if (has_first) a = morph_count(engine, &first, path, work);
    if (has_second) b = morph_count(engine, &second, path, work);
    /* Unequal counts use the first nonempty source, with unscaled frame time. */
    if (a == 0 || a != b) {
        if (a > 0 && count <= a) return morph_evaluate(engine, &first, frame, count, out, path, work);
        if (a == 0 && b > 0 && count <= b) return morph_evaluate(engine, &second, frame, count, out, path, work);
        return failed;
    }
    if (count > a) return failed;
    /* calloc refuses a count whose two buffers overflow size_t */
    morph_vertex_t *buffers = calloc(count, 2 * sizeof(*buffers));
    if (buffers == NULL) return failed;
    morph_vertex_t *left = buffers, *right = buffers + count;
    memcpy(left, out, (size_t)count * sizeof(*out));
    memcpy(right, out, (size_t)count * sizeof(*out));
    float time;
    morph_result_t ra = {0}, rb = {0};
    if (eng_animation_map_time(animation, &first, frame, &time)) ra = morph_evaluate(engine, &first, time, count, left, path, work);
    if (eng_animation_map_time(animation, &second, frame, &time)) rb = morph_evaluate(engine, &second, time, count, right, path, work);
    morph_result_t result = {ra.success || rb.success, ra.positions || rb.positions, ra.normals || rb.normals};
    for (uint32_t v = 0; result.success && v < count; v++) {
        if (ra.success && rb.success) {
            double weight = 1.0 - factor;
            out[v].position = (nmo_vector_t){(float)(left[v].position.x * weight + right[v].position.x * factor),
                (float)(left[v].position.y * weight + right[v].position.y * factor),
                (float)(left[v].position.z * weight + right[v].position.z * factor)};
        } else out[v].position = ra.success ? left[v].position : right[v].position;
        if (ra.normals && rb.normals) out[v].normal = morph_slerp(left[v].normal, right[v].normal, (int)coefficient);
        else if (ra.normals || rb.normals) out[v].normal = ra.normals ? left[v].normal : right[v].normal;
    }
    free(buffers);
    return result;
}

static morph_result_t morph_evaluate(const nmo_engine_t *engine, const eng_animation_source_t *animation,
                                      float frame, uint32_t count, morph_vertex_t *out,
                                      const eng_animation_path_t *path, uint32_t *work)
{
    const morph_result_t failed = {0};
    if (!isfinite(frame) || !eng_animation_visit(path, animation->id, work) ||
        (animation->state->flags & CK_OBJECTANIMATION_IGNOREMORPH)) return failed;
    eng_animation_path_t current = {animation->id, path != NULL ? path->depth + 1 : 1, path};
    if (animation->state->flags & CK_OBJECTANIMATION_MERGED) return morph_merge(engine, animation, frame, count, out, &current, work);
    morph_source_t source;
    if (animation->keys == NULL || !morph_source(animation->keys, &source) || count > source.info.vertex_count) return failed;
    float previous = -INFINITY;
    uint32_t high = source.info.key_count - 1;
    bool found = false;
    for (uint32_t i = 0; i < source.info.key_count; i++) {
        morph_key_t key = morph_key(&source, i);
        if (!isfinite(key.time) || key.time < previous) return failed;
        previous = key.time;
        if (!found && frame <= key.time) high = i, found = true;
    }
    uint32_t low = found && high > 0 ? high - 1 : high;
    morph_key_t a = morph_key(&source, low), b = morph_key(&source, high);
    double interval = (double)b.time - a.time;
    double right = low == high ? 0 : ((double)frame - a.time) / interval;
    double left = low == high ? 1 : ((double)b.time - frame) / interval;
    bool positions = a.positions != NULL && b.positions != NULL;
    bool normals = morph_key(&source, 0).normals != NULL && a.normals != NULL && b.normals != NULL;
    int coefficient = (int)(65536.0f * (float)right);
    for (uint32_t v = 0; v < count; v++) {
        if (positions) {
            nmo_vector_t pa, pb;
            memcpy(&pa, (const uint8_t *)a.positions + v * 12u, sizeof(pa));
            memcpy(&pb, (const uint8_t *)b.positions + v * 12u, sizeof(pb));
            out[v].position = (nmo_vector_t){(float)(pa.x * left + pb.x * right),
                (float)(pa.y * left + pb.y * right), (float)(pa.z * left + pb.z * right)};
        }
        if (normals) {
            uint32_t word = morph_normal_word(&a, v);
            out[v].normal = low == high ? word : morph_slerp(word, morph_normal_word(&b, v), coefficient);
        }
    }
    return (morph_result_t){true, positions, normals};
}

static uint32_t morph_compress(nmo_vector_t normal)
{
    double length = sqrt((double)normal.x*normal.x+(double)normal.y*normal.y+(double)normal.z*normal.z);
    if (!(length > 0)) return 0;
    double unit = 16384.0/6.28318530717958647692;
    int pitch = (int)(-asin(fmax(-1,fmin(1,normal.y/length)))*unit);
    int yaw = (int)(atan2(normal.x,normal.z)*unit);
    return ((uint32_t)pitch & 16383u) | (((uint32_t)yaw & 16383u)<<16);
}

static void morph_retain(const nmo_objectanimation_state_t *previous, nmo_objectanimation_state_t *result)
{
    for (uint32_t i = 0; previous->controllers != NULL && i < previous->controller_count; i++) {
        if (previous->controllers[i].type == NMO_OBJANIM_CONTROLLER_MORPH) {
            result->controllers[result->controller_count++] = previous->controllers[i];
            return;
        }
    }
    result->has_morph_counts = previous->has_morph_counts;
    result->morph_vertex_count = previous->morph_vertex_count;
    result->morph_key_count = previous->morph_key_count;
    result->morph_key_parsed_count = previous->morph_key_parsed_count;
    result->morph_keys = previous->morph_keys;
    result->morph_comp_count = previous->morph_comp_count;
    result->morph_comp_sizes = previous->morph_comp_sizes;
    result->morph_comp_data = previous->morph_comp_data;
    result->morph_normals_count = previous->morph_normals_count;
    result->morph_normals_sizes = previous->morph_normals_sizes;
    result->morph_normals_data = previous->morph_normals_data;
}

nmo_status_t eng_animation_transition_morph(nmo_engine_t *engine, nmo_object_id_t from, float from_frame,
    nmo_object_id_t to, float to_frame, nmo_arena_t *arena,
    const nmo_objectanimation_state_t *previous, nmo_objectanimation_state_t *result)
{
    eng_animation_source_t a, b;
    if (!eng_animation_source(engine,from,&a) || !eng_animation_source(engine,to,&b)) return NMO_ERR_INVALID_ARGUMENT;
    uint32_t work = ENG_ANIMATION_WORK_LIMIT;
    uint32_t count = morph_count(engine,&b,NULL,&work);
    if (count == 0) return NMO_OK;
    work = ENG_ANIMATION_WORK_LIMIT;
    uint32_t in_count = morph_count(engine,&a,NULL,&work);
    if (in_count != 0 && in_count != count) { morph_retain(previous,result); return NMO_OK; }
    if ((uint64_t)count*32+20 > UINT32_MAX) return NMO_ERR_NOMEM;
    morph_vertex_t *samples = calloc((size_t)count*2,sizeof(*samples));
    if (samples == NULL) return NMO_ERR_NOMEM;
    morph_vertex_t *first = samples, *last = samples+count;
    work = ENG_ANIMATION_WORK_LIMIT;
    morph_result_t rb = morph_evaluate(engine,&b,to_frame,count,last,NULL,&work), ra = {0};
    if (in_count != 0) {
        work = ENG_ANIMATION_WORK_LIMIT;
        ra = morph_evaluate(engine,&a,from_frame,count,first,NULL,&work);
    } else {
        nmo_object_id_t entity = eng_object_animation_entity(engine,to);
        const nmo_3dentity_state_t *target = morph_state(engine,entity,CKPGUID_3DENTITY);
        nmo_object_id_t mesh = target != NULL ? eng_object_ref(engine,entity,&target->current_mesh) : 0;
        const nmo_mesh_state_t *original = morph_state(engine,mesh,CKPGUID_MESH);
        if (original == NULL || original->vertices == NULL || original->vertex_count > count) {
            free(samples); morph_retain(previous,result); return NMO_OK;
        }
        const eng_mesh_copy_t *copy = eng_mesh_copy_find(engine,mesh);
        const nmo_vertex_t *vertices = copy != NULL ? copy->vertices : original->vertices;
        for (uint32_t v = 0; v < original->vertex_count; v++) {
            first[v].position = vertices[v].position;
            first[v].normal = morph_compress(vertices[v].normal);
        }
        ra = (morph_result_t){true,true,true};
    }
    if (!ra.success || !rb.success) { free(samples); return NMO_ERR_VALIDATION_FAILED; }
    bool normals = ra.normals && rb.normals;
    uint32_t bytes = 20+count*(normals ? 32u : 24u);
    uint8_t *data = nmo_arena_alloc(arena,bytes,_Alignof(uint32_t));
    if (data == NULL) { free(samples); return NMO_ERR_NOMEM; }
    uint32_t header[] = {2,count,normals};
    memcpy(data,header,sizeof(header));
    uint8_t *cursor = data+sizeof(header);
    for (size_t k = 0; k < 2; k++) {
        float time = k == 0 ? 0 : result->length;
        memcpy(cursor,&time,sizeof(time)); cursor += sizeof(time);
        const morph_vertex_t *key = k == 0 ? first : last;
        for (uint32_t v = 0; v < count; v++) { memcpy(cursor,&key[v].position,12); cursor += 12; }
        if (normals) for (uint32_t v = 0; v < count; v++) { memcpy(cursor,&key[v].normal,4); cursor += 4; }
    }
    result->controllers[result->controller_count++] = (nmo_objanim_controller_t){NMO_OBJANIM_CONTROLLER_MORPH,0,bytes,data};
    free(samples);
    return NMO_OK;
}

static void morph_bounds(eng_mesh_copy_t *copy, uint32_t count)
{
    double sum[3] = {0};
    for (uint32_t v = 0; v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        sum[0] += p->x, sum[1] += p->y, sum[2] += p->z;
    }
    copy->bary_center = (nmo_vector_t){(float)(sum[0] / count), (float)(sum[1] / count), (float)(sum[2] / count)};
    double squared = 0;
    for (uint32_t v = 0; v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        double x = (double)p->x - copy->bary_center.x, y = (double)p->y - copy->bary_center.y;
        double z = (double)p->z - copy->bary_center.z;
        squared = fmax(squared, x*x + y*y + z*z);
    }
    copy->radius = (float)sqrt(squared);
    copy->geometry_changed = true;
}

void eng_animation_morph(nmo_engine_t *engine, nmo_object_id_t animation,
                          nmo_object_id_t entity, float frame)
{
    eng_animation_source_t animation_source;
    if (!eng_animation_source(engine, animation, &animation_source) || !isfinite(frame) ||
        (animation_source.state->flags & CK_OBJECTANIMATION_IGNOREMORPH)) return;
    const nmo_3dentity_state_t *target = (const nmo_3dentity_state_t *)morph_state(engine, entity, CKPGUID_3DENTITY);
    if (target == NULL || target->skin != NULL) return;
    uint32_t work = ENG_ANIMATION_WORK_LIMIT;
    uint32_t count = morph_count(engine, &animation_source, NULL, &work);
    if (count == 0) return;
    nmo_object_id_t mesh = eng_object_ref(engine, entity, &target->current_mesh);
    const nmo_mesh_state_t *original = (const nmo_mesh_state_t *)morph_state(engine, mesh, CKPGUID_MESH);
    if (original == NULL || original->vertices == NULL || original->vertex_count == 0 ||
        original->vertex_count > count) return;
    morph_vertex_t *sample = malloc((size_t)original->vertex_count * sizeof(*sample));
    if (sample == NULL) return;
    const eng_mesh_copy_t *previous = eng_mesh_copy_find(engine, mesh);
    const nmo_vertex_t *vertices = previous != NULL ? previous->vertices : original->vertices;
    for (uint32_t v = 0; v < original->vertex_count; v++) {
        sample[v].position = vertices[v].position;
        sample[v].normal = 0;
    }
    work = ENG_ANIMATION_WORK_LIMIT;
    morph_result_t result = morph_evaluate(engine, &animation_source, frame, original->vertex_count, sample, NULL, &work);
    if (!result.success || (!result.positions && !result.normals)) { free(sample); return; }
    nmo_vector_t *rebuilt = result.positions && !result.normals
        ? malloc((size_t)original->vertex_count * sizeof(*rebuilt)) : NULL;
    if (result.positions && !result.normals && rebuilt == NULL) { free(sample); return; }
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, mesh);
    if (copy == NULL) { free(sample); free(rebuilt); return; }
    for (uint32_t v = 0; v < original->vertex_count; v++) {
        if (result.positions) copy->vertices[v].position = sample[v].position;
        if (result.normals) copy->vertices[v].normal = morph_normal(sample[v].normal);
    }
    if (result.positions && !result.normals) {
        nmo_mesh_state_t deformed = *original;
        deformed.vertices = copy->vertices;
        if (nmo_mesh_build_vertex_normals(&deformed, rebuilt) == NMO_OK) {
            for (uint32_t v = 0; v < original->vertex_count; v++) copy->vertices[v].normal = rebuilt[v];
        }
    }
    free(sample);
    free(rebuilt);
    if (result.positions) morph_bounds(copy, original->vertex_count);
    copy->version = ++engine->render.mesh_version;
}
