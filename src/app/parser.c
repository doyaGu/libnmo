/**
 * @file parser.c
 * @brief Load and save pipeline implementation (Phase 9 & 10)
 *
 * The save pipeline has been refactored to use the two-phase commit
 * architecture implemented in save_pipeline.c (Phase 1.4).
 */

#include "app/nmo_parser.h"
#include "app/nmo_save_pipeline.h"
#include "app/nmo_session.h"
#include "app/nmo_plugin.h"
#include "app/nmo_context.h"
#include "app/nmo_finish_loading.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
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
#include "session/nmo_object_repository.h"
#include "session/nmo_reference_resolver.h"
#include "type/type_system.h"
#include "core/nmo_guid.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
#include "miniz.h"  /* For compression/decompression */

/**
 * @brief Get plugin category label for logging
 */
static const char *nmo_plugin_category_label(uint32_t category) {
    switch (category) {
        case NMO_PLUGIN_MANAGER_DLL:  return "Manager";
        case NMO_PLUGIN_BEHAVIOR_DLL: return "Behavior";
        case NMO_PLUGIN_RENDER_DLL:   return "Render";
        case NMO_PLUGIN_SOUND_DLL:    return "Sound";
        case NMO_PLUGIN_INPUT_DLL:    return "Input";
        case NMO_PLUGIN_OBJECT_READER_DLL: return "ObjectReader";
        case NMO_PLUGIN_CUSTOM_DLL: return "Custom";
        default: return "Unknown";
    }
}

/**
 * @brief Format GUID to short string for logging
 */
static void nmo_format_guid_short(nmo_guid_t guid, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    if (nmo_guid_format(guid, buffer, buffer_size) <= 0) {
        buffer[0] = '\0';
    }
}

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

static int nmo_load_included_files(
    nmo_session_t *session,
    nmo_io_interface_t *io,
    const nmo_header1_t *hdr1,
    nmo_logger_t *logger
) {
    if (session == NULL || io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t expected = (hdr1 != NULL) ? hdr1->included_file_count : 0;
    uint32_t parsed = 0;
    nmo_arena_t *arena = nmo_session_get_arena(session);

    while (1) {
        uint32_t name_len = 0;
        int read_result = nmo_io_read_u32(io, &name_len);
        if (read_result != NMO_OK) {
            if (read_result == NMO_ERR_EOF) {
                break;
            }

            if (read_result == NMO_ERR_INVALID_ARGUMENT) {
                break;
            }

            nmo_log(logger, NMO_LOG_WARN,
                    "Failed to read included filename length: %d", read_result);
            return read_result;
        }

        char *name_buf = (char *) nmo_arena_alloc(arena, name_len + 1, 1);
        if (name_buf == NULL) {
            return NMO_ERR_NOMEM;
        }

        if (name_len > 0) {
            size_t bytes_read = 0;
            int name_read = nmo_io_read(io, name_buf, name_len, &bytes_read);
            if (name_read != NMO_OK || bytes_read != name_len) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "Failed to read included filename payload");
                return (name_read != NMO_OK) ? name_read : NMO_ERR_EOF;
            }
        }
        name_buf[name_len] = '\0';

        uint32_t data_size = 0;
        if (nmo_io_read_u32(io, &data_size) != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR,
                    "Failed to read included file size for '%s'", name_buf);
            return NMO_ERR_EOF;
        }

        void *payload = NULL;
        if (data_size > 0) {
            payload = nmo_arena_alloc(arena, data_size, 1);
            if (payload == NULL) {
                return NMO_ERR_NOMEM;
            }

            size_t bytes_read = 0;
            int data_result = nmo_io_read(io, payload, data_size, &bytes_read);
            if (data_result != NMO_OK || bytes_read != data_size) {
                nmo_log(logger, NMO_LOG_ERROR,
                        "Failed to read included payload for '%s'", name_buf);
                return (data_result != NMO_OK) ? data_result : NMO_ERR_EOF;
            }
        }

        int add_result = nmo_session_add_included_file_borrowed(
            session,
            name_buf,
            payload,
            data_size);
        if (add_result != NMO_OK) {
            return add_result;
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

    if (expected > parsed) {
        nmo_log(logger, NMO_LOG_INFO,
                "  Header references %u included file(s), parsed %u entries",
                expected, parsed);

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
                    return meta_result;
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

    return NMO_OK;
}


/**
 * Load file - 15-phase load pipeline (shared implementation)
 */
static int nmo_load_file_with_io(
    nmo_session_t *session,
    const char *path,
    nmo_io_interface_t *io,
    nmo_load_flags_t flags
) {
    if (session == NULL || path == NULL || io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_chunk_pool_t *chunk_pool = NULL;
    nmo_logger_t *logger = nmo_context_get_logger(ctx);

    nmo_session_reset_reference_resolver(session);

    const int enforce_plugin_dependencies = (flags & NMO_LOAD_CHECK_DEPENDENCIES) != 0;

    /* Phase 2: Parse File Header */
    nmo_log(logger, NMO_LOG_INFO, "Phase 2: Parsing file header");
    nmo_file_header_t header;
    nmo_result_t result = nmo_file_header_parse(io, &header);
    if (result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to parse file header");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    result = nmo_file_header_validate(&header);
    if (result.code != NMO_OK) {
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
        if (result.code != NMO_OK) {
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

    nmo_log(logger, NMO_LOG_INFO, "Found %u objects, %u managers, %zu plugins",
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
    nmo_log(logger, NMO_LOG_INFO, "Phase 6: Checking plugin dependencies (%zu plugins)",
            hdr1.plugin_dep_count);

    nmo_plugin_manager_t *plugin_manager = nmo_session_get_plugin_manager(session);

    if (hdr1.plugin_dep_count > 0 && plugin_manager == NULL) {
        nmo_log(logger, NMO_LOG_WARN, "  Plugin dependencies present but plugin manager is unavailable");
    }

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(session);
    size_t missing_plugins = (diag != NULL) ? diag->missing_count : 0;
    size_t outdated_plugins = (diag != NULL) ? diag->outdated_count : 0;
    (void)outdated_plugins;
    if (diag != NULL && diag->entries != NULL) {
        for (size_t i = 0; i < diag->entry_count; i++) {
            const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
            char guid_buffer[32];
            nmo_format_guid_short(entry->guid, guid_buffer, sizeof(guid_buffer));

            if (entry->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_MISSING) {
                nmo_log(logger, NMO_LOG_WARN,
                        "  Missing plugin %zu: guid=%s category=%s version=%u",
                        i,
                        guid_buffer,
                        nmo_plugin_category_label(entry->category),
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
                int hook_result = nmo_manager_invoke_pre_load(manager, session);
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
        if (result.code != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to parse data section");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return result.code;
        }

        nmo_log(logger, NMO_LOG_INFO, "  Data section parsed successfully");
        nmo_log(logger, NMO_LOG_INFO, "  Managers parsed: %u", data_sect.manager_count);
        nmo_log(logger, NMO_LOG_INFO, "  Objects parsed: %u", data_sect.object_count);

    }

    {
        int included_result = nmo_load_included_files(session, io, &hdr1, logger);
        if (included_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_WARN,
                    "Failed to load included files (code=%d)", included_result);
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

    nmo_id_remap_table_t *remap_table = NULL;

    /* Temporary array to map file index to created objects (for Phase 11) */
    nmo_object_t **created_objects = NULL;

    /* Skip object creation if no header1 data or no object descriptors */
    if (hdr1.objects == NULL || hdr1.object_count == 0) {
        nmo_log(logger, NMO_LOG_INFO, "  No objects to create (empty file or no object descriptors)");
        goto skip_object_processing;
    }

    /* Allocate temporary mapping array */
    created_objects = (nmo_object_t **) nmo_arena_alloc(arena,
                                                        sizeof(nmo_object_t *) * hdr1.object_count,
                                                        sizeof(void *));
    if (created_objects == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate object mapping array");
        nmo_load_session_destroy(load_session);
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }
    memset(created_objects, 0, sizeof(nmo_object_t *) * hdr1.object_count);

    for (size_t i = 0; i < hdr1.object_count; i++) {
        nmo_object_desc_t *desc = &hdr1.objects[i];

        /* Skip reference-only objects */
        if (desc->file_id & NMO_OBJECT_REFERENCE_FLAG) {
            nmo_log(logger, NMO_LOG_INFO, "  Object %zu: reference-only, skipping", i);
            created_objects[i] = NULL;
            continue;
        }

        /* Create object */
        nmo_object_t *obj = (nmo_object_t *) nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                             sizeof(void *));
        if (obj == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate object");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return NMO_ERR_NOMEM;
        }

        memset(obj, 0, sizeof(nmo_object_t));
        obj->class_id = desc->class_id;
        obj->name = desc->name;
        obj->flags = desc->flags;
        obj->arena = arena;

        /* Add to repository (assigns runtime ID) */
        int add_result = nmo_object_repository_add(repo, obj);
        if (add_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to add object to repository");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return add_result;
        }

        /* Register with load session (file ID -> runtime ID mapping) */
        int reg_result = nmo_load_session_register(load_session, obj, desc->file_id);
        if (reg_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to register object in load session");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return reg_result;
        }

        /* Store in temporary mapping */
        created_objects[i] = obj;

        nmo_log(logger, NMO_LOG_INFO, "  Created object %zu: file_id=%u, runtime_id=%u, class=0x%08X, name='%s'",
                i, desc->file_id, obj->id, obj->class_id, obj->name ? obj->name : "(null)");
    }

    /* Phase 11: Attach Object Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 11: Attaching object chunks");

    /* Connect object chunks to objects created in Phase 10 */
    if (data_sect.objects != NULL && created_objects != NULL) {
        for (uint32_t i = 0; i < data_sect.object_count && i < hdr1.object_count; i++) {
            nmo_object_data_t *obj_data = &data_sect.objects[i];
            nmo_object_t *obj = created_objects[i];

            /* Skip if object wasn't created (reference-only) */
            if (obj == NULL) {
                continue;
            }

            /* Attach chunk to object */
            obj->chunk = obj_data->chunk;

            if (obj_data->chunk != NULL) {
                nmo_log(logger, NMO_LOG_INFO, "  Object %u: runtime_id=%u, chunk attached (size=%u, version=%u)",
                        i, obj->id, obj_data->data_size, obj_data->chunk->chunk_version);
            } else {
                nmo_log(logger, NMO_LOG_INFO, "  Object %u: runtime_id=%u, no chunk data",
                        i, obj->id);
            }
        }
    } else {
        nmo_log(logger, NMO_LOG_INFO, "  No object chunks to attach");
    }

    /* Phase 12: Build ID Remap Table */
    nmo_log(logger, NMO_LOG_INFO, "Phase 12: Building ID remap table");

    remap_table = nmo_build_remap_table(load_session);
    if (remap_table == NULL) {
        nmo_log(logger, NMO_LOG_WARN, "Failed to build ID remap table (may be empty session)");
    } else {
        size_t remap_count = nmo_id_remap_table_get_count(remap_table);
        nmo_log(logger, NMO_LOG_INFO, "  Built remap table with %zu entries", remap_count);
    }

    /* Phase 13: Remap IDs in All Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 13: Remapping IDs in chunks");
    if (remap_table != NULL) {
        size_t remap_error_count = 0;
        for (size_t i = 0; i < hdr1.object_count; i++) {
            if (created_objects[i] != NULL && created_objects[i]->chunk != NULL) {
                nmo_result_t remap_result = nmo_chunk_remap_object_ids(created_objects[i]->chunk, remap_table);
                if (remap_result.code != NMO_OK) {
                    nmo_log(logger, NMO_LOG_ERROR, "  Failed to remap IDs in object %zu chunk", i);
                    remap_error_count++;
                }
            }
        }
        // Also remap manager chunks
        if (data_sect.managers != NULL) {
            for (uint32_t i = 0; i < data_sect.manager_count; i++) {
                if (data_sect.managers[i].chunk != NULL) {
                    nmo_result_t remap_result = nmo_chunk_remap_object_ids(data_sect.managers[i].chunk, remap_table);
                    if (remap_result.code != NMO_OK) {
                        nmo_log(logger, NMO_LOG_ERROR, "  Failed to remap IDs in manager %u chunk", i);
                        remap_error_count++;
                    }
                }
            }
        }
        if (remap_error_count > 0) {
            nmo_log(logger, NMO_LOG_WARN, "  ID remapping completed with %zu errors", remap_error_count);
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
                nmo_format_guid_short(mgr_data->guid, guid_buffer, sizeof(guid_buffer));

                nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_find_by_guid(
                    manager_reg,
                    mgr_data->guid);

                if (manager == NULL) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Skipping manager chunk GUID=%s (no registered manager); data preserved",
                            guid_buffer);
                    continue;
                }

                if (manager->load_data == NULL) {
                    nmo_log(logger, NMO_LOG_WARN,
                            "  Manager %s (GUID=%s) has no load_data hook; data preserved",
                            manager->name ? manager->name : "<unnamed>", guid_buffer);
                    continue;
                }

                const nmo_chunk_t *chunk = mgr_data->chunk;
                if (chunk == NULL) {
                    nmo_log(logger, NMO_LOG_INFO,
                            "  Manager %s (GUID=%s) has no chunk payload; nothing to dispatch",
                            manager->name ? manager->name : "<unnamed>", guid_buffer);
                    continue;
                }

                int load_result = nmo_manager_invoke_load_data(manager, session, chunk);
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

    size_t repo_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &repo_count);
    
    if (objects == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "  Failed to get objects from repository");
        goto skip_object_processing;
    }
    
    /* Get type registry for schema-based deserialization with vtable dispatch */
    nmo_type_registry_t *type_reg = nmo_context_get_type_registry(ctx);
    
    if (type_reg == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Type registry not initialized in context");
        return -1; /* Parser function returns int, not nmo_result_t */
    }

    size_t deserialized_count = 0;
    size_t skipped_count = 0;
    size_t error_count = 0;
    size_t no_schema_count = 0;

    for (size_t i = 0; i < repo_count; i++) {
        nmo_object_t *obj = objects[i];
        
        if (obj == NULL) {
            nmo_log(logger, NMO_LOG_WARN, "  Object %zu is NULL, skipping", i);
            skipped_count++;
            continue;
        }
        
        /* Skip objects without chunks (reference-only objects) */
        if (obj->chunk == NULL) {
            skipped_count++;
            continue;
        }

        /* Prepare chunk for reading - wrap in error handler to prevent crash */
        nmo_result_t read_result = {NMO_OK, NULL};
        
        /* Defensive: catch potential chunk corruption */
        if (obj->chunk->data.count == 0 || obj->chunk->data.data == NULL) {
            nmo_log(logger, NMO_LOG_WARN, "  Object %zu (ID=%u): chunk has invalid data pointer or zero size, skipping",
                    i, obj->id);
            skipped_count++;
            continue;
        }
        
        if (obj->chunk->arena == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "  Object %zu (ID=%u): chunk has NULL arena, skipping",
                    i, obj->id);
            error_count++;
            continue;
        }
        
        read_result = nmo_chunk_start_read(obj->chunk);
        if (read_result.code != NMO_OK) {
            error_count++;
            nmo_log(logger, NMO_LOG_ERROR, "  Object %zu (ID=%u): failed to start chunk read: %d",
                    i, obj->id, read_result.code);
            continue;
        }

        /* Query class hierarchy system for class info */
        const char *class_name = "Unknown";  /* Class name lookup removed */
        
        if (class_name == NULL) {
            /* Class ID not registered in hierarchy - no schema available */
            no_schema_count++;
            nmo_log(logger, NMO_LOG_WARN, "  Object %zu (ID=%u, class=0x%08X): unknown class ID, preserving raw chunk",
                    i, obj->id, obj->class_id);
            continue;
        }
        
        /* Find schema type with inheritance-based fallback
         * This searches up the class hierarchy until a schema is found */
        const nmo_type_descriptor_t *schema_type = 
            nmo_type_registry_find_by_class_id_inherited(type_reg, obj->class_id);
        
        if (schema_type == NULL) {
            /* No schema found even after checking parent classes */
            no_schema_count++;
            nmo_log(logger, NMO_LOG_WARN, "  Object %zu (ID=%u, class=0x%08X, type=%s): no schema found in hierarchy",
                    i, obj->id, obj->class_id, class_name);
            continue;
        }
        
        /* Check if schema has vtable with deserialize function */
        if (schema_type->vtable == NULL || schema_type->vtable->deserialize == NULL) {
            no_schema_count++;
            nmo_log(logger, NMO_LOG_WARN, "  Object %zu (ID=%u, class=0x%08X, type=%s): schema '%s' has no vtable read function",
                    i, obj->id, obj->class_id, class_name, schema_type->name);
            continue;
        }

        /* Allocate state structure based on schema size */
        void *state = nmo_arena_alloc(arena, schema_type->size, 8); /* 8-byte alignment for structs */
        if (state == NULL) {
            error_count++;
            nmo_log(logger, NMO_LOG_ERROR, "  Object %zu (ID=%u): failed to allocate %zu bytes for state",
                    i, obj->id, schema_type->size);
            continue;
        }
        memset(state, 0, schema_type->size);
        
        /* Call vtable read function (schema-driven deserialization) */
        nmo_result_t result = schema_type->vtable->deserialize(state, obj->chunk, schema_type, arena);
        
        /* Check result */
        if (result.code == NMO_OK) {
            /* Store state in object for later access */
            nmo_object_set_data(obj, state);
            deserialized_count++;
            nmo_log(logger, NMO_LOG_DEBUG, "  Object %zu (ID=%u, class=0x%08X, type=%s): deserialized",
                    i, obj->id, obj->class_id, class_name);
        } else {
            error_count++;
            const char *error_msg = result.error ? result.error->message : "unknown error";
            nmo_log(logger, NMO_LOG_ERROR, "  Object %zu (ID=%u, class=0x%08X, type=%s): deserialization failed: %s",
                    i, obj->id, obj->class_id, class_name, error_msg);
            /* Chain the error for better debugging */
            if (result.error != NULL) {
                nmo_log(logger, NMO_LOG_ERROR, "    Error chain: code=%d, severity=%d",
                        result.error->code, result.error->severity);
            }
        }
        
        i++; /* Increment loop counter */
    }

    nmo_log(logger, NMO_LOG_INFO, "  Deserialization summary: %zu deserialized, %zu no schema, %zu skipped (no chunk), %zu errors",
            deserialized_count, no_schema_count, skipped_count, error_count);

skip_object_processing:
    /* Update repo_count after potential skip */
    nmo_object_repository_get_all(repo, &repo_count);

    /* Phase 16: Manager Post-Load Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 16: Executing manager post-load hooks");

    if (manager_reg != NULL) {
        uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);

        for (uint32_t i = 0; i < manager_count; i++) {
            uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
            nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_get(manager_reg, manager_id);

            if (manager != NULL) {
                int hook_result = nmo_manager_invoke_post_load(manager, session);
                if (hook_result != NMO_OK) {
                    nmo_log(logger, NMO_LOG_WARN, "  Manager %u post-load hook failed: %d",
                            manager_id, hook_result);
                } else {
                    nmo_log(logger, NMO_LOG_INFO, "  Manager %u post-load hook executed", manager_id);
                }
            }
        }
    }

    /* Cleanup */
    if (remap_table != NULL) {
        nmo_id_remap_table_destroy(remap_table);
    }
    nmo_load_session_end(load_session);
    nmo_load_session_destroy(load_session);
    nmo_io_close(io);

    nmo_log(logger, NMO_LOG_INFO, "Load complete: %zu objects loaded", repo_count);

    /* Phase 17: Session-Level FinishLoading (Reference Resolution & Indexing) */
    nmo_log(logger, NMO_LOG_INFO, "Phase 17: Executing session-level finish loading");
    
    /* Determine finish loading flags based on load flags */
    uint32_t finish_flags = NMO_FINISH_LOAD_DEFAULT;
    
    if (flags & NMO_LOAD_SKIP_INDEX_BUILD) {
        /* Disable index building if requested */
        finish_flags &= ~NMO_FINISH_LOAD_BUILD_INDEXES;
    }
    
    if (flags & NMO_LOAD_SKIP_REFERENCE_RESOLVE) {
        /* Disable reference resolution if requested */
        finish_flags &= ~NMO_FINISH_LOAD_RESOLVE_REFERENCES;
    }
    
    /* Execute finish loading */
    int finish_result = nmo_session_finish_loading(session, finish_flags);
    if (finish_result != NMO_OK) {
        nmo_log(logger, NMO_LOG_WARN, "FinishLoading phase failed: %d (continuing anyway)", finish_result);
        /* Don't fail the entire load for finish loading issues */
    }

    return NMO_OK;
}

/**
 * Load file - 15-phase load pipeline
 */
int nmo_load_file(nmo_session_t *session, const char *path, nmo_load_flags_t flags) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_logger_t *logger = nmo_context_get_logger(ctx);

    /* Phase 1: Open IO */
    nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file: %s", path);
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to open file: %s", path);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    return nmo_load_file_with_io(session, path, io, flags);
}

/**
 * @brief Return default load options (Phase 2.1 Dual-Track IO)
 */
nmo_load_options_t nmo_load_options_default(void) {
    nmo_load_options_t opts = {
        .strategy = NMO_LOAD_STRATEGY_AUTO,
        .strict_crc = 0,
        .preserve_shadow = 1,
        .allocator = NULL,
        .flags = NMO_LOAD_DEFAULT
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
    nmo_result_t result = nmo_file_header_parse(io, &header);
    nmo_io_close(io);
    
    if (result.code != NMO_OK) {
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
 * @brief Load file with extended options (Phase 2.1 Dual-Track IO)
 *
 * Extended version of nmo_load_file() that accepts load options for
 * fine-grained control over the loading process. Currently supports:
 * - Strategy selection (auto-detect, force compressed, or force mmap)
 * - Flag passthrough to underlying loader
 *
 * Note: MMAP strategy is currently a no-op placeholder (falls back to
 * compressed path). Full mmap support requires IO layer changes.
 */
int nmo_load_file_ex(nmo_session_t *session,
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
    
    /* Determine actual strategy based on AUTO detection */
    nmo_load_strategy_t actual_strategy = opts->strategy;
    
    if (actual_strategy == NMO_LOAD_STRATEGY_AUTO) {
        int is_compressed = nmo_detect_file_compression(path);
        if (is_compressed < 0) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to detect file compression for: %s", path);
            return NMO_ERR_FILE_NOT_FOUND;
        }
        
        actual_strategy = is_compressed ? NMO_LOAD_STRATEGY_COMPRESSED : NMO_LOAD_STRATEGY_MMAP;
        nmo_log(logger, NMO_LOG_INFO, "Auto-detected load strategy: %s",
                actual_strategy == NMO_LOAD_STRATEGY_COMPRESSED ? "COMPRESSED" : "MMAP");
    }
    
    /* Store the detected strategy in session metadata */
    /* TODO: Add session field for tracking load strategy once session.c is updated */
    
    /* Execute the appropriate loading path */
    if (actual_strategy == NMO_LOAD_STRATEGY_MMAP) {
        if (!nmo_io_mmap_supported()) {
            nmo_log(logger, NMO_LOG_WARN,
                    "MMAP strategy requested but not supported on this platform, "
                    "falling back to standard loader");
            return nmo_load_file(session, path, opts->flags);
        }

        int is_compressed = nmo_detect_file_compression(path);
        if (is_compressed < 0) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to detect file compression for: %s", path);
            return NMO_ERR_FILE_NOT_FOUND;
        }

        if (is_compressed) {
            nmo_log(logger, NMO_LOG_WARN,
                    "MMAP strategy requested for compressed file; falling back to standard loader");
            return nmo_load_file(session, path, opts->flags);
        }

        nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file (mmap): %s", path);
        nmo_io_interface_t *io = nmo_mmap_io_open(path);
        if (io == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to open mmap for file: %s", path);
            return NMO_ERR_FILE_NOT_FOUND;
        }

        return nmo_load_file_with_io(session, path, io, opts->flags);
    }

    /* Compressed/standard path */
    return nmo_load_file(session, path, opts->flags);
}

/**
 * @brief Get the load strategy that was actually used
 *
 * Note: Currently returns AUTO since we don't track strategy in session yet.
 * TODO: Store detected strategy in session and return it here.
 */
nmo_load_strategy_t nmo_session_get_load_strategy(const nmo_session_t *session) {
    (void)session;  /* Unused for now */
    return NMO_LOAD_STRATEGY_AUTO;  /* TODO: Return actual strategy once tracked */
}

/**
 * Save file - Two-phase commit save pipeline (Phase 1.4)
 *
 * This function now delegates to the new two-phase commit architecture:
 *   Phase 1: Layout & Serialize (all data to memory)
 *   Phase 2: Pack & Commit (compress, CRC, write atomically)
 *
 * The old 14-phase inline implementation has been refactored into
 * save_pipeline.c for better separation of concerns and testability.
 */
int nmo_save_file(nmo_session_t *session, const char *path, nmo_save_flags_t flags) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Build save options from flags */
    nmo_save_options_t options = nmo_save_options_default();
    options.flags = flags;

    /* Determine compression settings from flags and file info */
    nmo_file_info_t file_info = nmo_session_get_file_info(session);
    const uint32_t compression_mask = NMO_FILE_WRITE_COMPRESS_HEADER | NMO_FILE_WRITE_COMPRESS_DATA;
    uint32_t compression_flags = file_info.write_mode & compression_mask;

    if (flags & NMO_SAVE_COMPRESSED) {
        compression_flags = NMO_FILE_WRITE_COMPRESS_BOTH;
    }

    options.compress_header = (compression_flags & NMO_FILE_WRITE_COMPRESS_HEADER) != 0;
    options.compress_data = (compression_flags & NMO_FILE_WRITE_COMPRESS_DATA) != 0;
    options.validate_before_write = (flags & NMO_SAVE_VALIDATE_BEFORE) != 0;

    /* Delegate to two-phase commit implementation */
    nmo_result_t result = nmo_save_file_ex(session, path, &options);

    return result.code;
}
