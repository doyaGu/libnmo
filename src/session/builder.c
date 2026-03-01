/**
 * @file builder.c
 * @brief Builder implementation for object/reference staging and save pipeline
 */

#include "session/nmo_builder.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_utils.h"
#include "core/nmo_error.h"
#include "io/nmo_io.h"
#include "io/nmo_io_file.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#define INITIAL_OBJECT_CAPACITY 64
#define REFERENCE_FLAG NMO_OBJECT_REFERENCE_FLAG
#define BUILDER_DEFAULT_FILE_VERSION 8u
#define BUILDER_DEFAULT_CK_VERSION 0x13022002u
#define BUILDER_DEFAULT_COMPRESSION_LEVEL 6

/**
 * File object entry (matches CKFileObject structure)
 */
typedef struct {
    nmo_object_id_t object_id;       /* Object ID */
    nmo_object_t *object_ptr;        /* Object pointer */
    nmo_class_id_t class_id;         /* Object class ID */
    nmo_manager_id_t manager_id;     /* Manager ID (if provided by caller) */
    uint32_t save_flags;             /* Save flags (reference-only) */
    uint32_t object_flags;           /* Object flags (copied metadata) */
    nmo_guid_t type_guid;            /* Optional type GUID metadata */
    const void *data;                /* Copied object payload */
    size_t size;                     /* Payload size in bytes */
    char *name;                      /* Object name (allocated from arena) */
    nmo_chunk_t *chunk;              /* Raw chunk wrapper for payload */
} nmo_file_object_t;

/**
 * Builder structure
 */
struct nmo_builder {
    nmo_arena_t *arena;              /* Memory arena */
    const nmo_type_runtime_t *type_runtime; /* Borrowed runtime view */
    char *output_path;               /* Output file path (arena-allocated) */
    
    /* File objects list */
    nmo_file_object_t *file_objects; /* Dynamic array of file objects */
    size_t object_count;             /* Current object count */
    size_t object_capacity;          /* Allocated capacity */
    
    /* Object tracking bitmasks (simple arrays) */
    uint32_t *saved_mask;            /* Objects already saved */
    uint32_t *referenced_mask;       /* Objects saved as references */
    size_t mask_size;                /* Size of mask arrays (in uint32_t units) */
    
    /* Statistics */
    nmo_object_id_t max_save_id;     /* Maximum object ID seen */
    int scene_saved;                 /* Whether scene/level was saved */

    /* Build outputs */
    nmo_object_desc_t *obj_descs;    /* Header1 object descriptors */
    void *header1_buffer;            /* Uncompressed Header1 */
    size_t header1_unpack_size;
    void *data_buffer;               /* Uncompressed data section */
    size_t data_unpack_size;
    void *header1_packed;            /* Packed Header1 */
    uint32_t header1_pack_size;
    void *data_packed;               /* Packed data section */
    uint32_t data_pack_size;
    nmo_file_header_t file_header;   /* Final file header */

    /* Save options (fixed defaults for builder) */
    uint32_t file_version;
    uint32_t file_version2;
    uint32_t ck_version;
    uint32_t product_version;
    uint32_t product_build;
    uint32_t write_mode;
    int compress_header;
    int compress_data;
    int compute_crc;
    int file_written;
    
    /* Error state */
    char error_msg[256];             /* Last error message */
    nmo_build_stage_t stage;         /* Current build stage */
};

static int builder_is_scene_or_level(
    const nmo_builder_t *builder,
    nmo_class_id_t class_id)
{
    if (builder == NULL || builder->type_runtime == NULL || builder->type_runtime->types == NULL) {
        return 0;
    }

    nmo_type_registry_t *registry = builder->type_runtime->types;
    const nmo_type_descriptor_t *child =
        nmo_type_registry_find_by_class_id_inherited(registry, (uint32_t)class_id);
    if (child == NULL) {
        return 0;
    }

    const nmo_type_descriptor_t *scene =
        nmo_type_registry_find_by_class_id(registry, (uint32_t)NMO_CID_SCENE);
    if (scene != NULL && nmo_type_is_derived_from(registry, child->id, scene->id)) {
        return 1;
    }

    const nmo_type_descriptor_t *level =
        nmo_type_registry_find_by_class_id(registry, (uint32_t)NMO_CID_LEVEL);
    if (level != NULL && nmo_type_is_derived_from(registry, child->id, level->id)) {
        return 1;
    }

    return 0;
}

static void builder_set_error(nmo_builder_t *builder, const char *fmt, ...) {
    if (builder == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(builder->error_msg, sizeof(builder->error_msg), fmt, args);
    va_end(args);
}

static nmo_status_t builder_build_header1(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (builder->header1_buffer != NULL) {
        NMO_RETURN_OK();
    }

    if (builder->object_count > UINT32_MAX) {
        builder_set_error(builder, "Object count exceeds 32-bit range");
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Object count overflow");
    }

    builder->obj_descs = NULL;
    if (builder->object_count > 0) {
        size_t desc_bytes = sizeof(nmo_object_desc_t) * builder->object_count;
        builder->obj_descs = (nmo_object_desc_t *)nmo_arena_alloc(
            builder->arena, desc_bytes, _Alignof(nmo_object_desc_t));
        if (builder->obj_descs == NULL) {
            builder_set_error(builder, "Failed to allocate object descriptors");
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Object descriptor allocation failed");
        }
        memset(builder->obj_descs, 0, desc_bytes);

        for (size_t i = 0; i < builder->object_count; i++) {
            nmo_file_object_t *file_obj = &builder->file_objects[i];
            nmo_object_desc_t *desc = &builder->obj_descs[i];
        desc->file_id = file_obj->object_id;
        desc->class_id = file_obj->class_id;
        desc->file_index = 0;
        desc->name = file_obj->name;
        desc->flags = 0;
        if ((file_obj->save_flags & REFERENCE_FLAG) != 0 ||
            (file_obj->object_flags & NMO_OBJECT_REFERENCE_FLAG) != 0) {
            desc->flags |= NMO_OBJECT_REFERENCE_FLAG;
        }
    }
    }

    nmo_header1_t hdr1;
    memset(&hdr1, 0, sizeof(hdr1));
    hdr1.object_count = (uint32_t)builder->object_count;
    hdr1.objects = builder->obj_descs;
    hdr1.plugin_dep_count = 0;
    hdr1.plugin_deps = NULL;
    hdr1.included_file_count = 0;
    hdr1.included_files = NULL;

    nmo_status_t result = nmo_header1_serialize(
        &hdr1, &builder->header1_buffer, &builder->header1_unpack_size, builder->arena);
    if (result != NMO_OK) {
        builder_set_error(builder, "Header1 serialize failed (%d)", result);
        return result;
    }

    uint32_t file_version = builder->file_version ? builder->file_version : BUILDER_DEFAULT_FILE_VERSION;
    size_t header_size = (file_version >= 5u) ? 64u : 32u;
    size_t offset = 0;
    if (!nmo_safe_add_size(header_size, builder->header1_unpack_size, &offset)) {
        builder_set_error(builder, "Header1 size overflow");
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Header1 size overflow");
    }

    for (size_t i = 0; i < builder->object_count; i++) {
        nmo_file_object_t *file_obj = &builder->file_objects[i];
        size_t chunk_size = file_obj->size;
        size_t entry_size = 4u;
        if (file_version < 7u) {
            entry_size += 4u;
        }

        if (offset > UINT32_MAX) {
            builder_set_error(builder, "File index exceeds 32-bit range");
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index overflow");
        }

        builder->obj_descs[i].file_index = (nmo_object_id_t)offset;

        size_t advance = 0;
        if (!nmo_safe_add_size(entry_size, chunk_size, &advance) ||
            !nmo_safe_add_size(offset, advance, &offset)) {
            builder_set_error(builder, "File index size overflow");
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "File index size overflow");
        }
    }

    result = nmo_header1_serialize(
        &hdr1, &builder->header1_buffer, &builder->header1_unpack_size, builder->arena);
    if (result != NMO_OK) {
        builder_set_error(builder, "Header1 serialize failed (%d)", result);
        return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t builder_build_data_section(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (builder->data_buffer != NULL) {
        NMO_RETURN_OK();
    }

    if (builder->object_count > UINT32_MAX) {
        builder_set_error(builder, "Object count exceeds 32-bit range");
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Object count overflow");
    }

    uint32_t file_version = builder->file_version ? builder->file_version : BUILDER_DEFAULT_FILE_VERSION;

    nmo_data_section_t data_section;
    memset(&data_section, 0, sizeof(data_section));
    data_section.manager_count = 0;
    data_section.managers = NULL;
    data_section.object_count = (uint32_t)builder->object_count;

    if (builder->object_count > 0) {
        data_section.objects = (nmo_object_data_t *)nmo_arena_alloc(
            builder->arena,
            sizeof(nmo_object_data_t) * builder->object_count,
            _Alignof(nmo_object_data_t));
        if (data_section.objects == NULL) {
            builder_set_error(builder, "Failed to allocate object data array");
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Object data allocation failed");
        }
        memset(data_section.objects, 0, sizeof(nmo_object_data_t) * builder->object_count);
    }

    for (size_t i = 0; i < builder->object_count; i++) {
        nmo_file_object_t *file_obj = &builder->file_objects[i];
        nmo_object_data_t *obj = &data_section.objects[i];

        obj->object_id = 0;
        if (file_version < 7u) {
            if (file_obj->object_id == 0) {
                builder_set_error(builder, "Legacy save requires non-zero object_id");
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Missing object_id for legacy save");
            }
            obj->object_id = file_obj->object_id;
        }

        obj->data_size = 0;
        obj->chunk = NULL;

        if (file_obj->size > 0) {
            if (file_obj->size > UINT32_MAX) {
                builder_set_error(builder, "Object payload exceeds 32-bit size");
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Object payload too large");
            }

            nmo_chunk_t *chunk = nmo_chunk_create(builder->arena);
            if (chunk == NULL) {
                builder_set_error(builder, "Failed to allocate chunk");
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Chunk allocation failed");
            }
            chunk->raw_data = file_obj->data;
            chunk->raw_size = file_obj->size;
            chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;

            file_obj->chunk = chunk;
            obj->chunk = chunk;
            obj->data_size = (uint32_t)file_obj->size;
        }
    }

    size_t data_size = nmo_data_section_calculate_size(&data_section, file_version, builder->arena);
    if (data_size == 0) {
        builder->data_buffer = NULL;
        builder->data_unpack_size = 0;
        NMO_RETURN_OK();
    }

    builder->data_buffer = nmo_arena_alloc(builder->arena, data_size, 16);
    if (builder->data_buffer == NULL) {
        builder_set_error(builder, "Failed to allocate data buffer");
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Data buffer allocation failed");
    }

    size_t bytes_written = 0;
    nmo_status_t result = nmo_data_section_serialize(
        &data_section, file_version, builder->data_buffer, data_size, &bytes_written, builder->arena);
    if (result != NMO_OK) {
        builder_set_error(builder, "Data section serialize failed (%d)", result);
        return result;
    }

    builder->data_unpack_size = bytes_written;
    NMO_RETURN_OK();
}

static nmo_status_t builder_compress_sections(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    builder->header1_packed = builder->header1_buffer;
    builder->header1_pack_size = (uint32_t)builder->header1_unpack_size;
    builder->data_packed = builder->data_buffer;
    builder->data_pack_size = (uint32_t)builder->data_unpack_size;

    if (builder->compress_header && builder->header1_unpack_size > 0) {
        mz_ulong bound = mz_compressBound((mz_ulong)builder->header1_unpack_size);
        void *compressed = nmo_arena_alloc(builder->arena, bound, 16);
        if (compressed == NULL) {
            builder_set_error(builder, "Header1 compression buffer allocation failed");
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Header1 compression buffer allocation failed");
        }

        mz_ulong dest_len = bound;
        int comp_result = mz_compress2(
            (unsigned char *)compressed,
            &dest_len,
            (const unsigned char *)builder->header1_buffer,
            (mz_ulong)builder->header1_unpack_size,
            BUILDER_DEFAULT_COMPRESSION_LEVEL);

        if (comp_result != MZ_OK) {
            builder_set_error(builder, "Header1 compression failed");
            NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Header1 compression failed");
        }

        if (dest_len < (mz_ulong)builder->header1_unpack_size) {
            builder->header1_packed = compressed;
            builder->header1_pack_size = (uint32_t)dest_len;
        }
    }

    if (builder->compress_data && builder->data_unpack_size > 0) {
        mz_ulong bound = mz_compressBound((mz_ulong)builder->data_unpack_size);
        void *compressed = nmo_arena_alloc(builder->arena, bound, 16);
        if (compressed == NULL) {
            builder_set_error(builder, "Data compression buffer allocation failed");
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Data compression buffer allocation failed");
        }

        mz_ulong dest_len = bound;
        int comp_result = mz_compress2(
            (unsigned char *)compressed,
            &dest_len,
            (const unsigned char *)builder->data_buffer,
            (mz_ulong)builder->data_unpack_size,
            BUILDER_DEFAULT_COMPRESSION_LEVEL);

        if (comp_result != MZ_OK) {
            builder_set_error(builder, "Data compression failed");
            NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Data compression failed");
        }

        if (dest_len < (mz_ulong)builder->data_unpack_size) {
            builder->data_packed = compressed;
            builder->data_pack_size = (uint32_t)dest_len;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t builder_build_file_header(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    nmo_file_header_t header;
    memset(&header, 0, sizeof(header));

    memcpy(header.signature, "Nemo Fi\0", 8);

    header.file_version = builder->file_version ? builder->file_version : BUILDER_DEFAULT_FILE_VERSION;
    header.file_version2 = builder->file_version2;
    header.ck_version = builder->ck_version ? builder->ck_version : BUILDER_DEFAULT_CK_VERSION;
    header.product_version = builder->product_version;
    header.product_build = builder->product_build;

    header.object_count = (uint32_t)builder->object_count;
    header.manager_count = 0;

    header.hdr1_pack_size = builder->header1_pack_size;
    header.hdr1_unpack_size = (uint32_t)builder->header1_unpack_size;
    header.data_pack_size = builder->data_pack_size;
    header.data_unpack_size = (uint32_t)builder->data_unpack_size;

    nmo_object_id_t max_file_id = 0;
    for (size_t i = 0; i < builder->object_count; i++) {
        if (builder->obj_descs[i].file_id > max_file_id) {
            max_file_id = builder->obj_descs[i].file_id;
        }
    }
    header.max_id_saved = max_file_id;

    uint32_t write_mode = builder->write_mode;
    if (builder->header1_packed != builder->header1_buffer) {
        write_mode |= NMO_FILE_WRITE_COMPRESS_HEADER;
    } else {
        write_mode &= ~NMO_FILE_WRITE_COMPRESS_HEADER;
    }
    if (builder->data_packed != builder->data_buffer) {
        write_mode |= NMO_FILE_WRITE_COMPRESS_DATA;
    } else {
        write_mode &= ~NMO_FILE_WRITE_COMPRESS_DATA;
    }
    header.file_write_mode = write_mode;

    uint32_t crc = 0;
    if (builder->compute_crc) {
        crc = mz_adler32(crc, (const uint8_t *)&header, 32);
        crc = mz_adler32(crc, (const uint8_t *)&header.object_count, 56);
        crc = mz_adler32(crc, (const uint8_t *)builder->header1_packed, builder->header1_pack_size);
        crc = mz_adler32(crc, (const uint8_t *)builder->data_packed, builder->data_pack_size);
    }
    header.crc = crc;

    builder->file_header = header;
    NMO_RETURN_OK();
}

static nmo_status_t builder_write_file(nmo_builder_t *builder) {
    if (builder == NULL || builder->output_path == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder output path");
    }

    if (builder->file_written) {
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(builder_build_header1(builder));
    NMO_RETURN_IF_ERROR(builder_build_data_section(builder));
    NMO_RETURN_IF_ERROR(builder_compress_sections(builder));
    NMO_RETURN_IF_ERROR(builder_build_file_header(builder));

    nmo_io_interface_t *io = nmo_file_io_open(builder->output_path, NMO_IO_WRITE | NMO_IO_CREATE);
    if (io == NULL) {
        builder_set_error(builder, "Failed to open output file");
        NMO_RETURN_ERROR(NMO_ERR_FILE_NOT_FOUND, NMO_SEVERITY_ERROR, "Cannot open output file");
    }

    nmo_status_t result = nmo_file_header_serialize(&builder->file_header, io);
    if (result != NMO_OK) {
        nmo_io_close(io);
        builder_set_error(builder, "Failed to write file header");
        return result;
    }

    if (builder->header1_pack_size > 0) {
        int write_result = nmo_io_write(io, builder->header1_packed, builder->header1_pack_size);
        if (write_result != NMO_OK) {
            nmo_io_close(io);
            builder_set_error(builder, "Header1 write failed");
            NMO_RETURN_ERROR(write_result, NMO_SEVERITY_ERROR, "Header1 write failed");
        }
    }

    if (builder->data_pack_size > 0) {
        int write_result = nmo_io_write(io, builder->data_packed, builder->data_pack_size);
        if (write_result != NMO_OK) {
            nmo_io_close(io);
            builder_set_error(builder, "Data section write failed");
            NMO_RETURN_ERROR(write_result, NMO_SEVERITY_ERROR, "Data section write failed");
        }
    }

    nmo_io_close(io);
    builder->file_written = 1;
    NMO_RETURN_OK();
}

/**
 * Helper: Check if bit is set in mask
 */
static int is_bit_set(const uint32_t *mask, size_t mask_size, nmo_object_id_t id) {
    size_t index = id / 32;
    size_t bit = id % 32;
    
    if (index >= mask_size) {
        return 0;
    }
    
    return (mask[index] & (1u << bit)) != 0;
}

/**
 * Helper: Set bit in mask
 */
static void set_bit(uint32_t *mask, size_t mask_size, nmo_object_id_t id) {
    size_t index = id / 32;
    size_t bit = id % 32;
    
    if (index >= mask_size) {
        return;
    }
    
    mask[index] |= (1u << bit);
}

/**
 * Helper: ensure bitmasks can represent a given object ID
 */
static int ensure_mask_capacity(nmo_builder_t *builder, nmo_object_id_t id) {
    size_t required_index = ((size_t)id) / 32u;
    if (required_index < builder->mask_size) {
        return NMO_OK;
    }

    size_t new_mask_size = builder->mask_size == 0 ? 1 : builder->mask_size;
    while (required_index >= new_mask_size) {
        if (new_mask_size > (SIZE_MAX / 2u)) {
            return NMO_ERR_NOMEM;
        }
        new_mask_size *= 2u;
    }

    if (new_mask_size > (SIZE_MAX / sizeof(uint32_t))) {
        return NMO_ERR_NOMEM;
    }
    size_t bytes = new_mask_size * sizeof(uint32_t);

    uint32_t *new_saved = (uint32_t *) nmo_arena_alloc(
        builder->arena, bytes, _Alignof(uint32_t));
    uint32_t *new_referenced = (uint32_t *) nmo_arena_alloc(
        builder->arena, bytes, _Alignof(uint32_t));
    if (new_saved == NULL || new_referenced == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(new_saved, 0, bytes);
    memset(new_referenced, 0, bytes);

    if (builder->saved_mask != NULL && builder->mask_size > 0) {
        memcpy(new_saved, builder->saved_mask, builder->mask_size * sizeof(uint32_t));
    }
    if (builder->referenced_mask != NULL && builder->mask_size > 0) {
        memcpy(new_referenced, builder->referenced_mask, builder->mask_size * sizeof(uint32_t));
    }

    builder->saved_mask = new_saved;
    builder->referenced_mask = new_referenced;
    builder->mask_size = new_mask_size;

    return NMO_OK;
}

/**
 * Helper: Grow file objects array
 */
static int grow_file_objects(nmo_builder_t *builder) {
    size_t new_capacity = builder->object_capacity * 2;
    if (new_capacity <= builder->object_capacity) {
        return NMO_ERR_NOMEM; /* Overflow in capacity doubling */
    }
    size_t alloc_size = new_capacity * sizeof(nmo_file_object_t);
    if (alloc_size / sizeof(nmo_file_object_t) != new_capacity) {
        return NMO_ERR_NOMEM; /* Overflow in byte-size computation */
    }
    // Use arena allocation instead of realloc to avoid mixed allocation patterns
    // Note: Old array is leaked (arena allocator limitation), but this is acceptable
    // since the arena will be cleaned up when the builder is destroyed.
    nmo_file_object_t *new_objects = (nmo_file_object_t *) nmo_arena_alloc(
        builder->arena,
        alloc_size,
        _Alignof(nmo_file_object_t)
    );

    if (new_objects == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy existing objects
    if (builder->file_objects != NULL && builder->object_count > 0) {
        memcpy(new_objects, builder->file_objects,
               builder->object_count * sizeof(nmo_file_object_t));
    }

    // Clear new entries
    memset(new_objects + builder->object_count, 0,
           (new_capacity - builder->object_count) * sizeof(nmo_file_object_t));

    builder->file_objects = new_objects;
    builder->object_capacity = new_capacity;

    return NMO_OK;
}

/**
 * Create builder
 */
nmo_builder_t *nmo_builder_create(const char *output_path, const nmo_type_runtime_t *type_runtime) {
    if (type_runtime == NULL || type_runtime->types == NULL || type_runtime->ops == NULL) {
        return NULL;
    }

    /* Create arena first for all allocations */
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        return NULL;
    }

    /* Allocate builder structure from arena */
    nmo_builder_t *builder = (nmo_builder_t *) nmo_arena_alloc(
        arena, sizeof(nmo_builder_t), _Alignof(nmo_builder_t));
    if (builder == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    memset(builder, 0, sizeof(nmo_builder_t));
    builder->arena = arena;
    builder->type_runtime = type_runtime;
    builder->output_path = NULL;

    /* Allocate file objects array from arena */
    builder->file_objects = (nmo_file_object_t *) nmo_arena_alloc(
        arena,
        INITIAL_OBJECT_CAPACITY * sizeof(nmo_file_object_t),
        _Alignof(nmo_file_object_t)
    );
    if (builder->file_objects == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    builder->object_capacity = INITIAL_OBJECT_CAPACITY;
    builder->object_count = 0;

    /* Allocate bitmasks from arena (support up to 1024 objects initially) */
    builder->mask_size = 32;  /* 32 * 32 bits = 1024 objects */
    builder->saved_mask = (uint32_t *) nmo_arena_alloc(
        arena, builder->mask_size * sizeof(uint32_t), _Alignof(uint32_t));
    builder->referenced_mask = (uint32_t *) nmo_arena_alloc(
        arena, builder->mask_size * sizeof(uint32_t), _Alignof(uint32_t));

    if (builder->saved_mask == NULL || builder->referenced_mask == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    memset(builder->saved_mask, 0, builder->mask_size * sizeof(uint32_t));
    memset(builder->referenced_mask, 0, builder->mask_size * sizeof(uint32_t));

    builder->max_save_id = 0;
    builder->scene_saved = 0;
    builder->stage = NMO_BUILD_STAGE_INIT;
    builder->error_msg[0] = '\0';

    builder->file_version = BUILDER_DEFAULT_FILE_VERSION;
    builder->file_version2 = 0;
    builder->ck_version = BUILDER_DEFAULT_CK_VERSION;
    builder->product_version = 0;
    builder->product_build = 0;
    builder->write_mode = 0;
    builder->compress_header = 1;
    builder->compress_data = 1;
    builder->compute_crc = 1;
    builder->file_written = 0;

    if (output_path != NULL) {
        size_t path_len = strlen(output_path);
        char *path_copy = (char *)nmo_arena_alloc(arena, path_len + 1, 1);
        if (path_copy == NULL) {
            nmo_arena_destroy(arena);
            return NULL;
        }
        memcpy(path_copy, output_path, path_len + 1);
        builder->output_path = path_copy;
    }

    return builder;
}

nmo_status_t nmo_builder_set_file_version(
    nmo_builder_t *builder,
    uint32_t file_version,
    uint32_t file_version2)
{
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (file_version < 2u || file_version > 9u) {
        builder_set_error(builder, "Unsupported file version (must be 2-9)");
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR, "Unsupported file version");
    }

    if (builder->header1_buffer != NULL || builder->data_buffer != NULL || builder->file_written) {
        builder_set_error(builder, "Cannot change file version after build has started");
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Builder already started");
    }

    builder->file_version = file_version;
    builder->file_version2 = file_version2;
    NMO_RETURN_OK();
}

nmo_status_t nmo_builder_set_write_mode(
    nmo_builder_t *builder,
    uint32_t write_mode)
{
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (builder->file_written) {
        builder_set_error(builder, "Cannot change write mode after file is written");
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Builder already completed");
    }

    builder->write_mode = write_mode;
    builder->compress_header = (write_mode & NMO_FILE_WRITE_COMPRESS_HEADER) != 0;
    builder->compress_data = (write_mode & NMO_FILE_WRITE_COMPRESS_DATA) != 0;

    NMO_RETURN_OK();
}

nmo_status_t nmo_builder_set_compression(
    nmo_builder_t *builder,
    int compress_header,
    int compress_data)
{
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (builder->file_written) {
        builder_set_error(builder, "Cannot change compression after file is written");
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Builder already completed");
    }

    builder->compress_header = (compress_header != 0);
    builder->compress_data = (compress_data != 0);

    builder->write_mode = (builder->write_mode & ~NMO_FILE_WRITE_COMPRESS_BOTH)
        | (builder->compress_header ? NMO_FILE_WRITE_COMPRESS_HEADER : 0)
        | (builder->compress_data ? NMO_FILE_WRITE_COMPRESS_DATA : 0);

    NMO_RETURN_OK();
}

/**
 * Destroy builder
 */
void nmo_builder_destroy(nmo_builder_t *builder) {
    if (builder == NULL) {
        return;
    }

    /* Since we use arena allocation for everything, just destroy the arena */
    nmo_arena_t *arena = builder->arena;
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    /* No need to free builder - it was allocated from the arena */
}

/**
 * Add object as reference
 * 
 * Based on CKFile::SaveObjectAsReference (reference/src/CKFile.cpp:810-838)
 */
nmo_status_t nmo_builder_add_object_as_reference(nmo_builder_t *builder, nmo_object_t *object) {
    if (builder == NULL || object == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }
    
    nmo_object_id_t obj_id = nmo_object_get_id(object);
    if (obj_id == NMO_OBJECT_ID_NONE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid object ID");
    }

    int mask_result = ensure_mask_capacity(builder, obj_id);
    if (mask_result != NMO_OK) {
        NMO_RETURN_ERROR(mask_result, NMO_SEVERITY_ERROR, "Failed to grow object bitmasks");
    }
    
    /* Check if already saved or referenced (matches m_AlreadySavedMask/m_AlreadyReferencedMask) */
    if (is_bit_set(builder->saved_mask, builder->mask_size, obj_id) ||
        is_bit_set(builder->referenced_mask, builder->mask_size, obj_id)) {
        NMO_RETURN_OK();  /* Already processed, not an error */
    }
    
    /* Mark as referenced (matches m_AlreadyReferencedMask.Set) */
    set_bit(builder->referenced_mask, builder->mask_size, obj_id);
    
    /* Update max ID (matches obj->GetID() > m_SaveIDMax check) */
    if (obj_id > builder->max_save_id) {
        builder->max_save_id = obj_id;
    }
    
    /* Grow array if needed */
    if (builder->object_count >= builder->object_capacity) {
        int result = grow_file_objects(builder);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to grow file objects array");
        }
    }
    
    /* Create file object entry (matches CKFileObject structure) */
    nmo_file_object_t *file_obj = &builder->file_objects[builder->object_count];
    
    file_obj->object_id = obj_id;
    file_obj->object_ptr = object;
    file_obj->class_id = nmo_object_get_class_id(object);
    file_obj->manager_id = 0;
    file_obj->save_flags = REFERENCE_FLAG;  /* Mark as reference (sign bit) */
    file_obj->object_flags = object->flags | NMO_OBJECT_REFERENCE_FLAG;
    file_obj->type_guid = nmo_object_get_type_guid(object);
    file_obj->data = NULL;
    file_obj->size = 0;
    file_obj->chunk = NULL;
    
    /* Copy name from object (matches CKStrdup(obj->GetName())) */
    const char *obj_name = nmo_object_get_name(object);
    if (obj_name != NULL && obj_name[0] != '\0') {
        size_t name_len = strlen(obj_name);
        char *name_copy = (char *) nmo_arena_alloc(builder->arena, name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, obj_name, name_len + 1);
            file_obj->name = name_copy;
        } else {
            file_obj->name = NULL;
        }
    } else {
        file_obj->name = NULL;
    }
    
    builder->object_count++;
    
    /* Track scene/level objects for save ordering via type-runtime hierarchy checks */
    if (builder_is_scene_or_level(builder, file_obj->class_id)) {
        /* Scene/level object detected - may need special handling in save pipeline */
    }

    builder->error_msg[0] = '\0';
    NMO_RETURN_OK();
}

/**
 * Builder pipeline API
 */

nmo_status_t nmo_builder_start(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }
    builder->stage = NMO_BUILD_STAGE_HEADER;
    builder->error_msg[0] = '\0';
    NMO_RETURN_OK();
}

nmo_build_stage_t nmo_builder_build_next_stage(nmo_builder_t *builder) {
    if (builder == NULL) {
        return NMO_BUILD_STAGE_COMPLETED;
    }

    switch (builder->stage) {
        case NMO_BUILD_STAGE_INIT:
            builder->stage = NMO_BUILD_STAGE_HEADER;
            break;
        case NMO_BUILD_STAGE_HEADER:
            if (builder_build_header1(builder) == NMO_OK) {
                builder->stage = NMO_BUILD_STAGE_HEADER1;
            }
            break;
        case NMO_BUILD_STAGE_HEADER1:
            if (builder_build_data_section(builder) == NMO_OK) {
                builder->stage = NMO_BUILD_STAGE_OBJECTS;
            }
            break;
        case NMO_BUILD_STAGE_OBJECTS:
            if (builder_write_file(builder) == NMO_OK) {
                builder->stage = NMO_BUILD_STAGE_COMPLETED;
            }
            break;
        default:
            builder->stage = NMO_BUILD_STAGE_COMPLETED;
            break;
    }
    
    return builder->stage;
}

nmo_build_stage_t nmo_builder_get_current_stage(const nmo_builder_t *builder) {
    return builder ? builder->stage : NMO_BUILD_STAGE_COMPLETED;
}

nmo_status_t nmo_builder_add_object_ex(
    nmo_builder_t *builder,
    uint32_t object_id,
    uint32_t manager_id,
    uint32_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    uint32_t flags,
    const void *data,
    size_t size)
{
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (object_id == NMO_OBJECT_ID_NONE || object_id == NMO_OBJECT_ID_INVALID) {
        snprintf(builder->error_msg, sizeof(builder->error_msg),
            "Invalid object_id %u", (unsigned)object_id);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid object_id");
    }

    if ((flags & NMO_OBJECT_REFERENCE_FLAG) != 0 && size > 0) {
        snprintf(builder->error_msg, sizeof(builder->error_msg),
            "reference object cannot include payload data");
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Reference object payload not allowed");
    }

    if (size > 0 && data == NULL) {
        snprintf(builder->error_msg, sizeof(builder->error_msg),
            "data must be non-NULL when size=%zu", size);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid payload");
    }

    nmo_object_id_t obj_id = (nmo_object_id_t)object_id;
    int mask_result = ensure_mask_capacity(builder, obj_id);
    if (mask_result != NMO_OK) {
        snprintf(builder->error_msg, sizeof(builder->error_msg),
            "Failed to grow object bitmasks for object_id %u", (unsigned)object_id);
        NMO_RETURN_ERROR(mask_result, NMO_SEVERITY_ERROR, "Failed to grow object bitmasks");
    }

    if (is_bit_set(builder->saved_mask, builder->mask_size, obj_id) ||
        is_bit_set(builder->referenced_mask, builder->mask_size, obj_id)) {
        builder->error_msg[0] = '\0';
        NMO_RETURN_OK();
    }

    if (builder->object_count >= builder->object_capacity) {
        int result = grow_file_objects(builder);
        if (result != NMO_OK) {
            snprintf(builder->error_msg, sizeof(builder->error_msg),
                "Failed to grow file object list");
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to grow file objects array");
        }
    }

    nmo_file_object_t *file_obj = &builder->file_objects[builder->object_count];
    memset(file_obj, 0, sizeof(*file_obj));
    file_obj->object_id = obj_id;
    file_obj->object_ptr = NULL;
    file_obj->class_id = (nmo_class_id_t)class_id;
    file_obj->manager_id = (nmo_manager_id_t)manager_id;
    file_obj->save_flags = ((flags & NMO_OBJECT_REFERENCE_FLAG) != 0) ? REFERENCE_FLAG : 0;
    file_obj->object_flags = flags;
    file_obj->type_guid = type_guid;
    file_obj->name = NULL;
    file_obj->chunk = NULL;

    if (name != NULL && name[0] != '\0') {
        size_t name_len = strlen(name);
        char *name_copy = (char *) nmo_arena_alloc(builder->arena, name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, name, name_len + 1);
            file_obj->name = name_copy;
        }
    }

    if (size > 0) {
        void *data_copy = nmo_arena_alloc(builder->arena, size, 1);
        if (data_copy == NULL) {
            snprintf(builder->error_msg, sizeof(builder->error_msg),
                "Failed to copy payload (%zu bytes)", size);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy object payload");
        }
        memcpy(data_copy, data, size);
        file_obj->data = data_copy;
        file_obj->size = size;
    }

    if ((flags & NMO_OBJECT_REFERENCE_FLAG) != 0) {
        set_bit(builder->referenced_mask, builder->mask_size, obj_id);
    } else {
        set_bit(builder->saved_mask, builder->mask_size, obj_id);
    }
    if (obj_id > builder->max_save_id) {
        builder->max_save_id = obj_id;
    }

    builder->object_count++;
    builder->error_msg[0] = '\0';
    NMO_RETURN_OK();
}

nmo_status_t nmo_builder_add_object(
    nmo_builder_t *builder, uint32_t object_id, uint32_t manager_id, const void *data, size_t size) {
    return nmo_builder_add_object_ex(
        builder,
        object_id,
        manager_id,
        (uint32_t)NMO_CLASS_ID_INVALID,
        NULL,
        (nmo_guid_t){0, 0},
        0,
        data,
        size);
}

nmo_status_t nmo_builder_finish(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }

    if (builder->stage == NMO_BUILD_STAGE_INIT) {
        nmo_status_t start_result = nmo_builder_start(builder);
        if (start_result != NMO_OK) {
            return start_result;
        }
    }

    while (builder->stage != NMO_BUILD_STAGE_COMPLETED) {
        nmo_build_stage_t stage = nmo_builder_build_next_stage(builder);
        if (stage == NMO_BUILD_STAGE_COMPLETED) {
            break;
        }
        if (builder->error_msg[0] != '\0') {
            return NMO_ERR_INTERNAL;
        }
    }

    NMO_RETURN_OK();
}

const char *nmo_builder_get_error(const nmo_builder_t *builder) {
    if (builder == NULL || builder->error_msg[0] == '\0') {
        return NULL;
    }
    return builder->error_msg;
}

uint32_t nmo_builder_get_object_count(const nmo_builder_t *builder) {
    return builder ? (uint32_t)builder->object_count : 0;
}

int nmo_builder_is_complete(const nmo_builder_t *builder) {
    return builder ? (builder->stage == NMO_BUILD_STAGE_COMPLETED) : 1;
}
