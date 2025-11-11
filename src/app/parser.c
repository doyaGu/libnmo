/**
 * @file parser.c
 * @brief Load pipeline implementation (Phase 9)
 */

#include "app/nmo_parser.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "io/nmo_io_compressed.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_chunk.h"
#include "format/nmo_object.h"
#include "session/nmo_load_session.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_object_repository.h"
#include "schema/nmo_schema_registry.h"
#include <stdlib.h>
#include <string.h>

/**
 * Load file - 15-phase load pipeline
 */
int nmo_load_file(nmo_session* session, const char* path, nmo_load_flags flags) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context* ctx = nmo_session_get_context(session);
    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);
    nmo_logger* logger = nmo_context_get_logger(ctx);

    (void)flags;  /* Flags will be used in full implementation */

    /* Phase 1: Open IO */
    nmo_log(logger, NMO_LOG_INFO, "Phase 1: Opening file: %s", path);
    nmo_io_interface* io = nmo_file_io_open(path, NMO_IO_READ);
    if (io == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to open file: %s", path);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    /* Phase 2: Parse File Header */
    nmo_log(logger, NMO_LOG_INFO, "Phase 2: Parsing file header");
    nmo_file_header header;
    nmo_result_t result = nmo_header_parse((nmo_header_t*)&header, io);
    if (result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to parse file header");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    result = nmo_header_validate((nmo_header_t*)&header);
    if (result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Invalid file header");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Set file info in session */
    nmo_file_info file_info = {
        .file_version = header.file_version,
        .ck_version = header.ck_version,
        .file_size = 0,  /* Will calculate from headers */
        .object_count = header.object_count,
        .manager_count = header.manager_count,
        .write_mode = header.file_write_mode
    };
    nmo_session_set_file_info(session, &file_info);

    /* Phase 3: Read and Decompress Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 3: Reading header1 (size: %u bytes)",
            header.hdr1_pack_size);

    void* hdr1_data = nmo_arena_alloc(arena, header.hdr1_unpack_size, 16);
    if (hdr1_data == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate header1 buffer");
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }

    /* Read header1 data (decompression handled if needed) */
    size_t bytes_read = 0;
    int read_result = nmo_io_read(io, hdr1_data, header.hdr1_pack_size, &bytes_read);
    if (read_result != NMO_OK || bytes_read != header.hdr1_pack_size) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to read header1 data");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Phase 4: Parse Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 4: Parsing header1");
    nmo_header1 hdr1;
    result = nmo_header1_parse(hdr1_data, header.hdr1_unpack_size, &hdr1, arena);
    if (result.code != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to parse header1");
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_log(logger, NMO_LOG_INFO, "Found %u objects, %u managers, %zu plugins",
            hdr1.object_count, header.manager_count, hdr1.plugin_dep_count);

    /* Phase 5: Start Load Session */
    nmo_log(logger, NMO_LOG_INFO, "Phase 5: Starting load session (max ID: %u)",
            header.max_id_saved);

    nmo_load_session* load_session = nmo_load_session_start(repo, header.max_id_saved);
    if (load_session == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to start load session");
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }

    /* Phase 6: Check Plugin Dependencies */
    nmo_log(logger, NMO_LOG_INFO, "Phase 6: Checking plugin dependencies (%zu plugins)",
            hdr1.plugin_dep_count);

    for (size_t i = 0; i < hdr1.plugin_dep_count; i++) {
        nmo_plugin_dep* dep = &hdr1.plugin_deps[i];
        nmo_log(logger, NMO_LOG_INFO, "  Plugin %zu: category=%u, version=%u",
                i, dep->category, dep->version);
        /* TODO: Actual plugin availability check */
    }

    /* Phase 7: Manager Pre-Load Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 7: Executing manager pre-load hooks");
    /* TODO: Execute manager hooks when manager system is implemented */

    /* Phase 8: Read and Decompress Data Section */
    nmo_log(logger, NMO_LOG_INFO, "Phase 8: Reading data section (size: %u bytes)",
            header.data_pack_size);

    void* data_section = nmo_arena_alloc(arena, header.data_unpack_size, 16);
    if (data_section == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate data section buffer");
        nmo_load_session_destroy(load_session);
        nmo_io_close(io);
        return NMO_ERR_NOMEM;
    }

    read_result = nmo_io_read(io, data_section, header.data_pack_size, &bytes_read);
    if (read_result != NMO_OK || bytes_read != header.data_pack_size) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to read data section");
        nmo_load_session_destroy(load_session);
        nmo_io_close(io);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Phase 9: Parse Manager Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 9: Parsing manager chunks");
    /* TODO: Parse and process manager chunks */

    /* Phase 10: Create Objects */
    nmo_log(logger, NMO_LOG_INFO, "Phase 10: Creating %u objects", hdr1.object_count);

    for (size_t i = 0; i < hdr1.object_count; i++) {
        nmo_object_desc* desc = &hdr1.objects[i];

        /* Skip reference-only objects */
        if (desc->file_id & NMO_OBJECT_REFERENCE_FLAG) {
            nmo_log(logger, NMO_LOG_INFO, "  Object %zu: reference-only, skipping", i);
            continue;
        }

        /* Create object */
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                         sizeof(void*));
        if (obj == NULL) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate object");
            nmo_load_session_destroy(load_session);
            nmo_io_close(io);
            return NMO_ERR_NOMEM;
        }

        memset(obj, 0, sizeof(nmo_object));
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

        nmo_log(logger, NMO_LOG_INFO, "  Created object %zu: file_id=%u, runtime_id=%u, class=0x%08X, name='%s'",
                i, desc->file_id, obj->id, obj->class_id, obj->name ? obj->name : "(null)");
    }

    /* Phase 11: Parse Object Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 11: Parsing object chunks");
    /* TODO: Parse chunks for each object */

    /* Phase 12: Build ID Remap Table */
    nmo_log(logger, NMO_LOG_INFO, "Phase 12: Building ID remap table");

    nmo_id_remap_table* remap_table = nmo_build_remap_table(load_session);
    if (remap_table == NULL) {
        nmo_log(logger, NMO_LOG_WARN, "Failed to build ID remap table (may be empty session)");
    } else {
        size_t remap_count = nmo_id_remap_table_get_count(remap_table);
        nmo_log(logger, NMO_LOG_INFO, "  Built remap table with %zu entries", remap_count);
    }

    /* Phase 13: Remap IDs in All Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 13: Remapping IDs in chunks");
    /* TODO: Apply ID remapping to all loaded chunks */

    /* Phase 14: Deserialize Objects */
    nmo_log(logger, NMO_LOG_INFO, "Phase 14: Deserializing objects");

    nmo_schema_registry* schema_reg = nmo_context_get_schema_registry(ctx);
    size_t repo_count;
    nmo_object** objects = nmo_object_repository_get_all(repo, &repo_count);

    for (size_t i = 0; i < repo_count; i++) {
        nmo_object* obj = objects[i];
        const nmo_schema_descriptor* schema =
            nmo_schema_registry_find_by_id(schema_reg, obj->class_id);

        if (schema != NULL && schema->deserialize_fn != NULL && obj->chunk != NULL) {
            /* TODO: Deserialize object using schema */
            nmo_log(logger, NMO_LOG_INFO, "  Deserializing object %zu (class 0x%08X)",
                    i, obj->class_id);
        }
    }

    /* Phase 15: Manager Post-Load Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 15: Executing manager post-load hooks");
    /* TODO: Execute manager hooks when manager system is implemented */

    /* Cleanup */
    if (remap_table != NULL) {
        nmo_id_remap_table_destroy(remap_table);
    }
    nmo_load_session_end(load_session);
    nmo_load_session_destroy(load_session);
    nmo_io_close(io);

    nmo_log(logger, NMO_LOG_INFO, "Load complete: %zu objects loaded", repo_count);

    return NMO_OK;
}

/**
 * Save file - 14-phase save pipeline
 */
int nmo_save_file(nmo_session* session, const char* path, nmo_save_flags flags) {
    if (session == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context* ctx = nmo_session_get_context(session);
    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);
    nmo_logger* logger = nmo_context_get_logger(ctx);

    (void)flags;  /* Flags will be used in full implementation */

    /* Phase 1: Validate Session State */
    nmo_log(logger, NMO_LOG_INFO, "Phase 1: Validating session state");

    size_t object_count;
    nmo_object** objects = nmo_object_repository_get_all(repo, &object_count);

    if (object_count == 0) {
        nmo_log(logger, NMO_LOG_ERROR, "Cannot save empty session");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_log(logger, NMO_LOG_INFO, "  Session has %zu objects to save", object_count);

    /* Phase 2: Manager Pre-Save Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 2: Executing manager pre-save hooks");
    /* TODO: Execute manager hooks when manager system is implemented */

    /* Phase 3: Build ID Remap Plan (runtime → file IDs) */
    nmo_log(logger, NMO_LOG_INFO, "Phase 3: Building ID remap plan");

    nmo_id_remap_plan* remap_plan = nmo_id_remap_plan_create(repo, objects, object_count);
    if (remap_plan == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to create ID remap plan");
        return NMO_ERR_NOMEM;
    }

    nmo_id_remap_table* remap_table = nmo_id_remap_plan_get_table(remap_plan);
    size_t remap_count = nmo_id_remap_table_get_count(remap_table);
    nmo_log(logger, NMO_LOG_INFO, "  Created remap plan with %zu entries", remap_count);

    /* Phase 4: Serialize Manager Chunks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 4: Serializing manager chunks");
    /* TODO: Serialize manager chunks when manager system is implemented */

    /* Phase 5: Serialize Object Chunks with ID Remapping */
    nmo_log(logger, NMO_LOG_INFO, "Phase 5: Serializing object chunks");

    nmo_schema_registry* schema_reg = nmo_context_get_schema_registry(ctx);

    for (size_t i = 0; i < object_count; i++) {
        nmo_object* obj = objects[i];
        const nmo_schema_descriptor* schema =
            nmo_schema_registry_find_by_id(schema_reg, obj->class_id);

        if (schema != NULL && schema->serialize_fn != NULL) {
            /* TODO: Serialize object using schema and apply ID remapping */
            nmo_log(logger, NMO_LOG_INFO, "  Serializing object %zu (class 0x%08X)",
                    i, obj->class_id);
        }
    }

    /* Phase 6: Compress Data Section */
    nmo_log(logger, NMO_LOG_INFO, "Phase 6: Compressing data section");
    /* TODO: Compress serialized data section */

    /* Placeholder sizes for now */
    uint32_t data_unpack_size = 0;  /* TODO: Calculate from serialized chunks */
    uint32_t data_pack_size = 0;    /* TODO: Calculate after compression */

    /* Phase 7: Build Object Descriptors for Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 7: Building object descriptors");

    nmo_object_desc* obj_descs = (nmo_object_desc*)nmo_arena_alloc(
        arena, sizeof(nmo_object_desc) * object_count, sizeof(void*));

    if (obj_descs == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to allocate object descriptors");
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_NOMEM;
    }

    for (size_t i = 0; i < object_count; i++) {
        nmo_object* obj = objects[i];

        /* Get file ID from remap plan */
        nmo_object_id file_id = 0;
        int lookup_result = nmo_id_remap_lookup(remap_table, obj->id, &file_id);

        if (lookup_result != NMO_OK) {
            nmo_log(logger, NMO_LOG_ERROR, "Failed to lookup file ID for object %u", obj->id);
            nmo_id_remap_plan_destroy(remap_plan);
            return lookup_result;
        }

        obj_descs[i].file_id = file_id;
        obj_descs[i].class_id = obj->class_id;
        obj_descs[i].name = (char*)obj->name;  /* Cast away const */
        obj_descs[i].parent_id = 0;  /* TODO: Calculate from hierarchy */
        obj_descs[i].flags = obj->flags;

        nmo_log(logger, NMO_LOG_INFO, "  Object %zu: runtime_id=%u → file_id=%u, class=0x%08X",
                i, obj->id, file_id, obj->class_id);
    }

    /* Phase 8: Build Plugin Dependencies List */
    nmo_log(logger, NMO_LOG_INFO, "Phase 8: Building plugin dependencies");
    /* TODO: Build plugin dependency list based on object classes */

    size_t plugin_count = 0;
    nmo_plugin_dep* plugin_deps = NULL;

    /* Phase 9: Compress Header1 */
    nmo_log(logger, NMO_LOG_INFO, "Phase 9: Building and compressing header1");

    /* TODO: Build actual header1 structure */
    uint32_t hdr1_unpack_size = 0;  /* TODO: Calculate from header1 contents */
    uint32_t hdr1_pack_size = 0;    /* TODO: Calculate after compression */

    /* Phase 10: Calculate File Sizes */
    nmo_log(logger, NMO_LOG_INFO, "Phase 10: Calculating file sizes");

    uint32_t file_size = sizeof(nmo_file_header) + hdr1_pack_size + data_pack_size;
    nmo_log(logger, NMO_LOG_INFO, "  Total file size: %u bytes", file_size);

    /* Phase 11: Build File Header */
    nmo_log(logger, NMO_LOG_INFO, "Phase 11: Building file header");

    nmo_file_info file_info = nmo_session_get_file_info(session);

    nmo_file_header header;
    memset(&header, 0, sizeof(nmo_file_header));

    /* Set signature */
    memcpy(header.signature, "Nemo Fi\0", 8);

    /* Set versions */
    header.file_version = (file_info.file_version != 0) ? file_info.file_version : 8;
    header.ck_version = (file_info.ck_version != 0) ? file_info.ck_version : 0x13022002;

    /* Set counts */
    header.object_count = (uint32_t)object_count;
    header.manager_count = file_info.manager_count;

    /* Set sizes */
    header.hdr1_pack_size = hdr1_pack_size;
    header.hdr1_unpack_size = hdr1_unpack_size;
    header.data_pack_size = data_pack_size;
    header.data_unpack_size = data_unpack_size;

    /* Set max ID */
    nmo_object_id max_file_id = 0;
    for (size_t i = 0; i < object_count; i++) {
        if (obj_descs[i].file_id > max_file_id) {
            max_file_id = obj_descs[i].file_id;
        }
    }
    header.max_id_saved = max_file_id;

    /* Set write mode */
    header.file_write_mode = file_info.write_mode;

    nmo_log(logger, NMO_LOG_INFO, "  File version: %u, CK version: 0x%08X",
            header.file_version, header.ck_version);
    nmo_log(logger, NMO_LOG_INFO, "  Objects: %u, Managers: %u, Max ID: %u",
            header.object_count, header.manager_count, header.max_id_saved);

    /* Phase 12: Open Output IO */
    nmo_log(logger, NMO_LOG_INFO, "Phase 12: Opening output file: %s", path);

    nmo_io_interface* io = nmo_file_io_open(path, NMO_IO_WRITE);
    if (io == NULL) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to open output file: %s", path);
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_FILE_NOT_FOUND;
    }

    /* Phase 13: Write File Header, Header1, Data Section */
    nmo_log(logger, NMO_LOG_INFO, "Phase 13: Writing file data");

    /* Write file header */
    nmo_log(logger, NMO_LOG_INFO, "  Writing file header (%zu bytes)", sizeof(nmo_file_header));
    int write_result = nmo_io_write(io, &header, sizeof(nmo_file_header));

    if (write_result != NMO_OK) {
        nmo_log(logger, NMO_LOG_ERROR, "Failed to write file header");
        nmo_io_close(io);
        nmo_id_remap_plan_destroy(remap_plan);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Write header1 (TODO: actual header1 data) */
    if (hdr1_pack_size > 0) {
        nmo_log(logger, NMO_LOG_INFO, "  Writing header1 (%u bytes)", hdr1_pack_size);
        /* TODO: Write compressed header1 data */
    }

    /* Write data section (TODO: actual data) */
    if (data_pack_size > 0) {
        nmo_log(logger, NMO_LOG_INFO, "  Writing data section (%u bytes)", data_pack_size);
        /* TODO: Write compressed data section */
    }

    /* Phase 14: Manager Post-Save Hooks */
    nmo_log(logger, NMO_LOG_INFO, "Phase 14: Executing manager post-save hooks");
    /* TODO: Execute manager hooks when manager system is implemented */

    /* Cleanup */
    nmo_io_close(io);
    nmo_id_remap_plan_destroy(remap_plan);

    nmo_log(logger, NMO_LOG_INFO, "Save complete: %zu objects saved to %s",
            object_count, path);

    (void)plugin_deps;  /* Suppress unused warning */
    (void)plugin_count; /* Suppress unused warning */

    return NMO_OK;
}
