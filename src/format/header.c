/**
 * @file header.c
 * @brief NMO file header parsing implementation
 */

#include "format/nmo_header.h"
#include <stdlib.h>
#include <string.h>

/**
 * Internal header structure
 */
struct nmo_header {
    nmo_file_header_t data;
};

/**
 * Create header context
 */
nmo_header_t *nmo_header_create(void) {
    nmo_header_t *header = (nmo_header_t *)malloc(sizeof(nmo_header_t));
    if (header == NULL) {
        return NULL;
    }

    /* Initialize with default values for a minimal valid header */
    memset(&header->data, 0, sizeof(nmo_file_header_t));
    memcpy(header->data.signature, "Nemo Fi\0", 8);
    header->data.file_version = 8;  /* Current version */
    header->data.ck_version = 0x13022002;  /* Default CK version */

    return header;
}

/**
 * Destroy header context
 */
void nmo_header_destroy(nmo_header_t *header) {
    if (header != NULL) {
        free(header);
    }
}

/**
 * Parse header from IO
 */
nmo_result_t nmo_header_parse(nmo_header_t *header, void *io) {
    if (header == NULL || io == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Header and IO cannot be NULL"));
    }

    return nmo_file_header_parse((nmo_io_interface_t *)io, &header->data);
}

/**
 * Write header to IO
 */
nmo_result_t nmo_header_write(const nmo_header_t *header, void *io) {
    if (header == NULL || io == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Header and IO cannot be NULL"));
    }

    return nmo_file_header_serialize(&header->data, (nmo_io_interface_t *)io);
}

/**
 * Get header size
 */
uint32_t nmo_header_get_size(const nmo_header_t *header) {
    if (header == NULL) {
        return 0;
    }

    /* Part0 is always 32 bytes, Part1 is 32 bytes if file_version >= 5 */
    return (header->data.file_version >= 5) ? 64 : 32;
}

/**
 * Validate header
 */
nmo_result_t nmo_header_validate(const nmo_header_t *header) {
    if (header == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Header cannot be NULL"));
    }

    return nmo_file_header_validate(&header->data);
}

/**
 * Parse Virtools file header from IO
 */
nmo_result_t nmo_file_header_parse(nmo_io_interface_t *io, nmo_file_header_t *header) {
    int result;

    /* Validate arguments */
    if (io == NULL || header == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "IO interface and header cannot be NULL"));
    }

    /* Initialize header to zero */
    memset(header, 0, sizeof(nmo_file_header_t));

    /* Read Part0 - signature (8 bytes) */
    result = nmo_io_read_exact(io, header->signature, 8);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read file signature"));
    }

    /* Validate signature immediately */
    if (memcmp(header->signature, "Nemo Fi\0", 8) != 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_SIGNATURE,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid file signature"));
    }

    /* Read Part0 - remaining fields (24 bytes) */
    result = nmo_io_read_u32(io, &header->crc);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read CRC"));
    }

    result = nmo_io_read_u32(io, &header->ck_version);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read CK version"));
    }

    result = nmo_io_read_u32(io, &header->file_version);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read file version"));
    }

    /* Validate file version */
    if (header->file_version < 2 || header->file_version > 9) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_UNSUPPORTED_VERSION,
                                          NMO_SEVERITY_ERROR,
                                          "Unsupported file version (must be 2-9)"));
    }

    result = nmo_io_read_u32(io, &header->file_version2);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read file version2"));
    }

    result = nmo_io_read_u32(io, &header->file_write_mode);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read file write mode"));
    }

    result = nmo_io_read_u32(io, &header->hdr1_pack_size);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read header1 packed size"));
    }

    /* Read Part1 if file_version >= 5 */
    if (header->file_version >= 5) {
        result = nmo_io_read_u32(io, &header->data_pack_size);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read data packed size"));
        }

        result = nmo_io_read_u32(io, &header->data_unpack_size);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read data unpacked size"));
        }

        result = nmo_io_read_u32(io, &header->manager_count);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read manager count"));
        }

        result = nmo_io_read_u32(io, &header->object_count);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read object count"));
        }

        result = nmo_io_read_u32(io, &header->max_id_saved);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read max ID saved"));
        }

        result = nmo_io_read_u32(io, &header->product_version);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read product version"));
        }

        result = nmo_io_read_u32(io, &header->product_build);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read product build"));
        }

        result = nmo_io_read_u32(io, &header->hdr1_unpack_size);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_EOF,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to read header1 unpacked size"));
        }
    }

    return nmo_result_ok();
}

/**
 * Validate Virtools file header
 */
nmo_result_t nmo_file_header_validate(const nmo_file_header_t *header) {
    /* Validate argument */
    if (header == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Header cannot be NULL"));
    }

    /* Validate signature */
    if (memcmp(header->signature, "Nemo Fi\0", 8) != 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_SIGNATURE,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid file signature"));
    }

    /* Validate file version */
    if (header->file_version < 2 || header->file_version > 9) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_UNSUPPORTED_VERSION,
                                          NMO_SEVERITY_ERROR,
                                          "Unsupported file version (must be 2-9)"));
    }

    return nmo_result_ok();
}

/**
 * Serialize Virtools file header to IO
 */
nmo_result_t nmo_file_header_serialize(const nmo_file_header_t *header, nmo_io_interface_t *io) {
    int result;

    /* Validate arguments */
    if (header == NULL || io == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Header and IO interface cannot be NULL"));
    }

    /* Write Part0 - signature (8 bytes) */
    result = nmo_io_write(io, header->signature, 8);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write file signature"));
    }

    /* Write Part0 - remaining fields (24 bytes) */
    result = nmo_io_write_u32(io, header->crc);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write CRC"));
    }

    result = nmo_io_write_u32(io, header->ck_version);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write CK version"));
    }

    result = nmo_io_write_u32(io, header->file_version);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write file version"));
    }

    result = nmo_io_write_u32(io, header->file_version2);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write file version2"));
    }

    result = nmo_io_write_u32(io, header->file_write_mode);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write file write mode"));
    }

    result = nmo_io_write_u32(io, header->hdr1_pack_size);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to write header1 packed size"));
    }

    /* Write Part1 if file_version >= 5 */
    if (header->file_version >= 5) {
        result = nmo_io_write_u32(io, header->data_pack_size);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write data packed size"));
        }

        result = nmo_io_write_u32(io, header->data_unpack_size);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write data unpacked size"));
        }

        result = nmo_io_write_u32(io, header->manager_count);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write manager count"));
        }

        result = nmo_io_write_u32(io, header->object_count);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write object count"));
        }

        result = nmo_io_write_u32(io, header->max_id_saved);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write max ID saved"));
        }

        result = nmo_io_write_u32(io, header->product_version);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write product version"));
        }

        result = nmo_io_write_u32(io, header->product_build);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write product build"));
        }

        result = nmo_io_write_u32(io, header->hdr1_unpack_size);
        if (result != NMO_OK) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CANT_WRITE_FILE,
                                              NMO_SEVERITY_ERROR,
                                              "Failed to write header1 unpacked size"));
        }
    }

    return nmo_result_ok();
}
