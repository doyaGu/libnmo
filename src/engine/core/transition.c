/* Engine-owned transition objects use the source model's local reference space. */
#include "../engine_internal.h"
#include "object/nmo_class_ids.h"
#include "core/nmo_arena.h"

#include <math.h>

nmo_arena_t *eng_animation_edit_arena(nmo_engine_t *engine, nmo_object_id_t animation)
{
    const eng_module_t *module = eng_module_of(engine, animation, NULL);
    if (module == NULL) return NULL;
    for (size_t i = 0; i < engine->modules.count; i++) {
        if (engine->modules.data[i].repo == module->repo && engine->modules.data[i].session != NULL) {
            return nmo_session_get_arena(engine->modules.data[i].session);
        }
    }
    int32_t index = (int32_t)(module-engine->modules.data);
    return eng_own_module(engine,index) == NMO_OK ? nmo_session_get_arena(engine->modules.data[index].session) : NULL;
}

bool eng_animation_local_ref(const nmo_engine_t *engine, nmo_object_id_t owner, nmo_object_id_t target, nmo_ref_t *out)
{
    if (target == 0) { *out = nmo_ref_from_id(0); return true; }
    nmo_object_id_t local = 0;
    const eng_module_t *from = eng_module_of(engine,owner,NULL), *to = eng_module_of(engine,target,&local);
    if (from == NULL || to == NULL || from->repo != to->repo) return false;
    *out = nmo_ref_from_id(local);
    return true;
}

typedef struct transition_pair {
    nmo_object_id_t from, to;
    eng_animation_pose_t starting;
} transition_pair_t;

static nmo_object_id_t transition_entity(nmo_engine_t *engine, nmo_object_id_t channel)
{
    eng_animation_source_t source;
    return eng_animation_source(engine,channel,&source) ? eng_object_ref(engine,channel,&source.state->entity) : 0;
}

static float transition_error(const eng_animation_pose_t *start, const eng_animation_pose_t *end)
{
    float error = 0;
    if (end->present & ENG_ANIM_POSITION) error += fabsf(end->position.x-start->position.x)+fabsf(end->position.y-start->position.y)+fabsf(end->position.z-start->position.z);
    if (end->present & ENG_ANIM_SCALE) error += fabsf(end->scale.x-start->scale.x)+fabsf(end->scale.y-start->scale.y)+fabsf(end->scale.z-start->scale.z);
    if (end->present & ENG_ANIM_ROTATION) {
        double dot = (double)end->rotation.x*start->rotation.x+(double)end->rotation.y*start->rotation.y+
            (double)end->rotation.z*start->rotation.z+(double)end->rotation.w*start->rotation.w;
        error += (float)(10*(1-fabs(dot)));
    }
    return error;
}

nmo_status_t eng_keyed_animation_transition(nmo_engine_t *engine, nmo_object_id_t result,
    nmo_object_id_t from, nmo_object_id_t to, uint32_t mode, float length, float frame_to, float *entry)
{
    if (from == to || entry == NULL || !isfinite(length) || !isfinite(frame_to) ||
        nmo_engine_object_class(engine,result) != NMO_CID_KEYEDANIMATION ||
        nmo_engine_object_class(engine,from) != NMO_CID_KEYEDANIMATION ||
        nmo_engine_object_class(engine,to) != NMO_CID_KEYEDANIMATION) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *a = eng_animation_state(engine,from), *b = eng_animation_state(engine,to);
    if (a == NULL || b == NULL || eng_animation_state(engine,result) == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_object_id_t character = eng_object_ref(engine,to,&b->character);
    if (eng_object_ref(engine,from,&a->character) != character) return NMO_ERR_INVALID_ARGUMENT;
    float step, out_length = eng_animation_length(b);
    if (!eng_animation_current_step(engine,from,&step) || !isfinite(step) || !isfinite(out_length) || out_length < 0 || !isfinite(b->frame_rate)) return NMO_ERR_VALIDATION_FAILED;
    length = fmaxf(length,1);
    if (mode & CK_TRANSITION_WARPTOSAMEPOS) frame_to = step*out_length;
    if (!isfinite(frame_to)) return NMO_ERR_VALIDATION_FAILED;
    float step_to = out_length != 0 ? frame_to/out_length : 0;
    if (!isfinite(step_to)) return NMO_ERR_VALIDATION_FAILED;
    if ((mode & CK_TRANSITION_USEVELOCITY) && length >= ENG_TRANSITION_MAX_SAMPLES) return NMO_ERR_NOT_SUPPORTED;
    uint32_t in_flags = a->flags, out_flags = b->flags;
    float fps = fabsf(b->frame_rate);
    nmo_object_id_t root = eng_object_ref(engine,to,&b->root_entity);
    nmo_object_id_t root_channel = eng_keyed_animation_root(engine,to);
    if (root_channel != 0) root = transition_entity(engine,root_channel);
    const eng_animation_channel_t *inputs, *outputs;
    size_t in_count, out_count;
    if (!eng_keyed_animation_channels(engine,from,&inputs,&in_count) || !eng_keyed_animation_channels(engine,to,&outputs,&out_count)) return NMO_ERR_NOMEM;
    transition_pair_t *pairs = calloc(out_count != 0 ? out_count : 1,sizeof(*pairs));
    if (pairs == NULL) return NMO_ERR_NOMEM;
    for (size_t i = 0; i < out_count; i++) {
        pairs[i].to = outputs[i].id;
        nmo_object_id_t entity = transition_entity(engine,pairs[i].to);
        if (entity == 0) continue;
        for (size_t j = 0; j < in_count; j++) if (transition_entity(engine,inputs[j].id) == entity) pairs[i].from = inputs[j].id;
        if (pairs[i].from != 0) {
            eng_animation_source_t source;
            eng_animation_source(engine,pairs[i].from,&source);
            pairs[i].starting = eng_animation_sample_pose(engine,pairs[i].from,fminf(fmaxf(step,0),1)*source.length);
        }
    }
    nmo_status_t status = NMO_OK;
    if (mode & CK_TRANSITION_WARPTOBEST) {
        double samples = fmax(floor((double)out_length*.5),1);
        if (samples > ENG_TRANSITION_MAX_SAMPLES) { free(pairs); return NMO_ERR_NOT_SUPPORTED; }
        float best_error = 1e30f, best_frame = 0;
        for (uint32_t frame = 0; frame < (uint32_t)samples; frame++) {
            float error = 0;
            for (size_t i = 0; i < out_count; i++) if (pairs[i].from != 0) {
                eng_animation_pose_t pose = eng_animation_sample_pose(engine,pairs[i].to,(float)frame);
                error += transition_error(&pairs[i].starting,&pose);
            }
            if (error < best_error) { best_error = error; best_frame = (float)frame; }
        }
        step_to = out_length != 0 ? best_frame/out_length : 0;
    }
    nmo_arena_t *arena = eng_animation_edit_arena(engine,result);
    if (arena == NULL) { free(pairs); return NMO_ERR_NOMEM; }
    const nmo_keyedanimation_state_t *old = nmo_object_get_state(eng_find_object(engine,result));
    size_t capacity = old->animation_count > out_count ? old->animation_count : out_count;
    nmo_object_id_t *ids = calloc(capacity != 0 ? capacity : 1,sizeof(*ids));
    nmo_objectanimation_state_t *states = calloc(capacity != 0 ? capacity : 1,sizeof(*states));
    if (ids == NULL || states == NULL) { free(ids); free(states); free(pairs); return NMO_ERR_NOMEM; }
    size_t count = 0, previous_count = 0;
    for (uint32_t i = 0; old->animation_ids != NULL && i < old->animation_count; i++) {
        nmo_object_id_t id = eng_object_ref(engine,result,&old->animation_ids[i]);
        if (nmo_engine_object_class(engine,id) == NMO_CID_OBJECTANIMATION) ids[count++] = id;
    }
    previous_count = count;
    while (status == NMO_OK && count < out_count) {
        ids[count] = eng_create_related_object(engine,result,NMO_CID_OBJECTANIMATION);
        if (ids[count] == 0) status = NMO_ERR_NOMEM;
        else count++;
    }
    nmo_ref_t *refs = count != 0 ? nmo_arena_alloc(arena,count*sizeof(*refs),_Alignof(nmo_ref_t)) : NULL;
    if (count != 0 && refs == NULL) status = NMO_ERR_NOMEM;
    nmo_keyedanimation_state_t generated = *old;
    generated.base.has_root_entity = generated.base.has_character = generated.base.has_length = generated.base.has_data = 1;
    generated.base.length = length; generated.base.frame_rate = fps;
    generated.base.flags = (generated.base.flags & ~(CKANIMATION_LINKTOFRAMERATE|CKANIMATION_ALLOWTURN)) |
        (out_flags & (CKANIMATION_LINKTOFRAMERATE|CKANIMATION_ALLOWTURN)) | CKANIMATION_SUBANIMSSORTED;
    generated.has_merge = 0; generated.merged = 0; generated.subanim_count = 0; generated.subanims = NULL;
    generated.animation_ids = refs; generated.animation_count = (uint32_t)count;
    if (!eng_animation_local_ref(engine,result,root,&generated.base.root_entity) ||
        !eng_animation_local_ref(engine,result,character,&generated.base.character)) status = NMO_ERR_INVALID_ARGUMENT;
    for (size_t i = 0; status == NMO_OK && i < count; i++) {
        if (!eng_animation_local_ref(engine,result,ids[i],&refs[i])) { status = NMO_ERR_INVALID_ARGUMENT; break; }
        const nmo_objectanimation_state_t *state = nmo_object_get_state(eng_find_object(engine,ids[i]));
        states[i] = (nmo_objectanimation_state_t){.base=state->base,.format=CKOBJANIM_FORMAT_CONTROLLERS,.has_length=1,.length=length,.merge_factor=.5f};
    }
    for (size_t i = 0; status == NMO_OK && i < out_count; i++) {
        if (pairs[i].from == 0) continue;
        size_t target = i;
        if (result == from) {
            for (target = 0; target < count && ids[target] != pairs[i].from; target++) {}
            if (target == count) continue;
        }
        bool is_root = transition_entity(engine,pairs[i].to) == root;
        status = eng_object_animation_capture_transition(engine,ids[target],pairs[i].from,step,pairs[i].to,step_to,length,
            is_root && (mode & CK_TRANSITION_USEVELOCITY),is_root && (in_flags & CKANIMATION_ALIGNORIENTATION),
            mode & CK_TRANSITION_WARPTOBEST ? &pairs[i].starting : NULL,&states[target]);
        if (status == NMO_OK) states[target].flags |= CK_OBJECTANIMATION_RESERVED;
    }
    if (status == NMO_OK) {
        for (size_t i = 0; i < count; i++) *(nmo_objectanimation_state_t *)nmo_object_get_state(eng_find_object(engine,ids[i])) = states[i];
        *(nmo_keyedanimation_state_t *)nmo_object_get_state(eng_find_object(engine,result)) = generated;
        eng_keyed_animation_refresh(engine,result);
        *entry = step_to*out_length;
    } else {
        for (size_t i = previous_count; i < count; i++) nmo_engine_destroy_object(engine,ids[i]);
    }
    free(ids); free(states); free(pairs);
    return status;
}
