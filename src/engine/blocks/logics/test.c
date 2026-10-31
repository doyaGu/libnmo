/**
 * @file test.c
 * @brief Ports of the Logics/Test building blocks.
 *
 * Each function does what the block's function in Logics.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Ray Intersection tests against the engine's 3D entities and their
 * meshes.
 */

#include "../../engine_block_util.h"

#include "object/nmo_class_ids.h"
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

static const nmo_engine_block_desc_t eng_logics_test_blocks[] = {
    {NMO_GUID(0x17d66d26, 0x726b7dec), "Test", bb_test, NULL, NULL},
    {NMO_GUID(0x671e4a87, 0x383b2912), "Ray Intersection", bb_ray_intersection, NULL, NULL},
};

nmo_status_t eng_register_logics_test(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_logics_test_blocks);
}
