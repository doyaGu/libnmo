/**
 * @file parser.c
 * @brief Load and save pipeline implementation (Phase 9 & 10)
 */

#include "app/nmo_parser.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "app/nmo_finish_loading.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
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
#include "schema/nmo_schema_registry.h"
#include <stdlib.h>
#include <string.h>
#include "miniz.h"  /* For decompression */

/**
 * @brief Default placeholder serialization function.
 *
 * This function serves as a stand-in for a proper, schema-based serialization.
 * It creates a simple chunk containing only the object's name.
 *
 * @param obj The object to serialize.
 * @param arena The arena for allocations.
 * @param logger The logger for messages.
 * @return A new nmo_chunk_t* on success, or NULL on failure.
 */
static nmo_chunk_t* nmo_default_serialize_object(nmo_object_t *obj, nmo_arena_t *arena, nmo_logger_t *logger) {
    if (!obj || !arena) {
        return NULL;
    }

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    if (!writer) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to create chunk writer for object %u", obj->id);
        return NULL;
    }

    nmo_chunk_writer_start(writer, obj->class_id, 7); // Start with current chunk version

    // Write object name as a string
    if (obj->name) {
        nmo_chunk_writer_write_string(writer, obj->name);
    } else {
        nmo_chunk_writer_write_string(writer, "");
    }

    nmo_chunk_t* new_chunk = nmo_chunk_writer_finalize(writer);
    if (!new_chunk) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to finalize chunk for object %u", obj->id);
        return NULL;
    }

    // The writer is managed by the arena, no need to destroy it.
    return new_chunk;
}

static int nmo_should_save_object_as_reference(
    const nmo_object_t *obj,
    nmo_save_flags_t flags
) {
    if (obj == NULL) {
        return 0;
    }

    if (flags & NMO_SAVE_AS_OBJECTS) {
        return 1;
    }

    if (obj->save_flags & NMO_OBJECT_REFERENCE_FLAG) {
        return 1;
    }

    if (obj->flags & NMO_OBJECT_REFERENCE_FLAG) {
        return 1;
    }

    return 0;
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

        parsed++;
    }

    if (expected > 0 && parsed != expected) {
        nmo_log(logger, NMO_LOG_WARN,
                "  Header references %u included file(s), parsed %u entries", expected, parsed);
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
    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_chunk_pool_t *chunk_pool = NULL;
    nmo_logger_t *logger = nmo_context_get_logger(ctx);

    nmo_session_reset_reference_resolver(session);

    (void) flags; /* Flags will be used in full implementation */

    /* Phase 1: Open IO */
    nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file: %s", path);
    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to open file: %s", path);
        return NMO_ERR_FILE_NOT_FOUND;
    }

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
        .ck_version = header.ck_version,
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

    for (size_t i = 0; i < hdr1.plugin_dep_count; i++) {
        nmo_plugin_dep_t *dep = &hdr1.plugin_deps[i];
        nmo_log(logger, NMO_LOG_INFO, "  Plugin %zu: category=%u, version=%u",
                i, dep->category, dep->version);
        /* TODO: Actual plugin availability check */
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

            /* TODO: Dispatch manager chunk to appropriate manager for deserialization */
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
        for (size_t i = 0; i < hdr1.object_count; i++) {
            if (created_objects[i] != NULL && created_objects[i]->chunk != NULL) {
                nmo_chunk_remap_object_ids(created_objects[i]->chunk, remap_table);
            }
        }
        // Also remap manager chunks
        if (data_sect.managers != NULL) {
            for (uint32_t i = 0; i < data_sect.manager_count; i++) {
                if (data_sect.managers[i].chunk != NULL) {
                    nmo_chunk_remap_object_ids(data_sect.managers[i].chunk, remap_table);
                }
            }
        }
    }

    /* Phase 14: Deserialize Objects */
    nmo_log(logger, NMO_LOG_INFO, "Phase 14: Deserializing objects");

    nmo_schema_registry_t *schema_reg = nmo_context_get_schema_registry(ctx);
    size_t repo_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &repo_count);

    for (size_t i = 0; i < repo_count; i++) {
        nmo_object_t *obj = objects[i];
        const nmo_schema_descriptor_t *schema =
            nmo_schema_registry_find_by_id(schema_reg, obj->class_id);

        if (schema != NULL && schema->deserialize_fn != NULL && obj->chunk != NULL) {
            /* Deserialize object using schema */
            nmo_log(logger, NMO_LOG_INFO, "  Deserializing object %zu (class 0x%08X)",
                    i, obj->class_id);
            
            /* Call schema deserialize function with chunk parser
             * The chunk parser is the chunk itself for read operations
             */
            schema->deserialize_fn(obj, (nmo_chunk_parser_t *)obj->chunk);
        }
    }

skip_object_processing:
    /* Update repo_count after potential skip */
    nmo_object_repository_get_all(repo, &repo_count);
    /* Phase 15: Manager Post-Load Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 15: Executing manager post-load hooks");

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

    /* TODO: Phase 16: Read Included Files (if any) */
    /*
     * According to VIRTOOLS_FILE_FORMAT_SPEC.md Section 11:
     * Included files are appended after the data section and are NOT part of the CRC.
     * They follow this format:
     *   [4 bytes] Filename length (n)
     *   [n bytes] Filename string
     *   [4 bytes] File data size (m)
     *   [m bytes] File data
     *
     * Implementation plan:
     * 1. Read the "included_file_count" from Header1 (already parsed in Phase 4)
     * 2. For each included file:
     *    a. Read filename length and filename
     *    b. Read file data size and data
     *    c. Extract to temporary directory or store in session
     * 3. Store included file information in session for later access
     *
     * Current status: NOT IMPLEMENTED
     * Reason: This is a rarely-used feature in Virtools files. Most .nmo files
     *         do not contain included files. We're prioritizing core functionality.
     */
    if (hdr1.included_file_count > 0) {
        nmo_log(logger, NMO_LOG_WARN, "File contains %u included files, but feature is not yet implemented",
                hdr1.included_file_count);
        nmo_log(logger, NMO_LOG_WARN, "Included files will be skipped. File may not load completely.");
    }

    /* Cleanup */
    if (remap_table != NULL) {
        nmo_id_remap_table_destroy(remap_table);
    }
    nmo_load_session_end(load_session);
    nmo_load_session_destroy(load_session);
    nmo_io_close(io);

    nmo_log(logger, NMO_LOG_INFO, "Load complete: %zu objects loaded", repo_count);

    /* Phase 16: FinishLoading (Phase 5 integration) */
    nmo_log(logger, NMO_LOG_INFO, "Phase 16: Executing finish loading phase");
    
    /* Determine finish loading flags based on load flags */
    uint32_t finish_flags = 0;
    
    if (flags & NMO_LOAD_DEFAULT) {
        /* Default: enable all finish loading operations */
        finish_flags = NMO_FINISH_LOAD_DEFAULT;
    }
    
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
 * Save file - 14-phase save pipeline
 */
int nmo_save_file(nmo_session_t *session, const char *path, nmo_save_flags_t flags) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_logger_t *logger = nmo_context_get_logger(ctx);

    (void) flags; /* Flags will be used in full implementation */

    /* Phase 1: Validate Session State */
    nmo_log(logger, NMO_LOG_INFO, "Phase 1: Validating session state");

    size_t object_count;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);

    if (object_count == 0) {
        nmo_log(logger, NMO_LOG_ERROR, "Cannot save empty session");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_log(logger, NMO_LOG_INFO, "  Session has %zu objects to save", object_count);

    /* Determine which objects should be serialized as references */
    uint8_t *reference_map = NULL;
    size_t reference_count = 0;

    reference_map = (uint8_t *) nmo_arena_alloc(arena, object_count * sizeof(uint8_t), 1);
    if (reference_map == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate reference map");
        return NMO_ERR_NOMEM;
    }
    memset(reference_map, 0, object_count * sizeof(uint8_t));

    for (size_t i = 0; i < object_count; i++) {
        if (nmo_should_save_object_as_reference(objects[i], flags)) {
            reference_map[i] = 1;
            reference_count++;
        }
    }

    if (reference_count > 0) {
        nmo_log(logger, NMO_LOG_INFO, "  Objects marked as references: %zu", reference_count);
    }

    /* Phase 2: Manager Pre-Save Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 2: Executing manager pre-save hooks");

    nmo_manager_registry_t *manager_reg = nmo_context_get_manager_registry(ctx);
    if (manager_reg != NULL) {
        uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);
        nmo_log(logger, NMO_LOG_INFO, "  Found %u registered managers", manager_count);

        for (uint32_t i = 0; i < manager_count; i++) {
            uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
            nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_get(manager_reg, manager_id);

            if (manager != NULL) {
                int hook_result = nmo_manager_invoke_pre_save(manager, session);
                if (hook_result != NMO_OK) {
                    nmo_log(logger, NMO_LOG_WARN, "  Manager %u pre-save hook failed: %d",
                            manager_id, hook_result);
                    /* Continue with other managers even if one fails */
                } else {
                    nmo_log(logger, NMO_LOG_INFO, "  Manager %u pre-save hook executed", manager_id);
                }
            }
        }
    }

    /* Phase 3: Build ID Remap Plan (runtime → file IDs) */
    nmo_log(logger, NMO_LOG_INFO, "Phase 3: Building ID remap plan");

    nmo_id_remap_plan_t *remap_plan = nmo_id_remap_plan_create(repo, objects, object_count);
    if (remap_plan == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to create ID remap plan");
        return NMO_ERR_NOMEM;
    }

    nmo_id_remap_table_t *remap_table = nmo_id_remap_plan_get_table(remap_plan);
    size_t remap_count = nmo_id_remap_table_get_count(remap_table);
    nmo_log(logger, NMO_LOG_INFO, "  Created remap plan with %zu entries", remap_count);

    /* Phase 4: Serialize Manager Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 4: Serializing manager chunks");
    /* TODO: Serialize manager chunks when manager system is implemented */

    /* Phase 5: Serialize Object Chunks with ID Remapping */
    nmo_log(logger, NMO_LOG_INFO, "Phase 5: Serializing object chunks");

    nmo_schema_registry_t *schema_reg = nmo_context_get_schema_registry(ctx);
    (void)schema_reg; // Suppress unused variable warning for now

    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects[i];

        if (reference_map[i]) {
            nmo_log(logger, NMO_LOG_INFO,
                    "  Skipping serialization for object %zu (reference placeholder)", i);
            continue;
        }

        // In a full implementation, we would find the correct schema here.
        // const nmo_schema_descriptor_t *schema =
        //     nmo_schema_registry_find_by_id(schema_reg, obj->class_id);

        // For now, we use a default serializer.
        // If a chunk already exists (e.g. from a loaded file), we could reuse it.
        // For now, we always re-serialize.
        nmo_log(logger, NMO_LOG_INFO, "  Serializing object %zu (class 0x%08X)", i, obj->class_id);
        obj->chunk = nmo_default_serialize_object(obj, arena, logger);
        if (obj->chunk == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to serialize object %u ('%s')", obj->id, obj->name ? obj->name : "");
            // Decide on error handling: continue or abort? For now, continue.
        }
    }

    /* Phase 6: Build and Compress Data Section */
    nmo_log(logger, NMO_LOG_INFO, "Phase 6: Building data section");

    /* Get manager data from session */
    uint32_t session_manager_count = 0;
    nmo_manager_data_t *session_managers = nmo_session_get_manager_data(session, &session_manager_count);

    /* Build data section structure from objects */
    nmo_data_section_t data_sect;
    memset(&data_sect, 0, sizeof(nmo_data_section_t));
    data_sect.manager_count = session_manager_count;
    data_sect.managers = session_managers;
    data_sect.object_count = (uint32_t) object_count;

    /* Allocate object data array */
    data_sect.objects = (nmo_object_data_t *) nmo_arena_alloc(arena,
                                                            sizeof(nmo_object_data_t) * object_count, sizeof(void *));
    if (data_sect.objects == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate object data array");
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_NOMEM;
    }

    /* Copy chunk pointers */
    for (size_t i = 0; i < object_count; i++) {
        if (reference_map[i]) {
            data_sect.objects[i].chunk = NULL;
            data_sect.objects[i].size = 0;
        } else {
            data_sect.objects[i].chunk = objects[i]->chunk;
        }
    }

    /* Calculate data section size */
    size_t data_unpack_size = nmo_data_section_calculate_size(&data_sect, 8, arena);
    nmo_log(logger, NMO_LOG_INFO, "  Data section unpack size: %zu bytes", data_unpack_size);

    /* Allocate buffer for uncompressed data */
    void *data_buffer = nmo_arena_alloc(arena, data_unpack_size, 16);
    if (data_buffer == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate data buffer");
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_NOMEM;
    }

    /* Serialize data section */
    size_t data_bytes_written = 0;
    nmo_result_t result = nmo_data_section_serialize(&data_sect, 8, data_buffer,
                                                     data_unpack_size, &data_bytes_written, arena);
    if (result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to serialize data section");
        nmo_id_remap_plan_destroy(remap_plan);
        return result.code;
    }

    nmo_log(logger, NMO_LOG_INFO, "  Data section serialized: %zu bytes", data_bytes_written);

    /* Compress data section */
    void *data_packed = data_buffer;
    uint32_t data_pack_size = (uint32_t) data_bytes_written;

    /* TODO: Add compression support when file_write_mode has compression flag */
    nmo_log(logger, NMO_LOG_INFO, "  Data section pack size: %u bytes (uncompressed)", data_pack_size);

    /* Phase 7: Build Object Descriptors for Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 7: Building object descriptors");

    nmo_object_desc_t *obj_descs = (nmo_object_desc_t *) nmo_arena_alloc(
        arena, sizeof(nmo_object_desc_t) * object_count, sizeof(void *));

    if (obj_descs == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate object descriptors");
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects[i];

        /* Get file ID from remap plan */
        nmo_object_id_t file_id = 0;
        int lookup_result = nmo_id_remap_lookup(remap_table, obj->id, &file_id);

        if (lookup_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to lookup file ID for object %u", obj->id);
            nmo_id_remap_plan_destroy(remap_plan);
            return lookup_result;
        }

        obj_descs[i].file_id = file_id;
        obj_descs[i].class_id = obj->class_id;
        obj_descs[i].name = (char *) obj->name; /* Cast away const */
        obj_descs[i].file_index = i;            /* TODO: Calculate from hierarchy */

        uint32_t descriptor_flags = obj->flags;
        if (reference_map[i]) {
            descriptor_flags |= NMO_OBJECT_REFERENCE_FLAG;
        }
        obj_descs[i].flags = descriptor_flags;

        nmo_log(logger, NMO_LOG_INFO, "  Object %zu: runtime_id=%u → file_id=%u, class=0x%08X",
                i, obj->id, file_id, obj->class_id);
    }

    /* Phase 8: Build Plugin Dependencies List */
    nmo_log(logger, NMO_LOG_INFO, "Phase 8: Building plugin dependencies");
    /* TODO: Build plugin dependency list based on object classes */
    /* For now, use empty plugin list */
    size_t plugin_count = 0;
    nmo_plugin_dep_t *plugin_deps = NULL;

    /* Phase 9: Build and Serialize Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 9: Building header1");

    /* Build Header1 structure */
    nmo_header1_t hdr1;
    memset(&hdr1, 0, sizeof(nmo_header1_t));
    hdr1.object_count = (uint32_t) object_count;
    hdr1.objects = obj_descs;
    hdr1.plugin_dep_count = plugin_count;
    hdr1.plugin_deps = plugin_deps;

    hdr1.included_file_count = 0;
    hdr1.included_files = NULL;
    if (session != NULL) {
        uint32_t included_file_count = 0;
        nmo_included_file_t *included_files = nmo_session_get_included_files(session, &included_file_count);
        hdr1.included_file_count = included_file_count;
        if (included_file_count > 0 && included_files != NULL) {
            hdr1.included_files = (nmo_included_file_desc_t *) nmo_arena_alloc(
                arena,
                sizeof(nmo_included_file_desc_t) * included_file_count,
                sizeof(void *)
            );
            if (hdr1.included_files == NULL) {
                nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate included file descriptors");
                nmo_id_remap_plan_destroy(remap_plan);
                return NMO_ERR_NOMEM;
            }

            for (uint32_t i = 0; i < included_file_count; i++) {
                hdr1.included_files[i].name = (char *) included_files[i].name;
                hdr1.included_files[i].data_size = included_files[i].size;
            }
        }
    }

    /* Serialize Header1 */
    void *hdr1_buffer = NULL;
    size_t hdr1_unpack_size = 0;
    result = nmo_header1_serialize(&hdr1, &hdr1_buffer, &hdr1_unpack_size, arena);
    if (result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to serialize header1");
        nmo_id_remap_plan_destroy(remap_plan);
        return result.code;
    }

    nmo_log(logger, NMO_LOG_INFO, "  Header1 serialized: %zu bytes", hdr1_unpack_size);

    /* For now, no compression */
    void *hdr1_packed = hdr1_buffer;
    uint32_t hdr1_pack_size = (uint32_t) hdr1_unpack_size;

    nmo_log(logger, NMO_LOG_INFO, "  Header1 pack size: %u bytes (uncompressed)", hdr1_pack_size);

    /* Phase 10: Calculate File Sizes */
    nmo_log(logger, NMO_LOG_INFO, "Phase 10: Calculating file sizes");

    uint32_t file_size = sizeof(nmo_file_header_t) + hdr1_pack_size + data_pack_size;
    nmo_log(logger, NMO_LOG_INFO, "  Total file size: %u bytes", file_size);

    /* Phase 11: Build File Header */
    nmo_log(logger, NMO_LOG_INFO, "Phase 11: Building file header");

    nmo_file_info_t file_info = nmo_session_get_file_info(session);

    nmo_file_header_t header;
    memset(&header, 0, sizeof(nmo_file_header_t));

    /* Set signature */
    memcpy(header.signature, "Nemo Fi\0", 8);

    /* Set versions */
    header.file_version = (file_info.file_version != 0) ? file_info.file_version : 8;
    header.ck_version = (file_info.ck_version != 0) ? file_info.ck_version : 0x13022002;

    /* Set counts */
    header.object_count = (uint32_t) object_count;
    header.manager_count = session_manager_count;

    /* Set sizes */
    header.hdr1_pack_size = hdr1_pack_size;
    header.hdr1_unpack_size = hdr1_unpack_size;
    header.data_pack_size = data_pack_size;
    header.data_unpack_size = data_unpack_size;

    /* Set max ID */
    nmo_object_id_t max_file_id = 0;
    for (size_t i = 0; i < object_count; i++) {
        if (obj_descs[i].file_id > max_file_id) {
            max_file_id = obj_descs[i].file_id;
        }
    }
    header.max_id_saved = max_file_id;

    /* Set write mode */
    header.file_write_mode = file_info.write_mode;

    /* Calculate CRC (adler32) over all sections */
    uint32_t crc = 0;
    crc = mz_adler32(crc, (const uint8_t *) &header, 32);              /* Part0 size (up to hdr1_pack_size) */
    crc = mz_adler32(crc, (const uint8_t *) &header.object_count, 56); /* Part1 size (from object_count) */
    crc = mz_adler32(crc, (const uint8_t *) hdr1_packed, hdr1_pack_size);
    crc = mz_adler32(crc, (const uint8_t *) data_packed, data_pack_size);
    header.crc = crc;

    nmo_log(logger, NMO_LOG_INFO, "  File version: %u, CK version: 0x%08X",
            header.file_version, header.ck_version);
    nmo_log(logger, NMO_LOG_INFO, "  Objects: %u, Managers: %u, Max ID: %u",
            header.object_count, header.manager_count, header.max_id_saved);
    nmo_log(logger, NMO_LOG_INFO, "  CRC: 0x%08X", crc);

    /* Phase 12: Open Output IO */
    nmo_log(logger, NMO_LOG_INFO, "Phase 12: Opening output file: %s", path);

    nmo_io_interface_t *io = nmo_file_io_open(path, NMO_IO_WRITE | NMO_IO_CREATE);
    if (io == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to open output file: %s", path);
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    /* Phase 13: Write File Header, Header1, Data Section */
    nmo_log(logger, NMO_LOG_INFO, "Phase 13: Writing file data");

    /* Write file header using proper serialization */
    nmo_log(logger, NMO_LOG_INFO, "  Writing file header (%zu bytes)", sizeof(nmo_file_header_t));
    nmo_result_t header_result = nmo_file_header_serialize(&header, io);

    if (header_result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to write file header");
        nmo_io_close(io);
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Write header1 */
    if (hdr1_pack_size > 0) {
        nmo_log(logger, NMO_LOG_INFO, "  Writing header1 (%u bytes)", hdr1_pack_size);
        int hdr1_write_result = nmo_io_write(io, hdr1_packed, hdr1_pack_size);
        if (hdr1_write_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to write header1 data");
            nmo_io_close(io);
            nmo_id_remap_plan_destroy(remap_plan);
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_log(logger, NMO_LOG_INFO, "  Header1 written successfully");
    }

    /* Write data section */
    if (data_pack_size > 0) {
        nmo_log(logger, NMO_LOG_INFO, "  Writing data section (%u bytes)", data_pack_size);
        int data_write_result = nmo_io_write(io, data_packed, data_pack_size);
        if (data_write_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to write data section");
            nmo_io_close(io);
            nmo_id_remap_plan_destroy(remap_plan);
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_log(logger, NMO_LOG_INFO, "  Data section written successfully");
    }

    /* Phase 14: Manager Post-Save Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 14: Executing manager post-save hooks");

    if (manager_reg != NULL) {
        uint32_t manager_count = nmo_manager_registry_get_count(manager_reg);

        for (uint32_t i = 0; i < manager_count; i++) {
            uint32_t manager_id = nmo_manager_registry_get_id_at(manager_reg, i);
            nmo_manager_t *manager = (nmo_manager_t *) nmo_manager_registry_get(manager_reg, manager_id);

            if (manager != NULL) {
                int hook_result = nmo_manager_invoke_post_save(manager, session);
                if (hook_result != NMO_OK) {
                    nmo_log(logger, NMO_LOG_WARN, "  Manager %u post-save hook failed: %d",
                            manager_id, hook_result);
                } else {
                    nmo_log(logger, NMO_LOG_INFO, "  Manager %u post-save hook executed", manager_id);
                }
            }
        }
    }

    if (hdr1.included_file_count > 0) {
        nmo_log(logger, NMO_LOG_INFO, "Phase 15: Writing %u included file(s)", hdr1.included_file_count);
        uint32_t included_count = 0;
        nmo_included_file_t *included_files = nmo_session_get_included_files(session, &included_count);
        if (included_files == NULL || included_count == 0) {
            nmo_log(logger, NMO_LOG_WARN, "Header declares included files, but session has none");
        } else {
            for (uint32_t i = 0; i < included_count; i++) {
                const char *name = included_files[i].name ? included_files[i].name : "";
                uint32_t name_len = (uint32_t) strlen(name);

                if (nmo_io_write_u32(io, name_len) != NMO_OK ||
                    (name_len > 0 && nmo_io_write(io, name, name_len) != NMO_OK) ||
                    nmo_io_write_u32(io, included_files[i].size) != NMO_OK) {
                    nmo_log(logger, NMO_LOG_ERROR, "Failed to write metadata for included file '%s'", name);
                    nmo_io_close(io);
                    nmo_id_remap_plan_destroy(remap_plan);
                    return NMO_ERR_IO;
                }

                if (included_files[i].size > 0 && included_files[i].data != NULL) {
                    if (nmo_io_write(io, included_files[i].data, included_files[i].size) != NMO_OK) {
                        nmo_log(logger, NMO_LOG_ERROR,
                                "Failed to write included payload for '%s'", name);
                        nmo_io_close(io);
                        nmo_id_remap_plan_destroy(remap_plan);
                        return NMO_ERR_IO;
                    }
                }
            }
        }
    }

    /* Cleanup */
    nmo_io_close(io);
    nmo_id_remap_plan_destroy(remap_plan);

    nmo_log(logger, NMO_LOG_INFO, "Save complete: %zu objects saved to %s",
            object_count, path);

    (void) plugin_deps;  /* Suppress unused warning */
    (void) plugin_count; /* Suppress unused warning */

    return NMO_OK;
}
