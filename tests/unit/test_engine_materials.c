/**
 * @file test_engine_materials.c
 * @brief Materials.dll blocks run in graphs: what they leave on the
 *        materials, meshes and textures the engine keeps.
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "core/nmo_arena.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/nmo_ref.h"

#include <math.h>

#define GUID_SET_AMBIENT NMO_GUID(0xaaaaaaaa, 0xaeae1287)
#define GUID_SET_SPECULAR NMO_GUID(0x20202020, 0x30303030)
#define GUID_SET_SPECULAR_POWER NMO_GUID(0x45454545, 0x15151515)
#define GUID_SET_BOTH_SIDED NMO_GUID(0x76319845, 0x48593399)
#define GUID_SET_FILL_MODE NMO_GUID(0xc5c4c3c2, 0x8c8c8c8c)
#define GUID_SET_SHADE_MODE NMO_GUID(0x95195195, 0x98795198)
#define GUID_SET_SRC_BLEND NMO_GUID(0x40234ddc, 0x012da700)
#define GUID_SET_DEST_BLEND NMO_GUID(0x036d1a87, 0xdf0a670c)
#define GUID_SET_TEXTURE_MAG NMO_GUID(0xda495631, 0xde155488)
#define GUID_SET_TEXTURE_MIN NMO_GUID(0x12589321, 0x32587123)
#define GUID_SET_COLOR_KEY NMO_GUID(0x41bb5af1, 0x01eb26e9)
#define GUID_SET_MATERIAL NMO_GUID(0x37865d50, 0x2e285fab)
#define GUID_SET_LIGHTING_MODE NMO_GUID(0x45899665, 0x45211254)
#define GUID_SET_WRAP_MODE NMO_GUID(0x12a5498d, 0x65657512)
#define GUID_SET_TRANSPARENT NMO_GUID(0x2fc37564, 0x299a2c9d)
#define GUID_ADD_CHANNEL NMO_GUID(0x89658965, 0x36654789)
#define GUID_ACTIVATE_CHANNEL NMO_GUID(0x55566666, 0x66666444)
#define GUID_SET_CHANNEL_SRC_BLEND NMO_GUID(0x3b9b0e7f, 0xb1316800)
#define GUID_SET_CHANNEL_MATERIAL NMO_GUID(0x2e0a6b92, 0x366a3f9b)
#define GUID_REMOVE_CHANNEL NMO_GUID(0x4a462e58, 0x022e369b)
#define GUID_CYLINDRICAL_MAPPING NMO_GUID(0x770a5fc9, 0x7bbe2aed)
#define GUID_SPHERICAL_MAPPING NMO_GUID(0x5e40778e, 0x44f36cf3)
#define GUID_PLANAR_MAPPING NMO_GUID(0x2a6a3fa3, 0x3280407d)
#define GUID_COPY_MAPPING NMO_GUID(0x086a487c, 0x3cd44acb)
#define GUID_TEXTURE_SINE NMO_GUID(0x4fe0646e, 0x7e0635c9)
#define GUID_CHANGE_TEXTURE_SIZE NMO_GUID(0x7a7c4d7b, 0x468d0f33)
#define GUID_WRITE_IN_TEXTURE NMO_GUID(0x1d57024d, 0x7c064238)
#define GUID_SET_MIPMAP_TEXTURE NMO_GUID(0x6e407fcf, 0x0eff06a5)
#define GUID_ENVIRONMENT_MAPPING NMO_GUID(0x10d1c997, 0x99dabef4)
#define GUID_SCREEN_MAPPING NMO_GUID(0x58ef12ca, 0x26805940)
#define GUID_SET_PRELIT_COLOR NMO_GUID(0x60415d44, 0x0d0174ea)

#define STEP_MS 10.0f

static void input_float(engine_fixture_t *f, nmo_object_id_t pin, float value)
{
    fixture_input_value(f, pin, &value, sizeof(value));
}

/* A block acting on `object`, run `delay` frames after the script starts (0: at once) */
static nmo_object_id_t block_on(engine_fixture_t *f, nmo_guid_t guid, const char *name,
                                nmo_object_id_t object, nmo_object_id_t after, uint32_t delay)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    nmo_object_id_t target = id != 0 ? fixture_target(f, id) : 0;
    if (target == 0) {
        return 0;
    }
    fixture_input_int(f, target, (int32_t)object);
    fixture_link(f, after != 0 ? BLOCK_OUT(f, after, 0) : f->script_in, BLOCK_IN(f, id, 0), delay);
    return id;
}

static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

static void start(engine_fixture_t *f)
{
    fixture_commit(f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f->engine, NMO_ENGINE_START_ALL));
}

static void step(engine_fixture_t *f, int frames)
{
    for (int i = 0; i < frames; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, STEP_MS));
    }
}

/* An entity's world matrix the identity */
static bool place_at_origin(engine_fixture_t *f, nmo_object_id_t id)
{
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, id);
    if (entity == NULL) {
        return false;
    }
    memset(entity->world_matrix, 0, sizeof(entity->world_matrix));
    entity->world_matrix[0] = entity->world_matrix[5] = entity->world_matrix[10] = 1.0f;
    entity->world_matrix[15] = 1.0f;
    entity->has_entityndata_chunk = 1;
    return true;
}

/*
 * A 3D object at the origin whose mesh is a unit quad in the XY plane (UVs
 * its X and Y) of two faces: the first in group 0 (no material), the second
 * in group 1 (`material`).
 */
static nmo_object_id_t quad_object(engine_fixture_t *f, nmo_object_id_t material, nmo_object_id_t *out_mesh)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_object_id_t object = fixture_create(f, NMO_CID_3DOBJECT, "Quad");
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, "Quad Mesh");
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)fixture_state(f, object);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(f, mesh);
    if (entity == NULL || state == NULL) {
        return 0;
    }
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, 4u * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, 2u * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, 6u * sizeof(uint16_t), _Alignof(uint16_t));
    state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(arena, 2u * sizeof(nmo_material_group_t),
                                                                      _Alignof(nmo_material_group_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL ||
        state->material_groups == NULL) {
        return 0;
    }
    memset(state->vertices, 0, 4u * sizeof(nmo_vertex_t));
    for (int i = 0; i < 4; i++) {
        state->vertices[i].position = (nmo_vector_t){(float)(i & 1), (float)(i >> 1), 0.0f};
        state->vertices[i].uv = (nmo_vector2_t){(float)(i & 1), (float)(i >> 1)};
    }
    static const uint16_t indices[6] = {0, 1, 2, 1, 3, 2};
    memcpy(state->face_vertex_indices, indices, sizeof(indices));
    memset(state->faces, 0, 2u * sizeof(nmo_face_t));
    state->faces[1].material_group_idx = 1;
    memset(state->material_groups, 0, 2u * sizeof(nmo_material_group_t));
    state->material_groups[1].material = nmo_ref_from_id(material);
    state->vertex_count = 4;
    state->face_count = 2;
    state->material_group_count = 2;
    state->has_material_groups = 1;
    entity->current_mesh = nmo_ref_from_id(mesh);
    *out_mesh = mesh;
    return place_at_origin(f, object) ? object : 0;
}

/* The UV of vertex `v` of a mesh's channel -1 */
static nmo_vector2_t uv_of(engine_fixture_t *f, nmo_object_id_t mesh, int v)
{
    nmo_engine_mesh_t desc;
    if (!nmo_engine_mesh(f->engine, mesh, &desc) || v >= desc.vertex_count) {
        return (nmo_vector2_t){-100.0f, -100.0f};
    }
    return desc.vertices[v].uv;
}

TEST(engine_materials, setters_change_the_material_the_engine_keeps)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    ASSERT_TRUE(material != 0);
    nmo_object_id_t ambient = block_on(&f, GUID_SET_AMBIENT, "Set Ambient", material, 0, 0);
    nmo_object_id_t specular = block_on(&f, GUID_SET_SPECULAR, "Set Specular", material, 0, 0);
    nmo_object_id_t sided = block_on(&f, GUID_SET_BOTH_SIDED, "Set Both Sided", material, 0, 0);
    nmo_object_id_t fill = block_on(&f, GUID_SET_FILL_MODE, "Set Fill Mode", material, 0, 0);
    nmo_object_id_t shade = block_on(&f, GUID_SET_SHADE_MODE, "Set Shade Mode", material, 0, 0);
    nmo_object_id_t src = block_on(&f, GUID_SET_SRC_BLEND, "Set Src Blend", material, 0, 0);
    nmo_object_id_t dest = block_on(&f, GUID_SET_DEST_BLEND, "Set Dest Blend", material, 0, 0);
    nmo_object_id_t mag = block_on(&f, GUID_SET_TEXTURE_MAG, "Set Texture Mag", material, 0, 0);
    nmo_object_id_t min = block_on(&f, GUID_SET_TEXTURE_MIN, "Set Texture Min", material, 0, 0);
    /* A frame later the obsolete power setter overrides Set Specular's */
    nmo_object_id_t power = block_on(&f, GUID_SET_SPECULAR_POWER, "Set Specular Power", material, specular, 1);
    ASSERT_TRUE(ambient != 0 && specular != 0 && sided != 0 && fill != 0 && shade != 0 && src != 0 &&
                dest != 0 && mag != 0 && min != 0 && power != 0);
    const float ambient_color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const float specular_color[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, ambient, 0), ambient_color, sizeof(ambient_color));
    fixture_input_value(&f, BLOCK_PIN(&f, specular, 0), specular_color, sizeof(specular_color));
    input_float(&f, BLOCK_PIN(&f, specular, 1), 8.0f);
    fixture_input_int(&f, BLOCK_PIN(&f, sided, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, fill, 0), VXFILL_POINT);
    fixture_input_int(&f, BLOCK_PIN(&f, shade, 0), VXSHADE_GOURAUD);
    fixture_input_int(&f, BLOCK_PIN(&f, src, 0), VXBLEND_SRCALPHA);
    fixture_input_int(&f, BLOCK_PIN(&f, dest, 0), VXBLEND_INVSRCALPHA);
    fixture_input_int(&f, BLOCK_PIN(&f, mag, 0), VXTEXTUREFILTER_NEAREST);
    fixture_input_int(&f, BLOCK_PIN(&f, min, 0), VXTEXTUREFILTER_MIPLINEAR);
    input_float(&f, BLOCK_PIN(&f, power, 0), 2.0f);
    start(&f);

    step(&f, 1);
    nmo_engine_material_t desc;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_FLOAT_EQ(0.5f, desc.ambient[1], 0.0f);
    ASSERT_FLOAT_EQ(0.5f, desc.specular[1], 0.0f);
    ASSERT_FLOAT_EQ(8.0f, desc.power, 0.0f);
    ASSERT_TRUE(desc.two_sided);
    ASSERT_EQ(VXFILL_POINT, desc.fill_mode);
    ASSERT_EQ(VXSHADE_GOURAUD, desc.shade_mode);
    ASSERT_EQ(VXBLEND_SRCALPHA, desc.source_blend);
    ASSERT_EQ(VXBLEND_INVSRCALPHA, desc.dest_blend);
    ASSERT_EQ(VXTEXTUREFILTER_NEAREST, desc.mag_filter);
    ASSERT_EQ(VXTEXTUREFILTER_MIPLINEAR, desc.min_filter);

    step(&f, 2);
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &desc));
    ASSERT_FLOAT_EQ(2.0f, desc.power, 0.0f);
    fixture_end(&f);
}

TEST(engine_materials, set_material_replaces_then_applies_and_mesh_flags_follow)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t first = fixture_create(&f, NMO_CID_MATERIAL, "First");
    nmo_object_id_t second = fixture_create(&f, NMO_CID_MATERIAL, "Second");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t quad = quad_object(&f, first, &mesh);
    ASSERT_TRUE(first != 0 && second != 0 && quad != 0);

    /* Frame 1: Second replaces First in group 1; frame 2: First on every face, in a new group */
    nmo_object_id_t replace = block_on(&f, GUID_SET_MATERIAL, "Set Material", quad, 0, 0);
    nmo_object_id_t apply = block_on(&f, GUID_SET_MATERIAL, "Set Material", quad, replace, 1);
    nmo_object_id_t wrap = block_on(&f, GUID_SET_WRAP_MODE, "Set Wrap Mode", quad, 0, 0);
    nmo_object_id_t lit = block_on(&f, GUID_SET_LIGHTING_MODE, "Set Lighting Mode", quad, 0, 0);
    nmo_object_id_t transparent = fixture_block(&f, GUID_SET_TRANSPARENT, "Set Transparent");
    ASSERT_TRUE(replace != 0 && apply != 0 && wrap != 0 && lit != 0 && transparent != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, replace, 0), (int32_t)second);
    fixture_input_int(&f, BLOCK_PIN(&f, replace, 1), (int32_t)first);
    fixture_input_int(&f, BLOCK_PIN(&f, apply, 0), (int32_t)first);
    fixture_input_int(&f, BLOCK_PIN(&f, wrap, 0), VXTEXTUREWRAP_V);
    fixture_input_int(&f, BLOCK_PIN(&f, lit, 0), VX_PRELITMESH);
    fixture_input_int(&f, fixture_target(&f, transparent), (int32_t)mesh);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, transparent, 0), 0);
    start(&f);

    step(&f, 1);
    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    ASSERT_EQ(2, desc.material_count);
    ASSERT_EQ(1u, desc.faces[1].material_group_idx);
    ASSERT_EQ(second, nmo_engine_mesh_material(f.engine, mesh, 1));
    ASSERT_TRUE(!desc.wrap_u && desc.wrap_v);
    ASSERT_TRUE(desc.prelit);
    const eng_mesh_copy_t *copy = eng_mesh_copy_find(f.engine, mesh);
    ASSERT_NOT_NULL(copy);
    ASSERT_TRUE((copy->flags & VXMESH_FORCETRANSPARENCY) != 0);

    step(&f, 2);
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    ASSERT_EQ(3, desc.material_count);
    ASSERT_EQ(2u, desc.faces[0].material_group_idx);
    ASSERT_EQ(2u, desc.faces[1].material_group_idx);
    ASSERT_EQ(first, nmo_engine_mesh_material(f.engine, mesh, 2));
    ASSERT_EQ(second, nmo_engine_mesh_material(f.engine, mesh, 1));
    fixture_end(&f);
}

TEST(engine_materials, channel_blocks_add_change_and_remove_a_channel)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t first = fixture_create(&f, NMO_CID_MATERIAL, "First");
    nmo_object_id_t second = fixture_create(&f, NMO_CID_MATERIAL, "Second");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t quad = quad_object(&f, first, &mesh);
    ASSERT_TRUE(first != 0 && second != 0 && quad != 0);

    nmo_object_id_t add = block_on(&f, GUID_ADD_CHANNEL, "Add Channel", mesh, 0, 0);
    nmo_object_id_t blend = block_on(&f, GUID_SET_CHANNEL_SRC_BLEND, "Set Channel Src Blend", mesh, add, 1);
    nmo_object_id_t off = block_on(&f, GUID_ACTIVATE_CHANNEL, "Activate Channel", mesh, add, 1);
    nmo_object_id_t swap = block_on(&f, GUID_SET_CHANNEL_MATERIAL, "Set Channel Material", mesh, add, 1);
    nmo_object_id_t remove = block_on(&f, GUID_REMOVE_CHANNEL, "Remove Channel", mesh, swap, 1);
    ASSERT_TRUE(add != 0 && blend != 0 && off != 0 && swap != 0 && remove != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, add, 0), (int32_t)first);
    fixture_input_int(&f, BLOCK_PIN(&f, blend, 1), VXBLEND_ONE);
    fixture_input_int(&f, BLOCK_PIN(&f, swap, 1), (int32_t)second);
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 1), (int32_t)second);
    start(&f);

    step(&f, 1);
    int32_t channel = -1;
    read_value(&f, BLOCK_POUT(&f, add, 0), &channel, sizeof(channel));
    ASSERT_EQ(0, channel);
    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);
    ASSERT_EQ(first, desc.channels[0].material);
    ASSERT_TRUE(desc.channels[0].active);
    ASSERT_NOT_NULL(desc.channels[0].uvs);
    ASSERT_FLOAT_EQ(1.0f, desc.channels[0].uvs[3 * 2 + 1], 0.0f);

    step(&f, 1);
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    ASSERT_EQ(1, desc.channel_count);
    ASSERT_EQ(VXBLEND_ONE, desc.channels[0].source_blend);
    ASSERT_TRUE(!desc.channels[0].active);
    ASSERT_EQ(second, desc.channels[0].material);

    step(&f, 1);
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    ASSERT_EQ(0, desc.channel_count);
    fixture_end(&f);
}

TEST(engine_materials, set_color_key_keys_the_texture)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Texture");
    ASSERT_TRUE(texture != 0);
    nmo_object_id_t key = block_on(&f, GUID_SET_COLOR_KEY, "Set Color Key", texture, 0, 0);
    nmo_object_id_t unkey = block_on(&f, GUID_SET_COLOR_KEY, "Set Color Key", texture, key, 1);
    ASSERT_TRUE(key != 0 && unkey != 0);
    const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, key, 0), magenta, sizeof(magenta));
    fixture_input_int(&f, BLOCK_PIN(&f, unkey, 1), 0);
    start(&f);

    step(&f, 1);
    ASSERT_TRUE(eng_texture_keyed(f.engine, texture));
    ASSERT_EQ(0xffff00ffu, eng_texture(f.engine, texture)->transparent_color);
    /* Without a color of its own, the input's default: clear black */
    step(&f, 2);
    ASSERT_TRUE(!eng_texture_keyed(f.engine, texture));
    ASSERT_EQ(0u, eng_texture(f.engine, texture)->transparent_color);
    fixture_end(&f);
}


TEST(engine_materials, mappings_project_the_vertices_into_uvs)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t planar_mesh = 0, round_mesh = 0, ball_mesh = 0, copied_mesh = 0;
    nmo_object_id_t planar_quad = quad_object(&f, 0, &planar_mesh);
    nmo_object_id_t round_quad = quad_object(&f, 0, &round_mesh);
    nmo_object_id_t ball_quad = quad_object(&f, 0, &ball_mesh);
    nmo_object_id_t copied_quad = quad_object(&f, 0, &copied_mesh);
    nmo_object_id_t referential = fixture_create(&f, NMO_CID_3DENTITY, "Referential");
    ASSERT_TRUE(planar_quad != 0 && round_quad != 0 && ball_quad != 0 && copied_quad != 0 &&
                place_at_origin(&f, referential));

    nmo_object_id_t planar = block_on(&f, GUID_PLANAR_MAPPING, "Planar Mapping", planar_quad, 0, 0);
    nmo_object_id_t round = block_on(&f, GUID_CYLINDRICAL_MAPPING, "Cylindrical Mapping", round_quad, 0, 0);
    nmo_object_id_t ball = block_on(&f, GUID_SPHERICAL_MAPPING, "Spherical Mapping", ball_quad, 0, 0);
    nmo_object_id_t copy = block_on(&f, GUID_COPY_MAPPING, "Copy Mapping", copied_mesh, 0, 0);
    ASSERT_TRUE(planar != 0 && round != 0 && ball != 0 && copy != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, round, 1), (int32_t)referential);
    fixture_input_int(&f, BLOCK_PIN(&f, copy, 1), (int32_t)planar_mesh);
    const float tile[2] = {2.0f, 4.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, copy, 3), tile, sizeof(tile));
    start(&f);
    step(&f, 1);

    /* Planar: U = x / 2 + 0.5, V = 0.5 - y / 2 */
    nmo_vector2_t uv = uv_of(&f, planar_mesh, 3);
    ASSERT_FLOAT_EQ(1.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, uv.y, 1e-6f);
    uv = uv_of(&f, planar_mesh, 0);
    ASSERT_FLOAT_EQ(0.5f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.5f, uv.y, 1e-6f);
    /* Cylindrical: U the angle about Y from +X, V = 0.5 - y / 2 */
    uv = uv_of(&f, round_mesh, 1);
    ASSERT_FLOAT_EQ(0.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.5f, uv.y, 1e-6f);
    uv = uv_of(&f, round_mesh, 2);
    ASSERT_FLOAT_EQ(0.25f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, uv.y, 1e-6f);
    /* Spherical: (1, 1, 0) is 45 degrees up on +X */
    uv = uv_of(&f, ball_mesh, 3);
    ASSERT_FLOAT_EQ(0.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.25f, uv.y, 1e-6f);
    /* Copy Mapping, which runs after Planar Mapping: the planar mesh's new UVs, tiled */
    uv = uv_of(&f, copied_mesh, 3);
    ASSERT_FLOAT_EQ(2.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, uv.y, 1e-6f);
    fixture_end(&f);
}

TEST(engine_materials, texture_sine_sways_the_owner_mesh_uvs)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = 0;
    ASSERT_TRUE(quad_object(&f, 0, &mesh) != 0);
    f.owner_id = mesh; /* the script is the mesh's */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    ASSERT_TRUE(f.script_id != 0);
    fixture_begin_edit(&f);
    nmo_object_id_t sine = fixture_block(&f, GUID_TEXTURE_SINE, "Texture Sine");
    ASSERT_TRUE(sine != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sine, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sine, 0), BLOCK_IN(&f, sine, 0), 1);
    input_float(&f, BLOCK_PIN(&f, sine, 0), 1.0f);
    input_float(&f, BLOCK_PIN(&f, sine, 1), 1.0f);
    start(&f);

    /* First frame, time 0: vertex 0 at angle -2 */
    step(&f, 1);
    nmo_vector2_t uv = uv_of(&f, mesh, 0);
    ASSERT_FLOAT_EQ(cosf(-2.0f) * 0.5f, uv.x, 1e-5f);
    ASSERT_FLOAT_EQ(sinf(-2.0f) * 0.5f, uv.y, 1e-5f);
    /* Then about the saved UVs, not the swayed ones: time 0.01 s */
    step(&f, 1);
    uv = uv_of(&f, mesh, 3);
    float angle = (3.0f / 4.0f - 0.5f) * 4.0f + 0.01f;
    ASSERT_FLOAT_EQ(cosf(angle) * -0.5f + 1.0f, uv.x, 1e-5f);
    ASSERT_FLOAT_EQ(sinf(angle) * -0.5f + 1.0f, uv.y, 1e-5f);
    fixture_end(&f);
}

/* Change Texture Size on `texture`, run `delay` frames after `after` (the script start for 0) */
static nmo_object_id_t size_texture(engine_fixture_t *f, nmo_object_id_t texture, float w, float h, bool clear,
                                    nmo_object_id_t after, uint32_t delay)
{
    nmo_object_id_t id = block_on(f, GUID_CHANGE_TEXTURE_SIZE, "Change Texture Size", texture, after, delay);
    if (id != 0) {
        const float size[2] = {w, h};
        fixture_input_value(f, BLOCK_PIN(f, id, 0), size, sizeof(size));
        fixture_input_int(f, BLOCK_PIN(f, id, 1), clear ? 1 : 0);
    }
    return id;
}

/* Write In Texture of one pixel of `color` at `u`, `v` */
static nmo_object_id_t write_texture(engine_fixture_t *f, nmo_object_id_t texture, float u, float v,
                                     const float color[4], nmo_object_id_t after)
{
    nmo_object_id_t id = block_on(f, GUID_WRITE_IN_TEXTURE, "Write In Texture", texture, after, 1);
    if (id != 0) {
        const float uv[2] = {u, v};
        fixture_input_value(f, BLOCK_PIN(f, id, 0), uv, sizeof(uv));
        fixture_input_value(f, BLOCK_PIN(f, id, 1), color, 4u * sizeof(float));
    }
    return id;
}

static const uint8_t *pixel_of(const nmo_engine_image_t *image, int x, int y)
{
    return &image->rgba[((size_t)y * (size_t)image->width + (size_t)x) * 4u];
}

TEST(engine_materials, texture_blocks_create_write_scale_and_set_levels)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t texture = fixture_create(&f, NMO_CID_TEXTURE, "Texture");
    nmo_object_id_t half = fixture_create(&f, NMO_CID_TEXTURE, "Half");
    ASSERT_TRUE(texture != 0 && half != 0);
    /* Frame 1: 3x3 rounds up to 4x4 and Half is 2x2; frame 2: a red and a green pixel */
    nmo_object_id_t create = size_texture(&f, texture, 3.0f, 3.0f, true, 0, 0);
    nmo_object_id_t create_half = size_texture(&f, half, 2.0f, 2.0f, true, 0, 0);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f}, green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    nmo_object_id_t write = write_texture(&f, texture, 0.5f, 0.5f, red, create);
    nmo_object_id_t write_half = write_texture(&f, half, 0.0f, 0.0f, green, create_half);
    /* Frame 3: Half is the texture's first user level; frame 4: the texture scaled to 8x8 */
    nmo_object_id_t levels = fixture_block(&f, GUID_SET_MIPMAP_TEXTURE, "Set MipMap Texture");
    ASSERT_TRUE(create != 0 && create_half != 0 && write != 0 && write_half != 0 && levels != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, levels, 0), (int32_t)texture);
    fixture_input_int(&f, BLOCK_PIN(&f, levels, 1), (int32_t)half);
    fixture_link(&f, BLOCK_OUT(&f, write, 0), BLOCK_IN(&f, levels, 0), 1);
    nmo_object_id_t scale = size_texture(&f, texture, 8.0f, 8.0f, false, levels, 1);
    ASSERT_TRUE(scale != 0);
    start(&f);

    step(&f, 2);
    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_EQ(4, image.width);
    ASSERT_EQ(4, image.height);
    ASSERT_EQ(255, pixel_of(&image, 2, 2)[0]);
    ASSERT_EQ(0, pixel_of(&image, 2, 2)[1]);
    ASSERT_EQ(0, pixel_of(&image, 1, 2)[0]);
    ASSERT_EQ(255, pixel_of(&image, 1, 2)[3]); /* opaque black elsewhere */

    step(&f, 1);
    ASSERT_EQ(2, nmo_engine_texture_user_mipmap_count(f.engine, texture));
    nmo_engine_image_t level;
    ASSERT_TRUE(nmo_engine_texture_user_mipmap(f.engine, texture, 1, &level));
    ASSERT_EQ(2, level.width);
    ASSERT_EQ(255, pixel_of(&level, 0, 0)[1]);
    ASSERT_TRUE(nmo_engine_texture_user_mipmap(f.engine, texture, 2, &level));
    ASSERT_EQ(1, level.width);
    ASSERT_EQ(0, pixel_of(&level, 0, 0)[3]); /* a level no input filled stays clear */

    /* Growing mixes neighbours: (4, 4) is the red pixel, (5, 4) half way to black */
    step(&f, 1);
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, texture, &image));
    ASSERT_EQ(8, image.width);
    ASSERT_EQ(255, pixel_of(&image, 4, 4)[0]);
    ASSERT_EQ(127, pixel_of(&image, 5, 4)[0]);
    ASSERT_EQ(255, pixel_of(&image, 5, 4)[3]);
    fixture_end(&f);
}

TEST(engine_materials, environment_mapping_maps_the_normals_at_the_next_render)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t mesh = 0;
    nmo_object_id_t quad = quad_object(&f, 0, &mesh);
    nmo_object_id_t referential = fixture_create(&f, NMO_CID_3DENTITY, "Referential");
    nmo_3dentity_state_t *ref = (nmo_3dentity_state_t *)fixture_state(&f, referential);
    ASSERT_TRUE(quad != 0 && ref != 0);
    /* The referential's X axis is the world's Z */
    const float turned[16] = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1};
    memcpy(ref->world_matrix, turned, sizeof(turned));
    ref->has_entityndata_chunk = 1;
    nmo_object_id_t map = block_on(&f, GUID_ENVIRONMENT_MAPPING, "Environment Mapping", quad, 0, 0);
    ASSERT_TRUE(map != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, map, 1), 4); /* Use Ref Orientation */
    fixture_input_int(&f, BLOCK_PIN(&f, map, 2), (int32_t)referential);
    start(&f);
    eng_show(f.engine, quad, 1);
    eng_show(f.engine, mesh, 1);

    step(&f, 1);
    nmo_vector2_t before = uv_of(&f, mesh, 3);
    ASSERT_FLOAT_EQ(1.0f, before.x, 0.0f); /* nothing until the render */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    const nmo_vector_t *n = &desc.vertices[3].normal;
    ASSERT_FLOAT_EQ(1.0f, fabsf(n->z), 1e-6f);
    /* U along the referential's X, V against its Y */
    ASSERT_FLOAT_EQ(0.45f * n->z + 0.5f, desc.vertices[3].uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(0.5f, desc.vertices[3].uv.y, 1e-6f);
    fixture_end(&f);
}

TEST(engine_materials, screen_mapping_projects_through_the_camera)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t mesh = 0;
    nmo_object_id_t quad = quad_object(&f, 0, &mesh);
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    ASSERT_TRUE(quad != 0 && lens != NULL && place_at_origin(&f, camera));
    lens->projection_type = 2; /* orthographic: no division by depth */
    lens->orthographic_zoom = 1.0f;
    lens->aspect_width = lens->aspect_height = 1;
    lens->near_plane = 1.0f;
    lens->far_plane = 100.0f;
    nmo_object_id_t screen = block_on(&f, GUID_SCREEN_MAPPING, "Screen Mapping", quad, 0, 0);
    ASSERT_TRUE(screen != 0);
    start(&f);
    f.engine->render.camera = camera;

    step(&f, 1);
    /* U = x / 2 - 0.5, V = -y / 2 - 0.5 in the default Screen UVs */
    nmo_vector2_t uv = uv_of(&f, mesh, 3);
    ASSERT_FLOAT_EQ(0.0f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(-1.0f, uv.y, 1e-6f);
    uv = uv_of(&f, mesh, 0);
    ASSERT_FLOAT_EQ(-0.5f, uv.x, 1e-6f);
    ASSERT_FLOAT_EQ(-0.5f, uv.y, 1e-6f);
    fixture_end(&f);
}

/* With Disable Perspective Correction the quad draws itself at the screen with UVs where its
 * vertices fall */
TEST(engine_materials, screen_mapping_without_perspective_draws_at_the_screen)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Screen");
    nmo_object_id_t mesh = 0;
    nmo_object_id_t quad = quad_object(&f, material, &mesh);
    nmo_mesh_state_t *state = (nmo_mesh_state_t *)fixture_state(&f, mesh);
    nmo_object_id_t camera = fixture_create(&f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *lens = (nmo_camera_state_t *)fixture_state(&f, camera);
    ASSERT_TRUE(material != 0 && quad != 0 && state != NULL && lens != NULL && place_at_origin(&f, camera));
    ((nmo_3dentity_state_t *)fixture_state(&f, quad))->world_matrix[14] = 5.0f; /* in front of the camera */
    /* one material group */
    state->material_groups[0].material = nmo_ref_from_id(material);
    state->faces[1].material_group_idx = 0;
    state->material_group_count = 1;
    lens->projection_type = 2;
    lens->orthographic_zoom = 1.0f;
    lens->aspect_width = lens->aspect_height = 1;
    lens->near_plane = 1.0f;
    lens->far_plane = 100.0f;
    nmo_object_id_t screen = block_on(&f, GUID_SCREEN_MAPPING, "Screen Mapping", quad, 0, 0);
    ASSERT_TRUE(screen != 0);
    engine_fixture_value_t *setting = &f.values[f.value_count++];
    setting->param = local_of(&f, screen, 0); /* Disable Perspective Correction */
    const int32_t on = 1;
    memcpy(setting->bytes, &on, sizeof(on));
    setting->size = sizeof(on);
    start(&f);
    f.engine->render.camera = camera;
    eng_show(f.engine, quad, 1);
    eng_show(f.engine, mesh, 1);

    step(&f, 1);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = NULL;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        ASSERT_TRUE(frame.draws[i].kind != NMO_ENGINE_DRAW_MESH || frame.draws[i].object != quad);
        if (frame.draws[i].kind == NMO_ENGINE_DRAW_PRIMITIVE && frame.draws[i].object == quad) {
            draw = &frame.draws[i];
        }
    }
    ASSERT_NOT_NULL(draw);
    ASSERT_TRUE(draw->screen);
    ASSERT_EQ(material, draw->material);
    ASSERT_EQ(6, draw->vertex_count);
    int32_t viewport[4];
    eng_render_viewport(f.engine, viewport);
    const eng_vector_t corner = {1.0f, 1.0f, 5.0f};
    eng_vector_t at;
    eng_render_project(f.engine, &corner, &at);
    const nmo_engine_vertex_t *v = &frame.vertices[draw->first_vertex + 4]; /* vertex 3 */
    ASSERT_FLOAT_EQ(at.x, v->x, 1e-4f);
    ASSERT_FLOAT_EQ(at.y, v->y, 1e-4f);
    ASSERT_EQ(0xFFFFFFFFu, v->color);
    ASSERT_FLOAT_EQ((at.x - (float)viewport[0]) / (float)viewport[2], v->u, 1e-5f);
    ASSERT_FLOAT_EQ((at.y - (float)viewport[1]) / (float)viewport[3], v->v, 1e-5f);
    nmo_vector2_t uv = uv_of(&f, mesh, 3);
    ASSERT_FLOAT_EQ(1.0f, uv.x, 0.0f); /* the mesh's own UVs stay */
    fixture_end(&f);
}

/* Set Prelit Color: a mesh's vertices take the Filtering Color and the Additional Color as their
 * specular one; a 3D sprite's material takes them as its diffuse and specular */
TEST(engine_materials, set_prelit_color_sets_colors_and_speculars)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t mesh = 0;
    nmo_object_id_t quad = quad_object(&f, 0, &mesh);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Sprite Material");
    nmo_object_id_t sprite = fixture_create(&f, NMO_CID_SPRITE3D, "Sprite");
    nmo_sprite3d_state_t *state = (nmo_sprite3d_state_t *)fixture_state(&f, sprite);
    ASSERT_TRUE(quad != 0 && material != 0 && state != NULL);
    ((nmo_material_state_t *)fixture_state(&f, material))->has_material_data = 1;
    state->has_data = 1;
    state->material = nmo_ref_from_id(material);
    nmo_object_id_t on_mesh = block_on(&f, GUID_SET_PRELIT_COLOR, "Mesh", quad, 0, 0);
    nmo_object_id_t on_sprite = block_on(&f, GUID_SET_PRELIT_COLOR, "Sprite", sprite, 0, 0);
    ASSERT_TRUE(on_mesh != 0 && on_sprite != 0);
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f}, gray[4] = {0.5f, 0.5f, 0.5f, 0.0f};
    nmo_object_id_t blocks[2] = {on_mesh, on_sprite};
    for (int i = 0; i < 2; i++) {
        fixture_input_value(&f, BLOCK_PIN(&f, blocks[i], 0), red, sizeof(red));
        fixture_input_value(&f, BLOCK_PIN(&f, blocks[i], 1), gray, sizeof(gray));
    }
    start(&f);
    step(&f, 1);

    nmo_engine_mesh_t desc;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &desc));
    ASSERT_TRUE(desc.prelit);
    ASSERT_EQ(0xFFFF0000u, desc.colors[2]);
    ASSERT_NOT_NULL(desc.speculars);
    ASSERT_EQ(0x007F7F80u, desc.speculars[2]); /* RGBAFTOCOLOR masks red and green, rounds blue */
    nmo_engine_material_t sprite_material;
    ASSERT_TRUE(nmo_engine_material(f.engine, material, &sprite_material));
    ASSERT_FLOAT_EQ(1.0f, sprite_material.diffuse[0], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, sprite_material.diffuse[1], 1e-6f);
    ASSERT_FLOAT_EQ(127.0f / 255.0f, sprite_material.specular[0], 1e-6f);
    ASSERT_FLOAT_EQ(128.0f / 255.0f, sprite_material.specular[2], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, sprite_material.specular[3], 1e-6f);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_materials, setters_change_the_material_the_engine_keeps);
    REGISTER_TEST(engine_materials, set_material_replaces_then_applies_and_mesh_flags_follow);
    REGISTER_TEST(engine_materials, channel_blocks_add_change_and_remove_a_channel);
    REGISTER_TEST(engine_materials, set_color_key_keys_the_texture);
    REGISTER_TEST(engine_materials, mappings_project_the_vertices_into_uvs);
    REGISTER_TEST(engine_materials, texture_sine_sways_the_owner_mesh_uvs);
    REGISTER_TEST(engine_materials, texture_blocks_create_write_scale_and_set_levels);
    REGISTER_TEST(engine_materials, environment_mapping_maps_the_normals_at_the_next_render);
    REGISTER_TEST(engine_materials, screen_mapping_projects_through_the_camera);
    REGISTER_TEST(engine_materials, screen_mapping_without_perspective_draws_at_the_screen);
    REGISTER_TEST(engine_materials, set_prelit_color_sets_colors_and_speculars);
TEST_MAIN_END()
