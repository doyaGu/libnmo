/* CKKeyedAnimation blends pair channels by their live entity bindings. */
#include "../engine_internal.h"
#include "object/nmo_class_ids.h"
#include "core/nmo_arena.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool merge_name(nmo_engine_t *engine, nmo_object_id_t id, const char *name)
{
    nmo_object_id_t local = 0;
    const eng_module_t *module = eng_module_of(engine,id,&local);
    return module != NULL && nmo_object_repository_rename(module->repo,local,name) == NMO_OK;
}

static void merge_ref(nmo_engine_t *engine, nmo_object_id_t owner, nmo_object_id_t target, nmo_ref_t *out)
{
    if (!eng_animation_local_ref(engine,owner,target,out)) *out = nmo_ref_from_id(0);
}

/* Union-only channels clone their keys and stretch key times, leaving both
 * source data and the new duration independent. Packed Bezier and morph keys
 * have variable records, so validate their layouts before visiting times. */
static void merge_stretch(nmo_objectanimation_state_t *state, float source_length, float length)
{
    if (source_length <= 0) return;
    float ratio = length/source_length;
    double factor = isfinite(ratio) && ratio != 0 ? ratio : (double)length/source_length;
    if (!(factor > 0) || factor == 1) return;
    for (uint32_t i = 0; state->controllers != NULL && i < state->controller_count; i++) {
        nmo_objanim_controller_t *controller = &state->controllers[i];
        nmo_objanim_morph_info_t morph;
        if (nmo_objanim_morph_controller_info(controller,&morph)) {
            size_t stride = sizeof(float)+(size_t)morph.vertex_count*(morph.has_normals ? 16 : 12);
            for (uint32_t k = 0; k < morph.key_count; k++) {
                uint8_t *key = (uint8_t *)controller->data+12+(size_t)k*stride;
                float time; memcpy(&time,key,sizeof(time)); time = (float)(time*factor); memcpy(key,&time,sizeof(time));
            }
            continue;
        }
        size_t bytes = 0;
        if (!nmo_objanim_controller_keys_size(controller->type,state->format,controller->data,
            controller->data_size,controller->key_count,&bytes)) continue;
        size_t offset = 0;
        for (uint32_t k = 0; k < controller->key_count; k++) {
            uint8_t *key = (uint8_t *)controller->data+offset;
            size_t stride = nmo_objanim_controller_is_bezier(controller->type)
                ? nmo_objanim_bezier_key_decode(key,bytes-offset,NULL)
                : nmo_objanim_controller_format_key_size(controller->type,state->format);
            if (stride < sizeof(float) || stride > bytes-offset) break;
            float time; memcpy(&time,key,sizeof(time)); time = (float)(time*factor); memcpy(key,&time,sizeof(time));
            offset += stride;
        }
    }
    for (uint32_t i = 0; state->morph_keys != NULL && i < state->morph_key_parsed_count; i++)
        state->morph_keys[i].time_step = (float)(state->morph_keys[i].time_step*factor);
}

static nmo_object_id_t merge_channel(nmo_engine_t *engine, nmo_object_id_t parent,
    nmo_object_id_t first, nmo_object_id_t second, float length)
{
    eng_animation_source_t source;
    if (!eng_animation_source(engine,first,&source)) return 0;
    nmo_object_id_t entity = eng_object_animation_entity(engine,first);
    nmo_object_id_t child = eng_create_related_object(engine,parent,NMO_CID_OBJECTANIMATION);
    if (child == 0) return 0;
    nmo_arena_t *arena = eng_animation_edit_arena(engine,child);
    nmo_objectanimation_state_t *state = nmo_object_get_state(eng_find_object(engine,child));
    nmo_objectanimation_state_t generated = *state;
    if (arena == NULL || !merge_name(engine,child,nmo_engine_object_name(engine,first))) goto failed;
    if (second == 0 && source.keys != NULL) {
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(engine->types,NMO_CID_OBJECTANIMATION);
        if (nmo_objectanimation_vtable.copy(source.keys,&generated,type,arena) != NMO_OK) goto failed;
        generated.base = state->base;
        generated.flags = source.state->flags;
        generated.merge_factor = eng_animation_merge_factor(engine,first);
        generated.has_merge = source.state->has_merge;
        merge_stretch(&generated,source.length,length);
    } else if (second != 0) {
        generated.flags = CK_OBJECTANIMATION_MERGED;
        generated.has_merge = 1; generated.merge_factor = .5f;
    }
    generated.format = second != 0 || source.keys == NULL ? CKOBJANIM_FORMAT_CONTROLLERS : generated.format;
    generated.has_length = 1; generated.length = length;
    generated.has_shared_anim = 0; generated.shared_anim = nmo_ref_from_id(0);
    generated.has_root_pos = 0; generated.root_pos = (nmo_vector_t){0};
    nmo_object_id_t a = second != 0 ? first : eng_animation_merge_source(engine,first,false);
    nmo_object_id_t b = second != 0 ? second : eng_animation_merge_source(engine,first,true);
    merge_ref(engine,child,a,&generated.anim1); merge_ref(engine,child,b,&generated.anim2);
    merge_ref(engine,child,entity,&generated.entity);
    *state = generated;
    if (!eng_animation_set_merge_sources(engine,child,a,b) || !eng_object_animation_set_entity(engine,child,entity)) goto failed;
    return child;
failed:
    nmo_engine_destroy_object(engine,child);
    return 0;
}

static nmo_object_id_t merge_part(const nmo_engine_t *engine, nmo_object_id_t id,
    const nmo_keyedanimation_state_t *state, uint32_t index)
{
    int32_t runtime = eng_map_get(&engine->animations.keyed_map,id);
    if (runtime >= 0 && engine->animations.keyed.data[runtime].sorted) {
        const eng_keyed_animation_t *keyed = &engine->animations.keyed.data[runtime];
        return index < keyed->order.count ? keyed->order.data[index].id : 0;
    }
    return state->animation_ids != NULL ? eng_object_ref(engine,id,&state->animation_ids[index]) : 0;
}

nmo_object_id_t eng_keyed_animation_merge(nmo_engine_t *engine, nmo_object_id_t first,
    nmo_object_id_t second, bool intersection)
{
    if (eng_animation_state(engine,first) == NULL || eng_animation_state(engine,second) == NULL ||
        nmo_engine_object_class(engine,first) != NMO_CID_KEYEDANIMATION ||
        nmo_engine_object_class(engine,second) != NMO_CID_KEYEDANIMATION) return 0;
    nmo_object_id_t character = eng_animation_character(engine,first);
    if (character == 0) return 0;
    const nmo_keyedanimation_state_t *left = nmo_object_get_state(eng_find_object(engine,first));
    const nmo_keyedanimation_state_t *right = nmo_object_get_state(eng_find_object(engine,second));
    float left_length = eng_animation_length(&left->base), right_length = eng_animation_length(&right->base);
    float length = right_length >= left_length ? right_length : left_length;
    if (!isfinite(length)) return 0;
    size_t capacity = (size_t)left->animation_count+(intersection ? 0 : (size_t)right->animation_count);
    if (capacity > UINT32_MAX || capacity > SIZE_MAX/sizeof(nmo_ref_t)) return 0;
    nmo_object_id_t result = eng_create_related_object(engine,first,NMO_CID_KEYEDANIMATION);
    if (result == 0) return 0;
    nmo_arena_t *arena = eng_animation_edit_arena(engine,result);
    nmo_ref_t *refs = arena != NULL && capacity != 0 ? nmo_arena_alloc(arena,capacity*sizeof(*refs),_Alignof(nmo_ref_t)) : NULL;
    size_t count = 0;
    if (arena == NULL || (capacity != 0 && refs == NULL)) goto failed;
    const char *a = nmo_engine_object_name(engine,first), *b = nmo_engine_object_name(engine,second);
    int size = snprintf(NULL,0,"%s+%s",a != NULL ? a : "",b != NULL ? b : "");
    char *name = size >= 0 ? nmo_arena_alloc(arena,(size_t)size+1,1) : NULL;
    if (name == NULL) goto failed;
    snprintf(name,(size_t)size+1,"%s+%s",a != NULL ? a : "",b != NULL ? b : "");
    if (!merge_name(engine,result,name)) goto failed;
    for (int side = 0; side < (intersection ? 1 : 2); side++) {
        const nmo_keyedanimation_state_t *source = side ? right : left;
        nmo_object_id_t source_id = side ? second : first, opposite = side ? first : second;
        for (uint32_t i = 0; source->animation_ids != NULL && i < source->animation_count; i++) {
            nmo_object_id_t part = merge_part(engine,source_id,source,i);
            nmo_object_id_t entity = eng_object_animation_entity(engine,part);
            if (entity == 0) continue;
            nmo_object_id_t match = eng_keyed_animation_part(engine,opposite,entity);
            if ((side && match != 0) || (match == 0 && intersection)) continue;
            nmo_object_id_t child = merge_channel(engine,result,part,match,length);
            if (child == 0) goto failed;
            /* Every result channel is allocated in this result's repository. */
            if (!eng_animation_local_ref(engine,result,child,&refs[count])) { nmo_engine_destroy_object(engine,child); goto failed; }
            count++;
        }
    }
    nmo_keyedanimation_state_t *state = nmo_object_get_state(eng_find_object(engine,result));
    state->base.has_data = state->base.has_length = state->base.has_character = 1;
    state->base.length = length; state->has_merge = 1; state->merged = 1; state->merge_factor = .5f;
    state->animation_count = (uint32_t)count; state->animation_ids = refs;
    merge_ref(engine,result,character,&state->base.character);
    if (!eng_animation_set_character(engine,result,character)) goto failed;
    eng_keyed_animation_refresh(engine,result);
    nmo_object_id_t root = 0;
    if (!eng_keyed_animation_update_root(engine,result,0,&root)) goto failed;
    state->base.has_root_entity = 1; merge_ref(engine,result,root,&state->base.root_entity);
    eng_add_to_level_scene(engine,result);
    return result;
failed:
    for (size_t i = 0; i < count; i++) nmo_engine_destroy_object(engine,eng_object_ref(engine,result,&refs[i]));
    nmo_engine_destroy_object(engine,result);
    return 0;
}
