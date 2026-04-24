/**
 * @file id_sanitizer.c
 * @brief ID sanitization pipeline implementation (Phase 1.1)
 */

#include "session/nmo_session_pipeline.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "format/nmo_object.h"
#include <stddef.h>
#include <stdalign.h>

struct nmo_id_sanitizer {
    nmo_hash_table_t *file_to_runtime;   /* file ID (CK_ID) -> runtime_id */
    nmo_hash_table_t *runtime_to_file;   /* runtime_id -> file ID */
    nmo_hash_table_t *negative_refs;     /* runtime_id -> original negative id */
    nmo_arena_t *arena;
};

static nmo_hash_table_t *nmo_id_sanitizer_make_table(size_t key_size, size_t value_size) {
    return nmo_hash_table_create(
        NULL,
        key_size,
        value_size,
        64,               /* initial capacity */
        nmo_hash_uint32,  /* works for both uint32 and int32 keys */
        NULL);
}

nmo_id_sanitizer_t *nmo_id_sanitizer_create(nmo_arena_t *arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_id_sanitizer_t *s = (nmo_id_sanitizer_t *) nmo_arena_alloc(
        arena, sizeof(nmo_id_sanitizer_t), alignof(nmo_id_sanitizer_t));
    if (s == NULL) {
        return NULL;
    }

    s->arena = arena;
    s->file_to_runtime = nmo_id_sanitizer_make_table(sizeof(uint32_t), sizeof(uint32_t));
    s->runtime_to_file = nmo_id_sanitizer_make_table(sizeof(uint32_t), sizeof(uint32_t));
    s->negative_refs = nmo_id_sanitizer_make_table(sizeof(uint32_t), sizeof(int32_t));

    if (!s->file_to_runtime || !s->runtime_to_file || !s->negative_refs) {
        nmo_id_sanitizer_destroy(s);
        return NULL;
    }

    return s;
}

void nmo_id_sanitizer_destroy(nmo_id_sanitizer_t *sanitizer) {
    if (sanitizer == NULL) {
        return;
    }

    if (sanitizer->file_to_runtime) {
        nmo_hash_table_destroy(sanitizer->file_to_runtime);
        sanitizer->file_to_runtime = NULL;
    }
    if (sanitizer->runtime_to_file) {
        nmo_hash_table_destroy(sanitizer->runtime_to_file);
        sanitizer->runtime_to_file = NULL;
    }
    if (sanitizer->negative_refs) {
        nmo_hash_table_destroy(sanitizer->negative_refs);
        sanitizer->negative_refs = NULL;
    }
}

void nmo_id_sanitizer_reset(nmo_id_sanitizer_t *sanitizer) {
    if (sanitizer == NULL) {
        return;
    }

    if (sanitizer->file_to_runtime) {
        nmo_hash_table_clear(sanitizer->file_to_runtime);
    }
    if (sanitizer->runtime_to_file) {
        nmo_hash_table_clear(sanitizer->runtime_to_file);
    }
    if (sanitizer->negative_refs) {
        nmo_hash_table_clear(sanitizer->negative_refs);
    }
}

uint32_t nmo_id_sanitize(uint32_t raw_id) {
    return raw_id & ~NMO_OBJECT_REFERENCE_FLAG;
}

nmo_status_t nmo_id_sanitizer_register(nmo_id_sanitizer_t *sanitizer,
                                       uint32_t file_id,
                                       uint32_t runtime_id) {
    if (sanitizer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t clean_file = nmo_id_sanitize(file_id);
    uint32_t clean_runtime = nmo_id_sanitize(runtime_id);

    nmo_status_t result = nmo_hash_table_insert(sanitizer->file_to_runtime, &clean_file, &clean_runtime);
    if (result != NMO_OK) {
        return result;
    }

    result = nmo_hash_table_insert(sanitizer->runtime_to_file, &clean_runtime, &clean_file);
    if (result != NMO_OK) {
        /* Roll back the first insert to keep tables consistent */
        nmo_hash_table_remove(sanitizer->file_to_runtime, &clean_file);
        return result;
    }

    return NMO_OK;
}

int32_t nmo_id_register_external(nmo_id_sanitizer_t *sanitizer, int32_t negative_id) {
    if (sanitizer == NULL) {
        return (int32_t) NMO_OBJECT_ID_INVALID;
    }

    if (negative_id == 0) {
        return (int32_t) NMO_OBJECT_ID_INVALID;
    }

    /* Positive IDs can be returned directly after mask stripping */
    if (negative_id > 0) {
        return (int32_t) nmo_id_sanitize((uint32_t) negative_id);
    }

    uint32_t runtime_id = (uint32_t) (-negative_id);
    nmo_status_t result = nmo_hash_table_insert(sanitizer->negative_refs, &runtime_id, &negative_id);
    if (result != NMO_OK) {
        return (int32_t) NMO_OBJECT_ID_INVALID;
    }

    return (int32_t) runtime_id;
}

nmo_status_t nmo_id_sanitizer_reseed(nmo_id_sanitizer_t *sanitizer,
                                     const uint32_t *file_ids,
                                     const uint32_t *runtime_ids,
                                     size_t count) {
    if (sanitizer == NULL || (count > 0 && (file_ids == NULL || runtime_ids == NULL))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_id_sanitizer_reset(sanitizer);

    for (size_t i = 0; i < count; i++) {
        int r = nmo_id_sanitizer_register(sanitizer, file_ids[i], runtime_ids[i]);
        if (r != NMO_OK) {
            return r;
        }
    }

    return NMO_OK;
}

uint32_t nmo_id_file_to_runtime(const nmo_id_sanitizer_t *sanitizer, uint32_t file_id) {
    if (sanitizer == NULL || sanitizer->file_to_runtime == NULL) {
        return NMO_OBJECT_ID_INVALID;
    }

    uint32_t runtime_id = 0;
    uint32_t key = nmo_id_sanitize(file_id);
    if (nmo_hash_table_get(sanitizer->file_to_runtime, &key, &runtime_id) == NMO_OK) {
        return runtime_id;
    }

    return NMO_OBJECT_ID_INVALID;
}

uint32_t nmo_id_runtime_to_file(const nmo_id_sanitizer_t *sanitizer, uint32_t runtime_id) {
    if (sanitizer == NULL || sanitizer->runtime_to_file == NULL) {
        return NMO_OBJECT_ID_INVALID;
    }

    uint32_t file_id = 0;
    uint32_t key = nmo_id_sanitize(runtime_id);
    if (nmo_hash_table_get(sanitizer->runtime_to_file, &key, &file_id) == NMO_OK) {
        return file_id;
    }

    return NMO_OBJECT_ID_INVALID;
}

int32_t nmo_id_original_external(const nmo_id_sanitizer_t *sanitizer, uint32_t runtime_id) {
    if (sanitizer == NULL || sanitizer->negative_refs == NULL) {
        return 0;
    }

    int32_t original = 0;
    uint32_t key = nmo_id_sanitize(runtime_id);
    if (nmo_hash_table_get(sanitizer->negative_refs, &key, &original) == NMO_OK) {
        return original;
    }

    return 0;
}
