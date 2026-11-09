/**
 * @file test.c
 * @brief Ports of the Logics/Test building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Ray Intersection tests against the engine's 3D entities and their
 * meshes.
 */

#include "../virtools.h"

#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_guids.h"

#include <math.h>
#include <string.h>

static int test_compare(int op, float a, float c)
{
    switch (op) {
    case 1: return a == c;
    case 2: return a != c;
    case 3: return a < c;
    case 4: return a <= c;
    case 5: return a > c;
    case 6: return a >= c;
    default: return 0;
    }
}

static int bb_test(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t op = get_in_int(b, 0, 1); /* CKEQUAL */
    float val[2] = {0.0f, 0.0f};
    const char *str[2] = {NULL, NULL};
    bool is_string = false;
    for (int a = 0; a < 2; a++) {
        nmo_guid_t guid = nmo_engine_block_input_parameter_type(b, a + 1);
        if (type_derives(b, guid, CKPGUID_FLOAT)) {
            val[a] = get_in_float(b, a + 1, 0.0f);
        } else if (nmo_guid_equals(guid, CKPGUID_STRING)) {
            size_t size = 0;
            const char *text = (const char *)nmo_engine_block_input_data(b, a + 1, &size);
            str[a] = (text != NULL && size > 0 && memchr(text, '\0', size) != NULL) ? text : NULL;
            is_string = true;
        } else {
            int32_t value = get_in_int(b, a + 1, 0);
            val[a] = nmo_guid_equals(guid, CKPGUID_BOOL) ? (value ? 1.0f : 0.0f) : (float)value;
        }
    }
    if (op <= 0) {
        return NMO_ENGINE_BR_OK;
    }
    int k;
    if (is_string) {
        /* ICompare: a null string sorts first */
        int cmp = str[0] == NULL ? (str[1] == NULL ? 0 : -1)
                  : str[1] == NULL ? 1 : strcmp(str[0], str[1]);
        k = test_compare(op, (float)cmp, 0.0f);
    } else {
        k = test_compare(op, val[0], val[1]);
    }
    OUT_ON(b, k ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Ray Intersection (Logics/Test, Logics.dll): the nearest 3D object within
 * Depth on the ray (in the Referential's space given one), among the
 * Filter group's objects or all 3D objects, characters and 3D sprites in the
 * level scene, the owner skipped unless "Skip Owner" is off. Each object is
 * first tested by its bounding sphere and world box, then its mesh. True
 * with what it hit (with "Output Param Update": its face, the face's vertex
 * nearest the hit, the point and normal in world space, the distance),
 * False otherwise. Characters' body parts and 3D sprites are not hit, and
 * every object counts as visible for "Skip Transparent".
 */
typedef struct ray_best {
    eng_vector_t origin, dir, end;
    float depth;
    nmo_object_id_t object;
    eng_ray_hit_t hit;
    float distance2;
} ray_best_t;

static void ray_test(nmo_engine_t *engine, nmo_object_id_t id, ray_best_t *ray)
{
    uint32_t class_id = (uint32_t)nmo_engine_object_class(engine, id);
    int32_t index = eng_entity_index(engine, id);
    if (index < 0 || !nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_3DENTITY) ||
        !eng_in_scene(engine, id) ||
        nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_CHARACTER) ||
        nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_SPRITE3D)) {
        return;
    }
    float radius = eng_entity_radius(engine, index);
    eng_vector_t center;
    eng_entity_bary_center(engine, index, &center);
    float dx = center.x - ray->origin.x, dy = center.y - ray->origin.y, dz = center.z - ray->origin.z;
    float distance2 = dz * dz + dy * dy + dx * dx;
    float reach = radius + ray->depth;
    if (!(distance2 < reach * reach)) {
        return;
    }
    double along = (double)dz * ray->dir.z + (double)dy * ray->dir.y + (double)dx * ray->dir.x;
    if (!(radius + along > 0.0) || !(distance2 - along * along < (double)radius * radius)) {
        return; /* behind the origin, or beside the ray */
    }
    eng_vector_t min, max;
    if (!eng_entity_world_box(engine, index, &min, &max) || !eng_ray_box(&ray->origin, &ray->dir, &min, &max)) {
        return;
    }
    eng_ray_hit_t hit;
    if (eng_entity_ray_intersection(engine, index, &ray->origin, &ray->end, &hit) == 0) {
        return;
    }
    eng_vector_t world;
    eng_entity_transform(engine, index, &world, &hit.point, -1);
    float wx = world.x - ray->origin.x, wy = world.y - ray->origin.y, wz = world.z - ray->origin.z;
    float d2 = wz * wz + wy * wy + wx * wx;
    if (d2 < ray->distance2) {
        ray->distance2 = d2;
        ray->object = id;
        ray->hit = hit;
    }
}

static int bb_ray_intersection(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_target(b);
    if (owner == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    if (get_local_int(b, 1, 1) == 0) {
        owner = 0; /* "Skip Owner" off */
    }
    ray_best_t ray;
    memset(&ray, 0, sizeof(ray));
    nmo_engine_block_get_input_value(b, 0, &ray.origin, sizeof(ray.origin));
    nmo_engine_block_get_input_value(b, 1, &ray.dir, sizeof(ray.dir));
    int32_t referential = input_entity(b, 2);
    if (referential >= 0) {
        eng_entity_transform(engine, referential, &ray.origin, &ray.origin, -1);
        eng_entity_transform_vector(engine, referential, &ray.dir, &ray.dir, -1, false);
    }
    eng_vector_normalize(&ray.dir);
    ray.depth = get_in_float(b, 3, 100.0f);
    ray.end = (eng_vector_t){ray.dir.x + ray.origin.x, ray.dir.y + ray.origin.y,
                             ray.dir.z + ray.origin.z};
    ray.distance2 = ray.depth * ray.depth;
    nmo_object_id_t filter = nmo_engine_block_get_input_object(b, 4);
    if (filter != 0) {
        int32_t count = nmo_engine_group_count(engine, filter);
        for (int32_t i = 0; i < count; i++) {
            nmo_object_id_t id = nmo_engine_group_object(engine, filter, i);
            if (id != owner) {
                ray_test(engine, id, &ray);
            }
        }
    } else {
        /* The context's 3D objects, then characters, then 3D sprites */
        static const uint32_t classes[3] = {NMO_CID_3DOBJECT, NMO_CID_CHARACTER, NMO_CID_SPRITE3D};
        for (int c = 0; c < 3; c++) {
            for (size_t i = 0; i < engine->entities.count; i++) {
                nmo_object_id_t id = engine->entities.data[i].id;
                if (!engine->entities.data[i].destroyed && id != owner &&
                    (uint32_t)nmo_engine_object_class(engine, id) == classes[c]) {
                    ray_test(engine, id, &ray);
                }
            }
        }
    }
    if (ray.object == 0 || !((double)ray.depth * ray.depth > ray.distance2)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t object = ray.object;
    nmo_engine_block_set_output_value(b, 0, &object, sizeof(object));
    int32_t index = eng_entity_index(engine, ray.object);
    if (get_local_int(b, 0, 1) != 0 && index >= 0) {
        /* The face's vertex nearest the hit (the later one on a tie) */
        float distances[3];
        for (int k = 0; k < 3; k++) {
            float x = ray.hit.corners[k].x - ray.hit.point.x, y = ray.hit.corners[k].y - ray.hit.point.y,
                  z = ray.hit.corners[k].z - ray.hit.point.z;
            distances[k] = (float)sqrt((double)(z * z + y * y + x * x));
        }
        int32_t nearest;
        if (distances[0] >= distances[1]) {
            nearest = distances[1] >= distances[2] ? ray.hit.vertices[2] : ray.hit.vertices[1];
        } else {
            nearest = distances[0] < distances[2] ? ray.hit.vertices[0] : ray.hit.vertices[2];
        }
        eng_vector_t point, normal;
        eng_entity_transform(engine, index, &point, &ray.hit.point, -1);
        eng_entity_transform_vector(engine, index, &normal, &ray.hit.normal, -1, false);
        eng_vector_normalize(&normal);
        float distance = (float)sqrt((double)ray.distance2);
        nmo_engine_block_set_output_value(b, 1, &ray.hit.face, sizeof(ray.hit.face));
        nmo_engine_block_set_output_value(b, 2, &nearest, sizeof(nearest));
        nmo_engine_block_set_output_value(b, 3, &point, sizeof(point));
        nmo_engine_block_set_output_value(b, 4, &normal, sizeof(normal));
        nmo_engine_block_set_output_value(b, 5, &distance, sizeof(distance));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Proximity: the distance between A and B (their positions, or barycenters,
 * a character's bounding box center) against Distance. The settings flags
 * (local 1) pick which of In Range, Out Range, Enter Range and Exit Range are
 * outputs; Enter and Exit are the frames the state (local 0) changes.
 */
#define PROXIMITY_WASOUT 0
#define PROXIMITY_WASIN 1

static eng_vector_t proximity_point(blk_t *b, int32_t entity, nmo_object_id_t id, bool barycenter)
{
    eng_vector_t point;
    if (!barycenter) {
        return eng_entity_position(b->engine, entity, -1);
    }
    if (nmo_engine_object_class(b->engine, id) == NMO_CID_CHARACTER) {
        eng_vector_t min = {0, 0, 0}, max = {0, 0, 0};
        eng_entity_world_box(b->engine, entity, &min, &max);
        return (eng_vector_t){(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }
    eng_entity_bary_center(b->engine, entity, &point);
    return point;
}

static int bb_proximity(blk_t *b)
{
    IN_OFF(b, 0);
    float distance = get_in_float(b, 0, 1.0f);
    bool barycenter = get_in_int(b, 3, 0) != 0;
    nmo_object_id_t id_a = nmo_engine_block_get_input_object(b, 1);
    nmo_object_id_t id_b = nmo_engine_block_get_input_object(b, 2);
    int32_t ea = eng_entity_index(b->engine, id_a), eb = eng_entity_index(b->engine, id_b);
    if (ea < 0 || eb < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* the original reads a missing entity */
    }
    eng_vector_t a = proximity_point(b, ea, id_a, barycenter);
    eng_vector_t c = proximity_point(b, eb, id_b, barycenter);
    eng_vector_t d = {a.x - c.x, a.y - c.y, a.z - c.z};
    float current = eng_vector_magnitude(&d);
    set_out_float(b, 0, current);
    int32_t flag = get_local_int(b, 1, 15);
    int32_t was_in = get_local_int(b, 0, 2);
    int32_t activation;
    if (current < distance) {
        activation = was_in == PROXIMITY_WASOUT && (flag & 4) ? 4 : 1; /* Enter Range, In Range */
        was_in = PROXIMITY_WASIN;
    } else {
        activation = was_in == PROXIMITY_WASIN && (flag & 8) ? 8 : 2; /* Exit Range, Out Range */
        was_in = PROXIMITY_WASOUT;
    }
    set_local_int(b, 0, was_in);
    int output = 0;
    for (int bit = 0; bit < 4; bit++) {
        if (flag & (1 << bit)) {
            if (activation & (1 << bit)) {
                OUT_ON(b, output);
                return NMO_ENGINE_BR_OK;
            }
            ++output;
        }
    }
    OUT_ON(b, output);
    return NMO_ENGINE_BR_OK;
}

/* All the objects of a group are 3D entities (CKGroup::GetCommonClassID derives from it) */
static bool group_of_entities(nmo_engine_t *engine, nmo_object_id_t group)
{
    int32_t count = nmo_engine_group_count(engine, group);
    if (count <= 0) {
        return false; /* an empty group's class is CKCID_BEOBJECT */
    }
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t object = nmo_engine_group_object(engine, group, i);
        if (object != 0 && !eng_is_child_class(engine, eng_object_class(engine, object), NMO_CID_3DENTITY)) {
            return false;
        }
    }
    return true;
}

/*
 * VxIntersect::RayBox with the entry point and normal: 1 for a hit ahead,
 * -1 when the ray starts in the box (or has no direction in it), 0 for none.
 */
static int ray_box(const eng_vector_t *origin, const eng_vector_t *dir, const eng_vector_t *min,
                   const eng_vector_t *max, eng_vector_t *point, eng_vector_t *normal)
{
    const float epsilon = 1.192092896e-07f;
    const float o[3] = {origin->x, origin->y, origin->z}, d[3] = {dir->x, dir->y, dir->z};
    const float lo[3] = {min->x, min->y, min->z}, hi[3] = {max->x, max->y, max->z};
    double near_t = -1.7976931348623157e308, far_t = 1.7976931348623157e308;
    int near_axis = 0;
    bool active = false;
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < epsilon) {
            if (!(o[i] >= lo[i] && o[i] <= hi[i])) {
                return 0;
            }
            continue;
        }
        active = true;
        double t1 = ((double)lo[i] - o[i]) / (double)d[i], t2 = ((double)hi[i] - o[i]) / (double)d[i];
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
        }
        if (near_t > far_t || far_t < epsilon) {
            return 0;
        }
    }
    float p[3] = {o[0], o[1], o[2]}, n[3] = {1.0f, 0.0f, 0.0f};
    if (!active) {
        *point = (eng_vector_t){p[0], p[1], p[2]};
        *normal = (eng_vector_t){n[0], n[1], n[2]};
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        p[i] = (float)((double)o[i] + (double)d[i] * near_t);
    }
    p[near_axis] = d[near_axis] > 0.0f ? lo[near_axis] : hi[near_axis];
    n[0] = 0.0f;
    n[near_axis] = d[near_axis] > 0.0f ? -1.0f : 1.0f;
    *point = (eng_vector_t){p[0], p[1], p[2]};
    *normal = (eng_vector_t){n[0], n[1], n[2]};
    return near_t <= epsilon ? -1 : 1;
}

/*
 * Ray Box Intersection: the nearest of a group's 3D entities (but the target,
 * in the scene) whose local box the ray from Origin along Direction (in
 * Referential) meets, within Depth. A ray starting in one reports that one
 * at once on Origin Inside.
 */
static int bb_ray_box_intersection(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t owner = nmo_engine_block_target(b);
    if (owner == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    eng_vector_t origin = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    eng_vector_t direction = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
    int32_t referential = input_entity(b, 2);
    if (referential >= 0) {
        eng_vector_t o = origin, d = direction;
        eng_vector_transform(&origin, &engine->entities.data[referential].world, &o);
        eng_vector_rotate(&direction, &engine->entities.data[referential].world, &d);
    }
    float depth = get_in_float(b, 3, 100.0f);
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 4);
    if (group == 0 || !group_of_entities(engine, group)) {
        return NMO_ENGINE_BR_OK;
    }
    float nearest = 100000.0f;
    int32_t found = -1;
    eng_vector_t found_point = {0, 0, 0}, found_normal = {0, 0, 0};
    for (int32_t i = 0; i < nmo_engine_group_count(engine, group); i++) {
        nmo_object_id_t id = nmo_engine_group_object(engine, group, i);
        int32_t entity = id != 0 && id != owner ? eng_entity_index(engine, id) : -1;
        eng_vector_t min, max;
        if (entity < 0 || !eng_in_scene(engine, id) || !eng_entity_local_box(engine, entity, &min, &max)) {
            continue;
        }
        eng_matrix_t inverse;
        eng_matrix_inverse(&inverse, &engine->entities.data[entity].world);
        eng_vector_t local_origin, local_direction, point, normal;
        eng_vector_transform(&local_origin, &inverse, &origin);
        eng_vector_rotate(&local_direction, &inverse, &direction);
        int hit = ray_box(&local_origin, &local_direction, &min, &max, &point, &normal);
        if (hit == 0) {
            continue;
        }
        if (hit < 0) {
            uint32_t object = (uint32_t)id;
            nmo_engine_block_set_output_value(b, 0, &object, sizeof(object));
            OUT_ON(b, 2);
            return NMO_ENGINE_BR_OK;
        }
        eng_vector_t world;
        eng_vector_transform(&world, &engine->entities.data[entity].world, &point);
        eng_vector_t delta = {world.x - origin.x, world.y - origin.y, world.z - origin.z};
        float d = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (d < nearest) {
            nearest = d;
            found = entity;
            found_point = world;
            found_normal = normal;
        }
    }
    if (found >= 0 && nearest < depth * depth) {
        set_out(b, 1, &found_point, sizeof(found_point));
        uint32_t object = (uint32_t)engine->entities.data[found].id;
        nmo_engine_block_set_output_value(b, 0, &object, sizeof(object));
        eng_vector_t normal;
        eng_vector_rotate(&normal, &engine->entities.data[found].world, &found_normal);
        eng_vector_normalize(&normal);
        set_out(b, 2, &normal, sizeof(normal));
        set_out_float(b, 3, sqrtf(nearest));
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/* ObjectBetween.cpp's RayBoxIntersection: the segment p0-p1 meets the box */
static bool segment_box(const eng_vector_t *min, const eng_vector_t *max, const eng_vector_t *p0,
                        const eng_vector_t *p1)
{
    const float o[3] = {p0->x, p0->y, p0->z};
    const float dir[3] = {p1->x - p0->x, p1->y - p0->y, p1->z - p0->z};
    const float lo[3] = {min->x, min->y, min->z}, hi[3] = {max->x, max->y, max->z};
    int quadrant[3];
    float candidate[3] = {0, 0, 0}, max_t[3];
    bool inside = true;
    for (int i = 0; i < 3; i++) {
        if (o[i] < lo[i]) {
            quadrant[i] = 1;
            candidate[i] = lo[i];
            inside = false;
        } else if (o[i] > hi[i]) {
            quadrant[i] = 0;
            candidate[i] = hi[i];
            inside = false;
        } else {
            quadrant[i] = 2;
        }
    }
    if (inside) {
        return true;
    }
    for (int i = 0; i < 3; i++) {
        max_t[i] = quadrant[i] != 2 && dir[i] != 0.0f ? (candidate[i] - o[i]) / dir[i] : -1.0f;
    }
    int plane = 0;
    for (int i = 1; i < 3; i++) {
        if (max_t[plane] < max_t[i]) {
            plane = i;
        }
    }
    if (max_t[plane] < 0.0f || max_t[plane] > 1.0f) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (plane != i) {
            float r = o[i] + max_t[plane] * dir[i];
            if (r < lo[i] || r > hi[i]) {
                return false;
            }
        }
    }
    return true;
}

/* ObjectBetween.cpp's RayFaceIntersection: the segment ori-dest crosses the triangle */
static bool segment_face(const eng_vector_t *ori, const eng_vector_t *dest, const eng_vector_t *v1,
                         const eng_vector_t *v2, const eng_vector_t *v3)
{
    eng_vector_t e1 = {v2->x - v1->x, v2->y - v1->y, v2->z - v1->z};
    eng_vector_t e2 = {v3->x - v1->x, v3->y - v1->y, v3->z - v1->z};
    eng_vector_t normal = eng_vector_cross(&e1, &e2);
    eng_vector_t v = {dest->x - ori->x, dest->y - ori->y, dest->z - ori->z};
    eng_vector_t ap = {ori->x - v1->x, ori->y - v1->y, ori->z - v1->z};
    float denom = v.x * normal.x + v.y * normal.y + v.z * normal.z;
    if (denom == 0.0f) {
        return false;
    }
    float t = -(normal.x * ap.x + normal.y * ap.y + normal.z * ap.z) / denom;
    if (t <= 0.0f || t > 1.0f) {
        return false;
    }
    const float res[3] = {ori->x + t * v.x, ori->y + t * v.y, ori->z + t * v.z};
    const float a[3] = {v1->x, v1->y, v1->z}, c1[3] = {v2->x, v2->y, v2->z}, c2[3] = {v3->x, v3->y, v3->z};
    float cmax = fabsf(normal.x);
    int i1 = 1, i2 = 2;
    if (cmax < fabsf(normal.y)) {
        i1 = 0;
        i2 = 2;
        cmax = fabsf(normal.y);
    }
    if (cmax < fabsf(normal.z)) {
        i1 = 0;
        i2 = 1;
    }
    float u0 = res[i1] - a[i1], w0 = res[i2] - a[i2];
    float u1 = c1[i1] - a[i1], w1 = c1[i2] - a[i2];
    float u2 = c2[i1] - a[i1], w2 = c2[i2] - a[i2];
    float alpha, beta;
    if (u1 == 0.0f) {
        beta = u0 / u2;
        if (beta >= 0.0f && beta <= 1.0f) {
            alpha = (w0 - beta * w2) / w1;
            return alpha >= 0.0f && alpha + beta <= 1.0f;
        }
        return false;
    }
    beta = (w0 * u1 - u0 * w1) / (w2 * u1 - u2 * w1);
    if (beta >= 0.0f && beta <= 1.0f) {
        alpha = (u0 - beta * u2) / u1;
        return alpha >= 0.0f && alpha + beta <= 1.0f;
    }
    return false;
}

/* An obstacle between the points: its world box, then (at precision 2) a face of its mesh */
static bool object_between(nmo_engine_t *engine, int32_t entity, int32_t precision,
                           const eng_vector_t *from, const eng_vector_t *to)
{
    eng_vector_t min, max;
    if (!eng_entity_world_box(engine, entity, &min, &max) || !segment_box(&min, &max, from, to)) {
        return false;
    }
    if (precision != 2) {
        return true;
    }
    eng_vector_t local_from, local_to;
    eng_entity_inverse_transform(engine, entity, &local_from, from, -1);
    eng_entity_inverse_transform(engine, entity, &local_to, to, -1);
    nmo_object_t *object = eng_find_object(engine, engine->entities.data[entity].id);
    const nmo_3dentity_state_t *state =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    nmo_object_id_t mesh_id =
        state != NULL ? eng_object_ref(engine, engine->entities.data[entity].id, &state->current_mesh) : 0;
    nmo_engine_mesh_t mesh;
    if (mesh_id == 0 || !nmo_engine_mesh(engine, mesh_id, &mesh)) {
        return false; /* no mesh: the next obstacle */
    }
    for (int32_t f = 0; f < mesh.face_count; f++) {
        eng_vector_t v[3];
        for (int k = 0; k < 3; k++) {
            const nmo_vector_t *p = &mesh.vertices[mesh.indices[3 * f + k]].position;
            v[k] = (eng_vector_t){p->x, p->y, p->z};
        }
        if (segment_face(&local_from, &local_to, &v[0], &v[1], &v[2])) {
            return true;
        }
    }
    return false;
}

/*
 * Object Between: the first obstacle on the segment between the two objects'
 * positions: of the objects with the "Fixed Obstacle" attribute, or of every
 * 3D object when Test Only Obstacles is false.
 */
static int bb_object_between(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t first = nmo_engine_block_get_input_object(b, 0);
    nmo_object_id_t second = nmo_engine_block_get_input_object(b, 1);
    int32_t precision = get_in_int(b, 2, 2);
    bool only_obstacles = get_in_int(b, 3, 1) != 0;
    int32_t e1 = eng_entity_index(engine, first), e2 = eng_entity_index(engine, second);
    eng_vector_t from = e1 >= 0 ? eng_entity_position(engine, e1, -1) : (eng_vector_t){0, 0, 0};
    eng_vector_t to = e2 >= 0 ? eng_entity_position(engine, e2, -1) : (eng_vector_t){0, 0, 0};
    if (only_obstacles) {
        int32_t attribute = -1;
        for (size_t t = 0; t < engine->attributes.types.count; t++) {
            const char *name = engine->attributes.types.data[t].name;
            if (engine->attributes.types.data[t].present && name != NULL && strcmp(name, "Fixed Obstacle") == 0) {
                attribute = (int32_t)t;
                break;
            }
        }
        for (size_t a = 0; attribute >= 0 && a < engine->attributes.values.count; a++) {
            const eng_attribute_t *holder = &engine->attributes.values.data[a];
            if (holder->type != attribute || !eng_in_scene(engine, holder->object) ||
                holder->object == first || holder->object == second) {
                continue;
            }
            int32_t entity = eng_entity_index(engine, holder->object);
            if (entity >= 0 && object_between(engine, entity, precision, &from, &to)) {
                uint32_t id = (uint32_t)holder->object;
                nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
                OUT_ON(b, 0);
                return NMO_ENGINE_BR_OK;
            }
        }
    } else {
        for (size_t e = 0; e < engine->entities.count; e++) {
            const eng_entity_t *entity = &engine->entities.data[e];
            if (entity->destroyed || entity->id == first || entity->id == second ||
                !eng_is_child_class(engine, eng_object_class(engine, entity->id), NMO_CID_3DOBJECT)) {
                continue;
            }
            if (object_between(engine, (int32_t)e, precision, &from, &to)) {
                uint32_t id = (uint32_t)entity->id;
                nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
                OUT_ON(b, 0);
                return NMO_ENGINE_BR_OK;
            }
        }
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

/*
 * Is In View Frustum: a 3D entity tested against the attached camera's
 * frustum (CK3dEntity::IsInViewFrustrum, or IsInViewFrustrumHierarchic with
 * Test Hierarchy), all inside as the test's mark says (IsAllInsideFrustrum),
 * its extents the ones the last render left (GetRenderExtents: the test here
 * does not update them, except a meshless entity's pixel); a 2D entity's
 * rectangle against the view. Version 3 also says whether it is all inside.
 * Other objects do nothing.
 */
static int bb_is_in_view_frustum(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    bool hierarchy = get_in_int(b, 1, 1) != 0;
    const eng_screen_mode_t *mode =
        &eng_screen_modes[engine->render.screen_mode >= 0 && engine->render.screen_mode < ENG_SCREEN_MODE_COUNT
                              ? engine->render.screen_mode
                              : 0];
    bool inside = false, all_inside = false;
    nmo_rect_t extents = {0, 0, 0, 0};
    int32_t entity = object != 0 ? eng_entity_index(engine, object) : -1;
    nmo_class_id_t class_id = object != 0 ? eng_object_class(engine, object) : 0;
    if (entity >= 0 && eng_is_child_class(engine, class_id, NMO_CID_3DENTITY)) {
        eng_matrix_t view;
        if (eng_camera_view_projection(engine, engine->render.camera, &view, NULL)) {
            int32_t viewport[4];
            eng_render_viewport(engine, viewport);
            inside = hierarchy ? eng_entity_in_view_frustum_hierarchic(engine, entity, &view)
                               : eng_entity_in_view_frustum(engine, entity, &view, viewport, false);
            all_inside = eng_entity_all_inside_frustum(engine, entity);
        }
        extents = engine->entities.data[entity].render_extents; /* GetRenderExtents */
    } else if (object != 0 && eng_is_child_class(engine, class_id, NMO_CID_2DENTITY)) {
        const eng_2d_t *flat = eng_2d_entity(engine, object);
        if (flat == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        extents = flat->rect;
        const nmo_rect_t view = {0.0f, 0.0f, (float)mode->width, (float)mode->height};
        bool outside = extents.right < view.left || extents.left > view.right ||
                       extents.bottom < view.top || extents.top > view.bottom;
        if (!outside) {
            inside = true;
            all_inside = extents.left >= view.left && extents.right <= view.right &&
                         extents.top >= view.top && extents.bottom <= view.bottom;
        }
    } else {
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_version(b) == 0x00030000u) {
        set_out_int(b, 1, all_inside ? 1 : 0);
    }
    if (inside) {
        if (nmo_guid_equals(nmo_engine_block_output_parameter_type(b, 0), CKPGUID_RECT)) {
            set_out(b, 0, &extents, sizeof(extents));
        } else {
            const float top_left[2] = {extents.left, extents.top};
            const float bottom_right[2] = {extents.right, extents.bottom};
            set_out(b, 0, top_left, sizeof(top_left));
            set_out(b, 1, bottom_right, sizeof(bottom_right));
        }
        OUT_ON(b, 0);
        return NMO_ENGINE_BR_OK;
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_logics_test_blocks[] = {
    {NMO_GUID_INIT(0x17d66d26, 0x726b7dec), "Test", bb_test, NULL, NULL},
    {NMO_GUID_INIT(0x671e4a87, 0x383b2912), "Ray Intersection", bb_ray_intersection, NULL, NULL},
    {NMO_GUID_INIT(0x5321cacb, 0xdcdc5213), "Proximity", bb_proximity, NULL, NULL},
    {NMO_GUID_INIT(0x671e5a87, 0x383b2372), "Ray Box Intersection", bb_ray_box_intersection, NULL,
     NULL},
    {NMO_GUID_INIT(0x45484b53, 0x08eb1fab), "Object Between", bb_object_between, NULL, NULL},
    {NMO_GUID_INIT(0x45542a7f, 0x4c2f01a6), "Is In View Frustum", bb_is_in_view_frustum, NULL,
     NULL},
};

nmo_status_t eng_register_logics_test(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_test_blocks);
}
