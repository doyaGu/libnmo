/**
 * @file gravity.c
 * @brief Ports of the TT Gravity building blocks (Ballance's own).
 *
 * Each function does what the block's function in TT_Gravity_RT.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../../engine_block_util.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <string.h>

/* TT_GetSoundProperties: the gain, pitch and pan of the target sound. */
static int bb_get_sound_properties(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_target(b));
    if (sound == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_engine_block_set_output_value(b, 0, &sound->gain, sizeof(sound->gain));
    nmo_engine_block_set_output_value(b, 1, &sound->pitch, sizeof(sound->pitch));
    nmo_engine_block_set_output_value(b, 2, &sound->pan, sizeof(sound->pan));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Sky: On makes the sky, a prism of side faces (and a top and a bottom)
 * around the camera, drawn first without the depth buffer, with a field of
 * view widened by Distortion. Each frame while active it takes the material
 * inputs and Vertex Color and shows; Off hides it. Reset or the script's
 * deactivation destroys it.
 */
static eng_sky_t *sky_of(nmo_engine_t *engine, int32_t node)
{
    for (size_t i = 0; i < engine->render.skies.count; i++) {
        if (engine->render.skies.data[i].node == node) {
            return &engine->render.skies.data[i];
        }
    }
    return NULL;
}

/* The sky's mesh as Sky() builds it */
static eng_mesh_t *sky_make_mesh(blk_t *b, int32_t sides, bool top, bool bottom)
{
    nmo_engine_t *engine = b->engine;
    float radius = get_in_float(b, 3, 70.0f);
    bool quadratic = get_in_int(b, 4, 1) != 0;
    float side_height = quadratic ? 0.0f : get_in_float(b, 5, 0.0f);
    float sky_y = get_in_float(b, 6, 0.0f);
    const float pi = 3.14159265358979f;
    if (quadratic) {
        float step = 2.0f * pi / (float)sides;
        float cr = cosf(step) * radius, sr = sinf(step) * radius;
        side_height = sqrtf(sr * sr + (radius - cr) * (radius - cr));
    }
    int32_t side_vertices = 4 + (top ? 3 : 0) + (bottom ? 3 : 0);
    int32_t side_faces = 2 + (top ? 1 : 0) + (bottom ? 1 : 0);
    nmo_object_id_t id = eng_create_object(engine, NMO_CID_MESH, "TT_Sky_Mesh");
    eng_mesh_t *mesh = NULL;
    if (id == 0 || !ENG_VEC_PUSH(engine->render.meshes, mesh)) {
        return NULL;
    }
    mesh->id = id;
    mesh->vertex_count = sides * side_vertices;
    mesh->face_count = sides * side_faces;
    mesh->material_count = sides + (top ? 1 : 0) + (bottom ? 1 : 0);
    mesh->vertices = (nmo_vertex_t *)calloc((size_t)mesh->vertex_count, sizeof(nmo_vertex_t));
    mesh->colors = (uint32_t *)calloc((size_t)mesh->vertex_count, sizeof(uint32_t));
    mesh->indices = (uint16_t *)calloc((size_t)mesh->face_count * 3u, sizeof(uint16_t));
    mesh->faces = (nmo_face_t *)calloc((size_t)mesh->face_count, sizeof(nmo_face_t));
    mesh->materials = (nmo_object_id_t *)calloc((size_t)mesh->material_count, sizeof(nmo_object_id_t));
    mesh->prelit = true;
    if (mesh->vertices == NULL || mesh->colors == NULL || mesh->indices == NULL ||
        mesh->faces == NULL || mesh->materials == NULL) {
        return mesh;
    }
    const float r = sinf(pi / 4.0f);
    nmo_vertex_t *vx = mesh->vertices;
    for (int32_t side = 0; side < sides; side++) {
        float step = 2.0f * pi / (float)sides;
        float bpi = step * (float)side + 1.25f * pi;
        float api = step * (float)(side + 1) + 1.25f * pi;
        float cb = cosf(bpi) * radius, ca = cosf(api) * radius;
        float sb = sinf(bpi) * radius, sa = sinf(api) * radius;
        float yt = sky_y + side_height * 0.5f, yb = sky_y - side_height * 0.5f;
        const float quad[4][5] = {{cb, yb, sb, 1.0f, 1.0f}, {ca, yb, sa, 0.0f, 1.0f},
                                  {ca, yt, sa, 0.0f, 0.0f}, {cb, yt, sb, 1.0f, 0.0f}};
        for (int k = 0; k < 4; k++, vx++) {
            vx->position.x = quad[k][0];
            vx->position.y = quad[k][1];
            vx->position.z = quad[k][2];
            vx->uv.x = quad[k][3];
            vx->uv.y = quad[k][4];
        }
        float ib = 1.0f / sqrtf(sb * sb + cb * cb), ia = 1.0f / sqrtf(sa * sa + ca * ca);
        const float cap[2][3][5] = {
            {{cb, yt, sb, cb * ib * r + 0.5f, -sb * ib * r + 0.5f},
             {ca, yt, sa, ca * ia * r + 0.5f, -sa * ia * r + 0.5f},
             {0.0f, yt, 0.0f, 0.5f, 0.5f}},
            {{cb, yb, sb, cb * ib * r + 0.5f, -sb * ib * r + 0.5f},
             {0.0f, yb, 0.0f, 0.5f, 0.5f},
             {ca, yb, sa, ca * ia * r + 0.5f, -sa * ia * r + 0.5f}}};
        for (int c = 0; c < 2; c++) {
            if (c == 0 ? !top : !bottom) {
                continue;
            }
            for (int k = 0; k < 3; k++, vx++) {
                vx->position.x = cap[c][k][0];
                vx->position.y = cap[c][k][1];
                vx->position.z = cap[c][k][2];
                vx->uv.x = cap[c][k][3];
                vx->uv.y = cap[c][k][4];
            }
        }
    }
    /* SetFaceVertexIndex (c, a, b) and (c, d, a) a side, then (a, b, c) a cap */
    int32_t a = 0, bb = 1, c = 2, d = 3, f = 0;
    for (int32_t side = 0; side < sides; side++) {
        const int32_t faces[2][3] = {{c, a, bb}, {c, d, a}};
        for (int k = 0; k < 2; k++, f++) {
            memcpy(&mesh->indices[f * 3], (uint16_t[3]){(uint16_t)faces[k][0], (uint16_t)faces[k][1],
                                                         (uint16_t)faces[k][2]},
                   3 * sizeof(uint16_t));
            mesh->faces[f].material_group_idx = (uint32_t)side;
        }
        a += 4, bb += 4, c += 4, d += 4;
        for (int cap = 0; cap < 2; cap++) {
            if (cap == 0 ? !top : !bottom) {
                continue;
            }
            mesh->indices[f * 3] = (uint16_t)a;
            mesh->indices[f * 3 + 1] = (uint16_t)bb;
            mesh->indices[f * 3 + 2] = (uint16_t)c;
            mesh->faces[f].material_group_idx = (uint32_t)(sides + (cap == 1 && top ? 1 : 0));
            f++;
            a += 3, bb += 3, c += 3, d += 3;
        }
    }
    return mesh;
}

static int bb_sky(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t sides = get_local_int(b, 0, 4);
    bool top = get_local_int(b, 1, 0) != 0;
    bool bottom = get_local_int(b, 2, 0) != 0;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_sky_t *sky = sky_of(engine, b->node);
        if (sky == NULL && !ENG_VEC_PUSH(engine->render.skies, sky)) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        sky->node = b->node;
        float distortion = get_in_float(b, 0, 0.3f);
        sky->distortion = distortion > 1.0f ? 1.0f : distortion;
        sky->orientation = nmo_engine_block_get_input_object(b, 2);
        sky->shown = false;
        eng_mesh_t *mesh = sides > 0 ? sky_make_mesh(b, sides, top, bottom) : NULL;
        sky = sky_of(engine, b->node);
        sky->mesh = mesh != NULL ? mesh->id : 0;
        eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    eng_sky_t *sky = sky_of(engine, b->node);
    eng_mesh_t *mesh = sky != NULL ? eng_render_mesh(engine, sky->mesh) : NULL;
    if (mesh == NULL || mesh->materials == NULL || mesh->colors == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    for (int32_t m = 0; m < mesh->material_count; m++) {
        mesh->materials[m] = nmo_engine_block_get_input_object(b, 7 + m);
    }
    float color[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 1, color, sizeof(color));
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = color[i] < 0.0f ? 0.0f : color[i] > 1.0f ? 1.0f : color[i];
        argb |= (uint32_t)(v * 255.0f + 0.5f) << (i == 3 ? 24 : 16 - 8 * i);
    }
    for (int32_t v = 0; v < mesh->vertex_count; v++) {
        mesh->colors[v] = argb;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        sky->shown = false;
        return NMO_ENGINE_BR_OK;
    }
    sky->shown = true;
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_sky(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        eng_sky_t *sky = sky_of(b->engine, b->node);
        if (sky != NULL) {
            sky->shown = false; /* CKDestroyObject(ent) */
            sky->node = -1;
        }
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * The skies' pre render callback: the sky at the camera, turned as its
 * orientation object (or not), its projection's field of view widened from
 * the camera's, near 1 and far 200.
 */
void eng_draw_skies(nmo_engine_t *engine, const float view[16])
{
    const eng_render_t *render = &engine->render;
    const nmo_camera_state_t *camera =
        render->camera != 0
            ? (const nmo_camera_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                  engine->types, eng_find_object(engine, render->camera), CKPGUID_CAMERA)
            : NULL;
    float current_fov = camera != NULL ? camera->fov : 0.5f;
    const float pi = 3.14159265358979f;
    for (size_t i = 0; i < render->skies.count; i++) {
        eng_sky_t sky = render->skies.data[i];
        if (!sky.shown || sky.mesh == 0) {
            continue;
        }
        nmo_engine_draw_t *draw = eng_render_push(engine);
        if (draw == NULL) {
            return;
        }
        draw->kind = NMO_ENGINE_DRAW_MESH;
        draw->mesh = sky.mesh;
        int32_t orientation = sky.orientation != 0 ? eng_entity_index(engine, sky.orientation) : -1;
        if (orientation >= 0) {
            memcpy(draw->world, engine->entities.data[orientation].world.m, sizeof(draw->world));
        } else {
            draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
        }
        draw->world[12] = view[12];
        draw->world[13] = view[13];
        draw->world[14] = view[14];
        draw->no_z_test = draw->no_z_write = true;
        draw->fov = current_fov + sky.distortion * (pi - current_fov);
        draw->near_plane = 1.0f;
        draw->far_plane = 200.0f;
    }
}

/*
 * TT_TextureSine: the target mesh's UVs of Channel (-1: the base UVs)
 * swayed around the UVs saved in local 0 (when the block was attached),
 * each by X/Y Amplitude times its distance from 0.5, along a wave of the
 * vertex's place in the mesh plus the time (local 1) times Velocity; the
 * time wraps at a period. A channel the mesh lacks is a parameter error
 * (Out all the same).
 */
static int bb_texture_sine(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    float velocity = get_in_float(b, 2, 1.0f);
    nmo_object_id_t mesh = nmo_engine_block_target(b);
    nmo_object_t *object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    const nmo_mesh_state_t *state =
        object != NULL ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_MESH)
                       : NULL;
    if (state == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t channel = get_in_int(b, 3, -1);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    if (channel < -1 || channel >= (int32_t)state->material_channel_count) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, mesh, 0);
    float time = 0.0f;
    nmo_engine_block_get_local_value(b, 1, &time, sizeof(time));
    const eng_node_t *node = block_node(b);
    const eng_param_t *saved = eng_node_param(engine, node->first_local, node->local_count, 0);
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, mesh);
    nmo_engine_mesh_t desc;
    if (saved != NULL && saved->data != NULL && copy != NULL &&
        nmo_engine_mesh(engine, mesh, &desc)) {
        int32_t count = desc.vertex_count;
        if ((size_t)count > saved->size / (2u * sizeof(float))) {
            count = (int32_t)(saved->size / (2u * sizeof(float)));
        }
        float *uvs = NULL; /* a channel's, two floats a vertex; the base ones are in the vertices */
        if (channel >= 0 && (size_t)channel < copy->channels.count) {
            nmo_engine_channel_t *ch = &copy->channels.data[channel];
            if (ch->uvs == NULL) {
                ch->uvs = calloc((size_t)desc.vertex_count * 2u, sizeof(float));
            }
            uvs = (float *)ch->uvs;
        }
        float x_amplitude = get_in_float(b, 0, 0.1f), y_amplitude = get_in_float(b, 1, 0.1f);
        for (int32_t v = 0; v < count && (channel < 0 || uvs != NULL); v++) {
            float su = 0.0f, sv = 0.0f;
            memcpy(&su, saved->data + (size_t)v * 8u, sizeof(su));
            memcpy(&sv, saved->data + (size_t)v * 8u + 4u, sizeof(sv));
            double angle = ((double)v / (double)desc.vertex_count - 0.5) * 4.0 + time * velocity;
            float u = (float)(cos(angle) * (0.5 - su) * x_amplitude + su);
            float w = (float)(sin(angle) * (0.5 - sv) * y_amplitude + sv);
            if (channel < 0) {
                copy->vertices[v].uv.x = u;
                copy->vertices[v].uv.y = w;
            } else {
                uvs[v * 2] = u;
                uvs[v * 2 + 1] = w;
            }
        }
        copy->version = ++engine->render.mesh_version;
    }
    time = (float)((double)engine->delta * 0.001 + (double)time);
    if ((double)time * (double)velocity > 6.2831855) {
        time = (float)((double)time - 6.2831855 / (double)velocity);
    }
    nmo_engine_block_set_local_value(b, 1, &time, sizeof(time));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT ProximityVolumeControl: while on, the Sound's gain follows how near
 * Object #2 is to Object #1: full within Near-Distance, nothing past
 * Far-Distance, linear in the squared distance between, as CalcGain makes
 * it (0.02 * 50^x), into CKWaveSound::SetGain.
 * Off stops it (Off out); On starts it (On out).
 */
static int bb_proximity_volume_control(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t on = 0;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        nmo_engine_block_set_local_value(b, 0, &on, sizeof(on));
    } else if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        on = 1;
        nmo_engine_block_set_local_value(b, 0, &on, sizeof(on));
    } else {
        on = get_local_int(b, 0, 0) & 0xFF;
    }
    nmo_object_id_t object1 = nmo_engine_block_get_input_object(b, 0);
    nmo_object_id_t object2 = nmo_engine_block_get_input_object(b, 1);
    if (object1 == 0 || object2 == 0 || eng_entity_index(engine, object1) < 0 ||
        eng_entity_index(engine, object2) < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t pos = eng_entity_position(engine, eng_entity_index(engine, object2),
                                           eng_entity_index(engine, object1));
    float distance = pos.z * pos.z + pos.y * pos.y + pos.x * pos.x;
    float near_distance = get_in_float(b, 2, 0.0f), far_distance = get_in_float(b, 3, 10.0f);
    near_distance *= near_distance;
    far_distance *= far_distance;
    float x = 0.0f;
    if (distance < far_distance) {
        x = distance > near_distance
                ? (float)(1.0 - (double)(distance - near_distance) / (far_distance - near_distance))
                : 1.0f;
    }
    eng_sound_t *sound = eng_sound(engine, nmo_engine_block_get_input_object(b, 4));
    if (sound == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    /* CalcGain */
    sound->gain = x > 1.0f ? 1.0f : x >= 0.0f ? (float)(pow(50.0, (double)x) * 0.02) : 0.0f;
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
    if (on != 0) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT SpeedOMeter: the target's speed since the last run, in units a second
 * (the frame's time times Loop-Framedelay above 1), and with "Calculate
 * relative speed?" where it is between Minimum and Maximum Speed, clamped to
 * [0, 1]. As TT_Gravity_RT.dll has it, nothing guards a zero time or range.
 */
static int bb_speedometer(blk_t *b)
{
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool relative = (get_local_int(b, 1, 0) & 0xFF) != 0; /* CKBOOL, read by its low byte */
    int32_t frames = get_local_int(b, 2, 1);
    float delta = nmo_engine_block_delta_time(b);
    if (frames > 1) {
        delta = (float)((double)frames * delta);
    }
    eng_vector_t last = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 0, &last, sizeof(last));
    eng_vector_t pos = eng_entity_position(b->engine, entity, -1);
    float dx = pos.x - last.x, dy = pos.y - last.y, dz = pos.z - last.z;
    float speed = (float)(sqrt((double)(dz * dz + dy * dy + dx * dx)) * 1000.0 / delta);
    nmo_engine_block_set_output_value(b, 0, &speed, sizeof(speed));
    if (relative) {
        float min = get_in_float(b, 0, 0.0f), max = get_in_float(b, 1, 0.0f);
        double ratio = (double)(speed - min) / (double)(max - min);
        /* x87: a NaN is not <= 1, so it reads as 1 */
        speed = ratio <= 1.0 ? (ratio < 0.0 ? 0.0f : (float)ratio) : 1.0f;
        nmo_engine_block_set_output_value(b, 1, &speed, sizeof(speed));
    }
    nmo_engine_block_set_local_value(b, 0, &pos, sizeof(pos));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Extra: an extra-point pickup, as TT_Gravity_RT.dll has it. The target's
 * children name its parts after their 14th character ("Floor", the shadow;
 * "Ball0", the center) and its small balls after their 18th ("1" to "8").
 * Idle, the small balls circle the target, each about its own axis, until
 * the ball comes within ActivationDistance; then they fly away from where
 * the ball was for FlyAwayTime, then home in on it, each hit hiding one and
 * moving the next object of HitFrameGroup there. Showing and hiding are
 * effect events; the parts' scripts are activated and deactivated in the
 * scene. The engine has one scene, so the DLL's "no scene" errors cannot
 * happen.
 */
#define EXTRA_MAX_BALLS 8

typedef struct extra_ball {
    nmo_object_id_t ball;
    eng_vector_t position; /* where it was last */
    bool flying;           /* not hit yet */
    float force;
} extra_ball_t;

/* The SmallBallList local: the DLL keeps a linked list behind a pointer */
typedef struct extra_state {
    int32_t count; /* 0 for no list */
    extra_ball_t balls[EXTRA_MAX_BALLS];
} extra_state_t;

enum { EXTRA_IDLE = 0, EXTRA_AWAY = 1, EXTRA_HOMING = 2, EXTRA_READY = 3, EXTRA_NONE = -1 };

static void extra_show(blk_t *b, nmo_object_id_t id, bool show)
{
    if (id != 0) {
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, id, show ? 1 : 0);
    }
}

/* CKBeObject::GetScript(0) */
static nmo_object_id_t extra_script(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = id != 0 ? eng_map_get(&engine->object_map, id) : -1;
    if (index < 0 || engine->objects.data[index].script_count == 0) {
        return 0;
    }
    return engine->nodes.data[*eng_index(engine, engine->objects.data[index].first_script)].id;
}

/* CKScene::Activate (with a reset) or DeActivate of an object's first script */
static void extra_script_active(nmo_engine_t *engine, nmo_object_id_t id, bool active)
{
    nmo_object_id_t script = extra_script(engine, id);
    if (script != 0) {
        nmo_engine_activate_script(engine, script, active, active);
    }
}

/* CK3dEntity::GetChild(0); 0 for none */
static nmo_object_id_t extra_first_child(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine, id);
    int32_t child = index >= 0 ? engine->entities.data[index].first_child : -1;
    return child >= 0 ? engine->entities.data[child].id : 0;
}

/* GetChildOfObject: the first child whose name has `name` from character `at` on */
static nmo_object_id_t extra_child(nmo_engine_t *engine, nmo_object_id_t id, size_t at,
                                   const char *name)
{
    int32_t index = eng_entity_index(engine, id);
    for (int32_t c = index >= 0 ? engine->entities.data[index].first_child : -1; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        const char *child = nmo_engine_object_name(engine, engine->entities.data[c].id);
        size_t length = child != NULL ? strlen(child) : 0u;
        /* GetChildOfObject compares as many characters as `name` has: copies' numbered names match */
        if (length >= at && strncmp(child + at, name, strlen(name)) == 0) {
            return engine->entities.data[c].id;
        }
    }
    return 0;
}

static eng_vector_t extra_position(nmo_engine_t *engine, nmo_object_id_t id, int32_t ref)
{
    eng_vector_t zero = {0.0f, 0.0f, 0.0f};
    int32_t index = eng_entity_index(engine, id);
    return index >= 0 ? eng_entity_position(engine, index, ref) : zero;
}

static void extra_set_position(nmo_engine_t *engine, nmo_object_id_t id, const eng_vector_t *p,
                               int32_t ref)
{
    int32_t index = eng_entity_index(engine, id);
    if (index >= 0) {
        eng_entity_set_position(engine, index, p, ref, false);
    }
}

static float extra_distance2(const eng_vector_t *a, const eng_vector_t *b)
{
    return (b->z - a->z) * (b->z - a->z) + (b->y - a->y) * (b->y - a->y) +
           (b->x - a->x) * (b->x - a->x);
}

static nmo_object_id_t extra_local_object(blk_t *b, int pos)
{
    uint32_t id = 0;
    nmo_engine_block_get_local_value(b, pos, &id, sizeof(id));
    return id;
}

static void extra_set_local_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

static void extra_set_local_float(blk_t *b, int pos, float value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

static bool extra_flying(blk_t *b)
{
    return (get_in_int(b, 12, 0) & 0xFF) != 0;
}

/* The list's balls' scripts deactivated, the balls hidden, the list dropped (sub_10001F10) */
static void extra_free_list(blk_t *b, extra_state_t *state)
{
    for (int32_t i = 0; i < state->count; i++) {
        extra_script_active(b->engine, extra_first_child(b->engine, state->balls[i].ball), false);
        extra_show(b, state->balls[i].ball, false);
    }
    state->count = 0;
}

/* sub_10001EC0: the list freed, not initialized, no status */
static void extra_uninitialize(blk_t *b, extra_state_t *state)
{
    extra_free_list(b, state);
    extra_set_local_int(b, 5, 0);
    extra_set_local_int(b, 4, EXTRA_NONE);
}

static int extra_initialize(blk_t *b, extra_state_t *state)
{
    nmo_engine_t *engine = b->engine;
    int32_t status = get_local_int(b, 4, EXTRA_NONE);
    if (status == EXTRA_NONE) {
        state->count = 0;
    } else if (status == EXTRA_AWAY) {
        extra_uninitialize(b, state);
    } else if (state->count > 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t count = get_in_int(b, 1, 6);
    if (count <= 0) {
        count = 1;
    }
    extra_set_local_int(b, 7, count);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_entity_index(engine, target) < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool flying = extra_flying(b);
    nmo_object_id_t floor = extra_child(engine, target, 14, "Floor");
    nmo_object_id_t center = extra_child(engine, target, 14, "Ball0");
    if ((!flying && floor == 0) || center == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "There is no Floorglowobject in hirachy!" */
    }
    nmo_engine_block_set_local_value(b, 2, &floor, sizeof(floor));
    nmo_engine_block_set_local_value(b, 3, &center, sizeof(center));
    if (floor != 0) {
        nmo_engine_activate_object(engine, floor, false);
        extra_show(b, floor, false);
    }
    extra_script_active(engine, extra_first_child(engine, center), false);
    extra_show(b, center, false);
    float force = get_in_float(b, 7, 0.06f);
    float width = get_in_float(b, 9, 0.06f);
    width = (float)(width / (double)count);
    force = force - width;
    extra_state_t list;
    memset(&list, 0, sizeof(list));
    for (int32_t i = 0; i < count; i++) {
        static const char *const names[EXTRA_MAX_BALLS] = {"1", "2", "3", "4", "5", "6", "7", "8"};
        nmo_object_id_t ball =
            i < EXTRA_MAX_BALLS ? extra_child(engine, target, 18, names[i]) : 0;
        if (ball == 0) {
            return NMO_ENGINE_BR_PARAMETERERROR; /* "Missing one or more Billboards." */
        }
        nmo_object_id_t frame = extra_first_child(engine, ball);
        extra_script_active(engine, frame, false);
        extra_show(b, ball, false);
        extra_ball_t *small = &list.balls[list.count++];
        small->ball = ball;
        small->position = extra_position(engine, ball, -1);
        small->flying = true;
        force = width + force;
        small->force = force;
        if (frame == 0 || extra_script(engine, frame) == 0) {
            return NMO_ENGINE_BR_PARAMETERERROR; /* "There is no Frame..." / no script */
        }
        nmo_engine_activate_script(engine, extra_script(engine, frame), false, false);
    }
    *state = list;
    extra_set_local_float(b, 6, get_in_float(b, 10, 1000.0f));
    extra_set_local_int(b, 5, 1);
    extra_set_local_int(b, 4, EXTRA_IDLE);
    return NMO_ENGINE_BR_OK;
}

static int extra_activate(blk_t *b, extra_state_t *state)
{
    if (state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool flying = extra_flying(b);
    nmo_object_id_t shadow = extra_local_object(b, 2), center = extra_local_object(b, 3);
    extra_show(b, center, true);
    if (shadow != 0 && !flying) {
        extra_show(b, shadow, true);
    }
    for (int32_t i = 0; i < state->count; i++) {
        nmo_object_id_t frame = extra_first_child(b->engine, state->balls[i].ball);
        extra_script_active(b->engine, frame, true);
        extra_show(b, state->balls[i].ball, true);
        extra_script_active(b->engine, frame, true);
    }
    return NMO_ENGINE_BR_OK;
}

/* Idle: the small balls circle the target until the ball comes near */
static int extra_idle(blk_t *b, extra_state_t *state)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t ball = input_entity(b, 0);
    if (ball < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t ball_pos = eng_entity_position(engine, ball, -1);
    float activation = get_in_float(b, 2, 2.0f);
    activation = activation * activation;
    eng_vector_t target_pos = eng_entity_position(engine, target, -1);
    if (extra_distance2(&target_pos, &ball_pos) > activation) {
        float speed = get_in_float(b, 4, 5.0f) * get_local_float(b, 8, 0.0f);
        if (state->count == 0) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        /* Each ball's axis; from the seventh on, the sixth's */
        static const eng_vector_t axes[6] = {
            {0.0f, 1.0f, 0.0f},      {0.0f, 0.0f, 1.0f},          {1.0f, 0.0f, 0.0f},
            {0.5f, 0.5f, 0.70700002f}, {0.70700002f, -0.70700002f, 0.0f}, {-0.5f, -0.5f, 0.70700002f},
        };
        for (int32_t i = 0; i < state->count; i++) {
            const eng_vector_t *axis = &axes[i < 6 ? i : 5];
            eng_vector_t local = extra_position(engine, state->balls[i].ball, target);
            eng_matrix_t rotation;
            eng_matrix_from_rotation(&rotation, axis, speed);
            eng_vector_t p;
            eng_vector_rotate(&p, &rotation, &local);
            double length = sqrt((double)(p.z * p.z + p.y * p.y + p.x * p.x));
            if (length != 2.0) {
                /* back on the sphere of radius 2 */
                double inverse = 1.0 / length;
                float x = (float)(inverse * p.x), y = (float)(p.y * inverse);
                float z = (float)(inverse * p.z + inverse * p.z);
                p.x = x + x;
                p.y = y + y;
                p.z = z;
            }
            extra_set_position(engine, state->balls[i].ball, &p, target);
        }
        return NMO_ENGINE_BR_OK;
    }
    extra_set_local_int(b, 4, EXTRA_AWAY);
    bool flying = extra_flying(b);
    nmo_object_id_t shadow = extra_local_object(b, 2), center = extra_local_object(b, 3);
    if (!flying && shadow != 0) {
        extra_show(b, shadow, false);
    }
    if (center != 0) {
        nmo_object_id_t frame = extra_first_child(engine, center);
        extra_script_active(engine, frame, false);
        extra_show(b, center, false);
        if (frame != 0) {
            extra_show(b, frame, true);
            extra_script_active(engine, frame, true);
        }
    }
    if (state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    for (int32_t i = 0; i < state->count; i++) {
        state->balls[i].position = extra_position(engine, state->balls[i].ball, -1);
    }
    int32_t activated = 1;
    nmo_engine_block_set_output_value(b, 0, &activated, sizeof(activated));
    OUT_ON(b, 0);
    ball_pos.y = (float)(ball_pos.y - 2.0999999);
    nmo_engine_block_set_local_value(b, 10, &ball_pos, sizeof(ball_pos));
    return NMO_ENGINE_BR_OK;
}

/* Away: for FlyAwayTime the small balls fly from below where the ball was */
static int extra_away(blk_t *b, extra_state_t *state)
{
    eng_vector_t away = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 10, &away, sizeof(away));
    away.y = (float)(away.y - 3.0);
    float left = (float)(get_local_float(b, 6, 0.0f) - nmo_engine_block_delta_time(b));
    if (!(left > 0.0f)) {
        extra_set_local_int(b, 4, EXTRA_HOMING);
        return NMO_ENGINE_BR_OK;
    }
    extra_set_local_float(b, 6, left);
    float force = get_in_float(b, 5, 50.0f);
    float damping = get_in_float(b, 6, 0.5f);
    force = get_local_float(b, 8, 0.0f) * force;
    if (state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    for (int32_t i = 0; i < state->count; i++) {
        extra_ball_t *small = &state->balls[i];
        eng_vector_t old = small->position;
        eng_vector_t pos = extra_position(b->engine, small->ball, -1);
        small->position = pos;
        eng_vector_t next = {(pos.x - old.x) * damping + (pos.x - away.x) * force + pos.x,
                             (pos.y - old.y) * damping + (pos.y - away.y) * force + pos.y,
                             (pos.z - old.z) * damping + (pos.z - away.z) * force + pos.z};
        extra_set_position(b->engine, small->ball, &next, -1);
    }
    return NMO_ENGINE_BR_OK;
}

/* Homing: the small balls fly to the ball, each one near enough a hit */
static int extra_homing(blk_t *b, extra_state_t *state)
{
    nmo_engine_t *engine = b->engine;
    int32_t remaining = get_local_int(b, 7, 0);
    if (remaining <= 0) {
        extra_set_local_int(b, 4, EXTRA_READY);
        OUT_ON(b, 2);
        return NMO_ENGINE_BR_OK;
    }
    int32_t ball = input_entity(b, 0);
    if (ball < 0 || state->count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t ball_pos = eng_entity_position(engine, ball, -1);
    float damping = get_in_float(b, 8, 0.9f);
    float time = get_local_float(b, 8, 0.0f);
    int32_t hits = 0;
    for (int32_t i = 0; i < state->count; i++) {
        extra_ball_t *small = &state->balls[i];
        if (!small->flying) {
            continue;
        }
        eng_vector_t pos = extra_position(engine, small->ball, -1);
        float collision = get_in_float(b, 3, 2.0f);
        /* The squared distance against the distance itself, as the DLL compares them */
        if (extra_distance2(&pos, &ball_pos) <= collision) {
            hits++;
            small->flying = false;
            extra_set_local_int(b, 7, --remaining);
            nmo_object_id_t frame = extra_first_child(engine, small->ball);
            if (frame != 0) {
                extra_script_active(engine, frame, false);
            }
            extra_show(b, small->ball, false);
            nmo_object_id_t group = nmo_engine_block_get_input_object(b, 11);
            nmo_object_id_t hit = group != 0 ? nmo_engine_group_object(engine, group, i) : 0;
            if (hit != 0) {
                eng_vector_t at = extra_position(engine, small->ball, -1);
                extra_set_position(engine, hit, &at, -1);
                extra_script_active(engine, hit, true);
            }
        } else {
            eng_vector_t old = small->position;
            small->position = pos;
            eng_vector_t next = {
                (ball_pos.x - pos.x) * small->force * time + (pos.x - old.x) * damping + pos.x,
                (ball_pos.y - pos.y) * small->force * time + (pos.y - old.y) * damping + pos.y,
                (ball_pos.z - pos.z) * small->force * time + (pos.z - old.z) * damping + pos.z};
            extra_set_position(engine, small->ball, &next, -1);
        }
    }
    if (hits > 0) {
        nmo_engine_block_set_output_value(b, 0, &hits, sizeof(hits));
        OUT_ON(b, 1);
    }
    return NMO_ENGINE_BR_OK;
}

/* DestroyExtra (Off): the parts hidden and their scripts off; the list stays */
static void extra_destroy(blk_t *b, extra_state_t *state)
{
    if (state->count == 0) {
        return;
    }
    bool flying = extra_flying(b);
    nmo_object_id_t shadow = extra_local_object(b, 2), center = extra_local_object(b, 3);
    if (center != 0) {
        extra_script_active(b->engine, extra_first_child(b->engine, center), false);
        extra_show(b, center, false);
    }
    if (shadow != 0 && !flying) {
        extra_show(b, shadow, false);
    }
    for (int32_t i = 0; i < state->count; i++) {
        extra_script_active(b->engine, extra_first_child(b->engine, state->balls[i].ball), false);
        extra_show(b, state->balls[i].ball, false);
    }
}

static int bb_tt_extra(blk_t *b)
{
    extra_state_t *state = (extra_state_t *)nmo_engine_block_state(b, sizeof(extra_state_t));
    if (state == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    bool active = false;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        extra_set_local_int(b, 0, 0);
        extra_destroy(b, state);
        return NMO_ENGINE_BR_OK;
    }
    bool initialized = (get_local_int(b, 5, 0) & 0xFF) != 0;
    bool exactness = (get_local_int(b, 11, 1) & 0xFF) != 0;
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        if (!initialized) {
            /* "Extra is'nt intialized yet, this could slow all down." */
            if (extra_initialize(b, state) != NMO_ENGINE_BR_OK) {
                return NMO_ENGINE_BR_PARAMETERERROR;
            }
            initialized = true;
        }
        active = true;
        extra_set_local_int(b, 0, 1);
        extra_set_local_float(b, 8, (float)(nmo_engine_block_delta_time(b) * 0.001));
        if (exactness) {
            extra_set_local_int(b, 9, get_in_int(b, 13, 2));
        }
        if (extra_activate(b, state) != NMO_ENGINE_BR_OK) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
    } else if (nmo_engine_block_is_input_active(b, 2)) {
        IN_OFF(b, 2);
        if (!initialized) {
            return extra_initialize(b, state) == NMO_ENGINE_BR_OK
                       ? NMO_ENGINE_BR_OK
                       : NMO_ENGINE_BR_PARAMETERERROR;
        }
    } else {
        active = (get_local_int(b, 0, 0) & 0xFF) != 0;
    }
    if (!active || !initialized) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (exactness) {
        /* Every Exactness Frame frames, the frame's time is the one the speeds use */
        uint32_t counter = (uint32_t)get_local_int(b, 9, 0) - 1u;
        if ((counter & 0x7FFFFFFFu) == 0) { /* read as a float: 0.0 or -0.0 */
            extra_set_local_float(b, 8, (float)(nmo_engine_block_delta_time(b) * 0.001));
            counter = (uint32_t)get_in_int(b, 13, 2);
        }
        extra_set_local_int(b, 9, (int32_t)counter);
    }
    int status = NMO_ENGINE_BR_OK;
    switch (get_local_int(b, 4, EXTRA_NONE)) {
    case EXTRA_IDLE: status = extra_idle(b, state); break;
    case EXTRA_AWAY: status = extra_away(b, state); break;
    case EXTRA_HOMING: status = extra_homing(b, state); break;
    default: break;
    }
    /* ExecuteExtra maps any failure to a parameter error ("Could'nt execute") */
    return status == NMO_ENGINE_BR_OK ? NMO_ENGINE_BR_ACTIVATENEXTFRAME
                                      : NMO_ENGINE_BR_PARAMETERERROR;
}

/*
 * TT_ExtraCallback: a reset, a deactivated script or a removed block takes
 * an initialized Extra down; a load or an attachment starts without a list.
 */
static int cb_tt_extra(blk_t *b, uint32_t message)
{
    extra_state_t *state = (extra_state_t *)nmo_engine_block_state(b, sizeof(extra_state_t));
    if (state == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        if ((get_local_int(b, 5, 0) & 0xFF) != 0) {
            extra_uninitialize(b, state);
        }
    } else if (message == NMO_ENGINE_MSG_LOAD) {
        state->count = 0;
        extra_set_local_int(b, 5, 0);
        extra_set_local_int(b, 0, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Simple Shadow: On makes the shadow's material (white, its texture
 * copied and clamped); while active, each frame the render callback projects
 * it on the floors (objects with the Floor attribute) under the target, as a
 * material channel multiplying them (ZERO, SRCCOLOR) with UVs from the
 * target's position over each floor. Off, reset or the script's deactivation
 * take it off the floors.
 */
static eng_shadow_t *shadow_of(nmo_engine_t *engine, int32_t node)
{
    for (size_t i = 0; i < engine->render.shadows.count; i++) {
        if (engine->render.shadows.data[i].node == node) {
            return &engine->render.shadows.data[i];
        }
    }
    return NULL;
}

/* The floor's mesh copy, where the shadow's channel goes */
static eng_mesh_copy_t *shadow_floor_mesh(nmo_engine_t *engine, nmo_object_id_t floor)
{
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, eng_find_object(engine, floor), CKPGUID_3DENTITY);
    return state != NULL ? eng_mesh_copy(engine, eng_object_ref(engine, floor, &state->current_mesh))
                         : NULL;
}

/* A_Delete_SoftShadow_From_Floors */
static void shadow_remove(nmo_engine_t *engine, eng_shadow_t *shadow)
{
    for (int32_t f = 0; f < shadow->floor_count; f++) {
        eng_mesh_copy_t *mesh = shadow_floor_mesh(engine, shadow->floors[f]);
        if (mesh != NULL) {
            eng_mesh_remove_channel(mesh, eng_mesh_channel(mesh, shadow->material));
            mesh->version = ++engine->render.mesh_version;
        }
    }
    shadow->floor_count = 0;
}

static void shadow_reset(nmo_engine_t *engine, int32_t node)
{
    eng_shadow_t *shadow = shadow_of(engine, node);
    if (shadow != NULL) {
        shadow_remove(engine, shadow);
        shadow->node = -1;
        shadow->ran = 0;
    }
}

static int32_t floor_attribute(const nmo_engine_t *engine)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, "Floor") == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

/* TTSimpleShadowRenderCallback, with A_GetFloors */
static void shadow_update(nmo_engine_t *engine, eng_shadow_t *shadow, int32_t node)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t entity = eng_entity_index(engine, shadow->entity);
    eng_material_t *material = eng_material(engine, shadow->material);
    if (entity < 0 || material == NULL) {
        return;
    }
    material->desc.texture = shadow->texture = nmo_engine_block_get_input_object(b, 0);
    float zoom = get_in_float(b, 1, 2.0f);
    float max_height = get_in_float(b, 2, 100.0f);
    eng_vector_t min, max, wmin, wmax;
    eng_entity_local_box(engine, entity, &min, &max);
    eng_vector_t axis = {engine->entities.data[entity].world.m[0][0],
                         engine->entities.data[entity].world.m[0][1],
                         engine->entities.data[entity].world.m[0][2]};
    float width = (max.x - min.x) * eng_vector_magnitude(&axis);
    if (zoom == 0.0f || width == 0.0f) {
        return;
    }
    float inv_zoom = 1.0f / (zoom * width);
    eng_entity_world_box(engine, entity, &wmin, &wmax);
    float get_inv_zoom = 1.0f / (zoom * (wmax.x - wmin.x));
    nmo_object_id_t old[ENG_SHADOW_FLOORS];
    int32_t old_count = shadow->floor_count;
    memcpy(old, shadow->floors, sizeof(old));
    eng_vector_t positions[ENG_SHADOW_FLOORS], scales[ENG_SHADOW_FLOORS];
    int32_t count = 0;
    int32_t type = floor_attribute(engine);
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        int32_t floor = attribute->type == type ? eng_entity_index(engine, attribute->object) : -1;
        if (floor < 0 || engine->entities.data[floor].destroyed ||
            !nmo_engine_object_is_visible(engine, attribute->object)) {
            continue;
        }
        eng_vector_t fmin, fmax;
        eng_entity_world_box(engine, floor, &fmin, &fmax);
        if (fmin.x > wmax.x || fmin.z > wmax.z || fmax.x < wmin.x || fmax.z < wmin.z ||
            fmin.y > wmax.y || wmin.y - fmax.y > max_height) {
            continue;
        }
        eng_vector_t pos = eng_entity_position(engine, entity, floor);
        eng_vector_t lmin, lmax;
        eng_entity_local_box(engine, floor, &lmin, &lmax);
        eng_vector_t scale = eng_entity_scale(engine, floor, true);
        float tx = get_inv_zoom * scale.x, tz = get_inv_zoom * scale.z;
        if ((lmax.x - pos.x) * tx >= -0.5f && (lmin.x - pos.x) * tx <= 0.5f &&
            (lmax.z - pos.z) * tz >= -0.5f && (lmin.z - pos.z) * tz <= 0.5f &&
            count < ENG_SHADOW_FLOORS) {
            positions[count] = pos;
            scales[count] = scale;
            shadow->floors[count++] = attribute->object;
        }
    }
    shadow->floor_count = count;
    /* Off the floors it left */
    for (int32_t a = 0; a < old_count; a++) {
        bool kept = false;
        for (int32_t f = 0; f < count && !kept; f++) {
            kept = old[a] == shadow->floors[f];
        }
        eng_mesh_copy_t *mesh = kept ? NULL : shadow_floor_mesh(engine, old[a]);
        if (mesh != NULL) {
            eng_mesh_remove_channel(mesh, eng_mesh_channel(mesh, shadow->material));
            mesh->version = ++engine->render.mesh_version;
        }
    }
    /* On the floors under it, its UVs from the object's place over each */
    for (int32_t f = 0; f < count; f++) {
        eng_mesh_copy_t *mesh = shadow_floor_mesh(engine, shadow->floors[f]);
        nmo_engine_mesh_t desc;
        if (mesh == NULL || !nmo_engine_mesh(engine, mesh->mesh, &desc)) {
            continue;
        }
        int32_t channel = eng_mesh_channel(mesh, shadow->material);
        if (channel < 0) {
            channel = eng_mesh_add_channel(mesh, shadow->material);
            if (channel < 0) {
                continue;
            }
            mesh->channels.data[channel].source_blend = VXBLEND_ZERO;
            mesh->channels.data[channel].dest_blend = VXBLEND_SRCCOLOR;
        }
        nmo_engine_channel_t *ch = &mesh->channels.data[channel];
        float *uvs = (float *)ch->uvs;
        if (uvs == NULL) {
            uvs = (float *)malloc((size_t)desc.vertex_count * 2u * sizeof(float));
            ch->uvs = uvs;
        }
        float tx = inv_zoom * scales[f].x, tz = inv_zoom * scales[f].z;
        float u0 = positions[f].x * tx + 0.5f, v0 = positions[f].z * tz + 0.5f;
        for (int32_t v = 0; uvs != NULL && v < desc.vertex_count; v++) {
            uvs[v * 2] = u0 - mesh->vertices[v].position.x * tx;
            uvs[v * 2 + 1] = v0 - mesh->vertices[v].position.z * tz;
        }
        mesh->version = ++engine->render.mesh_version;
    }
}

void eng_update_shadows(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->render.shadows.count; i++) {
        eng_shadow_t *shadow = &engine->render.shadows.data[i];
        if (shadow->node >= 0 && shadow->ran == engine->frame) {
            shadow_update(engine, shadow, shadow->node);
        }
    }
}

static int bb_simple_shadow(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        shadow_reset(engine, b->node);
        eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        shadow_reset(engine, b->node);
        nmo_object_id_t id = eng_create_object(engine, NMO_CID_MATERIAL, "TT_SimpleShadow Material");
        eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
        if (material == NULL) {
            material = eng_material_new(engine, id);
        }
        eng_shadow_t *shadow = shadow_of(engine, -1);
        if (material == NULL || (shadow == NULL && !ENG_VEC_PUSH(engine->render.shadows, shadow))) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f}, black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        memcpy(material->desc.emissive, white, sizeof(white));
        memcpy(material->desc.diffuse, white, sizeof(white));
        memcpy(material->desc.specular, black, sizeof(black));
        memcpy(material->desc.ambient, black, sizeof(black));
        material->desc.address_mode = VXTEXTURE_ADDRESSCLAMP;
        material->desc.texture_blend = VXTEXTUREBLEND_COPY;
        memset(shadow, 0, sizeof(*shadow));
        shadow->node = b->node;
        shadow->material = id;
        eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 1);
    }
    eng_shadow_t *shadow = shadow_of(engine, b->node);
    if (shadow != NULL) {
        shadow->entity = target;
        shadow->ran = engine->frame + 1; /* AddPreRenderCallBack(..., TRUE) for this frame */
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_simple_shadow(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        shadow_reset(b->engine, b->node);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_tt_gravity_blocks[] = {
    {NMO_GUID(0x30fa6a70, 0x3b3f728a), "TT_GetSoundProperties", bb_get_sound_properties, NULL,
     NULL},
    {NMO_GUID(0x36691920, 0x3b261630), "TT Sky", bb_sky, cb_sky, NULL},
    {NMO_GUID(0x51bd5521, 0x672c67bf), "TT SpeedOMeter", bb_speedometer, NULL, NULL},
    {NMO_GUID(0x009c1208, 0x3a8d779e), "TT_TextureSine", bb_texture_sine, NULL, NULL},
    {NMO_GUID(0x38571b61, 0x64cd2174), "TT ProximityVolumeControl", bb_proximity_volume_control,
     NULL, NULL},
    {NMO_GUID(0x7f0517c2, 0x52cc76bb), "TT Simple Shadow", bb_simple_shadow, cb_simple_shadow,
     NULL},
    {NMO_GUID(0x36106bd9, 0x51813906), "TT Extra", bb_tt_extra, cb_tt_extra, NULL},
};

/* TT Sky's sky behind the 3D entities, TT Simple Shadow's floors before the draws */
static void gravity_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args)
{
    (void)state;
    if (stage == ENG_RENDER_PREPARE) {
        eng_update_shadows(engine);
    } else if (stage == ENG_RENDER_BACKGROUND) {
        eng_draw_skies(engine, args->view);
    }
}

static const eng_manager_desc_t gravity_manager = {
    .name = "TT Gravity Render",
    .render = gravity_render,
};

nmo_status_t eng_register_tt_gravity(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &gravity_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_tt_gravity_blocks) : status;
}
