/**
 * @file transform.c
 * @brief The 3D entities' matrices and hierarchy, as CK3dEntity keeps them.
 *
 * Each CK3dEntity of a file the engine runs has a world matrix (the file's)
 * and a local one (relative to its parent), as RCK3dEntity does once a file
 * is loaded. The functions here are RCK3dEntity's (the reference is the
 * Ballanced render engine): changing either matrix updates the other, and
 * the children move along unless asked to keep their place. The matrix
 * functions are VxMath's (Vx3DMultiplyMatrix and the others), whose
 * convention is the one here: rows 0 to 2 hold the X, Y and Z axes, row 3
 * the position, and a point maps to x * row 0 + y * row 1 + z * row 2 +
 * row 3. Nothing is drawn: the matrices are what blocks and operations read.
 */

#include "../engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ENG_EPSILON 1.1920929e-7f /* FLT_EPSILON, VxMath's EPSILON */

/* ============================================================================
 * VxMath
 * ============================================================================ */

void eng_matrix_identity(eng_matrix_t *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1.0f;
}

/* Vx3DMultiplyMatrix: a * b, b applied first; the affine part only. */
void eng_matrix_multiply(eng_matrix_t *result, const eng_matrix_t *a, const eng_matrix_t *b)
{
    eng_matrix_t temp;
    for (int i = 0; i < 4; i++) {
        const double b0 = b->m[i][0], b1 = b->m[i][1], b2 = b->m[i][2];
        for (int j = 0; j < 3; j++) {
            temp.m[i][j] = (float)((double)a->m[0][j] * b0 + (double)a->m[1][j] * b1 +
                                   (double)a->m[2][j] * b2 + (i == 3 ? a->m[3][j] : 0.0));
        }
    }
    temp.m[0][3] = temp.m[1][3] = temp.m[2][3] = 0.0f;
    temp.m[3][3] = 1.0f;
    *result = temp;
}

/* Vx3DInverseMatrix: the inverse of an affine matrix; unchanged when it has none. */
void eng_matrix_inverse(eng_matrix_t *result, const eng_matrix_t *a)
{
    const float(*m)[4] = a->m;
    const double determinant = (double)m[0][0] * ((double)m[1][1] * m[2][2] - (double)m[1][2] * m[2][1]) +
                               (double)m[0][1] * ((double)m[1][2] * m[2][0] - (double)m[1][0] * m[2][2]) +
                               (double)m[0][2] * ((double)m[1][0] * m[2][1] - (double)m[1][1] * m[2][0]);
    if (determinant == 0.0) {
        return;
    }
    double linear[3][3];
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            const int i = (column + 1) % 3, j = (column + 2) % 3;
            const int k = (row + 1) % 3, l = (row + 2) % 3;
            linear[row][column] =
                ((double)m[i][k] * m[j][l] - (double)m[i][l] * m[j][k]) / determinant;
        }
    }
    eng_matrix_t temp;
    eng_matrix_identity(&temp);
    for (int column = 0; column < 3; column++) {
        for (int row = 0; row < 3; row++) {
            temp.m[row][column] = (float)linear[row][column];
        }
        const double translation = m[3][0] * linear[0][column] + m[3][1] * linear[1][column] +
                                   m[3][2] * linear[2][column];
        temp.m[3][column] = (float)-translation;
    }
    *result = temp;
}

/* Vx3DMultiplyMatrixVector: a point through the matrix. */
void eng_vector_transform(eng_vector_t *result, const eng_matrix_t *m, const eng_vector_t *v)
{
    const double x = v->x, y = v->y, z = v->z;
    float out[3];
    for (int c = 0; c < 3; c++) {
        out[c] = (float)(x * m->m[0][c] + y * m->m[1][c] + z * m->m[2][c] + m->m[3][c]);
    }
    result->x = out[0];
    result->y = out[1];
    result->z = out[2];
}

/* Vx3DRotateVector: a direction through the matrix (no translation). */
void eng_vector_rotate(eng_vector_t *result, const eng_matrix_t *m, const eng_vector_t *v)
{
    const double x = v->x, y = v->y, z = v->z;
    float out[3];
    for (int c = 0; c < 3; c++) {
        out[c] = (float)(x * m->m[0][c] + y * m->m[1][c] + z * m->m[2][c]);
    }
    result->x = out[0];
    result->y = out[1];
    result->z = out[2];
}

float eng_vector_magnitude(const eng_vector_t *v)
{
    const double x = v->x, y = v->y, z = v->z;
    return (float)sqrt((x * x + y * y) + z * z);
}

/* VxVector::Normalize: unchanged when it has no length. */
void eng_vector_normalize(eng_vector_t *v)
{
    const double x = v->x, y = v->y, z = v->z;
    const double square = (x * x + y * y) + z * z;
    if (square > 0.0) {
        const double length = sqrt(square);
        v->x = (float)(x / length);
        v->y = (float)(y / length);
        v->z = (float)(z / length);
    }
}

eng_vector_t eng_vector_cross(const eng_vector_t *a, const eng_vector_t *b)
{
    eng_vector_t r = {a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z,
                      a->x * b->y - a->y * b->x};
    return r;
}

static float eng_vector_dot(const eng_vector_t *a, const eng_vector_t *b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

/* Vx3DMatrixFromRotation: a rotation of `angle` radians about `axis`. */
void eng_matrix_from_rotation(eng_matrix_t *result, const eng_vector_t *axis, float angle)
{
    const float s = (float)sin(angle), c = (float)cos(angle);
    const float t = 1.0f - c;
    const double inverse = 1.0 / sqrt((double)axis->x * axis->x + (double)axis->y * axis->y +
                                      (double)axis->z * axis->z);
    const float x = (float)(axis->x * inverse), y = (float)(axis->y * inverse),
                z = (float)(axis->z * inverse);
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xyt = x * y * t, xzt = x * z * t, yzt = y * z * t;
    const float xs = x * s, ys = y * s, zs = z * s;
    eng_matrix_identity(result);
    result->m[0][0] = (1.0f - xx) * c + xx;
    result->m[0][1] = xyt - zs;
    result->m[0][2] = xzt + ys;
    result->m[1][0] = xyt + zs;
    result->m[1][1] = (1.0f - yy) * c + yy;
    result->m[1][2] = yzt - xs;
    result->m[2][0] = xzt - ys;
    result->m[2][1] = yzt + xs;
    result->m[2][2] = (1.0f - zz) * c + zz;
}

/* Vx3DMatrixFromEulerAngles */
void eng_matrix_from_euler(eng_matrix_t *result, float ax, float ay, float az)
{
    float cx = 1.0f, sx = 0.0f, cy = 1.0f, sy = 0.0f, cz = 1.0f, sz = 0.0f;
    if (fabsf(ax) > 1e-8f) {
        sx = (float)sin(ax);
        cx = (float)cos(ax);
    }
    if (fabsf(ay) > 1e-8f) {
        sy = (float)sin(ay);
        cy = (float)cos(ay);
    }
    if (fabsf(az) > 1e-8f) {
        sz = (float)sin(az);
        cz = (float)cos(az);
    }
    const float cxcz = cx * cz, cxsz = cx * sz, sxcz = sx * cz, sxsz = sx * sz;
    eng_matrix_identity(result);
    result->m[0][0] = cz * cy;
    result->m[0][1] = sz * cy;
    result->m[0][2] = -sy;
    result->m[1][0] = sxcz * sy - cxsz;
    result->m[1][1] = sxsz * sy + cxcz;
    result->m[1][2] = sx * cy;
    result->m[2][0] = cxcz * sy + sxsz;
    result->m[2][1] = cxsz * sy - sxcz;
    result->m[2][2] = cx * cy;
}

/* Vx3DMatrixToEulerAngles */
void eng_matrix_to_euler(const eng_matrix_t *m, float *ax, float *ay, float *az)
{
    const float m00 = m->m[0][0], m01 = m->m[0][1], m02 = m->m[0][2];
    const float m12 = m->m[1][2], m22 = m->m[2][2], m21 = m->m[2][1], m11 = m->m[1][1];
    const double magnitude = sqrt((double)m00 * m00 + (double)m01 * m01);
    if (!(magnitude > 16.0f * ENG_EPSILON)) {
        *ax = atan2f(-m21, m11);
        *ay = (float)atan2(-(double)m02, magnitude);
        *az = 0.0f;
    } else {
        *ax = atan2f(m12, m22);
        *ay = (float)atan2(-(double)m02, magnitude);
        *az = atan2f(m01, m00);
    }
}

static eng_vector_t eng_matrix_row(const eng_matrix_t *m, int row)
{
    eng_vector_t v = {m->m[row][0], m->m[row][1], m->m[row][2]};
    return v;
}

static void eng_matrix_set_row(eng_matrix_t *m, int row, const eng_vector_t *v)
{
    m->m[row][0] = v->x;
    m->m[row][1] = v->y;
    m->m[row][2] = v->z;
}

/* ============================================================================
 * Entities
 * ============================================================================ */

static eng_entity_t *eng_entity_at(nmo_engine_t *engine, int32_t index)
{
    return &engine->entities.data[index];
}

int32_t eng_entity_index(const nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = id != 0 ? eng_map_get(&engine->entity_map, id) : -1;
    return index >= 0 && !engine->entities.data[index].destroyed ? index : -1;
}

static int32_t eng_entity_add(nmo_engine_t *engine, nmo_object_id_t id, const float *world,
                              uint32_t moveable_flags)
{
    eng_entity_t *entity = NULL;
    if (!ENG_VEC_PUSH(engine->entities, entity)) {
        return -1;
    }
    int32_t index = (int32_t)engine->entities.count - 1;
    if (!eng_map_put(&engine->entity_map, id, index)) {
        engine->entities.count--;
        return -1;
    }
    entity->id = id;
    if (world != NULL) {
        memcpy(entity->world.m, world, sizeof(entity->world.m));
    } else {
        eng_matrix_identity(&entity->world);
    }
    entity->local = entity->world;
    entity->moveable_flags = moveable_flags;
    entity->parent = entity->first_child = entity->last_child = -1;
    entity->next_sibling = entity->prev_sibling = -1;
    return index;
}

/* The children list: m_Children, in the order the entities became children. */
static void eng_entity_unlink(nmo_engine_t *engine, int32_t index)
{
    eng_entity_t *entity = eng_entity_at(engine, index);
    if (entity->parent < 0) {
        return;
    }
    eng_entity_t *parent = eng_entity_at(engine, entity->parent);
    if (entity->prev_sibling >= 0) {
        eng_entity_at(engine, entity->prev_sibling)->next_sibling = entity->next_sibling;
    } else {
        parent->first_child = entity->next_sibling;
    }
    if (entity->next_sibling >= 0) {
        eng_entity_at(engine, entity->next_sibling)->prev_sibling = entity->prev_sibling;
    } else {
        parent->last_child = entity->prev_sibling;
    }
    entity->parent = entity->next_sibling = entity->prev_sibling = -1;
}

static void eng_entity_link(nmo_engine_t *engine, int32_t index, int32_t parent_index)
{
    eng_entity_t *entity = eng_entity_at(engine, index);
    eng_entity_t *parent = eng_entity_at(engine, parent_index);
    entity->parent = parent_index;
    entity->prev_sibling = parent->last_child;
    entity->next_sibling = -1;
    if (parent->last_child >= 0) {
        eng_entity_at(engine, parent->last_child)->next_sibling = index;
    } else {
        parent->first_child = index;
    }
    parent->last_child = index;
}

/*
 * The 3D entities of a loaded file: the world matrix each was saved with,
 * its parent, and the local matrix that makes (SetParent(parent, TRUE)).
 */
nmo_status_t eng_load_entities(nmo_engine_t *engine, const eng_range_t *range)
{
    const eng_module_t *owner = &engine->modules.data[range->module];
    size_t count = 0;
    nmo_object_t **objects =
        owner->repo != NULL ? nmo_object_repository_get_all(owner->repo, &count) : NULL;
    size_t first = engine->entities.count;
    for (size_t i = 0; objects != NULL && i < count; i++) {
        if (nmo_object_get_id(objects[i]) < range->first ||
            nmo_object_get_id(objects[i]) > range->last) {
            continue;
        }
        const nmo_3dentity_state_t *state =
            (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                engine->types, objects[i], CKPGUID_3DENTITY);
        if (state == NULL) {
            continue;
        }
        nmo_object_id_t id = eng_module_id(owner, nmo_object_get_id(objects[i]));
        if (eng_entity_add(engine, id, state->world_matrix, state->moveable_flags) < 0) {
            return NMO_ERR_NOMEM;
        }
    }
    /* The parents, once every entity of the file is there */
    for (size_t i = 0; objects != NULL && i < count; i++) {
        if (nmo_object_get_id(objects[i]) < range->first ||
            nmo_object_get_id(objects[i]) > range->last) {
            continue;
        }
        const nmo_3dentity_state_t *state =
            (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                engine->types, objects[i], CKPGUID_3DENTITY);
        nmo_object_id_t parent_local = state != NULL ? nmo_ref_runtime_id(&state->parent) : 0u;
        if (parent_local == 0) {
            continue;
        }
        int32_t index = eng_entity_index(engine, eng_module_id(owner, nmo_object_get_id(objects[i])));
        int32_t parent = eng_entity_index(engine, eng_module_id(owner, parent_local));
        if (index >= (int32_t)first && parent >= 0 && parent != index) {
            eng_entity_link(engine, index, parent);
        }
    }
    for (size_t i = first; i < engine->entities.count; i++) {
        eng_entity_t *entity = eng_entity_at(engine, (int32_t)i);
        if (entity->parent >= 0) {
            eng_matrix_t inverse = eng_entity_at(engine, entity->parent)->world;
            eng_matrix_inverse(&inverse, &eng_entity_at(engine, entity->parent)->world);
            eng_matrix_multiply(&entity->local, &inverse, &entity->world);
        }
    }
    return NMO_OK;
}

void eng_free_entities(nmo_engine_t *engine)
{
    free(engine->entities.data);
    free(engine->entity_map.slots);
}

/* A new 3D entity (CKContext::CreateObject): at the origin, without a parent. */
nmo_status_t eng_add_entity(nmo_engine_t *engine, nmo_object_id_t id)
{
    return eng_entity_add(engine, id, NULL, NMO_3DENTITY_CTOR_MOVEABLE_FLAGS) >= 0 ? NMO_OK
                                                                                   : NMO_ERR_NOMEM;
}

static eng_matrix_t eng_entity_inverse(nmo_engine_t *engine, int32_t index)
{
    const eng_matrix_t *world = &eng_entity_at(engine, index)->world;
    eng_matrix_t inverse = *world;
    eng_matrix_inverse(&inverse, world);
    return inverse;
}

static bool eng_follows_parent(const eng_entity_t *child)
{
    return (child->moveable_flags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0;
}

static void eng_entity_local_changed(nmo_engine_t *engine, int32_t index, bool keep_children);

/* The children after the entity's world matrix changed: in place, or moving along. */
static void eng_entity_children_follow(nmo_engine_t *engine, int32_t index, bool keep_children)
{
    if (keep_children) {
        eng_matrix_t inverse = eng_entity_inverse(engine, index);
        for (int32_t c = eng_entity_at(engine, index)->first_child; c >= 0;
             c = eng_entity_at(engine, c)->next_sibling) {
            eng_entity_t *child = eng_entity_at(engine, c);
            if (eng_follows_parent(child)) {
                eng_matrix_multiply(&child->local, &inverse, &child->world);
            }
        }
        return;
    }
    for (int32_t c = eng_entity_at(engine, index)->first_child; c >= 0;
         c = eng_entity_at(engine, c)->next_sibling) {
        if (eng_follows_parent(eng_entity_at(engine, c))) {
            eng_entity_local_changed(engine, c, false);
        }
    }
}

/* WorldMatrixChanged: the local matrix follows, then the children. */
static void eng_entity_world_changed(nmo_engine_t *engine, int32_t index, bool keep_children)
{
    eng_entity_t *entity = eng_entity_at(engine, index);
    if (entity->parent >= 0) {
        eng_matrix_t inverse = eng_entity_inverse(engine, entity->parent);
        eng_matrix_multiply(&entity->local, &inverse, &entity->world);
    } else {
        entity->local = entity->world;
    }
    eng_entity_children_follow(engine, index, keep_children);
}

/* LocalMatrixChanged: the world matrix follows, then the children. */
static void eng_entity_local_changed(nmo_engine_t *engine, int32_t index, bool keep_children)
{
    eng_entity_t *entity = eng_entity_at(engine, index);
    if (entity->parent >= 0) {
        eng_matrix_multiply(&entity->world, &eng_entity_at(engine, entity->parent)->world,
                            &entity->local);
    } else {
        entity->world = entity->local;
    }
    eng_entity_children_follow(engine, index, keep_children);
}

void eng_entity_set_world_matrix(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m,
                                 bool keep_children)
{
    eng_entity_at(engine, index)->world = *m;
    eng_entity_world_changed(engine, index, keep_children);
}

void eng_entity_set_local_matrix(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m,
                                 bool keep_children)
{
    eng_entity_at(engine, index)->local = *m;
    eng_entity_local_changed(engine, index, keep_children);
}

/* Transform: a point of the entity's space in world space (Ref's, given one). */
void eng_entity_transform(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                          const eng_vector_t *src, int32_t ref)
{
    const eng_matrix_t *world = &eng_entity_at(engine, index)->world;
    if (ref >= 0) {
        eng_matrix_t inverse = eng_entity_inverse(engine, ref), m;
        eng_matrix_multiply(&m, &inverse, world);
        eng_vector_transform(dest, &m, src);
    } else {
        eng_vector_transform(dest, world, src);
    }
}

/* InverseTransform: a point of world space (Ref's, given one) in the entity's space. */
void eng_entity_inverse_transform(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                                  const eng_vector_t *src, int32_t ref)
{
    eng_matrix_t inverse = eng_entity_inverse(engine, index);
    if (ref >= 0) {
        eng_matrix_t m;
        eng_matrix_multiply(&m, &inverse, &eng_entity_at(engine, ref)->world);
        eng_vector_transform(dest, &m, src);
    } else {
        eng_vector_transform(dest, &inverse, src);
    }
}

/* TransformVector and InverseTransformVector: the same for a direction. */
void eng_entity_transform_vector(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                                 const eng_vector_t *src, int32_t ref, bool inverse_transform)
{
    eng_matrix_t m;
    if (inverse_transform) {
        m = eng_entity_inverse(engine, index);
        if (ref >= 0) {
            eng_matrix_multiply(&m, &m, &eng_entity_at(engine, ref)->world);
        }
    } else {
        m = eng_entity_at(engine, index)->world;
        if (ref >= 0) {
            eng_matrix_t inverse = eng_entity_inverse(engine, ref);
            eng_matrix_multiply(&m, &inverse, &m);
        }
    }
    eng_vector_rotate(dest, &m, src);
}

/* SetPosition: the world position, from Ref's space given one. */
void eng_entity_set_position(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
                             int32_t ref, bool keep_children)
{
    eng_entity_t *entity = eng_entity_at(engine, index);
    bool parent_relative = ref >= 0 && ref == entity->parent;
    eng_vector_t requested = *position, world;
    if (ref >= 0) {
        eng_entity_transform(engine, ref, &world, position, -1);
    } else {
        world = *position;
    }
    entity = eng_entity_at(engine, index);
    eng_matrix_set_row(&entity->world, 3, &world);
    eng_entity_world_changed(engine, index, keep_children);
    if (parent_relative) {
        /* Keep the requested value through the world matrix's rounding */
        eng_matrix_set_row(&eng_entity_at(engine, index)->local, 3, &requested);
    }
}

/* GetPosition: the world position, or in Ref's space (the local one for the parent). */
eng_vector_t eng_entity_position(nmo_engine_t *engine, int32_t index, int32_t ref)
{
    const eng_entity_t *entity = eng_entity_at(engine, index);
    if (ref < 0) {
        return eng_matrix_row(&entity->world, 3);
    }
    if (ref == entity->parent) {
        return eng_matrix_row(&entity->local, 3);
    }
    eng_vector_t world = eng_matrix_row(&entity->world, 3), position;
    eng_entity_inverse_transform(engine, ref, &position, &world, -1);
    return position;
}

/* Translate: moved by a vector, in Ref's space given one. */
void eng_entity_translate(nmo_engine_t *engine, int32_t index, const eng_vector_t *vector,
                          int32_t ref, bool keep_children)
{
    eng_vector_t move = *vector;
    if (ref >= 0) {
        eng_entity_transform_vector(engine, ref, &move, vector, -1, false);
    }
    eng_entity_t *entity = eng_entity_at(engine, index);
    entity->world.m[3][0] += move.x;
    entity->world.m[3][1] += move.y;
    entity->world.m[3][2] += move.z;
    eng_entity_world_changed(engine, index, keep_children);
}

/* Rotate: about an axis (of Ref's space given one), keeping the position. */
void eng_entity_rotate(nmo_engine_t *engine, int32_t index, const eng_vector_t *axis, float angle,
                       int32_t ref, bool keep_children)
{
    if (fabsf(angle) < ENG_EPSILON) {
        return;
    }
    eng_vector_t world_axis = *axis;
    if (ref >= 0) {
        eng_vector_rotate(&world_axis, &eng_entity_at(engine, ref)->world, axis);
    }
    eng_matrix_t rotation, world;
    eng_matrix_from_rotation(&rotation, &world_axis, angle);
    const eng_matrix_t *current = &eng_entity_at(engine, index)->world;
    eng_vector_t position = eng_matrix_row(current, 3);
    eng_matrix_multiply(&world, &rotation, current);
    eng_matrix_set_row(&world, 3, &position);
    eng_entity_set_world_matrix(engine, index, &world, keep_children);
}

/* GetScale: the length of each axis of the local or world matrix. */
eng_vector_t eng_entity_scale(nmo_engine_t *engine, int32_t index, bool local)
{
    const eng_entity_t *entity = eng_entity_at(engine, index);
    const eng_matrix_t *m = local ? &entity->local : &entity->world;
    eng_vector_t scale;
    eng_vector_t axes[3] = {eng_matrix_row(m, 0), eng_matrix_row(m, 1), eng_matrix_row(m, 2)};
    scale.x = eng_vector_magnitude(&axes[0]);
    scale.y = eng_vector_magnitude(&axes[1]);
    scale.z = eng_vector_magnitude(&axes[2]);
    return scale;
}

static float eng_nonzero(float value)
{
    return value == 0.0f ? 0.000001f : value;
}

/* SetScale: each axis of the local or world matrix to that length (0 is 1e-6). */
void eng_entity_set_scale(nmo_engine_t *engine, int32_t index, const eng_vector_t *scale,
                          bool keep_children, bool local)
{
    const float requested[3] = {eng_nonzero(scale->x), eng_nonzero(scale->y),
                                eng_nonzero(scale->z)};
    eng_entity_t *entity = eng_entity_at(engine, index);
    eng_matrix_t *m = local ? &entity->local : &entity->world;
    for (int axis = 0; axis < 3; axis++) {
        const double x = m->m[axis][0], y = m->m[axis][1], z = m->m[axis][2];
        const double length = sqrt(x * x + y * y + z * z);
        if (length > 0.0) {
            const double factor = requested[axis] / length;
            for (int c = 0; c < 3; c++) {
                m->m[axis][c] = (float)(m->m[axis][c] * factor);
            }
        }
    }
    if (local) {
        eng_entity_local_changed(engine, index, keep_children);
    } else {
        eng_entity_world_changed(engine, index, keep_children);
    }
}

/* AddScale: the local or world matrix scaled along its axes (0 is 1e-6). */
void eng_entity_add_scale(nmo_engine_t *engine, int32_t index, const eng_vector_t *scale,
                          bool keep_children, bool local)
{
    eng_matrix_t scaling, result;
    eng_matrix_identity(&scaling);
    scaling.m[0][0] = eng_nonzero(scale->x);
    scaling.m[1][1] = eng_nonzero(scale->y);
    scaling.m[2][2] = eng_nonzero(scale->z);
    const eng_entity_t *entity = eng_entity_at(engine, index);
    eng_matrix_multiply(&result, local ? &entity->local : &entity->world, &scaling);
    if (local) {
        eng_entity_set_local_matrix(engine, index, &result, keep_children);
    } else {
        eng_entity_set_world_matrix(engine, index, &result, keep_children);
    }
}

/*
 * SetOrientation: the world axes (of Ref's space given one; Right is Up x
 * Dir when not given), each keeping the length it had (0 components are 1e-6).
 */
void eng_entity_set_orientation(nmo_engine_t *engine, int32_t index, const eng_vector_t *dir,
                                const eng_vector_t *up, const eng_vector_t *right, int32_t ref,
                                bool keep_children)
{
    eng_vector_t scale = eng_entity_scale(engine, index, false);
    scale.x = eng_nonzero(scale.x);
    scale.y = eng_nonzero(scale.y);
    scale.z = eng_nonzero(scale.z);
    eng_vector_t axes[3]; /* right, up, dir */
    if (ref >= 0) {
        const eng_matrix_t *world = &eng_entity_at(engine, ref)->world;
        eng_vector_rotate(&axes[2], world, dir);
        eng_vector_rotate(&axes[1], world, up);
        if (right != NULL) {
            eng_vector_rotate(&axes[0], world, right);
        } else {
            axes[0] = eng_vector_cross(&axes[1], &axes[2]);
        }
    } else {
        axes[2] = *dir;
        axes[1] = *up;
        axes[0] = right != NULL ? *right : eng_vector_cross(up, dir);
    }
    const float lengths[3] = {scale.x, scale.y, scale.z};
    eng_entity_t *entity = eng_entity_at(engine, index);
    for (int a = 0; a < 3; a++) {
        eng_vector_t axis = {eng_nonzero(axes[a].x), eng_nonzero(axes[a].y), eng_nonzero(axes[a].z)};
        const float inverse = (float)(1.0 / (double)eng_vector_magnitude(&axis));
        axis.x = axis.x * inverse * lengths[a];
        axis.y = axis.y * inverse * lengths[a];
        axis.z = axis.z * inverse * lengths[a];
        eng_matrix_set_row(&entity->world, a, &axis);
    }
    eng_entity_world_changed(engine, index, keep_children);
}

/* GetOrientation: the unit axes in world space, or in Ref's. */
void eng_entity_orientation(nmo_engine_t *engine, int32_t index, eng_vector_t *dir,
                            eng_vector_t *up, eng_vector_t *right, int32_t ref)
{
    eng_matrix_t m = eng_entity_at(engine, index)->world;
    if (ref >= 0) {
        eng_matrix_t inverse = eng_entity_inverse(engine, ref);
        eng_matrix_multiply(&m, &inverse, &m);
    }
    eng_vector_t *axes[3] = {right, up, dir};
    for (int a = 0; a < 3; a++) {
        if (axes[a] != NULL) {
            *axes[a] = eng_matrix_row(&m, a);
            eng_vector_normalize(axes[a]);
        }
    }
}

/*
 * LookAt (CK3dEntity::LookAt): the Z axis toward a point (of Ref's space),
 * the world's Y axis up, nothing when the point is where the entity is.
 */
void eng_entity_look_at(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
                        int32_t ref, bool keep_children)
{
    const eng_matrix_t *world = &eng_entity_at(engine, index)->world;
    eng_vector_t here = eng_matrix_row(world, 3), target = *position;
    if (ref >= 0) {
        eng_entity_transform(engine, ref, &target, position, -1);
    }
    eng_vector_t dir = {target.x - here.x, target.y - here.y, target.z - here.z};
    const float length = eng_vector_magnitude(&dir);
    if (length < ENG_EPSILON) {
        return;
    }
    eng_vector_t current_dir = eng_matrix_row(world, 2);
    eng_vector_t cross = eng_vector_cross(&dir, &current_dir);
    if (eng_vector_dot(&cross, &cross) == 0.0f) {
        return;
    }
    dir.x *= 1.0f / length;
    dir.y *= 1.0f / length;
    dir.z *= 1.0f / length;
    const eng_vector_t axis_y = {0.0f, 1.0f, 0.0f};
    eng_vector_t right = eng_vector_cross(&axis_y, &dir);
    eng_vector_t up = eng_vector_cross(&dir, &right);
    if (fabsf(up.y) == 0.0f) {
        return;
    }
    eng_vector_t old_up = eng_matrix_row(world, 1);
    if (eng_vector_dot(&up, &old_up) < 0.0f) {
        right.x = -right.x, right.y = -right.y, right.z = -right.z;
        up.x = -up.x, up.y = -up.y, up.z = -up.z;
    }
    eng_entity_set_orientation(engine, index, &dir, &up, &right, -1, keep_children);
}

/* The entity's current mesh, as the file has it; NULL for none. */
static const nmo_mesh_state_t *eng_entity_mesh(nmo_engine_t *engine, int32_t index,
                                               const eng_module_t **out_module)
{
    nmo_object_id_t id = eng_entity_at(engine, index)->id;
    nmo_object_t *object = eng_find_object(engine, id);
    const nmo_3dentity_state_t *state =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    nmo_object_id_t mesh_local = state != NULL ? nmo_ref_runtime_id(&state->current_mesh) : 0u;
    const eng_module_t *module = eng_module_of(engine, id, NULL);
    nmo_object_t *mesh =
        mesh_local != 0 ? eng_find_object(engine, eng_module_id(module, mesh_local)) : NULL;
    if (out_module != NULL) {
        *out_module = module;
    }
    return mesh != NULL ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                              engine->types, mesh, CKPGUID_MESH)
                        : NULL;
}

/* CK3dEntity::GetCurrentMesh() != NULL */
bool eng_entity_has_mesh(nmo_engine_t *engine, int32_t index)
{
    return eng_entity_mesh(engine, index, NULL) != NULL;
}

/*
 * GetBaryCenter: the barycenter of the current mesh (the mean of its
 * vertices, as loaded) in world space; the position for an entity without
 * a mesh, and false then.
 */
bool eng_entity_bary_center(nmo_engine_t *engine, int32_t index, eng_vector_t *out)
{
    const nmo_mesh_state_t *mesh_state = eng_entity_mesh(engine, index, NULL);
    if (mesh_state == NULL) {
        *out = eng_matrix_row(&eng_entity_at(engine, index)->world, 3);
        return false;
    }
    eng_vector_t local = {mesh_state->bary_center.x, mesh_state->bary_center.y,
                          mesh_state->bary_center.z};
    eng_entity_transform(engine, index, out, &local, -1);
    return true;
}

/* GetRadius: the mesh's radius times the largest world axis scale; 0 without a mesh. */
float eng_entity_radius(nmo_engine_t *engine, int32_t index)
{
    const nmo_mesh_state_t *mesh = eng_entity_mesh(engine, index, NULL);
    if (mesh == NULL) {
        return 0.0f; /* the world box of an entity without a mesh is not valid */
    }
    const eng_matrix_t *world = &eng_entity_at(engine, index)->world;
    float scale[3];
    for (int row = 0; row < 3; row++) {
        eng_vector_t axis = eng_matrix_row(world, row);
        scale[row] = eng_vector_magnitude(&axis) * mesh->radius;
    }
    float yz = scale[1] < scale[2] ? scale[2] : scale[1];
    return scale[0] < yz ? yz : scale[0];
}

/*
 * The local box (GetBoundingBox(TRUE)): the current mesh's vertices' extent;
 * all zero, and false, without a mesh.
 *
 * The world box (UpdateBox): the mesh's local box (its vertices' extent)
 * through the world matrix as VxBbox::TransformFrom has it; false without a
 * mesh.
 */
bool eng_entity_local_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min,
                          eng_vector_t *max)
{
    const nmo_mesh_state_t *mesh = eng_entity_mesh(engine, index, NULL);
    float low[3] = {0.0f, 0.0f, 0.0f}, high[3] = {0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; mesh != NULL && i < mesh->vertex_count; i++) {
        const float p[3] = {mesh->vertices[i].position.x, mesh->vertices[i].position.y,
                            mesh->vertices[i].position.z};
        for (int k = 0; k < 3; k++) {
            if (i == 0 || low[k] > p[k]) {
                low[k] = p[k];
            }
            if (i == 0 || high[k] < p[k]) {
                high[k] = p[k];
            }
        }
    }
    *min = (eng_vector_t){low[0], low[1], low[2]};
    *max = (eng_vector_t){high[0], high[1], high[2]};
    return mesh != NULL;
}

bool eng_entity_world_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min,
                          eng_vector_t *max)
{
    eng_vector_t local_min, local_max;
    if (!eng_entity_local_box(engine, index, &local_min, &local_max)) {
        return false;
    }
    const float low[3] = {local_min.x, local_min.y, local_min.z};
    const float high[3] = {local_max.x, local_max.y, local_max.z};
    const eng_matrix_t *m = &eng_entity_at(engine, index)->world;
    eng_vector_t center = {(low[0] + high[0]) * 0.5f, (low[1] + high[1]) * 0.5f,
                           (low[2] + high[2]) * 0.5f};
    eng_vector_t c;
    eng_vector_transform(&c, m, &center);
    float half[3];
    for (int k = 0; k < 3; k++) {
        half[k] = (fabsf((high[0] - low[0]) * m->m[0][k]) + fabsf((high[1] - low[1]) * m->m[1][k]) +
                   fabsf((high[2] - low[2]) * m->m[2][k])) *
                  0.5f;
    }
    *min = (eng_vector_t){c.x - half[0], c.y - half[1], c.z - half[2]};
    *max = (eng_vector_t){c.x + half[0], c.y + half[1], c.z + half[2]};
    return true;
}

/* VxIntersect::RayBox: the half-line from `origin` along `dir` meets the box. */
bool eng_ray_box(const eng_vector_t *origin, const eng_vector_t *dir, const eng_vector_t *min,
                 const eng_vector_t *max)
{
    const float o[3] = {origin->x, origin->y, origin->z}, d[3] = {dir->x, dir->y, dir->z};
    const float lo[3] = {min->x, min->y, min->z}, hi[3] = {max->x, max->y, max->z};
    double near_t = 0.0, far_t = 1.7976931348623157e308;
    for (int k = 0; k < 3; k++) {
        if (d[k] == 0.0f) {
            if (o[k] < lo[k] || o[k] > hi[k]) {
                return false;
            }
            continue;
        }
        double a = ((double)lo[k] - o[k]) / d[k], b = ((double)hi[k] - o[k]) / d[k];
        double first = a < b ? a : b, last = a < b ? b : a;
        if (first > near_t) {
            near_t = first;
        }
        if (last < far_t) {
            far_t = last;
        }
    }
    return near_t <= far_t;
}

/* VxIntersect::PointInFace: the projection axes of the face's dominant normal; 0 outside. */
static int eng_point_in_face(const float *p, const float *a, const float *b, const float *c,
                             const nmo_vector_t *n, int *i1, int *i2)
{
    float largest = fabsf(n->x);
    *i1 = 1;
    *i2 = 2;
    if (largest < fabsf(n->y)) {
        *i1 = 0;
        *i2 = 2;
        largest = fabsf(n->y);
    }
    if (largest < fabsf(n->z)) {
        *i1 = 0;
        *i2 = 1;
    }
    const int u = *i1, v = *i2;
    const float *vertices[3] = {a, b, c};
    bool positive = false, negative = false;
    for (int edge = 0; edge < 3; edge++) {
        const float *s = vertices[edge], *e = vertices[(edge + 1) % 3];
        double side = ((double)p[u] - s[u]) * ((double)e[v] - s[v]) -
                      ((double)p[v] - s[v]) * ((double)e[u] - s[u]);
        positive = positive || side > 0.0;
        negative = negative || side < 0.0;
        if (positive && negative) {
            return 0;
        }
    }
    if (positive) {
        return 2;
    }
    if (negative) {
        return 1;
    }
    /* collinear: inside the projected extent */
    for (int k = 0; k < 2; k++) {
        int axis = k == 0 ? u : v;
        float lo = fminf(a[axis], fminf(b[axis], c[axis])), hi = fmaxf(a[axis], fmaxf(b[axis], c[axis]));
        if (p[axis] < lo || p[axis] > hi) {
            return 0;
        }
    }
    return 2;
}

/* Whether the material of a material group is two-sided (CKMaterial::IsTwoSided) */
static bool eng_group_two_sided(nmo_engine_t *engine, const eng_module_t *module,
                                const nmo_mesh_state_t *mesh, uint32_t group)
{
    if (group >= mesh->material_group_count) {
        return false;
    }
    nmo_object_id_t local = nmo_ref_runtime_id(&mesh->material_groups[group].material);
    nmo_object_t *material = local != 0 ? eng_find_object(engine, eng_module_id(module, local)) : NULL;
    if (material == NULL || nmo_object_get_class_id(material) != NMO_CID_MATERIAL) {
        return false;
    }
    const nmo_material_state_t *state = (const nmo_material_state_t *)nmo_object_get_state(material);
    return state != NULL && (state->packed_flags & 1u) != 0;
}

/*
 * CK3dEntity::RayIntersection (the default options: a ray, the nearest
 * face): the ray from `pos1` through `pos2` (world space) on the current
 * mesh in the entity's space, faces culled from behind unless their
 * material is two-sided (the face normals built from the vertices, as CK
 * builds them after a load). The hit is in the entity's space, with the vertex
 * normals interpolated there. The number of faces the ray meets; 0 without
 * a mesh. Texture transparency (PreciseTexturePick) is not tested.
 */
int eng_entity_ray_intersection(nmo_engine_t *engine, int32_t index, const eng_vector_t *pos1,
                                const eng_vector_t *pos2, eng_ray_hit_t *hit)
{
    const eng_module_t *module = NULL;
    const nmo_mesh_state_t *mesh = eng_entity_mesh(engine, index, &module);
    if (mesh == NULL || mesh->face_count == 0 || mesh->vertex_count == 0) {
        return 0;
    }
    eng_vector_t origin, end;
    eng_entity_inverse_transform(engine, index, &origin, pos1, -1);
    eng_entity_inverse_transform(engine, index, &end, pos2, -1);
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {end.x - origin.x, end.y - origin.y, end.z - origin.z};
    int hits = 0, best_i1 = 0, best_i2 = 0;
    uint32_t best_face = 0;
    float best = 1.0e35f;
    bool found = false;
    for (uint32_t f = 0; f < mesh->face_count; f++) {
        const uint16_t *v = &mesh->face_vertex_indices[3u * f];
        if (v[0] >= mesh->vertex_count || v[1] >= mesh->vertex_count || v[2] >= mesh->vertex_count) {
            continue;
        }
        const nmo_vector_t *p0 = &mesh->vertices[v[0]].position, *p1 = &mesh->vertices[v[1]].position,
                           *p2 = &mesh->vertices[v[2]].position;
        /* RCKMesh::BuildFaceNormals, as after a load: the files save none */
        eng_vector_t e1 = {p1->x - p0->x, p1->y - p0->y, p1->z - p0->z};
        eng_vector_t e2 = {p2->x - p0->x, p2->y - p0->y, p2->z - p0->z};
        eng_vector_t cross = eng_vector_cross(&e1, &e2);
        float length = eng_vector_magnitude(&cross);
        if (length > 0.0f) {
            float inverse = 1.0f / length;
            cross = (eng_vector_t){cross.x * inverse, cross.y * inverse, cross.z * inverse};
        }
        const nmo_vector_t normal = {cross.x, cross.y, cross.z};
        const nmo_vector_t *n = &normal;
        bool culled = !eng_group_two_sided(engine, module, mesh, mesh->faces[f].material_group_idx);
        double denominator = (double)n->x * d[0] + (double)n->y * d[1] + (double)n->z * d[2];
        if (culled ? denominator > -ENG_EPSILON : fabs(denominator) < ENG_EPSILON) {
            continue;
        }
        double numerator = (double)n->x * ((double)p0->x - o[0]) +
                           (double)n->y * ((double)p0->y - o[1]) + (double)n->z * ((double)p0->z - o[2]);
        double t = numerator / denominator;
        if (t < -ENG_EPSILON) {
            continue;
        }
        const float point[3] = {(float)(o[0] + d[0] * t), (float)(o[1] + d[1] * t),
                                (float)(o[2] + d[2] * t)};
        const float a[3] = {p0->x, p0->y, p0->z}, b[3] = {p1->x, p1->y, p1->z},
                    c[3] = {p2->x, p2->y, p2->z};
        int i1 = 0, i2 = 0;
        if (eng_point_in_face(point, a, b, c, n, &i1, &i2) == 0) {
            continue;
        }
        hits++;
        if ((float)t < best) {
            found = true;
            best = (float)t;
            best_face = f;
            best_i1 = i1;
            best_i2 = i2;
        }
    }
    if (found && hit != NULL) {
        const uint16_t *v = &mesh->face_vertex_indices[3u * best_face];
        const nmo_vertex_t *a = &mesh->vertices[v[0]], *b = &mesh->vertices[v[1]],
                           *c = &mesh->vertices[v[2]];
        const float p[3] = {d[0] * best + o[0], d[1] * best + o[1], d[2] * best + o[2]};
        const float pa[3] = {a->position.x, a->position.y, a->position.z};
        const float pb[3] = {b->position.x, b->position.y, b->position.z};
        const float pc[3] = {c->position.x, c->position.y, c->position.z};
        /* VxIntersect::GetPointCoefficients: each weight its opposite signed area */
        const double ax = pa[best_i1], ay = pa[best_i2], bx = pb[best_i1], by = pb[best_i2];
        const double cx = pc[best_i1], cy = pc[best_i2], px = p[best_i1], py = p[best_i2];
        const double det = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        const float w0 = (float)(((bx - px) * (cy - py) - (by - py) * (cx - px)) / det);
        const float w1 = (float)(((px - ax) * (cy - ay) - (py - ay) * (cx - ax)) / det);
        const float w2 = (float)(((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / det);
        hit->point = (eng_vector_t){p[0], p[1], p[2]};
        hit->normal = (eng_vector_t){w0 * a->normal.x + w1 * b->normal.x + w2 * c->normal.x,
                                     w0 * a->normal.y + w1 * b->normal.y + w2 * c->normal.y,
                                     w0 * a->normal.z + w1 * b->normal.z + w2 * c->normal.z};
        hit->face = (int32_t)best_face;
        hit->vertices[0] = v[0];
        hit->vertices[1] = v[1];
        hit->vertices[2] = v[2];
        for (int k = 0; k < 3; k++) {
            const nmo_vector_t *q = &mesh->vertices[v[k]].position;
            hit->corners[k] = (eng_vector_t){q->x, q->y, q->z};
        }
    }
    return found ? hits : 0;
}

static bool eng_is_place(const nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t class_id = nmo_engine_object_class(engine, id);
    return class_id != 0 &&
           nmo_type_registry_is_class_derived_from(engine->types, (uint32_t)class_id, NMO_CID_PLACE);
}

/*
 * SetParent: false for itself, a descendant, or a place under a place;
 * the world matrix kept (the local one follows), or the local one.
 */
bool eng_entity_set_parent(nmo_engine_t *engine, int32_t index, int32_t parent,
                           bool keep_world_position)
{
    if (parent == index) {
        return false;
    }
    if (eng_entity_at(engine, index)->parent == parent) {
        return true;
    }
    if (parent >= 0 && eng_is_place(engine, eng_entity_at(engine, index)->id) &&
        eng_is_place(engine, eng_entity_at(engine, parent)->id)) {
        return false;
    }
    for (int32_t a = parent >= 0 ? eng_entity_at(engine, parent)->parent : -1; a >= 0;
         a = eng_entity_at(engine, a)->parent) {
        if (a == index) {
            return false;
        }
    }
    eng_entity_unlink(engine, index);
    if (parent >= 0) {
        eng_entity_link(engine, index, parent);
    }
    if (keep_world_position) {
        eng_entity_t *entity = eng_entity_at(engine, index);
        if (parent >= 0) {
            eng_matrix_t inverse = eng_entity_inverse(engine, parent);
            eng_matrix_multiply(&entity->local, &inverse, &entity->world);
        } else {
            entity->local = entity->world;
        }
    } else {
        eng_entity_local_changed(engine, index, false);
    }
    return true;
}

/* CKContext::DestroyObject of an entity: its children keep their place without it. */
void eng_entity_destroy(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_entity_index(engine, id);
    if (index < 0) {
        return;
    }
    while (eng_entity_at(engine, index)->first_child >= 0) {
        eng_entity_set_parent(engine, eng_entity_at(engine, index)->first_child, -1, true);
    }
    eng_entity_unlink(engine, index);
    eng_entity_at(engine, index)->destroyed = true;
}

/* ============================================================================
 * The host's view
 * ============================================================================ */

bool nmo_engine_entity_world_matrix(const nmo_engine_t *engine, nmo_object_id_t entity_id,
                                    float out_matrix[16])
{
    int32_t index = engine != NULL ? eng_entity_index(engine, entity_id) : -1;
    if (index < 0) {
        return false;
    }
    memcpy(out_matrix, engine->entities.data[index].world.m, 16 * sizeof(float));
    return true;
}

bool nmo_engine_entity_local_matrix(const nmo_engine_t *engine, nmo_object_id_t entity_id,
                                    float out_matrix[16])
{
    int32_t index = engine != NULL ? eng_entity_index(engine, entity_id) : -1;
    if (index < 0) {
        return false;
    }
    memcpy(out_matrix, engine->entities.data[index].local.m, 16 * sizeof(float));
    return true;
}

nmo_object_id_t nmo_engine_entity_parent(const nmo_engine_t *engine, nmo_object_id_t entity_id)
{
    int32_t index = engine != NULL ? eng_entity_index(engine, entity_id) : -1;
    int32_t parent = index >= 0 ? engine->entities.data[index].parent : -1;
    return parent >= 0 ? engine->entities.data[parent].id : 0;
}

nmo_status_t nmo_engine_set_entity_world_matrix(nmo_engine_t *engine, nmo_object_id_t entity_id,
                                                const float matrix[16], bool keep_children)
{
    int32_t index = engine != NULL ? eng_entity_index(engine, entity_id) : -1;
    if (index < 0 || matrix == NULL) {
        return NMO_ERR_NOT_FOUND;
    }
    eng_matrix_t m;
    memcpy(m.m, matrix, sizeof(m.m));
    eng_entity_set_world_matrix(engine, index, &m, keep_children);
    return NMO_OK;
}
