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

#include "../../engine_block_util.h"
#include "../../engine_tt.h"
#include "type/nmo_type_query.h"
#include "type/nmo_object_guids.h"
#include "object/builtin/nmo_3dentity_schemas.h"

#include "object/nmo_class_ids.h"

#include <math.h>
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

/* TT_SetMipMapping: the target material's texture mipmapped and its filter modes set (an effect). */
static int bb_set_mip_mapping(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
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

static const nmo_engine_block_desc_t eng_tt_toolbox_blocks[] = {
    {NMO_GUID(0x09b335b3, 0x12d17cdc), "TT_LinearVolume", bb_linear_volume, NULL, NULL},
    {NMO_GUID(0x7e212b2f, 0x24db67c6), "TT_ReflectionMapping", bb_reflection_mapping, NULL, NULL},
    {NMO_GUID(0x416b4f0e, 0x192d339a), "TT_PreloadTextures", bb_preload_textures, NULL, NULL},
    {NMO_GUID(0x4a446c43, 0x66fa2375), "TT_Debug", bb_tt_debug, NULL, NULL},
    {NMO_GUID(0x3d00718f, 0x2c8b33a2), "TT_DebugON", bb_debug_on, NULL, NULL},
    {NMO_GUID(0x4c6513e9, 0x20e11177), "TT GetEnvironmentVariable", bb_get_environment_variable,
     NULL, NULL},
    {NMO_GUID(0x556e6df3, 0x24207d37), "TT_GetCurrentDirectory", bb_get_current_directory, NULL,
     NULL},
    {NMO_GUID(0x524a6bcb, 0x66f67774), "TT_ReplacePath", bb_replace_path, NULL, NULL},
    {NMO_GUID(0x38c34dab, 0x5dd03bdf), "TT_SetMipMapping", bb_set_mip_mapping, NULL, NULL},
    {NMO_GUID(0x7c663b40, 0x2f4b347e), "TT_TextureInfo", bb_texture_info, NULL, NULL},
    {NMO_GUID(0x3c392558, 0x419d2680), "TT Set_2DSprite", bb_set_2d_sprite, NULL, NULL},
    {NMO_GUID(0x4c94621d, 0x24fe2cf3), "TT OperationSystem", bb_operation_system, NULL, NULL},
    {NMO_GUID(0x3b826e04, 0x6e764285), "TT GetMemoryStatus", bb_get_memory_status, NULL, NULL},
    {NMO_GUID(0x6ac67901, 0x7d2a6059), "TT_Timer", bb_tt_timer, NULL, NULL},
    {NMO_GUID(0x5ae74e2f, 0x799c49b5), "TT_SplitString", bb_split_string, NULL, NULL},
    {NMO_GUID(0x14d325d1, 0x6748654e), "TT PushButton2", bb_push_button2, NULL, NULL},
    {NMO_GUID(0x750f20fc, 0x3741186d), "TT_Counter", bb_tt_counter, NULL, NULL},
    {NMO_GUID(0x18f96977, 0x18e20f83), "TT ConvertPixel-Homogen", bb_convert_pixel_homogen, NULL,
     NULL},
    {NMO_GUID(0x2a2a63ca, 0x00826175), "TT Scaleable Proximity", bb_scaleable_proximity, NULL,
     NULL},
    {NMO_GUID(0x0fd4755f, 0x7de22dc8), "TT Set Dynamic Position", bb_set_dynamic_position, NULL,
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
