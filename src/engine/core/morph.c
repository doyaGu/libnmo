/* CKObjectAnimation's morph channel, applied to an engine-owned mesh copy. */
#include "../engine_internal.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
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

static nmo_vector_t morph_normal(const morph_key_t *a, const morph_key_t *b,
                                  uint32_t vertex, int coefficient, bool interpolate)
{
    int pitch = morph_angle(a, vertex, 0), yaw = morph_angle(a, vertex, 1);
    if (interpolate) {
        pitch += morph_fixed(morph_angle(b, vertex, 0) - pitch, coefficient);
        yaw = (yaw + 16384) & 16383;
        int delta = ((morph_angle(b, vertex, 1) + 16384) & 16383) - yaw;
        /* Correct the native positive-wrap defect as in Ballanced's VxMath. */
        if (delta > 8192) delta -= 16384;
        else if (delta < -8192) delta += 16384;
        yaw += morph_fixed(delta, coefficient);
    }
    double x = (pitch & 16383) * (6.28318530717958647692 / 16384);
    double y = (yaw & 16383) * (6.28318530717958647692 / 16384);
    return (nmo_vector_t){(float)(sin(y) * cos(x)), (float)-sin(x), (float)(cos(y) * cos(x))};
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

void eng_animation_morph(nmo_engine_t *engine, const nmo_objectanimation_state_t *state,
                          nmo_object_id_t entity, float frame)
{
    if (!isfinite(frame) || (state->flags & CK_OBJECTANIMATION_IGNOREMORPH)) return;
    const nmo_3dentity_state_t *target = (const nmo_3dentity_state_t *)morph_state(engine, entity, CKPGUID_3DENTITY);
    if (target == NULL || target->skin != NULL) return;
    morph_source_t source;
    if (!morph_source(state, &source)) return;
    /* Validate all times before touching the runtime mesh, even at an endpoint. */
    float previous = -INFINITY;
    uint32_t high = source.info.key_count - 1;
    bool found = false;
    for (uint32_t i = 0; i < source.info.key_count; i++) {
        morph_key_t key = morph_key(&source, i);
        if (!isfinite(key.time) || key.time < previous) return;
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
    if (!positions && !normals) return;
    nmo_object_id_t mesh = eng_object_ref(engine, entity, &target->current_mesh);
    const nmo_mesh_state_t *original = (const nmo_mesh_state_t *)morph_state(engine, mesh, CKPGUID_MESH);
    if (original == NULL || original->vertices == NULL || original->vertex_count == 0 ||
        original->vertex_count > source.info.vertex_count) return;
    nmo_vector_t *rebuilt = positions && !normals ? malloc(original->vertex_count * sizeof(*rebuilt)) : NULL;
    if (positions && !normals && rebuilt == NULL) return;
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, mesh);
    if (copy == NULL) { free(rebuilt); return; }
    int coefficient = (int)(65536.0f * (float)right);
    for (uint32_t v = 0; v < original->vertex_count; v++) {
        if (positions) {
            nmo_vector_t pa, pb;
            memcpy(&pa, (const uint8_t *)a.positions + v * 12u, sizeof(pa));
            memcpy(&pb, (const uint8_t *)b.positions + v * 12u, sizeof(pb));
            copy->vertices[v].position = (nmo_vector_t){(float)(pa.x * left + pb.x * right),
                (float)(pa.y * left + pb.y * right), (float)(pa.z * left + pb.z * right)};
        }
        if (normals) copy->vertices[v].normal = morph_normal(&a, &b, v, coefficient, low != high);
    }
    if (rebuilt != NULL) {
        nmo_mesh_state_t deformed = *original;
        deformed.vertices = copy->vertices;
        if (nmo_mesh_build_vertex_normals(&deformed, rebuilt) == NMO_OK) {
            for (uint32_t v = 0; v < original->vertex_count; v++) copy->vertices[v].normal = rebuilt[v];
        }
    }
    free(rebuilt);
    if (positions) morph_bounds(copy, original->vertex_count);
    copy->version = ++engine->render.mesh_version;
}
