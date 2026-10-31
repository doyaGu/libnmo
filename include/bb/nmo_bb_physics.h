/**
 * @file nmo_bb_physics.h
 * @brief The physics building block pack: physics_RT's blocks and the
 *        physics manager (CKIpionManager) they keep, which a host's
 *        provider simulates.
 */

#ifndef NMO_BB_PHYSICS_H
#define NMO_BB_PHYSICS_H

#include "engine/nmo_engine.h"

#define NMO_BB_PHYSICS_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BB_PHYSICS_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/** Register physics_RT's blocks and add the physics manager to `engine`. */
NMO_API nmo_status_t nmo_bb_physics_register(nmo_engine_t *engine);

/*
 * The physics blocks (physics_RT: Physicalize, Set Physics Hinge, Slider,
 * Spring, Ball Joint and Globals, SetPhysicsForce, Physics Impulse, Physics
 * WakeUp, PhysicsCollDetection, PhysicsContinuousContact, PhysicsBuoyancy,
 * Physics Reset) keep what CKIpionManager keeps: the physical objects with
 * their parameters, the constraints between them, the gravity and the time
 * factor. The pack
 * does not simulate: a host's provider gets these and moves the entities
 * (nmo_engine_set_entity_world_matrix()) each frame; without one, nothing
 * moves. Positions and directions are in world space.
 */

typedef enum nmo_engine_physics_shape {
    NMO_ENGINE_PHYSICS_BALL = 0,    /* a ball of the radius (no surface name, or ball shapes) */
    NMO_ENGINE_PHYSICS_POLYGON = 1  /* the convex and concave meshes, by surface name */
} nmo_engine_physics_shape_t;

typedef struct nmo_engine_physics_object {
    nmo_object_id_t entity;
    nmo_engine_physics_shape_t shape;
    float ball_radius;
    const nmo_object_id_t *convex;  /* the meshes, in Physicalize's order (0 for none) */
    int32_t convex_count;
    const nmo_object_id_t *concave;
    int32_t concave_count;
    const char *collision_surface;  /* the name a polygon shape is shared by */
    const char *collision_group;
    float scale[3];                 /* the entity's local scale, which polygon shapes take */
    bool fixed;
    bool start_frozen;
    bool enable_collision;          /* false for a ball without a surface name, as in CK */
    bool automatic_mass_center;
    float shift_mass_center[3];     /* with automatic_mass_center false */
    float friction;
    float elasticity;
    float mass;
    float linear_damping;
    float rotation_damping;
} nmo_engine_physics_object_t;

typedef enum nmo_engine_physics_constraint_kind {
    NMO_ENGINE_PHYSICS_HINGE = 0,
    NMO_ENGINE_PHYSICS_SLIDER = 1,
    NMO_ENGINE_PHYSICS_SPRING = 2,
    NMO_ENGINE_PHYSICS_BALL_JOINT = 3,
    NMO_ENGINE_PHYSICS_FORCE = 4    /* SetPhysicsForce: a constant force on object1 alone */
} nmo_engine_physics_constraint_kind_t;

typedef struct nmo_engine_physics_constraint {
    nmo_engine_physics_constraint_kind_t kind;
    nmo_object_id_t object1;        /* the block's target */
    nmo_object_id_t object2;
    float anchor[3];                /* hinge, slider, ball joint: the joint; spring: on object1;
                                       force: where it pushes, in object1's own space */
    float anchor2[3];               /* spring: on object2 */
    float axis[3];                  /* hinge: the rotation axis; slider: second - first point;
                                       force: the force */
    bool limited;
    float lower, upper;             /* hinge: radians; slider: along the axis */
    float length, constant, damping, global_damping; /* spring */
} nmo_engine_physics_constraint_t;

/*
 * PhysicsBuoyancy: the object becomes a phantom (no collisions, it only
 * notes the objects in it) whose intruding objects float in a liquid: the
 * plane through the three surface points (IVP_U_Float_Hesse::calc_hesse,
 * normalized), with the current flowing at `current`.
 */
typedef struct nmo_engine_physics_buoyancy {
    nmo_object_id_t object;
    float surface[3][3];      /* three points of the liquid's surface, world space */
    float current[3];         /* the current's velocity (m/s), world space */
    float density;            /* medium density, kg/m^3 */
    float damp_factor;        /* pressure damping */
    float friction_factor;
    float viscosity;
    float torque_factor;      /* "Airplane like factor" */
    float suction_factor;     /* the viscosity's input factor */
} nmo_engine_physics_buoyancy_t;

typedef struct nmo_engine_physics_provider {
    void *user_data;
    /* Each may be NULL. A handle is the provider's; the engine passes it back. */
    nmo_status_t (*create_object)(void *user_data, const nmo_engine_physics_object_t *object,
                                  void **out_handle);
    void (*destroy_object)(void *user_data, void *handle);
    nmo_status_t (*create_constraint)(void *user_data,
                                      const nmo_engine_physics_constraint_t *constraint,
                                      void *object1, void *object2, void **out_handle);
    void (*destroy_constraint)(void *user_data, void *handle);
    /* An impulse at a point (Physics Impulse) */
    void (*push)(void *user_data, void *object, const float position[3], const float impulse[3]);
    void (*wake_up)(void *user_data, void *object);
    void (*set_gravity)(void *user_data, const float gravity[3]);
    /* Set Physics Globals' Clean Physics World and Physics Reset: the environment made
     * again (Physics Reset then destroys every object and constraint handle) */
    void (*reset)(void *user_data);
    /* A step of `seconds` (the frame time smoothed, times the time factor) */
    void (*simulate)(void *user_data, nmo_engine_t *engine, float seconds);
    /* Report the object's collisions and contacts (nmo_engine_physics_collision and
     * nmo_engine_physics_contact) from now on, or no more */
    void (*listen)(void *user_data, void *object, bool listen);
    /* The simulation's time in seconds (IVP_Environment::current_time) */
    double (*time)(void *user_data);
    /* DeleteCollisionSurfaces: drop the shapes kept by collision surface name (nothing is
     * physical then) */
    void (*delete_surfaces)(void *user_data);
    /* PhysicsBuoyancy: the object a phantom, the liquid in it */
    nmo_status_t (*add_buoyancy)(void *user_data, void *object,
                                 const nmo_engine_physics_buoyancy_t *buoyancy);
} nmo_engine_physics_provider_t;

/* A collision of a listened object (IVP_Event_Collision before its impact) */
typedef struct nmo_engine_physics_collision {
    nmo_object_id_t object;  /* the listened object */
    nmo_object_id_t other;
    bool object_first;       /* whether it is the contact's first object */
    float speed[3];          /* the contact's speed: the second object's surface relative to the first */
    float normal[3];         /* the surface normal, from the first object toward the second */
    float point[3];          /* where they touch */
} nmo_engine_physics_collision_t;

/** A provider reports a listened object's collision (PhysicsCollDetection hears it). */
NMO_API void nmo_engine_physics_collision(nmo_engine_t *engine,
                                          const nmo_engine_physics_collision_t *collision);
/**
 * A provider reports a listened object's contact with `other` starting or
 * ending (friction created or deleted; PhysicsContinuousContact hears it).
 */
NMO_API void nmo_engine_physics_contact(nmo_engine_t *engine, nmo_object_id_t object,
                                        nmo_object_id_t other, bool started);

/** Give the engine a physics provider (copied); NULL for none. */
NMO_API void nmo_engine_set_physics_provider(nmo_engine_t *engine,
                                             const nmo_engine_physics_provider_t *provider);
/** Whether `entity` is physical; `out` (optional) its parameters, valid until the next step. */
NMO_API bool nmo_engine_physics_object(const nmo_engine_t *engine, nmo_object_id_t entity,
                                       nmo_engine_physics_object_t *out);
/** The physical objects, in the order they became physical. */
NMO_API int32_t nmo_engine_physics_object_count(const nmo_engine_t *engine);
NMO_API bool nmo_engine_physics_object_at(const nmo_engine_t *engine, int32_t index,
                                          nmo_engine_physics_object_t *out);
/** The constraints the physics blocks made, in order. */
NMO_API int32_t nmo_engine_physics_constraint_count(const nmo_engine_t *engine);
NMO_API bool nmo_engine_physics_constraint(const nmo_engine_t *engine, int32_t index,
                                           nmo_engine_physics_constraint_t *out);
/** The gravity Set Physics Globals set: (0, -9.81, 0) until then. */
NMO_API void nmo_engine_physics_gravity(const nmo_engine_t *engine, float out[3]);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BB_PHYSICS_H */
