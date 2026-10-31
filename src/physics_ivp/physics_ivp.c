/**
 * @file physics_ivp.c
 * @brief The engine's physics provider on libivp, doing what physics_RT's
 *        CKIpionManager does with IVP (the reference is physics_RT.dll).
 *
 * The environment is made at the first object, with CK's gravity
 * (0, -9.81, 0). An object takes its entity's world pose (its matrix read
 * as unit axes, as VxQuaternion::FromMatrix reads it), a simple material,
 * the damping, the collision group as IVP's no-collision ident, and a ball
 * or a polygon shape: each convex mesh's points (duplicates left out) as a
 * convex hull, each concave face as a two sided triangle, scaled by the
 * entity's scale and kept by surface name. Constraints are IVP templates
 * (set_constraint_ws, limited axes), springs IVP springs, a constant force
 * a controller pushing the core each PSI. After each step the moving
 * objects' poses are written back to their entities, without scale. A
 * listened object reports its collisions and friction contacts. A buoyancy
 * makes its object a phantom and floats the cores in it in a liquid.
 */

#include "engine/nmo_physics_ivp.h"

#include <ivp/builder/ivp_build_ledge.h>
#include <ivp/builder/ivp_build_ledge_soup.h>
#include <ivp/builder/ivp_build_surface.h>
#include <ivp/controllers/ivp_actuator_spring.h>
#include <ivp/controllers/ivp_constraint.h>
#include <ivp/controllers/ivp_constraint_types.h>
#include <ivp/controllers/ivp_controller.h>
#include <ivp/controllers/ivp_controller_buoyancy.h>
#include <ivp/controllers/ivp_liquid_surface.h>
#include <ivp/dynamics/ivp_core.h>
#include <ivp/dynamics/ivp_environment.h>
#include <ivp/dynamics/ivp_object.h>
#include <ivp/dynamics/ivp_phantom.h>
#include <ivp/events/ivp_listener_collision.h>
#include <ivp/geometry/ivp_compact_surface.h>
#include <ivp/geometry/ivp_surman.h>
#include <ivp/math/ivp_quat.h>
#include <ivp/math/ivp_transform.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A physical object: the IVP object, its ball surface manager and collision listener */
typedef struct ivp_body {
    struct nmo_physics_ivp *owner;
    ivp_object_t *object; /* NULL once its environment is gone */
    nmo_object_id_t entity;
    ivp_surman_ball_t ball;
    ivp_collision_listener_t listener;
    bool listening;
} ivp_body_t;

/* A polygon shape, kept by its surface name */
typedef struct ivp_shape {
    char *name;
    ivp_compact_surface_t *surface;
    ivp_surman_polygon_t surman;
} ivp_shape_t;

/* A buoyancy's liquid surface, and its attacher until that goes */
typedef struct ivp_liquid {
    ivp_liquid_surface_simple_t surface;
    ivp_buoyancy_attacher_t *attacher;
} ivp_liquid_t;

/* A constraint, spring or force: its IVP state and controller */
typedef struct ivp_joint {
    nmo_engine_physics_constraint_kind_t kind;
    ivp_constraint_state_t constraint;
    ivp_spring_state_t spring;
    /* force: where (core space) and what (world space) it pushes */
    ivp_body_t *body;
    ivp_vec3r_t position_cs;
    ivp_vec3r_t force_ws;
    ivp_controller_handle_t controller;
    bool alive;
} ivp_joint_t;

struct nmo_physics_ivp {
    nmo_engine_t *engine;
    ivp_environment_t *env;
    ivp_vec3r_t gravity;
    ivp_shape_t **shapes;
    size_t shape_count;
    ivp_body_t **bodies;
    size_t body_count;
    ivp_joint_t **joints;
    size_t joint_count;
    /* CKIpionManager::m_LiquidSurfaces: the liquids' surfaces, which their buoyancy reads */
    ivp_liquid_t **liquids;
    size_t liquid_count;
};

static bool push_pointer(void ***items, size_t *count, void *item)
{
    void **grown = (void **)realloc(*items, (*count + 1u) * sizeof(void *));
    if (grown == NULL) {
        return false;
    }
    grown[(*count)++] = item;
    *items = grown;
    return true;
}

/* CKIpionManager::CreateEnvironment */
static ivp_environment_t *environment(nmo_physics_ivp_t *p)
{
    if (p->env == NULL) {
        ivp_environment_config_t config;
        ivp_environment_config_init(&config);
        config.gravity = p->gravity;
        p->env = ivp_environment_create(&config);
        if (p->env != NULL) {
            ivp_environment_set_gravity(p->env, &p->gravity);
        }
    }
    return p->env;
}

/* The entity's world pose as IVP takes it: its position, and its rotation
 * from the transposed matrix with the axes taken as unit (FillTemplateInfo) */
static void entity_pose(nmo_engine_t *engine, nmo_object_id_t entity, ivp_vec3r_t *position,
                        ivp_quatr_t *orientation)
{
    float m[16];
    if (!nmo_engine_entity_world_matrix(engine, entity, m)) {
        *position = ivp_vec3r(0, 0, 0);
        *orientation = ivp_quatr_identity();
        return;
    }
    ivp_mat3r_t rot;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            rot.rows[r].k[c] = m[c * 4 + r];
        }
    }
    ivp_quat_from_mat3(orientation, &rot);
    *position = ivp_vec3r(m[12], m[13], m[14]);
}

/* AddConvexSurface: the mesh's points, each once, scaled, as a convex hull */
static int add_convex(nmo_engine_t *engine, ivp_ledge_soup_t *soup, ivp_compact_ledge_t ***ledges,
                      size_t *ledge_count, nmo_object_id_t mesh, const float scale[3])
{
    int32_t count = nmo_engine_mesh_vertex_count(engine, mesh);
    if (count <= 0) {
        return 0;
    }
    ivp_vec3f_t *points = (ivp_vec3f_t *)calloc((size_t)count, sizeof(ivp_vec3f_t));
    float (*raw)[3] = (float (*)[3])calloc((size_t)count, sizeof(float[3]));
    int n = 0;
    for (int32_t i = 0; points != NULL && raw != NULL && i < count; i++) {
        float p[3];
        if (!nmo_engine_mesh_vertex(engine, mesh, i, p)) {
            continue;
        }
        int k = 0;
        while (k < n && memcmp(raw[k], p, sizeof(p)) != 0) {
            k++;
        }
        if (k == n) {
            memcpy(raw[n], p, sizeof(p));
            for (int c = 0; c < 3; c++) {
                points[n].k[c] = p[c] * scale[c];
            }
            n++;
        }
    }
    ivp_compact_ledge_t *ledge = n > 0 ? ivp_build_convex_ledge(NULL, points, n) : NULL;
    free(points);
    free(raw);
    if (ledge == NULL) {
        return 0;
    }
    if (!push_pointer((void ***)ledges, ledge_count, ledge)) {
        ivp_compact_ledge_free(NULL, ledge);
        return 0;
    }
    ivp_ledge_soup_add(soup, ledge);
    return 1;
}

/* AddConcaveSurface: each face as a two sided triangle */
static int add_concave(nmo_engine_t *engine, ivp_ledge_soup_t *soup, ivp_compact_ledge_t ***ledges,
                       size_t *ledge_count, nmo_object_id_t mesh, const float scale[3])
{
    int32_t faces = nmo_engine_mesh_face_count(engine, mesh);
    for (int32_t f = 0; f < faces; f++) {
        int32_t v[3];
        ivp_vec3f_t points[3];
        bool ok = nmo_engine_mesh_face(engine, mesh, f, v);
        for (int k = 0; ok && k < 3; k++) {
            float p[3];
            ok = nmo_engine_mesh_vertex(engine, mesh, v[k], p);
            for (int c = 0; ok && c < 3; c++) {
                points[k].k[c] = p[c] * scale[c];
            }
        }
        ivp_compact_ledge_t *ledge = ok ? ivp_build_convex_ledge(NULL, points, 3) : NULL;
        if (ledge != NULL) {
            if (push_pointer((void ***)ledges, ledge_count, ledge)) {
                ivp_ledge_soup_add(soup, ledge);
            } else {
                ivp_compact_ledge_free(NULL, ledge);
            }
        }
    }
    return 1;
}

/* The polygon shape of the surface name, made from the meshes the first time */
static ivp_shape_t *polygon_shape(nmo_physics_ivp_t *p, const nmo_engine_physics_object_t *desc)
{
    for (size_t i = 0; i < p->shape_count; i++) {
        if (strcmp(p->shapes[i]->name, desc->collision_surface) == 0) {
            return p->shapes[i];
        }
    }
    ivp_ledge_soup_t soup;
    ivp_ledge_soup_init(&soup, NULL);
    ivp_compact_ledge_t **ledges = NULL;
    size_t ledge_count = 0;
    int made = 0;
    for (int32_t i = 0; i < desc->convex_count; i++) {
        if (desc->convex[i] != 0) {
            made += add_convex(p->engine, &soup, &ledges, &ledge_count, desc->convex[i], desc->scale);
        }
    }
    for (int32_t i = 0; i < desc->concave_count; i++) {
        if (desc->concave[i] != 0) {
            made += add_concave(p->engine, &soup, &ledges, &ledge_count, desc->concave[i], desc->scale);
        }
    }
    ivp_compact_surface_t *surface = made > 0 && ledge_count > 0 ? ivp_ledge_soup_compile(&soup) : NULL;
    ivp_ledge_soup_destroy(&soup);
    for (size_t i = 0; i < ledge_count; i++) {
        ivp_compact_ledge_free(NULL, ledges[i]);
    }
    free(ledges);
    if (surface == NULL) {
        return NULL; /* "Error: incorrect mesh" */
    }
    ivp_shape_t *shape = (ivp_shape_t *)calloc(1, sizeof(*shape));
    size_t length = strlen(desc->collision_surface);
    char *name = (char *)malloc(length + 1u);
    if (shape == NULL || name == NULL || !push_pointer((void ***)&p->shapes, &p->shape_count, shape)) {
        free(shape);
        free(name);
        ivp_compact_surface_free(NULL, surface);
        return NULL;
    }
    memcpy(name, desc->collision_surface, length + 1u);
    shape->name = name;
    shape->surface = surface;
    ivp_surman_polygon_init(&shape->surman, surface);
    return shape;
}

/* CreatePhysicsObjectOnParameters, FillTemplateInfo, CreatePhysicBall / CreatePhysicsPolygon */
static nmo_status_t create_object(void *user_data, const nmo_engine_physics_object_t *desc,
                                  void **out_handle)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    *out_handle = NULL;
    ivp_environment_t *env = environment(p);
    ivp_body_t *body = (ivp_body_t *)calloc(1, sizeof(*body));
    if (env == NULL || body == NULL) {
        free(body);
        return NMO_ERR_NOMEM;
    }
    body->owner = p;
    body->entity = desc->entity;
    const ivp_surman_t *surman = NULL;
    ivp_object_template_t t;
    ivp_object_template_init(&t);
    if (desc->shape == NMO_ENGINE_PHYSICS_BALL) {
        t.type = IVP_OBJECT_BALL;
        t.ball_radius = desc->ball_radius;
        ivp_surman_ball_init(&body->ball, desc->ball_radius);
        surman = &body->ball.base;
    } else {
        ivp_shape_t *shape = polygon_shape(p, desc);
        if (shape == NULL) {
            free(body);
            return NMO_ERR_INVALID_ARGUMENT;
        }
        t.type = IVP_OBJECT_POLYGON;
        surman = &shape->surman.base;
    }
    t.mass = desc->mass;
    t.material = ivp_material_default();
    t.material.friction = desc->friction;
    t.material.elasticity = desc->elasticity;
    t.physical_unmoveable = desc->fixed;
    t.name = nmo_engine_object_name(p->engine, desc->entity);
    t.rot_inertia_is_factor = true;
    t.rot_speed_damp_factor =
        ivp_vec3r(desc->rotation_damping, desc->rotation_damping, desc->rotation_damping);
    t.speed_damp_factor = desc->linear_damping;
    t.nocoll_group_ident = desc->collision_group[0] != '\0' ? desc->collision_group : NULL;
    if (!desc->automatic_mass_center) {
        t.use_mass_center_override = true;
        t.mass_center_override = ivp_vec3r(desc->shift_mass_center[0], desc->shift_mass_center[1],
                                           desc->shift_mass_center[2]);
    }
    entity_pose(p->engine, desc->entity, &t.position, &t.orientation);
    t.client_data = body;
    t.enable_collision_detection = desc->enable_collision;
    t.start_in_simulation = false;
    body->object = ivp_environment_create_object(env, &t, surman);
    if (body->object == NULL || !push_pointer((void ***)&p->bodies, &p->body_count, body)) {
        if (body->object != NULL) {
            ivp_environment_destroy_object(env, body->object);
        }
        free(body);
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!desc->start_frozen) {
        ivp_object_ensure_in_simulation(body->object);
    }
    *out_handle = body;
    return NMO_OK;
}

static void destroy_object(void *user_data, void *handle)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    ivp_body_t *body = (ivp_body_t *)handle;
    if (body == NULL) {
        return;
    }
    if (body->object != NULL && p->env != NULL) {
        if (body->listening) {
            ivp_object_remove_collision_listener(body->object, &body->listener);
        }
        ivp_environment_destroy_object(p->env, body->object);
    }
    body->object = NULL;
    for (size_t i = 0; i < p->body_count; i++) {
        if (p->bodies[i] == body) {
            memmove(&p->bodies[i], &p->bodies[i + 1], (p->body_count - i - 1u) * sizeof(*p->bodies));
            p->body_count--;
            break;
        }
    }
    free(body);
}

static ivp_vec3r_t vec(const float v[3])
{
    return ivp_vec3r(v[0], v[1], v[2]);
}

/* A world point in the object's space */
static ivp_vec3r_t object_space(ivp_object_t *object, const float world[3])
{
    ivp_transform_r_t m;
    ivp_object_get_m_world_f_object_AT(object, &m);
    ivp_vec3r_t w = vec(world), out;
    ivp_transform_inv_point(&out, &m, &w);
    return out;
}

/* PhysicsControllerForce::do_simulation_controller: the force at its point each PSI */
static void force_controller(const ivp_event_sim_t *event_sim, ivp_object_t *object, void *ctx)
{
    ivp_joint_t *joint = (ivp_joint_t *)ctx;
    (void)object;
    if (joint->body == NULL || joint->body->object == NULL) {
        return;
    }
    ivp_core_t *core = ivp_object_get_core(joint->body->object);
    ivp_transform_r_t m;
    ivp_core_calc_at_matrix(core, ivp_environment_get_time(event_sim->environment), &m);
    ivp_vec3r_t position_ws;
    ivp_transform_point(&position_ws, &m, &joint->position_cs);
    ivp_core_async_push_ws(core, &position_ws, &joint->force_ws);
}

static nmo_status_t create_constraint(void *user_data, const nmo_engine_physics_constraint_t *c,
                                      void *object1, void *object2, void **out_handle)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    *out_handle = NULL;
    ivp_body_t *a = (ivp_body_t *)object1, *b = (ivp_body_t *)object2;
    bool force = c->kind == NMO_ENGINE_PHYSICS_FORCE;
    if (p->env == NULL || a == NULL || a->object == NULL || (!force && (b == NULL || b->object == NULL))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    ivp_joint_t *joint = (ivp_joint_t *)calloc(1, sizeof(*joint));
    if (joint == NULL) {
        return NMO_ERR_NOMEM;
    }
    joint->kind = c->kind;
    ivp_vec3r_t anchor = vec(c->anchor), axis = vec(c->axis);
    if (c->kind == NMO_ENGINE_PHYSICS_SPRING) {
        /* PhysicsSpringCall: anchors in world space, absolute values */
        ivp_vec3r_t a_os = object_space(a->object, c->anchor), b_os = object_space(b->object, c->anchor2);
        ivp_spring_state_init(&joint->spring);
        ivp_spring_state_setup(&joint->spring, a->object, &a_os, b->object, &b_os, c->constant,
                               c->damping, c->length);
        /* IVP_Template_Spring::rel_pos_damp, taken by the constructor: the setter would wake
           the objects, and a spring between frozen objects leaves them frozen */
        joint->spring.rel_pos_damp = (ivp_real_t)(float)c->global_damping;
        joint->controller = ivp_environment_add_controller(p->env, IVP_CP_ACTUATOR,
                                                           ivp_controller_spring, &joint->spring);
    } else if (force) {
        /* PhysicsForceCall: the point in the core's space at its making */
        float world[3];
        ivp_transform_r_t object_m, core_m;
        ivp_object_get_m_world_f_object_AT(a->object, &object_m);
        ivp_vec3r_t w;
        ivp_transform_point(&w, &object_m, &anchor);
        for (int k = 0; k < 3; k++) {
            world[k] = (float)w.k[k];
        }
        ivp_core_t *core = ivp_object_get_core(a->object);
        ivp_core_calc_at_matrix(core, ivp_environment_get_time(p->env), &core_m);
        ivp_vec3r_t ws = vec(world);
        ivp_transform_inv_point(&joint->position_cs, &core_m, &ws);
        joint->force_ws = axis;
        joint->body = a;
        joint->controller = ivp_environment_create_core_controller(p->env, IVP_CP_ACTUATOR,
                                                                   force_controller, joint);
        ivp_environment_add_controller_to_core(p->env, joint->controller, a->object);
    } else {
        /* PhysicsHingeCall, PhysicsSliderCall, PhysicsBallJointCall: set_constraint_ws */
        ivp_constraint_template_t t;
        ivp_constraint_template_init(&t);
        if (c->kind == NMO_ENGINE_PHYSICS_HINGE) {
            ivp_constraint_template_set_constraint_ws(&t, a->object, &anchor, &axis, 3u, 2u,
                                                      b->object, NULL);
            if (c->limited) {
                ivp_constraint_template_limit_axis(&t, 3 + 2, c->lower, c->upper);
            }
        } else if (c->kind == NMO_ENGINE_PHYSICS_SLIDER) {
            ivp_constraint_template_set_constraint_ws(&t, a->object, &anchor, &axis, 2u, 3u,
                                                      b->object, NULL);
            if (c->limited) {
                ivp_constraint_template_limit_axis(&t, 2, c->lower, c->upper);
            }
        } else {
            ivp_constraint_template_set_constraint_ws(&t, a->object, &anchor, NULL, 3u, 0u,
                                                      b->object, NULL);
        }
        ivp_constraint_init_from_template(&joint->constraint, &t);
        joint->controller = ivp_environment_add_controller(p->env, IVP_CP_CONSTRAINTS,
                                                           ivp_controller_constraint, &joint->constraint);
    }
    if (!ivp_controller_handle_is_valid(joint->controller) ||
        !push_pointer((void ***)&p->joints, &p->joint_count, joint)) {
        if (ivp_controller_handle_is_valid(joint->controller)) {
            ivp_environment_remove_controller(p->env, joint->controller);
        }
        free(joint);
        return NMO_ERR_INVALID_ARGUMENT;
    }
    joint->alive = true;
    *out_handle = joint;
    return NMO_OK;
}

static void destroy_constraint(void *user_data, void *handle)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    ivp_joint_t *joint = (ivp_joint_t *)handle;
    if (joint == NULL) {
        return;
    }
    if (joint->alive && p->env != NULL) {
        ivp_environment_remove_controller(p->env, joint->controller);
    }
    joint->alive = false;
}

/* async_push_object_ws */
static void push(void *user_data, void *object, const float position[3], const float impulse[3])
{
    ivp_body_t *body = (ivp_body_t *)object;
    (void)user_data;
    if (body != NULL && body->object != NULL) {
        ivp_vec3r_t at = vec(position), by = vec(impulse);
        ivp_object_async_push_ws(body->object, &at, &by);
    }
}

static void wake_up(void *user_data, void *object)
{
    ivp_body_t *body = (ivp_body_t *)object;
    (void)user_data;
    if (body != NULL && body->object != NULL) {
        ivp_object_ensure_in_simulation(body->object);
    }
}

static void set_gravity(void *user_data, const float gravity[3])
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    p->gravity = vec(gravity);
    if (p->env != NULL) {
        ivp_environment_set_gravity(p->env, &p->gravity);
    }
}

/* The entity of the other object of a contact; 0 for one not physical through the engine */
static nmo_object_id_t other_entity(const ivp_body_t *body, ivp_object_t *a, ivp_object_t *b)
{
    ivp_object_t *other = a == body->object ? b : a;
    const ivp_body_t *other_body =
        other != NULL ? (const ivp_body_t *)ivp_object_get_client_data(other) : NULL;
    return other_body != NULL ? other_body->entity : 0;
}

static void copy3(float out[3], const ivp_vec3r_t *v)
{
    for (int k = 0; k < 3; k++) {
        out[k] = (float)v->k[k];
    }
}

/* event_pre_collision */
static void heard_collision(void *ctx, const ivp_collision_event_t *event)
{
    ivp_body_t *body = (ivp_body_t *)ctx;
    nmo_engine_physics_collision_t c;
    memset(&c, 0, sizeof(c));
    c.object = body->entity;
    c.other = other_entity(body, event->obj_a, event->obj_b);
    c.object_first = event->obj_a == body->object;
    copy3(c.speed, &event->speed);
    copy3(c.normal, &event->contact_normal);
    copy3(c.point, &event->contact_point);
    nmo_engine_physics_collision(body->owner->engine, &c);
}

/* event_friction_created / event_friction_deleted */
static void heard_contact_started(void *ctx, const ivp_friction_event_t *event)
{
    ivp_body_t *body = (ivp_body_t *)ctx;
    nmo_engine_physics_contact(body->owner->engine, body->entity,
                               other_entity(body, event->obj_a, event->obj_b), true);
}

static void heard_contact_ended(void *ctx, const ivp_friction_event_t *event)
{
    ivp_body_t *body = (ivp_body_t *)ctx;
    nmo_engine_physics_contact(body->owner->engine, body->entity,
                               other_entity(body, event->obj_a, event->obj_b), false);
}

/* add_listener_collision (pre collision and friction) / remove_listener_collision */
static void listen_object(void *user_data, void *object, bool on)
{
    ivp_body_t *body = (ivp_body_t *)object;
    (void)user_data;
    if (body == NULL || body->object == NULL || body->listening == on) {
        return;
    }
    if (on) {
        ivp_collision_listener_init(&body->listener,
                                    IVP_COLLISION_CB_PRE_COLLISION | IVP_COLLISION_CB_FRICTION_CREATED |
                                        IVP_COLLISION_CB_FRICTION_DELETED,
                                    body);
        body->listener.pre_collision_fn = heard_collision;
        body->listener.friction_created_fn = heard_contact_started;
        body->listener.friction_deleted_fn = heard_contact_ended;
        body->listening = ivp_object_add_collision_listener(body->object, &body->listener) == 0;
    } else {
        ivp_object_remove_collision_listener(body->object, &body->listener);
        body->listening = false;
    }
}

static double now(void *user_data)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    return p->env != NULL ? (double)ivp_environment_get_time(p->env) : 0.0;
}

/* An attacher gone with its phantom's set (the phantom's object deleted) */
static void attacher_deleted(void *ctx, ivp_buoyancy_attacher_t *attacher)
{
    (void)attacher;
    ((ivp_liquid_t *)ctx)->attacher = NULL;
}

/*
 * PhysicsBuoyancyCall::Call: the object a phantom keeping the cores in it,
 * the plane through the surface points as IVP_U_Float_Hesse makes it
 * (normal (p2 - p0) x (p1 - p0), normalized; the liquid on its positive
 * side), and a buoyancy attached to those cores with the block's factors
 * and IVP_Template_Buoyancy's other values as the DLL sets them.
 */
static nmo_status_t add_buoyancy(void *user_data, void *object, const nmo_engine_physics_buoyancy_t *b)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    ivp_body_t *body = (ivp_body_t *)object;
    if (p->env == NULL || body == NULL || body->object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    ivp_template_phantom_t phantom_template;
    ivp_template_phantom_init(&phantom_template);
    phantom_template.manage_intruding_cores = true;
    ivp_controller_phantom_t *phantom = ivp_object_get_controller_phantom(body->object);
    if (phantom == NULL) {
        phantom = ivp_object_convert_to_phantom(body->object, &phantom_template);
    }
    if (phantom == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    /* IVP_U_Float_Hesse::calc_hesse and normize, in its float and double steps */
    const float *p0 = b->surface[0], *p1 = b->surface[1], *p2 = b->surface[2];
    double a[3], e[3];
    for (int k = 0; k < 3; k++) {
        a[k] = (double)p1[k] - p0[k];
        e[k] = (double)p2[k] - p0[k];
    }
    float n[3] = {(float)(e[1] * a[2] - a[1] * e[2]), (float)(e[2] * a[0] - a[2] * e[0]),
                  (float)(e[0] * a[1] - a[0] * e[1])};
    float hesse = (float)-((double)n[0] * p0[0] + (double)n[1] * p0[1] + (double)n[2] * p0[2]);
    double length = sqrt((double)n[0] * n[0] + (double)n[1] * n[1] + (double)n[2] * n[2]);
    if (length < 1e-12) {
        n[0] = n[1] = n[2] = 0.0f;
        hesse = 0.0f;
    } else {
        double inv = 1.0 / length;
        for (int k = 0; k < 3; k++) {
            n[k] = (float)(n[k] * inv);
        }
        hesse = (float)(hesse * inv);
    }
    ivp_liquid_t *liquid = (ivp_liquid_t *)calloc(1, sizeof(*liquid));
    ivp_buoyancy_state_t *t = (ivp_buoyancy_state_t *)calloc(1, sizeof(*t));
    if (liquid == NULL || t == NULL ||
        !push_pointer((void ***)&p->liquids, &p->liquid_count, liquid)) {
        free(liquid);
        free(t);
        return NMO_ERR_NOMEM;
    }
    /* libivp's plane: the normal out of the liquid, normal . x = height on it */
    ivp_vec3r_t out_normal = ivp_vec3r(-n[0], -n[1], -n[2]);
    ivp_liquid_surface_simple_init_plane(&liquid->surface, &out_normal, hesse, b->density);
    liquid->surface.flow_velocity = vec(b->current);
    ivp_buoyancy_init_plane(t, &out_normal, hesse, b->density);
    ivp_buoyancy_set_liquid_surface(t, &liquid->surface.base);
    t->simulate_wing_behavior = false;
    t->use_interpolation = false;
    t->linear_drag = b->damp_factor;
    t->fluid_density = b->density;
    t->friction_damp_factor = b->friction_factor;
    t->buoyancy_eps = 1.0e-10;
    t->angular_drag = 0.01f;
    t->max_interpolation_tries = 10;
    t->max_tries_nr_of_vectors_involved = 15;
    t->use_stochastic_insertion = true;
    t->insert_extrapol_only = true;
    t->mi_weight_current_speed = 1.0;
    t->mi_weight_surface = 1.0;
    t->mi_weight_rot_speed = 1.0;
    t->max_res = 0.01f;
    t->nr_future_psi_for_extrapolation = 1;
    t->viscosity_factor = b->viscosity;
    t->torque_factor = b->torque_factor;
    t->viscosity_input_factor = b->suction_factor;
    liquid->attacher =
        ivp_buoyancy_attacher_create(p->env, t, ivp_controller_phantom_get_intruding_cores(phantom));
    free(t);
    if (liquid->attacher == NULL) {
        return NMO_ERR_NOMEM;
    }
    ivp_buoyancy_attacher_set_deleted_listener(liquid->attacher, attacher_deleted, liquid);
    return NMO_OK;
}

/* The attachers detached before their environment goes: on its deletion an attacher would take
   the cores out of its phantom's set, which the phantom then removes them from again */
static void detach_liquids(nmo_physics_ivp_t *p)
{
    for (size_t i = 0; i < p->liquid_count; i++) {
        if (p->liquids[i]->attacher != NULL) {
            ivp_buoyancy_attacher_destroy(p->liquids[i]->attacher);
            p->liquids[i]->attacher = NULL;
        }
    }
}

/* The liquids' surfaces freed, once no buoyancy reads them */
static void drop_liquids(nmo_physics_ivp_t *p)
{
    for (size_t i = 0; i < p->liquid_count; i++) {
        free(p->liquids[i]);
    }
    p->liquid_count = 0;
}

/* The environment's objects and controllers gone (their handles stay, inert) */
static void drop_environment(nmo_physics_ivp_t *p)
{
    for (size_t i = 0; i < p->joint_count; i++) {
        p->joints[i]->alive = false;
    }
    for (size_t i = 0; i < p->body_count; i++) {
        p->bodies[i]->object = NULL;
        p->bodies[i]->listening = false;
    }
    detach_liquids(p);
    if (p->env != NULL) {
        ivp_environment_destroy(p->env);
        p->env = NULL;
    }
    drop_liquids(p);
}

/* Set Physics Globals' Clean Physics World: DeleteEnvironment, CreateEnvironment */
static void reset(void *user_data)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    drop_environment(p);
    p->gravity = ivp_vec3r(0, -9.81, 0);
    environment(p);
}

/* CKIpionManager::DeleteCollisionSurface: the polygon shapes freed (no object uses them) */
static void delete_surfaces(void *user_data)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    for (size_t i = 0; i < p->shape_count; i++) {
        ivp_compact_surface_free(NULL, p->shapes[i]->surface);
        free(p->shapes[i]->name);
        free(p->shapes[i]);
    }
    p->shape_count = 0;
}

/* simulate_dtime, then each moving object's pose to its entity */
static void simulate(void *user_data, nmo_engine_t *engine, float seconds)
{
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)user_data;
    if (p->env == NULL) {
        return;
    }
    ivp_environment_simulate_dtime(p->env, seconds);
    for (size_t i = p->body_count; i-- > 0;) {
        ivp_object_t *object = p->bodies[i]->object;
        if (object == NULL || ivp_object_is_unmoveable(object) || ivp_object_is_frozen(object)) {
            continue;
        }
        ivp_transform_r_t m;
        ivp_object_get_m_world_f_object_AT(object, &m);
        float world[16] = {0};
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                world[c * 4 + r] = (float)m.rot.rows[r].k[c];
            }
        }
        world[12] = (float)m.pos.k[0];
        world[13] = (float)m.pos.k[1];
        world[14] = (float)m.pos.k[2];
        world[15] = 1.0f;
        nmo_engine_set_entity_world_matrix(engine, p->bodies[i]->entity, world, false);
    }
}

nmo_status_t nmo_physics_ivp_attach(nmo_engine_t *engine, nmo_physics_ivp_t **out_physics)
{
    if (engine == NULL || out_physics == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_physics_ivp_t *p = (nmo_physics_ivp_t *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return NMO_ERR_NOMEM;
    }
    p->engine = engine;
    p->gravity = ivp_vec3r(0, -9.81, 0);
    nmo_engine_physics_provider_t provider = {
        .user_data = p,
        .create_object = create_object,
        .destroy_object = destroy_object,
        .create_constraint = create_constraint,
        .destroy_constraint = destroy_constraint,
        .push = push,
        .wake_up = wake_up,
        .set_gravity = set_gravity,
        .reset = reset,
        .simulate = simulate,
        .listen = listen_object,
        .time = now,
        .delete_surfaces = delete_surfaces,
        .add_buoyancy = add_buoyancy,
    };
    nmo_engine_set_physics_provider(engine, &provider);
    *out_physics = p;
    return NMO_OK;
}

void nmo_physics_ivp_detach(nmo_physics_ivp_t *p)
{
    if (p == NULL) {
        return;
    }
    nmo_engine_set_physics_provider(p->engine, NULL);
    drop_environment(p);
    for (size_t i = 0; i < p->body_count; i++) {
        free(p->bodies[i]);
    }
    for (size_t i = 0; i < p->joint_count; i++) {
        free(p->joints[i]);
    }
    delete_surfaces(p);
    free(p->bodies);
    free(p->joints);
    free(p->shapes);
    free(p->liquids);
    free(p);
}
