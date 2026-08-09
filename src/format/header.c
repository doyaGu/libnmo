/**
 * @file header.c
 * @brief NMO file header parsing implementation
 */

#include "format/nmo_header.h"
#include "core/nmo_allocator.h"
#include <miniz.h>
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
    nmo_allocator_t alloc = nmo_allocator_default();
    
    nmo_header_t *header = (nmo_header_t *)nmo_alloc(&alloc, sizeof(nmo_header_t), _Alignof(nmo_header_t));
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
        nmo_allocator_t alloc = nmo_allocator_default();
        nmo_free(&alloc, header);
    }
}

/**
 * Parse header from IO
 */
nmo_status_t nmo_header_parse(nmo_header_t *header, void *io) {
    if (header == NULL || io == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Header and IO cannot be NULL");
    }

    return nmo_file_header_parse((nmo_io_interface_t *)io, &header->data);
}

/**
 * Write header to IO
 */
nmo_status_t nmo_header_write(const nmo_header_t *header, void *io) {
    if (header == NULL || io == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Header and IO cannot be NULL");
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
nmo_status_t nmo_header_validate(const nmo_header_t *header) {
    if (header == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Header cannot be NULL");
    }

    return nmo_file_header_validate(&header->data);
}

/**
 * Parse Virtools file header from IO
 */
static nmo_status_t file_header_read_status(nmo_status_t status) {
    return status == NMO_ERR_EOF ? NMO_ERR_TRUNCATED_CHUNK : status;
}

nmo_status_t nmo_file_header_parse(nmo_io_interface_t *io, nmo_file_header_t *header) {
    /* Validate arguments */
    if (io == NULL || header == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "IO interface and header cannot be NULL");
    }

    nmo_file_header_t staged;
    memset(&staged, 0, sizeof(staged));

    /* Read Part0 - signature (8 bytes) */
    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_exact(io, staged.signature, 8)),
        "Failed to read file signature");

    /* Validate signature immediately */
    if (memcmp(staged.signature, "Nemo Fi\0", 8) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_SIGNATURE, NMO_SEVERITY_ERROR, "Invalid file signature");
    }

    /* Read Part0 - remaining fields (24 bytes) */
    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_u32(io, &staged.crc)),
        "Failed to read CRC");
    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_u32(io, &staged.ck_version)),
        "Failed to read CK version");
    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_u32(io, &staged.file_version)),
        "Failed to read file version");

    /* Validate file version */
    if (staged.file_version < 2 || staged.file_version > 9) {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR, "Unsupported file version (must be 2-9)");
    }

    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_u32(io, &staged.file_version2)),
        "Failed to read file version2");

    /* CK2 treats non-zero FileVersion2 as an incompatible/legacy header */
    if (staged.file_version2 != 0) {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR, "Unsupported legacy file header (FileVersion2 != 0)");
    }

    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_u32(io, &staged.file_write_mode)),
        "Failed to read file write mode");
    NMO_RETURN_IF_ERROR_CTX(
        file_header_read_status(nmo_io_read_u32(io, &staged.hdr1_pack_size)),
        "Failed to read header1 packed size");

    /* Read Part1 if file_version >= 5 */
    if (staged.file_version >= 5) {
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.data_pack_size)),
            "Failed to read data packed size");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.data_unpack_size)),
            "Failed to read data unpacked size");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.manager_count)),
            "Failed to read manager count");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.object_count)),
            "Failed to read object count");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.max_id_saved)),
            "Failed to read max ID saved");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.product_version)),
            "Failed to read product version");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.product_build)),
            "Failed to read product build");
        NMO_RETURN_IF_ERROR_CTX(
            file_header_read_status(nmo_io_read_u32(io, &staged.hdr1_unpack_size)),
            "Failed to read header1 unpacked size");
    }

    *header = staged;
    NMO_RETURN_OK();
}

/**
 * Validate Virtools file header
 */
nmo_status_t nmo_file_header_validate(const nmo_file_header_t *header) {
    /* Validate argument */
    if (header == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Header cannot be NULL");
    }

    /* Validate signature */
    if (memcmp(header->signature, "Nemo Fi\0", 8) != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_SIGNATURE, NMO_SEVERITY_ERROR, "Invalid file signature");
    }

    /* Validate file version */
    if (header->file_version < 2 || header->file_version > 9) {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR, "Unsupported file version (must be 2-9)");
    }
    if (header->file_version2 != 0) {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                         "Unsupported legacy file header (FileVersion2 != 0)");
    }

    NMO_RETURN_OK();
}

uint32_t nmo_file_header_compute_crc(const nmo_file_header_t *header,
                                     const uint8_t *header1_packed,
                                     uint32_t header1_pack_size,
                                     const uint8_t *data_packed,
                                     uint32_t data_pack_size) {
    if (header == NULL) {
        return 0;
    }
    if ((header1_pack_size > 0 && header1_packed == NULL) ||
        (data_pack_size > 0 && data_packed == NULL)) {
        return 0;
    }

    nmo_file_header_t crc_header = *header;
    crc_header.crc = 0;

    uint32_t crc = 0;
    crc = (uint32_t)mz_adler32(crc, (const uint8_t *)&crc_header, 32);
    if (crc_header.file_version >= 5) {
        crc = (uint32_t)mz_adler32(crc, (const uint8_t *)&crc_header.data_pack_size, 32);
    }
    if (header1_pack_size > 0) {
        crc = (uint32_t)mz_adler32(crc, header1_packed, header1_pack_size);
    }
    if (data_pack_size > 0) {
        crc = (uint32_t)mz_adler32(crc, data_packed, data_pack_size);
    }
    return crc;
}

/**
 * Serialize Virtools file header to IO
 */
nmo_status_t nmo_file_header_serialize(const nmo_file_header_t *header, nmo_io_interface_t *io) {
    /* Validate arguments */
    if (header == NULL || io == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Header and IO interface cannot be NULL");
    }
    NMO_RETURN_IF_ERROR(nmo_file_header_validate(header));

    /* Write Part0 - signature (8 bytes) */
    NMO_RETURN_IF_ERROR_CTX(
        nmo_io_write(io, header->signature, 8),
        "Failed to write file signature");

    /* Write Part0 - remaining fields (24 bytes) */
    NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->crc), "Failed to write CRC");
    NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->ck_version), "Failed to write CK version");
    NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->file_version), "Failed to write file version");
    NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->file_version2), "Failed to write file version2");
    NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->file_write_mode), "Failed to write file write mode");
    NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->hdr1_pack_size), "Failed to write header1 packed size");

    /* Write Part1 if file_version >= 5 */
    if (header->file_version >= 5) {
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->data_pack_size), "Failed to write data packed size");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->data_unpack_size), "Failed to write data unpacked size");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->manager_count), "Failed to write manager count");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->object_count), "Failed to write object count");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->max_id_saved), "Failed to write max ID saved");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->product_version), "Failed to write product version");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->product_build), "Failed to write product build");
        NMO_RETURN_IF_ERROR_CTX(nmo_io_write_u32(io, header->hdr1_unpack_size), "Failed to write header1 unpacked size");
    }

    NMO_RETURN_OK();
}
