/**
 * @file test_interface_chunk.c
 * @brief Unit tests for interface chunk parser
 */

#include "test_framework.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"

#include <string.h>

/* ============================================================================
 * Helper: build a minimal v0x14 interface chunk
 *
 * Layout: version=0x14, total_count=1, one script root with:
 *   behavior_id=100, flags=0x8000 (header-only), script_index=0,
 *   h_pos=0, v_pos=0, h_start_pos=10.0, v_start_pos=20.0, v_size=100.0,
 *   empty bitmap (two ints = 0), color=0x96C8FA
 * ============================================================================ */

/* Helper: write script header common fields (used by all script-based tests) */
static void write_script_header_fields(nmo_chunk_t *chunk,
                                        nmo_object_id_t behavior_id,
                                        uint32_t flags,
                                        uint32_t script_index,
                                        float h_pos, float v_pos) {
    nmo_chunk_write_object_id(chunk, behavior_id);
    nmo_chunk_write_dword(chunk, flags);
    nmo_chunk_write_dword(chunk, script_index);
    nmo_chunk_write_float(chunk, h_pos);   /* rect.hPos */
    nmo_chunk_write_float(chunk, v_pos);   /* rect.vPos */
}

static nmo_chunk_t *build_minimal_chunk(nmo_arena_t *arena) {
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (!chunk) return NULL;

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* version */
    nmo_chunk_write_dword(chunk, 0x14);
    /* total_count (1 = script root only, no sub-behaviors) */
    nmo_chunk_write_int(chunk, 1);

    /* --- script header (entry 0) --- */
    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    /* h_start_pos, v_start_pos, v_size */
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    /* empty bitmap: legacy format = two ints of 0 */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    /* color (v >= 0x14) */
    nmo_chunk_write_dword(chunk, 0x96C8FA);

    nmo_chunk_close(chunk);
    return chunk;
}

/* ============================================================================
 * Helper: write extra data identifier as raw DWORDs.
 *
 * Using nmo_chunk_write_identifier() back-patches data[prev_identifier_pos+1],
 * which corrupts earlier data when no prior identifier exists.  Write the
 * identifier + next-pointer pair manually to avoid corruption.
 * ============================================================================ */

static void write_extra_identifier_raw(nmo_chunk_t *chunk, uint32_t id) {
    nmo_chunk_write_dword(chunk, id);
    nmo_chunk_write_dword(chunk, 0);  /* next-pointer = 0 (end of chain) */
}

/* ============================================================================
 * Helper: write a link endpoint
 * ============================================================================ */

static void write_endpoint(nmo_chunk_t *chunk, nmo_object_id_t id,
                           int32_t index, uint32_t type) {
    nmo_chunk_write_object_id(chunk, id);
    nmo_chunk_write_int(chunk, index);
    nmo_chunk_write_dword(chunk, type);
}

/* ============================================================================
 * Callback: is_building_block for test context
 * ============================================================================ */

typedef struct {
    nmo_object_id_t bb_id;  /* ID that is a building block */
} test_bb_data_t;

static bool test_is_building_block(nmo_object_id_t id, void *user_data) {
    test_bb_data_t *data = (test_bb_data_t *)user_data;
    return id == data->bb_id;
}

/* ============================================================================
 * Tests - existing (Tasks 1-3)
 * ============================================================================ */

TEST(interface_chunk, parse_minimal) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = build_minimal_chunk(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_interface_data_t data;
    memset(&data, 0, sizeof(data));

    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    /* Top-level fields */
    ASSERT_EQ(0x14, (int)data.version);
    ASSERT_EQ(0, (int)data.sub_count);

    /* Script header */
    ASSERT_EQ(100, (int)data.script.behavior_id);
    ASSERT_EQ((int)NMO_INTERFACE_FLAG_HEADER_ONLY, (int)data.script.flags);
    ASSERT_EQ(0, (int)data.script.script_index);
    ASSERT_FLOAT_EQ(10.0f, data.script.h_start_pos, 0.001f);
    ASSERT_FLOAT_EQ(20.0f, data.script.v_start_pos, 0.001f);
    ASSERT_FLOAT_EQ(100.0f, data.script.v_size, 0.001f);
    ASSERT_NULL(data.script.snapshot_data);
    ASSERT_EQ(0, (int)data.script.snapshot_size);
    ASSERT_EQ(0x96C8FA, (int)data.script.color);

    /* Body should be absent (header-only) */
    ASSERT_FALSE(data.script.body.has_body);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, reject_version_too_low) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x11); /* below minimum */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, st);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, reject_version_too_high) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x17); /* above maximum */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, st);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, version_min_accepted) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* version 0x12 (minimum) */
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_VERSION_MIN);
    nmo_chunk_write_int(chunk, 1);
    /* script header */
    write_script_header_fields(chunk, 50, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 5.0f);
    nmo_chunk_write_float(chunk, 6.0f);
    nmo_chunk_write_float(chunk, 80.0f);
    /* empty bitmap */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    /* no color field for v < 0x14 */
    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(NMO_INTERFACE_VERSION_MIN, data.version);
    ASSERT_EQ(50, (int)data.script.behavior_id);
    ASSERT_EQ(0, (int)data.script.color); /* no color at v0x12 */

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, version_max_accepted) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* version 0x16 (maximum) */
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_VERSION_MAX);
    nmo_chunk_write_int(chunk, 1);
    /* script header */
    write_script_header_fields(chunk, 200, NMO_INTERFACE_FLAG_HEADER_ONLY, 3,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 1.0f);
    nmo_chunk_write_float(chunk, 2.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    /* empty bitmap */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    /* color (v >= 0x14) */
    nmo_chunk_write_dword(chunk, 0xFF0000);
    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_EQ(NMO_INTERFACE_VERSION_MAX, data.version);
    ASSERT_EQ(200, (int)data.script.behavior_id);
    ASSERT_EQ(3, (int)data.script.script_index);
    ASSERT_EQ(0xFF0000, (int)data.script.color);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, folded_script_omits_body) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);
    write_script_header_fields(chunk, 321, NMO_INTERFACE_FLAG_FOLDED, 1,
                               400.0f, 460.0f);
    nmo_chunk_write_float(chunk, 220.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);
    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_FALSE(data.script.body.has_body);
    ASSERT_EQ(0, (int)data.sub_count);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, folded_script_partial_body_is_truncated) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);
    write_script_header_fields(chunk, 321, NMO_INTERFACE_FLAG_FOLDED, 1,
                               400.0f, 460.0f);
    nmo_chunk_write_float(chunk, 220.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);
    nmo_chunk_write_int(chunk, 0); /* link count only; ops/comments are missing */
    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, st);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Task 4: Link parsing tests
 * ============================================================================ */

TEST(interface_chunk, parse_links) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* version 0x15, total_count=1 */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    /* script header: has body (flags=0) */
    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    nmo_chunk_write_int(chunk, 0);            /* empty bitmap */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0x96C8FA);   /* color */

    /* --- Body --- */
    /* 2 links */
    nmo_chunk_write_int(chunk, 2);

    /* Link 0: behavior type, no highlight, 0 routing points */
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_LINK_BEHAVIOR); /* type=1 */
    nmo_chunk_write_object_id(chunk, 500);                     /* link_id */
    write_endpoint(chunk, 200, 0, NMO_INTERFACE_ENDPOINT_BOUT);  /* start */
    nmo_chunk_write_int(chunk, 0);                             /* 0 routing points */
    write_endpoint(chunk, 300, 1, NMO_INTERFACE_ENDPOINT_BIN);   /* end */

    /* Link 1: parameter type with highlight flag, 3 routing points */
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_LINK_PARAMETER | NMO_INTERFACE_LINK_HIGHLIGHT_FLAG);
    nmo_chunk_write_object_id(chunk, 501);                     /* link_id */
    write_endpoint(chunk, 400, 0, NMO_INTERFACE_ENDPOINT_POUT); /* start */
    nmo_chunk_write_int(chunk, 3);                             /* 3 routing points */
    nmo_chunk_write_float(chunk, 1.0f); nmo_chunk_write_float(chunk, 2.0f);
    nmo_chunk_write_float(chunk, 3.0f); nmo_chunk_write_float(chunk, 4.0f);
    nmo_chunk_write_float(chunk, 5.0f); nmo_chunk_write_float(chunk, 6.0f);
    write_endpoint(chunk, 401, 2, NMO_INTERFACE_ENDPOINT_PIN);  /* end */

    /* 0 operations, 0 comments, 0 params */
    nmo_chunk_write_int(chunk, 0);  /* operations */
    nmo_chunk_write_int(chunk, 0);  /* comments */
    nmo_chunk_write_int(chunk, 0);  /* local params */
    nmo_chunk_write_int(chunk, 0);  /* shared params */

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_TRUE(data.script.body.has_body);
    ASSERT_EQ(2, (int)data.script.body.link_count);
    ASSERT_NOT_NULL(data.script.body.links);

    /* Link 0 */
    nmo_interface_link_t *l0 = &data.script.body.links[0];
    ASSERT_EQ(NMO_INTERFACE_LINK_BEHAVIOR, (int)l0->type);
    ASSERT_FALSE(l0->highlight);
    ASSERT_EQ(500, (int)l0->link_id);
    ASSERT_EQ(200, (int)l0->start.id);
    ASSERT_EQ(0, l0->start.index);
    ASSERT_EQ(NMO_INTERFACE_ENDPOINT_BOUT, (int)l0->start.type);
    ASSERT_EQ(0, (int)l0->point_count);
    ASSERT_NULL(l0->points);
    ASSERT_EQ(300, (int)l0->end.id);
    ASSERT_EQ(1, l0->end.index);
    ASSERT_EQ(NMO_INTERFACE_ENDPOINT_BIN, (int)l0->end.type);

    /* Link 1 */
    nmo_interface_link_t *l1 = &data.script.body.links[1];
    ASSERT_EQ(NMO_INTERFACE_LINK_PARAMETER, (int)l1->type);
    ASSERT_TRUE(l1->highlight);
    ASSERT_EQ(501, (int)l1->link_id);
    ASSERT_EQ(400, (int)l1->start.id);
    ASSERT_EQ(NMO_INTERFACE_ENDPOINT_POUT, (int)l1->start.type);
    ASSERT_EQ(3, (int)l1->point_count);
    ASSERT_NOT_NULL(l1->points);
    ASSERT_FLOAT_EQ(1.0f, l1->points[0], 0.001f);
    ASSERT_FLOAT_EQ(2.0f, l1->points[1], 0.001f);
    ASSERT_FLOAT_EQ(3.0f, l1->points[2], 0.001f);
    ASSERT_FLOAT_EQ(4.0f, l1->points[3], 0.001f);
    ASSERT_FLOAT_EQ(5.0f, l1->points[4], 0.001f);
    ASSERT_FLOAT_EQ(6.0f, l1->points[5], 0.001f);
    ASSERT_EQ(401, (int)l1->end.id);
    ASSERT_EQ(2, l1->end.index);
    ASSERT_EQ(NMO_INTERFACE_ENDPOINT_PIN, (int)l1->end.type);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, dev_layout_omits_color_and_inline_body) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_interface_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);
    nmo_chunk_write_dword(chunk, 0x15);
    /* Script marker triggers sectioned layout detection */
    nmo_chunk_write_identifier(chunk, 0xB0000002u);
    nmo_chunk_write_int(chunk, 1);

    /* Script header section */
    nmo_chunk_write_identifier(chunk, 0xB0070000u);
    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    memset(&data, 0, sizeof(data));
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, &ctx, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(0, (int)data.script.color);
    ASSERT_TRUE(data.script.body.has_body);
    ASSERT_EQ(0, (int)data.script.body.link_count);
    ASSERT_EQ(0, (int)data.script.body.operation_count);
    ASSERT_EQ(0, (int)data.script.body.comment_count);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, dev_layout_parse_sectioned_links) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_interface_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 1);
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_identifier(chunk, 0xB0000002u);
    nmo_chunk_write_int(chunk, 1);

    /* Script header section */
    nmo_chunk_write_identifier(chunk, 0xB0070000u);
    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    /* Links section for script (layoutIndex=0) */
    nmo_chunk_write_identifier(chunk, 0xB0030000u);
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, NMO_INTERFACE_LINK_PARAMETER);
    nmo_chunk_write_object_id(chunk, 300);
    write_endpoint(chunk, 101, 2, NMO_INTERFACE_ENDPOINT_PIN);
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_float(chunk, 12.0f);
    nmo_chunk_write_float(chunk, 34.0f);
    write_endpoint(chunk, 102, 3, NMO_INTERFACE_ENDPOINT_POUT);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    memset(&data, 0, sizeof(data));
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, &ctx, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(1, (int)data.script.body.link_count);
    nmo_interface_link_t *link = &data.script.body.links[0];
    ASSERT_TRUE(link->highlight);
    ASSERT_EQ(NMO_INTERFACE_LINK_PARAMETER, (int)link->type);
    ASSERT_EQ(300, (int)link->link_id);
    ASSERT_EQ(101, (int)link->start.id);
    ASSERT_EQ(2, link->start.index);
    ASSERT_EQ(NMO_INTERFACE_ENDPOINT_PIN, (int)link->start.type);
    ASSERT_EQ(1, (int)link->point_count);
    ASSERT_FLOAT_EQ(12.0f, link->points[0], 0.001f);
    ASSERT_FLOAT_EQ(34.0f, link->points[1], 0.001f);
    ASSERT_EQ(102, (int)link->end.id);
    ASSERT_EQ(3, link->end.index);
    ASSERT_EQ(NMO_INTERFACE_ENDPOINT_POUT, (int)link->end.type);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Task 5: Operations, comments, parameters tests
 * ============================================================================ */

TEST(interface_chunk, parse_operations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    /* script header with body */
    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* 0 links */
    nmo_chunk_write_int(chunk, 0);

    /* 2 operations */
    nmo_chunk_write_int(chunk, 2);
    nmo_chunk_write_object_id(chunk, 600);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_float(chunk, 60.0f);
    nmo_chunk_write_object_id(chunk, 601);
    nmo_chunk_write_float(chunk, 70.0f);
    nmo_chunk_write_float(chunk, 80.0f);

    /* 0 comments, 0 params */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(2, (int)data.script.body.operation_count);
    ASSERT_NOT_NULL(data.script.body.operations);
    ASSERT_EQ(600, (int)data.script.body.operations[0].id);
    ASSERT_FLOAT_EQ(50.0f, data.script.body.operations[0].h_pos, 0.001f);
    ASSERT_FLOAT_EQ(60.0f, data.script.body.operations[0].v_pos, 0.001f);
    ASSERT_EQ(601, (int)data.script.body.operations[1].id);
    ASSERT_FLOAT_EQ(70.0f, data.script.body.operations[1].h_pos, 0.001f);
    ASSERT_FLOAT_EQ(80.0f, data.script.body.operations[1].v_pos, 0.001f);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_comments_v16) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* v0x16 to test style flags */
    nmo_chunk_write_dword(chunk, 0x16);
    nmo_chunk_write_int(chunk, 1);

    /* script header with body */
    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* 0 links, 0 ops */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    /* 1 comment */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_float(chunk, 10.0f);   /* left */
    nmo_chunk_write_float(chunk, 20.0f);   /* top */
    nmo_chunk_write_float(chunk, 200.0f);  /* right */
    nmo_chunk_write_float(chunk, 100.0f);  /* bottom */
    nmo_chunk_write_string(chunk, "Test comment");
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_COMMENT_COLLAPSED | NMO_INTERFACE_COMMENT_LOCKED);

    /* 0 params */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(1, (int)data.script.body.comment_count);
    ASSERT_NOT_NULL(data.script.body.comments);

    nmo_interface_comment_t *c = &data.script.body.comments[0];
    ASSERT_FLOAT_EQ(10.0f, c->left, 0.001f);
    ASSERT_FLOAT_EQ(20.0f, c->top, 0.001f);
    ASSERT_FLOAT_EQ(200.0f, c->right, 0.001f);
    ASSERT_FLOAT_EQ(100.0f, c->bottom, 0.001f);
    ASSERT_NOT_NULL(c->text);
    ASSERT_STR_EQ("Test comment", c->text);
    ASSERT_EQ((int)(NMO_INTERFACE_COMMENT_COLLAPSED | NMO_INTERFACE_COMMENT_LOCKED),
              (int)c->style_flags);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_comments_v15_no_style) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* v0x15: no style flags */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    nmo_chunk_write_int(chunk, 0);  /* links */
    nmo_chunk_write_int(chunk, 0);  /* ops */

    /* 1 comment, no style flags */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_float(chunk, 5.0f);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_string(chunk, "v15 comment");
    /* no style_flags written at v0x15 */

    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(1, (int)data.script.body.comment_count);
    ASSERT_EQ(0, (int)data.script.body.comments[0].style_flags);
    ASSERT_STR_EQ("v15 comment", data.script.body.comments[0].text);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_parameters_v15) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    nmo_chunk_write_int(chunk, 0);  /* links */
    nmo_chunk_write_int(chunk, 0);  /* ops */
    nmo_chunk_write_int(chunk, 0);  /* comments */

    /* 2 local params */
    nmo_chunk_write_int(chunk, 2);
    /* positions */
    nmo_chunk_write_int(chunk, 5);   nmo_chunk_write_int(chunk, 10);   /* local[0] */
    nmo_chunk_write_int(chunk, 15);  nmo_chunk_write_int(chunk, 20);   /* local[1] */
    /* styles */
    nmo_chunk_write_int(chunk, (int32_t)NMO_INTERFACE_PARAM_STYLE_NAME);
    nmo_chunk_write_int(chunk, (int32_t)NMO_INTERFACE_PARAM_STYLE_VALUE);

    /* 1 shared param (v >= 0x15: simplified source) */
    nmo_chunk_write_int(chunk, 1);
    /* position */
    nmo_chunk_write_int(chunk, 30);  nmo_chunk_write_int(chunk, 40);
    /* style */
    nmo_chunk_write_int(chunk, (int32_t)NMO_INTERFACE_PARAM_STYLE_NAMEVALUE);
    /* source_id (v >= 0x15: single ObjectID) */
    nmo_chunk_write_object_id(chunk, 700);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    /* Local params */
    ASSERT_EQ(2, (int)data.script.body.params.local_count);
    ASSERT_NOT_NULL(data.script.body.params.locals);
    ASSERT_EQ(5,  data.script.body.params.locals[0].h_pos);
    ASSERT_EQ(10, data.script.body.params.locals[0].v_pos);
    ASSERT_EQ((int)NMO_INTERFACE_PARAM_STYLE_NAME, (int)data.script.body.params.locals[0].style);
    ASSERT_EQ(0, (int)data.script.body.params.locals[0].source_id);
    ASSERT_EQ(15, data.script.body.params.locals[1].h_pos);
    ASSERT_EQ(20, data.script.body.params.locals[1].v_pos);
    ASSERT_EQ((int)NMO_INTERFACE_PARAM_STYLE_VALUE, (int)data.script.body.params.locals[1].style);

    /* Shared params */
    ASSERT_EQ(1, (int)data.script.body.params.shared_count);
    ASSERT_NOT_NULL(data.script.body.params.shared);
    ASSERT_EQ(30, data.script.body.params.shared[0].h_pos);
    ASSERT_EQ(40, data.script.body.params.shared[0].v_pos);
    ASSERT_EQ((int)NMO_INTERFACE_PARAM_STYLE_NAMEVALUE, (int)data.script.body.params.shared[0].style);
    ASSERT_EQ(700, (int)data.script.body.params.shared[0].source_id);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_parameters_v14_legacy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* v0x14 (< 0x15): legacy 3-field shared source */
    nmo_chunk_write_dword(chunk, 0x14);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    nmo_chunk_write_int(chunk, 0);  /* links */
    nmo_chunk_write_int(chunk, 0);  /* ops */
    nmo_chunk_write_int(chunk, 0);  /* comments */

    /* 0 local params */
    nmo_chunk_write_int(chunk, 0);

    /* 1 shared param (v < 0x15: skip ObjectID, read ObjectID, skip INT) */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, 25);  nmo_chunk_write_int(chunk, 35);
    nmo_chunk_write_int(chunk, (int32_t)NMO_INTERFACE_PARAM_STYLE_COLLAPSED);
    /* legacy 3-field source: ignored_id, source_id, ignored_int */
    nmo_chunk_write_object_id(chunk, 999);   /* ignored */
    nmo_chunk_write_object_id(chunk, 800);   /* actual source_id */
    nmo_chunk_write_int(chunk, 42);          /* ignored */

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(1, (int)data.script.body.params.shared_count);
    ASSERT_EQ(800, (int)data.script.body.params.shared[0].source_id);
    ASSERT_EQ(25, data.script.body.params.shared[0].h_pos);
    ASSERT_EQ(35, data.script.body.params.shared[0].v_pos);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Task 6: Sub-behavior and graph IO tests
 * ============================================================================ */

TEST(interface_chunk, parse_sub_behaviors) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* v0x15, 3 behaviors (1 script + 2 subs) */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 3);

    /* --- Script header (entry 0): header-only --- */
    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0x96C8FA);

    /* --- Sub-behavior 0: has body, with 1 link --- */
    nmo_chunk_write_object_id(chunk, 200);
    nmo_chunk_write_dword(chunk, 0);           /* flags: has body */
    nmo_chunk_write_dword(chunk, 1);           /* depth */
    nmo_chunk_write_float(chunk, 30.0f);       /* h_pos */
    nmo_chunk_write_float(chunk, 40.0f);       /* v_pos */
    nmo_chunk_write_float(chunk, 120.0f);      /* h_size */
    nmo_chunk_write_float(chunk, 80.0f);       /* v_size */
    nmo_chunk_write_float(chunk, 150.0f);      /* h_expand_size */
    nmo_chunk_write_float(chunk, 100.0f);      /* v_expand_size */

    /* body: 1 link */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_LINK_BEHAVIOR);
    nmo_chunk_write_object_id(chunk, 550);
    write_endpoint(chunk, 200, 0, NMO_INTERFACE_ENDPOINT_BOUT);
    nmo_chunk_write_int(chunk, 0);
    write_endpoint(chunk, 300, 0, NMO_INTERFACE_ENDPOINT_BIN);

    /* 0 ops, 0 comments, params, no graph IO (ctx=NULL) */
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);

    /* --- Sub-behavior 1: header-only --- */
    nmo_chunk_write_object_id(chunk, 300);
    nmo_chunk_write_dword(chunk, NMO_INTERFACE_FLAG_HEADER_ONLY);
    nmo_chunk_write_dword(chunk, 2);           /* depth */
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_float(chunk, 60.0f);
    nmo_chunk_write_float(chunk, 80.0f);
    nmo_chunk_write_float(chunk, 40.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    nmo_chunk_write_float(chunk, 60.0f);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(2, (int)data.sub_count);
    ASSERT_NOT_NULL(data.subs);

    /* Sub 0 */
    ASSERT_EQ(200, (int)data.subs[0].behavior_id);
    ASSERT_EQ(0, (int)data.subs[0].flags);
    ASSERT_EQ(1, (int)data.subs[0].depth);
    ASSERT_FLOAT_EQ(30.0f, data.subs[0].h_pos, 0.001f);
    ASSERT_FLOAT_EQ(40.0f, data.subs[0].v_pos, 0.001f);
    ASSERT_FLOAT_EQ(120.0f, data.subs[0].h_size, 0.001f);
    ASSERT_FLOAT_EQ(80.0f, data.subs[0].v_size, 0.001f);
    ASSERT_FLOAT_EQ(150.0f, data.subs[0].h_expand_size, 0.001f);
    ASSERT_FLOAT_EQ(100.0f, data.subs[0].v_expand_size, 0.001f);
    ASSERT_TRUE(data.subs[0].body.has_body);
    ASSERT_EQ(1, (int)data.subs[0].body.link_count);
    ASSERT_EQ(550, (int)data.subs[0].body.links[0].link_id);

    /* Sub 1 */
    ASSERT_EQ(300, (int)data.subs[1].behavior_id);
    ASSERT_EQ((int)NMO_INTERFACE_FLAG_HEADER_ONLY, (int)data.subs[1].flags);
    ASSERT_EQ(2, (int)data.subs[1].depth);
    ASSERT_FLOAT_EQ(50.0f, data.subs[1].h_pos, 0.001f);
    ASSERT_FLOAT_EQ(60.0f, data.subs[1].v_pos, 0.001f);
    ASSERT_FALSE(data.subs[1].body.has_body);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_graph_io) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    /* Set up ctx: behavior 200 is NOT a building block */
    test_bb_data_t bb_data;
    bb_data.bb_id = 999; /* some other ID */
    nmo_interface_parse_ctx_t ctx;
    ctx.is_building_block = test_is_building_block;
    ctx.user_data = &bb_data;

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    /* v0x15, 2 behaviors (1 script + 1 sub) */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 2);

    /* Script header: header-only */
    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* Sub-behavior with body (non-BB, non-script -> needs graph IO) */
    nmo_chunk_write_object_id(chunk, 200);
    nmo_chunk_write_dword(chunk, 0);
    nmo_chunk_write_dword(chunk, 1);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    nmo_chunk_write_float(chunk, 80.0f);
    nmo_chunk_write_float(chunk, 120.0f);
    nmo_chunk_write_float(chunk, 90.0f);

    /* Empty body sections */
    nmo_chunk_write_int(chunk, 0);  /* links */
    nmo_chunk_write_int(chunk, 0);  /* ops */
    nmo_chunk_write_int(chunk, 0);  /* comments */

    /* params (not BB, so params are read) */
    nmo_chunk_write_int(chunk, 0);  /* local params */
    nmo_chunk_write_int(chunk, 0);  /* shared params */

    /* Graph IO: 4 arrays */
    /* inward inputs: 2 entries */
    nmo_chunk_write_int(chunk, 2);
    nmo_chunk_write_int(chunk, 10); nmo_chunk_write_int(chunk, -1);
    nmo_chunk_write_int(chunk, 20); nmo_chunk_write_int(chunk, -1);
    /* outward inputs: 1 entry */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, 30); nmo_chunk_write_int(chunk, -1);
    /* inward outputs: 0 entries */
    nmo_chunk_write_int(chunk, 0);
    /* outward outputs: 1 entry */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, 40); nmo_chunk_write_int(chunk, 1);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, &ctx, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(1, (int)data.sub_count);
    ASSERT_NOT_NULL(data.subs[0].body.graph_io);

    nmo_interface_graph_io_t *gio = data.subs[0].body.graph_io;
    ASSERT_EQ(2, (int)gio->inward_input_count);
    ASSERT_EQ(10, gio->inward_inputs[0]);
    ASSERT_EQ(20, gio->inward_inputs[1]);
    ASSERT_EQ(1, (int)gio->outward_input_count);
    ASSERT_EQ(30, gio->outward_inputs[0]);
    ASSERT_EQ(0, (int)gio->inward_output_count);
    ASSERT_EQ(1, (int)gio->outward_output_count);
    ASSERT_EQ(40, gio->outward_outputs[0]);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_graph_io_skipped_for_bb) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    /* Set up ctx: behavior 200 IS a building block */
    test_bb_data_t bb_data;
    bb_data.bb_id = 200;
    nmo_interface_parse_ctx_t ctx;
    ctx.is_building_block = test_is_building_block;
    ctx.user_data = &bb_data;

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 2);

    /* Script header: header-only */
    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* BB sub-behavior with body: no params, no graph IO in stream */
    nmo_chunk_write_object_id(chunk, 200);
    nmo_chunk_write_dword(chunk, 0);
    nmo_chunk_write_dword(chunk, 1);
    nmo_chunk_write_float(chunk, 10.0f);
    nmo_chunk_write_float(chunk, 20.0f);
    nmo_chunk_write_float(chunk, 100.0f);
    nmo_chunk_write_float(chunk, 80.0f);
    nmo_chunk_write_float(chunk, 120.0f);
    nmo_chunk_write_float(chunk, 90.0f);

    nmo_chunk_write_int(chunk, 0);  /* links */
    nmo_chunk_write_int(chunk, 0);  /* ops */
    nmo_chunk_write_int(chunk, 0);  /* comments */
    /* no params (BB), no graph IO (BB) */

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, &ctx, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_TRUE(data.subs[0].body.has_body);
    ASSERT_EQ(0, (int)data.subs[0].body.params.local_count);
    ASSERT_EQ(0, (int)data.subs[0].body.params.shared_count);
    ASSERT_NULL(data.subs[0].body.graph_io);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, graph_io_skipped_for_script) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    /* Even with ctx, script should never have graph IO */
    test_bb_data_t bb_data;
    bb_data.bb_id = 999;
    nmo_interface_parse_ctx_t ctx;
    ctx.is_building_block = test_is_building_block;
    ctx.user_data = &bb_data;

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    /* Script header with body */
    write_script_header_fields(chunk, 100, 0, 0, 0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    nmo_chunk_write_int(chunk, 0);  /* links */
    nmo_chunk_write_int(chunk, 0);  /* ops */
    nmo_chunk_write_int(chunk, 0);  /* comments */
    nmo_chunk_write_int(chunk, 0);  /* local params */
    nmo_chunk_write_int(chunk, 0);  /* shared params */
    /* no graph IO for script */

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, &ctx, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_TRUE(data.script.body.has_body);
    ASSERT_NULL(data.script.body.graph_io);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Task 7: Extra data section tests
 * ============================================================================ */

TEST(interface_chunk, parse_extra_data_v3) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* Extra data section: identifier v3 (written as raw DWORDs) */
    write_extra_identifier_raw(chunk, NMO_INTERFACE_EXTRA_ID_V3);

    /* 2 entries */
    nmo_chunk_write_int(chunk, 2);

    /* Entry 0: type 1 (single ID) */
    nmo_chunk_write_dword(chunk, 1);
    nmo_chunk_write_object_id(chunk, 900);
    /* sub-entries (version >= 2): 1 sub-entry */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, 2);      /* value1=2 -> has id2 */
    nmo_chunk_write_int(chunk, 99);     /* value2 */
    nmo_chunk_write_object_id(chunk, 901);  /* id1 */
    nmo_chunk_write_object_id(chunk, 902);  /* id2 (because value1=2) */

    /* Entry 1: type 3 (two IDs) */
    nmo_chunk_write_dword(chunk, 3);
    nmo_chunk_write_object_id(chunk, 910);  /* id1 */
    nmo_chunk_write_object_id(chunk, 911);  /* id2 */
    /* 0 sub-entries */
    nmo_chunk_write_int(chunk, 0);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_TRUE(data.extra.present);
    ASSERT_EQ(3, (int)data.extra.version);
    ASSERT_EQ(2, (int)data.extra.entry_count);

    /* Entry 0 */
    ASSERT_EQ(1, (int)data.extra.entries[0].type);
    ASSERT_EQ(900, (int)data.extra.entries[0].id1);
    ASSERT_EQ(1, (int)data.extra.entries[0].sub_count);
    ASSERT_EQ(2, data.extra.entries[0].sub_entries[0].value1);
    ASSERT_EQ(99, data.extra.entries[0].sub_entries[0].value2);
    ASSERT_EQ(901, (int)data.extra.entries[0].sub_entries[0].id1);
    ASSERT_EQ(902, (int)data.extra.entries[0].sub_entries[0].id2);

    /* Entry 1 */
    ASSERT_EQ(3, (int)data.extra.entries[1].type);
    ASSERT_EQ(910, (int)data.extra.entries[1].id1);
    ASSERT_EQ(911, (int)data.extra.entries[1].id2);
    ASSERT_EQ(0, (int)data.extra.entries[1].sub_count);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_extra_data_v2_adjustment) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* Extra data: identifier v2 (written as raw DWORDs) */
    write_extra_identifier_raw(chunk, NMO_INTERFACE_EXTRA_ID_V2);

    /* 1 entry: type 4 (value) */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_dword(chunk, 4);
    nmo_chunk_write_int(chunk, 42);

    /* 1 sub-entry with value1=4 (should become 6 after v2 adjustment) */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_int(chunk, 4);      /* value1: raw=4, adjusted=6 */
    nmo_chunk_write_int(chunk, 77);     /* value2 */
    nmo_chunk_write_object_id(chunk, 800);  /* id1 */
    /* value1 after adjustment = 6, which is NOT in {2,3,8,9,10,11} -> buffer */
    nmo_chunk_write_buffer(chunk, "AB", 2);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_TRUE(data.extra.present);
    ASSERT_EQ(2, (int)data.extra.version);
    ASSERT_EQ(1, (int)data.extra.entry_count);
    ASSERT_EQ(4, (int)data.extra.entries[0].type);
    ASSERT_EQ(42, data.extra.entries[0].value);

    /* Sub-entry: value1 should be 4+2=6 after v2 adjustment */
    ASSERT_EQ(1, (int)data.extra.entries[0].sub_count);
    ASSERT_EQ(6, data.extra.entries[0].sub_entries[0].value1);
    ASSERT_EQ(77, data.extra.entries[0].sub_entries[0].value2);
    ASSERT_EQ(800, (int)data.extra.entries[0].sub_entries[0].id1);
    ASSERT_NOT_NULL(data.extra.entries[0].sub_entries[0].data);
    ASSERT_EQ(2, (int)data.extra.entries[0].sub_entries[0].data_size);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_extra_data_v1_no_subs) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    /* Extra data: identifier v1 (written as raw DWORDs) */
    write_extra_identifier_raw(chunk, NMO_INTERFACE_EXTRA_ID_V1);

    /* 1 entry: type 2 (single ID), no sub-entries at v1 */
    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_dword(chunk, 2);
    nmo_chunk_write_object_id(chunk, 850);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_TRUE(data.extra.present);
    ASSERT_EQ(1, (int)data.extra.version);
    ASSERT_EQ(1, (int)data.extra.entry_count);
    ASSERT_EQ(2, (int)data.extra.entries[0].type);
    ASSERT_EQ(850, (int)data.extra.entries[0].id1);
    ASSERT_EQ(0, (int)data.extra.entries[0].sub_count);
    ASSERT_NULL(data.extra.entries[0].sub_entries);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, no_extra_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = build_minimal_chunk(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_FALSE(data.extra.present);
    ASSERT_EQ(0, (int)data.extra.entry_count);

    nmo_arena_destroy(arena);
}

TEST(interface_chunk, parse_extra_sub_with_id2_values) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);

    nmo_chunk_write_identifier(chunk, 1);  /* identifier chain entry */
    nmo_chunk_write_dword(chunk, 0x15);
    nmo_chunk_write_int(chunk, 1);

    write_script_header_fields(chunk, 100, NMO_INTERFACE_FLAG_HEADER_ONLY, 0,
                               0.0f, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 0.0f);
    nmo_chunk_write_float(chunk, 50.0f);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_int(chunk, 0);
    nmo_chunk_write_dword(chunk, 0);

    write_extra_identifier_raw(chunk, NMO_INTERFACE_EXTRA_ID_V3);

    nmo_chunk_write_int(chunk, 1);
    nmo_chunk_write_dword(chunk, 1);
    nmo_chunk_write_object_id(chunk, 900);

    /* 3 sub-entries testing all id2-triggering value1 values: 3, 9, 11 */
    nmo_chunk_write_int(chunk, 3);

    /* sub 0: value1=3 -> id2 */
    nmo_chunk_write_int(chunk, 3);
    nmo_chunk_write_int(chunk, 10);
    nmo_chunk_write_object_id(chunk, 1001);
    nmo_chunk_write_object_id(chunk, 1002);

    /* sub 1: value1=9 -> id2 */
    nmo_chunk_write_int(chunk, 9);
    nmo_chunk_write_int(chunk, 20);
    nmo_chunk_write_object_id(chunk, 1003);
    nmo_chunk_write_object_id(chunk, 1004);

    /* sub 2: value1=11 -> id2 */
    nmo_chunk_write_int(chunk, 11);
    nmo_chunk_write_int(chunk, 30);
    nmo_chunk_write_object_id(chunk, 1005);
    nmo_chunk_write_object_id(chunk, 1006);

    nmo_chunk_close(chunk);

    nmo_interface_data_t data;
    nmo_status_t st = nmo_interface_chunk_parse(chunk, arena, NULL, &data);
    ASSERT_EQ(NMO_OK, st);

    ASSERT_EQ(3, (int)data.extra.entries[0].sub_count);

    ASSERT_EQ(3, data.extra.entries[0].sub_entries[0].value1);
    ASSERT_EQ(1002, (int)data.extra.entries[0].sub_entries[0].id2);
    ASSERT_NULL(data.extra.entries[0].sub_entries[0].data);

    ASSERT_EQ(9, data.extra.entries[0].sub_entries[1].value1);
    ASSERT_EQ(1004, (int)data.extra.entries[0].sub_entries[1].id2);

    ASSERT_EQ(11, data.extra.entries[0].sub_entries[2].value1);
    ASSERT_EQ(1006, (int)data.extra.entries[0].sub_entries[2].id2);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Task 9: Integration test with real NMO files
 * ============================================================================ */

TEST(interface_chunk, integration_real_file) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Try BBSamples file first (should have interface chunks) */
    int load_ok = nmo_session_load_file(session,
        "data/BBSamples/3D Transformations/Look At.cmo", NULL, NULL);
    if (load_ok != NMO_OK) {
        /* Test data not available -- skip */
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    /* post_load should have called nmo_behavior_parse_all_interfaces.
     * Iterate all behaviors and check that at least one has interface_data. */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    size_t count = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &count);
    ASSERT_TRUE(count > 0);

    size_t behaviors_with_interface = 0;
    size_t behaviors_total = 0;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = all[i];
        if (!obj || nmo_object_get_class_id(obj) != NMO_CID_BEHAVIOR) continue;

        const nmo_behavior_state_t *state =
            (const nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (!state) continue;
        behaviors_total++;

        if (state->interface_data != NULL) {
            behaviors_with_interface++;
            /* Verify basic fields */
            ASSERT_TRUE(state->interface_data->version >= NMO_INTERFACE_VERSION_MIN);
            ASSERT_TRUE(state->interface_data->version <= NMO_INTERFACE_VERSION_MAX);
            /* Raw chunk should have been cleared */
            ASSERT_NULL(state->interface_chunk);
        }
    }

    /* BBSamples files should have at least one behavior with interface data.
     * If all has_interface flags are false, the file may not contain
     * interface chunks -- in that case just verify no crash. */
    ASSERT_TRUE(behaviors_total > 0);

    size_t behaviors_with_has_interface = 0;
    for (size_t j = 0; j < count; j++) {
        nmo_object_t *obj2 = all[j];
        if (!obj2 || nmo_object_get_class_id(obj2) != NMO_CID_BEHAVIOR) continue;
        const nmo_behavior_state_t *st2 =
            (const nmo_behavior_state_t *)nmo_object_get_state(obj2);
        if (!st2) continue;
        if (st2->has_interface) behaviors_with_has_interface++;
    }

    if (behaviors_with_has_interface > 0) {
        /* File has interface chunks -- at least some should be parsed */
        ASSERT_TRUE(behaviors_with_interface > 0);
    }
    /* else: file has no interface chunks, just verify pipeline didn't crash */

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(interface_chunk, integration_prevent_collision_parses_all_interfaces) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    int load_ok = nmo_session_load_file(session,
        "data/BBSamples/Collisions/Prevent Collision.cmo", NULL, NULL);
    if (load_ok != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *script = nmo_object_repository_find_by_file_id(repo, 250);
    ASSERT_NOT_NULL(script);
    ASSERT_EQ((int)NMO_CID_BEHAVIOR, (int)nmo_object_get_class_id(script));

    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(script);
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(state->has_interface);
    ASSERT_NOT_NULL(state->interface_data);
    ASSERT_NULL(state->interface_chunk);
    ASSERT_TRUE(state->interface_data->sub_count > 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ============================================================================
 * Test registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Tasks 1-3 */
    REGISTER_TEST(interface_chunk, parse_minimal);
    REGISTER_TEST(interface_chunk, reject_version_too_low);
    REGISTER_TEST(interface_chunk, reject_version_too_high);
    REGISTER_TEST(interface_chunk, version_min_accepted);
    REGISTER_TEST(interface_chunk, version_max_accepted);
    REGISTER_TEST(interface_chunk, folded_script_omits_body);
    REGISTER_TEST(interface_chunk, folded_script_partial_body_is_truncated);
    /* Task 4: Links */
    REGISTER_TEST(interface_chunk, parse_links);
    REGISTER_TEST(interface_chunk, dev_layout_omits_color_and_inline_body);
    REGISTER_TEST(interface_chunk, dev_layout_parse_sectioned_links);
    /* Task 5: Operations, comments, parameters */
    REGISTER_TEST(interface_chunk, parse_operations);
    REGISTER_TEST(interface_chunk, parse_comments_v16);
    REGISTER_TEST(interface_chunk, parse_comments_v15_no_style);
    REGISTER_TEST(interface_chunk, parse_parameters_v15);
    REGISTER_TEST(interface_chunk, parse_parameters_v14_legacy);
    /* Task 6: Sub-behaviors and graph IO */
    REGISTER_TEST(interface_chunk, parse_sub_behaviors);
    REGISTER_TEST(interface_chunk, parse_graph_io);
    REGISTER_TEST(interface_chunk, parse_graph_io_skipped_for_bb);
    REGISTER_TEST(interface_chunk, graph_io_skipped_for_script);
    /* Task 7: Extra data */
    REGISTER_TEST(interface_chunk, parse_extra_data_v3);
    REGISTER_TEST(interface_chunk, parse_extra_data_v2_adjustment);
    REGISTER_TEST(interface_chunk, parse_extra_data_v1_no_subs);
    REGISTER_TEST(interface_chunk, no_extra_data);
    REGISTER_TEST(interface_chunk, parse_extra_sub_with_id2_values);
    /* Task 9: Integration */
    REGISTER_TEST(interface_chunk, integration_real_file);
    REGISTER_TEST(interface_chunk, integration_prevent_collision_parses_all_interfaces);
TEST_MAIN_END()
