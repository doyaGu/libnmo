/**
 * @file test_legacy_file_load.c
 * @brief Loading files below version 7, which have no Header1 object table
 *
 * CKFile::ReadFileHeaders reads no Header1 below version 7, and below version
 * 5 no section sizes; CKFile::ReadFileData then reads the data section to the
 * end of the file, takes each object id from the section, its class id from
 * the chunk and its name from the chunk's CK_STATESAVE_NAME section. Below
 * version 4 an entry carries the class id, the save flags and the stored size
 * itself, and the bytes are zlib packed when the write mode says so.
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_utils.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_header.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_context.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_statesave_ids.h"
#include "runtime/nmo_document.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "nmo_types.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct byte_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} byte_buffer_t;

static void buffer_append(byte_buffer_t *buffer, const void *bytes, size_t count)
{
    if (buffer->size + count > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 256u;
        while (capacity < buffer->size + count) {
            capacity *= 2u;
        }
        buffer->data = (uint8_t *)realloc(buffer->data, capacity);
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, bytes, count);
    buffer->size += count;
}

static void buffer_u32(byte_buffer_t *buffer, uint32_t value)
{
    uint8_t bytes[4];
    nmo_write_u32_le(bytes, value);
    buffer_append(buffer, bytes, sizeof(bytes));
}

/* A chunk of class class_id that holds only the name section. */
static nmo_chunk_t *name_chunk(nmo_arena_t *arena, nmo_class_id_t class_id, const char *name)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        return NULL;
    }
    chunk->class_id = class_id;
    if (nmo_chunk_start_write(chunk) != NMO_OK ||
        nmo_chunk_write_identifier(chunk, CK_STATESAVE_NAME) != NMO_OK ||
        nmo_chunk_write_string(chunk, name) != NMO_OK) {
        return NULL;
    }
    nmo_chunk_close(chunk);
    return chunk;
}

typedef struct legacy_object {
    uint32_t file_id;
    nmo_class_id_t class_id;
    const char *name;   /* NULL: an entry without a chunk */
    bool packed;        /* below version 4: store the dwords zlib packed */
} legacy_object_t;

/* The data section of a file of the given version: the leading counts, no
 * managers, and one entry per object. */
static bool build_data_section(nmo_arena_t *arena,
                               uint32_t version,
                               const legacy_object_t *objects,
                               size_t count,
                               byte_buffer_t *out)
{
    uint32_t max_id = 0u;
    for (size_t i = 0; i < count; ++i) {
        if ((objects[i].file_id & 0x7FFFFFFFu) > max_id) {
            max_id = objects[i].file_id & 0x7FFFFFFFu;
        }
    }
    buffer_u32(out, max_id);
    buffer_u32(out, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) {
        buffer_u32(out, objects[i].file_id);
        if (objects[i].name == NULL) {
            buffer_u32(out, 0u);
            continue;
        }
        nmo_chunk_t *chunk = name_chunk(arena, objects[i].class_id, objects[i].name);
        if (chunk == NULL) {
            return false;
        }
        if (version >= 4u) {
            void *bytes = NULL;
            size_t size = 0;
            if (nmo_chunk_serialize_version1(chunk, &bytes, &size, arena) != NMO_OK) {
                return false;
            }
            buffer_u32(out, (uint32_t)size);
            buffer_append(out, bytes, size);
            continue;
        }
        const size_t size = chunk->data.count * sizeof(uint32_t);
        uint8_t *raw = (uint8_t *)malloc(size);
        for (size_t d = 0; d < chunk->data.count; ++d) {
            nmo_write_u32_le(raw + d * 4u, ((const uint32_t *)chunk->data.data)[d]);
        }
        buffer_u32(out, (uint32_t)size);
        buffer_u32(out, objects[i].class_id);
        buffer_u32(out, 0u); /* save flags */
        if (objects[i].packed) {
            mz_ulong packed_size = mz_compressBound((mz_ulong)size);
            uint8_t *packed = (uint8_t *)malloc(packed_size);
            /* An entry whose stored size equals its size is read as not packed. */
            if (mz_compress(packed, &packed_size, raw, (mz_ulong)size) != MZ_OK ||
                packed_size == size) {
                free(packed);
                free(raw);
                return false;
            }
            buffer_u32(out, (uint32_t)packed_size);
            buffer_append(out, packed, packed_size);
            free(packed);
        } else {
            buffer_u32(out, (uint32_t)size);
            buffer_append(out, raw, size);
        }
        free(raw);
    }
    return true;
}

/* A whole file: header (32 bytes below version 5), no Header1, the data
 * section, zlib packed as a whole when compress is set (version 5 and 6). */
static bool build_file(nmo_arena_t *arena,
                       uint32_t version,
                       uint32_t write_mode,
                       bool compress,
                       const legacy_object_t *objects,
                       size_t count,
                       byte_buffer_t *out)
{
    byte_buffer_t data = {0};
    if (!build_data_section(arena, version, objects, count, &data)) {
        free(data.data);
        return false;
    }
    const uint32_t crc = nmo_file_data_crc(data.data, data.size);

    uint8_t *stored = data.data;
    mz_ulong stored_size = (mz_ulong)data.size;
    uint8_t *packed = NULL;
    if (compress) {
        stored_size = mz_compressBound((mz_ulong)data.size);
        packed = (uint8_t *)malloc(stored_size);
        if (mz_compress(packed, &stored_size, data.data, (mz_ulong)data.size) != MZ_OK) {
            free(packed);
            free(data.data);
            return false;
        }
        stored = packed;
    }

    buffer_append(out, "Nemo Fi\0", 8);
    buffer_u32(out, crc);
    buffer_u32(out, 0x13022002u);
    buffer_u32(out, version);
    buffer_u32(out, 0u);           /* FileVersion2 */
    buffer_u32(out, write_mode);
    buffer_u32(out, 0u);           /* Hdr1PackSize */
    if (version >= 5u) {
        buffer_u32(out, (uint32_t)stored_size);
        buffer_u32(out, (uint32_t)data.size);
        buffer_u32(out, 0u);       /* managers */
        buffer_u32(out, (uint32_t)count);
        buffer_u32(out, 0u);       /* MaxIDSaved, replaced by the section's */
        buffer_u32(out, 0u);       /* product version */
        buffer_u32(out, 0u);       /* product build */
        buffer_u32(out, 0u);       /* Hdr1UnPackSize */
    }
    buffer_append(out, stored, stored_size);
    free(packed);
    free(data.data);
    return true;
}

static bool write_file(const char *path, const byte_buffer_t *bytes)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const bool ok = fwrite(bytes->data, 1u, bytes->size, file) == bytes->size;
    return fclose(file) == 0 && ok;
}

static nmo_status_t load_legacy_file(nmo_context_t *ctx,
                                     uint32_t version,
                                     uint32_t write_mode,
                                     bool compress,
                                     const legacy_object_t *objects,
                                     size_t count,
                                     const char *path,
                                     nmo_document_t **out_document)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    byte_buffer_t file = {0};
    bool built = arena != NULL &&
                 build_file(arena, version, write_mode, compress, objects, count, &file) &&
                 write_file(path, &file);
    free(file.data);
    nmo_arena_destroy(arena);
    if (!built) {
        return NMO_ERR_INVALID_STATE;
    }
    return nmo_document_load_file(ctx, path, NULL, out_document);
}

static void assert_loaded(nmo_document_t *document,
                          uint32_t version,
                          const legacy_object_t *objects,
                          size_t count)
{
    nmo_object_repository_t *repo = nmo_document_get_repository(document);
    ASSERT_NOT_NULL(repo);
    size_t with_chunk = 0;
    for (size_t i = 0; i < count; ++i) {
        if (objects[i].name == NULL) {
            continue;
        }
        ++with_chunk;
        nmo_object_t *object = nmo_object_repository_find_by_name(repo, objects[i].name);
        ASSERT_NOT_NULL(object);
        ASSERT_EQ(objects[i].class_id, nmo_object_get_class_id(object));
    }
    /* An entry without a chunk has no class below version 7 and creates no object. */
    ASSERT_EQ(with_chunk, nmo_object_repository_get_count(repo));

    nmo_file_info_t info = nmo_document_get_file_info(document);
    ASSERT_EQ(version, info.file_version);
    ASSERT_EQ(NMO_CRC_OK, info.crc_status);
}

/* CKFile::Save writes the current version; a file loaded below 7 is saved as 8
 * and loads back with the same objects. */
static void assert_saves_as_version_8(nmo_context_t *ctx,
                                      nmo_document_t *document,
                                      const legacy_object_t *objects,
                                      size_t count,
                                      const char *path)
{
    ASSERT_EQ(NMO_OK, nmo_document_save_file(document, path, NULL));
    nmo_document_t *reloaded = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, path, NULL, &reloaded));
    assert_loaded(reloaded, 8u, objects, count);
    nmo_document_destroy(reloaded);
}

TEST(legacy_file_load, versions_4_to_6_take_ids_from_the_section_and_names_from_the_chunks)
{
    const legacy_object_t objects[] = {
        {5u, NMO_CID_GROUP, "First group", false},
        {9u, NMO_CID_GROUP, "Second group", false},
    };
    for (uint32_t version = 4u; version <= 6u; ++version) {
        nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
        ASSERT_NOT_NULL(ctx);
        nmo_document_t *document = NULL;
        ASSERT_EQ(NMO_OK, load_legacy_file(ctx, version, 0u, false, objects, 2u,
                                           NMO_TEST_SCRATCH_FILE("legacy_v4_6.nmo"),
                                           &document));
        assert_loaded(document, version, objects, 2u);
        assert_saves_as_version_8(ctx, document, objects, 2u,
                                  NMO_TEST_SCRATCH_FILE("legacy_v4_6_saved.nmo"));
        nmo_document_destroy(document);
        nmo_context_release(ctx);
    }
}

TEST(legacy_file_load, versions_5_and_6_unpack_a_compressed_data_section)
{
    const legacy_object_t objects[] = {
        {3u, NMO_CID_GROUP, "Packed group", false},
    };
    for (uint32_t version = 5u; version <= 6u; ++version) {
        nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
        ASSERT_NOT_NULL(ctx);
        nmo_document_t *document = NULL;
        ASSERT_EQ(NMO_OK, load_legacy_file(ctx, version, NMO_FILE_WRITE_WHOLE_COMPRESSED, true,
                                           objects, 1u,
                                           NMO_TEST_SCRATCH_FILE("legacy_v5_6_packed.nmo"),
                                           &document));
        assert_loaded(document, version, objects, 1u);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
    }
}

TEST(legacy_file_load, versions_2_and_3_read_their_own_entry_layout)
{
    const legacy_object_t objects[] = {
        {4u, NMO_CID_GROUP, "Plain group", false},
        {6u, 0, NULL, false},
        {7u, NMO_CID_GROUP, "Packed group packed group packed group packed group", true},
    };
    for (uint32_t version = 2u; version <= 3u; ++version) {
        nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
        ASSERT_NOT_NULL(ctx);
        nmo_document_t *document = NULL;
        ASSERT_EQ(NMO_OK, load_legacy_file(ctx, version, NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD,
                                           false, objects, 3u,
                                           NMO_TEST_SCRATCH_FILE("legacy_v2_3.nmo"),
                                           &document));
        assert_loaded(document, version, objects, 3u);
        assert_saves_as_version_8(ctx, document, objects, 3u,
                                  NMO_TEST_SCRATCH_FILE("legacy_v2_3_saved.nmo"));
        nmo_document_destroy(document);
        nmo_context_release(ctx);
    }
}

TEST(legacy_file_load, an_entry_without_a_class_id_is_corrupt)
{
    /* Version 2 entry: id, size, then a zero class id. */
    byte_buffer_t data = {0};
    buffer_u32(&data, 1u);
    buffer_u32(&data, 1u);
    buffer_u32(&data, 1u);
    buffer_u32(&data, 4u);
    buffer_u32(&data, 0u);
    buffer_u32(&data, 0u);
    buffer_u32(&data, 4u);
    buffer_u32(&data, 0u);

    byte_buffer_t file = {0};
    buffer_append(&file, "Nemo Fi\0", 8);
    buffer_u32(&file, nmo_file_data_crc(data.data, data.size));
    buffer_u32(&file, 0x13022002u);
    buffer_u32(&file, 2u);
    buffer_u32(&file, 0u);
    buffer_u32(&file, 0u);
    buffer_u32(&file, 0u);
    buffer_append(&file, data.data, data.size);
    const char *path = NMO_TEST_SCRATCH_FILE("legacy_v2_no_class.nmo");
    ASSERT_TRUE(write_file(path, &file));
    free(file.data);
    free(data.data);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_ERR_CORRUPT, nmo_document_load_file(ctx, path, NULL, &document));
    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(legacy_file_load, versions_4_to_6_take_ids_from_the_section_and_names_from_the_chunks);
    REGISTER_TEST(legacy_file_load, versions_5_and_6_unpack_a_compressed_data_section);
    REGISTER_TEST(legacy_file_load, versions_2_and_3_read_their_own_entry_layout);
    REGISTER_TEST(legacy_file_load, an_entry_without_a_class_id_is_corrupt);
TEST_MAIN_END()
