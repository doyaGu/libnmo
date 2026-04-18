/**
 * @file test_interface_edit.c
 * @brief Unit tests for interface chunk edit API
 */

#include "test_framework.h"
#include "format/nmo_interface_edit.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"

#include <string.h>

/* ============================================================================
 * Helpers
 * ============================================================================ */

/* Build a minimal nmo_interface_data_t with one script + N sub-behaviors.
 * Script behavior_id = 100, sub IDs = 201, 202, ... 200+sub_count.
 * Script body has_body = true. Sub bodies: first has has_body=true,
 * rest have has_body=false (header-only). */
static void build_test_data(nmo_arena_t *arena, nmo_interface_data_t *out,
                            size_t sub_count) {
    memset(out, 0, sizeof(*out));
    out->version = 0x15;

    out->script.behavior_id = 100;
    out->script.flags = 0;
    out->script.body.has_body = true;

    out->sub_count = sub_count;
    if (sub_count > 0) {
        out->subs = (nmo_interface_behavior_t *)nmo_arena_alloc(
            arena, sub_count * sizeof(nmo_interface_behavior_t),
            alignof(nmo_interface_behavior_t));
        memset(out->subs, 0, sub_count * sizeof(nmo_interface_behavior_t));
        for (size_t i = 0; i < sub_count; i++) {
            out->subs[i].behavior_id = (nmo_object_id_t)(201 + i);
            out->subs[i].h_pos = (float)(i * 100);
            out->subs[i].v_pos = (float)(i * 50);
            if (i == 0) {
                out->subs[i].body.has_body = true;
            } else {
                out->subs[i].flags = NMO_INTERFACE_FLAG_HEADER_ONLY;
                out->subs[i].body.has_body = false;
            }
        }
    } else {
        out->subs = NULL;
    }
}

/* Add links and operations to a body (arena-allocated). */
static void populate_body(nmo_arena_t *arena, nmo_interface_body_t *body,
                          size_t link_count, nmo_object_id_t first_link_id,
                          size_t op_count, nmo_object_id_t first_op_id) {
    body->link_count = link_count;
    if (link_count > 0) {
        body->links = (nmo_interface_link_t *)nmo_arena_alloc(
            arena, link_count * sizeof(nmo_interface_link_t),
            alignof(nmo_interface_link_t));
        memset(body->links, 0, link_count * sizeof(nmo_interface_link_t));
        for (size_t i = 0; i < link_count; i++) {
            body->links[i].link_id = first_link_id + (nmo_object_id_t)i;
            body->links[i].type = NMO_INTERFACE_LINK_BEHAVIOR;
        }
    }
    body->operation_count = op_count;
    if (op_count > 0) {
        body->operations = (nmo_interface_operation_t *)nmo_arena_alloc(
            arena, op_count * sizeof(nmo_interface_operation_t),
            alignof(nmo_interface_operation_t));
        memset(body->operations, 0, op_count * sizeof(nmo_interface_operation_t));
        for (size_t i = 0; i < op_count; i++) {
            body->operations[i].id = first_op_id + (nmo_object_id_t)i;
            body->operations[i].h_pos = (float)(i * 10);
            body->operations[i].v_pos = (float)(i * 20);
        }
    }
}

/* ============================================================================
 * Tests: find_sub
 * ============================================================================ */

TEST(interface_edit, find_sub_found) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 3);

    nmo_interface_behavior_t *sub = nmo_interface_find_sub(&data, 202);
    ASSERT_NOT_NULL(sub);
    ASSERT_EQ(202u, sub->behavior_id);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_sub_not_found) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 2);

    ASSERT_NULL(nmo_interface_find_sub(&data, 999));
    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_sub_null_data) {
    ASSERT_NULL(nmo_interface_find_sub(NULL, 100));
}

TEST(interface_edit, find_sub_does_not_match_script) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 1);

    /* Script behavior_id is 100 — find_sub should NOT return it */
    ASSERT_NULL(nmo_interface_find_sub(&data, 100));
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Tests: find_body
 * ============================================================================ */

TEST(interface_edit, find_body_script) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 1);

    nmo_interface_body_t *body = nmo_interface_find_body(&data, 100);
    ASSERT_NOT_NULL(body);
    ASSERT_TRUE(body->has_body);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_body_sub) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 2);

    /* Sub 201 has has_body=true */
    nmo_interface_body_t *body = nmo_interface_find_body(&data, 201);
    ASSERT_NOT_NULL(body);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_body_header_only_returns_null) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 2);

    /* Sub 202 has has_body=false (header-only) */
    ASSERT_NULL(nmo_interface_find_body(&data, 202));

    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_body_not_found) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    ASSERT_NULL(nmo_interface_find_body(&data, 999));
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Tests: find_link
 * ============================================================================ */

TEST(interface_edit, find_link_in_script_body) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 1);
    populate_body(arena, &data.script.body, 2, 500, 0, 0);

    nmo_interface_link_t *link = nmo_interface_find_link(&data, 501);
    ASSERT_NOT_NULL(link);
    ASSERT_EQ(501u, link->link_id);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_link_in_sub_body) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 1);
    populate_body(arena, &data.subs[0].body, 3, 600, 0, 0);

    nmo_interface_link_t *link = nmo_interface_find_link(&data, 602);
    ASSERT_NOT_NULL(link);
    ASSERT_EQ(602u, link->link_id);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_link_not_found) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    ASSERT_NULL(nmo_interface_find_link(&data, 999));
    nmo_arena_destroy(arena);
}

TEST(interface_edit, find_link_skips_no_body) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 2);
    /* Sub 202 (index 1) has has_body=false — its links should be skipped */
    ASSERT_NULL(nmo_interface_find_link(&data, 999));
    nmo_arena_destroy(arena);
}

TEST(interface_edit, body_find_link) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    populate_body(arena, &data.script.body, 2, 700, 0, 0);

    ASSERT_NOT_NULL(nmo_interface_body_find_link(&data.script.body, 700));
    ASSERT_NULL(nmo_interface_body_find_link(&data.script.body, 999));

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Tests: find_operation
 * ============================================================================ */

TEST(interface_edit, find_operation_global) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 1);
    populate_body(arena, &data.subs[0].body, 0, 0, 2, 800);

    nmo_interface_operation_t *op = nmo_interface_find_operation(&data, 801);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(801u, op->id);

    ASSERT_NULL(nmo_interface_find_operation(&data, 999));
    nmo_arena_destroy(arena);
}

TEST(interface_edit, body_find_operation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    populate_body(arena, &data.script.body, 0, 0, 3, 900);

    ASSERT_NOT_NULL(nmo_interface_body_find_operation(&data.script.body, 902));
    ASSERT_NULL(nmo_interface_body_find_operation(&data.script.body, 999));

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Tests: add_comment
 * ============================================================================ */

TEST(interface_edit, add_comment_to_empty_body) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    size_t idx = 999;
    nmo_status_t st = nmo_interface_body_add_comment(
        &data.script.body, arena, "Hello", 10.0f, 20.0f, 100.0f, 50.0f, 0, &idx);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(0u, idx);
    ASSERT_EQ(1u, data.script.body.comment_count);
    ASSERT_NOT_NULL(data.script.body.comments);
    ASSERT_STR_EQ("Hello", data.script.body.comments[0].text);
    ASSERT_FLOAT_EQ(10.0f, data.script.body.comments[0].left, 0.001f);
    ASSERT_FLOAT_EQ(20.0f, data.script.body.comments[0].top, 0.001f);
    ASSERT_FLOAT_EQ(100.0f, data.script.body.comments[0].right, 0.001f);
    ASSERT_FLOAT_EQ(50.0f, data.script.body.comments[0].bottom, 0.001f);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, add_comment_grows_array) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    nmo_interface_body_add_comment(
        &data.script.body, arena, "First", 0, 0, 10, 10, 0, NULL);
    size_t idx;
    nmo_interface_body_add_comment(
        &data.script.body, arena, "Second", 20, 20, 30, 30, 0, &idx);
    ASSERT_EQ(1u, idx);
    ASSERT_EQ(2u, data.script.body.comment_count);
    ASSERT_STR_EQ("First", data.script.body.comments[0].text);
    ASSERT_STR_EQ("Second", data.script.body.comments[1].text);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, add_comment_null_text) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    nmo_status_t st = nmo_interface_body_add_comment(
        &data.script.body, arena, NULL, 0, 0, 10, 10, 0, NULL);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_NULL(data.script.body.comments[0].text);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, add_comment_no_body_fails) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_body_t body;
    memset(&body, 0, sizeof(body));
    body.has_body = false;

    nmo_status_t st = nmo_interface_body_add_comment(
        &body, arena, "Fail", 0, 0, 10, 10, 0, NULL);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, st);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, add_comment_sets_section_flag) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    data.format_flags |= NMO_INTERFACE_FORMAT_SECTIONED;
    data.script.body.has_comments_section = false;

    nmo_interface_body_add_comment(
        &data.script.body, arena, "Test", 0, 0, 10, 10, 0, NULL);
    ASSERT_TRUE(data.script.body.has_comments_section);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, add_comment_with_style_flags) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    nmo_interface_body_add_comment(
        &data.script.body, arena, "Locked",
        0, 0, 10, 10, NMO_INTERFACE_COMMENT_LOCKED, NULL);
    ASSERT_EQ(NMO_INTERFACE_COMMENT_LOCKED,
              data.script.body.comments[0].style_flags);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Tests: remove_comment
 * ============================================================================ */

TEST(interface_edit, remove_comment_middle) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    nmo_interface_body_t *body = &data.script.body;

    nmo_interface_body_add_comment(body, arena, "A", 0, 0, 1, 1, 0, NULL);
    nmo_interface_body_add_comment(body, arena, "B", 0, 0, 2, 2, 0, NULL);
    nmo_interface_body_add_comment(body, arena, "C", 0, 0, 3, 3, 0, NULL);

    nmo_status_t st = nmo_interface_body_remove_comment(body, 1);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(2u, body->comment_count);
    ASSERT_STR_EQ("A", body->comments[0].text);
    ASSERT_STR_EQ("C", body->comments[1].text);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, remove_comment_last_nulls_array) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    nmo_interface_body_t *body = &data.script.body;

    nmo_interface_body_add_comment(body, arena, "Only", 0, 0, 1, 1, 0, NULL);
    nmo_status_t st = nmo_interface_body_remove_comment(body, 0);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(0u, body->comment_count);
    ASSERT_NULL(body->comments);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, remove_comment_out_of_bounds) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    nmo_status_t st = nmo_interface_body_remove_comment(&data.script.body, 0);
    ASSERT_EQ(NMO_ERR_OUT_OF_BOUNDS, st);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, remove_comment_no_body_fails) {
    nmo_interface_body_t body;
    memset(&body, 0, sizeof(body));
    body.has_body = false;

    nmo_status_t st = nmo_interface_body_remove_comment(&body, 0);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, st);
}

/* ============================================================================
 * Tests: set_comment_text
 * ============================================================================ */

TEST(interface_edit, set_comment_text) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    nmo_interface_body_t *body = &data.script.body;

    nmo_interface_body_add_comment(body, arena, "Old", 0, 0, 1, 1, 0, NULL);
    nmo_status_t st = nmo_interface_body_set_comment_text(body, arena, 0, "New");
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("New", body->comments[0].text);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, set_comment_text_to_null) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);
    nmo_interface_body_t *body = &data.script.body;

    nmo_interface_body_add_comment(body, arena, "Old", 0, 0, 1, 1, 0, NULL);
    nmo_status_t st = nmo_interface_body_set_comment_text(body, arena, 0, NULL);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_NULL(body->comments[0].text);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, set_comment_text_out_of_bounds) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_data_t data;
    build_test_data(arena, &data, 0);

    nmo_status_t st = nmo_interface_body_set_comment_text(
        &data.script.body, arena, 0, "Fail");
    ASSERT_EQ(NMO_ERR_OUT_OF_BOUNDS, st);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, set_comment_text_no_body_fails) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_body_t body;
    memset(&body, 0, sizeof(body));
    body.has_body = false;

    nmo_status_t st = nmo_interface_body_set_comment_text(&body, arena, 0, "Fail");
    ASSERT_EQ(NMO_ERR_INVALID_STATE, st);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Tests: add_point and clear_points
 * ============================================================================ */

TEST(interface_edit, add_point_to_empty_link) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_link_t link;
    memset(&link, 0, sizeof(link));

    nmo_status_t st = nmo_interface_link_add_point(&link, arena, 50.0f, 75.0f);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(1u, link.point_count);
    ASSERT_NOT_NULL(link.points);
    ASSERT_FLOAT_EQ(50.0f, link.points[0], 0.001f);
    ASSERT_FLOAT_EQ(75.0f, link.points[1], 0.001f);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, add_point_grows_array) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_link_t link;
    memset(&link, 0, sizeof(link));

    nmo_interface_link_add_point(&link, arena, 10.0f, 20.0f);
    nmo_interface_link_add_point(&link, arena, 30.0f, 40.0f);
    nmo_interface_link_add_point(&link, arena, 50.0f, 60.0f);

    ASSERT_EQ(3u, link.point_count);
    ASSERT_FLOAT_EQ(10.0f, link.points[0], 0.001f);
    ASSERT_FLOAT_EQ(20.0f, link.points[1], 0.001f);
    ASSERT_FLOAT_EQ(30.0f, link.points[2], 0.001f);
    ASSERT_FLOAT_EQ(40.0f, link.points[3], 0.001f);
    ASSERT_FLOAT_EQ(50.0f, link.points[4], 0.001f);
    ASSERT_FLOAT_EQ(60.0f, link.points[5], 0.001f);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, clear_points) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_interface_link_t link;
    memset(&link, 0, sizeof(link));

    nmo_interface_link_add_point(&link, arena, 10.0f, 20.0f);
    nmo_interface_link_add_point(&link, arena, 30.0f, 40.0f);
    ASSERT_EQ(2u, link.point_count);

    nmo_interface_link_clear_points(&link);
    ASSERT_EQ(0u, link.point_count);
    ASSERT_NULL(link.points);

    nmo_arena_destroy(arena);
}

TEST(interface_edit, clear_points_already_empty) {
    nmo_interface_link_t link;
    memset(&link, 0, sizeof(link));

    /* Should not crash */
    nmo_interface_link_clear_points(&link);
    ASSERT_EQ(0u, link.point_count);
    ASSERT_NULL(link.points);
}

/* ============================================================================
 * Test registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Lookups: find_sub, find_body */
    REGISTER_TEST(interface_edit, find_sub_found);
    REGISTER_TEST(interface_edit, find_sub_not_found);
    REGISTER_TEST(interface_edit, find_sub_null_data);
    REGISTER_TEST(interface_edit, find_sub_does_not_match_script);
    REGISTER_TEST(interface_edit, find_body_script);
    REGISTER_TEST(interface_edit, find_body_sub);
    REGISTER_TEST(interface_edit, find_body_header_only_returns_null);
    REGISTER_TEST(interface_edit, find_body_not_found);
    /* Lookups: find_link, find_operation */
    REGISTER_TEST(interface_edit, find_link_in_script_body);
    REGISTER_TEST(interface_edit, find_link_in_sub_body);
    REGISTER_TEST(interface_edit, find_link_not_found);
    REGISTER_TEST(interface_edit, find_link_skips_no_body);
    REGISTER_TEST(interface_edit, body_find_link);
    REGISTER_TEST(interface_edit, find_operation_global);
    REGISTER_TEST(interface_edit, body_find_operation);
    /* Comment mutations */
    REGISTER_TEST(interface_edit, add_comment_to_empty_body);
    REGISTER_TEST(interface_edit, add_comment_grows_array);
    REGISTER_TEST(interface_edit, add_comment_null_text);
    REGISTER_TEST(interface_edit, add_comment_no_body_fails);
    REGISTER_TEST(interface_edit, add_comment_sets_section_flag);
    REGISTER_TEST(interface_edit, add_comment_with_style_flags);
    REGISTER_TEST(interface_edit, remove_comment_middle);
    REGISTER_TEST(interface_edit, remove_comment_last_nulls_array);
    REGISTER_TEST(interface_edit, remove_comment_out_of_bounds);
    REGISTER_TEST(interface_edit, remove_comment_no_body_fails);
    REGISTER_TEST(interface_edit, set_comment_text);
    REGISTER_TEST(interface_edit, set_comment_text_to_null);
    REGISTER_TEST(interface_edit, set_comment_text_out_of_bounds);
    REGISTER_TEST(interface_edit, set_comment_text_no_body_fails);
    /* Link routing points */
    REGISTER_TEST(interface_edit, add_point_to_empty_link);
    REGISTER_TEST(interface_edit, add_point_grows_array);
    REGISTER_TEST(interface_edit, clear_points);
    REGISTER_TEST(interface_edit, clear_points_already_empty);
TEST_MAIN_END()
