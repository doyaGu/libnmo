/**
 * @file mesh_modifiers.c
 * @brief Ports of the MeshModifiers building blocks and the LOD manager.
 *
 * Each function does what the block's function in MeshModifiers.dll does.
 * Ballance ships no MeshModifiers.dll, so the reference is the Ballanced
 * source of the DLL, with CK3dEntity and CKMesh as Ballanced's RenderEngine
 * has them. The deformers write the mesh's run time copy (eng_mesh_copy) and
 * call eng_mesh_vertices_moved (CKMesh::ModifierVertexMove). "Reset Mesh"
 * reads back the mesh's initial value: the engine keeps none for meshes, so
 * a mesh in the scene gets the file's vertices (eng_mesh_reset_vertices).
 */

#include "virtools.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MM_PI 3.1415926535897932f
#define MM_HALFPI 1.5707963267948966f

/* ============================================================================
 * The entities' meshes (CK3dEntity::m_Meshes)
 * ============================================================================ */

static const nmo_3dentity_state_t *entity_state(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    return object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                engine->types, object, CKPGUID_3DENTITY)
                          : NULL;
}

static nmo_object_id_t current_mesh(nmo_engine_t *engine, nmo_object_id_t entity)
{
    const nmo_3dentity_state_t *state = entity_state(engine, entity);
    return state != NULL ? eng_object_ref(engine, entity, &state->current_mesh) : 0;
}

/* CK3dEntity::SetCurrentMesh (a mesh of the entity's document; see eng_entity_set_current_mesh) */
static void set_current_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh, bool add)
{
    if (current_mesh(engine, entity) == mesh) {
        return;
    }
    if (eng_entity_set_current_mesh(engine, entity, mesh) && mesh != 0 && add) {
        eng_entity_add_mesh(engine, entity, mesh);
    }
}

/* ============================================================================
 * Mesh access
 * ============================================================================ */

static nmo_mesh_state_t *mesh_state(nmo_engine_t *engine, nmo_object_id_t mesh)
{
    nmo_object_t *object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    return object != NULL ? (nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                engine->types, object, CKPGUID_MESH)
                          : NULL;
}

/* The mesh's vertices as they are now (GetModifierVertices of a mesh not written to); NULL for none */
static const nmo_vertex_t *mesh_vertices(nmo_engine_t *engine, nmo_object_id_t mesh, uint32_t *count)
{
    const nmo_mesh_state_t *state = mesh_state(engine, mesh);
    const eng_mesh_copy_t *copy = state != NULL ? eng_mesh_copy_find(engine, mesh) : NULL;
    *count = state != NULL ? state->vertex_count : 0;
    return copy != NULL ? copy->vertices : state != NULL ? state->vertices : NULL;
}

static uint32_t vertex_count(nmo_engine_t *engine, nmo_object_id_t mesh)
{
    const nmo_mesh_state_t *state = mesh_state(engine, mesh);
    return state != NULL ? state->vertex_count : 0;
}

static nmo_object_id_t local_object(blk_t *b, int pos)
{
    nmo_object_id_t id = 0;
    nmo_engine_block_get_local_value(b, pos, &id, sizeof(id));
    return id;
}

/* A box parameter as a VxBbox holds it: Max, then Min */
typedef struct mm_box {
    eng_vector_t max, min;
} mm_box_t;

/* CKMesh::GetLocalBox of the copy's vertices */
static mm_box_t local_box(nmo_engine_t *engine, const eng_mesh_copy_t *copy)
{
    uint32_t count = vertex_count(engine, copy->mesh);
    mm_box_t box = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    for (uint32_t v = 0; v < count; v++) {
        const nmo_vector_t *p = &copy->vertices[v].position;
        if (v == 0) {
            box.max = box.min = (eng_vector_t){p->x, p->y, p->z};
            continue;
        }
        box.min.x = fminf(box.min.x, p->x), box.min.y = fminf(box.min.y, p->y), box.min.z = fminf(box.min.z, p->z);
        box.max.x = fmaxf(box.max.x, p->x), box.max.y = fmaxf(box.max.y, p->y), box.max.z = fmaxf(box.max.z, p->z);
    }
    return box;
}

static float axis_of(const eng_vector_t *v, int axis)
{
    return axis == 0 ? v->x : axis == 1 ? v->y : v->z;
}

/* The first axis of `axis` that is not 0 (Z when none) */
static int main_axis(const eng_vector_t *axis)
{
    int n = 0;
    while (n < 3 && axis_of(axis, n) == 0.0f) {
        n++;
    }
    return n == 3 ? 2 : n;
}

/* The rotation of `angle` about (x, y, z) (Vx3DMatrixFromRotation), identity for no axis */
static eng_matrix_t rotation(float x, float y, float z, float angle)
{
    eng_matrix_t m;
    eng_matrix_identity(&m);
    if (x != 0.0f || y != 0.0f || z != 0.0f) {
        eng_vector_t axis = {x, y, z};
        eng_matrix_from_rotation(&m, &axis, angle);
    }
    return m;
}

/*
 * Reset Mesh: the old versions (before 2.0) put back the positions they kept
 * in local `saved`; the others read the mesh's initial value back.
 */
static void reset_mesh(blk_t *b, eng_mesh_copy_t *copy, int saved)
{
    nmo_engine_t *engine = b->engine;
    if (nmo_engine_block_version(b) < 0x00020000u) {
        size_t size = 0;
        const eng_param_t *param = block_local(b, saved);
        const eng_vector_t *positions = param != NULL ? (const eng_vector_t *)param->data : NULL;
        size = param != NULL ? param->size / sizeof(eng_vector_t) : 0;
        uint32_t count = vertex_count(engine, copy->mesh);
        for (uint32_t v = 0; positions != NULL && v < count && v < size; v++) {
            copy->vertices[v].position = (nmo_vector_t){positions[v].x, positions[v].y, positions[v].z};
        }
    } else if (eng_in_scene(engine, copy->mesh)) {
        eng_mesh_reset_vertices(engine, copy);
    }
}

/* The target's current mesh's copy; NULL for no target or no mesh */
static eng_mesh_copy_t *target_mesh_copy(blk_t *b)
{
    nmo_object_id_t mesh = current_mesh(b->engine, nmo_engine_block_target(b));
    return mesh != 0 && mesh_state(b->engine, mesh) != NULL ? eng_mesh_copy(b->engine, mesh) : NULL;
}

/* A target that is a mesh: its copy; NULL for none */
static eng_mesh_copy_t *target_is_mesh(blk_t *b)
{
    nmo_object_id_t mesh = nmo_engine_block_target(b);
    return mesh_state(b->engine, mesh) != NULL ? eng_mesh_copy(b->engine, mesh) : NULL;
}

static eng_vector_t position_of(const nmo_vertex_t *v)
{
    return (eng_vector_t){v->position.x, v->position.y, v->position.z};
}

static void set_position(nmo_vertex_t *v, const eng_vector_t *p)
{
    v->position = (nmo_vector_t){p->x, p->y, p->z};
}

/* ============================================================================
 * The entity's meshes
 * ============================================================================ */

/* Select Mesh: the target's current mesh, added to its meshes with Add If Missing. */
static int bb_select_mesh(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t entity = nmo_engine_block_target(b);
    if (entity_state(b->engine, entity) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    set_current_mesh(b->engine, entity, nmo_engine_block_get_input_object(b, 0), get_in_int(b, 1, 1) != 0);
    return NMO_ENGINE_BR_OK;
}

/* Add Mesh: the mesh joins the target's meshes. */
static int bb_add_mesh(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t entity = nmo_engine_block_target(b);
    if (entity_state(b->engine, entity) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_entity_add_mesh(b->engine, entity, nmo_engine_block_get_input_object(b, 0));
    return NMO_ENGINE_BR_OK;
}

/* Remove Mesh: the mesh leaves the target's meshes; when it was the current one, the first
   left (none without one) is. */
static int bb_remove_mesh(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    if (entity_state(engine, entity) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_entity_remove_mesh(engine, entity, nmo_engine_block_get_input_object(b, 0));
    return NMO_ENGINE_BR_OK;
}

/*
 * Change Referential (CK3dEntity::ChangeReferential): the vertices of each of
 * the target's meshes move into the referential's space (the world without
 * one), so the target can take its world matrix without moving them.
 */
static int bb_change_referential(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t id = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, id);
    if (index < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t ref = input_entity(b, 0);
    eng_entity_t *entity = &engine->entities.data[index];
    eng_matrix_t change, inverse;
    if (ref >= 0) {
        eng_matrix_inverse(&inverse, &engine->entities.data[ref].world);
    }
    if (entity->moveable_flags & VX_MOVEABLE_WORLDALIGNED) {
        if (ref < 0) {
            return NMO_ENGINE_BR_OK;
        }
        change = inverse;
    } else if (ref >= 0) {
        eng_matrix_multiply(&change, &inverse, &entity->world);
    } else {
        change = entity->world;
    }
    int32_t meshes = eng_entity_mesh_count(engine, id);
    for (int32_t i = 0; i < meshes; i++) {
        nmo_object_id_t mesh = eng_entity_mesh_at(engine, id, i);
        eng_mesh_copy_t *copy = mesh_state(engine, mesh) != NULL ? eng_mesh_copy(engine, mesh) : NULL;
        uint32_t count = copy != NULL ? vertex_count(engine, copy->mesh) : 0;
        for (uint32_t v = 0; v < count; v++) {
            eng_vector_t p = position_of(&copy->vertices[v]), moved;
            eng_vector_transform(&moved, &change, &p);
            set_position(&copy->vertices[v], &moved);
        }
        if (copy != NULL && !copy->prelit) {
            for (uint32_t v = 0; v < count; v++) {
                const nmo_vector_t *n = &copy->vertices[v].normal;
                eng_vector_t normal = {n->x, n->y, n->z}, turned;
                eng_vector_rotate(&turned, &change, &normal);
                eng_vector_normalize(&turned);
                copy->vertices[v].normal = (nmo_vector_t){turned.x, turned.y, turned.z};
            }
            eng_mesh_build_face_normals(engine, copy);
        }
        if (copy != NULL) {
            eng_mesh_vertices_moved(engine, copy, false);
        }
    }
    eng_matrix_t world;
    if (ref >= 0) {
        world = engine->entities.data[ref].world;
    } else {
        eng_matrix_identity(&world);
    }
    nmo_engine_set_entity_world_matrix(engine, id, &world.m[0][0], true);
    entity = &engine->entities.data[index];
    if (ref >= 0) {
        entity->moveable_flags &= ~(uint32_t)VX_MOVEABLE_WORLDALIGNED;
    } else {
        entity->moveable_flags |= VX_MOVEABLE_WORLDALIGNED;
    }
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The target mesh's faces and vertices
 * ============================================================================ */

/* Invert Winding (CKMesh::InverseWinding): each face's second and third vertices swap. */
static int bb_invert_winding(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_mesh_copy_t *copy = target_is_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_mesh_state_t *state = mesh_state(b->engine, copy->mesh);
    for (uint32_t f = 0; state->face_vertex_indices != NULL && f < state->face_count; f++) {
        uint16_t *k = &state->face_vertex_indices[3u * f];
        uint16_t second = k[1];
        k[1] = k[2];
        k[2] = second;
    }
    eng_mesh_vertices_moved(b->engine, copy, true);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Face Properties: the face's material and vertex indices, each when its
 * input has a value. A face the mesh does not have is left alone (CK writes
 * past the faces).
 */
static int bb_set_face_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    eng_mesh_copy_t *copy = target_is_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_mesh_state_t *state = mesh_state(engine, copy->mesh);
    int32_t face = get_in_int(b, 0, 0);
    int32_t material = 0;
    if (nmo_engine_block_get_input_value(b, 1, &material, sizeof(material)) == NMO_OK) {
        eng_mesh_set_face_material(engine, copy->mesh, face, nmo_engine_block_get_input_object(b, 1));
    }
    eng_vector_t v = {0.0f, 0.0f, 0.0f};
    if (nmo_engine_block_get_input_value(b, 2, &v, sizeof(v)) == NMO_OK) {
        if (face >= 0 && (uint32_t)face < state->face_count && state->face_vertex_indices != NULL) {
            uint16_t *k = &state->face_vertex_indices[3u * (uint32_t)face];
            k[0] = (uint16_t)(int32_t)v.x;
            k[1] = (uint16_t)(int32_t)v.y;
            k[2] = (uint16_t)(int32_t)v.z;
        }
        eng_mesh_vertices_moved(engine, copy, false);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Set Vertex Properties: the vertex's position, then its colors (a prelit
 * mesh) or its normal (a lit one), then its UVs in a channel, each when its
 * input has a value. The engine keeps no specular colors.
 */
static int bb_set_vertex_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    eng_mesh_copy_t *copy = target_is_mesh(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t vertex = get_in_int(b, 0, 0);
    int32_t count = (int32_t)vertex_count(engine, copy->mesh);
    if (vertex < 0 || vertex >= count) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_vertex_t *target = &copy->vertices[vertex];
    eng_vector_t v = {0.0f, 0.0f, 0.0f};
    if (nmo_engine_block_get_input_value(b, 1, &v, sizeof(v)) == NMO_OK) {
        set_position(target, &v);
    }
    if (copy->prelit) {
        float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t *colors = NULL;
        if (nmo_engine_block_get_input_value(b, 3, color, sizeof(color)) == NMO_OK &&
            (colors = eng_mesh_copy_colors(engine, copy)) != NULL) {
            colors[vertex] = vxcolor_to_color(color);
        }
    } else if (nmo_engine_block_get_input_value(b, 2, &v, sizeof(v)) == NMO_OK) {
        target->normal = (nmo_vector_t){v.x, v.y, v.z};
    }
    float uv[2] = {0.0f, 0.0f};
    if (nmo_engine_block_get_input_value(b, 5, uv, sizeof(uv)) == NMO_OK) {
        size_t stride = 0;
        int32_t uv_count = 0;
        float *uvs = eng_mesh_uvs(engine, copy, get_in_int(b, 6, -1), &stride, &uv_count);
        if (uvs != NULL && vertex < uv_count) {
            float *at = (float *)((uint8_t *)uvs + (size_t)vertex * stride);
            at[0] = uv[0];
            at[1] = uv[1];
        }
    }
    copy->version = ++engine->render.mesh_version;
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The deformers
 * ============================================================================ */

/*
 * Sine Deform: each coordinate grows by itself times Deform Axis' one times
 * Magnitude times cos(Wave Length x Time + itself); Time (local 0) goes on by
 * the frame's time, back by a period past 2 pi. With Reset Mesh from the
 * initial vertices.
 */
static int bb_sine_deform(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float time = get_local_float(b, 0, 0.0f);
    eng_vector_t axis = get_in_vector(b, 0, 1.0f, 1.0f, 1.0f);
    float magnitude = get_in_float(b, 1, 1.0f), length = get_in_float(b, 2, 1.0f);
    if (get_in_int(b, 3, 1) != 0) {
        reset_mesh(b, copy, 1);
    }
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        nmo_vector_t *v = &copy->vertices[i].position;
        v->x = v->x + v->x * axis.x * magnitude * (float)cos((double)(length * time + v->x));
        v->y = v->y + v->y * axis.y * magnitude * (float)cos((double)(length * time + v->y));
        v->z = v->z + v->z * axis.z * magnitude * (float)cos((double)(length * time + v->z));
    }
    time += nmo_engine_block_delta_time(b) * 0.001f;
    if (length * time > 2.0f * MM_PI) {
        time -= 2.0f * MM_PI / length;
    }
    nmo_engine_block_set_local_value(b, 0, &time, sizeof(time));
    eng_mesh_vertices_moved(engine, copy, true);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Ken Perlin's coherent noise in three dimensions (Logics' perlin.cpp, a copy in the DLL with
   tables of its own); the tables come from MSVC's rand() from its first seed */
#define MM_B 0x100
#define MM_BM 0xff
#define MM_N 0x1000

typedef struct mm_perlin {
    bool ready;
    int32_t p[MM_B + MM_B + 2];
    float g3[MM_B + MM_B + 2][3];
} mm_perlin_t;

static mm_perlin_t mm_noise_tables;

static int32_t mm_rand(uint32_t *seed)
{
    *seed = *seed * 214013u + 2531011u;
    return (int32_t)((*seed >> 16) & 0x7fffu);
}

static void mm_perlin_init(mm_perlin_t *t)
{
    uint32_t seed = 1u;
    int i;
    for (i = 0; i < MM_B; i++) {
        t->p[i] = i;
        (void)mm_rand(&seed); /* g1 */
        (void)mm_rand(&seed); /* g2 */
        (void)mm_rand(&seed);
        float *g = t->g3[i];
        for (int j = 0; j < 3; j++) {
            g[j] = (float)((mm_rand(&seed) % (MM_B + MM_B)) - MM_B) / MM_B;
        }
        float s = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
        g[0] = g[0] / s, g[1] = g[1] / s, g[2] = g[2] / s;
    }
    while (--i) {
        int32_t k = t->p[i], j = mm_rand(&seed) % MM_B;
        t->p[i] = t->p[j];
        t->p[j] = k;
    }
    for (i = 0; i < MM_B + 2; i++) {
        t->p[MM_B + i] = t->p[i];
        memcpy(t->g3[MM_B + i], t->g3[i], sizeof(t->g3[i]));
    }
    t->ready = true;
}

static float mm_lerp(float t, float a, float b)
{
    return a + t * (b - a);
}

static float mm_noise3(const mm_perlin_t *tab, const float vec[3])
{
    int32_t b0[3], b1[3];
    float r0[3], r1[3];
    for (int i = 0; i < 3; i++) {
        float t = vec[i] + MM_N;
        b0[i] = ((int32_t)t) & MM_BM;
        b1[i] = (b0[i] + 1) & MM_BM;
        r0[i] = t - (float)(int32_t)t;
        r1[i] = r0[i] - 1.0f;
    }
    int32_t i = tab->p[b0[0]], j = tab->p[b1[0]];
    int32_t b00 = tab->p[i + b0[1]], b10 = tab->p[j + b0[1]], b01 = tab->p[i + b1[1]], b11 = tab->p[j + b1[1]];
    float t = r0[0] * r0[0] * (3.0f - 2.0f * r0[0]);
    float sy = r0[1] * r0[1] * (3.0f - 2.0f * r0[1]);
    float sz = r0[2] * r0[2] * (3.0f - 2.0f * r0[2]);
#define MM_AT3(q, rx, ry, rz) ((rx) * (q)[0] + (ry) * (q)[1] + (rz) * (q)[2])
    float u = MM_AT3(tab->g3[b00 + b0[2]], r0[0], r0[1], r0[2]);
    float v = MM_AT3(tab->g3[b10 + b0[2]], r1[0], r0[1], r0[2]);
    float a = mm_lerp(t, u, v);
    u = MM_AT3(tab->g3[b01 + b0[2]], r0[0], r1[1], r0[2]);
    v = MM_AT3(tab->g3[b11 + b0[2]], r1[0], r1[1], r0[2]);
    float bb = mm_lerp(t, u, v);
    float c = mm_lerp(sy, a, bb);
    u = MM_AT3(tab->g3[b00 + b1[2]], r0[0], r0[1], r1[2]);
    v = MM_AT3(tab->g3[b10 + b1[2]], r1[0], r0[1], r1[2]);
    a = mm_lerp(t, u, v);
    u = MM_AT3(tab->g3[b01 + b1[2]], r0[0], r1[1], r1[2]);
    v = MM_AT3(tab->g3[b11 + b1[2]], r1[0], r1[1], r1[2]);
    bb = mm_lerp(t, u, v);
    float d = mm_lerp(sy, a, bb);
#undef MM_AT3
    return mm_lerp(sz, c, d);
}

/*
 * Noise: each vertex moves by Axis times Perlin noise of its position times
 * Scale (offset by a half), its components taken from (y, z, seed),
 * (x, z, seed) and (x, y, seed); Seed is a hundredth (an integer Seed of old
 * files counts too). With Reset Mesh from the initial vertices.
 */
static int bb_noise(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float seed = 0.1f;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_INT)) {
        seed = (float)get_in_int(b, 0, 0);
    } else {
        seed = get_in_float(b, 0, 0.1f);
    }
    seed *= 0.01f;
    float scale = get_in_float(b, 1, 50.0f);
    eng_vector_t axis = get_in_vector(b, 2, 0.1f, 0.0f, 0.0f);
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_in_int(b, 3, 1) != 0) {
        reset_mesh(b, copy, 0);
    }
    if (!mm_noise_tables.ready) {
        mm_perlin_init(&mm_noise_tables);
    }
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        nmo_vector_t *v = &copy->vertices[i].position;
        float sp[3] = {v->x * scale + 0.5f, v->y * scale + 0.5f, v->z * scale + 0.5f};
        float noise[3] = {sp[1], sp[2], seed};
        float dx = mm_noise3(&mm_noise_tables, noise);
        noise[0] = sp[0];
        float dy = mm_noise3(&mm_noise_tables, noise);
        noise[1] = sp[1];
        float dz = mm_noise3(&mm_noise_tables, noise);
        v->x = v->x + dx * axis.x;
        v->y = v->y + dy * axis.y;
        v->z = v->z + dz * axis.z;
    }
    eng_mesh_vertices_moved(engine, copy, true);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The box local the deformers read (the block's attach callback saved the mesh's box there) */
static mm_box_t local_box_param(blk_t *b, int pos)
{
    mm_box_t box = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    nmo_engine_block_get_local_value(b, pos, &box, sizeof(box));
    return box;
}

/*
 * Taper: along the main axis of Axis (over the initial box's length), the two
 * other coordinates scale by 1 + z Amount + Curve z (1 - z), z mirrored with
 * Symmetry; nothing (the input staying active) for a flat box.
 */
static int bb_taper(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float amount = get_in_float(b, 0, 0.1f), curve = get_in_float(b, 1, 0.0f);
    eng_vector_t axis = get_in_vector(b, 2, 0.0f, 0.0f, 1.0f);
    int naxis = main_axis(&axis);
    bool symmetry = get_in_int(b, 3, 0) != 0, first = get_in_int(b, 4, 1) != 0, second = get_in_int(b, 5, 1) != 0;
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_in_int(b, 6, 1) != 0) {
        reset_mesh(b, copy, 0);
    }
    mm_box_t box = local_box_param(b, 1);
    float l = axis_of(&box.max, naxis) - axis_of(&box.min, naxis);
    if (l == 0.0f) {
        return NMO_ENGINE_BR_OK;
    }
    l = -l;
    eng_matrix_t mat = naxis == 0 ? rotation(0.0f, 1.0f, 0.0f, -MM_HALFPI)
                       : naxis == 1 ? rotation(1.0f, 0.0f, 0.0f, -MM_HALFPI)
                                    : rotation(0.0f, 0.0f, 0.0f, 0.0f);
    eng_matrix_t inv;
    eng_matrix_inverse(&inv, &mat);
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        eng_vector_t p = position_of(&copy->vertices[i]), v;
        eng_vector_transform(&v, &mat, &p);
        float z = v.z / l;
        if (symmetry && z < 0.0f) {
            z = -z;
        }
        float f = 1.0f + z * amount + curve * z * (1.0f - z);
        if (first) {
            v.x *= f;
        }
        if (second) {
            v.y *= f;
        }
        eng_vector_transform(&p, &inv, &v);
        set_position(&copy->vertices[i], &p);
    }
    eng_mesh_vertices_moved(engine, copy, get_in_int(b, 7, 1) != 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Twist: about the main axis of Axis, each vertex turns by Angle times its
 * height over the initial box's (Bias bends that to a power of the height).
 */
static int bb_twist(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float angle = get_in_float(b, 0, MM_HALFPI), bias = get_in_float(b, 1, 0.0f);
    eng_vector_t axis = get_in_vector(b, 2, 0.0f, 1.0f, 0.0f);
    int naxis = main_axis(&axis);
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_in_int(b, 3, 1) != 0) {
        reset_mesh(b, copy, 0);
    }
    bool do_bias = bias != 0.0f;
    if (do_bias) {
        bias = 1.0f - (bias + 100.0f) / 200.0f;
        bias = bias < 0.00001f ? 0.00001f : bias > 0.99999f ? 0.99999f : bias;
        bias = (float)(log((double)bias) / log(0.5));
    } else {
        bias = 1.0f;
    }
    eng_matrix_t mat = naxis == 0 ? rotation(0.0f, 1.0f, 0.0f, -MM_HALFPI)
                       : naxis == 1 ? rotation(1.0f, 0.0f, 0.0f, MM_HALFPI)
                                    : rotation(0.0f, 0.0f, 0.0f, 0.0f);
    eng_matrix_t inv;
    eng_matrix_inverse(&inv, &mat);
    mm_box_t box = local_box_param(b, 1);
    float height = axis_of(&box.max, naxis) - axis_of(&box.min, naxis), per_height = 0.0f;
    if (height == 0.0f) {
        angle = 0.0f;
    } else {
        per_height = angle / height;
    }
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        eng_vector_t p = position_of(&copy->vertices[i]), v;
        eng_vector_transform(&v, &mat, &p);
        float x = v.x, y = v.y, z = v.z, a = 0.0f;
        if (do_bias) {
            float u = z / height;
            a = angle * (float)pow(fabs((double)u), (double)bias);
            if (u < 0.0f) {
                a = -a;
            }
        } else {
            a = z * per_height;
        }
        float c = (float)cos((double)a), s = (float)sin((double)a);
        v.x = c * x + s * y;
        v.y = -s * x + c * y;
        eng_vector_transform(&p, &inv, &v);
        set_position(&copy->vertices[i], &p);
    }
    eng_mesh_vertices_moved(engine, copy, get_in_int(b, 4, 1) != 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Bend: about the main axis of Axis turned by Direction, the mesh bends into
 * an arc of half Angle over the box's length, its normals bent along with
 * Normal Recalculation. Reset Mesh also takes the box again.
 */
static int bb_bend(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR; /* the DLL calls through no target */
    }
    float angle = get_in_float(b, 0, MM_HALFPI) * 0.5f, direction = get_in_float(b, 1, 0.0f);
    eng_vector_t axis = get_in_vector(b, 2, 0.0f, 0.0f, 1.0f);
    int naxis = main_axis(&axis);
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_in_int(b, 3, 1) != 0) {
        reset_mesh(b, copy, 0);
        if (nmo_engine_block_version(b) >= 0x00020000u) {
            mm_box_t initial = local_box(engine, copy);
            nmo_engine_block_set_local_value(b, 1, &initial, sizeof(initial));
        }
    }
    mm_box_t box = local_box_param(b, 1);
    eng_matrix_t mat = naxis == 0 ? rotation(0.0f, 1.0f, 0.0f, -MM_HALFPI)
                       : naxis == 1 ? rotation(0.0f, 0.0f, 1.0f, -MM_HALFPI)
                                    : rotation(0.0f, 0.0f, 0.0f, 0.0f);
    if (direction != 0.0f) {
        eng_matrix_t turn = rotation(0.0f, 0.0f, 1.0f, direction);
        eng_matrix_multiply(&mat, &turn, &mat);
    }
    float len = axis_of(&box.max, naxis) - axis_of(&box.min, naxis);
    if (fabs((double)angle) < 0.0001) {
        eng_mesh_vertices_moved(engine, copy, false);
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    float r = len / angle, inv_r = 1.0f / r;
    eng_matrix_t inv;
    eng_matrix_inverse(&inv, &mat);
    bool normals = get_in_int(b, 4, 1) != 0;
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        eng_vector_t p = position_of(&copy->vertices[i]), v, n;
        eng_vector_transform(&v, &mat, &p);
        float x = v.x, y = v.y, yr = y * inv_r;
        float c = (float)cos((double)(MM_PI - yr)), s = (float)sin((double)(MM_PI - yr));
        v.x = r * c + r - x * c;
        v.y = r * s - x * s;
        eng_vector_transform(&p, &inv, &v);
        set_position(&copy->vertices[i], &p);
        if (normals) {
            const nmo_vector_t *fn = &copy->vertices[i].normal;
            eng_vector_t normal = {fn->x, fn->y, fn->z};
            eng_vector_rotate(&n, &mat, &normal);
            n.x += x;
            n.y += y;
            x = n.x;
            y = n.y;
            yr = y * inv_r;
            c = (float)cos((double)(MM_PI - yr));
            s = (float)sin((double)(MM_PI - yr));
            n.x = r * c + r - x * c;
            n.y = r * s - x * s;
            n.x -= v.x;
            n.y -= v.y;
            eng_vector_rotate(&normal, &inv, &n);
            copy->vertices[i].normal = (nmo_vector_t){normal.x, normal.y, normal.z};
        }
    }
    eng_mesh_vertices_moved(engine, copy, false);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Stretch: along the main axis of Axis the mesh stretches by Stretch
 * (squashes below 0) and the two other coordinates shrink (grow) by a
 * parabola over the initial box's height, Amplify scaling that.
 */
static int bb_stretch(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    float stretch = get_in_float(b, 0, 0.1f), amplify = get_in_float(b, 1, 0.0f);
    eng_vector_t axis = get_in_vector(b, 2, 0.0f, 0.0f, 1.0f);
    int naxis = main_axis(&axis);
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_in_int(b, 3, 1) != 0) {
        reset_mesh(b, copy, 0);
    }
    amplify = amplify >= 0.0f ? amplify + 1.0f : 1.0f / (-amplify + 1.0f);
    mm_box_t box = local_box_param(b, 1);
    float height_max = axis_of(&box.max, naxis), height_min = axis_of(&box.min, naxis);
    eng_matrix_t mat = naxis == 0 ? rotation(0.0f, 1.0f, 0.0f, -MM_HALFPI)
                       : naxis == 1 ? rotation(1.0f, 0.0f, 0.0f, -MM_HALFPI)
                                    : rotation(0.0f, 0.0f, 0.0f, 0.0f);
    eng_matrix_t inv;
    eng_matrix_inverse(&inv, &mat);
    float xy_scale = 0.0f, z_scale = 0.0f;
    if (stretch < 0.0f) {
        xy_scale = amplify * -stretch + 1.0f;
        z_scale = -1.0f / (stretch - 1.0f);
    } else {
        xy_scale = 1.0f / (amplify * stretch + 1.0f);
        z_scale = stretch + 1.0f;
    }
    float a = 4.0f * (1.0f - xy_scale), bb = -4.0f * (1.0f - xy_scale), c = 1.0f;
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        eng_vector_t p = position_of(&copy->vertices[i]), v;
        eng_vector_transform(&v, &mat, &p);
        float height = (v.z - height_min) / (height_max - height_min);
        float fraction = ((a * height) + bb) * height + c;
        v.x *= fraction;
        v.y *= fraction;
        v.z *= z_scale;
        eng_vector_transform(&p, &inv, &v);
        set_position(&copy->vertices[i], &p);
    }
    eng_mesh_vertices_moved(engine, copy, true);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Texture Displace: each vertex takes the texel under its UVs (wrapped into
 * the source rectangle) and, with 3D Displacement, moves along its normal by
 * the texel's channels times their scales; without, that is its height (y).
 * The normals are rebuilt with Normal Recalculation. Versions 1 and 2 read
 * fewer inputs. A texel outside the image leaves its vertex alone.
 */
static int bb_texture_displace(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (entity_state(engine, nmo_engine_block_target(b)) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_texture_t *texture = eng_texture(engine, nmo_engine_block_get_input_object(b, 0));
    if (texture == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t width = 0, height = 0;
    eng_bitmap_size(engine, texture, &width, &height);
    eng_surface_t *surface = eng_texture_surface(engine, texture, -1);
    if (surface == NULL || copy->vertices == NULL || width <= 0 || height <= 0) {
        return NMO_ENGINE_BR_OK;
    }
    float red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 0.0f;
    float tmin[2] = {0.0f, 0.0f}, tmax[2] = {-1.0f, -1.0f};
    bool reset = false, rebuild = true, displace_3d = false;
    uint32_t version = nmo_engine_block_version(b);
    if (version == 0x00010000u) {
        float effect = get_in_float(b, 1, 0.0f);
        int32_t channel = get_in_int(b, 2, 0);
        red = channel == 0 ? effect : 0.0f;
        green = channel == 1 ? effect : 0.0f;
        blue = channel == 2 ? effect : 0.0f;
        nmo_engine_block_get_input_value(b, 3, tmin, sizeof(tmin));
        nmo_engine_block_get_input_value(b, 4, tmax, sizeof(tmax));
    } else if (version == 0x00020000u || version == 0x00030000u) {
        red = get_in_float(b, 1, 0.0f);
        green = get_in_float(b, 2, 0.0f);
        blue = get_in_float(b, 3, 0.0f);
        alpha = get_in_float(b, 4, 0.0f);
        nmo_engine_block_get_input_value(b, 5, tmin, sizeof(tmin));
        nmo_engine_block_get_input_value(b, 6, tmax, sizeof(tmax));
        if (version == 0x00030000u) {
            reset = get_in_int(b, 7, 0) != 0;
            rebuild = get_in_int(b, 8, 1) != 0;
            displace_3d = get_local_int(b, 0, 0) != 0;
        }
    }
    if (reset && eng_in_scene(engine, copy->mesh)) {
        eng_mesh_reset_vertices(engine, copy);
        if (displace_3d) {
            eng_mesh_vertices_moved(engine, copy, true);
        }
    }
    const float size[2] = {(float)width, (float)height};
    for (int k = 0; k < 2; k++) {
        while (tmin[k] < 0.0f) tmin[k] += size[k];
        while (tmin[k] > size[k]) tmin[k] -= size[k];
        while (tmax[k] < 0.0f) tmax[k] += size[k];
        while (tmax[k] > size[k]) tmax[k] -= size[k];
    }
    float start[2], len[2];
    for (int k = 0; k < 2; k++) {
        start[k] = tmin[k];
        len[k] = tmax[k] - tmin[k];
        if (tmin[k] > tmax[k]) {
            start[k] = 0.0f;
            len[k] = tmin[k] - tmax[k];
        }
    }
    uint32_t count = vertex_count(engine, copy->mesh);
    for (uint32_t i = 0; i < count; i++) {
        nmo_vertex_t *vertex = &copy->vertices[i];
        float u = vertex->uv.x, v = vertex->uv.y;
        u -= (float)(int32_t)u;
        if (u < 0.0f) u += 1.0f;
        v -= (float)(int32_t)v;
        if (v < 0.0f) v += 1.0f;
        int32_t x = (int32_t)(start[0] + u * len[0]), y = (int32_t)(start[1] + v * len[1]);
        int64_t texel = (int64_t)x + (int64_t)width * y;
        if (x < 0 || y < 0 || texel >= (int64_t)surface->width * surface->height) {
            continue;
        }
        const uint8_t *rgba = &surface->rgba[4 * (size_t)texel];
        float amount = rgba[0] * red + rgba[1] * green + rgba[2] * blue + rgba[3] * alpha;
        if (displace_3d) {
            vertex->position.x += vertex->normal.x * amount;
            vertex->position.y += vertex->normal.y * amount;
            vertex->position.z += vertex->normal.z * amount;
        } else {
            vertex->position.y = amount;
        }
    }
    if (rebuild && !copy->prelit) {
        eng_mesh_vertices_moved(engine, copy, true);
    } else {
        if (rebuild) {
            eng_mesh_build_face_normals(engine, copy);
        }
        eng_mesh_vertices_moved(engine, copy, false);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Vertex Translate: Vertex Count vertices from Vertex Start Index move by the
 * Translation Vector (in the referential's space; the target's own when it
 * is the target); out of range a parameter error. Reset reads the initial
 * vertices back. The normals are rebuilt either way.
 */
static int bb_vertex_translate(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, target);
    if (index < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 1)) {
        reset_mesh(b, copy, 0);
        IN_OFF(b, 1);
    } else {
        eng_vector_t move = get_in_vector(b, 2, 0.0f, 0.0f, 0.0f);
        int32_t start = get_in_int(b, 0, 0), count = get_in_int(b, 1, 1);
        nmo_object_id_t ref = nmo_engine_block_get_input_object(b, 3);
        if (ref != target) {
            eng_entity_transform_vector(engine, index, &move, &move, eng_entity_index(engine, ref), true);
        }
        int32_t vertices = (int32_t)vertex_count(engine, copy->mesh);
        if (start < 0 || count < 0 || start > vertices || start + count > vertices) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
        for (int32_t i = start; i < start + count; i++) {
            copy->vertices[i].position.x += move.x;
            copy->vertices[i].position.y += move.y;
            copy->vertices[i].position.z += move.z;
        }
        IN_OFF(b, 0);
    }
    eng_mesh_vertices_moved(engine, copy, true);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The morphers
 * ============================================================================ */

/* Mesh Morpher's load callback: the owner's current mesh is the initial one, none made yet */
static int cb_mesh_morpher(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_object_id_t initial = current_mesh(b->engine, nmo_engine_block_owner(b)), none = 0;
        nmo_engine_block_set_local_value(b, 0, &initial, sizeof(initial));
        nmo_engine_block_set_local_value(b, 1, &none, sizeof(none));
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Mesh Morpher: the target shows a copy of Start Mesh (made once, "<name>_Morpher")
 * whose vertices are Effect Value of the way from Start Mesh's to End Mesh's,
 * the vertex normals too with Calculate Vertices Normals. The meshes must be
 * of the same class with as many vertices.
 */
static int bb_mesh_morpher(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    nmo_object_id_t start = nmo_engine_block_get_input_object(b, 0), end = nmo_engine_block_get_input_object(b, 1);
    if (mesh_state(engine, start) == NULL || mesh_state(engine, end) == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (nmo_object_get_class_id(eng_find_object(engine, start)) !=
            nmo_object_get_class_id(eng_find_object(engine, end)) ||
        vertex_count(engine, start) != vertex_count(engine, end)) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (entity_state(engine, entity) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR; /* the DLL calls through no target */
    }
    nmo_object_id_t made = local_object(b, 1);
    if (mesh_state(engine, made) == NULL) {
        made = 0;
    }
    if (current_mesh(engine, entity) != made) {
        nmo_object_id_t old = current_mesh(engine, entity);
        nmo_engine_block_set_local_value(b, 0, &old, sizeof(old));
    }
    if (made == 0 || vertex_count(engine, made) != vertex_count(engine, start)) {
        if (made != 0) {
            nmo_engine_destroy_object(engine, made);
        }
        const char *name = nmo_engine_object_name(engine, start);
        size_t length = name != NULL ? strlen(name) : 0;
        char *morph_name = (char *)malloc(length + sizeof("_Morpher"));
        if (morph_name == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        memcpy(morph_name, name != NULL ? name : "", length);
        memcpy(morph_name + length, "_Morpher", sizeof("_Morpher"));
        made = eng_mesh_duplicate(engine, entity, start, morph_name);
        free(morph_name);
        nmo_engine_block_set_local_value(b, 1, &made, sizeof(made));
        if (made == 0) {
            return NMO_ENGINE_BR_OK;
        }
    }
    set_current_mesh(engine, entity, made, false);
    bool vertex_normals = get_in_int(b, 3, 0) != 0;
    float effect = get_in_float(b, 2, 0.5f);
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, made);
    uint32_t count = 0, end_count = 0;
    const nmo_vertex_t *from = mesh_vertices(engine, start, &count), *to = mesh_vertices(engine, end, &end_count);
    if (copy == NULL || from == NULL || to == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    for (uint32_t i = 0; i < count; i++) {
        const nmo_vector_t *s = &from[i].position, *e = &to[i].position;
        copy->vertices[i].position = (nmo_vector_t){s->x + (e->x - s->x) * effect, s->y + (e->y - s->y) * effect,
                                                    s->z + (e->z - s->z) * effect};
        if (vertex_normals) {
            const nmo_vector_t *sn = &from[i].normal, *en = &to[i].normal;
            eng_vector_t n = {sn->x + (en->x - sn->x) * effect, sn->y + (en->y - sn->y) * effect,
                              sn->z + (en->z - sn->z) * effect};
            eng_vector_normalize(&n);
            copy->vertices[i].normal = (nmo_vector_t){n.x, n.y, n.z};
        }
    }
    eng_mesh_vertices_moved(engine, copy, false);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Multi Mesh Morpher: the target's current mesh becomes the meshes' vertices
 * weighted by their coefficients over their sum; with Use Relative Morphing,
 * the first mesh (the neutral one) plus each other's difference from it
 * times its coefficient. A mesh of another vertex count is skipped.
 */
static int bb_multi_mesh_morpher(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    if (entity_state(engine, entity) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_copy_t *copy = target_mesh_copy(b);
    if (copy == NULL || copy->vertices == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    uint32_t count = vertex_count(engine, copy->mesh), first_count = 0;
    nmo_object_id_t first = nmo_engine_block_get_input_object(b, 0);
    const nmo_vertex_t *neutral = first != 0 ? mesh_vertices(engine, first, &first_count) : NULL;
    if (neutral == NULL || first_count != count) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    /* the first mesh may be the target's own: read it from a copy */
    eng_vector_t *base = count > 0 ? (eng_vector_t *)malloc(count * sizeof(eng_vector_t)) : NULL;
    if (base == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    for (uint32_t v = 0; v < count; v++) {
        base[v] = position_of(&neutral[v]);
    }
    int pins = nmo_engine_block_input_parameter_count(b);
    bool relative = get_local_int(b, 2, 0) != 0;
    if (relative) {
        for (uint32_t v = 0; v < count; v++) {
            set_position(&copy->vertices[v], &base[v]);
        }
    } else {
        float sum = 0.0f;
        for (int a = 1; a < pins; a += 2) {
            sum += get_in_float(b, a, 1.0f);
        }
        float inverse = sum == 0.0f ? 1.0f : 1.0f / sum;
        float coef = get_in_float(b, 1, 1.0f) * inverse;
        for (uint32_t v = 0; v < count; v++) {
            eng_vector_t p = {coef * base[v].x, coef * base[v].y, coef * base[v].z};
            set_position(&copy->vertices[v], &p);
        }
        for (int a = 2; a < pins; a += 2) {
            uint32_t mesh_count = 0;
            nmo_object_id_t mesh = nmo_engine_block_get_input_object(b, a);
            const nmo_vertex_t *other = mesh != 0 ? mesh_vertices(engine, mesh, &mesh_count) : NULL;
            float weight = get_in_float(b, a + 1, 1.0f);
            if (other == NULL || mesh_count != count || weight == 0.0f) {
                continue;
            }
            weight *= inverse;
            for (uint32_t v = 0; v < count; v++) {
                copy->vertices[v].position.x += weight * other[v].position.x;
                copy->vertices[v].position.y += weight * other[v].position.y;
                copy->vertices[v].position.z += weight * other[v].position.z;
            }
        }
    }
    for (int a = 2; relative && a < pins; a += 2) {
        uint32_t mesh_count = 0;
        nmo_object_id_t mesh = nmo_engine_block_get_input_object(b, a);
        const nmo_vertex_t *other = mesh != 0 ? mesh_vertices(engine, mesh, &mesh_count) : NULL;
        float weight = get_in_float(b, a + 1, 1.0f);
        if (other == NULL || mesh_count != count || weight == 0.0f) {
            continue;
        }
        for (uint32_t v = 0; v < count; v++) {
            copy->vertices[v].position.x += weight * (other[v].position.x - base[v].x);
            copy->vertices[v].position.y += weight * (other[v].position.y - base[v].y);
            copy->vertices[v].position.z += weight * (other[v].position.z - base[v].z);
        }
    }
    free(base);
    eng_mesh_vertices_moved(engine, copy, get_local_int(b, 1, 1) != 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * The mesh's counts
 * ============================================================================ */

/* Set Vertex Count (CKMesh::SetVertexCount): the target mesh's vertex count (100 without a value). */
static int bb_set_vertex_count(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t mesh = nmo_engine_block_target(b);
    if (mesh_state(b->engine, mesh) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_set_vertex_count(b->engine, mesh, get_in_int(b, 0, 100));
    return NMO_ENGINE_BR_OK;
}

/* Set Face Count (CKMesh::SetFaceCount): the target mesh's face count (100 without a value). */
static int bb_set_face_count(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t mesh = nmo_engine_block_target(b);
    if (mesh_state(b->engine, mesh) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_mesh_set_face_count(b->engine, mesh, get_in_int(b, 0, 100));
    return NMO_ENGINE_BR_OK;
}

/* Explode's load callback: no mesh made yet */
static int cb_explode(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        nmo_object_id_t none = 0;
        nmo_engine_block_set_local_value(b, 6, &none, sizeof(none));
    }
    return NMO_ENGINE_BR_OK;
}

/* rand() % 100 / 100, as the DLL takes it */
static float explode_random(nmo_engine_t *engine)
{
    return (float)(eng_random_u15(engine) % 100u) * 0.01f;
}

/*
 * Explode: In shows a copy of the owner's mesh ("<name>Explode") whose faces
 * are apart; each frame (Loop In) the faces fly from the mesh's center by the
 * curve's growth since the last frame times Max Distance Factor times its
 * radius (a random part of it each), plus Spreading Direction times their
 * distance, turning with Rotation, each corner apart with Deformation. Past
 * the Duration the owner gets its mesh back, is hidden, and Out.
 */
static int bb_explode(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    if (entity_state(engine, entity) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t duration = get_in_int(b, 0, 1000);
    float max_distance = get_in_float(b, 2, 2.0f);
    size_t curve_size = 0;
    const void *curve = nmo_engine_block_input_data(b, 1, &curve_size);
    if (curve == NULL || curve_size == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t elapsed = 0;
    if (IN_ACTIVE(b, 0)) {
        nmo_object_id_t made = local_object(b, 6);
        if (made != 0 && mesh_state(engine, made) != NULL) {
            nmo_engine_destroy_object(engine, made);
        }
        nmo_object_id_t original = current_mesh(engine, entity);
        nmo_engine_block_set_local_value(b, 5, &original, sizeof(original));
        const char *name = original != 0 ? nmo_engine_object_name(engine, original) : NULL;
        size_t length = name != NULL ? strlen(name) : 0;
        char *explode_name = (char *)malloc(length + sizeof("Explode"));
        made = 0;
        if (explode_name != NULL && original != 0) {
            memcpy(explode_name, name != NULL ? name : "", length);
            memcpy(explode_name + length, "Explode", sizeof("Explode"));
            made = eng_mesh_duplicate(engine, entity, original, explode_name);
        }
        free(explode_name);
        if (made != 0) {
            eng_mesh_dissociate_faces(engine, made);
        }
        nmo_engine_block_set_local_value(b, 6, &made, sizeof(made));
        set_current_mesh(engine, entity, made, true);
        eng_vector_t center = {0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
        eng_mesh_sphere(engine, made, &center, &radius);
        nmo_engine_block_set_local_value(b, 3, &center, sizeof(center));
        nmo_engine_block_set_local_value(b, 0, &elapsed, sizeof(elapsed));
        float last = max_distance * nmo_engine_curve_y(curve, curve_size, 0.0f);
        nmo_engine_block_set_local_value(b, 1, &last, sizeof(last));
        IN_OFF(b, 0);
    } else {
        IN_OFF(b, 1);
        elapsed = get_local_int(b, 0, 0);
    }
    nmo_object_id_t shown = current_mesh(engine, entity);
    eng_mesh_copy_t *copy = mesh_state(engine, shown) != NULL ? eng_mesh_copy(engine, shown) : NULL;
    if (copy == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    eng_vector_t direction = get_in_vector(b, 3, 0.0f, 0.0f, 0.0f);
    bool rotate = get_in_int(b, 4, 1) != 0, deform = get_in_int(b, 5, 0) != 0;
    elapsed += (int32_t)nmo_engine_block_delta_time(b);
    nmo_engine_block_set_local_value(b, 0, &elapsed, sizeof(elapsed));
    float value = max_distance * nmo_engine_curve_y(curve, curve_size, (float)elapsed / (float)duration);
    float delta = value - get_local_float(b, 1, 0.0f);
    nmo_engine_block_set_local_value(b, 1, &value, sizeof(value));
    eng_vector_t center = {0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_local_value(b, 3, &center, sizeof(center));
    eng_vector_t sphere_center;
    float radius = 0.0f;
    eng_mesh_sphere(engine, shown, &sphere_center, &radius);
    float effect = max_distance * radius;
    const nmo_mesh_state_t *state = mesh_state(engine, shown);
    uint32_t count = state->vertex_count;
    for (uint32_t f = 0; state->face_vertex_indices != NULL && f < state->face_count; f++) {
        const uint16_t *k = &state->face_vertex_indices[3u * f];
        if (k[0] >= count || k[1] >= count || k[2] >= count) {
            continue;
        }
        nmo_vector_t *corner[3] = {&copy->vertices[k[0]].position, &copy->vertices[k[1]].position,
                                   &copy->vertices[k[2]].position};
        eng_vector_t from = {corner[0]->x - center.x, corner[0]->y - center.y, corner[0]->z - center.z};
        float distance = eng_vector_magnitude(&from);
        eng_vector_normalize(&from);
        for (int c = 0; c < 3; c++) {
            if (c > 0 && !deform) {
                break;
            }
            float push = delta * effect * explode_random(engine);
            eng_vector_t step = {from.x * push + direction.x * distance * 0.1f,
                                 from.y * push + direction.y * distance * 0.1f,
                                 from.z * push + direction.z * distance * 0.1f};
            for (int v = deform ? c : 0; v < (deform ? c + 1 : 3); v++) {
                corner[v]->x += step.x, corner[v]->y += step.y, corner[v]->z += step.z;
            }
        }
        if (rotate) {
            eng_vector_t axis = {(corner[0]->x + corner[1]->x + corner[2]->x) * 0.33333333f,
                                 (corner[0]->y + corner[1]->y + corner[2]->y) * 0.33333333f,
                                 (corner[0]->z + corner[1]->z + corner[2]->z) * 0.33333333f};
            eng_matrix_t turn = rotation(axis.x, axis.y, axis.z, delta * 10.0f);
            for (int v = 0; v < 3; v++) {
                eng_vector_t p = {corner[v]->x, corner[v]->y, corner[v]->z}, moved;
                eng_vector_transform(&moved, &turn, &p);
                *corner[v] = (nmo_vector_t){moved.x, moved.y, moved.z};
            }
        }
    }
    eng_mesh_vertices_moved(engine, copy, false);
    if (elapsed < duration) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t original = local_object(b, 5), made = local_object(b, 6);
    set_current_mesh(engine, entity, original, true);
    eng_entity_remove_mesh(engine, entity, made);
    if (made != 0 && mesh_state(engine, made) != NULL) {
        nmo_engine_destroy_object(engine, made);
    }
    eng_show(engine, entity, 0); /* CKHIDE */
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Skin Join
 * ============================================================================ */

/* A vertex of the owner's mesh takes vertex `iv` of joined object `ie` (-1: none) */
typedef struct mm_correspondence {
    int32_t ie, iv;
} mm_correspondence_t;

/* The pre render callback: each vertex with a correspondence takes its joined vertex, and
   normal, into the owner's space; the face normals are built again */
static void skin_join_render(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)transparent;
    const eng_node_t *block = &engine->nodes.data[node];
    eng_param_t *joined = eng_node_param(engine, block->first_local, block->local_count, 0);
    eng_param_t *table = eng_node_param(engine, block->first_local, block->local_count, 1);
    int32_t index = eng_entity_index(engine, entity);
    nmo_object_id_t mesh = current_mesh(engine, entity);
    eng_mesh_copy_t *copy = mesh_state(engine, mesh) != NULL ? eng_mesh_copy(engine, mesh) : NULL;
    if (joined == NULL || table == NULL || table->data == NULL || index < 0 || copy == NULL) {
        return;
    }
    size_t objects = joined->data != NULL ? joined->size / sizeof(nmo_object_id_t) : 0;
    eng_matrix_t inverse;
    eng_matrix_inverse(&inverse, &engine->entities.data[index].world);
    eng_matrix_t *into = objects > 0 ? (eng_matrix_t *)malloc(objects * sizeof(eng_matrix_t)) : NULL;
    nmo_object_id_t *meshes = objects > 0 ? (nmo_object_id_t *)calloc(objects, sizeof(nmo_object_id_t)) : NULL;
    for (size_t c = 0; into != NULL && meshes != NULL && c < objects; c++) {
        nmo_object_id_t object = 0;
        memcpy(&object, joined->data + c * sizeof(object), sizeof(object));
        int32_t at = eng_entity_index(engine, object);
        eng_matrix_identity(&into[c]);
        if (at >= 0) {
            eng_matrix_multiply(&into[c], &inverse, &engine->entities.data[at].world);
            meshes[c] = current_mesh(engine, object);
        }
    }
    uint32_t count = vertex_count(engine, mesh);
    size_t held = table->size / sizeof(mm_correspondence_t);
    for (uint32_t i = 0; into != NULL && meshes != NULL && i < count && i < held; i++) {
        mm_correspondence_t link;
        memcpy(&link, table->data + i * sizeof(link), sizeof(link));
        if (link.ie < 0 || (size_t)link.ie >= objects || meshes[link.ie] == 0) {
            continue;
        }
        uint32_t other_count = 0;
        const nmo_vertex_t *other = mesh_vertices(engine, meshes[link.ie], &other_count);
        if (other == NULL || link.iv < 0 || (uint32_t)link.iv >= other_count) {
            continue;
        }
        eng_vector_t p = position_of(&other[link.iv]), n = {other[link.iv].normal.x + p.x,
                                                          other[link.iv].normal.y + p.y,
                                                          other[link.iv].normal.z + p.z};
        eng_vector_t at, tip;
        eng_vector_transform(&at, &into[link.ie], &p);
        eng_vector_transform(&tip, &into[link.ie], &n);
        tip.x -= at.x, tip.y -= at.y, tip.z -= at.z;
        eng_vector_normalize(&tip);
        set_position(&copy->vertices[i], &at);
        copy->vertices[i].normal = (nmo_vector_t){tip.x, tip.y, tip.z};
    }
    free(into);
    free(meshes);
    eng_mesh_build_face_normals(engine, copy);
    eng_mesh_vertices_moved(engine, copy, false);
}

static void skin_join_attach(blk_t *b, bool on)
{
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    if (on) {
        eng_entity_callback_t callback = {owner, ENG_ENTITY_PRE_RENDER, skin_join_render, b->node, false};
        eng_add_entity_callback(b->engine, &callback);
    } else {
        eng_remove_entity_callback(b->engine, owner, ENG_ENTITY_PRE_RENDER, skin_join_render, b->node);
    }
}

/*
 * The edited callback: the joined objects (inputs 1 on), and for each vertex
 * of the owner's mesh the nearest of their vertices (in the world) within
 * Threshold.
 */
static void skin_join_edited(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    int pins = nmo_engine_block_input_parameter_count(b);
    nmo_object_id_t *objects = pins > 1 ? (nmo_object_id_t *)malloc((size_t)pins * sizeof(nmo_object_id_t)) : NULL;
    size_t count = 0, total = 0;
    for (int a = 1; objects != NULL && a < pins; a++) {
        nmo_object_id_t object = nmo_engine_block_get_input_object(b, a);
        if (object != 0 && entity_state(engine, object) != NULL) {
            objects[count++] = object;
            total += vertex_count(engine, current_mesh(engine, object));
        }
    }
    nmo_engine_block_set_local_value(b, 0, objects, count * sizeof(nmo_object_id_t));
    eng_vector_t *positions = total > 0 ? (eng_vector_t *)malloc(total * sizeof(eng_vector_t)) : NULL;
    mm_correspondence_t *of = total > 0 ? (mm_correspondence_t *)malloc(total * sizeof(mm_correspondence_t)) : NULL;
    size_t at = 0;
    for (size_t o = 0; positions != NULL && of != NULL && o < count; o++) {
        uint32_t vertices = 0;
        const nmo_vertex_t *v = mesh_vertices(engine, current_mesh(engine, objects[o]), &vertices);
        int32_t index = eng_entity_index(engine, objects[o]);
        for (uint32_t i = 0; v != NULL && index >= 0 && i < vertices && at < total; i++, at++) {
            eng_vector_t p = position_of(&v[i]);
            eng_vector_transform(&positions[at], &engine->entities.data[index].world, &p);
            of[at] = (mm_correspondence_t){(int32_t)o, (int32_t)i};
        }
    }
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t owner_index = eng_entity_index(engine, owner);
    uint32_t mine = 0;
    const nmo_vertex_t *own = mesh_vertices(engine, current_mesh(engine, owner), &mine);
    mm_correspondence_t *table = own != NULL && mine > 0 && owner_index >= 0
        ? (mm_correspondence_t *)malloc(mine * sizeof(mm_correspondence_t)) : NULL;
    float threshold = get_in_float(b, 0, 0.2f);
    threshold *= threshold;
    for (uint32_t i = 0; table != NULL && i < mine; i++) {
        eng_vector_t p = position_of(&own[i]), world;
        eng_vector_transform(&world, &engine->entities.data[owner_index].world, &p);
        float best = 66666666.0f;
        table[i] = (mm_correspondence_t){-1, -1};
        for (size_t j = 0; positions != NULL && j < at; j++) {
            float dx = world.x - positions[j].x, dy = world.y - positions[j].y, dz = world.z - positions[j].z;
            float d = dx * dx + dy * dy + dz * dz;
            if (d < threshold && d < best) {
                best = d;
                table[i] = (mm_correspondence_t){of[j].ie, of[j].iv};
            }
        }
    }
    if (table != NULL) {
        nmo_engine_block_set_local_value(b, 1, table, mine * sizeof(mm_correspondence_t));
    }
    free(table);
    free(positions);
    free(of);
    free(objects);
}

/* Skin Join's callbacks: its render callback goes and comes back with the script, and goes
   for good at reset; edited, the correspondences are made */
static int cb_skin_join(blk_t *b, uint32_t message)
{
    bool joining = get_local_int(b, 2, 0) != 0;
    if (message == NMO_ENGINE_MSG_EDITED) {
        skin_join_edited(b);
    } else if (message == NMO_ENGINE_MSG_RESET && joining) {
        const int32_t off = 0;
        nmo_engine_block_set_local_value(b, 2, &off, sizeof(off));
        skin_join_attach(b, false);
    } else if ((message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) && joining) {
        skin_join_attach(b, false);
    } else if ((message == NMO_ENGINE_MSG_RESUME || message == NMO_ENGINE_MSG_ACTIVATESCRIPT) && joining) {
        skin_join_attach(b, true);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Skin Join: On, the owner's mesh vertices follow the joined objects' vertices
 * they were matched with (a pre render callback, so only while the owner is in
 * view); Off stops. The DLL hands a joined object's box in view to an owner
 * that was all outside (SetBoundingBox) so it renders again; the engine has no
 * user boxes, so that is left out.
 */
static int bb_skin_join(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        const int32_t on = 1;
        nmo_engine_block_set_local_value(b, 2, &on, sizeof(on));
        skin_join_attach(b, true);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        skin_join_attach(b, false);
        const int32_t off = 0;
        nmo_engine_block_set_local_value(b, 2, &off, sizeof(off));
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Precompute Lighting
 * ============================================================================ */

/* VxPreparedLight: a light as the vertices of the entity see it */
typedef struct mm_light {
    int32_t type;
    eng_vector_t position;  /* in the entity's space (a parallel light's normalized) */
    eng_vector_t direction; /* toward the light, in the entity's space */
    eng_vector_t halfway;
    float range, inv_range;
    float r, g, b;
    float att[3];
    float falloff, cosphi, costheta, inv_theta_minus_phi;
    bool specular;
} mm_light_t;

static float mm_dot(const eng_vector_t *a, const eng_vector_t *b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

/* LightingEngine::LitVertex: the lights' diffuse and specular on a vertex */
static void lit_vertex(const mm_light_t *lights, size_t count, float power, const eng_vector_t *pos,
                       const eng_vector_t *normal, float diffuse[3], float specular[3])
{
    diffuse[0] = diffuse[1] = diffuse[2] = specular[0] = specular[1] = specular[2] = 0.0f;
    for (size_t l = 0; l < count; l++) {
        const mm_light_t *light = &lights[l];
        float attspot = 1.0f, factor = 0.0f;
        eng_vector_t direction = {light->position.x - pos->x, light->position.y - pos->y, light->position.z - pos->z};
        float att = eng_vector_magnitude(&direction);
        switch (light->type) {
        case VX_LIGHTPOINT:
            if (att < light->range) {
                direction.x /= att, direction.y /= att, direction.z /= att;
                att = (light->range - att) * light->inv_range;
                att = light->att[0] + light->att[1] * att + light->att[2] * att * att;
                factor = att * mm_dot(normal, &direction);
            }
            break;
        case VX_LIGHTSPOT:
            if (att < light->range) {
                direction.x /= att, direction.y /= att, direction.z /= att;
                float rho = mm_dot(&direction, &light->direction);
                if (rho <= light->cosphi) {
                    attspot = 0.0f;
                } else {
                    float spot = 1.0f;
                    if (rho <= light->costheta) {
                        spot = (rho - light->cosphi) * light->inv_theta_minus_phi;
                        if (light->falloff != 1.0f) {
                            spot = (float)pow((double)spot, (double)light->falloff);
                        }
                    }
                    att = (light->range - att) * light->inv_range;
                    attspot = (light->att[0] + light->att[1] * att + light->att[2] * att * att) * spot;
                    factor = attspot * mm_dot(normal, &direction);
                }
            }
            break;
        case VX_LIGHTDIREC:
            factor = mm_dot(normal, &light->direction);
            break;
        case VX_LIGHTPARA:
            factor = mm_dot(normal, &light->position);
            break;
        default:
            break;
        }
        if (factor > 0.0f) {
            diffuse[0] += light->r * factor, diffuse[1] += light->g * factor, diffuse[2] += light->b * factor;
        }
        if (light->specular && power != 0.0f) {
            float s = mm_dot(normal, &light->halfway);
            if (s > 0.0f && attspot > 0.0f) {
                if (power != 1.0f) {
                    s = (float)pow((double)s, (double)power);
                }
                s *= attspot;
                specular[0] += light->r * s, specular[1] += light->g * s, specular[2] += light->b * s;
            }
        }
    }
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/*
 * Precompute Lighting: the target's mesh becomes prelit, its vertex colors
 * what the target lights in the scene (shown, within range) and the scene's
 * ambient light make of each material group's material (a group without
 * one is skipped). With Specular, the lights' specular too, the halfway
 * vector taken from the camera's position as the DLL takes it.
 */
static int bb_precompute_lighting(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, entity);
    if (index < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t mesh = current_mesh(engine, entity);
    eng_mesh_copy_t *copy = mesh_state(engine, mesh) != NULL ? eng_mesh_copy(engine, mesh) : NULL;
    if (copy == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool with_specular = get_in_int(b, 0, 0) != 0;
    int32_t camera = eng_entity_index(engine, engine->render.camera);
    eng_vector_t camera_position = camera >= 0 ? eng_entity_position(engine, camera, -1) : (eng_vector_t){0.0f, 0.0f, 0.0f};
    eng_vector_t center;
    float radius = 0.0f;
    eng_mesh_sphere(engine, mesh, &center, &radius);
    nmo_object_id_t *ids = NULL;
    size_t total = eng_objects_of_class(engine, NMO_CID_TARGETLIGHT, &ids);
    mm_light_t *lights = total > 0 ? (mm_light_t *)calloc(total, sizeof(mm_light_t)) : NULL;
    size_t count = 0;
    eng_vector_t entity_position = eng_entity_position(engine, index, -1);
    for (size_t i = 0; lights != NULL && i < total; i++) {
        const eng_light_t *light = eng_light(engine, ids[i]);
        int32_t at = eng_entity_index(engine, ids[i]);
        if (light == NULL || at < 0 || !eng_in_scene(engine, ids[i]) ||
            !(eng_object_visibility(engine, ids[i]) & CK_OBJECT_VISIBLE)) {
            continue;
        }
        eng_vector_t world = eng_entity_position(engine, at, -1);
        if (light->type == VX_LIGHTSPOT || light->type == VX_LIGHTPOINT) {
            eng_vector_t apart = {entity_position.x - world.x, entity_position.y - world.y, entity_position.z - world.z};
            if (light->range < eng_vector_magnitude(&apart) - radius) {
                continue;
            }
        }
        mm_light_t *prepared = &lights[count++];
        prepared->type = light->type;
        prepared->position = eng_entity_position(engine, at, index);
        if (light->type == VX_LIGHTPARA) {
            eng_vector_normalize(&prepared->position);
        }
        if (light->type == VX_LIGHTPOINT) {
            prepared->direction = prepared->position;
        } else {
            const eng_matrix_t *m = &engine->entities.data[at].world;
            eng_vector_t z = {m->m[2][0], m->m[2][1], m->m[2][2]};
            eng_entity_transform_vector(engine, index, &prepared->direction, &z, -1, true);
            eng_vector_normalize(&prepared->direction);
            prepared->direction = (eng_vector_t){-prepared->direction.x, -prepared->direction.y, -prepared->direction.z};
        }
        memcpy(prepared->att, light->attenuation, sizeof(prepared->att));
        prepared->range = light->range;
        prepared->inv_range = 1.0f / light->range;
        prepared->r = light->color[0], prepared->g = light->color[1], prepared->b = light->color[2];
        if (light->type == VX_LIGHTSPOT) {
            prepared->falloff = light->fall_off_shape;
            prepared->cosphi = cosf(light->fall_off * 0.5f);
            prepared->costheta = cosf(light->hot_spot * 0.5f);
            prepared->inv_theta_minus_phi = 1.0f / (prepared->costheta - prepared->cosphi);
        }
        prepared->specular = with_specular && light->specular;
        if (prepared->specular) {
            eng_vector_t eye = camera_position;
            eng_vector_normalize(&eye);
            prepared->halfway = (eng_vector_t){eye.x + prepared->direction.x, eye.y + prepared->direction.y,
                                               eye.z + prepared->direction.z};
            eng_vector_normalize(&prepared->halfway);
        }
    }
    free(ids);
    nmo_engine_mesh_t shape;
    uint32_t *colors = eng_mesh_copy_colors(engine, copy);
    uint32_t vertices = vertex_count(engine, mesh);
    bool *done = vertices > 0 ? (bool *)malloc(vertices * sizeof(bool)) : NULL;
    if (colors != NULL && done != NULL && nmo_engine_mesh(engine, mesh, &shape)) {
        float ambient[4] = {(float)((engine->render.ambient_light >> 16) & 0xFFu) / 255.0f,
                            (float)((engine->render.ambient_light >> 8) & 0xFFu) / 255.0f,
                            (float)(engine->render.ambient_light & 0xFFu) / 255.0f, 1.0f};
        for (int32_t g = 0; g < shape.material_count; g++) {
            nmo_object_id_t material_id = nmo_engine_mesh_material(engine, mesh, g);
            const eng_material_t *material = material_id != 0 ? eng_material(engine, material_id) : NULL;
            if (material == NULL) {
                continue;
            }
            const nmo_engine_material_t *m = &material->desc;
            float base[3];
            for (int c = 0; c < 3; c++) {
                base[c] = m->ambient[c] * ambient[c] + m->emissive[c];
            }
            memset(done, 0, vertices * sizeof(bool));
            for (int32_t f = 0; f < shape.face_count; f++) {
                uint32_t group = shape.faces != NULL ? shape.faces[f].material_group_idx : 0;
                if (nmo_engine_mesh_material(engine, mesh, (int32_t)group) != material_id) {
                    continue;
                }
                for (int k = 0; k < 3; k++) {
                    uint16_t v = shape.indices[3 * f + k];
                    if (v >= vertices || done[v]) {
                        continue;
                    }
                    eng_vector_t p = position_of(&copy->vertices[v]);
                    eng_vector_t n = {copy->vertices[v].normal.x, copy->vertices[v].normal.y, copy->vertices[v].normal.z};
                    float diffuse[3], specular[3];
                    lit_vertex(lights, count, m->power, &p, &n, diffuse, specular);
                    float color[4];
                    for (int c = 0; c < 3; c++) {
                        color[c] = clamp01(base[c] + m->diffuse[c] * diffuse[c] + m->specular[c] * specular[c]);
                    }
                    color[3] = clamp01(m->diffuse[3]);
                    colors[v] = vxcolor_to_color(color);
                    done[v] = true;
                }
            }
        }
    }
    free(done);
    free(lights);
    if (!copy->prelit) {
        eng_mesh_set_lit_mode(engine, mesh, 0);
    }
    copy->version = ++engine->render.mesh_version;
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Patch and progressive meshes
 * ============================================================================ */

/* Set PatchMesh Steps (CKPatchMesh::SetIterationCount): the target patch mesh's, or old
   versions' target 3D object's current one; nothing for another mesh. The engine draws patch
   meshes as their file has them. */
static int bb_set_patch_mesh_steps(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (entity_state(engine, target) != NULL) {
        target = current_mesh(engine, target);
    }
    nmo_object_t *object = target != 0 ? eng_find_object(engine, target) : NULL;
    nmo_patchmesh_state_t *patch = object != NULL && nmo_object_get_class_id(object) == NMO_CID_PATCHMESH
        ? (nmo_patchmesh_state_t *)nmo_object_get_state(object) : NULL;
    if (patch != NULL) {
        patch->iteration_count = get_in_int(b, 0, 1);
    }
    return NMO_ENGINE_BR_OK;
}

/* Set Progressive Mesh Options (CreatePM / DestroyPM, EnablePMGeoMorph, SetPMGeoMorphStep): the
   target mesh's progressive mesh flags; the engine renders every face either way. */
static int bb_set_progressive_mesh_options(blk_t *b)
{
    nmo_mesh_state_t *state = mesh_state(b->engine, nmo_engine_block_target(b));
    if (state == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    state->has_progressive_mesh = get_in_int(b, 0, 1) != 0;
    state->pm_morph_enabled = get_in_int(b, 1, 0) != 0;
    state->pm_morph_step = get_in_int(b, 2, 0);
    return NMO_ENGINE_BR_OK;
}

/* CKParameterIn::IsEnabled: the input was not disabled (a "Modify Parameters" setting) */
static bool input_enabled(blk_t *b, int pos)
{
    const eng_node_t *node = block_node(b);
    eng_param_t *pin = eng_node_param(b->engine, node->first_pin, node->pin_count, (uint32_t)pos);
    nmo_object_t *object = pin != NULL ? eng_find_object(b->engine, pin->id) : NULL;
    const nmo_parameterin_state_t *state = object != NULL && nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN
        ? (const nmo_parameterin_state_t *)nmo_object_get_state(object) : NULL;
    return state == NULL || !state->is_disabled;
}

/* ============================================================================
 * The LOD manager (LODManager)
 * ============================================================================ */

enum { LOD_ALPHA = 1, LOD_MULTI_MESH = 2, LOD_ANIMATION = 4, LOD_PATCH_MESH = 8 };

/* LODOptions as the "LOD Options" parameter holds it */
typedef struct lod_options {
    int32_t flags;
    float screen_mag, screen_min;
    float faces_mag, faces_min;
    float alpha_screen_mag, alpha_screen_min;
} lod_options_t;

/* A LOD entity faded this frame: its draws from `first` go alpha blended at `alpha`; a hidden
   one gets `mesh` back after its render */
typedef struct lod_fade {
    nmo_object_id_t entity;
    size_t first;
    float alpha;
    nmo_object_id_t mesh;
} lod_fade_t;

typedef struct lod_state {
    bool active, build_normals, played;
    float factor;
    ENG_VEC(nmo_object_id_t) entities; /* the entities with the render callbacks */
    ENG_VEC(lod_fade_t) fades;
} lod_state_t;

static void lod_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    lod_state_t *lod = (lod_state_t *)state;
    free(lod->entities.data);
    free(lod->fades.data);
}

static void lod_pre_process(nmo_engine_t *engine, void *state);
static void lod_reset(nmo_engine_t *engine, void *state);

static const eng_manager_desc_t lod_manager = {
    .name = "LOD (Level Of Detail)",
    .state_size = sizeof(lod_state_t),
    .destroy = lod_destroy,
    .pre_process = lod_pre_process,
    .reset = lod_reset,
};

static int32_t lod_attribute(const nmo_engine_t *engine)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, "LOD Object") == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

/* GetLOD: the entity's LOD options; NULL without the attribute */
static lod_options_t *lod_options(nmo_engine_t *engine, nmo_object_id_t entity)
{
    eng_attribute_t *attribute = eng_attribute_find(engine, entity, lod_attribute(engine));
    if (attribute == NULL) {
        return NULL;
    }
    if (attribute->value.size < sizeof(lod_options_t) || attribute->value.data == NULL) {
        /* LODOptionsParameterCreator's defaults */
        const lod_options_t initial = {0, 0.4f, 0.01f, 1.0f, 0.02f, 0.1f, 0.05f};
        if (!eng_param_resize(&attribute->value, sizeof(lod_options_t))) {
            return NULL;
        }
        memcpy(attribute->value.data, &initial, sizeof(initial));
    }
    return (lod_options_t *)attribute->value.data;
}

/* SetObjectLOD: the mesh of the nearest face count above the proportion (Multiple Meshes),
   the character's animation LOD, the fade (Alpha); patch and progressive meshes keep their
   faces in the engine */
static void lod_set_object(nmo_engine_t *engine, lod_state_t *lod, nmo_object_id_t entity, float faces,
                           float alpha, const lod_options_t *options)
{
    if (options->flags & LOD_MULTI_MESH) {
        int32_t count = eng_entity_mesh_count(engine, entity), most = -1;
        for (int32_t i = 0; i < count; i++) {
            const nmo_mesh_state_t *state = mesh_state(engine, eng_entity_mesh_at(engine, entity, i));
            int32_t n = state != NULL ? (int32_t)state->face_count : 0;
            most = n > most ? n : most;
        }
        if (count >= 2) {
            int32_t wanted = (int32_t)((float)most * faces), best = most;
            wanted = wanted > most ? most : wanted;
            nmo_object_id_t chosen = 0;
            for (int32_t i = 0; i < count; i++) {
                nmo_object_id_t mesh = eng_entity_mesh_at(engine, entity, i);
                const nmo_mesh_state_t *state = mesh_state(engine, mesh);
                int32_t delta = (state != NULL ? (int32_t)state->face_count : 0) - wanted;
                if (delta >= 0 && delta < best) {
                    best = delta;
                    chosen = mesh;
                }
            }
            if (chosen != 0) {
                set_current_mesh(engine, entity, chosen, false);
            }
        }
    }
    nmo_object_t *object = eng_find_object(engine, entity);
    if ((options->flags & LOD_ANIMATION) && object != NULL && nmo_object_get_class_id(object) == NMO_CID_CHARACTER) {
        eng_character_set_animation_lod(engine, entity, faces);
    }
    nmo_object_id_t mesh = current_mesh(engine, entity);
    if (mesh == 0 || !(options->flags & LOD_ALPHA) || alpha >= 1.0f) {
        return;
    }
    lod_fade_t *fade = NULL;
    if (!ENG_VEC_PUSH(lod->fades, fade)) {
        return;
    }
    *fade = (lod_fade_t){entity, engine->render.draws.count, alpha, 0};
    if (alpha <= 0.001f) {
        fade->mesh = mesh; /* not drawn this frame */
        set_current_mesh(engine, entity, 0, false);
    }
}

/* LODPreRender: the share of the screen the entity's box takes picks its faces and alpha */
static void lod_pre_render(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)node;
    (void)transparent;
    lod_state_t *lod = (lod_state_t *)eng_manager_state(engine, &lod_manager);
    lod_options_t *options = lod != NULL ? lod_options(engine, entity) : NULL;
    int32_t index = eng_entity_index(engine, entity), camera = eng_entity_index(engine, engine->render.camera);
    float fov = 0.0f;
    if (options == NULL || index < 0 || camera < 0 || !eng_camera_fov(engine, engine->render.camera, &fov)) {
        return;
    }
    eng_vector_t min, max;
    if (!eng_entity_world_box(engine, index, &min, &max)) {
        return;
    }
    float dx = max.x - min.x, dy = max.y - min.y, dz = max.z - min.z, square = dx * dx + dy * dy + dz * dz;
    const eng_matrix_t *cam = &engine->entities.data[camera].world, *at = &engine->entities.data[index].world;
    float z = cam->m[2][0] * (at->m[3][0] - cam->m[3][0]) + cam->m[2][1] * (at->m[3][1] - cam->m[3][1]) +
              cam->m[2][2] * (at->m[3][2] - cam->m[3][2]);
    float width = 2.0f * tanf(0.5f * fov), inv_width2 = 1.0f / (width * width);
    float screen = square / (z * z) * inv_width2, first = screen;
    float smin = options->screen_min * options->screen_min, smag = options->screen_mag * options->screen_mag;
    float faces = options->faces_mag, alpha = 1.0f;
    if (smin < smag) {
        screen = screen < smin ? smin : screen > smag ? smag : screen;
        faces = options->faces_min + (screen - smin) / (smag - smin) * (options->faces_mag - options->faces_min);
    } else if (screen < (smin + smag) * 0.5f) {
        faces = options->faces_min;
    }
    if (options->flags & LOD_ALPHA) {
        float amin = options->alpha_screen_min * options->alpha_screen_min;
        float amag = options->alpha_screen_mag * options->alpha_screen_mag, s = first;
        if (amin < amag) {
            s = first < amin ? amin : first > amag ? amag : first;
            alpha = (s - amin) / (amag - amin);
        } else if (s < (amin + amag) * 0.5f) {
            alpha = 0.0f;
        }
    }
    lod_set_object(engine, lod, entity, faces * lod->factor, alpha * lod->factor, options);
}

/* LODPostRender: the entity's draws of this frame fade (the DLL's material alpha), a hidden one
   gets its mesh back */
static void lod_post_render(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)node;
    (void)transparent;
    lod_state_t *lod = (lod_state_t *)eng_manager_state(engine, &lod_manager);
    for (size_t i = 0; lod != NULL && i < lod->fades.count; i++) {
        lod_fade_t *fade = &lod->fades.data[i];
        if (fade->entity != entity) {
            continue;
        }
        if (fade->mesh != 0) {
            set_current_mesh(engine, entity, fade->mesh, false);
        }
        for (size_t d = fade->first; d < engine->render.draws.count; d++) {
            nmo_engine_draw_t *draw = &engine->render.draws.data[d];
            if (draw->object == entity) {
                draw->fade = true;
                draw->fade_alpha = fade->alpha;
            }
        }
        lod->fades.data[i] = lod->fades.data[--lod->fades.count];
        return;
    }
}

static void lod_attach(nmo_engine_t *engine, lod_state_t *lod, nmo_object_id_t entity, bool on)
{
    eng_entity_callback_t pre = {entity, ENG_ENTITY_PRE_RENDER, lod_pre_render, -1, false};
    eng_entity_callback_t post = {entity, ENG_ENTITY_POST_RENDER, lod_post_render, -1, false};
    if (on) {
        for (size_t i = 0; i < lod->entities.count; i++) {
            if (lod->entities.data[i] == entity) {
                return;
            }
        }
        nmo_object_id_t *slot = NULL;
        if (ENG_VEC_PUSH(lod->entities, slot)) {
            *slot = entity;
            eng_add_entity_callback(engine, &pre);
            eng_add_entity_callback(engine, &post);
        }
        return;
    }
    eng_remove_entity_callback(engine, entity, ENG_ENTITY_PRE_RENDER, lod_pre_render, -1);
    eng_remove_entity_callback(engine, entity, ENG_ENTITY_POST_RENDER, lod_post_render, -1);
    /* RestoreNormalLOD: the most faces, no fade */
    lod_options_t *options = lod_options(engine, entity);
    if (options != NULL) {
        lod_set_object(engine, lod, entity, options->faces_mag, 1.0f, options);
    }
}

/* PreRenderCallbackOfAllLODObjects */
static void lod_attach_all(nmo_engine_t *engine, lod_state_t *lod, bool on)
{
    int32_t type = lod_attribute(engine);
    if (!on) {
        for (size_t i = 0; i < lod->entities.count; i++) {
            lod_attach(engine, lod, lod->entities.data[i], false);
        }
        lod->entities.count = 0;
        return;
    }
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        if (attribute->type == type && eng_entity_index(engine, attribute->object) >= 0) {
            lod_attach(engine, lod, attribute->object, true);
        }
    }
}

/* ActivateLODManager */
static void lod_activate(nmo_engine_t *engine, lod_state_t *lod, bool active)
{
    if (active != lod->active) {
        lod_attach_all(engine, lod, active);
        lod->active = active;
    }
}

/* OnCKPlay from a reset: the defaults, then active */
static void lod_pre_process(nmo_engine_t *engine, void *state)
{
    lod_state_t *lod = (lod_state_t *)state;
    lod->fades.count = 0;
    if (!lod->played) {
        lod->played = true;
        lod->factor = 1.0f;
        lod->build_normals = false;
        lod->active = false;
        lod_activate(engine, lod, true);
    }
}

/* OnCKPostReset: not active; the next play activates it again */
static void lod_reset(nmo_engine_t *engine, void *state)
{
    lod_state_t *lod = (lod_state_t *)state;
    lod_activate(engine, lod, false);
    lod->played = false;
}

/* LOD Manager Options: the enabled inputs (Activate, Global LOD Factor, Build Normals For
   Progressive Meshes) set the manager; the outputs give it. */
static int bb_lod_manager_options(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_t *engine = b->engine;
    lod_state_t *lod = (lod_state_t *)eng_manager_state(engine, &lod_manager);
    if (lod == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_input_parameter_count(b) > 0 && input_enabled(b, 0)) {
        lod_activate(engine, lod, get_in_int(b, 0, 1) != 0);
    }
    if (nmo_engine_block_input_parameter_count(b) > 1 && input_enabled(b, 1)) {
        lod->factor = get_in_float(b, 1, 1.0f);
    }
    if (nmo_engine_block_input_parameter_count(b) > 2 && input_enabled(b, 2)) {
        lod->build_normals = get_in_int(b, 2, 0) != 0;
    }
    const int32_t active = lod->active ? 1 : 0, normals = lod->build_normals ? 1 : 0;
    set_out_int(b, 0, active);
    set_out_float(b, 1, lod->factor);
    set_out_int(b, 2, normals);
    return NMO_ENGINE_BR_OK;
}

/*
 * Set LOD Attribute: the target gets the "LOD Object" attribute (the
 * defaults) when it has none; the enabled inputs set its options (the
 * percentages as fractions). Nothing when the documents have no such
 * attribute type.
 */
static int bb_set_lod_attribute(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    lod_state_t *lod = (lod_state_t *)eng_manager_state(engine, &lod_manager);
    if (entity_state(engine, entity) == NULL || lod == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    if (lod_options(engine, entity) == NULL && nmo_engine_object_set_attribute(engine, entity, lod_attribute(engine)) &&
        lod->active) {
        lod_attach(engine, lod, entity, true); /* the attribute callback at run time */
    }
    lod_options_t *options = lod_options(engine, entity);
    if (options == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    const int32_t bits[4] = {LOD_MULTI_MESH, LOD_ANIMATION, LOD_PATCH_MESH, LOD_ALPHA};
    int pins = nmo_engine_block_input_parameter_count(b);
    for (int i = 0; i < 4 && i < pins; i++) {
        if (input_enabled(b, i)) {
            if (get_in_int(b, i, 0) != 0) {
                options->flags |= bits[i];
            } else {
                options->flags &= ~bits[i];
            }
        }
    }
    float *values[6] = {&options->screen_mag, &options->screen_min, &options->faces_mag,
                        &options->faces_min, &options->alpha_screen_mag, &options->alpha_screen_min};
    const float defaults[6] = {0.4f, 0.01f, 1.0f, 0.03f, 0.1f, 0.05f};
    for (int i = 0; i < 6 && 4 + i < pins; i++) {
        if (input_enabled(b, 4 + i)) {
            *values[i] = get_in_float(b, 4 + i, defaults[i]);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* Get LOD Attribute: the target's LOD options (Found), or Not Found. */
static int bb_get_lod_attribute(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = nmo_engine_block_target(b);
    if (entity_state(engine, entity) == NULL || eng_manager_state(engine, &lod_manager) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    lod_options_t *options = lod_options(engine, entity);
    if (options == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 0);
    const int32_t bits[4] = {LOD_MULTI_MESH, LOD_ANIMATION, LOD_PATCH_MESH, LOD_ALPHA};
    for (int i = 0; i < 4; i++) {
        set_out_int(b, i, (options->flags & bits[i]) ? 1 : 0);
    }
    const float values[6] = {options->screen_mag, options->screen_min, options->faces_mag,
                             options->faces_min, options->alpha_screen_mag, options->alpha_screen_min};
    for (int i = 0; i < 6; i++) {
        set_out_float(b, 4 + i, values[i]);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_mesh_modifier_blocks[] = {
    {NMO_GUID_INIT(0x43890176, 0x58b2244e), "Select Mesh", bb_select_mesh, NULL, NULL},
    {NMO_GUID_INIT(0x7a1d593e, 0x12c5648d), "Add Mesh", bb_add_mesh, NULL, NULL},
    {NMO_GUID_INIT(0x0002145e, 0x6e1b2592), "Remove Mesh", bb_remove_mesh, NULL, NULL},
    {NMO_GUID_INIT(0x5f6428f2, 0x04e55c7a), "Change Referential", bb_change_referential, NULL, NULL},
    {NMO_GUID_INIT(0x31ce61cd, 0x69a44beb), "Invert Winding", bb_invert_winding, NULL, NULL},
    {NMO_GUID_INIT(0x1d8e2716, 0x64f47239), "Set Face Properties", bb_set_face_properties, NULL, NULL},
    {NMO_GUID_INIT(0x696353b2, 0x40667087), "Set Vertex Properties", bb_set_vertex_properties, NULL, NULL},
    {NMO_GUID_INIT(0x45123704, 0x72f25769), "Sine Deform", bb_sine_deform, NULL, NULL},
    {NMO_GUID_INIT(0x3918083f, 0x77d70f5c), "Noise", bb_noise, NULL, NULL},
    {NMO_GUID_INIT(0x0e971a6b, 0x210f703a), "Taper", bb_taper, NULL, NULL},
    {NMO_GUID_INIT(0x75b066af, 0x70371b90), "Twist", bb_twist, NULL, NULL},
    {NMO_GUID_INIT(0x7c9a7008, 0x6b0f1d1f), "Bend", bb_bend, NULL, NULL},
    {NMO_GUID_INIT(0x22d774f6, 0x4ac542c3), "Stretch", bb_stretch, NULL, NULL},
    {NMO_GUID_INIT(0x32145678, 0x32145678), "Texture Displace", bb_texture_displace, NULL, NULL},
    {NMO_GUID_INIT(0x08032ebc, 0x791960c9), "Vertex Translate", bb_vertex_translate, NULL, NULL},
    {NMO_GUID_INIT(0xff0d010a, 0x001d010a), "Mesh Morpher", bb_mesh_morpher, cb_mesh_morpher, NULL},
    {NMO_GUID_INIT(0x53da65c4, 0x6f0f194a), "Multi Mesh Morpher", bb_multi_mesh_morpher, NULL, NULL},
    {NMO_GUID_INIT(0x4bf4352e, 0x6c786db2), "Set Vertex Count", bb_set_vertex_count, NULL, NULL},
    {NMO_GUID_INIT(0x15a14196, 0x69530840), "Set Face Count", bb_set_face_count, NULL, NULL},
    {NMO_GUID_INIT(0x002d010a, 0x003d010a), "Explode", bb_explode, cb_explode, NULL},
    {NMO_GUID_INIT(0x38677c72, 0x4f4715cb), "Skin Join", bb_skin_join, cb_skin_join, NULL},
    {NMO_GUID_INIT(0x053a5be9, 0x78cc3e15), "Precompute Lighting", bb_precompute_lighting, NULL, NULL},
    {NMO_GUID_INIT(0x50c12830, 0x18033a52), "Set PatchMesh Steps", bb_set_patch_mesh_steps, NULL, NULL},
    {NMO_GUID_INIT(0x10357830, 0x18099a52), "Set Progressive Mesh Options", bb_set_progressive_mesh_options, NULL, NULL},
    {NMO_GUID_INIT(0x2b557187, 0x02027baf), "LOD Manager Options", bb_lod_manager_options, NULL, NULL},
    {NMO_GUID_INIT(0x0d360136, 0x63d0633b), "Set LOD Attribute", bb_set_lod_attribute, NULL, NULL},
    {NMO_GUID_INIT(0x62bf39fa, 0x53e264c6), "Get LOD Attribute", bb_get_lod_attribute, NULL, NULL},
};

nmo_status_t eng_register_mesh_modifiers(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &lod_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_mesh_modifier_blocks) : status;
}
