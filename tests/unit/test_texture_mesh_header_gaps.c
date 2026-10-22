/**
 * @file test_texture_mesh_header_gaps.c
 * @brief Details of textures, meshes, data sections and file headers checked
 *        against the engine
 *
 * Covers the desired video format of a texture, raw bitmap slots below 24 bits
 * per pixel, the two forms of a mesh weight section, the manager block of a
 * data section, the file checksum of the versions below 8 and the plugin list
 * of Header1.
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_utils.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_data.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_image.h"
#include "io/nmo_io_memory.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_statesave_ids.h"

#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Texture desired video format
 * ------------------------------------------------------------------------ */

static nmo_status_t texture_round_trip(nmo_arena_t *arena,
                                       uint32_t desired_format,
                                       nmo_texture_state_t *out_loaded)
{
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_texture_state_t source;
    nmo_status_t status = nmo_texture_vtable.create(&source, NULL, NULL);
    if (status != NMO_OK) return status;
    source.has_oldtexonly = 1u;
    source.save_options = NMO_CKTEXTURE_USEGLOBAL;
    source.has_desired_video_format = 1u;
    source.desired_video_format = desired_format;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) return NMO_ERR_NOMEM;
    status = nmo_chunk_start_write(chunk);
    if (status != NMO_OK) return status;
    status = nmo_texture_serialize(&source, chunk, NULL, &ser_ctx);
    if (status != NMO_OK) return status;
    nmo_chunk_close(chunk);

    status = nmo_chunk_start_read(chunk);
    if (status != NMO_OK) return status;
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    status = nmo_texture_vtable.create(out_loaded, NULL, NULL);
    if (status != NMO_OK) return status;
    status = nmo_texture_deserialize(out_loaded, chunk, NULL, &des_ctx);
    nmo_texture_vtable.destroy(&source, NULL, NULL);
    return status;
}

TEST(texture_gaps, desired_video_format_keeps_the_stored_value)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);

    /* A value past _32_X8L8V8U8: the engine works with _16_ARGB1555 after the
     * load, the state keeps and writes back what the file holds. */
    nmo_texture_state_t loaded;
    ASSERT_EQ(NMO_OK, texture_round_trip(arena, 0x40u, &loaded));
    ASSERT_EQ(1u, loaded.has_desired_video_format);
    ASSERT_EQ(0x40u, loaded.desired_video_format);
    ASSERT_EQ((uint32_t)_16_ARGB1555, nmo_texture_effective_desired_video_format(&loaded));
    nmo_texture_vtable.destroy(&loaded, NULL, NULL);

    /* The last valid value passes through, the first invalid one does not. */
    ASSERT_EQ(NMO_OK, texture_round_trip(arena, (uint32_t)_32_X8L8V8U8, &loaded));
    ASSERT_EQ((uint32_t)_32_X8L8V8U8, loaded.desired_video_format);
    ASSERT_EQ((uint32_t)_32_X8L8V8U8, nmo_texture_effective_desired_video_format(&loaded));
    nmo_texture_vtable.destroy(&loaded, NULL, NULL);

    ASSERT_EQ(NMO_OK, texture_round_trip(arena, (uint32_t)_32_X8L8V8U8 + 1u, &loaded));
    ASSERT_EQ((uint32_t)_32_X8L8V8U8 + 1u, loaded.desired_video_format);
    ASSERT_EQ((uint32_t)_16_ARGB1555, nmo_texture_effective_desired_video_format(&loaded));
    nmo_texture_vtable.destroy(&loaded, NULL, NULL);

    ASSERT_EQ(NMO_OK, texture_round_trip(arena, 0xFFFFFFFFu, &loaded));
    ASSERT_EQ(0xFFFFFFFFu, loaded.desired_video_format);
    ASSERT_EQ((uint32_t)_16_ARGB1555, nmo_texture_effective_desired_video_format(&loaded));
    nmo_texture_vtable.destroy(&loaded, NULL, NULL);

    ASSERT_EQ((uint32_t)UNKNOWN_PF, nmo_texture_effective_desired_video_format(NULL));
    nmo_arena_destroy(arena);
}

/* ------------------------------------------------------------------------
 * Raw bitmap slots below 24 bits per pixel
 * ------------------------------------------------------------------------ */

TEST(texture_gaps, raw_slot_below_24_bits_per_pixel_decodes_like_32)
{
    const uint8_t red[4]   = {10, 11, 20, 21};
    const uint8_t green[4] = {12, 13, 22, 23};
    const uint8_t blue[4]  = {14, 15, 24, 25};
    const uint8_t alpha[4] = {16, 17, 26, 27};
    const uint8_t expected[16] = {
        20, 22, 24, 26,  21, 23, 25, 27,
        10, 12, 14, 16,  11, 13, 15, 17,
    };
    const int depths[] = {8, 16, 24, 32};

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); ++i) {
        uint8_t *pixels = NULL;
        int channels = 0;
        ASSERT_EQ(NMO_OK, nmo_image_reconstruct_pixels(
            red, green, blue, alpha, 4, 4, 4, 4, 2, 2, depths[i],
            arena, &pixels, &channels));
        ASSERT_EQ(4, channels);
        ASSERT_EQ(0, memcmp(expected, pixels, sizeof(expected)));
    }

    /* Without an alpha plane the image is opaque, as the engine builds it. */
    uint8_t *opaque = NULL;
    int channels = 0;
    ASSERT_EQ(NMO_OK, nmo_image_reconstruct_pixels(
        red, green, blue, NULL, 4, 4, 4, 0, 2, 2, 16, arena, &opaque, &channels));
    ASSERT_EQ(255, opaque[3]);

    /* Zero bits per pixel is "no image" in ReadRawBitmap. */
    uint8_t *none = NULL;
    ASSERT_NE(NMO_OK, nmo_image_reconstruct_pixels(
        red, green, blue, alpha, 4, 4, 4, 4, 2, 2, 0, arena, &none, &channels));
    nmo_arena_destroy(arena);
}

/* ------------------------------------------------------------------------
 * Mesh weight section
 * ------------------------------------------------------------------------ */

static nmo_chunk_t *weights_chunk(nmo_arena_t *arena)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) return NULL;
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    if (nmo_chunk_start_write(chunk) != NMO_OK) return NULL;
    if (nmo_chunk_write_identifier(chunk, CK_STATESAVE_MESHWEIGHTS) != NMO_OK) return NULL;
    return chunk;
}

static nmo_status_t load_weights(nmo_arena_t *arena, nmo_chunk_t *chunk,
                                 nmo_mesh_state_t *state)
{
    nmo_chunk_close(chunk);
    nmo_status_t status = nmo_chunk_start_read(chunk);
    if (status != NMO_OK) return status;
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    status = nmo_mesh_vtable.create(state, NULL, NULL);
    if (status != NMO_OK) return status;
    return nmo_mesh_deserialize(state, chunk, NULL, &des_ctx);
}

TEST(mesh_gaps, weight_section_of_eight_bytes_is_the_uniform_form)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);

    /* Count and one float: every weight takes the float, even when the
     * float's bits equal the byte size of the array (3 * 4). */
    nmo_chunk_t *chunk = weights_chunk(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 12u));
    nmo_mesh_state_t state;
    ASSERT_EQ(NMO_OK, load_weights(arena, chunk, &state));
    ASSERT_EQ(3u, state.vertex_weight_count);
    uint32_t bits = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        memcpy(&bits, &state.vertex_weights[i], sizeof(bits));
        ASSERT_EQ(12u, bits);
    }
    nmo_mesh_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(mesh_gaps, weight_section_of_more_than_eight_bytes_is_a_buffer)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);

    const float values[2] = {0.25f, 0.75f};
    nmo_chunk_t *chunk = weights_chunk(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(chunk, values, sizeof(values)));
    nmo_mesh_state_t state;
    ASSERT_EQ(NMO_OK, load_weights(arena, chunk, &state));
    ASSERT_EQ(2u, state.vertex_weight_count);
    ASSERT_EQ(0.25f, state.vertex_weights[0]);
    ASSERT_EQ(0.75f, state.vertex_weights[1]);
    nmo_mesh_vtable.destroy(&state, NULL, NULL);

    /* A buffer whose stated size is not the count's: the engine copies the
     * stated size, libnmo keeps what fits and leaves the rest zero. */
    chunk = weights_chunk(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(chunk, values, sizeof(values)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 1.0f));
    ASSERT_EQ(NMO_OK, load_weights(arena, chunk, &state));
    ASSERT_EQ(3u, state.vertex_weight_count);
    ASSERT_EQ(0.25f, state.vertex_weights[0]);
    ASSERT_EQ(0.75f, state.vertex_weights[1]);
    ASSERT_EQ(0.0f, state.vertex_weights[2]);
    nmo_mesh_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(mesh_gaps, face_masks_copy_every_available_face_in_order)
{
    for (uint32_t version = 8u; version <= 9u; ++version) {
        for (uint32_t faces = 0u; faces <= 5u; ++faces) {
            for (uint32_t masks = 0u; masks <= 5u; ++masks) {
                nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
                ASSERT_NOT_NULL(arena);
                nmo_chunk_t *chunk = nmo_chunk_create(arena);
                ASSERT_NOT_NULL(chunk);
                chunk->data_version = version;
                ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
                ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_MESHFACES));
                ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, (int32_t)faces));
                if (version < 9u && faces > 0u) {
                    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
                    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(chunk, 0u));
                    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, (int32_t)faces));
                }
                for (uint32_t i = 0u; i < faces * 2u; ++i) {
                    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
                }
                ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_MESHFACECHANMASK));
                ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, (int32_t)masks));
                for (uint32_t i = 0u; i < masks / 2u; ++i) {
                    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword_as_words(
                        chunk, (0x1200u + 2u * i) | ((0x1201u + 2u * i) << 16)));
                }
                if ((masks & 1u) != 0u) {
                    ASSERT_EQ(NMO_OK, nmo_chunk_write_word(chunk, (uint16_t)(0x1200u + masks - 1u)));
                }
                nmo_mesh_state_t state;
                ASSERT_EQ(NMO_OK, load_weights(arena, chunk, &state));
                ASSERT_EQ(faces, state.face_count);
                for (uint32_t i = 0u; i < faces; ++i) {
                    ASSERT_EQ(i < masks ? 0x1200u + i : 0xFFFFu, state.faces[i].channel_mask);
                }
                nmo_mesh_vtable.destroy(&state, NULL, NULL);
                nmo_arena_destroy(arena);
            }
        }
    }
}

/* ------------------------------------------------------------------------
 * Data section: the manager block exists from file version 6
 * ------------------------------------------------------------------------ */

TEST(data_gaps, manager_block_is_only_written_from_version_6)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    ASSERT_NOT_NULL(arena);

    nmo_manager_data_t manager = {0};
    manager.guid.d1 = 0x11111111u;
    manager.guid.d2 = 0x22222222u;
    nmo_object_data_t object = {0};
    object.object_id = 0x55u;

    nmo_data_section_t section = {0};
    section.manager_count = 1u;
    section.managers = &manager;
    section.object_count = 1u;
    section.objects = &object;

    /* version, bytes: 8 leading bytes below version 8 (SaveIDMax, ObjectCount),
     * manager entry 12 (6+), object entry header 8 (below 7) or 4 (7+), no
     * chunk data in either entry. */
    const struct { uint32_t version; size_t expected; } cases[] = {
        {5u, 16u}, {6u, 28u}, {7u, 24u}, {8u, 16u},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t buffer[64];
        size_t written = 0;
        ASSERT_EQ(NMO_OK, nmo_data_section_serialize(
            &section, cases[i].version, buffer, sizeof(buffer), &written, arena));
        ASSERT_EQ(cases[i].expected, written);
        ASSERT_EQ(cases[i].expected, nmo_data_section_calculate_size(
            &section, cases[i].version, arena));

        nmo_data_section_t parsed = {0};
        parsed.manager_count = 1u;
        parsed.object_count = 1u;
        ASSERT_EQ(NMO_OK, nmo_data_section_parse(
            buffer, written, cases[i].version, &parsed, NULL, arena));
        ASSERT_EQ(1u, parsed.object_count);
        if (cases[i].version >= 6u) {
            ASSERT_EQ(1u, parsed.manager_count);
            ASSERT_EQ(0x11111111u, parsed.managers[0].guid.d1);
            ASSERT_EQ(0x22222222u, parsed.managers[0].guid.d2);
        } else {
            /* Below version 6 the engine reads no managers. */
            ASSERT_EQ(0u, parsed.manager_count);
            ASSERT_TRUE(parsed.managers == NULL);
            ASSERT_EQ(0x55u, parsed.objects[0].object_id);
        }

        /* A parsed section can be planned and written again. */
        size_t rewritten = 0;
        uint8_t again[64];
        ASSERT_EQ(NMO_OK, nmo_data_section_serialize(
            &parsed, cases[i].version, again, sizeof(again), &rewritten, arena));
        ASSERT_EQ(written, rewritten);
        ASSERT_EQ(0, memcmp(buffer, again, written));
    }
    nmo_arena_destroy(arena);
}

/* ------------------------------------------------------------------------
 * File header: versions below 7 and the checksum of the versions below 8
 * ------------------------------------------------------------------------ */

static nmo_file_header_t sample_header(uint32_t version)
{
    nmo_file_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.signature, "Nemo Fi\0", 8);
    header.file_version = version;
    header.ck_version = 0x13022002u;
    return header;
}

TEST(header_gaps, versions_5_to_8_parse_and_validate)
{
    for (uint32_t version = 5u; version <= 8u; ++version) {
        nmo_file_header_t header = sample_header(version);
        header.object_count = 3u;

        nmo_io_interface_t *write_io = nmo_memory_io_open_write(128);
        ASSERT_NOT_NULL(write_io);
        ASSERT_EQ(NMO_OK, nmo_file_header_serialize(&header, write_io));
        size_t size = 0;
        const void *bytes = nmo_memory_io_get_data(write_io, &size);
        ASSERT_EQ(64u, size);

        nmo_io_interface_t *read_io = nmo_memory_io_open_read(bytes, size);
        ASSERT_NOT_NULL(read_io);
        nmo_file_header_t parsed;
        ASSERT_EQ(NMO_OK, nmo_file_header_parse(read_io, &parsed));
        ASSERT_EQ(version, parsed.file_version);
        ASSERT_EQ(3u, parsed.object_count);
        nmo_io_close(read_io);
        nmo_io_close(write_io);

        ASSERT_EQ(NMO_OK, nmo_file_header_validate(&parsed));
    }
}

TEST(header_gaps, data_checksum_is_adler32_from_zero)
{
    /* s1 = 97, s2 = 97 */
    ASSERT_EQ(0x00610061u, nmo_file_data_crc((const uint8_t *)"a", 1));
    /* s1 = 97+98+99, s2 = 97 + 195 + 294; zlib's adler32 from 1 gives 0x024D0127 */
    ASSERT_EQ(0x024A0126u, nmo_file_data_crc((const uint8_t *)"abc", 3));
    ASSERT_EQ(0u, nmo_file_data_crc((const uint8_t *)"abc", 0));
    ASSERT_EQ(0u, nmo_file_data_crc(NULL, 0));
}

TEST(header_gaps, version_7_checksum_covers_the_unpacked_data_only)
{
    const uint8_t header1[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t packed[5] = {9, 9, 9, 9, 9};
    uint8_t unpacked[40];
    for (size_t i = 0; i < sizeof(unpacked); ++i) unpacked[i] = (uint8_t)(i * 7u + 1u);

    nmo_file_header_t header = sample_header(7u);
    header.hdr1_pack_size = sizeof(header1);
    header.data_pack_size = sizeof(packed);
    header.data_unpack_size = sizeof(unpacked);

    const uint32_t expected = nmo_file_data_crc(unpacked, sizeof(unpacked));
    const uint32_t crc = nmo_file_crc_for_version(
        &header, header1, sizeof(header1), packed, sizeof(packed),
        unpacked, sizeof(unpacked));
    ASSERT_EQ(expected, crc);

    header.crc = crc;
    ASSERT_EQ(NMO_OK, nmo_file_header_verify_crc(
        &header, header1, sizeof(header1), packed, sizeof(packed),
        unpacked, sizeof(unpacked)));

    unpacked[17] ^= 0x40u;
    ASSERT_EQ(NMO_ERR_CHECKSUM_MISMATCH, nmo_file_header_verify_crc(
        &header, header1, sizeof(header1), packed, sizeof(packed),
        unpacked, sizeof(unpacked)));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_file_header_verify_crc(
        &header, header1, sizeof(header1), packed, sizeof(packed),
        NULL, sizeof(unpacked)));

    /* The packed sections and the header do not matter below version 8. */
    unpacked[17] ^= 0x40u;
    header.hdr1_pack_size = 99u;
    ASSERT_EQ(NMO_OK, nmo_file_header_verify_crc(
        &header, NULL, 0, NULL, 0, unpacked, sizeof(unpacked)));

    /* Version 1 and below are not checked. */
    nmo_file_header_t old = sample_header(1u);
    old.crc = 0x12345678u;
    ASSERT_EQ(NMO_OK, nmo_file_header_verify_crc(&old, NULL, 0, NULL, 0, NULL, 0));
}

TEST(header_gaps, version_8_checksum_is_the_header_and_packed_sections)
{
    const uint8_t header1[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t packed[5] = {9, 8, 7, 6, 5};
    const uint8_t unpacked[3] = {1, 2, 3};

    nmo_file_header_t header = sample_header(8u);
    header.hdr1_pack_size = sizeof(header1);
    header.hdr1_unpack_size = 40u;
    header.data_pack_size = sizeof(packed);
    header.data_unpack_size = 90u;

    const uint32_t via_version = nmo_file_crc_for_version(
        &header, header1, sizeof(header1), packed, sizeof(packed),
        unpacked, sizeof(unpacked));
    ASSERT_EQ(nmo_file_header_compute_crc(
        &header, header1, sizeof(header1), packed, sizeof(packed)), via_version);
    ASSERT_NE(nmo_file_data_crc(unpacked, sizeof(unpacked)), via_version);

    header.crc = via_version;
    ASSERT_EQ(NMO_OK, nmo_file_header_verify_crc(
        &header, header1, sizeof(header1), packed, sizeof(packed), NULL, 0));
    header.object_count ^= 1u;
    /* The count is part of the second header block the checksum covers. */
    ASSERT_EQ(NMO_ERR_CHECKSUM_MISMATCH, nmo_file_header_verify_crc(
        &header, header1, sizeof(header1), packed, sizeof(packed), NULL, 0));
}

/* ------------------------------------------------------------------------
 * Header1 plugin list over the corpus
 * ------------------------------------------------------------------------ */

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

typedef struct plugin_loop_stats {
    size_t files;
    size_t mismatches;
    size_t zero_guid_files;
    size_t unreadable;
} plugin_loop_stats_t;

/* Offset just past the plugin section that starts at pos. */
static size_t plugin_section_end(const uint8_t *data, size_t size, size_t pos)
{
    if (pos + 4u > size) return size + 1u;
    uint32_t categories = read_u32(data + pos);
    pos += 4u;
    for (uint32_t c = 0; c < categories; ++c) {
        if (pos + 8u > size) return size + 1u;
        uint32_t guids = read_u32(data + pos + 4u);
        pos += 8u;
        if ((size_t)guids > (size - pos) / 8u) return size + 1u;
        pos += (size_t)guids * 8u;
    }
    return pos;
}

static void check_plugin_list(const char *path, void *user)
{
    plugin_loop_stats_t *stats = (plugin_loop_stats_t *)user;
    FILE *file = fopen(path, "rb");
    if (file == NULL) { stats->unreadable++; return; }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = length > 0 ? (uint8_t *)malloc((size_t)length) : NULL;
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(bytes);
        stats->unreadable++;
        return;
    }
    fclose(file);

    nmo_io_interface_t *io = nmo_memory_io_open_read(bytes, (size_t)length);
    nmo_file_header_t header;
    if (io == NULL || nmo_file_header_parse(io, &header) != NMO_OK ||
        header.file_version < 8u || (size_t)length < 64u + header.hdr1_pack_size) {
        if (io != NULL) nmo_io_close(io);
        free(bytes);
        return; /* not a file this check applies to */
    }
    nmo_io_close(io);

    const uint8_t *header1 = bytes + 64;
    uint8_t *inflated = NULL;
    if (header.hdr1_pack_size != header.hdr1_unpack_size) {
        mz_ulong dest = header.hdr1_unpack_size;
        inflated = (uint8_t *)malloc(header.hdr1_unpack_size > 0 ? header.hdr1_unpack_size : 1u);
        if (inflated == NULL ||
            mz_uncompress(inflated, &dest, header1, header.hdr1_pack_size) != MZ_OK) {
            free(inflated);
            free(bytes);
            stats->unreadable++;
            return;
        }
        header1 = inflated;
    }
    const size_t header1_size = header.hdr1_unpack_size;

    /* The object table: id, class, file index, name length, name. */
    size_t plugin_pos = 0;
    int valid = 1;
    for (uint32_t i = 0; i < header.object_count && valid; ++i) {
        if (plugin_pos + 16u > header1_size) { valid = 0; break; }
        uint32_t name_length = read_u32(header1 + plugin_pos + 12u);
        plugin_pos += 16u;
        if ((size_t)name_length > header1_size - plugin_pos) { valid = 0; break; }
        plugin_pos += name_length;
    }
    const size_t plugin_end = valid ? plugin_section_end(header1, header1_size, plugin_pos) : 0u;
    if (!valid || plugin_end > header1_size) {
        free(inflated);
        free(bytes);
        stats->unreadable++;
        return;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 16);
    nmo_header1_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    parsed.object_count = header.object_count;
    void *rewritten = NULL;
    size_t rewritten_size = 0;
    const size_t plugin_size = plugin_end - plugin_pos;
    int equal = 0;
    if (arena != NULL &&
        nmo_header1_parse(header1, header1_size, &parsed, arena) == NMO_OK &&
        nmo_header1_serialize(&parsed, &rewritten, &rewritten_size, arena) == NMO_OK &&
        rewritten_size >= plugin_size + 8u) {
        /* Names can change length in a rewrite (embedded NULs), so find the
         * plugin section from the end: it is followed by the included-file
         * stub of two dwords. */
        const uint8_t *rewritten_plugins =
            (const uint8_t *)rewritten + rewritten_size - 8u - plugin_size;
        equal = memcmp(rewritten_plugins, header1 + plugin_pos, plugin_size) == 0;
    }
    stats->files++;
    if (!equal) stats->mismatches++;

    /* Count the files that carry an all-zero GUID: they must be in the list. */
    uint32_t categories = read_u32(header1 + plugin_pos);
    size_t scan = plugin_pos + 4u;
    int has_zero = 0;
    for (uint32_t c = 0; c < categories; ++c) {
        uint32_t guids = read_u32(header1 + scan + 4u);
        scan += 8u;
        for (uint32_t g = 0; g < guids; ++g) {
            if (read_u32(header1 + scan) == 0u && read_u32(header1 + scan + 4u) == 0u) has_zero = 1;
            scan += 8u;
        }
    }
    if (has_zero) stats->zero_guid_files++;

    if (arena != NULL) nmo_arena_destroy(arena);
    free(inflated);
    free(bytes);
}

TEST(header1_gaps, rewritten_plugin_list_matches_every_corpus_file)
{
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");
    plugin_loop_stats_t stats = {0};
    ASSERT_EQ(0, test_corpus_walk(NMO_TEST_DATA_DIR, check_plugin_list, &stats));
    ASSERT_GT(stats.files, 0u);
    ASSERT_EQ(0u, stats.mismatches);
    ASSERT_EQ(0u, stats.unreadable);
    /* The corpus holds files with a zero GUID inside a category; rewriting
     * keeps those entries. */
    ASSERT_GT(stats.zero_guid_files, 0u);
}

TEST(header1_gaps, plugin_entry_with_zero_guid_is_kept)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 12);
    ASSERT_NOT_NULL(arena);

    nmo_plugin_dep_t deps[3] = {
        {.guid = {0x01020304u, 0x05060708u}, .category = 4u, .version = 0u},
        {.guid = {0u, 0u}, .category = 4u, .version = 0u},
        {.guid = {0u, 0u}, .category = 2u, .version = 0u},
    };
    nmo_header1_t header;
    memset(&header, 0, sizeof(header));
    header.plugin_dep_count = 3u;
    header.plugin_deps = deps;

    void *data = NULL;
    size_t size = 0;
    ASSERT_EQ(NMO_OK, nmo_header1_serialize(&header, &data, &size, arena));

    nmo_header1_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    ASSERT_EQ(NMO_OK, nmo_header1_parse(data, size, &parsed, arena));
    ASSERT_EQ(3u, parsed.plugin_dep_count);
    ASSERT_EQ(4u, parsed.plugin_deps[0].category);
    ASSERT_EQ(0x01020304u, parsed.plugin_deps[0].guid.d1);
    ASSERT_EQ(4u, parsed.plugin_deps[1].category);
    ASSERT_EQ(0u, parsed.plugin_deps[1].guid.d1);
    ASSERT_EQ(0u, parsed.plugin_deps[1].guid.d2);
    ASSERT_EQ(2u, parsed.plugin_deps[2].category);
    nmo_arena_destroy(arena);
}

TEST(texture_gaps, reader_dimensions_are_required_only_for_nonempty_slots)
{
    const nmo_bitmap_slot_ids_t ids = {1u, 2u, 3u, 4u, 5u};
    static const int32_t dimensions[][2] = {{0, 0}, {-1, 1}, {1, -1}, {1, 1}};
    for (uint32_t count = 0; count <= 1u; ++count) {
        for (size_t i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
            nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
            ASSERT_NOT_NULL(arena);
            nmo_chunk_t *chunk = nmo_chunk_create(arena);
            ASSERT_NOT_NULL(chunk);
            ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, ids.reader));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, (int32_t)count));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, dimensions[i][0]));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, dimensions[i][1]));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 32));
            if (count != 0u) {
                ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
            }
            nmo_chunk_close(chunk);
            nmo_bitmap_slots_t bitmap = {0};
            const bool valid = count == 0u ||
                (dimensions[i][0] > 0 && dimensions[i][1] > 0);
            ASSERT_EQ(valid ? NMO_OK : NMO_ERR_INVALID_FORMAT,
                nmo_bitmap_slots_read(chunk, arena, &ids, &bitmap));
            nmo_texture_reader_slot_t empty = {0};
            bitmap.kind = CKTEXTURE_BITMAP_READER;
            bitmap.slot_count = count;
            bitmap.reader_width = dimensions[i][0];
            bitmap.reader_height = dimensions[i][1];
            bitmap.reader_slots = count == 0u ? NULL : &empty;
            ASSERT_EQ(valid ? NMO_OK : NMO_ERR_VALIDATION_FAILED,
                nmo_bitmap_slots_validate(&bitmap));
            nmo_texture_state_t texture;
            ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&texture, NULL, NULL));
            texture.bitmap_kind = CKTEXTURE_BITMAP_READER;
            texture.slot_count = bitmap.slot_count;
            texture.reader_width = bitmap.reader_width;
            texture.reader_height = bitmap.reader_height;
            texture.reader_slots = bitmap.reader_slots;
            ASSERT_EQ(valid ? NMO_OK : NMO_ERR_VALIDATION_FAILED,
                nmo_texture_vtable.validate(&texture, NULL, NULL));
            nmo_arena_destroy(arena);
        }
    }
}

TEST(material_gaps, legacy_color_prefixes_keep_constructor_defaults)
{
    static const uint32_t sizes[] = {0u, 1u, 4u, 7u, 8u, 12u, 15u, 16u, 17u};
    static const uint32_t expected[4][8] = {
        {0xFFB2B2B2u, 0xFFB2B2B2u, 0xFFFFB2B2u, 0xFFFF7FB2u,
         0xFFFF00B2u, 0xFFFF007Fu, 0xFFFF007Fu, 0x3FFF007Fu},
        {0xFF4C4C4Cu, 0xFF4C4C4Cu, 0xFFFF4C4Cu, 0xFFFF1F4Cu,
         0xFFFF004Cu, 0xFFFF007Fu, 0xFFFF007Fu, 0x3FFF007Fu},
        {0xFF7F7F7Fu, 0xFF7F7F7Fu, 0xFFFF7F7Fu, 0xFFFF7F7Fu,
         0xFFFF007Fu, 0xFFFF007Fu, 0xFFFF007Fu, 0x3FFF007Fu},
        {0xFF000000u, 0xFF000000u, 0xFFFF0000u, 0xFFFF0000u,
         0xFFFF0000u, 0xFFFF007Fu, 0xFFFF007Fu, 0x3FFF007Fu},
    };
    const uint32_t payload_words[] = {0x3F800000u, 0u, 0x3F000000u, 0x3E800000u, 0u};
    uint8_t payload[sizeof(payload_words)];
    for (size_t d = 0; d < sizeof(payload_words) / sizeof(payload_words[0]); ++d) {
        nmo_write_u32_le(payload + d * 4u, payload_words[d]);
    }
    /* Vary each color separately, then all four together (including the
     * minimum 17-DWORD legacy section with four empty color prefixes). */
    for (size_t selected = 0u; selected <= 4u; ++selected) {
        for (size_t c = 0u; c < sizeof(sizes) / sizeof(sizes[0]); ++c) {
            nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
            ASSERT_NOT_NULL(arena);
            nmo_chunk_t *chunk = nmo_chunk_create(arena);
            ASSERT_NOT_NULL(chunk);
            chunk->class_id = NMO_CID_MATERIAL;
            chunk->data_version = 4u;
            ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA));
            for (size_t color = 0u; color < 4u; ++color) {
                const uint32_t size = selected == color || selected == 4u ? sizes[c] : 16u;
                ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(chunk, payload, size));
            }
            ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 12.0f));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
            const uint32_t settings[] = {6u, 4u, 2u, 2u, 2u, 1u, 2u, 3u, 1u, 0x12345678u, 4u};
            for (size_t s = 0; s < sizeof(settings) / sizeof(settings[0]); ++s) {
                ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, settings[s]));
            }
            nmo_chunk_close(chunk);
            nmo_deserialize_context_t des = nmo_deserialize_context_create(
                arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
            nmo_material_state_t loaded;
            ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&loaded, NULL, NULL));
            loaded.specular_power = 42.0f;
            ASSERT_EQ(sizes[c] <= 16u ? NMO_OK : NMO_ERR_INVALID_FORMAT,
                nmo_material_deserialize(&loaded, chunk, NULL, &des));
            if (sizes[c] <= 16u) {
                const uint32_t actual[] = {loaded.diffuse_color, loaded.ambient_color,
                                          loaded.specular_color, loaded.emissive_color};
                for (size_t color = 0u; color < 4u; ++color) {
                    ASSERT_EQ(selected == color || selected == 4u ? expected[color][c] : 0x3FFF007Fu,
                              actual[color]);
                }
                ASSERT_EQ(12.0f, loaded.specular_power);
                ASSERT_EQ(0x12345678u, loaded.texture_border_color);
                nmo_serialize_context_t ser = nmo_serialize_context_create(
                    arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
                nmo_chunk_t *saved = nmo_chunk_create(arena);
                ASSERT_NOT_NULL(saved);
                saved->class_id = NMO_CID_MATERIAL;
                saved->data_version = 4u;
                ASSERT_EQ(NMO_OK, nmo_material_serialize(&loaded, saved, NULL, &ser));
                nmo_chunk_close(saved);
                nmo_material_state_t reloaded;
                ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&reloaded, NULL, NULL));
                ASSERT_EQ(NMO_OK, nmo_material_deserialize(&reloaded, saved, NULL, &des));
                ASSERT_EQ(loaded.diffuse_color, reloaded.diffuse_color);
                ASSERT_EQ(loaded.ambient_color, reloaded.ambient_color);
                ASSERT_EQ(loaded.specular_color, reloaded.specular_color);
                ASSERT_EQ(loaded.emissive_color, reloaded.emissive_color);
                nmo_material_vtable.destroy(&reloaded, NULL, NULL);
            } else {
                ASSERT_EQ(42.0f, loaded.specular_power);
            }
            nmo_material_vtable.destroy(&loaded, NULL, NULL);
            nmo_arena_destroy(arena);
        }
    }
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(material_gaps, legacy_color_prefixes_keep_constructor_defaults);
    REGISTER_TEST(texture_gaps, reader_dimensions_are_required_only_for_nonempty_slots);
    REGISTER_TEST(texture_gaps, desired_video_format_keeps_the_stored_value);
    REGISTER_TEST(texture_gaps, raw_slot_below_24_bits_per_pixel_decodes_like_32);
    REGISTER_TEST(mesh_gaps, weight_section_of_eight_bytes_is_the_uniform_form);
    REGISTER_TEST(mesh_gaps, weight_section_of_more_than_eight_bytes_is_a_buffer);
    REGISTER_TEST(mesh_gaps, face_masks_copy_every_available_face_in_order);
    REGISTER_TEST(data_gaps, manager_block_is_only_written_from_version_6);
    REGISTER_TEST(header_gaps, versions_5_to_8_parse_and_validate);
    REGISTER_TEST(header_gaps, data_checksum_is_adler32_from_zero);
    REGISTER_TEST(header_gaps, version_7_checksum_covers_the_unpacked_data_only);
    REGISTER_TEST(header_gaps, version_8_checksum_is_the_header_and_packed_sections);
    REGISTER_TEST(header1_gaps, rewritten_plugin_list_matches_every_corpus_file);
    REGISTER_TEST(header1_gaps, plugin_entry_with_zero_guid_is_kept);
TEST_MAIN_END()
