/**
 * @file parser.c
 * @brief Load and save pipeline implementation (Phase 9 & 10)
 *
 * The save pipeline has been refactored to use the two-phase commit
 * architecture implemented in save_pipeline.c (Phase 1.4).
 */

#include "app/nmo_load.h"
#include "session/nmo_session.h"
#include "session/nmo_session_internal.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_diagnostics.h"
#include "session/nmo_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_object_system.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_mmap.h"
#include "io/nmo_io_compressed.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "session/nmo_load_session.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_id_sanitizer.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_shadow_storage.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"
#include <string.h>
#include <stdalign.h>
#include "miniz.h"  /* For compression/decompression */

#define NMO_PARSER_MAX_HEADER1_SIZE (64u * 1024u * 1024u)


/**
 * @brief Register included file with metadata only (for Header1 metadata entries)
 */
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

        uint32_t data_size = 0;
        if (nmo_io_read_u32(io, &data_size) != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to read included file size for '%s'", name_buf);
            result = NMO_ERR_TRUNCATED_CHUNK;
            goto cleanup;
        }

        if (max_data_size == 0) {
            max_data_size = NMO_LOAD_DEFAULT_MAX_INCLUDED_FILE_SIZE;
        }
        if (data_size > max_data_size) {
            nmo_log(logger, NMO_LOG_ERROR,
                "Included file '%s' too large (%u bytes)", name_buf, data_size);
            result = NMO_ERR_INVALID_FORMAT;
            goto cleanup;
        }

        if (shadow_storage != NULL) {
            int append_result = nmo_shadow_buffer_append_u32(
                arena, &shadow_blob, &shadow_size, &shadow_capacity, data_size);
            if (append_result != NMO_OK) {
                result = append_result;
                goto cleanup;
            }
        }

        void *payload = NULL;
        if (data_size > 0) {
            payload = nmo_arena_alloc(arena, data_size, 1);
            if (payload == NULL) {
                result = NMO_ERR_NOMEM;
                goto cleanup;
            }

            size_t bytes_read = 0;
            int data_result = nmo_io_read(io, payload, data_size, &bytes_read);
            if (data_result != NMO_OK || bytes_read != data_size) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "Failed to read included payload for '%s'", name_buf);
                result = (data_result != NMO_OK) ? data_result : NMO_ERR_TRUNCATED_CHUNK;
                goto cleanup;
            }
        }

        if (shadow_storage != NULL && data_size > 0) {
            int append_result = nmo_shadow_buffer_append(
                arena, &shadow_blob, &shadow_size, &shadow_capacity, payload, data_size);
            if (append_result != NMO_OK) {
                result = append_result;
                goto cleanup;
            }
        }

        int add_result = nmo_session_add_included_file_borrowed(
            session,
            name_buf,
            payload,
            data_size);
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
            if (desc->data_size != data_size) {
                nmo_log(logger, NMO_LOG_INFO,
                        "Included file '%s' size mismatch (Header1=%u, Payload=%u)",
                        name_buf, desc->data_size, data_size);
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
            /* Header1 provided an authoritative include table, but payload is incomplete. */
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
            /* This is expected behavior: Virtools writer never populates Header1
             * included file descriptors. Files are appended after data section
             * without metadata (see VIRTOOLS_FILE_FORMAT_SPEC.md Section 11.2) */
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

    /* shadow_blob is arena-allocated; lifetime tied to session arena */

    return result;
}


/**
 * Load file - 15-phase load pipeline (shared implementation)
 */
static int nmo_load_file_with_io(
    nmo_session_t *session,
    const char *path,
    nmo_io_interface_t *io,
    const nmo_load_options_t *opts
) {
    if (session == NULL || path == NULL || io == NULL || opts == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_chunk_pool_t *chunk_pool = NULL;
    nmo_logger_t *logger = nmo_context_get_logger(ctx);
    nmo_id_sanitizer_t *id_sanitizer = nmo_session_get_id_sanitizer(session);
    nmo_shadow_storage_t *shadow_storage = nmo_session_get_shadow_storage(session);
    size_t repo_count = 0;

    const nmo_load_flags_t flags = opts->flags;
    const int preserve_shadow = (flags & NMO_LOAD_PRESERVE_SHADOW) != 0;

    if (shadow_storage != NULL) {
        nmo_shadow_storage_reset(shadow_storage);
        if (!preserve_shadow) {
            shadow_storage = NULL;
        }
    }

    nmo_session_reset_reference_resolver(session);
    if (id_sanitizer != NULL) {
        nmo_id_sanitizer_reset(id_sanitizer);
    }

    const int enforce_plugin_dependencies = (flags & NMO_LOAD_CHECK_DEPENDENCIES) != 0;

    /* Phase 2: Parse File Header */
    nmo_log(logger, NMO_LOG_INFO, "Phase 2: Parsing file header");
    nmo_file_header_t header;
    nmo_status_t result = nmo_file_header_parse(io, &header);
    if (result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to parse file header");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    result = nmo_file_header_validate(&header);
    if (result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Invalid file header");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Set file info in session */
    nmo_file_info_t file_info = {
        .file_version = header.file_version,
        .file_version2 = header.file_version2,
        .ck_version = header.ck_version,
        .product_version = header.product_version,
        .product_build = header.product_build,
        .file_size = 0, /* Will calculate from headers */
        .object_count = header.object_count,
        .manager_count = header.manager_count,
        .write_mode = header.file_write_mode
    };
    nmo_session_set_file_info(session, &file_info);
    
    /* Store file header in session (opaquely to maintain layer separation) */
    nmo_session_set_file_header(session, &header, sizeof(nmo_file_header_t));

    /* Phase 3: Read and Decompress Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 3: Reading header1 (size: %u bytes)",
            header.hdr1_pack_size);

    nmo_header1_t hdr1;
    memset(&hdr1, 0, sizeof(nmo_header1_t));
    hdr1.object_count = header.object_count;

    if ((header.hdr1_pack_size > 0 && header.hdr1_unpack_size == 0) ||
        (header.hdr1_pack_size == 0 && header.hdr1_unpack_size > 0)) {
        nmo_log(logger, NMO_LOG_ERROR, "Invalid header1 size fields");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (header.hdr1_pack_size > NMO_PARSER_MAX_HEADER1_SIZE ||
        header.hdr1_unpack_size > NMO_PARSER_MAX_HEADER1_SIZE) {
        nmo_log(logger, NMO_LOG_ERROR, "Header1 size exceeds limit");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Skip header1 if empty (for files with no header1 data) */
    if (header.hdr1_pack_size == 0 || header.hdr1_unpack_size == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No header1 data (empty file or minimal format)");
        hdr1.plugin_dep_count = 0;
        hdr1.plugin_deps = NULL;
    } else {
        /* Read packed header1 data */
        void *packed_hdr1 = nmo_arena_alloc(arena, header.hdr1_pack_size, 16);
        if (packed_hdr1 == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate packed header1 buffer");
            nmo_io_close(io);
            return NMO_ERR_NOMEM;
        }

        size_t bytes_read = 0;
        int read_result = nmo_io_read(io, packed_hdr1, header.hdr1_pack_size, &bytes_read);
        if (read_result != NMO_OK || bytes_read != header.hdr1_pack_size) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to read header1 data");
            nmo_io_close(io);
            return NMO_ERR_INVALID_ARGUMENT;
        }

        /* Decompress if needed */
        void *hdr1_data = NULL;
        size_t hdr1_size = 0;

        if (header.hdr1_pack_size != header.hdr1_unpack_size) {
            nmo_log(logger, NMO_LOG_INFO, "  Decompressing header1: %u -> %u bytes",
                    header.hdr1_pack_size, header.hdr1_unpack_size);

            hdr1_data = nmo_arena_alloc(arena, header.hdr1_unpack_size, 16);
            if (hdr1_data == NULL) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate unpacked header1 buffer");
                nmo_io_close(io);
                return NMO_ERR_NOMEM;
            }

            mz_ulong dest_len = header.hdr1_unpack_size;
            int uncompress_result = mz_uncompress((unsigned char *) hdr1_data, &dest_len,
                                                  (const unsigned char *) packed_hdr1,
                                                  header.hdr1_pack_size);
            if (uncompress_result != MZ_OK) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to decompress header1: %d",
                        uncompress_result);
                nmo_io_close(io);
                return NMO_ERR_INVALID_ARGUMENT;
            }

            if (dest_len != header.hdr1_unpack_size) {
                nmo_log(logger, NMO_LOG_ERROR, "Header1 decompression size mismatch: expected %u, got %lu",
                        header.hdr1_unpack_size, dest_len);
                nmo_io_close(io);
                return NMO_ERR_INVALID_ARGUMENT;
            }

            hdr1_size = dest_len;
            nmo_log(logger, NMO_LOG_INFO, "  Decompression successful: %zu bytes", hdr1_size);
        } else {
            /* Already uncompressed */
            hdr1_data = packed_hdr1;
            hdr1_size = header.hdr1_pack_size;
        }

        /* Phase 4: Parse Header1 */
        nmo_log(logger, NMO_LOG_INFO, "Phase 4: Parsing header1");
        result = nmo_header1_parse(hdr1_data, hdr1_size, &hdr1, arena);
        if (result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to parse header1");
            nmo_io_close(io);
            return NMO_ERR_INVALID_ARGUMENT;
        }
    }

    int dep_store_result = nmo_session_set_plugin_dependencies(session, hdr1.plugin_deps, hdr1.plugin_dep_count);
    if (dep_store_result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR,
                "Failed to store plugin dependencies (code=%d)", dep_store_result);
        nmo_io_close(io);
        return dep_store_result;
    }

        nmo_log(logger, NMO_LOG_INFO, "Found %u objects, %u managers, %u plugins",
            hdr1.object_count, header.manager_count, hdr1.plugin_dep_count);

    /* Phase 5: Start Load Session */
    nmo_log(logger, NMO_LOG_INFO, "Phase 5: Starting load session (max ID: %u)",
            header.max_id_saved);

    nmo_load_session_t *load_session = nmo_load_session_start(repo, header.max_id_saved);
    if (load_session == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to start load session");
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }

    /* Phase 6: Check Plugin Dependencies */
        nmo_log(logger, NMO_LOG_INFO, "Phase 6: Checking plugin dependencies (%u plugins)",
            hdr1.plugin_dep_count);

    nmo_extension_registry_t *ext_registry = nmo_session_get_extension_registry(session);

    if (hdr1.plugin_dep_count > 0 && ext_registry == NULL) {
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
    } else if (hdr1.plugin_dep_count > 0) {
        nmo_log(logger, NMO_LOG_INFO,
                "  Plugin diagnostics unavailable (dependencies=%u)", hdr1.plugin_dep_count);
    }

    if (missing_plugins > 0 && enforce_plugin_dependencies) {
        nmo_log(logger, NMO_LOG_ERROR,
                "Missing %zu required plugin(s); aborting due to NMO_LOAD_CHECK_DEPENDENCIES", missing_plugins);
        nmo_load_session_destroy(load_session);
        nmo_io_close(io);
        return NMO_ERR_NOT_FOUND;
    }

    /* Phase 7: Manager Pre-Load Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 7: Executing manager pre-load hooks");

    nmo_manager_registry_t *manager_reg = nmo_context_get_manager_registry(ctx);
    if (manager_reg != NULL) {
        uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);
        nmo_log(logger, NMO_LOG_INFO, "  Found %u registered managers", manager_count);

        for (uint32_t i = 0; i < manager_count; i++) {
            uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
            nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_get(manager_reg, manager_id);

            if (manager != NULL) {
                nmo_runtime_event_ctx_t event_ctx = {
                    .event = NMO_RUNTIME_EVENT_PRE_LOAD,
                    .manager_id = manager_id,
                    .manager_guid = manager->guid
                };
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
            header.data_pack_size);

    nmo_data_section_t data_sect;
    memset(&data_sect, 0, sizeof(nmo_data_section_t));
    data_sect.manager_count = header.manager_count;
    data_sect.object_count = header.object_count;

    /* Skip data section if empty */
    if (header.data_pack_size == 0 || header.data_unpack_size == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No data section (empty file or minimal format)");
    } else {
        /* Read packed data */
        void *packed_buffer = nmo_arena_alloc(arena, header.data_pack_size, 16);
        if (packed_buffer == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate packed data buffer");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return NMO_ERR_NOMEM;
        }

        size_t bytes_read = 0;
        int read_result = nmo_io_read(io, packed_buffer, header.data_pack_size, &bytes_read);
        if (read_result != NMO_OK || bytes_read != header.data_pack_size) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to read data section");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return NMO_ERR_INVALID_ARGUMENT;
        }

        /* Decompress if needed */
        void *data_buffer = NULL;
        size_t data_size = 0;

        if (header.data_pack_size != header.data_unpack_size) {
            nmo_log(logger, NMO_LOG_INFO, "  Decompressing data: %u -> %u bytes",
                    header.data_pack_size, header.data_unpack_size);

            data_buffer = nmo_arena_alloc(arena, header.data_unpack_size, 16);
            if (data_buffer == NULL) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate unpacked data buffer");
                nmo_load_session_destroy(load_session);
                nmo_io_close(io);
                return NMO_ERR_NOMEM;
            }

            mz_ulong dest_len = header.data_unpack_size;
            int uncompress_result = mz_uncompress((unsigned char *) data_buffer, &dest_len,
                                                  (const unsigned char *) packed_buffer,
                                                  header.data_pack_size);
            if (uncompress_result != MZ_OK) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to decompress data section: %d",
                        uncompress_result);
                nmo_load_session_destroy(load_session);
                nmo_io_close(io);
                return NMO_ERR_INVALID_ARGUMENT;
            }

            if (dest_len != header.data_unpack_size) {
                nmo_log(logger, NMO_LOG_ERROR, "Data decompression size mismatch: expected %u, got %lu",
                        header.data_unpack_size, dest_len);
                nmo_load_session_destroy(load_session);
                nmo_io_close(io);
                return NMO_ERR_INVALID_ARGUMENT;
            }

            data_size = dest_len;
            nmo_log(logger, NMO_LOG_INFO, "  Decompression successful: %zu bytes", data_size);
        } else {
            /* Already uncompressed */
            data_buffer = packed_buffer;
            data_size = header.data_pack_size;
        }

        /* Parse Data section */
        if (chunk_pool == NULL) {
            size_t pool_hint = (size_t)header.object_count + (size_t)header.manager_count;
            chunk_pool = nmo_session_ensure_chunk_pool(session, pool_hint);
            if (chunk_pool == NULL) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Chunk pool unavailable; falling back to direct chunk allocations");
            }
        }

        result = nmo_data_section_parse(data_buffer, data_size, header.file_version,
                                        &data_sect, chunk_pool, arena);
        if (result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to parse data section");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return result;
        }

        nmo_log(logger, NMO_LOG_INFO, "  Data section parsed successfully");
        nmo_log(logger, NMO_LOG_INFO, "  Managers parsed: %u", data_sect.manager_count);
        nmo_log(logger, NMO_LOG_INFO, "  Objects parsed: %u", data_sect.object_count);

    }

    {
        int included_result = nmo_load_included_files(
            session,
            io,
            &hdr1,
            logger,
            shadow_storage,
            preserve_shadow,
            opts->max_included_name_len,
            opts->max_included_file_size);
        if (included_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to load included files (code=%d)", included_result);
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return included_result;
        }
    }

    /* Phase 9: Parse Manager Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 9: Parsing manager chunks");

    /* Process manager chunks if present */
    if (data_sect.managers != NULL) {
        for (uint32_t i = 0; i < data_sect.manager_count; i++) {
            nmo_manager_data_t *mgr_data = &data_sect.managers[i];
            nmo_log(logger, NMO_LOG_INFO, "  Manager %u: GUID={0x%08X,0x%08X}, DataSize=%u",
                    i, mgr_data->guid.d1, mgr_data->guid.d2, mgr_data->data_size);

            /* Manager chunks are dispatched in Phase 13b for proper deserialization */
            if (mgr_data->chunk != NULL) {
                nmo_log(logger, NMO_LOG_INFO, "    Manager chunk present (version %u)",
                        mgr_data->chunk->chunk_version);
            }
        }

        /* Store manager data in session for round-trip */
        nmo_session_set_manager_data(session, data_sect.managers, data_sect.manager_count);
    } else {
        nmo_log(logger, NMO_LOG_INFO, "  No manager chunks to process");
    }

    /* Phase 10: Create Objects */
    nmo_log(logger, NMO_LOG_INFO, "Phase 10: Creating %u objects", hdr1.object_count);

    size_t remap_error_count = 0;

    /* Skip object creation if no header1 data or no object descriptors */
    if (hdr1.objects == NULL || hdr1.object_count == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No objects to create (empty file or no object descriptors)");
        goto skip_object_processing;
    }

    {
        const nmo_allocator_t *obj_allocator = nmo_context_get_allocator(ctx);
        int prep_result = nmo_object_system_prepare_loaded_objects(
            obj_allocator,
            arena,
            repo,
            id_sanitizer,
            load_session,
            hdr1.objects,
            hdr1.object_count,
            data_sect.objects,
            data_sect.object_count,
            data_sect.managers,
            data_sect.manager_count,
            logger,
            &remap_error_count);

        if (prep_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to prepare loaded objects (code=%d)", prep_result);
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return prep_result;
        }

        if (remap_error_count > 0) {
            nmo_log(logger, NMO_LOG_WARN,
                    "  ID remapping completed with %zu errors", remap_error_count);
        }
    }

    /* Phase 13b: Dispatch manager chunks to registered managers */
    nmo_log(logger, NMO_LOG_INFO, "Phase 13b: Dispatching manager chunks");

    if (data_sect.managers != NULL && data_sect.manager_count > 0) {
        if (manager_reg == NULL) {
            nmo_log(logger, NMO_LOG_WARN, "  Manager registry unavailable; preserving %u chunk(s) for round-trip",
                    data_sect.manager_count);
        } else {
            for (uint32_t i = 0; i < data_sect.manager_count; i++) {
                nmo_manager_data_t *mgr_data = &data_sect.managers[i];
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

                nmo_runtime_event_ctx_t event_ctx = {
                    .event = NMO_RUNTIME_EVENT_POST_LOAD,
                    .manager_id = 0,
                    .manager_guid = mgr_data->guid,
                    .manager_chunk_in = chunk
                };
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
        const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(ctx);
        if (type_rt == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Type registry not initialized in context");
            nmo_load_session_end(load_session);
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return NMO_ERR_INVALID_STATE;
        }

        nmo_object_system_deserialize_stats_t stats = {0};
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
            hdr1.object_count,
            &stats);

        if (deser_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "  Repository deserialization failed (code=%d)", deser_result);
            nmo_load_session_end(load_session);
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return deser_result;
        }

        nmo_log(logger, NMO_LOG_INFO,
                "  Deserialization summary: %zu deserialized, %zu no schema, %zu skipped (no chunk), %zu errors",
                stats.deserialized,
                stats.no_schema,
                stats.skipped_no_chunk + stats.skipped_null + stats.skipped_empty_chunk,
                stats.errors);
    }

skip_object_processing:
    /* Update repo_count after potential skip */
    nmo_object_repository_get_all(repo, &repo_count);

    /* Cleanup */
    nmo_load_session_end(load_session);
    nmo_load_session_destroy(load_session);
    nmo_io_close(io);

    nmo_log(logger, NMO_LOG_INFO, "Load complete: %zu objects loaded", repo_count);

    /* Runtime kernel finalizes load semantics (dependency remap, post-hooks, indexing). */
    {
        nmo_runtime_request_t request = {
            .kind = NMO_RUNTIME_OP_LOAD,
            .flags = NMO_RUNTIME_REQUEST_DEFAULT
        };
        nmo_runtime_report_t report;
        memset(&report, 0, sizeof(report));
        int runtime_result = nmo_runtime_kernel_finalize_load(session, &request, &report);
        if (runtime_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_WARN,
                    "Runtime load finalization failed: %d (continuing)", runtime_result);
        }
    }

    return NMO_OK;
}

/**
 * @brief Return default load options
 */
nmo_load_options_t nmo_load_options_default(void) {
    nmo_load_options_t opts = {
        .allocator = NULL,
        .flags = NMO_LOAD_DEFAULT,
        .max_included_name_len = NMO_LOAD_DEFAULT_MAX_INCLUDED_NAME_LEN,
        .max_included_file_size = NMO_LOAD_DEFAULT_MAX_INCLUDED_FILE_SIZE,
    };
    return opts;
}

/**
 * @brief Detect if a file uses compression by reading its header
 *
 * @param path File path to inspect
 * @return 1 if compressed, 0 if uncompressed, -1 on error
 */
static int nmo_detect_file_compression(const char *path) {
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        return -1;
    }
    
    /* Parse the file header to check compression flags */
    nmo_file_header_t header;
    nmo_status_t result = nmo_file_header_parse(io, &header);
    nmo_io_close(io);
    
    if (result != NMO_OK) {
        return -1;
    }
    
    /* Check if any compression is enabled */
    const uint32_t compression_mask = NMO_FILE_WRITE_COMPRESS_HEADER | NMO_FILE_WRITE_COMPRESS_DATA;
    int is_compressed = (header.file_write_mode & compression_mask) != 0;
    
    /* Also check if header1 is compressed (pack_size != unpack_size) */
    if (header.hdr1_pack_size != header.hdr1_unpack_size) {
        is_compressed = 1;
    }
    
    /* And data section compression */
    if (header.data_pack_size != header.data_unpack_size) {
        is_compressed = 1;
    }
    
    return is_compressed;
}

/**
 * @brief Load file with automatic IO selection
 *
 * Detects compression and uses mmap for uncompressed files when
 * supported, falling back to standard file IO otherwise.
 */
int nmo_load_file(nmo_session_t *session,
                  const char *path,
                  const nmo_load_options_t *opts) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Use defaults if no options provided */
    nmo_load_options_t local_opts;
    if (opts == NULL) {
        local_opts = nmo_load_options_default();
        opts = &local_opts;
    }
    
    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_logger_t *logger = nmo_context_get_logger(ctx);
    
    /* Select best IO path automatically (mmap for uncompressed, fallback to standard) */
    int is_compressed = nmo_detect_file_compression(path);
    if (is_compressed < 0) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to detect file compression for: %s", path);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    if (!is_compressed && nmo_io_mmap_supported()) {
        nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file (mmap): %s", path);
        nmo_io_interface_t *io = nmo_mmap_io_open(path);
        if (io == NULL) {
            nmo_log(logger, NMO_LOG_WARN,
                    "Failed to open mmap for file, falling back to standard IO: %s", path);
        } else {
            return nmo_load_file_with_io(session, path, io, opts);
        }
    }

    nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file: %s", path);
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to open file: %s", path);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    return nmo_load_file_with_io(session, path, io, opts);
}
