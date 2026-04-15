/**
 * @file test_obj_parser.c
 * @brief Unit tests for tinyobjloader-aligned OBJ parsing.
 */

#include "format/nmo_obj_parser.h"
#include "test_framework.h"

#include <string.h>

static void parse_obj_text(const char *text, nmo_arena_t **out_arena,
                           nmo_obj_data_t *out_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_status_t st = nmo_obj_parse(arena, text, strlen(text), out_data);
    ASSERT_EQ(NMO_OK, st);

    *out_arena = arena;
}

TEST(obj_parser, parses_vertex_colors_and_distinguishes_vertex_weight) {
    const char *obj =
        "v 0 0 0 1 0.5 0.25\n"
        "v 1 0 0 0.75\n"
        "v 0 1 0\n"
        "f 1 2 3\n";

    nmo_arena_t *arena = NULL;
    nmo_obj_data_t data;
    parse_obj_text(obj, &arena, &data);

    ASSERT_EQ(3u, data.pos_count);
    ASSERT_NOT_NULL(data.colors);
    ASSERT_NOT_NULL(data.position_has_color);
    ASSERT_TRUE(data.position_has_color[0]);
    ASSERT_FALSE(data.position_has_color[1]);
    ASSERT_FALSE(data.position_has_color[2]);
    ASSERT_FLOAT_EQ(1.0f, data.colors[0], 0.0001f);
    ASSERT_FLOAT_EQ(0.5f, data.colors[1], 0.0001f);
    ASSERT_FLOAT_EQ(0.25f, data.colors[2], 0.0001f);

    nmo_arena_destroy(arena);
}

TEST(obj_parser, rejects_invalid_zero_and_out_of_range_indices) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_obj_data_t data;
    const char *zero_pos =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 0 2 3\n";
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
              nmo_obj_parse(arena, zero_pos, strlen(zero_pos), &data));

    const char *bad_negative =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f -4 2 3\n";
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
              nmo_obj_parse(arena, bad_negative, strlen(bad_negative), &data));

    nmo_arena_destroy(arena);
}

TEST(obj_parser, records_materials_mtllib_and_no_material_sentinel) {
    const char *obj =
        "mtllib base.mtl escaped\\ name.mtl\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 1 1 0\n"
        "f 1 2 3\n"
        "usemtl mat_a\n"
        "f 2 4 3\n"
        "usemtl mat_a\n"
        "f -3 -2 -1\n";

    nmo_arena_t *arena = NULL;
    nmo_obj_data_t data;
    parse_obj_text(obj, &arena, &data);

    ASSERT_EQ(3u, data.face_count);
    ASSERT_EQ(NMO_OBJ_NO_MATERIAL, data.faces[0].material_group);
    ASSERT_EQ(0u, data.faces[1].material_group);
    ASSERT_EQ(0u, data.faces[2].material_group);
    ASSERT_EQ(1u, data.material_name_count);
    ASSERT_STR_EQ("mat_a", data.material_names[0]);
    ASSERT_EQ(2u, data.mtllib_name_count);
    ASSERT_STR_EQ("base.mtl", data.mtllib_names[0]);
    ASSERT_STR_EQ("escaped name.mtl", data.mtllib_names[1]);

    nmo_arena_destroy(arena);
}

TEST(obj_parser, attaches_object_group_and_smoothing_metadata) {
    const char *obj =
        "o Cube\n"
        "g Front Back\n"
        "s 42\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n"
        "l 1 2 3\n"
        "p 1 3\n";

    nmo_arena_t *arena = NULL;
    nmo_obj_data_t data;
    parse_obj_text(obj, &arena, &data);

    ASSERT_EQ(1u, data.object_name_count);
    ASSERT_EQ(1u, data.group_name_count);
    ASSERT_STR_EQ("Cube", data.object_names[0]);
    ASSERT_STR_EQ("Front Back", data.group_names[0]);
    ASSERT_EQ(42u, data.faces[0].smoothing_group);
    ASSERT_EQ(0u, data.faces[0].object_idx);
    ASSERT_EQ(0u, data.faces[0].group_idx);
    ASSERT_EQ(2u, data.line_count);
    ASSERT_EQ(0u, data.lines[0].object_idx);
    ASSERT_EQ(0u, data.lines[0].group_idx);
    ASSERT_EQ(2u, data.point_count);
    ASSERT_EQ(2, data.points[1].verts.pos_idx);

    nmo_arena_destroy(arena);
}

TEST(obj_parser, parses_inline_comments_and_primitives) {
    const char *obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 2 0 0\n"
        "l 1 2 3 # line comment\n"
        "p 1 3 # point comment\n";

    nmo_arena_t *arena = NULL;
    nmo_obj_data_t data;
    parse_obj_text(obj, &arena, &data);

    ASSERT_EQ(2u, data.line_count);
    ASSERT_EQ(2u, data.point_count);
    ASSERT_EQ(0, data.lines[0].verts[0].pos_idx);
    ASSERT_EQ(1, data.lines[0].verts[1].pos_idx);
    ASSERT_EQ(1, data.lines[1].verts[0].pos_idx);
    ASSERT_EQ(2, data.lines[1].verts[1].pos_idx);
    ASSERT_EQ(0, data.points[0].verts.pos_idx);
    ASSERT_EQ(2, data.points[1].verts.pos_idx);

    nmo_arena_destroy(arena);
}

TEST(obj_parser, triangulates_quads_with_shortest_diagonal_and_ngons) {
    const char *obj =
        "v 0 0 0\n"
        "v 3 0 0\n"
        "v 3 3 0\n"
        "v 0 1 0\n"
        "f 1 2 3 4\n"
        "v 4 0 0\n"
        "v 5 1 0\n"
        "v 4 2 0\n"
        "f 2 5 6 7 3\n";

    nmo_arena_t *arena = NULL;
    nmo_obj_data_t data;
    parse_obj_text(obj, &arena, &data);

    ASSERT_EQ(5u, data.face_count);
    ASSERT_EQ(0, data.faces[0].verts[0].pos_idx);
    ASSERT_EQ(1, data.faces[0].verts[1].pos_idx);
    ASSERT_EQ(3, data.faces[0].verts[2].pos_idx);
    ASSERT_EQ(1, data.faces[1].verts[0].pos_idx);
    ASSERT_EQ(2, data.faces[1].verts[1].pos_idx);
    ASSERT_EQ(3, data.faces[1].verts[2].pos_idx);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(obj_parser, parses_vertex_colors_and_distinguishes_vertex_weight);
    REGISTER_TEST(obj_parser, rejects_invalid_zero_and_out_of_range_indices);
    REGISTER_TEST(obj_parser, records_materials_mtllib_and_no_material_sentinel);
    REGISTER_TEST(obj_parser, attaches_object_group_and_smoothing_metadata);
    REGISTER_TEST(obj_parser, parses_inline_comments_and_primitives);
    REGISTER_TEST(obj_parser, triangulates_quads_with_shortest_diagonal_and_ngons);
TEST_MAIN_END()
