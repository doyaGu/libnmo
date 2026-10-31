/**
 * @file physics.c
 * @brief The physics blocks of physics_RT and what CKIpionManager keeps for
 *        them, without a simulation.
 *
 * Each block does what physics_RT.dll's does (the reference is the DLL):
 * Physicalize records a physical object with its parameters and shape,
 * the Set Physics blocks a constraint between two of them, Set Physics
 * Globals the gravity and time factor. A constraint block's call, and
 * Physics WakeUp's, runs at once when its objects are physical and is
 * tried again before each step until they are (PhysicsCallManager). After
 * the behaviors each frame the manager steps (CKIpionManager::PostProcess)
 * by the frame time smoothed over the last frames, times the time factor.
 * The simulation is the host's provider (nmo_engine_set_physics_provider());
 * without one, nothing moves.
 */

#include "../engine_block_util.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* An input string, "" for none */
static const char *get_in_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : "";
}

static char *dup_string(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

/* ============================================================================
 * The manager
 * ============================================================================ */

static void physics_free_object(eng_physics_object_t *object)
{
    free(object->meshes);
    free(object->surface);
    free(object->group);
}

void eng_physics_init(nmo_engine_t *engine)
{
    eng_physics_t *physics = &engine->physics;
    physics->gravity[0] = 0.0f;
    physics->gravity[1] = -9.81f;
    physics->gravity[2] = 0.0f;
    physics->time_factor = 0.001; /* ms to s */
}

void eng_free_physics(nmo_engine_t *engine)
{
    eng_physics_t *physics = &engine->physics;
    for (size_t i = 0; i < physics->objects.count; i++) {
        physics_free_object(&physics->objects.data[i]);
    }
    free(physics->objects.data);
    free(physics->constraints.data);
    free(physics->calls.data);
    for (size_t i = 0; i < physics->surfaces.count; i++) {
        free(physics->surfaces.data[i]);
    }
    free(physics->surfaces.data);
    for (size_t i = 0; i < physics->contacts.count; i++) {
        free(physics->contacts.data[i].groups);
    }
    free(physics->listeners.data);
    free(physics->contacts.data);
    free(physics->records.data);
}

/* CKIpionManager::GetPhysicsObject; -1 when it is not physical */
static int32_t physics_object_index(const nmo_engine_t *engine, nmo_object_id_t entity)
{
    for (size_t i = 0; entity != 0 && i < engine->physics.objects.count; i++) {
        if (engine->physics.objects.data[i].desc.entity == entity) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool physics_has_surface(const nmo_engine_t *engine, const char *name)
{
    for (size_t i = 0; i < engine->physics.surfaces.count; i++) {
        if (strcmp(engine->physics.surfaces.data[i], name) == 0) {
            return true;
        }
    }
    return false;
}

void nmo_engine_set_physics_provider(nmo_engine_t *engine,
                                     const nmo_engine_physics_provider_t *provider)
{
    if (engine == NULL) {
        return;
    }
    engine->physics.has_provider = provider != NULL;
    if (provider != NULL) {
        engine->physics.provider = *provider;
    } else {
        memset(&engine->physics.provider, 0, sizeof(engine->physics.provider));
    }
}

static const nmo_engine_physics_provider_t *provider_of(const nmo_engine_t *engine)
{
    return engine->physics.has_provider ? &engine->physics.provider : NULL;
}

bool nmo_engine_physics_object(const nmo_engine_t *engine, nmo_object_id_t entity,
                               nmo_engine_physics_object_t *out)
{
    int32_t index = engine != NULL ? physics_object_index(engine, entity) : -1;
    if (index >= 0 && out != NULL) {
        *out = engine->physics.objects.data[index].desc;
    }
    return index >= 0;
}

int32_t nmo_engine_physics_object_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->physics.objects.count : 0;
}

bool nmo_engine_physics_object_at(const nmo_engine_t *engine, int32_t index,
                                  nmo_engine_physics_object_t *out)
{
    if (engine == NULL || index < 0 || (size_t)index >= engine->physics.objects.count) {
        return false;
    }
    if (out != NULL) {
        *out = engine->physics.objects.data[index].desc;
    }
    return true;
}

int32_t nmo_engine_physics_constraint_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->physics.constraints.count : 0;
}

bool nmo_engine_physics_constraint(const nmo_engine_t *engine, int32_t index,
                                   nmo_engine_physics_constraint_t *out)
{
    if (engine == NULL || index < 0 || (size_t)index >= engine->physics.constraints.count) {
        return false;
    }
    if (out != NULL) {
        *out = engine->physics.constraints.data[index].desc;
    }
    return true;
}

void nmo_engine_physics_gravity(const nmo_engine_t *engine, float out[3])
{
    for (int k = 0; k < 3; k++) {
        out[k] = engine != NULL ? engine->physics.gravity[k] : 0.0f;
    }
}

static const nmo_mesh_state_t *public_mesh(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = engine != NULL && id != 0 ? eng_find_object(engine, id) : NULL;
    return object != NULL ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                engine->types, object, CKPGUID_MESH)
                          : NULL;
}

int32_t nmo_engine_mesh_vertex_count(const nmo_engine_t *engine, nmo_object_id_t mesh)
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    return state != NULL ? (int32_t)state->vertex_count : -1;
}

bool nmo_engine_mesh_vertex(const nmo_engine_t *engine, nmo_object_id_t mesh, int32_t index,
                            float out_position[3])
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    if (state == NULL || index < 0 || (uint32_t)index >= state->vertex_count) {
        return false;
    }
    out_position[0] = state->vertices[index].position.x;
    out_position[1] = state->vertices[index].position.y;
    out_position[2] = state->vertices[index].position.z;
    return true;
}

int32_t nmo_engine_mesh_face_count(const nmo_engine_t *engine, nmo_object_id_t mesh)
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    return state != NULL ? (int32_t)state->face_count : -1;
}

bool nmo_engine_mesh_face(const nmo_engine_t *engine, nmo_object_id_t mesh, int32_t index,
                          int32_t out_vertices[3])
{
    const nmo_mesh_state_t *state = public_mesh(engine, mesh);
    if (state == NULL || index < 0 || (uint32_t)index >= state->face_count) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        out_vertices[k] = state->face_vertex_indices[3u * (uint32_t)index + (uint32_t)k];
    }
    return true;
}

/* ============================================================================
 * Calls (PhysicsCallManager)
 * ============================================================================ */

/* A call's kind: a constraint kind, or one of these */
#define CALL_WAKE_UP (-1)
#define CALL_COLL_DETECTION (-2)
#define CALL_CONTINUOUS_CONTACT (-3)
#define CALL_DELETE_SURFACES (-4)
#define CALL_BUOYANCY (-5)
#define CALL_RESET (-6)

/* The constraint a constraint block made (its IVP_Handle local), +1; 0 for none */
static int32_t *block_constraint(blk_t *b)
{
    return (int32_t *)nmo_engine_block_state(b, sizeof(int32_t));
}

static int32_t entity_of(nmo_engine_t *engine, nmo_object_id_t id)
{
    return id != 0 ? eng_entity_index(engine, id) : -1;
}

/* A point in a referential's space to world space, as is without one */
static eng_vector_t to_world(nmo_engine_t *engine, nmo_object_id_t referential, eng_vector_t p)
{
    int32_t index = entity_of(engine, referential);
    if (index >= 0) {
        eng_vector_t world;
        eng_entity_transform(engine, index, &world, &p, -1);
        return world;
    }
    return p;
}

static void set3(float out[3], eng_vector_t v)
{
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

/*
 * PhysicsHingeCall, PhysicsSliderCall, PhysicsSpringCall and
 * PhysicsBallJointCall::Call (and PhysicsForceCall's, on the target
 * alone): the constraint from the block's inputs now, when the target and
 * Object2 are physical. True when the call is done (made, or never to be:
 * no target or Object2).
 */
static bool call_constraint(nmo_engine_t *engine, int32_t node,
                            nmo_engine_physics_constraint_kind_t kind)
{
    blk_t block = {engine, node};
    blk_t *b = &block;
    nmo_object_id_t target = nmo_engine_block_target(b);
    bool force = kind == NMO_ENGINE_PHYSICS_FORCE;
    nmo_object_id_t object2 = force ? 0 : nmo_engine_block_get_input_object(b, 0);
    if (target == 0 || (!force && object2 == 0)) {
        return true;
    }
    int32_t first = physics_object_index(engine, target);
    int32_t second = force ? -1 : physics_object_index(engine, object2);
    if (first < 0 || (!force && second < 0)) {
        return false; /* until both are physical */
    }
    nmo_engine_physics_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.object1 = target;
    c.object2 = object2;
    c.kind = kind;
    if (force) {
        /* PhysicsForceCall: Force Value along Direction (in Direction Ref's
         * space; X for none), at Position (the object's own with the target
         * as Pos Referential, else in Pos Referential's space) */
        eng_vector_t position = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f), direction = get_in_vector(b, 2, 0.0f, 0.0f, 0.0f);
        nmo_object_id_t referential = nmo_engine_block_get_input_object(b, 1);
        int32_t direction_ref = entity_of(engine, nmo_engine_block_get_input_object(b, 3));
        float value = get_in_float(b, 4, 10.0f);
        eng_vector_t v = direction;
        if (direction_ref >= 0) {
            eng_entity_transform_vector(engine, direction_ref, &v, &direction, -1, false);
        }
        double d[3] = {v.x, v.y, v.z};
        double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int k = 0; k < 3; k++) {
            d[k] = length > 0.0 ? d[k] / length : (k == 0 ? 1.0 : 0.0);
            c.axis[k] = (float)(d[k] * value);
        }
        if (referential == target) {
            set3(c.anchor, position);
        } else {
            eng_vector_t world = to_world(engine, referential, position), local;
            eng_entity_inverse_transform(engine, entity_of(engine, target), &local, &world, -1);
            set3(c.anchor, local);
        }
    } else if (kind == NMO_ENGINE_PHYSICS_HINGE) {
        /* Hinge: about the Joint Referential's direction, at its position */
        eng_vector_t pos = {0.0f, 0.0f, 0.0f}, dir = {0.0f, 0.0f, 1.0f};
        int32_t ref = entity_of(engine, nmo_engine_block_get_input_object(b, 1));
        if (ref >= 0) {
            eng_vector_t up, right;
            eng_entity_orientation(engine, ref, &dir, &up, &right, -1);
            pos = eng_entity_position(engine, ref, -1);
        }
        c.limited = get_in_int(b, 2, 0) != 0;
        float lower = get_in_float(b, 3, -45.0f), upper = get_in_float(b, 4, 45.0f);
        set3(c.anchor, pos);
        set3(c.axis, dir);
        c.lower = (float)(lower * 0.0174532925199433);
        c.upper = (float)(upper * 0.0174532925199433);
    } else if (kind == NMO_ENGINE_PHYSICS_SLIDER) {
        /* Slider: along the line from the first axis point to the second */
        eng_vector_t p1 = {0.0f, 0.0f, 0.0f}, p2 = {0.0f, 0.0f, 0.0f};
        int32_t a1 = entity_of(engine, nmo_engine_block_get_input_object(b, 1));
        int32_t a2 = entity_of(engine, nmo_engine_block_get_input_object(b, 2));
        c.limited = get_in_int(b, 3, 0) != 0;
        c.lower = get_in_float(b, 4, -1.0f);
        c.upper = get_in_float(b, 5, 1.0f);
        if (a1 >= 0) {
            p1 = eng_entity_position(engine, a1, -1);
        }
        if (a2 >= 0) {
            p2 = eng_entity_position(engine, a2, -1);
        }
        set3(c.anchor, p1);
        c.axis[0] = p2.x - p1.x;
        c.axis[1] = p2.y - p1.y;
        c.axis[2] = p2.z - p1.z;
    } else if (kind == NMO_ENGINE_PHYSICS_SPRING) {
        /* Spring: between a point of each, in its referential's space */
        eng_vector_t p1 = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f), p2 = get_in_vector(b, 3, 0.0f, 0.0f, 0.0f);
        set3(c.anchor, to_world(engine, nmo_engine_block_get_input_object(b, 2), p1));
        set3(c.anchor2, to_world(engine, nmo_engine_block_get_input_object(b, 4), p2));
        c.length = get_in_float(b, 5, 1.0f);
        c.constant = get_in_float(b, 6, 1.0f);
        c.damping = get_in_float(b, 7, 0.1f);
        c.global_damping = get_in_float(b, 8, 0.1f);
    } else {
        /* Ball Joint: at Position 1, in Referential 1's space */
        eng_vector_t p = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
        set3(c.anchor, to_world(engine, nmo_engine_block_get_input_object(b, 2), p));
    }
    eng_physics_constraint_t *record = NULL;
    if (!ENG_VEC_PUSH(engine->physics.constraints, record)) {
        return true;
    }
    memset(record, 0, sizeof(*record));
    record->desc = c;
    record->alive = true;
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (provider != NULL && provider->create_constraint != NULL) {
        provider->create_constraint(provider->user_data, &c,
                                    engine->physics.objects.data[first].handle,
                                    second >= 0 ? engine->physics.objects.data[second].handle : NULL,
                                    &record->handle);
    }
    int32_t *slot = block_constraint(b);
    if (slot != NULL) {
        *slot = (int32_t)engine->physics.constraints.count;
    }
    return true;
}

/* PhysicsWakeUpCall::Call: the target woken once it is physical */
static bool call_wake_up(nmo_engine_t *engine, int32_t node)
{
    blk_t block = {engine, node};
    nmo_object_id_t target = nmo_engine_block_target(&block);
    if (target == 0) {
        return true;
    }
    int32_t index = physics_object_index(engine, target);
    if (index < 0) {
        return false;
    }
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (provider != NULL && provider->wake_up != NULL) {
        provider->wake_up(provider->user_data, engine->physics.objects.data[index].handle);
    }
    return true;
}

/*
 * PhysicsDeleteCollisionSurfacesCall::Call: the polygon shapes kept by
 * surface name dropped once nothing is physical ("Please dephysicalize all
 * objects before calling DeleteCollisionSurfaces!!!" until then).
 */
static bool call_delete_surfaces(nmo_engine_t *engine)
{
    eng_physics_t *physics = &engine->physics;
    if (physics->objects.count > 0) {
        return false;
    }
    for (size_t i = 0; i < physics->surfaces.count; i++) {
        free(physics->surfaces.data[i]);
    }
    physics->surfaces.count = 0;
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (provider != NULL && provider->delete_surfaces != NULL) {
        provider->delete_surfaces(provider->user_data);
    }
    return true;
}

/* ============================================================================
 * Collisions and contacts (PhysicsCollDetection, PhysicsContinuousContact)
 * ============================================================================ */

static double physics_now(const nmo_engine_t *engine)
{
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    return provider != NULL && provider->time != NULL ? provider->time(provider->user_data)
                                                      : engine->physics.time;
}

/* Whether anything listens to the entity's collisions or contacts */
static bool physics_listened(const nmo_engine_t *engine, nmo_object_id_t entity)
{
    for (size_t i = 0; i < engine->physics.listeners.count; i++) {
        const eng_physics_listener_t *l = &engine->physics.listeners.data[i];
        if (l->alive && l->entity == entity) {
            return true;
        }
    }
    for (size_t i = 0; i < engine->physics.contacts.count; i++) {
        const eng_physics_contacts_t *c = &engine->physics.contacts.data[i];
        if (c->alive && c->entity == entity) {
            return true;
        }
    }
    return false;
}

/* add_listener_collision / remove_listener_collision, once per object */
static void physics_listen(nmo_engine_t *engine, nmo_object_id_t entity, bool listen)
{
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    int32_t index = physics_object_index(engine, entity);
    if (provider != NULL && provider->listen != NULL && index >= 0) {
        provider->listen(provider->user_data, engine->physics.objects.data[index].handle, listen);
    }
}

/* PhysicsCollDetectionCall::Call: a listener once the target is physical */
static bool call_coll_detection(nmo_engine_t *engine, int32_t node)
{
    blk_t block = {engine, node};
    blk_t *b = &block;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return true;
    }
    float min_speed = get_in_float(b, 0, 0.3f), max_speed = get_in_float(b, 1, 0.3f);
    float sleep = get_in_float(b, 2, 0.5f);
    int32_t collision_id = get_in_int(b, 3, 1);
    if (physics_object_index(engine, target) < 0) {
        return false;
    }
    bool listened = physics_listened(engine, target);
    eng_physics_listener_t *l = NULL;
    if (!ENG_VEC_PUSH(engine->physics.listeners, l)) {
        return true;
    }
    *l = (eng_physics_listener_t){node, target, min_speed, max_speed, sleep, collision_id, 0.0, true};
    int32_t *slot = (int32_t *)nmo_engine_block_state(b, sizeof(int32_t));
    if (slot != NULL) {
        *slot = (int32_t)engine->physics.listeners.count;
    }
    if (!listened) {
        physics_listen(engine, target, true);
    }
    return true;
}

/* PhysicsContinuousContactCall::Call: the object's contact data, made once; done either way */
static bool call_continuous_contact(nmo_engine_t *engine, int32_t node)
{
    blk_t block = {engine, node};
    blk_t *b = &block;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return true;
    }
    float delay_start = get_in_float(b, 0, 0.1f), delay_end = get_in_float(b, 1, 0.1f);
    engine->physics.group_output_count = get_local_int(b, 0, 5);
    if (physics_object_index(engine, target) < 0) {
        return true;
    }
    for (size_t i = 0; i < engine->physics.contacts.count; i++) {
        if (engine->physics.contacts.data[i].alive && engine->physics.contacts.data[i].entity == target) {
            return true; /* the object has its contact data already */
        }
    }
    int32_t count = engine->physics.group_output_count > 0 ? engine->physics.group_output_count : 0;
    eng_contact_group_t *groups =
        (eng_contact_group_t *)calloc(count > 0 ? (size_t)count : 1u, sizeof(eng_contact_group_t));
    bool listened = physics_listened(engine, target);
    eng_physics_contacts_t *c = NULL;
    if (groups == NULL || !ENG_VEC_PUSH(engine->physics.contacts, c)) {
        free(groups);
        return true;
    }
    *c = (eng_physics_contacts_t){node, target, delay_start, delay_end, groups, count, true};
    int32_t *slot = (int32_t *)nmo_engine_block_state(b, sizeof(int32_t));
    if (slot != NULL) {
        *slot = (int32_t)engine->physics.contacts.count;
    }
    if (!listened) {
        physics_listen(engine, target, true);
    }
    return true;
}

/* The integer value of `object`'s attribute named `name`; `fallback` without one */
static int32_t attribute_named(const nmo_engine_t *engine, nmo_object_id_t object, const char *name,
                               int32_t fallback)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, name) == 0) {
            int32_t value = fallback;
            const void *data = NULL;
            size_t size = 0;
            if (nmo_engine_object_attribute_value(engine, object, (int32_t)t, &data, &size, NULL) ==
                    NMO_OK &&
                data != NULL && size >= sizeof(value)) {
                memcpy(&value, data, sizeof(value));
            }
            return value;
        }
    }
    return fallback;
}

/*
 * PhysicsCollDetectionListener::event_pre_collision: a collision of the
 * object as fast as Min Speed, Sleep afterwards after the last and while
 * Collision is not active, with the other object of the Collision ID when
 * "Use Collision ID": the other object, the speed between Min and Max as
 * [0, 1], the normal from the object, and the point (its z left 0, as the
 * DLL leaves it).
 */
void nmo_engine_physics_collision(nmo_engine_t *engine, const nmo_engine_physics_collision_t *c)
{
    if (engine == NULL || c == NULL) {
        return;
    }
    for (size_t i = 0; i < engine->physics.listeners.count; i++) {
        eng_physics_listener_t *l = &engine->physics.listeners.data[i];
        if (!l->alive || l->entity != c->object) {
            continue;
        }
        blk_t block = {engine, l->node};
        blk_t *b = &block;
        int32_t other_id = attribute_named(engine, c->other, "Coll Detection ID", -1);
        bool use_id = get_local_int(b, 1, 0) != 0;
        if ((use_id && other_id != l->collision_id) || physics_object_index(engine, c->object) < 0) {
            continue;
        }
        double now = physics_now(engine);
        if (now - l->last_time < l->sleep || nmo_engine_block_is_output_active(b, 0)) {
            continue;
        }
        double magnitude = sqrt((double)c->speed[0] * c->speed[0] + (double)c->speed[1] * c->speed[1] +
                                (double)c->speed[2] * c->speed[2]);
        float speed = (float)magnitude;
        if (!(speed > l->min_speed)) {
            continue;
        }
        float scaled;
        if (speed <= 0.000099999997f) {
            scaled = 0.000099999997f;
        } else if (l->max_speed < 0.000099999997f || (scaled = speed / l->max_speed) > 1.0f) {
            scaled = 1.0f;
        }
        uint32_t other = c->other;
        float normal[3], position[3] = {c->point[0], c->point[1], 0.0f};
        for (int k = 0; k < 3; k++) {
            normal[k] = c->object_first ? c->normal[k] : -c->normal[k];
        }
        nmo_engine_block_set_output_value(b, 0, &other, sizeof(other));
        nmo_engine_block_set_output_value(b, 1, &scaled, sizeof(scaled));
        nmo_engine_block_set_output_value(b, 2, normal, sizeof(normal));
        nmo_engine_block_set_output_value(b, 3, position, sizeof(position));
        l->last_time = now;
        OUT_ON(b, 0);
    }
}

static void record_add(nmo_engine_t *engine, int32_t contacts, int32_t id, double time)
{
    eng_contact_record_t *r = NULL;
    if (ENG_VEC_PUSH(engine->physics.records, r)) {
        *r = (eng_contact_record_t){contacts, id, time};
    }
}

static void record_remove(nmo_engine_t *engine, size_t index)
{
    eng_contact_record_t *data = engine->physics.records.data;
    memmove(&data[index], &data[index + 1],
            (engine->physics.records.count - index - 1u) * sizeof(*data));
    engine->physics.records.count--;
}

/*
 * PhysicsContinuousContactListener (friction created / deleted): a contact
 * with an object of a "Continuous Contact ID" counts in its group; the first
 * one gives "contact on" once Time Delay Start has passed since it started,
 * the end of the last "contact off" once Time Delay End has.
 */
void nmo_engine_physics_contact(nmo_engine_t *engine, nmo_object_id_t object, nmo_object_id_t other,
                                bool started)
{
    if (engine == NULL) {
        return;
    }
    int32_t id = attribute_named(engine, other, "Continuous Contact ID", 0) - 1;
    if (id < 0 || physics_object_index(engine, object) < 0) {
        return;
    }
    for (size_t i = 0; i < engine->physics.contacts.count; i++) {
        eng_physics_contacts_t *c = &engine->physics.contacts.data[i];
        if (!c->alive || c->entity != object || id >= c->group_count) {
            continue;
        }
        eng_contact_group_t *g = &c->groups[id];
        double now = physics_now(engine);
        if (!started) {
            if (--g->number < 0) {
                g->number = 0;
            }
            if (g->number == 0 && g->active) {
                record_add(engine, (int32_t)i, id, now); /* until the end's delay passes */
            }
            continue;
        }
        if (++g->number != 1) {
            continue;
        }
        bool resumed = false;
        for (size_t r = 0; r < engine->physics.records.count;) {
            eng_contact_record_t *record = &engine->physics.records.data[r];
            if (record->contacts != (int32_t)i || record->id != id) {
                r++;
            } else if (g->active) {
                record_remove(engine, r); /* the contact goes on: no "contact off" */
            } else if (c->delay_start < now - record->time) {
                record_remove(engine, r);
                g->active = true;
                blk_t block = {engine, c->node};
                OUT_ON(&block, 2 * id);
                resumed = true;
            } else {
                r++;
            }
        }
        if (!g->active && !resumed) {
            record_add(engine, (int32_t)i, id, now);
        }
    }
}

/* PhysicsContactManager::Process, after each step: the delays that passed */
static void contacts_process(nmo_engine_t *engine)
{
    double now = physics_now(engine);
    for (size_t r = engine->physics.records.count; r-- > 0;) {
        eng_contact_record_t record = engine->physics.records.data[r];
        eng_physics_contacts_t *c = &engine->physics.contacts.data[record.contacts];
        eng_contact_group_t *g = &c->groups[record.id];
        double elapsed = now - record.time;
        blk_t block = {engine, c->node};
        if (g->active) {
            if (g->number != 0) {
                record_remove(engine, r);
            } else if (elapsed > c->delay_end) {
                g->active = false;
                OUT_ON(&block, 2 * record.id + 1);
                record_remove(engine, r);
            }
        } else if (g->number <= 0) {
            if (c->delay_start < elapsed * 0.5) {
                record_remove(engine, r);
            }
        } else if (elapsed > c->delay_start) {
            g->active = true;
            OUT_ON(&block, 2 * record.id);
            record_remove(engine, r);
        }
    }
}

/* A contact data ended: its records dropped */
static void contacts_stop(nmo_engine_t *engine, int32_t index)
{
    engine->physics.contacts.data[index].alive = false;
    for (size_t r = 0; r < engine->physics.records.count;) {
        if (engine->physics.records.data[r].contacts == index) {
            record_remove(engine, r);
        } else {
            r++;
        }
    }
}

/* The listeners and contacts of an object no longer physical, gone with its IVP object */
static void physics_forget_listeners(nmo_engine_t *engine, nmo_object_id_t entity)
{
    for (size_t i = 0; i < engine->physics.listeners.count; i++) {
        if (engine->physics.listeners.data[i].entity == entity) {
            engine->physics.listeners.data[i].alive = false;
        }
    }
    for (size_t i = 0; i < engine->physics.contacts.count; i++) {
        if (engine->physics.contacts.data[i].entity == entity) {
            contacts_stop(engine, (int32_t)i);
        }
    }
}

/*
 * PhysicsBuoyancyCall::Call: once the target is physical, it becomes a
 * phantom with the liquid of the plane through the three surface points in
 * it, the current flowing from the start point toward the end point at its
 * strength. A missing entity input ends the call with nothing done.
 */
static bool call_buoyancy(nmo_engine_t *engine, int32_t node)
{
    blk_t block = {engine, node};
    blk_t *b = &block;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return true;
    }
    int32_t points[5];
    for (int i = 0; i < 5; i++) {
        points[i] = entity_of(engine, nmo_engine_block_get_input_object(b, i));
        if (points[i] < 0) {
            return true;
        }
    }
    nmo_engine_physics_buoyancy_t buoyancy;
    memset(&buoyancy, 0, sizeof(buoyancy));
    buoyancy.object = target;
    float strength = get_in_float(b, 5, 1.0f);
    for (int i = 0; i < 3; i++) {
        set3(buoyancy.surface[i], eng_entity_position(engine, points[i], -1));
    }
    eng_vector_t start = eng_entity_position(engine, points[3], -1);
    eng_vector_t end = eng_entity_position(engine, points[4], -1);
    double d[3] = {end.x - start.x, end.y - start.y, end.z - start.z};
    double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k < 3; k++) {
        /* IVP_U_Float_Point::normize; still water for points at the same place */
        buoyancy.current[k] = length > 0.0 ? (float)(d[k] / length * strength) : 0.0f;
    }
    buoyancy.density = get_in_float(b, 6, 998.0f);
    buoyancy.damp_factor = get_in_float(b, 7, 1.0f);
    buoyancy.friction_factor = get_in_float(b, 8, 0.05f);
    buoyancy.viscosity = get_in_float(b, 9, 0.01f);
    buoyancy.torque_factor = get_in_float(b, 10, 0.0f);
    buoyancy.suction_factor = get_in_float(b, 11, 0.1f);
    int32_t index = physics_object_index(engine, target);
    if (index < 0) {
        return false;
    }
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (provider != NULL && provider->add_buoyancy != NULL) {
        provider->add_buoyancy(provider->user_data, engine->physics.objects.data[index].handle,
                               &buoyancy);
    }
    return true;
}

/*
 * CKIpionManager::Reset (PhysicsResetCall::Call): the environment destroyed
 * and made again (default gravity, the time factor back to 0.001), every
 * physical object forgotten and the kept calls with it. The collision
 * surfaces stay.
 */
static bool call_reset(nmo_engine_t *engine)
{
    eng_physics_t *physics = &engine->physics;
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (provider != NULL && provider->reset != NULL) {
        provider->reset(provider->user_data);
    }
    for (size_t i = 0; i < physics->constraints.count; i++) {
        eng_physics_constraint_t *c = &physics->constraints.data[i];
        if (c->alive && provider != NULL && provider->destroy_constraint != NULL) {
            provider->destroy_constraint(provider->user_data, c->handle);
        }
        c->alive = false;
    }
    for (size_t i = 0; i < physics->objects.count; i++) {
        eng_physics_object_t *object = &physics->objects.data[i];
        if (provider != NULL && provider->destroy_object != NULL) {
            provider->destroy_object(provider->user_data, object->handle);
        }
        physics_free_object(object);
    }
    physics->objects.count = 0;
    physics->calls.count = 0;
    for (size_t i = 0; i < physics->listeners.count; i++) {
        physics->listeners.data[i].alive = false;
    }
    for (size_t i = 0; i < physics->contacts.count; i++) {
        contacts_stop(engine, (int32_t)i);
    }
    physics->time_factor = 0.001;
    physics->gravity[0] = 0.0f;
    physics->gravity[1] = -9.81f;
    physics->gravity[2] = 0.0f;
    return true;
}

static bool call_run(nmo_engine_t *engine, const eng_physics_call_t *call)
{
    switch (call->kind) {
    case CALL_BUOYANCY: return call_buoyancy(engine, call->node);
    case CALL_RESET: return call_reset(engine);
    case CALL_WAKE_UP: return call_wake_up(engine, call->node);
    case CALL_COLL_DETECTION: return call_coll_detection(engine, call->node);
    case CALL_CONTINUOUS_CONTACT: return call_continuous_contact(engine, call->node);
    case CALL_DELETE_SURFACES: return call_delete_surfaces(engine);
    default:
        return call_constraint(engine, call->node, (nmo_engine_physics_constraint_kind_t)call->kind);
    }
}

/* PhysicsCallManager::Process: now, or kept for the steps until it is done */
static void physics_call(nmo_engine_t *engine, int kind, int32_t node)
{
    eng_physics_call_t call = {kind, node};
    if (call_run(engine, &call)) {
        return;
    }
    eng_physics_call_t *kept = NULL;
    if (ENG_VEC_PUSH(engine->physics.calls, kept)) {
        *kept = call;
    }
}

/*
 * CKIpionManager::PostProcess: the frame time smoothed (a quarter of the
 * new one, three of the last), the kept calls tried again, then a step.
 */
void eng_physics_post_process(nmo_engine_t *engine)
{
    eng_physics_t *physics = &engine->physics;
    physics->delta = (engine->delta + physics->delta * 3.0) * 0.25;
    physics->physics_delta = physics->delta * physics->time_factor;
    size_t kept = 0;
    for (size_t i = 0; i < physics->calls.count; i++) {
        eng_physics_call_t call = physics->calls.data[i];
        if (!call_run(engine, &call)) {
            physics->calls.data[kept++] = call;
        }
    }
    physics->calls.count = kept;
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (provider != NULL && provider->simulate != NULL) {
        provider->simulate(provider->user_data, engine, (float)physics->physics_delta);
    }
    physics->time += physics->physics_delta;
    contacts_process(engine);
}

/* ============================================================================
 * Blocks
 * ============================================================================ */

static const nmo_mesh_state_t *mesh_state(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    return object != NULL ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                engine->types, object, CKPGUID_MESH)
                          : NULL;
}

/*
 * Physicalize: the target physical (Physicalize), or not any more
 * (Unphysicalize, when Physicalize is not active). A ball without a
 * collision surface name or with ball shapes, else the convex and concave
 * meshes as a polygon shape kept by that name; nothing for meshes that
 * make none ("Error: incorrect mesh"). Of the balls, the last radius
 * counts. Out1 or Out2 either way.
 */
static int bb_physicalize(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t entity = entity_of(engine, target);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t existing = physics_object_index(engine, target);
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    if (!IN_ACTIVE(b, 0)) {
        IN_OFF(b, 1);
        if (existing >= 0) {
            physics_forget_listeners(engine, target);
            eng_physics_object_t *object = &engine->physics.objects.data[existing];
            if (provider != NULL && provider->destroy_object != NULL) {
                provider->destroy_object(provider->user_data, object->handle);
            }
            physics_free_object(object);
            memmove(object, object + 1,
                    (engine->physics.objects.count - (size_t)existing - 1u) * sizeof(*object));
            engine->physics.objects.count--;
        }
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    if (existing >= 0) {
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_physics_object_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.entity = target;
    desc.fixed = get_in_int(b, 0, 0) != 0;
    desc.friction = get_in_float(b, 1, 0.4f);
    desc.elasticity = get_in_float(b, 2, 0.5f);
    desc.mass = get_in_float(b, 3, 1.0f);
    const char *group = get_in_string(b, 4);
    desc.start_frozen = get_in_int(b, 5, 0) != 0;
    desc.enable_collision = get_in_int(b, 6, 1) != 0;
    desc.automatic_mass_center = get_in_int(b, 7, 1) != 0;
    desc.linear_damping = get_in_float(b, 8, 0.1f);
    desc.rotation_damping = get_in_float(b, 9, 0.1f);
    const char *surface = get_in_string(b, 10);
    int32_t convex = get_local_int(b, 0, 1), balls = get_local_int(b, 1, 0),
            concave = get_local_int(b, 2, 0);
    convex = convex > 0 ? convex : 0;
    balls = balls > 0 ? balls : 0;
    concave = concave > 0 ? concave : 0;
    nmo_object_id_t *meshes =
        (nmo_object_id_t *)calloc((size_t)convex + (size_t)concave + 1u, sizeof(nmo_object_id_t));
    if (meshes == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    int pin = 11;
    for (int32_t i = 0; i < convex; i++) {
        meshes[i] = nmo_engine_block_get_input_object(b, pin++);
    }
    desc.ball_radius = 1.0f;
    for (int32_t i = 0; i < balls; i++) {
        desc.ball_radius = get_in_float(b, ++pin, desc.ball_radius); /* the radius after each position */
        ++pin;
    }
    for (int32_t i = 0; i < concave; i++) {
        meshes[convex + i] = nmo_engine_block_get_input_object(b, pin++);
    }
    eng_vector_t shift = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 3, &shift, sizeof(shift));
    if (!desc.automatic_mass_center) {
        set3(desc.shift_mass_center, shift);
    }
    set3(desc.scale, eng_entity_scale(engine, entity, true));

    /* CreatePhysicsObjectOnParameters */
    bool created = true;
    if (surface[0] == '\0' || balls > 0) {
        desc.shape = NMO_ENGINE_PHYSICS_BALL;
        if (surface[0] == '\0') {
            desc.enable_collision = false;
        }
    } else {
        desc.shape = NMO_ENGINE_PHYSICS_POLYGON;
        if (!physics_has_surface(engine, surface)) {
            int32_t shapes = 0;
            for (int32_t i = 0; i < convex; i++) {
                const nmo_mesh_state_t *mesh = mesh_state(engine, meshes[i]);
                shapes += mesh != NULL && mesh->vertex_count > 0 ? 1 : 0;
            }
            for (int32_t i = 0; i < concave; i++) {
                shapes += meshes[convex + i] != 0 ? 1 : 0;
            }
            char **name = NULL;
            if (shapes == 0) {
                created = false; /* "Error: incorrect mesh for %s !" */
            } else if (ENG_VEC_PUSH(engine->physics.surfaces, name)) {
                *name = dup_string(surface);
            }
        }
    }
    eng_physics_object_t *object = NULL;
    if (!created || !ENG_VEC_PUSH(engine->physics.objects, object)) {
        free(meshes);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    memset(object, 0, sizeof(*object));
    object->meshes = meshes;
    object->surface = dup_string(surface);
    object->group = dup_string(group);
    desc.convex = meshes;
    desc.convex_count = convex;
    desc.concave = meshes + convex;
    desc.concave_count = concave;
    desc.collision_surface = object->surface != NULL ? object->surface : "";
    desc.collision_group = object->group != NULL ? object->group : "";
    object->desc = desc;
    if (provider != NULL && provider->create_object != NULL) {
        provider->create_object(provider->user_data, &object->desc, &object->handle);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * The constraint blocks (Set Physics Hinge, Slider, Spring, Ball Joint,
 * and SetPhysicsForce, whose force is kept the same way):
 * Create makes the constraint, now or once both objects are physical, once
 * (Out1); Shutdown removes it (Out2).
 */
static int physics_constraint_block(blk_t *b, nmo_engine_physics_constraint_kind_t kind)
{
    int32_t *slot = block_constraint(b);
    if (slot == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        if (*slot == 0) {
            if (nmo_engine_block_target(b) == 0) {
                return NMO_ENGINE_BR_OWNERERROR;
            }
            physics_call(b->engine, (int)kind, b->node);
        }
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (*slot != 0) {
        eng_physics_constraint_t *record = &b->engine->physics.constraints.data[*slot - 1];
        const nmo_engine_physics_provider_t *provider = provider_of(b->engine);
        if (record->alive && provider != NULL && provider->destroy_constraint != NULL) {
            provider->destroy_constraint(provider->user_data, record->handle);
        }
        record->alive = false;
        *slot = 0;
    }
    IN_OFF(b, 1);
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

static int bb_physics_hinge(blk_t *b)
{
    return physics_constraint_block(b, NMO_ENGINE_PHYSICS_HINGE);
}

static int bb_physics_slider(blk_t *b)
{
    return physics_constraint_block(b, NMO_ENGINE_PHYSICS_SLIDER);
}

static int bb_physics_spring(blk_t *b)
{
    return physics_constraint_block(b, NMO_ENGINE_PHYSICS_SPRING);
}

static int bb_physics_ball_joint(blk_t *b)
{
    return physics_constraint_block(b, NMO_ENGINE_PHYSICS_BALL_JOINT);
}

static int bb_physics_force(blk_t *b)
{
    return physics_constraint_block(b, NMO_ENGINE_PHYSICS_FORCE);
}

/* TT_PhysicsCallback: a reset forgets the constraint (which stays, as in CK) */
static int cb_physics_constraint(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_RESET) {
        int32_t *slot = block_constraint(b);
        if (slot != NULL) {
            *slot = 0;
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* Physics WakeUp: the target woken, now or once it is physical. */
static int bb_physics_wake_up(blk_t *b)
{
    if (nmo_engine_block_target(b) == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    physics_call(b->engine, CALL_WAKE_UP, b->node);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* PhysicsBuoyancy: the target a phantom holding a liquid, now or once it is physical (Out; In
   stays active, as in the DLL). */
static int bb_physics_buoyancy(blk_t *b)
{
    if (nmo_engine_block_target(b) == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    physics_call(b->engine, CALL_BUOYANCY, b->node);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Physics Reset: the physics made again, every object no longer physical. */
static int bb_physics_reset(blk_t *b)
{
    physics_call(b->engine, CALL_RESET, b->node);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* DeleteCollisionSurfaces: the kept collision surfaces deleted, now or once nothing is physical. */
static int bb_delete_collision_surfaces(blk_t *b)
{
    physics_call(b->engine, CALL_DELETE_SURFACES, b->node);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Get Profiler Values: Start puts the number of physical objects in
 * HasPhysicsCalls (Out); Reset clears the manager's profiling counters,
 * which the engine does not keep (ResetOut).
 */
static int bb_get_profiler_values(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t count = (int32_t)b->engine->physics.objects.count;
    nmo_engine_block_set_output_value(b, 0, &count, sizeof(count));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Physics Globals: Set Values sets the gravity and the time factor;
 * Clean Physics World makes the environment again (its gravity the
 * default). Out either way.
 */
static int bb_set_physics_globals(blk_t *b)
{
    eng_physics_t *physics = &b->engine->physics;
    eng_vector_t gravity = {0.0f, -9.81f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, &gravity, sizeof(gravity));
    float factor = get_in_float(b, 1, 1.0f);
    const nmo_engine_physics_provider_t *provider = provider_of(b->engine);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        physics->time_factor = factor * 0.001;
        set3(physics->gravity, gravity);
        if (provider != NULL && provider->set_gravity != NULL) {
            provider->set_gravity(provider->user_data, physics->gravity);
        }
    }
    if (nmo_engine_block_input_count(b) > 1 && IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        physics->gravity[0] = 0.0f;
        physics->gravity[1] = -9.81f;
        physics->gravity[2] = 0.0f;
        if (provider != NULL && provider->reset != NULL) {
            provider->reset(provider->user_data);
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Physics Impulse: a push on the target at Position (in Referential's
 * space) along Direction (in Direction Ref's space; or toward the second
 * point, with "2 pos instead of dir"), of Impulse (per second with
 * "Constant Force"). With the target as Referential, the point and the
 * direction are the object's own. Nothing, not even Out, for a target
 * that is not a movable physical object.
 */
static int bb_physics_impulse(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool two_points = get_local_int(b, 0, 0) != 0, constant = get_local_int(b, 1, 0) != 0;
    eng_vector_t position = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t referential = nmo_engine_block_get_input_object(b, 1);
    eng_vector_t direction = get_in_vector(b, 2, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t direction_ref = nmo_engine_block_get_input_object(b, 3);
    double impulse = get_in_float(b, 4, 10.0f);
    if (constant) {
        impulse = (float)(impulse * engine->physics.physics_delta);
    }
    int32_t index = physics_object_index(engine, target);
    if (index < 0 || engine->physics.objects.data[index].desc.fixed) {
        return NMO_ENGINE_BR_OK;
    }
    eng_vector_t pos = to_world(engine, referential, position);
    double dir[3];
    if (two_points) {
        eng_vector_t to = to_world(engine, direction_ref, direction);
        dir[0] = (double)to.x - pos.x;
        dir[1] = (double)to.y - pos.y;
        dir[2] = (double)to.z - pos.z;
    } else {
        eng_vector_t v = direction;
        int32_t ref = entity_of(engine, direction_ref);
        if (ref >= 0) {
            eng_entity_transform_vector(engine, ref, &v, &direction, -1, false);
        }
        dir[0] = v.x;
        dir[1] = v.y;
        dir[2] = v.z;
    }
    double length = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (int k = 0; k < 3; k++) {
        dir[k] = length > 0.0 ? dir[k] / length * impulse : 0.0;
    }
    float push[3] = {(float)dir[0], (float)dir[1], (float)dir[2]};
    if (referential == target) {
        /* the object's own point and direction, turned into world space */
        eng_vector_t local = {push[0], push[1], push[2]}, world;
        eng_entity_transform_vector(engine, entity_of(engine, target), &world, &local, -1, false);
        set3(push, world);
    }
    float at[3];
    set3(at, pos);
    const nmo_engine_physics_provider_t *provider = provider_of(engine);
    void *handle = engine->physics.objects.data[index].handle;
    if (provider != NULL && provider->wake_up != NULL) {
        provider->wake_up(provider->user_data, handle); /* ensure_in_simulation */
    }
    if (provider != NULL && provider->push != NULL) {
        provider->push(provider->user_data, handle, at, push);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * PhysicsCollDetection: Create listens to the target's collisions (once
 * it is physical) and stays active, Collision for each one heard; Stop
 * ends it.
 */
static int bb_physics_coll_detection(blk_t *b)
{
    int32_t *slot = (int32_t *)nmo_engine_block_state(b, sizeof(int32_t));
    if (slot == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        if (*slot == 0) {
            if (nmo_engine_block_target(b) == 0) {
                return NMO_ENGINE_BR_OWNERERROR;
            }
            physics_call(b->engine, CALL_COLL_DETECTION, b->node);
        }
        IN_OFF(b, 0);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        if (*slot != 0) {
            eng_physics_listener_t *l = &b->engine->physics.listeners.data[*slot - 1];
            bool was = l->alive;
            l->alive = false;
            if (was && !physics_listened(b->engine, l->entity)) {
                physics_listen(b->engine, l->entity, false);
            }
        }
        return NMO_ENGINE_BR_OK; /* the handle stays, as the DLL leaves it */
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * PhysicsContinuousContact: Create counts the target's contacts by the
 * other objects' "Continuous Contact ID" (contact on N / contact off N)
 * and stays active; Stop ends it.
 */
static int bb_physics_continuous_contact(blk_t *b)
{
    int32_t *slot = (int32_t *)nmo_engine_block_state(b, sizeof(int32_t));
    if (slot == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        if (*slot == 0) {
            if (nmo_engine_block_target(b) == 0) {
                return NMO_ENGINE_BR_OWNERERROR;
            }
            physics_call(b->engine, CALL_CONTINUOUS_CONTACT, b->node);
        }
        IN_OFF(b, 0);
    } else if (IN_ACTIVE(b, 1)) {
        if (*slot != 0) {
            eng_physics_contacts_t *c = &b->engine->physics.contacts.data[*slot - 1];
            if (c->alive && physics_object_index(b->engine, c->entity) >= 0) {
                contacts_stop(b->engine, *slot - 1);
                if (!physics_listened(b->engine, c->entity)) {
                    physics_listen(b->engine, c->entity, false);
                }
            }
        }
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK; /* the handle stays, as the DLL leaves it */
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static const nmo_engine_block_desc_t eng_physics_blocks[] = {
    {NMO_GUID(0x7522370e, 0x37ec15ec), "Physicalize", bb_physicalize, NULL, NULL},
    {NMO_GUID(0x0c7e39bb, 0x16db20d5), "Physics Impulse", bb_physics_impulse, NULL, NULL},
    {NMO_GUID(0x38b851b5, 0x72ca74ac), "Physics WakeUp", bb_physics_wake_up, NULL, NULL},
    {NMO_GUID(0x72af347c, 0x03da71e1), "Set Physics Globals", bb_set_physics_globals, NULL, NULL},
    {NMO_GUID(0x41cd3653, 0x0de60c1d), "Set Physics Hinge", bb_physics_hinge,
     cb_physics_constraint, NULL},
    {NMO_GUID(0x2973360e, 0x23d31aa7), "Set Physics Slider", bb_physics_slider,
     cb_physics_constraint, NULL},
    {NMO_GUID(0x24a06a3a, 0x07100fce), "Set Physics Spring", bb_physics_spring,
     cb_physics_constraint, NULL},
    {NMO_GUID(0x5e624f0a, 0x35160450), "Set Physics Ball Joint", bb_physics_ball_joint,
     cb_physics_constraint, NULL},
    {NMO_GUID(0x56e20c57, 0x0b926068), "SetPhysicsForce", bb_physics_force, cb_physics_constraint,
     NULL},
    {NMO_GUID(0x7435488d, 0x201d1188), "PhysicsCollDetection", bb_physics_coll_detection,
     cb_physics_constraint, NULL},
    {NMO_GUID(0x199e4cf1, 0x545a78fe), "PhysicsContinuousContact", bb_physics_continuous_contact,
     NULL, NULL},
    {NMO_GUID(0x53bf75aa, 0x770c7021), "DeleteCollisionSurfaces", bb_delete_collision_surfaces,
     NULL, NULL},
    {NMO_GUID(0x1c8e61d1, 0x32723c6f), "Get Profiler Values", bb_get_profiler_values, NULL, NULL},
    {NMO_GUID(0x2c015f2b, 0x5b147512), "PhysicsBuoyancy", bb_physics_buoyancy, NULL, NULL},
    {NMO_GUID(0x5714345e, 0x792262ae), "Physics Reset", bb_physics_reset, NULL, NULL},
};

nmo_status_t eng_register_physics(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_physics_blocks);
}
