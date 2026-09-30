/**
 * @file test_mesh_normals.c
 * @brief Derived vertex normals of a mesh whose file omits them.
 */

#include "test_framework.h"

#include "object/builtin/nmo_mesh_schemas.h"

#include <math.h>
#include <string.h>

/* Two triangles sharing the edge 1-2 and lying in the z = 0 plane, wound so
 * that cross(p1 - p0, p2 - p0) points along +z. */
static void make_quad(nmo_mesh_state_t *mesh, nmo_vertex_t vertices[4],
                      uint16_t indices[6])
{
    memset(mesh, 0, sizeof(*mesh));
    memset(vertices, 0, 4 * sizeof(*vertices));
    vertices[0].position = (nmo_vector_t){0.0f, 0.0f, 0.0f};
    vertices[1].position = (nmo_vector_t){1.0f, 0.0f, 0.0f};
    vertices[2].position = (nmo_vector_t){0.0f, 1.0f, 0.0f};
    vertices[3].position = (nmo_vector_t){1.0f, 1.0f, 0.0f};
    const uint16_t ids[6] = {0, 1, 2, 1, 3, 2};
    memcpy(indices, ids, sizeof(ids));

    mesh->vertices = vertices;
    mesh->vertex_count = 4;
    mesh->face_vertex_indices = indices;
    mesh->face_count = 2;
}

TEST(mesh_normals, all_zero_normals_count_as_derived) {
    nmo_mesh_state_t mesh;
    nmo_vertex_t vertices[4];
    uint16_t indices[6];
    make_quad(&mesh, vertices, indices);

    ASSERT_TRUE(nmo_mesh_normals_are_derived(&mesh));

    mesh.zero_normals_stored = true;
    ASSERT_FALSE(nmo_mesh_normals_are_derived(&mesh));

    mesh.zero_normals_stored = false;
    vertices[2].normal = (nmo_vector_t){0.0f, 0.0f, 1.0f};
    ASSERT_FALSE(nmo_mesh_normals_are_derived(&mesh));
}

TEST(mesh_normals, builds_unit_normals_from_face_winding) {
    nmo_mesh_state_t mesh;
    nmo_vertex_t vertices[4];
    uint16_t indices[6];
    make_quad(&mesh, vertices, indices);

    nmo_vector_t normals[4];
    ASSERT_EQ(NMO_OK, nmo_mesh_build_vertex_normals(&mesh, normals));
    for (int i = 0; i < 4; ++i) {
        ASSERT_FLOAT_EQ(0.0f, normals[i].x, 0.0001f);
        ASSERT_FLOAT_EQ(0.0f, normals[i].y, 0.0001f);
        ASSERT_FLOAT_EQ(1.0f, normals[i].z, 0.0001f);
    }
}

TEST(mesh_normals, averages_faces_that_meet_at_a_vertex) {
    /* A roof: the shared vertices 1 and 2 see both slopes and get the mean
     * direction; the outer vertices keep their own face's normal. */
    nmo_mesh_state_t mesh;
    nmo_vertex_t vertices[4];
    uint16_t indices[6];
    make_quad(&mesh, vertices, indices);
    vertices[3].position = (nmo_vector_t){1.0f, 1.0f, 1.0f};

    nmo_vector_t normals[4];
    ASSERT_EQ(NMO_OK, nmo_mesh_build_vertex_normals(&mesh, normals));

    /* Face 0 is (v0, v1, v2) with normal +z. Face 1 is (v1, v3, v2): with
     * e1 = (0,1,1) and e2 = (-1,1,0) its normal is cross(e1, e2) = (-1,-1,1). */
    const float f1x = -1.0f;
    const float f1y = -1.0f;
    const float f1z = 1.0f;
    const float f1len = sqrtf(f1x * f1x + f1y * f1y + f1z * f1z);
    const float sx = f1x / f1len;
    const float sy = f1y / f1len;
    const float sz = 1.0f + f1z / f1len;
    const float slen = sqrtf(sx * sx + sy * sy + sz * sz);

    ASSERT_FLOAT_EQ(sx / slen, normals[1].x, 0.0001f);
    ASSERT_FLOAT_EQ(sy / slen, normals[1].y, 0.0001f);
    ASSERT_FLOAT_EQ(sz / slen, normals[1].z, 0.0001f);
    ASSERT_FLOAT_EQ(0.0f, normals[0].x, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f, normals[0].z, 0.0001f);
}

TEST(mesh_normals, ignores_faces_with_bad_indices_and_degenerate_faces) {
    nmo_mesh_state_t mesh;
    nmo_vertex_t vertices[4];
    uint16_t indices[6];
    make_quad(&mesh, vertices, indices);
    indices[3] = 9;                       /* out of range: second face skipped */
    vertices[2].position = vertices[0].position;   /* first face degenerate */

    nmo_vector_t normals[4];
    ASSERT_EQ(NMO_OK, nmo_mesh_build_vertex_normals(&mesh, normals));
    for (int i = 0; i < 4; ++i) {
        ASSERT_FLOAT_EQ(0.0f, normals[i].x, 0.0001f);
        ASSERT_FLOAT_EQ(0.0f, normals[i].y, 0.0001f);
        ASSERT_FLOAT_EQ(0.0f, normals[i].z, 0.0001f);
    }
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(mesh_normals, all_zero_normals_count_as_derived);
    REGISTER_TEST(mesh_normals, builds_unit_normals_from_face_winding);
    REGISTER_TEST(mesh_normals, averages_faces_that_meet_at_a_vertex);
    REGISTER_TEST(mesh_normals, ignores_faces_with_bad_indices_and_degenerate_faces);
TEST_MAIN_END()
