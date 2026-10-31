/**
 * @file collisions.c
 * @brief Ports of the Collisions building blocks, on the engine's 3D
 *        entities.
 *
 * Each function does what the block's function in Collisions.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ).
 */

#include "../engine_block_util.h"

#include <math.h>

/* VxOBB: a local box through a world matrix (unit axes, extents scaled by the axes' lengths) */
typedef struct obb {
    float center[3];
    float axis[3][3];
    float extent[3];
} obb_t;

static void obb_from(nmo_engine_t *engine, int32_t entity, obb_t *out)
{
    eng_vector_t min, max;
    eng_entity_local_box(engine, entity, &min, &max);
    const eng_matrix_t *m = &engine->entities.data[entity].world;
    eng_vector_t center = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f}, c;
    eng_vector_transform(&c, m, &center);
    out->center[0] = c.x;
    out->center[1] = c.y;
    out->center[2] = c.z;
    const float half[3] = {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    for (int i = 0; i < 3; i++) {
        double length = sqrt((double)m->m[i][0] * m->m[i][0] + (double)m->m[i][1] * m->m[i][1] +
                             (double)m->m[i][2] * m->m[i][2]);
        for (int k = 0; k < 3; k++) {
            out->axis[i][k] = length > 0.0 ? (float)(m->m[i][k] / length) : 0.0f;
        }
        out->extent[i] = (float)(length * half[i]);
    }
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* VxIntersect::OBBOBB: no separating axis among the fifteen */
static bool obb_overlap(const obb_t *a, const obb_t *b)
{
    const float t[3] = {b->center[0] - a->center[0], b->center[1] - a->center[1],
                        b->center[2] - a->center[2]};
    float r[3][3], abs_r[3][3], ta[3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] = dot3(a->axis[i], b->axis[j]);
            abs_r[i][j] = fabsf(r[i][j]);
        }
        ta[i] = dot3(t, a->axis[i]);
        if (fabsf(ta[i]) > a->extent[i] + abs_r[i][0] * b->extent[0] + abs_r[i][1] * b->extent[1] +
                               abs_r[i][2] * b->extent[2]) {
            return false;
        }
    }
    for (int j = 0; j < 3; j++) {
        if (fabsf(dot3(t, b->axis[j])) > b->extent[j] + abs_r[0][j] * a->extent[0] +
                                             abs_r[1][j] * a->extent[1] + abs_r[2][j] * a->extent[2]) {
            return false;
        }
    }
    for (int i = 0; i < 3; i++) {
        int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        for (int j = 0; j < 3; j++) {
            int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            float distance = fabsf(ta[i2] * r[i1][j] - ta[i1] * r[i2][j]);
            float reach = a->extent[i1] * abs_r[i2][j] + a->extent[i2] * abs_r[i1][j] +
                          b->extent[j1] * abs_r[i][j2] + b->extent[j2] * abs_r[i][j1];
            if (distance > reach) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Box Box Intersection (Collisions.dll): True when the two entities' boxes,
 * local ones through their world matrices (CKCollisionManager::
 * BoxBoxIntersection with local boxes), overlap; False otherwise. The
 * Hierarchy inputs, and characters' body parts, are not taken into account.
 */
static int bb_box_box_intersection(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t first = input_entity(b, 0), second = input_entity(b, 1);
    if (first >= 0 && second >= 0) {
        obb_t a, c;
        obb_from(b->engine, first, &a);
        obb_from(b->engine, second, &c);
        if (obb_overlap(&a, &c)) {
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_OK;
        }
    }
    OUT_ON(b, 1);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_collisions_blocks[] = {
    {NMO_GUID(0x64154401, 0x76cf37af), "Box Box Intersection", bb_box_box_intersection, NULL, NULL},
};

nmo_status_t eng_register_collisions(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_collisions_blocks);
}
