/**
 * @file cameras.c
 * @brief Ports of the Cameras building blocks that keep state.
 *
 * Each function does what the block's function in Cameras.dll does (the
 * reference is the Ballanced source of the blocks).
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Set As Active Camera: the render context looks through the target camera */
static int bb_set_as_active_camera(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = camera != 0 ? eng_find_object(b->engine, camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types, object,
                                                         CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    b->engine->render.camera = camera; /* AttachViewpointToCamera */
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, camera, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Clipping Planes: the camera's front and back planes */
static int bb_set_clipping_planes(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = camera != 0 ? eng_find_object(b->engine, camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types, object,
                                                         CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_set_camera_planes(b->engine, camera, get_in_float(b, 0, 0.1f), get_in_float(b, 1, 200.0f));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, camera, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set FOV stores radians verbatim, including non-finite values. */
static int bb_set_fov(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!eng_set_camera_fov(b->engine,camera,get_in_float(b,0,50.0f*3.14159265358979323846f/180))) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Set Zoom converts a focal length in millimeters into a horizontal FOV. */
static int bb_set_zoom(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float focal_length = get_in_float(b,0,50);
    if (focal_length <= 0.0f) return NMO_ENGINE_BR_PARAMETERERROR;
    if (!eng_set_camera_fov(b->engine,camera,2.0f*atanf(18.0f/focal_length))) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Orthographic Zoom advances IO before checking the target, including on errors. */
static int bb_orthographic_zoom(blk_t *b)
{
    IN_OFF(b,0);
    OUT_ON(b,0);
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float zoom = get_in_float(b,0,1);
    if (nmo_engine_block_version(b) < 0x00020000u) zoom *= 0.01f;
    if (!eng_set_camera_orthographic_zoom(b->engine,camera,zoom)) return NMO_ENGINE_BR_GENERICERROR;
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Set Projection stores the integer verbatim; the camera tests its low bit. */
static int bb_set_projection(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    if (!eng_set_camera_projection_type(b->engine,camera,(uint32_t)get_in_int(b,0,1))) return NMO_ENGINE_BR_GENERICERROR;
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Get Current Camera reports the camera attached to the main render context. */
static int bb_get_current_camera(blk_t *b)
{
    IN_OFF(b,0);
    OUT_ON(b,0);
    nmo_object_id_t camera = b->engine->render.camera;
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        camera = 0;
    }
    nmo_engine_block_set_output_value(b,0,&camera,sizeof(camera));
    return NMO_ENGINE_BR_OK;
}

/* Dolly translates a target camera along its own scaled Z axis. */
static int bb_dolly(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    int32_t entity = eng_entity_index(b->engine,camera);
    nmo_object_t *object = entity >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_TARGETCAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    eng_vector_t move = {0,0,get_in_float(b,0,1)};
    eng_entity_translate(b->engine,entity,&move,entity,get_in_int(b,1,1) == 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_set_camera_target(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL || nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_TARGETCAMERA) == NULL) return NMO_ENGINE_BR_OWNERERROR;
    IN_OFF(b,0);
    OUT_ON(b,0);
    nmo_object_id_t target = nmo_engine_block_get_input_object(b,0);
    int32_t entity = eng_entity_index(b->engine,target);
    if (nmo_engine_block_version(b) == 0x00010000u) {
        if (entity < 0) return NMO_ENGINE_BR_PARAMETERERROR;
        nmo_object_id_t previous = eng_camera_target(b->engine,camera);
        if (previous != 0) {
            eng_vector_t position = eng_entity_position(b->engine,entity,-1);
            eng_entity_set_position(b->engine,eng_entity_index(b->engine,previous),&position,-1,false);
            return NMO_ENGINE_BR_OK;
        }
    } else if (entity < 0 || nmo_engine_object_class(b->engine,target) != NMO_CID_3DENTITY) {
        target = 0; /* The modern block rejects subclasses too. */
    }
    return eng_set_camera_target(b->engine,camera,target) ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_GENERICERROR;
}


/* ============================================================================
 * Camera Orbit
 * ============================================================================ */

/* A Key setting, its default when 0 */
static uint32_t key_setting(blk_t *b, int pos, uint32_t fallback)
{
    int32_t key = get_local_int(b, pos, 0);
    return key != 0 ? (uint32_t)key : fallback;
}

static bool key_down(blk_t *b, uint32_t key)
{
    return nmo_engine_is_key_down(b->engine, key);
}

static float vector_length(const eng_vector_t *v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

/*
 * Camera Orbit: each time it runs, the target camera turns about Target (the
 * origin without one) with the arrow keys and moves nearer or farther with
 * Zoom In and Zoom Out, by a thousandth of its distance a millisecond (three
 * with Speed x2), then looks at it (a target camera's target goes there
 * instead); Distance is the new distance.
 */
static int bb_camera_orbit(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t camera = nmo_engine_block_target(b);
    int32_t entity = camera != 0 ? eng_entity_index(engine, camera) : -1;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t zoom_in = key_setting(b, 0, 0xC9), zoom_out = key_setting(b, 1, 0xD1);
    uint32_t up = key_setting(b, 2, 0xC8), down = key_setting(b, 3, 0xD0);
    uint32_t left = key_setting(b, 4, 0xCB), right = key_setting(b, 5, 0xCD);
    uint32_t fast = key_setting(b, 6, 0x36);
    int32_t focus = input_entity(b, 0);
    eng_vector_t center = focus >= 0 ? eng_entity_position(engine, focus, -1) : (eng_vector_t){0.0f, 0.0f, 0.0f};
    nmo_object_id_t target = eng_is_child_class(engine, nmo_engine_object_class(engine, camera), NMO_CID_TARGETCAMERA)
                                 ? eng_camera_target(engine, camera)
                                 : 0;
    int32_t target_entity = target != 0 ? eng_entity_index(engine, target) : -1;
    if (target_entity >= 0) {
        eng_entity_set_position(engine, target_entity, &center, -1, false);
    }
    eng_vector_t at = eng_entity_position(engine, entity, -1);
    eng_vector_t offset = {at.x - center.x, at.y - center.y, at.z - center.z};
    float d = vector_length(&offset);
    float step = (key_down(b, fast) ? 0.003f : 0.001f) * nmo_engine_block_delta_time(b) * d;
    eng_vector_t move = {0.0f, 0.0f, 0.0f};
    if (key_down(b, up)) {
        move.y = step;
    }
    if (key_down(b, down)) {
        move.y -= step;
    }
    if (key_down(b, left)) {
        move.x = -step;
    }
    if (key_down(b, right)) {
        move.x += step;
    }
    eng_entity_translate(engine, entity, &move, entity, false);
    at = eng_entity_position(engine, entity, -1);
    offset = (eng_vector_t){at.x - center.x, at.y - center.y, at.z - center.z};
    float now = vector_length(&offset);
    move = (eng_vector_t){0.0f, 0.0f, now - d};
    if (key_down(b, zoom_in)) {
        move.z += step;
    }
    if (key_down(b, zoom_out)) {
        move.z -= step;
    }
    eng_entity_translate(engine, entity, &move, entity, false);
    float distance = now - move.z;
    nmo_engine_block_set_output_value(b, 0, &distance, sizeof(distance));
    if (target_entity < 0) {
        eng_entity_look_at(engine, entity, &center, -1, false);
    }
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Keyboard, Mouse, Joystick and Generic Camera Orbit (GeneralCameraOrbit)
 * ============================================================================ */

#define ORBIT_INF 12345789.0f
#define ORBIT_TWO_PI 6.2831855f

/* Their inputs and locals: 0 Target Position, 1 Target Referential, 2 Move Speed, 3 Return
   Speed, 4-7 the horizontal and vertical limits, 8 Zoom Speed, 9 Zoom Min, 10 Zoom Max;
   locals 0 the initial angles and radius, 1 the rotation, 2 Stopping, 3 Limits, 4 Returns */
typedef void (*orbit_input_fn)(blk_t *b, float rotation[3], float delta, bool returns, int32_t *stopping);

/* Bring an angle back toward 0 by `speed` at most */
static float orbit_return(float angle, float speed)
{
    if (angle < 0.0f) {
        float gap = -angle;
        angle += speed < gap ? speed : gap;
    }
    if (angle > 0.0f) {
        angle -= speed >= angle ? angle : speed;
    }
    return angle;
}

static void orbit_returns(blk_t *b, float rotation[3], float delta)
{
    float speed = get_in_float(b, 3, 1.75f) * delta * 0.001f;
    rotation[0] = orbit_return(rotation[0], speed);
    rotation[1] = orbit_return(rotation[1], speed);
}

static float orbit_clamp(float value, float low, float high)
{
    value = value > high ? high : value;
    return value <= low ? low : value;
}

/* The move by keys or input activations: right (+X), left, down (+Y), up, as limited */
static void orbit_step(blk_t *b, float rotation[3], float delta, bool limited, bool to_max_x, bool to_min_x,
                       bool to_max_y, bool to_min_y, float limit)
{
    float speed = get_in_float(b, 2, 0.87f) * delta * 0.001f;
    if (limited) {
        float min_h = get_in_float(b, 4, -limit), max_h = get_in_float(b, 5, limit);
        float min_v = get_in_float(b, 6, -limit), max_v = get_in_float(b, 7, limit);
        if (to_max_x) {
            float gap = max_h - rotation[0];
            rotation[0] += speed < gap ? speed : gap;
        }
        if (to_min_x) {
            float gap = rotation[0] - min_h;
            rotation[0] -= speed < gap ? speed : gap;
        }
        if (to_max_y) {
            float gap = max_v - rotation[1];
            rotation[1] += speed < gap ? speed : gap;
        }
        if (to_min_y) {
            float gap = rotation[1] - min_v;
            rotation[1] -= speed < gap ? speed : gap;
        }
    } else {
        rotation[0] += to_max_x ? speed : 0.0f;
        rotation[0] -= to_min_x ? speed : 0.0f;
        rotation[1] += to_max_y ? speed : 0.0f;
        rotation[1] -= to_min_y ? speed : 0.0f;
    }
}

/* The zoom by keys or activations: in (nearer), out, by Zoom Speed a second, as limited */
static void orbit_zoom(blk_t *b, float rotation[3], float delta, bool limited, bool in, bool out)
{
    float speed = get_in_float(b, 8, 40.0f) * delta * 0.001f;
    if (limited) {
        float min_zoom = get_in_float(b, 9, -40.0f), max_zoom = get_in_float(b, 10, 10.0f);
        if (in) {
            float gap = max_zoom + rotation[2];
            rotation[2] -= speed < gap ? speed : gap;
        }
        if (out) {
            float gap = -min_zoom - rotation[2];
            rotation[2] += speed < gap ? speed : gap;
        }
    } else {
        rotation[2] -= in ? speed : 0.0f;
        rotation[2] += out ? speed : 0.0f;
    }
}

/* Keyboard Camera Orbit's keys: the arrows, Page Up and Page Down by default */
static void orbit_keyboard(blk_t *b, float rotation[3], float delta, bool returns, int32_t *stopping)
{
    bool limited = get_local_int(b, 3, 1) != 0;
    uint32_t left = key_setting(b, 5, 0xCB), right = key_setting(b, 6, 0xCD);
    uint32_t up = key_setting(b, 7, 0xC8), down = key_setting(b, 8, 0xD0);
    uint32_t zoom_in = key_setting(b, 9, 0xC9), zoom_out = key_setting(b, 10, 0xD1);
    if ((key_down(b, left) || key_down(b, right) || key_down(b, up) || key_down(b, down)) && *stopping == 0) {
        orbit_step(b, rotation, delta, limited, key_down(b, right), key_down(b, left), key_down(b, down),
                   key_down(b, up), 1.5700001f);
    } else if (returns && (rotation[0] != 0.0f || rotation[1] != 0.0f)) {
        orbit_returns(b, rotation, delta);
    }
    /* As the DLL tests it: Zoom In zooms even while stopping */
    if (key_down(b, zoom_in) || (key_down(b, zoom_out) && *stopping == 0)) {
        orbit_zoom(b, rotation, delta, limited, key_down(b, zoom_in), key_down(b, zoom_out));
    }
}

/* Mouse Camera Orbit's mouse: its move turns, its wheel zooms; Resume ends a return */
static void orbit_mouse(blk_t *b, float rotation[3], float delta, bool returns, int32_t *stopping)
{
    bool limited = get_local_int(b, 3, 1) != 0;
    float dx = 0.0f, dy = 0.0f, wheel = 0.0f;
    nmo_engine_mouse_relative(b->engine, &dx, &dy, &wheel);
    if (returns && nmo_engine_block_input_count(b) > 2 && IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        *stopping = 0;
        nmo_engine_block_set_local_value(b, 2, stopping, sizeof(*stopping));
    }
    if (*stopping == 0) {
        float speed = get_in_float(b, 2, 0.87f) * delta * 0.001f;
        rotation[0] -= dx * speed;
        rotation[1] -= dy * speed;
        if (limited) {
            rotation[0] = orbit_clamp(rotation[0], get_in_float(b, 4, -1.5700001f), get_in_float(b, 5, 1.5700001f));
            rotation[1] = orbit_clamp(rotation[1], get_in_float(b, 6, -1.5700001f), get_in_float(b, 7, 1.5700001f));
        }
    } else if (returns && (rotation[0] != 0.0f || rotation[1] != 0.0f)) {
        orbit_returns(b, rotation, delta);
    }
    if (wheel != 0.0f && *stopping == 0) {
        float speed = get_in_float(b, 8, 40.0f) * delta * 0.000099999997f;
        rotation[2] -= speed * wheel;
        if (limited) {
            rotation[2] = orbit_clamp(rotation[2], -get_in_float(b, 10, 10.0f), -get_in_float(b, 9, -40.0f));
        }
    }
}

/* Joystick Camera Orbit's stick beyond Threshold turns, two buttons zoom */
static void orbit_joystick(blk_t *b, float rotation[3], float delta, bool returns, int32_t *stopping)
{
    bool limited = get_local_int(b, 3, 1) != 0;
    int32_t joystick = get_local_int(b, 5, 0);
    nmo_engine_joystick_state_t state;
    memset(&state, 0, sizeof(state));
    nmo_engine_joystick_state(b->engine, joystick, &state);
    float inverse = get_local_int(b, 8, 0) != 0 ? -1.0f : 1.0f;
    float threshold = get_local_float(b, 9, 0.1f);
    if ((threshold >= fabsf(state.position[0]) && threshold >= fabsf(state.position[1])) || *stopping != 0) {
        if (returns && (rotation[0] != 0.0f || rotation[1] != 0.0f)) {
            orbit_returns(b, rotation, delta);
        }
    } else {
        float speed = get_in_float(b, 2, 0.87f) * delta * 0.001f;
        rotation[0] += speed * state.position[0];
        rotation[1] += speed * state.position[1] * inverse;
        if (limited) {
            rotation[0] = orbit_clamp(rotation[0], get_in_float(b, 4, -1.5700001f), get_in_float(b, 5, 1.5700001f));
            rotation[1] = orbit_clamp(rotation[1], get_in_float(b, 6, -1.5700001f), get_in_float(b, 7, 1.5700001f));
        }
    }
    int32_t in_button = get_local_int(b, 6, 1), out_button = get_local_int(b, 7, 2);
    bool in = in_button >= 0 && in_button < 32 && (state.buttons & (1u << in_button));
    bool out = out_button >= 0 && out_button < 32 && (state.buttons & (1u << out_button));
    if (in || (out && *stopping == 0)) {
        orbit_zoom(b, rotation, delta, limited, in, out);
    }
}

/* Generic Camera Orbit's inputs 2 to 7 (Move Left, Right, Up, Down, Zoom In, Zoom Out) */
static void orbit_generic(blk_t *b, float rotation[3], float delta, bool returns, int32_t *stopping)
{
    bool limited = get_local_int(b, 3, 1) != 0;
    int32_t returning = get_local_int(b, 5, 0);
    bool left = IN_ACTIVE(b, 2), right = IN_ACTIVE(b, 3), up = IN_ACTIVE(b, 4), down = IN_ACTIVE(b, 5);
    if ((left || right || up || down) && *stopping == 0) {
        if (returning) {
            returning = 0;
            set_local_int(b, 5, returning);
        }
        orbit_step(b, rotation, delta, limited, left, right, up, down, 1.5707964f);
        for (int i = 2; i <= 5; i++) {
            IN_OFF(b, i);
        }
    } else if (returns && (rotation[0] != 0.0f || rotation[1] != 0.0f)) {
        if (!returning) {
            set_local_int(b, 5, 1);
            return;
        }
        orbit_returns(b, rotation, delta);
    } else if (*stopping) {
        rotation[0] = ORBIT_INF;
    }
    bool in = IN_ACTIVE(b, 6), out = IN_ACTIVE(b, 7);
    if ((in || out) && *stopping == 0) {
        orbit_zoom(b, rotation, delta, limited, in, out);
        IN_OFF(b, 6);
        IN_OFF(b, 7);
    }
}

static void get_local_vector(blk_t *b, int pos, float out[3])
{
    nmo_engine_block_get_local_value(b, pos, out, 3u * sizeof(float));
}

static void set_local_vector(blk_t *b, int pos, const float v[3])
{
    nmo_engine_block_set_local_value(b, pos, v, 3u * sizeof(float));
}

/*
 * GeneralCameraOrbit: On takes the camera's angles and distance about Target
 * Position (in Target Referential); then each frame the input function
 * turns and zooms from there, the camera placed on that sphere looking at
 * the position (a target camera's target goes there). Off stops: with
 * Returns, Exit Off once the camera is back, else at once. `sdk` is the
 * later SDK version (Generic Camera Orbit, which Ballance's DLL has not):
 * it skips frames without angles, keeps off the poles and turns the camera
 * up its orbit instead of looking at the position.
 */
static int general_camera_orbit(blk_t *b, orbit_input_fn input, bool sdk)
{
    nmo_engine_t *engine = b->engine;
    float delta = nmo_engine_block_delta_time(b);
    bool returns = get_local_int(b, 4, 1) != 0;
    int32_t stopping = 0;
    float initial[3] = {0.0f, 0.0f, 0.0f}, rotation[3] = {0.0f, 0.0f, 0.0f};
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        get_local_vector(b, 0, initial);
        get_local_vector(b, 1, rotation);
        if (sdk) {
            stopping = 1;
            set_local_int(b, 2, stopping);
            if (!returns || (rotation[0] == 0.0f && rotation[1] == 0.0f)) {
                OUT_ON(b, 1);
                return NMO_ENGINE_BR_OK;
            }
        } else if (!returns || initial[0] == ORBIT_INF) {
            OUT_ON(b, 1);
            const float none[3] = {ORBIT_INF, ORBIT_INF, ORBIT_INF};
            set_local_vector(b, 0, none);
            return NMO_ENGINE_BR_OK;
        } else {
            set_local_int(b, 2, 1);
        }
    }
    nmo_object_id_t camera = nmo_engine_block_target(b);
    int32_t entity = camera != 0 ? eng_entity_index(engine, camera) : -1;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t center = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    int32_t referential = input_entity(b, 1);
    initial[0] = initial[1] = initial[2] = 0.0f;
    rotation[0] = rotation[1] = rotation[2] = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_vector_t at = eng_entity_position(engine, entity, referential);
        eng_vector_t p = {at.x - center.x, at.y - center.y, at.z - center.z};
        initial[2] = vector_length(&p);
        float length = vector_length(&p);
        if (length > 0.0f) {
            p = (eng_vector_t){p.x / length, p.y / length, p.z / length};
        }
        initial[1] = acosf(orbit_clamp(p.y, -1.0f, 1.0f));
        p.y = 0.0f;
        length = vector_length(&p);
        if (length > 0.0f) {
            p = (eng_vector_t){p.x / length, p.y / length, p.z / length};
        }
        initial[0] = acosf(orbit_clamp(p.x, -1.0f, 1.0f));
        if (p.z < 0.0f) {
            initial[0] = -initial[0];
        }
        eng_vector_t dir, up, right;
        eng_entity_orientation(engine, entity, &dir, &up, &right, -1);
        if (up.y < 0.0f) {
            initial[0] = (float)fmod((double)(initial[0] + 3.1415927f), (double)ORBIT_TWO_PI);
            initial[1] = -initial[1];
        }
        set_local_vector(b, 0, initial);
        set_local_vector(b, 1, rotation);
        set_local_int(b, 2, 0);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    get_local_vector(b, 0, initial);
    get_local_vector(b, 1, rotation);
    stopping = get_local_int(b, 2, 0);
    input(b, rotation, delta, returns, &stopping);
    if (sdk) {
        if (initial[0] == ORBIT_INF || rotation[0] == ORBIT_INF) {
            return NMO_ENGINE_BR_OK;
        }
        if (rotation[1] == 1.5707964f || rotation[1] == -1.5707964f) {
            float min_h = get_in_float(b, 4, -1.5707964f), max_h = get_in_float(b, 5, 1.5707964f);
            if (max_h - min_h > 2.0f * 0.0001f) {
                float sign = rotation[1] > 0.0f ? 1.0f : -1.0f;
                rotation[1] += max_h >= sign * 1.5707964f + 0.0001f ? 0.0001f : -0.0001f;
            }
        }
    }
    rotation[0] = (float)fmod((double)rotation[0], (double)ORBIT_TWO_PI);
    rotation[1] = (float)fmod((double)rotation[1], (double)ORBIT_TWO_PI);
    set_local_vector(b, 1, rotation);
    float x = initial[0] + rotation[0], y = initial[1] + rotation[1], r = initial[2] + rotation[2];
    float cy = cosf(y), sxy = sinf(x) * sinf(y), cxy = cosf(x) * sinf(y);
    eng_vector_t position = {cxy * r + center.x, cy * r + center.y, sxy * r + center.z};
    eng_entity_set_position(engine, entity, &position, referential, false);
    nmo_object_id_t target = eng_is_child_class(engine, nmo_engine_object_class(engine, camera), NMO_CID_TARGETCAMERA)
                                 ? eng_camera_target(engine, camera)
                                 : 0;
    int32_t target_entity = target != 0 ? eng_entity_index(engine, target) : -1;
    if (target_entity >= 0) {
        eng_entity_set_position(engine, target_entity, &center, referential, true);
    } else if (!sdk) {
        eng_entity_look_at(engine, entity, &center, referential, false);
    } else {
        eng_vector_t dir = {center.x - position.x, center.y - position.y, center.z - position.z};
        float angle = initial[0] + rotation[0];
        /* (0, 0, -1) turned about Y by the horizontal angle (Vx3DMatrixFromRotationAndOrigin) */
        eng_vector_t turned;
        eng_matrix_t m;
        eng_vector_t axis = {0.0f, 1.0f, 0.0f}, z = {0.0f, 0.0f, -1.0f};
        eng_matrix_from_rotation(&m, &axis, angle);
        eng_vector_rotate(&turned, &m, &z);
        eng_vector_t up = eng_vector_cross(&turned, &dir);
        eng_entity_set_orientation(engine, entity, &dir, &up, NULL, referential, false);
    }
    if (stopping && rotation[0] == 0.0f && rotation[1] == 0.0f) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int bb_keyboard_camera_orbit(blk_t *b) { return general_camera_orbit(b, orbit_keyboard, false); }

static int bb_mouse_camera_orbit(blk_t *b) { return general_camera_orbit(b, orbit_mouse, false); }

static int bb_joystick_camera_orbit(blk_t *b) { return general_camera_orbit(b, orbit_joystick, false); }

static int bb_generic_camera_orbit(blk_t *b) { return general_camera_orbit(b, orbit_generic, true); }

/* The camera orbits' load: no angles yet (the SDK's clears the rotation too) */
static int cb_camera_orbit(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        const float none[3] = {ORBIT_INF, ORBIT_INF, ORBIT_INF};
        set_local_vector(b, 0, none);
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_generic_camera_orbit(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        const float none[3] = {ORBIT_INF, ORBIT_INF, ORBIT_INF}, zero[3] = {0.0f, 0.0f, 0.0f};
        set_local_vector(b, 0, none);
        set_local_vector(b, 1, zero);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Vertigo (the DLL's): In measures the Focal Point's depth from the camera
 * and the size of the view at it; each Loop In the camera moves along its Z
 * by the Progression Curve's step of Distortion Amount of that depth
 * (backward the amount's odds), and its field of view widens or narrows to
 * keep that size, for Number of Frames frames.
 */
static int bb_vertigo(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t camera = nmo_engine_block_target(b);
    int32_t entity = camera != 0 ? eng_entity_index(engine, camera) : -1;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t steps = get_local_int(b, 0, 0);
    if (IN_ACTIVE(b, 0)) {
        steps = get_in_int(b, 1, 100);
        int32_t focus = input_entity(b, 0);
        if (focus < 0) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        eng_vector_t at = eng_entity_position(engine, focus, entity);
        float amount = get_in_float(b, 3, 0.5f);
        float depth = at.z;
        if (get_in_int(b, 2, 1) == 0) {
            depth = -depth;
            amount = amount / (1.0f - amount);
        }
        float going[3] = {amount * 0.0f, amount * 0.0f, depth * amount};
        float fov = 0.5f;
        eng_camera_fov(engine, camera, &fov);
        float size = (float)(tan((double)fov * 0.5) * sqrt((double)(depth * depth)));
        float step = (float)(1.0 / (double)steps);
        float last = 0.0f;
        set_local_int(b, 0, steps);
        nmo_engine_block_set_local_value(b, 1, &size, sizeof(size));
        nmo_engine_block_set_local_value(b, 2, &last, sizeof(last));
        nmo_engine_block_set_local_value(b, 3, &step, sizeof(step));
        nmo_engine_block_set_local_value(b, 4, going, sizeof(going));
        IN_OFF(b, 0);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 1);
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 4, &curve_size);
    float size = get_local_float(b, 1, 0.0f), last = get_local_float(b, 2, 0.0f), step = get_local_float(b, 3, 0.0f);
    float going[3] = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 4, going, sizeof(going));
    float rise = nmo_engine_curve_y(curve, curve_size, step + last) - nmo_engine_curve_y(curve, curve_size, last);
    eng_vector_t move = {going[0] * rise, going[1] * rise, going[2] * rise};
    eng_entity_translate(engine, entity, &move, entity, false);
    int32_t focus = input_entity(b, 0);
    if (focus < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t at = eng_entity_position(engine, focus, entity);
    float half = (float)atan2((double)(size / fabsf(at.z)), 1.0);
    eng_set_camera_fov(engine, camera, half + half);
    steps--;
    set_local_int(b, 0, steps);
    last = step + last;
    nmo_engine_block_set_local_value(b, 2, &last, sizeof(last));
    OUT_ON(b, steps != 0 ? 1 : 0);
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, camera, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Camera Color Filter
 * ============================================================================ */

/* The Camera Color Filter blocks whose post render callback the next frame runs */
typedef struct cameras_render {
    ENG_VEC(int32_t) filters;
} cameras_render_t;

static void cameras_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args);
static void cameras_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    free(((cameras_render_t *)state)->filters.data);
}

static const eng_manager_desc_t cameras_manager = {
    .name = "Camera Color Filter",
    .state_size = sizeof(cameras_render_t),
    .destroy = cameras_destroy,
    .render = cameras_render,
};

/*
 * Camera Color Filter: each frame until Off, a temporary post render
 * callback colors the view through the target camera.
 */
static int bb_camera_color_filter(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    cameras_render_t *state = (cameras_render_t *)eng_manager_state(b->engine, &cameras_manager);
    int32_t *slot = NULL;
    if (state != NULL && ENG_VEC_PUSH(state->filters, slot)) {
        *slot = b->node;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* RGBAFTOCOLOR(const VxColor *): each channel times 255, truncated, in [0, 255] */
static uint32_t vxcolor_argb(const float c[4])
{
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] * 255.0f;
        uint32_t byte = !(v > -2147483648.0f && v < 2147483648.0f) ? 0u
                        : v >= 255.0f                               ? 255u
                        : v > 0.0f                                  ? (uint32_t)(int32_t)v
                                                                    : 0u;
        argb |= byte << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/*
 * The filter's callback: through the block's target camera, the view
 * covered in Filter Color, blended by its alpha of Density with Hard Light,
 * else added Density times, no Z.
 */
static void cameras_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args)
{
    cameras_render_t *filters = (cameras_render_t *)state;
    if (stage != ENG_RENDER_OVERLAY) {
        return;
    }
    for (size_t i = 0; i < filters->filters.count; i++) {
        nmo_engine_block_t handle = {engine, filters->filters.data[i]};
        blk_t *b = &handle;
        if (engine->render.camera == 0 || nmo_engine_block_target(b) != engine->render.camera) {
            continue;
        }
        float color[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
        float density = get_in_float(b, 1, 20.0f);
        bool hard = get_in_int(b, 2, 0) != 0;
        nmo_engine_draw_t *draw = NULL;
        if (!ENG_VEC_PUSH(engine->render.draws, draw)) {
            break;
        }
        memset(draw, 0, sizeof(*draw));
        draw->kind = NMO_ENGINE_DRAW_QUAD;
        draw->rect[2] = args->width;
        draw->rect[3] = args->height;
        if (hard) {
            color[3] = density;
            draw->source_blend = VXBLEND_SRCALPHA;
            draw->dest_blend = VXBLEND_INVSRCALPHA;
        } else {
            color[3] = 1.0f;
            color[0] *= density, color[1] *= density, color[2] *= density;
            draw->source_blend = VXBLEND_ONE;
            draw->dest_blend = VXBLEND_ONE;
        }
        draw->color = draw->color2 = vxcolor_argb(color);
        draw->blend = true;
    }
    filters->filters.count = 0; /* temporary callbacks */
}

static const nmo_engine_block_desc_t eng_cameras_blocks[] = {
    {NMO_GUID_INIT(0x74123741, 0x4563ffff), "Set Camera Target", bb_set_camera_target, NULL, NULL},
    {NMO_GUID_INIT(0xb98b83bb, 0x87787878), "Dolly", bb_dolly, NULL, NULL},
    {NMO_GUID_INIT(0x368f22b1, 0x222957e4), "Get Current Camera", bb_get_current_camera, NULL, NULL},
    {NMO_GUID_INIT(0xe444e666, 0x4eee6eee), "Set Projection", bb_set_projection, NULL, NULL},
    {NMO_GUID_INIT(0x0a125555, 0x0eee5488), "Orthographic Zoom", bb_orthographic_zoom, NULL, NULL},
    {NMO_GUID_INIT(0x40540a0d, 0x000aaaaa), "Set Zoom", bb_set_zoom, NULL, NULL},
    {NMO_GUID_INIT(0xaaaacccc, 0xaaaacccc), "Set FOV", bb_set_fov, NULL, NULL},
    {NMO_GUID_INIT(0x652316a2, 0x01aa09a9), "Set Clipping Planes", bb_set_clipping_planes, NULL, NULL},
    {NMO_GUID_INIT(0x368f0ab1, 0x2d8957e4), "Set As Active Camera", bb_set_as_active_camera, NULL, NULL},
    {NMO_GUID_INIT(0x777d999e, 0xdef777d8), "Camera Orbit", bb_camera_orbit, NULL, NULL},
    {NMO_GUID_INIT(0x07610f4d, 0x69747b9d), "Keyboard Camera Orbit", bb_keyboard_camera_orbit, cb_camera_orbit,
     NULL},
    {NMO_GUID_INIT(0x2356342f, 0x9542674f), "Mouse Camera Orbit", bb_mouse_camera_orbit, cb_camera_orbit, NULL},
    {NMO_GUID_INIT(0x400f0e6f, 0x72822162), "Joystick Camera Orbit", bb_joystick_camera_orbit, cb_camera_orbit,
     NULL},
    {NMO_GUID_INIT(0x104d0913, 0x766563dd), "Generic Camera Orbit", bb_generic_camera_orbit,
     cb_generic_camera_orbit, NULL},
    {NMO_GUID_INIT(0xb00d010a, 0xc00d010a), "Vertigo", bb_vertigo, NULL, NULL},
    {NMO_GUID_INIT(0x00cd010b, 0x00dd010b), "Camera Color Filter", bb_camera_color_filter, NULL, NULL},
};

nmo_status_t eng_register_cameras(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &cameras_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_cameras_blocks) : status;
}
