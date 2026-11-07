/**
 * @file world_environments.c
 * @brief Ports of the World Environments building blocks that keep state.
 *
 * Each function does what the block's function in WorldEnvironments.dll does
 * (the reference is the Ballanced source of the blocks): it changes the
 * render context's background or fog.
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t color_argb(const float color[4], float alpha)
{
    uint32_t argb = 0;
    const float c[4] = {color[0], color[1], color[2], alpha};
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        argb |= (uint32_t)(v * 255.0f + 0.5f) << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

/* Set Background Color: the diffuse of the render context's background material */
static int bb_set_background_color(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    b->engine->render.background_color = color_argb(color, color[3]);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Fog: mode, start, end, density and color. Version 2 takes the
 * VXFOG_MODE; an older one (no locals) turns linear fog on or off and
 * scales the distances by 2.8.
 */
static int bb_set_fog(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    bool old_mode = nmo_engine_block_version(b) != 0x00020000 &&
                    nmo_engine_block_local_parameter_count(b) == 0;
    int32_t mode = get_in_int(b, 0, 0);
    float start = get_in_float(b, 1, 0.0f);
    float end = get_in_float(b, 2, 10.0f);
    if (old_mode) {
        mode = mode != 0 ? 3 : 0; /* VXFOG_LINEAR */
        start *= 2.8f;
        end *= 2.8f;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 4, color, sizeof(color));
    eng_render_t *render = &b->engine->render;
    render->fog_mode = mode;
    render->fog_start = start;
    render->fog_end = end;
    render->fog_density = get_in_float(b, 3, 1.0f);
    render->fog_color = color_argb(color, 1.0f);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/* RGBAFTOCOLOR(r, g, b, a): each channel scaled to its place and rounded (half to even), masked */
static uint32_t rgba_to_color(const float c[4])
{
    uint32_t r = (uint32_t)(int64_t)rint((double)c[0] * 16711680.0);
    uint32_t g = (uint32_t)(int64_t)rint((double)c[1] * 65280.0);
    uint32_t bl = (uint32_t)(int64_t)rint((double)c[2] * 255.0);
    uint32_t a = (uint32_t)(int64_t)rint((double)c[3] * 255.0);
    return (g & 0xFF00u) | (r & 0xFF0000u) | (bl & 0xFFu) | (a << 24);
}

/* Set Background Image: the texture of the render context's background material */
static int bb_set_background_image(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    b->engine->render.background_texture = nmo_engine_block_get_input_object(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Background Material: the background material takes every member of
 * the material; what a background shows of it is its diffuse color and
 * texture.
 */
static int bb_set_background_material(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_material_t desc;
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    if (material != 0 && nmo_engine_material(b->engine, material, &desc)) {
        b->engine->render.background_color = color_argb(desc.diffuse, desc.diffuse[3]);
        b->engine->render.background_texture = desc.texture;
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Set Ambient Light Color: CKRenderContext::SetAmbientLight with the color's red, green and blue */
static int bb_set_ambient_light_color(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    color[3] = 1.0f;
    b->engine->render.ambient_light = rgba_to_color(color);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Sky Around, Clouds Around and Sky Around (CubeMap)
 * ============================================================================ */

typedef enum { SKY_AROUND, CLOUDS_AROUND, SKY_CUBEMAP } sky_kind_t;

/*
 * A sky block's cube: the mesh, the 3D entity CK makes for it (here the
 * draw the render manager makes), its materials, and what the block last
 * set. The cube is drawn at the camera with a field of view widened by
 * Distortion, near 1 and far 200, without the depth buffer: before the 3D
 * entities (Sky Around), or after them, blended (Clouds Around).
 */
typedef struct sky_cube {
    int32_t node;
    sky_kind_t kind;
    nmo_object_id_t mesh;
    nmo_object_id_t materials[6];
    float distortion;
    float scale;              /* the CubeMap's Distance */
    nmo_object_id_t orientation; /* Clouds Around's Orientation Object */
    bool on;                  /* the block's own flag: On shows it each frame */
    bool shown;
} sky_cube_t;

typedef struct sky_render {
    ENG_VEC(sky_cube_t) cubes;
} sky_render_t;

static void sky_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                       const eng_render_args_t *args);
static void sky_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    free(((sky_render_t *)state)->cubes.data);
}

static const eng_manager_desc_t sky_manager = {
    .name = "Sky Around",
    .state_size = sizeof(sky_render_t),
    .destroy = sky_destroy,
    .render = sky_render,
};

static sky_cube_t *sky_cube(nmo_engine_t *engine, int32_t node)
{
    sky_render_t *state = (sky_render_t *)eng_manager_state(engine, &sky_manager);
    for (size_t i = 0; state != NULL && i < state->cubes.count; i++) {
        if (state->cubes.data[i].node == node) {
            return &state->cubes.data[i];
        }
    }
    return NULL;
}

/* The cube of side 20 CK builds: 24 vertices (a face's four each) or 8, twelve faces */
static eng_mesh_t *sky_make_mesh(nmo_engine_t *engine, bool cubemap)
{
    static const float faces24[24][3] = {
        {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1},  {1, -1, -1}, {1, -1, -1}, {1, 1, -1},
        {1, 1, 1},    {1, -1, 1},  {1, -1, 1},  {1, 1, 1},   {-1, 1, 1},  {-1, -1, 1},
        {-1, -1, 1},  {-1, 1, 1},  {-1, 1, -1}, {-1, -1, -1}, {1, 1, 1},  {1, 1, -1},
        {-1, 1, -1},  {-1, 1, 1},  {1, -1, -1}, {1, -1, 1},  {-1, -1, 1}, {-1, -1, -1}};
    static const float corners8[8][3] = {{-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {1, -1, -1},
                                         {-1, 1, 1},   {-1, -1, 1}, {1, 1, 1},  {1, -1, 1}};
    static const uint16_t quads24[12][3] = {{2, 1, 0},    {2, 0, 3},    {6, 5, 4},    {6, 4, 7},
                                            {10, 9, 8},   {10, 8, 11},  {14, 13, 12}, {14, 12, 15},
                                            {18, 17, 16}, {18, 16, 19}, {22, 21, 20}, {22, 20, 23}};
    static const uint16_t faces8[12][3] = {{2, 1, 0}, {2, 0, 3}, {6, 2, 3}, {6, 3, 7}, {4, 6, 7}, {4, 7, 5},
                                           {1, 4, 5}, {1, 5, 0}, {1, 2, 6}, {1, 6, 4}, {5, 7, 3}, {5, 3, 0}};
    static const float uvs[4][2] = {{1, 1}, {1, 0}, {0, 0}, {0, 1}};
    nmo_object_id_t id = eng_create_object(engine, NMO_CID_MESH, cubemap ? "SkyAroundCubeMap" : "SkyAround Cube");
    eng_mesh_t *mesh = NULL;
    if (id == 0 || !ENG_VEC_PUSH(engine->render.meshes, mesh)) {
        return NULL;
    }
    mesh->id = id;
    mesh->vertex_count = cubemap ? 8 : 24;
    mesh->face_count = 12;
    mesh->material_count = cubemap ? 1 : 6;
    mesh->vertices = (nmo_vertex_t *)calloc((size_t)mesh->vertex_count, sizeof(nmo_vertex_t));
    mesh->colors = (uint32_t *)calloc((size_t)mesh->vertex_count, sizeof(uint32_t));
    mesh->indices = (uint16_t *)calloc(36u, sizeof(uint16_t));
    mesh->faces = (nmo_face_t *)calloc(12u, sizeof(nmo_face_t));
    mesh->materials = (nmo_object_id_t *)calloc((size_t)mesh->material_count, sizeof(nmo_object_id_t));
    mesh->prelit = true; /* SetLitMode(VX_PRELITMESH) */
    if (mesh->vertices == NULL || mesh->colors == NULL || mesh->indices == NULL || mesh->faces == NULL ||
        mesh->materials == NULL) {
        return mesh;
    }
    for (int32_t v = 0; v < mesh->vertex_count; v++) {
        const float *p = cubemap ? corners8[v] : faces24[v];
        mesh->vertices[v].position = (nmo_vector_t){p[0] * 10.0f, p[1] * 10.0f, p[2] * 10.0f};
        if (!cubemap) {
            mesh->vertices[v].uv = (nmo_vector2_t){uvs[v % 4][0], uvs[v % 4][1]};
        }
        mesh->colors[v] = 0xFFFFFFFFu;
    }
    for (int32_t f = 0; f < 12; f++) {
        memcpy(&mesh->indices[f * 3], cubemap ? faces8[f] : quads24[f], 3u * sizeof(uint16_t));
        mesh->faces[f].material_group_idx = cubemap ? 0u : (uint32_t)(f / 2);
    }
    return mesh;
}

/*
 * The callbacks: the load makes the cube (and its six materials, their
 * textures clamped; Clouds Around's blended), hidden; Pause, Reset and the
 * script's deactivation hide it, its activation shows it.
 */
static int sky_callback(blk_t *b, uint32_t message, sky_kind_t kind)
{
    nmo_engine_t *engine = b->engine;
    if (message == NMO_ENGINE_MSG_LOAD) {
        sky_render_t *state = (sky_render_t *)eng_manager_state(engine, &sky_manager);
        sky_cube_t *cube = sky_cube(engine, b->node);
        if (cube == NULL) {
            if (state == NULL || !ENG_VEC_PUSH(state->cubes, cube)) {
                return NMO_ENGINE_BR_GENERICERROR;
            }
        }
        memset(cube, 0, sizeof(*cube));
        cube->node = b->node;
        cube->kind = kind;
        cube->scale = 1.0f;
        eng_mesh_t *mesh = sky_make_mesh(engine, kind == SKY_CUBEMAP);
        cube = sky_cube(engine, b->node);
        if (mesh == NULL || cube == NULL) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        cube->mesh = mesh->id;
        for (int i = 0; kind != SKY_CUBEMAP && i < 6; i++) {
            char name[24];
            memcpy(name, kind == CLOUDS_AROUND ? "Fog-Cube Material 0" : "Sky-Cube Material 0", 20);
            name[18] = (char)('0' + i);
            nmo_object_id_t id = eng_create_object(engine, NMO_CID_MATERIAL, name);
            eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
            if (material == NULL) {
                continue;
            }
            material->desc.texture = nmo_engine_block_get_input_object(b, i);
            material->desc.address_mode = VXTEXTURE_ADDRESSCLAMP;
            material->desc.alpha_blend = kind == CLOUDS_AROUND;
            cube = sky_cube(engine, b->node);
            cube->materials[i] = id;
            mesh = eng_render_mesh(engine, cube->mesh);
            if (mesh != NULL && mesh->materials != NULL) {
                mesh->materials[i] = id;
            }
        }
        return NMO_ENGINE_BR_OK;
    }
    sky_cube_t *cube = sky_cube(engine, b->node);
    if (cube == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_RESET ||
        message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        cube->shown = false;
    } else if (message == NMO_ENGINE_MSG_ACTIVATESCRIPT && kind != CLOUDS_AROUND) {
        cube->shown = true;
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_sky_around(blk_t *b, uint32_t message) { return sky_callback(b, message, SKY_AROUND); }

static int cb_clouds_around(blk_t *b, uint32_t message) { return sky_callback(b, message, CLOUDS_AROUND); }

static int cb_sky_around_cubemap(blk_t *b, uint32_t message) { return sky_callback(b, message, SKY_CUBEMAP); }

/* The cube's vertex colors: the Filtering Color (RGBAFTOCOLOR); the Additional Color is
   their specular one, which the engine's meshes do not keep */
static void sky_colors(blk_t *b, sky_cube_t *cube, int input)
{
    float color[4] = {1.0f, 1.0f, 1.0f, 0.5f};
    nmo_engine_block_get_input_value(b, input, color, sizeof(color));
    uint32_t argb = rgba_to_color(color);
    eng_mesh_t *mesh = eng_render_mesh(b->engine, cube->mesh);
    for (int32_t v = 0; mesh != NULL && mesh->colors != NULL && v < mesh->vertex_count; v++) {
        mesh->colors[v] = argb;
    }
}

/*
 * Sky Around (Clouds Around, Sky Around (CubeMap)): On gives the cube its
 * textures (its material), Off hides it; each frame while on it shows at
 * the camera in the Filtering Color. Clouds Around takes its textures and
 * blend factors every frame, and turns as its Orientation Object; the
 * CubeMap cube is Distance times bigger.
 */
static int sky_block(blk_t *b, sky_kind_t kind)
{
    nmo_engine_t *engine = b->engine;
    sky_cube_t *cube = sky_cube(engine, b->node);
    int distortion_input = kind == SKY_CUBEMAP ? 2 : 6;
    float distortion = get_in_float(b, distortion_input, kind == CLOUDS_AROUND ? 0.3f : 0.2f);
    if (cube == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    cube->distortion = !(distortion < 1.0f) ? 1.0f : distortion;
    if (kind == CLOUDS_AROUND) {
        cube->orientation = nmo_engine_block_get_input_object(b, 11);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        cube->shown = false;
        cube->on = false;
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        if (kind == SKY_AROUND) {
            for (int i = 0; i < 6; i++) {
                eng_material_t *material = eng_material(engine, cube->materials[i]);
                if (material != NULL) {
                    material->desc.texture = nmo_engine_block_get_input_object(b, i);
                }
            }
        } else if (kind == SKY_CUBEMAP) {
            eng_mesh_t *mesh = eng_render_mesh(engine, cube->mesh);
            if (mesh != NULL && mesh->materials != NULL) {
                mesh->materials[0] = nmo_engine_block_get_input_object(b, 0); /* ReplaceMaterial(NULL, it) */
            }
        }
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        cube->on = true;
    }
    if (cube->on) {
        cube->shown = true;
    }
    if (kind == CLOUDS_AROUND) {
        int32_t source = get_in_int(b, 9, VXBLEND_SRCALPHA), dest = get_in_int(b, 10, VXBLEND_INVSRCALPHA);
        for (int i = 0; i < 6; i++) {
            eng_material_t *material = eng_material(engine, cube->materials[i]);
            if (material != NULL) {
                material->desc.texture = nmo_engine_block_get_input_object(b, i);
                material->desc.source_blend = source;
                material->desc.dest_blend = dest;
            }
        }
    }
    sky_colors(b, cube, kind == SKY_CUBEMAP ? 3 : 7);
    if (kind == SKY_CUBEMAP) {
        cube->scale = get_in_float(b, 1, 1.0f);
    }
    if (engine->render.camera == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int bb_sky_around(blk_t *b) { return sky_block(b, SKY_AROUND); }

static int bb_clouds_around(blk_t *b) { return sky_block(b, CLOUDS_AROUND); }

static int bb_sky_around_cubemap(blk_t *b) { return sky_block(b, SKY_CUBEMAP); }

/* The cubes' pre render callbacks: at the camera, the projection widened, no depth buffer */
static void sky_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                       const eng_render_args_t *args)
{
    if (stage != ENG_RENDER_BACKGROUND && stage != ENG_RENDER_SCENE) {
        return;
    }
    sky_render_t *skies = (sky_render_t *)state;
    float fov = 0.5f;
    eng_camera_fov(engine, engine->render.camera, &fov);
    const float pi = 3.141592654f;
    for (size_t i = 0; i < skies->cubes.count; i++) {
        sky_cube_t cube = skies->cubes.data[i];
        bool last = cube.kind == CLOUDS_AROUND;
        if (!cube.shown || cube.mesh == 0 || last != (stage == ENG_RENDER_SCENE)) {
            continue;
        }
        nmo_engine_draw_t *draw = eng_render_push(engine);
        if (draw == NULL) {
            return;
        }
        draw->kind = NMO_ENGINE_DRAW_MESH;
        draw->mesh = cube.mesh;
        int32_t orientation = cube.orientation != 0 ? eng_entity_index(engine, cube.orientation) : -1;
        if (orientation >= 0) {
            /* The orientation's axes, made unit */
            const eng_matrix_t *m = &engine->entities.data[orientation].world;
            for (int r = 0; r < 3; r++) {
                float length = sqrtf(m->m[r][0] * m->m[r][0] + m->m[r][1] * m->m[r][1] + m->m[r][2] * m->m[r][2]);
                for (int c = 0; c < 3; c++) {
                    draw->world[r * 4 + c] = length > 0.0f ? m->m[r][c] / length : 0.0f;
                }
            }
            draw->world[15] = 1.0f;
        } else {
            draw->world[0] = draw->world[5] = draw->world[10] = cube.scale;
            draw->world[15] = 1.0f;
        }
        draw->world[12] = args->view[12];
        draw->world[13] = args->view[13];
        draw->world[14] = args->view[14];
        draw->no_z_test = draw->no_z_write = true;
        draw->transparent = last;
        draw->fov = fov + (pi - fov) * cube.distortion;
        draw->near_plane = 1.0f;
        draw->far_plane = 200.0f;
    }
}

static const nmo_engine_block_desc_t eng_world_environments_blocks[] = {
    {NMO_GUID_INIT(0xf5faaaaa, 0xfdd5bd00), "Set Background Color", bb_set_background_color, NULL, NULL},
    {NMO_GUID_INIT(0x151aaaaa, 0xffd5bdbc), "Set Fog", bb_set_fog, NULL, NULL},
    {NMO_GUID_INIT(0x1254d1ce, 0x001d0a58), "Set Background Image", bb_set_background_image, NULL, NULL},
    {NMO_GUID_INIT(0xddccefec, 0xd010c102), "Set Background Material", bb_set_background_material, NULL,
     NULL},
    {NMO_GUID_INIT(0x136b3f22, 0x0dc12786), "Set Ambient Light Color", bb_set_ambient_light_color, NULL,
     NULL},
    {NMO_GUID_INIT(0xed453298, 0x01ae789f), "Sky Around", bb_sky_around, cb_sky_around, NULL},
    {NMO_GUID_INIT(0x77c04509, 0x00a36a46), "Clouds Around", bb_clouds_around, cb_clouds_around, NULL},
    {NMO_GUID_INIT(0x01157f66, 0x0af04fd2), "Sky Around(CubeMap)", bb_sky_around_cubemap,
     cb_sky_around_cubemap, NULL},
};

nmo_status_t eng_register_world_environments(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &sky_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_world_environments_blocks) : status;
}
