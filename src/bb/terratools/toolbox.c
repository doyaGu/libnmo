/**
 * @file toolbox.c
 * @brief Ports of the TT Toolbox building blocks.
 *
 * Each function does what the block's function in TT_Toolbox_RT.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). What they ask the machine for comes from a fixed machine: Windows,
 * no video memory, in debug mode once TT_DebugON turned it on. The
 * environment, current directory and paths come from the host.
 */

#include "../bb_sdk.h"
#include "terratools.h"
#include "type/nmo_type_query.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_3dentity_schemas.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "../../engine/engine_curve.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * TT Toolbox and TT Interface Manager
 * ============================================================================ */

/*
 * TT_Debug: with Enable, in debug mode, writes its text to the console and
 * the log file (an effect) and takes its input; otherwise In stays active.
 * Exit either way.
 */
static int bb_tt_debug(blk_t *b)
{
    if (get_in_int(b, 3, 0) != 0 && eng_tt(b->engine)->debug_mode) {
        IN_OFF(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT_DebugON: turns the debug manager's debug mode on. */
static int bb_debug_on(blk_t *b)
{
    eng_tt(b->engine)->debug_mode = true;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT GetEnvironmentVariable: the variable's value (Found), or Not Found and a parameter error. */
static int bb_get_environment_variable(blk_t *b)
{
    IN_OFF(b, 0);
    const char *name = input_string(b, 0);
    const eng_registry_value_t *entry = name != NULL ? eng_environment_find(b->engine, name) : NULL;
    if (entry == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    set_out(b, 0, entry->value, strlen(entry->value) + 1u);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT_GetCurrentDirectory: the current directory (OK), or empty (Fail); its input stays active. */
static int bb_get_current_directory(blk_t *b)
{
    const char *directory = b->engine->host.current_directory;
    if (directory == NULL || directory[0] == '\0') {
        set_out(b, 0, "", 1u);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    set_out(b, 0, directory, strlen(directory) + 1u);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ReplacePath: the first path of category PathIDX (0 bitmap, 1 data,
 * 2 sound) that is not Current Directory + Old renamed Current Directory +
 * New (Ok). The original compares that way round: Failed only when every
 * path is the old one, there is none, or the category is another. Its
 * input stays active.
 */
static int bb_replace_path(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    const char *old_path = input_string(b, 0);
    const char *new_path = input_string(b, 1);
    const char *directory = input_string(b, 2);
    int32_t category = get_in_int(b, 3, 0);
    if (category < 0 || category >= ENG_PATH_CATEGORIES) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    directory = directory != NULL ? directory : "";
    old_path = old_path != NULL ? old_path : "";
    new_path = new_path != NULL ? new_path : "";
    size_t directory_length = strlen(directory);
    size_t old_length = strlen(old_path);
    for (size_t i = 0; i < engine->host.paths[category].count; i++) {
        const char *path = engine->host.paths[category].data[i];
        if (strlen(path) == directory_length + old_length &&
            memcmp(path, directory, directory_length) == 0 &&
            memcmp(path + directory_length, old_path, old_length) == 0) {
            continue;
        }
        size_t new_length = strlen(new_path);
        char *renamed = (char *)malloc(directory_length + new_length + 1u);
        if (renamed == NULL) {
            return NMO_ENGINE_BR_GENERICERROR;
        }
        memcpy(renamed, directory, directory_length);
        memcpy(renamed + directory_length, new_path, new_length + 1u);
        free(engine->host.paths[category].data[i]);
        engine->host.paths[category].data[i] = renamed;
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_SetMipMapping: the target material's texture uses MipMap Count levels
 * (CKTexture::UseMipmap), and the material takes Filter Min and Filter Mag.
 */
static int bb_set_mip_mapping(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    eng_material_t *material = eng_material(b->engine, nmo_engine_block_target(b));
    if (material != NULL && material->desc.texture != 0) {
        eng_texture_use_mipmap(b->engine, material->desc.texture, get_in_int(b, 2, 1));
        material->desc.min_filter = get_in_int(b, 0, 7); /* VXTEXTUREFILTER_ANISOTROPIC */
        material->desc.mag_filter = get_in_int(b, 1, 7);
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_TextureInfo: the driver's video memory (none), the textures, and those
 * in video memory (TT_PreloadTextures); the engine holds no surfaces, so
 * the memory they use reads 0 MB.
 */
static int bb_texture_info(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(b->engine, NMO_CID_TEXTURE, &ids);
    int32_t in_video = 0;
    for (size_t i = 0; i < count; i++) {
        if (eng_map_get(&eng_tt(b->engine)->video_textures, ids[i]) >= 0) {
            in_video++;
        }
    }
    free(ids);
    const int32_t max_video_memory = 0, textures = (int32_t)count;
    const float no_memory = 0.0f;
    set_out(b, 0, &max_video_memory, sizeof(max_video_memory));
    set_out(b, 1, &textures, sizeof(textures));
    set_out(b, 2, &no_memory, sizeof(no_memory));
    set_out(b, 3, &in_video, sizeof(in_video));
    set_out(b, 4, &no_memory, sizeof(no_memory));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_LinearVolume: a volume in [0, 1] made linear, 0.02 * 50^x: 1 above 1,
 * 0 at 0.01 or below (and for a NaN, as the x87 compares it).
 */
static int bb_linear_volume(blk_t *b)
{
    IN_OFF(b, 0);
    float x = 0.0f;
    nmo_engine_block_get_input_value(b, 0, &x, sizeof(x));
    float linear = 0.0f;
    if (x > 1.0f) {
        linear = 1.0f;
    } else if ((double)x > 0.01) { /* against the double 0.01 */
        linear = (float)(pow(50.0, (double)x) * 0.02);
    }
    nmo_engine_block_set_output_value(b, 0, &linear, sizeof(linear));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ReflectionMapping: each frame the target's current mesh gets the UVs of
 * the camera's view reflected at each vertex (its base UVs, whatever the
 * MatChannel input says, as the DLL does): Off ends it (Exit Off), On
 * reports it (Exit On), and it stays active while the target has a mesh.
 */
static int bb_reflection_mapping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    }
    int32_t entity = target != 0 ? eng_entity_index(engine, target) : -1;
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, eng_find_object(engine, target), CKPGUID_3DENTITY);
    eng_mesh_copy_t *mesh =
        state != NULL ? eng_mesh_copy(engine, eng_object_ref(engine, target, &state->current_mesh)) : NULL;
    nmo_engine_mesh_t desc;
    if (mesh == NULL || !nmo_engine_mesh(engine, mesh->mesh, &desc)) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t camera = input_entity(b, 1);
    eng_vector_t eye = camera >= 0 ? eng_entity_position(engine, camera, entity)
                                   : (eng_vector_t){0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < desc.vertex_count; i++) {
        nmo_vertex_t *v = &mesh->vertices[i];
        eng_vector_t e = {eye.x - v->position.x, eye.y - v->position.y, eye.z - v->position.z};
        eng_vector_normalize(&e);
        float d = 2.0f * (v->normal.x * e.x + v->normal.y * e.y + v->normal.z * e.z);
        eng_vector_t r = {d * v->normal.x - e.x, d * v->normal.y - e.y, d * v->normal.z - e.z};
        eng_vector_normalize(&r);
        v->uv.x = (r.x + 1.0f) * 0.5f;
        v->uv.y = (r.z + 1.0f) * 0.5f;
    }
    mesh->version = ++engine->render.mesh_version;
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}


/*
 * TT_PreloadTextures: each texture not in video memory put there; the
 * count of those moved. The engine's driver takes every texture, so a
 * texture counts the first time.
 */
static int bb_preload_textures(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t *ids = NULL;
    size_t count = eng_objects_of_class(b->engine, NMO_CID_TEXTURE, &ids);
    int32_t moved = 0;
    for (size_t i = 0; i < count; i++) {
        if (eng_map_get(&eng_tt(b->engine)->video_textures, ids[i]) < 0 &&
            eng_map_put(&eng_tt(b->engine)->video_textures, ids[i], 1)) {
            moved++;
        }
    }
    free(ids);
    nmo_engine_block_set_output_value(b, 0, &moved, sizeof(moved));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Set_2DSprite: its 2D entity in homogeneous coordinates, at Position and
 * Size (each its own while not an input of the "Add Input" setting), with
 * the UV-Rect as its source rect when that is an input.
 */
#define SPRITE_INPUT_UV 1
#define SPRITE_INPUT_SIZE 2
#define SPRITE_INPUT_POSITION 4

static int bb_set_2d_sprite(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    eng_2d_t *entity = eng_2d_entity(b->engine, nmo_engine_block_target(b));
    if (entity == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t inputs = get_local_int(b, 0, 0);
    int pos = 0;
    bool homogeneous = (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) != 0;
    float position[2] = {0.0f, 0.0f}, size[2] = {0.0f, 0.0f};
    if (inputs & SPRITE_INPUT_POSITION) {
        nmo_engine_block_get_input_value(b, pos++, position, sizeof(position));
    } else if (homogeneous) {
        position[0] = entity->homogeneous.left;
        position[1] = entity->homogeneous.top;
    }
    if (inputs & SPRITE_INPUT_SIZE) {
        nmo_engine_block_get_input_value(b, pos++, size, sizeof(size));
    } else if (homogeneous) {
        size[0] = entity->homogeneous.right - entity->homogeneous.left;
        size[1] = entity->homogeneous.bottom - entity->homogeneous.top;
    }
    if (inputs & SPRITE_INPUT_UV) {
        nmo_engine_block_get_input_value(b, pos++, &entity->source, sizeof(entity->source));
    }
    /* SetHomogeneousCoordinates, then SetHomogeneousRect */
    nmo_rect_t rect = {position[0], position[1], position[0] + size[0], position[1] + size[1]};
    entity->flags |= CK_2DENTITY_USEHOMOGENEOUSCOORD;
    entity->homogeneous = rect;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, entity->id, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT OperationSystem: a Windows of the VXOS_WIN31..VXOS_WINSEVEN range reads as 2, 0, 6. */
static int bb_operation_system(blk_t *b)
{
    const int32_t id = 2, minor = 0, major = 6;
    set_out(b, 0, &id, sizeof(id));
    set_out(b, 1, &minor, sizeof(minor));
    set_out(b, 2, &major, sizeof(major));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT GetMemoryStatus: the fixed machine's memory, in megabytes. */
static int bb_get_memory_status(blk_t *b)
{
    const int32_t load = 50;
    const float total = 4096.0f, available = 2048.0f, virtual_total = 2048.0f,
                virtual_available = 1536.0f;
    set_out(b, 0, &load, sizeof(load));
    set_out(b, 1, &total, sizeof(total));
    set_out(b, 2, &available, sizeof(available));
    set_out(b, 3, &virtual_total, sizeof(virtual_total));
    set_out(b, 4, &virtual_available, sizeof(virtual_available));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT PushButton2: the button state (local 0) and whether the left button was down (local 1) */
/*
 * TT PushButton2: Roll Over when the mouse comes over the target 2D entity
 * with the cursor shown, Released when it leaves, Mouse Down when the left
 * button goes down over it.
 */
static int bb_push_button2(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool turned_on = false;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, 0);
        set_local_int(b, 1, 0);
        turned_on = true;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t old_state = get_local_int(b, 0, 0);
    bool old_down = get_local_int(b, 1, 0) != 0;
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int32_t state = target == nmo_engine_entity_under_mouse(engine) &&
                            nmo_engine_cursor_is_visible(engine)
                        ? 1
                        : 0;
    bool down = nmo_engine_is_mouse_button_down(engine, 0);
    bool changed = false;
    if (turned_on && down) {
        old_down = true;
        set_local_int(b, 1, 1);
    }
    if (down != old_down) {
        set_local_int(b, 1, down ? 1 : 0);
        changed = true;
    }
    if (state != old_state) {
        set_local_int(b, 0, state);
        OUT_ON(b, state != 0 ? 1 : 0); /* Roll Over, Released */
    }
    if (changed && state == 1 && down) {
        OUT_ON(b, 2); /* Mouse Down */
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT_Timer: the TT Time Manager's time, which every running timer advances
 * by the frame time (On/Reset and Off restart it, Pause and Play stop and
 * resume it), in Elapsed Time.
 */
static int bb_tt_timer(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (IN_ACTIVE(b, 0)) {
        eng_tt(engine)->timing = true;
        eng_tt(engine)->time = 0.0f;
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    if (IN_ACTIVE(b, 1)) {
        eng_tt(engine)->timing = false;
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 2)) {
        eng_tt(engine)->timing = true;
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 3)) {
        eng_tt(engine)->timing = true;
        eng_tt(engine)->time = 0.0f;
        IN_OFF(b, 3);
        OUT_ON(b, 3);
        return NMO_ENGINE_BR_OK;
    }
    float delta = nmo_engine_block_delta_time(b);
    float elapsed = eng_tt(engine)->time + delta;
    if (eng_tt(engine)->timing) {
        eng_tt(engine)->time += delta;
    }
    set_out(b, 0, &elapsed, sizeof(elapsed));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT_SplitString: the parts of Text between Delimiters (" " when none) into
 * the outputs after Elements Found, each as its output's type reads text.
 */
static int bb_split_string(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *text = input_string(b, 0);
    if (text == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    const char *delimiter = input_string(b, 1);
    if (delimiter == NULL || delimiter[0] == '\0') {
        delimiter = " ";
    }
    size_t delimiter_length = strlen(delimiter);
    int outputs = nmo_engine_block_output_parameter_count(b);
    int32_t elements = 0;
    const char *rest = text;
    for (const char *p = strstr(rest, delimiter); p != NULL; p = strstr(p + delimiter_length, delimiter)) {
        if (p != rest) {
            if (++elements >= outputs) {
                break;
            }
            size_t length = (size_t)(p - rest);
            char *token = (char *)malloc(length + 1u);
            if (token == NULL) {
                break;
            }
            memcpy(token, rest, length);
            token[length] = '\0';
            eng_param_set_text(b->engine, block_output(b, elements), token);
            free(token);
        }
        rest = p + delimiter_length;
    }
    if (rest[0] != '\0' && ++elements < outputs) {
        eng_param_set_text(b->engine, block_output(b, elements), rest);
    }
    set_out(b, 0, &elements, sizeof(elements));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_Counter: Initialize sets the count to Start (Min when Start is outside
 * [Min, Max]); Count Up and Count Down step it, wrapping around [Min, Max].
 * The count in the local and the output, each call.
 */
static int bb_tt_counter(blk_t *b)
{
    int32_t start = get_in_int(b, 0, 0), min = get_in_int(b, 1, 0), max = get_in_int(b, 2, 9);
    int32_t count = get_local_int(b, 0, 0);
    if (start < min || start > max) {
        start = min; /* "TT Counter: Start value out of range" */
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        count = start;
        OUT_ON(b, 0);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        if (++count > max) {
            count = min;
        }
        OUT_ON(b, 1);
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        if (--count < min) {
            count = max;
        }
        OUT_ON(b, 2);
    }
    set_local_int(b, 0, count);
    set_out(b, 0, &count, sizeof(count));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT ConvertPixel-Homogen: a 2D vector in pixels of the render context made
 * homogeneous (divided by its size), or, with local 0 false, a homogeneous
 * one made pixels (truncated); X, Y and the vector out.
 */
static int bb_convert_pixel_homogen(blk_t *b)
{
    IN_OFF(b, 0);
    float v[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, v, sizeof(v));
    float width = (float)eng_screen_modes[b->engine->render.screen_mode].width;
    float height = (float)eng_screen_modes[b->engine->render.screen_mode].height;
    if ((get_local_int(b, 0, 1) & 0xff) != 0) {
        v[0] /= width;
        v[1] /= height;
    } else {
        v[0] = (float)(int32_t)(width * v[0]);
        v[1] = (float)(int32_t)(height * v[1]);
    }
    set_out(b, 0, &v[0], sizeof(v[0]));
    set_out(b, 1, &v[1], sizeof(v[1]));
    set_out(b, 2, v, sizeof(v));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Scaleable Proximity's outputs (the Use Outputs setting) and axes (Check Axis) */
enum {
    PROXIMITY_IN = 1,
    PROXIMITY_OUT = 2,
    PROXIMITY_ENTER = 4,
    PROXIMITY_EXIT = 8,
    PROXIMITY_WAS_OUT = 0,
    PROXIMITY_WAS_IN = 1,
};

/* The position Scaleable Proximity measures from: the barycenter, or the position. */
static eng_vector_t proximity_point(nmo_engine_t *engine, int32_t entity, bool barycenter)
{
    eng_vector_t p;
    if (barycenter) {
        /* A character would take its bounding box's center: the engine has no box for it */
        eng_entity_bary_center(engine, entity, &p);
    } else {
        p = eng_entity_position(engine, entity, -1);
    }
    return p;
}

/*
 * TT Scaleable Proximity: whether two entities are within Distance (on the
 * checked axes, squared or not), checked again after a number of frames
 * that grows with the distance between the two exactness distances; In,
 * Out, Enter and Exit Range among the outputs the setting keeps.
 */
static int bb_scaleable_proximity(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t outputs = get_local_int(b, 2, 15);
    int32_t axes = get_local_int(b, 3, 0);
    bool squared = get_local_int(b, 4, 1) != 0;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_is_input_active(b, 0)) {
        IN_OFF(b, 0);
        int32_t was = PROXIMITY_OUT;
        nmo_engine_block_set_local_value(b, 0, &was, sizeof(was));
    }
    int32_t last_check = get_local_int(b, 1, 1);
    if (--last_check > 0) {
        nmo_engine_block_set_local_value(b, 1, &last_check, sizeof(last_check));
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    float distance = get_in_float(b, 0, 0.0f);
    bool barycenter = get_in_int(b, 3, 0) != 0;
    int32_t a = input_entity(b, 1), c = input_entity(b, 2);
    if (a < 0 || c < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t pa = proximity_point(engine, a, barycenter);
    eng_vector_t pb = proximity_point(engine, c, barycenter);
    eng_vector_t a_to_b = {0.0f, 0.0f, 0.0f};
    float current = 0.0f;
    if (axes & 4) {
        a_to_b.z = pb.z - pa.z;
        current += a_to_b.z * a_to_b.z;
    }
    if (axes & 2) {
        a_to_b.y = pb.y - pa.y;
        current += a_to_b.y * a_to_b.y;
    }
    if (axes & 1) {
        a_to_b.x = pb.x - pa.x;
        current += a_to_b.x * a_to_b.x;
    }
    float min_exact = get_in_float(b, 4, 0.0f), max_exact = get_in_float(b, 5, 10.0f);
    int32_t min_delay = get_in_int(b, 6, 1), max_delay = get_in_int(b, 7, 10);
    if (squared) {
        distance *= distance;
        min_exact *= min_exact;
        max_exact *= max_exact;
    } else {
        current = powf(current, 0.5f);
    }
    nmo_engine_block_set_output_value(b, 0, &current, sizeof(current));
    if (nmo_engine_block_output_parameter_count(b) > 1) {
        nmo_engine_block_set_output_value(b, 1, &a_to_b, sizeof(a_to_b));
    }
    if (current < max_exact) {
        last_check = current > min_exact
                         ? min_delay + (int32_t)((current - min_exact) /
                                                 (double)(max_exact - min_exact) *
                                                 (max_delay - min_delay))
                         : min_delay;
    } else {
        last_check = max_delay;
    }
    if (last_check < 1) {
        last_check = 1;
    }
    nmo_engine_block_set_local_value(b, 1, &last_check, sizeof(last_check));
    int32_t was = get_local_int(b, 0, PROXIMITY_WAS_OUT);
    int32_t activation;
    if (current < distance) {
        activation = was == PROXIMITY_WAS_OUT || was == 2 ? PROXIMITY_ENTER : PROXIMITY_IN;
        was = PROXIMITY_WAS_IN;
    } else {
        activation = was == PROXIMITY_WAS_IN || was == 2 ? PROXIMITY_EXIT : PROXIMITY_OUT;
        was = PROXIMITY_WAS_OUT;
    }
    nmo_engine_block_set_local_value(b, 0, &was, sizeof(was));
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        if (outputs & (1 << i)) {
            if (activation & (1 << i)) {
                OUT_ON(b, pos);
            }
            pos++;
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT Set Dynamic Position: each frame the target follows the object like a
 * spring (Force, Damping and Offset per axis, in the coordinate system's
 * space given one), kept within MaxDistance2Target of it.
 */
static int bb_set_dynamic_position(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t frame = input_entity(b, 10);
    bool status = false;
    if (nmo_engine_block_is_input_active(b, 1)) {
        IN_OFF(b, 1);
        int32_t off = 0;
        nmo_engine_block_set_local_value(b, 0, &off, sizeof(off));
    } else {
        if (nmo_engine_block_is_input_active(b, 0)) {
            IN_OFF(b, 0);
            OUT_ON(b, 0);
            int32_t on = 1;
            nmo_engine_block_set_local_value(b, 0, &on, sizeof(on));
            eng_vector_t start = eng_entity_position(engine, target, frame);
            nmo_engine_block_set_local_value(b, 1, &start, sizeof(start));
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
        status = get_local_int(b, 0, 0) != 0;
    }
    int32_t object = input_entity(b, 0);
    if (object < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t object_position = eng_entity_position(engine, object, frame);
    eng_vector_t old_target = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 1, &old_target, sizeof(old_target));
    eng_vector_t target_position = eng_entity_position(engine, target, frame);
    nmo_engine_block_set_local_value(b, 1, &target_position, sizeof(target_position));
    eng_vector_t delta = {target_position.x - old_target.x, target_position.y - old_target.y,
                          target_position.z - old_target.z};
    eng_vector_t offset_to = {object_position.x - target_position.x,
                              object_position.y - target_position.y,
                              object_position.z - target_position.z};
    const float factor = nmo_engine_block_delta_time(b) * 0.001f;
    const float force[3] = {get_in_float(b, 1, 1.0f) * factor, get_in_float(b, 2, 1.0f) * factor,
                            get_in_float(b, 3, 1.0f) * factor};
    const float damping[3] = {get_in_float(b, 4, 0.0f), get_in_float(b, 5, 0.0f),
                              get_in_float(b, 6, 0.0f)};
    const float offset[3] = {get_in_float(b, 7, 0.0f), get_in_float(b, 8, 0.0f),
                             get_in_float(b, 9, 0.0f)};
    float max_distance = get_in_float(b, 11, 0.0f);
    target_position.x += (offset_to.x - offset[0]) * force[0] + delta.x * damping[0];
    target_position.y += (offset_to.y - offset[1]) * force[1] + delta.y * damping[1];
    target_position.z += (offset_to.z - offset[2]) * force[2] + delta.z * damping[2];
    object_position.x -= target_position.x;
    object_position.y -= target_position.y;
    object_position.z -= target_position.z;
    bool at_object = object_position.x == 0.0f && object_position.y == 0.0f &&
                     object_position.z == 0.0f;
    float distance = at_object ? 0.0f : eng_vector_magnitude(&object_position);
    eng_vector_t vector = {0.0f, 0.0f, 0.0f};
    if (distance > max_distance && max_distance > 0.0f) {
        vector = object_position;
        vector.x *= max_distance;
        vector.y *= max_distance;
        vector.z *= max_distance;
        target_position.x = delta.x - vector.x;
        target_position.y = delta.y - vector.y;
        target_position.z = delta.z - vector.z;
    }
    eng_entity_set_position(engine, target, &target_position, frame, false);
    nmo_engine_block_set_output_value(b, 0, &vector, sizeof(vector));
    if (status) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* TT Joystick Waiter follows TT_Toolbox_RT.dll (0x10023bc0): On rebases
 * sliders, relative sliders use sensitivity Z, and disconnected slots keep
 * running against the input snapshot. Output parameters hold the history. */
static int bb_tt_joystick_waiter(blk_t *b)
{
    if (IN_ACTIVE(b,1)) { IN_OFF(b,1); return NMO_ENGINE_BR_OK; }
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (eng_map_get(&b->engine->destroyed,owner) >= 0 ||
        !eng_is_child_class(b->engine,nmo_engine_object_class(b->engine,owner),NMO_CID_BEOBJECT)) return NMO_ENGINE_BR_OWNERERROR;
    nmo_engine_joystick_state_t state;
    (void)nmo_engine_joystick_state(b->engine,get_in_int(b,0,0),&state);
    eng_vector_t sensitivity = {.5f,.5f,.5f}; nmo_engine_block_get_local_value(b,0,&sensitivity,sizeof(sensitivity));
    float previous[8] = {0}, old_pov = -1;
    nmo_engine_block_get_output_value(b,0,previous,3*sizeof(float));
    nmo_engine_block_get_output_value(b,1,previous+3,3*sizeof(float));
    nmo_engine_block_get_output_value(b,2,previous+6,2*sizeof(float));
    nmo_engine_block_get_output_value(b,3,&old_pov,sizeof(old_pov));
    nmo_engine_block_set_output_value(b,0,state.position,sizeof(state.position));
    nmo_engine_block_set_output_value(b,1,state.rotation,sizeof(state.rotation));
    nmo_engine_block_set_output_value(b,2,state.sliders,sizeof(state.sliders));
    nmo_engine_block_set_output_value(b,3,&state.point_of_view,sizeof(state.point_of_view));
    if (nmo_engine_block_version(b) < 0x00020000u) {
        for (int i = 0; i < 6; i++) if (state.buttons & (1u << i)) OUT_ON(b,4+i);
        if (state.position[1] < -sensitivity.y) OUT_ON(b,0);
        if (state.position[1] > sensitivity.y) OUT_ON(b,1);
        if (state.position[0] < -sensitivity.x) OUT_ON(b,2);
        if (state.position[0] > sensitivity.x) OUT_ON(b,3);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b,0)) {
        previous[6] = state.sliders[0]; previous[7] = state.sliders[1]; IN_OFF(b,0);
    }
    uint32_t axes = (uint32_t)get_local_int(b,1,3), relative = (uint32_t)get_local_int(b,3,448);
    int output = 1; bool one_active = false;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t bit = 1u << i;
        if ((axes & bit) == 0) continue;
        float sample = i < 3 ? state.position[i] : i < 6 ? state.rotation[i-3] : state.sliders[i-6]; bool active;
        if (relative & bit) {
            double delta = i < 3 ? (double)previous[i]-sample : (double)sample-previous[i];
            double threshold = i < 6 ? sensitivity.y : sensitivity.z;
            if (i == 6) { float difference = (float)delta; nmo_engine_block_set_output_value(b,4,&difference,sizeof(difference)); }
            /* Native x87 range tests treat unordered relative deltas as movement;
             * compare before rounding the optional float Sliders Diff output. */
            active = !(delta >= -threshold && delta <= threshold);
        } else active = sample < -sensitivity.x || sample > sensitivity.x;
        if (active) { OUT_ON(b,output); one_active = true; }
        output++;
    }
    if (axes & 256u) {
        /* The native POV equality check also suppresses unordered changes. */
        bool changed = state.point_of_view < old_pov || state.point_of_view > old_pov;
        if ((relative & 256u) == 0 || changed) {
            OUT_ON(b,output+(state.point_of_view >= 0 ? 0 : 1)); one_active = true;
        }
        output += 2;
    }
    uint32_t buttons = (uint32_t)get_local_int(b,2,1);
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t bit = 1u << i;
        if ((buttons & bit) == 0) continue;
        if (state.buttons & bit) { OUT_ON(b,output); one_active = true; }
        output++;
    }
    if (!one_active) OUT_ON(b,0);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT_TestForJoystick queries the current frame and finishes on Yes or No. */
static int bb_test_for_joystick(blk_t *b)
{
    bool attached = nmo_engine_joystick_state(b->engine,get_in_int(b,0,0),NULL);
    OUT_ON(b,attached ? 0 : 1);
    IN_OFF(b,0);
    return NMO_ENGINE_BR_OK;
}

/* Four ordered half-axis assignments; the negative channel may overwrite
 * the positive channel. Unselected components, including output Z, stay zero. */
static int bb_joystick_vector_mapper(blk_t *b)
{
    eng_vector_t input = get_in_vector(b,0,0,0,0), output = {0};
    const float components[] = {input.x,input.y,input.z};
    for (int channel = 0; channel < 4; channel++) {
        int32_t selector = get_in_int(b,1+channel,0);
        if (selector < 0 || selector > 5) continue;
        float value = components[selector/2];
        if (selector%2 ? value < 0 : value >= 0) {
            if (selector%2) value = -value;
            if (channel%2) value = -value;
            if (channel < 2) output.x = value; else output.y = value;
        }
    }
    nmo_engine_block_set_output_value(b,0,&output,sizeof(output));
    IN_OFF(b,0); OUT_ON(b,0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Logic, tests and narratives
 * ============================================================================ */

/* A box parameter's value as a VxBbox holds it: Max, then Min */
static void set_out_box(blk_t *b, int pos, eng_vector_t min, eng_vector_t max)
{
    const float box[6] = {max.x, max.y, max.z, min.x, min.y, min.z};
    set_out(b, pos, box, sizeof(box));
}

/* TT_ConvertBox: the box of the two corners (its input left active, as in the DLL) */
static int bb_convert_box(blk_t *b)
{
    eng_vector_t min = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f), max = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
    set_out_box(b, 0, min, max);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT Find SubString: the first String input the Object's name holds (its output and rank), else None */
static int bb_find_substring(blk_t *b)
{
    IN_OFF(b, 0);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    const char *name = object != 0 ? nmo_engine_object_name(b->engine, object) : NULL;
    int count = nmo_engine_block_input_parameter_count(b);
    for (int32_t i = 1; i < count; i++) {
        const char *text = input_string(b, i);
        if (name != NULL && text != NULL && strstr(name, text) != NULL) {
            OUT_ON(b, i);
            set_out_int(b, 0, i);
            return NMO_ENGINE_BR_OK;
        }
    }
    OUT_ON(b, 0);
    set_out_int(b, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/* Vx2DVector::Normalize: by the inverse of its length, when it has one */
static void normalize_2d(float v[2])
{
    double length = sqrt((double)(v[1] * v[1] + v[0] * v[0]));
    if (length != 0.0) {
        double inv = 1.0 / length;
        v[0] = (float)(inv * v[0]);
        v[1] = (float)(inv * v[1]);
    }
}

/* TT_GetFullAngle: the angle from A to B all the way round (0 to 2 pi), in radians and degrees */
static int bb_get_full_angle(blk_t *b)
{
    float a[2] = {0.0f, 0.0f}, c[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, a, sizeof(a));
    nmo_engine_block_get_input_value(b, 1, c, sizeof(c));
    normalize_2d(a);
    normalize_2d(c);
    float dot = c[1] * a[1] + c[0] * a[0];
    float cross = a[0] * c[1] + -a[1] * c[0];
    if (dot > 1.0f) {
        dot = 1.0f;
    }
    float rad = cross <= 0.0f ? (float)acos(dot) : (float)(3.1415927f - acos(dot) + 3.1415927f);
    set_out_float(b, 0, rad);
    set_out_float(b, 1, rad * 57.295776f);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* CK3dEntity::GetBoundingBox(FALSE): at its position when it has no geometry */
static void entity_world_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    if (!eng_entity_world_box(engine, index, min, max)) {
        *min = *max = eng_entity_position(engine, index, -1);
    }
}

/* TT GetGroupBoundingBox: the box around the 3D entities of the group (from +-10000) */
static int bb_get_group_bounding_box(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_vector_t min = {10000.0f, 10000.0f, 10000.0f}, max = {-10000.0f, -10000.0f, -10000.0f};
    int32_t count = nmo_engine_group_count(engine, group);
    for (int32_t i = 0; i < count; i++) {
        int32_t index = eng_entity_index(engine, nmo_engine_group_object(engine, group, i));
        if (index < 0) {
            continue;
        }
        eng_vector_t emin, emax;
        entity_world_box(engine, index, &emin, &emax);
        min = (eng_vector_t){fminf(min.x, emin.x), fminf(min.y, emin.y), fminf(min.z, emin.z)};
        max = (eng_vector_t){fmaxf(max.x, emax.x), fmaxf(max.y, emax.y), fmaxf(max.z, emax.z)};
    }
    set_out_box(b, 0, min, max);
    return NMO_ENGINE_BR_OK;
}

/* CK3dEntity::GetZOrder: its scene graph node's highest priority, its own or a descendant's */
static int32_t hierarchy_z_order(const nmo_engine_t *engine, int32_t index)
{
    int32_t z = engine->entities.data[index].render_priority;
    for (int32_t c = engine->entities.data[index].first_child; c >= 0; c = engine->entities.data[c].next_sibling) {
        int32_t child = hierarchy_z_order(engine, c);
        z = child > z ? child : z;
    }
    return z;
}

/* TT_GetZOrder: the target's rendering priority */
static int bb_get_z_order(blk_t *b)
{
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    set_out_int(b, 0, hierarchy_z_order(b->engine, target));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_Chrono: the Elapsed Time counted each frame from On (from 0), held at
 * Stop; as in the DLL, one flag for every chronometer: On after a Stop
 * resumes instead of restarting.
 */
static int bb_chrono(blk_t *b)
{
    eng_tt_t *tt = eng_tt(b->engine);
    float elapsed = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        if (tt->chrono_on) {
            tt->chrono_on = false;
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
        set_out_float(b, 0, elapsed);
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        tt->chrono_on = true;
        return NMO_ENGINE_BR_OK;
    }
    elapsed = get_out_float(b, 0) + nmo_engine_block_delta_time(b);
    set_out_float(b, 0, elapsed);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT_SearchRow: the first row of the target array whose cell in Search
 * Column equals Search Value, when the value's type fits the column's (else
 * it looks for an empty key); none for a column of another type.
 */
static int bb_search_row(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t array = nmo_engine_block_target(b);
    if (array == 0 || nmo_engine_array_row_count(engine, array) < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t column = get_in_int(b, 0, 0);
    nmo_guid_t in = nmo_engine_block_input_parameter_type(b, 1);
    nmo_engine_cell_t key;
    memset(&key, 0, sizeof(key));
    key.type = nmo_engine_array_column_type(engine, array, column);
    switch (key.type) {
    case CKARRAYTYPE_INT:
    case CKARRAYTYPE_FLOAT:
    case CKARRAYTYPE_OBJECT: {
        nmo_guid_t fits = key.type == CKARRAYTYPE_INT ? CKPGUID_INT : key.type == CKARRAYTYPE_FLOAT ? CKPGUID_FLOAT
                                                                                                  : CKPGUID_BEOBJECT;
        if (nmo_guid_equals(in, fits)) {
            nmo_engine_block_get_input_value(b, 1, &key.value, sizeof(key.value));
        }
        break;
    }
    case CKARRAYTYPE_STRING:
        if (nmo_guid_equals(in, CKPGUID_STRING)) {
            key.text = input_string(b, 1);
        }
        break;
    case CKARRAYTYPE_PARAMETER: {
        nmo_engine_cell_t first;
        if (nmo_engine_array_get_cell(engine, array, 0, column, &first) == NMO_OK &&
            nmo_guid_equals(in, first.parameter_type)) {
            key.data = nmo_engine_block_input_data(b, 1, &key.size);
        }
        break;
    }
    default:
        return NMO_ENGINE_BR_OK; /* "Wrong valuetype" */
    }
    set_out_int(b, 0, nmo_engine_array_find_row(engine, array, column, &key, 0));
    return NMO_ENGINE_BR_OK;
}

/* TT ChildrenIterator: Reset counts the target's children, then each Next gives the next one */
static int bb_children_iterator(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t index = get_out_int(b, 2), count = 0;
    for (int32_t c = engine->entities.data[target].first_child; c >= 0; c = engine->entities.data[c].next_sibling) {
        count++;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        index = 0;
        set_out_int(b, 0, count);
    } else {
        IN_OFF(b, 1);
        index++;
    }
    int32_t child = engine->entities.data[target].first_child;
    for (int32_t i = 0; child >= 0 && i < index; i++) {
        child = engine->entities.data[child].next_sibling;
    }
    if (index >= 0 && index < count && child >= 0) {
        nmo_object_id_t id = engine->entities.data[child].id;
        set_out(b, 1, &id, sizeof(id));
        set_out_int(b, 2, index);
        OUT_ON(b, 1);
    } else {
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* TT ConvertTimeToString: "MM:SS:CC" of a time under an hour, else "00:00:00" */
static int bb_convert_time_to_string(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float time = get_in_float(b, 0, 0.0f);
    char text[9] = "00:00:00";
    if (time < 3600000.0f && time > 0.0f) {
        int64_t ms = (int64_t)time;
        int32_t seconds = (int32_t)(int64_t)((double)(ms - ms % 1000) * 0.001) % 60;
        int32_t minutes = (int32_t)(ms / 60000);
        if (minutes > 59) {
            minutes = 0;
        }
        int32_t centi = (int32_t)(int64_t)((double)(ms % 1000) * 0.1);
        text[0] = (char)('0' + minutes / 10);
        text[1] = (char)('0' + minutes % 10);
        text[3] = (char)('0' + seconds / 10);
        text[4] = (char)('0' + seconds % 10);
        text[6] = (char)('0' + centi / 10);
        text[7] = (char)('0' + centi % 10);
    }
    nmo_engine_block_set_output_value(b, 0, text, sizeof(text));
    return NMO_ENGINE_BR_OK;
}

/* TT_ParameterMapper: each active input's integer picks an output (one past None), None out of range */
static int bb_parameter_mapper(blk_t *b)
{
    int inputs = nmo_engine_block_input_count(b), outputs = nmo_engine_block_output_count(b);
    for (int i = 0; i < inputs; i++) {
        if (!IN_ACTIVE(b, i)) {
            continue;
        }
        IN_OFF(b, i);
        int32_t value = get_in_int(b, i, 0);
        OUT_ON(b, value < 0 || value >= outputs - 1 ? 0 : value + 1);
    }
    return NMO_ENGINE_BR_OK;
}

/* Yes (with No off) or No (with Yes off) */
static int yes_or_no(blk_t *b, bool yes)
{
    IN_OFF(b, 0);
    nmo_engine_block_activate_output(b, 0, yes);
    nmo_engine_block_activate_output(b, 1, !yes);
    return NMO_ENGINE_BR_OK;
}

/* TT_IsObjectActive: whether the object is active in the scene */
static int bb_is_object_active(blk_t *b)
{
    return yes_or_no(b, nmo_engine_object_is_active(b->engine, nmo_engine_block_get_input_object(b, 0)));
}

/* TT_IsObjectVisible: whether the object is shown */
static int bb_is_object_visible(blk_t *b)
{
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    if (object == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    return yes_or_no(b, nmo_engine_object_is_visible(b->engine, object));
}

/* TT_IsScriptActive: whether the script is active; a parameter error ("No script") without one */
static int bb_is_script_active(blk_t *b)
{
    nmo_object_id_t script = nmo_engine_block_get_input_object(b, 0);
    if (script == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    return yes_or_no(b, nmo_engine_behavior_is_active(b->engine, script));
}

/* TT_GetTime: the TT Time Manager's time */
static int bb_get_time(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    set_out_float(b, 0, eng_tt(b->engine)->time);
    return NMO_ENGINE_BR_OK;
}

/* TT_DebugOFF: out of debug mode */
static int bb_debug_off(blk_t *b)
{
    eng_tt(b->engine)->debug_mode = false;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_KeySwitch, the DLL's: active until Off, testing no key (a parameter
 * error, an exception in the DLL, when its keys and outputs differ in number).
 */
static int bb_key_switch(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_input_parameter_count(b) != nmo_engine_block_output_count(b)) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT Activate Object: the object active in the scene; with Activate All Scripts its scripts, reset when asked */
static int bb_tt_activate_object(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    if (object == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool reset = get_in_int(b, 1, 0) != 0;
    nmo_engine_activate_object(engine, object, true);
    int32_t index = get_in_int(b, 2, 0) != 0 ? eng_map_get(&engine->object_map, object) : -1;
    if (index >= 0) {
        const eng_object_t *owner = &engine->objects.data[index];
        for (uint32_t s = 0; s < owner->script_count; s++) {
            int32_t script = *eng_index(engine, owner->first_script + s);
            nmo_engine_activate_script(engine, engine->nodes.data[script].id, true, reset);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* The 3D entity classes whose hierarchy TT Restore Dynamic IC takes (the DLL's list) */
static bool restore_hierarchy_class(int32_t class_id)
{
    switch (class_id) {
    case NMO_CID_3DENTITY: case NMO_CID_CAMERA: case NMO_CID_TARGETCAMERA: case NMO_CID_CURVEPOINT:
    case NMO_CID_SPRITE3D: case NMO_CID_LIGHT: case NMO_CID_TARGETLIGHT: case NMO_CID_CHARACTER:
    case NMO_CID_3DOBJECT: case NMO_CID_BODYPART: case NMO_CID_CURVE:
        return true;
    default:
        return false;
    }
}

static bool is_dynamic(const nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->dynamic.objects.count; i++) {
        if (engine->dynamic.objects.data[i] == id) {
            return true;
        }
    }
    return false;
}

static nmo_object_id_t current_mesh_of(nmo_engine_t *engine, nmo_object_id_t id)
{
    const nmo_3dentity_state_t *state = (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
        engine->types, eng_find_object(engine, id), CKPGUID_3DENTITY);
    return state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
}

/* The object's initial value read back, a dynamic one's current mesh kept; false without one */
static bool restore_dynamic(nmo_engine_t *engine, nmo_object_id_t id, bool keep_mesh)
{
    if (!eng_in_scene(engine, id) || eng_map_get(&engine->ics.map, id) < 0) {
        return false;
    }
    nmo_object_id_t mesh = keep_mesh && is_dynamic(engine, id) ? current_mesh_of(engine, id) : 0;
    eng_restore_ic(engine, id);
    if (mesh != 0) {
        eng_entity_set_current_mesh(engine, id, mesh);
    }
    return true;
}

/*
 * TT Restore Dynamic IC: the target's initial condition read back (a dynamic
 * object keeping its mesh); with Hierarchy, as in the DLL, its first child's,
 * once for each child (a dynamic one put back under the target). The DLL
 * loops forever when that child has no initial condition: here it stops.
 */
static int bb_restore_dynamic_ic(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t class_id = nmo_engine_object_class(engine, target);
    restore_dynamic(engine, target, restore_hierarchy_class(class_id));
    int32_t entity = eng_entity_index(engine, target);
    uint8_t hierarchy = 0;
    nmo_engine_block_get_input_value(b, 0, &hierarchy, sizeof(hierarchy));
    if (entity < 0 || !restore_hierarchy_class(class_id) || hierarchy == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t count = 0;
    for (int32_t c = engine->entities.data[entity].first_child; c >= 0; c = engine->entities.data[c].next_sibling) {
        count++;
    }
    for (int32_t i = 0; i < count; i++) {
        int32_t first = engine->entities.data[entity].first_child;
        if (first < 0) {
            break;
        }
        nmo_object_id_t child = engine->entities.data[first].id;
        bool dynamic = is_dynamic(engine, child);
        if (!restore_dynamic(engine, child, true)) {
            break;
        }
        if (dynamic) {
            eng_entity_set_parent(engine, eng_entity_index(engine, child), entity, true);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* A parameter named `name` among the behavior's outputs and locals, then its sub-behaviors' (depth first) */
static eng_param_t *find_named_param(nmo_engine_t *engine, int32_t node, const char *name)
{
    const eng_node_t *n = &engine->nodes.data[node];
    for (int pass = 0; pass < 2; pass++) {
        uint32_t first = pass == 0 ? n->first_pout : n->first_local, count = pass == 0 ? n->pout_count : n->local_count;
        for (uint32_t i = 0; i < count; i++) {
            eng_param_t *param = eng_node_param(engine, first, count, (int)i);
            const char *pname = param != NULL ? nmo_engine_object_name(engine, param->id) : NULL;
            if (pname != NULL && strcmp(pname, name) == 0) {
                return param;
            }
        }
    }
    for (uint32_t c = 0; c < n->child_count; c++) {
        eng_param_t *found = find_named_param(engine, *eng_index(engine, n->first_child + c), name);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

/* The inputs (not shared) of the behavior and its sub-behaviors reading a source named `name` read
   `param` instead, the source renamed "~* Replaced by: <param> *~", when the types fit */
static void replace_named_sources(nmo_engine_t *engine, int32_t node, const char *name, eng_param_t *param)
{
    const eng_node_t *n = &engine->nodes.data[node];
    const char *pname = nmo_engine_object_name(engine, param->id);
    for (uint32_t i = 0; i < n->pin_count; i++) {
        eng_param_t *pin = eng_node_param(engine, n->first_pin, n->pin_count, (int)i);
        if (pin == NULL || pin->shared || pin->source < 0) {
            continue;
        }
        eng_param_t *source = &engine->params.data[pin->source];
        const char *sname = nmo_engine_object_name(engine, source->id);
        if (sname == NULL || strcmp(sname, name) != 0 || !eng_types_compatible(engine, source, param)) {
            continue;
        }
        const char *format = "~* Replaced by: %s *~";
        int length = snprintf(NULL, 0, format, pname != NULL ? pname : "(null)");
        char *renamed = length >= 0 ? (char *)malloc((size_t)length + 1) : NULL;
        if (renamed != NULL) {
            snprintf(renamed, (size_t)length + 1, format, pname != NULL ? pname : "(null)");
            nmo_object_set_name(eng_find_object(engine, source->id), renamed);
            free(renamed);
        }
        pin->source = (int32_t)(param - engine->params.data);
    }
    for (uint32_t c = 0; c < n->child_count; c++) {
        replace_named_sources(engine, *eng_index(engine, n->first_child + c), name, param);
    }
}

/*
 * TT ReplaceInputparameter: the inputs of Set Script reading a parameter
 * named Set Parametername read instead the output or local of Get Script
 * named Get Parametername. Its input stays active, as in the DLL.
 */
static int bb_replace_input_parameter(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t get = eng_map_get(&engine->node_map, nmo_engine_block_get_input_object(b, 0));
    int32_t set = eng_map_get(&engine->node_map, nmo_engine_block_get_input_object(b, 2));
    const char *get_name = input_string(b, 1), *set_name = input_string(b, 3);
    if (get < 0 || set < 0 || get_name == NULL || get_name[0] == '\0' || set_name == NULL || set_name[0] == '\0') {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_param_t *param = find_named_param(engine, get, get_name);
    if (param == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    replace_named_sources(engine, set, set_name, param);
    return NMO_ENGINE_BR_OK;
}

/* TT WriteBack: Value's 4 bytes written into the parameter Parameter reads (its direct source); as in the
   DLL, it activates an output it does not have */
static int bb_write_back(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    const eng_node_t *node = block_node(b);
    eng_param_t *pin = eng_node_param(engine, node->first_pin, node->pin_count, 0);
    if (pin != NULL && !pin->shared && pin->source >= 0) {
        eng_param_t *value = input_source(b, 1, true);
        if (value != NULL && value->data != NULL && value->size >= 4) {
            uint8_t bytes[4];
            memcpy(bytes, value->data, 4);
            eng_param_write(engine, &engine->params.data[pin->source], bytes, 4);
        }
    }
    IN_OFF(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* TT_ShowVector: the curve's first two control points at the two positions, each frame */
static int bb_show_vector(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    eng_vector_t ends[2] = {get_in_vector(b, 0, 0.0f, 0.0f, 0.0f), get_in_vector(b, 1, 0.0f, 0.0f, 0.0f)};
    const eng_curve_points_t *points = eng_curve_points(engine, nmo_engine_block_get_input_object(b, 2));
    for (size_t i = 0; points != NULL && i < 2 && i < points->count; i++) {
        int32_t point = eng_entity_index(engine, points->ids[i]);
        if (point >= 0) {
            eng_entity_set_position(engine, point, &ends[i], -1, false);
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static const nmo_engine_block_desc_t eng_tt_toolbox_blocks[] = {
    {NMO_GUID_INIT(0x594a136e,0x58135ef9),"TT Joystick Waiter",bb_tt_joystick_waiter,NULL,NULL},
    {NMO_GUID_INIT(0x229865eb,0x577619e4),"TT_TestForJoystick",bb_test_for_joystick,NULL,NULL},
    {NMO_GUID_INIT(0x04f80efd,0x76b47b33),"TT_JoystickVectorMapper",bb_joystick_vector_mapper,NULL,NULL},
    {NMO_GUID_INIT(0x09b335b3, 0x12d17cdc), "TT_LinearVolume", bb_linear_volume, NULL, NULL},
    {NMO_GUID_INIT(0x7e212b2f, 0x24db67c6), "TT_ReflectionMapping", bb_reflection_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x416b4f0e, 0x192d339a), "TT_PreloadTextures", bb_preload_textures, NULL, NULL},
    {NMO_GUID_INIT(0x4a446c43, 0x66fa2375), "TT_Debug", bb_tt_debug, NULL, NULL},
    {NMO_GUID_INIT(0x3d00718f, 0x2c8b33a2), "TT_DebugON", bb_debug_on, NULL, NULL},
    {NMO_GUID_INIT(0x4c6513e9, 0x20e11177), "TT GetEnvironmentVariable", bb_get_environment_variable,
     NULL, NULL},
    {NMO_GUID_INIT(0x556e6df3, 0x24207d37), "TT_GetCurrentDirectory", bb_get_current_directory, NULL,
     NULL},
    {NMO_GUID_INIT(0x524a6bcb, 0x66f67774), "TT_ReplacePath", bb_replace_path, NULL, NULL},
    {NMO_GUID_INIT(0x38c34dab, 0x5dd03bdf), "TT_SetMipMapping", bb_set_mip_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x7c663b40, 0x2f4b347e), "TT_TextureInfo", bb_texture_info, NULL, NULL},
    {NMO_GUID_INIT(0x3c392558, 0x419d2680), "TT Set_2DSprite", bb_set_2d_sprite, NULL, NULL},
    {NMO_GUID_INIT(0x4c94621d, 0x24fe2cf3), "TT OperationSystem", bb_operation_system, NULL, NULL},
    {NMO_GUID_INIT(0x3b826e04, 0x6e764285), "TT GetMemoryStatus", bb_get_memory_status, NULL, NULL},
    {NMO_GUID_INIT(0x6ac67901, 0x7d2a6059), "TT_Timer", bb_tt_timer, NULL, NULL},
    {NMO_GUID_INIT(0x5ae74e2f, 0x799c49b5), "TT_SplitString", bb_split_string, NULL, NULL},
    {NMO_GUID_INIT(0x14d325d1, 0x6748654e), "TT PushButton2", bb_push_button2, NULL, NULL},
    {NMO_GUID_INIT(0x750f20fc, 0x3741186d), "TT_Counter", bb_tt_counter, NULL, NULL},
    {NMO_GUID_INIT(0x18f96977, 0x18e20f83), "TT ConvertPixel-Homogen", bb_convert_pixel_homogen, NULL,
     NULL},
    {NMO_GUID_INIT(0x2a2a63ca, 0x00826175), "TT Scaleable Proximity", bb_scaleable_proximity, NULL,
     NULL},
    {NMO_GUID_INIT(0x694867ff, 0x7db16d1f), "TT_ConvertBox", bb_convert_box, NULL, NULL},
    {NMO_GUID_INIT(0x12f31e27, 0x4cea0bf5), "TT Find SubString", bb_find_substring, NULL, NULL},
    {NMO_GUID_INIT(0x7a18014d, 0x009d2e73), "TT_GetFullAngle", bb_get_full_angle, NULL, NULL},
    {NMO_GUID_INIT(0x3cc07535, 0x037e351c), "TT GetGroupBoundingBox", bb_get_group_bounding_box, NULL, NULL},
    {NMO_GUID_INIT(0x09163112, 0x1d2b4454), "TT_GetZOrder", bb_get_z_order, NULL, NULL},
    {NMO_GUID_INIT(0x2fca6b65, 0x73b31ebd), "TT_ShowVector", bb_show_vector, NULL, NULL},
    {NMO_GUID_INIT(0x325d722d, 0x3d9e16e9), "TT_Chrono", bb_chrono, NULL, NULL},
    {NMO_GUID_INIT(0x279603a9, 0x16f20716), "TT_SearchRow", bb_search_row, NULL, NULL},
    {NMO_GUID_INIT(0x78041c90, 0x16de3cde), "TT ChildrenIterator", bb_children_iterator, NULL, NULL},
    {NMO_GUID_INIT(0x69e34443, 0x459f23db), "TT ConvertTimeToString", bb_convert_time_to_string, NULL, NULL},
    {NMO_GUID_INIT(0x033e30a1, 0x42f452fc), "TT_ParameterMapper", bb_parameter_mapper, NULL, NULL},
    {NMO_GUID_INIT(0x409f03d9, 0x7616062c), "TT_IsObjectActive", bb_is_object_active, NULL, NULL},
    {NMO_GUID_INIT(0x66e06b8a, 0x57db26d0), "TT_IsScriptActive", bb_is_script_active, NULL, NULL},
    {NMO_GUID_INIT(0x39cf3dd3, 0x11a92281), "TT_IsObjectVisible", bb_is_object_visible, NULL, NULL},
    {NMO_GUID_INIT(0x59245fae, 0x24a123f3), "TT Activate Object", bb_tt_activate_object, NULL, NULL},
    {NMO_GUID_INIT(0x680269cb, 0x50814d03), "TT Restore Dynamic IC", bb_restore_dynamic_ic, NULL, NULL},
    {NMO_GUID_INIT(0x54a776b7, 0x20fc1e7c), "TT ReplaceInputparameter", bb_replace_input_parameter, NULL, NULL},
    {NMO_GUID_INIT(0x78b31127, 0x0a875030), "TT WriteBack", bb_write_back, NULL, NULL},
    {NMO_GUID_INIT(0x4fbb3808, 0x4a99083c), "TT_KeySwitch", bb_key_switch, NULL, NULL},
    {NMO_GUID_INIT(0x3e715746, 0x45ce3b5a), "TT_GetTime", bb_get_time, NULL, NULL},
    {NMO_GUID_INIT(0x70f65b3e, 0x2dc41372), "TT_DebugOFF", bb_debug_off, NULL, NULL},
    {NMO_GUID_INIT(0x0fd4755f, 0x7de22dc8), "TT Set Dynamic Position", bb_set_dynamic_position, NULL,
     NULL},
};

nmo_status_t eng_register_tt_toolbox(nmo_engine_t *engine)
{
    nmo_status_t status = eng_tt_add(engine);
    if (status != NMO_OK) {
        return status;
    }
    eng_tt(engine)->timing = true;
    return ENG_REGISTER_BLOCKS(engine, eng_tt_toolbox_blocks);
}
