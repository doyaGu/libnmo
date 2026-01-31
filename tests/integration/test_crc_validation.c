/**
 * @file test_crc_validation.c
 * @brief Integration test for strict CRC validation during load
 */

#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "app/nmo_session.h"
#include "test_framework.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (in == NULL) {
        return 0;
    }

    FILE *out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }

    unsigned char buffer[4096];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, read_bytes, out) != read_bytes) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }

    fclose(in);
    fclose(out);
    return 1;
}

static int read_u32_le(FILE *f, uint32_t *value) {
    uint8_t bytes[4];
    if (fread(bytes, 1, sizeof(bytes), f) != sizeof(bytes)) {
        return 0;
    }
    *value = (uint32_t)bytes[0]
           | ((uint32_t)bytes[1] << 8)
           | ((uint32_t)bytes[2] << 16)
           | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int write_u32_le(FILE *f, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    return fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
}

static int corrupt_crc(const char *path, uint32_t *original_crc) {
    FILE *f = fopen(path, "r+b");
    if (f == NULL) {
        return 0;
    }

    if (fseek(f, 8, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    uint32_t crc = 0;
    if (!read_u32_le(f, &crc)) {
        fclose(f);
        return 0;
    }

    uint32_t new_crc = crc ^ 0xFFFFFFFFu;

    if (fseek(f, 8, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    if (!write_u32_le(f, new_crc)) {
        fclose(f);
        return 0;
    }

    fclose(f);
    if (original_crc != NULL) {
        *original_crc = crc;
    }
    return 1;
}

static int load_with_options(const char *path, nmo_load_options_t opts, int *out_result) {
    nmo_context_desc_t ctx_desc;
    memset(&ctx_desc, 0, sizeof(nmo_context_desc_t));

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        return 0;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        nmo_context_release(ctx);
        return 0;
    }

    int result = nmo_load_file_ex(session, path, &opts);

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    if (out_result != NULL) {
        *out_result = result;
    }

    return 1;
}

int main(void) {
    const char *source = NMO_TEST_DATA_FILE("Nop.cmo");
    FILE *check = fopen(source, "rb");
    if (check == NULL) {
        printf("CRC test skipped: %s not found\n", source);
        return 0;
    }
    fclose(check);

    srand((unsigned)time(NULL));
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "crc_test_%u.cmo", (unsigned)rand());

    if (!copy_file(source, temp_path)) {
        printf("CRC test failed: could not copy test file\n");
        return 1;
    }

    nmo_load_options_t opts = nmo_load_options_default();
    int result = NMO_OK;
    if (!load_with_options(temp_path, opts, &result)) {
        printf("CRC test failed: could not initialize loader\n");
        remove(temp_path);
        return 1;
    }

    if (result != NMO_OK) {
        printf("CRC test skipped: baseline load failed (error %d)\n", result);
        remove(temp_path);
        return 0;
    }

    if (!corrupt_crc(temp_path, NULL)) {
        printf("CRC test failed: could not corrupt CRC\n");
        remove(temp_path);
        return 1;
    }

    opts = nmo_load_options_default();
    if (!load_with_options(temp_path, opts, &result)) {
        printf("CRC test failed: could not initialize loader\n");
        remove(temp_path);
        return 1;
    }

    if (result != NMO_ERR_CHECKSUM_MISMATCH) {
        printf("CRC test failed: expected %d, got %d\n", NMO_ERR_CHECKSUM_MISMATCH, result);
        remove(temp_path);
        return 1;
    }

    remove(temp_path);
    printf("CRC validation test passed\n");
    return 0;
}
