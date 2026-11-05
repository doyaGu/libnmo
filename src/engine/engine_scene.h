/**
 * @file engine_scene.h
 * @brief Objects in the level: scene membership, initial conditions, dynamic
 *        objects, and copying and deleting objects with their dependencies.
 */

#ifndef NMO_ENGINE_SCENE_H
#define NMO_ENGINE_SCENE_H

#include "engine_array.h"
#include "engine_render.h"

/*
 * An object's initial condition in the level scene (CKScene's object
 * initial value), of what the engine models: an array's columns and cells,
 * a 3D entity's world matrix.
 */
typedef struct eng_ic {
    nmo_object_id_t id;
    bool has_array;
    eng_array_t array;
    bool has_matrix;
    float world[16];
    bool has_parent;
    nmo_object_id_t parent; /* a 3D entity's parent then (0 for none), which CK3dEntity::Load sets */
    bool has_planes;        /* a camera's front and back planes */
    float near_plane, far_plane;
    bool has_material;      /* a material's members */
    nmo_engine_material_t material;
    bool has_light;         /* a light's color and activity */
    float light_color[4];
    bool light_active;
    bool has_sprite3d;
    eng_sprite3d_t sprite3d;
    int32_t visibility; /* CK_OBJECT_VISIBLE and HIERACHICALHIDE, which CKObject::Load restores */
} eng_ic_t;

/* The initial conditions of the level scene's objects */
typedef struct eng_ics {
    ENG_VEC(eng_ic_t) list;
    eng_map_t map; /* object -> index in list */
} eng_ics_t;

/* CKObjectManager's dynamic objects and deferred deletions */
typedef struct eng_dynamic {
    /* The objects created or loaded as dynamic (CK_OBJECTCREATION_DYNAMIC), in that order */
    ENG_VEC(nmo_object_id_t) objects;
    bool delete_all; /* DeleteAllDynamicObjects, done at the end of the frame */
    /* While behaviors run (m_DeferDestroyObjects), destroyed objects wait here for the
       end of the frame (CKObjectManager::PostProcess) */
    bool defer;
    ENG_VEC(nmo_object_id_t) deferred;
} eng_dynamic_t;

/* core/schedule.c */
/* CKLevel::AddObject for every object of document `module` (Object Load) */
void eng_add_module_to_level(nmo_engine_t *engine, int32_t module);
bool eng_in_scene(const nmo_engine_t *engine, nmo_object_id_t id);
void eng_add_to_level_scene(nmo_engine_t *engine, nmo_object_id_t id);

/* core/scene.c */
/* CKScene::SetObjectInitialValue: what the engine models of the object */
void eng_save_ic(nmo_engine_t *engine, nmo_object_id_t id);
/* Decode the object's non-file initial value without changing the document. */
nmo_status_t eng_load_ic(nmo_engine_t *engine, nmo_object_id_t id, struct nmo_chunk *chunk);
/* CKReadObjectState of the object's initial value in the level scene, when it is in it and has one */
void eng_restore_ic(nmo_engine_t *engine, nmo_object_id_t id);
void eng_free_ics(nmo_engine_t *engine);

/* core/module.c */
/* Count `id` among the dynamic objects (CK_OBJECTCREATION_DYNAMIC) */
void eng_mark_dynamic(nmo_engine_t *engine, nmo_object_id_t id);

/* core/copy.c */
#define ENG_DEPENDENCY_CLASSES 128
/* A CKDependencies: CK_DEPENDENCIES_FLAGS, and each class's dependency mask */
typedef struct eng_dependencies {
    uint32_t flags;
    uint32_t classes[ENG_DEPENDENCY_CLASSES];
} eng_dependencies_t;
void eng_default_copy_dependencies(eng_dependencies_t *deps);
void eng_dependencies_from_param(nmo_engine_t *engine, nmo_object_id_t param_id, bool deleting,
                                 eng_dependencies_t *deps);
void eng_destroy_objects(nmo_engine_t *engine, const nmo_object_id_t *ids, int32_t count,
                         const eng_dependencies_t *deps);
nmo_status_t eng_copy_objects(nmo_engine_t *engine, const nmo_object_id_t *ids, int32_t count,
                              const eng_dependencies_t *deps, bool activate, bool dynamic,
                              nmo_object_id_t *out_copies);
/* What a copy takes of its original's run time state */
void eng_copy_attributes(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to);
void eng_copy_array(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to);
void eng_copy_group(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to,
                    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);

#endif /* NMO_ENGINE_SCENE_H */
