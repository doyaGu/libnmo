#include "engine_fixture.h"
#include "core/nmo_arena.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_query.h"
#include "../../src/engine/engine_internal.h"
#include "../../src/engine/engine_transform.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define GUID_SELECT_MESH NMO_GUID(0x43890176, 0x58b2244e)
#define GUID_ADD_MESH NMO_GUID(0x7a1d593e, 0x12c5648d)
#define GUID_REMOVE_MESH NMO_GUID(0x0002145e, 0x6e1b2592)
#define GUID_CHANGE_REFERENTIAL NMO_GUID(0x5f6428f2, 0x04e55c7a)
#define GUID_INVERT_WINDING NMO_GUID(0x31ce61cd, 0x69a44beb)
#define GUID_SET_FACE_PROPERTIES NMO_GUID(0x1d8e2716, 0x64f47239)
#define GUID_SET_VERTEX_PROPERTIES NMO_GUID(0x696353b2, 0x40667087)
#define GUID_SINE_DEFORM NMO_GUID(0x45123704, 0x72f25769)
#define GUID_TAPER NMO_GUID(0x0e971a6b, 0x210f703a)
#define GUID_VERTEX_TRANSLATE NMO_GUID(0x08032ebc, 0x791960c9)
#define GUID_MESH_MORPHER NMO_GUID(0xff0d010a, 0x001d010a)
#define GUID_MULTI_MESH_MORPHER NMO_GUID(0x53da65c4, 0x6f0f194a)
#define GUID_SET_VERTEX_COUNT NMO_GUID(0x4bf4352e, 0x6c786db2)
#define GUID_SET_FACE_COUNT NMO_GUID(0x15a14196, 0x69530840)
#define GUID_SKIN_JOIN NMO_GUID(0x38677c72, 0x4f4715cb)
#define GUID_PRECOMPUTE_LIGHTING NMO_GUID(0x053a5be9, 0x78cc3e15)
#define GUID_GET_LOD_ATTRIBUTE NMO_GUID(0x62bf39fa, 0x53e264c6)

static nmo_object_id_t started_block(engine_fixture_t *f, nmo_guid_t guid, const char *name)
{
    nmo_object_id_t block = fixture_block(f, guid, name);
    if (block != 0) {
        fixture_link(f, f->script_in, BLOCK_IN(f, block, 0), 0);
    }
    return block;
}

/* The block's local parameter `pos`, settings included in declaration order */
static nmo_object_id_t local_of(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

/* A mesh of `side` x `side` vertices one apart in XZ (rows along Z) at height `y`, two faces a square */
static nmo_object_id_t grid_mesh(engine_fixture_t *f, const char *name, int side, float y)
{
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, name);
    nmo_mesh_state_t *state = mesh != 0 ? (nmo_mesh_state_t *)fixture_state(f, mesh) : NULL;
    if (state == NULL) {
        return 0;
    }
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    uint32_t count = (uint32_t)(side * side), faces = (uint32_t)(2 * (side - 1) * (side - 1));
    state->vertices = (nmo_vertex_t *)nmo_arena_alloc(arena, count * sizeof(nmo_vertex_t), _Alignof(nmo_vertex_t));
    state->faces = (nmo_face_t *)nmo_arena_alloc(arena, faces * sizeof(nmo_face_t), _Alignof(nmo_face_t));
    state->face_vertex_indices = (uint16_t *)nmo_arena_alloc(arena, faces * 3 * sizeof(uint16_t), _Alignof(uint16_t));
    state->vertex_colors = (uint32_t *)nmo_arena_alloc(arena, count * sizeof(uint32_t), _Alignof(uint32_t));
    state->vertex_specular = (uint32_t *)nmo_arena_alloc(arena, count * sizeof(uint32_t), _Alignof(uint32_t));
    if (state->vertices == NULL || state->faces == NULL || state->face_vertex_indices == NULL ||
        state->vertex_colors == NULL || state->vertex_specular == NULL) {
        return 0;
    }
    for (uint32_t v = 0; v < count; v++) {
        state->vertex_colors[v] = 0xFFFFFFFFu;
        state->vertex_specular[v] = 0u;
    }
    memset(state->vertices, 0, count * sizeof(nmo_vertex_t));
    memset(state->faces, 0, faces * sizeof(nmo_face_t));
    for (int r = 0; r < side; r++) {
        for (int c = 0; c < side; c++) {
            nmo_vertex_t *v = &state->vertices[r * side + c];
            v->position = (nmo_vector_t){(float)c, y, (float)r};
            v->normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
            v->uv = (nmo_vector2_t){(float)c / (float)(side - 1), (float)r / (float)(side - 1)};
        }
    }
    uint32_t k = 0;
    for (int r = 0; r + 1 < side; r++) {
        for (int c = 0; c + 1 < side; c++) {
            const uint16_t a = (uint16_t)(r * side + c), b = (uint16_t)(a + 1), d = (uint16_t)(a + side);
            const uint16_t e = (uint16_t)(d + 1);
            const uint16_t quad[6] = {a, d, b, b, d, e};
            memcpy(&state->face_vertex_indices[k * 3], quad, sizeof(quad));
            state->faces[k].normal = state->faces[k + 1].normal = (nmo_vector_t){0.0f, 1.0f, 0.0f};
            k += 2;
        }
    }
    /* one group, no material: a mesh with faces saves at least one */
    state->material_groups = (nmo_material_group_t *)nmo_arena_alloc(arena, sizeof(nmo_material_group_t),
                                                                      _Alignof(nmo_material_group_t));
    if (state->material_groups == NULL) {
        return 0;
    }
    memset(state->material_groups, 0, sizeof(nmo_material_group_t));
    state->material_group_count = 1;
    state->has_material_groups = 1;
    state->vertex_count = count;
    state->face_count = faces;
    nmo_mesh_update_bounding_volumes(state);
    return mesh;
}

/* A 3D object at (x, y, z) showing `mesh`, which is its one mesh */
static nmo_object_id_t object_with(engine_fixture_t *f, const char *name, nmo_object_id_t mesh, float x, float y,
                                   float z)
{
    nmo_object_id_t object = fixture_create(f, NMO_CID_3DOBJECT, name);
    nmo_3dentity_state_t *state = object != 0 ? (nmo_3dentity_state_t *)fixture_state(f, object) : NULL;
    if (state == NULL) {
        return 0;
    }
    state->world_matrix[12] = x;
    state->world_matrix[13] = y;
    state->world_matrix[14] = z;
    if (mesh != 0) {
        nmo_arena_t *arena = nmo_session_get_arena(f->session);
        state->current_mesh = nmo_ref_from_id(mesh);
        state->mesh_ids = (nmo_ref_t *)nmo_arena_alloc(arena, sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
        if (state->mesh_ids != NULL) {
            state->mesh_ids[0] = nmo_ref_from_id(mesh);
            state->mesh_count = 1;
        }
    }
    return object;
}

static nmo_vector_t vertex_of(engine_fixture_t *f, nmo_object_id_t mesh, int v)
{
    nmo_engine_mesh_t out;
    nmo_vector_t none = {NAN, NAN, NAN};
    return nmo_engine_mesh(f->engine, mesh, &out) && v < out.vertex_count ? out.vertices[v].position : none;
}

static nmo_object_id_t current_mesh(engine_fixture_t *f, nmo_object_id_t entity)
{
    nmo_object_t *object = eng_find_object(f->engine, entity);
    const nmo_3dentity_state_t *state = object != NULL
        ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(f->engine->types, object,
                                                                                          CKPGUID_3DENTITY)
        : NULL;
    return state != NULL ? eng_object_ref(f->engine, entity, &state->current_mesh) : 0;
}

TEST(engine_mesh_modifiers, entity_mesh_blocks_keep_its_meshes)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t first = grid_mesh(&f, "First", 2, 0.0f), second = grid_mesh(&f, "Second", 2, 0.0f);
    nmo_object_id_t object = object_with(&f, "Object", first, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(first != 0 && second != 0 && object != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t add = fixture_block(&f, GUID_ADD_MESH, "Add");
    nmo_object_id_t select = fixture_block(&f, GUID_SELECT_MESH, "Select");
    nmo_object_id_t remove = fixture_block(&f, GUID_REMOVE_MESH, "Remove");
    ASSERT_TRUE(add != 0 && select != 0 && remove != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, add, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, add, 0), BLOCK_IN(&f, select, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, select, 0), BLOCK_IN(&f, remove, 0), 1);
    const nmo_object_id_t blocks[3] = {add, select, remove};
    for (int i = 0; i < 3; i++) {
        fixture_input_int(&f, fixture_target(&f, blocks[i]), (int32_t)object);
        fixture_input_int(&f, BLOCK_PIN(&f, blocks[i], 0), (int32_t)second);
    }
    fixture_input_int(&f, BLOCK_PIN(&f, select, 1), 0); /* not added again */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ((int64_t)second, (int64_t)current_mesh(&f, object));
    /* Removing the current mesh makes the first one left current */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, remove));
    ASSERT_EQ((int64_t)first, (int64_t)current_mesh(&f, object));
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, change_referential_moves_the_vertices_into_the_referential)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Mesh", 2, 0.0f);
    nmo_object_id_t object = object_with(&f, "Object", mesh, 1.0f, 0.0f, 0.0f);
    nmo_object_id_t ref = object_with(&f, "Ref", 0, 0.0f, 0.0f, 5.0f);
    ASSERT_TRUE(mesh != 0 && object != 0 && ref != 0);
    ((nmo_3dentity_state_t *)fixture_state(&f, object))->moveable_flags = 0; /* not world aligned */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t change = started_block(&f, GUID_CHANGE_REFERENTIAL, "Change");
    ASSERT_TRUE(change != 0);
    fixture_input_int(&f, fixture_target(&f, change), (int32_t)object);
    fixture_input_int(&f, BLOCK_PIN(&f, change, 0), (int32_t)ref);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* The vertices stay where they were in the world: (1, 0, 0) is (1, 0, -5) from the referential */
    nmo_vector_t p = vertex_of(&f, mesh, 0);
    ASSERT_FLOAT_EQ(1.0f, p.x, 1e-6f);
    ASSERT_FLOAT_EQ(-5.0f, p.z, 1e-6f);
    int32_t index = eng_entity_index(f.engine, object);
    ASSERT_TRUE(index >= 0);
    ASSERT_FLOAT_EQ(0.0f, f.engine->entities.data[index].world.m[3][0], 1e-6f);
    ASSERT_FLOAT_EQ(5.0f, f.engine->entities.data[index].world.m[3][2], 1e-6f);
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, face_and_vertex_blocks_edit_the_target_mesh)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Mesh", 2, 0.0f);
    ASSERT_TRUE(mesh != 0);
    ((nmo_mesh_state_t *)fixture_state(&f, mesh))->flags |= VXMESH_PRELITMODE; /* colors, not normals */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t invert = fixture_block(&f, GUID_INVERT_WINDING, "Invert");
    nmo_object_id_t face = fixture_block(&f, GUID_SET_FACE_PROPERTIES, "Face");
    nmo_object_id_t vertex = fixture_block(&f, GUID_SET_VERTEX_PROPERTIES, "Vertex");
    ASSERT_TRUE(invert != 0 && face != 0 && vertex != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, invert, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, invert, 0), BLOCK_IN(&f, face, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, face, 0), BLOCK_IN(&f, vertex, 0), 0);
    fixture_input_int(&f, fixture_target(&f, invert), (int32_t)mesh);
    fixture_input_int(&f, fixture_target(&f, face), (int32_t)mesh);
    fixture_input_int(&f, fixture_target(&f, vertex), (int32_t)mesh);
    fixture_input_int(&f, BLOCK_PIN(&f, face, 0), 1);
    const float indices[3] = {3.0f, 2.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, face, 2), indices, sizeof(indices));
    fixture_input_int(&f, BLOCK_PIN(&f, vertex, 0), 3);
    const float position[3] = {9.0f, 8.0f, 7.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, vertex, 1), position, sizeof(position));
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f}, blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, vertex, 3), red, sizeof(red));
    fixture_input_value(&f, BLOCK_PIN(&f, vertex, 4), blue, sizeof(blue)); /* Vertex Specular Color */
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    /* face 0 was (0, 2, 1): its last two swap; face 1 takes the indices given */
    ASSERT_EQ(0, out.indices[0]);
    ASSERT_EQ(1, out.indices[1]);
    ASSERT_EQ(2, out.indices[2]);
    ASSERT_EQ(3, out.indices[3]);
    ASSERT_EQ(2, out.indices[4]);
    ASSERT_EQ(1, out.indices[5]);
    ASSERT_FLOAT_EQ(9.0f, out.vertices[3].position.x, 0.0f);
    ASSERT_FLOAT_EQ(7.0f, out.vertices[3].position.z, 0.0f);
    ASSERT_EQ(0xFFFF0000u, out.colors[3]);
    ASSERT_NOT_NULL(out.speculars);
    ASSERT_EQ(0xFF0000FFu, out.speculars[3]);
    ASSERT_EQ(0u, out.speculars[2]);
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, deformers_move_the_current_mesh_vertices)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t waved_mesh = grid_mesh(&f, "Waved", 3, 0.0f), tapered_mesh = grid_mesh(&f, "Tapered", 3, 0.0f);
    nmo_object_id_t moved_mesh = grid_mesh(&f, "Moved", 3, 0.0f);
    nmo_object_id_t waved = object_with(&f, "Waved", waved_mesh, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t tapered = object_with(&f, "Tapered", tapered_mesh, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t moved = object_with(&f, "Moved", moved_mesh, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(waved != 0 && tapered != 0 && moved != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t sine = started_block(&f, GUID_SINE_DEFORM, "Sine");
    nmo_object_id_t taper = started_block(&f, GUID_TAPER, "Taper");
    nmo_object_id_t translate = started_block(&f, GUID_VERTEX_TRANSLATE, "Translate");
    ASSERT_TRUE(sine != 0 && taper != 0 && translate != 0);
    fixture_input_int(&f, fixture_target(&f, sine), (int32_t)waved);
    fixture_input_int(&f, fixture_target(&f, taper), (int32_t)tapered);
    fixture_input_int(&f, fixture_target(&f, translate), (int32_t)moved);
    const float one = 1.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, taper, 0), &one, sizeof(one)); /* Amount */
    fixture_input_int(&f, BLOCK_PIN(&f, translate, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, translate, 1), 2);
    const float up[3] = {0.0f, 1.0f, 0.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, translate, 2), up, sizeof(up));
    fixture_input_int(&f, BLOCK_PIN(&f, translate, 3), (int32_t)moved); /* its own space */
    nmo_object_id_t box = local_of(&f, taper, 1), time = local_of(&f, sine, 0);
    ASSERT_TRUE(box != 0 && time != 0);
    fixture_commit(&f, NULL);
    /* the box the attach callback saved: Max, then Min */
    const float initial_box[6] = {2.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f};
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, box, initial_box, sizeof(initial_box)));
    const float start = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, time, &start, sizeof(start)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Sine Deform at time 0: x + x cos(x) on each axis */
    nmo_vector_t p = vertex_of(&f, waved_mesh, 8);
    ASSERT_FLOAT_EQ(2.0f + 2.0f * (float)cos(2.0), p.x, 1e-5f);
    ASSERT_FLOAT_EQ(2.0f + 2.0f * (float)cos(2.0), p.z, 1e-5f);
    float now = -1.0f;
    const void *data = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, time, &data, NULL, NULL));
    memcpy(&now, data, sizeof(now));
    ASSERT_FLOAT_EQ(0.02f, now, 1e-6f);
    /* Taper along Z over a length of 2: x times 1 - z/2 */
    ASSERT_FLOAT_EQ(0.0f, vertex_of(&f, tapered_mesh, 8).x, 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, vertex_of(&f, tapered_mesh, 5).x, 1e-6f);
    ASSERT_FLOAT_EQ(2.0f, vertex_of(&f, tapered_mesh, 2).x, 1e-6f);
    /* Vertex Translate: vertices 1 and 2 up by one */
    ASSERT_FLOAT_EQ(0.0f, vertex_of(&f, moved_mesh, 0).y, 0.0f);
    ASSERT_FLOAT_EQ(1.0f, vertex_of(&f, moved_mesh, 1).y, 0.0f);
    ASSERT_FLOAT_EQ(1.0f, vertex_of(&f, moved_mesh, 2).y, 0.0f);
    ASSERT_FLOAT_EQ(0.0f, vertex_of(&f, moved_mesh, 3).y, 0.0f);
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, multi_mesh_morpher_blends_meshes_into_the_current_one)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t low = grid_mesh(&f, "Low", 2, 0.0f), high = grid_mesh(&f, "High", 2, 2.0f);
    nmo_object_id_t blended = grid_mesh(&f, "Blended", 2, 9.0f);
    nmo_object_id_t mixed = object_with(&f, "Mixed", blended, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(low != 0 && high != 0 && blended != 0 && mixed != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t multi = started_block(&f, GUID_MULTI_MESH_MORPHER, "Multi");
    ASSERT_TRUE(multi != 0);
    const float one = 1.0f, three = 3.0f;
    fixture_input_int(&f, fixture_target(&f, multi), (int32_t)mixed);
    fixture_input_int(&f, BLOCK_PIN(&f, multi, 0), (int32_t)low);
    fixture_input_value(&f, BLOCK_PIN(&f, multi, 1), &one, sizeof(one));
    fixture_input_int(&f, BLOCK_PIN(&f, multi, 2), (int32_t)high);
    fixture_input_value(&f, BLOCK_PIN(&f, multi, 3), &three, sizeof(three));
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* (1 x 0 + 3 x 2) / 4 into the target's own mesh */
    ASSERT_EQ((int64_t)blended, (int64_t)current_mesh(&f, mixed));
    ASSERT_FLOAT_EQ(1.5f, vertex_of(&f, blended, 3).y, 1e-6f);
    fixture_end(&f);
}

static nmo_object_id_t saved_input(engine_fixture_t *f, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t value = 0;
    if (nmo_script_edit_ensure_input_parameter_source(f->tx, pin, &value) != NMO_OK ||
        nmo_script_edit_set_parameter_value(f->tx, value, text) != NMO_OK) {
        return 0;
    }
    return value;
}

static const char *resolve_saved(void *data, const char *name)
{
    (void)name;
    return (const char *)data;
}

/* Mesh Morpher makes its mesh in the target's document: a loaded file's (the fixture's own
   document takes no new objects) */
TEST(engine_mesh_modifiers, mesh_morpher_shows_a_blended_copy_of_the_start_mesh)
{
    engine_fixture_t source;
    fixture_begin_document(&source);
    nmo_object_id_t low = grid_mesh(&source, "Low", 2, 0.0f), high = grid_mesh(&source, "High", 2, 2.0f);
    nmo_object_id_t own = grid_mesh(&source, "Own", 2, 5.0f);
    nmo_object_id_t morphed = object_with(&source, "Morphed", own, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(low != 0 && high != 0 && own != 0 && morphed != 0);
    source.owner_id = morphed; /* the block is not targetable: it morphs its owner */
    source.script_id = fixture_add_script(&source, "Script", &source.script_in);
    fixture_begin_edit(&source);
    nmo_object_id_t morpher = started_block(&source, GUID_MESH_MORPHER, "Morpher");
    ASSERT_TRUE(morpher != 0);
    char text[32];
    snprintf(text, sizeof(text), "%u", (unsigned)low);
    ASSERT_TRUE(saved_input(&source, BLOCK_PIN(&source, morpher, 0), text) != 0);
    snprintf(text, sizeof(text), "%u", (unsigned)high);
    ASSERT_TRUE(saved_input(&source, BLOCK_PIN(&source, morpher, 1), text) != 0);
    nmo_object_id_t effect = saved_input(&source, BLOCK_PIN(&source, morpher, 2), "0");
    ASSERT_TRUE(effect != 0);
    fixture_commit_bare(&source, NULL);
    nmo_beobject_state_t *owner = fixture_state(&source, source.owner_id);
    owner->has_single_activity = 1;
    owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&source, source.script_id);
    script->has_single_activity = 1;
    script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *path = NMO_TEST_SCRATCH_FILE("engine_mesh_morpher.cmo");
    nmo_status_t saved = nmo_session_save_file(source.session, path, NULL, NULL);
    ASSERT_EQ(NMO_OK, saved);

    engine_fixture_t host;
    fixture_begin_document(&host);
    fixture_begin_edit(&host);
    fixture_commit(&host, NULL);
    nmo_engine_set_file_resolver(host.engine, resolve_saved, (void *)path);
    int32_t file = -1;
    ASSERT_EQ(NMO_OK, nmo_engine_load(host.engine, "morpher.cmo", true, &file));
    ASSERT_EQ(NMO_OK, nmo_engine_start(host.engine, NMO_ENGINE_START_SCENE_DEFAULT));
    nmo_object_id_t block = nmo_engine_object_named(host.engine, "Morpher", NMO_CID_BEHAVIOR);
    ASSERT_TRUE(block != 0);
    const eng_module_t *module = eng_module_of(host.engine, block, NULL);
    nmo_object_id_t loaded = eng_module_id(module, morphed), start = eng_module_id(module, low);
    nmo_object_id_t loaded_own = eng_module_id(module, own);
    const float quarter = 0.25f; /* a Percentage of 25 */
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(host.engine, eng_module_id(module, effect), &quarter, sizeof(quarter)));
    ASSERT_EQ(NMO_OK, nmo_engine_step(host.engine, 20.0f));
    ASSERT_EQ(1u, runs(&host, block));
    /* a copy of the start mesh a quarter of the way to the end one */
    nmo_object_id_t shown = current_mesh(&host, loaded);
    ASSERT_TRUE(shown != 0 && shown != loaded_own && shown != start);
    ASSERT_STR_EQ("Low_Morpher", nmo_engine_object_name(host.engine, shown));
    ASSERT_FLOAT_EQ(0.5f, vertex_of(&host, shown, 3).y, 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, vertex_of(&host, start, 3).y, 0.0f);
    fixture_end(&host);
    fixture_end(&source);
}

TEST(engine_mesh_modifiers, count_blocks_resize_the_target_mesh)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t cut = grid_mesh(&f, "Cut", 3, 0.0f), grown = grid_mesh(&f, "Grown", 2, 0.0f);
    ASSERT_TRUE(cut != 0 && grown != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t fewer = fixture_block(&f, GUID_SET_VERTEX_COUNT, "Fewer");
    nmo_object_id_t faces = fixture_block(&f, GUID_SET_FACE_COUNT, "Faces");
    nmo_object_id_t more = started_block(&f, GUID_SET_VERTEX_COUNT, "More");
    ASSERT_TRUE(fewer != 0 && faces != 0 && more != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, fewer, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, fewer, 0), BLOCK_IN(&f, faces, 0), 0);
    fixture_input_int(&f, fixture_target(&f, fewer), (int32_t)cut);
    fixture_input_int(&f, fixture_target(&f, faces), (int32_t)cut);
    fixture_input_int(&f, fixture_target(&f, more), (int32_t)grown);
    fixture_input_int(&f, BLOCK_PIN(&f, fewer, 0), 6);
    fixture_input_int(&f, BLOCK_PIN(&f, faces, 0), 6);
    fixture_input_int(&f, BLOCK_PIN(&f, more, 0), 7);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* Six vertices drop the four faces of the second row of squares; six faces add two of vertex 0 */
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, cut, &out));
    ASSERT_EQ(6, out.vertex_count);
    ASSERT_EQ(6, out.face_count);
    const uint16_t first[6] = {0, 3, 1, 1, 3, 4};
    for (int k = 0; k < 6; k++) {
        ASSERT_EQ(first[k], out.indices[k]);
    }
    for (int k = 12; k < 18; k++) {
        ASSERT_EQ(0, out.indices[k]);
    }
    ASSERT_FLOAT_EQ(2.0f, out.vertices[5].position.x, 0.0f);
    /* More vertices: the new ones zeroed and white */
    ASSERT_TRUE(nmo_engine_mesh(f.engine, grown, &out));
    ASSERT_EQ(7, out.vertex_count);
    ASSERT_EQ(2, out.face_count);
    ASSERT_FLOAT_EQ(1.0f, out.vertices[3].position.x, 0.0f);
    ASSERT_FLOAT_EQ(0.0f, out.vertices[6].position.x, 0.0f);
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, dissociating_faces_gives_each_corner_a_vertex)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Mesh", 3, 0.0f);
    ASSERT_TRUE(mesh != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_TRUE(eng_mesh_dissociate_faces(f.engine, mesh));
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    ASSERT_EQ(24, out.vertex_count);
    ASSERT_EQ(8, out.face_count);
    for (int k = 0; k < 24; k++) {
        ASSERT_EQ(k, out.indices[k]);
    }
    /* face 1 was (1, 3, 4) */
    ASSERT_FLOAT_EQ(1.0f, out.vertices[3].position.x, 0.0f);
    ASSERT_FLOAT_EQ(0.0f, out.vertices[4].position.x, 0.0f);
    ASSERT_FLOAT_EQ(1.0f, out.vertices[4].position.z, 0.0f);
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, skin_join_makes_the_owner_follow_the_joined_vertices)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t skin_mesh = grid_mesh(&f, "Skin", 2, 0.0f), bone_mesh = grid_mesh(&f, "Bone", 2, 0.0f);
    nmo_object_id_t skin = object_with(&f, "Skin", skin_mesh, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t bone = object_with(&f, "Bone", bone_mesh, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(skin != 0 && bone != 0);
    f.owner_id = skin; /* the block joins its owner */
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t join = started_block(&f, GUID_SKIN_JOIN, "Join");
    ASSERT_TRUE(join != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, join, 1), (int32_t)bone);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_notify_behavior_edited(f.engine, join, NULL));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    const float moved[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 5, 0, 1};
    ASSERT_EQ(NMO_OK, nmo_engine_set_entity_world_matrix(f.engine, bone, moved, false));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    /* Each skin vertex took the bone vertex it was on, where the bone is now */
    ASSERT_FLOAT_EQ(5.0f, vertex_of(&f, skin_mesh, 0).y, 1e-6f);
    ASSERT_FLOAT_EQ(1.0f, vertex_of(&f, skin_mesh, 3).x, 1e-6f);
    ASSERT_FLOAT_EQ(5.0f, vertex_of(&f, skin_mesh, 3).y, 1e-6f);

    /* The skin all outside the last frame and the bone not: the skin takes the bone's world box
       (SetBoundingBox), which its next join takes back */
    int32_t skin_at = eng_entity_index(f.engine, skin), bone_at = eng_entity_index(f.engine, bone);
    f.engine->entities.data[skin_at].frustum = ENG_FRUSTUM_OUTSIDE;
    f.engine->entities.data[bone_at].frustum = ENG_FRUSTUM_INSIDE;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    eng_vector_t low, high, bone_low, bone_high;
    ASSERT_TRUE(f.engine->entities.data[skin_at].user_box);
    ASSERT_TRUE(eng_entity_world_box(f.engine, skin_at, &low, &high));
    ASSERT_TRUE(eng_entity_world_box(f.engine, bone_at, &bone_low, &bone_high));
    ASSERT_FLOAT_EQ(bone_low.y, low.y, 1e-5f);
    ASSERT_FLOAT_EQ(bone_high.x, high.x, 1e-5f);
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_FALSE(f.engine->entities.data[skin_at].user_box);
    fixture_end(&f);
}

TEST(engine_mesh_modifiers, precompute_lighting_makes_the_mesh_prelit)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Mesh", 2, 0.0f);
    nmo_object_id_t lit = object_with(&f, "Lit", mesh, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t material = fixture_create(&f, NMO_CID_MATERIAL, "Material");
    ASSERT_TRUE(mesh != 0 && lit != 0 && material != 0);
    nmo_material_state_t *colors = (nmo_material_state_t *)fixture_state(&f, material);
    ASSERT_NOT_NULL(colors);
    colors->diffuse_color = 0xFFFFFFFFu;
    colors->ambient_color = 0xFF000000u;
    colors->specular_color = 0xFF000000u;
    colors->emissive_color = 0xFF336699u;
    ((nmo_mesh_state_t *)fixture_state(&f, mesh))->material_groups[0].material = nmo_ref_from_id(material);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t precompute = started_block(&f, GUID_PRECOMPUTE_LIGHTING, "Precompute");
    ASSERT_TRUE(precompute != 0);
    fixture_input_int(&f, fixture_target(&f, precompute), (int32_t)lit);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* No light: the emissive color, the diffuse alpha */
    nmo_engine_mesh_t out;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &out));
    ASSERT_TRUE(out.prelit);
    ASSERT_NOT_NULL(out.colors);
    uint32_t c = out.colors[2];
    ASSERT_EQ(0xFFu, (c >> 24) & 0xFFu);
    ASSERT_TRUE(abs((int)((c >> 16) & 0xFFu) - 0x33) <= 1);
    ASSERT_TRUE(abs((int)((c >> 8) & 0xFFu) - 0x66) <= 1);
    ASSERT_TRUE(abs((int)(c & 0xFFu) - 0x99) <= 1);
    fixture_end(&f);
}

/* (LOD Manager Options has no test: its "Modify Parameters" setting has a type the edit layer
   does not know, so it cannot make the block) */
TEST(engine_mesh_modifiers, get_lod_attribute_without_the_attribute_is_not_found)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = grid_mesh(&f, "Mesh", 2, 0.0f);
    nmo_object_id_t object = object_with(&f, "Object", mesh, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(object != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t get = started_block(&f, GUID_GET_LOD_ATTRIBUTE, "Get");
    nmo_object_id_t missing = fixture_block(&f, GUID_SET_VERTEX_COUNT, "Missing"); /* any block, run by Not Found */
    ASSERT_TRUE(get != 0 && missing != 0);
    fixture_link(&f, BLOCK_OUT(&f, get, 1), BLOCK_IN(&f, missing, 0), 0);
    fixture_input_int(&f, fixture_target(&f, get), (int32_t)object);
    fixture_input_int(&f, fixture_target(&f, missing), (int32_t)mesh);
    fixture_input_int(&f, BLOCK_PIN(&f, missing, 0), 4);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, missing));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_mesh_modifiers, entity_mesh_blocks_keep_its_meshes);
REGISTER_TEST(engine_mesh_modifiers, change_referential_moves_the_vertices_into_the_referential);
REGISTER_TEST(engine_mesh_modifiers, face_and_vertex_blocks_edit_the_target_mesh);
REGISTER_TEST(engine_mesh_modifiers, deformers_move_the_current_mesh_vertices);
REGISTER_TEST(engine_mesh_modifiers, multi_mesh_morpher_blends_meshes_into_the_current_one);
REGISTER_TEST(engine_mesh_modifiers, mesh_morpher_shows_a_blended_copy_of_the_start_mesh);
REGISTER_TEST(engine_mesh_modifiers, count_blocks_resize_the_target_mesh);
REGISTER_TEST(engine_mesh_modifiers, dissociating_faces_gives_each_corner_a_vertex);
REGISTER_TEST(engine_mesh_modifiers, skin_join_makes_the_owner_follow_the_joined_vertices);
REGISTER_TEST(engine_mesh_modifiers, precompute_lighting_makes_the_mesh_prelit);
REGISTER_TEST(engine_mesh_modifiers, get_lod_attribute_without_the_attribute_is_not_found);
TEST_MAIN_END()
