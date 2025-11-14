#include "io/nmo_io_stream.h"

#include "io/nmo_io_file.h"
#include "io/nmo_io.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_manager.h"
#include "format/nmo_data.h"
#include "core/nmo_utils.h"
#include "core/nmo_allocator.h"
#include <miniz.h>
#include <string.h>
#include <stdlib.h>
#include <stdalign.h>

#define STREAM_DEFAULT_BUFFER_SIZE (64 * 1024)

struct nmo_stream_reader {
    nmo_io_interface_t *io;
    nmo_file_header_t header;
    nmo_header1_t header1;
    nmo_manager_data_t *managers;
    uint32_t manager_count;

    nmo_arena_t *arena;
    int owns_arena;

    size_t buffer_size;
    uint8_t *out_buffer;
    size_t out_pos;
    size_t out_filled;

    uint8_t *in_buffer;
    size_t compressed_remaining;
    size_t uncompressed_remaining;
    int data_compressed;
    int stream_finished;

    mz_stream zstream;
    int zstream_initialized;

    uint32_t next_object_index;
    uint32_t objects_total;
};

struct nmo_stream_writer {
    nmo_io_interface_t *io;
    nmo_file_header_t header;

    size_t buffer_size;
    uint8_t *out_buffer;
    int compress_data;
    int compression_level;

    mz_stream zstream;
    int zstream_initialized;

    size_t data_uncompressed_bytes;
    size_t data_compressed_bytes;
    uint32_t objects_written;

    nmo_arena_t *scratch_arena;
    int owns_arena;

    int finalized;
};

// =============================================================================
// Reader helpers
// =============================================================================

static nmo_result_t reader_fill_output(nmo_stream_reader_t *reader) {
    if (reader->stream_finished) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_INFO,
                                          "Data section fully consumed"));
    }

    reader->out_pos = 0;
    reader->out_filled = 0;

    if (!reader->data_compressed) {
        if (reader->compressed_remaining == 0) {
            reader->stream_finished = 1;
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_INFO,
                                              "No more uncompressed data"));
        }

        size_t to_read = reader->buffer_size;
        if (to_read > reader->compressed_remaining) {
            to_read = reader->compressed_remaining;
        }

        size_t bytes_read = 0;
        int io_result = nmo_io_read(reader->io, reader->out_buffer, to_read, &bytes_read);
        if (io_result != NMO_OK || bytes_read == 0) {
            reader->stream_finished = 1;
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_READ_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to stream data section"));
        }

        reader->out_filled = bytes_read;
        reader->compressed_remaining -= bytes_read;
        if (reader->compressed_remaining == 0) {
            reader->stream_finished = 1;
        }
        return nmo_result_ok();
    }

    if (!reader->zstream_initialized) {
        memset(&reader->zstream, 0, sizeof(reader->zstream));
        int mz_result = mz_inflateInit(&reader->zstream);
        if (mz_result != MZ_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to initialize inflate stream"));
        }
        reader->zstream_initialized = 1;
    }

    reader->zstream.next_out = reader->out_buffer;
    reader->zstream.avail_out = (unsigned int)reader->buffer_size;

    while (reader->zstream.avail_out > 0) {
        if (reader->zstream.avail_in == 0 && reader->compressed_remaining > 0) {
            size_t to_read = reader->buffer_size;
            if (to_read > reader->compressed_remaining) {
                to_read = reader->compressed_remaining;
            }

            size_t bytes_read = 0;
            int io_result = nmo_io_read(reader->io, reader->in_buffer, to_read, &bytes_read);
            if (io_result != NMO_OK || bytes_read == 0) {
                reader->stream_finished = 1;
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_READ_FILE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to read compressed data"));
            }

            reader->compressed_remaining -= bytes_read;
            reader->zstream.next_in = reader->in_buffer;
            reader->zstream.avail_in = (unsigned int)bytes_read;
        }

        int status = mz_inflate(&reader->zstream, MZ_NO_FLUSH);
        if (status == MZ_STREAM_END) {
            reader->stream_finished = 1;
            break;
        } else if (status != MZ_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                              NMO_SEVERITY_ERROR,
                                              "Inflate failed"));
        }

        if (reader->zstream.avail_in == 0 && reader->compressed_remaining == 0) {
            if (reader->zstream.avail_out == reader->buffer_size) {
                reader->stream_finished = 1;
                break;
            }
        }

        if (reader->zstream.avail_out == 0) {
            break;
        }

        if (reader->compressed_remaining == 0 && reader->zstream.avail_in == 0) {
            break;
        }
    }

    reader->out_filled = reader->buffer_size - reader->zstream.avail_out;
    if (reader->out_filled == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_INFO,
                                          "No more decompressed bytes available"));
    }

    return nmo_result_ok();
}

static nmo_result_t reader_copy_bytes(nmo_stream_reader_t *reader, void *dst, size_t size) {
    if (size > reader->uncompressed_remaining) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_INFO,
                                          "Attempted to read beyond data section"));
    }

    uint8_t *out = (uint8_t *)dst;
    while (size > 0) {
        size_t available = reader->out_filled - reader->out_pos;
        if (available == 0) {
            reader->out_pos = 0;
            reader->out_filled = 0;
            nmo_result_t fill_result = reader_fill_output(reader);
            if (fill_result.code != NMO_OK) {
                return fill_result;
            }
            available = reader->out_filled;
        }

        size_t chunk = NMO_MIN(size, available);
        memcpy(out, reader->out_buffer + reader->out_pos, chunk);
        reader->out_pos += chunk;
        out += chunk;
        size -= chunk;
        reader->uncompressed_remaining -= chunk;
    }
    return nmo_result_ok();
}

static nmo_result_t reader_skip_bytes(nmo_stream_reader_t *reader, size_t size) {
    if (size > reader->uncompressed_remaining) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_INFO,
                                          "Attempted to skip beyond data section"));
    }

    while (size > 0) {
        size_t available = reader->out_filled - reader->out_pos;
        if (available == 0) {
            reader->out_pos = 0;
            reader->out_filled = 0;
            nmo_result_t fill_result = reader_fill_output(reader);
            if (fill_result.code != NMO_OK) {
                return fill_result;
            }
            available = reader->out_filled;
        }

        size_t chunk = NMO_MIN(size, available);
        reader->out_pos += chunk;
        size -= chunk;
        reader->uncompressed_remaining -= chunk;
    }
    return nmo_result_ok();
}

static nmo_result_t reader_read_u32(nmo_stream_reader_t *reader, uint32_t *value) {
    uint32_t tmp = 0;
    nmo_result_t result = reader_copy_bytes(reader, &tmp, sizeof(uint32_t));
    if (result.code != NMO_OK) {
        return result;
    }
    *value = nmo_read_u32_le((const uint8_t *)&tmp);
    return nmo_result_ok();
}

static nmo_result_t reader_load_managers(nmo_stream_reader_t *reader) {
    if (reader->header.file_version < 6 || reader->header.manager_count == 0) {
        reader->managers = NULL;
        reader->manager_count = 0;
        return nmo_result_ok();
    }

    size_t total = reader->header.manager_count;
    reader->manager_count = reader->header.manager_count;
    reader->managers = (nmo_manager_data_t *)nmo_arena_alloc(
        reader->arena,
        sizeof(nmo_manager_data_t) * total,
        _Alignof(nmo_manager_data_t));
    if (reader->managers == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate manager cache"));
    }
    memset(reader->managers, 0, sizeof(nmo_manager_data_t) * total);

    for (uint32_t i = 0; i < reader->manager_count; ++i) {
        nmo_manager_data_t *mgr = &reader->managers[i];
        nmo_result_t res = reader_copy_bytes(reader, &mgr->guid, sizeof(nmo_guid_t));
        if (res.code != NMO_OK) {
            return res;
        }

        uint32_t data_size = 0;
        res = reader_read_u32(reader, &data_size);
        if (res.code != NMO_OK) {
            return res;
        }
        mgr->data_size = data_size;

        if (data_size > 0) {
            void *buffer = nmo_arena_alloc(reader->arena, data_size, 4);
            if (buffer == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to allocate manager blob"));
            }
            res = reader_copy_bytes(reader, buffer, data_size);
            if (res.code != NMO_OK) {
                return res;
            }

            mgr->chunk = nmo_chunk_create(reader->arena);
            if (mgr->chunk == NULL) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to create manager chunk"));
            }

            res = nmo_chunk_parse(mgr->chunk, buffer, data_size);
            if (res.code != NMO_OK) {
                return res;
            }
        }
    }

    return nmo_result_ok();
}

nmo_stream_reader_t *nmo_stream_reader_create(const char *path,
                                              const nmo_stream_reader_config_t *config) {
    if (path == NULL) {
        return NULL;
    }

    nmo_stream_reader_t *reader = (nmo_stream_reader_t *)malloc(sizeof(nmo_stream_reader_t));
    if (reader == NULL) {
        return NULL;
    }
    memset(reader, 0, sizeof(*reader));

    reader->buffer_size = (config && config->buffer_size) ? config->buffer_size : STREAM_DEFAULT_BUFFER_SIZE;
    reader->arena = config && config->arena ? config->arena : nmo_arena_create(config ? config->allocator : NULL,
                                                                               reader->buffer_size * 2);
    reader->owns_arena = (config == NULL || config->arena == NULL) ? 1 : 0;

    if (reader->arena == NULL) {
        free(reader);
        return NULL;
    }

    reader->io = nmo_file_io_open(path, NMO_IO_READ);
    if (reader->io == NULL) {
        if (reader->owns_arena) {
            nmo_arena_destroy(reader->arena);
        }
        free(reader);
        return NULL;
    }

    nmo_result_t result = nmo_file_header_parse(reader->io, &reader->header);
    if (result.code != NMO_OK) {
        nmo_io_close(reader->io);
        if (reader->owns_arena) {
            nmo_arena_destroy(reader->arena);
        }
        free(reader);
        return NULL;
    }

    result = nmo_file_header_validate(&reader->header);
    if (result.code != NMO_OK) {
        nmo_io_close(reader->io);
        if (reader->owns_arena) {
            nmo_arena_destroy(reader->arena);
        }
        free(reader);
        return NULL;
    }

    memset(&reader->header1, 0, sizeof(reader->header1));
    reader->header1.object_count = reader->header.object_count;

    if (reader->header.hdr1_pack_size > 0 && reader->header.hdr1_unpack_size > 0) {
        void *packed = nmo_arena_alloc(reader->arena, reader->header.hdr1_pack_size, 16);
        if (packed == NULL) {
            nmo_stream_reader_destroy(reader);
            return NULL;
        }

        size_t bytes_read = 0;
        int read_status = nmo_io_read(reader->io, packed, reader->header.hdr1_pack_size, &bytes_read);
        if (read_status != NMO_OK || bytes_read != reader->header.hdr1_pack_size) {
            nmo_stream_reader_destroy(reader);
            return NULL;
        }

        void *hdr1_buffer = packed;
        if (reader->header.hdr1_pack_size != reader->header.hdr1_unpack_size) {
            hdr1_buffer = nmo_arena_alloc(reader->arena, reader->header.hdr1_unpack_size, 16);
            if (hdr1_buffer == NULL) {
                nmo_stream_reader_destroy(reader);
                return NULL;
            }

            mz_ulong dest_len = reader->header.hdr1_unpack_size;
            int mz_status = mz_uncompress((unsigned char *)hdr1_buffer, &dest_len,
                                          (const unsigned char *)packed,
                                          reader->header.hdr1_pack_size);
            if (mz_status != MZ_OK || dest_len != reader->header.hdr1_unpack_size) {
                nmo_stream_reader_destroy(reader);
                return NULL;
            }
        }

        result = nmo_header1_parse(hdr1_buffer, reader->header.hdr1_unpack_size,
                                   &reader->header1, reader->arena);
        if (result.code != NMO_OK) {
            nmo_stream_reader_destroy(reader);
            return NULL;
        }
    }

    reader->data_compressed = (reader->header.data_pack_size != reader->header.data_unpack_size);
    reader->compressed_remaining = reader->header.data_pack_size;
    reader->uncompressed_remaining = reader->header.data_unpack_size;
    reader->objects_total = reader->header.object_count;

    reader->out_buffer = (uint8_t *)malloc(reader->buffer_size);
    if (reader->out_buffer == NULL) {
        nmo_stream_reader_destroy(reader);
        return NULL;
    }

    if (reader->data_compressed) {
        reader->in_buffer = (uint8_t *)malloc(reader->buffer_size);
        if (reader->in_buffer == NULL) {
            nmo_stream_reader_destroy(reader);
            return NULL;
        }
    }

    result = reader_load_managers(reader);
    if (result.code != NMO_OK) {
        nmo_stream_reader_destroy(reader);
        return NULL;
    }

    return reader;
}

void nmo_stream_reader_destroy(nmo_stream_reader_t *reader) {
    if (reader == NULL) {
        return;
    }

    if (reader->zstream_initialized) {
        mz_inflateEnd(&reader->zstream);
    }

    if (reader->io != NULL) {
        nmo_io_close(reader->io);
    }

    if (reader->data_compressed && reader->in_buffer != NULL) {
        free(reader->in_buffer);
    }

    if (reader->out_buffer != NULL) {
        free(reader->out_buffer);
    }

    if (reader->owns_arena && reader->arena != NULL) {
        nmo_arena_destroy(reader->arena);
    }

    free(reader);
}

const nmo_file_header_t *nmo_stream_reader_get_header(const nmo_stream_reader_t *reader) {
    return reader ? &reader->header : NULL;
}

const nmo_header1_t *nmo_stream_reader_get_header1(const nmo_stream_reader_t *reader) {
    return reader ? &reader->header1 : NULL;
}

const nmo_manager_data_t *nmo_stream_reader_get_managers(const nmo_stream_reader_t *reader,
                                                         uint32_t *out_count) {
    if (out_count != NULL) {
        *out_count = reader ? reader->manager_count : 0;
    }
    return reader ? reader->managers : NULL;
}

nmo_result_t nmo_stream_reader_read_next_object(nmo_stream_reader_t *reader,
                                                nmo_arena_t *arena,
                                                nmo_object_t **out_object) {
    if (reader == NULL || arena == NULL || out_object == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments"));
    }

    if (reader->next_object_index >= reader->objects_total) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_INFO,
                                          "No more objects available"));
    }

    uint32_t stored_id = 0;
    if (reader->header.file_version < 7) {
        nmo_result_t id_res = reader_read_u32(reader, &stored_id);
        if (id_res.code != NMO_OK) {
            return id_res;
        }
    }

    uint32_t chunk_size = 0;
    nmo_result_t size_res = reader_read_u32(reader, &chunk_size);
    if (size_res.code != NMO_OK) {
        return size_res;
    }

    nmo_chunk_t *chunk = NULL;
    if (chunk_size > 0) {
        void *buffer = nmo_arena_alloc(arena, chunk_size, 4);
        if (buffer == NULL) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to allocate chunk buffer"));
        }

        nmo_result_t copy_res = reader_copy_bytes(reader, buffer, chunk_size);
        if (copy_res.code != NMO_OK) {
            return copy_res;
        }

        chunk = nmo_chunk_create(arena);
        if (chunk == NULL) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to allocate chunk"));
        }

        nmo_result_t parse_res = nmo_chunk_parse(chunk, buffer, chunk_size);
        if (parse_res.code != NMO_OK) {
            return parse_res;
        }
    }

    const nmo_object_desc_t *desc = NULL;
    if (reader->header1.objects != NULL && reader->header1.object_count > reader->next_object_index) {
        desc = &reader->header1.objects[reader->next_object_index];
    }

    nmo_object_id_t runtime_id = desc ? desc->file_id : stored_id;
    nmo_class_id_t class_id = desc ? desc->class_id : (chunk ? chunk->class_id : 0);

    nmo_object_t *object = nmo_object_create(arena, runtime_id, class_id);
    if (object == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to allocate object"));
    }

    if (desc && desc->name) {
        nmo_object_set_name(object, desc->name, arena);
    }

    if (desc) {
        nmo_object_set_file_index(object, desc->file_index);
    }

    if (chunk != NULL) {
        nmo_object_set_chunk(object, chunk);
    }

    reader->next_object_index++;
    *out_object = object;
    return nmo_result_ok();
}

nmo_result_t nmo_stream_reader_skip_object(nmo_stream_reader_t *reader) {
    if (reader == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Reader is NULL"));
    }

    if (reader->next_object_index >= reader->objects_total) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_INFO,
                                          "No more objects to skip"));
    }

    if (reader->header.file_version < 7) {
        nmo_result_t id_res = reader_skip_bytes(reader, sizeof(uint32_t));
        if (id_res.code != NMO_OK) {
            return id_res;
        }
    }

    uint32_t chunk_size = 0;
    nmo_result_t size_res = reader_read_u32(reader, &chunk_size);
    if (size_res.code != NMO_OK) {
        return size_res;
    }

    if (chunk_size > 0) {
        nmo_result_t skip_res = reader_skip_bytes(reader, chunk_size);
        if (skip_res.code != NMO_OK) {
            return skip_res;
        }
    }

    reader->next_object_index++;
    return nmo_result_ok();
}

// =============================================================================
// Writer helpers
// =============================================================================

static nmo_result_t writer_finish_deflate(nmo_stream_writer_t *writer) {
    if (!writer->compress_data || !writer->zstream_initialized) {
        return nmo_result_ok();
    }

    int status;
    do {
        if (writer->zstream.avail_out == 0) {
            int io_res = nmo_io_write(writer->io, writer->out_buffer, writer->buffer_size);
            if (io_res != NMO_OK) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to flush compressed block"));
            }
            writer->data_compressed_bytes += writer->buffer_size;
            writer->zstream.next_out = writer->out_buffer;
            writer->zstream.avail_out = (unsigned int)writer->buffer_size;
        }

        status = mz_deflate(&writer->zstream, MZ_FINISH);

        size_t produced = writer->buffer_size - writer->zstream.avail_out;
        if (produced > 0) {
            int io_res = nmo_io_write(writer->io, writer->out_buffer, produced);
            if (io_res != NMO_OK) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to flush compressed data"));
            }
            writer->data_compressed_bytes += produced;
            writer->zstream.next_out = writer->out_buffer;
            writer->zstream.avail_out = (unsigned int)writer->buffer_size;
        }
    } while (status == MZ_OK);

    if (status != MZ_STREAM_END) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to finish deflate stream"));
    }

    return nmo_result_ok();
}

static nmo_result_t writer_write_bytes(nmo_stream_writer_t *writer,
                                       const void *data,
                                       size_t size) {
    writer->data_uncompressed_bytes += size;

    if (!writer->compress_data) {
        if (size == 0) {
            return nmo_result_ok();
        }
        int io_res = nmo_io_write(writer->io, data, size);
        if (io_res != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write data"));
        }
        writer->data_compressed_bytes += size;
        return nmo_result_ok();
    }

    if (!writer->zstream_initialized) {
        memset(&writer->zstream, 0, sizeof(writer->zstream));
        writer->zstream.next_out = writer->out_buffer;
        writer->zstream.avail_out = (unsigned int)writer->buffer_size;
        int status = mz_deflateInit(&writer->zstream, writer->compression_level);
        if (status != MZ_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to initialize deflate stream"));
        }
        writer->zstream_initialized = 1;
    }

    writer->zstream.next_in = (const unsigned char *)data;
    writer->zstream.avail_in = (unsigned int)size;

    while (writer->zstream.avail_in > 0) {
        if (writer->zstream.avail_out == 0) {
            int io_res = nmo_io_write(writer->io, writer->out_buffer, writer->buffer_size);
            if (io_res != NMO_OK) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to write compressed buffer"));
            }
            writer->data_compressed_bytes += writer->buffer_size;
            writer->zstream.next_out = writer->out_buffer;
            writer->zstream.avail_out = (unsigned int)writer->buffer_size;
        }

        int status = mz_deflate(&writer->zstream, MZ_NO_FLUSH);
        if (status != MZ_OK) {
            if (status == MZ_BUF_ERROR && writer->zstream.avail_out == 0) {
                continue;
            }
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                              NMO_SEVERITY_ERROR,
                                              "Deflate error"));
        }

        size_t produced = writer->buffer_size - writer->zstream.avail_out;
        if (produced > 0) {
            int io_res = nmo_io_write(writer->io, writer->out_buffer, produced);
            if (io_res != NMO_OK) {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                                  NMO_SEVERITY_ERROR,
                                                  "Failed to flush compressed bytes"));
            }
            writer->data_compressed_bytes += produced;
            writer->zstream.next_out = writer->out_buffer;
            writer->zstream.avail_out = (unsigned int)writer->buffer_size;
        }
    }

    return nmo_result_ok();
}

nmo_stream_writer_t *nmo_stream_writer_create(const char *path,
                                              const nmo_file_header_t *header,
                                              const nmo_stream_writer_options_t *options) {
    if (path == NULL || header == NULL) {
        return NULL;
    }

    nmo_stream_writer_t *writer = (nmo_stream_writer_t *)malloc(sizeof(nmo_stream_writer_t));
    if (writer == NULL) {
        return NULL;
    }
    memset(writer, 0, sizeof(*writer));

    memcpy(&writer->header, header, sizeof(nmo_file_header_t));
    writer->buffer_size = (options && options->buffer_size) ? options->buffer_size : STREAM_DEFAULT_BUFFER_SIZE;
    writer->compress_data = options ? options->compress_data : ((header->file_write_mode & NMO_FILE_WRITE_COMPRESS_DATA) != 0);
    writer->compression_level = options && options->compression_level ? options->compression_level : 6;

    if (writer->compress_data) {
        writer->header.file_write_mode |= NMO_FILE_WRITE_COMPRESS_DATA;
    } else {
        writer->header.file_write_mode &= ~NMO_FILE_WRITE_COMPRESS_DATA;
    }

    writer->header.data_pack_size = 0;
    writer->header.data_unpack_size = 0;

    writer->header.hdr1_pack_size = options ? (uint32_t)options->header1_size : header->hdr1_pack_size;
    writer->header.hdr1_unpack_size = options ? (uint32_t)options->header1_uncompressed_size : header->hdr1_unpack_size;

    writer->scratch_arena = nmo_arena_create(NULL, STREAM_DEFAULT_BUFFER_SIZE);
    writer->owns_arena = 1;
    if (writer->scratch_arena == NULL) {
        free(writer);
        return NULL;
    }

    writer->io = nmo_file_io_open(path, NMO_IO_WRITE | NMO_IO_CREATE);
    if (writer->io == NULL) {
        nmo_arena_destroy(writer->scratch_arena);
        free(writer);
        return NULL;
    }

    writer->out_buffer = (uint8_t *)(writer->compress_data ? malloc(writer->buffer_size) : NULL);
    if (writer->compress_data && writer->out_buffer == NULL) {
        nmo_stream_writer_destroy(writer);
        return NULL;
    }

    nmo_result_t header_result = nmo_file_header_serialize(&writer->header, writer->io);
    if (header_result.code != NMO_OK) {
        nmo_stream_writer_destroy(writer);
        return NULL;
    }

    if (options && options->header1_data && options->header1_size > 0) {
        int io_res = nmo_io_write(writer->io, options->header1_data, options->header1_size);
        if (io_res != NMO_OK) {
            nmo_stream_writer_destroy(writer);
            return NULL;
        }
    }

    writer->data_uncompressed_bytes = 0;
    writer->data_compressed_bytes = 0;
    writer->objects_written = 0;
    writer->finalized = 0;

    return writer;
}

nmo_result_t nmo_stream_writer_write_object(nmo_stream_writer_t *writer,
                                            const nmo_object_t *object) {
    if (writer == NULL || object == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid writer or object"));
    }

    if (writer->finalized) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR,
                                          "Writer already finalized"));
    }

    if (writer->header.object_count && writer->objects_written >= writer->header.object_count) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR,
                                          "Object count exceeds header declaration"));
    }

    const nmo_chunk_t *chunk = nmo_object_get_chunk(object);
    const void *chunk_data = NULL;
    size_t chunk_size = 0;

    if (chunk != NULL) {
        if (chunk->raw_data != NULL && chunk->raw_size > 0) {
            chunk_data = chunk->raw_data;
            chunk_size = chunk->raw_size;
        } else {
            nmo_result_t res = nmo_chunk_serialize(chunk, (void **)&chunk_data, &chunk_size, writer->scratch_arena);
            if (res.code != NMO_OK) {
                return res;
            }
        }
    }

    if (writer->header.file_version < 7) {
        uint32_t obj_id = nmo_object_get_file_index(object);
        uint8_t buffer[4];
        nmo_write_u32_le(buffer, obj_id);
        nmo_result_t res = writer_write_bytes(writer, buffer, sizeof(buffer));
        if (res.code != NMO_OK) {
            return res;
        }
    }

    uint8_t size_buffer[4];
    nmo_write_u32_le(size_buffer, (uint32_t)chunk_size);
    nmo_result_t res = writer_write_bytes(writer, size_buffer, sizeof(size_buffer));
    if (res.code != NMO_OK) {
        return res;
    }

    if (chunk_size > 0) {
        res = writer_write_bytes(writer, chunk_data, chunk_size);
        if (res.code != NMO_OK) {
            return res;
        }
    }

    if (chunk && chunk->raw_data == NULL) {
        nmo_arena_reset(writer->scratch_arena);
    }

    writer->objects_written++;
    return nmo_result_ok();
}

nmo_result_t nmo_stream_writer_finalize(nmo_stream_writer_t *writer) {
    if (writer == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Writer is NULL"));
    }

    if (writer->finalized) {
        return nmo_result_ok();
    }

    if (writer->compress_data && writer->zstream_initialized) {
        nmo_result_t flush_res = writer_finish_deflate(writer);
        if (flush_res.code != NMO_OK) {
            return flush_res;
        }
        mz_deflateEnd(&writer->zstream);
        writer->zstream_initialized = 0;
    }

    writer->header.data_pack_size = (uint32_t)writer->data_compressed_bytes;
    writer->header.data_unpack_size = (uint32_t)writer->data_uncompressed_bytes;

    nmo_io_seek(writer->io, 0, NMO_SEEK_SET);
    nmo_result_t header_res = nmo_file_header_serialize(&writer->header, writer->io);
    if (header_res.code != NMO_OK) {
        return header_res;
    }

    writer->finalized = 1;
    return nmo_result_ok();
}

void nmo_stream_writer_destroy(nmo_stream_writer_t *writer) {
    if (writer == NULL) {
        return;
    }

    nmo_stream_writer_finalize(writer);

    if (writer->zstream_initialized) {
        mz_deflateEnd(&writer->zstream);
    }

    if (writer->io != NULL) {
        nmo_io_close(writer->io);
    }

    if (writer->compress_data && writer->out_buffer != NULL) {
        free(writer->out_buffer);
    }

    if (writer->owns_arena && writer->scratch_arena != NULL) {
        nmo_arena_destroy(writer->scratch_arena);
    }

    free(writer);
}
