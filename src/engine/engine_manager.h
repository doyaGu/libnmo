/**
 * @file engine_manager.h
 * @brief The managers block packs add to an engine (CKBaseManager): the
 *        state each keeps and the points of a frame the engine calls it at.
 *
 * The engine's core never names a pack: a pack adds its managers when it is
 * registered, and the core calls them through these hooks, in the order
 * they were added.
 */

#ifndef NMO_ENGINE_MANAGER_H
#define NMO_ENGINE_MANAGER_H

#include "engine_base.h"

/* Where in nmo_engine_render_frame() a render hook runs */
typedef enum eng_render_stage {
    ENG_RENDER_PREPARE,    /* before the scene's draws are made: meshes a manager changes */
    ENG_RENDER_BACKGROUND, /* after the background 2D entities, before the 3D entities */
    ENG_RENDER_SCENE,      /* after the 3D entities */
    ENG_RENDER_ENTITY_2D,  /* a 2D entity's post render callbacks; `entity` is the entity */
    ENG_RENDER_OVERLAY,    /* after the foreground 2D entities (the post render callbacks) */
    ENG_RENDER_POST,       /* the post sprite render callbacks, last */
    ENG_RENDER_PASSES      /* after PREPARE, before the frame's draws: the render passes to make */
} eng_render_stage_t;

typedef struct eng_render_args {
    const float *view;     /* the camera's matrix */
    float width, height;   /* the render context's size */
    nmo_object_id_t entity; /* ENG_RENDER_ENTITY_2D's entity, else 0 (the root) */
    bool pass;             /* drawing a render pass: the temporary callbacks stay for the frame */
} eng_render_args_t;

typedef struct eng_manager_desc {
    const char *name;
    size_t state_size; /* the zeroed state the engine keeps for it */
    /* The engine is destroyed: free what the state holds (not the state) */
    void (*destroy)(nmo_engine_t *engine, void *state);
    /* Before the behaviors of a frame run (CKBaseManager::PreProcess) */
    void (*pre_process)(nmo_engine_t *engine, void *state);
    /* After the behaviors of a frame ran (CKBaseManager::PostProcess) */
    void (*post_process)(nmo_engine_t *engine, void *state);
    void (*render)(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                   const eng_render_args_t *args);
    /* CKBaseManager::OnCKReset, before the behaviors' reset callbacks. */
    void (*reset)(nmo_engine_t *engine, void *state);
} eng_manager_desc_t;

typedef struct eng_manager {
    const eng_manager_desc_t *desc;
    void *state;
} eng_manager_t;

/* Add a manager once (again: its state as it is); its state in `*out_state` when not NULL */
nmo_status_t eng_add_manager(nmo_engine_t *engine, const eng_manager_desc_t *desc, void **out_state);
/* The state of the manager added with `desc`; NULL when it was not added */
void *eng_manager_state(const nmo_engine_t *engine, const eng_manager_desc_t *desc);

/* core/manager.c: the hooks, for the core */
void eng_managers_pre_process(nmo_engine_t *engine);
void eng_managers_reset(nmo_engine_t *engine);
void eng_managers_post_process(nmo_engine_t *engine);
void eng_managers_render(nmo_engine_t *engine, eng_render_stage_t stage, const eng_render_args_t *args);
/* Destroy the managers, the last added first */
void eng_free_managers(nmo_engine_t *engine);

#endif /* NMO_ENGINE_MANAGER_H */
