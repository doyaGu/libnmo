/**
 * @file deserializer.c
 * @brief Phased deserializer pipeline and internal ID-remapping
 */

#include "session/nmo_deserializer.h"
#include "deserializer_internal.h"

#include "session/nmo_session.h"
#include "session/nmo_session_internal.h"
#include "session/nmo_context.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_object_system.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_id_sanitizer.h"

#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_diagnostics.h"

#include "object/nmo_object_repository.h"
#include "object/nmo_shadow_storage.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"

#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"

#include "type/nmo_type_system.h"

#include "io/nmo_io.h"

#include "core/nmo_arena.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_logger.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

#include "miniz.h"

#define NMO_PARSER_MAX_HEADER1_SIZE (64u * 1024u * 1024u)

/* ============================================================================
 * Deserializer context (opaque)
 * ============================================================================ */

typedef struct nmo_deserializer {
    /* Session + IO */
    nmo_session_t *session;
    nmo_io_interface_t *io;
    nmo_load_options_t options;
    nmo_load_stats_t stats;

    /* Derived from session */
    nmo_context_t *ctx;
    nmo_arena_t *arena;
    nmo_object_repository_t *repo;
    nmo_logger_t *logger;
    nmo_id_sanitizer_t *id_sanitizer;
    nmo_shadow_storage_t *shadow_storage;
    nmo_chunk_pool_t *chunk_pool;

    /* Header data */
    nmo_file_header_t header;
    nmo_header1_t hdr1;

    /* Data section */
    nmo_data_section_t data_sect;

    /* ID remapping (formerly nmo_load_session) */
    nmo_object_id_t saved_id_max;
    nmo_object_id_t id_base;
    nmo_hash_table_t *id_mappings;
    int active;
    nmo_arena_t *remap_arena;

    /* Phase tracking: 0=none, 1=header, 2=objects, 3=finalized */
    int phase_completed;

    /* Internal load session pointer (for object_system calls) */
    nmo_deserializer_t *load_session;
} nmo_deserializer_t;

/* ============================================================================
 * Internal ID-remapping API (legacy load session)
 * ============================================================================ */

nmo_deserializer_t *nmo_deserializer_start(nmo_object_repository_t *repo,
                                           nmo_object_id_t max_saved_id) {
    if (repo == NULL) {
        return NULL;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        return NULL;
    }

    nmo_deserializer_t *session = (nmo_deserializer_t *) nmo_arena_alloc(
        arena, sizeof(nmo_deserializer_t), sizeof(void *));
    if (session == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }
    memset(session, 0, sizeof(nmo_deserializer_t));
    session->remap_arena = arena;

    /* Initialize mapping table using generic hash table */
    size_t initial_capacity = 64;
    session->id_mappings = nmo_hash_table_create(
        NULL,
        sizeof(nmo_object_id_t),    /* key: file object index */
        sizeof(nmo_object_id_t),    /* value: runtime_id */
        initial_capacity,
        nmo_hash_uint32,            /* hash function for uint32_t */
        NULL                        /* use default memcmp */
    );

    if (session->id_mappings == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    session->repo = repo;
    session->saved_id_max = max_saved_id;
    session->active = 1;

    /* Track next available runtime ID base (for potential remap logic). */
    size_t existing_count = nmo_object_repository_get_count(repo);
    if (existing_count > 0) {
        /* Find max existing ID */
        size_t count;
        nmo_object_t **objects = nmo_object_repository_get_all(repo, &count);
        nmo_object_id_t max_id = 0;
        for (size_t i = 0; i < count; i++) {
            if (objects[i]->id > max_id) {
                max_id = objects[i]->id;
            }
        }
        session->id_base = max_id + 1;
    } else {
        session->id_base = 1; /* Start from 1 (0 is invalid) */
    }

    return session;
}

int nmo_deserializer_register(nmo_deserializer_t *session,
                              nmo_object_t *obj,
                              nmo_object_id_t file_index) {
    if (session == NULL || obj == NULL || !session->active) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check if already registered */
    if (nmo_hash_table_contains(session->id_mappings, &file_index)) {
        return NMO_ERR_INVALID_STATE;
    }

    /* Add mapping */
    nmo_status_t result = nmo_hash_table_insert(session->id_mappings, &file_index, &obj->id);
    if (result != NMO_OK) {
        return result;
    }

    return NMO_OK;
}

int nmo_deserializer_end(nmo_deserializer_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    session->active = 0;
    return NMO_OK;
}

int nmo_deserializer_get_runtime_id(const nmo_deserializer_t *session,
                                    nmo_object_id_t file_index,
                                    nmo_object_id_t *out_runtime_id)
{
    if (session == NULL || out_runtime_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_id_t runtime_id = 0;
    nmo_status_t result = nmo_hash_table_get(session->id_mappings, &file_index, &runtime_id);
    if (result != NMO_OK) {
        return result;
    }

    *out_runtime_id = runtime_id;
    return NMO_OK;
}

nmo_object_repository_t *nmo_deserializer_get_repository(
    const nmo_deserializer_t *session) {
    return session ? session->repo : NULL;
}

nmo_object_id_t nmo_deserializer_get_id_base(const nmo_deserializer_t *session) {
    return session ? session->id_base : 0;
}

nmo_object_id_t nmo_deserializer_get_max_saved_id(const nmo_deserializer_t *session) {
    return session ? session->saved_id_max : 0;
}

void nmo_deserializer_destroy_legacy(nmo_deserializer_t *session) {
    if (session != NULL) {
        nmo_hash_table_destroy(session->id_mappings);
        nmo_arena_destroy(session->remap_arena);
    }
}

/**
 * Iterator context for collecting mappings
 */
typedef struct {
    nmo_object_id_t *file_ids;
    nmo_object_id_t *runtime_ids;
    size_t index;
} mapping_collector_t;

static int collect_mapping(const void *key, void *value, void *user_data) {
    mapping_collector_t *collector = (mapping_collector_t *)user_data;
    collector->file_ids[collector->index] = *(const nmo_object_id_t *)key;
    collector->runtime_ids[collector->index] = *(nmo_object_id_t *)value;
    collector->index++;
    return 1; /* Continue iteration */
}

int nmo_load_session_get_mappings(const nmo_deserializer_t *session,
                                  nmo_object_id_t **file_ids,
                                  nmo_object_id_t **runtime_ids,
                                  size_t *count) {
    if (session == NULL || file_ids == NULL || runtime_ids == NULL || count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t mapping_count = nmo_hash_table_get_count(session->id_mappings);
    if (mapping_count == 0) {
        *file_ids = NULL;
        *runtime_ids = NULL;
        *count = 0;
        return NMO_OK;
    }

    /* Allocate arrays */
    nmo_object_id_t *fids = (nmo_object_id_t *) nmo_arena_alloc(
        session->remap_arena, mapping_count * sizeof(nmo_object_id_t), alignof(nmo_object_id_t));
    nmo_object_id_t *rids = (nmo_object_id_t *) nmo_arena_alloc(
        session->remap_arena, mapping_count * sizeof(nmo_object_id_t), alignof(nmo_object_id_t));

    if (fids == NULL || rids == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Collect mappings using iterator */
    mapping_collector_t collector = {
        .file_ids = fids,
        .runtime_ids = rids,
        .index = 0
    };

    nmo_hash_table_iterate(session->id_mappings, collect_mapping, &collector);

    *file_ids = fids;
    *runtime_ids = rids;
    *count = mapping_count;

    return NMO_OK;
}

/* ============================================================================
 * Static helpers (migrated from load.c)
 * ============================================================================ */

static int nmo_register_included_metadata(
    nmo_session_t *session,
    const char *name,
    uint32_t data_size
) {
    nmo_included_file_metadata_t meta;
    meta.owner_ids = NULL;
    meta.owner_count = 0;
    meta.attributes = NMO_INCLUDED_FILE_ATTR_METADATA_ONLY;
    const char *safe_name = (name != NULL) ? name : "";
    return nmo_session_add_included_file_borrowed_ex(
        session,
        safe_name,
        NULL,
        data_size,
        &meta);
}

static int nmo_shadow_buffer_append(nmo_arena_t *arena,
                                    uint8_t **buffer,
                                    size_t *size,
                                    size_t *capacity,
                                    const void *data,
                                    size_t data_size) {
    if (data_size == 0) {
        return NMO_OK;
    }

    const size_t max_size = (size_t)-1;
    if (*size > max_size - data_size) {
        return NMO_ERR_INVALID_FORMAT;
    }

    size_t required = *size + data_size;

    if (required > *capacity) {
        size_t new_capacity = (*capacity != 0) ? (*capacity * 2) : 256;

        if (*capacity != 0 && new_capacity < *capacity) {
            return NMO_ERR_INVALID_FORMAT;
        }

        while (new_capacity < required) {
            if (new_capacity > (max_size / 2)) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }

        uint8_t *new_buffer = (uint8_t *)nmo_arena_alloc(arena, new_capacity, 1);
        if (new_buffer == NULL) {
            return NMO_ERR_NOMEM;
        }

        if (*buffer != NULL && *size > 0) {
            memcpy(new_buffer, *buffer, *size);
        }

        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *size, data, data_size);
    *size += data_size;
    return NMO_OK;
}

static int nmo_shadow_buffer_append_u32(nmo_arena_t *arena,
                                        uint8_t **buffer,
                                        size_t *size,
                                        size_t *capacity,
                                        uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    return nmo_shadow_buffer_append(arena, buffer, size, capacity, bytes, sizeof(bytes));
}

static int nmo_load_included_files(
    nmo_session_t *session,
    nmo_io_interface_t *io,
    const nmo_header1_t *hdr1,
    nmo_logger_t *logger,
    nmo_shadow_storage_t *shadow_storage,
    int preserve_shadow,
    uint32_t max_name_len,
    uint32_t max_data_size
) {
    if (session == NULL || io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t expected = (hdr1 != NULL) ? hdr1->included_file_count : 0;
    uint32_t parsed = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);

    const int has_authoritative_table =
        (hdr1 != NULL && hdr1->included_files != NULL && hdr1->included_file_count > 0);

    uint8_t *shadow_blob = NULL;
    size_t shadow_size = 0;
    size_t shadow_capacity = 0;

    if (!preserve_shadow || shadow_storage == NULL) {
        shadow_storage = NULL;
    }

    int result = NMO_OK;

    while (1) {
        if (has_authoritative_table && parsed >= expected) {
            break;
        }

        uint32_t name_len = 0;
        int read_result = nmo_io_read_u32(io, &name_len);
        if (read_result != NMO_OK) {
            if (read_result == NMO_ERR_EOF) {
                break;
            }

            nmo_log(logger, NMO_LOG_WARN,
                    "Failed to read included filename length: %d", read_result);
            result = read_result;
            goto cleanup;
        }

        if (max_name_len == 0) {
            max_name_len = NMO_LOAD_DEFAULT_MAX_INCLUDED_NAME_LEN;
        }
        if (name_len > max_name_len) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Included filename too large (%u bytes)", name_len);
            result = NMO_ERR_INVALID_FORMAT;
            goto cleanup;
        }

        const size_t name_alloc_size = (size_t)name_len + 1u;

        if (shadow_storage != NULL) {
            int append_result = nmo_shadow_buffer_append_u32(
                arena, &shadow_blob, &shadow_size, &shadow_capacity, name_len);
            if (append_result != NMO_OK) {
                result = append_result;
                goto cleanup;
            }
        }

        char *name_buf = (char *) nmo_arena_alloc(arena, name_alloc_size, 1);
        if (name_buf == NULL) {
            result = NMO_ERR_NOMEM;
            goto cleanup;
        }

        if (name_len > 0) {
            size_t bytes_read = 0;
            int name_read = nmo_io_read(io, name_buf, name_len, &bytes_read);
            if (name_read != NMO_OK || bytes_read != name_len) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "Failed to read included filename payload");
                result = (name_read != NMO_OK) ? name_read : NMO_ERR_TRUNCATED_CHUNK;
                goto cleanup;
            }
        }
        name_buf[name_len] = '\0';

        if (shadow_storage != NULL && name_len > 0) {
            int append_result = nmo_shadow_buffer_append(
                arena, &shadow_blob, &shadow_size, &shadow_capacity, name_buf, name_len);
            if (append_result != NMO_OK) {
                result = append_result;
                goto cleanup;
            }
        }

        uint32_t data_size_val = 0;
        if (nmo_io_read_u32(io, &data_size_val) != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to read included file size for '%s'", name_buf);
            result = NMO_ERR_TRUNCATED_CHUNK;
            goto cleanup;
        }

        if (max_data_size == 0) {
            max_data_size = NMO_LOAD_DEFAULT_MAX_INCLUDED_FILE_SIZE;
        }
        if (data_size_val > max_data_size) {
            nmo_log(logger, NMO_LOG_ERROR,
                "Included file '%s' too large (%u bytes)", name_buf, data_size_val);
            result = NMO_ERR_INVALID_FORMAT;
            goto cleanup;
        }

        if (shadow_storage != NULL) {
            int append_result = nmo_shadow_buffer_append_u32(
                arena, &shadow_blob, &shadow_size, &shadow_capacity, data_size_val);
            if (append_result != NMO_OK) {
                result = append_result;
                goto cleanup;
            }
        }

        void *payload = NULL;
        if (data_size_val > 0) {
            payload = nmo_arena_alloc(arena, data_size_val, 1);
            if (payload == NULL) {
                result = NMO_ERR_NOMEM;
                goto cleanup;
            }

            size_t bytes_read = 0;
            int data_result = nmo_io_read(io, payload, data_size_val, &bytes_read);
            if (data_result != NMO_OK || bytes_read != data_size_val) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "Failed to read included payload for '%s'", name_buf);
                result = (data_result != NMO_OK) ? data_result : NMO_ERR_TRUNCATED_CHUNK;
                goto cleanup;
            }
        }

        if (shadow_storage != NULL && data_size_val > 0) {
            int append_result = nmo_shadow_buffer_append(
                arena, &shadow_blob, &shadow_size, &shadow_capacity, payload, data_size_val);
            if (append_result != NMO_OK) {
                result = append_result;
                goto cleanup;
            }
        }

        int add_result = nmo_session_add_included_file_borrowed(
            session,
            name_buf,
            payload,
            data_size_val);
        if (add_result != NMO_OK) {
            result = add_result;
            goto cleanup;
        }

        if (hdr1 != NULL && hdr1->included_files != NULL && parsed < hdr1->included_file_count) {
            const nmo_included_file_desc_t *desc = &hdr1->included_files[parsed];
            if (desc->name != NULL && strcmp(desc->name, name_buf) != 0) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Included file #%u name mismatch (Header1='%s', Payload='%s')",
                        parsed, desc->name, name_buf);
            }
            if (desc->data_size != data_size_val) {
                nmo_log(logger, NMO_LOG_INFO,
                        "Included file '%s' size mismatch (Header1=%u, Payload=%u)",
                        name_buf, desc->data_size, data_size_val);
            }
        }

        parsed++;
    }

    if (expected != 0 && parsed > expected) {
        nmo_log(logger, NMO_LOG_WARN,
                "  Parsed %u included file(s), but Header1 advertised %u",
                parsed, expected);
    }

    if (expected > parsed) {
        nmo_log(logger, NMO_LOG_INFO,
                "  Header references %u included file(s), parsed %u entries",
                expected, parsed);

        if (has_authoritative_table) {
            result = NMO_ERR_TRUNCATED_CHUNK;
            goto cleanup;
        }

        if (hdr1 != NULL && hdr1->included_files != NULL) {
            for (uint32_t i = parsed; i < expected; i++) {
                const nmo_included_file_desc_t *desc = &hdr1->included_files[i];
                const char *meta_name = (desc != NULL && desc->name != NULL)
                    ? desc->name
                    : "";
                int meta_result = nmo_register_included_metadata(
                    session,
                    meta_name,
                    desc != NULL ? desc->data_size : 0u);
                if (meta_result != NMO_OK) {
                    result = meta_result;
                    goto cleanup;
                }
            }
            nmo_log(logger, NMO_LOG_INFO,
                    "  Recorded %u metadata-only include entries",
                    expected - parsed);
        } else {
            nmo_log(logger, NMO_LOG_DEBUG,
                    "  Note: Header1 included file descriptors not populated (expected for Virtools format)");
        }
    }

cleanup:
    if (shadow_storage != NULL && result == NMO_OK) {
        int shadow_result = nmo_shadow_capture_included_files(
            shadow_storage, shadow_blob, shadow_size);
        if (shadow_result != NMO_OK) {
            result = shadow_result;
        }
    }

    return result;
}

/* ============================================================================
 * Default load options
 * ============================================================================ */

nmo_load_options_t nmo_load_options_default(void) {
    nmo_load_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.allocator = NULL;
    opts.flags = NMO_LOAD_DEFAULT;
    opts.max_included_name_len = NMO_LOAD_DEFAULT_MAX_INCLUDED_NAME_LEN;
    opts.max_included_file_size = NMO_LOAD_DEFAULT_MAX_INCLUDED_FILE_SIZE;
    return opts;
}

/* ============================================================================
 * Phased deserializer API
 * ============================================================================ */

nmo_deserializer_t *nmo_deserializer_create(
    nmo_session_t *session,
    nmo_io_interface_t *io,
    const nmo_load_options_t *options)
{
    if (session == NULL || io == NULL) {
        return NULL;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    if (arena == NULL) {
        return NULL;
    }

    nmo_deserializer_t *ds = (nmo_deserializer_t *)nmo_arena_alloc(
        arena, sizeof(nmo_deserializer_t), sizeof(void *));
    if (ds == NULL) {
        return NULL;
    }
    memset(ds, 0, sizeof(nmo_deserializer_t));

    ds->session = session;
    ds->io = io;

    if (options != NULL) {
        ds->options = *options;
    } else {
        ds->options = nmo_load_options_default();
    }

    /* Derive subsystems from session */
    ds->ctx = nmo_session_get_context(session);
    ds->arena = arena;
    ds->repo = nmo_session_get_repository(session);
    ds->logger = nmo_context_get_logger(ds->ctx);
    ds->id_sanitizer = nmo_session_get_id_sanitizer(session);
    ds->shadow_storage = nmo_session_get_shadow_storage(session);
    ds->chunk_pool = NULL;
    ds->phase_completed = 0;
    ds->load_session = NULL;

    return ds;
}

nmo_status_t nmo_deserializer_parse_header(nmo_deserializer_t *ds)
{
    if (ds == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ds->phase_completed != 0) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_t *session = ds->session;
    nmo_io_interface_t *io = ds->io;
    nmo_arena_t *arena = ds->arena;
    nmo_logger_t *logger = ds->logger;
    const nmo_load_flags_t flags = ds->options.flags;
    const int preserve_shadow = (flags & NMO_LOAD_PRESERVE_SHADOW) != 0;

    if (ds->shadow_storage != NULL) {
        nmo_shadow_storage_reset(ds->shadow_storage);
        if (!preserve_shadow) {
            ds->shadow_storage = NULL;
        }
    }

    nmo_session_reset_reference_resolver(session);
    if (ds->id_sanitizer != NULL) {
        nmo_id_sanitizer_reset(ds->id_sanitizer);
    }

    /* Phase 2: Parse File Header */
    nmo_log(logger, NMO_LOG_INFO, "Phase 2: Parsing file header");
    nmo_status_t result = nmo_file_header_parse(io, &ds->header);
    if (result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to parse file header");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    result = nmo_file_header_validate(&ds->header);
    if (result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Invalid file header");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Set file info in session */
    nmo_file_info_t file_info;
    memset(&file_info, 0, sizeof(file_info));
    file_info.file_version = ds->header.file_version;
    file_info.file_version2 = ds->header.file_version2;
    file_info.ck_version = ds->header.ck_version;
    file_info.product_version = ds->header.product_version;
    file_info.product_build = ds->header.product_build;
    file_info.file_size = 0;
    file_info.object_count = ds->header.object_count;
    file_info.manager_count = ds->header.manager_count;
    file_info.write_mode = ds->header.file_write_mode;
    nmo_session_set_file_info(session, &file_info);

    /* Store file header in session */
    nmo_session_set_file_header(session, &ds->header, sizeof(nmo_file_header_t));

    /* Phase 3: Read and Decompress Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 3: Reading header1 (size: %u bytes)",
            ds->header.hdr1_pack_size);

    memset(&ds->hdr1, 0, sizeof(nmo_header1_t));
    ds->hdr1.object_count = ds->header.object_count;

    if ((ds->header.hdr1_pack_size > 0 && ds->header.hdr1_unpack_size == 0) ||
        (ds->header.hdr1_pack_size == 0 && ds->header.hdr1_unpack_size > 0)) {
        nmo_log(logger, NMO_LOG_ERROR, "Invalid header1 size fields");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (ds->header.hdr1_pack_size > NMO_PARSER_MAX_HEADER1_SIZE ||
        ds->header.hdr1_unpack_size > NMO_PARSER_MAX_HEADER1_SIZE) {
        nmo_log(logger, NMO_LOG_ERROR, "Header1 size exceeds limit");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Skip header1 if empty */
    if (ds->header.hdr1_pack_size == 0 || ds->header.hdr1_unpack_size == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No header1 data (empty file or minimal format)");
        ds->hdr1.plugin_dep_count = 0;
        ds->hdr1.plugin_deps = NULL;
    } else {
        /* Read packed header1 data */
        void *packed_hdr1 = nmo_arena_alloc(arena, ds->header.hdr1_pack_size, 16);
        if (packed_hdr1 == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate packed header1 buffer");
            return NMO_ERR_NOMEM;
        }

        size_t bytes_read = 0;
        int read_result = nmo_io_read(io, packed_hdr1, ds->header.hdr1_pack_size, &bytes_read);
        if (read_result != NMO_OK || bytes_read != ds->header.hdr1_pack_size) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to read header1 data");
            return NMO_ERR_INVALID_ARGUMENT;
        }

        /* Decompress if needed */
        void *hdr1_data = NULL;
        size_t hdr1_size = 0;

        if (ds->header.hdr1_pack_size != ds->header.hdr1_unpack_size) {
            nmo_log(logger, NMO_LOG_INFO, "  Decompressing header1: %u -> %u bytes",
                    ds->header.hdr1_pack_size, ds->header.hdr1_unpack_size);

            hdr1_data = nmo_arena_alloc(arena, ds->header.hdr1_unpack_size, 16);
            if (hdr1_data == NULL) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate unpacked header1 buffer");
                return NMO_ERR_NOMEM;
            }

            mz_ulong dest_len = ds->header.hdr1_unpack_size;
            int uncompress_result = mz_uncompress((unsigned char *) hdr1_data, &dest_len,
                                                  (const unsigned char *) packed_hdr1,
                                                  ds->header.hdr1_pack_size);
            if (uncompress_result != MZ_OK) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to decompress header1: %d",
                        uncompress_result);
                return NMO_ERR_INVALID_ARGUMENT;
            }

            if (dest_len != ds->header.hdr1_unpack_size) {
                nmo_log(logger, NMO_LOG_ERROR, "Header1 decompression size mismatch: expected %u, got %lu",
                        ds->header.hdr1_unpack_size, dest_len);
                return NMO_ERR_INVALID_ARGUMENT;
            }

            hdr1_size = dest_len;
            nmo_log(logger, NMO_LOG_INFO, "  Decompression successful: %zu bytes", hdr1_size);
        } else {
            /* Already uncompressed */
            hdr1_data = packed_hdr1;
            hdr1_size = ds->header.hdr1_pack_size;
        }

        /* Phase 4: Parse Header1 */
        nmo_log(logger, NMO_LOG_INFO, "Phase 4: Parsing header1");
        result = nmo_header1_parse(hdr1_data, hdr1_size, &ds->hdr1, arena);
        if (result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to parse header1");
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    int dep_store_result = nmo_session_set_plugin_dependencies(session, ds->hdr1.plugin_deps, ds->hdr1.plugin_dep_count);
    if (dep_store_result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR,
                "Failed to store plugin dependencies (code=%d)", dep_store_result);
        return dep_store_result;
    }

    nmo_log(logger, NMO_LOG_INFO, "Found %u objects, %u managers, %u plugins",
            ds->hdr1.object_count, ds->header.manager_count, ds->hdr1.plugin_dep_count);

    /* Populate stats */
    ds->stats.file_version = ds->header.file_version;
    ds->stats.crc = ds->header.crc;
    ds->stats.header_compressed = (ds->header.hdr1_pack_size != ds->header.hdr1_unpack_size);
    ds->stats.header1_size = ds->header.hdr1_unpack_size;

    ds->phase_completed = 1;
    return NMO_OK;
}

nmo_status_t nmo_deserializer_parse_objects(nmo_deserializer_t *ds)
{
    if (ds == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ds->phase_completed != 1) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_t *session = ds->session;
    nmo_io_interface_t *io = ds->io;
    nmo_arena_t *arena = ds->arena;
    nmo_logger_t *logger = ds->logger;
    nmo_object_repository_t *repo = ds->repo;
    nmo_id_sanitizer_t *id_sanitizer = ds->id_sanitizer;
    nmo_shadow_storage_t *shadow_storage = ds->shadow_storage;
    const nmo_load_flags_t flags = ds->options.flags;
    const int preserve_shadow = (flags & NMO_LOAD_PRESERVE_SHADOW) != 0;
    const int enforce_plugin_dependencies = (flags & NMO_LOAD_CHECK_DEPENDENCIES) != 0;
    nmo_status_t result;

    /* Phase 5: Start Load Session */
    nmo_log(logger, NMO_LOG_INFO, "Phase 5: Starting load session (max ID: %u)",
            ds->header.max_id_saved);

    nmo_deserializer_t *load_session = nmo_deserializer_start(repo, ds->header.max_id_saved);
    if (load_session == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to start load session");
        return NMO_ERR_NOMEM;
    }
    ds->load_session = load_session;

    /* Phase 6: Check Plugin Dependencies */
    nmo_log(logger, NMO_LOG_INFO, "Phase 6: Checking plugin dependencies (%u plugins)",
            ds->hdr1.plugin_dep_count);

    nmo_extension_registry_t *ext_registry = nmo_session_get_extension_registry(session);

    if (ds->hdr1.plugin_dep_count > 0 && ext_registry == NULL) {
        nmo_log(logger, NMO_LOG_WARN, "  Plugin dependencies present but extension registry is unavailable");
    }

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(session);
    size_t missing_plugins = (diag != NULL) ? diag->missing_count : 0;
    size_t outdated_plugins = (diag != NULL) ? diag->outdated_count : 0;
    (void)outdated_plugins;
    if (diag != NULL && diag->entries != NULL) {
        for (size_t i = 0; i < diag->entry_count; i++) {
            const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
            char guid_buffer[32];
            nmo_guid_format(entry->guid, guid_buffer, sizeof(guid_buffer));

            if (entry->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_MISSING) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Missing plugin %zu: guid=%s category=%s version=%u",
                        i,
                        guid_buffer,
                        nmo_extension_category_label(entry->category),
                        entry->required_version);
                continue;
            }

            const char *resolved_name = (entry->resolved_name != NULL)
                ? entry->resolved_name
                : "<unnamed>";

            if (entry->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Plugin %s (guid=%s) version %u is older than required version %u",
                        resolved_name,
                        guid_buffer,
                        entry->resolved_version,
                        entry->required_version);
            } else {
                nmo_log(logger, NMO_LOG_INFO,
                        "  Plugin %s (guid=%s) satisfied dependency (version=%u)",
                        resolved_name,
                        guid_buffer,
                        entry->resolved_version);
            }
        }
    } else if (ds->hdr1.plugin_dep_count > 0) {
        nmo_log(logger, NMO_LOG_INFO,
                "  Plugin diagnostics unavailable (dependencies=%u)", ds->hdr1.plugin_dep_count);
    }

    if (missing_plugins > 0 && enforce_plugin_dependencies) {
        nmo_log(logger, NMO_LOG_ERROR,
                "Missing %zu required plugin(s); aborting due to NMO_LOAD_CHECK_DEPENDENCIES", missing_plugins);
        nmo_deserializer_destroy_legacy(load_session);
        ds->load_session = NULL;
        return NMO_ERR_NOT_FOUND;
    }

    /* Phase 7: Manager Pre-Load Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 7: Executing manager pre-load hooks");

    nmo_manager_registry_t *manager_reg = nmo_context_get_manager_registry(ds->ctx);
    if (manager_reg != NULL) {
        uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);
        nmo_log(logger, NMO_LOG_INFO, "  Found %u registered managers", manager_count);

        for (uint32_t i = 0; i < manager_count; i++) {
            uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
            nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_get(manager_reg, manager_id);

            if (manager != NULL) {
                nmo_runtime_event_ctx_t event_ctx;
                memset(&event_ctx, 0, sizeof(event_ctx));
                event_ctx.event = NMO_RUNTIME_EVENT_PRE_LOAD;
                event_ctx.manager_id = manager_id;
                event_ctx.manager_guid = manager->guid;
                int hook_result = nmo_manager_invoke_event(manager, session, &event_ctx);
                if (hook_result != NMO_OK) {
                    nmo_log(logger, NMO_LOG_WARN, "  Manager %u pre-load hook failed: %d",
                            manager_id, hook_result);
                } else {
                    nmo_log(logger, NMO_LOG_INFO, "  Manager %u pre-load hook executed", manager_id);
                }
            }
        }
    }

    /* Phase 8: Read and Decompress Data Section */
    nmo_log(logger, NMO_LOG_INFO, "Phase 8: Reading data section (size: %u bytes)",
            ds->header.data_pack_size);

    memset(&ds->data_sect, 0, sizeof(nmo_data_section_t));
    ds->data_sect.manager_count = ds->header.manager_count;
    ds->data_sect.object_count = ds->header.object_count;

    /* Skip data section if empty */
    if (ds->header.data_pack_size == 0 || ds->header.data_unpack_size == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No data section (empty file or minimal format)");
    } else {
        /* Read packed data */
        void *packed_buffer = nmo_arena_alloc(arena, ds->header.data_pack_size, 16);
        if (packed_buffer == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate packed data buffer");
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return NMO_ERR_NOMEM;
        }

        size_t bytes_read = 0;
        int read_result = nmo_io_read(io, packed_buffer, ds->header.data_pack_size, &bytes_read);
        if (read_result != NMO_OK || bytes_read != ds->header.data_pack_size) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to read data section");
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return NMO_ERR_INVALID_ARGUMENT;
        }

        /* Decompress if needed */
        void *data_buffer = NULL;
        size_t data_size = 0;

        if (ds->header.data_pack_size != ds->header.data_unpack_size) {
            nmo_log(logger, NMO_LOG_INFO, "  Decompressing data: %u -> %u bytes",
                    ds->header.data_pack_size, ds->header.data_unpack_size);

            data_buffer = nmo_arena_alloc(arena, ds->header.data_unpack_size, 16);
            if (data_buffer == NULL) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate unpacked data buffer");
                nmo_deserializer_destroy_legacy(load_session);
                ds->load_session = NULL;
                return NMO_ERR_NOMEM;
            }

            mz_ulong dest_len = ds->header.data_unpack_size;
            int uncompress_result = mz_uncompress((unsigned char *) data_buffer, &dest_len,
                                                  (const unsigned char *) packed_buffer,
                                                  ds->header.data_pack_size);
            if (uncompress_result != MZ_OK) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to decompress data section: %d",
                        uncompress_result);
                nmo_deserializer_destroy_legacy(load_session);
                ds->load_session = NULL;
                return NMO_ERR_INVALID_ARGUMENT;
            }

            if (dest_len != ds->header.data_unpack_size) {
                nmo_log(logger, NMO_LOG_ERROR, "Data decompression size mismatch: expected %u, got %lu",
                        ds->header.data_unpack_size, dest_len);
                nmo_deserializer_destroy_legacy(load_session);
                ds->load_session = NULL;
                return NMO_ERR_INVALID_ARGUMENT;
            }

            data_size = dest_len;
            nmo_log(logger, NMO_LOG_INFO, "  Decompression successful: %zu bytes", data_size);
        } else {
            /* Already uncompressed */
            data_buffer = packed_buffer;
            data_size = ds->header.data_pack_size;
        }

        /* Parse Data section */
        if (ds->chunk_pool == NULL) {
            size_t pool_hint = (size_t)ds->header.object_count + (size_t)ds->header.manager_count;
            ds->chunk_pool = nmo_session_ensure_chunk_pool(session, pool_hint);
            if (ds->chunk_pool == NULL) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Chunk pool unavailable; falling back to direct chunk allocations");
            }
        }

        result = nmo_data_section_parse(data_buffer, data_size, ds->header.file_version,
                                        &ds->data_sect, ds->chunk_pool, arena);
        if (result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to parse data section");
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return result;
        }

        nmo_log(logger, NMO_LOG_INFO, "  Data section parsed successfully");
        nmo_log(logger, NMO_LOG_INFO, "  Managers parsed: %u", ds->data_sect.manager_count);
        nmo_log(logger, NMO_LOG_INFO, "  Objects parsed: %u", ds->data_sect.object_count);
    }

    /* Load included files */
    {
        int included_result = nmo_load_included_files(
            session,
            io,
            &ds->hdr1,
            logger,
            shadow_storage,
            preserve_shadow,
            ds->options.max_included_name_len,
            ds->options.max_included_file_size);
        if (included_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to load included files (code=%d)", included_result);
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return included_result;
        }
    }

    /* Phase 9: Parse Manager Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 9: Parsing manager chunks");

    if (ds->data_sect.managers != NULL) {
        for (uint32_t i = 0; i < ds->data_sect.manager_count; i++) {
            nmo_manager_data_t *mgr_data = &ds->data_sect.managers[i];
            nmo_log(logger, NMO_LOG_INFO, "  Manager %u: GUID={0x%08X,0x%08X}, DataSize=%u",
                    i, mgr_data->guid.d1, mgr_data->guid.d2, mgr_data->data_size);

            if (mgr_data->chunk != NULL) {
                nmo_log(logger, NMO_LOG_INFO, "    Manager chunk present (version %u)",
                        mgr_data->chunk->chunk_version);
            }
        }

        /* Store manager data in session for round-trip */
        nmo_session_set_manager_data(session, ds->data_sect.managers, ds->data_sect.manager_count);
    } else {
        nmo_log(logger, NMO_LOG_INFO, "  No manager chunks to process");
    }

    /* Phase 10: Create Objects */
    nmo_log(logger, NMO_LOG_INFO, "Phase 10: Creating %u objects", ds->hdr1.object_count);

    size_t remap_error_count = 0;

    /* Skip object creation if no header1 data or no object descriptors */
    if (ds->hdr1.objects == NULL || ds->hdr1.object_count == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No objects to create (empty file or no object descriptors)");
        goto skip_object_processing;
    }

    {
        const nmo_allocator_t *obj_allocator = nmo_context_get_allocator(ds->ctx);
        int prep_result = nmo_object_system_prepare_loaded_objects(
            obj_allocator,
            arena,
            repo,
            id_sanitizer,
            load_session,
            ds->hdr1.objects,
            ds->hdr1.object_count,
            ds->data_sect.objects,
            ds->data_sect.object_count,
            ds->data_sect.managers,
            ds->data_sect.manager_count,
            logger,
            &remap_error_count);

        if (prep_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to prepare loaded objects (code=%d)", prep_result);
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return prep_result;
        }

        if (remap_error_count > 0) {
            nmo_log(logger, NMO_LOG_WARN,
                    "  ID remapping completed with %zu errors", remap_error_count);
        }
    }

    /* Phase 13b: Dispatch manager chunks to registered managers */
    nmo_log(logger, NMO_LOG_INFO, "Phase 13b: Dispatching manager chunks");

    if (ds->data_sect.managers != NULL && ds->data_sect.manager_count > 0) {
        if (manager_reg == NULL) {
            nmo_log(logger, NMO_LOG_WARN, "  Manager registry unavailable; preserving %u chunk(s) for round-trip",
                    ds->data_sect.manager_count);
        } else {
            for (uint32_t i = 0; i < ds->data_sect.manager_count; i++) {
                nmo_manager_data_t *mgr_data = &ds->data_sect.managers[i];
                char guid_buffer[64];
                nmo_guid_format(mgr_data->guid, guid_buffer, sizeof(guid_buffer));

                nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_find_by_guid(
                    manager_reg,
                    mgr_data->guid);

                if (manager == NULL) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Skipping manager chunk GUID=%s (no registered manager); data preserved",
                            guid_buffer);
                    continue;
                }

                const nmo_chunk_t *chunk = mgr_data->chunk;
                if (chunk == NULL) {
                    nmo_log(logger, NMO_LOG_INFO,
                            "  Manager %s (GUID=%s) has no chunk payload; nothing to dispatch",
                            manager->name ? manager->name : "<unnamed>", guid_buffer);
                    continue;
                }

                nmo_runtime_event_ctx_t event_ctx;
                memset(&event_ctx, 0, sizeof(event_ctx));
                event_ctx.event = NMO_RUNTIME_EVENT_POST_LOAD;
                event_ctx.manager_id = 0;
                event_ctx.manager_guid = mgr_data->guid;
                event_ctx.manager_chunk_in = chunk;
                int load_result = nmo_manager_invoke_event(manager, session, &event_ctx);
                if (load_result == NMO_OK) {
                    mgr_data->flags |= NMO_MANAGER_DATA_FLAG_DISPATCHED;
                    nmo_log(logger, NMO_LOG_INFO,
                            "  Manager %s (GUID=%s) consumed its chunk", manager->name ? manager->name : "<unnamed>",
                            guid_buffer);
                } else {
                    mgr_data->flags |= NMO_MANAGER_DATA_FLAG_ERROR;
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Manager %s (GUID=%s) failed to load data (code=%d); chunk preserved",
                            manager->name ? manager->name : "<unnamed>", guid_buffer, load_result);
                }
            }
        }
    } else {
        nmo_log(logger, NMO_LOG_INFO, "  No manager chunks to dispatch");
    }

    nmo_log(logger, NMO_LOG_INFO, "Phase 13b completed");

    /* Phase 14: Deserialize Objects */
    nmo_log(logger, NMO_LOG_INFO, "Phase 14: Deserializing objects");

    {
        const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ds->ctx);
        if (type_rt == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Type registry not initialized in context");
            nmo_deserializer_end(load_session);
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return NMO_ERR_INVALID_STATE;
        }

        nmo_object_system_deserialize_stats_t deser_stats;
        memset(&deser_stats, 0, sizeof(deser_stats));
        nmo_reference_resolver_t *reference_resolver = nmo_session_ensure_reference_resolver(session);
        if (reference_resolver == NULL) {
            nmo_log(logger, NMO_LOG_WARN,
                    "  Failed to create session reference resolver; continuing without registration");
        }

        nmo_status_t deser_result = nmo_object_system_deserialize_loaded_objects(
            repo,
            type_rt,
            arena,
            logger,
            shadow_storage,
            NMO_DESER_FLAG_FILE_MODE | NMO_DESER_FLAG_PRESERVE_RAW,
            reference_resolver,
            load_session,
            ds->hdr1.object_count,
            &deser_stats);

        if (deser_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "  Repository deserialization failed (code=%d)", deser_result);
            nmo_deserializer_end(load_session);
            nmo_deserializer_destroy_legacy(load_session);
            ds->load_session = NULL;
            return deser_result;
        }

        nmo_log(logger, NMO_LOG_INFO,
                "  Deserialization summary: %zu deserialized, %zu no schema, %zu skipped (no chunk), %zu errors",
                deser_stats.deserialized,
                deser_stats.no_schema,
                deser_stats.skipped_no_chunk + deser_stats.skipped_null + deser_stats.skipped_empty_chunk,
                deser_stats.errors);

        ds->stats.object_count = deser_stats.deserialized;
    }

skip_object_processing:
    /* Update stats */
    {
        size_t repo_count = 0;
        nmo_object_repository_get_all(repo, &repo_count);
        ds->stats.object_count = repo_count;
    }
    ds->stats.manager_count = ds->data_sect.manager_count;
    ds->stats.data_compressed = (ds->header.data_pack_size != ds->header.data_unpack_size);
    ds->stats.data_size = ds->header.data_unpack_size;

    /* Cleanup load session */
    nmo_deserializer_end(load_session);
    nmo_deserializer_destroy_legacy(load_session);
    ds->load_session = NULL;

    /* Close IO now that all reads are done */
    nmo_io_close(io);
    ds->io = NULL;

    nmo_log(logger, NMO_LOG_INFO, "Load complete: %zu objects loaded", ds->stats.object_count);

    ds->phase_completed = 2;
    return NMO_OK;
}

nmo_status_t nmo_deserializer_finalize(nmo_deserializer_t *ds)
{
    if (ds == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ds->phase_completed != 2) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_logger_t *logger = ds->logger;

    /* Runtime kernel finalizes load semantics (dependency remap, post-hooks, indexing). */
    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_LOAD;
    request.flags = NMO_RUNTIME_REQUEST_DEFAULT;

    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    int runtime_result = nmo_runtime_kernel_finalize_load(ds->session, &request, &report);
    if (runtime_result != NMO_OK) {
        nmo_log(logger, NMO_LOG_WARN,
                "Runtime load finalization failed: %d (continuing)", runtime_result);
    }

    ds->phase_completed = 3;
    return NMO_OK;
}

nmo_load_stats_t nmo_deserializer_get_stats(const nmo_deserializer_t *ds)
{
    nmo_load_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    if (ds != NULL) {
        stats = ds->stats;
    }
    return stats;
}

void nmo_deserializer_destroy(nmo_deserializer_t *ds)
{
    if (ds == NULL) {
        return;
    }

    /* Clean up load session if still active (error path) */
    if (ds->load_session != NULL) {
        nmo_deserializer_destroy_legacy(ds->load_session);
        ds->load_session = NULL;
    }

    /* Close IO if still open (error path) */
    if (ds->io != NULL) {
        nmo_io_close(ds->io);
        ds->io = NULL;
    }

    /* The deserializer struct itself is arena-allocated (session arena),
     * so it will be freed when the session is destroyed. */
}
