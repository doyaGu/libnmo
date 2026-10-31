/**
 * @file engine_physics.h
 * @brief The physics manager (CKIpionManager) the physics blocks keep; the
 *        host's provider simulates.
 */

#ifndef NMO_ENGINE_PHYSICS_H
#define NMO_ENGINE_PHYSICS_H

#include "engine_base.h"

/* A physical object (Physicalize): its parameters and the provider's handle */
typedef struct eng_physics_object {
    nmo_engine_physics_object_t desc; /* its arrays and strings are the fields below */
    nmo_object_id_t *meshes;          /* the convex, then the concave */
    char *surface;
    char *group;
    void *handle;
} eng_physics_object_t;

typedef struct eng_physics_constraint {
    nmo_engine_physics_constraint_t desc;
    void *handle;
    bool alive; /* not removed by Shutdown */
} eng_physics_constraint_t;

/* PhysicsCollDetectionListener: a collision listener of a PhysicsCollDetection */
typedef struct eng_physics_listener {
    int32_t node;
    nmo_object_id_t entity;
    float min_speed, max_speed, sleep;
    int32_t collision_id;
    double last_time; /* of the last collision it reported */
    bool alive;       /* listening: not stopped, its object not unphysicalized */
} eng_physics_listener_t;

/* PhysicsContactData: the contacts of a PhysicsContinuousContact's object, by group */
typedef struct eng_contact_group {
    int32_t number; /* contacts going on */
    bool active;    /* "contact on" given, "contact off" not yet */
} eng_contact_group_t;

typedef struct eng_physics_contacts {
    int32_t node;
    nmo_object_id_t entity;
    float delay_start, delay_end;
    eng_contact_group_t *groups;
    int32_t group_count;
    bool alive;
} eng_physics_contacts_t;

/* PhysicsContactRecord: a group's contact starting or ending, until its delay passes */
typedef struct eng_contact_record {
    int32_t contacts; /* index into the contacts */
    int32_t id;
    double time;
} eng_contact_record_t;

/* A call kept until its objects are physical: a constraint kind, or -1 for a wake-up */
typedef struct eng_physics_call {
    int kind;
    int32_t node;
} eng_physics_call_t;

/* CKIpionManager */
typedef struct eng_physics {
    ENG_VEC(eng_physics_object_t) objects;
    ENG_VEC(eng_physics_constraint_t) constraints;
    ENG_VEC(eng_physics_call_t) calls;
    ENG_VEC(char *) surfaces; /* the polygon shapes made, by name */
    ENG_VEC(eng_physics_listener_t) listeners;
    ENG_VEC(eng_physics_contacts_t) contacts;
    ENG_VEC(eng_contact_record_t) records; /* PhysicsContactManager::m_Data */
    int32_t group_output_count;            /* PhysicsContactManager::m_NumberGroupOutput */
    double time;                           /* the steps' total, for a provider without a clock */
    nmo_engine_physics_provider_t provider;
    bool has_provider;
    float gravity[3];
    double time_factor;   /* m_PhysicsTimeFactor: the step per ms of frame */
    double delta;         /* m_DeltaTime: the frame time, smoothed */
    double physics_delta; /* m_PhysicsDeltaTime: the last step */
} eng_physics_t;

/* blocks/physics.c */
void eng_physics_init(nmo_engine_t *engine);
void eng_free_physics(nmo_engine_t *engine);
void eng_physics_post_process(nmo_engine_t *engine);

#endif /* NMO_ENGINE_PHYSICS_H */
