/* This editor-only behavior shares keyframe data, never character playback or targets. */
#include "virtools.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

typedef struct hierarchy_pair { int32_t first, second, child, candidate; bool entered; } hierarchy_pair_t;

static size_t child_count(const nmo_engine_t *engine, int32_t entity)
{
    size_t count = 0;
    for (int32_t child = engine->entities.data[entity].first_child; child >= 0; child = engine->entities.data[child].next_sibling) if (++count > engine->entities.count) return SIZE_MAX;
    return count;
}

static bool same_hierarchy(nmo_engine_t *engine, nmo_object_id_t first, nmo_object_id_t second)
{
    int32_t a = eng_entity_index(engine,first), b = eng_entity_index(engine,second);
    if (a < 0 || b < 0) return false;
    ENG_VEC(hierarchy_pair_t) stack = {0}; hierarchy_pair_t *slot = NULL; bool result = false;
    if (!ENG_VEC_PUSH(stack,slot)) return false;
    *slot = (hierarchy_pair_t){.first = a,.second = b};
    while (stack.count != 0) {
        hierarchy_pair_t *pair = &stack.data[stack.count-1]; bool done = false;
        if (!pair->entered) {
            size_t n = child_count(engine,pair->first), m = child_count(engine,pair->second);
            if (n == SIZE_MAX || n != m) { result = false; done = true; }
            else { pair->child = engine->entities.data[pair->first].first_child; pair->candidate = engine->entities.data[pair->second].first_child; pair->entered = true; }
        }
        if (!done && (pair->child < 0 || pair->candidate < 0)) { result = pair->child < 0; done = true; }
        if (done) {
            stack.count--; if (stack.count == 0) break;
            pair = &stack.data[stack.count-1];
            if (result) {
                pair->child = engine->entities.data[pair->child].next_sibling;
                pair->candidate = engine->entities.data[pair->second].first_child; /* Original DLL permits reusing a matching subtree. */
            } else pair->candidate = engine->entities.data[pair->candidate].next_sibling;
        } else {
            hierarchy_pair_t next = {.first = pair->child,.second = pair->candidate};
            if (stack.count >= engine->entities.count || !ENG_VEC_PUSH(stack,slot)) { result = false; break; }
            *slot = next;
        }
    }
    free(stack.data); return result;
}

static nmo_object_id_t body_character(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_entity_index(engine,id) >= 0 ? eng_find_object(engine,id) : NULL;
    const nmo_bodypart_state_t *body = object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_BODYPART) : NULL;
    return body != NULL ? eng_object_ref(engine,id,&body->character) : 0;
}

static int share_edited(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_EDITED) return NMO_ENGINE_BR_OK;
    nmo_engine_t *engine = b->engine; nmo_object_id_t source = nmo_engine_block_target(b);
    if (eng_entity_index(engine,source) < 0 || !eng_is_child_class(engine,nmo_engine_object_class(engine,source),NMO_CID_CHARACTER)) return NMO_ENGINE_BR_OK;
    nmo_object_id_t group = nmo_engine_block_get_input_object(b,0); int32_t count = eng_map_get(&engine->destroyed,group) < 0 ? nmo_engine_group_count(engine,group) : -1;
    if (count < 0) return NMO_ENGINE_BR_OK;
    ENG_VEC(nmo_object_id_t) candidates = {0}; uint32_t shared = 0;
    for (size_t m = 0; m < engine->modules.count; m++) {
        const eng_module_t *module = &engine->modules.data[m]; size_t objects_count = 0; nmo_object_t **objects = nmo_object_repository_get_all(module->repo,&objects_count);
        for (size_t i = 0; i < objects_count; i++) if (nmo_object_get_class_id(objects[i]) == NMO_CID_OBJECTANIMATION) {
            nmo_object_id_t id = eng_module_id(module,nmo_object_get_id(objects[i])), *slot = NULL;
            if (eng_map_get(&engine->destroyed,id) >= 0) continue;
            if (!ENG_VEC_PUSH(candidates,slot)) { free(candidates.data); return NMO_ENGINE_BR_GENERICERROR; }
            *slot = id;
        }
    }
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t character = nmo_engine_group_object(engine,group,i);
        if (character == source || !eng_is_child_class(engine,nmo_engine_object_class(engine,character),NMO_CID_CHARACTER) || !same_hierarchy(engine,character,source)) continue;
        for (size_t j = 0; j < eng_character_animation_count(engine,source); j++) {
            nmo_object_id_t animation = eng_character_animation(engine,source,j);
            if (nmo_engine_object_class(engine,animation) != NMO_CID_KEYEDANIMATION) continue;
            const nmo_keyedanimation_state_t *keyed = nmo_object_get_state(eng_find_object(engine,animation));
            for (uint32_t k = 0; keyed->animation_ids != NULL && k < keyed->animation_count; k++) {
                nmo_object_id_t part = eng_object_ref(engine,animation,&keyed->animation_ids[k]);
                for (size_t n = 0; n < candidates.count; n++) {
                    nmo_object_id_t candidate = candidates.data[n];
                    if (candidate != part && body_character(engine,eng_object_animation_entity(engine,candidate)) == character && eng_object_animation_compare(engine,candidate,part)) {
                        if (!eng_object_animation_share(engine,candidate,part)) { free(candidates.data); return NMO_ENGINE_BR_GENERICERROR; }
                        shared++;
                    }
                }
            }
        }
    }
    free(candidates.data); nmo_engine_block_set_output_value(b,0,&shared,sizeof(shared)); return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_share_animation(nmo_engine_t *engine)
{
    const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x1c0d5e24,0xde47b706),"Share Character Animation Keys",NULL,share_edited,NULL};
    return nmo_engine_register_block(engine,&block);
}
