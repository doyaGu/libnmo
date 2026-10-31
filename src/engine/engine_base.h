/**
 * @file engine_base.h
 * @brief The containers the engine's state is made of: growing arrays and
 *        object ID maps.
 */

#ifndef NMO_ENGINE_BASE_H
#define NMO_ENGINE_BASE_H

#include "engine/nmo_engine.h"
#include "engine/nmo_engine_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Every index the engine keeps is a position in the engine array of that kind; -1 is none. */

#define ENG_VEC(type) struct { type *data; size_t count; size_t capacity; }

bool eng_vec_reserve(void **data, size_t *capacity, size_t needed, size_t item_size);
#define ENG_VEC_PUSH(vec, out_item)                                                         \
    (eng_vec_reserve((void **)&(vec).data, &(vec).capacity, (vec).count + 1u,               \
                     sizeof(*(vec).data))                                                   \
         ? ((out_item) = &(vec).data[(vec).count++], memset((out_item), 0, sizeof(*(out_item))), true) \
         : false)

typedef struct eng_map_slot {
    nmo_object_id_t key;
    int32_t value;
} eng_map_slot_t;

/* Object ID -> index, open addressing */
typedef struct eng_map {
    eng_map_slot_t *slots;
    size_t capacity;
    size_t count;
} eng_map_t;

int32_t eng_map_get(const eng_map_t *map, nmo_object_id_t key);
bool eng_map_put(eng_map_t *map, nmo_object_id_t key, int32_t value);

/* A new heap string from a printf format, sized exactly; NULL when out of memory */
char *eng_text_printf(const char *format, ...);

/* The first `length` bytes of `text` in a new heap string; NULL when out of memory */
static inline char *eng_text_copy(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

#endif /* NMO_ENGINE_BASE_H */
