/**
 * @file floor.c
 * @brief The floor manager (FloorManager of Collisions.dll).
 *
 * Floors are the 3D entities with the Floor attribute, in the order they got
 * it. GetNearestFloors casts a vertical line through each floor whose world
 * box is near the point: through its faces (not steeper than the limit
 * angle) in its own space, or through its local box. ConstrainToFloor, which
 * Ballance's DLL does not have yet, is the later SDK's (Ballanced's source):
 * it pushes a point off the border edges of the floor it is over.
 */

#include "../engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FLOOR_EPSILON 1.1920929e-7f
#define FLOOR_FAR 100000000.0f

void eng_floors_init(nmo_engine_t *engine)
{
    /* FloorManager's constructor: 80 degrees */
    engine->floors.limit_angle = 1.3962635f;
    engine->floors.cos_angle = (float)cos(1.396263480186462);
    engine->floors.cache_size = 8;
    engine->floors.cache_threshold = 0.1f;
}

void eng_free_floors(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->floors.edges.count; i++) {
        free(engine->floors.edges.data[i].edges);
    }
    free(engine->floors.edges.data);
}

int32_t eng_floor_attribute(const nmo_engine_t *engine)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, "Floor") == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

void eng_floor_set_limit_angle(nmo_engine_t *engine, float angle)
{
    if (angle > 1.5707964f) {
        angle = 1.5707964f;
    } else if (angle < 0.0f) {
        angle = 0.0f;
    }
    engine->floors.limit_angle = angle;
    engine->floors.cos_angle = (float)cos(angle);
}

void eng_floor_point_clear(eng_floor_point_t *point)
{
    memset(point, 0, sizeof(*point));
    point->up_face = -1;
    point->down_face = -1;
    point->up_distance = FLOOR_FAR;
    point->down_distance = -FLOOR_FAR;
}

bool eng_floor_read(nmo_engine_t *engine, nmo_object_id_t object, eng_floor_settings_t *settings)
{
    const eng_attribute_t *attribute = object != 0 ? eng_attribute_find(engine, object, eng_floor_attribute(engine)) : NULL;
    if (attribute == NULL) {
        return false;
    }
    int32_t *fields[5] = {&settings->geometry, &settings->moving, &settings->type, &settings->hierarchy,
                          &settings->first};
    if (attribute->fields != NULL) {
        for (uint32_t i = 0; i < 5 && i < attribute->field_count; i++) {
            *fields[i] = attribute->fields[i];
        }
        return true;
    }
    if (!attribute->has_value || !nmo_guid_equals(attribute->value.type, CKPGUID_FLOOR)) {
        return true;
    }
    /* Set at run time: CK makes the fields with the attribute's default, "0;TRUE;0;FALSE;TRUE" */
    bool made = true;
    for (uint32_t i = 0; i < 5 && made; i++) {
        uint32_t local = 0;
        if (attribute->value.size >= 4u * (i + 1u)) {
            memcpy(&local, attribute->value.data + 4u * i, 4);
        }
        made = local == 0;
    }
    if (made) {
        const eng_floor_settings_t defaults = {CKFLOOR_FACES, 1, 0, 0, 1};
        *settings = defaults;
        return true;
    }
    for (uint32_t i = 0; i < 5; i++) {
        eng_struct_field(engine, object, attribute->value.data, attribute->value.size, i, fields[i]);
    }
    return true;
}

void eng_floor_add(nmo_engine_t *engine, nmo_object_id_t object, const eng_floor_settings_t *settings)
{
    int32_t type = eng_floor_attribute(engine);
    nmo_engine_object_set_attribute(engine, object, type);
    eng_attribute_t *attribute = eng_attribute_find(engine, object, type);
    if (attribute == NULL || !attribute->has_value) {
        return;
    }
    if (attribute->fields == NULL) {
        attribute->fields = (int32_t *)malloc(5 * sizeof(int32_t));
        if (attribute->fields == NULL) {
            return;
        }
        attribute->field_count = 5;
    }
    const int32_t values[5] = {settings->geometry, settings->moving, settings->type, settings->hierarchy,
                               settings->first};
    memcpy(attribute->fields, values, sizeof(values));
}

/* ============================================================================
 * The floors' geometry
 * ============================================================================ */

/* The entity's current mesh as it is now; false for none */
static bool floor_mesh(nmo_engine_t *engine, nmo_object_id_t floor, nmo_object_id_t *id, nmo_engine_mesh_t *mesh)
{
    const nmo_3dentity_state_t *state =
        (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, eng_find_object(engine, floor), CKPGUID_3DENTITY);
    *id = state != NULL ? eng_object_ref(engine, floor, &state->current_mesh) : 0;
    return *id != 0 && nmo_engine_mesh(engine, *id, mesh);
}

static eng_vector_t mesh_point(const nmo_engine_mesh_t *mesh, int32_t face, int k)
{
    const nmo_vector_t *p = &mesh->vertices[mesh->indices[face * 3 + k]].position;
    return (eng_vector_t){p->x, p->y, p->z};
}

/* CKMesh::GetFaceNormal: the one the mesh keeps, built from the face when it keeps none */
static eng_vector_t mesh_face_normal(const nmo_engine_mesh_t *mesh, int32_t face)
{
    if (mesh->faces != NULL) {
        const nmo_vector_t *n = &mesh->faces[face].normal;
        if (n->x != 0.0f || n->y != 0.0f || n->z != 0.0f) {
            return (eng_vector_t){n->x, n->y, n->z};
        }
    }
    eng_vector_t p0 = mesh_point(mesh, face, 0), p1 = mesh_point(mesh, face, 1), p2 = mesh_point(mesh, face, 2);
    eng_vector_t e1 = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, e2 = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    eng_vector_t n = eng_vector_cross(&e1, &e2);
    eng_vector_normalize(&n);
    return n;
}

static float axis_of(const eng_vector_t *v, int axis)
{
    return axis == 0 ? v->x : axis == 1 ? v->y : v->z;
}

static void min_max3(float a, float b, float c, float *min, float *max)
{
    *min = fminf(a, fminf(b, c));
    *max = fmaxf(a, fmaxf(b, c));
}

/* VxIntersect::LinePlane, the plane through `point` of normal `n` */
static bool line_plane(eng_vector_t origin, eng_vector_t dir, eng_vector_t n, eng_vector_t point, eng_vector_t *out)
{
    double denominator = (double)n.x * dir.x + (double)n.y * dir.y + (double)n.z * dir.z;
    if (fabs(denominator) < FLOOR_EPSILON) {
        return false;
    }
    float d = -(n.x * point.x + n.y * point.y + n.z * point.z); /* VxPlane::Create */
    double t = -((double)n.x * origin.x + (double)n.y * origin.y + (double)n.z * origin.z + d) / denominator;
    *out = (eng_vector_t){(float)(origin.x + (double)dir.x * t), (float)(origin.y + (double)dir.y * t),
                          (float)(origin.z + (double)dir.z * t)};
    return true;
}

/* VxIntersect::LineFace: the line meets the face's plane inside the face */
static bool line_face(eng_vector_t origin, eng_vector_t dir, eng_vector_t a, eng_vector_t b, eng_vector_t c,
                      eng_vector_t n, eng_vector_t *out, float *dist)
{
    double denominator = (double)n.x * dir.x + (double)n.y * dir.y + (double)n.z * dir.z;
    if (fabs(denominator) < FLOOR_EPSILON) {
        return false;
    }
    double numerator = (double)n.x * ((double)a.x - origin.x) + (double)n.y * ((double)a.y - origin.y) +
                       (double)n.z * ((double)a.z - origin.z);
    double t = numerator / denominator;
    *dist = (float)t;
    *out = (eng_vector_t){(float)(origin.x + (double)dir.x * t), (float)(origin.y + (double)dir.y * t),
                          (float)(origin.z + (double)dir.z * t)};
    const float p[3] = {out->x, out->y, out->z}, va[3] = {a.x, a.y, a.z}, vb[3] = {b.x, b.y, b.z},
                vc[3] = {c.x, c.y, c.z};
    const nmo_vector_t normal = {n.x, n.y, n.z};
    int i1, i2;
    return eng_point_in_face(p, va, vb, vc, &normal, &i1, &i2) != 0;
}

/* VxIntersect::LineBox: the entry point and normal of the line through the box */
static bool line_box(eng_vector_t origin, eng_vector_t dir, eng_vector_t min, eng_vector_t max, eng_vector_t *in,
                     eng_vector_t *normal)
{
    double near_t = -1.7976931348623157e308, far_t = 1.7976931348623157e308;
    int near_axis = 0, far_axis = 0;
    bool active = false;
    for (int i = 0; i < 3; i++) {
        float d = axis_of(&dir, i), o = axis_of(&origin, i), lo = axis_of(&min, i), hi = axis_of(&max, i);
        if (fabsf(d) < FLOOR_EPSILON) {
            if (o < lo || o > hi) {
                return false;
            }
            continue;
        }
        active = true;
        double t1 = ((double)lo - o) / d, t2 = ((double)hi - o) / d;
        if (t1 > t2) {
            double t = t1;
            t1 = t2;
            t2 = t;
        }
        if (t1 > near_t) {
            near_t = t1;
            near_axis = i;
        }
        if (t2 < far_t) {
            far_t = t2;
            far_axis = i;
        }
        if (near_t > far_t) {
            return false;
        }
    }
    if (!active) {
        *in = origin;
        *normal = (eng_vector_t){0.0f, 0.0f, 0.0f};
        return true;
    }
    bool near_max = axis_of(&dir, near_axis) < 0.0f, far_max = axis_of(&dir, far_axis) > 0.0f;
    int near_rank = 2 * near_axis + near_max, far_rank = 2 * far_axis + far_max;
    if (near_t < 0.0 && (far_t <= 0.0 || fabs(far_t) < fabs(near_t) ||
                         (fabs(far_t) == fabs(near_t) && far_rank < near_rank))) {
        near_t = far_t;
        near_axis = far_axis;
        near_max = far_max;
    }
    float p[3];
    for (int i = 0; i < 3; i++) {
        p[i] = (float)((double)axis_of(&origin, i) + (double)axis_of(&dir, i) * near_t);
    }
    p[near_axis] = near_max ? axis_of(&max, near_axis) : axis_of(&min, near_axis);
    *in = (eng_vector_t){p[0], p[1], p[2]};
    float n[3] = {0.0f, 0.0f, 0.0f};
    n[near_axis] = axis_of(&dir, near_axis) > 0.0f ? -1.0f : 1.0f;
    *normal = (eng_vector_t){n[0], n[1], n[2]};
    return true;
}

/* FloorRayIntersect: the faces from `from` to `to` (excluded) the line crosses, in the floor's space */
static bool floor_ray_intersect(const nmo_engine_t *engine, const nmo_engine_mesh_t *mesh, const float extent[4],
                                bool first, int x, int z, eng_vector_t origin, eng_vector_t dir, int32_t from,
                                int32_t to, float *tmax, int32_t *face, eng_vector_t *inter)
{
    int32_t step = to < from ? -1 : 1;
    bool collision = false;
    for (int32_t i = from; i != to; i += step) {
        eng_vector_t p0 = mesh_point(mesh, i, 0), p1 = mesh_point(mesh, i, 1), p2 = mesh_point(mesh, i, 2);
        float min, max;
        min_max3(axis_of(&p0, x), axis_of(&p1, x), axis_of(&p2, x), &min, &max);
        if (extent[1] < min || extent[0] > max) {
            continue;
        }
        min_max3(axis_of(&p0, z), axis_of(&p1, z), axis_of(&p2, z), &min, &max);
        if (extent[3] < min || extent[2] > max) {
            continue;
        }
        eng_vector_t n = mesh_face_normal(mesh, i);
        if (dir.x * n.x + dir.y * n.y + dir.z * n.z < engine->floors.cos_angle) {
            continue;
        }
        eng_vector_t hit;
        float t;
        if (line_face(origin, dir, p0, p1, p2, n, &hit, &t)) {
            if (first) {
                *face = i;
                *inter = hit;
                return true;
            }
            if (fabsf(t) < *tmax) {
                *tmax = fabsf(t);
                *face = i;
                *inter = hit;
                collision = true;
            }
        }
    }
    return collision;
}

/* TestMovingFloor: the vertical line through the floor's faces, in its space */
static bool test_moving_floor(nmo_engine_t *engine, int32_t floor, const nmo_engine_mesh_t *mesh,
                              const eng_vector_t *position, eng_vector_t *inter, eng_vector_t *normal, bool first,
                              int32_t *face)
{
    eng_vector_t origin, dir, up = {0.0f, 1.0f, 0.0f};
    eng_entity_inverse_transform(engine, floor, &origin, position, -1);
    eng_entity_transform_vector(engine, floor, &dir, &up, -1, true);
    eng_vector_normalize(&dir);
    /* The local axis nearest the world's up */
    int x = 1, z = 2;
    eng_vector_t plane = {1.0f, 0.0f, 0.0f};
    float least = dir.z * dir.z + dir.y * dir.y;
    if (dir.x * dir.x + dir.z * dir.z < least) {
        least = dir.x * dir.x + dir.z * dir.z;
        plane = (eng_vector_t){0.0f, 1.0f, 0.0f};
        x = 0;
    }
    if (dir.x * dir.x + dir.y * dir.y < least) {
        plane = (eng_vector_t){0.0f, 0.0f, 1.0f};
        x = 0;
        z = 1;
    }
    /* The line's extents across the local box */
    eng_vector_t min, max, i1 = {0.0f, 0.0f, 0.0f}, i2 = {0.0f, 0.0f, 0.0f};
    eng_entity_local_box(engine, floor, &min, &max);
    line_plane(origin, dir, plane, max, &i1);
    line_plane(origin, dir, plane, min, &i2);
    const float extent[4] = {fminf(axis_of(&i1, x), axis_of(&i2, x)), fmaxf(axis_of(&i1, x), axis_of(&i2, x)),
                             fminf(axis_of(&i1, z), axis_of(&i2, z)), fmaxf(axis_of(&i1, z), axis_of(&i2, z))};
    float tmax = FLOOR_FAR;
    bool collision = floor_ray_intersect(engine, mesh, extent, first, x, z, origin, dir, 0, mesh->face_count, &tmax,
                                         face, inter);
    if (collision) {
        eng_vector_t n = mesh_face_normal(mesh, *face), local = *inter;
        eng_entity_transform_vector(engine, floor, normal, &n, -1, false);
        eng_entity_transform(engine, floor, inter, &local, -1);
    }
    return collision;
}

/* TestBoxFloor: the vertical line through the floor's local box (its normal transformed as a point) */
static bool test_box_floor(nmo_engine_t *engine, int32_t floor, const eng_vector_t *position, eng_vector_t *inter,
                           eng_vector_t *normal)
{
    eng_vector_t min, max, origin, dir, up = {0.0f, 1.0f, 0.0f};
    eng_entity_local_box(engine, floor, &min, &max);
    eng_entity_inverse_transform(engine, floor, &origin, position, -1);
    eng_entity_transform_vector(engine, floor, &dir, &up, -1, true);
    eng_vector_t in = {0.0f, 0.0f, 0.0f}, n = {0.0f, 0.0f, 0.0f};
    bool collision = line_box(origin, dir, min, max, &in, &n);
    eng_entity_transform(engine, floor, inter, &in, -1);
    eng_entity_transform(engine, floor, normal, &n, -1);
    return collision;
}

/* FloorFastRejection: no faces, or the point beside the world box, or farther than the floors found */
static bool floor_fast_rejection(nmo_engine_t *engine, int32_t floor, const nmo_engine_mesh_t *mesh,
                                 const eng_vector_t *position, float nearest_down, float nearest_up)
{
    if (mesh->face_count == 0) {
        return true;
    }
    eng_vector_t min, max;
    eng_entity_world_box(engine, floor, &min, &max);
    if (position->x < min.x || position->x > max.x || position->z < min.z || position->z > max.z) {
        return true;
    }
    float above = position->y - max.y;
    if (above > 0.0f) {
        return above > -nearest_down;
    }
    float below = position->y - min.y;
    return below <= 0.0f && -below > nearest_up;
}

int eng_floor_nearest(nmo_engine_t *engine, const eng_vector_t *position, eng_floor_point_t *point,
                      nmo_object_id_t exclude)
{
    eng_floor_point_clear(point);
    if (isnan(position->x) || isnan(position->y) || isnan(position->z)) {
        return CKFLOOR_NOFLOOR;
    }
    int32_t type = eng_floor_attribute(engine);
    /* The fields read stay for the next floor whose attribute lacks one */
    eng_floor_settings_t settings = {CKFLOOR_FACES, 0, 0, 0, 0};
    int32_t face = 0;
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        nmo_object_id_t object = attribute->object;
        int32_t floor = attribute->type == type && object != exclude ? eng_entity_index(engine, object) : -1;
        nmo_object_id_t mesh_id;
        nmo_engine_mesh_t mesh;
        if (floor < 0 || engine->entities.data[floor].destroyed || !floor_mesh(engine, object, &mesh_id, &mesh) ||
            floor_fast_rejection(engine, floor, &mesh, position, point->down_distance, point->up_distance)) {
            continue;
        }
        eng_floor_read(engine, object, &settings);
        eng_vector_t hit, normal;
        bool collision;
        if (settings.geometry == CKFLOOR_BOX) {
            collision = test_box_floor(engine, floor, position, &hit, &normal);
            face = 0;
        } else {
            collision = test_moving_floor(engine, floor, &mesh, position, &hit, &normal, settings.first != 0, &face);
        }
        if (!collision) {
            continue;
        }
        float distance = hit.y - position->y;
        if (distance > 0.0f) {
            if (distance < point->up_distance) {
                point->up_floor = object;
                point->up_distance = distance;
                point->up_normal = normal;
                point->up_face = face;
            }
        } else if (distance > point->down_distance) {
            point->down_floor = object;
            point->down_distance = distance;
            point->down_normal = normal;
            point->down_face = face;
        }
    }
    if (point->down_floor == 0) {
        return point->up_floor != 0 ? CKFLOOR_UP : CKFLOOR_NOFLOOR;
    }
    return point->up_floor != 0 && -point->down_distance >= point->up_distance ? CKFLOOR_UP : CKFLOOR_DOWN;
}

/* ============================================================================
 * ConstrainToFloor (the later SDK's)
 * ============================================================================ */

/* An edge of the faces as ComputeEdges' hash table keeps it */
typedef struct floor_hash_edge {
    uint16_t a, b, face;
    int32_t count;
    int32_t next;
} floor_hash_edge_t;

static int32_t near_2_power(int32_t v)
{
    int32_t i = v & -v;
    while (i < v) {
        i <<= 1;
    }
    return i;
}

/* ComputeEdges: the border edges (on a single face) of upward faces, in the world's XZ plane, outward
   normals; in the order an XHashTable keyed by the edge (hashed a * b) gives them */
static void compute_edges(nmo_engine_t *engine, int32_t floor, const nmo_engine_mesh_t *mesh, eng_floor_edges_t *out)
{
    const eng_matrix_t *world = &engine->entities.data[floor].world;
    eng_vector_t dx = {world->m[0][0], world->m[0][1], world->m[0][2]};
    eng_vector_t dy = {world->m[1][0], world->m[1][1], world->m[1][2]};
    eng_vector_t cp = eng_vector_cross(&dx, &dy);
    bool direct = cp.x * world->m[2][0] + cp.y * world->m[2][1] + cp.z * world->m[2][2] >= 0.0f;
    int32_t faces = mesh->face_count;
    int32_t required = faces * 3;
    int32_t size = near_2_power((int32_t)((float)required / 0.75f) + 1);
    if (size < 16) {
        size = 16;
    }
    int32_t *table = (int32_t *)malloc((size_t)size * sizeof(int32_t));
    floor_hash_edge_t *pool = (floor_hash_edge_t *)malloc((size_t)(required > 0 ? required : 1) * sizeof(*pool));
    if (table == NULL || pool == NULL) {
        free(table);
        free(pool);
        return;
    }
    for (int32_t i = 0; i < size; i++) {
        table[i] = -1;
    }
    int32_t used = 0;
    for (int32_t f = 0; f < faces; f++) {
        for (int k = 0; k < 3; k++) {
            uint16_t a = mesh->indices[f * 3 + k], b = mesh->indices[f * 3 + (k + 1) % 3];
            int32_t index = (int32_t)((uint32_t)a * (uint32_t)b) & (size - 1);
            int32_t e = table[index];
            while (e >= 0 && !((pool[e].a == a && pool[e].b == b) || (pool[e].a == b && pool[e].b == a))) {
                e = pool[e].next;
            }
            if (e >= 0) {
                pool[e].count++;
                continue;
            }
            pool[used] = (floor_hash_edge_t){a, b, (uint16_t)f, 1, table[index]};
            table[index] = used++;
        }
    }
    out->edges = (eng_floor_edge_t *)malloc((size_t)(used > 0 ? used : 1) * sizeof(eng_floor_edge_t));
    out->count = 0;
    for (int32_t bucket = 0; out->edges != NULL && bucket < size; bucket++) {
        for (int32_t e = table[bucket]; e >= 0; e = pool[e].next) {
            if (pool[e].count != 1) {
                continue;
            }
            const nmo_vector_t *pa = &mesh->vertices[pool[e].a].position, *pb = &mesh->vertices[pool[e].b].position;
            eng_vector_t la = {pa->x, pa->y, pa->z}, lb = {pb->x, pb->y, pb->z}, va, vb, fnormal;
            eng_entity_transform(engine, floor, &va, &la, -1);
            eng_entity_transform(engine, floor, &vb, &lb, -1);
            eng_vector_t n = mesh_face_normal(mesh, pool[e].face);
            eng_entity_transform_vector(engine, floor, &fnormal, &n, -1, false);
            if (fnormal.y <= FLOOR_EPSILON) {
                continue;
            }
            eng_floor_edge_t *edge = &out->edges[out->count++];
            edge->a[0] = va.x;
            edge->a[1] = va.z;
            edge->b[0] = vb.x;
            edge->b[1] = vb.z;
            eng_vector_t vedge = {vb.x - va.x, 0.0f, vb.z - va.z};
            eng_vector_t norm = eng_vector_cross(&fnormal, &vedge);
            if (!direct) {
                norm = (eng_vector_t){-norm.x, -norm.y, -norm.z};
            }
            norm.y = 0.0f;
            eng_vector_normalize(&norm);
            edge->length = eng_vector_magnitude(&vedge);
            edge->inv_length = 1.0f / edge->length;
            edge->normal[0] = norm.x;
            edge->normal[1] = norm.z;
            edge->d = -(edge->normal[0] * edge->a[0] + edge->normal[1] * edge->a[1]);
        }
    }
    free(table);
    free(pool);
}

/* The floor's edges, computed once a mesh (FloorManager::m_FloorEdges) */
static const eng_floor_edges_t *floor_edges(nmo_engine_t *engine, int32_t floor, nmo_object_id_t mesh_id,
                                            const nmo_engine_mesh_t *mesh)
{
    for (size_t i = 0; i < engine->floors.edges.count; i++) {
        if (engine->floors.edges.data[i].mesh == mesh_id) {
            return &engine->floors.edges.data[i];
        }
    }
    eng_floor_edges_t *edges = NULL;
    if (!ENG_VEC_PUSH(engine->floors.edges, edges)) {
        return NULL;
    }
    memset(edges, 0, sizeof(*edges));
    edges->mesh = mesh_id;
    compute_edges(engine, floor, mesh, edges);
    return edges;
}

static void normalize_2d(float *v)
{
    float length = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (length != 0.0f) {
        v[0] /= length;
        v[1] /= length;
    }
}

/* SlideOnEdges: the point pushed a radius away from each edge it is inside of, unless going out
   over that edge leads onto another floor */
static bool slide_on_edges(nmo_engine_t *engine, eng_vector_t *position, float radius, const eng_floor_edges_t *edges,
                           nmo_object_id_t floor, int32_t exclude_attribute)
{
    bool touched = false;
    float pos[2] = {position->x, position->z};
    for (int32_t i = 0; i < edges->count; i++) {
        const eng_floor_edge_t *e = &edges->edges[i];
        float dist = e->normal[0] * pos[0] + e->normal[1] * pos[1] + e->d;
        if (dist >= radius || dist <= 0.0f) {
            continue;
        }
        float left[2] = {(e->a[0] - e->b[0]) * e->inv_length, (e->a[1] - e->b[1]) * e->inv_length};
        float dist_left = left[0] * (pos[0] - e->a[0]) + left[1] * (pos[1] - e->a[1]);
        if (dist_left >= radius) {
            continue;
        }
        if (dist_left >= 0.0f) { /* the left corner */
            float move = radius - sqrtf(dist_left * dist_left + dist * dist);
            if (move > 0.0f) {
                float d[2] = {pos[0] - e->a[0], pos[1] - e->a[1]};
                normalize_2d(d);
                pos[0] += move * d[0];
                pos[1] += move * d[1];
                touched = true;
            }
            continue;
        }
        float dist_right = -(e->length + dist_left);
        if (dist_right >= radius) {
            continue;
        }
        if (dist_right >= 0.0f) { /* the right corner */
            float move = radius - sqrtf(dist_right * dist_right + dist * dist);
            if (move > 0.0f) {
                float d[2] = {pos[0] - e->b[0], pos[1] - e->b[1]};
                normalize_2d(d);
                pos[0] += move * d[0];
                pos[1] += move * d[1];
                touched = true;
            }
            continue;
        }
        /* Along the edge: free when another floor is past it */
        eng_vector_t going = {pos[0] - radius * e->normal[0], position->y, pos[1] - radius * e->normal[1]};
        eng_floor_point_t point;
        int nearest = eng_floor_nearest(engine, &going, &point, floor);
        if (nearest != CKFLOOR_NOFLOOR) {
            nmo_object_id_t next = nearest == CKFLOOR_DOWN ? point.down_floor : point.up_floor;
            if (exclude_attribute == -1 || !nmo_engine_object_has_attribute(engine, next, exclude_attribute)) {
                continue;
            }
        }
        pos[0] += (radius - dist) * e->normal[0];
        pos[1] += (radius - dist) * e->normal[1];
        touched = true;
    }
    position->x = pos[0];
    position->z = pos[1];
    return touched;
}

/* The floor CK_FLOORNEAREST names */
static nmo_object_id_t nearest_floor(int nearest, const eng_floor_point_t *point)
{
    return nearest == CKFLOOR_DOWN ? point->down_floor : nearest == CKFLOOR_UP ? point->up_floor : 0;
}

/* ConstrainToFloor(position) */
static bool constrain_point(nmo_engine_t *engine, const eng_vector_t *position, float radius, eng_vector_t *out,
                            int32_t exclude_attribute)
{
    eng_floor_point_t point;
    nmo_object_id_t floor = nearest_floor(eng_floor_nearest(engine, position, &point, 0), &point);
    if (floor == 0) {
        return false;
    }
    if (exclude_attribute != -1 && nmo_engine_object_has_attribute(engine, floor, exclude_attribute)) {
        floor = nearest_floor(eng_floor_nearest(engine, position, &point, floor), &point);
        if (floor == 0 || nmo_engine_object_has_attribute(engine, floor, exclude_attribute)) {
            return false;
        }
    }
    int32_t index = eng_entity_index(engine, floor);
    nmo_object_id_t mesh_id;
    nmo_engine_mesh_t mesh;
    if (index < 0 || !floor_mesh(engine, floor, &mesh_id, &mesh)) {
        return false;
    }
    const eng_floor_edges_t *edges = floor_edges(engine, index, mesh_id, &mesh);
    if (edges == NULL) {
        return false;
    }
    eng_vector_t moved = *position;
    if (!slide_on_edges(engine, &moved, radius, edges, floor, exclude_attribute)) {
        return false;
    }
    *out = moved;
    return true;
}

bool eng_floor_constrain(nmo_engine_t *engine, const eng_vector_t *old, const eng_vector_t *position,
                         float radius, eng_vector_t *out, int32_t exclude_attribute)
{
    eng_vector_t dir = {position->x - old->x, position->y - old->y, position->z - old->z};
    float dist = eng_vector_magnitude(&dir);
    if (dist < radius) {
        return constrain_point(engine, position, radius, out, exclude_attribute);
    }
    /* A little less than the radius: constrained last frame, the old position is a radius off the edge */
    float reduced = radius * 0.99f;
    int steps = (int)(dist / reduced);
    float scale = reduced / dist;
    dir = (eng_vector_t){dir.x * scale, dir.y * scale, dir.z * scale};
    eng_vector_t pos = *old;
    while (steps--) {
        pos = (eng_vector_t){pos.x + dir.x, pos.y + dir.y, pos.z + dir.z};
        if (constrain_point(engine, &pos, radius, out, exclude_attribute)) {
            return true;
        }
    }
    return constrain_point(engine, position, radius, out, exclude_attribute);
}
