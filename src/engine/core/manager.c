/**
 * @file manager.c
 * @brief The managers block packs add to an engine, and the hooks the core
 *        calls them through.
 */

#include "../engine_internal.h"

#include <stdlib.h>

nmo_status_t eng_add_manager(nmo_engine_t *engine, const eng_manager_desc_t *desc, void **out_state)
{
    if (engine == NULL || desc == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    void *state = eng_manager_state(engine, desc);
    if (state == NULL) {
        for (size_t i = 0; i < engine->managers.count; i++) {
            if (engine->managers.data[i].desc == desc) {
                if (out_state != NULL) {
                    *out_state = NULL;
                }
                return NMO_OK; /* added before, without state */
            }
        }
        if (desc->state_size > 0) {
            state = calloc(1, desc->state_size);
            if (state == NULL) {
                return NMO_ERR_NOMEM;
            }
        }
        eng_manager_t *manager = NULL;
        if (!ENG_VEC_PUSH(engine->managers, manager)) {
            free(state);
            return NMO_ERR_NOMEM;
        }
        manager->desc = desc;
        manager->state = state;
    }
    if (out_state != NULL) {
        *out_state = state;
    }
    return NMO_OK;
}

void *eng_manager_state(const nmo_engine_t *engine, const eng_manager_desc_t *desc)
{
    for (size_t i = 0; engine != NULL && i < engine->managers.count; i++) {
        if (engine->managers.data[i].desc == desc) {
            return engine->managers.data[i].state;
        }
    }
    return NULL;
}

bool eng_font_coordinates(nmo_engine_t *engine, int32_t font, float out[256][6])
{
    for (size_t i = 0; i < engine->managers.count; i++) {
        const eng_manager_t *manager = &engine->managers.data[i];
        if (manager->desc->font_coordinates != NULL) {
            return manager->desc->font_coordinates(engine, manager->state, font, out);
        }
    }
    return false;
}

void eng_managers_reset(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->managers.count; i++) {
        const eng_manager_t *manager = &engine->managers.data[i];
        if (manager->desc->reset != NULL) manager->desc->reset(engine,manager->state);
    }
}

void eng_managers_pre_process(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->managers.count; i++) {
        const eng_manager_t *manager = &engine->managers.data[i];
        if (manager->desc->pre_process != NULL) {
            manager->desc->pre_process(engine, manager->state);
        }
    }
}

void eng_managers_post_process(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->managers.count; i++) {
        const eng_manager_t *manager = &engine->managers.data[i];
        if (manager->desc->post_process != NULL) {
            manager->desc->post_process(engine, manager->state);
        }
    }
}

void eng_managers_render(nmo_engine_t *engine, eng_render_stage_t stage, const eng_render_args_t *args)
{
    for (size_t i = 0; i < engine->managers.count; i++) {
        const eng_manager_t *manager = &engine->managers.data[i];
        if (manager->desc->render != NULL) {
            manager->desc->render(engine, manager->state, stage, args);
        }
    }
}

void eng_free_managers(nmo_engine_t *engine)
{
    for (size_t i = engine->managers.count; i-- > 0;) {
        const eng_manager_t *manager = &engine->managers.data[i];
        if (manager->desc->destroy != NULL) {
            manager->desc->destroy(engine, manager->state);
        }
        free(manager->state);
    }
    free(engine->managers.data);
    engine->managers.data = NULL;
    engine->managers.count = 0;
}
