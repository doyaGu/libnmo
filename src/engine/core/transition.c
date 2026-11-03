/* Engine-owned transition objects use the source model's local reference space. */
#include "../engine_internal.h"

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
