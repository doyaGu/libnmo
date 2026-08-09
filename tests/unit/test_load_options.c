/**
 * @file test_load_options.c
 * @brief Load profile tests
 */

#include "../test_framework.h"

#include "document/nmo_document_load.h"
#include "document/nmo_document.h"
#include "document/nmo_document_save.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_memory.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_object_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_header.h"
#include "format/nmo_object.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_type_system.h"
#include "core/nmo_allocator.h"
#include "../../src/session/load_diagnostics_internal.h"

#include <stdio.h>

static void destroy_ctx_session(nmo_context_t *ctx, nmo_session_t *session) {
    if (session != NULL) {
        nmo_session_destroy(session);
    }
    if (ctx != NULL) {
        nmo_context_release(ctx);
    }
}

static int load_fixture_with_profile(
    const char *path,
    nmo_load_profile_t profile,
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_load_perf_stats_t *out_stats
) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (ctx == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        nmo_context_release(ctx);
        return NMO_ERR_NOMEM;
    }

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = profile;
    opts.collect_perf_stats = true;
    opts.perf_stats = out_stats;

    int st = nmo_session_load_file(session, path, &opts, NULL);
    if (st != NMO_OK) {
        destroy_ctx_session(ctx, session);
        return st;
    }

    *out_ctx = ctx;
    *out_session = session;
    return NMO_OK;
}

TEST(load_options, metadata_profile_stops_after_header_and_rejects_mutation)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_METADATA,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    nmo_file_info_t info = nmo_session_get_file_info(session);
    ASSERT_TRUE(info.object_count > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_PARSE].calls > 0);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_DATA_READ].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_DATA_PARSE].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_OBJECT_DESERIALIZE].calls);
    ASSERT_TRUE(nmo_session_is_partial_load(session));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    ASSERT_EQ((size_t)0, nmo_object_repository_get_count(repo));

    remove("test_metadata_profile_should_not_save.nmo");
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_session_save_file(session,
                                    "test_metadata_profile_should_not_save.nmo",
                                    NULL,
                                    NULL));
    remove("test_metadata_profile_direct_save_should_not_save.nmo");
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_save_file(session,
                            "test_metadata_profile_direct_save_should_not_save.nmo",
                            NULL));

    nmo_object_id_t created_id = 0;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_session_create_object(session,
                                        NMO_CID_OBJECT,
                                        "blocked",
                                        (nmo_guid_t){0, 0},
                                        &created_id,
                                        NULL));

    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_workspace_edit_t *edit = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_workspace_edit_begin(workspace, "blocked", &edit));
    ASSERT_NULL(edit);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);

    destroy_ctx_session(ctx, session);
}

TEST(load_options, partial_profile_rejects_non_empty_session)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    int st = nmo_load_file(session, "data/Ballance/Gameplay.nmo", NULL);
    if (st != NMO_OK) {
        destroy_ctx_session(ctx, session);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    size_t original_count = nmo_object_repository_get_count(repo);
    ASSERT_TRUE(original_count > 0);
    ASSERT_FALSE(nmo_session_is_partial_load(session));

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_load_file(session, "data/Ballance/Camera.nmo", &opts));
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    ASSERT_EQ(original_count, nmo_object_repository_get_count(repo));

    destroy_ctx_session(ctx, session);
}

TEST(load_options, partial_profile_rejects_non_object_session_state)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const char payload[] = "stale";
    ASSERT_EQ(NMO_OK,
              nmo_session_add_included_file(session,
                                            "stale.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    uint32_t included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_load_file(session, "data/Ballance/Camera.nmo", &opts));
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    destroy_ctx_session(ctx, session);
}

TEST(load_options, phased_partial_profile_rejects_non_empty_session)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    int st = nmo_load_file(session, "data/Ballance/Gameplay.nmo", NULL);
    if (st != NMO_OK) {
        destroy_ctx_session(ctx, session);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    size_t original_count = nmo_object_repository_get_count(repo);
    ASSERT_TRUE(original_count > 0);
    ASSERT_FALSE(nmo_session_is_partial_load(session));

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    nmo_io_interface_t *io = nmo_file_io_open("data/Ballance/Camera.nmo", NMO_IO_READ);
    ASSERT_NOT_NULL(io);
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, &opts);
    ASSERT_NOT_NULL(ds);

    nmo_status_t parse_result = nmo_deserializer_parse_header(ds);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, parse_result);
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    ASSERT_EQ(original_count, nmo_object_repository_get_count(repo));

    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
}

TEST(load_options, phased_partial_profile_rejects_non_object_session_state)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const char payload[] = "stale";
    ASSERT_EQ(NMO_OK,
              nmo_session_add_included_file(session,
                                            "stale.bin",
                                            payload,
                                            (uint32_t)sizeof(payload)));
    uint32_t included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    nmo_io_interface_t *io = nmo_file_io_open("data/Ballance/Camera.nmo", NMO_IO_READ);
    ASSERT_NOT_NULL(io);
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, &opts);
    ASSERT_NOT_NULL(ds);

    nmo_status_t parse_result = nmo_deserializer_parse_header(ds);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, parse_result);
    ASSERT_FALSE(nmo_session_is_partial_load(session));
    included_count = 0;
    ASSERT_NOT_NULL(nmo_session_get_included_files(session, &included_count));
    ASSERT_EQ((uint32_t)1, included_count);

    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
}

TEST(load_options, two_phase_serializer_rejects_partial_session)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_METADATA,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    ASSERT_TRUE(nmo_session_is_partial_load(session));
    nmo_save_options_t save_opts = nmo_save_options_default();
    nmo_serializer_t *serializer = nmo_serializer_create(session, &save_opts);
    ASSERT_NOT_NULL(serializer);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, nmo_serializer_layout(serializer));

    nmo_serializer_destroy(serializer);
    destroy_ctx_session(ctx, session);
}

TEST(load_options, header_only_profile_stops_after_header)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_HEADER_ONLY,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    nmo_file_info_t info = nmo_session_get_file_info(session);
    ASSERT_TRUE(info.object_count > 0);
    ASSERT_TRUE(nmo_session_get_header(session) != NULL);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_PARSE].calls > 0);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_DATA_READ].calls);
    ASSERT_EQ((uint64_t)0, stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls);
    ASSERT_TRUE(nmo_session_is_partial_load(session));

    destroy_ctx_session(ctx, session);
}

TEST(load_options, full_profile_is_default)
{
    nmo_load_perf_stats_t stats = {0};
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;

    int st = load_fixture_with_profile(
        "data/Ballance/Gameplay.nmo",
        NMO_LOAD_PROFILE_FULL,
        &ctx,
        &session,
        &stats);
    if (st != NMO_OK) {
        return;
    }

    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_DATA_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OBJECT_DESERIALIZE].calls > 0);
    ASSERT_FALSE(nmo_session_is_partial_load(session));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    ASSERT_TRUE(nmo_object_repository_get_count(repo) > 0);

    destroy_ctx_session(ctx, session);
}

static nmo_status_t parse_memory_header(const void *data,
                                        size_t size,
                                        int *out_materialized)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (ctx == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        nmo_context_release(ctx);
        return NMO_ERR_NOMEM;
    }
    nmo_io_interface_t *io = nmo_memory_io_open_read(data, size);
    if (io == NULL) {
        destroy_ctx_session(ctx, session);
        return NMO_ERR_NOMEM;
    }
    nmo_deserializer_t *ds = nmo_deserializer_create(session, io, NULL);
    if (ds == NULL) {
        nmo_io_close(io);
        destroy_ctx_session(ctx, session);
        return NMO_ERR_NOMEM;
    }

    nmo_status_t result = nmo_deserializer_parse_header(ds);
    if (out_materialized != NULL) {
        *out_materialized = nmo_session_has_materialized_load_state(session);
    }
    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
    return result;
}

static nmo_status_t parse_header1_payload(const void *payload,
                                          size_t payload_size,
                                          uint32_t packed_size,
                                          uint32_t unpacked_size,
                                          int *out_materialized)
{
    nmo_file_header_t header = {0};
    memcpy(header.signature, "Nemo Fi\0", sizeof(header.signature));
    header.file_version = 8;
    header.hdr1_pack_size = packed_size;
    header.hdr1_unpack_size = unpacked_size;

    nmo_io_interface_t *write_io = nmo_memory_io_open_write(64 + payload_size);
    if (write_io == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_status_t result = nmo_file_header_serialize(&header, write_io);
    if (result == NMO_OK && payload_size > 0) {
        result = nmo_io_write(write_io, payload, payload_size);
    }
    if (result == NMO_OK) {
        size_t size = 0;
        const void *data = nmo_memory_io_get_data(write_io, &size);
        result = data != NULL
            ? parse_memory_header(data, size, out_materialized)
            : NMO_ERR_INTERNAL;
    }
    nmo_io_close(write_io);
    return result;
}

static nmo_status_t parse_data_payload(const void *payload,
                                       size_t payload_size,
                                       uint32_t packed_size,
                                       uint32_t unpacked_size)
{
    nmo_file_header_t header = {0};
    memcpy(header.signature, "Nemo Fi\0", sizeof(header.signature));
    header.file_version = 8;
    header.hdr1_pack_size = sizeof(uint32_t);
    header.hdr1_unpack_size = sizeof(uint32_t);
    header.data_pack_size = packed_size;
    header.data_unpack_size = unpacked_size;

    nmo_io_interface_t *write_io = nmo_memory_io_open_write(
        64 + sizeof(uint32_t) + payload_size);
    if (write_io == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_status_t result = nmo_file_header_serialize(&header, write_io);
    if (result == NMO_OK) {
        result = nmo_io_write_u32(write_io, 0);
    }
    if (result == NMO_OK && payload_size > 0) {
        result = nmo_io_write(write_io, payload, payload_size);
    }

    size_t size = 0;
    const void *data = nmo_memory_io_get_data(write_io, &size);
    if (result != NMO_OK || data == NULL) {
        nmo_io_close(write_io);
        return result != NMO_OK ? result : NMO_ERR_INTERNAL;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_session_t *session = ctx != NULL ? nmo_session_create(ctx) : NULL;
    nmo_io_interface_t *read_io = session != NULL
        ? nmo_memory_io_open_read(data, size)
        : NULL;
    nmo_deserializer_t *ds = read_io != NULL
        ? nmo_deserializer_create(session, read_io, NULL)
        : NULL;
    if (ds == NULL) {
        if (read_io != NULL) {
            nmo_io_close(read_io);
        }
        destroy_ctx_session(ctx, session);
        nmo_io_close(write_io);
        return NMO_ERR_NOMEM;
    }

    result = nmo_deserializer_parse_header(ds);
    if (result == NMO_OK) {
        result = nmo_deserializer_parse_objects(ds);
    }
    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
    nmo_io_close(write_io);
    return result;
}

static nmo_status_t parse_included_payload(const void *payload,
                                           size_t payload_size,
                                           uint32_t included_count,
                                           uint32_t *out_loaded_count)
{
    nmo_file_header_t header = {0};
    memcpy(header.signature, "Nemo Fi\0", sizeof(header.signature));
    header.file_version = 8;
    header.hdr1_pack_size = 3u * sizeof(uint32_t);
    header.hdr1_unpack_size = 3u * sizeof(uint32_t);

    nmo_io_interface_t *write_io = nmo_memory_io_open_write(
        64 + header.hdr1_pack_size + payload_size);
    if (write_io == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_status_t result = nmo_file_header_serialize(&header, write_io);
    if (result == NMO_OK) {
        result = nmo_io_write_u32(write_io, 0);
    }
    if (result == NMO_OK) {
        result = nmo_io_write_u32(write_io, sizeof(uint32_t));
    }
    if (result == NMO_OK) {
        result = nmo_io_write_u32(write_io, included_count);
    }
    if (result == NMO_OK && payload_size > 0) {
        result = nmo_io_write(write_io, payload, payload_size);
    }

    size_t size = 0;
    const void *data = nmo_memory_io_get_data(write_io, &size);
    if (result != NMO_OK || data == NULL) {
        nmo_io_close(write_io);
        return result != NMO_OK ? result : NMO_ERR_INTERNAL;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_session_t *session = ctx != NULL ? nmo_session_create(ctx) : NULL;
    nmo_io_interface_t *read_io = session != NULL
        ? nmo_memory_io_open_read(data, size)
        : NULL;
    nmo_deserializer_t *ds = read_io != NULL
        ? nmo_deserializer_create(session, read_io, NULL)
        : NULL;
    if (ds == NULL) {
        if (read_io != NULL) {
            nmo_io_close(read_io);
        }
        destroy_ctx_session(ctx, session);
        nmo_io_close(write_io);
        return NMO_ERR_NOMEM;
    }

    result = nmo_deserializer_parse_header(ds);
    if (result == NMO_OK) {
        result = nmo_deserializer_parse_objects(ds);
    }
    if (out_loaded_count != NULL) {
        (void)nmo_session_get_included_files(session, out_loaded_count);
    }
    nmo_deserializer_destroy(ds);
    destroy_ctx_session(ctx, session);
    nmo_io_close(write_io);
    return result;
}

typedef struct rejecting_allocator_context {
    size_t allocation_calls;
} rejecting_allocator_context_t;

static void *rejecting_alloc(void *user_data, size_t size, size_t alignment) {
    rejecting_allocator_context_t *ctx =
        (rejecting_allocator_context_t *)user_data;
    (void)size;
    (void)alignment;
    ctx->allocation_calls++;
    return NULL;
}

static void rejecting_free(void *user_data, void *ptr) {
    (void)user_data;
    (void)ptr;
}

TEST(load_options, diagnostics_lifecycle_is_caller_owned)
{
    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
    ASSERT_NULL(diagnostics.issues);
    ASSERT_EQ((size_t)0, diagnostics.count);
    ASSERT_EQ((size_t)0, diagnostics.capacity);

    nmo_load_options_t options = nmo_load_options_default();
    options.diagnostics = &diagnostics;
    ASSERT_EQ(&diagnostics, options.diagnostics);

    nmo_load_diagnostics_reset(&diagnostics);
    nmo_load_diagnostics_destroy(&diagnostics);
    ASSERT_NULL(diagnostics.issues);
    ASSERT_EQ((size_t)0, diagnostics.count);
}

TEST(load_options, diagnostics_capture_current_chunk_section)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x1234u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xDEADBEEFu));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(chunk, 0x1234u));

    nmo_object_t *object = nmo_object_create(NULL, 77u, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(object);
    object->file_id = 88u;
    nmo_type_descriptor_t schema = {0};
    schema.name = "CKObject";

    nmo_load_diagnostics_t diagnostics;
    nmo_load_diagnostics_init(&diagnostics);
    ASSERT_EQ(NMO_OK, nmo_load_diagnostics_append(
        &diagnostics, object, &schema, chunk, 0u,
        NMO_ERR_TRUNCATED_CHUNK, "truncated"));
    ASSERT_EQ((size_t)1, diagnostics.count);
    ASSERT_EQ(0x1234u, diagnostics.issues[0].section_id);
    ASSERT_EQ((size_t)2, diagnostics.issues[0].dword_offset);
    ASSERT_EQ(77u, diagnostics.issues[0].object_id);
    ASSERT_EQ(88u, diagnostics.issues[0].file_id);

    nmo_load_diagnostics_destroy(&diagnostics);
    nmo_object_destroy(object);
    nmo_arena_destroy(arena);
}

TEST(load_options, custom_allocator_controls_object_and_schema_storage)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    rejecting_allocator_context_t reject_ctx = {0};
    nmo_allocator_t rejecting = nmo_allocator_custom(
        rejecting_alloc, rejecting_free, &reject_ctx);
    nmo_load_options_t options = nmo_load_options_default();
    options.allocator = &rejecting;

    ASSERT_EQ(NMO_ERR_NOMEM,
              nmo_session_load_file(
                  session, "data/Ballance/Gameplay.nmo", &options, NULL));
    ASSERT_TRUE(reject_ctx.allocation_calls > 0);
    ASSERT_EQ((size_t)0,
              nmo_object_repository_get_count(
                  nmo_session_get_repository(session)));

    destroy_ctx_session(ctx, session);
}

TEST(load_options, changed_object_save_preserves_unlisted_chunks)
{
    const char *preserved_path = "test_changed_only_preserved.nmo";
    const char *default_path = "test_changed_only_default.nmo";
    remove(preserved_path);
    remove(default_path);

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_OK, nmo_load_file(
        session, "data/Ballance/Camera.nmo", NULL));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *object = NULL;
    const size_t count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *candidate = nmo_object_repository_get_by_index(repo, i);
        if (candidate != NULL && candidate->state != NULL &&
            candidate->chunk != NULL) {
            object = candidate;
            break;
        }
    }
    ASSERT_NOT_NULL(object);
    const nmo_object_id_t file_id = object->file_id;
    nmo_object_state_t *state = (nmo_object_state_t *)object->state;
    const uint32_t original_visibility = state->visibility_flags;
    state->visibility_flags ^= NMO_CKOBJECT_VISIBLE;
    const uint32_t changed_visibility = state->visibility_flags;
    ASSERT_NE(original_visibility, changed_visibility);

    nmo_save_options_t changed_only = nmo_save_options_default();
    changed_only.flags |= NMO_SAVE_CHANGED_OBJECTS_ONLY;
    ASSERT_EQ(NMO_OK, nmo_save_file(
        session, preserved_path, &changed_only));

    nmo_save_options_t defaults = nmo_save_options_default();
    ASSERT_EQ(NMO_OK, nmo_save_file(session, default_path, &defaults));
    destroy_ctx_session(ctx, session);

    nmo_context_t *preserved_ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(preserved_ctx);
    nmo_session_t *preserved_session = nmo_session_create(preserved_ctx);
    ASSERT_NOT_NULL(preserved_session);
    ASSERT_EQ(NMO_OK, nmo_load_file(
        preserved_session, preserved_path, NULL));
    nmo_object_t *preserved_object = nmo_object_repository_find_by_file_id(
        nmo_session_get_repository(preserved_session), file_id);
    ASSERT_NOT_NULL(preserved_object);
    ASSERT_EQ(original_visibility,
              ((nmo_object_state_t *)preserved_object->state)->visibility_flags);
    destroy_ctx_session(preserved_ctx, preserved_session);

    nmo_context_t *default_ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(default_ctx);
    nmo_session_t *default_session = nmo_session_create(default_ctx);
    ASSERT_NOT_NULL(default_session);
    ASSERT_EQ(NMO_OK, nmo_load_file(default_session, default_path, NULL));
    nmo_object_t *default_object = nmo_object_repository_find_by_file_id(
        nmo_session_get_repository(default_session), file_id);
    ASSERT_NOT_NULL(default_object);
    ASSERT_EQ(changed_visibility,
              ((nmo_object_state_t *)default_object->state)->visibility_flags);
    destroy_ctx_session(default_ctx, default_session);

    ASSERT_EQ(0, remove(preserved_path));
    ASSERT_EQ(0, remove(default_path));
}

TEST(load_options, phased_header_parse_preserves_format_errors)
{
    static const uint8_t truncated[] = { 'N', 'e', 'm', 'o' };
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              parse_memory_header(truncated, sizeof(truncated), NULL));

    static const uint8_t invalid_signature[8] = {
        'N', 'o', 't', ' ', 'N', 'M', 'O', '\0'
    };
    ASSERT_EQ(NMO_ERR_INVALID_SIGNATURE,
              parse_memory_header(invalid_signature,
                                  sizeof(invalid_signature),
                                  NULL));

    const uint8_t incomplete_header1 = 0;
    int materialized = 1;
    ASSERT_EQ(NMO_ERR_BUFFER_OVERRUN,
              parse_header1_payload(&incomplete_header1, 1, 1, 1,
                                    &materialized));
    ASSERT_FALSE(materialized);
}

TEST(load_options, phased_header1_classifies_payload_failures)
{
    const uint8_t short_payload = 0;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              parse_header1_payload(&short_payload, 1, 4, 4, NULL));

    static const uint8_t corrupt_compressed_payload[4] = {
        0xDE, 0xAD, 0xBE, 0xEF
    };
    ASSERT_EQ(NMO_ERR_DECOMPRESSION_FAILED,
              parse_header1_payload(corrupt_compressed_payload,
                                    sizeof(corrupt_compressed_payload),
                                    sizeof(corrupt_compressed_payload),
                                    8,
                                    NULL));
}

TEST(load_options, load_file_preserves_header_errors)
{
    const char *path = "test_load_options_invalid_header.nmo";
    FILE *file = fopen(path, "wb");
    ASSERT_NOT_NULL(file);
    static const uint8_t invalid_signature[8] = {
        'N', 'o', 't', ' ', 'N', 'M', 'O', '\0'
    };
    ASSERT_EQ(sizeof(invalid_signature),
              fwrite(invalid_signature, 1, sizeof(invalid_signature), file));
    ASSERT_EQ(0, fclose(file));

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    ASSERT_EQ(NMO_ERR_INVALID_SIGNATURE,
              nmo_load_file(session, path, NULL));

    file = fopen(path, "wb");
    ASSERT_NOT_NULL(file);
    static const uint8_t truncated[] = { 'N', 'e', 'm', 'o' };
    ASSERT_EQ(sizeof(truncated),
              fwrite(truncated, 1, sizeof(truncated), file));
    ASSERT_EQ(0, fclose(file));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_load_file(session, path, NULL));

    destroy_ctx_session(ctx, session);
    ASSERT_EQ(0, remove(path));
}

TEST(load_options, phased_data_classifies_payload_failures)
{
    const uint8_t short_payload = 0;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              parse_data_payload(&short_payload, 1, 4, 4));

    static const uint8_t corrupt_compressed_payload[4] = {
        0xDE, 0xAD, 0xBE, 0xEF
    };
    ASSERT_EQ(NMO_ERR_DECOMPRESSION_FAILED,
              parse_data_payload(corrupt_compressed_payload,
                                 sizeof(corrupt_compressed_payload),
                                 sizeof(corrupt_compressed_payload),
                                 8));
}

TEST(load_options, included_file_failure_does_not_publish_prefix)
{
    static const uint8_t one_of_two_payload[] = {
        1, 0, 0, 0, 'a',
        1, 0, 0, 0, 'x'
    };
    uint32_t loaded_count = UINT32_MAX;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              parse_included_payload(one_of_two_payload,
                                     sizeof(one_of_two_payload),
                                     2,
                                     &loaded_count));
    ASSERT_EQ(0u, loaded_count);
}

TEST(load_options, included_file_loading_stops_at_header_count)
{
    static const uint8_t two_file_payload[] = {
        1, 0, 0, 0, 'a',
        1, 0, 0, 0, 'x',
        1, 0, 0, 0, 'b',
        1, 0, 0, 0, 'y'
    };
    uint32_t loaded_count = UINT32_MAX;
    ASSERT_EQ(NMO_OK,
              parse_included_payload(two_file_payload,
                                     sizeof(two_file_payload),
                                     1,
                                     &loaded_count));
    ASSERT_EQ(1u, loaded_count);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(load_options, metadata_profile_stops_after_header_and_rejects_mutation);
    REGISTER_TEST(load_options, partial_profile_rejects_non_empty_session);
    REGISTER_TEST(load_options, partial_profile_rejects_non_object_session_state);
    REGISTER_TEST(load_options, phased_partial_profile_rejects_non_empty_session);
    REGISTER_TEST(load_options, phased_partial_profile_rejects_non_object_session_state);
    REGISTER_TEST(load_options, two_phase_serializer_rejects_partial_session);
    REGISTER_TEST(load_options, changed_object_save_preserves_unlisted_chunks);
    REGISTER_TEST(load_options, header_only_profile_stops_after_header);
    REGISTER_TEST(load_options, full_profile_is_default);
    REGISTER_TEST(load_options, diagnostics_lifecycle_is_caller_owned);
    REGISTER_TEST(load_options, diagnostics_capture_current_chunk_section);
    REGISTER_TEST(load_options, custom_allocator_controls_object_and_schema_storage);
    REGISTER_TEST(load_options, phased_header_parse_preserves_format_errors);
    REGISTER_TEST(load_options, phased_header1_classifies_payload_failures);
    REGISTER_TEST(load_options, load_file_preserves_header_errors);
    REGISTER_TEST(load_options, phased_data_classifies_payload_failures);
    REGISTER_TEST(load_options, included_file_failure_does_not_publish_prefix);
    REGISTER_TEST(load_options, included_file_loading_stops_at_header_count);
TEST_MAIN_END()


