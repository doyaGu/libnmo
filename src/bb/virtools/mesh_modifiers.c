/**
 * @file mesh_modifiers.c
 * @brief Ports of the MeshModifiers building blocks that keep a mesh's vertex
 *        and face counts.
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

typedef struct mm_entity_meshes {
    nmo_object_id_t entity;
    ENG_VEC(nmo_object_id_t) meshes;
} mm_entity_meshes_t;

typedef struct mm_state {
    ENG_VEC(mm_entity_meshes_t) entities;
} mm_state_t;

static void mm_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    mm_state_t *mm = (mm_state_t *)state;
    for (size_t i = 0; i < mm->entities.count; i++) {
        free(mm->entities.data[i].meshes.data);
    }
    free(mm->entities.data);
}

static const eng_manager_desc_t mm_manager = {
    .name = "MeshModifiers Meshes",
    .state_size = sizeof(mm_state_t),
    .destroy = mm_destroy,
};

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

/* The entity's meshes, its file's at first; NULL for no 3D entity or out of memory */
static mm_entity_meshes_t *entity_meshes(nmo_engine_t *engine, nmo_object_id_t entity)
{
    mm_state_t *mm = (mm_state_t *)eng_manager_state(engine, &mm_manager);
    const nmo_3dentity_state_t *state = entity_state(engine, entity);
    if (mm == NULL || state == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < mm->entities.count; i++) {
        if (mm->entities.data[i].entity == entity) {
            return &mm->entities.data[i];
        }
    }
    mm_entity_meshes_t *slot = NULL;
    if (!ENG_VEC_PUSH(mm->entities, slot)) {
        return NULL;
    }
    *slot = (mm_entity_meshes_t){entity, {0}};
    for (uint32_t i = 0; i < state->mesh_count; i++) {
        nmo_object_id_t mesh = eng_object_ref(engine, entity, &state->mesh_ids[i]);
        nmo_object_id_t *item = NULL;
        if (mesh != 0 && ENG_VEC_PUSH(slot->meshes, item)) {
            *item = mesh;
        }
    }
    return slot;
}

/* CK3dEntity::AddMesh: once */
static void add_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh)
{
    mm_entity_meshes_t *list = mesh != 0 ? entity_meshes(engine, entity) : NULL;
    for (size_t i = 0; list != NULL && i < list->meshes.count; i++) {
        if (list->meshes.data[i] == mesh) {
            return;
        }
    }
    nmo_object_id_t *item = NULL;
    if (list != NULL && ENG_VEC_PUSH(list->meshes, item)) {
        *item = mesh;
    }
}

/* CK3dEntity::SetCurrentMesh (a mesh of the entity's document; see eng_entity_set_current_mesh) */
static void set_current_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh, bool add)
{
    if (current_mesh(engine, entity) == mesh) {
        return;
    }
    if (eng_entity_set_current_mesh(engine, entity, mesh) && mesh != 0 && add) {
        add_mesh(engine, entity, mesh);
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
    add_mesh(b->engine, entity, nmo_engine_block_get_input_object(b, 0));
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
    nmo_object_id_t mesh = nmo_engine_block_get_input_object(b, 0);
    mm_entity_meshes_t *list = mesh != 0 ? entity_meshes(engine, entity) : NULL;
    for (size_t i = 0; list != NULL && i < list->meshes.count; i++) {
        if (list->meshes.data[i] != mesh) {
            continue;
        }
        memmove(&list->meshes.data[i], &list->meshes.data[i + 1],
                (list->meshes.count - i - 1) * sizeof(nmo_object_id_t));
        list->meshes.count--;
        if (current_mesh(engine, entity) == mesh) {
            set_current_mesh(engine, entity, list->meshes.count > 0 ? list->meshes.data[0] : 0, false);
        }
        break;
    }
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
    mm_entity_meshes_t *list = entity_meshes(engine, id);
    for (size_t i = 0; list != NULL && i < list->meshes.count; i++) {
        eng_mesh_copy_t *copy = mesh_state(engine, list->meshes.data[i]) != NULL
                                    ? eng_mesh_copy(engine, list->meshes.data[i]) : NULL;
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
};

nmo_status_t eng_register_mesh_modifiers(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &mm_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_mesh_modifier_blocks) : status;
}
