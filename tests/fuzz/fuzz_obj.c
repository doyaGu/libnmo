/**
 * @file fuzz_obj.c
 * @brief Fuzz the Wavefront OBJ reader behind `nmo mesh import`.
 */

#include "fuzz_common.h"

#include "core/nmo_arena.h"
#include "format/nmo_obj_parser.h"

#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        fuzz_fail("cannot create an arena", NMO_ERR_NOMEM);
    }
    nmo_obj_data_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    (void)nmo_obj_parse(arena, (const char *)data, size, &parsed);
    nmo_arena_destroy(arena);
    return 0;
}
