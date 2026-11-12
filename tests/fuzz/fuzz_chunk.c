/**
 * @file fuzz_chunk.c
 * @brief Fuzz the chunk decoders on their own: the file-format chunk buffer
 *        (nmo_chunk_deserialize), the old VERSION1 layout (nmo_chunk_parse) and
 *        the behavior interface chunk read from either.
 */

#include "fuzz_common.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_interface_chunk.h"

#include <stdlib.h>
#include <string.h>

static void parse_interface(nmo_chunk_t *chunk, nmo_arena_t *arena) {
    nmo_interface_data_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    (void)nmo_interface_chunk_parse(chunk, arena, NULL, &parsed);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* The decoders read dwords; give them the 4-byte aligned buffer a file
     * loader hands them. */
    uint32_t *aligned = (uint32_t *)malloc(size / 4u * 4u + 4u);
    if (aligned == NULL) {
        return 0;
    }
    if (size > 0) {
        memcpy(aligned, data, size);
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        fuzz_fail("cannot create an arena", NMO_ERR_NOMEM);
    }

    nmo_chunk_t *chunk = NULL;
    if (nmo_chunk_deserialize(aligned, size, arena, &chunk) == NMO_OK && chunk != NULL) {
        void *out = NULL;
        size_t out_size = 0;
        (void)nmo_chunk_serialize(chunk, &out, &out_size, arena);
        parse_interface(chunk, arena);
        nmo_chunk_destroy(chunk);
    }

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    if (legacy == NULL) {
        fuzz_fail("cannot create a chunk", NMO_ERR_NOMEM);
    }
    if (nmo_chunk_parse(legacy, aligned, size) == NMO_OK) {
        void *out = NULL;
        size_t out_size = 0;
        (void)nmo_chunk_serialize_version1(legacy, &out, &out_size, arena);
        parse_interface(legacy, arena);
    }
    nmo_chunk_destroy(legacy);

    nmo_arena_destroy(arena);
    free(aligned);
    return 0;
}
