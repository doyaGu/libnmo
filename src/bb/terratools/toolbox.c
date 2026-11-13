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
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
#include "format/nmo_image_codec.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "../../engine/engine_curve.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/platform.h"

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
 * TT Set_2DSprite and TT Set_2DSprite_old: the target 2D entity in
 * homogeneous coordinates, at Position and Size (each its own while not an
 * input of the "Add Input" setting), with the UV-Rect as its source rect
 * when that is an input. The old one takes the rect as two 2D vectors,
 * left-right and top-bottom.
 */
#define SPRITE_INPUT_UV 1
#define SPRITE_INPUT_SIZE 2
#define SPRITE_INPUT_POSITION 4

static int set_2d_sprite(blk_t *b, bool uv_pairs)
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
    if ((inputs & SPRITE_INPUT_UV) && uv_pairs) {
        float left_right[2] = {0.0f, 0.0f}, top_bottom[2] = {0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, pos, left_right, sizeof(left_right));
        nmo_engine_block_get_input_value(b, pos + 1, top_bottom, sizeof(top_bottom));
        entity->source = (nmo_rect_t){left_right[0], top_bottom[0], left_right[1], top_bottom[1]};
    } else if (inputs & SPRITE_INPUT_UV) {
        nmo_engine_block_get_input_value(b, pos, &entity->source, sizeof(entity->source));
    }
    /* SetHomogeneousCoordinates, then SetHomogeneousRect */
    nmo_rect_t rect = {position[0], position[1], position[0] + size[0], position[1] + size[1]};
    entity->flags |= CK_2DENTITY_USEHOMOGENEOUSCOORD;
    entity->homogeneous = rect;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, entity->id, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_set_2d_sprite(blk_t *b)
{
    return set_2d_sprite(b, false);
}

static int bb_set_2d_sprite_old(blk_t *b)
{
    return set_2d_sprite(b, true);
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

/* TT GetCurvePoint: the target curve's control point Number (renamed "Curvepoints", as in the DLL) */
static int bb_get_curve_point(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t curve = nmo_engine_block_target(b);
    const eng_curve_points_t *points = curve != 0 ? eng_curve_points(engine, curve) : NULL;
    if (points == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t number = get_in_int(b, 0, 0);
    /* The DLL names the point before testing it; it would crash on none, here it says so */
    if (number < 0 || (size_t)number >= points->count) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t point = points->ids[number];
    nmo_object_t *object = eng_find_object(engine, point);
    if (object != NULL) {
        nmo_object_set_name(object, "Curvepoints");
    }
    nmo_engine_block_set_output_value(b, 0, &point, sizeof(point));
    return NMO_ENGINE_BR_OK;
}

/*
 * The DLL's projection of a point on a segment in the XZ plane: along the
 * segment's normal, false outside the segment; the height is the segment's
 * at that X (at that Z when the segment does not move in X).
 */
static bool project_on_segment_xz(eng_vector_t *out, const eng_vector_t *a, const eng_vector_t *c,
                                  const eng_vector_t *p)
{
    float dx = c->x - a->x, dy = c->y - a->y, dz = c->z - a->z;
    double nx = -(double)dz;
    float nz = dx;
    double length = sqrt(nx * nx + (double)nz * nz);
    if (length != 0.0) {
        double inv = 1.0 / length;
        nx = nx * inv;
        nz = (float)(inv * dx);
    }
    double pz = (double)p->z - a->z, px = (double)p->x - a->x;
    double det = nx * dz - (double)nz * dx;
    float t = (float)((pz * nx - px * nz) / det);
    float s = (float)((pz * dx - px * dz) / det);
    if (t > 1.0f || t < 0.0f) {
        return false;
    }
    double x = nx * s + p->x;
    float z = s * nz + p->z;
    out->x = (float)x;
    out->z = z;
    out->y = dx == 0.0f ? (float)((z - a->z) / (c->z - a->z) * dy + a->y)
                        : (float)((x - a->x) / dx * dy + a->y);
    return true;
}

static float distance_squared(const eng_vector_t *a, const eng_vector_t *c)
{
    float dx = a->x - c->x, dy = a->y - c->y, dz = a->z - c->z;
    return dz * dz + dy * dy + dx * dx;
}

/*
 * TT GetNearestCurvePosition: the point of the curve (taken as a closed
 * polyline of its control points) nearest Position in XZ among those within
 * the Prefered Height Delta of it, else the nearest in 3D; the Step is the
 * length along the curve to the control point before it plus the straight
 * distance from there, over the curve's length.
 */
static int bb_get_nearest_curve_position(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t curve = nmo_engine_block_get_input_object(b, 0);
    const eng_curve_points_t *view = curve != 0 ? eng_curve_points(engine, curve) : NULL;
    if (view == NULL) {
        return NMO_ENGINE_BR_OK; /* "No Valid Curve" on the console */
    }
    eng_vector_t pos = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
    float height = get_local_float(b, 0, 2.5f);
    size_t count = view->count;
    if (count == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL reads a point it never found */
    }
    nmo_object_id_t *ids = (nmo_object_id_t *)malloc(count * sizeof(*ids));
    eng_vector_t *positions = (eng_vector_t *)malloc(count * sizeof(*positions));
    if (ids == NULL || positions == NULL) {
        free(ids);
        free(positions);
        return NMO_ENGINE_BR_OK;
    }
    memcpy(ids, view->ids, count * sizeof(*ids));
    for (size_t i = 0; i < count; i++) {
        int32_t index = eng_entity_index(engine, ids[i]);
        positions[i] = index >= 0 ? eng_entity_position(engine, index, -1) : (eng_vector_t){0, 0, 0};
    }
    float best_3d = 1000000000.0f, best_xz = 1000000000.0f;
    eng_vector_t near_3d = {0, 0, 0}, from_3d = {0, 0, 0}, near_xz = {0, 0, 0}, from_xz = {0, 0, 0};
    nmo_object_id_t point_3d = 0, point_xz = 0;
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        size_t next = i == count - 1 ? 0 : i + 1;
        const eng_vector_t *p = &positions[i], *q = &positions[next];
        float pxz = (pos.z - p->z) * (pos.z - p->z) + (pos.x - p->x) * (pos.x - p->x);
        float qxz = (pos.z - q->z) * (pos.z - q->z) + (pos.x - q->x) * (pos.x - q->x);
        eng_vector_t proj = {0, 0, 0};
        if (project_on_segment_xz(&proj, p, q, &pos)) {
            float dy = proj.y - pos.y;
            float d3 = distance_squared(&proj, &pos);
            if (best_3d > d3) {
                best_3d = d3;
                near_3d = proj;
                from_3d = *p;
                point_3d = ids[i];
            }
            float dxz = (proj.z - pos.z) * (proj.z - pos.z) + (proj.x - pos.x) * (proj.x - pos.x);
            if (best_xz > dxz && fabsf(dy) < height) {
                best_xz = dxz;
                near_xz = proj;
                from_xz = *p;
                point_xz = ids[i];
                found = true;
            }
        } else {
            float dp = p->y - pos.y, dq = q->y - pos.y;
            float d3 = distance_squared(p, &pos);
            if (best_3d > d3) {
                best_3d = d3;
                near_3d = from_3d = *p;
                point_3d = ids[i];
            }
            d3 = distance_squared(q, &pos);
            if (best_3d > d3) {
                best_3d = d3;
                near_3d = from_3d = *q;
                point_3d = ids[next];
            }
            if (best_xz > pxz && fabsf(dp) < height) {
                best_xz = pxz;
                near_xz = from_xz = *p;
                point_xz = ids[i];
                found = true;
            }
            if (best_xz > qxz && fabsf(dq) < height) {
                best_xz = qxz;
                near_xz = from_xz = *q;
                point_xz = ids[next];
                found = true;
            }
        }
    }
    free(ids);
    free(positions);
    eng_vector_t near = found ? near_xz : near_3d, from = found ? from_xz : from_3d;
    nmo_object_id_t point = found ? point_xz : point_3d;
    float curve_length = 0.0f, point_length = 0.0f;
    eng_curve_length(engine, curve, &curve_length);
    eng_curve_point_length(engine, point, &point_length);
    float step = (float)((sqrt((double)distance_squared(&near, &from)) + point_length) / curve_length);
    set_out(b, 0, &near, sizeof(near));
    set_out_float(b, 1, step);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * The DLL's local box of an entity with its hierarchy: its hierarchical box
 * in its own space while it is turned to the world's axes (children turned
 * with it), as {Max, Min}; zero when nothing in it has geometry.
 */
static void hierarchy_local_box(nmo_engine_t *engine, int32_t index, eng_vector_t box[2])
{
    eng_matrix_t saved = engine->entities.data[index].world;
    const eng_vector_t dir = {0, 0, 1}, up = {0, 1, 0}, right = {1, 0, 0};
    eng_entity_set_orientation(engine, index, &dir, &up, &right, -1, false);
    eng_vector_t min, max;
    box[0] = box[1] = (eng_vector_t){0, 0, 0};
    if (eng_entity_hierarchical_box(engine, index, &min, &max)) {
        eng_entity_inverse_transform(engine, index, &box[0], &max, -1);
        eng_entity_inverse_transform(engine, index, &box[1], &min, -1);
    }
    eng_entity_set_world_matrix(engine, index, &saved, false);
}

/* CK3dEntity::GetBoundingBox(TRUE), or the hierarchy's local box, as {Max, Min} */
static void entity_local_box(nmo_engine_t *engine, int32_t index, bool hierarchy, eng_vector_t box[2])
{
    if (hierarchy) {
        hierarchy_local_box(engine, index, box);
    } else {
        eng_entity_local_box(engine, index, &box[1], &box[0]);
    }
}

/*
 * TT Bounding Sphere Intersection: each entity's sphere is its local box's
 * corners in the world, centred at their sum times Threshold, with the
 * squared diagonal times Threshold as its "squared radius"; True when the
 * squared distance of the centres is at most the sum of those, as in the
 * DLL.
 */
static int bb_bounding_sphere_intersection(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    int32_t entities[2] = {input_entity(b, 0), input_entity(b, 1)};
    if (entities[0] < 0 || entities[1] < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through a missing entity */
    }
    float threshold = get_in_float(b, 4, 0.5f);
    eng_vector_t centres[2];
    float radii[2];
    for (int e = 0; e < 2; e++) {
        eng_vector_t box[2], world[2];
        entity_local_box(engine, entities[e], get_in_int(b, 2 + e, 0) != 0, box);
        eng_entity_transform(engine, entities[e], &world[0], &box[0], -1);
        eng_entity_transform(engine, entities[e], &world[1], &box[1], -1);
        radii[e] = distance_squared(&world[0], &world[1]) * threshold;
        centres[e] = (eng_vector_t){(world[0].x + world[1].x) * threshold, (world[1].y + world[0].y) * threshold,
                                    (world[1].z + world[0].z) * threshold};
    }
    float d = distance_squared(&centres[1], &centres[0]);
    OUT_ON(b, d <= radii[1] + radii[0] ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* The DLL's segment test against a box ({Max, Min}): separating axes */
static bool segment_box(const eng_vector_t *origin, const eng_vector_t *dir, const eng_vector_t box[2])
{
    float sx = box[1].x + box[0].x, sy = box[1].y + box[0].y, sz = box[1].z + box[0].z;
    float cx = sx == 0.0f ? 0.0f : sx * 0.5f, cy = sy == 0.0f ? 0.0f : sy * 0.5f;
    float cz = sz == 0.0f ? 0.0f : sz * 0.5f;
    float hx = box[0].x - cx, hy = box[0].y - cy, hz = box[0].z - cz;
    float wx = dir->x * 0.5f, wy = dir->y * 0.5f, wz = dir->z * 0.5f;
    float mx = wx + (origin->x - cx), my = (origin->y - cy) + wy, mz = origin->z - cz + wz;
    if (fabsf(wx) + hx < fabsf(mx) || fabsf(wy) + hy < fabsf(my) || fabsf(wz) + hz < fabsf(mz)) {
        return false;
    }
    float ay = fabsf(dir->y), az = fabsf(dir->z), ax = fabsf(dir->x);
    return !(ay * hz + az * hy < fabsf(dir->z * my - mz * dir->y)) &&
           ax * hz + az * hx >= fabsf(mz * dir->x - dir->z * mx) &&
           ax * hy + ay * hx >= fabsf(dir->y * mx - my * dir->x);
}

/*
 * TT CheckCurveCollision: TRUE when a segment between consecutive control
 * points of a curve in the group crosses the target's local box (with its
 * hierarchy's on HierachieBB). With the MinDistance setting, MinDistance is
 * the least of |T-p| + |T-q| - |q-p| over the segments looked at before
 * one crossed (1e12 for none).
 */
static int bb_check_curve_collision(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR; /* the DLL throws */
    }
    if (group == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool hierarchy = get_in_int(b, 1, 0) != 0;
    bool want_min = get_local_int(b, 0, 1) != 0;
    eng_vector_t box[2];
    entity_local_box(engine, target, hierarchy, box);
    eng_vector_t t = eng_entity_position(engine, target, -1);
    float min_distance = 1.0e12f;
    bool hit = false;
    int32_t count = nmo_engine_group_count(engine, group);
    for (int32_t g = 0; g < count && !hit; g++) {
        const eng_curve_points_t *view = eng_curve_points(engine, nmo_engine_group_object(engine, group, g));
        if (view == NULL || view->count < 2) {
            continue;
        }
        size_t n = view->count;
        nmo_object_id_t *ids = (nmo_object_id_t *)malloc(n * sizeof(*ids));
        if (ids == NULL) {
            break;
        }
        memcpy(ids, view->ids, n * sizeof(*ids));
        for (size_t j = 0; j + 1 < n && !hit; j++) {
            int32_t pi = eng_entity_index(engine, ids[j]), qi = eng_entity_index(engine, ids[j + 1]);
            if (pi < 0 || qi < 0) {
                continue;
            }
            eng_vector_t p = eng_entity_position(engine, pi, target), q = eng_entity_position(engine, qi, target);
            if (want_min) {
                eng_vector_t pw = eng_entity_position(engine, pi, -1), qw = eng_entity_position(engine, qi, -1);
                double d = sqrt((double)distance_squared(&t, &qw)) + sqrt((double)distance_squared(&t, &pw)) -
                           sqrt((double)distance_squared(&qw, &pw));
                if (d < min_distance) {
                    min_distance = (float)d;
                }
            }
            eng_vector_t dir = {q.x - p.x, q.y - p.y, q.z - p.z};
            hit = segment_box(&p, &dir, box);
        }
        free(ids);
    }
    if (hit) {
        OUT_ON(b, 0);
    } else {
        OUT_ON(b, 1);
    }
    if (want_min) {
        set_out_float(b, 0, min_distance);
    }
    IN_OFF(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* SquareMagnitude-based normalisation, as the DLL inlines it (no zero test) */
static eng_vector_t unit_of(const eng_vector_t *v)
{
    double inv = 1.0 / sqrt((double)(v->x * v->x + v->y * v->y + v->z * v->z));
    return (eng_vector_t){(float)(inv * v->x), (float)(v->y * inv), (float)(v->z * inv)};
}

/*
 * TT Homing Missile: the target flies at Missilespeed (per second) toward
 * Destination, its velocity turned toward it by Adaptationspeed each frame
 * (In starts it straight at it), facing where it flies; Target Reach once it
 * is within the destination's radius, else Loop Out.
 */
static int bb_homing_missile(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t missile = target_entity(b), destination = input_entity(b, 0);
    if (missile < 0 || destination < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t from = eng_entity_position(engine, missile, -1);
    eng_vector_t to = eng_entity_position(engine, destination, -1);
    eng_vector_t offset = {to.x - from.x, to.y - from.y, to.z - from.z};
    float speed = (float)(nmo_engine_block_delta_time(b) * 0.001 * get_in_float(b, 1, 1.0f));
    eng_vector_t velocity = {0, 0, 0};
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        eng_vector_t u = unit_of(&offset);
        velocity = (eng_vector_t){u.x * speed, u.y * speed, u.z * speed};
    } else {
        nmo_engine_block_get_output_value(b, 0, &velocity, sizeof(velocity));
        IN_OFF(b, 1);
    }
    float adapt = get_in_float(b, 2, 0.5f);
    eng_vector_t toward = unit_of(&offset), heading = unit_of(&velocity);
    float keep = (float)(1.0 - adapt);
    velocity = (eng_vector_t){heading.x * keep + toward.x * adapt, heading.y * keep + toward.y * adapt,
                              keep * heading.z + toward.z * adapt};
    eng_vector_normalize(&velocity);
    velocity = (eng_vector_t){velocity.x * speed, velocity.y * speed, velocity.z * speed};
    eng_vector_t dir = unit_of(&velocity);
    eng_vector_t world_up = {0, 1, 0};
    eng_vector_t right = eng_vector_cross(&world_up, &dir);
    eng_vector_normalize(&right);
    eng_vector_t up = eng_vector_cross(&dir, &right);
    eng_vector_normalize(&up);
    eng_vector_t next = {velocity.x + from.x, velocity.y + from.y, velocity.z + from.z};
    eng_entity_set_orientation(engine, missile, &dir, &up, &right, -1, false);
    eng_entity_set_position(engine, missile, &next, -1, false);
    set_out(b, 0, &velocity, sizeof(velocity));
    float distance = (float)sqrt((double)distance_squared(&next, &to));
    OUT_ON(b, eng_entity_radius(engine, destination) <= distance ? 1 : 0);
    return NMO_ENGINE_BR_OK;
}

enum { FOLLOW_TIME_BASED, FOLLOW_INIT_ACT_POS, FOLLOW_VIEW_OLD, FOLLOW_CAM_OLD, FOLLOW_OLD_DELTA, FOLLOW_INIT };

/*
 * TT FollowCamera: a target camera chasing Target Entity: its target eases
 * toward the entity's View Offset point and the camera toward its Camera
 * Offset point (by the attenuations a frame, scaled by the averaged frame
 * time when Time Based), pushed back by Back Attenuation when nearer the
 * entity than the offset. On starts from the camera and its target where
 * they are (Init, set on load and script activation and never cleared);
 * the InitActPos setting is unused. The DLL positions the camera's target
 * (no LookAt) and needs one; without a target here the view point is only
 * tracked.
 */
static int bb_follow_camera(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t camera = target_entity(b);
    if (camera < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t camera_id = nmo_engine_block_target(b);
    int32_t view = eng_entity_index(engine, eng_camera_target(engine, camera_id));
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t entity = input_entity(b, 0);
    eng_vector_t view_offset = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
    float view_att = get_in_float(b, 2, 1.0f);
    eng_vector_t cam_offset = get_in_vector(b, 3, 0.0f, 0.0f, 0.0f);
    float cam_att = get_in_float(b, 4, 1.0f);
    float fov = get_in_float(b, 5, 0.0f);
    float back_att = get_in_float(b, 6, 0.25f);
    eng_matrix_t world = engine->entities.data[camera].world;
    if (world.m[0][0] < 0.0f) {
        world.m[0][0] = fabsf(world.m[0][0]);
        world.m[1][1] = fabsf(world.m[1][1]);
        eng_entity_set_world_matrix(engine, camera, &world, false);
    }
    eng_set_camera_fov(engine, camera_id, fov);
    if (entity < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL transforms through a missing entity */
    }
    eng_vector_t view_goal, cam_goal;
    eng_entity_transform(engine, entity, &view_goal, &view_offset, -1);
    eng_entity_transform(engine, entity, &cam_goal, &cam_offset, -1);
    eng_vector_t view_old = {0, 0, 0}, cam_old = {0, 0, 0};
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        if (get_local_int(b, FOLLOW_INIT, 0) != 0) {
            cam_old = eng_entity_position(engine, camera, -1);
            view_old = view >= 0 ? eng_entity_position(engine, view, -1) : view_goal;
        } else {
            view_old = view_goal;
            cam_old = cam_goal;
        }
        nmo_engine_block_set_local_value(b, FOLLOW_VIEW_OLD, &view_old, sizeof(view_old));
        nmo_engine_block_set_local_value(b, FOLLOW_CAM_OLD, &cam_old, sizeof(cam_old));
        float delta = nmo_engine_block_delta_time(b);
        nmo_engine_block_set_local_value(b, FOLLOW_OLD_DELTA, &delta, sizeof(delta));
    }
    nmo_engine_block_get_local_value(b, FOLLOW_VIEW_OLD, &view_old, sizeof(view_old));
    nmo_engine_block_get_local_value(b, FOLLOW_CAM_OLD, &cam_old, sizeof(cam_old));
    eng_vector_t view_delta = {view_goal.x - view_old.x, view_goal.y - view_old.y, view_goal.z - view_old.z};
    eng_vector_t cam_delta = {cam_goal.x - cam_old.x, cam_goal.y - cam_old.y, cam_goal.z - cam_old.z};
    if (get_local_int(b, FOLLOW_TIME_BASED, 0) != 0) {
        float delta = nmo_engine_block_delta_time(b);
        float old = get_local_float(b, FOLLOW_OLD_DELTA, 0.0f);
        float most = (float)(delta * 1.1);
        if (old > most) {
            delta = most;
        }
        float least = (float)(delta - delta * 0.1);
        if (old >= least) {
            least = delta;
        }
        float average = (float)((old * 4.0f + least) * 0.2);
        view_att = (float)(average * view_att * 0.07);
        view_att = view_att < 0.0f ? 0.0f : view_att > 1.0f ? 1.0f : view_att;
        cam_att = (float)(average * cam_att * 0.07);
        cam_att = cam_att < 0.0f ? 0.0f : cam_att > 1.0f ? 1.0f : cam_att;
        nmo_engine_block_set_local_value(b, FOLLOW_OLD_DELTA, &average, sizeof(average));
    }
    view_old = (eng_vector_t){view_delta.x * view_att + view_old.x, view_delta.y * view_att + view_old.y,
                              view_delta.z * view_att + view_old.z};
    cam_old = (eng_vector_t){cam_delta.x * cam_att + cam_old.x, cam_delta.y * cam_att + cam_old.y,
                             cam_delta.z * cam_att + cam_old.z};
    eng_vector_t local;
    eng_entity_inverse_transform(engine, entity, &local, &cam_old, -1);
    if (eng_vector_magnitude(&cam_offset) > eng_vector_magnitude(&local)) {
        cam_old = (eng_vector_t){(cam_goal.x - cam_old.x) * back_att + cam_old.x,
                                 (cam_goal.y - cam_old.y) * back_att + cam_old.y,
                                 (cam_goal.z - cam_old.z) * back_att + cam_old.z};
    }
    if (view >= 0) {
        eng_entity_set_position(engine, view, &view_old, -1, false);
    }
    eng_entity_set_position(engine, camera, &cam_old, -1, false);
    nmo_engine_block_set_local_value(b, FOLLOW_VIEW_OLD, &view_old, sizeof(view_old));
    nmo_engine_block_set_local_value(b, FOLLOW_CAM_OLD, &cam_old, sizeof(cam_old));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT FollowCamera's callback: Init on load, a new scene and script activation (and attach in CK) */
static int follow_camera_callback(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_NEWSCENE ||
        message == NMO_ENGINE_MSG_ACTIVATESCRIPT) {
        set_local_int(b, FOLLOW_INIT, 1);
    }
    return NMO_ENGINE_BR_OK;
}

/* ---- Mesh deformers ---- */

/* The DLL's sine tables: sin of each whole degree */
static float sine_degrees(int32_t degree)
{
    return (float)sin((double)degree * (double)0.0027777778f * (double)6.2831855f);
}

/* The target's current mesh as a run time copy, its vertex count in `count`; NULL for none */
static eng_mesh_copy_t *deformed_mesh(nmo_engine_t *engine, nmo_object_id_t entity, uint32_t *count)
{
    nmo_object_id_t mesh = entity != 0 ? current_mesh_of(engine, entity) : 0;
    nmo_object_t *object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    const nmo_mesh_state_t *state = object != NULL ? (const nmo_mesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_MESH) : NULL;
    if (state == NULL) {
        return NULL;
    }
    *count = state->vertex_count;
    return eng_mesh_copy(engine, mesh);
}

/* The callbacks' copy of the mesh's positions into local `pos` (attach, load, edit) */
static void save_vertices(blk_t *b, int pos)
{
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(b->engine, nmo_engine_block_target(b), &count);
    if (copy == NULL || count == 0) {
        return;
    }
    nmo_vector_t *saved = (nmo_vector_t *)malloc(count * sizeof(*saved));
    if (saved == NULL) {
        return;
    }
    for (uint32_t v = 0; v < count; v++) {
        saved[v] = copy->vertices[v].position;
    }
    nmo_engine_block_set_local_value(b, pos, saved, count * sizeof(*saved));
    free(saved);
}

/* The positions local `pos` holds, back on the mesh (delete, detach, reset) */
static void restore_vertices(blk_t *b, int pos)
{
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(b->engine, nmo_engine_block_target(b), &count);
    const eng_param_t *saved = block_local(b, pos);
    if (copy == NULL || saved == NULL || saved->data == NULL || saved->size < sizeof(nmo_vector_t)) {
        return;
    }
    const nmo_vector_t *positions = (const nmo_vector_t *)saved->data;
    for (uint32_t v = 0; v < count && (v + 1) * sizeof(nmo_vector_t) <= saved->size; v++) {
        copy->vertices[v].position = positions[v];
    }
    eng_mesh_vertices_moved(b->engine, copy, true);
}

/* The positions local `pos` holds, `count` at most; NULL for none */
static const nmo_vector_t *saved_vertices(blk_t *b, int pos, uint32_t *count)
{
    const eng_param_t *saved = block_local(b, pos);
    if (saved == NULL || saved->data == NULL) {
        return NULL;
    }
    uint32_t held = (uint32_t)(saved->size / sizeof(nmo_vector_t));
    *count = held < *count ? held : *count;
    return (const nmo_vector_t *)saved->data;
}

/*
 * TT_GeneratePlane: a "generated Entity" in the scene at Position, scaled
 * (in the world) by Scale, with a "generated Mesh" of Size X by Size Y unit
 * squares in XZ going -Z, four vertices and two faces a square, face normals
 * only; its input stays active, as in the DLL.
 */
static int bb_generate_plane(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    OUT_ON(b, 0);
    eng_vector_t position = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f), scale = get_in_vector(b, 1, 1.0f, 1.0f, 1.0f);
    int32_t size_x = get_in_int(b, 2, 1), size_y = get_in_int(b, 3, 1);
    int32_t squares = size_x > 0 && size_y > 0 ? size_x * size_y : 0;
    nmo_mesh_state_t *state = NULL;
    nmo_object_id_t mesh = eng_create_mesh(engine, "generated Mesh", 4 * squares, 2 * squares, &state);
    nmo_object_id_t entity = eng_create_object(engine, NMO_CID_3DENTITY, "generated Entity");
    if (mesh == 0 || entity == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t v = 0, f = 0;
    for (int32_t y = 0; y < size_y && squares > 0; y++) {
        for (int32_t x = 0; x < size_x; x++, v += 4, f += 2) {
            const float near = -(float)y, far = -(float)(y + 1);
            state->vertices[v].position = (nmo_vector_t){(float)x, 0.0f, near};
            state->vertices[v + 1].position = (nmo_vector_t){(float)x, 0.0f, far};
            state->vertices[v + 2].position = (nmo_vector_t){(float)(x + 1), 0.0f, far};
            state->vertices[v + 3].position = (nmo_vector_t){(float)(x + 1), 0.0f, near};
            const uint16_t faces[6] = {(uint16_t)v, (uint16_t)(v + 3), (uint16_t)(v + 2),
                                       (uint16_t)(v + 1), (uint16_t)v, (uint16_t)(v + 2)};
            memcpy(&state->face_vertex_indices[3 * f], faces, sizeof(faces));
        }
    }
    /* BuildFaceNormals: each square's faces face up */
    for (int32_t i = 0; i < 2 * squares; i++) {
        state->faces[i].normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
    }
    nmo_mesh_update_bounding_volumes(state);
    eng_entity_set_current_mesh(engine, entity, mesh);
    int32_t index = eng_entity_index(engine, entity);
    if (index >= 0) {
        eng_entity_set_position(engine, index, &position, -1, false);
        eng_entity_set_scale(engine, index, &scale, false, false);
    }
    eng_add_to_level_scene(engine, entity);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_HighMapMeshform: each vertex's height is the alpha of the texel its UVs
 * point at in the texture of the mesh's material MatChannel + 1 (taken as
 * its material groups' list), times Height / 100 (X and Z kept).
 */
static int bb_high_map_meshform(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, target, &count);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t channel = get_in_int(b, 0, -1);
    nmo_object_id_t material = nmo_engine_mesh_material(engine, copy->mesh, channel + 1);
    eng_material_t *desc = material != 0 ? eng_material(engine, material) : NULL;
    eng_texture_t *texture = desc != NULL && desc->desc.texture != 0 ? eng_texture(engine, desc->desc.texture) : NULL;
    eng_surface_t *surface = texture != NULL ? eng_texture_surface(engine, texture, -1) : NULL;
    if (surface == NULL || surface->rgba == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through a missing material or texture */
    }
    float height = get_in_float(b, 1, 1.0f);
    size_t stride = 0;
    int32_t uv_count = 0;
    const float *uvs = eng_mesh_uvs(engine, copy, -1, &stride, &uv_count);
    int32_t w = surface->width, h = surface->height;
    for (int32_t v = 0; uvs != NULL && v < uv_count && (uint32_t)v < count; v++) {
        const float *uv = (const float *)((const uint8_t *)uvs + (size_t)v * stride);
        int32_t x = abs((int32_t)(int64_t)((double)w * uv[0]) % (w + 1));
        int32_t y = h - abs((int32_t)(int64_t)((double)h * uv[1]) % (h + 1));
        /* the DLL reads past the image at x == w or y == h; nothing there here */
        int32_t alpha = x < w && y < h ? surface->rgba[4 * ((size_t)y * (size_t)w + (size_t)x) + 3] : 0;
        copy->vertices[v].position.y = (float)((double)alpha * height * 0.01);
    }
    eng_mesh_vertices_moved(engine, copy, true);
    return NMO_ENGINE_BR_OK;
}

/* The callbacks of the deformers keeping the mesh's positions in local `pos` */
static int deformer_callback(blk_t *b, uint32_t message, int pos)
{
    if (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_EDITED) {
        save_vertices(b, pos);
    } else if (message == NMO_ENGINE_MSG_RESET) {
        restore_vertices(b, pos);
    }
    return NMO_ENGINE_BR_OK;
}

enum { MAGNET_SINUS, MAGNET_VARRAY, MAGNET_TIME };

/*
 * TT MagnetMeshForm: each vertex of the rest shape (VArray, kept at load)
 * pulled toward the Magnet-Entity: nothing from Max Distance off its
 * radius, Strength at Min Distance, falling to nothing at its centre; with
 * Sinus-Deform the pull swings by 30% along Z over time. Off leaves the
 * mesh as it is.
 */
static int bb_magnet_mesh_form(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target_id = nmo_engine_block_target(b);
    int32_t target = target_entity(b), magnet = input_entity(b, 0);
    float max_distance = get_in_float(b, 1, 0.0f), min_distance = get_in_float(b, 2, 0.0f);
    float strength = get_in_float(b, 3, 0.0f);
    bool sinus = get_local_int(b, MAGNET_SINUS, 0) != 0;
    float time = 0.0f;
    if (sinus) {
        time = (float)(nmo_engine_block_delta_time(b) * 0.0099999998 + get_local_float(b, MAGNET_TIME, 0.0f));
        nmo_engine_block_set_local_value(b, MAGNET_TIME, &time, sizeof(time));
    }
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, target_id, &count);
    if (target < 0 || magnet < 0 || copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR; /* the DLL calls through them */
    }
    const nmo_vector_t *rest = saved_vertices(b, MAGNET_VARRAY, &count);
    eng_vector_t centre = eng_entity_position(engine, magnet, target);
    float radius = eng_entity_radius(engine, magnet);
    float slope = (float)(1.0 / (min_distance - max_distance) * strength);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        float zero = 0.0f;
        nmo_engine_block_set_local_value(b, MAGNET_TIME, &zero, sizeof(zero));
    }
    for (uint32_t v = 0; rest != NULL && v < count; v++) {
        eng_vector_t d = {centre.x - rest[v].x, centre.y - rest[v].y, centre.z - rest[v].z};
        float distance = (float)(sqrt((double)(d.z * d.z + d.y * d.y + d.x * d.x)) - radius);
        if (distance >= max_distance) {
            copy->vertices[v].position = rest[v];
            continue;
        }
        float pull = distance <= min_distance ? (distance + radius) / (radius + min_distance) * strength
                                              : (distance - max_distance) * slope;
        if (sinus) {
            int32_t degree = (int32_t)(int64_t)(fabs((double)(rest[v].z - time)) * 50.0) % 360;
            pull = (float)((sine_degrees(degree) * 0.3 + 1.0) * pull);
        }
        eng_vector_normalize(&d);
        copy->vertices[v].position = (nmo_vector_t){d.x * pull + rest[v].x, d.y * pull + rest[v].y,
                                                    d.z * pull + rest[v].z};
    }
    eng_mesh_vertices_moved(engine, copy, true);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int magnet_mesh_form_callback(blk_t *b, uint32_t message)
{
    return deformer_callback(b, message, MAGNET_VARRAY);
}

enum { SINUS_COUNT, SINUS_VERTICES, SINUS_WAVES };

/* TT SinusMeshdeform's wave, as the DLL keeps it */
typedef struct sinus_wave {
    float height, wavelength, k; /* k: the box diagonal times 6.28 over the wavelength */
    float time, phase;           /* phase: time times k */
    float speed;                 /* Timefactor / 100 */
    float x, y, z;               /* SinPos, Y cleared */
} sinus_wave_t;

/*
 * TT SinusMeshdeform: each vertex's height is the sum of SinusCount circular
 * sine waves (the DLL's sine table, in degrees) round their SinPos, of
 * Height, with a wavelength in units of the target's box diagonal, moving
 * by Timefactor (X and Z kept). On reads the waves from the last
 * 4 x SinusCount inputs.
 */
static int bb_sinus_meshdeform(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t waves = get_local_int(b, SINUS_COUNT, 0);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
        eng_entity_local_box(engine, target, &min, &max);
        float dy = min.y - max.y, dz = min.z - max.z, dx = min.x - max.x;
        float diagonal = (float)(sqrt((double)(dz * dz + dy * dy + dx * dx)) * 6.2800002);
        sinus_wave_t *made = waves > 0 ? (sinus_wave_t *)calloc((size_t)waves, sizeof(*made)) : NULL;
        int32_t first = nmo_engine_block_input_parameter_count(b) - 4 * waves;
        for (int32_t i = 0; made != NULL && i < waves; i++) {
            sinus_wave_t *w = &made[i];
            w->speed = (float)(get_in_float(b, first + 4 * i, 0.0f) * 0.0099999998);
            w->height = get_in_float(b, first + 4 * i + 1, 0.0f);
            w->wavelength = get_in_float(b, first + 4 * i + 2, 0.0f);
            w->k = diagonal / w->wavelength;
            eng_vector_t at = get_in_vector(b, first + 4 * i + 3, 0.0f, 0.0f, 0.0f);
            w->x = at.x;
            w->z = at.z;
        }
        if (made != NULL) {
            nmo_engine_block_set_local_value(b, SINUS_WAVES, made, (size_t)waves * sizeof(*made));
        }
        free(made);
    }
    eng_param_t *param = block_local(b, SINUS_WAVES);
    sinus_wave_t *wave = param != NULL ? (sinus_wave_t *)param->data : NULL;
    int32_t held = param != NULL ? (int32_t)(param->size / sizeof(sinus_wave_t)) : 0;
    waves = waves < held ? waves : held;
    float delta = nmo_engine_block_delta_time(b);
    for (int32_t i = 0; i < waves; i++) {
        wave[i].time = wave[i].speed * delta + wave[i].time;
        wave[i].phase = wave[i].time * wave[i].k;
    }
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, nmo_engine_block_target(b), &count);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    for (uint32_t v = 0; v < count; v++) {
        nmo_vector_t *p = &copy->vertices[v].position;
        float y = 0.0f;
        for (int32_t i = 0; i < waves; i++) {
            float dx = p->x - wave[i].x, dz = p->z - wave[i].z, py = -wave[i].y;
            double distance = sqrt((double)(dz * dz + py * py + dx * dx));
            int32_t degree = (int32_t)(int64_t)fabs(distance * wave[i].k - wave[i].phase) % 360;
            y = sine_degrees(degree) * wave[i].height + y;
        }
        p->y = y;
    }
    eng_mesh_vertices_moved(engine, copy, true);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int sinus_meshdeform_callback(blk_t *b, uint32_t message)
{
    return deformer_callback(b, message, SINUS_VERTICES);
}

enum { PMS_COUNT, PMS_VERTICES, PMS_TIME, PMS_WAVE_X, PMS_WAVE_Y, PMS_WIND };

/* TT PMS_Meshdeform's wave, as the DLL keeps it (the X waves use the first four) */
typedef struct pms_wave {
    float wavelength, k, phase, amplitude, moved_phase, speed, frequency;
} pms_wave_t;

/* MSVC's rand() after srand(seed) */
static int32_t msvc_rand(uint32_t *state)
{
    *state = *state * 214013u + 2531011u;
    return (int32_t)((*state >> 16) & 0x7FFFu);
}

/* The DLL's random number: from `low`, `range` wide */
static float pms_random(uint32_t *state, float low, float range)
{
    return (float)((double)msvc_rand(state) * range * 0.000030518509 + low);
}

/* The DLL's sine: its table at |a x 359| degrees */
static float pms_sine(float a)
{
    double degrees = (double)a * 359.0;
    if (degrees < 0.0) {
        degrees = degrees * -1.0;
    }
    return sine_degrees((int32_t)(int64_t)degrees % 360);
}

/*
 * TT PMS_Meshdeform: the height of each vertex from Wellenanzahl
 * Pierson-Moskowitz waves along X (turned by Winddirection, scaled by
 * WaveScale / 5), their X first pushed by fixed waves along Z (X and Z
 * kept). In sets the waves up from srand(10), as the DLL does; as in the
 * DLL the pushed X is twice the vertex's X plus the push, and each height
 * wave is joined by one of half its wave number at 0.8 its amplitude.
 */
static int bb_pms_meshdeform(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t waves = get_local_int(b, PMS_COUNT, 1);
    float wind = get_in_float(b, 0, 0.0f), max_amplitude = get_in_float(b, 1, 0.6f);
    float wave_scale = (float)(get_in_float(b, 2, 1.0f) * 0.2);
    float time_scale = get_in_float(b, 3, 1.0f);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        uint32_t seed = 10;
        pms_wave_t *xs = waves > 0 ? (pms_wave_t *)calloc((size_t)waves, sizeof(*xs)) : NULL;
        pms_wave_t *ys = waves > 0 ? (pms_wave_t *)calloc((size_t)waves, sizeof(*ys)) : NULL;
        const double pi2_4 = pow(6.283185482025146, 4.0);
        for (int32_t i = 0; xs != NULL && ys != NULL && i < waves; i++) {
            pms_wave_t *y = &ys[i], *x = &xs[i];
            y->wavelength = get_in_float(b, 4 + 3 * i, 0.0f);
            y->k = (float)(6.2831855 / y->wavelength);
            y->frequency = (float)(pow((double)y->k * 9.81, 0.5) * 0.1591549386630057);
            y->amplitude = (float)(0.7698888000000002 / (pow((double)y->frequency, 5.0) * pi2_4));
            y->amplitude = (float)(exp(-pow((double)y->frequency * 3.92156862745098, -4.0)) * y->amplitude);
            y->amplitude = (float)(y->amplitude / (0.16 / max_amplitude));
            y->amplitude = pms_random(&seed, -1.0f, 1.0f) * y->amplitude;
            y->phase = pms_random(&seed, 0.0f, 6.2831855f);
            y->speed = (float)(pow(9.81 / y->k, 0.5) * 5.0);
            x->wavelength = get_in_float(b, 5 + 3 * i, 0.0f);
            x->amplitude = get_in_float(b, 6 + 3 * i, 0.0f);
            x->k = x->wavelength == 0.0f ? 0.0f : (float)(6.2831855 / x->wavelength);
            x->phase = pms_random(&seed, 0.0f, 6.2831855f);
        }
        if (xs != NULL && ys != NULL) {
            nmo_engine_block_set_local_value(b, PMS_WAVE_X, xs, (size_t)waves * sizeof(*xs));
            nmo_engine_block_set_local_value(b, PMS_WAVE_Y, ys, (size_t)waves * sizeof(*ys));
        }
        free(xs);
        free(ys);
        eng_matrix_t matrix;
        const eng_vector_t up = {0.0f, 1.0f, 0.0f};
        eng_matrix_from_rotation(&matrix, &up, wind);
        nmo_engine_block_set_local_value(b, PMS_WIND, &matrix, sizeof(matrix));
    }
    float time = (float)(time_scale * nmo_engine_block_delta_time(b) * 0.000099999997 +
                         get_local_float(b, PMS_TIME, 0.0f));
    nmo_engine_block_set_local_value(b, PMS_TIME, &time, sizeof(time));
    eng_param_t *xp = block_local(b, PMS_WAVE_X), *yp = block_local(b, PMS_WAVE_Y);
    pms_wave_t *xs = xp != NULL ? (pms_wave_t *)xp->data : NULL, *ys = yp != NULL ? (pms_wave_t *)yp->data : NULL;
    int32_t held_x = xp != NULL ? (int32_t)(xp->size / sizeof(pms_wave_t)) : 0;
    int32_t held_y = yp != NULL ? (int32_t)(yp->size / sizeof(pms_wave_t)) : 0;
    waves = waves < held_x ? waves : held_x;
    waves = waves < held_y ? waves : held_y;
    eng_matrix_t matrix;
    memset(&matrix, 0, sizeof(matrix));
    nmo_engine_block_get_local_value(b, PMS_WIND, &matrix, sizeof(matrix));
    for (int32_t i = 0; i < waves; i++) {
        ys[i].moved_phase = (float)(ys[i].phase - (double)time * ys[i].k * ys[i].speed);
    }
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, target, &count);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    for (uint32_t v = 0; v < count; v++) {
        nmo_vector_t *p = &copy->vertices[v].position;
        eng_vector_t original = {p->x, p->y, p->z}, turned;
        eng_vector_rotate(&turned, &matrix, &original);
        turned.x = turned.x * wave_scale;
        turned.y = turned.y * wave_scale;
        turned.z = turned.z * wave_scale;
        float push = turned.x;
        for (int32_t i = 0; i < waves; i++) {
            push = pms_sine(turned.z * xs[i].k - xs[i].phase) * xs[i].amplitude + push;
        }
        turned.x = turned.x + push;
        float height = 0.0f;
        for (int32_t i = 0; i < waves; i++) {
            height = pms_sine(turned.x * ys[i].k - ys[i].moved_phase) * ys[i].amplitude + height;
            height = (float)(pms_sine((float)(turned.x * ys[i].k * 0.5f) - ys[i].moved_phase) * ys[i].amplitude *
                             0.80000001 + height);
        }
        p->y = height;
    }
    eng_mesh_vertices_moved(engine, copy, true);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int pms_meshdeform_callback(blk_t *b, uint32_t message)
{
    return deformer_callback(b, message, PMS_VERTICES);
}

enum { RIPPLE_GRID, RIPPLE_VERTICES };

/*
 * TT_RippleWave: a square grid of heights and speeds over the mesh's
 * vertices (a square count of them, in rows): each speed eases toward its
 * neighbours' weighted heights and the heights move by it; a Drop pushes a
 * 3 x 3 dent in at a random point. The vertices keep the X and Z In saved
 * and take the heights. As in the DLL the speed of the easing is Viscosity
 * too (Speed is not read).
 */
static int bb_ripple_wave(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    uint32_t count = 0;
    eng_mesh_copy_t *copy = target != 0 ? deformed_mesh(engine, target, &count) : NULL;
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t side = (int32_t)(int64_t)pow((double)count, 0.5);
    if ((uint32_t)(side * side) != count) {
        return NMO_ENGINE_BR_OWNERERROR; /* "Mesh hat keine quadratische Vertexanzahl!" */
    }
    float drop = get_in_float(b, 0, 0.0f);
    float viscosity = get_in_float(b, 1, 0.0f), speed = get_in_float(b, 1, 0.0f);
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        save_vertices(b, RIPPLE_VERTICES);
        float *grid = (float *)calloc(count > 0 ? 2u * count : 1u, sizeof(float));
        if (grid != NULL) {
            nmo_engine_block_set_local_value(b, RIPPLE_GRID, grid, 2u * count * sizeof(float));
        }
        free(grid);
    }
    eng_param_t *gp = block_local(b, RIPPLE_GRID);
    float *grid = gp != NULL && gp->size >= 2u * count * sizeof(float) ? (float *)gp->data : NULL;
    uint32_t saved_count = count;
    const nmo_vector_t *saved = saved_vertices(b, RIPPLE_VERTICES, &saved_count);
    if (grid == NULL || saved == NULL || saved_count < count) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME; /* the DLL reads what In never made */
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        float range = (float)(side - 1);
        int32_t column = (int32_t)(int64_t)((double)eng_random_u15(engine) * range * 0.000030518509 + 1.0);
        int32_t row = (int32_t)(int64_t)((double)eng_random_u15(engine) * range * 0.000030518509 + 1.0);
        static const float weights[3][3] = {{0.0625f, 0.125f, 0.0625f}, {0.125f, 0.25f, 0.125f},
                                            {0.0625f, 0.125f, 0.0625f}};
        for (int32_t dr = -1; dr <= 1; dr++) {
            for (int32_t dc = -1; dc <= 1; dc++) {
                int64_t cell = (int64_t)(row + dr) * side + column + dc;
                if (cell >= 0 && cell < (int64_t)count) { /* the DLL writes past the grid there */
                    grid[2 * cell + 1] = grid[2 * cell + 1] - drop * weights[dr + 1][dc + 1];
                }
            }
        }
    }
    for (int32_t r = 0; r < side; r++) {
        for (int32_t c = 0; c < side; c++) {
            float *cell = &grid[2 * ((size_t)r * (size_t)side + (size_t)c)];
            double mean = 0.0;
            if (r <= 1 || r >= side - 1 || c <= 1 || c >= side - 1) {
                cell[0] = 0.0f;
                cell[1] = 0.0f;
            } else {
#define H(rr, cc) ((double)grid[2 * ((size_t)(rr) * (size_t)side + (size_t)(cc))])
                double diagonal = H(r + 1, c + 1) + H(r - 1, c + 1) + H(r + 1, c - 1) + H(r - 1, c - 1);
                double straight = H(r, c - 1) + H(r, c + 1) + H(r - 1, c) + H(r + 1, c);
#undef H
                mean = diagonal * 0.082999997 + straight * 0.16599999;
            }
            float eased = viscosity * cell[1];
            cell[1] = (float)((mean - cell[0]) * speed + eased);
        }
    }
    for (uint32_t v = 0; v < count; v++) {
        float height = grid[2 * v + 1] + grid[2 * v];
        grid[2 * v] = height;
        copy->vertices[v].position = (nmo_vector_t){saved[v].x, height, saved[v].z};
    }
    eng_mesh_vertices_moved(engine, copy, true);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT_RippleWave's callback: the saved positions back on delete, detach and reset (none saved at load) */
static int ripple_wave_callback(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_RESET) {
        restore_vertices(b, RIPPLE_VERTICES);
    }
    return NMO_ENGINE_BR_OK;
}

/* ---- Materials and mapping ---- */

/* CKMesh::UVChanged: the copy's UVs are new */
static void uvs_changed(blk_t *b, eng_mesh_copy_t *copy)
{
    copy->version = ++b->engine->render.mesh_version;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, copy->mesh, 0);
}

static float *uv_of(float *uvs, size_t stride, int32_t v)
{
    return (float *)((uint8_t *)uvs + (size_t)v * stride);
}

/*
 * TT_SetMapping: the UVs of the MatChannel turned by Rotate round the middle
 * of their range, tiled and offset; with New Planar Mapping they are first
 * the vertices' X and Z across the target's box. The range: U's half width
 * is the centre for both, and the last vertex is left out of it, as in the
 * DLL.
 */
static int bb_set_mapping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t channel = get_in_int(b, 0, -1);
    bool planar = get_in_int(b, 1, 0) != 0;
    float rotate = get_in_float(b, 2, 0.0f);
    float offset[2] = {0.0f, 0.0f}, tile[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 3, offset, sizeof(offset));
    nmo_engine_block_get_input_value(b, 4, tile, sizeof(tile));
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, nmo_engine_block_target(b), &count);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    size_t stride = 0;
    int32_t uv_count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &uv_count);
    if (uvs == NULL) {
        return NMO_ENGINE_BR_OK; /* the DLL writes through a missing channel */
    }
    int32_t n = (int32_t)count < uv_count ? (int32_t)count : uv_count;
    float umin = 0.0f, vmin = 0.0f, umax = 1.0f, vmax = 1.0f;
    if (planar) {
        eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
        eng_entity_local_box(engine, target, &min, &max);
        float sx = (float)(1.0 / (max.x - min.x)), sz = (float)(1.0 / (min.z - max.z));
        for (int32_t v = 0; v < n; v++) {
            float *uv = uv_of(uvs, stride, v);
            uv[0] = (copy->vertices[v].position.x - min.x) * sx;
            uv[1] = (copy->vertices[v].position.z - max.z) * sz;
        }
    } else if (n > 0) {
        umin = umax = uvs[0];
        vmin = vmax = uvs[1];
        for (int32_t v = 0; v + 1 < n; v++) {
            const float *uv = uv_of(uvs, stride, v);
            if (umin <= uv[0]) {
                if (umax < uv[0]) {
                    umax = uv[0];
                }
            } else {
                umin = uv[0];
            }
            if (vmin <= uv[1]) {
                if (vmax < uv[1]) {
                    vmax = uv[1];
                }
            } else {
                vmin = uv[1];
            }
        }
    }
    float half = (float)((umax - umin) * 0.5);
    double s = sin((double)rotate), c = cos((double)rotate);
    for (int32_t v = 0; v < n; v++) {
        float *uv = uv_of(uvs, stride, v);
        float a = vmax - uv[1] - half - vmin;
        float d = uv[0] - half - umin;
        uv[0] = (float)(s * a + c * d);
        uv[1] = (float)(c * a - s * d);
        uv[0] = tile[0] * uv[0] + half + umin;
        uv[1] = half - tile[1] * uv[1] + vmin;
        uv[0] = offset[0] + uv[0];
        uv[1] = offset[1] + uv[1];
    }
    uvs_changed(b, copy);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_RefractionMapping: each frame, the MatChannel's UVs are where the
 * camera's ray to each vertex, bent by its normal times RefractionFactor,
 * meets the plane Depth below (in the target's space, as Y), across the
 * target's box in X and Z.
 */
static int bb_refraction_mapping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t channel = get_in_int(b, 0, -1);
    int32_t camera = input_entity(b, 1);
    float depth = get_in_float(b, 2, 10.0f), factor = get_in_float(b, 3, 0.94999999f);
    if (camera < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through a missing camera */
    }
    eng_vector_t eye = eng_entity_position(engine, camera, target);
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, nmo_engine_block_target(b), &count);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
    eng_entity_local_box(engine, target, &min, &max);
    float sx = (float)(1.0 / (max.x - min.x)), sz = (float)(1.0 / (max.z - min.z));
    size_t stride = 0;
    int32_t uv_count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &uv_count);
    for (int32_t v = 0; uvs != NULL && v < uv_count && (uint32_t)v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position, *nrm = &copy->vertices[v].normal;
        eng_vector_t to = {eye.x - p->x, eye.y - p->y, eye.z - p->z};
        eng_vector_normalize(&to);
        eng_vector_t ray = {-(factor * nrm->x + to.x), -(to.y + factor * nrm->y), -(to.z + factor * nrm->z)};
        eng_vector_normalize(&ray);
        float t = (p->y - depth) / ray.y;
        float *uv = uv_of(uvs, stride, v);
        uv[0] = (ray.x * t + p->x - min.x) * sx;
        uv[1] = (ray.z * t + p->z - min.z) * sz;
    }
    uvs_changed(b, copy);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* VxVector of `to` - `from`, normalised as the DLL inlines it (1 / sqrt, no zero test) */
static eng_vector_t unit_toward(const eng_vector_t *from, const eng_vector_t *to)
{
    eng_vector_t d = {to->x - from->x, to->y - from->y, to->z - from->z};
    double inv = 1.0 / sqrt((double)(d.x * d.x + d.z * d.z + d.y * d.y));
    return (eng_vector_t){(float)(d.x * inv), (float)(d.y * inv), (float)(d.z * inv)};
}

static float dot3(const eng_vector_t *a, const eng_vector_t *c)
{
    return a->x * c->x + a->y * c->y + a->z * c->z;
}

/*
 * TT_BumpMap: each frame, every face shifts its vertices' MatChannel UVs
 * (from the ones On saved) along its two edges by how much each edge points
 * at the Light, times Emboss Factor; a vertex shared by faces keeps the last
 * face's.
 */
static int bb_bump_map(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t channel = get_in_int(b, 0, -1);
    int32_t light = input_entity(b, 1);
    float emboss = get_in_float(b, 2, 0.0099999998f);
    if (light < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through a missing light */
    }
    eng_vector_t at = eng_entity_position(engine, light, target);
    nmo_object_id_t mesh = current_mesh_of(engine, nmo_engine_block_target(b));
    nmo_object_t *object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    const nmo_mesh_state_t *state = object != NULL ? (const nmo_mesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_MESH) : NULL;
    eng_mesh_copy_t *copy = state != NULL ? eng_mesh_copy(engine, mesh) : NULL;
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    size_t stride = 0;
    int32_t uv_count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &uv_count);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        float *saved = (float *)malloc((size_t)(uv_count > 0 ? uv_count : 1) * 2u * sizeof(float));
        for (int32_t v = 0; saved != NULL && uvs != NULL && v < uv_count; v++) {
            const float *uv = uv_of(uvs, stride, v);
            saved[2 * v] = uv[0];
            saved[2 * v + 1] = uv[1];
        }
        if (saved != NULL && uvs != NULL) {
            nmo_engine_block_set_local_value(b, 0, saved, (size_t)uv_count * 2u * sizeof(float));
        }
        free(saved);
    }
    const eng_param_t *param = block_local(b, 0);
    const float *saved = param != NULL ? (const float *)param->data : NULL;
    uint32_t held = param != NULL ? (uint32_t)(param->size / (2u * sizeof(float))) : 0;
    uint32_t vertices = state->vertex_count;
    for (uint32_t f = 0; uvs != NULL && saved != NULL && state->face_vertex_indices != NULL && f < state->face_count; f++) {
        const uint16_t *k = &state->face_vertex_indices[3u * f];
        if (k[0] >= vertices || k[1] >= vertices || k[2] >= vertices || k[0] >= held || k[1] >= held ||
            k[2] >= held || (int32_t)k[0] >= uv_count || (int32_t)k[1] >= uv_count || (int32_t)k[2] >= uv_count) {
            continue;
        }
        const nmo_vector_t *qa = &copy->vertices[k[0]].position, *qb = &copy->vertices[k[1]].position;
        const nmo_vector_t *qc = &copy->vertices[k[2]].position;
        eng_vector_t pa = {qa->x, qa->y, qa->z}, pb = {qb->x, qb->y, qb->z}, pc = {qc->x, qc->y, qc->z};
        eng_vector_t la = unit_toward(&pa, &at), ab = unit_toward(&pa, &pb), ac = unit_toward(&pa, &pc);
        float *uv = uv_of(uvs, stride, k[0]);
        uv[0] = dot3(&ab, &la) * emboss + saved[2 * k[0]];
        uv[1] = dot3(&la, &ac) * emboss + saved[2 * k[0] + 1];
        eng_vector_t lb = unit_toward(&pb, &at), bc = unit_toward(&pb, &pc), ba = unit_toward(&pb, &pa);
        uv = uv_of(uvs, stride, k[1]);
        uv[0] = dot3(&bc, &lb) * emboss + saved[2 * k[1]];
        uv[1] = dot3(&lb, &ba) * emboss + saved[2 * k[1] + 1];
        eng_vector_t lc = unit_toward(&pc, &at), ca = unit_toward(&pc, &pa), cb = unit_toward(&pc, &pb);
        uv = uv_of(uvs, stride, k[2]);
        uv[0] = dot3(&ca, &lc) * emboss + saved[2 * k[2]];
        uv[1] = dot3(&cb, &lc) * emboss + saved[2 * k[2] + 1];
    }
    uvs_changed(b, copy);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* A material made at run time with the toolbox's shading look: white diffuse and ambient, no
   emissive or specular, its texture wrapped and modulated */
static nmo_object_id_t shading_material(nmo_engine_t *engine, const char *name)
{
    nmo_object_id_t id = eng_create_object(engine, NMO_CID_MATERIAL, name);
    eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
    if (material == NULL && id != 0) {
        material = eng_material_new(engine, id);
    }
    if (material == NULL) {
        return 0;
    }
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f}, white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(material->desc.emissive, black, sizeof(black));
    memcpy(material->desc.diffuse, white, sizeof(white));
    memcpy(material->desc.specular, black, sizeof(black));
    memcpy(material->desc.ambient, white, sizeof(white));
    material->desc.address_mode = VXTEXTURE_ADDRESSWRAP;
    material->desc.texture_blend = VXTEXTUREBLEND_MODULATE;
    return id;
}

/* CKBitmapData::SetPixel of an ARGB color */
static void put_pixel(eng_surface_t *surface, int32_t x, int32_t y, uint32_t argb)
{
    uint8_t *p = &surface->rgba[4 * ((size_t)y * (size_t)surface->width + (size_t)x)];
    p[0] = (uint8_t)(argb >> 16);
    p[1] = (uint8_t)(argb >> 8);
    p[2] = (uint8_t)argb;
    p[3] = (uint8_t)(argb >> 24);
}

/* A texture made at run time (CKTexture::Create(width, height, 32)); NULL for none */
static eng_surface_t *made_texture(nmo_engine_t *engine, const char *name, int32_t width, int32_t height,
                                   nmo_object_id_t *out)
{
    nmo_object_id_t id = eng_create_object(engine, NMO_CID_TEXTURE, name);
    eng_texture_t *texture = id != 0 ? eng_texture(engine, id) : NULL;
    *out = id;
    if (texture == NULL || !eng_texture_create(engine, texture, width, height, 0)) {
        return NULL;
    }
    eng_surface_t *surface = eng_texture_surface(engine, texture, -1);
    return surface != NULL && surface->rgba != NULL ? surface : NULL;
}

/* CKMesh::AddChannel(material, TRUE): the new channel with a copy of the mesh's UVs; -1 for none */
static int32_t add_uv_channel(nmo_engine_t *engine, eng_mesh_copy_t *copy, nmo_object_id_t material)
{
    size_t stride = 0;
    int32_t count = 0;
    const float *own = eng_mesh_uvs(engine, copy, -1, &stride, &count);
    int32_t channel = eng_mesh_add_channel(copy, material);
    if (channel < 0 || own == NULL || count <= 0) {
        return channel;
    }
    float *uvs = (float *)malloc((size_t)count * 2u * sizeof(float));
    for (int32_t v = 0; uvs != NULL && v < count; v++) {
        const float *uv = (const float *)((const uint8_t *)own + (size_t)v * stride);
        uvs[2 * v] = uv[0];
        uvs[2 * v + 1] = uv[1];
    }
    copy->channels.data[channel].uvs = uvs;
    return channel;
}

/*
 * TT_CelShading: On adds a channel (replacing the mesh's colour: source ONE,
 * dest ZERO, lit) whose "Mat CelShading" reads a 16 x 1 "Tex CelShading" of
 * three tones of Color, and keeps smooth normals (the face normals round
 * each position summed); each frame the channel's U is 1 less how much each
 * vertex faces away from the Light. As in the DLL, after On the UVs written
 * are channel 0's. With Outline, On also has the target drawn again over
 * everything after it renders, in wireframe, from then on.
 */
/* TT_CelShading's outline (the target's post render callback): its mesh rendered again in the
   wireframe global render mode with the Z test always passing, so its edges show over everything */
static void cel_outline(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)node;
    int32_t index = eng_entity_index(engine, entity);
    nmo_object_id_t mesh = index >= 0 ? current_mesh_of(engine, entity) : 0;
    nmo_engine_draw_t *draw =
        mesh != 0 ? eng_render_entity_mesh(engine, entity, mesh, &engine->entities.data[index].world, transparent)
                  : NULL;
    if (draw != NULL) {
        draw->wireframe = true;
        draw->no_z_test = true;
    }
}

static int bb_cel_shading(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t light = input_entity(b, 0);
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 1, color, sizeof(color));
    if (light < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through a missing light */
    }
    eng_vector_t at = eng_entity_position(engine, light, target);
    nmo_object_id_t mesh = current_mesh_of(engine, nmo_engine_block_target(b));
    nmo_object_t *object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    const nmo_mesh_state_t *state = object != NULL ? (const nmo_mesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_MESH) : NULL;
    eng_mesh_copy_t *copy = state != NULL ? eng_mesh_copy(engine, mesh) : NULL;
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t channel = 0;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t count = state->vertex_count;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        nmo_object_id_t material = shading_material(engine, "Mat CelShading");
        nmo_object_id_t texture = 0;
        eng_surface_t *surface = made_texture(engine, "Tex CelShading", 16, 1, &texture);
        if (surface != NULL) {
            for (int32_t x = 0; x < 16; x++) {
                float add = x < 3 ? 0.0f : x < 8 ? 0.5f : 1.0f;
                const float tone[4] = {(float)((color[0] + add) * 0.5), (float)((color[1] + add) * 0.5),
                                       (float)((color[2] + add) * 0.5), 0.0f};
                put_pixel(surface, x, 0, vxcolor_to_color(tone));
            }
            eng_texture_changed(engine, texture);
        }
        eng_material_t *desc = material != 0 ? eng_material(engine, material) : NULL;
        if (desc != NULL) {
            desc->desc.texture = texture;
        }
        int32_t before = (int32_t)copy->channels.count;
        channel = add_uv_channel(engine, copy, material);
        if (channel != before) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        copy->channels.data[channel].source_blend = VXBLEND_ONE;
        copy->channels.data[channel].dest_blend = VXBLEND_ZERO;
        copy->channels.data[channel].lit = true;
        /* Smooth normals: the face normals of the faces with a vertex at the same place */
        const nmo_face_t *faces = copy->faces != NULL ? copy->faces : state->faces;
        nmo_vector_t *normals = (nmo_vector_t *)calloc(count > 0 ? count : 1, sizeof(*normals));
        for (uint32_t v = 0; normals != NULL && v < count; v++) {
            const nmo_vector_t *p = &copy->vertices[v].position;
            double sum[3] = {0.0, 0.0, 0.0};
            for (uint32_t f = 0; faces != NULL && state->face_vertex_indices != NULL && f < state->face_count; f++) {
                const uint16_t *k = &state->face_vertex_indices[3u * f];
                bool touches = false;
                for (int i = 0; i < 3 && !touches; i++) {
                    const nmo_vector_t *q = k[i] < count ? &copy->vertices[k[i]].position : NULL;
                    touches = q != NULL && p->x == q->x && p->y == q->y && p->z == q->z;
                }
                if (touches) {
                    sum[0] = faces[f].normal.x + (float)sum[0];
                    sum[1] = faces[f].normal.y + (float)sum[1];
                    sum[2] = faces[f].normal.z + (float)sum[2];
                }
            }
            double length = sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
            normals[v] = (nmo_vector_t){(float)(1.0 / length * sum[0]), (float)(1.0 / length * sum[1]),
                                        (float)(1.0 / length * sum[2])};
        }
        if (normals != NULL) {
            nmo_engine_block_set_local_value(b, 0, normals, count * sizeof(*normals));
        }
        free(normals);
        /* Outline: the target drawn again after it renders, from now on */
        if (get_in_int(b, 2, 0) != 0) {
            eng_entity_callback_t outline = {nmo_engine_block_target(b), ENG_ENTITY_POST_RENDER, cel_outline,
                                             b->node, false};
            eng_add_entity_callback(engine, &outline);
        }
    }
    const eng_param_t *param = block_local(b, 0);
    const nmo_vector_t *normals = param != NULL ? (const nmo_vector_t *)param->data : NULL;
    uint32_t held = param != NULL ? (uint32_t)(param->size / sizeof(nmo_vector_t)) : 0;
    size_t stride = 0;
    int32_t uv_count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &uv_count);
    for (uint32_t v = 0; uvs != NULL && normals != NULL && v < count && v < held && (int32_t)v < uv_count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        eng_vector_t from = {p->x - at.x, p->y - at.y, p->z - at.z};
        eng_vector_normalize(&from);
        float facing = from.x * normals[v].x + from.z * normals[v].z + from.y * normals[v].y;
        if (facing < 0.0f) {
            facing = 0.0f;
        }
        float *uv = uv_of(uvs, stride, (int32_t)v);
        uv[1] = 0.0f;
        uv[0] = 1.0f - facing;
    }
    uvs_changed(b, copy);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * TT_AnisoMapping: On adds a channel (added to the mesh's colour: ONE, ONE)
 * whose "Mat AnisoMap" reads a Texture Size square "Tex AnisoMap" of an
 * anisotropic highlight in Color (tinted with Metal), both put in the
 * scene, and keeps base UVs from the vertices (Mapping 1: the cosine and
 * sine of the direction's X, else the distance over the box's Min corner's,
 * times Size); each frame the last channel's UVs are the base UVs through
 * the light and camera directions.
 */
static int bb_aniso_mapping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t light = input_entity(b, 0), camera = input_entity(b, 1);
    uint32_t count = 0;
    eng_mesh_copy_t *copy = deformed_mesh(engine, nmo_engine_block_target(b), &count);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        int32_t size = get_in_int(b, 2, 2);
        float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 3, color, sizeof(color));
        bool metal = get_in_int(b, 4, 0) != 0;
        int32_t mapping = get_in_int(b, 5, 1);
        float scale = get_in_float(b, 6, 1.0f);
        nmo_object_id_t material = shading_material(engine, "Mat AnisoMap");
        nmo_object_id_t texture = 0;
        eng_surface_t *surface = size > 0 ? made_texture(engine, "Tex AnisoMap", size, size, &texture) : NULL;
        if (surface == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        for (int32_t x = 0; x < size; x++) {
            double a = 1.0 - ((double)x + (double)x) / (double)(size - 1);
            float af = (float)a;
            double sa = pow(1.0 - a * af, 0.5);
            for (int32_t y = 0; y < size; y++) {
                double c = ((double)y + (double)y) / (double)(size - 1) - 1.0;
                float cf = (float)c;
                double s = pow(1.0 - c * cf, 0.5) * sa - cf * af;
                float clipped = s >= 0.0 ? (float)s : 0.0f;
                double spec = pow((double)clipped, 39.99999940395356) * 0.800000011920929;
                float tone[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 3; i++) {
                    double value = ((metal ? color[i] : 1.0f) * spec + color[i] * sa * 0.2) * color[3];
                    tone[i] = value < 0.0 ? 0.0f : value <= 1.0 ? (float)value : 1.0f;
                }
                put_pixel(surface, x, y, vxcolor_to_color(tone));
            }
        }
        eng_texture_changed(engine, texture);
        eng_material_t *desc = material != 0 ? eng_material(engine, material) : NULL;
        if (desc != NULL) {
            desc->desc.texture = texture;
        }
        int32_t before = (int32_t)copy->channels.count;
        int32_t channel = add_uv_channel(engine, copy, material);
        if (channel != before) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        copy->channels.data[channel].source_blend = VXBLEND_ONE;
        copy->channels.data[channel].dest_blend = VXBLEND_ONE;
        eng_add_to_level_scene(engine, texture);
        eng_add_to_level_scene(engine, material);
        eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
        eng_entity_local_box(engine, target, &min, &max);
        float reach = (float)sqrt((double)(min.x * min.x + min.z * min.z + min.y * min.y));
        float *base = (float *)malloc((count > 0 ? count : 1) * 2u * sizeof(float));
        for (uint32_t v = 0; base != NULL && v < count; v++) {
            const nmo_vector_t *p = &copy->vertices[v].position;
            eng_vector_t dir = {0.0f, 0.0f, 0.0f};
            if (!(p->x == 0.0f && p->y == 0.0f && p->z == 0.0f)) {
                double inv = 1.0 / sqrt((double)(p->x * p->x + p->z * p->z + p->y * p->y));
                dir = (eng_vector_t){(float)(p->x * inv), (float)(p->y * inv), (float)(p->z * inv)};
            }
            float length = (float)sqrt((double)(p->x * p->x + p->z * p->z + p->y * p->y));
            if (mapping == 1) {
                base[2 * v] = length >= 0.0099999998f ? (float)(cos(fabs((double)dir.x)) * scale) : 0.0f;
                base[2 * v + 1] = length >= 0.0099999998f ? (float)(sin(fabs((double)dir.x)) * scale) : 0.0f;
            } else {
                float share = length / reach;
                base[2 * v] = base[2 * v + 1] = share * scale;
            }
        }
        if (base != NULL) {
            nmo_engine_block_set_local_value(b, 0, base, count * 2u * sizeof(float));
        }
        free(base);
    }
    if (light < 0 || camera < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through them */
    }
    eng_vector_t eye = eng_entity_position(engine, camera, target), lamp = eng_entity_position(engine, light, target);
    size_t stride = 0;
    int32_t uv_count = 0;
    float *uvs = eng_mesh_uvs(engine, copy, (int32_t)copy->channels.count - 1, &stride, &uv_count);
    const eng_param_t *param = block_local(b, 0);
    const float *base = param != NULL ? (const float *)param->data : NULL;
    uint32_t held = param != NULL ? (uint32_t)(param->size / (2u * sizeof(float))) : 0;
    for (uint32_t v = 0; uvs != NULL && base != NULL && v < count && v < held && (int32_t)v < uv_count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        eng_vector_t to_eye = {eye.x - p->x, eye.y - p->y, eye.z - p->z};
        eng_vector_t to_lamp = {lamp.x - p->x, lamp.y - p->y, lamp.z - p->z};
        eng_vector_normalize(&to_eye);
        eng_vector_normalize(&to_lamp);
        /* Vx3DMultiplyMatrixVector with rows: the light direction, the camera direction */
        float *uv = uv_of(uvs, stride, (int32_t)v);
        uv[0] = base[2 * v] * to_lamp.x + base[2 * v + 1] * to_eye.x;
        uv[1] = base[2 * v] * to_lamp.y + base[2 * v + 1] * to_eye.y;
    }
    uvs_changed(b, copy);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT_AnisoMapping's callback: on reset its last channel, when active, goes with its material and texture */
static int aniso_mapping_callback(blk_t *b, uint32_t message)
{
    if (message != NMO_ENGINE_MSG_RESET) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t mesh = current_mesh_of(engine, nmo_engine_block_target(b));
    eng_mesh_copy_t *copy = mesh != 0 ? eng_mesh_copy_find(engine, mesh) : NULL;
    if (copy == NULL || copy->channels.count == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t last = (int32_t)copy->channels.count - 1;
    if (copy->channels.data[last].active) {
        nmo_object_id_t material = copy->channels.data[last].material;
        eng_material_t *desc = material != 0 ? eng_material(engine, material) : NULL;
        nmo_object_id_t doomed[2] = {desc != NULL ? desc->desc.texture : 0, material};
        eng_mesh_remove_channel(copy, last);
        copy->version = ++engine->render.mesh_version;
        eng_destroy_objects(engine, doomed[0] != 0 ? doomed : &doomed[1], doomed[0] != 0 ? 2 : 1, NULL);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ListAllCurrentUsedTextures: a row at the end of Array for each slot
 * file of each texture (its first column), removed when the cell takes no
 * text; with OnInVideoMemory only the textures in video memory, none on
 * the machine the toolbox reports.
 */
static int bb_list_all_current_used_textures(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    bool video_only = get_local_int(b, 0, 1) == 1;
    nmo_object_id_t *ids = NULL;
    size_t count = video_only ? 0 : eng_objects_of_class(engine, NMO_CID_TEXTURE, &ids);
    for (size_t i = 0; i < count; i++) {
        eng_texture_t *texture = eng_texture(engine, ids[i]);
        nmo_bitmap_slots_t view;
        bool has_view = eng_bitmap_view(engine, ids[i], &view);
        int32_t slots = texture != NULL ? texture->slot_count : 0;
        for (int32_t s = 0; s < slots; s++) {
            const char *name = (size_t)s < texture->files.count && texture->files.data[s] != NULL
                                   ? texture->files.data[s]
                               : has_view && view.slot_filenames != NULL && (uint32_t)s < view.slot_count
                                   ? view.slot_filenames[s]
                                   : NULL;
            int32_t row = -1;
            nmo_engine_array_insert_row(engine, array, -1, &row);
            nmo_engine_cell_t cell = {.type = CKARRAYTYPE_STRING, .text = name != NULL ? name : ""};
            if (row < 0 || nmo_engine_array_column_type(engine, array, 0) != CKARRAYTYPE_STRING ||
                nmo_engine_array_set_cell(engine, array, row, 0, &cell) != NMO_OK) {
                if (row >= 0) {
                    nmo_engine_array_remove_row(engine, array, row);
                }
            }
        }
    }
    free(ids);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_SaveAlpha: the target texture's alpha copied into its red, green and
 * blue, its alpha cleared, then its first slot saved to the file Filename
 * names, in the format its extension names, through the host's writer.
 */
static int bb_save_alpha(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t id = nmo_engine_block_target(b);
    eng_texture_t *texture = id != 0 ? eng_texture(engine, id) : NULL;
    if (texture == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_surface_t *surface = eng_texture_surface(engine, texture, -1);
    if (surface != NULL && surface->rgba != NULL) {
        for (size_t i = 0; i < (size_t)surface->width * (size_t)surface->height; i++) {
            uint8_t *p = &surface->rgba[4 * i];
            p[0] = p[1] = p[2] = p[3];
            p[3] = 0;
        }
        eng_texture_changed(engine, id);
    }
    const char *name = input_string(b, 0);
    eng_surface_t *first = eng_texture_surface(engine, texture, 0);
    const char *dot = name != NULL ? strrchr(name, '.') : NULL;
    const nmo_image_codec_t *codec = dot != NULL ? nmo_image_codec_find_by_extension(dot + 1) : NULL;
    if (first != NULL && first->rgba != NULL && codec != NULL && codec->encode != NULL &&
        engine->host.write_file != NULL) {
        nmo_arena_t *arena = nmo_arena_create(NULL, 0);
        nmo_bitmap_properties_t props = {codec->format, 100, 0, true, dot + 1};
        uint8_t *data = NULL;
        size_t size = 0;
        if (arena != NULL && codec->encode(first->rgba, first->width, first->height, 4, &props, arena, &data,
                                           &size) == NMO_OK) {
            engine->host.write_file(engine->host.write_file_user_data, name, data, size);
            eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, id, 0);
        }
        if (arena != NULL) {
            nmo_arena_destroy(arena);
        }
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_Set Material Values: the target material takes Get Material's colours,
 * power, first texture, texture modes, blending, sides, Z and alpha test
 * settings (not its effect). Perspective correction is not modelled.
 */
static int bb_set_material_values(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b), source = nmo_engine_block_get_input_object(b, 0);
    eng_material_t *from = target != 0 && source != 0 ? eng_material(engine, source) : NULL;
    nmo_engine_material_t copied;
    if (from == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    copied = from->desc;
    eng_material_t *to = eng_material(engine, target);
    if (to == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t texgen = to->desc.texgen;
    nmo_object_id_t texgen_referential = to->desc.texgen_referential;
    to->desc = copied;
    to->desc.texgen = texgen;
    to->desc.texgen_referential = texgen_referential;
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ---- Effects ---- */

/* TT_SetFSAA: the render context's antialiasing render state; its input stays active and no output
   fires, as in the DLL */
static int bb_set_fsaa(blk_t *b)
{
    eng_render_settings(b->engine)->antialias = (get_in_int(b, 0, 0) & 0xFF) != 0;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_ViewFrustumClipping: each frame, every 3D entity in the scene shown
 * when CK3dEntity::IsInViewFrustrum has it in Camera's view (its mesh's box,
 * its origin without a mesh, never for a mesh without vertices), hidden
 * otherwise; all shown again when its script is deactivated. The DLL also
 * draws the scene through the camera first (into the back buffer the frame
 * clears again); that is not drawn here.
 */
static int bb_view_frustum_clipping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t camera = nmo_engine_block_get_input_object(b, 0);
    if (camera == 0) {
        camera = engine->render.camera;
    }
    eng_matrix_t view;
    if (!eng_camera_view_projection(engine, camera, &view, NULL)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    int32_t viewport[4];
    eng_render_viewport(engine, viewport);
    for (size_t i = 0; i < engine->entities.count; i++) {
        nmo_object_id_t id = engine->entities.data[i].id;
        if (engine->entities.data[i].destroyed || !eng_in_scene(engine, id)) {
            continue;
        }
        /* IsInViewFrustrum(rc, 255): the render extents updated too */
        eng_show(engine, id, eng_entity_in_view_frustum(engine, (int32_t)i, &view, viewport, true) ? 1 : 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int view_frustum_clipping_callback(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    if (message != NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        return NMO_ENGINE_BR_OK;
    }
    for (size_t i = 0; i < engine->entities.count; i++) {
        const eng_entity_t *entity = &engine->entities.data[i];
        if (!entity->destroyed && eng_in_scene(engine, entity->id)) {
            eng_show(engine, entity->id, 1);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_BitmapFont: On makes (in the render context) an "Ent" with a "Mesh" of
 * a quad a character of Text at Z 10, Scale apart from Position, each
 * showing its cell of a 16 x 16 font texture from character 32 (Cursiv: 128
 * cells on), in Material. As in the DLL, Off does nothing (the block tests
 * On twice), each On makes another entity, and the blend and colour inputs
 * are not used.
 */
static int bb_bitmap_font(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (!IN_ACTIVE(b, 0)) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    float position[2] = {0.0f, 0.0f}, scale[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 1, position, sizeof(position));
    nmo_engine_block_get_input_value(b, 2, scale, sizeof(scale));
    const char *text = input_string(b, 3);
    if (text == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    bool cursive = (get_in_int(b, 4, 0) & 0xFF) != 0;
    int32_t length = (int32_t)strlen(text);
    nmo_mesh_state_t *state = NULL;
    nmo_object_id_t mesh = eng_create_mesh(engine, "Mesh", 4 * length, 2 * length, &state);
    nmo_object_id_t entity = eng_create_object(engine, NMO_CID_3DENTITY, "Ent");
    if (mesh == 0 || entity == 0) {
        return NMO_ENGINE_BR_OK;
    }
    for (int32_t i = 0; i < length; i++) {
        int32_t cell = (unsigned char)text[i] - 32 + (cursive ? 128 : 0);
        /* the DLL reads outside its 256 cells there; the first cell here */
        cell = cell < 0 || cell > 255 ? 0 : cell;
        float u0 = (float)(cell % 16) * 0.0625f, u1 = u0 + 0.0625f;
        float v0 = (float)(cell / 16) * 0.0625f, v1 = v0 + 0.0625f;
        float x0 = (float)i * scale[0] + position[0], x1 = (float)(i + 1) * scale[0] + position[0];
        float y1 = scale[1] + position[1];
        nmo_vertex_t *v = &state->vertices[4 * i];
        v[0].position = (nmo_vector_t){x0, position[1], 10.0f};
        v[1].position = (nmo_vector_t){x0, y1, 10.0f};
        v[2].position = (nmo_vector_t){x1, y1, 10.0f};
        v[3].position = (nmo_vector_t){x1, position[1], 10.0f};
        v[0].uv = (nmo_vector2_t){u0, v1};
        v[1].uv = (nmo_vector2_t){u0, v0};
        v[2].uv = (nmo_vector2_t){u1, v0};
        v[3].uv = (nmo_vector2_t){u1, v1};
        const uint16_t k = (uint16_t)(4 * i);
        const uint16_t faces[6] = {k, (uint16_t)(k + 1), (uint16_t)(k + 3), (uint16_t)(k + 1), (uint16_t)(k + 2),
                                   (uint16_t)(k + 3)};
        memcpy(&state->face_vertex_indices[6 * i], faces, sizeof(faces));
    }
    /* BuildNormals: the quads face -Z */
    for (int32_t f = 0; f < 2 * length; f++) {
        state->faces[f].normal = (nmo_vector_t){0.0f, 0.0f, -1.0f};
    }
    for (int32_t v = 0; v < 4 * length; v++) {
        state->vertices[v].normal = (nmo_vector_t){0.0f, 0.0f, -1.0f};
    }
    nmo_mesh_update_bounding_volumes(state);
    if (material != 0) {
        eng_mesh_apply_material(engine, mesh, material);
    }
    eng_entity_set_current_mesh(engine, entity, mesh);
    int32_t index = eng_entity_index(engine, entity);
    if (index >= 0) {
        const eng_vector_t origin = {0.0f, 0.0f, 0.0f};
        eng_entity_set_position(engine, index, &origin, -1, false);
    }
    eng_add_to_level_scene(engine, entity);
    return NMO_ENGINE_BR_OK;
}

enum { SKY_STATE, SKY_SIDES, SKY_TOP, SKY_BOTTOM };
enum { SKY_ENTITY_SLOT = 17, SKY_ORIENTATION_SLOT = 18, SKY_STATE_WORDS = 19 };

/* The UV the sky's top and bottom fans give a ring point: its direction in XZ halved round 0.5 */
static nmo_vector2_t sky_fan_uv(float x, float z)
{
    if (x == 0.0f && z == 0.0f) {
        return (nmo_vector2_t){0.0f, 0.0f};
    }
    double inv = 1.0 / sqrt((double)(z * z + x * x));
    return (nmo_vector2_t){(float)(inv * x) * 0.5f + 0.5f, (float)(inv * z * 0.5 + 0.5)};
}

/*
 * TT SkyAround: On makes a "TT_SkyAround_Entity" (hidden, in the level)
 * with a prelit "TT_SkyAround_Mesh": a ring of SideMaterials quads of Radius
 * round Y Position (square, or Sideface Height high), with top and bottom
 * fans when the settings say, its vertex colours the filtering colour and
 * its specular the additional one, each part in its material input; each
 * frame it is shown at the viewpoint, turned as Orientation Object (none:
 * the world's axes). Off hides it. Distortion and the blend inputs are not
 * used, as in the DLL.
 */
static int bb_sky_around(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        float filter[4] = {1.0f, 1.0f, 1.0f, 0.5f};
        nmo_engine_block_get_input_value(b, 1, filter, sizeof(filter));
        uint32_t filtering = rgbaf_to_color(filter);
        nmo_engine_block_get_input_value(b, 2, filter, sizeof(filter));
        uint32_t additional = rgbaf_to_color(filter);
        nmo_object_id_t orientation = nmo_engine_block_get_input_object(b, 5);
        float radius = get_in_float(b, 6, 70.0f);
        bool square = get_in_int(b, 7, 1) != 0;
        float height = square ? 0.0f : get_in_float(b, 8, 0.0f);
        float y = get_in_float(b, 9, 0.0f);
        int32_t sides = get_local_int(b, SKY_SIDES, 0);
        bool top = get_local_int(b, SKY_TOP, 0) != 0, bottom = get_local_int(b, SKY_BOTTOM, 0) != 0;
        int32_t per_vertices = 4 + (top ? 3 : 0) + (bottom ? 3 : 0);
        int32_t per_faces = 2 + (top ? 1 : 0) + (bottom ? 1 : 0);
        if (square) {
            double step = 6.2831855 / (double)sides;
            float cx = (float)(cos(step) * radius), sz = (float)(sin(step) * radius);
            height = (float)sqrt((double)(-sz * -sz + (radius - cx) * (radius - cx)));
        }
        nmo_mesh_state_t *state = NULL;
        nmo_object_id_t mesh = sides > 0 ? eng_create_mesh(engine, "TT_SkyAround_Mesh", sides * per_vertices,
                                                           sides * per_faces, &state)
                                         : 0;
        nmo_object_id_t entity = mesh != 0 ? eng_create_object(engine, NMO_CID_3DENTITY, "TT_SkyAround_Entity") : 0;
        if (entity == 0) {
            return NMO_ENGINE_BR_GENERICERROR;
        }
        float low = (float)(y - height * 0.5), high = (float)(height * 0.5 + y);
        for (int32_t s = 0; s < sides; s++) {
            double step = 6.2831855 / (double)sides;
            double a0 = (double)s * step, a1 = step * (double)(s + 1);
            float x0 = (float)(cos(a0) * radius), z0 = (float)(sin(a0) * radius);
            float x1 = (float)(cos(a1) * radius), z1 = (float)(sin(a1) * radius);
            nmo_vertex_t *v = &state->vertices[s * per_vertices];
            v[0].position = (nmo_vector_t){x0, low, z0};
            v[0].uv = (nmo_vector2_t){1.0f, 1.0f};
            v[1].position = (nmo_vector_t){x1, low, z1};
            v[1].uv = (nmo_vector2_t){0.0f, 1.0f};
            v[2].position = (nmo_vector_t){x1, high, z1};
            v[2].uv = (nmo_vector2_t){0.0f, 0.0f};
            v[3].position = (nmo_vector_t){x0, high, z0};
            v[3].uv = (nmo_vector2_t){1.0f, 0.0f};
            int32_t k = 4;
            if (top) {
                v[k].position = (nmo_vector_t){x0, high, z0};
                v[k].uv = sky_fan_uv(x0, z0);
                v[k + 1].position = (nmo_vector_t){x1, high, z1};
                v[k + 1].uv = sky_fan_uv(x1, z1);
                v[k + 2].position = (nmo_vector_t){0.0f, high, 0.0f};
                v[k + 2].uv = (nmo_vector2_t){0.5f, 0.5f};
                k += 3;
            }
            if (bottom) {
                v[k].position = (nmo_vector_t){x0, low, z0};
                v[k].uv = sky_fan_uv(x0, z0);
                v[k + 1].position = (nmo_vector_t){0.0f, low, 0.0f};
                v[k + 1].uv = (nmo_vector2_t){0.5f, 0.5f};
                v[k + 2].position = (nmo_vector_t){x1, low, z1};
                v[k + 2].uv = sky_fan_uv(x1, z1);
            }
        }
        for (int32_t i = 0; i < sides * per_vertices; i++) {
            state->vertex_colors[i] = filtering;
            state->vertex_specular[i] = additional;
        }
        state->flags |= VXMESH_PRELITMODE;
        for (int32_t s = 0; s < sides; s++) {
            uint16_t base = (uint16_t)(s * per_vertices);
            uint16_t *k = &state->face_vertex_indices[3 * s * per_faces];
            const uint16_t quad[6] = {(uint16_t)(base + 2), base, (uint16_t)(base + 1), (uint16_t)(base + 2),
                                      (uint16_t)(base + 3), base};
            memcpy(k, quad, sizeof(quad));
            int32_t f = 2, at = 4;
            if (top) {
                k[3 * f] = (uint16_t)(base + at);
                k[3 * f + 1] = (uint16_t)(base + at + 1);
                k[3 * f + 2] = (uint16_t)(base + at + 2);
                f++;
                at += 3;
            }
            if (bottom) {
                k[3 * f] = (uint16_t)(base + at);
                k[3 * f + 1] = (uint16_t)(base + at + 1);
                k[3 * f + 2] = (uint16_t)(base + at + 2);
            }
        }
        nmo_mesh_update_bounding_volumes(state);
        for (int32_t s = 0; s < sides; s++) {
            int32_t f = s * per_faces;
            nmo_object_id_t side = nmo_engine_block_get_input_object(b, 10 + s);
            eng_mesh_set_face_material(engine, mesh, f, side);
            eng_mesh_set_face_material(engine, mesh, f + 1, side);
            f += 2;
            if (top) {
                eng_mesh_set_face_material(engine, mesh, f++, nmo_engine_block_get_input_object(b, sides + 10));
            }
            if (bottom) {
                eng_mesh_set_face_material(engine, mesh, f,
                                           nmo_engine_block_get_input_object(b, sides + (top ? 11 : 10)));
            }
        }
        eng_add_to_level_scene(engine, entity);
        eng_show(engine, entity, 0);
        eng_entity_set_current_mesh(engine, entity, mesh);
        uint32_t words[SKY_STATE_WORDS];
        memset(words, 0, sizeof(words));
        words[SKY_ENTITY_SLOT] = (uint32_t)entity;
        words[SKY_ORIENTATION_SLOT] = (uint32_t)orientation;
        nmo_engine_block_set_local_value(b, SKY_STATE, words, sizeof(words));
    }
    uint32_t words[SKY_STATE_WORDS];
    memset(words, 0, sizeof(words));
    nmo_engine_block_get_local_value(b, SKY_STATE, words, sizeof(words));
    nmo_object_id_t entity = (nmo_object_id_t)words[SKY_ENTITY_SLOT];
    int32_t index = entity != 0 ? eng_entity_index(engine, entity) : -1;
    if (index < 0) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_show(engine, entity, 0);
        return NMO_ENGINE_BR_OK;
    }
    eng_show(engine, entity, 1);
    eng_matrix_t eye;
    eng_render_viewpoint(engine, &eye);
    int32_t orientation = eng_entity_index(engine, (nmo_object_id_t)words[SKY_ORIENTATION_SLOT]);
    eng_matrix_t world;
    if (orientation >= 0) {
        world = engine->entities.data[orientation].world;
    } else {
        memset(&world, 0, sizeof(world));
        world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1.0f;
    }
    eng_entity_set_world_matrix(engine, index, &world, false);
    eng_vector_t at = {eye.m[3][0], eye.m[3][1], eye.m[3][2]};
    eng_entity_set_position(engine, index, &at, -1, false);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT SkyAround's callback: its entity destroyed on reset (and delete, detach), the state cleared on
   load (and attach) */
static int sky_around_callback(blk_t *b, uint32_t message)
{
    uint32_t words[SKY_STATE_WORDS];
    memset(words, 0, sizeof(words));
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_engine_block_set_local_value(b, SKY_STATE, words, sizeof(words));
    } else if (message == NMO_ENGINE_MSG_RESET) {
        nmo_engine_block_get_local_value(b, SKY_STATE, words, sizeof(words));
        nmo_object_id_t entity = (nmo_object_id_t)words[SKY_ENTITY_SLOT];
        if (entity != 0 && eng_find_object(b->engine, entity) != NULL) {
            eng_destroy_objects(b->engine, &entity, 1, NULL);
        }
        words[SKY_ENTITY_SLOT] = 0;
        nmo_engine_block_set_local_value(b, SKY_STATE, words, sizeof(words));
    }
    return NMO_ENGINE_BR_OK;
}

/* ---- Shadows and flares ---- */

/* A floor the shadow blocks look at: an object with the Floor attribute, shown, not hidden by a
   parent and not all outside the last frame's view; -1 for none */
static int32_t visible_floor(nmo_engine_t *engine, const eng_attribute_t *attribute, int32_t type)
{
    int32_t floor = attribute->type == type ? eng_entity_index(engine, attribute->object) : -1;
    if (floor < 0 || engine->entities.data[floor].destroyed ||
        !nmo_engine_object_is_visible(engine, attribute->object) || eng_render_hidden_by_parent(engine, floor) ||
        eng_entity_all_outside_frustum(engine, floor)) {
        return -1;
    }
    return floor;
}

/* A TRIANGLES draw of quads (corners in world space, a fan each), textured and blended, without the
   depth test or write */
static void push_overlay_quads(nmo_engine_t *engine, nmo_object_id_t object, const eng_vector_t *corners,
                               const float (*uvs)[2], const uint32_t *colors, int32_t quads,
                               nmo_object_id_t texture, int32_t source_blend, int32_t dest_blend)
{
    eng_render_push_quads(engine, object, corners, uvs, colors, quads, texture, source_blend, dest_blend);
}

enum { SHADOW_PLANE_CORNERS };

/* TT ShadowPlane's pre-render callback: the shadow quad at its corners in the target's space,
   white, in Texture, blended SourceBlend / DestBlend, without the depth test */
static void shadow_plane_draw(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)transparent;
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t target = eng_entity_index(engine, entity);
    const eng_param_t *param = block_local(b, SHADOW_PLANE_CORNERS);
    if (target < 0 || param == NULL || param->data == NULL || param->size < 12u * sizeof(float)) {
        return; /* "keine Geometriedaten" */
    }
    const float *local = (const float *)param->data;
    eng_vector_t corners[4];
    for (int i = 0; i < 4; i++) {
        const eng_vector_t p = {local[3 * i], local[3 * i + 1], local[3 * i + 2]};
        eng_vector_transform(&corners[i], &engine->entities.data[target].world, &p);
    }
    static const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    const uint32_t white = 0xFFFFFFFFu;
    push_overlay_quads(engine, entity, corners, uvs, &white, 1, nmo_engine_block_get_input_object(b, 1),
                       get_in_int(b, 3, VXBLEND_ZERO), get_in_int(b, 4, VXBLEND_SRCCOLOR));
}

/*
 * TT ShadowPlane: a textured quad under the target, its box's XZ rectangle
 * (times Size) projected from the Light onto the first floor the light's
 * ray to the target meets, drawn before the target each frame without the
 * depth test. With no floor met the quad stays where it was (at the
 * target's centre at first).
 */
static int bb_shadow_plane(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_remove_entity_callback(engine, nmo_engine_block_target(b), ENG_ENTITY_PRE_RENDER, shadow_plane_draw,
                                   b->node);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        const float zero[12] = {0};
        nmo_engine_block_set_local_value(b, SHADOW_PLANE_CORNERS, zero, sizeof(zero));
    }
    nmo_object_id_t target_id = nmo_engine_block_target(b);
    eng_entity_callback_t callback = {target_id, ENG_ENTITY_PRE_RENDER, shadow_plane_draw, b->node, true};
    eng_add_entity_callback(engine, &callback);
    int32_t light = input_entity(b, 0);
    if (light < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the DLL calls through a missing light */
    }
    float size[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 2, size, sizeof(size));
    eng_vector_t from = eng_entity_position(engine, light, target);
    eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
    eng_entity_local_box(engine, target, &min, &max);
    const eng_vector_t box[4] = {{max.x * size[0], 0.0f, max.z * size[1]}, {min.x * size[0], 0.0f, max.z * size[1]},
                                 {min.x * size[0], 0.0f, min.z * size[1]}, {max.x * size[0], 0.0f, min.z * size[1]}};
    eng_param_t *param = block_local(b, SHADOW_PLANE_CORNERS);
    if (param == NULL || param->data == NULL || param->size < 12u * sizeof(float)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    float *corners = (float *)param->data;
    int32_t type = eng_floor_attribute(engine);
    const eng_matrix_t *world = &engine->entities.data[target].world;
    eng_vector_t ray_from, ray_to;
    const eng_vector_t origin = {0.0f, 0.0f, 0.0f};
    eng_vector_transform(&ray_from, world, &from);
    eng_vector_transform(&ray_to, world, &origin);
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        int32_t floor = visible_floor(engine, &engine->attributes.values.data[i], type);
        eng_ray_hit_t hit;
        if (floor < 0 || eng_entity_ray_intersection(engine, floor, &ray_from, &ray_to, &hit) <= 0) {
            continue;
        }
        eng_vector_t hit_world, hit_local;
        eng_vector_transform(&hit_world, &engine->entities.data[floor].world, &hit.point);
        eng_entity_inverse_transform(engine, target, &hit_local, &hit_world, -1);
        float distance = (float)sqrt((double)distance_squared(&hit_local, &from));
        if (distance > 0.0f) {
            double reach = sqrt((double)(from.y * from.y + from.z * from.z + from.x * from.x));
            for (int c = 0; c < 4; c++) {
                eng_vector_t d = {box[c].x - from.x, box[c].y - from.y, box[c].z - from.z};
                double length = sqrt((double)(d.z * d.z + d.y * d.y + d.x * d.x));
                double scale = length * distance / reach;
                double inv = 1.0 / length;
                corners[3 * c] = (float)(d.x * inv * scale) + from.x;
                corners[3 * c + 1] = (float)(d.y * inv * scale) + from.y;
                corners[3 * c + 2] = (float)(d.z * inv * scale) + from.z;
            }
        }
        break;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

enum { SHADOW_MAP_DATA };
enum { SHADOW_MAP_FLOORS = 16, SHADOW_MAP_ENTRY = 7, SHADOW_MAP_WORDS = 115, SHADOW_MAP_COUNT = 114 };

/* TT ShadowMapping's floor entry: the floor, then its rectangle in the target's space ({Max, Min}) */
static uint32_t *shadow_map_entry(uint32_t *words, int32_t k)
{
    return &words[2 + SHADOW_MAP_ENTRY * k];
}

/* The shadow channel off the floor (GetChannelByMaterial, RemoveChannel) */
static void shadow_map_off(nmo_engine_t *engine, nmo_object_id_t floor, nmo_object_id_t material)
{
    nmo_object_id_t mesh = floor != 0 ? current_mesh_of(engine, floor) : 0;
    eng_mesh_copy_t *copy = mesh != 0 ? eng_mesh_copy_find(engine, mesh) : NULL;
    int32_t channel = copy != NULL ? eng_mesh_channel(copy, material) : -1;
    if (channel >= 0) {
        eng_mesh_remove_channel(copy, channel);
        copy->version = ++engine->render.mesh_version;
    }
}

/*
 * The floors TT ShadowMapping darkens (16 at most): those (shown) where a
 * corner of the target's XZ rectangle (its box times Size), projected from
 * the Light onto the floor's height in the target's space, falls within the
 * floor's box; each with the projected rectangle's bounds. As in the DLL,
 * the target's world height is tested against the floor's local box.
 */
static int32_t shadow_map_floors(blk_t *b, int32_t target, uint32_t *words)
{
    nmo_engine_t *engine = b->engine;
    int32_t light = input_entity(b, 0), count = 0;
    if (light < 0) {
        return 0;
    }
    eng_vector_t from = eng_entity_position(engine, light, target);
    float size[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 2, size, sizeof(size));
    eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
    eng_entity_local_box(engine, target, &min, &max);
    const eng_vector_t box[4] = {{max.x * size[0], 0.0f, max.z * size[1]}, {min.x * size[0], 0.0f, max.z * size[1]},
                                 {min.x * size[0], 0.0f, min.z * size[1]}, {max.x * size[0], 0.0f, min.z * size[1]}};
    float height = engine->entities.data[target].world.m[3][1];
    int32_t type = eng_floor_attribute(engine);
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        int32_t floor = visible_floor(engine, &engine->attributes.values.data[i], type);
        if (floor < 0) {
            continue;
        }
        eng_vector_t at = eng_entity_position(engine, floor, target);
        eng_vector_t fmin = {0, 0, 0}, fmax = {0, 0, 0};
        eng_entity_local_box(engine, floor, &fmin, &fmax);
        if (!(from.y > 0.0f && height > fmin.y)) {
            continue;
        }
        bool inside = false;
        float lo_x = 1000.0f, lo_z = 1000.0f, hi_x = -1000.0f, hi_z = -1000.0f;
        for (int c = 0; c < 4; c++) {
            double k = (double)(from.y - at.y) / (double)(from.y - box[c].y);
            eng_vector_t p = {(float)((box[c].x - from.x) * k + from.x), at.y, (float)((box[c].z - from.z) * k + from.z)};
            if (!inside) {
                eng_vector_t world, local;
                eng_vector_transform(&world, &engine->entities.data[target].world, &p);
                eng_entity_inverse_transform(engine, floor, &local, &world, -1);
                inside = local.x > fmin.x && local.x < fmax.x && local.z > fmin.z && local.z < fmax.z;
            }
            lo_x = p.x < lo_x ? p.x : lo_x;
            hi_x = p.x > hi_x ? p.x : hi_x;
            lo_z = p.z < lo_z ? p.z : lo_z;
            hi_z = p.z > hi_z ? p.z : hi_z;
        }
        if (inside && count < SHADOW_MAP_FLOORS) {
            uint32_t *entry = shadow_map_entry(words, count++);
            const float rect[6] = {hi_x, 0.0f, hi_z, lo_x, 0.0f, lo_z};
            entry[0] = (uint32_t)engine->attributes.values.data[i].object;
            memcpy(&entry[1], rect, sizeof(rect));
        }
    }
    return count;
}

/*
 * TT ShadowMapping: a "SimpleShadow Material" channel (Texture, clamped,
 * copied; blended SourceBlend / DestBlend) on the floors the target's
 * projected rectangle falls on, its UVs each frame the floor's vertices
 * across that rectangle in the target's space; off the floors it leaves and
 * on Off.
 */
static int bb_shadow_mapping(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t target = target_entity(b);
    if (target < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t words[SHADOW_MAP_WORDS], old[SHADOW_MAP_WORDS];
    memset(words, 0, sizeof(words));
    nmo_engine_block_get_local_value(b, SHADOW_MAP_DATA, words, sizeof(words));
    memcpy(old, words, sizeof(old));
    nmo_object_id_t material = (nmo_object_id_t)words[0];
    int32_t old_count = (int32_t)old[SHADOW_MAP_COUNT];
    old_count = old_count < 0 ? 0 : old_count > SHADOW_MAP_FLOORS ? SHADOW_MAP_FLOORS : old_count;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, nmo_engine_block_output_count(b) >= 2 ? 1 : 0);
        for (int32_t k = 0; k < old_count; k++) {
            shadow_map_off(engine, (nmo_object_id_t)shadow_map_entry(old, k)[0], material);
        }
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 1);
    if ((uint32_t)texture != words[1]) {
        words[1] = (uint32_t)texture;
        eng_material_t *desc = material != 0 ? eng_material(engine, material) : NULL;
        if (desc != NULL) {
            desc->desc.texture = texture;
        }
    }
    int32_t source_blend = get_in_int(b, 3, VXBLEND_ZERO), dest_blend = get_in_int(b, 4, VXBLEND_SRCCOLOR);
    int32_t count = shadow_map_floors(b, target, words);
    words[SHADOW_MAP_COUNT] = (uint32_t)count;
    for (int32_t k = 0; k < old_count; k++) {
        nmo_object_id_t floor = (nmo_object_id_t)shadow_map_entry(old, k)[0];
        bool kept = false;
        for (int32_t j = 0; j < count && !kept; j++) {
            kept = shadow_map_entry(words, j)[0] == (uint32_t)floor;
        }
        if (!kept) {
            shadow_map_off(engine, floor, material);
        }
    }
    for (int32_t k = 0; k < count; k++) {
        const uint32_t *entry = shadow_map_entry(words, k);
        nmo_object_id_t floor_id = (nmo_object_id_t)entry[0];
        int32_t floor = eng_entity_index(engine, floor_id);
        nmo_object_id_t mesh = current_mesh_of(engine, floor_id);
        eng_mesh_copy_t *copy = mesh != 0 ? eng_mesh_copy(engine, mesh) : NULL;
        if (copy == NULL || floor < 0) {
            continue;
        }
        int32_t channel = eng_mesh_channel(copy, material);
        if (channel < 0) {
            channel = add_uv_channel(engine, copy, material);
            if (channel < 0) {
                continue;
            }
            copy->channels.data[channel].source_blend = source_blend;
            copy->channels.data[channel].dest_blend = dest_blend;
        }
        float rect[6];
        memcpy(rect, &entry[1], sizeof(rect));
        size_t stride = 0;
        int32_t uv_count = 0;
        float *uvs = eng_mesh_uvs(engine, copy, channel, &stride, &uv_count);
        for (int32_t v = 0; uvs != NULL && v < uv_count; v++) {
            const nmo_vector_t *p = &copy->vertices[v].position;
            eng_vector_t from = {p->x, p->y, p->z}, local;
            eng_entity_inverse_transform(engine, target, &local, &from, floor);
            float *uv = uv_of(uvs, stride, v);
            uv[0] = (local.x - rect[3]) / (rect[0] - rect[3]);
            uv[1] = (local.z - rect[5]) / (rect[2] - rect[5]);
        }
        copy->version = ++engine->render.mesh_version;
    }
    nmo_engine_block_set_local_value(b, SHADOW_MAP_DATA, words, sizeof(words));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT ShadowMapping's callback: its material made on load (and attach); its channels off the floors
   on reset (and pause, new scene, script deactivation) */
static int shadow_mapping_callback(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    uint32_t words[SHADOW_MAP_WORDS];
    memset(words, 0, sizeof(words));
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_object_id_t id = eng_create_object(engine, NMO_CID_MATERIAL, "SimpleShadow Material");
        eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
        if (material == NULL && id != 0) {
            material = eng_material_new(engine, id);
        }
        if (material != NULL) {
            const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f}, black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            memcpy(material->desc.emissive, white, sizeof(white));
            memcpy(material->desc.diffuse, white, sizeof(white));
            memcpy(material->desc.specular, black, sizeof(black));
            memcpy(material->desc.ambient, black, sizeof(black));
            material->desc.address_mode = VXTEXTURE_ADDRESSCLAMP;
            material->desc.texture_blend = VXTEXTUREBLEND_COPY;
        }
        words[0] = (uint32_t)id;
        words[1] = 0xFFFFFFFFu;
        nmo_engine_block_set_local_value(b, SHADOW_MAP_DATA, words, sizeof(words));
    } else if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_PAUSE ||
               message == NMO_ENGINE_MSG_NEWSCENE || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        nmo_engine_block_get_local_value(b, SHADOW_MAP_DATA, words, sizeof(words));
        int32_t count = (int32_t)words[SHADOW_MAP_COUNT];
        for (int32_t k = 0; k < count && k < SHADOW_MAP_FLOORS; k++) {
            shadow_map_off(engine, (nmo_object_id_t)shadow_map_entry(words, k)[0], (nmo_object_id_t)words[0]);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* A flare of TT_LensFlare, from a row of its Flares Array */
typedef struct lens_flare {
    float position;     /* along the line from the eye to the light: 0 the eye, 1 the light */
    float rotation;     /* how much it turns with its place on screen */
    float size[2];
    int16_t fade_in, fade_out; /* frames */
    float cos_limit;    /* shown while the light is within this cosine of the view's axis */
    int32_t state;      /* 0 off, 1 fading in, 2 fading out, 4 shown */
    float color[3];
    float alpha, target_alpha;
    float rect[4];      /* its part of the texture in pixels (left, top, right, bottom); all 0: all of it */
} lens_flare_t;

enum { LENS_FLARES, LENS_EACH_FRAME };

static lens_flare_t lens_flare_default(void)
{
    lens_flare_t flare;
    memset(&flare, 0, sizeof(flare));
    flare.position = 1.0f;
    flare.size[0] = flare.size[1] = 1.0f;
    flare.fade_in = flare.fade_out = 100;
    flare.color[0] = flare.color[1] = flare.color[2] = 1.0f;
    return flare;
}

static lens_flare_t *lens_flares(blk_t *b, int32_t *count)
{
    eng_param_t *param = block_local(b, LENS_FLARES);
    *count = param != NULL && param->data != NULL ? (int32_t)(param->size / sizeof(lens_flare_t)) : 0;
    return *count > 0 ? (lens_flare_t *)param->data : NULL;
}

/* TT_LensFlare's post-render callback: the flares fade by whether the light is in view and not
   picked behind something, then draw as quads on a plane twice the near plane in front of the eye */
static void lens_flare_draw(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)transparent;
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t count = 0;
    lens_flare_t *flares = lens_flares(b, &count);
    int32_t light = eng_entity_index(engine, entity);
    int32_t camera = eng_entity_index(engine, engine->render.camera);
    float near_plane = 0.0f, far_plane = 0.0f;
    if (flares == NULL || light < 0 || camera < 0 ||
        !eng_camera_planes(engine, engine->render.camera, &near_plane, &far_plane)) {
        return;
    }
    float front = (float)(near_plane + 0.0099999998);
    eng_vector_t world = eng_entity_position(engine, light, -1), local;
    eng_entity_inverse_transform(engine, camera, &local, &world, -1);
    double length = sqrt((double)(local.x * local.x + local.z * local.z + local.y * local.y));
    float facing = (float)(1.0 / sqrt((double)(front * front)) * front * (local.z / length));
    eng_vector_t screen;
    eng_render_project(engine, &world, &screen);
    nmo_engine_pick_t pick;
    memset(&pick, 0, sizeof(pick));
    bool hit = engine->input.pick != NULL &&
               engine->input.pick(engine->input.pick_user_data, (int32_t)screen.x, (int32_t)screen.y, &pick);
    const eng_screen_mode_t *mode =
        &eng_screen_modes[engine->render.screen_mode >= 0 && engine->render.screen_mode < ENG_SCREEN_MODE_COUNT
                              ? engine->render.screen_mode
                              : 0];
    bool fade_in = false;
    if (!hit) {
        fade_in = !(screen.x <= 15.0f || (float)(mode->width - 15) <= screen.x || screen.y <= 15.0f);
    } else {
        eng_vector_t eye = eng_entity_position(engine, camera, -1);
        fade_in = sqrt((double)distance_squared(&eye, &world)) <= pick.distance;
    }
    if (fade_in) {
        for (int32_t i = 0; i < count; i++) {
            if (flares[i].state == 0 || (flares[i].state & 2) != 0) {
                if (flares[i].cos_limit < facing) {
                    flares[i].state = 1;
                }
            } else if (flares[i].cos_limit > facing) {
                flares[i].state = 2;
            }
        }
    } else {
        bool any = false;
        for (int32_t i = 0; i < count; i++) {
            if (flares[i].state != 0) {
                flares[i].state = 2;
                any = true;
            }
        }
        if (!any && hit) {
            return;
        }
    }
    float multiplier[2] = {1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 2, multiplier, sizeof(multiplier));
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 1);
    eng_texture_t *bitmap = texture != 0 ? eng_texture(engine, texture) : NULL;
    int32_t tw = 0, th = 0;
    float su = 1.0f, sv = 1.0f;
    if (bitmap != NULL && eng_bitmap_size(engine, bitmap, &tw, &th) && tw > 0 && th > 0) {
        su = (float)(1.0 / (double)tw);
        sv = (float)(1.0 / (double)th);
    }
    eng_vector_t *corners = (eng_vector_t *)malloc((size_t)count * 4u * sizeof(*corners));
    float (*uvs)[2] = (float (*)[2])malloc((size_t)count * 4u * sizeof(*uvs));
    uint32_t *colors = (uint32_t *)malloc((size_t)count * sizeof(*colors));
    int32_t quads = 0;
    const eng_matrix_t *eye = &engine->entities.data[camera].world;
    for (int32_t i = 0; corners != NULL && uvs != NULL && colors != NULL && i < count; i++) {
        lens_flare_t *f = &flares[i];
        if (f->state == 1) {
            f->alpha = (float)(f->target_alpha / (double)f->fade_in + f->alpha);
            if (f->alpha > f->target_alpha) {
                f->alpha = f->target_alpha;
                f->state = 4;
            }
        } else if (f->state == 2) {
            f->alpha = (float)(f->alpha - f->target_alpha / (double)f->fade_out);
            if (f->alpha < 0.0099999998f) {
                f->state = 0;
                f->alpha = 0.0f;
                continue;
            }
        } else if (f->state != 4) {
            continue;
        }
        float plane = front + front;
        float scale = plane / local.z;
        float x = f->position * local.x * scale, y = local.y * f->position * scale;
        float c = 1.0f, s = 0.0f;
        if (f->rotation != 0.0f) {
            double angle = (y + x) * f->rotation;
            c = (float)cos(angle);
            s = (float)sin(angle);
        }
        float sx = multiplier[0] * f->size[0], sy = multiplier[1] * f->size[1];
        float ax = sx * c, ay = sx * -s, bx = sy * s, by = c * sy;
        const eng_vector_t quad[4] = {{bx + (x - ax), by + (y - ay), plane},
                                      {bx + ax + x, by + ay + y, plane},
                                      {ax + x - bx, ay + y - by, plane},
                                      {x - ax - bx, y - ay - by, plane}};
        for (int k = 0; k < 4; k++) {
            eng_vector_transform(&corners[4 * quads + k], eye, &quad[k]);
        }
        float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
        if (f->rect[0] != f->rect[2] && f->rect[3] != f->rect[1]) {
            u0 = su * f->rect[0];
            u1 = su * f->rect[2];
            v0 = sv * f->rect[1];
            v1 = sv * f->rect[3];
        }
        const float quad_uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        memcpy(uvs[4 * quads], quad_uv, sizeof(quad_uv));
        const float tone[4] = {f->color[0], f->color[1], f->color[2], f->alpha};
        colors[quads++] = vxcolor_to_color(tone);
    }
    if (quads > 0) {
        push_overlay_quads(engine, entity, corners, (const float (*)[2])uvs, colors, quads, texture,
                           get_in_int(b, 3, VXBLEND_ONE), get_in_int(b, 4, VXBLEND_ZERO));
    }
    free(corners);
    free(uvs);
    free(colors);
}

/* CKDataArray::GetColumnParameterGuid: a PARAMETER column's type; null for another column or none */
static nmo_guid_t eng_array_column_param_type(nmo_engine_t *engine, nmo_object_id_t array, int32_t column)
{
    const eng_array_t *runtime = eng_find_array(engine, array);
    if (runtime == NULL || column < 0 || (uint32_t)column >= runtime->column_count ||
        runtime->columns[column].type != CKARRAYTYPE_PARAMETER) {
        return NMO_GUID(0u, 0u);
    }
    return runtime->columns[column].param_type;
}

/* The flares read from Flares Array: a row each, the columns that have the expected types */
static void lens_flare_read(blk_t *b, nmo_object_id_t array)
{
    nmo_engine_t *engine = b->engine;
    int32_t rows = nmo_engine_array_row_count(engine, array);
    int32_t held = 0;
    lens_flares(b, &held);
    if (rows != held) {
        lens_flare_t *made = rows > 0 ? (lens_flare_t *)malloc((size_t)rows * sizeof(*made)) : NULL;
        for (int32_t i = 0; made != NULL && i < rows; i++) {
            made[i] = lens_flare_default();
        }
        nmo_engine_block_set_local_value(b, LENS_FLARES, made, made != NULL ? (size_t)rows * sizeof(*made) : 0);
        free(made);
    }
    int32_t count = 0;
    lens_flare_t *flares = lens_flares(b, &count);
    for (int32_t i = 0; i < count && i < rows; i++) {
        nmo_engine_cell_t cell;
        lens_flare_t *f = &flares[i];
        if (nmo_engine_array_column_type(engine, array, 0) == CKARRAYTYPE_PARAMETER &&
            nmo_guid_equals(eng_array_column_param_type(engine, array, 0), CKPGUID_RECT) &&
            nmo_engine_array_get_cell(engine, array, i, 0, &cell) == NMO_OK && cell.data != NULL && cell.size >= 16) {
            memcpy(f->rect, cell.data, sizeof(f->rect));
        }
        if (nmo_engine_array_column_type(engine, array, 1) == CKARRAYTYPE_FLOAT &&
            nmo_engine_array_get_cell(engine, array, i, 1, &cell) == NMO_OK) {
            memcpy(&f->position, &cell.value, sizeof(float));
        }
        if (nmo_guid_equals(eng_array_column_param_type(engine, array, 2), CKPGUID_COLOR) &&
            nmo_engine_array_get_cell(engine, array, i, 2, &cell) == NMO_OK && cell.data != NULL && cell.size >= 16) {
            float color[4];
            memcpy(color, cell.data, sizeof(color));
            f->color[0] = color[0], f->color[1] = color[1], f->color[2] = color[2];
            f->target_alpha = color[3];
        }
        if (nmo_guid_equals(eng_array_column_param_type(engine, array, 3), CKPGUID_2DVECTOR) &&
            nmo_engine_array_get_cell(engine, array, i, 3, &cell) == NMO_OK && cell.data != NULL && cell.size >= 8) {
            memcpy(f->size, cell.data, sizeof(f->size));
        }
        if (nmo_engine_array_column_type(engine, array, 4) == CKARRAYTYPE_FLOAT &&
            nmo_engine_array_get_cell(engine, array, i, 4, &cell) == NMO_OK) {
            memcpy(&f->rotation, &cell.value, sizeof(float));
        }
        if (nmo_engine_array_column_type(engine, array, 5) == CKARRAYTYPE_INT &&
            nmo_engine_array_get_cell(engine, array, i, 5, &cell) == NMO_OK) {
            f->fade_in = (int16_t)cell.value;
        }
        if (nmo_engine_array_column_type(engine, array, 6) == CKARRAYTYPE_INT &&
            nmo_engine_array_get_cell(engine, array, i, 6, &cell) == NMO_OK) {
            f->fade_out = (int16_t)cell.value;
        }
        if (nmo_guid_equals(eng_array_column_param_type(engine, array, 7), CKPGUID_ANGLE) &&
            nmo_engine_array_get_cell(engine, array, i, 7, &cell) == NMO_OK && cell.data != NULL && cell.size >= 4) {
            float angle = 0.0f;
            memcpy(&angle, cell.data, sizeof(angle));
            if (angle == 0.0f) {
                angle = 1.5700001f;
            }
            f->cos_limit = (float)cos((double)angle);
        }
    }
}

/*
 * TT_LensFlare: lens flares for the target light, drawn after it each frame
 * (with the render context's camera); On reads the flares from Flares
 * Array (then each frame with Read Array Each Frame), Off stops them. The
 * DLL draws the quads with the light's world matrix current; they are taken
 * here in the camera's space, where their coordinates are worked out.
 */
static int bb_lens_flare(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0 || eng_entity_index(engine, target) < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool each_frame = get_local_int(b, LENS_EACH_FRAME, 0) != 0;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        eng_remove_entity_callback(engine, target, ENG_ENTITY_POST_RENDER, lens_flare_draw, b->node);
        return NMO_ENGINE_BR_OK;
    }
    eng_entity_callback_t callback = {target, ENG_ENTITY_POST_RENDER, lens_flare_draw, b->node, true};
    eng_add_entity_callback(engine, &callback);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        each_frame = true;
    }
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    if (each_frame && array != 0) {
        lens_flare_read(b, array);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT_LensFlare's callback: one default flare on load (and attach) */
static int lens_flare_callback(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        lens_flare_t flare = lens_flare_default();
        nmo_engine_block_set_local_value(b, LENS_FLARES, &flare, sizeof(flare));
    }
    return NMO_ENGINE_BR_OK;
}

/* ---- TT_SceneStencilShadow ---- */

/* The attribute type of that name; -1 for none */
static int32_t attribute_type_named_tt(const nmo_engine_t *engine, const char *name)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, name) == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

/* An edge of a caster's mesh: its vertices, the faces either side (face1 -1 for an open edge) and
   their centres, as the DLL keeps it */
typedef struct stencil_edge {
    int32_t v0, v1, face0, face1;
    eng_vector_t centre0, centre1;
} stencil_edge_t;

/* An object with the StencilShadow attribute, and its shadow volume */
typedef struct stencil_caster {
    nmo_object_id_t object, mesh;          /* the object and the mesh its attribute names */
    nmo_object_id_t shadow_entity, shadow_mesh;
    float length;                          /* how far the volume reaches */
    bool fixed;                            /* the volume made at On only */
    bool redraw;                           /* the object drawn again over its shadow */
    stencil_edge_t *edges;
    int32_t edge_count;
    int32_t (*pairs)[2];                   /* the silhouette edges, facing the volume's outside */
    int32_t pair_count;
    int32_t slots;                         /* the quads the shadow mesh has room for */
} stencil_caster_t;

typedef struct stencil_block {
    int32_t node;
    nmo_object_id_t sprite, material;
    stencil_caster_t *casters;
    int32_t caster_count;
} stencil_block_t;

/* The toolbox's render context post render callbacks of the frame (temporary ones) */
typedef struct toolbox_render {
    ENG_VEC(stencil_block_t) stencil_blocks;
    ENG_VEC(int32_t) stencil_frame;
} toolbox_render_t;

static void stencil_casters_free(stencil_block_t *block)
{
    for (int32_t i = 0; i < block->caster_count; i++) {
        free(block->casters[i].edges);
        free(block->casters[i].pairs);
    }
    free(block->casters);
    block->casters = NULL;
    block->caster_count = 0;
}

static void toolbox_render_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    toolbox_render_t *tr = (toolbox_render_t *)state;
    for (size_t i = 0; i < tr->stencil_blocks.count; i++) {
        stencil_casters_free(&tr->stencil_blocks.data[i]);
    }
    free(tr->stencil_blocks.data);
    free(tr->stencil_frame.data);
}

static void stencil_shadow_draw(nmo_engine_t *engine, stencil_block_t *block);

static void toolbox_render_hook(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                                const eng_render_args_t *args)
{
    toolbox_render_t *tr = (toolbox_render_t *)state;
    if (stage != ENG_RENDER_SCENE) {
        return;
    }
    for (size_t i = 0; i < tr->stencil_frame.count; i++) {
        for (size_t k = 0; k < tr->stencil_blocks.count; k++) {
            if (tr->stencil_blocks.data[k].node == tr->stencil_frame.data[i]) {
                stencil_shadow_draw(engine, &tr->stencil_blocks.data[k]);
            }
        }
    }
    if (!args->pass) {
        tr->stencil_frame.count = 0;
    }
}

static const eng_manager_desc_t toolbox_render = {
    .name = "TT Toolbox render callbacks",
    .state_size = sizeof(toolbox_render_t),
    .destroy = toolbox_render_destroy,
    .render = toolbox_render_hook,
};

static stencil_block_t *stencil_block(nmo_engine_t *engine, int32_t node, bool make)
{
    toolbox_render_t *tr = (toolbox_render_t *)eng_manager_state(engine, &toolbox_render);
    for (size_t i = 0; tr != NULL && i < tr->stencil_blocks.count; i++) {
        if (tr->stencil_blocks.data[i].node == node) {
            return &tr->stencil_blocks.data[i];
        }
    }
    stencil_block_t *block = NULL;
    if (tr == NULL || !make || !ENG_VEC_PUSH(tr->stencil_blocks, block)) {
        return NULL;
    }
    memset(block, 0, sizeof(*block));
    block->node = node;
    return block;
}

/* The mesh's state, and its positions and face normals now */
static const nmo_mesh_state_t *caster_mesh(nmo_engine_t *engine, nmo_object_id_t mesh, const nmo_vertex_t **vertices,
                                           const nmo_face_t **faces)
{
    nmo_object_t *object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    const nmo_mesh_state_t *state = object != NULL ? (const nmo_mesh_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_MESH) : NULL;
    if (state == NULL) {
        return NULL;
    }
    const eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh);
    *vertices = copy != NULL ? copy->vertices : state->vertices;
    *faces = copy != NULL && copy->faces != NULL ? copy->faces : state->faces;
    return state;
}

static eng_vector_t mesh_point(const nmo_vertex_t *vertices, int32_t v)
{
    return (eng_vector_t){vertices[v].position.x, vertices[v].position.y, vertices[v].position.z};
}

static bool same_point(const eng_vector_t *a, const eng_vector_t *c)
{
    return a->x == c->x && a->y == c->y && a->z == c->z;
}

static eng_vector_t face_centre(const nmo_mesh_state_t *state, const nmo_vertex_t *vertices, int32_t face)
{
    const uint16_t *k = &state->face_vertex_indices[3 * face];
    eng_vector_t a = mesh_point(vertices, k[0]), b = mesh_point(vertices, k[1]), c = mesh_point(vertices, k[2]);
    return (eng_vector_t){(float)((b.x + a.x + c.x) * 0.33333334), (float)((b.y + a.y + c.y) * 0.33333334f),
                          (float)((b.z + a.z + c.z) * 0.33333334f)};
}

/* The DLL's AddEdge: the edge, unless one with its vertices (by index or place, either way round)
   is there */
static void stencil_add_edge(stencil_caster_t *caster, const nmo_mesh_state_t *state, const nmo_vertex_t *vertices,
                             int32_t v0, int32_t v1, int32_t face0, int32_t face1)
{
    eng_vector_t p0 = mesh_point(vertices, v0), p1 = mesh_point(vertices, v1);
    for (int32_t i = 0; i < caster->edge_count; i++) {
        const stencil_edge_t *e = &caster->edges[i];
        eng_vector_t q0 = mesh_point(vertices, e->v0), q1 = mesh_point(vertices, e->v1);
        if ((e->v0 == v0 && e->v1 == v1) || (e->v0 == v1 && e->v1 == v0) || (same_point(&p0, &q0) && same_point(&p1, &q1)) ||
            (same_point(&p0, &q1) && same_point(&p1, &q0))) {
            return;
        }
    }
    stencil_edge_t *e = &caster->edges[caster->edge_count++];
    e->v0 = v0;
    e->v1 = v1;
    e->face0 = face0;
    e->face1 = face1;
    e->centre0 = face_centre(state, vertices, face0);
    e->centre1 = face1 >= 0 ? face_centre(state, vertices, face1) : (eng_vector_t){0, 0, 0};
}

/* The DLL's edge search: another face with an edge at the same places (either way round) makes the
   edge a shared one; else it is open, added the other way round */
static void stencil_edge_of_face(stencil_caster_t *caster, const nmo_mesh_state_t *state, const nmo_vertex_t *vertices,
                                 int32_t face, int32_t a, int32_t b)
{
    eng_vector_t pa = mesh_point(vertices, a), pb = mesh_point(vertices, b);
    for (int32_t g = 0; g < (int32_t)state->face_count; g++) {
        if (g == face) {
            continue;
        }
        const uint16_t *k = &state->face_vertex_indices[3 * g];
        eng_vector_t A = mesh_point(vertices, k[0]), B = mesh_point(vertices, k[1]), C = mesh_point(vertices, k[2]);
        if ((same_point(&pa, &A) && (same_point(&pb, &B) || same_point(&pb, &C))) ||
            (same_point(&pa, &B) && (same_point(&pb, &A) || same_point(&pb, &C))) ||
            (same_point(&pa, &C) && (same_point(&pb, &B) || same_point(&pb, &A)))) {
            stencil_add_edge(caster, state, vertices, a, b, face, g);
            return;
        }
    }
    stencil_add_edge(caster, state, vertices, b, a, face, -1);
}

/* CK3dEntity::GetMesh(index) */
static nmo_object_id_t entity_mesh_at(nmo_engine_t *engine, nmo_object_id_t entity, int32_t index)
{
    const nmo_3dentity_state_t *state = (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
        engine->types, eng_find_object(engine, entity), CKPGUID_3DENTITY);
    if (state == NULL || index < 0) {
        return 0;
    }
    if ((uint32_t)index < state->mesh_count && state->mesh_ids != NULL) {
        return eng_object_ref(engine, entity, &state->mesh_ids[index]);
    }
    return index == 0 ? eng_object_ref(engine, entity, &state->current_mesh) : 0;
}

/* On: the casters' edges, and a shadow entity and mesh each (with room for every edge) */
static bool stencil_casters_make(blk_t *b, stencil_block_t *block, int32_t type)
{
    nmo_engine_t *engine = b->engine;
    stencil_casters_free(block);
    int32_t holders = 0;
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        holders += engine->attributes.values.data[i].type == type ? 1 : 0;
    }
    nmo_engine_block_set_local_value(b, 4, &holders, sizeof(holders));
    block->casters = holders > 0 ? (stencil_caster_t *)calloc((size_t)holders, sizeof(*block->casters)) : NULL;
    for (size_t i = 0; block->casters != NULL && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        if (attribute->type != type || !nmo_engine_object_is_visible(engine, attribute->object)) {
            continue;
        }
        int32_t fields[4] = {0, 0, 0, 0};
        for (uint32_t f = 0; f < 4; f++) {
            eng_struct_field(engine, attribute->object, attribute->value.data, attribute->value.size, f, &fields[f]);
        }
        stencil_caster_t *caster = &block->casters[block->caster_count];
        caster->object = attribute->object;
        caster->fixed = (fields[1] & 0xFF) != 0;
        memcpy(&caster->length, &fields[2], sizeof(float));
        caster->redraw = (fields[3] & 0xFF) != 0;
        caster->mesh = entity_mesh_at(engine, attribute->object, fields[0]);
        const nmo_vertex_t *vertices = NULL;
        const nmo_face_t *faces = NULL;
        const nmo_mesh_state_t *state = caster_mesh(engine, caster->mesh, &vertices, &faces);
        if (state == NULL || vertices == NULL) {
            return false; /* "3dEntity hat keine Mesh!" */
        }
        block->caster_count++;
        int32_t face_count = (int32_t)state->face_count;
        caster->edges = (stencil_edge_t *)calloc((size_t)(face_count > 0 ? 3 * face_count : 1), sizeof(*caster->edges));
        for (int32_t face = 0; caster->edges != NULL && state->face_vertex_indices != NULL && face < face_count; face++) {
            const uint16_t *k = &state->face_vertex_indices[3 * face];
            if (k[0] >= state->vertex_count || k[1] >= state->vertex_count || k[2] >= state->vertex_count) {
                continue;
            }
            stencil_edge_of_face(caster, state, vertices, face, k[0], k[1]);
            stencil_edge_of_face(caster, state, vertices, face, k[1], k[2]);
            stencil_edge_of_face(caster, state, vertices, face, k[2], k[0]);
        }
        caster->pairs = (int32_t (*)[2])calloc((size_t)(caster->edge_count > 0 ? caster->edge_count : 1),
                                               sizeof(*caster->pairs));
        caster->slots = caster->edge_count > 16383 ? 16383 : caster->edge_count;
        nmo_mesh_state_t *shadow = NULL;
        caster->shadow_entity = eng_create_object(engine, NMO_CID_3DENTITY, "ShadowEnt");
        caster->shadow_mesh = eng_create_mesh(engine, "ShadowMesh", 4 * caster->slots, 2 * caster->slots, &shadow);
        if (caster->shadow_entity == 0 || caster->shadow_mesh == 0) {
            return false; /* "ShadowEntity konnte nicht erstellt werden" */
        }
        for (int32_t s = 0; s < caster->slots; s++) {
            const uint16_t q = (uint16_t)(4 * s);
            const uint16_t quad[6] = {q, (uint16_t)(q + 2), (uint16_t)(q + 1), (uint16_t)(q + 1), (uint16_t)(q + 2),
                                      (uint16_t)(q + 3)};
            memcpy(&shadow->face_vertex_indices[6 * s], quad, sizeof(quad));
        }
        eng_entity_set_current_mesh(engine, caster->shadow_entity, caster->shadow_mesh);
    }
    return true;
}

/* The light's direction for a caster: toward its target (a spot with one), else against its Z axis
   (in the world, as the DLL takes it) */
static eng_vector_t stencil_light_direction(nmo_engine_t *engine, int32_t light, int32_t object,
                                            const eng_vector_t *at, int32_t type)
{
    if (type == VX_LIGHTSPOT) {
        const eng_light_t *state = eng_light(engine, engine->entities.data[light].id);
        int32_t target = state != NULL ? eng_entity_index(engine, state->target) : -1;
        if (target >= 0) {
            eng_vector_t to = eng_entity_position(engine, target, object);
            eng_vector_t d = {at->x - to.x, at->y - to.y, at->z - to.z};
            return unit_of(&d);
        }
    }
    eng_vector_t dir, up, right;
    eng_entity_orientation(engine, light, &dir, &up, &right, -1);
    return (eng_vector_t){dir.x * -1.0f, dir.y * -1.0f, dir.z * -1.0f};
}

/* The silhouette and the volume of a caster this frame, in the object's space */
static void stencil_caster_update(nmo_engine_t *engine, stencil_caster_t *caster, int32_t light, int32_t type)
{
    int32_t object = eng_entity_index(engine, caster->object);
    const nmo_vertex_t *vertices = NULL;
    const nmo_face_t *faces = NULL;
    const nmo_mesh_state_t *state = caster_mesh(engine, caster->mesh, &vertices, &faces);
    if (object < 0 || state == NULL || vertices == NULL || faces == NULL) {
        return;
    }
    eng_vector_t at = eng_entity_position(engine, light, object);
    bool point = type == VX_LIGHTPOINT;
    eng_vector_t toward = point ? (eng_vector_t){0, 0, 0} : stencil_light_direction(engine, light, object, &at, type);
    caster->pair_count = 0;
    for (int32_t i = 0; i < caster->edge_count; i++) {
        const stencil_edge_t *e = &caster->edges[i];
        const nmo_vector_t *n0 = &faces[e->face0].normal;
        if (point) {
            eng_vector_t d = {at.x - e->centre0.x, at.y - e->centre0.y, at.z - e->centre0.z};
            toward = unit_of(&d);
        }
        bool lit0 = n0->x * toward.x + toward.z * n0->z + toward.y * n0->y >= 0.0f;
        if (e->face1 == -1) {
            if (lit0) {
                caster->pairs[caster->pair_count][0] = e->v1;
                caster->pairs[caster->pair_count++][1] = e->v0;
            }
            continue;
        }
        const nmo_vector_t *n1 = &faces[e->face1].normal;
        if (point) {
            eng_vector_t d = {at.x - e->centre1.x, at.y - e->centre1.y, at.z - e->centre1.z};
            toward = unit_of(&d);
        }
        bool lit1 = n1->x * toward.x + toward.z * n1->z + toward.y * n1->y >= 0.0f;
        if (lit0 && !lit1) {
            caster->pairs[caster->pair_count][0] = e->v0;
            caster->pairs[caster->pair_count++][1] = e->v1;
        } else if (!lit0 && lit1) {
            caster->pairs[caster->pair_count][0] = e->v1;
            caster->pairs[caster->pair_count++][1] = e->v0;
        }
    }
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, caster->shadow_mesh);
    if (copy == NULL) {
        return;
    }
    for (int32_t s = 0; s < caster->slots; s++) {
        nmo_vertex_t *v = &copy->vertices[4 * s];
        if (s >= caster->pair_count) {
            for (int k = 0; k < 4; k++) {
                v[k].position = (nmo_vector_t){0.0f, 0.0f, 0.0f}; /* a quad with no room: out of the way */
            }
            continue;
        }
        eng_vector_t pa = mesh_point(vertices, caster->pairs[s][0]), pb = mesh_point(vertices, caster->pairs[s][1]);
        eng_vector_t da = toward, db = toward;
        if (point) {
            eng_vector_t a = {at.x - pa.x, at.y - pa.y, at.z - pa.z}, c = {at.x - pb.x, at.y - pb.y, at.z - pb.z};
            da = unit_of(&a);
            db = unit_of(&c);
        }
        v[0].position = (nmo_vector_t){pa.x, pa.y, pa.z};
        v[1].position = (nmo_vector_t){pb.x, pb.y, pb.z};
        v[2].position = (nmo_vector_t){pa.x - da.x * caster->length, pa.y - da.y * caster->length,
                                       pa.z - da.z * caster->length};
        v[3].position = (nmo_vector_t){pb.x - db.x * caster->length, pb.y - db.y * caster->length,
                                       pb.z - db.z * caster->length};
    }
    eng_mesh_vertices_moved(engine, copy, false);
}

/*
 * TT_SceneStencilShadow's render callback: each caster's volume drawn to the
 * stencil only, its front then (turned over) its back; the ShadowSprite
 * over the screen where the stencil is not 0; the casters asking for it
 * drawn again there without the depth test. As in CK, a stencil-only mesh
 * writes 1 where it passes the depth test whatever the block's increment
 * and decrement states, so the shadow is wherever a volume shows. The DLL
 * clears the stencil first; the frame's clear stands in for it here.
 */
static void stencil_shadow_draw(nmo_engine_t *engine, stencil_block_t *block)
{
    nmo_engine_stencil_t saved = engine->render.stencil;
    for (int pass = 0; pass < 2; pass++) {
        for (int32_t i = 0; i < block->caster_count; i++) {
            const stencil_caster_t *caster = &block->casters[i];
            int32_t object = eng_entity_index(engine, caster->object);
            if (object < 0 || caster->shadow_entity == 0) {
                continue;
            }
            nmo_engine_draw_t *draw = eng_render_entity_mesh(engine, caster->shadow_entity, caster->shadow_mesh,
                                                             &engine->entities.data[object].world, false);
            if (draw != NULL) {
                draw->stencil_only = true;
                draw->mirrored = pass == 1;
            }
        }
    }
    eng_render_use_stencil_bits(engine, 1u);
    nmo_engine_stencil_t *stencil = &engine->render.stencil;
    stencil->enabled = true;
    stencil->func = VXCMP_NOTEQUAL;
    stencil->fail = stencil->zfail = stencil->pass = VXSTENCILOP_KEEP;
    stencil->ref = 0;
    stencil->mask = 0xFFFFFFFFu;
    stencil->write_mask = 0xFFu;
    if (block->sprite != 0) {
        const eng_screen_mode_t *mode =
            &eng_screen_modes[engine->render.screen_mode >= 0 && engine->render.screen_mode < ENG_SCREEN_MODE_COUNT
                                  ? engine->render.screen_mode
                                  : 0];
        eng_show(engine, block->sprite, 1);
        eng_render_2d_entity(engine, block->sprite, (float)mode->width, (float)mode->height);
        eng_show(engine, block->sprite, 0);
    }
    for (int32_t i = 0; i < block->caster_count; i++) {
        const stencil_caster_t *caster = &block->casters[i];
        int32_t object = eng_entity_index(engine, caster->object);
        nmo_object_id_t mesh = object >= 0 ? current_mesh_of(engine, caster->object) : 0;
        if (!caster->redraw || mesh == 0) {
            continue;
        }
        nmo_engine_draw_t *draw = eng_render_entity_mesh(engine, caster->object, mesh,
                                                         &engine->entities.data[object].world, false);
        if (draw != NULL) {
            draw->no_z_test = true;
        }
    }
    engine->render.stencil = saved;
}

/*
 * TT_SceneStencilShadow: shadows of the objects with the StencilShadow
 * attribute (its mesh index, whether the shadow is made once, how far it
 * reaches, whether the object is drawn over it) from Light, in ShadowColor,
 * drawn after the scene each frame. On makes each caster's edges (faces
 * meeting at the same places), the "ShadowSprite" and its "ShadowMat";
 * each frame the edges between a lit and an unlit face (or open and lit)
 * are pushed away from the light into a volume. The machine has a stencil
 * buffer.
 */
static int bb_scene_stencil_shadow(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    int32_t light = input_entity(b, 0);
    if (light < 0) {
        return NMO_ENGINE_BR_GENERICERROR; /* "kein Licht ausgewählt" */
    }
    const eng_light_t *light_state = eng_light(engine, engine->entities.data[light].id);
    int32_t light_type = light_state != NULL ? light_state->type : VX_LIGHTPOINT;
    nmo_engine_block_set_local_value(b, 3, &engine->entities.data[light].world, sizeof(eng_matrix_t));
    toolbox_render_t *tr = (toolbox_render_t *)eng_manager_state(engine, &toolbox_render);
    int32_t *queued = NULL;
    if (tr != NULL && ENG_VEC_PUSH(tr->stencil_frame, queued)) {
        *queued = b->node;
    }
    bool on = false;
    stencil_block_t *block = stencil_block(engine, b->node, true);
    if (IN_ACTIVE(b, 0)) {
        on = true;
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        if (block == NULL) {
            return NMO_ENGINE_BR_GENERICERROR;
        }
        block->sprite = eng_create_object(engine, NMO_CID_2DENTITY, "ShadowSprite");
        block->material = eng_create_object(engine, NMO_CID_MATERIAL, "ShadowMat");
        eng_material_t *material = block->material != 0 ? eng_material(engine, block->material) : NULL;
        if (material == NULL && block->material != 0) {
            material = eng_material_new(engine, block->material);
        }
        if (block->sprite == 0 || material == NULL) {
            return NMO_ENGINE_BR_GENERICERROR; /* "Sprite konnte nicht erstellt werden" */
        }
        float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 1, color, sizeof(color));
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f}, white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        memcpy(material->desc.ambient, black, sizeof(black));
        memcpy(material->desc.diffuse, color, sizeof(color));
        memcpy(material->desc.specular, black, sizeof(black));
        memcpy(material->desc.emissive, white, sizeof(white));
        material->desc.alpha_blend = true;
        material->desc.source_blend = VXBLEND_SRCALPHA;
        material->desc.dest_blend = VXBLEND_INVSRCALPHA;
        eng_add_to_level_scene(engine, block->sprite);
        eng_2d_t *sprite = eng_2d_entity(engine, block->sprite);
        if (sprite != NULL) {
            sprite->flags |= CK_2DENTITY_USEHOMOGENEOUSCOORD; /* SetHomogeneousCoordinates */
            sprite->material = block->material;
        }
        eng_2d_set_position(engine, block->sprite, 0.0f, 0.0f, true, false, 0);
        eng_2d_set_size(engine, block->sprite, 1.0f, 1.0f, true, false);
        eng_show(engine, block->sprite, 0);
        uint32_t ids[2] = {(uint32_t)block->sprite, (uint32_t)block->material};
        nmo_engine_block_set_local_value(b, 1, &ids[0], sizeof(ids[0]));
        nmo_engine_block_set_local_value(b, 2, &ids[1], sizeof(ids[1]));
        int32_t type = attribute_type_named_tt(engine, "StencilShadow");
        if (type >= 0 && !stencil_casters_make(b, block, type)) {
            return NMO_ENGINE_BR_GENERICERROR;
        }
    }
    for (int32_t i = 0; block != NULL && i < block->caster_count; i++) {
        if (block->casters[i].fixed && !on) {
            continue;
        }
        stencil_caster_update(engine, &block->casters[i], light, light_type);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Sprites, buttons, motor sounds and fonts
 * ============================================================================ */

/* TT GetUV2DSprite: the target 2D entity's source rect. */
static int bb_get_uv_2d_sprite(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    const eng_2d_t *entity = eng_2d_entity(b->engine, nmo_engine_block_target(b));
    if (entity == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    set_out(b, 0, &entity->source, sizeof(entity->source));
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Set_3DSprite: the target sprite's Position (world), Size, Offset and
 * UV mapping (UV Left Top to UV Right Bottom), each when the "Add Input"
 * setting has it as an input (8, 4, 2 and 1).
 */
static int bb_set_3d_sprite(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t inputs = get_local_int(b, 0, 0);
    int pos = 0;
    if (inputs >= 8) {
        inputs -= 8;
        eng_vector_t position = get_in_vector(b, pos++, 0.0f, 0.0f, 0.0f);
        int32_t entity = eng_entity_index(engine, target);
        if (entity >= 0) {
            eng_entity_set_position(engine, entity, &position, -1, false);
        }
    }
    if (inputs >= 4) {
        inputs -= 4;
        nmo_vector2_t size = {1.0f, 1.0f};
        nmo_engine_block_get_input_value(b, pos++, &size, sizeof(size));
        eng_sprite3d_set_size(engine, target, size);
    }
    if (inputs >= 2) {
        inputs -= 2;
        nmo_vector2_t offset = {0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, pos++, &offset, sizeof(offset));
        eng_sprite3d_set_offset(engine, target, offset);
    }
    if (inputs >= 1) {
        nmo_vector2_t left_top = {0.0f, 0.0f}, right_bottom = {0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, pos, &left_top, sizeof(left_top));
        nmo_engine_block_get_input_value(b, pos + 1, &right_bottom, sizeof(right_bottom));
        eng_sprite3d_t *sprite = eng_sprite3d(engine, target);
        if (sprite != NULL) {
            sprite->uv_rect = (nmo_rect_t){left_top.x, left_top.y, right_bottom.x, right_bottom.y};
        }
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT PushButton: the target 2D entity takes the Rollover Material and Roll
 * Over fires when the mouse comes over it, the Released Material and
 * Released when it leaves; over it, the left button going down gives the
 * Pressed Material and Mouse Down, going up the Rollover Material and Mouse
 * Up. On clears the state (local 0) but leaves the button down (local 1):
 * the DLL zeroes only the low byte of the bool it writes, whose other bytes
 * are a pointer's.
 */
static int bb_push_button(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, 0);
        set_local_int(b, 1, 1);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t state = target == nmo_engine_entity_under_mouse(engine) ? 1 : 0;
    bool down = nmo_engine_is_mouse_button_down(engine, 0);
    int32_t old_state = get_local_int(b, 0, 0);
    bool old_down = get_local_int(b, 1, 0) != 0;
    bool changed = down != old_down;
    if (changed) {
        set_local_int(b, 1, down ? 1 : 0);
    }
    eng_2d_t *entity = eng_2d_entity(engine, target);
    if (state != old_state) {
        set_local_int(b, 0, state);
        int pin = state != 0 ? 2 : 0; /* Rollover, Released Material */
        if (entity != NULL) {
            entity->material = nmo_engine_block_get_input_object(b, pin);
        }
        OUT_ON(b, state != 0 ? 1 : 0); /* Roll Over, Released */
    }
    if (changed && state == 1) {
        if (entity != NULL) {
            entity->material = nmo_engine_block_get_input_object(b, down ? 1 : 2);
        }
        OUT_ON(b, down ? 2 : 3); /* Mouse Down, Mouse Up */
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* fscanf with a format of text, white space and %f: the floats read go to `out` in order up
   to the first thing that does not match, and `*text` moves past what matched. */
static void motor_scan(const char **text, const char *format, float *const *out)
{
    const char *s = *text;
    int n = 0;
    for (const char *f = format; *f != '\0'; f++) {
        if (*f == ' ' || *f == '\n') {
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
                s++;
            }
        } else if (f[0] == '%' && f[1] == 'f') {
            f++;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
                s++;
            }
            char *end = NULL;
            double value = strtod(s, &end);
            if (end == s) {
                break;
            }
            *out[n++] = (float)value;
            s = end;
        } else if (*s == *f) {
            s++;
        } else {
            break;
        }
    }
    *text = s;
}

/*
 * TT_LoadMotorSettings: the motor settings every TT_MotorSound uses, from
 * SettingsFile: a Volume line, then a line of pitches, speeds and fades for
 * each sound (Loading Finished). A value the file does not have stays as it
 * was. Error when the file is missing, or empty, which reads the same here.
 */
static int bb_load_motor_settings(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    const char *name = input_string(b, 0);
    size_t size = 0;
    unsigned char *data = name != NULL ? eng_read_file(engine, name, &size) : NULL;
    char *text = data != NULL ? (char *)malloc(size + 1u) : NULL;
    if (text == NULL) {
        free(data);
        if (eng_tt(engine)->debug_mode) {
            eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0); /* "File motor.txt ..." */
        }
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    memcpy(text, data, size);
    text[size] = '\0';
    free(data);
    eng_tt_t *tt = eng_tt(engine);
    const char *p = text;
    float *volume[1] = {&tt->motor_volume};
    motor_scan(&p, "Volume:%f\n", volume);
    for (int i = 0; i < 3; i++) {
        eng_motor_band_t *band = &tt->motor_bands[i];
        float *fields[6] = {&band->pitch_max, &band->pitch_min, &band->speed_max,
                            &band->speed_min, &band->fade_in, &band->fade_out};
        motor_scan(&p, "PitchMax:%f PitchMin:%f vMax:%f vMin:%f vFadeIn:%f vFadeOut:%f\n", fields);
    }
    free(text);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The DLL's line through (x_b, b) and (x_a, a), at x */
static float motor_map(float x, float a, float b, float x_a, float x_b)
{
    return (float)(((double)a - b) / ((double)x_a - x_b) * ((double)x - x_b) + b);
}

static bool motor_sound_playing(const eng_sound_t *sound)
{
    return sound->playing && !sound->paused;
}

/* One sound of TT_MotorSound at `speed`: its pitch and volume (0 when out of its band) */
static void motor_sound_band(blk_t *b, eng_sound_t *sound, const eng_motor_band_t *band,
                             float speed, float *pitch, float *volume)
{
    eng_tt_t *tt = eng_tt(b->engine);
    if (speed < band->speed_min || speed >= band->speed_max) {
        if (motor_sound_playing(sound)) {
            eng_sound_stop(sound, 0.0f);
        }
        return;
    }
    *pitch = motor_map(speed, band->pitch_max, band->pitch_min, band->speed_max, band->speed_min);
    sound->pitch = *pitch;
    if (speed < band->fade_in) {
        *volume = motor_map(speed, 0.0f, 1.0f, band->speed_min, band->fade_in);
    } else if (speed <= band->fade_out) {
        *volume = 1.0f;
    } else {
        *volume = motor_map(speed, 0.0f, 1.0f, band->speed_max, band->fade_out);
    }
    sound->gain = (float)((double)tt->motor_volume * tt->motor_global_volume * *volume);
    if (!motor_sound_playing(sound)) {
        eng_sound_play(b, sound, sound->loop, 0.0f);
    }
}

static void motor_sounds_stop(blk_t *b)
{
    for (int i = 1; i <= 3; i++) {
        eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_get_input_object(b, i));
        if (sound != NULL) {
            eng_sound_stop(sound, 0.0f);
        }
    }
}

/*
 * TT_MotorSound: three looping sounds an engine's Speed plays through, each
 * in its band of the motor settings, its pitch rising over the band and its
 * volume (Volume x GlobalVolume x its fade) fading at the ends; out of its
 * band a sound stops. The first frame after the script is activated starts
 * all three, silent. Start Playing each frame; Stop stops them (End
 * Playing), as does the script's deactivation.
 */
static int bb_motor_sound(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        motor_sounds_stop(b);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    eng_tt_t *tt = eng_tt(engine);
    bool first = get_local_int(b, 0, 0) != 0;
    float speed = get_in_float(b, 0, 0.0f);
    nmo_engine_block_get_input_value(b, 4, &tt->motor_global_volume, sizeof(float));
    eng_sound_t *sounds[3];
    for (int i = 0; i < 3; i++) {
        sounds[i] = eng_sound(engine, nmo_engine_block_get_input_object(b, i + 1));
        if (sounds[i] == NULL) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
    }
    if (first) {
        for (int i = 0; i < 3; i++) {
            sounds[i]->loop = true;
            sounds[i]->gain = 0.0f;
        }
        for (int i = 0; i < 3; i++) {
            eng_sound_play(b, sounds[i], true, 0.0f);
        }
        set_local_int(b, 0, 0);
    }
    float pitch[3] = {0.0f, 0.0f, 0.0f}, volume[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) {
        motor_sound_band(b, sounds[i], &tt->motor_bands[i], speed, &pitch[i], &volume[i]);
    }
    for (int i = 0; i < 3; i++) {
        set_out(b, 2 * i, &pitch[i], sizeof(float));
        set_out(b, 2 * i + 1, &volume[i], sizeof(float));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Activating the script makes the next frame the first; deactivating it stops the sounds. */
static int motor_sound_callback(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_ACTIVATESCRIPT) {
        set_local_int(b, 0, 1);
    } else if (message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        motor_sounds_stop(b);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * TT_FontCoordinatesToDataArray: a new array named ArrayName, in the scene,
 * with a float column of each of ustart, vstart, uwidth, uprewidth,
 * upostwidth and vwidth and a row for each character of Font but the last
 * (0 to 254), in FontCoordinates. A font not made gives no rows (the DLL
 * reads through a null font).
 */
static int bb_font_coordinates_to_data_array(blk_t *b)
{
    static const char *const columns[6] = {"ustart", "vstart", "uwidth",
                                           "uprewidth", "upostwidth", "vwidth"};
    nmo_engine_t *engine = b->engine;
    const char *name = input_string(b, 0);
    nmo_object_id_t array = eng_create_object(engine, NMO_CID_DATAARRAY, name != NULL ? name : "");
    if (array != 0) {
        for (int32_t c = 0; c < 6; c++) {
            nmo_engine_array_insert_column(engine, array, -1, CKARRAYTYPE_FLOAT, NMO_GUID(0u, 0u));
            nmo_engine_array_set_column_name(engine, array, c, columns[c]);
        }
        eng_add_to_level_scene(engine, array);
        float (*glyphs)[6] = (float (*)[6])malloc(256u * sizeof(*glyphs));
        if (glyphs != NULL && eng_font_coordinates(engine, get_in_int(b, 1, 0), glyphs)) {
            for (int32_t row = 0; row < 255; row++) {
                nmo_engine_array_insert_row(engine, array, -1, NULL);
                for (int32_t c = 0; c < 6; c++) {
                    nmo_engine_cell_t cell = {.type = CKARRAYTYPE_FLOAT};
                    memcpy(&cell.value, &glyphs[row][c], sizeof(float));
                    nmo_engine_array_set_cell(engine, array, row, c, &cell);
                }
            }
        }
        free(glyphs);
    }
    set_out(b, 0, &array, sizeof(array));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * TT Switch on Layer: each frame, the square of the Layer under the target
 * (on the preferred grid, the target's position taken in Ref's space): In
 * while it holds Value and the target was inside, Enter when it comes in,
 * Exit (with the move since the last frame) when it leaves, Out while it is
 * elsewhere. In clears Was Inside the way TT PushButton's On does, only its
 * low byte, so the first frame reads as inside. Off stops it.
 */
static int bb_switch_on_layer(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_vector_t position = eng_entity_position(engine, entity, -1);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        nmo_engine_block_set_local_value(b, 0, &position, sizeof(position));
        set_local_int(b, 1, 1);
    }
    int32_t wanted = get_in_int(b, 2, 255);
    int32_t ref = input_entity(b, 0);
    int32_t type = get_in_int(b, 1, 1);
    eng_grid_t *grid = eng_grid_preferred(engine, &position, ref);
    eng_layer_t *layer = grid != NULL ? eng_grid_layer(engine, grid, type) : NULL;
    if (layer != NULL) {
        eng_vector_t local = eng_grid_local(engine, grid, &position, ref);
        int32_t value = eng_layer_value(grid, layer, (int32_t)local.x, (int32_t)local.z);
        bool was_inside = get_local_int(b, 1, 0) != 0;
        if (value == wanted) {
            if (was_inside) {
                OUT_ON(b, 0);
            } else {
                set_local_int(b, 1, 1);
                OUT_ON(b, 2);
            }
        } else if (was_inside) {
            set_local_int(b, 1, 0);
            eng_vector_t last = {0.0f, 0.0f, 0.0f};
            nmo_engine_block_get_local_value(b, 0, &last, sizeof(last));
            const eng_vector_t exit_vector = {position.x - last.x, position.y - last.y, position.z - last.z};
            set_out(b, 0, &exit_vector, sizeof(exit_vector));
            OUT_ON(b, 3);
        } else {
            OUT_ON(b, 1);
        }
    }
    nmo_engine_block_set_local_value(b, 0, &position, sizeof(position));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Files: through the host's file system (nmo_engine_set_file_system). None
 * of these blocks deactivates its input, as in the DLL.
 * ============================================================================ */

static const char *file_name_input(blk_t *b, int pos)
{
    const char *name = input_string(b, pos);
    return name != NULL ? name : "";
}

/* TT_CopyFile: CopyFile(Source, Destination, fail if exists = !Overwrite); OK or Fail. */
static int bb_copy_file(blk_t *b)
{
    const nmo_engine_file_system_t *fs = &b->engine->host.files;
    bool overwrite = get_in_int(b, 2, 0) != 0;
    bool done = fs->copy != NULL &&
                fs->copy(fs->user_data, file_name_input(b, 0), file_name_input(b, 1), overwrite);
    OUT_ON(b, done ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

typedef bool (*file_operation_fn)(void *user_data, const char *name);

static int file_operation(blk_t *b, file_operation_fn operation)
{
    const nmo_engine_file_system_t *fs = &b->engine->host.files;
    bool done = operation != NULL && operation(fs->user_data, file_name_input(b, 0));
    OUT_ON(b, done ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* TT_CreateDirectory: CreateDirectory(Directory); OK or Fail. */
static int bb_create_directory(blk_t *b)
{
    return file_operation(b, b->engine->host.files.create_directory);
}

/* TT_DeleteFile: DeleteFile(File); OK or Fail. */
static int bb_delete_file(blk_t *b)
{
    return file_operation(b, b->engine->host.files.remove_file);
}

/* TT_RemoveDirectory: RemoveDirectory(Directory); OK or Fail. */
static int bb_remove_directory(blk_t *b)
{
    return file_operation(b, b->engine->host.files.remove_directory);
}

/* TT_SetCurrentDirectory: SetCurrentDirectory(CurrentDirectory); OK or Fail. */
static int bb_set_current_directory(blk_t *b)
{
    return file_operation(b, b->engine->host.files.set_current_directory);
}

/* TT_ExistFile: Yes when there is a file (or directory) of that name; without the host's check,
   when the resolver gives a path that opens. */
static int bb_exist_file(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    const char *name = file_name_input(b, 0);
    bool exists;
    if (engine->host.files.exists != NULL) {
        exists = engine->host.files.exists(engine->host.files.user_data, name);
    } else {
        const char *path = engine->host.resolve_file != NULL
                               ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
                               : NULL;
        FILE *file = path != NULL ? nmo_platform_fopen(path, "rb") : NULL;
        exists = file != NULL;
        if (file != NULL) {
            fclose(file);
        }
    }
    OUT_ON(b, exists ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* TT_GetFileNameFromPath: what follows the last backslash (slash with Use Slash), or all of it. */
static int bb_get_file_name_from_path(blk_t *b)
{
    const char *path = file_name_input(b, 0);
    const char *last = strrchr(path, get_local_int(b, 0, 0) == 1 ? '/' : '\\');
    const char *name = last != NULL ? last + 1 : path;
    set_out(b, 0, name, strlen(name) + 1u);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* CKDataArray::SetElementStringValue: a string, or an int or float read from it; false for
   other columns, or a key column that has the value in another row */
static bool set_element_string(nmo_engine_t *engine, nmo_object_id_t array, int32_t row, int32_t column,
                               const char *text)
{
    if (column < 0 || column >= nmo_engine_array_column_count(engine, array) ||
        row >= nmo_engine_array_row_count(engine, array)) {
        return false;
    }
    nmo_engine_cell_t cell = {.type = nmo_engine_array_column_type(engine, array, column)};
    if (cell.type == CKARRAYTYPE_INT) {
        int32_t value = atoi(text);
        memcpy(&cell.value, &value, sizeof(value));
    } else if (cell.type == CKARRAYTYPE_FLOAT) {
        float value = (float)atof(text);
        memcpy(&cell.value, &value, sizeof(value));
    } else if (cell.type == CKARRAYTYPE_STRING) {
        cell.text = text;
    } else {
        return false;
    }
    if (column == nmo_engine_array_key_column(engine, array)) {
        int32_t found = nmo_engine_array_find_row(engine, array, column, &cell, 0);
        if ((found == row && nmo_engine_array_find_row(engine, array, column, &cell, row + 1) >= 0) ||
            (found >= 0 && found != row)) {
            return false;
        }
    }
    return nmo_engine_array_set_cell(engine, array, row, column, &cell) == NMO_OK;
}

typedef struct list_dir {
    nmo_engine_t *engine;
    nmo_object_id_t array;
} list_dir_t;

/* A row at the end with the entry's name, taken back when the first column cannot hold it */
static void list_dir_found(void *context, const char *name)
{
    list_dir_t *list = (list_dir_t *)context;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || list->array == 0) {
        return;
    }
    nmo_engine_array_insert_row(list->engine, list->array, -1, NULL);
    int32_t row = nmo_engine_array_row_count(list->engine, list->array) - 1;
    if (row >= 0 && !set_element_string(list->engine, list->array, row, 0, name)) {
        nmo_engine_array_remove_row(list->engine, list->array, row);
    }
}

/*
 * TT_ListDir: a row of Array for each entry SearchPath finds but "." and
 * "..", its name in the first column. The DLL activates no output, and
 * returns -1 ("Invalid File Handle") when the search cannot start, which
 * leaves it active.
 */
static int bb_list_dir(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    list_dir_t list = {engine, nmo_engine_block_get_input_object(b, 1)};
    if (list.array != 0 && eng_find_array(engine, list.array) == NULL) {
        list.array = 0;
    }
    const nmo_engine_file_system_t *fs = &engine->host.files;
    if (fs->list == NULL || !fs->list(fs->user_data, file_name_input(b, 0), list_dir_found, &list)) {
        return -1;
    }
    return NMO_ENGINE_BR_OK;
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
    {NMO_GUID_INIT(0x50eb3a17, 0x015c5bf9), "TT GetCurvePoint", bb_get_curve_point, NULL, NULL},
    {NMO_GUID_INIT(0x04db6e14, 0x2c151f73), "TT GetNearestCurvePosition", bb_get_nearest_curve_position, NULL, NULL},
    {NMO_GUID_INIT(0x32a6308d, 0x1d3d0931), "TT Bounding Sphere Intersection", bb_bounding_sphere_intersection, NULL,
     NULL},
    {NMO_GUID_INIT(0x7e9a4415, 0x199e4eb3), "TT CheckCurveCollision", bb_check_curve_collision, NULL, NULL},
    {NMO_GUID_INIT(0x44d43591, 0x28f91084), "TT Homing Missile", bb_homing_missile, NULL, NULL},
    {NMO_GUID_INIT(0x595b4723, 0x576c6749), "TT FollowCamera", bb_follow_camera, follow_camera_callback, NULL},
    {NMO_GUID_INIT(0x5ae72f89, 0x57eb5413), "TT_GeneratePlane", bb_generate_plane, NULL, NULL},
    {NMO_GUID_INIT(0x2e3c2d7e, 0x7be7234e), "TT_HighMapMeshform", bb_high_map_meshform, NULL, NULL},
    {NMO_GUID_INIT(0x08e53929, 0x4c9c5205), "TT MagnetMeshForm", bb_magnet_mesh_form, magnet_mesh_form_callback, NULL},
    {NMO_GUID_INIT(0x4f907f12, 0x16af15d6), "TT SinusMeshdeform", bb_sinus_meshdeform, sinus_meshdeform_callback, NULL},
    {NMO_GUID_INIT(0x38b87d43, 0x149b755b), "TT PMS_Meshdeform", bb_pms_meshdeform, pms_meshdeform_callback, NULL},
    {NMO_GUID_INIT(0x2f6a47b3, 0x35f4755c), "TT_RippleWave", bb_ripple_wave, ripple_wave_callback, NULL},
    {NMO_GUID_INIT(0x2e36319f, 0x6ec6031f), "TT_SetMapping", bb_set_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x53024810, 0x22ed280f), "TT_RefractionMapping", bb_refraction_mapping, NULL, NULL},
    {NMO_GUID_INIT(0x630d05f7, 0x1dda541f), "TT_BumpMap", bb_bump_map, NULL, NULL},
    {NMO_GUID_INIT(0x2d2d2a64, 0x5a0501c7), "TT_CelShading", bb_cel_shading, NULL, NULL},
    {NMO_GUID_INIT(0x73b30b5e, 0x4fed4c65), "TT_AnisoMapping", bb_aniso_mapping, aniso_mapping_callback, NULL},
    {NMO_GUID_INIT(0x6b6d781e, 0x4b531860), "TT_ListAllCurrentUsedTextures", bb_list_all_current_used_textures, NULL, NULL},
    {NMO_GUID_INIT(0x5ca62d31, 0x5d2031b8), "TT_SaveAlpha", bb_save_alpha, NULL, NULL},
    {NMO_GUID_INIT(0x62963fa9, 0x7258745c), "TT_Set Material Values", bb_set_material_values, NULL, NULL},
    {NMO_GUID_INIT(0x30bd6156, 0x14772834), "TT_SetFSAA", bb_set_fsaa, NULL, NULL},
    {NMO_GUID_INIT(0x412e4cd0, 0x6f860de3), "TT_ViewFrustumClipping", bb_view_frustum_clipping, view_frustum_clipping_callback, NULL},
    {NMO_GUID_INIT(0x173e60ff, 0x4db14bcc), "TT_BitmapFont", bb_bitmap_font, NULL, NULL},
    {NMO_GUID_INIT(0x2c7d5826, 0x688778f4), "TT SkyAround", bb_sky_around, sky_around_callback, NULL},
    {NMO_GUID_INIT(0x137c3d66, 0x3ff1752d), "TT ShadowPlane", bb_shadow_plane, NULL, NULL},
    {NMO_GUID_INIT(0x334c7dfe, 0x47c81a31), "TT ShadowMapping", bb_shadow_mapping, shadow_mapping_callback, NULL},
    {NMO_GUID_INIT(0x241b32a5, 0x71fe3357), "TT_LensFlare", bb_lens_flare, lens_flare_callback, NULL},
    {NMO_GUID_INIT(0x4dcf4ca0, 0x474e25ec), "TT_SceneStencilShadow", bb_scene_stencil_shadow, NULL, NULL},
    {NMO_GUID_INIT(0x4df00dac, 0x562c27da), "TT PushButton", bb_push_button, NULL, NULL},
    {NMO_GUID_INIT(0x1a1a67aa, 0x664108ed), "TT_MotorSound", bb_motor_sound, motor_sound_callback, NULL},
    {NMO_GUID_INIT(0x0f822838, 0x74116583), "TT_LoadMotorSettings", bb_load_motor_settings, NULL, NULL},
    {NMO_GUID_INIT(0x56680b62, 0x23083054), "TT Set_2DSprite_old", bb_set_2d_sprite_old, NULL, NULL},
    {NMO_GUID_INIT(0x408e30b5, 0x22a07ada), "TT Set_3DSprite", bb_set_3d_sprite, NULL, NULL},
    {NMO_GUID_INIT(0x4f16689a, 0x472b1c63), "TT GetUV2DSprite", bb_get_uv_2d_sprite, NULL, NULL},
    {NMO_GUID_INIT(0x73e256e5, 0x6552118e), "TT_FontCoordinatesToDataArray", bb_font_coordinates_to_data_array,
     NULL, NULL},
    {NMO_GUID_INIT(0x1a151160, 0x1be26257), "TT Switch on Layer", bb_switch_on_layer, NULL, NULL},
    {NMO_GUID_INIT(0x22e510f9, 0x070f528a), "TT_CopyFile", bb_copy_file, NULL, NULL},
    {NMO_GUID_INIT(0x2d2a0d16, 0x6b955ee1), "TT_SetCurrentDirectory", bb_set_current_directory, NULL, NULL},
    {NMO_GUID_INIT(0x03442842, 0x01df7a5f), "TT_CreateDirectory", bb_create_directory, NULL, NULL},
    {NMO_GUID_INIT(0x53ba46bc, 0x31782f60), "TT_ExistFile", bb_exist_file, NULL, NULL},
    {NMO_GUID_INIT(0x562f6aab, 0x07ea0f0b), "TT_DeleteFile", bb_delete_file, NULL, NULL},
    {NMO_GUID_INIT(0x7a2105fa, 0x282d2a22), "TT_RemoveDirectory", bb_remove_directory, NULL, NULL},
    {NMO_GUID_INIT(0x496f429e, 0x7d602e9d), "TT_ListDir", bb_list_dir, NULL, NULL},
    {NMO_GUID_INIT(0x68e631f7, 0x13f914fe), "TT_GetFileNameFromPath", bb_get_file_name_from_path, NULL, NULL},
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
    status = eng_add_manager(engine, &toolbox_render, NULL);
    if (status != NMO_OK) {
        return status;
    }
    return ENG_REGISTER_BLOCKS(engine, eng_tt_toolbox_blocks);
}
