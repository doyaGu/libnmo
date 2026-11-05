/* CKKeyframeData has an identity separate from the animation owning or sharing it. */
#include "../engine_internal.h"
#include "object/nmo_class_ids.h"

#include <math.h>

const nmo_objectanimation_state_t *eng_animation_shared_keys(const nmo_engine_t *engine, nmo_object_id_t animation)
{
    int32_t index = eng_map_get(&engine->animations.key_map,animation);
    return index >= 0 ? &engine->animations.keys.data[index].data->state : NULL;
}

static eng_animation_key_data_t *new_data(nmo_engine_t *engine, const nmo_objectanimation_state_t *state)
{
    eng_animation_key_data_t *data = calloc(1,sizeof(*data)), **slot = NULL;
    if (data == NULL) return NULL;
    data->origin = state;
    data->state = state != NULL ? *state : (nmo_objectanimation_state_t){.format = CKOBJANIM_FORMAT_CONTROLLERS,.has_length = 1,.length = 100};
    if (!data->state.has_length) { data->state.has_length = 1; data->state.length = 100; }
    if (!ENG_VEC_PUSH(engine->animations.key_data,slot)) { free(data); return NULL; }
    *slot = data; return data;
}

static bool bind_data(nmo_engine_t *engine, nmo_object_id_t animation, eng_animation_key_data_t *data)
{
    int32_t index = eng_map_get(&engine->animations.key_map,animation);
    if (index < 0) {
        eng_animation_key_binding_t *slot = NULL; index = (int32_t)engine->animations.keys.count;
        if (!ENG_VEC_PUSH(engine->animations.keys,slot)) return false;
        if (!eng_map_put(&engine->animations.key_map,animation,index)) { engine->animations.keys.count--; return false; }
        slot->id = animation;
    }
    engine->animations.keys.data[index].data = data; return true;
}

/* Freeze existing serialized shares before replacing any owner's data. The
 * payloads stay in their module arenas, which outlive logical object deletion. */
static bool capture_data(nmo_engine_t *engine)
{
    for (size_t m = 0; m < engine->modules.count; m++) {
        const eng_module_t *module = &engine->modules.data[m]; size_t count = 0;
        nmo_object_t **objects = nmo_object_repository_get_all(module->repo,&count);
        for (size_t i = 0; i < count; i++) {
            if (nmo_object_get_class_id(objects[i]) != NMO_CID_OBJECTANIMATION) continue;
            nmo_object_id_t local = nmo_object_get_id(objects[i]);
            if (!eng_module_has(module,local)) continue;
            nmo_object_id_t id = eng_module_id(module,local); eng_animation_source_t source;
            if (eng_map_get(&engine->animations.key_map,id) >= 0 || !eng_animation_source(engine,id,&source)) continue;
            eng_animation_key_data_t *data = NULL;
            for (size_t k = 0; source.keys != NULL && k < engine->animations.key_data.count; k++) {
                eng_animation_key_data_t *candidate = engine->animations.key_data.data[k];
                if (candidate->origin == source.keys || &candidate->state == source.keys) { data = candidate; break; }
            }
            if (data == NULL) data = new_data(engine,source.keys);
            if (data == NULL || !bind_data(engine,id,data)) return false;
        }
    }
    return true;
}

bool eng_object_animation_share(nmo_engine_t *engine, nmo_object_id_t target, nmo_object_id_t source)
{
    eng_animation_source_t a, b;
    if (target == source || !eng_animation_source(engine,target,&a) || (source != 0 && !eng_animation_source(engine,source,&b)) || !capture_data(engine)) return false;
    eng_animation_key_data_t *data = source != 0 ? engine->animations.keys.data[eng_map_get(&engine->animations.key_map,source)].data : new_data(engine,NULL);
    return data != NULL && bind_data(engine,target,data);
}

void eng_animation_keys_changed(nmo_engine_t *engine, nmo_object_id_t animation, const nmo_objectanimation_state_t *state)
{
    int32_t index = eng_map_get(&engine->animations.key_map,animation);
    if (index >= 0) engine->animations.keys.data[index].data->state = *state;
}

void eng_copy_animation_keys(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target, bool share)
{
    int32_t index = eng_map_get(&engine->animations.key_map,source);
    if (index < 0) return;
    eng_animation_key_data_t *data = engine->animations.keys.data[index].data;
    if (!share) {
        nmo_arena_t *arena = eng_animation_edit_arena(engine,target);
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(engine->types,NMO_CID_OBJECTANIMATION);
        nmo_objectanimation_state_t copy = {0};
        if (arena == NULL || nmo_objectanimation_vtable.copy(&data->state,&copy,type,arena) != NMO_OK) return;
        data = new_data(engine,NULL); if (data == NULL) return; data->state = copy;
    }
    bind_data(engine,target,data);
}

void eng_free_animation_keys(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->animations.key_data.count; i++) free(engine->animations.key_data.data[i]);
    free(engine->animations.key_data.data); free(engine->animations.keys.data); free(engine->animations.key_map.slots);
}

static const nmo_objanim_controller_t *controller(const nmo_objectanimation_state_t *state, unsigned channel)
{
    const nmo_objanim_controller_t *result = NULL;
    for (uint32_t i = 0; state->controllers != NULL && i < state->controller_count; i++) {
        const nmo_objanim_controller_t *c = &state->controllers[i];
        if ((c->type & 255u) == channel && (nmo_objanim_controller_key_size(c->type) != 0 || nmo_objanim_controller_is_bezier(c->type))) result = c;
    }
    return result;
}

static float scalar(const void *data, size_t offset)
{
    float result; memcpy(&result,(const uint8_t *)data+offset,sizeof(result)); return result;
}

static bool equal_controller(const nmo_objanim_controller_t *a, nmo_objectanimation_format_t af,
    const nmo_objanim_controller_t *b, nmo_objectanimation_format_t bf)
{
    if (b == NULL || a->type != b->type || a->key_count != b->key_count) return false;
    size_t as = 0, bs = 0;
    if (!nmo_objanim_controller_keys_size(a->type,af,a->data,a->data_size,a->key_count,&as) || !nmo_objanim_controller_keys_size(b->type,bf,b->data,b->data_size,b->key_count,&bs)) return false;
    size_t ao = 0, bo = 0;
    for (uint32_t i = 0; i < a->key_count; i++) {
        if (nmo_objanim_controller_is_bezier(a->type)) {
            nmo_objanim_bezier_key_t x, y;
            size_t ax = nmo_objanim_bezier_key_decode((const uint8_t *)a->data+ao,as-ao,&x), by = nmo_objanim_bezier_key_decode((const uint8_t *)b->data+bo,bs-bo,&y);
            if (ax == 0 || by == 0 || x.flags != y.flags || !(fabs((double)x.time-y.time) <= 0)) return false;
            for (int c = 0; c < 3; c++) {
                if (!(fabs((double)x.position[c]-y.position[c]) <= 0)) return false;
                for (int t = 0; t < 2; t++) if (((x.flags >> (16*t)) & 0xffffu) == 0x20u && x.tangent[t][c] != y.tangent[t][c]) return false;
            }
            ao += ax; bo += by;
        } else {
            uint32_t az = nmo_objanim_controller_format_key_size(a->type,af), bz = nmo_objanim_controller_format_key_size(b->type,bf), bytes = nmo_objanim_controller_key_size(a->type);
            bool axis = a->type == 0x2f200b08u;
            for (uint32_t c = 0; c < bytes/4; c++) {
                size_t ax = ao+4*c+(axis && az == 24 && c != 0 ? 4 : 0), by = bo+4*c+(axis && bz == 24 && c != 0 ? 4 : 0);
                float x = scalar(a->data,ax), y = scalar(b->data,by);
                bool metadata = bytes >= 36 && c >= (bytes == 36 ? 4u : 5u);
                if (metadata ? x != y : !(fabs((double)x-y) <= 0)) return false;
            }
            ao += az; bo += bz;
        }
    }
    return true;
}

bool eng_object_animation_compare(const nmo_engine_t *engine, nmo_object_id_t target, nmo_object_id_t source)
{
    eng_animation_source_t a, b;
    if (!eng_animation_source(engine,target,&a) || !eng_animation_source(engine,source,&b)) return false;
    if (target == source) return true;
    float length_a = a.keys != NULL && !a.keys->has_length ? 100 : a.length, length_b = b.keys != NULL && !b.keys->has_length ? 100 : b.length;
    if (length_a != length_b) return false;
    const unsigned channels[] = {1,2,4,8};
    for (size_t i = 0; a.keys != NULL && i < sizeof(channels)/sizeof(channels[0]); i++) {
        const nmo_objanim_controller_t *c = controller(a.keys,channels[i]);
        if (c != NULL && (b.keys == NULL || !equal_controller(c,a.keys->format,controller(b.keys,channels[i]),b.keys->format))) return false;
    }
    return a.keys == NULL || (b.keys != NULL && eng_animation_morph_compare(a.keys,b.keys));
}
