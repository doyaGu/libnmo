/**
 * @file nmo_data.c
 * @brief NMO Data section parsing implementation
 */

#include "format/nmo_data.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_pool.h"
#include "core/nmo_utils.h"
#include "miniz.h"
#include <string.h>
#include <stdalign.h>

/* Helper macros */
#define CHECK_BUFFER_SIZE(arena, pos, needed, total) \
    do { \
        if (!nmo_check_buffer_bounds((pos), (needed), (total))) { \
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR, \
                "Data section buffer overrun: pos=%zu needed=%zu total=%zu", \
                (size_t)(pos), (size_t)(needed), (size_t)(total)); \
        } \
    } while (0)

/**
 * @brief Whether a data section of this file version carries the manager block
 *
 * CKFile::ReadFileData reads the managers only from file version 6 on. In
 * older files the bytes are not a manager block, and a writer that put one in
 * would make the engine read the first manager as an object.
 */
static bool data_section_has_manager_block(uint32_t file_version) {
    return file_version >= 6u;
}

/**
 * @brief Whether a data section of this file version starts with its own counts
 *
 * CKFile::ReadFileData reads SaveIDMax and ObjectCount from the first eight
 * bytes of the section for every file version below 8. From version 8 on the
 * header holds them.
 */
static bool data_section_has_leading_counts(uint32_t file_version) {
    return file_version < 8u;
}

/**
 * @brief Bytes the leading counts take in a data section of this file version
 */
static size_t data_section_leading_bytes(uint32_t file_version) {
    return data_section_has_leading_counts(file_version) ? 8u : 0u;
}

/**
 * @brief Parse manager data from buffer
 *
 * Manager data format (for file_version >= 6):
 *   For each manager:
 *     - CKGUID (8 bytes: d1, d2)
 *     - data_size (4 bytes int32)
 *     - chunk_data (data_size bytes)
 */
static nmo_chunk_t *allocate_chunk(nmo_chunk_pool_t *chunk_pool, nmo_arena_t *arena) {
    if (chunk_pool != NULL) {
        nmo_chunk_t *chunk = nmo_chunk_pool_acquire(chunk_pool);
        if (chunk != NULL) {
            return chunk;
        }
    }
    return nmo_chunk_create(arena);
}

static nmo_status_t parse_manager_data(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    nmo_data_section_t *section,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    if (section->manager_count == 0) {
        section->managers = NULL;
        NMO_RETURN_OK();
    }

    /* Allocate manager data array */
    size_t manager_bytes = 0;
    if (!nmo_safe_mul_size(sizeof(nmo_manager_data_t), section->manager_count, &manager_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Manager data allocation overflow");
    }
    section->managers = (nmo_manager_data_t *) nmo_arena_alloc(
        arena,
        manager_bytes,
        alignof(nmo_manager_data_t));
    if (section->managers == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate manager data array");
    }
    memset(section->managers, 0, manager_bytes);

    /* Parse each manager */
    for (uint32_t i = 0; i < section->manager_count; i++) {
        nmo_manager_data_t *mgr = &section->managers[i];

        /* Read CKGUID (8 bytes: d1, d2) */
        CHECK_BUFFER_SIZE(arena, *pos, 8, size);
        mgr->guid.d1 = nmo_read_u32_le(data + *pos);
        *pos += 4;
        mgr->guid.d2 = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Read data size */
        CHECK_BUFFER_SIZE(arena, *pos, 4, size);
        mgr->data_size = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Parse chunk data if present */
        if (mgr->data_size > 0) {
            CHECK_BUFFER_SIZE(arena, *pos, mgr->data_size, size);

            /* Create chunk */
            mgr->chunk = allocate_chunk(chunk_pool, arena);
            if (mgr->chunk == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Failed to create manager chunk (index=%u, size=%u)",
                                        (unsigned)i, (unsigned)mgr->data_size);
            }

            /* Parse chunk from buffer */
            NMO_RETURN_IF_ERROR_CTX(nmo_chunk_parse(mgr->chunk, data + *pos, mgr->data_size),
                                    "Failed to parse manager chunk (index=%u, size=%u)",
                                    (unsigned)i,
                                    (unsigned)mgr->data_size);

            mgr->chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
            mgr->chunk->uncompressed_size = mgr->data_size;

            *pos += mgr->data_size;
        } else {
            mgr->chunk = NULL;
        }

        mgr->flags = 0;
    }

    NMO_RETURN_OK();
}

/**
 * @brief Parse an object entry of a file below version 4
 *
 * CKFile::ReadFileData reads these entries itself instead of through
 * CKStateChunk::ConvertFromBuffer: after the object id and the size comes,
 * when the size is not zero, the class id, the save flags, the stored size and
 * the stored bytes. The chunk is built as CKStateChunk(class id, NULL) builds
 * it, with data and chunk version 0, so its reads take the layout of chunks
 * older than version 4. With NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD and a stored
 * size that differs from the size, the bytes are zlib packed; a chunk that
 * does not unpack is dropped, as the engine drops it.
 */
static nmo_status_t parse_object_entry_v2(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    uint32_t file_write_mode,
    uint32_t index,
    nmo_object_data_t *obj,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    CHECK_BUFFER_SIZE(arena, *pos, 8, size);
    obj->object_id = nmo_read_u32_le(data + *pos);
    obj->data_size = nmo_read_u32_le(data + *pos + 4);
    *pos += 8;
    obj->chunk = NULL;
    if ((int32_t)obj->data_size <= 0) {
        obj->data_size = 0;
        NMO_RETURN_OK();
    }

    CHECK_BUFFER_SIZE(arena, *pos, 12, size);
    const uint32_t class_id = nmo_read_u32_le(data + *pos);
    const uint32_t stored_size = nmo_read_u32_le(data + *pos + 8);
    *pos += 12;
    if (class_id == 0) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR,
                         "Object entry %u has no class id", (unsigned)index);
    }
    CHECK_BUFFER_SIZE(arena, *pos, stored_size, size);
    const uint8_t *stored = data + *pos;
    *pos += stored_size;

    const bool packed = stored_size != obj->data_size &&
                        (file_write_mode & NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD) != 0;
    const uint8_t *bytes = stored;
    size_t byte_count = stored_size;
    uint8_t *unpacked = NULL;
    if (packed) {
        unpacked = (uint8_t *)nmo_arena_alloc(arena, obj->data_size, alignof(uint32_t));
        if (unpacked == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate object entry %u", (unsigned)index);
        }
        mz_ulong unpacked_size = obj->data_size;
        if (mz_uncompress(unpacked, &unpacked_size, stored, stored_size) != MZ_OK) {
            obj->data_size = 0;
            NMO_RETURN_OK();
        }
        bytes = unpacked;
        byte_count = (size_t)unpacked_size;
    }

    nmo_chunk_t *chunk = allocate_chunk(chunk_pool, arena);
    if (chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to create object chunk (index=%u)", (unsigned)index);
    }
    chunk->class_id = class_id;
    chunk->chunk_class_id = (uint8_t)(class_id & 0xFFu);
    chunk->data_version = 0;
    chunk->chunk_version = 0;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    const size_t dword_count = byte_count / sizeof(uint32_t);
    for (size_t i = 0; i < dword_count; ++i) {
        const uint32_t value = nmo_read_u32_le(bytes + i * sizeof(uint32_t));
        NMO_RETURN_IF_ERROR(nmo_arena_array_append(&chunk->data, &value));
    }
    chunk->uncompressed_size = dword_count * sizeof(uint32_t);
    obj->chunk = chunk;
    NMO_RETURN_OK();
}

/**
 * @brief Parse object data from buffer
 *
 * Object data format (for file_version >= 4):
 *   For each object:
 *     - [only if version < 7] object_id (4 bytes int32)
 *     - data_size (4 bytes int32)
 *     - chunk_data (data_size bytes)
 */
static nmo_status_t parse_object_data(
    const uint8_t *data,
    size_t size,
    size_t *pos,
    uint32_t file_version,
    nmo_data_section_t *section,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    if (section->object_count == 0) {
        section->objects = NULL;
        NMO_RETURN_OK();
    }

    /* Allocate object data array */
    size_t object_bytes = 0;
    if (!nmo_safe_mul_size(sizeof(nmo_object_data_t), section->object_count, &object_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Object data allocation overflow");
    }
    section->objects = (nmo_object_data_t *) nmo_arena_alloc(
        arena,
        object_bytes,
        alignof(nmo_object_data_t));
    if (section->objects == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object data array");
    }
    memset(section->objects, 0, object_bytes);

    /* Parse each object */
    for (uint32_t i = 0; i < section->object_count; i++) {
        nmo_object_data_t *obj = &section->objects[i];

        obj->object_id = 0;
        if (file_version < 4) {
            NMO_RETURN_IF_ERROR(parse_object_entry_v2(
                data, size, pos, section->file_write_mode, i, obj, chunk_pool, arena));
            continue;
        }

        /* For file_version < 7, object ID is stored here */
        /* For file_version >= 8, object IDs are in Header1 */
        if (file_version < 7) {
            CHECK_BUFFER_SIZE(arena, *pos, 4, size);
            obj->object_id = nmo_read_u32_le(data + *pos);
            *pos += 4;
            /* Object ID is not stored in nmo_object_data for version < 7
             * because it's redundant with Header1 in version >= 8 */
        }

        /* Read data size */
        CHECK_BUFFER_SIZE(arena, *pos, 4, size);
        obj->data_size = nmo_read_u32_le(data + *pos);
        *pos += 4;

        /* Parse chunk data if present */
        if (obj->data_size > 0) {
            CHECK_BUFFER_SIZE(arena, *pos, obj->data_size, size);

            /* Create chunk */
            obj->chunk = allocate_chunk(chunk_pool, arena);
            if (obj->chunk == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Failed to create object chunk (index=%u, size=%u)",
                                        (unsigned)i, (unsigned)obj->data_size);
            }

            /* Parse chunk from buffer */
            NMO_RETURN_IF_ERROR_CTX(nmo_chunk_parse(obj->chunk, data + *pos, obj->data_size),
                                    "Failed to parse object chunk (index=%u, size=%u)",
                                    (unsigned)i,
                                    (unsigned)obj->data_size);

            obj->chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
            obj->chunk->uncompressed_size = obj->data_size;

            *pos += obj->data_size;
        } else {
            obj->chunk = NULL;
        }
    }

    NMO_RETURN_OK();
}

static void release_pooled_chunks(nmo_data_section_t *section,
                                  nmo_chunk_pool_t *chunk_pool) {
    if (section == NULL || chunk_pool == NULL) {
        return;
    }

    for (uint32_t i = 0; section->managers != NULL &&
                         i < section->manager_count; ++i) {
        nmo_chunk_t *chunk = section->managers[i].chunk;
        if (chunk != NULL && nmo_chunk_pool_validate(chunk_pool, chunk)) {
            nmo_chunk_pool_release(chunk_pool, chunk);
        }
    }
    for (uint32_t i = 0; section->objects != NULL &&
                         i < section->object_count; ++i) {
        nmo_chunk_t *chunk = section->objects[i].chunk;
        if (chunk != NULL && nmo_chunk_pool_validate(chunk_pool, chunk)) {
            nmo_chunk_pool_release(chunk_pool, chunk);
        }
    }
}

static nmo_status_t data_section_validate_storage(
    const nmo_data_section_t *data_section)
{
    if (data_section == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (data_section->manager_count > 0 && data_section->managers == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (data_section->object_count > 0 && data_section->objects == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    return NMO_OK;
}

nmo_status_t nmo_data_section_parse(
    const void *data,
    size_t size,
    uint32_t file_version,
    nmo_data_section_t *data_section,
    nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena) {
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL arena passed to nmo_data_section_parse");
    }

    if (data == NULL || data_section == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL pointer passed to nmo_data_section_parse");
    }

    nmo_data_section_t staged;
    memset(&staged, 0, sizeof(staged));
    /* The engine does not read a manager block below file version 6, so the
       section holds no managers whatever the header says. */
    staged.manager_count = data_section_has_manager_block(file_version)
        ? data_section->manager_count : 0u;
    staged.object_count = data_section->object_count;
    staged.save_id_max = data_section->save_id_max;
    staged.file_write_mode = data_section->file_write_mode;

    const uint8_t *buffer = (const uint8_t *) data;
    size_t pos = 0;

    /* Below version 8 the section carries SaveIDMax and ObjectCount first,
       and the engine reads the objects by the section's count. */
    if (data_section_has_leading_counts(file_version)) {
        CHECK_BUFFER_SIZE(arena, pos, 8, size);
        staged.save_id_max = nmo_read_u32_le(buffer + pos);
        pos += 4;
        const uint32_t section_object_count = nmo_read_u32_le(buffer + pos);
        pos += 4;

        /* From version 7 the object table is in the header and has as many
           entries as the header says; the engine fills it from the section. */
        if (file_version >= 7u && section_object_count > staged.object_count) {
            NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR,
                             "Data section holds %u objects but the header declares %u",
                             (unsigned) section_object_count,
                             (unsigned) staged.object_count);
        }
        /* An entry takes at least its size field, so a count the rest of the
           section cannot hold is rejected before anything is allocated for it. */
        {
            const size_t entry_min = (file_version < 7u) ? 8u : 4u;
            if ((size_t) section_object_count > (size - pos) / entry_min) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Data section object count %u does not fit the section",
                                 (unsigned) section_object_count);
            }
        }
        staged.object_count = section_object_count;
    }

    /* Parse manager data (file_version >= 6) */
    nmo_status_t result = NMO_OK;
    if (data_section_has_manager_block(file_version) && staged.manager_count > 0) {
        result = parse_manager_data(
            buffer, size, &pos, &staged, chunk_pool, arena);
        if (result != NMO_OK) {
            release_pooled_chunks(&staged, chunk_pool);
            return result;
        }
    }

    /* Parse object data */
    if (staged.object_count > 0) {
        result = parse_object_data(
            buffer, size, &pos, file_version, &staged, chunk_pool, arena);
        if (result != NMO_OK) {
            release_pooled_chunks(&staged, chunk_pool);
            return result;
        }
    }

    *data_section = staged;
    NMO_RETURN_OK();
}

nmo_status_t nmo_data_section_serialize(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written,
    nmo_arena_t *arena) {
    if (bytes_written != NULL) {
        *bytes_written = 0;
    }
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL arena passed to nmo_data_section_serialize");
    }
    if (data_section == NULL || buffer == NULL || bytes_written == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to nmo_data_section_serialize");
    }
    nmo_data_section_plan_t plan;
    NMO_RETURN_IF_ERROR(nmo_data_section_plan_build(
        data_section, file_version, arena, &plan));
    NMO_RETURN_IF_ERROR(nmo_data_section_plan_write(
        data_section, &plan, file_version, (uint8_t *)buffer, buffer_size));

    *bytes_written = plan.total_size;
    NMO_RETURN_OK();
}

static nmo_status_t data_section_make_slice(
    const nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_data_chunk_slice_t *out_slice) {
    if (out_slice == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL data slice output");
    }

    memset(out_slice, 0, sizeof(*out_slice));

    if (chunk == NULL) {
        NMO_RETURN_OK();
    }

    void *serialized_data = NULL;
    size_t serialized_size = 0;
    NMO_RETURN_IF_ERROR_CTX(nmo_chunk_serialize_version1(chunk, &serialized_data, &serialized_size, arena),
                            "Failed to serialize data section chunk for plan");

    out_slice->bytes = (const uint8_t *)serialized_data;
    out_slice->size = serialized_size;
    NMO_RETURN_OK();
}

static nmo_status_t data_section_plan_add_entry_size(size_t *total_size, size_t header_bytes, size_t chunk_size) {
    if (total_size == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL data plan size output");
    }
    if (chunk_size > (size_t)UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR,
                         "Data section chunk too large to serialize (size=%zu)", chunk_size);
    }
    if (!nmo_safe_add_size(*total_size, header_bytes, total_size) ||
        !nmo_safe_add_size(*total_size, chunk_size, total_size)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Data section plan size overflow");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_data_section_plan_build(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    nmo_arena_t *arena,
    nmo_data_section_plan_t *out_plan) {
    if (data_section == NULL || arena == NULL || out_plan == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_data_section_plan_build");
    }

    memset(out_plan, 0, sizeof(*out_plan));
    NMO_RETURN_IF_ERROR(data_section_validate_storage(data_section));

    nmo_data_section_plan_t staged;
    memset(&staged, 0, sizeof(staged));
    staged.total_size = data_section_leading_bytes(file_version);

    if (data_section->manager_count > 0 && data_section_has_manager_block(file_version)) {
        size_t slice_bytes = 0;
        if (!nmo_safe_mul_size(sizeof(nmo_data_chunk_slice_t),
                               data_section->manager_count,
                               &slice_bytes)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Manager slice allocation overflow");
        }

        staged.manager_slices = (nmo_data_chunk_slice_t *)nmo_arena_alloc(
            arena, slice_bytes, alignof(nmo_data_chunk_slice_t));
        if (staged.manager_slices == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate manager slices");
        }
        staged.manager_count = data_section->manager_count;

        for (uint32_t i = 0; i < data_section->manager_count; i++) {
            const nmo_manager_data_t *mgr = &data_section->managers[i];
            nmo_data_chunk_slice_t *slice = &staged.manager_slices[i];
            NMO_RETURN_IF_ERROR_CTX(data_section_make_slice(mgr->chunk, arena, slice),
                                    "Failed to plan manager data chunk (index=%u)",
                                    (unsigned)i);
            NMO_RETURN_IF_ERROR(data_section_plan_add_entry_size(&staged.total_size, 12u, slice->size));
        }
    }

    if (data_section->object_count > 0) {
        size_t slice_bytes = 0;
        if (!nmo_safe_mul_size(sizeof(nmo_data_chunk_slice_t),
                               data_section->object_count,
                               &slice_bytes)) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Object slice allocation overflow");
        }

        staged.object_slices = (nmo_data_chunk_slice_t *)nmo_arena_alloc(
            arena, slice_bytes, alignof(nmo_data_chunk_slice_t));
        if (staged.object_slices == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object slices");
        }
        staged.object_count = data_section->object_count;

        size_t entry_header_bytes = (file_version < 7) ? 8u : 4u;
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            const nmo_object_data_t *obj = &data_section->objects[i];
            nmo_data_chunk_slice_t *slice = &staged.object_slices[i];
            if (file_version < 7 && obj->object_id == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                                 "Missing object ID for legacy data section (index=%u)",
                                 (unsigned)i);
            }
            NMO_RETURN_IF_ERROR_CTX(data_section_make_slice(obj->chunk, arena, slice),
                                    "Failed to plan object data chunk (index=%u)",
                                    (unsigned)i);
            NMO_RETURN_IF_ERROR(data_section_plan_add_entry_size(&staged.total_size,
                                                                 entry_header_bytes,
                                                                 slice->size));
        }
    }

    *out_plan = staged;
    NMO_RETURN_OK();
}

static nmo_status_t data_section_validate_plan(
    const nmo_data_section_t *data_section,
    const nmo_data_section_plan_t *plan,
    uint32_t file_version) {
    const uint32_t expected_manager_count =
        data_section_has_manager_block(file_version) ? data_section->manager_count : 0u;
    if (plan->manager_count != expected_manager_count ||
        plan->object_count != data_section->object_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Data section plan does not match section counts");
    }
    if (plan->manager_count > 0 && plan->manager_slices == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing manager slices");
    }
    if (plan->object_count > 0 && plan->object_slices == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing object slices");
    }

    size_t expected_size = data_section_leading_bytes(file_version);
    for (size_t i = 0; i < plan->manager_count; i++) {
        const nmo_data_chunk_slice_t *slice = &plan->manager_slices[i];
        if (slice->size > 0 && slice->bytes == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                             "Manager plan slice has size but no bytes (index=%zu)", i);
        }
        NMO_RETURN_IF_ERROR(data_section_plan_add_entry_size(
            &expected_size, 12u, slice->size));
    }

    size_t object_header_size = (file_version < 7) ? 8u : 4u;
    for (size_t i = 0; i < plan->object_count; i++) {
        const nmo_data_chunk_slice_t *slice = &plan->object_slices[i];
        if (file_version < 7 && data_section->objects[i].object_id == 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                             "Missing object ID for legacy data section (index=%zu)", i);
        }
        if (slice->size > 0 && slice->bytes == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                             "Object plan slice has size but no bytes (index=%zu)", i);
        }
        NMO_RETURN_IF_ERROR(data_section_plan_add_entry_size(
            &expected_size, object_header_size, slice->size));
    }

    if (plan->total_size != expected_size) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Data section plan size mismatch (expected=%zu, actual=%zu)",
                         expected_size, plan->total_size);
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_data_section_plan_write(
    const nmo_data_section_t *data_section,
    const nmo_data_section_plan_t *plan,
    uint32_t file_version,
    uint8_t *output,
    size_t output_size) {
    if (data_section == NULL || plan == NULL || (output == NULL && output_size > 0)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_data_section_plan_write");
    }
    NMO_RETURN_IF_ERROR(data_section_validate_storage(data_section));
    NMO_RETURN_IF_ERROR(data_section_validate_plan(data_section, plan, file_version));
    if (output_size < plan->total_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR,
                         "Data section output buffer too small (need=%zu, have=%zu)",
                         plan->total_size, output_size);
    }

    size_t pos = 0;

    if (data_section_has_leading_counts(file_version)) {
        nmo_write_u32_le(output + pos, data_section->save_id_max);
        pos += 4;
        nmo_write_u32_le(output + pos, data_section->object_count);
        pos += 4;
    }

    for (uint32_t i = 0; i < plan->manager_count; i++) {
        const nmo_manager_data_t *mgr = &data_section->managers[i];
        const nmo_data_chunk_slice_t *slice = &plan->manager_slices[i];

        nmo_write_u32_le(output + pos, mgr->guid.d1);
        pos += 4;
        nmo_write_u32_le(output + pos, mgr->guid.d2);
        pos += 4;
        nmo_write_u32_le(output + pos, (uint32_t)slice->size);
        pos += 4;
        if (slice->size > 0) {
            memcpy(output + pos, slice->bytes, slice->size);
            pos += slice->size;
        }
    }

    for (uint32_t i = 0; i < data_section->object_count; i++) {
        const nmo_object_data_t *obj = &data_section->objects[i];
        const nmo_data_chunk_slice_t *slice = &plan->object_slices[i];

        if (file_version < 7) {
            nmo_write_u32_le(output + pos, obj->object_id);
            pos += 4;
        }

        nmo_write_u32_le(output + pos, (uint32_t)slice->size);
        pos += 4;
        if (slice->size > 0) {
            memcpy(output + pos, slice->bytes, slice->size);
            pos += slice->size;
        }
    }

    NMO_RETURN_OK();
}

size_t nmo_data_section_calculate_size(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    nmo_arena_t *arena) {
    if (data_section == NULL || arena == NULL) {
        return 0;
    }
    if (data_section_validate_storage(data_section) != NMO_OK) {
        return 0;
    }

    size_t total_size = data_section_leading_bytes(file_version);

    /* Manager data (none before file version 6) */
    for (uint32_t i = 0; data_section_has_manager_block(file_version) &&
                         i < data_section->manager_count; i++) {
        const nmo_manager_data_t *mgr = &data_section->managers[i];
        if (!nmo_safe_add_size(total_size, 8u, &total_size) ||
            !nmo_safe_add_size(total_size, 4u, &total_size)) {
            return 0;
        }

        nmo_data_chunk_slice_t slice;
        if (data_section_make_slice(mgr->chunk, arena, &slice) != NMO_OK) {
            return 0;
        }

        if (slice.size > (size_t)UINT32_MAX ||
            !nmo_safe_add_size(total_size, slice.size, &total_size)) {
            return 0;
        }
    }

    /* Object data */
    for (uint32_t i = 0; i < data_section->object_count; i++) {
        const nmo_object_data_t *obj = &data_section->objects[i];

        if (file_version < 7) {
            if (!nmo_safe_add_size(total_size, 4u, &total_size)) {
                return 0;
            }
        }

        if (!nmo_safe_add_size(total_size, 4u, &total_size)) {
            return 0;
        }

        nmo_data_chunk_slice_t slice;
        if (data_section_make_slice(obj->chunk, arena, &slice) != NMO_OK) {
            return 0;
        }

        if (slice.size > (size_t)UINT32_MAX ||
            !nmo_safe_add_size(total_size, slice.size, &total_size)) {
            return 0;
        }
    }

    return total_size;
}

void nmo_data_section_free(nmo_data_section_t *data_section) {
    if (data_section == NULL) {
        return;
    }

    /* Free manager chunks */
    if (data_section->managers != NULL) {
        for (uint32_t i = 0; i < data_section->manager_count; i++) {
            if (data_section->managers[i].chunk != NULL) {
                nmo_chunk_clear(data_section->managers[i].chunk);
            }
        }
    }

    /* Free object chunks */
    if (data_section->objects != NULL) {
        for (uint32_t i = 0; i < data_section->object_count; i++) {
            if (data_section->objects[i].chunk != NULL) {
                nmo_chunk_clear(data_section->objects[i].chunk);
            }
        }
    }

    /* Note: managers and objects arrays are arena-allocated, no free needed */
    memset(data_section, 0, sizeof(nmo_data_section_t));
}
